/*
 * Procesos/mecanizado.h
 *
 * Created on: 14 may 2026
 * Author: DjSteker
 */

/**
 * @file mecanizado.h
 * @brief Tipos geométricos y generación de trayectorias CNC.
 *
 * @details
 * Convierte la geometría Gerber en movimientos de herramienta y proporciona
 * utilidades para construir figuras, calcular offsets, extraer fronteras
 * y ordenar caminos de mecanizado.
 *
 * Las funciones se agrupan como métodos estáticos de Mecanizado.
 * La configuración y los datos de trabajo se reciben mediante parámetros.
 *
 * @par Modos de trabajo
 * - Contorno básico: construirTrayectoria().
 * - Aislamiento por grupos: construirTrayectoriaAislamiento().
 * - Limpieza de la zona sin cobre: construirLimpieza().
 * - Selección de limpieza o perfilado interior: construirTrayectoriaPerfilado().
 *
 * @par Unidades
 * Las coordenadas, diámetros, distancias y tolerancias deben expresarse
 * en unidades compatibles con la geometría Gerber recibida.
 *
 * @warning
 * Las comprobaciones geométricas utilizan tolerancias y aproximaciones
 * poligonales. La generación de una trayectoria no sustituye su revisión
 * en el visor y en simulación antes del mecanizado.
 *
 * @author DjSteker
 * @date 14 may 2026
 */

#ifndef MECANIZADO_H_
#define MECANIZADO_H_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "drill.h"
#include "gerber.h"

// =============================================================================
// TIPOS DEL RECORRIDO
// =============================================================================

/**
 * @enum TipoMovimiento
 * @brief Acción elemental de la herramienta.
 */
enum class TipoMovimiento {
	Viaje,  ///< Desplazamiento XY con la herramienta levantada.
	Corte,  ///< Desplazamiento XY de mecanizado.
	Subir,  ///< Retracción del eje Z a la altura segura.
	Bajar   ///< Penetración del eje Z a la profundidad de trabajo.
};

/**
 * @struct MovimientoHerramienta
 * @brief Movimiento elemental de una trayectoria CNC.
 *
 * @details
 * Los movimientos XY utilizan desde y hasta como extremos.
 * En los movimientos exclusivamente verticales ambos puntos
 * pueden coincidir.
 */
struct MovimientoHerramienta {
	TipoMovimiento tipo;  ///< Acción que debe ejecutarse.
	gerber::Vec2 desde;   ///< Posición XY inicial.
	gerber::Vec2 hasta;   ///< Posición XY final.
};

// =============================================================================
// GEOMETRÍA BÁSICA
// =============================================================================

/**
 * @brief Secuencia de vértices de un contorno o de un camino.
 *
 * @details
 * Su interpretación depende de la función:
 * - En polígonos de entrada, el cierre suele ser implícito.
 * - En caminos de salida, un lazo cerrado repite el primer punto al final.
 *
 * @note No todo Poligono representa necesariamente un camino cerrado.
 */
using Poligono = std::vector<gerber::Vec2>;

/**
 * @struct Caja
 * @brief Caja envolvente alineada con los ejes.
 */
struct Caja {
	double x0;  ///< Coordenada X mínima.
	double y0;  ///< Coordenada Y mínima.
	double x1;  ///< Coordenada X máxima.
	double y1;  ///< Coordenada Y máxima.
};

// =============================================================================
// ÍNDICES ESPACIALES
// =============================================================================

/**
 * @struct Rejilla
 * @brief Índice espacial uniforme de cajas envolventes.
 *
 * @details
 * Cada celda almacena los índices de las cajas que la atraviesan.
 * Las consultas producen candidatos que deben verificarse geométricamente.
 *
 * @note Una misma caja puede aparecer varias veces en una consulta.
 */
struct Rejilla {
	double x0 = 0.0;   ///< Origen X de la rejilla.
	double y0 = 0.0;   ///< Origen Y de la rejilla.
	double cel = 1.0;  ///< Tamaño de celda.

	int nx = 1;  ///< Número de columnas.
	int ny = 1;  ///< Número de filas.

	std::vector<std::vector<int>> celdas; ///< Índices por celda.

