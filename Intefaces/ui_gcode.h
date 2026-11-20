/*
 * Intefaces/ui_gcode.h
 *
 *  Created on: 13 abr 2026
 *      Author: DjSteker
 */

/**
 * @file Interfaces/ui_gcode.h
 * @brief Ventana de generación de G-Code a partir de Gerber y taladros.
 *
 * @details
 * Define `GCodeWindow`, la ventana más completa de la aplicación. Se
 * organiza en un `GtkNotebook` con cuatro pestañas:
 *
 * 1. **Configuración:** selección de la herramienta de corte (de la base
 *    de datos), ruta del archivo Gerber, parámetros de corte (avance,
 *    avance vertical, Z de corte, Z de seguridad) y botón de generación.
 *    Muestra también la información del Gerber cargado (unidades,
 *    dimensiones, número de primitivas y estado).
 * 2. **G-Code:** texto generado, número de líneas y ruta del archivo
 *    guardado.
 * 3. **Visualizador:** área de dibujo Cairo que renderiza el cobre del
 *    Gerber y los taladros.
 * 4. **Taladros (.drl):** ruta del archivo de taladro, resumen de
 *    operaciones y parámetros de mecanizado (Z segura, avances, Z de
 *    contacto y perforación, retracción, aceleración) con su botón de
 *    generación propio.
 *
 * ### Quién lo utiliza
 * - `ui_main.cpp`: el botón "Generar G-Code" del menú principal crea y
 *   muestra una instancia de esta ventana.
 *
 * ### Advertencias sobre el código actual
 * - `on_draw` está declarado aquí y conectado al área de dibujo, pero su
 *   implementación no está en `ui_gcode.cpp`: vive en
 *   `docs/parche_on_draw.cpp`, que debe compilarse junto con la ventana
 *   para que el enlace funcione.
 * - La generación de G-Code de Gerber escribe siempre el resultado en
 *   `<ruta_gerber>.gcode` sin pedir confirmación si ya existe.
 * - Los valores numéricos del formulario se leen con `std::stod` sin
 *   validación previa: un campo vacío lanza una excepción no capturada.
 */

#ifndef INTERFACES_UI_GCODE_H_
#define INTERFACES_UI_GCODE_H_

#include <gtk/gtk.h>
#include <string>
#include "../BaseDatos/DatabaseHerramienta.h"
#include "../Procesos/gerber.h"
#include "../Procesos/drill.h"

/**
 * @brief Ventana GTK4 de generación de G-Code (Gerber y taladros).
 *
 * @details
 * Almacena los resultados del parseo del Gerber y del archivo de taladros
 * en memoria (`current_result` y `current_drill_result`) para que el
 * visualizador y los generadores trabajen sobre ellos sin releer los
 * archivos. Se autodestruye (delete de la instancia C++) al cerrar la
 * ventana GTK.
 */
class GCodeWindow {
private:
	// --- Widgets principales ---
	GtkWidget *window;   ///< Ventana GTK4 de este objeto.
	GtkWidget *notebook; ///< Contenedor de las cuatro pestañas.

	// --- Panel de Configuración (Pestaña 1) ---
	GtkWidget *treeview_tools;    ///< Tabla de herramientas disponibles.
	GtkListStore *liststore_tools; ///< Modelo de la tabla de herramientas.
	GtkWidget *entry_herramienta_id; ///< Id de la herramienta seleccionada.
	GtkWidget *entry_diametro;   ///< Diámetro de la herramienta (mm).
	GtkWidget *entry_gerber_path; ///< Ruta del archivo Gerber.
	GtkWidget *btn_buscar_gerber;  ///< Botón de selección de archivo Gerber.
	GtkWidget *entry_feedrate;    ///< Avance de corte (mm/min).
	GtkWidget *entry_plunge_rate;  ///< Avance vertical (mm/min).
	GtkWidget *entry_cut_z;        ///< Profundidad de corte en Z (mm).
	GtkWidget *entry_safe_z;       ///< Altura segura en Z (mm).
	GtkWidget *btn_generar;       ///< Botón que genera el G-Code del Gerber.

