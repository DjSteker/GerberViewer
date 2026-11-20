/*
 * interfaz.h
 *
 *  Created on: 14 may 2026
 *      Author: DjSteker
 */

/**
 * @file    interfaz.h
 * @brief   Estado global, renderizado y construcción de la interfaz GTK4.
 *
 * @details Es el corazón de la aplicación: define @c EstadoApp (el
 *          estado global compartido) y declara las funciones que
 *          construyen la interfaz y la mantienen sincronizada con
 *          los datos (Gerber cargado, taladros, trayectoria CNC y
 *          configuración GCode).
 *
 * @par     Quién lo utiliza
 *          - @c Nucleo/main.cpp: define el puntero global @c estado
 *            (apunta a un @c EstadoApp local de @c main) y arranca la
 *            aplicación llamando a @c construirUI().
 *          - @c Nucleo/interfaz.cpp: implementa todas las funciones
 *            de esta cabecera (callbacks de botones, dibujo Cairo,
 *            panel de información y configuración GCode).
 *
 * @note    Todo el estado vive en un único objeto global: la
 *          aplicación no es segura para múltiples hilos que toquen
 *          la interfaz a la vez.
 *
 * @author  DjSteker
 * @date    14 may 2026
 */

#ifndef NUCLEO_INTERFAZ_H_
#define NUCLEO_INTERFAZ_H_

#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "mecanizado.h"
#include "Procesos/drill.h"
#include "Procesos/gerber.h"

/**
 * @struct EstadoApp
 * @brief  Estado global de la aplicación: datos, vista e interfaz.
 *
 * @details Centraliza todo lo que la aplicación necesita mientras
 *          está abierta, organizado en bloques:
 *          - Datos cargados: resultado del parseo Gerber y del
 *            archivo de taladro.
 *          - Trayectoria CNC calculada (@c MovimientoHerramienta).
 *          - Estado de la vista: zoom, desplazamiento y capas
 *            visibles.
 *          - Widgets GTK de la ventana: área de dibujo, barra de
 *            estado, etiquetas de información y controles de
 *            configuración GCode.
 *
 * @note    Los punteros a widgets son @c nullptr hasta que
 *          @c construirUI() crea la interfaz.
 */

struct EstadoApp {

	// ── Datos: archivo Gerber ──────────────────────────────────────
	gerber::GerberResult resultado;   ///< @brief Primitivas del Gerber cargado.
	bool tieneArchivo = false;        ///< @brief @c true si hay un Gerber cargado.
	std::string rutaArchivo;           ///< @brief Ruta del archivo Gerber abierto.

	// ── Trayectoria CNC ────────────────────────────────────────────
	std::vector<MovimientoHerramienta> trayectoria;  ///< @brief Recorrido CNC calculado (vacío si no hay).
	bool trayectoriaSucia = true;      ///< @brief @c true = la trayectoria debe recalcularse (se limpia al recalcular).

	bool mostrarTrayectoriaGerber = false;    ///< @brief Mostrar el recorrido derivado del GCode Gerber.
	bool mostrarTrayectoriaTaladro = false;    ///< @brief Mostrar el recorrido de taladro.

	// ── Vista (zoom y paneo) ───────────────────────────────────────
	double zoom = 1.0;                 ///< @brief Factor de zoom actual del visor.
	double desplX = 0.0;               ///< @brief Desplazamiento horizontal de la vista (paneo).
	double desplY = 0.0;               ///< @brief Desplazamiento vertical de la vista (paneo).
	double origenArrastreX = 0.0;      ///< @brief Origen X del arrastre en curso (inicio del paneo).
	double origenArrastreY = 0.0;      ///< @brief Origen Y del arrastre en curso (inicio del paneo).

	// ── Capas visibles ─────────────────────────────────────────────
	bool mostrarGerber = true;         ///< @brief Dibujar la capa Gerber (cobre/huecos).
	bool mostrarTrayectoria = false;   ///< @brief Dibujar la trayectoria CNC calculada.
	bool mostrarLimites = true;        ///< @brief Dibujar los límites de la placa.
	bool mostrarCuadricula = false;    ///< @brief Dibujar la cuadrícula de fondo.

