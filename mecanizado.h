/*
 * mecanizado.h
 *
 *  Created on: 14 may 2026
 *      Author: DjSteker
 */

/**
 * @file    mecanizado.h
 * @brief   Generación del recorrido CNC (toolpath) a partir de la geometría Gerber.
 *
 * @author  DjSteker
 * @date    14 may 2026
 * @version 2.0
 *
 * @details Este módulo es el puente entre el mundo "geometría" y el mundo
 *          "máquina": convierte el resultado de parsear un archivo Gerber
 *          RS-274X (pistas, regiones y flashes de cobre) o un archivo de
 *          taladro Excellon en un recorrido de herramienta que una CNC puede
 *          ejecutar.
 *
 * @par     Proceso general del aislamiento (v2)
 *          1. El parser Gerber (@c Procesos/gerber.h) entrega un
 *             @c gerber::GerberResult con las primitivas de cobre.
 *          2. @c construirGrupos() convierte cada primitiva en un polígono
 *             (cápsula para segmentos y arcos, contorno para flashes y
 *             regiones) y agrupa en "pistas" las figuras cercanas entre sí.
 *          3. Por cada pista y cada pasada se calcula el offset de cada
 *             figura y se obtiene la frontera exterior de la UNIÓN de esas
 *             figuras: se cortan los bordes en sus intersecciones exactas y
 *             se descartan los trozos que quedan dentro de otra figura de la
 *             misma pista o dentro de la zona prohibida de otra pista.
 *          4. Los trozos exteriores se encadenan en caminos continuos
 *             (lazos cerrados o tramos abiertos) y cada camino se fresa de
 *             una vez.
 *          5. Entre caminos la fresa sube y viaja en línea recta: con Z
 *             arriba no hace falta esquivar cobre, por lo que no existe
 *             lógica de centroide ni de tramos seguros.
 *
 * @par     Quién lo utiliza
 *          - @c Nucleo/interfaz.cpp: guarda la trayectoria en
 *            @c EstadoApp::trayectoria, la recalcula al pulsar "Recorrido"
 *            o "Aislamiento Correcto" y la dibuja en el visor Cairo.
 *          - @c Interfaces/ui_gcode.cpp: usa los mismos tipos para
 *            previsualizar y exportar el GCode de aislamiento y taladro.
 *
 * @par     Unidades
 *          Las coordenadas están en las mismas unidades del archivo Gerber
 *          (mm o pulgadas, según @c GerberResult::unit). Los parámetros de
 *          @c GCodeConfig (diámetro de fresa, avances, Z seguro) se
 *          interpretan en esas mismas unidades.
 *
 * @note    Dependencias: solo biblioteca estándar de C++17 más los módulos
 *          internos del proyecto (@c Procesos/gerber.h y @c Procesos/drill.h).
 *          No utiliza bibliotecas externas.
 */

#ifndef MECANIZADO_H_
#define MECANIZADO_H_

#include <string>
#include <utility>
#include <vector>

#include "Procesos/drill.h"
#include "Procesos/gerber.h"

// ─────────────────────────────────────────────────────────────
// Tipos del recorrido
// ─────────────────────────────────────────────────────────────

/**
 * @enum   TipoMovimiento
 * @brief  Clase de movimiento de la herramienta dentro del recorrido CNC.
 *
 * Cada elemento de la trayectoria es un desplazamiento entre dos puntos
 * (@c desde → @c hasta) clasificado con este tipo. La interfaz usa el tipo
 * para elegir el color al dibujar (viajes vs cortes) y el generador de
 * GCode para emitir G00/G01 o las subidas/bajadas de eje Z.
 */
enum class TipoMovimiento {
	Viaje,  ///< Desplazamiento rápido (G00) con la herramienta levantada: no mecaniza.
	Corte,  ///< Desplazamiento de mecanizado (G01) con la fresa penetrada en el material.
	Subir,  ///< Retracción de la herramienta: sube el eje Z hasta la altura segura (safe Z).
	Bajar   ///< Penetración de la herramienta: baja el eje Z hasta la profundidad de corte.
};