	// --- Info del Gerber ---
	GtkWidget *lbl_gbr_unit;       ///< Etiqueta con las unidades del Gerber.
	GtkWidget *lbl_gbr_bounds;     ///< Etiqueta con las dimensiones del cobre.
	GtkWidget *lbl_gbr_primitives; ///< Etiqueta con el número de primitivas.
	GtkWidget *lbl_gbr_status;     ///< Etiqueta con el estado del parseo.

	// --- Pestaña 2: G-Code ---
	GtkWidget *textview_gcode;   ///< Visor de texto con el G-Code generado.
	GtkWidget *lbl_gcode_lines;  ///< Etiqueta con el número de líneas.
	GtkWidget *lbl_gcode_path;   ///< Etiqueta con la ruta del archivo guardado.

	// --- Pestaña 3: Visualizador ---
	GtkWidget *drawing_area;      ///< Área de dibujo Cairo del PCB.
	GtkWidget *lbl_render_status; ///< Etiqueta con el estado del renderizado.

	// --- Datos para renderizado ---
	gerber::GerberResult current_result;     ///< Resultado del parseo Gerber.
	drill::DrillResult current_drill_result; ///< Resultado del parseo .drl.

	// --- Widgets para .drl (Pestaña 4) ---
	// Archivo .drl
	GtkWidget *btn_buscar_drl;        ///< Botón de selección de archivo .drl.
	GtkWidget *entry_drl_path;         ///< Ruta del archivo .drl.
	GtkWidget *lbl_drl_status;         ///< Etiqueta con el estado del parseo .drl.
	GtkWidget *lbl_drl_operations;     ///< Etiqueta con el resumen de operaciones.

	// Parámetros de taladrado
	GtkWidget *entry_drl_safe_z;           ///< Altura segura (Z).
	GtkWidget *entry_drl_travel_feedrate;  ///< Velocidad de viaje (X/Y, mm/min).
	GtkWidget *entry_drl_plunge_feedrate;  ///< Velocidad Z antes de contacto (mm/min).
	GtkWidget *entry_drl_contact_z;        ///< Posición Z de contacto.
	GtkWidget *entry_drl_drill_feedrate;   ///< Velocidad de perforación (mm/min).
	GtkWidget *entry_drl_drill_z;          ///< Posición Z de perforación.
	GtkWidget *entry_drl_retract_z;        ///< Posición Z al retraer.
	GtkWidget *entry_drl_acceleration;     ///< Aceleración (mm/s²).
	GtkWidget *btn_generar_drl_gcode;      ///< Botón para generar G-code de taladros.

	// --- Funciones privadas ---

	/**
	 * @brief Rellena la tabla de herramientas desde la base de datos.
	 */
	void cargarHerramientas();

	/**
	 * @brief Vuelca el resumen de operaciones de taladro en la pestaña 4.
	 * @param res Resultado del parseo del archivo .drl.
	 */
	void cargarDrillOperations(const drill::DrillResult &res);

	/**
	 * @brief Devuelve el Id de la herramienta seleccionada en la tabla.
	 * @return Id seleccionado o cadena vacía si no hay selección.
	 */
	std::string obtenerHerramientaSeleccionada();

	/**
	 * @brief Muestra un diálogo de error del flujo Gerber.
	 * @param msg Texto del aviso de error.
	 */
	void mostrarError(const std::string &msg);

	/**
	 * @brief Muestra un diálogo de error del flujo de taladros.
	 * @param msg Texto del aviso de error.
	 */
	void mostrarErrorDrill(const std::string &msg);

	/**
	 * @brief Actualiza las etiquetas de información del Gerber.
	 * @details Muestra unidades, dimensiones del cobre, número de
	 * primitivas y estado del parseo a partir del resultado.
	 * @param res Resultado del parseo Gerber.
	 */
	void actualizarInfoGerber(const gerber::GerberResult &res);

	/**
	 * @brief Muestra el G-Code generado en la pestaña 2.
	 * @param gcode      Texto completo del G-Code.
	 * @param savedPath  Ruta del archivo donde se ha guardado.
	 */
	void mostrarGCode(const std::string &gcode, const std::string &savedPath);

