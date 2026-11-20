/*
 * Intefaces/ui_gcode.cpp
 *
 *  Created on: 13 abr 2026
 *      Author: DjSteker
 */

#include "ui_gcode.h"
#include "../BaseDatos/DatabaseHerramienta.h"
#include "../Procesos/gerber.h"
#include "../Procesos/drill.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cairo.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────────────────

GCodeWindow::GCodeWindow() {
	// Inicializar resultados
	current_result.ok = false;
	current_drill_result.ok = false;

	// Crear ventana principal
	window = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(window), "Gerber Studio: Configuración, G-Code y Taladrado");
	gtk_window_set_default_size(GTK_WINDOW(window), 1400, 900);

	// Contenedor principal: Notebook (Pestañas)
	notebook = gtk_notebook_new();
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
	gtk_window_set_child(GTK_WINDOW(window), notebook);

	// =========================================================================
	// PESTAÑA 1: CONFIGURACIÓN (Herramientas y Gerber)
	// =========================================================================
	GtkWidget *config_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_margin_start(config_box, 10);
	gtk_widget_set_margin_end(config_box, 10);
	gtk_widget_set_margin_top(config_box, 10);
	gtk_widget_set_margin_bottom(config_box, 10);

	// --- Herramientas ---
	GtkWidget *frame_tools = gtk_frame_new("Herramienta de Corte");
	gtk_widget_set_tooltip_text(frame_tools, "Seleccione una herramienta de la lista para configurar los parámetros de corte");
	GtkWidget *box_tools = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_start(box_tools, 6);
	gtk_widget_set_margin_end(box_tools, 6);
	gtk_widget_set_margin_top(box_tools, 6);
	gtk_widget_set_margin_bottom(box_tools, 6);
	gtk_frame_set_child(GTK_FRAME(frame_tools), box_tools);

	// TreeView para herramientas
	liststore_tools = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	treeview_tools = gtk_tree_view_new_with_model(GTK_TREE_MODEL(liststore_tools));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview_tools), TRUE);
	gtk_widget_set_tooltip_text(treeview_tools, "Lista de herramientas disponibles. Haga clic en una para seleccionarla");

	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview_tools), -1, "ID", renderer, "text", 0, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview_tools), -1, "Herramienta", renderer, "text", 1, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview_tools), -1, "Tipo", renderer, "text", 2, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview_tools), -1, "Ø (mm)", renderer, "text", 3, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview_tools), -1, "Flautas", renderer, "text", 4, NULL);

	GtkWidget *scroll_tools = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_tools), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request(scroll_tools, -1, 120);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll_tools), treeview_tools);
	gtk_box_append(GTK_BOX(box_tools), scroll_tools);

	// Grid para detalles de la herramienta seleccionada
	GtkWidget *grid_tool = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid_tool), 8);
	gtk_grid_set_row_spacing(GTK_GRID(grid_tool), 4);

	gtk_grid_attach(GTK_GRID(grid_tool), gtk_label_new("ID:"), 0, 0, 1, 1);
	entry_herramienta_id = gtk_entry_new();
	gtk_editable_set_editable(GTK_EDITABLE(entry_herramienta_id), FALSE);
	gtk_widget_set_tooltip_text(entry_herramienta_id, "Identificador único de la herramienta seleccionada");
	gtk_grid_attach(GTK_GRID(grid_tool), entry_herramienta_id, 1, 0, 1, 1);

	gtk_grid_attach(GTK_GRID(grid_tool), gtk_label_new("Diámetro:"), 0, 1, 1, 1);
	entry_diametro = gtk_entry_new();
	gtk_editable_set_editable(GTK_EDITABLE(entry_diametro), FALSE);
	gtk_widget_set_tooltip_text(entry_diametro, "Diámetro de corte de la herramienta (mm)");
	gtk_grid_attach(GTK_GRID(grid_tool), entry_diametro, 1, 1, 1, 1);

	gtk_box_append(GTK_BOX(box_tools), grid_tool);
	gtk_box_append(GTK_BOX(config_box), frame_tools);

	// --- Archivo Gerber ---
	GtkWidget *frame_gbr = gtk_frame_new("Archivo Gerber");
	gtk_widget_set_tooltip_text(frame_gbr, "Cargue un archivo Gerber (.gbr, .ger) para procesarlo");
	GtkWidget *box_gbr = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_start(box_gbr, 6);
	gtk_widget_set_margin_end(box_gbr, 6);
	gtk_widget_set_margin_top(box_gbr, 6);
	gtk_widget_set_margin_bottom(box_gbr, 6);
	gtk_frame_set_child(GTK_FRAME(frame_gbr), box_gbr);

	GtkWidget *box_path = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	entry_gerber_path = gtk_entry_new();
	gtk_widget_set_hexpand(entry_gerber_path, TRUE);
	gtk_editable_set_editable(GTK_EDITABLE(entry_gerber_path), FALSE);
	gtk_widget_set_tooltip_text(entry_gerber_path, "Ruta del archivo Gerber cargado");
	btn_buscar_gerber = gtk_button_new_with_label("Examinar Gerber…");
	gtk_widget_set_tooltip_text(btn_buscar_gerber, "Buscar y cargar un archivo Gerber");
	gtk_box_append(GTK_BOX(box_path), entry_gerber_path);
	gtk_box_append(GTK_BOX(box_path), btn_buscar_gerber);
	gtk_box_append(GTK_BOX(box_gbr), box_path);

	// Información del Gerber
	GtkWidget *frame_info = gtk_frame_new("Información del Archivo");
	GtkWidget *grid_info = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid_info), 8);
	gtk_grid_set_row_spacing(GTK_GRID(grid_info), 4);
	gtk_widget_set_margin_start(grid_info, 6);
	gtk_widget_set_margin_end(grid_info, 6);
	gtk_widget_set_margin_top(grid_info, 6);
	gtk_widget_set_margin_bottom(grid_info, 6);
	gtk_frame_set_child(GTK_FRAME(frame_info), grid_info);

	gtk_grid_attach(GTK_GRID(grid_info), gtk_label_new("Estado:"), 0, 0, 1, 1);
	lbl_gbr_status = gtk_label_new("—");
	gtk_label_set_xalign(GTK_LABEL(lbl_gbr_status), 0);
	gtk_widget_set_tooltip_text(lbl_gbr_status, "Estado del archivo Gerber cargado");
	gtk_grid_attach(GTK_GRID(grid_info), lbl_gbr_status, 1, 0, 1, 1);

	gtk_grid_attach(GTK_GRID(grid_info), gtk_label_new("Unidades:"), 0, 1, 1, 1);
	lbl_gbr_unit = gtk_label_new("—");
	gtk_label_set_xalign(GTK_LABEL(lbl_gbr_unit), 0);
	gtk_widget_set_tooltip_text(lbl_gbr_unit, "Unidades del archivo (MM o IN)");
	gtk_grid_attach(GTK_GRID(grid_info), lbl_gbr_unit, 1, 1, 1, 1);

	gtk_grid_attach(GTK_GRID(grid_info), gtk_label_new("Dimensiones:"), 0, 2, 1, 1);
	lbl_gbr_bounds = gtk_label_new("—");
	gtk_label_set_xalign(GTK_LABEL(lbl_gbr_bounds), 0);
	gtk_widget_set_tooltip_text(lbl_gbr_bounds, "Ancho × Alto del diseño");
	gtk_grid_attach(GTK_GRID(grid_info), lbl_gbr_bounds, 1, 2, 1, 1);

	gtk_grid_attach(GTK_GRID(grid_info), gtk_label_new("Primitivas:"), 0, 3, 1, 1);
	lbl_gbr_primitives = gtk_label_new("—");
	gtk_label_set_xalign(GTK_LABEL(lbl_gbr_primitives), 0);
	gtk_widget_set_tooltip_text(lbl_gbr_primitives, "Número de primitivas (segmentos, arcos, flashes, regiones)");
	gtk_grid_attach(GTK_GRID(grid_info), lbl_gbr_primitives, 1, 3, 1, 1);

	gtk_box_append(GTK_BOX(box_gbr), frame_info);
	gtk_box_append(GTK_BOX(config_box), frame_gbr);

	// --- Parámetros de corte para Gerber ---
	GtkWidget *frame_cfg = gtk_frame_new("Parámetros de Corte (Gerber)");
	gtk_widget_set_tooltip_text(frame_cfg, "Configuración para la generación de G-code a partir del archivo Gerber");
	GtkWidget *grid_cfg = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid_cfg), 8);
	gtk_grid_set_row_spacing(GTK_GRID(grid_cfg), 6);
	gtk_widget_set_margin_start(grid_cfg, 8);
	gtk_widget_set_margin_end(grid_cfg, 8);
	gtk_widget_set_margin_top(grid_cfg, 8);
	gtk_widget_set_margin_bottom(grid_cfg, 8);
	gtk_frame_set_child(GTK_FRAME(frame_cfg), grid_cfg);

	auto addRow = [&](const char *label, GtkWidget *&entry, const char *defVal, int row, const char *tooltip) {
		GtkWidget *lbl = gtk_label_new(label);
		gtk_label_set_xalign(GTK_LABEL(lbl), 1.0);
		gtk_grid_attach(GTK_GRID(grid_cfg), lbl, 0, row, 1, 1);
		entry = gtk_entry_new();
		gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_NUMBER);
		gtk_editable_set_text(GTK_EDITABLE(entry), defVal);
		gtk_widget_set_hexpand(entry, TRUE);
		gtk_widget_set_tooltip_text(entry, tooltip);
		gtk_grid_attach(GTK_GRID(grid_cfg), entry, 1, row, 1, 1);
	};

	addRow("Feed rate (mm/min):", entry_feedrate, "400", 0, "Velocidad de avance durante el corte");
	addRow("Plunge rate (mm/min):", entry_plunge_rate, "50", 1, "Velocidad de descenso inicial");
	addRow("Profundidad Z corte:", entry_cut_z, "-0.1", 2, "Profundidad de corte (valor negativo)");
	addRow("Z seguro:", entry_safe_z, "2.0", 3, "Altura segura para movimientos rápidos");

	gtk_box_append(GTK_BOX(config_box), frame_cfg);

	// Botón para generar G-code de Gerber
	btn_generar = gtk_button_new_with_label("⚙ Generar G-Code para Gerber");
	gtk_widget_add_css_class(btn_generar, "suggested-action");
	gtk_widget_set_tooltip_text(btn_generar, "Generar código G para el archivo Gerber cargado");
	gtk_widget_set_margin_top(btn_generar, 4);
	gtk_box_append(GTK_BOX(config_box), btn_generar);

	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), config_box, gtk_label_new("Configuración"));

	// =========================================================================
	// PESTAÑA 2: G-CODE (Vista previa)
	// =========================================================================
	GtkWidget *gcode_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_start(gcode_box, 6);
	gtk_widget_set_margin_end(gcode_box, 6);
	gtk_widget_set_margin_top(gcode_box, 6);
	gtk_widget_set_margin_bottom(gcode_box, 6);

	GtkWidget *frame_preview = gtk_frame_new("Vista Previa del G-Code");
	gtk_widget_set_tooltip_text(frame_preview, "Aquí se mostrará el código G generado");
	gtk_widget_set_vexpand(frame_preview, TRUE);
	GtkWidget *box_preview = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_margin_start(box_preview, 6);
	gtk_widget_set_margin_end(box_preview, 6);
	gtk_widget_set_margin_top(box_preview, 6);
	gtk_widget_set_margin_bottom(box_preview, 6);
	gtk_frame_set_child(GTK_FRAME(frame_preview), box_preview);

	GtkWidget *box_stats = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	lbl_gcode_lines = gtk_label_new("Líneas: —");
	gtk_label_set_xalign(GTK_LABEL(lbl_gcode_lines), 0);
	gtk_widget_set_tooltip_text(lbl_gcode_lines, "Número de líneas de código G");
	lbl_gcode_path = gtk_label_new("Archivo: —");
	gtk_label_set_xalign(GTK_LABEL(lbl_gcode_path), 0);
	gtk_label_set_ellipsize(GTK_LABEL(lbl_gcode_path), PANGO_ELLIPSIZE_START);
	gtk_widget_set_hexpand(lbl_gcode_path, TRUE);
	gtk_widget_set_tooltip_text(lbl_gcode_path, "Ruta donde se guardó el archivo G-code");
	gtk_box_append(GTK_BOX(box_stats), lbl_gcode_lines);
	gtk_box_append(GTK_BOX(box_stats), lbl_gcode_path);
	gtk_box_append(GTK_BOX(box_preview), box_stats);

	textview_gcode = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(textview_gcode), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textview_gcode), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(textview_gcode), TRUE);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(textview_gcode), 6);
	gtk_widget_set_tooltip_text(textview_gcode, "Contenido del archivo G-code generado");

	GtkWidget *scroll_gcode = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_gcode), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(scroll_gcode, TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll_gcode), textview_gcode);
	gtk_box_append(GTK_BOX(box_preview), scroll_gcode);
	gtk_box_append(GTK_BOX(gcode_box), frame_preview);

	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), gcode_box, gtk_label_new("G-Code"));

	// =========================================================================
	// PESTAÑA 3: VISUALIZADOR (Gerber y Taladros)
	// =========================================================================
	GtkWidget *render_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_start(render_box, 6);
	gtk_widget_set_margin_end(render_box, 6);
	gtk_widget_set_margin_top(render_box, 6);
	gtk_widget_set_margin_bottom(render_box, 6);

	GtkWidget *frame_render = gtk_frame_new("Visualizador de Gerber y Taladros");
	gtk_widget_set_tooltip_text(frame_render, "Visualización del diseño Gerber y los taladros cargados");
	gtk_widget_set_vexpand(frame_render, TRUE);
	GtkWidget *box_render = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_margin_start(box_render, 6);
	gtk_widget_set_margin_end(box_render, 6);
	gtk_widget_set_margin_top(box_render, 6);
	gtk_widget_set_margin_bottom(box_render, 6);
	gtk_frame_set_child(GTK_FRAME(frame_render), box_render);

	lbl_render_status = gtk_label_new("Sin datos para renderizar. Carga un archivo Gerber o .drl.");
	gtk_label_set_xalign(GTK_LABEL(lbl_render_status), 0);
	gtk_widget_set_tooltip_text(lbl_render_status, "Estado actual del renderizado");
	gtk_widget_set_margin_start(lbl_render_status, 6);
	gtk_box_append(GTK_BOX(box_render), lbl_render_status);

	drawing_area = gtk_drawing_area_new();
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area), on_draw, this, NULL);
	gtk_widget_set_size_request(drawing_area, -1, 500);
	gtk_widget_set_vexpand(drawing_area, TRUE);
	gtk_widget_set_hexpand(drawing_area, TRUE);
	gtk_widget_set_tooltip_text(drawing_area, "Área de dibujo: Gerber en verde, taladros en rojo");
	gtk_box_append(GTK_BOX(box_render), drawing_area);
	gtk_box_append(GTK_BOX(render_box), frame_render);

	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), render_box, gtk_label_new("Visualizador"));

	// =========================================================================
	// PESTAÑA 4: TALADRADO (.drl) - CON TODAS LAS MEJORAS
	// =========================================================================
	GtkWidget *drill_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_start(drill_box, 6);
	gtk_widget_set_margin_end(drill_box, 6);
	gtk_widget_set_margin_top(drill_box, 6);
	gtk_widget_set_margin_bottom(drill_box, 6);

	// Frame principal para la pestaña de taladrado
	GtkWidget *frame_drl_main = gtk_frame_new("Configuración de Taladrado (Excellon)");
	gtk_widget_set_tooltip_text(frame_drl_main, "Configuración para generar código G de taladrado a partir de archivos .drl");
	GtkWidget *box_drl_main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_start(box_drl_main, 6);
	gtk_widget_set_margin_end(box_drl_main, 6);
	gtk_widget_set_margin_top(box_drl_main, 6);
	gtk_widget_set_margin_bottom(box_drl_main, 6);
	gtk_frame_set_child(GTK_FRAME(frame_drl_main), box_drl_main);

	// --- Sección para cargar el archivo .drl ---
	GtkWidget *frame_drl_file = gtk_frame_new("Archivo Excellon (.drl)");
	gtk_widget_set_tooltip_text(frame_drl_file, "Cargue un archivo .drl para configurar el taladrado");
	GtkWidget *box_drl_file = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_start(box_drl_file, 6);
	gtk_widget_set_margin_end(box_drl_file, 6);
	gtk_widget_set_margin_top(box_drl_file, 6);
	gtk_widget_set_margin_bottom(box_drl_file, 6);
	gtk_frame_set_child(GTK_FRAME(frame_drl_file), box_drl_file);

	// Box horizontal para el campo de texto y el botón
	GtkWidget *box_drl_path = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	entry_drl_path = gtk_entry_new();
	gtk_widget_set_hexpand(entry_drl_path, TRUE);
	gtk_editable_set_editable(GTK_EDITABLE(entry_drl_path), FALSE);
	gtk_widget_set_tooltip_text(entry_drl_path, "Ruta del archivo .drl cargado");
	btn_buscar_drl = gtk_button_new_with_label("Examinar .drl…");
	gtk_widget_set_tooltip_text(btn_buscar_drl, "Buscar y cargar un archivo Excellon (.drl)");
	gtk_box_append(GTK_BOX(box_drl_path), entry_drl_path);
	gtk_box_append(GTK_BOX(box_drl_path), btn_buscar_drl);
	gtk_box_append(GTK_BOX(box_drl_file), box_drl_path);

	// Información del archivo .drl
	GtkWidget *frame_drl_info = gtk_frame_new("Información del Archivo");
	GtkWidget *grid_drl_info = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid_drl_info), 8);
	gtk_grid_set_row_spacing(GTK_GRID(grid_drl_info), 4);
	gtk_widget_set_margin_start(grid_drl_info, 6);
	gtk_widget_set_margin_end(grid_drl_info, 6);
	gtk_widget_set_margin_top(grid_drl_info, 6);
	gtk_widget_set_margin_bottom(grid_drl_info, 6);
	gtk_frame_set_child(GTK_FRAME(frame_drl_info), grid_drl_info);

	// Estado
	gtk_grid_attach(GTK_GRID(grid_drl_info), gtk_label_new("Estado:"), 0, 0, 1, 1);
	lbl_drl_status = gtk_label_new("—");
	gtk_label_set_xalign(GTK_LABEL(lbl_drl_status), 0);
	gtk_widget_set_tooltip_text(lbl_drl_status, "Estado del archivo .drl cargado");
	gtk_grid_attach(GTK_GRID(grid_drl_info), lbl_drl_status, 1, 0, 1, 1);

	// Número de taladros
	gtk_grid_attach(GTK_GRID(grid_drl_info), gtk_label_new("Número de taladros:"), 0, 1, 1, 1);
	lbl_drl_operations = gtk_label_new("—");
	gtk_label_set_xalign(GTK_LABEL(lbl_drl_operations), 0);
	gtk_widget_set_tooltip_text(lbl_drl_operations, "Cantidad de taladros en el archivo");
	gtk_grid_attach(GTK_GRID(grid_drl_info), lbl_drl_operations, 1, 1, 1, 1);

	gtk_box_append(GTK_BOX(box_drl_file), frame_drl_info);
	gtk_box_append(GTK_BOX(box_drl_main), frame_drl_file);

	// Separador entre secciones
	GtkWidget *separator1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_margin_top(separator1, 6);
	gtk_widget_set_margin_bottom(separator1, 6);
	gtk_box_append(GTK_BOX(box_drl_main), separator1);

	// --- Sección para parámetros de taladrado (agrupados en frames temáticos) ---
	GtkWidget *frame_drl_params = gtk_frame_new("Parámetros de Taladrado");
	gtk_widget_set_tooltip_text(frame_drl_params, "Configuración detallada para la generación de G-code de taladrado");
	GtkWidget *box_drl_params = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_start(box_drl_params, 6);
	gtk_widget_set_margin_end(box_drl_params, 6);
	gtk_widget_set_margin_top(box_drl_params, 6);
	gtk_widget_set_margin_bottom(box_drl_params, 6);
	gtk_frame_set_child(GTK_FRAME(frame_drl_params), box_drl_params);

	// =========================================================================
	// FRAME: Configuración de Movimiento
	// =========================================================================
	GtkWidget *frame_movement = gtk_frame_new("Movimiento");
	gtk_widget_set_tooltip_text(frame_movement, "Parámetros relacionados con el movimiento entre taladros");
	GtkWidget *box_movement = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_frame_set_child(GTK_FRAME(frame_movement), box_movement);

	GtkWidget *grid_movement = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid_movement), 8);
	gtk_grid_set_row_spacing(GTK_GRID(grid_movement), 4);
	gtk_box_append(GTK_BOX(box_movement), grid_movement);

	auto addMovementParam = [&](const char *label, GtkWidget *&entry, const char *defVal, int row, const char *tooltip) {
		GtkWidget *lbl = gtk_label_new(label);
		gtk_label_set_xalign(GTK_LABEL(lbl), 1.0);
		gtk_grid_attach(GTK_GRID(grid_movement), lbl, 0, row, 1, 1);
		entry = gtk_entry_new();
		gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_NUMBER);
		gtk_editable_set_text(GTK_EDITABLE(entry), defVal);
		gtk_widget_set_hexpand(entry, TRUE);
		gtk_widget_set_tooltip_text(entry, tooltip);
		gtk_grid_attach(GTK_GRID(grid_movement), entry, 1, row, 1, 1);
	};

	int movement_row = 0;
	addMovementParam("Altura segura (Z):", entry_drl_safe_z, "5.0", movement_row++, "Altura segura para movimientos rápidos entre taladros (mm)");
	addMovementParam("Velocidad de viaje (X/Y, mm/min):", entry_drl_travel_feedrate, "500", movement_row++, "Velocidad de movimiento horizontal entre taladros (mm/min)");

	gtk_box_append(GTK_BOX(box_drl_params), frame_movement);

	// =========================================================================
	// FRAME: Configuración de Perforación
	// =========================================================================
	GtkWidget *frame_drilling = gtk_frame_new("Perforación");
	gtk_widget_set_tooltip_text(frame_drilling, "Parámetros relacionados con el proceso de perforación");
	GtkWidget *box_drilling = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_frame_set_child(GTK_FRAME(frame_drilling), box_drilling);

	GtkWidget *grid_drilling = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid_drilling), 8);
	gtk_grid_set_row_spacing(GTK_GRID(grid_drilling), 4);
	gtk_box_append(GTK_BOX(box_drilling), grid_drilling);

	auto addDrillingParam = [&](const char *label, GtkWidget *&entry, const char *defVal, int row, const char *tooltip) {
		GtkWidget *lbl = gtk_label_new(label);
		gtk_label_set_xalign(GTK_LABEL(lbl), 1.0);
		gtk_grid_attach(GTK_GRID(grid_drilling), lbl, 0, row, 1, 1);
		entry = gtk_entry_new();
		gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_NUMBER);
		gtk_editable_set_text(GTK_EDITABLE(entry), defVal);
		gtk_widget_set_hexpand(entry, TRUE);
		gtk_widget_set_tooltip_text(entry, tooltip);
		gtk_grid_attach(GTK_GRID(grid_drilling), entry, 1, row, 1, 1);
	};

	int drilling_row = 0;
	addDrillingParam("Velocidad Z antes de contacto (mm/min):", entry_drl_plunge_feedrate, "50", drilling_row++, "Velocidad de descenso inicial hasta la altura de contacto (mm/min)");
	addDrillingParam("Posición Z de contacto:", entry_drl_contact_z, "0.5", drilling_row++, "Altura a la que la broca toca el material (mm)");
	addDrillingParam("Velocidad de perforación (mm/min):", entry_drl_drill_feedrate, "200", drilling_row++, "Velocidad de perforación una vez en contacto con el material (mm/min)");
	addDrillingParam("Posición Z de perforación:", entry_drl_drill_z, "-2.0", drilling_row++, "Profundidad final de la perforación (valor negativo en mm)");

	gtk_box_append(GTK_BOX(box_drl_params), frame_drilling);

	// =========================================================================
	// FRAME: Configuración de Retracción
	// =========================================================================
	GtkWidget *frame_retraction = gtk_frame_new("Retracción");
	gtk_widget_set_tooltip_text(frame_retraction, "Parámetros para la retracción de la broca después de perforar");
	GtkWidget *box_retraction = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_frame_set_child(GTK_FRAME(frame_retraction), box_retraction);

	GtkWidget *grid_retraction = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid_retraction), 8);
	gtk_grid_set_row_spacing(GTK_GRID(grid_retraction), 4);
	gtk_box_append(GTK_BOX(box_retraction), grid_retraction);

	auto addRetractionParam = [&](const char *label, GtkWidget *&entry, const char *defVal, int row, const char *tooltip) {
		GtkWidget *lbl = gtk_label_new(label);
		gtk_label_set_xalign(GTK_LABEL(lbl), 1.0);
		gtk_grid_attach(GTK_GRID(grid_retraction), lbl, 0, row, 1, 1);
		entry = gtk_entry_new();
		gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_NUMBER);
		gtk_editable_set_text(GTK_EDITABLE(entry), defVal);
		gtk_widget_set_hexpand(entry, TRUE);
		gtk_widget_set_tooltip_text(entry, tooltip);
		gtk_grid_attach(GTK_GRID(grid_retraction), entry, 1, row, 1, 1);
	};

	int retraction_row = 0;
	addRetractionParam("Posición Z al retraer:", entry_drl_retract_z, "5.0", retraction_row++, "Altura a la que se retrae la broca después de perforar (mm)");
	addRetractionParam("Aceleración (mm/s²):", entry_drl_acceleration, "100", retraction_row++, "Aceleración para todos los movimientos (mm/s²)");

	gtk_box_append(GTK_BOX(box_drl_params), frame_retraction);

	gtk_box_append(GTK_BOX(box_drl_main), frame_drl_params);

	// Separador antes del botón
	GtkWidget *separator2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_margin_top(separator2, 6);
	gtk_widget_set_margin_bottom(separator2, 6);
	gtk_box_append(GTK_BOX(box_drl_main), separator2);

	// --- Botón para generar G-code de taladros ---
	btn_generar_drl_gcode = gtk_button_new_with_label("⚙ Generar G-Code para Taladros");
	gtk_widget_add_css_class(btn_generar_drl_gcode, "suggested-action");
	gtk_widget_set_tooltip_text(btn_generar_drl_gcode, "Generar código G para el archivo .drl cargado con los parámetros configurados");
	gtk_widget_set_margin_top(btn_generar_drl_gcode, 6);
	gtk_box_append(GTK_BOX(box_drl_main), btn_generar_drl_gcode);

	gtk_box_append(GTK_BOX(drill_box), frame_drl_main);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), drill_box, gtk_label_new("Taladrado .drl"));

	// =========================================================================
	// CONEXIÓN DE SEÑALES
	// =========================================================================
	g_signal_connect(btn_buscar_gerber, "clicked", G_CALLBACK(on_btn_buscar_clicked), this);
	g_signal_connect(btn_buscar_drl, "clicked", G_CALLBACK(on_btn_buscar_drl_clicked), this);
	g_signal_connect(btn_generar, "clicked", G_CALLBACK(on_btn_generar_clicked), this);
	g_signal_connect(btn_generar_drl_gcode, "clicked", G_CALLBACK(on_btn_generar_drl_clicked), this);

	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview_tools));
	g_signal_connect(sel, "changed", G_CALLBACK(on_row_selected), this);

	g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) {
		delete static_cast<GCodeWindow*>(data);
	}),
	this);

	// Configurar el drawing area
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area), on_draw, this, NULL);

	// Cargar herramientas al inicio
	cargarHerramientas();
}

