/*
 * Intefaces/ui_tools.cpp
 *
 * Created on: 14 abr 2026
 * Author: DjSteker
 */

#include "ui_tools.h"
#include "../BaseDatos/DatabaseHerramienta.h"
#include <iostream>
#include <cstdio>

ToolWindow::ToolWindow() {
	window = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(window), "Gestión de Herramientas CNC");
	gtk_window_set_default_size(GTK_WINDOW(window), 1100, 700);

	GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_window_set_child(GTK_WINDOW(window), main_box);

	// TreeView (resumen)
	liststore = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(liststore));

	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview), -1, "ID", renderer, "text", 0, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview), -1, "Herramienta", renderer, "text", 1, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview), -1, "Tipo", renderer, "text", 2, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview), -1, "Ø Corte", renderer, "text", 3, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview), -1, "Flautas", renderer, "text", 4, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview), -1, "Ubicación", renderer, "text", 5, NULL);

	GtkWidget *scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), treeview);
	gtk_widget_set_vexpand(scrolled, TRUE);
	gtk_box_append(GTK_BOX(main_box), scrolled);

	// Grid con todos los campos
	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_widget_set_margin_start(grid, 12);
	gtk_widget_set_margin_end(grid, 12);
	gtk_widget_set_margin_top(grid, 8);

	int row = 0;

	auto add_label_entry = [&](const char *label_text, GtkWidget *&entry, int col) {
		gtk_grid_attach(GTK_GRID(grid), gtk_label_new(label_text), col * 2, row, 1, 1);
		entry = gtk_entry_new();
		gtk_grid_attach(GTK_GRID(grid), entry, col * 2 + 1, row, 1, 1);
	};

	add_label_entry("ID:", entry_id, 0);
	row++;
	add_label_entry("Fabricante:", entry_fabricante, 0);
	add_label_entry("Modelo:", entry_modelo, 1);
	row++;
	add_label_entry("Material:", entry_material, 0);
	add_label_entry("Costo:", entry_costo, 1);
	row++;
	add_label_entry("Ø Corte (mm):", entry_diam_corte, 0);
	add_label_entry("Ø Mango (mm):", entry_diam_mango, 1);
	row++;
	add_label_entry("Long. Corte (mm):", entry_long_corte, 0);
	add_label_entry("Long. Total (mm):", entry_long_total, 1);
	row++;
	add_label_entry("Nº Flautas:", entry_num_flautas, 0);
	add_label_entry("Tipo Punta:", entry_tipo_punta, 1);
	row++;
	add_label_entry("Hélice:", entry_direccion_helice, 0);
	add_label_entry("RPM:", entry_rpm, 1);
	row++;
	add_label_entry("Feed Rate:", entry_feedrate, 0);
	add_label_entry("Chipload:", entry_chipload, 1);
	row++;
	add_label_entry("Stepdown:", entry_stepdown, 0);
	add_label_entry("Stepover:", entry_stepover, 1);
	row++;
	add_label_entry("Horas Uso:", entry_horas_uso, 0);
	add_label_entry("Ubicación:", entry_ubicacion, 1);
	row++;

	gtk_editable_set_editable(GTK_EDITABLE(entry_id), FALSE);

	// Broca
	gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Es Broca:"), 0, row, 1, 1);
	check_es_broca = gtk_check_button_new();
	gtk_grid_attach(GTK_GRID(grid), check_es_broca, 1, row, 1, 1);
	row++;

	add_label_entry("Ángulo Punta (°):", entry_angulo_punta, 0);
	add_label_entry("Tipo Broca:", entry_tipo_broca, 1);
	row++;

	gtk_box_append(GTK_BOX(main_box), grid);

	// Botones
	GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_widget_set_margin_start(btn_box, 12);
	gtk_widget_set_margin_end(btn_box, 12);
	gtk_widget_set_margin_bottom(btn_box, 12);

	GtkWidget *btn_add = gtk_button_new_with_label("Añadir");
	GtkWidget *btn_update = gtk_button_new_with_label("Actualizar");
	GtkWidget *btn_delete = gtk_button_new_with_label("Eliminar");
	GtkWidget *btn_close = gtk_button_new_with_label("Cerrar");

	gtk_box_append(GTK_BOX(btn_box), btn_add);
	gtk_box_append(GTK_BOX(btn_box), btn_update);
	gtk_box_append(GTK_BOX(btn_box), btn_delete);
	gtk_box_append(GTK_BOX(btn_box), btn_close);

	gtk_box_append(GTK_BOX(main_box), btn_box);

	// Signals
	g_signal_connect(btn_add, "clicked", G_CALLBACK(on_btn_add_clicked), this);
	g_signal_connect(btn_update, "clicked", G_CALLBACK(on_btn_update_clicked), this);
	g_signal_connect(btn_delete, "clicked", G_CALLBACK(on_btn_delete_clicked), this);
	g_signal_connect(btn_close, "clicked", G_CALLBACK(gtk_window_close), window);

	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
	g_signal_connect(sel, "changed", G_CALLBACK(on_row_selected), this);

	cargarLista();

	g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) {
		delete static_cast<ToolWindow*>(data);
	}),
	this);
}