	/**
	 * @brief Genera el G-Code de taladros a partir de los parámetros.
	 * @details Recorre las operaciones de taladro del resultado y produce
	 * los movimientos (viaje, contacto, perforación y retracción) con los
	 * avances y alturas configuradas en la pestaña 4.
	 * @param drillResult     Operaciones parseadas del .drl.
	 * @param tool            Herramienta seleccionada.
	 * @param safeZ           Altura segura de desplazamiento.
	 * @param travelFeedrate  Avance de viaje X/Y (mm/min).
	 * @param plungeFeedrate  Avance Z de aproximación (mm/min).
	 * @param contactZ        Z de contacto con la superficie.
	 * @param drillFeedrate   Avance de perforación (mm/min).
	 * @param drillZ          Z de perforación final.
	 * @param retractZ        Z de retracción tras cada taladro.
	 * @param acceleration     Aceleración (mm/s²).
	 * @return Texto del G-Code de taladros.
	 */
	std::string generarGCodeTaladros(const drill::DrillResult &drillResult, const HerramientaProps &tool, double safeZ, double travelFeedrate, double plungeFeedrate, double contactZ,
			double drillFeedrate, double drillZ, double retractZ, double acceleration);

public:
	/**
	 * @brief Construye la ventana con las cuatro pestañas y sus widgets.
	 * @details Conecta todos los callbacks, configura el área de dibujo y
	 * carga la tabla de herramientas al abrir.
	 */
	GCodeWindow();

	/**
	 * @brief Libera los recursos de la ventana.
	 */
	~GCodeWindow();

	/**
	 * @brief Presenta la ventana al usuario (gtk_window_present).
	 */
	void show();

	// --- Callbacks ---

	/**
	 * @brief Callback del botón de búsqueda: abre el selector de Gerber.
	 * @param btn        Botón pulsado (no usado).
	 * @param user_data  Puntero a la instancia GCodeWindow.
	 */
	static void on_btn_buscar_clicked(GtkButton*, gpointer user_data);

	/**
	 * @brief Callback del botón de búsqueda: abre el selector de .drl.
	 * @param btn        Botón pulsado (no usado).
	 * @param user_data  Puntero a la instancia GCodeWindow.
	 */
	static void on_btn_buscar_drl_clicked(GtkButton*, gpointer user_data);

	/**
	 * @brief Callback del botón Generar: produce el G-Code del Gerber.
	 * @details Valida herramienta y ruta, parsea el Gerber, rellena la
	 * configuración de corte a partir del formulario y de la herramienta
	 * seleccionada, genera el G-Code y lo guarda en
	 * `<ruta_gerber>.gcode`.
	 * @param btn        Botón pulsado (no usado).
	 * @param user_data  Puntero a la instancia GCodeWindow.
	 */
	static void on_btn_generar_clicked(GtkButton*, gpointer user_data);

	/**
	 * @brief Callback del botón Generar de taladros.
	 * @details Valida la ruta del .drl, parsea el archivo y llama a
	 * `generarGCodeTaladros` con los parámetros de la pestaña 4.
	 * @param btn        Botón pulsado (no usado).
	 * @param user_data  Puntero a la instancia GCodeWindow.
	 */
	static void on_btn_generar_drl_clicked(GtkButton*, gpointer user_data);

	/**
	 * @brief Callback de selección de herramienta en la tabla.
	 * @details Copia el Id y el diámetro de la herramienta seleccionada
	 * a los campos del formulario.
	 * @param sel        Selección del treeview.
	 * @param user_data  Puntero a la instancia GCodeWindow.
	 */
	static void on_row_selected(GtkTreeSelection*, gpointer user_data);

	/**
	 * @brief Función de dibujo del visualizador (Cairo).
	 * @warning No está implementada en `ui_gcode.cpp`: su implementación
	 * se encuentra en `docs/parche_on_draw.cpp` y debe compilarse junto
	 * con esta ventana.
	 * @param area       Área de dibujo GTK.
	 * @param cr         Contexto Cairo de renderizado.
	 * @param width      Ancho del área en píxeles.
	 * @param height     Alto del área en píxeles.
	 * @param user_data  Puntero a la instancia GCodeWindow.
	 */
	static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data);
};

#endif /* INTERFACES_UI_GCODE_H_ */

