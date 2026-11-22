/*
 * Procesos/gerber.h
 *
 *  Created on: 13 abr 2026
 *      Author: DjSteker
 */

/**
 * @file    gerber.h
 * @brief   Parser de Gerber RS-274X y generador de GCode para PCB.
 *
 * @details Librería central del proyecto: define la geometría del
 *          PCB, el parser del formato Gerber RS-274X (extensión
 *          @c .gbr) y el generador de GCode que traduce esa geometría
 *          a código CNC.
 *
 * @par     Proceso general
 *          1. @c GerberParser lee el archivo y produce un
 *             @c GerberResult: una lista de @c Primitive
 *             (segmentos, arcos, flashes y regiones) con su
 *             polaridad (Dark = cobre, Clear = hueco).
 *          2. La interfaz dibuja esas primitivas; @c Mecanizado y
 *             @c ToolpathGenerator calculan el recorrido CNC.
 *          3. @c GCodeGenerator convierte el resultado en GCode
 *             (modo contorno o modo aislamiento).
 *
 * @par     Quién lo utiliza
 *          - @c Nucleo/interfaz.h / .cpp: carga el archivo, guarda
 *            @c EstadoApp::resultado y dibuja las primitivas.
 *          - @c Mecanizado/mecanizado.h / .cpp: trayectorias CNC y
 *            GCode de taladro (usan @c geom::offsetPolygon).
 *          - @c Procesos/ToolpathGenerator.h: generación de
 *            trayectorias.
 *          - @c Procesos/verificacion_segura.h: validación de
 *            colisiones contra las primitivas.
 *          - @c Interfaces/ui_gcode.h / .cpp: exportación de GCode.
 *
 * @note    Unidades: @c GerberResult::unit indica "MM" o "IN"; el
 *          resto del proyecto trabaja en las unidades del archivo.
 * @note    Sin dependencias externas: solo STL de C++17.
 *
 * @author  DjSteker
 * @date    13 abr 2026
 */

#ifndef PROCESOS_GERBER_H_
#define PROCESOS_GERBER_H_

#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace gerber {

// ─────────────────────────────────────────────────────────────────────────────
//  Tipos geométricos básicos
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @struct Vec2
 * @brief  Punto o vector 2D con coordenadas @c x e @c y.
 *
 * @details Incluye aritmética básica (suma, resta, producto por
 *          escalar) y utilidades de longitud/normalización. Es el
 *          tipo de coordenadas usado en todo el proyecto.
 */
struct Vec2 {
	double x = 0.0;  ///< @brief Coordenada horizontal.
	double y = 0.0;  ///< @brief Coordenada vertical.

	Vec2() = default;

	/**
	 * @brief Construye el punto (x, y).
	 * @param x Coordenada horizontal.
	 * @param y Coordenada vertical.
	 */
	Vec2(double x, double y) :
			x(x), y(y) {
	}

	/**
	 * @brief Suma de vectores (traslación).
	 * @param o Vector a sumar.
	 * @return Vector resultante.
	 */
	Vec2 operator+(const Vec2 &o) const {
		return {x + o.x, y + o.y};
	}

	/**
	 * @brief Resta de vectores (vector de @c o a @c this).
	 * @param o Vector a restar.
	 * @return Vector resultante.
	 */
	Vec2 operator-(const Vec2 &o) const {
		return {x - o.x, y - o.y};
	}

	/**
	 * @brief Producto por escalar (escalado).
	 * @param s Escalar.
	 * @return Vector escalado.
	 */
	Vec2 operator*(double s) const {
		return {x * s, y * s};
	}

	/**
	 * @brief Módulo (longitud euclídea) del vector.
	 * @return Distancia al origen.
	 */
	double length() const {
		return std::sqrt(x * x + y * y);
	}

	/**
	 * @brief Vector unitario en la misma dirección.
	 * @return El vector normalizado, o (0, 0) si la longitud es 0.
	 */
	Vec2 normalized() const {
		double l = length();
		return l > 0 ? Vec2 { x / l, y / l } : Vec2 { 0, 0 };
	}
};

/**
 * @struct BBox
 * @brief  Caja envolvente alineada a los ejes (bounding box).
 *
 * @details Acumula los extremos mínimos y máximos de un conjunto de
 *          puntos. Antes de expandir, los límites están "vacíos"
 *          (±1e18) y @ref valid es @c false.
 */