GCodeWindow::~GCodeWindow() {
	if (window && GTK_IS_WINDOW(window)) {
		gtk_window_destroy(GTK_WINDOW(window));
	}
}

void GCodeWindow::show() {
	gtk_window_present(GTK_WINDOW(window));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers privados
// ─────────────────────────────────────────────────────────────────────────────
void GCodeWindow::mostrarError(const std::string &msg) {
	GtkAlertDialog *dlg = gtk_alert_dialog_new("Error");
	gtk_alert_dialog_set_detail(dlg, msg.c_str());
	gtk_alert_dialog_show(dlg, GTK_WINDOW(window));
	g_object_unref(dlg);
}

void GCodeWindow::actualizarInfoGerber(const gerber::GerberResult &res) {
	current_result = res;  // Guardar para el renderizado
	if (!res.ok) {
		gtk_label_set_text(GTK_LABEL(lbl_gbr_status), "⛔ Error al parsear");
		gtk_label_set_text(GTK_LABEL(lbl_gbr_unit), "—");
		gtk_label_set_text(GTK_LABEL(lbl_gbr_bounds), "—");
		gtk_label_set_text(GTK_LABEL(lbl_gbr_primitives), "—");
		gtk_label_set_text(GTK_LABEL(lbl_render_status), "Error: No se puede renderizar.");
		gtk_widget_queue_draw(drawing_area);  // Redibujar
		return;
	}

	gtk_label_set_text(GTK_LABEL(lbl_gbr_status), "✔ OK");
	gtk_label_set_text(GTK_LABEL(lbl_gbr_unit), res.unit.c_str());

	if (res.bounds.valid) {
		std::ostringstream ss;
		ss << std::fixed << std::setprecision(4) << res.bounds.width() << " × " << res.bounds.height() << " " << res.unit;
		gtk_label_set_text(GTK_LABEL(lbl_gbr_bounds), ss.str().c_str());
	} else {
		gtk_label_set_text(GTK_LABEL(lbl_gbr_bounds), "(sin geometría)");
	}

	size_t nSeg = 0, nArc = 0, nFlash = 0, nReg = 0;
	for (const auto &p : res.primitives) {
		switch (p.type) {
		case gerber::PrimType::Segment:
			nSeg++;
			break;
		case gerber::PrimType::Arc:
			nArc++;
			break;
		case gerber::PrimType::Flash:
			nFlash++;
			break;
		case gerber::PrimType::Region:
			nReg++;
			break;
		}
	}
	std::ostringstream sp;
	sp << res.primitives.size() << "  (seg:" << nSeg << "  arc:" << nArc << "  flash:" << nFlash << "  región:" << nReg << ")";
	gtk_label_set_text(GTK_LABEL(lbl_gbr_primitives), sp.str().c_str());

	//gtk_label_set_text(GTK_LABEL(lbl_render_status), "Listo para renderizar (" + std::to_string(res.primitives.size()) + " primitivas)");
	std::ostringstream oss;
	oss << "Listo para renderizar (" << res.primitives.size() << " primitivas)";
	gtk_label_set_text(GTK_LABEL(lbl_render_status), oss.str().c_str());

	gtk_widget_queue_draw(drawing_area);  // Redibujar el área
}

void GCodeWindow::mostrarGCode(const std::string &gcode, const std::string &savedPath) {
	size_t lines = 0;
	for (char c : gcode)
		if (c == '\n')
			lines++;
	std::ostringstream ls;
	ls << "Líneas: " << lines << "  (" << gcode.size() << " bytes)";
	gtk_label_set_text(GTK_LABEL(lbl_gcode_lines), ls.str().c_str());
	std::string pathLabel = "Guardado en: " + savedPath;
	gtk_label_set_text(GTK_LABEL(lbl_gcode_path), pathLabel.c_str());

	GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview_gcode));
	gtk_text_buffer_set_text(buf, gcode.c_str(), (gint) gcode.size());
	GtkTextIter start;
	gtk_text_buffer_get_start_iter(buf, &start);
	gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(textview_gcode), &start, 0.0, TRUE, 0.0, 0.0);
}