/**
 * @struct MovimientoHerramienta
 * @brief  Un paso elemental del recorrido CNC.
 *
 * Representa un desplazamiento de la herramienta desde un punto hasta otro,
 * junto con la acción que se realiza (viajar, cortar, subir o bajar). La
 * lista completa de movimientos conforma el toolpath que se dibuja en el
 * visor y del que se genera el GCode final.
 */
struct MovimientoHerramienta {
	TipoMovimiento tipo;   ///< Acción realizada en este paso (viaje, corte, subir, bajar).
	gerber::Vec2 desde;    ///< Punto de inicio del movimiento (posición actual de la fresa).
	gerber::Vec2 hasta;    ///< Punto de destino (o el mismo punto si solo cambia Z).
};

// ─────────────────────────────────────────────────────────────
// Geometría de la unión de formas
// ─────────────────────────────────────────────────────────────

/**
 * @brief   Polígono como secuencia de vértices en coordenadas de placa.
 *
 * @details Es el tipo con el que trabajan la unión de formas y la frontera
 *          exterior. Los polígonos de entrada de @c fronteraUnion deben
 *          estar en sentido antihorario (CCW) y cerrados de forma implícita
 *          (el último vértice NO repite al primero; el cierre es virtual).
 */
using Poligono = std::vector<gerber::Vec2>;

/**
 * @struct Caja
 * @brief  Caja envolvente alineada a los ejes de un @c Poligono.
 *
 * @details Se usa como filtro rápido antes de hacer pruebas costosas:
 *          si dos cajas no solapan, los polígonos que representan no
 *          pueden intersecarse ni contenerse, así que se saltan sin más
 *          cálculo. La construye @c cajaDe() y la consultan
 *          @c cajasSolapan() y @c puntoEnCaja().
 */
struct Caja {
	double x0;  ///< Extremo mínimo en X.
	double y0;  ///< Extremo mínimo en Y.
	double x1;  ///< Extremo máximo en X.
	double y1;  ///< Extremo máximo en Y.
};

/**
 * @brief   Calcula la caja envolvente de un polígono.
 * @param   p Polígono de entrada (puede estar vacío; el resultado no
 *            tiene sentido en ese caso).
 * @return  Caja mínima que contiene todos los vértices de @p p.
 */
Caja cajaDe(const Poligono &p);

/**
 * @brief   Comprueba si dos cajas envolventes se solapan.
 * @details Solape estricto por comparación de extremos; el contacto por un
 *          borde cuenta como solape.
 * @param   a Primera caja.
 * @param   b Segunda caja.
 * @return  @c true si comparten alguna zona (o borde) del plano.
 */
bool cajasSolapan(const Caja &a, const Caja &b);

/**
 * @brief   Comprueba si un punto cae dentro de una caja.
 * @param   c Caja de referencia.
 * @param   p Punto a comprobar (los bordes cuentan como dentro).
 * @return  @c true si @p p está dentro o en el borde de @p c.
 */
bool puntoEnCaja(const Caja &c, const gerber::Vec2 &p);

/**
 * @brief   Intersección entre los segmentos ab y cd.
 * @details Resuelve el sistema 2x2 de las dos rectas. Solo acepta
 *          cruces transversales: el parámetro @c t sobre ab debe estar
 *          estrictamente entre 0 y 1 (tolerancia 1e-9), y @c u sobre cd
 *          entre 0 y 1. Así se evitan dobles conteos cuando dos bordes
 *          comparten un vértice exacto.
 * @param   a Inicio del primer segmento.
 * @param   b Fin del primer segmento.
 * @param   c Inicio del segundo segmento.
 * @param   d Fin del segundo segmento.
 * @param   t Salida: parámetro del cruce sobre ab (solo válido si
 *            devuelve @c true).
 * @return  @c true si los segmentos se cruzan en un punto interior.
 */
bool interseccionSegmentos(const gerber::Vec2 &a, const gerber::Vec2 &b, const gerber::Vec2 &c, const gerber::Vec2 &d, double &t);