struct BBox {
	double minX = 1e18, minY = 1e18;   ///< @brief Extremo inferior (empieza en +1e18).
	double maxX = -1e18, maxY = -1e18;  ///< @brief Extremo superior (empieza en -1e18).

	/// @brief @c true una vez se ha expandido con al menos un punto.
	bool valid = false;

	/**
	 * @brief   Amplía la caja para incluir el punto (x, y).
	 * @param   x Coordenada horizontal del punto.
	 * @param   y Coordenada vertical del punto.
	 * @note    La primera llamada marca la caja como válida.
	 */
	void expand(double x, double y) {
		if (x < minX) {
			minX = x;}
		if (x > maxX) {
			maxX = x;}
		if (y < minY) {
			minY = y;}
		if (y > maxY) {
			maxY = y;}
		valid = true;
	}

	/**
	 * @brief   Amplía la caja para incluir un punto.
	 * @param   v Punto a incluir.
	 */
	void expand(const Vec2 &v) {
		expand(v.x, v.y);
	}

	/**
	 * @brief Ancho de la caja (maxX - minX).
	 * @return Valor en unidades del archivo Gerber.
	 */
	double width() const {
		return maxX - minX;
	}

	/**
	 * @brief Alto de la caja (maxY - minY).
	 * @return Valor en unidades del archivo Gerber.
	 */
	double height() const {
		return maxY - minY;
	}

	/**
	 * @brief Centro geométrico de la caja.
	 * @return Punto medio entre los extremos.
	 */
	Vec2 center() const {
		return {(minX + maxX) * 0.5, (minY + maxY) * 0.5};
	}
};

// ─────────────────────────────────────────────────────────────────────────────
//  Apertures (formas de la herramienta de dibujo)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @enum   ApertureType
 * @brief  Forma de una apertura (aperture) del archivo Gerber.
 */
enum class ApertureType {
	Circle,     ///< @brief Círculo (diámetro = outer).
	Rectangle,  ///< @brief Rectángulo (outer = ancho, height = alto).
	Oblong,     ///< @brief Ranura (rectángulo con extremos redondeados).
	Polygon,     ///< @brief Polígono regular (sides lados, rotación).
	Macro,       ///< @brief Definida por macro AM (composición de primitivas).
	RoundRect    ///< @brief Rectángulo con esquinas redondeadas (cornerRadius).
};

/**
 * @struct Aperture
 * @brief  Definición de una apertura: la "forma de dibujo" del Gerber.
 *
 * @details Una aperture es la herramienta con la que el Gerber traza
 *          (círculos, rectángulos...) o estampa (flash) geometría.
 *          Según @ref type, algunos campos aplican y otros se
 *          ignoran. La interpretación de @c outer depende del tipo
 *          (diámetro en Circle, ancho en Rectangle).
 */