	// ── Configuración GCode ─────────────────────────────────────────
	gerber::GCodeConfig configGCode;   ///< @brief Última configuración leída de los controles.

	// ── Ventana principal ───────────────────────────────────────────
	GtkApplicationWindow *ventanaPrincipal = nullptr; ///< @brief Ventana raíz de la aplicación.
	GtkWidget *areaDibujo = nullptr;                  ///< @brief Área de dibujo Cairo (visor Gerber/CNC).
	GtkWidget *barraEstado = nullptr;                 ///< @brief Barra de estado inferior (@c establecerEstado escribe aquí).
	GtkWidget *cuaderno = nullptr;                    ///< @brief Notebook con las pestañas laterales.

	// ── Etiquetas del panel de información ──────────────────────────
	GtkWidget *etqArchivo = nullptr;       ///< @brief Etiqueta: nombre del archivo cargado.
	GtkWidget *etqUnidad = nullptr;        ///< @brief Etiqueta: unidades del archivo (MM/IN).
	GtkWidget *etqTamano = nullptr;        ///< @brief Etiqueta: dimensiones de la placa.
	GtkWidget *etqPrimitivas = nullptr;    ///< @brief Etiqueta: número de primitivas.
	GtkWidget *etqMovimientos = nullptr;   ///< @brief Etiqueta: cortes y desplazamientos de la trayectoria.

	// ── Casillas de capas visibles ─────────────────────────────────
	GtkCheckButton *chkMostrarGerber = nullptr;        ///< @brief Casilla: capa Gerber.
	GtkCheckButton *chkMostrarTrayectoria = nullptr;   ///< @brief Casilla: trayectoria CNC.
	GtkCheckButton *chkMostrarLimites = nullptr;       ///< @brief Casilla: límites de la placa.
	GtkCheckButton *chkMostrarCuadricula = nullptr;    ///< @brief Casilla: cuadrícula.

	// ── Controles de configuración GCode ─────────────────────────────
	GtkSpinButton *spinAvance = nullptr;           ///< @brief Campo: avance de corte (feedRate).
	GtkSpinButton *spinPenetracion = nullptr;      ///< @brief Campo: avance de penetración (plungeRate).
	GtkSpinButton *spinZSeguro = nullptr;          ///< @brief Campo: altura segura Z (safeZ).
	GtkSpinButton *spinZCorte = nullptr;           ///< @brief Campo: profundidad de corte (cutZ).
	GtkSpinButton *spinDiamHerramienta = nullptr;  ///< @brief Campo: diámetro de fresa (toolDiameter).
	GtkSpinButton *spinHuelgo = nullptr;           ///< @brief Campo: holgura del aislamiento.
	GtkSpinButton *spinPasadas = nullptr;          ///< @brief Campo: pasadas del aislamiento.
	GtkCheckButton *chkOptimizar = nullptr;        ///< @brief Casilla: optimizar recorridos (optimizePaths).
	GtkCheckButton *chkComandosArco = nullptr;     ///< @brief Casilla: usar G02/G03 nativos (useArcCommands).
	GtkCheckButton *chkAislamiento = nullptr;      ///< @brief Casilla: modo aislamiento frente a contorno.
	GtkCheckButton *chkMetrico = nullptr;          ///< @brief Casilla: unidades métricas (metricMode).

	// ── Datos: archivo de taladro ────────────────────────────────────
	drill::DrillResult resultadoTaladro;  ///< @brief Operaciones del archivo .drl cargado.
	bool tieneTaladro = false;            ///< @brief @c true si hay un archivo de taladro cargado.
	std::string rutaTaladro;               ///< @brief Ruta del archivo de taladro abierto.
	bool mostrarTaladro = false;           ///< @brief Dibujar los taladros en el visor.