void GCodeWindow::cargarHerramientas() {
	gtk_list_store_clear(liststore_tools);
	auto tools = DatabaseHerramienta::ObtenerTodos();
	GtkTreeIter iter;
	for (const auto &t : tools) {
		gtk_list_store_append(liststore_tools, &iter);
		char diam[20];
		snprintf(diam, sizeof(diam), "%.3f", t.diametroCorte);
		std::string nombre = t.fabricante + " " + t.modelo;
		gtk_list_store_set(liststore_tools, &iter, 0, t.id.c_str(), 1, nombre.c_str(), 2, t.tipoPunta.c_str(), 3, diam, 4, std::to_string(t.numFlautas).c_str(), -1);
	}
}

std::string GCodeWindow::obtenerHerramientaSeleccionada() {
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview_tools));
	GtkTreeModel *model;
	GtkTreeIter iter;
	if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
		gchar *id = nullptr;
		gtk_tree_model_get(model, &iter, 0, &id, -1);
		std::string sid = id ? id : "";
		g_free(id);
		return sid;
	}
	return "";
}

// ─────────────────────────────────────────────────────────────────────────────
//  Callbacks
// ─────────────────────────────────────────────────────────────────────────────
void GCodeWindow::on_btn_buscar_clicked(GtkButton*, gpointer user_data) {
	auto *self = static_cast<GCodeWindow*>(user_data);
	GtkFileDialog *dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, "Seleccionar archivo Gerber");

	GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
	GtkFileFilter *fgbr = gtk_file_filter_new();
	gtk_file_filter_set_name(fgbr, "Archivos Gerber (*.gbr *.ger)");
	gtk_file_filter_add_pattern(fgbr, "*.gbr");
	gtk_file_filter_add_pattern(fgbr, "*.ger");
	g_list_store_append(filters, fgbr);
	g_object_unref(fgbr);

	GtkFileFilter *fall = gtk_file_filter_new();
	gtk_file_filter_set_name(fall, "Todos los archivos");
	gtk_file_filter_add_pattern(fall, "*");
	g_list_store_append(filters, fall);
	g_object_unref(fall);

	gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
	g_object_unref(filters);

	gtk_file_dialog_open(dialog, GTK_WINDOW(self->window), nullptr, +[](GObject *source, GAsyncResult *res, gpointer user_data) {
		auto *self = static_cast<GCodeWindow*>(user_data);
		GError *error = nullptr;
		GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, &error);
		if (file) {
			char *path = g_file_get_path(file);
			if (path) {
				gtk_editable_set_text(GTK_EDITABLE(self->entry_gerber_path), path);
				gerber::GerberParser parser;
				gerber::GerberResult result = parser.parseFile(path);
				self->actualizarInfoGerber(result);
				g_free(path);
			}
			g_object_unref(file);
		} else if (error) {
			g_error_free(error);
		}
		g_object_unref(source);
	},
	self);
}