struct Aperture {
	/// @brief Forma de la apertura.
	ApertureType type = ApertureType::Circle;
	/// @brief Diámetro (Circle/Polygon) o ancho (Rectangle/Oblong/RoundRect).
	double outer = 0.0;
	/// @brief Diámetro del agujero interior (0 = apertura sólida).
	double inner = 0.0;
	/// @brief Alto para Rectangle/Oblong/RoundRect.
	double height = 0.0;
	/// @brief Radio de esquina para RoundRect.
	double cornerRadius = 0.0;
	/// @brief Centro calculado de la apertura (RoundRect).
	double centerX = 0.0;
	/// @brief Centro calculado de la apertura (RoundRect).
	double centerY = 0.0;
	/// @brief Número de lados para Polygon.
	int sides = 0;
	/// @brief Rotación en grados.
	double rot = 0.0;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Primitivas de renderizado
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @enum   PrimType
 * @brief  Tipo de primitiva geométrica tras el parseo.
 */
enum class PrimType {
	Segment,  ///< @brief Línea trazada con una aperture entre dos puntos (D01).
	Arc,      ///< @brief Arco trazado con una aperture (D01 con G02/G03).
	Flash,    ///< @brief Estampado de una aperture en un punto (D03).
	Region    ///< @brief Polígono relleno delimitado por G36/G37.
};

/**
 * @enum   Polarity
 * @brief  Polaridad de una primitiva: cómo afecta a la máscara de cobre.
 */
enum class Polarity {
	Dark,  ///< @brief Añade material (cobre) — se dibuja y se respeta al mecanizar.
	Clear  ///< @brief Quita material (hueco/ventana) — zona a evitar o aislar.
};

/**
 * @struct Segment
 * @brief  Segmento recto trazado con una aperture.
 *
 * @details Representa un trazo de línea con grosor: @c width es el
 *          diámetro de la apertura con la que se dibujó.
 */
struct Segment {
	Vec2 start, end;  ///< @brief Extremos del segmento.
	double width;     ///< @brief Diámetro de la apertura (grosor del trazo).
};

/**
 * @struct Arc
 * @brief  Arco trazado con una aperture.
 *
 * @details Arco de circunferencia con centro, radio y ángulos de
 *          inicio/fin en radianes. @c width es el diámetro de la
 *          apertura (grosor del trazo).
 */
struct Arc {
	Vec2 start, end, center;          ///< @brief Puntos inicial/final y centro del arco.
	double radius;                   ///< @brief Radio del arco.
	double startAngle, endAngle;      ///< @brief Ángulos inicial y final en radianes.
	bool ccw;                         ///< @brief @c true si el arco es antihorario (G03).
	double width;                     ///< @brief Diámetro de la apertura (grosor del trazo).
};

/**
 * @struct Flash
 * @brief  Estampado de una aperture en un punto (D03).
 *
 * @details Representa un pad o vía: la forma @c aperture estampada
 *          en la posición @c pos.
 */
struct Flash {
	Vec2 pos;         ///< @brief Centro del flash.
	Aperture aperture;  ///< @brief Forma estampada (círculo, rectángulo, macro...).
};

/**
 * @struct Region
 * @brief  Polígono relleno delimitado por G36/G37.
 *
 * @details Región de cobre: un contorno cerrado @c outline y,
 *          opcionalmente, agujeros interiores @c holes.
 */
struct Region {
	std::vector<Vec2> outline;           ///< @brief Contorno cerrado de la región.
	std::vector<std::vector<Vec2>> holes;  ///< @brief Agujeros interiores (contornos cerrados).
};

/**
 * @struct Primitive
 * @brief  Primitiva del Gerber ya parseada, con polaridad y capa.
 *
 * @details Une el tipo (@ref type), la polaridad y la geometría
 *          concreta (@c data, un @c variant). Para extraer la
 *          geometría según el tipo se usan los helpers @c asX().
 *
 * @warning Los helpers @c asSegment()/@c asArc()/@c asFlash()/
 *         @c asRegion() lanzan @c std::bad_variant_access si se
 *         pide un tipo distinto al de @ref type.
 */
struct Primitive {
	PrimType type;                     ///< @brief Tipo de geometría almacenada en @c data.
	Polarity polaridad = Polarity::Dark;  ///< @brief Dark = cobre, Clear = hueco.
	int layer = 0;                     ///< @brief Número de capa/primitiva (uso interno).
	std::variant<Segment, Arc, Flash, Region> data;  ///< @brief Geometría según @c type.

	/**
	 * @brief   Accede a la geometría como Segment.
	 * @return  Referencia constante al segmento.
	 * @warning Solo válido si @ref type es @c PrimType::Segment.
	 */
	const Segment& asSegment() const {
		return std::get<Segment>(data);
	}

	/**
	 * @brief   Accede a la geometría como Arc.
	 * @return  Referencia constante al arco.
	 * @warning Solo válido si @ref type es @c PrimType::Arc.
	 */
	const Arc& asArc() const {
		return std::get<Arc>(data);
	}

	/**
	 * @brief   Accede a la geometría como Flash.
	 * @return  Referencia constante al flash.
	 * @warning Solo válido si @ref type es @c PrimType::Flash.
	 */
	const Flash& asFlash() const {
		return std::get<Flash>(data);
	}