	/**
	 * @brief Construye el índice de las cajas recibidas.
	 * @param cajas Cajas que se indexarán.
	 * @param celdaDeseada Tamaño inicial deseado de celda.
	 *
	 * @details
	 * Incrementa el tamaño de celda cuando es necesario para limitar
	 * aproximadamente el número total de celdas.
	 */
	void construir(const std::vector<Caja> &cajas, double celdaDeseada) {
		celdas.clear();

		Caja t { 1e300, 1e300, -1e300, -1e300 };

		for (const auto &c : cajas) {
			t.x0 = std::min(t.x0, c.x0);
			t.y0 = std::min(t.y0, c.y0);
			t.x1 = std::max(t.x1, c.x1);
			t.y1 = std::max(t.y1, c.y1);
		}

		if (cajas.empty()) {
			x0 = y0 = 0.0;
			cel = 1.0;
			nx = ny = 1;
			celdas.assign(1, { });
			return;
		}

		cel = std::max(celdaDeseada, 1e-3);

		while (((t.x1 - t.x0) / cel + 1.0) * ((t.y1 - t.y0) / cel + 1.0) > 2e6) {
			cel *= 2.0;
		}

		x0 = t.x0;
		y0 = t.y0;

		nx = static_cast<int>((t.x1 - t.x0) / cel) + 1;
		ny = static_cast<int>((t.y1 - t.y0) / cel) + 1;

		celdas.assign(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny), { });

		for (std::size_t i = 0; i < cajas.size(); ++i) {
			for (int j = iy(cajas[i].y0); j <= iy(cajas[i].y1); ++j) {
				for (int k = ix(cajas[i].x0); k <= ix(cajas[i].x1); ++k) {
					const std::size_t indice = static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) + static_cast<std::size_t>(k);

					celdas[indice].push_back(static_cast<int>(i));
				}
			}
		}
	}

	/**
	 * @brief Obtiene la columna correspondiente a una coordenada.
	 * @param x Coordenada X.
	 * @return Índice limitado al intervalo de columnas.
	 */
	int ix(double x) const {
		return std::clamp(static_cast<int>(std::floor((x - x0) / cel)), 0, nx - 1);
	}

	/**
	 * @brief Obtiene la fila correspondiente a una coordenada.
	 * @param y Coordenada Y.
	 * @return Índice limitado al intervalo de filas.
	 */
	int iy(double y) const {
		return std::clamp(static_cast<int>(std::floor((y - y0) / cel)), 0, ny - 1);
	}

	/**
	 * @brief Visita los candidatos de las celdas de una caja.
	 * @tparam F Tipo de la función receptora.
	 * @param c Caja de consulta.
	 * @param f Función invocada como f(indice).
	 *
	 * @pre Se ha llamado a construir().
	 * @note Los índices pueden repetirse.
	 */
	template<class F>
	void consultar(const Caja &c, F &&f) const {
		for (int j = iy(c.y0); j <= iy(c.y1); ++j) {
			for (int k = ix(c.x0); k <= ix(c.x1); ++k) {
				const std::size_t indice = static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) + static_cast<std::size_t>(k);

				for (int i : celdas[indice]) {
					f(i);
				}
			}
		}
	}
};

/**
 * @struct IndiceBordes
 * @brief Índice de aristas para consultas sobre polígonos con anillos.
 *
 * @details
 * Utiliza una rejilla de cajas para las distancias y franjas horizontales
 * para las pruebas de pertenencia mediante la regla par-impar.
 */
struct IndiceBordes {
	/**
	 * @struct Borde
	 * @brief Segmento que forma parte de un anillo.
	 */
	struct Borde {
		gerber::Vec2 a; ///< Primer extremo.
		gerber::Vec2 b; ///< Segundo extremo.
	};

	std::vector<Borde> bordes; ///< Aristas de todos los anillos.
	Rejilla rej;               ///< Índice espacial de las aristas.

	double yMin = 0.0; ///< Coordenada inferior de las franjas.
	double h = 1.0;    ///< Altura de cada franja.
	int nf = 1;       ///< Número de franjas.

	std::vector<std::vector<int>> filas; ///< Aristas por franja.

