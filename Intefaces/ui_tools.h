/*
 * Intefaces/ui_tools.h
 *
 * Created on: 14 abr 2026
 * Author: DjSteker
 */

/**
 * @file Interfaces/ui_tools.h
 * @brief Ventana de gestión de herramientas de corte (fichas CRUD).
 *
 * @details
 * Define `ToolWindow`, la interfaz GTK4 del gestor de herramientas CNC.
 * Muestra en un `GtkTreeView` el resumen de las herramientas guardadas
 * (Id, herramienta, tipo, diámetro de corte, flautas y ubicación) y un
 * formulario con todos los campos de la ficha `HerramientaProps` para
 * dar de alta, actualizar o eliminar herramientas mediante
 * `DatabaseHerramienta`.
 *
 * ### Quién lo utiliza
 * - `ui_main.cpp`: el botón "Gestor de Herramientas" del menú principal
 *   crea y muestra una instancia de esta ventana.
 *
 * ### Advertencias sobre el código actual
 * - El alta y la actualización validan los campos numéricos y muestran
 *   un diálogo de error si fallan; la eliminación es silenciosa.
 * - El campo Id no es editable: si se deja vacío en un alta, se genera
 *   con `DatabaseHerramienta::GenerarIDAutomatico()`.
 */

#ifndef INTERFACES_UI_TOOLS_H_
#define INTERFACES_UI_TOOLS_H_

#include <gtk/gtk.h>
#include <string>

/**
 * @brief Ventana GTK4 de gestión de herramientas de corte.
 *
 * @details
 * Ventana "Gestión de Herramientas CNC" (1100x700) con una tabla
 * resumen arriba, un formulario con los datos de la herramienta debajo y
 * una fila de botones (Añadir, Actualizar, Eliminar, Cerrar). Se
 * autodestruye (delete de la instancia C++) al cerrar la ventana GTK.
 */
class ToolWindow {
private:
	GtkWidget *window;       ///< Ventana GTK4 de este objeto.
	GtkWidget *treeview;     ///< Vista de tabla con el resumen de herramientas.
	GtkListStore *liststore; ///< Modelo de la tabla (6 columnas de texto).

	// Todos los campos de entrada
	GtkWidget *entry_id;              ///< Id de la herramienta (solo lectura; se autogenera si se deja vacío).
	GtkWidget *entry_fabricante;      ///< Fabricante.
	GtkWidget *entry_modelo;          ///< Modelo o referencia.
	GtkWidget *entry_material;        ///< Material de la herramienta.
	GtkWidget *entry_costo;           ///< Coste de adquisición.
	GtkWidget *entry_diam_corte;      ///< Diámetro de corte (mm).
	GtkWidget *entry_diam_mango;      ///< Diámetro del mango (mm).
	GtkWidget *entry_long_corte;      ///< Longitud de corte (mm).
	GtkWidget *entry_long_total;      ///< Longitud total (mm).
	GtkWidget *entry_num_flautas;     ///< Número de flautas.
	GtkWidget *entry_tipo_punta;      ///< Tipo de punta.
	GtkWidget *entry_direccion_helice; ///< Sentido de la hélice.
	GtkWidget *entry_rpm;             ///< RPM sugeridas.
	GtkWidget *entry_feedrate;        ///< Avance lineal sugerido (mm/min).
	GtkWidget *entry_chipload;        ///< Carga por diente (mm).
	GtkWidget *entry_stepdown;        ///< Profundidad de pasada axial (mm).
	GtkWidget *entry_stepover;        ///< Ancho de paso radial (mm).
	GtkWidget *entry_horas_uso;       ///< Horas de uso acumuladas.
	GtkWidget *entry_ubicacion;       ///< Ubicación física.
	GtkWidget *check_es_broca;        ///< Marca si la herramienta es una broca.
	GtkWidget *entry_angulo_punta;    ///< Ángulo de punta (grados).
	GtkWidget *entry_tipo_broca;      ///< Subtipo de broca.

	/**
	 * @brief Recarga la tabla con todas las herramientas de la base de datos.
	 */
	void cargarLista();

	/**
	 * @brief Vacía todos los campos del formulario.
	 */
	void limpiarCampos();

public:
	/**
	 * @brief Construye la ventana con la tabla, el formulario y los botones.
	 */
	ToolWindow();

	/**
	 * @brief Libera los recursos de la ventana.
	 */
	~ToolWindow();

	/**
	 * @brief Presenta la ventana al usuario (gtk_window_present).
	 */
	void show();

	/**
	 * @brief Callback del botón Añadir: valida el formulario y da de alta.
	 * @details Genera un Id automático si el campo está vacío, valida los
	 * numéricos (diálogo de error si fallan) y llama a
	 * `DatabaseHerramienta::Agregar`.
	 * @param btn        Botón pulsado (no usado).
	 * @param user_data  Puntero a la instancia ToolWindow.
	 */
	static void on_btn_add_clicked(GtkButton *btn, gpointer user_data);

	/**
	 * @brief Callback del botón Actualizar: modifica la fila seleccionada.
	 * @details Requiere una fila seleccionada; rellena la ficha con el
	 * formulario y llama a `DatabaseHerramienta::Modificar`.
	 * @param btn        Botón pulsado (no usado).
	 * @param user_data  Puntero a la instancia ToolWindow.
	 */
	static void on_btn_update_clicked(GtkButton *btn, gpointer user_data);

	/**
	 * @brief Callback del botón Eliminar: borra la fila seleccionada.
	 * @details No pide confirmación; llama a
	 * `DatabaseHerramienta::Eliminar` y recarga la tabla.
	 * @param btn        Botón pulsado (no usado).
	 * @param user_data  Puntero a la instancia ToolWindow.
	 */
	static void on_btn_delete_clicked(GtkButton *btn, gpointer user_data);

	/**
	 * @brief Callback de selección de fila: vuelca la herramienta en el formulario.
	 * @param sel        Selección del treeview.
	 * @param user_data  Puntero a la instancia ToolWindow.
	 */
	static void on_row_selected(GtkTreeSelection *sel, gpointer user_data);

	/**
	 * @brief Devuelve el Id de la fila seleccionada en la tabla.
	 * @return Id seleccionado o cadena vacía si no hay selección.
	 */
	std::string obtenerIdSeleccionado();
};

#endif /* INTERFACES_UI_TOOLS_H_ */

