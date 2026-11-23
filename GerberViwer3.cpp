//== == ==
// Nombre        : GerberViwer3.cpp
// Autor         : DjSteker
// Version       : V3.1
// Copyright     :
// Descripcion   : Visor GTK4 para archivos Gerber RS-274X + Isolation Routing correcto
//                 • Renderizado Cairo correcto (arcos via polyline, regiones, flashes)
//                 • Visualizacion del recorrido CNC (rapids + cuts derivados del GCode)
//                 • Panel lateral: info, capas, configuracion GCode
//== == ==

#include <gtk/gtk.h>
#include "interfaz.h"   // EstadoApp, construirUI, estado

// Definición del puntero global (declarado extern en interfaz.h)
EstadoApp *estado = nullptr;

static void on_activate(GtkApplication *app, gpointer) {
	construirUI(app);
}

int main(int argc, char *argv[]) {
	EstadoApp state;
	estado = &state;

	GtkApplication *app = gtk_application_new("com.example.gerber_viewer", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);

	int rc = g_application_run(G_APPLICATION(app), argc, argv);
	printf("✓ Visor Gerber finalizado\n");
	fflush(stdout);
	g_object_unref(app);
	return rc;
}