	/**
	 * @brief Construye los índices a partir de los anillos.
	 * @param anillos Contornos con cierre implícito.
	 */
	void construir(const std::vector<Poligono> &anillos) {
		bordes.clear();

		for (const auto &r : anillos) {
			for (std::size_t i = 0, n = r.size(); i < n; ++i) {
				bordes.push_back( { r[i], r[(i + 1) % n] });
			}
		}

		// Estado vacío válido: evita convertir extensiones centinela a int.
		if (bordes.empty()) {
			rej.construir(std::vector<Caja> { }, 1.0);
			yMin = 0.0;
			h = 1.0;
			nf = 1;
			filas.assign(1, { });
			return;
		}

		std::vector<Caja> cajas(bordes.size());

		double ymax = -1e300;
		yMin = 1e300;
		double suma = 0.0;

		for (std::size_t i = 0; i < bordes.size(); ++i) {
			const auto &e = bordes[i];

			cajas[i] = Caja { std::min(e.a.x, e.b.x), std::min(e.a.y, e.b.y), std::max(e.a.x, e.b.x), std::max(e.a.y, e.b.y) };

			yMin = std::min(yMin, cajas[i].y0);
			ymax = std::max(ymax, cajas[i].y1);

			suma += std::max(cajas[i].x1 - cajas[i].x0, cajas[i].y1 - cajas[i].y0);
		}

		const double cel = std::clamp(4.0 * suma / static_cast<double>(bordes.size()), 0.2, 5.0);

		rej.construir(cajas, cel);

		h = cel;

		while ((ymax - yMin) / h + 1.0 > 1e6) {
			h *= 2.0;
		}

		nf = static_cast<int>((ymax - yMin) / h) + 1;
		filas.assign(static_cast<std::size_t>(nf), { });

		for (std::size_t i = 0; i < bordes.size(); ++i) {
			const int f0 = std::clamp(static_cast<int>(std::floor((cajas[i].y0 - yMin) / h)), 0, nf - 1);

			const int f1 = std::clamp(static_cast<int>(std::floor((cajas[i].y1 - yMin) / h)), 0, nf - 1);

			for (int f = f0; f <= f1; ++f) {
				filas[static_cast<std::size_t>(f)].push_back(static_cast<int>(i));
			}
		}
	}

	/**
	 * @brief Comprueba la pertenencia mediante la regla par-impar.
	 * @param p Punto de consulta.
	 * @return true si se clasifica como interior.
	 * @note La clasificación de puntos sobre el borde no es un contrato.
	 */
	bool dentro(const gerber::Vec2 &p) const {
		if (bordes.empty()) {
			return false;
		}

		const int f = static_cast<int>(std::floor((p.y - yMin) / h));

		if (f < 0 || f >= nf) {
			return false;
		}

		bool in = false;

		for (int i : filas[static_cast<std::size_t>(f)]) {
			const auto &e = bordes[static_cast<std::size_t>(i)];

			if (((e.a.y > p.y) != (e.b.y > p.y)) && (p.x < (e.b.x - e.a.x) * (p.y - e.a.y) / (e.b.y - e.a.y) + e.a.x)) {
				in = !in;
			}
		}

		return in;
	}

	/**
	 * @brief Comprueba si alguna arista está a distancia menor o igual a r.
	 * @param p Punto de consulta.
	 * @param r Radio de búsqueda, no negativo.
	 * @return true si se encuentra alguna arista dentro del radio.
	 */
	bool hayBordeA(const gerber::Vec2 &p, double r) const {
		if (bordes.empty()) {
			return false;
		}

		bool hay = false;
		const double r2 = r * r;

		rej.consultar(Caja { p.x - r, p.y - r, p.x + r, p.y + r }, [&](int i) {
			if (hay) {
				return;
			}

			const auto &e = bordes[static_cast<std::size_t>(i)];

			const double vx = e.b.x - e.a.x;
			const double vy = e.b.y - e.a.y;
			const double l2 = vx * vx + vy * vy;

			double t = l2 > 1e-24 ? ((p.x - e.a.x) * vx + (p.y - e.a.y) * vy) / l2 : 0.0;

			t = std::clamp(t, 0.0, 1.0);

			const double ex = p.x - (e.a.x + vx * t);
			const double ey = p.y - (e.a.y + vy * t);

			hay = ex * ex + ey * ey <= r2;
		}
		);

		return hay;
	}
};

// =============================================================================
// REPRESENTACIÓN DE FIGURAS Y CAMINOS
// =============================================================================

/**
 * @struct Figura
 * @brief Figura de cobre representada como cápsula o conjunto de anillos.
 */
struct Figura {
	bool capsula = false; ///< true si utiliza la representación de cápsula.

	gerber::Vec2 a; ///< Primer extremo del eje de la cápsula.
	gerber::Vec2 b; ///< Segundo extremo del eje de la cápsula.
	double r = 0.0; ///< Radio de la cápsula.