/**
 * @brief   Frontera exterior de la unión de varias formas.
 *
 * @details Es el corazón del aislamiento v2. Aprovecha que
 *          @c offset(unión) == unión(offsets) y trabaja en tres fases:
 *          - Troceado: cada borde de cada forma se corta en todos sus
 *            puntos de intersección con los bordes de las demás formas y
 *            con los de las zonas prohibidas. De cada subtramo se toma el
 *            punto medio y se descarta si cae dentro de otra forma de la
 *            misma pista o dentro de una zona prohibida.
 *          - Encadenado: los subtramos que quedan se unen por sus
 *            extremos (tabla hash de celdas de 1e-5 con búsqueda en las 9
 *            celdas vecinas, tolerancia 1e-6) respetando el sentido CCW.
 *          - Clasificación: se generan primero los caminos abiertos
 *            (los que empiezan en un extremo sin sucesor, típicamente
 *            cortados por zonas prohibidas) y después los lazos cerrados,
 *            donde el último vértice se hace igual al primero.
 *
 * @param   formas      Polígonos CCW de la misma pista ya expandidos por
 *                      el offset de la pasada actual.
 * @param   prohibidas  Zonas por las que la fresa no puede pasar: offsets
 *                      base de las figuras de OTRAS pistas. Pueden venir
 *                      vacías.
 * @return  Caminos continuos. Cada camino es una polilínea a recorrer de
 *          un tirón; los cerrados cumplen @c front() == @c back().
 */
std::vector<Poligono> fronteraUnion(const std::vector<Poligono> &formas, const std::vector<const Poligono*> &prohibidas);

// ─────────────────────────────────────────────────────────────
// Agrupado de primitivas en pistas
// ─────────────────────────────────────────────────────────────

/**
 * @struct Info
 * @brief  Figura de cobre ya convertida a polígono, con su clasificación.
 *
 * @details Es la unidad de trabajo de @c construirGrupos() y de
 *          @c construirTrayectoriaAislamiento(). Cada primitiva Dark del
 *          Gerber produce una @c Info:
 *          - Segment: cápsula del trazo con su anchura real.
 *          - Arc: cadena de cápsulas, una por tramo de la polilínea del
 *            arco (así las pistas curvas se tratan igual que las rectas).
 *          - Flash: polígono de su apertura.
 *          - Region: contorno tal cual; si su área supera el umbral se
 *            marca como isla y no se mecaniza.
 */
struct Info {
	std::vector<gerber::Vec2> poly;  ///< Polígono de la figura (orientación cualquiera; se normaliza a CCW al usarla).
	int idx;                         ///< Índice de la primitiva original en @c GerberResult::primitives (-1 para figuras sintéticas como las cápsulas de arcos).
	bool esActivo;                   ///< @c true si la figura se mecaniza (las islas grandes y las degeneradas son @c false).
	bool esIsla;                     ///< @c true si es una región grande que solo actúa como referencia, sin generar corte.
};

/**
 * @brief   Convierte las primitivas Gerber en figuras y las agrupa en pistas.
 *
 * @details Proceso:
 *          - Recorre las primitivas de polaridad Dark y genera una o
 *            varias @c Info por cada una (los arcos generan una cápsula
 *            por tramo de su polilínea).
 *          - Aplica Union-Find: dos figuras se funden en la misma pista
 *            si algún par de vértices (muestreados cada 2) está a una
 *            distancia menor que @c 2*tolConexion.
 *          - Descarta los grupos que no contengan ninguna figura activa.
 *
 * @param   res               Resultado del parser Gerber.
 * @param   gruposInfo        Salida: un grupo (pista) por entrada, cada
 *                            uno con sus figuras.
 * @param   tolConexion       Distancia base de conexión entre figuras.
 * @param   radioHerramienta  Radio de la fresa; solo se usa para el
 *                            umbral de área que separa islas de regiones
 *                            mecanizables.
 */
void construirGrupos(const gerber::GerberResult &res, std::vector<std::vector<Info>> &gruposInfo, double tolConexion, double radioHerramienta);

// ─────────────────────────────────────────────────────────────
// Construcción de trayectorias
// ─────────────────────────────────────────────────────────────