void GCodeWindow::on_btn_generar_clicked(GtkButton*, gpointer user_data) {
	auto *self = static_cast<GCodeWindow*>(user_data);
	std::string tool_id = self->obtenerHerramientaSeleccionada();
	std::string gerberPath = gtk_editable_get_text(GTK_EDITABLE(self->entry_gerber_path));

	if (tool_id.empty()) {
		self->mostrarError("Selecciona una herramienta.");
		return;
	}
	if (gerberPath.empty()) {
		self->mostrarError("Selecciona un archivo Gerber.");
		return;
	}

	auto tools = DatabaseHerramienta::ObtenerTodos();
	HerramientaProps tool;
	bool found = false;
	for (const auto &t : tools) {
		if (t.id == tool_id) {
			tool = t;
			found = true;
			break;
		}
	}
	if (!found) {
		self->mostrarError("Herramienta no encontrada.");
		return;
	}

	gerber::GerberParser parser;
	gerber::GerberResult result = parser.parseFile(gerberPath);
	self->actualizarInfoGerber(result);

	if (!result.ok) {
		self->mostrarError("Error al parsear: " + result.error);
		return;
	}

	gerber::GCodeConfig cfg;
	cfg.feedRate = std::stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_feedrate)));
	cfg.plungeRate = std::stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_plunge_rate)));
	cfg.cutZ = std::stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_cut_z)));
	cfg.safeZ = std::stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_safe_z)));
	cfg.toolDiameter = tool.diametroCorte;
	cfg.metricMode = (result.unit == "MM");
	cfg.addComments = true;
	cfg.optimizePaths = true;
	if (result.bounds.valid) {
		cfg.offsetX = -result.bounds.minX;
		cfg.offsetY = -result.bounds.minY;
	}

	gerber::GCodeGenerator generator(cfg);
	std::string gcode = generator.generate(result);

	std::string outputPath = gerberPath + ".gcode";
	std::ofstream out(outputPath);
	if (!out) {
		self->mostrarError("No se pudo escribir: " + outputPath);
		return;
	}
	out << gcode;
	out.close();

	self->mostrarGCode(gcode, outputPath);
}

void GCodeWindow::on_row_selected(GtkTreeSelection*, gpointer user_data) {
	auto *self = static_cast<GCodeWindow*>(user_data);
	std::string id = self->obtenerHerramientaSeleccionada();
	if (id.empty())
		return;
	gtk_editable_set_text(GTK_EDITABLE(self->entry_herramienta_id), id.c_str());
	auto tools = DatabaseHerramienta::ObtenerTodos();
	for (const auto &t : tools) {
		if (t.id == id) {
			char buf[32];
			snprintf(buf, sizeof(buf), "%.3f mm", t.diametroCorte);
			gtk_editable_set_text(GTK_EDITABLE(self->entry_diametro), buf);
			break;
		}
	}
}

void GCodeWindow::on_btn_buscar_drl_clicked(GtkButton*, gpointer user_data) {
	auto *self = static_cast<GCodeWindow*>(user_data);
	GtkFileDialog *dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, "Seleccionar archivo Excellon (.drl)");

	// Configurar filtros
	GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
	GtkFileFilter *fdrl = gtk_file_filter_new();
	gtk_file_filter_set_name(fdrl, "Archivos Excellon (*.drl, *.txt)");
	gtk_file_filter_add_pattern(fdrl, "*.drl");
	gtk_file_filter_add_pattern(fdrl, "*.txt");
	g_list_store_append(filters, fdrl);
	g_object_unref(fdrl);

	GtkFileFilter *fall = gtk_file_filter_new();
	gtk_file_filter_set_name(fall, "Todos los archivos");
	gtk_file_filter_add_pattern(fall, "*");
	g_list_store_append(filters, fall);
	g_object_unref(fall);

	gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
	g_object_unref(filters);

	// Abrir diálogo asíncrono
	gtk_file_dialog_open(dialog, GTK_WINDOW(self->window), nullptr, +[](GObject *source, GAsyncResult *res, gpointer user_data) {
		auto *self = static_cast<GCodeWindow*>(user_data);
		GError *error = nullptr;
		GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, &error);
		if (file) {
			char *path = g_file_get_path(file);
			if (path) {
				gtk_editable_set_text(GTK_EDITABLE(self->entry_drl_path), path);
				drill::DrillParser parser;
				drill::DrillResult result = parser.parseFile(path);
				self->cargarDrillOperations(result);
				g_free(path);
			}
			g_object_unref(file);
		} else if (error) {
			self->mostrarErrorDrill("Error al abrir el archivo: " + std::string(error->message));
			g_error_free(error);
		}
		g_object_unref(source);
	},
	self);
}

void GCodeWindow::cargarDrillOperations(const drill::DrillResult &res) {
	current_drill_result = res;
	if (!res.ok) {
		gtk_label_set_text(GTK_LABEL(lbl_drl_status), "⛔ Error al parsear");
		gtk_label_set_text(GTK_LABEL(lbl_drl_operations), "—");
		std::ostringstream oss;
		oss << "Error: " << res.error;
		gtk_label_set_text(GTK_LABEL(lbl_render_status), oss.str().c_str());
		gtk_widget_queue_draw(drawing_area);
		return;
	}

	gtk_label_set_text(GTK_LABEL(lbl_drl_status), "✔ OK");
	std::ostringstream oss;
	oss << res.operations.size() << " taladros";
	gtk_label_set_text(GTK_LABEL(lbl_drl_operations), oss.str().c_str());

	oss.str("");
	oss << "Listo para renderizar (" << res.operations.size() << " taladros)";
	gtk_label_set_text(GTK_LABEL(lbl_render_status), oss.str().c_str());

	gtk_widget_queue_draw(drawing_area);
}

void GCodeWindow::mostrarErrorDrill(const std::string &msg) {
	GtkAlertDialog *dlg = gtk_alert_dialog_new("Error al cargar .drl");
	gtk_alert_dialog_set_detail(dlg, msg.c_str());
	gtk_alert_dialog_show(dlg, GTK_WINDOW(window));
	g_object_unref(dlg);
}

//void GCodeWindow::on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
//    auto* self = static_cast<GCodeWindow*>(user_data);
//
//    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
//    cairo_paint(cr);
//
//    // --- Renderizado de Gerber ---
//    if (self->current_result.ok && !self->current_result.primitives.empty()) {
//        const auto& bounds = self->current_result.bounds;
//        double scaleX = (width  - 40) / bounds.width();
//        double scaleY = (height - 40) / bounds.height();
//        double scale  = std::min(scaleX, scaleY);
//        if (scale <= 0) scale = 1.0;
//
//        double offsetX = (width  - bounds.width()  * scale) / 2.0;
//        double offsetY = (height - bounds.height() * scale) / 2.0;
//
//        cairo_translate(cr, offsetX, offsetY);
//        cairo_scale(cr, scale, scale);
//        cairo_translate(cr, -bounds.minX, -bounds.minY);
//
//        // Dibujar primitivas de Gerber (existente)
//        // ...
//    }
//
//    // --- Renderizado de taladros ---
//    if (self->current_drill_result.ok && !self->current_drill_result.operations.empty()) {
//        double scale = 1.0;
//        double offsetX = 20;
//        double offsetY = 20;
//
//        for (const auto& op : self->current_drill_result.operations) {
//            double x = op.x;
//            double y = op.y;
//            double diameter = op.diameter;
//
//            // Aplicar escala y offset
//            cairo_save(cr);
//            cairo_translate(cr, offsetX, offsetY);
//            cairo_scale(cr, 1.0, 1.0); // No escalar, usar unidades del archivo
//
//            // Dibujar un círculo para el taladro
//            cairo_set_source_rgb(cr, 0.8, 0.1, 0.1); // Rojo para taladros
//            cairo_set_line_width(cr, 1.0);
//            cairo_arc(cr, x, y, diameter / 2.0, 0, 2 * M_PI);
//            cairo_stroke_preserve(cr);
//            cairo_fill(cr);
//
//            cairo_restore(cr);
//        }
//    }
//
//    // Borde
//    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
//    cairo_set_line_width(cr, 1.0);
//    cairo_rectangle(cr, 10, 10, width - 20, height - 20);
//    cairo_stroke(cr);
//}