	/**
	 * @brief   Accede a la geometría como Region.
	 * @return  Referencia constante a la región.
	 * @warning Solo válido si @ref type es @c PrimType::Region.
	 */
	const Region& asRegion() const {
		return std::get<Region>(data);
	}
};

// ─────────────────────────────────────────────────────────────────────────────
//  Resultado del Parser
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @struct GerberResult
 * @brief  Resultado de parsear un archivo Gerber RS-274X.
 *
 * @details Contiene todas las primitivas extraídas, la caja
 *          envolvente y el estado del parseo. Es el tipo central
 *          que viaja por toda la aplicación (@c EstadoApp,
 *          mecanizado, toolpaths, GCode).
 */
struct GerberResult {
	std::vector<Primitive> primitives;  ///< @brief Todas las primitivas, en orden de aparición.
	BBox bounds;                        ///< @brief Caja envolvente de toda la geometría.
	std::string unit;                   ///< @brief Unidades del archivo: "MM" o "IN".
	bool ok = false;                    ///< @brief @c true si el parseo terminó sin error.
	std::string error;                   ///< @brief Mensaje de error cuando @c ok es @c false.

	/**
	 * @brief   Itera solo las primitivas de tipo Segment.
	 * @param   cb Callback que recibe cada segmento y su polaridad.
	 */
	void forEachSegment(std::function<void(const Segment&, Polarity)> cb) const;

	/**
	 * @brief   Itera solo las primitivas de tipo Arc.
	 * @param   cb Callback que recibe cada arco y su polaridad.
	 */
	void forEachArc(std::function<void(const Arc&, Polarity)> cb) const;

	/**
	 * @brief   Itera solo las primitivas de tipo Flash.
	 * @param   cb Callback que recibe cada flash y su polaridad.
	 */
	void forEachFlash(std::function<void(const Flash&, Polarity)> cb) const;

	/**
	 * @brief   Itera solo las primitivas de tipo Region.
	 * @param   cb Callback que recibe cada región y su polaridad.
	 */
	void forEachRegion(std::function<void(const Region&, Polarity)> cb) const;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Parser principal
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @class GerberParser
 * @brief  Parser del formato Gerber RS-274X (archivos .gbr).
 *
 * @details Convierte el contenido de un archivo Gerber en un
 *          @c GerberResult. Mantiene el estado del parseo en el
 *          struct privado @c State: formato de coordenadas,
 *          aperturas registradas (ADD), posición del cursor,
 *          polaridad (LP), modo de interpolación (G01/G02/G03),
 *          regiones activas (G36/G37) y macros AM.
 *
 * Ejemplo:
 * @code
 *   gerber::GerberParser parser;
 *   gerber::GerberResult res = parser.parseFile("top.gbr");
 *   if (res.ok) {
 *       res.forEachSegment([](const Segment &s, Polarity pol) {
 *           // trazado de cada segmento con su polaridad
 *       });
 *   }
 * @endcode
 */
class GerberParser {
public:
	/**
	 * @brief   Parsea un archivo Gerber desde disco.
	 * @param   path Ruta del archivo .gbr.
	 * @return  Resultado con las primitivas, límites y estado
	 *          (@c ok / @c error). Si el archivo no se puede abrir,
	 *          @c ok queda a @c false con el mensaje de error.
	 */
	GerberResult parseFile(const std::string &path);

	/**
	 * @brief   Parsea el contenido Gerber desde una cadena en memoria.
	 * @param   src Contenido completo del archivo (con o sin \r).
	 * @return  Resultado equivalente al de @c parseFile().
	 */
	GerberResult parseString(const std::string &src);

	/**
	 * @brief   Decodifica una coordenada según el formato FSLA.
	 * @param   s     Texto de la coordenada (p. ej. "X1750000").
	 * @param   intD  Dígitos enteros del formato.
	 * @param   decD  Dígitos decimales del formato.
	 * @return  Valor numérico de la coordenada.
	 */
	double parseCoord(const std::string &s, int intD, int decD);

private:
	/**
	 * @struct State
	 * @brief  Estado interno del parser durante el recorrido.
	 *
	 * @details Agrupa todo lo que evoluciona línea a línea: macros,
	 *          formato de coordenadas, aperturas, posición, polaridad,
	 *          modo de interpolación, región en construcción y el
	 *          resultado acumulado.
	 */
	struct State {
		std::unordered_map<std::string, std::string> apertureMacros;  ///< @brief Macros AM definidas (nombre → definición).

