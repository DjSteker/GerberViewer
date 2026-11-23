/*
 * Procesos/IslasCobre.h
 *
 *  Created on: 16 nov 2026
 *      Author: DjSteker
 */

/**
 * @file    IslasCobre.h
 * @brief   Detección y perfilado de cobre que NO es pista (masas, pads, islas).
 *
 * @details Este módulo cubre el cobre que los módulos de aislamiento actuales
 *          no tratan:
 *          - Primitivas @c Region (G36/G37): masas/rellenos de cobre,
 *            incluyendo sus agujeros interiores (@c Region::holes).
 *          - Primitivas @c Flash: pads y vías.
 *          - Las figuras anteriores se funden en "islas" cuando se tocan
 *            (vértice dentro del otro o a menos de @c tolConexion), usando
 *            Union-Find.
 *
 *          Los @c Segment y @c Arc se consideran pistas y se ignoran: para
 *          ellas existen @c Mecanizado y @c ToolpathGenerator.
 *
 * @par     Estrategia de mecanizado (generarTrayectoria)
 *          - Contornos de isla: pasadas concéntricas HACIA ADENTRO
 *            (offset negativo), primera pasada a @c toolDiameter/2 +
 *            @c clearance del borde y siguientes a @c stepover.
 *          - Agujeros de masa: anillos concéntricos ALREDEDOR del hueco
 *            (offset positivo desde el borde del agujero), primera pasada
 *            a radio de herramienta + clearance y siguientes hacia el
 *            interior del cobre. Cada anillo se valida: todos sus puntos
 *            deben quedar dentro del cobre de la isla y fuera de TODOS los
 *            agujeros; si falla, se detienen las pasadas de ese contorno.
 *          - Entre anillos la herramienta viaja en rápido; al final retorno
 *            a (0, 0).
 *
 * @note    Unidades: las mismas del archivo Gerber (mm o pulgadas).
 * @note    Sin dependencias externas: solo STL de C++17.
 *
 * @author  DjSteker
 * @date    16 nov 2026
 * @version 1.0.0
 */

#ifndef PROCESOS_ISLASCOBRE_H_
#define PROCESOS_ISLASCOBRE_H_

#include <vector>

#include "gerber.h"

namespace islas {

/**
 * @struct MovimientoIslas
 * @brief  Movimiento elemental producido por @ref DetectorIslas.
 *
 * @details Equivalente funcional a @c ToolpathMove, definido aquí para que
 *          este módulo no dependa de @c ToolpathGenerator ni de
 *          @c verificacion_segura.h. La interfaz lo convierte a
 *          @c MovimientoHerramienta (rapid → Viaje, resto → Corte).
 */
struct MovimientoIslas {
	bool viaje;             ///< @brief @c true = desplazamiento rápido (G00); @c false = corte (G01).
	gerber::Vec2 desde;     ///< @brief Punto de partida del tramo.
	gerber::Vec2 hasta;     ///< @brief Punto de llegada del tramo.
};

/**
 * @struct IslaCobre
 * @brief  Isla de cobre detectada: cobre masivo/pads conectados entre sí.
 */
struct IslaCobre {
	/** @brief Contornos exteriores de cada figura que compone la isla (CCW). */
	std::vector<std::vector<gerber::Vec2>> contornos;

	/** @brief Agujeros interiores (@c Region::holes) de las figuras de la isla. */
	std::vector<std::vector<gerber::Vec2>> agujeros;

	/** @brief Área aproximada de la isla (suma de contornos menos agujeros). */
	double area = 0.0;
};

/**
 * @struct ConfigIslas
 * @brief  Parámetros de detección y perfilado de islas.
 *
 * @note   Todos los valores en las unidades del archivo Gerber (típicamente mm).
 */
struct ConfigIslas {
	/** @brief Diámetro de la fresa. Determina el radio de la primera pasada. */
	double toolDiameter = 0.1;

	/** @brief Holgura extra de seguridad respecto al borde del cobre. */
	double clearance = 0.0;

	/** @brief Paso entre pasadas como fracción del diámetro (0.35 = 35 %). */
	double stepoverFactor = 0.35;

	/** @brief Número máximo de pasadas por contorno/agujero. */
	int maxPasadas = 15;

	/** @brief Distancia máxima entre figuras para considerarlas la misma isla. */
	double tolConexion = 0.01;

	/** @brief Figuras con área menor se descartan (ruido). */
	double areaMinima = 0.01;
};

/**
 * @class DetectorIslas
 * @brief  Detecta islas de cobre no-pista y genera su trayectoria de perfilado.
 *
 * Ejemplo:
 * @code
 *   islas::ConfigIslas cfg;
 *   cfg.toolDiameter = 0.2;
 *   cfg.clearance    = 0.05;
 *   islas::DetectorIslas detector;
 *   auto movs = detector.generarTrayectoria(resultado, cfg);
 * @endcode
 */
class DetectorIslas {
public:
	/**
	 * @brief   Detecta las islas de cobre (Flash + Region Dark conectados).
	 * @param   resultado Resultado del parser Gerber (debe tener @c ok == true).
	 * @param   cfg       Configuración (usará @c tolConexion y @c areaMinima).
	 * @return  Lista de islas; vacía si no hay cobre no-pista.
	 */
	std::vector<IslaCobre> detectar(const gerber::GerberResult &resultado, const ConfigIslas &cfg);

	/**
	 * @brief   Genera la trayectoria de perfilado de todas las islas.
	 * @details Para cada isla: pasadas interiores en sus contornos y anillos
	 *          alrededor de sus agujeros, con validación de seguridad propia
	 *          (punto dentro del cobre de la isla y fuera de sus agujeros).
	 *          Termina con un rápido de retorno a (0, 0).
	 * @param   resultado Resultado del parser Gerber.
	 * @param   cfg       Configuración de mecanizado.
	 * @return  Secuencia de movimientos (rápidos y cortes) lista para convertir
	 *          a @c MovimientoHerramienta.
	 */
	std::vector<MovimientoIslas> generarTrayectoria(const gerber::GerberResult &resultado, const ConfigIslas &cfg);
};

}  // namespace islas

#endif /* PROCESOS_ISLASCOBRE_H_ */