	/**
	 * @brief Anillos de la figura.
	 *
	 * En figuras ordinarias, el exterior se orienta CCW y los huecos CW.
	 * En figuras compuestas pueden almacenarse varios lazos.
	 * Los offsets dependen de la orientación de cada anillo.
	 */
	std::vector<Poligono> anillos;

	Caja caja { 0, 0, 0, 0 }; ///< Caja envolvente del contorno finito.

	/**
	 * @brief Invierte el signo de la distancia en sdFigura().
	 * @note No convierte caja en la envolvente de un complemento infinito.
	 */
	bool complemento = false;

	std::shared_ptr<IndiceBordes> indice; ///< Índice opcional de aristas.
};

/**
 * @struct ProhFig
 * @brief Referencia a una figura que limita una trayectoria.
 *
 * @warning Los objetos referenciados deben vivir durante su utilización.
 */
struct ProhFig {
	const Figura *f;                    ///< Figura de referencia.
	const std::vector<Poligono> *zona;   ///< Curvas de su zona de exclusión.
};

/**
 * @struct RawForma
 * @brief Curva candidata de offset antes del recorte.
 */
struct RawForma {
	Poligono p;       ///< Curva candidata.
	std::size_t fig;  ///< Índice en el vector de figuras correspondiente.
	bool convexo;     ///< Permite omitir el análisis de autointersecciones.
};

/**
 * @struct Tramo
 * @brief Camino pendiente de ordenar y emitir.
 */
struct Tramo {
	Poligono p;   ///< Vértices del camino.
	bool cerrado; ///< true si el último vértice repite al primero.
	Caja caja;    ///< Envolvente del camino.
};

// =============================================================================
// CLASE DE MECANIZADO
// =============================================================================

/**
 * @class Mecanizado
 * @brief Agrupa operaciones geométricas y generadores de trayectorias.
 *
 * @details
 * Todos sus métodos son estáticos. Se invocan mediante Mecanizado::metodo().
 * Los métodos auxiliares permanecen públicos durante la refactorización.
 *
 * @note
 * Los argumentos por defecto se declaran aquí, no de nuevo en las
 * definiciones de mecanizado.cpp.
 */
class Mecanizado {
public:
	/**
	 * @brief Resolución angular por semicircunferencia.
	 *
	 * Con 24 pasos, la separación angular de la rejilla es de 7.5 grados.
	 */
	static const int PASOS_TAPA = 24;

	/**
	 * @struct Info
	 * @brief Representación poligonal usada por construirGrupos().
	 *
	 * @note Se mantiene como tipo anidado: Mecanizado::Info.
	 */
	struct Info {
		std::vector<gerber::Vec2> poly; ///< Polígono de la figura.
		int idx;                       ///< Índice de la primitiva original.
		bool esActivo;                 ///< Figura activa para ese algoritmo.
		bool esIsla;                   ///< Región clasificada como referencia.
	};

	// -------------------------------------------------------------------------
	// Geometría básica
	// -------------------------------------------------------------------------

	/**
	 * @brief Calcula la distancia euclídea al cuadrado.
	 * @param a Primer punto.
	 * @param b Segundo punto.
	 * @return Distancia al cuadrado.
	 */
	static double d2(const gerber::Vec2 &a, const gerber::Vec2 &b);

	/**
	 * @brief Calcula el valor absoluto del área de un polígono.
	 * @param poly Polígono con cierre implícito.
	 * @return Área no negativa; cero si hay menos de tres vértices.
	 */
	static double areaPoligono(const std::vector<gerber::Vec2> &poly);

	/**
	 * @brief Clasifica un punto mediante la regla par-impar.
	 * @param pt Punto consultado.
	 * @param poly Polígono con cierre implícito.
	 * @return true si el punto se clasifica como interior.
	 * @note Los puntos sobre el borde pueden resultar ambiguos.
	 */
	static bool pip(const gerber::Vec2 &pt, const std::vector<gerber::Vec2> &poly);

	/**
	 * @brief Invierte el polígono si su orientación es horaria.
	 * @param[in,out] poly Polígono que se normalizará.
	 * @return true si se invirtió el orden.
	 */
	static bool asegurarCCW(std::vector<gerber::Vec2> &poly);

	/**
	 * @brief Calcula la distancia al cuadrado al borde de un polígono.
	 * @param p Punto consultado.
	 * @param q Polígono con cierre implícito.
	 * @return Mínimo de las distancias al cuadrado a sus aristas.
	 */
	static double dist2Borde(const gerber::Vec2 &p, const Poligono &q);