		// Formato de coordenadas
		int intDigits = 3;        ///< @brief Dígitos enteros del formato (FSLA).
		int decDigits = 6;         ///< @brief Dígitos decimales del formato (FSLA).
		bool trailingZero = false;  ///< @brief @c false = ceros a la izquierda omitidos.
		bool absolute = true;       ///< @brief @c true = coordenadas absolutas (G90).

		// Aperturas registradas
		std::unordered_map<int, Aperture> apertures;  ///< @brief Aperturas definidas (Dnn → forma).
		int currentAperture = 0;                       ///< @brief Aperture seleccionada (Dnn).

		// Posición actual
		Vec2 pos;                          ///< @brief Posición del cursor de dibujo.
		Polarity polaridad = Polarity::Dark;  ///< @brief Polaridad vigente (LP).

		// Modo de interpolación
		int interpMode = 1;       ///< @brief 1 = lineal, 2 = arco CW, 3 = arco CCW.
		bool multiQuadrant = false;  ///< @brief @c true = arcos multicumadrante (G75).

		// Región activa
		bool inRegion = false;              ///< @brief @c true entre G36 y G37.
		std::vector<Vec2> regionContour;     ///< @brief Contorno de la región en construcción.

		// Resultado acumulado
		GerberResult result;  ///< @brief Primitivas y estado que devolverá el parser.
		BBox bounds;          ///< @brief Límites acumulados durante el parseo.

		int layer = 0;  ///< @brief Contador de capa/primitiva.
	};

	/**
	 * @brief   Procesa un parámetro de bloque %...% (FS, MO, ADD, LP, AM...).
	 * @param   param Texto del parámetro sin los delimitadores %.
	 * @param   st    Estado del parser a actualizar.
	 */
	void processParam(const std::string &param, State &st);

	/**
	 * @brief   Procesa un comando de dibujo (D01/D02/D03, G36/G37, Dnn...).
	 * @param   cmd Texto del comando.
	 * @param   st  Estado del parser a actualizar.
	 */
	void processCommand(const std::string &cmd, State &st);

	/**
	 * @brief   Interpreta la definición de una aperture (ADD).
	 * @param   def     Texto de la definición (p. ej. "10C,0.5X0.2").
	 * @param   macros  Macros AM disponibles para apertures de tipo macro.
	 * @return  La apertura construida.
	 */
	Aperture parseAperture(const std::string &def, const std::unordered_map<std::string, std::string> &macros);

	/**
	 * @brief   Añade un segmento trazado al resultado.
	 * @param   from Punto inicial (posición previa del cursor).
	 * @param   to   Punto final.
	 * @param   w    Diámetro de la apertura vigente.
	 * @param   st   Estado del parser (acumula primitiva y límites).
	 */
	void commitSegment(const Vec2 &from, const Vec2 &to, double w, State &st);

	/**
	 * @brief   Añade un arco trazado al resultado.
	 * @param   from   Punto inicial.
	 * @param   to     Punto final.
	 * @param   offset Desplazamiento del centro respecto al inicio (I/J).
	 * @param   ccw    @c true si el arco es antihorario.
	 * @param   w      Diámetro de la apertura vigente.
	 * @param   st     Estado del parser.
	 */
	void commitArc(const Vec2 &from, const Vec2 &to, const Vec2 &offset, bool ccw, double w, State &st);

	/**
	 * @brief   Actualiza los límites (@c BBox) con una primitiva.
	 * @param   p   Primitiva recién añadida.
	 * @param   bb  Caja envolvente a ampliar.
	 */
	void computeBounds(const Primitive &p, BBox &bb);

