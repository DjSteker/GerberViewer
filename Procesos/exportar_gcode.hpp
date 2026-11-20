/*
 * Procesos/exportar_gcode.hpp
 *
 *  Created on: 20 nov 2026
 *      Author: usuario001
 *
 *  Exportación a GCode de la trayectoria que se ve en pantalla
 *  (MovimientoHerramienta) y del taladrado, con origen en la esquina de la
 *  placa, espejo en X, simplificación y arcos G02/G03.
 */

#ifndef PROCESOS_EXPORTAR_GCODE_HPP_
#define PROCESOS_EXPORTAR_GCODE_HPP_

#include <string>
#include <vector>

#include "drill.h"
#include "gerber.h"
#include "../mecanizado.h"

/// Cambio de coordenadas de la placa al sistema de la máquina.
struct Transformacion {
	double ox = 0.0;          ///< Esquina que pasa a ser X0
	double oy = 0.0;          ///< Esquina que pasa a ser Y0
	bool espejoX = false;     ///< Espejo en X (cara inferior)
	double ancho = 0.0;       ///< Ancho de la placa (eje del espejo)

	gerber::Vec2 aplicar(const gerber::Vec2 &p) const {
		double x = p.x - ox;
		const double y = p.y - oy;
		if (espejoX) {
			x = ancho - x;
		}
		return {x, y};
	}
};

struct OpcionesGCode {
	Transformacion tr;
	double zSeguro = 2.0;       ///< mm
	double zCorte = -0.1;       ///< mm (profundidad de corte; negativo = hacia abajo)
	double avance = 50.0;       ///< mm/min
	double penetracion = 20.0;  ///< mm/min
	int husillo = 10000;        ///< S
	bool metrico = true;        ///< false: G20 y todo escalado a pulgadas
	int decimales = 4;
	bool usarArcos = true;      ///< ajustar G02/G03 a los tramos curvos
	double tolerancia = 0.002;  ///< mm: desviación máxima al simplificar / ajustar arcos
	bool comentarios = true;
	std::string titulo = "Aislamiento";
};

/// Estadísticas de la última exportación (para la barra de estado).
struct EstadisticasGCode {
	size_t lineas = 0;
	size_t arcos = 0;
	double largoCorte = 0.0;   ///< mm
};

std::string generarGCodeTrayectoria(const std::vector<MovimientoHerramienta> &mov, const OpcionesGCode &op, EstadisticasGCode *est = nullptr);

/// Taladrado: agrupa por broca, ordena por cercanía, ranuras y taladrado por pasos.
/// @param peck  Profundidad de cada paso (mm); 0 = un solo paso.
std::string generarGCodeTaladroT(const drill::DrillResult &res, const OpcionesGCode &op, double peck);

#endif /* PROCESOS_EXPORTAR_GCODE_HPP_ */
