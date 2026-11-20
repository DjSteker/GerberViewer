/*
 * Nucleo/interfaz_gcode.hpp
 *

 */

/*
 * Nucleo/interfaz_gcode.hpp
 *
 *  Created on: 20 nov 2026
 *      Author: usuario001
 *
 *  Exportación de GCode desde la trayectoria visible, botones de perfilado
 *  y cálculo de trayectorias en segundo plano (la interfaz no se congela).
 */

#ifndef NUCLEO_INTERFAZ_GCODE_HPP_
#define NUCLEO_INTERFAZ_GCODE_HPP_

#include <gtk/gtk.h>

/// Qué trayectoria calcular.
enum class ModoCalculo {
	Seleccion,             ///< Según la casilla "Enrutamiento de aislamiento" (aislamiento o contorno)
	PerfilCobre,           ///< Botón "Perfilar Cobre"
	Islas,                 ///< Botón "Perfilar Islas"
	AislamientoCorregido   ///< Botón "Aislamiento Correcto"
};

/// Calcula en un hilo y, al terminar, actualiza estado->trayectoria y redibuja.
void lanzarCalculo(ModoCalculo modo);

/// Recalcula 300 ms después del último cambio de parámetros (si hay una capa de recorrido visible).
void solicitarRecalculo();

/// Marco del panel lateral con origen, espejo, tolerancia, husillo y taladrado por pasos.
GtkWidget* construirPanelExportacion();

// Callbacks de los botones de la cabecera (todos GtkButton*: señal "clicked")
void alPerfilarCobre(GtkButton*, gpointer);
void alPerfilarIslas(GtkButton*, gpointer);
void alAislamientoCorregido(GtkButton*, gpointer);
void alGuardarGCode(GtkButton*, gpointer);
void alPrevisualizarGCode(GtkButton*, gpointer);
void alGenerarGCodeTaladro(GtkButton*, gpointer);

#endif /* NUCLEO_INTERFAZ_GCODE_HPP_ */