	/**
	 * @brief   Evalúa una macro AM con sus parámetros y produce la apertura.
	 * @param   name      Nombre de la macro.
	 * @param   params    Parámetros pasados en la definición ADD.
	 * @param   macroDef  Definición textual de la macro.
	 * @return  La apertura resultante de evaluar la macro.
	 */
	Aperture evaluateMacro(const std::string &name, const std::vector<double> &params, const std::string &macroDef);
};

// ─────────────────────────────────────────────────────────────────────────────
//  Configuración del generador GCode
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @struct GCodeConfig
 * @brief  Parámetros del generador de GCode y del mecanizado.
 *
 * @details Agrupa velocidades, profundidades, herramienta y opciones
 *          de formato. La rellena la interfaz con los valores de los
 *          controles (avance, Z seguro, diámetro de fresa...) y la
 *          consumen @c GCodeGenerator, @c Mecanizado y
 *          @c ToolpathGenerator.
 *
 * @note    Las velocidades están en unidades/minuto y las
 *          profundidades en mm o pulgadas según el modo; los
 *          negativos indican bajar el eje Z.
 */
struct GCodeConfig {
	// Velocidades (unidades/min)
	double feedRate = 50.0;    ///< @brief Avance de corte (G01), unidades/min.
	double plungeRate = 20.0;  ///< @brief Avance de penetración en Z (G01 Z), unidades/min.
	double rapidRate = 600.0;   ///< @brief Avance de desplazamiento rápido (G00), unidades/min.

	// Profundidades (mm o pulgadas, negativo = hacia abajo)
	double safeZ = 2.0;         ///< @brief Altura segura de desplazamiento (Z positivo).
	double cutZ = -0.1;         ///< @brief Profundidad de corte (taladro, contorno).
	double traceZ = -0.05;      ///< @brief Profundidad para aislamiento de pistas.

	// Herramienta
	double toolDiameter = 0.1;  ///< @brief Diámetro de la fresa.

	// Tolerancia de arcos (para aproximación si se necesita)
	double arcTolerance = 0.01;  ///< @brief Tolerancia al aproximar arcos con segmentos.

	// Origen / escala
	double offsetX = 0.0;  ///< @brief Desplazamiento del origen en X.
	double offsetY = 0.0;  ///< @brief Desplazamiento del origen en Y.
	double scale = 1.0;    ///< @brief Escala global aplicada al GCode.

	// Opciones de formato
	bool useArcCommands = true;  ///< @brief @c true = emitir G02/G03 nativos; @c false = aproximar con segmentos.
	bool addComments = true;     ///< @brief @c true = añadir comentarios explicativos al GCode.
	bool metricMode = true;     ///< @brief @c true = G21 (mm); @c false = G20 (pulgadas).
	int decimalPlaces = 4;      ///< @brief Decimales emitidos en las coordenadas.
	bool optimizePaths = true;    ///< @brief @c true = reordenar para menos desplazamientos en vacío.
};

// ─────────────────────────────────────────────────────────────────────────────
//  Generador GCode
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @class GCodeGenerator
 * @brief  Convierte un @c GerberResult en un programa GCode.
 *
 * @details Recorre las primitivas y emite el código correspondiente:
 *          cabecera (unidades, cotas, arranque de husillo),
 *          mecanizado de cada primitiva (segmentos, arcos, flashes,
 *          regiones) y pie de programa. Ofrece dos modos: contorno
 *          (@c generate) y aislamiento (@c generateIsolation).
 *
 *          La configuración se copia en el constructor; el estado de
 *          salida (@c out_, posición, husillo) se reinicia al
 *          comienzo de cada llamada a @c generate().
 *
 * Ejemplo:
 * @code
 *   gerber::GCodeConfig cfg;
 *   cfg.toolDiameter = 0.2;
 *   gerber::GCodeGenerator gen(cfg);
 *   std::string gcode = gen.generateIsolation(res, 0.05);
 * @endcode
 */
class GCodeGenerator {
public:
	/**
	 * @brief   Constructor con configuración.
	 * @param   cfg Configuración de mecanizado (se copia internamente).
	 * @note    Si se omite, se usan los valores por defecto de
	 *          @ref GCodeConfig.
	 */
	explicit GCodeGenerator(const GCodeConfig &cfg = { }) :
			cfg_(cfg) {
	}

	/**
	 * @brief   Genera el GCode mecanizando las primitivas tal cual.
	 * @param   res Resultado del parser Gerber (debe ser válido).
	 * @return  Programa GCode completo (con cabecera y pie).
	 */
	std::string generate(const GerberResult &res);