//void GCodeWindow::on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
//    auto* self = static_cast<GCodeWindow*>(user_data);
//
//    // Fondo blanco
//    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
//    cairo_paint(cr);
//
//    // Si no hay datos para renderizar, mostrar mensaje
//    if (!self->current_result.ok && !self->current_drill_result.ok) {
//        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
//        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
//        cairo_set_font_size(cr, 14);
//        cairo_move_to(cr, 20, 30);
//        cairo_show_text(cr, "Carga un archivo Gerber o .drl para renderizar.");
//        return;
//    }
//
//    // Calcular escala y offset (basado en Gerber si está cargado, sino en .drl)
//    double scale = 1.0;
//    double offsetX = 20;
//    double offsetY = 20;
//    double minX = 0, minY = 0;
//    double maxX = width - 40, maxY = height - 40;
//
//    if (self->current_result.ok && self->current_result.bounds.valid) {
//        const auto& bounds = self->current_result.bounds;
//        double scaleX = (width - 40) / bounds.width();
//        double scaleY = (height - 40) / bounds.height();
//        scale = std::min(scaleX, scaleY);
//        if (scale <= 0) scale = 1.0;
//
//        offsetX = (width - bounds.width() * scale) / 2.0;
//        offsetY = (height - bounds.height() * scale) / 2.0;
//        minX = bounds.minX;
//        minY = bounds.minY;
//    } else if (self->current_drill_result.ok && !self->current_drill_result.operations.empty()) {
//        // Si solo hay .drl, calcular bounds aproximados
//        double minDrillX = 1e18, minDrillY = 1e18;
//        double maxDrillX = -1e18, maxDrillY = -1e18;
//        for (const auto& op : self->current_drill_result.operations) {
//            if (op.x < minDrillX) minDrillX = op.x;
//            if (op.x > maxDrillX) maxDrillX = op.x;
//            if (op.y < minDrillY) minDrillY = op.y;
//            if (op.y > maxDrillY) maxDrillY = op.y;
//        }
//        double drillWidth = maxDrillX - minDrillX;
//        double drillHeight = maxDrillY - minDrillY;
//        if (drillWidth > 0 && drillHeight > 0) {
//            double scaleX = (width - 40) / drillWidth;
//            double scaleY = (height - 40) / drillHeight;
//            scale = std::min(scaleX, scaleY);
//            if (scale <= 0) scale = 1.0;
//
//            offsetX = (width - drillWidth * scale) / 2.0;
//            offsetY = (height - drillHeight * scale) / 2.0;
//            minX = minDrillX;
//            minY = minDrillY;
//        }
//    }
//
//    // Aplicar transformación
//    cairo_save(cr);
//    cairo_translate(cr, offsetX, offsetY);
//    cairo_scale(cr, scale, scale);
//    cairo_translate(cr, -minX, -minY);
//
//    // =========================================================================
//    // RENDERIZAR GERBER
//    // =========================================================================
//    if (self->current_result.ok && !self->current_result.primitives.empty()) {
//        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
//        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
//
//        for (const auto& p : self->current_result.primitives) {
//            if (p.type == gerber::PrimType::Segment) {
//                const auto& seg = p.asSegment();
//                cairo_set_source_rgb(cr, 0.18, 0.55, 0.18);  // Verde para pistas
//                cairo_set_line_width(cr, seg.width);
//                cairo_move_to(cr, seg.start.x, seg.start.y);
//                cairo_line_to(cr, seg.end.x, seg.end.y);
//                cairo_stroke(cr);
//            } else if (p.type == gerber::PrimType::Arc) {
//                const auto& a = p.asArc();
//                cairo_set_source_rgb(cr, 0.18, 0.55, 0.18);
//                cairo_set_line_width(cr, a.width);
//                if (a.ccw) {
//                    cairo_arc(cr, a.center.x, a.center.y, a.radius, a.startAngle, a.endAngle);
//                } else {
//                    cairo_arc_negative(cr, a.center.x, a.center.y, a.radius, a.startAngle, a.endAngle);
//                }
//                cairo_stroke(cr);
//            } else if (p.type == gerber::PrimType::Flash) {
//                const auto& f = p.asFlash();
//                cairo_set_source_rgb(cr, 0.8, 0.7, 0.0);  // Dorado para pads
//                if (f.aperture.type == gerber::ApertureType::Circle) {
//                    cairo_arc(cr, f.pos.x, f.pos.y, f.aperture.outer / 2.0, 0, 2 * M_PI);
//                    cairo_fill(cr);
//                }
//            } else if (p.type == gerber::PrimType::Region) {
//                const auto& reg = p.asRegion();
//                if (!reg.outline.empty()) {
//                    cairo_set_source_rgba(cr, 0.18, 0.55, 0.18, 0.85);
//                    cairo_move_to(cr, reg.outline[0].x, reg.outline[0].y);
//                    for (size_t i = 1; i < reg.outline.size(); i++) {
//                        cairo_line_to(cr, reg.outline[i].x, reg.outline[i].y);
//                    }
//                    cairo_close_path(cr);
//                    cairo_fill(cr);
//                }
//            }
//        }
//    }
//
//    // =========================================================================
//    // RENDERIZAR TALADROS (.drl)
//    // =========================================================================
//    if (self->current_drill_result.ok && !self->current_drill_result.operations.empty()) {
//        cairo_set_source_rgb(cr, 0.8, 0.1, 0.1);  // Rojo para taladros
//        for (const auto& op : self->current_drill_result.operations) {
//            cairo_arc(cr, op.x, op.y, op.diameter / 2.0, 0, 2 * M_PI);
//            cairo_fill(cr);
//        }
//    }
//
//    // Restablecer transformación
//    cairo_restore(cr);
//
//    // Dibujar borde
//    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
//    cairo_set_line_width(cr, 1.0);
//    cairo_rectangle(cr, 10, 10, width - 20, height - 20);
//    cairo_stroke(cr);
//}
// ─────────────────────────────────────────────────────────────────────────────
//  on_draw  –  Reemplaza la función existente en ui_gcode.cpp
//
//  Correcciones:
//    1. Flip del eje Y: Gerber tiene Y↑, Cairo tiene Y↓.
//       Sin flip los arcos se invierten → esquinas donde debería haber curvas.
//    2. Arcos: con Y negado, cairo_arc (CCW) aparece CW en pantalla, así que
//       se intercambian cairo_arc / cairo_arc_negative.
//    3. Line cap y line join ROUND se mantienen, pero ahora se restablecen
//       dentro del save/restore para no contaminarse entre llamadas.
// ─────────────────────────────────────────────────────────────────────────────

//void GCodeWindow::on_draw(GtkDrawingArea* /*area*/, cairo_t* cr, int width, int height, gpointer user_data)
//{
//    auto* self = static_cast<GCodeWindow*>(user_data);
//
//    // ── Fondo ────────────────────────────────────────────────────────────────
//    cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);   // fondo oscuro tipo PCB
//    cairo_paint(cr);
//
//    const bool haveGbr   = self->current_result.ok && !self->current_result.primitives.empty();
//    const bool haveDrill = self->current_drill_result.ok && !self->current_drill_result.operations.empty();
//
//    if (!haveGbr && !haveDrill) {
//        cairo_set_source_rgb(cr, 0.55, 0.55, 0.55);
//        cairo_select_font_face(cr, "Sans",
//                               CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
//        cairo_set_font_size(cr, 14);
//        cairo_move_to(cr, 20, 30);
//        cairo_show_text(cr, "Carga un archivo Gerber o .drl para renderizar.");
//        return;
//    }
//
//    // ── Calcular bounding box unificada (Gerber + taladros) ──────────────────
//    double bMinX =  1e18, bMinY =  1e18;
//    double bMaxX = -1e18, bMaxY = -1e18;
//
//    if (haveGbr && self->current_result.bounds.valid) {
//        const auto& b = self->current_result.bounds;
//        bMinX = std::min(bMinX, b.minX);  bMaxX = std::max(bMaxX, b.maxX);
//        bMinY = std::min(bMinY, b.minY);  bMaxY = std::max(bMaxY, b.maxY);
//    }
//    if (haveDrill) {
//        for (const auto& op : self->current_drill_result.operations) {
//            double r = op.diameter * 0.5;
//            bMinX = std::min(bMinX, op.x - r);  bMaxX = std::max(bMaxX, op.x + r);
//            bMinY = std::min(bMinY, op.y - r);  bMaxY = std::max(bMaxY, op.y + r);
//        }
//    }
//
//    const double margin   = 20.0;
//    const double bW       = bMaxX - bMinX;
//    const double bH       = bMaxY - bMinY;
//    if (bW < 1e-9 || bH < 1e-9) return;
//
//    const double availW   = width  - 2 * margin;
//    const double availH   = height - 2 * margin;
//    const double scale    = std::min(availW / bW, availH / bH);
//
//    // Centrar dentro del área disponible
//    const double drawW    = bW * scale;
//    const double drawH    = bH * scale;
//    const double offX     = margin + (availW - drawW) * 0.5;
//    const double offY     = margin + (availH - drawH) * 0.5;
//
//    // ── Transformación con FLIP de Y ─────────────────────────────────────────
//    //
//    //   Queremos:  screen_x = offX + (gx - bMinX) * scale
//    //              screen_y = offY + (bMaxY - gy) * scale     ← eje Y invertido
//    //
//    //   En Cairo (translate después de scale):
//    //     cairo_translate(offX - bMinX*scale,  offY + bMaxY*scale)
//    //     cairo_scale(scale, -scale)
//    //
//    cairo_save(cr);
//    cairo_translate(cr, offX - bMinX * scale, offY + bMaxY * scale);
//    cairo_scale(cr, scale, -scale);
//
//    // Calidad de renderizado
//    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
//
//    // ── RENDERIZAR GERBER ────────────────────────────────────────────────────
//    if (haveGbr) {
//        cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
//        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
//
//        for (const auto& p : self->current_result.primitives) {
//
//            // Polaridad Clear = substrae cobre → fondo oscuro
//            if (p.polarity == gerber::Polarity::Clear) {
//                cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
//            } else {
//                cairo_set_source_rgb(cr, 0.18, 0.72, 0.28);  // verde cobre
//            }
//
//            switch (p.type) {
//
//            case gerber::PrimType::Segment: {
//                const auto& seg = p.asSegment();
//                cairo_set_line_width(cr, seg.width > 0 ? seg.width : 0.01);
//                cairo_move_to(cr, seg.start.x, seg.start.y);
//                cairo_line_to(cr, seg.end.x,   seg.end.y);
//                cairo_stroke(cr);
//                break;
//            }
//
//            case gerber::PrimType::Arc: {
//                const auto& a = p.asArc();
//                cairo_set_line_width(cr, a.width > 0 ? a.width : 0.01);
//                // ── CORRECCIÓN CLAVE ──────────────────────────────────────
//                // Con Y negado, cairo_arc (CCW en coords usuario) aparece CW
//                // en pantalla.  Intercambiamos arc / arc_negative para que
//                // el sentido en pantalla coincida con el del Gerber.
//                if (a.ccw) {
//                    cairo_arc_negative(cr, a.center.x, a.center.y,
//                                       a.radius, a.startAngle, a.endAngle);
//                } else {
//                    cairo_arc(cr, a.center.x, a.center.y,
//                              a.radius, a.startAngle, a.endAngle);
//                }
//                cairo_stroke(cr);
//                break;
//            }
//
//            case gerber::PrimType::Flash: {
//                const auto& f = p.asFlash();
//                switch (f.aperture.type) {
//
//                case gerber::ApertureType::Circle: {
//                    double r = f.aperture.outer * 0.5;
//                    cairo_arc(cr, f.pos.x, f.pos.y, r, 0, 2 * M_PI);
//                    cairo_fill(cr);
//                    // Agujero interior (si lo tiene)
//                    if (f.aperture.inner > 0) {
//                        cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
//                        cairo_arc(cr, f.pos.x, f.pos.y, f.aperture.inner * 0.5, 0, 2 * M_PI);
//                        cairo_fill(cr);
//                    }
//                    break;
//                }
//
//                case gerber::ApertureType::Rectangle: {
//                    // Si es un Flash de tipo Rectangle, pero tiene ancho de pista (outer = width, height = 0),
//                    // dibujarlo como una línea (segmento)
//                    if (f.aperture.height == 0) {
//                        // Es una pista (track) con ancho f.aperture.outer
//                        cairo_set_line_width(cr, f.aperture.outer * scale);
//                        cairo_move_to(cr, f.pos.x - f.aperture.outer * 0.5, f.pos.y);
//                        cairo_line_to(cr, f.pos.x + f.aperture.outer * 0.5, f.pos.y);
//                        cairo_stroke(cr);
//                    } else {
//                        // Es un pad rectangular
//                        double hw = f.aperture.outer * 0.5;
//                        double hh = f.aperture.height * 0.5;
//                        double rot = f.aperture.rot * M_PI / 180.0;
//
//                        cairo_save(cr);
//                        cairo_translate(cr, f.pos.x, f.pos.y);
//                        cairo_rotate(cr, rot);
//                        cairo_rectangle(cr, -hw, -hh, hw * 2, hh * 2);
//                        cairo_fill(cr);
//                        cairo_restore(cr);
//                    }
//                    break;
//                }
//
//                case gerber::ApertureType::Oblong: {
//                    double hw  = f.aperture.outer  * 0.5;
//                    double hh  = (f.aperture.height > 0 ? f.aperture.height
//                                                        : f.aperture.outer) * 0.5;
//                    double rot = f.aperture.rot * M_PI / 180.0;
//
//                    if (f.aperture.type == gerber::ApertureType::Oblong) {
//                        // Oblong = rectángulo con extremos semicirculares
//                        // Aproximado como rectángulo redondeado
//                        double r = std::min(hw, hh);
//                        cairo_save(cr);
//                        cairo_translate(cr, f.pos.x, f.pos.y);
//                        cairo_rotate(cr, rot);
//                        // Dibujamos el rectángulo redondeado manualmente
//                        cairo_move_to(cr,  hw - r,  hh);
//                        cairo_arc(cr,  hw - r,  hh - r, r,  M_PI_2,       0);
//                        cairo_arc(cr,  hw - r, -hh + r, r,  0,            -M_PI_2);
//                        cairo_arc(cr, -hw + r, -hh + r, r, -M_PI_2,      -M_PI);
//                        cairo_arc(cr, -hw + r,  hh - r, r, -M_PI,        -3*M_PI_2);
//                        cairo_close_path(cr);
//                        cairo_fill(cr);
//                        cairo_restore(cr);
//                    } else {
//                        cairo_save(cr);
//                        cairo_translate(cr, f.pos.x, f.pos.y);
//                        cairo_rotate(cr, rot);
//                        cairo_rectangle(cr, -hw, -hh, hw * 2, hh * 2);
//                        cairo_fill(cr);
//                        cairo_restore(cr);
//                    }
//                    break;
//                }
//
//                case gerber::ApertureType::Polygon: {
//                    int    n   = std::max(3, f.aperture.sides);
//                    double r   = f.aperture.outer * 0.5;
//                    double rot = f.aperture.rot * M_PI / 180.0;
//                    cairo_move_to(cr,
//                        f.pos.x + r * std::cos(rot),
//                        f.pos.y + r * std::sin(rot));
//                    for (int k = 1; k < n; k++) {
//                        double a = rot + 2 * M_PI * k / n;
//                        cairo_line_to(cr, f.pos.x + r * std::cos(a), f.pos.y + r * std::sin(a));
//                    }
//                    cairo_close_path(cr);
//                    cairo_fill(cr);
//                    break;
//                }
//
//                default:
//                    // Macro u otro: fallback círculo
//                    cairo_arc(cr, f.pos.x, f.pos.y, f.aperture.outer * 0.5, 0, 2 * M_PI);
//                    cairo_fill(cr);
//                    break;
//                }
//                break;   // case Flash
//            }
//
//            case gerber::PrimType::Region: {
//                const auto& reg = p.asRegion();
//                if (reg.outline.size() < 2) {break;}
//                cairo_move_to(cr, reg.outline[0].x, reg.outline[0].y);
//                for (size_t i = 1; i < reg.outline.size(); i++) {
//                    cairo_line_to(cr, reg.outline[i].x, reg.outline[i].y);}
//                cairo_close_path(cr);
//                cairo_fill(cr);
//                break;
//            }
//
//            } // switch p.type
//        } // for primitives
//    } // haveGbr
//
//    // ── RENDERIZAR TALADROS ──────────────────────────────────────────────────
//    if (haveDrill) {
//        for (const auto& op : self->current_drill_result.operations) {
//            double r = op.diameter * 0.5;
//            if (r < 1e-9) {r = 0.15;}   // mínimo visible
//
//            // Anillo exterior (cobre pad de taladro)
//            cairo_set_source_rgb(cr, 0.9, 0.8, 0.1);   // dorado
//            cairo_arc(cr, op.x, op.y, r * 1.5, 0, 2 * M_PI);
//            cairo_fill(cr);
//
//            // Agujero real
//            cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
//            cairo_arc(cr, op.x, op.y, r, 0, 2 * M_PI);
//            cairo_fill(cr);
//        }
//    }
//
//    cairo_restore(cr);   // restaurar transformación
//
//    // ── Borde del área de visualización ──────────────────────────────────────
//    cairo_set_source_rgb(cr, 0.35, 0.35, 0.35);
//    cairo_set_line_width(cr, 1.0);
//    cairo_rectangle(cr, margin * 0.5, margin * 0.5, width  - margin, height - margin);
//    cairo_stroke(cr);
//}