ToolWindow::~ToolWindow() {
	if (window && gtk_widget_get_parent(window)) {
		gtk_window_destroy(GTK_WINDOW(window));
	}
}

void ToolWindow::show() {
	gtk_window_present(GTK_WINDOW(window));
}

// Función auxiliar segura para convertir string a double
static double safe_stod(const std::string &str, const std::string &fieldName) {
	if (str.empty()) {
		throw std::invalid_argument("El campo '" + fieldName + "' no puede estar vacío.");
	}
	try {
		return std::stod(str);
	} catch (const std::exception&) {
		throw std::invalid_argument("El valor '" + str + "' no es un número válido para '" + fieldName + "'.");
	}
}

// Función auxiliar segura para convertir string a int
static int safe_stoi(const std::string &str, const std::string &fieldName) {
	if (str.empty()) {
		throw std::invalid_argument("El campo '" + fieldName + "' no puede estar vacío.");
	}
	try {
		return std::stoi(str);
	} catch (const std::exception&) {
		throw std::invalid_argument("El valor '" + str + "' no es un entero válido para '" + fieldName + "'.");
	}
}

void ToolWindow::cargarLista() {
	gtk_list_store_clear(liststore);
	auto tools = DatabaseHerramienta::ObtenerTodos();

	GtkTreeIter iter;
	for (const auto &h : tools) {
		gtk_list_store_append(liststore, &iter);
		char diam[20];
		snprintf(diam, sizeof(diam), "%.2f", h.diametroCorte);

		std::string name = h.fabricante + " " + h.modelo;

		gtk_list_store_set(liststore, &iter, 0, h.id.c_str(), 1, name.c_str(), 2, h.tipoPunta.c_str(), 3, diam, 4, std::to_string(h.numFlautas).c_str(), 5, h.ubicacion.c_str(), -1);
	}
}

void ToolWindow::limpiarCampos() {
	gtk_editable_set_text(GTK_EDITABLE(entry_id), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_fabricante), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_modelo), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_material), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_costo), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_diam_corte), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_diam_mango), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_long_corte), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_long_total), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_num_flautas), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_tipo_punta), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_direccion_helice), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_rpm), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_feedrate), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_chipload), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_stepdown), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_stepover), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_horas_uso), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_ubicacion), "");
	gtk_check_button_set_active(GTK_CHECK_BUTTON(check_es_broca), FALSE);
	gtk_editable_set_text(GTK_EDITABLE(entry_angulo_punta), "");
	gtk_editable_set_text(GTK_EDITABLE(entry_tipo_broca), "");
}