	/**
	 * @brief   Genera el GCode de aislamiento alrededor del cobre.
	 * @details Para cada segmento/arco/flash/región @c Dark calcula un
	 *          offset de @c ancho/2 + @p clearance + diámetro/2 y
	 *          mecaniza ese contorno; las primitivas @c Clear se
	 *          omiten. Es la ruta clásica para aislar pistas de PCB.
	 * @param   res       Resultado del parser Gerber.
	 * @param   clearance Holgura extra entre la fresa y el borde del
	 *                    cobre (0.0 por defecto).
	 * @return  Programa GCode de aislamiento.
	 */
	std::string generateIsolation(const GerberResult &res, double clearance = 0.0);
private:
	GCodeConfig cfg_;      ///< @brief Configuración copiada del constructor.
	std::string out_;      ///< @brief Buffer del GCode en construcción.
	Vec2 curPos_;          ///< @brief Posición actual de la herramienta.
	bool spindle_ = false;  ///< @brief @c true si el husillo está arrancado (M03).

	/**
	 * @brief Emite la cabecera del programa (unidades, cotas, M03).
	 */
	void header();

	/**
	 * @brief Emite el pie del programa (parada de husillo, M30).
	 */
	void footer();

	/**
	 * @brief   Desplazamiento rápido (G00) a (x, y) y opcionalmente Z.
	 * @param   x Coordenada X destino.
	 * @param   y Coordenada Y destino.
	 * @param   z Altura Z destino; si se omite, no se emite Z.
	 */
	void rapid(double x, double y, double z = -1e18);

	/**
	 * @brief Corte en línea recta (G01) hasta (x, y) con el avance de corte.
	 */
	void move(double x, double y);

	/**
	 * @brief Movimiento vertical de corte (G01 Z) con el avance de penetración.
	 */
	void moveZ(double z);

	/**
	 * @brief   Arco (G02/G03 o segmentos según @c useArcCommands).
	 * @param   x,y  Punto destino.
	 * @param   i,j  Centro relativo del arco.
	 * @param   ccw  @c true = antihorario (G03).
	 */
	void arc(double x, double y, double i, double j, bool ccw);

	/**
	 * @brief Baja el eje Z hasta la profundidad de corte (con @c traceZ).
	 */
	void plunge();

	/**
	 * @brief Retrae la herramienta hasta la altura segura (G00 Z safeZ).
	 */
	void retract();

	/**
	 * @brief Añade un comentario al GCode (si @c addComments está activo).
	 */
	void comment(const std::string &s);

	/**
	 * @brief Añade una línea al buffer de salida @c out_.
	 */
	void emit(const std::string &line);

	/**
	 * @brief Mecaniza un segmento: rápido al inicio, penetración, corte.
	 */
	void machineSegment(const Segment &seg);

	/**
	 * @brief Mecaniza un arco (nativo o aproximado por segmentos).
	 */
	void machineArc(const Arc &a);

	/**
	 * @brief Mecaniza un flash (pad/vía) siguiendo su contorno.
	 */
	void machineFlash(const Flash &f);

	/**
	 * @brief Mecaniza una región recorriendo su contorno con offset.
	 */
	void machineRegion(const Region &r);

	/**
	 * @struct PathOp
	 * @brief  Operación de mecanizado para el optimizador de recorridos.
	 * @details Asocia el tipo de primitiva con su punto de inicio y su
	 *          índice en el @c GerberResult, para poder reordenarlas.
	 */
	struct PathOp {
		enum class Kind {
			Segment,  ///< @brief Operación sobre un segmento.
			Arc,      ///< @brief Operación sobre un arco.
			Flash,    ///< @brief Operación sobre un flash.
			Region    ///< @brief Operación sobre una región.
		} kind;       ///< @brief Tipo de primitiva.
		Vec2 startPt;  ///< @brief Punto donde empieza la operación.
		size_t idx;    ///< @brief Índice de la primitiva original.
	};

	/**
	 * @brief   Construye la lista de operaciones del resultado.
	 * @param   res Resultado del parser Gerber.
	 * @return  Una @c PathOp por cada primitiva mecanizable.
	 */
	std::vector<PathOp> buildOps(const GerberResult &res);

	/**
	 * @brief   Reordena las operaciones por vecino más cercano (greedy).
	 * @param   ops Operaciones de entrada.
	 * @return  Índices de @p ops en el orden óptimo de mecanizado.
	 */
	std::vector<size_t> optimizeOrder(const std::vector<PathOp> &ops);