/*
 * Intefaces/ui_gcode.cpp  —  PARCHE: función on_draw corregida
 *
 * PROBLEMA ORIGINAL
 * ─────────────────
 * La transformación  cairo_scale(cr, scale, -scale)  voltea el eje Y para
 * que el origen Gerber (esquina inferior-izquierda) quede arriba en pantalla.
 * Ese volteo INVIERTE la orientación de TODOS los polígonos:
 *   · Un polígono CCW en coords Gerber pasa a ser CW en espacio Cairo.
 * Con la regla de relleno por defecto (CAIRO_FILL_RULE_WINDING), Cairo
 * interpreta erróneamente la "parte interior" de los polígonos complejos
 * (cobre circundante a pistas), produciendo el efecto "cuadrados" descrito.
 *
 * CORRECCIONES APLICADAS
 * ──────────────────────
 * 1. Usar CAIRO_FILL_RULE_EVEN_ODD para regiones  → no depende de orientación.
 * 2. cairo_new_path() antes de cada primitiva      → evita acumulación de path.
 * 3. Mínimo de vértices corregido a < 3            → un polígono necesita ≥ 3.
 * 4. Renderizado de agujeros (reg.holes)           → contornos internos.
 * 5. Polaridad Clear con CAIRO_OPERATOR_CLEAR      → sustrae cobre real.
 * 6. Todos los brazos de if / for / switch escritos completamente.
 */