	/**
	 * @brief Calcula la distancia al cuadrado a un segmento.
	 * @param p Punto consultado.
	 * @param a Primer extremo.
	 * @param b Segundo extremo.
	 * @return Distancia al cuadrado al segmento finito.
	 */
	static double dist2PuntoSeg(const gerber::Vec2 &p, const gerber::Vec2 &a, const gerber::Vec2 &b);

	/**
	 * @brief Elimina vértices consecutivos prácticamente coincidentes.
	 * @param P Secuencia de entrada.
	 * @return Secuencia sin duplicados consecutivos ni cierre repetido.
	 */
	static Poligono sinDuplicados(const Poligono &P);

	// -------------------------------------------------------------------------
	// Cajas e intersecciones
	// -------------------------------------------------------------------------

	/**
	 * @brief Calcula la caja envolvente de los vértices.
	 * @param p Secuencia no vacía.
	 * @return Caja mínima alineada con los ejes.
	 * @pre p no está vacío para obtener una envolvente válida.
	 */
	static Caja cajaDe(const Poligono &p);

	/**
	 * @brief Comprueba el solape de dos cajas, incluidos sus bordes.
	 * @param a Primera caja.
	 * @param b Segunda caja.
	 * @return true si sus intervalos se solapan en ambos ejes.
	 */
	static bool cajasSolapan(const Caja &a, const Caja &b);

	/**
	 * @brief Comprueba la pertenencia a una caja, incluidos los bordes.
	 * @param c Caja de referencia.
	 * @param p Punto consultado.
	 * @return true si el punto pertenece a la caja.
	 */
	static bool puntoEnCaja(const Caja &c, const gerber::Vec2 &p);

	/**
	 * @brief Desplaza los límites de una caja.
	 * @param c Caja original.
	 * @param m Margen: positivo amplía y negativo contrae.
	 * @return Caja modificada.
	 */
	static Caja expandirCaja(Caja c, double m);

	/**
	 * @brief Estima un tamaño de celda a partir de las cajas.
	 * @param cajas Cajas que se indexarán.
	 * @return Tamaño estimado y acotado por la implementación.
	 */
	static double celdaPara(const std::vector<Caja> &cajas);

	/**
	 * @brief Busca una intersección transversal entre dos segmentos.
	 * @param a Inicio del primer segmento.
	 * @param b Fin del primer segmento.
	 * @param c Inicio del segundo segmento.
	 * @param d Fin del segundo segmento.
	 * @param[out] t Parámetro del cruce sobre a + t * (b - a).
	 * @return true si el cruce satisface los límites de la implementación.
	 *
	 * @details
	 * Excluye los extremos del primer segmento con tolerancia.
	 * Admite parámetros entre cero y uno en el segundo.
	 * No resuelve solapes colineales.
	 */
	static bool interseccionSegmentos(const gerber::Vec2 &a, const gerber::Vec2 &b, const gerber::Vec2 &c, const gerber::Vec2 &d, double &t);

	// -------------------------------------------------------------------------
	// Conversión de geometría
	// -------------------------------------------------------------------------

	/**
	 * @brief Construye la aproximación poligonal básica de un segmento ancho.
	 * @param s Segmento de entrada.
	 * @param ptsTapa Pasos por semicircunferencia; debe ser positivo.
	 * @return Contorno; vacío si el segmento se considera degenerado.
	 */
	static std::vector<gerber::Vec2> segmentoAPoligono(const gerber::Segment &s, int ptsTapa = 8);

	/**
	 * @brief Añade puntos de un arco alineados con la rejilla angular global.
	 * @param[in,out] out Secuencia receptora.
	 * @param c Centro.
	 * @param r Radio.
	 * @param a0 Ángulo inicial en radianes.
	 * @param a1 Ángulo final en radianes.
	 * @param paso Separación angular de la rejilla.
	 * @pre a0 < a1 y paso > 0.
	 */
	static void anadirArcoGrid(std::vector<gerber::Vec2> &out, const gerber::Vec2 &c, double r, double a0, double a1, double paso);