	/**
	 * @brief   Transforma una coordenada X al sistema del GCode.
	 * @details Aplica el offset y la escala de @c GCodeConfig.
	 */
	double tx(double x) const {
		return (x + cfg_.offsetX) * cfg_.scale;
	}

	/**
	 * @brief   Transforma una coordenada Y al sistema del GCode.
	 * @details Aplica el offset y la escala de @c GCodeConfig.
	 */
	double ty(double y) const {
		return (y + cfg_.offsetY) * cfg_.scale;
	}

	/**
	 * @brief   Formatea un número con los decimales configurados.
	 * @return  Cadena con el número listo para el GCode.
	 */
	std::string fmt(double v) const;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Utilidades de geometría (usadas internamente, también accesibles)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief   Funciones geométricas compartidas por los módulos de mecanizado.
 */
namespace geom {
/**
 * @brief   Aproxima un arco con una polilínea de segmentos rectos.
 * @param   center      Centro del arco.
 * @param   radius      Radio del arco.
 * @param   startAngle  Ángulo inicial (radianes).
 * @param   endAngle    Ángulo final (radianes).
 * @param   ccw         @c true = antihorario.
 * @param   tolerance   Precisión de la aproximación (se baja si se omite).
 * @return  Puntos de la polilínea, en orden de recorrido.
 */
std::vector<Vec2> arcToPolyline(const Vec2 &center, double radius, double startAngle, double endAngle, bool ccw, double tolerance = 0.01);

/**
 * @brief   Desplaza un polígono hacia afuera (o adentro) una distancia fija.
 * @details Método simplificado por bisección de normales: mantiene la
 *          distancia perpendicular constante y limita el
 *          desplazamiento en ángulos muy agudos para evitar
 *          "explosiones" del contorno. No detecta
 *          autointersecciones: con offsets negativos grandes el
 *          resultado puede degenerar.
 * @param   poly    Contorno (se espera cerrado, en CCW para offset positivo hacia afuera).
 * @param   offset  Distancia a desplazar cada vértice (positivo =
 *                  afuera, negativo = adentro).
 * @return  El polígono desplazado; el original sin tocar si tiene
 *          menos de 3 vértices.
 *
 * @note    Es la operación central del módulo @c Mecanizado
 *          (contornos de corte y zonas de exclusión).
 */
std::vector<Vec2> offsetPolygon(const std::vector<Vec2> &poly, double offset);

/**
 * @brief   Convierte un flash (pad/vía) en su polígono equivalente.
 * @details La forma depende del tipo de @c Aperture: círculo,
 *          rectángulo, oblongo, polígono, redondeado...
 * @param   f                  Flash a convertir.
 * @param   segmentsPerCircle  Puntos usados para aproximar curvas.
 * @return  Contorno del flash en coordenadas de trabajo.
 */
std::vector<Vec2> flashToPolygon(const Flash &f, int segmentsPerCircle = 32);

/**
 * @brief   Rota un polígono alrededor de un punto.
 * @param   poly      Contorno a rotar (se pasa por valor y se modifica).
 * @param   angleRad  Ángulo de rotación en radianes.
 * @param   pivot     Punto fijo alrededor del cual rotar.
 * @return  El polígono rotado.
 */
std::vector<Vec2> rotatePolygon(std::vector<Vec2> poly, double angleRad, const Vec2 &pivot);

/**
 * @brief   Área con signo de un polígono (fórmula del shoelace).
 * @param   poly Contorno cerrado.
 * @return  Área; el signo indica la orientación (positiva = CCW).
 */
double signedArea(const std::vector<Vec2> &poly);

/**
 * @brief   Asegura que el polígono quede en orden antihorario (CCW).
 * @param   poly Contorno a normalizar (se pasa por valor).
 * @return  El mismo polígono, invertido si era horario.
 */
std::vector<Vec2> ensureCCW(std::vector<Vec2> poly);

/**
 * @brief   Ángulo entre dos vectores.
 * @param   a Primer vector.
 * @param   b Segundo vector.
 * @return  Ángulo en radianes.
 */
double angleBetween(const Vec2 &a, const Vec2 &b);
}  // namespace geom

}  // namespace gerber

#endif /* PROCESOS_GERBER_H_ */

