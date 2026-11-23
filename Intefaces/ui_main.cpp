/*
 * Intefaces/ui_main.cpp
 *
 *  Created on: 13 abr 2026
 *      Author: DjSteker
 */

#include "ui_main.h"
#include "ui_materials.h"
#include "ui_tools.h"
#include "ui_gcode.h"
#include <iostream>

MainWindow::MainWindow() {
	// GTK4: gtk_window_new() NO toma argumentos (se eliminó GTK_WINDOW_TOPLEVEL)
	window = gtk_window_new();

	gtk_window_set_title(GTK_WINDOW(window), "Gerber Manager - Menú Principal");
	gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);

	// Crear caja de márgenes (GTK4 usa GtkBox para espaciado)
	GtkWidget *margin_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_margin_start(margin_box, 20);
	gtk_widget_set_margin_end(margin_box, 20);
	gtk_widget_set_margin_top(margin_box, 20);
	gtk_widget_set_margin_bottom(margin_box, 20);

	grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 15);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 15);

	// GTK4: gtk_box_append() para añadir elementos a GtkBox
	gtk_box_append(GTK_BOX(margin_box), grid);

	// GTK4: gtk_window_set_child() en lugar de gtk_container_set_child
	gtk_window_set_child(GTK_WINDOW(window), margin_box);

	// Botón Materiales
	btn_materiales = gtk_button_new_with_label("Gestión de Materiales");
	gtk_grid_attach(GTK_GRID(grid), btn_materiales, 0, 0, 1, 1);
	g_signal_connect(btn_materiales, "clicked", G_CALLBACK(on_btn_materiales_clicked), this);

	// Botón Herramientas
	btn_herramientas = gtk_button_new_with_label("Gestor de Herramientas");
	gtk_grid_attach(GTK_GRID(grid), btn_herramientas, 0, 1, 1, 1);
	g_signal_connect(btn_herramientas, "clicked", G_CALLBACK(on_btn_herramientas_clicked), this);

	// Botón G-Code
	btn_gcode = gtk_button_new_with_label("Generar G-Code");
	gtk_grid_attach(GTK_GRID(grid), btn_gcode, 0, 2, 1, 1);
	g_signal_connect(btn_gcode, "clicked", G_CALLBACK(on_btn_gcode_clicked), this);

	// Botón Perfiles
	btn_perfiles = gtk_button_new_with_label("Perfiles de Trabajo");
	gtk_grid_attach(GTK_GRID(grid), btn_perfiles, 0, 3, 1, 1);
	g_signal_connect(btn_perfiles, "clicked", G_CALLBACK(on_btn_perfiles_clicked), this);

	g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget *w, gpointer data) {
		// 'data' es el puntero 'this' pasado al constructor
		MainWindow *self = static_cast<MainWindow*>(data);
		if (self) {
			delete self;  // Libera la memoria del objeto MainWindow
		}
	}),
	this);
}

MainWindow::~MainWindow() {
	// GTK4: gtk_window_destroy() en lugar de gtk_widget_destroy()
	//gtk_window_destroy(GTK_WINDOW(window));
	// Si llegamos aquí, significa que el objeto se está destruyendo.
	// No necesitamos llamar a gtk_window_destroy() aquí porque la señal 'destroy'
	// ya se disparó antes de que el destructor de C++ se llamara.
	// Pero por seguridad, si la ventana sigue viva (caso raro), la destruimos.
	if (window && gtk_widget_get_parent(window)) {
		gtk_window_destroy(GTK_WINDOW(window));
	}
}

void MainWindow::show() {
	// GTK4: gtk_window_present() en lugar de gtk_widget_show()
	gtk_window_present(GTK_WINDOW(window));
}

void MainWindow::on_btn_materiales_clicked(GtkButton *button, gpointer user_data) {
	(void) button;     // Evitar advertencia de variable no usada
	(void) user_data;  // user_data no se necesita aquí

	// Instanciar ventana de materiales
	MaterialWindow *mat_win = new MaterialWindow();
	mat_win->show();
}

void MainWindow::on_btn_herramientas_clicked(GtkButton *button, gpointer user_data) {
	(void) button;
	(void) user_data;
	ToolWindow *tool_win = new ToolWindow();
	tool_win->show();
}

void MainWindow::on_btn_gcode_clicked(GtkButton*, gpointer) {
	GCodeWindow *win = new GCodeWindow();
	win->show();
}

void MainWindow::on_btn_perfiles_clicked(GtkButton *button, gpointer user_data) {
	(void) button;
	(void) user_data;
	std::cout << "Abrir Perfiles de Trabajo...\n";
}