	/**
	 * @brief Construye una cápsula con tapas sobre la rejilla angular global.
	 * @param seg Segmento que define el eje.
	 * @param ancho Anchura total solicitada.
	 * @param pasos Pasos por semicircunferencia; debe ser positivo.
	 * @return Contorno poligonal con cierre implícito.
	 * @note La implementación aplica un radio mínimo de 0.01.
	 */
	static std::vector<gerber::Vec2> segmentoACapsula(const gerber::Segment &seg, double ancho, int pasos = PASOS_TAPA);

	/**
	 * @brief Construye una figura de tipo cápsula.
	 * @param a Primer extremo del eje.
	 * @param b Segundo extremo del eje.
	 * @param r Radio solicitado.
	 * @return Figura con contorno y caja calculados.
	 */
	static Figura nuevaCapsula(const gerber::Vec2 &a, const gerber::Vec2 &b, double r);

	/**
	 * @brief Construye una figura poligonal de un solo anillo.
	 * @param poly Contorno exterior.
	 * @return Figura con orientación normalizada y caja calculada.
	 */
	static Figura nuevoPoligono(Poligono poly);

	/**
	 * @brief Convierte un flash en una figura.
	 * @param fl Flash de entrada.
	 * @param[out] out Figura generada cuando la conversión tiene éxito.
	 * @return true si se obtuvo una figura válida.
	 *
	 * @details
	 * Círculos y oblongos utilizan cápsulas; los demás casos se convierten
	 * mediante gerber::geom::flashToPolygon().
	 */
	static bool flashAFigura(const gerber::Flash &fl, Figura &out);

	/**
	 * @brief Recalcula la envolvente y el índice opcional de una figura.
	 * @param[in,out] f Figura que se actualizará.
	 */
	static void calcularCaja(Figura &f);

	// -------------------------------------------------------------------------
	// Distancias y offsets
	// -------------------------------------------------------------------------

	/**
	 * @brief Calcula la distancia con signo sin aplicar complemento.
	 * @param f Figura consultada.
	 * @param p Punto.
	 * @return Distancia negativa dentro y positiva fuera.
	 */
	static double sdBruto(const Figura &f, const gerber::Vec2 &p);

	/**
	 * @brief Calcula la distancia con signo aplicando Figura::complemento.
	 * @param f Figura consultada.
	 * @param p Punto.
	 * @return Distancia de sdBruto(), invertida si corresponde.
	 */
	static double sdFigura(const Figura &f, const gerber::Vec2 &p);

	/**
	 * @brief Evalúa el umbral de distancia con posible aceleración espacial.
	 * @param f Figura consultada.
	 * @param p Punto.
	 * @param umbral Distancia de comparación.
	 * @return Resultado de la comparación implementada.
	 * @note Los casos de igualdad dependen de las pruebas del índice.
	 */
	static bool sdMenor(const Figura &f, const gerber::Vec2 &p, double umbral);

	/**
	 * @brief Genera una curva candidata desplazada a la derecha del anillo.
	 * @param P Anillo orientado.
	 * @param d Distancia del desplazamiento.
	 * @return Curva sin resolver necesariamente sus autointersecciones.
	 *
	 * @details
	 * Con el cobre a la izquierda del anillo y d positivo, el desplazamiento
	 * apunta fuera del cobre. Invertir el anillo cambia el lado desplazado.
	 */
	static Poligono offsetCrudo(const Poligono &P, double d);

	/**
	 * @brief Añade las curvas candidatas de offset de una figura.
	 * @param f Figura original.
	 * @param d Distancia de offset.
	 * @param[in,out] out Vector al que se añaden las curvas.
	 */
	static void rawDeFigura(const Figura &f, double d, std::vector<Poligono> &out);

	// -------------------------------------------------------------------------
	// Extracción y agrupamiento
	// -------------------------------------------------------------------------

	/**
	 * @brief Convierte primitivas Gerber en figuras.
	 * @param res Resultado del parser.
	 * @param soloIslas Si es true, omite segmentos y arcos Dark.
	 * @return Figuras con sus cajas e índices actualizados.
	 *
	 * @warning
	 * El tratamiento actual de Clear es parcial: asocia flashes y regiones
	 * Clear a una región Dark anterior mediante una prueba de pertenencia.
	 * No implementa una composición booleana general de todas las primitivas.
	 */
	static std::vector<Figura> crearFiguras(const gerber::GerberResult &res, bool soloIslas);