// ─────────────────────────────────────────────────────────────────────────────
//  Reemplaza ÚNICAMENTE el cuerpo de on_draw en ui_gcode.cpp
//  La firma no cambia:
//    void GCodeWindow::on_draw(GtkDrawingArea*, cairo_t* cr,
//                              int width, int height, gpointer user_data)
// ─────────────────────────────────────────────────────────────────────────────
void GCodeWindow::on_draw(GtkDrawingArea* /*area*/, cairo_t *cr, int width, int height, gpointer user_data) {
	auto *self = static_cast<GCodeWindow*>(user_data);

	// ── 0. Fondo oscuro (PCB substrate) ──────────────────────────────────────
	cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
	cairo_paint(cr);

	const bool haveGbr = self->current_result.ok && !self->current_result.primitives.empty();
	const bool haveDrill = self->current_drill_result.ok && !self->current_drill_result.operations.empty();

	if (!haveGbr && !haveDrill) {
		// Sin datos: mostrar mensaje
		cairo_set_source_rgb(cr, 0.55, 0.55, 0.55);
		cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
		cairo_set_font_size(cr, 14.0);
		cairo_move_to(cr, 20.0, 30.0);
		cairo_show_text(cr, "Carga un archivo Gerber o .drl para renderizar.");
		return;
	}

	// ── 1. Bounding box unificada (Gerber + taladros) ─────────────────────────
	double bMinX = 1e18, bMinY = 1e18;
	double bMaxX = -1e18, bMaxY = -1e18;

	if (haveGbr && self->current_result.bounds.valid) {
		const auto &b = self->current_result.bounds;
		if (b.minX < bMinX) {
			bMinX = b.minX;
		}
		if (b.maxX > bMaxX) {
			bMaxX = b.maxX;
		}
		if (b.minY < bMinY) {
			bMinY = b.minY;
		}
		if (b.maxY > bMaxY) {
			bMaxY = b.maxY;
		}
	}

	if (haveDrill) {
		for (const auto &op : self->current_drill_result.operations) {
			double r = op.diameter * 0.5;
			if (r < 1e-9) {
				r = 0.15;
			}

			if ((op.x - r) < bMinX) {
				bMinX = op.x - r;
			}
			if ((op.x + r) > bMaxX) {
				bMaxX = op.x + r;
			}
			if ((op.y - r) < bMinY) {
				bMinY = op.y - r;
			}
			if ((op.y + r) > bMaxY) {
				bMaxY = op.y + r;
			}
		}
	}

	const double margin = 20.0;
	const double bW = bMaxX - bMinX;
	const double bH = bMaxY - bMinY;

	if (bW < 1e-9 || bH < 1e-9) {
		// BBox degenerada, no dibujar
		return;
	}

	const double availW = static_cast<double>(width) - 2.0 * margin;
	const double availH = static_cast<double>(height) - 2.0 * margin;
	const double scale = std::min(availW / bW, availH / bH);

	const double drawW = bW * scale;
	const double drawH = bH * scale;
	const double offX = margin + (availW - drawW) * 0.5;
	const double offY = margin + (availH - drawH) * 0.5;

	// ── 2. Transformación global con flip de Y ────────────────────────────────
	//
	//   screen_x = offX + (gx - bMinX) * scale
	//   screen_y = offY + (bMaxY - gy) * scale      ← eje Y invertido
	//
	//   Equivalente Cairo:
	//     translate(offX - bMinX*scale,  offY + bMaxY*scale)
	//     scale(scale, -scale)
	//
	cairo_save(cr);
	cairo_translate(cr, offX - bMinX * scale, offY + bMaxY * scale);
	cairo_scale(cr, scale, -scale);
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

	// ── 3. RENDERIZAR GERBER ──────────────────────────────────────────────────
	if (haveGbr) {
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

		for (const auto &p : self->current_result.primitives) {
			// ── 3a. Determinar color / operador según polaridad ──────────────
			if (p.polaridad == gerber::Polarity::Clear) {
				// Polaridad Clear: sustrae cobre.
				// Usamos OPERATOR_CLEAR para borrar realmente lo que haya
				// debajo en lugar de sobreescribir con el color de fondo.
				cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
			} else {
				// Polaridad Dark: deposita cobre.
				cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
				cairo_set_source_rgb(cr, 0.18, 0.72, 0.28);  // verde cobre
			}

			// ── 3b. Dibujar según tipo de primitiva ──────────────────────────
			switch (p.type) {
			// ── SEGMENTO ─────────────────────────────────────────────────
			case gerber::PrimType::Segment: {
				const auto &seg = p.asSegment();
				const double w = (seg.width > 1e-9) ? seg.width : 0.01;

				cairo_new_path(cr);
				cairo_set_line_width(cr, w);
				cairo_move_to(cr, seg.start.x, seg.start.y);
				cairo_line_to(cr, seg.end.x, seg.end.y);
				cairo_stroke(cr);
				break;
			}

				// ── ARCO ─────────────────────────────────────────────────────
			case gerber::PrimType::Arc: {
				const auto &a = p.asArc();
				const double w = (a.width > 1e-9) ? a.width : 0.01;

				cairo_new_path(cr);
				cairo_set_line_width(cr, w);

				// Con Y negado, CCW en coords usuario → CW en pantalla.
				// Intercambiamos arc / arc_negative para conservar el
				// sentido visual correcto.
				if (a.ccw) {
					cairo_arc_negative(cr, a.center.x, a.center.y, a.radius, a.startAngle, a.endAngle);
				} else {
					cairo_arc(cr, a.center.x, a.center.y, a.radius, a.startAngle, a.endAngle);
				}

				cairo_stroke(cr);
				break;
			}

				// ── FLASH ─────────────────────────────────────────────────────
			case gerber::PrimType::Flash: {
				const auto &f = p.asFlash();
				const double cx = f.pos.x;
				const double cy = f.pos.y;

				switch (f.aperture.type) {
				// Flash circular ──────────────────────────────────────
				case gerber::ApertureType::Circle: {
					const double r = f.aperture.outer * 0.5;

					cairo_new_path(cr);
					cairo_arc(cr, cx, cy, r, 0.0, 2.0 * M_PI);
					cairo_fill(cr);

					// Agujero interior (annular ring)
					if (f.aperture.inner > 1e-9) {
						// Para el agujero siempre borramos,
						// independientemente de la polaridad del pad
						cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
						cairo_new_path(cr);
						cairo_arc(cr, cx, cy, f.aperture.inner * 0.5, 0.0, 2.0 * M_PI);
						cairo_fill(cr);

						// Restaurar operador al que tenía la primitiva
						if (p.polaridad == gerber::Polarity::Clear) {
							cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
						} else {
							cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
							cairo_set_source_rgb(cr, 0.18, 0.72, 0.28);
						}
					}
					break;
				}

					// Flash rectangular ───────────────────────────────────
				case gerber::ApertureType::Rectangle: {
					const double hw = f.aperture.outer * 0.5;
					const double hh = (f.aperture.height > 1e-9) ? f.aperture.height * 0.5 : hw;
					const double rot = f.aperture.rot * M_PI / 180.0;

					cairo_save(cr);
					cairo_translate(cr, cx, cy);
					cairo_rotate(cr, rot);
					cairo_new_path(cr);
					cairo_rectangle(cr, -hw, -hh, hw * 2.0, hh * 2.0);
					cairo_fill(cr);
					cairo_restore(cr);

					// Agujero interior
					if (f.aperture.inner > 1e-9) {
						cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
						cairo_new_path(cr);
						cairo_arc(cr, cx, cy, f.aperture.inner * 0.5, 0.0, 2.0 * M_PI);
						cairo_fill(cr);

						if (p.polaridad == gerber::Polarity::Clear) {
							cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
						} else {
							cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
							cairo_set_source_rgb(cr, 0.18, 0.72, 0.28);
						}
					}
					break;
				}

					// Flash oblongo (oblong / rounded rect) ───────────────
				case gerber::ApertureType::Oblong: {
					const double hw = f.aperture.outer * 0.5;
					const double hh = (f.aperture.height > 1e-9) ? f.aperture.height * 0.5 : hw;
					const double r = std::min(hw, hh);
					const double rot = f.aperture.rot * M_PI / 180.0;

					cairo_save(cr);
					cairo_translate(cr, cx, cy);
					cairo_rotate(cr, rot);
					cairo_new_path(cr);

					// Rectángulo redondeado = oblong
					cairo_move_to(cr, hw - r, hh);
					cairo_arc(cr, hw - r, hh - r, r, M_PI_2, 0.0);
					cairo_arc(cr, hw - r, -hh + r, r, 0.0, -M_PI_2);
					cairo_arc(cr, -hw + r, -hh + r, r, -M_PI_2, -M_PI);
					cairo_arc(cr, -hw + r, hh - r, r, -M_PI, -3.0 * M_PI_2);
					cairo_close_path(cr);
					cairo_fill(cr);
					cairo_restore(cr);

					if (f.aperture.inner > 1e-9) {
						cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
						cairo_new_path(cr);
						cairo_arc(cr, cx, cy, f.aperture.inner * 0.5, 0.0, 2.0 * M_PI);
						cairo_fill(cr);

						if (p.polaridad == gerber::Polarity::Clear) {
							cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
						} else {
							cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
							cairo_set_source_rgb(cr, 0.18, 0.72, 0.28);
						}
					}
					break;
				}

					// Flash poligonal ─────────────────────────────────────
				case gerber::ApertureType::Polygon: {
					const int n = std::max(3, f.aperture.sides);
					const double r = f.aperture.outer * 0.5;
					const double rot = f.aperture.rot * M_PI / 180.0;

					cairo_new_path(cr);
					cairo_move_to(cr, cx + r * std::cos(rot), cy + r * std::sin(rot));

					for (int k = 1; k < n; k++) {
						const double a = rot + 2.0 * M_PI * k / n;
						cairo_line_to(cr, cx + r * std::cos(a), cy + r * std::sin(a));
					}

					cairo_close_path(cr);
					cairo_fill(cr);

					if (f.aperture.inner > 1e-9) {
						cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
						cairo_new_path(cr);
						cairo_arc(cr, cx, cy, f.aperture.inner * 0.5, 0.0, 2.0 * M_PI);
						cairo_fill(cr);

						if (p.polaridad == gerber::Polarity::Clear) {
							cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
						} else {
							cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
							cairo_set_source_rgb(cr, 0.18, 0.72, 0.28);
						}
					}
					break;
				}

					// Macro u otro: fallback círculo ──────────────────────
				default: {
					const double r = f.aperture.outer * 0.5;
					cairo_new_path(cr);
					cairo_arc(cr, cx, cy, r, 0.0, 2.0 * M_PI);
					cairo_fill(cr);
					break;
				}

				}  // switch aperture type
				break;
			}  // case Flash

				// ── REGIÓN ───────────────────────────────────────────────────
				//
				// CORRECCIÓN PRINCIPAL:
				//   • cairo_new_path()          — path limpio antes de empezar
				//   • CAIRO_FILL_RULE_EVEN_ODD  — no depende de orientación
				//     (el volteo de Y cambia CCW→CW y rompía WINDING)
				//   • Mínimo 3 vértices         — polígono degenerado si < 3
				//   • Agujeros (reg.holes)      — subpaths internos
				// ─────────────────────────────────────────────────────────────
				//                case gerber::PrimType::Region:
				//                {
				//                    const auto& reg = p.asRegion();
				//
				//                    // Necesitamos al menos 3 puntos para un polígono real
				//                    if (reg.outline.size() < 3)
				//                    {
				//                        break;
				//                    }
				//
				//                    cairo_new_path(cr);
				//
				//                    // ── Contorno exterior ────────────────────────────────────
				//                    cairo_move_to(cr, reg.outline[0].x, reg.outline[0].y);
				//
				//                    for (size_t i = 1; i < reg.outline.size(); i++)
				//                    {
				//                        cairo_line_to(cr, reg.outline[i].x, reg.outline[i].y);
				//                    }
				//
				//                    cairo_close_path(cr);
				//
				//                    // ── Agujeros interiores (subpaths) ───────────────────────
				//                    // Con EVEN_ODD, cada subpath adicional dentro del exterior
				//                    // actúa como un agujero automáticamente.
				//                    for (const auto& hole : reg.holes)
				//                    {
				//                        if (hole.size() >= 3)
				//                        {
				//                            cairo_move_to(cr, hole[0].x, hole[0].y);
				//
				//                            for (size_t i = 1; i < hole.size(); i++)
				//                            {
				//                                cairo_line_to(cr, hole[i].x, hole[i].y);
				//                            }
				//
				//                            cairo_close_path(cr);
				//                        }
				//                        else
				//                        {
				//                            // Agujero degenerado: ignorar
				//                            // (no rompemos el path principal)
				//                        }
				//                    }
				//
				//                    // ── Rellenar con regla EVEN-ODD ──────────────────────────
				//                    // CRÍTICO: esta regla es independiente de la orientación
				//                    // del polígono, por lo que el volteo de Y no la afecta.
				//                    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
				//                    cairo_fill(cr);
				//                    // Restaurar la regla por defecto para las demás primitivas
				//                    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
				//
				//                    break;
				//                }   // case Region

				// En Intefaces/ui_gcode.cpp, dentro de on_draw, case gerber::PrimType::Region:

				//                case gerber::PrimType::Region:
				//                  {
				//                    const auto& reg = p.asRegion();
				//
				//                    // 1. Validar que hay suficientes puntos
				//                    if (reg.outline.size() < 3) {
				//                      break;
				//                    }
				//
				//                    // 2. Configurar el operador según polaridad
				//                    if (p.polarity == gerber::Polarity::Clear) {
				//                        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
				//                    } else {
				//                        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
				//                        cairo_set_source_rgb(cr, 0.18, 0.72, 0.28);
				//                    }
				//
				//
				//                    // 3. Construir el path
				//                    cairo_new_path(cr);
				//
				//                    // Mover al primer punto
				//                    cairo_move_to(cr, reg.outline[0].x, reg.outline[0].y);
				//
				//                    // Dibujar líneas hasta el último punto
				//                    for (size_t i = 1; i < reg.outline.size(); i++) {
				//                      cairo_line_to(cr, reg.outline[i].x, reg.outline[i].y);
				//                    }
				//
				//                    // 4. CERRAR EL CAMINO EXPLÍCITAMENTE
				//                    // Esto conecta el último punto con el primero.
				//                    // Si no haces esto, Cairo no sabe que es un polígono cerrado y puede dibujar mal.
				//                    cairo_close_path(cr);
				//
				//                    // 5. Rellenar
				//                    // Si la región tiene agujeros (holes), deberías dibujarlos como sub-paths.
				//                    // Pero en Gerber simple, a veces los agujeros se manejan con otra región Clear.
				//                    // Si tu struct 'Region' tiene 'holes', dibújalos aquí con cairo_move_to/line_to/close_path
				//                    // antes de hacer cairo_fill().
				//
				//                    cairo_fill(cr);
				//
				//                    // Restaurar operador por defecto para no afectar a otros objetos
				//                    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
				//                    break;
				//                  }// case Region
			case gerber::PrimType::Region: {
				const auto &reg = p.asRegion();

				// 1. Validación de seguridad
				if (reg.outline.empty()) {
					break;
				}

				// 2. Gestión de Polaridad (Crucial para aislamiento en KiCad)
				// Si la polaridad es CLEAR, usamos el operador CLEAR de Cairo para "perforar" el cobre.
				if (p.polaridad == gerber::Polarity::Clear) {
					cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
				} else {
					cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
					// Color de cobre (Verde circuitería)
					cairo_set_source_rgb(cr, 0.18, 0.72, 0.28);
				}

				// 3. Construcción del Polígono
				cairo_new_path(cr);

				// Importante: Movemos al punto inicial
				cairo_move_to(cr, reg.outline[0].x, reg.outline[0].y);

				// Dibujamos el contorno.
				// Si aparecen "cuadrados" aquí, es porque el parser está metiendo puntos extra
				// o porque se está usando una apertura. En modo REGION no se usa apertura.
				for (size_t i = 1; i < reg.outline.size(); ++i) {
					cairo_line_to(cr, reg.outline[i].x, reg.outline[i].y);
				}

				// 4. EL CIERRE ES LA CLAVE
				// cairo_close_path conecta el último punto con el primero automáticamente
				// eliminando huecos en líneas inclinadas.
				cairo_close_path(cr);

				// 5. Relleno sólido
				cairo_fill(cr);

				// Restaurar operador por defecto
				cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
				break;
			}  // case Region

			}  // switch p.type

		}  // for primitives

		// Restaurar operador por defecto al salir del bloque Gerber
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	}  // if haveGbr

	// ── 4. RENDERIZAR TALADROS ────────────────────────────────────────────────
	if (haveDrill) {
		for (const auto &op : self->current_drill_result.operations) {
			double r = op.diameter * 0.5;

			if (r < 1e-9) {
				r = 0.15;  // radio mínimo visible
			}

			// Anillo de cobre (pad) alrededor del taladro
			cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
			cairo_set_source_rgb(cr, 0.90, 0.80, 0.10);  // dorado
			cairo_new_path(cr);
			cairo_arc(cr, op.x, op.y, r * 1.5, 0.0, 2.0 * M_PI);
			cairo_fill(cr);

			// Agujero real (borra el centro del pad)
			cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
			cairo_new_path(cr);
			cairo_arc(cr, op.x, op.y, r, 0.0, 2.0 * M_PI);
			cairo_fill(cr);

			// Restaurar operador para el siguiente taladro
			cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		}
	}

	cairo_restore(cr);  // ← deshace translate+scale del paso 2

	// ── 5. Borde decorativo del área de visualización ─────────────────────────
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_set_source_rgb(cr, 0.35, 0.35, 0.35);
	cairo_set_line_width(cr, 1.0);
	cairo_new_path(cr);
	cairo_rectangle(cr, margin * 0.5, margin * 0.5, static_cast<double>(width) - margin, static_cast<double>(height) - margin);
	cairo_stroke(cr);
}