void ToolWindow::on_btn_add_clicked(GtkButton*, gpointer user_data) {
	auto *self = static_cast<ToolWindow*>(user_data);
	HerramientaProps h;

	try {
		// 1. Obtener y validar ID
		h.id = gtk_editable_get_text(GTK_EDITABLE(self->entry_id));
		if (h.id.empty()) {
			h.id = DatabaseHerramienta::GenerarIDAutomatico();
			gtk_editable_set_text(GTK_EDITABLE(self->entry_id), h.id.c_str());
		}

		// 2. Obtener textos
		std::string txtFabricante = gtk_editable_get_text(GTK_EDITABLE(self->entry_fabricante));
		std::string txtModelo = gtk_editable_get_text(GTK_EDITABLE(self->entry_modelo));
		std::string txtMaterial = gtk_editable_get_text(GTK_EDITABLE(self->entry_material));
		std::string txtTipoPunta = gtk_editable_get_text(GTK_EDITABLE(self->entry_tipo_punta));
		std::string txtHelice = gtk_editable_get_text(GTK_EDITABLE(self->entry_direccion_helice));
		std::string txtUbicacion = gtk_editable_get_text(GTK_EDITABLE(self->entry_ubicacion));
		std::string txtTipoBroca = gtk_editable_get_text(GTK_EDITABLE(self->entry_tipo_broca));

		// 3. Convertir y validar numéricos con manejo de errores
		h.fabricante = txtFabricante;
		h.modelo = txtModelo;
		h.material = txtMaterial;
		h.costo = safe_stod(txtFabricante.empty() ? "0" : gtk_editable_get_text(GTK_EDITABLE(self->entry_costo)), "Costo");  // Nota: corregir lógica si el campo está vacío

		// Corrección: Obtener el texto específico de cada campo
		h.costo = safe_stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_costo)), "Costo");
		h.diametroCorte = safe_stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_diam_corte)), "Diámetro de Corte");
		h.diametroMango = safe_stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_diam_mango)), "Diámetro de Mango");
		h.longitudCorte = safe_stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_long_corte)), "Longitud de Corte");
		h.longitudTotal = safe_stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_long_total)), "Longitud Total");
		h.numFlautas = safe_stoi(gtk_editable_get_text(GTK_EDITABLE(self->entry_num_flautas)), "Número de Flautas");
		h.tipoPunta = txtTipoPunta;
		h.direccionHelice = txtHelice;
		h.rpm = safe_stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_rpm)), "RPM");
		h.feedRate = safe_stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_feedrate)), "Feed Rate");
		h.chipload = safe_stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_chipload)), "Chipload");
		h.stepdown = safe_stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_stepdown)), "Stepdown");
		h.stepover = safe_stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_stepover)), "Stepover");
		h.horasUso = safe_stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_horas_uso)), "Horas de Uso");
		h.ubicacion = txtUbicacion;
		h.esBroca = gtk_check_button_get_active(GTK_CHECK_BUTTON(self->check_es_broca));
		h.anguloPunta = safe_stod(gtk_editable_get_text(GTK_EDITABLE(self->entry_angulo_punta)), "Ángulo de Punta");
		h.tipoBroca = txtTipoBroca;

		// 4. Guardar en base de datos
		if (DatabaseHerramienta::Agregar(h)) {
			self->cargarLista();
			self->limpiarCampos();
			// Opcional: Mostrar mensaje de éxito
		} else {
			std::cerr << "Error al guardar la herramienta en la base de datos.\n";
			// Mostrar alerta GTK
			GtkAlertDialog *dlg = gtk_alert_dialog_new("Error de Guardado");
			gtk_alert_dialog_set_detail(dlg, "No se pudo guardar la herramienta. Verifique los permisos del archivo.");
			gtk_alert_dialog_show(dlg, GTK_WINDOW(self->window));
			g_object_unref(dlg);
		}

	} catch (const std::invalid_argument &e) {
		// Capturar el error de conversión y mostrarlo al usuario
		std::cerr << "Error de entrada: " << e.what() << "\n";

		GtkAlertDialog *dlg = gtk_alert_dialog_new("Entrada Inválida");
		gtk_alert_dialog_set_detail(dlg, e.what());
		gtk_alert_dialog_show(dlg, GTK_WINDOW(self->window));
		g_object_unref(dlg);

		// No limpiamos campos ni guardamos, dejamos que el usuario corrija
		return;
	} catch (const std::exception &e) {
		std::cerr << "Error inesperado: " << e.what() << "\n";
		GtkAlertDialog *dlg = gtk_alert_dialog_new("Error");
		gtk_alert_dialog_set_detail(dlg, "Ocurrió un error inesperado: ");
		gtk_alert_dialog_show(dlg, GTK_WINDOW(self->window));
		g_object_unref(dlg);
	}
}
void ToolWindow::on_btn_update_clicked(GtkButton*, gpointer user_data) {
	auto *self = static_cast<ToolWindow*>(user_data);
	std::string id = self->obtenerIdSeleccionado();
	if (id.empty()) {
		return;
	}

	HerramientaProps h;
	h.id = id;
	h.fabricante = gtk_editable_get_text(GTK_EDITABLE(self->entry_fabricante));
	h.modelo = gtk_editable_get_text(GTK_EDITABLE(self->entry_modelo));
	h.material = gtk_editable_get_text(GTK_EDITABLE(self->entry_material));
	h.costo = atof(gtk_editable_get_text(GTK_EDITABLE(self->entry_costo)));
	h.diametroCorte = atof(gtk_editable_get_text(GTK_EDITABLE(self->entry_diam_corte)));
	h.diametroMango = atof(gtk_editable_get_text(GTK_EDITABLE(self->entry_diam_mango)));
	h.longitudCorte = atof(gtk_editable_get_text(GTK_EDITABLE(self->entry_long_corte)));
	h.longitudTotal = atof(gtk_editable_get_text(GTK_EDITABLE(self->entry_long_total)));
	h.numFlautas = atoi(gtk_editable_get_text(GTK_EDITABLE(self->entry_num_flautas)));
	h.tipoPunta = gtk_editable_get_text(GTK_EDITABLE(self->entry_tipo_punta));
	h.direccionHelice = gtk_editable_get_text(GTK_EDITABLE(self->entry_direccion_helice));
	h.rpm = atof(gtk_editable_get_text(GTK_EDITABLE(self->entry_rpm)));
	h.feedRate = atof(gtk_editable_get_text(GTK_EDITABLE(self->entry_feedrate)));
	h.chipload = atof(gtk_editable_get_text(GTK_EDITABLE(self->entry_chipload)));
	h.stepdown = atof(gtk_editable_get_text(GTK_EDITABLE(self->entry_stepdown)));
	h.stepover = atof(gtk_editable_get_text(GTK_EDITABLE(self->entry_stepover)));
	h.horasUso = atof(gtk_editable_get_text(GTK_EDITABLE(self->entry_horas_uso)));
	h.ubicacion = gtk_editable_get_text(GTK_EDITABLE(self->entry_ubicacion));
	h.esBroca = gtk_check_button_get_active(GTK_CHECK_BUTTON(self->check_es_broca));
	h.anguloPunta = atof(gtk_editable_get_text(GTK_EDITABLE(self->entry_angulo_punta)));
	h.tipoBroca = gtk_editable_get_text(GTK_EDITABLE(self->entry_tipo_broca));

	DatabaseHerramienta::Modificar(h);
	self->cargarLista();
}

void ToolWindow::on_btn_delete_clicked(GtkButton*, gpointer user_data) {
	auto *self = static_cast<ToolWindow*>(user_data);
	std::string id = self->obtenerIdSeleccionado();
	if (!id.empty()) {
		DatabaseHerramienta::Eliminar(id);
		self->cargarLista();
		self->limpiarCampos();
	}
}

void ToolWindow::on_row_selected(GtkTreeSelection*, gpointer user_data) {
	auto *self = static_cast<ToolWindow*>(user_data);
	std::string id = self->obtenerIdSeleccionado();
	if (id.empty()) {
		return;
	}

	// Solo cargamos el ID por simplicidad (puedes expandir para cargar todos los campos)
	gtk_editable_set_text(GTK_EDITABLE(self->entry_id), id.c_str());
}

std::string ToolWindow::obtenerIdSeleccionado() {
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
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
