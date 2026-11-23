/*
 * Intefaces/ui_materials.h
 *
 *  Created on: 13 abr 2026
 *      Author: DjSteker
 */

/**
 * @file Interfaces/ui_materials.h
 * @brief Ventana de gestión de materiales de trabajo (fichas CRUD).
 *
 * @details
 * Define `MaterialWindow`, la interfaz GTK4 de la gestión de materiales.
 * Muestra en un `GtkTreeView` los materiales guardados y un formulario
 * con los campos de la ficha `MaterialProps` organizados por grupos
 * (identificación, propiedades mecánicas, térmicas, ópticas y
 * electromagnéticas, apariencia, desgaste y observaciones). Las altas,
 * modificaciones y eliminaciones se delegan en `DatabaseMaterial`.
 *
 * ### Quién lo utiliza
 * - `ui_main.cpp`: el botón "Gestión de Materiales" del menú principal
 *   crea y muestra una instancia de esta ventana.
 *
 * ### Advertencias sobre el código actual
 * - El botón Actualizar no está implementado: muestra el aviso "Función
 *   Modificar pendiente de implementación completa".
 * - El formulario recoge todos los campos de la ficha, pero
 *   `DatabaseMaterial` solo persiste Id, Nombre y Tipo en el XML; el
 *   resto se pierde al reiniciar la aplicación.
 */

#ifndef INTERFACES_UI_MATERIALS_H_
#define INTERFACES_UI_MATERIALS_H_

#include <gtk/gtk.h>
#include <string>
#include "../BaseDatos/DatabaseMaterial.h"

/**
 * @brief Ventana GTK4 de gestión de materiales de trabajo.
 *
 * @details
 * Ventana con una tabla resumen, un formulario agrupado por tipos de
 * propiedad y una fila de botones (Añadir, Actualizar, Eliminar,
 * Cerrar). Se autodestruye (delete de la instancia C++) al cerrar la
 * ventana GTK.
 */
class MaterialWindow {
private:
	GtkWidget *window;       ///< Ventana GTK4 de este objeto.
	GtkWidget *treeview;     ///< Vista de tabla con el resumen de materiales.
	GtkListStore *liststore; ///< Modelo de la tabla.

	// --- Campos de Identificación ---
	GtkWidget *entry_id;     ///< Id del material (solo lectura; se autogenera si se deja vacío).
	GtkWidget *entry_nombre; ///< Nombre descriptivo.
	GtkWidget *entry_tipo;   ///< Tipo o familia del material.
	GtkWidget *entry_proveedor; ///< Proveedor del material.

	// --- Propiedades Mecánicas ---
	GtkWidget *entry_dureza;                 ///< Dureza.
	GtkWidget *entry_resistencia_traccion;  ///< Resistencia a la tracción (MPa).
	GtkWidget *entry_limite_elasticidad;     ///< Límite elástico (MPa).
	GtkWidget *entry_coef_friccion;          ///< Coeficiente de fricción.
	GtkWidget *entry_trabajabilidad;         ///< Índice de trabajabilidad.
	GtkWidget *check_endurecimiento;         ///< Marca de endurecimiento por trabajo.

	// --- Propiedades Térmicas ---
	GtkWidget *entry_punto_fusion;          ///< Temperatura de fusión (°C).
	GtkWidget *entry_punto_ebullicion;      ///< Temperatura de ebullición (°C).
	GtkWidget *entry_calor_especifico;      ///< Calor específico.
	GtkWidget *entry_conductividad_termica;  ///< Conductividad térmica.
	GtkWidget *entry_difusividad_termica;   ///< Difusividad térmica.
	GtkWidget *entry_coef_expansion;        ///< Coeficiente de expansión térmica.

	// --- Propiedades Ópticas y EM ---
	GtkWidget *entry_emisividad;             ///< Emisividad térmica.
	GtkWidget *entry_reflectividad;          ///< Reflectividad.
	GtkWidget *entry_absortividad;           ///< Absortividad.
	GtkWidget *entry_indice_refraction;      ///< Índice de refracción.
	GtkWidget *entry_permitividad;           ///< Permitividad relativa.
	GtkWidget *entry_permeabilidad;          ///< Permeabilidad relativa.
	GtkWidget *entry_conductividad_electrica; ///< Conductividad eléctrica.
	GtkWidget *entry_freq_resonancia;        ///< Frecuencia de resonancia del plasma.

	// --- Apariencia ---
	GtkWidget *entry_color_superficie;  ///< Color de la superficie.
	GtkWidget *entry_temp_max_emision;   ///< Temperatura máxima de emisión.

	// --- Desgaste ---
	GtkWidget *entry_abrasividad;         ///< Abrasividad frente a la herramienta.
	GtkWidget *check_tendencia_adhesion;  ///< Marca de tendencia a la adhesión.

	// --- Observaciones ---
	GtkWidget *entry_observaciones;  ///< Notas libres (campo de texto multilínea).

	// Funciones internas

	/**
	 * @brief Recarga la tabla con todos los materiales de la base de datos.
	 */
	void cargarLista();

	/**
	 * @brief Vacía todos los campos del formulario.
	 */
	void limpiarCampos();

	/**
	 * @brief Devuelve el Id de la fila seleccionada en la tabla.
	 * @return Id seleccionado o cadena vacía si no hay selección.
	 */
	std::string obtenerIdSeleccionado();

	/**
	 * @brief Muestra un diálogo de error con un mensaje.
	 * @param msg Texto del aviso de error.
	 */
	void mostrarError(const std::string &msg);

public:
	/**
	 * @brief Construye la ventana con la tabla, el formulario y los botones.
	 */
	MaterialWindow();

	/**
	 * @brief Libera los recursos de la ventana.
	 */
	~MaterialWindow();

	/**
	 * @brief Presenta la ventana al usuario (gtk_window_present).
	 */
	void show();

	// Callbacks

	/**
	 * @brief Callback del botón Añadir: valida el formulario y da de alta.
	 * @details Genera un Id automático si el campo está vacío, valida los
	 * numéricos (diálogo de error si fallan) y llama a
	 * `DatabaseMaterial::Agregar`.
	 * @param btn        Botón pulsado (no usado).
	 * @param user_data  Puntero a la instancia MaterialWindow.
	 */
	static void on_btn_add_clicked(GtkButton *btn, gpointer user_data);

	/**
	 * @brief Callback del botón Actualizar (sin implementar).
	 * @warning Solo muestra el aviso de que la modificación está
	 * pendiente de implementación; no modifica nada.
	 * @param btn        Botón pulsado (no usado).
	 * @param user_data  Puntero a la instancia MaterialWindow.
	 */
	static void on_btn_update_clicked(GtkButton *btn, gpointer user_data);

	/**
	 * @brief Callback del botón Eliminar: borra la fila seleccionada.
	 * @details Llama a `DatabaseMaterial::Eliminar`, recarga la tabla y
	 * avisa con un diálogo si no hay fila seleccionada o falla el borrado.
	 * @param btn        Botón pulsado (no usado).
	 * @param user_data  Puntero a la instancia MaterialWindow.
	 */
	static void on_btn_delete_clicked(GtkButton *btn, gpointer user_data);

	/**
	 * @brief Callback de selección de fila: vuelca el material en el formulario.
	 * @param sel        Selección del treeview.
	 * @param user_data  Puntero a la instancia MaterialWindow.
	 */
	static void on_row_selected(GtkTreeSelection *sel, gpointer user_data);
};

#endif /* INTERFACES_UI_MATERIALS_H_ */