	/**
	 * @brief Agrupa figuras por proximidad, cruces o contención.
	 * @param res Resultado del parser.
	 * @param tolConexion Tolerancia base de conexión.
	 * @param soloIslas Si es true, omite segmentos y arcos al extraer.
	 * @return Grupos obtenidos mediante Union-Find.
	 */
	static std::vector<std::vector<Figura>> agruparFiguras(const gerber::GerberResult &res, double tolConexion, bool soloIslas = false);

	/**
	 * @brief Agrupamiento poligonal conservado por compatibilidad.
	 * @param res Resultado del parser.
	 * @param[in,out] gruposInfo Vector al que se añaden los grupos.
	 * @param tolConexion Tolerancia base de conexión.
	 * @param radioHerramienta Radio usado para clasificar regiones grandes.
	 *
	 * @note Es una ruta distinta de agruparFiguras(), basada en Info.
	 */
	static void construirGrupos(const gerber::GerberResult &res, std::vector<std::vector<Info>> &gruposInfo, double tolConexion, double radioHerramienta);

	// -------------------------------------------------------------------------
	// Fronteras y encadenado
	// -------------------------------------------------------------------------

	/**
	 * @brief Extrae caminos de la frontera de una unión poligonal.
	 * @param formas Polígonos orientados CCW.
	 * @param prohibidas Polígonos de exclusión.
	 * @return Caminos abiertos o lazos con cierre explícito.
	 *
	 * @details
	 * Divide aristas en sus cruces, descarta fragmentos interiores
	 * y encadena los extremos compatibles.
	 */
	static std::vector<Poligono> fronteraUnion(const std::vector<Poligono> &formas, const std::vector<const Poligono*> &prohibidas);

	/**
	 * @brief Comprueba la pertenencia a una zona de exclusión.
	 * @param z Figura y curvas de referencia.
	 * @param p Punto consultado.
	 * @param dProh Separación respecto a la figura.
	 * @param eps Tolerancia de comparación.
	 * @return true si el punto se considera dentro de la zona.
	 *
	 * @pre Los punteros de z son válidos; para cápsulas, zona no está vacía.
	 */
	static bool dentroZona(const ProhFig &z, const gerber::Vec2 &p, double dProh, double eps);

	/**
	 * @brief Recorta curvas candidatas por distancias y encadena fragmentos.
	 * @param raw Curvas candidatas.
	 * @param figs Figuras referenciadas por RawForma::fig.
	 * @param d Distancia de la pasada.
	 * @param eps Tolerancia geométrica.
	 * @param proh Zonas de exclusión.
	 * @param dProh Separación de las zonas de exclusión.
	 * @param limite Caja opcional de recorte; nullptr desactiva el recorte.
	 * @return Caminos abiertos y lazos cerrados.
	 *
	 * @warning El límite rectangular no equivale al contorno real de la PCB.
	 */
	static std::vector<Poligono> fronteraUnionSD(const std::vector<RawForma> &raw, const std::vector<const Figura*> &figs, double d, double eps, const std::vector<ProhFig> &proh, double dProh,
			const Caja *limite = nullptr);

	/**
	 * @brief Une extremos cercanos de caminos abiertos.
	 * @param[in,out] caminos Caminos que se modificarán.
	 * @param tol Distancia máxima de unión.
	 * @param proh Zonas de exclusión consultadas.
	 * @param dProh Separación de exclusión.
	 * @param eps Tolerancia.
	 *
	 * @note La comprobación del puente utiliza muestras discretas.
	 */
	static void soldarCaminos(std::vector<Poligono> &caminos, double tol, const std::vector<ProhFig> &proh, double dProh, double eps);

	/**
	 * @brief Ordena caminos por cercanía y emite movimientos.
	 * @param[in,out] tramos Caminos pendientes.
	 * @param[in,out] movimientos Vector receptor de movimientos.
	 * @param[in,out] actual Posición XY actual de la herramienta.
	 * @param[in,out] abajo Estado de penetración de la herramienta.
	 * @param distPuente Longitud máxima para considerar un puente de corte.
	 * @param puenteSeguro Función que valida el puente propuesto.
	 *
	 * @details
	 * Puede rotar la entrada de los lazos e invertir caminos abiertos.
	 * Si no se autoriza un puente, utiliza subida, viaje y bajada.
	 * La retracción final corresponde al llamador.
	 */
	static void rutaTramos(std::vector<Tramo> &tramos, std::vector<MovimientoHerramienta> &movimientos, gerber::Vec2 &actual, bool &abajo, double distPuente,
			const std::function<bool(const gerber::Vec2&, const gerber::Vec2&)> &puenteSeguro);