/**
 * @brief   Construye el recorrido CNC con un offset simple (una sola pasada).
 *
 * @details Modo básico "contorno": convierte cada primitiva de cobre en
 *          polígono (cápsula, flash o región), la expande con un offset de
 *          @c radioHerramienta + huelgo y recorre el contorno. Cuando un
 *          tramo cae dentro del cobre (zona protegida) levanta la
 *          herramienta y viaja en rápido hasta poder volver a cortar.
 *          Termina con un viaje de regreso al origen (0, 0).
 *
 * @param   res         Resultado del parser Gerber con la geometría de cobre.
 * @param   cfg         Configuración CNC (diámetro de fresa, avances, Z seguro).
 * @param   aislamiento Parámetro reservado por compatibilidad con la
 *                      interfaz; no altera el comportamiento.
 * @param   huelgo      Separación extra (en unidades de la placa) que se
 *                      suma al radio de la herramienta al calcular el offset.
 * @return  Lista de movimientos. Vacía si @p res no es válido o no hay
 *          geometría de cobre.
 *
 * @note    La llama @c Nucleo/interfaz.cpp cuando el modo aislamiento está
 *          desactivado.
 */
std::vector<MovimientoHerramienta> construirTrayectoria(const gerber::GerberResult &res, const gerber::GCodeConfig &cfg, bool aislamiento, double huelgo);

/**
 * @brief   Construye el recorrido CNC de aislamiento (multipasada).
 *
 * @details Estrategia principal para aislar pistas de una PCB, basada en
 *          la identidad @c offset(unión) == unión(offsets):
 *          - @c construirGrupos() entrega las figuras de cada pista.
 *          - De cada figura activa se calcula la zona prohibida: su
 *            polígono CCW expandido con @c radioHerramienta + huelgo.
 *          - Por cada pasada (0 a @p maxPasadas - 1) y cada pista:
 *              1. Se expande cada figura de la pista a la distancia
 *                 @c distBase + pasada * pasoPasada
 *                 (paso = 45 % del diámetro de la fresa).
 *              2. @c fronteraUnion() devuelve los caminos continuos de la
 *                 frontera exterior de la unión, ya recortados contra las
 *                 zonas prohibidas de las demás pistas.
 *              3. Se mecaniza cada camino de una vez (lazos cerrados se
 *                 rotan para empezar por el vértice más cercano; tramos
 *                 abiertos se invierten si conviene), eligiendo siempre
 *                 el camino pendiente más próximo a la posición actual.
 *          - Entre caminos la fresa sube y viaja en línea recta: con Z
 *            arriba no hay nada que esquivar, así que no existe lógica de
 *            centroide ni de tramos seguros.
 *          - Al terminar sube la herramienta y regresa al origen (0, 0).
 *
 * @param   res         Resultado del parser Gerber con la geometría de cobre.
 * @param   cfg         Configuración CNC (diámetro de fresa, avances, Z seguro).
 * @param   huelgo      Separación extra entre la fresa y el cobre, en
 *                      unidades de la placa.
 * @param   maxPasadas  Número de contornos concéntricos que se mecanizan
 *                      alrededor de cada pista (mínimo 1).
 * @return  Lista de movimientos. Vacía si @p res no es válido o no hay
 *          geometría de cobre.
 *
 * @note    La usan @c Nucleo/interfaz.cpp (botones "Recorrido" y
 *          "Aislamiento Correcto", y el recálculo automático al cambiar la
 *          configuración) y @c Interfaces/ui_gcode.cpp para previsualizar
 *          el aislamiento antes de exportar el GCode.
 */
std::vector<MovimientoHerramienta> construirTrayectoriaAislamiento(const gerber::GerberResult &res, const gerber::GCodeConfig &cfg, double huelgo, int maxPasadas);