	GtkCheckButton *chkMostrarTaladro = nullptr;             ///< @brief Casilla: capa de taladros.
	GtkCheckButton *chkMostrarTrayectoriaGerber = nullptr;   ///< @brief Casilla: recorrido del GCode Gerber.
	GtkCheckButton *chkMostrarTrayectoriaTaladro = nullptr;  ///< @brief Casilla: recorrido de taladro.

	// ── Modos de mecanizado ─────────────────────────────────────────
	bool mostrarPerfiladoCobre = false;          ///< @brief Trayectoria generada por "Perfilar Cobre".
	bool mostrarAislamientoCorregido = false;    ///< @brief Trayectoria de "Aislamiento Correcto".
	bool mostrarPerfiladoIslas = false;          ///< @brief Trayectoria generada por "Perfilar Islas" (cobre no-pista).

};

/**
 * @brief   Puntero global al estado de la aplicación.
 *
 * @warning Apunta a un @c EstadoApp local de @c main(): es válido
 *          solo mientras @c main() esté en ejecución. No usarlo
 *          antes de que @c main() lo asigne ni desde otros hilos.
 */
extern EstadoApp *estado;

/**
 * @brief   Muestra un mensaje en la barra de estado inferior.
 * @param   msg Texto a mostrar (se sustituye al anterior).
 */
void establecerEstado(const std::string &msg);

/**
 * @brief   Lee la configuración GCode de los controles de la interfaz.
 * @details Vuelca en un @c gerber::GCodeConfig los valores actuales
 *          de los controles: @c spinAvance (feedRate),
 *          @c spinPenetracion (plungeRate), @c spinZSeguro (safeZ),
 *          @c spinZCorte (cutZ), @c spinDiamHerramienta
 *          (toolDiameter), @c chkOptimizar (optimizePaths),
 *          @c chkComandosArco (useArcCommands) y @c chkMetrico
 *          (metricMode). @c addComments se fuerza a @c true.
 *
 *          Los campos sin control asociado (@c rapidRate, @c traceZ,
 *          @c arcTolerance, offsets, escala y @c decimalPlaces)
 *          quedan con sus valores por defecto.
 *
 * @return  Configuración lista para @c GCodeGenerator o @c Mecanizado.
 */
gerber::GCodeConfig leerConfigGCode();

/**
 * @brief   Actualiza las etiquetas del panel de información.
 * @details Refresca nombre de archivo, unidades, dimensiones de la
 *          placa, número de primitivas y resumen de movimientos. Si
 *          no hay archivo cargado, muestra valores vacíos ("—").
 */
void actualizarPanelInfo();

/**
 * @brief   Calcula (o recalcula) la trayectoria CNC del Gerber.
 * @details Proceso:
 *          - Si no hay archivo cargado, no hace nada.
 *          - Lee la configuración con @c leerConfigGCode(), la
 *            holgura (@c spinHuelgo) y el número de pasadas
 *            (@c spinPasadas).
 *          - Si @c chkAislamiento está activo, llama a
 *            @c construirTrayectoriaAislamiento(); si no, a
 *            @c construirTrayectoria() (modo contorno).
 *          - Marca @c trayectoriaSucia como limpia y actualiza la
 *            etiqueta de movimientos con el recuento de cortes y
 *            desplazamientos.
 */
void SeleccionTrayectoria();

/**
 * @brief   Construye toda la interfaz GTK4 de la aplicación.
 * @details Crea y conecta: la ventana principal (1280×800) con su
 *          barra de título y botones (Abrir Gerber, Ajustar,
 *          Taladros, Recorrido, Perfilar Cobre, Aislamiento Correcto,
 *          Herramientas, GCode Taladro, Ver GCode, Guardar GCode), el
 *          área de dibujo Cairo, el panel lateral de información y
 *          capas, los controles de configuración GCode y el notebook
 *          de pestañas. Los punteros de @c estado quedan asignados.
 *
 * @param   app Aplicación GTK ya activada (señal "activate").
 */
void construirUI(GtkApplication *app);

#endif /* NUCLEO_INTERFAZ_H_ */