	// -------------------------------------------------------------------------
	// Generación de trayectorias
	// -------------------------------------------------------------------------

	/**
	 * @brief Genera la trayectoria básica de contornos.
	 * @param res Geometría Gerber.
	 * @param cfg Configuración CNC.
	 * @param aislamiento Parámetro reservado por compatibilidad.
	 * @param huelgo Separación adicional al radio de herramienta.
	 * @return Movimientos de la trayectoria.
	 *
	 * @note Conserva el algoritmo básico existente, no el aislamiento por grupos.
	 */
	static std::vector<MovimientoHerramienta> construirTrayectoria(const gerber::GerberResult &res, const gerber::GCodeConfig &cfg, bool aislamiento, double huelgo);

	/**
	 * @brief Genera offsets de aislamiento por grupos de cobre.
	 * @param res Geometría Gerber.
	 * @param cfg Configuración CNC.
	 * @param huelgo Separación adicional al cobre.
	 * @param maxPasadas Número solicitado de pasadas; se fuerza al menos una.
	 * @return Movimientos de aislamiento.
	 *
	 * @details
	 * Extrae figuras, las agrupa, obtiene fronteras recortadas contra
	 * otros grupos y ordena globalmente los caminos resultantes.
	 */
	static std::vector<MovimientoHerramienta>
	construirTrayectoriaAislamiento(const gerber::GerberResult &res, const gerber::GCodeConfig &cfg, double huelgo, int maxPasadas);

	/**
	 * @brief Genera pasadas exteriores para limpiar la zona sin cobre.
	 * @param res Geometría Gerber.
	 * @param cfg Configuración CNC.
	 * @param huelgo Separación adicional al cobre.
	 * @param maxPasadas Número máximo solicitado de pasadas.
	 * @return Movimientos de limpieza.
	 *
	 * @details
	 * Trabaja contra todas las figuras de cobre simultáneamente y recorta
	 * las curvas mediante una caja de trabajo.
	 *
	 * @warning
	 * La envolvente Gerber usada como límite no representa necesariamente
	 * el borde físico de la placa.
	 */
	static std::vector<MovimientoHerramienta> construirLimpieza(const gerber::GerberResult &res, const gerber::GCodeConfig &cfg, double huelgo, int maxPasadas);

	/**
	 * @brief Selecciona limpieza exterior o perfilado interior de islas.
	 * @param res Geometría Gerber.
	 * @param cfg Configuración CNC.
	 * @param huelgo Separación adicional de la primera pasada.
	 * @param maxPasadas Número máximo solicitado de pasadas.
	 * @param soloIslas Selección del algoritmo.
	 * @return Movimientos generados.
	 *
	 * @details
	 * - false: delega en construirLimpieza(), sobre la zona sin cobre.
	 * - true: selecciona pads y regiones y genera pasadas interiores
	 *   sobre los contornos unidos de las figuras seleccionadas.
	 *
	 * @warning
	 * El modo interior retira material del cobre seleccionado.
	 * No equivale al aislamiento exterior para conservar ese cobre.
	 */
	static std::vector<MovimientoHerramienta>
	construirTrayectoriaPerfilado(const gerber::GerberResult &res, const gerber::GCodeConfig &cfg, double huelgo, int maxPasadas, bool soloIslas);

	// -------------------------------------------------------------------------
	// Generación de GCode
	// -------------------------------------------------------------------------

	/**
	 * @brief Genera GCode de taladrado a partir del resultado Excellon.
	 * @param resTal Operaciones de taladrado.
	 * @param cfg Configuración de alturas y avances.
	 * @return Programa generado o mensaje de error.
	 *
	 * @details
	 * La implementación agrupa operaciones por diámetro y emite
	 * posicionamiento, penetración y retracción.
	 */
	static std::string generarGCodeTaladro(const drill::DrillResult &resTal, const gerber::GCodeConfig &cfg);

	/**
	 * @brief Genera GCode a partir de una secuencia de movimientos.
	 * @param movs Trayectoria que se exportará.
	 * @param cfg Configuración CNC.
	 * @return Programa generado según la implementación del exportador.
	 *
	 * @note Esta declaración requiere su definición correspondiente en el .cpp.
	 */
	static std::string generarGCodeDeTrayectoria(const std::vector<MovimientoHerramienta> &movs, const gerber::GCodeConfig &cfg);
};

#endif /* MECANIZADO_H_ */