/**
 * @brief   Construye el recorrido CNC de perfilado interior del cobre.
 *
 * @details Estrategia "hacia dentro": para cada grupo de cobre calcula el
 *          contorno de la unión de sus figuras (con sus huecos Clear) y
 *          genera pasadas concéntricas que se alejan del borde hacia el
 *          interior, recortadas por distancia: nunca se sale del cobre ni
 *          cruza entre figuras solapadas. Comparte con el aislamiento la
 *          extraccion de fronteras (fronteraUnionSD) y el orden global de
 *          fresa (rutaTramos), con puentes solo si quedan dentro del cobre.
 *
 * @param   res         Resultado del parser Gerber con la geometría de cobre.
 * @param   cfg         Configuración CNC (diámetro de fresa, avances, Z seguro).
 * @param   huelgo      Separación de la primera pasada al borde del cobre, en
 *                      unidades de la placa.
 * @param   maxPasadas  Número de pasadas interiores (mínimo 1). El bucle se
 *                      detiene antes si ya no cabe la fresa.
 * @param   soloIslas   @c true: solo pads, masas y regiones (el cobre que no
 *                      es pista); @c false: todo el cobre (pistas incluidas).
 * @return  Lista de movimientos. Vacía si @p res no es válido o no hay cobre.
 *
 * @note    La usan "Perfilar Cobre" (con @c soloIslas = false) y
 *          "Perfilar Islas" (con @c soloIslas = true) en
 *          @c Nucleo/interfaz.cpp, y el módulo hilado de GCode
 *          @c Nucleo/interfaz_gcode.cpp.
 */
std::vector<MovimientoHerramienta> construirTrayectoriaPerfilado(const gerber::GerberResult &res, const gerber::GCodeConfig &cfg, double huelgo, int maxPasadas, bool soloIslas);

// ─────────────────────────────────────────────────────────────
// GCode de taladrado
// ─────────────────────────────────────────────────────────────

/**
 * @brief   Genera el GCode de taladrado a partir de un archivo Excellon.
 *
 * @details Traduce las operaciones de taladro del @c drill::DrillResult a
 *          un programa GCode listo para la CNC:
 *          - Cabecera: unidades (G21/G20), plano XY (G17), cotas
 *            absolutas (G90), avance por minuto (G94), subida a Z seguro
 *            y arranque de husillo (M03).
 *          - Agrupa los taladros por diámetro: por cada grupo emite el
 *            cambio de herramienta (M06 T*n*) y la secuencia por agujero:
 *            posicionamiento en rápido (G00 X Y), bajada a Z seguro,
 *            penetración a la profundidad de corte (G01 Z con el avance
 *            de penetración) y retroceso a Z seguro.
 *          - Cierre: regreso al origen, parada de husillo (M05) y fin de
 *            programa (M30).
 *
 * @param   resTal  Resultado del parser de taladro (archivo .drl).
 * @param   cfg     Configuración CNC: aporta @c safeZ, @c cutZ y
 *                  @c plungeRate.
 * @return  Cadena con el programa GCode completo. Si el parseo falló o no
 *          hay operaciones, devuelve un mensaje de error terminado en M30.
 *
 * @note    La llama @c Nucleo/interfaz.cpp al exportar el GCode de taladro.
 */
std::string generarGCodeTaladro(const drill::DrillResult &resTal, const gerber::GCodeConfig &cfg);

/**
 * @brief   Genera el programa GCode a partir del recorrido calculado.
 * @details Traduce exactamente la lista de @c MovimientoHerramienta que la
 *          interfaz dibuja: @c Subir emite G00 Z a la altura segura,
 *          @c Viaje un G00 X Y, @c Bajar un G01 Z a la profundidad de corte
 *          (@c traceZ si es negativo, si no @c cutZ) con el avance de
 *          penetración, y @c Corte un G01 X Y con el avance de corte.
 *          Termina con subida, retorno a (0, 0), M05 y M30.
 * @param   movs Recorrido calculado (por ejemplo @c EstadoApp::trayectoria).
 * @param   cfg  Configuración CNC.
 * @return  Programa GCode completo; cadena con error si no hay recorrido.
 * @note    La usan "Ver GCode" y "Guardar GCode" cuando ya hay recorrido
 *          calculado, garantizando que el fichero coincide con el visor.
 */
std::string generarGCodeDeTrayectoria(const std::vector<MovimientoHerramienta> &movs, const gerber::GCodeConfig &cfg);

#endif /* MECANIZADO_H_ */