void GCodeWindow::on_btn_generar_drl_clicked(GtkButton*, gpointer user_data) {
	auto *self = static_cast<GCodeWindow*>(user_data);
	std::string tool_id = self->obtenerHerramientaSeleccionada();
	std::string drillPath = gtk_editable_get_text(GTK_EDITABLE(self->entry_drl_path));

	if (tool_id.empty()) {
		self->mostrarErrorDrill("Selecciona una herramienta.");
		return;
	}
	if (drillPath.empty()) {
		self->mostrarErrorDrill("Selecciona un archivo .drl.");
		return;
	}

	// Obtener la herramienta seleccionada
	auto tools = DatabaseHerramienta::ObtenerTodos();
	HerramientaProps tool;
	bool found = false;
	for (const auto &t : tools) {
		if (t.id == tool_id) {
			tool = t;
			found = true;
			break;
		}
	}
	if (!found) {
		self->mostrarErrorDrill("Herramienta no encontrada.");
		return;
	}

	// Parsear el archivo .drl
	drill::DrillParser parser;
	drill::DrillResult result = parser.parseFile(drillPath);
	if (!result.ok) {
		self->mostrarErrorDrill("Error al parsear: " + result.error);
		return;
	}

	// Obtener parámetros de la interfaz
	double safeZ = std::stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_drl_safe_z)));
	double travelFeedrate = std::stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_drl_travel_feedrate)));
	double plungeFeedrate = std::stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_drl_plunge_feedrate)));
	double contactZ = std::stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_drl_contact_z)));
	double drillFeedrate = std::stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_drl_drill_feedrate)));
	double drillZ = std::stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_drl_drill_z)));
	double retractZ = std::stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_drl_retract_z)));
	double acceleration = std::stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_drl_acceleration)));

	// Generar G-code para taladros
	std::string gcode = self->generarGCodeTaladros(result, tool, safeZ, travelFeedrate, plungeFeedrate, contactZ, drillFeedrate, drillZ, retractZ, acceleration);

	// Guardar en archivo
	std::string outputPath = drillPath + "_drill.gcode";
	std::ofstream out(outputPath);
	if (!out) {
		self->mostrarErrorDrill("No se pudo escribir: " + outputPath);
		return;
	}
	out << gcode;
	out.close();

	// Mostrar el G-code generado en la pestaña de G-Code
	self->mostrarGCode(gcode, outputPath);
}

//std::string GCodeWindow::generarGCodeTaladros(const drill::DrillResult& drillResult,
//                                              const HerramientaProps& tool,
//                                              double safeZ, double travelFeedrate, double plungeFeedrate,
//                                              double contactZ, double drillFeedrate, double drillZ,
//                                              double retractZ, double acceleration) {
//    std::ostringstream gcode;
//
//    // Encabezado
//    gcode << "G21 ; Unidades métricas\n";
//    gcode << "G90 ; Modo absoluto\n";
//    gcode << "G17 ; Plano XY\n";
//    gcode << "G40 ; Compensación de herramienta desactivada\n";
//    gcode << "G49 ; Compensación de longitud de herramienta desactivada\n";
//    gcode << "G80 ; Cancelar ciclo de taladrado\n";
//    gcode << "M06 T" << tool.id << " ; Seleccionar herramienta " << tool.fabricante << " " << tool.modelo << "\n";
//    gcode << "M03 S" << static_cast<int>(tool.rpm) << " ; Encender spindle a " << tool.rpm << " RPM\n";
//    gcode << "G43 H" << tool.id << " ; Compensación de longitud de herramienta\n";
//    gcode << "\n";
//
//    // Configurar aceleración (si la máquina lo soporta)
//    gcode << "M204 S" << acceleration << " ; Aceleración (mm/s²)\n";
//    gcode << "\n";
//
//    // Mover a altura segura
//    gcode << "G0 Z" << safeZ << " F" << travelFeedrate << " ; Subir a altura segura\n";
//    gcode << "\n";
//
//    // Generar código para cada taladro
//    for (size_t i = 0; i < drillResult.operations.size(); i++) {
//        const auto& op = drillResult.operations[i];
//        gcode << "G0 X" << op.x << " Y" << op.y << " F" << travelFeedrate << " ; Mover a posición " << (i+1) << "\n";
//        gcode << "G0 Z" << safeZ << " ; Asegurar altura segura\n";
//        gcode << "G1 Z" << contactZ << " F" << plungeFeedrate << " ; Bajar a altura de contacto\n";
//        gcode << "G1 Z" << drillZ << " F" << drillFeedrate << " ; Perforar a profundidad\n";
//        gcode << "G0 Z" << retractZ << " F" << plungeFeedrate << " ; Retraer a altura segura\n";
//        gcode << "\n";
//    }
//
//    // Finalizar
//    gcode << "G0 Z" << safeZ << " ; Subir a altura segura\n";
//    gcode << "M05 ; Apagar spindle\n";
//    gcode << "G28 G91 Z0 ; Retraer eje Z\n";
//    gcode << "G28 G91 X0 Y0 ; Retraer ejes X e Y\n";
//    gcode << "G90 ; Volver a modo absoluto\n";
//    gcode << "M30 ; Fin del programa\n";
//
//    return gcode.str();
//}
std::string GCodeWindow::generarGCodeTaladros(const drill::DrillResult &drillResult, const HerramientaProps &tool, double safeZ, double travelFeedrate, double plungeFeedrate, double contactZ,
		double drillFeedrate, double drillZ, double retractZ, double acceleration) {
	std::ostringstream gcode;

	// =========================================================================
	// ENCABEZADO
	// =========================================================================
	gcode << "O1000 ; Programa de taladrado generado por GerberTronix\n";
	gcode << "G21 ; Unidades métricas\n";
	gcode << "G90 ; Modo absoluto\n";
	gcode << "G17 ; Plano XY\n";
	gcode << "G40 ; Compensación de herramienta desactivada\n";
	gcode << "G49 ; Compensación de longitud de herramienta desactivada\n";
	gcode << "G80 ; Cancelar ciclo de taladrado\n";
	gcode << "M06 T" << tool.id << " ; Seleccionar herramienta: " << tool.fabricante << " " << tool.modelo << "\n";
	gcode << "M03 S" << static_cast<int>(tool.rpm) << " ; Encender spindle a " << tool.rpm << " RPM\n";
	gcode << "G43 H" << tool.id << " ; Aplicar compensación de longitud de herramienta\n";

	// Configurar aceleración (si la máquina lo soporta)
	gcode << "M204 S" << acceleration << " ; Aceleración: " << acceleration << " mm/s²\n";
	gcode << "\n";

	// Mover a altura segura al inicio
	gcode << "G0 Z" << safeZ << " F" << travelFeedrate << " ; Subir a altura segura inicial\n";
	gcode << "\n";

	// =========================================================================
	// CICLO DE TALADRADO PARA CADA OPERACIÓN
	// =========================================================================
	for (size_t i = 0; i < drillResult.operations.size(); i++) {
		const auto &op = drillResult.operations[i];

		// 1. Movimiento rápido a la posición X,Y del taladro
		gcode << "G0 X" << op.x << " Y" << op.y << " F" << travelFeedrate << " ; Movimiento rápido a posición de taladro " << (i + 1) << "\n";

		// 2. Bajar a altura de contacto (con velocidad de plunge)
		gcode << "G1 Z" << contactZ << " F" << plungeFeedrate << " ; Bajar a altura de contacto (" << contactZ << " mm)\n";

		// 3. Perforar a profundidad final (con velocidad de perforación)
		gcode << "G1 Z" << drillZ << " F" << drillFeedrate << " ; Perforar a profundidad final (" << drillZ << " mm)\n";

		// 4. RETRACCIÓN: Subir a altura de contacto (con velocidad de perforación)
		gcode << "G1 Z" << contactZ << " F" << drillFeedrate << " ; Retraer a altura de contacto (misma velocidad de perforación)\n";

		// 5. Subir a altura segura (con velocidad de viaje)
		gcode << "G0 Z" << safeZ << " F" << travelFeedrate << " ; Subir a altura segura (velocidad de viaje)\n";
		gcode << "\n";
	}

	// =========================================================================
	// FINALIZACIÓN
	// =========================================================================
	// Asegurar que la broca está en altura segura
	gcode << "G0 Z" << safeZ << " ; Asegurar altura segura final\n";

	// Apagar spindle y retraer ejes
	gcode << "M05 ; Apagar spindle\n";
	gcode << "G28 G91 Z0 ; Retraer eje Z a home\n";
	gcode << "G28 G91 X0 Y0 ; Retraer ejes X e Y a home\n";
	gcode << "G90 ; Volver a modo absoluto\n";

	// Fin del programa
	gcode << "M30 ; Fin del programa\n";

	return gcode.str();
}
