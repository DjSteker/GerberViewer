/*
 * interfaz.cpp
 *
 *  Created on: 14 may 2026
 *      Author: DjSteker
 *  Renderizado Cairo, callbacks GTK4 y construcción de la UI
 */

#include "interfaz.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "Procesos/ToolpathGenerator.h"
#include "Intefaces/ui_tools.h"
#include "Intefaces/ui_materials.h"
#include "Procesos/IslasCobre.h"

#include "Nucleo/interfaz_gcode.hpp"
#include "Procesos/unidades.h"

//#include <cstdio>
//#ifndef GERBER_DEBUG
//#define printf(...) ((void)0)
//#endif

void establecerEstado(const std::string &msg) {
	gtk_label_set_text(GTK_LABEL(estado->barraEstado), msg.c_str());
}

gerber::GCodeConfig leerConfigGCode() {
	gerber::GCodeConfig c;
	c.feedRate = gtk_spin_button_get_value(estado->spinAvance);
	c.plungeRate = gtk_spin_button_get_value(estado->spinPenetracion);
	c.safeZ = gtk_spin_button_get_value(estado->spinZSeguro);
	c.cutZ = gtk_spin_button_get_value(estado->spinZCorte);
	c.toolDiameter = gtk_spin_button_get_value(estado->spinDiamHerramienta);
	c.optimizePaths = gtk_check_button_get_active(estado->chkOptimizar);
	c.useArcCommands = gtk_check_button_get_active(estado->chkComandosArco);
	c.metricMode = gtk_check_button_get_active(estado->chkMetrico);
	c.addComments = true;
	return c;
}

void actualizarPanelInfo() {
	if (!estado->tieneArchivo) {
		gtk_label_set_markup(GTK_LABEL(estado->etqArchivo), "<i>Sin archivo</i>");
		gtk_label_set_text(GTK_LABEL(estado->etqUnidad), "—");
		gtk_label_set_text(GTK_LABEL(estado->etqTamano), "—");
		gtk_label_set_text(GTK_LABEL(estado->etqPrimitivas), "—");
		if (estado->etqMovimientos) {
			gtk_label_set_text(GTK_LABEL(estado->etqMovimientos), "—");
		}
		return;
	}
	const auto &r = estado->resultado;
	std::string fname = estado->rutaArchivo;
	size_t sl = fname.rfind('/');
	if (sl != std::string::npos) {
		fname = fname.substr(sl + 1);
	}
	gtk_label_set_text(GTK_LABEL(estado->etqArchivo), fname.c_str());
	gtk_label_set_text(GTK_LABEL(estado->etqUnidad), r.unit.c_str());

	std::ostringstream dim;
	if (r.bounds.valid) {
		dim << std::fixed << std::setprecision(3) << r.bounds.width() << " × " << r.bounds.height() << " " << r.unit;
	} else {
		dim << "desconocido";
	}
	gtk_label_set_text(GTK_LABEL(estado->etqTamano), dim.str().c_str());

	int segs = 0, arcos = 0, flashes = 0, regiones = 0;
	for (const auto &p : r.primitives) {
		switch (p.type) {
		case gerber::PrimType::Segment:
			segs++;
			break;
		case gerber::PrimType::Arc:
			arcos++;
			break;
		case gerber::PrimType::Flash:
			flashes++;
			break;
		case gerber::PrimType::Region:
			regiones++;
			break;
		}
	}
	std::ostringstream cnt;
	cnt << (int) r.primitives.size() << " S" << segs << " A" << arcos << " F" << flashes << " R" << regiones;
	gtk_label_set_text(GTK_LABEL(estado->etqPrimitivas), cnt.str().c_str());
}

void SeleccionTrayectoria() {
	if (!estado->tieneArchivo) {
		return;
	}
	gerber::GCodeConfig cfg = leerConfigGCode();
	bool aislamiento = gtk_check_button_get_active(estado->chkAislamiento);
	double huelgo = gtk_spin_button_get_value(estado->spinHuelgo);
	int maxPasadas = (int) gtk_spin_button_get_value(estado->spinPasadas);
	establecerEstado("Calculando trayectoria...");
	if (aislamiento) {
		estado->trayectoria = construirTrayectoriaAislamiento(estado->resultado, cfg, huelgo, maxPasadas);
	} else {
		estado->trayectoria = construirTrayectoria(estado->resultado, cfg, false, huelgo);
	}
	estado->trayectoriaSucia = false;
	establecerEstado("Trayectoria lista (" + std::to_string(estado->trayectoria.size()) + " movimientos)");

	size_t rapidos = 0, cortes = 0;
	for (auto &m : estado->trayectoria) {
		if (m.tipo == TipoMovimiento::Viaje) {
			rapidos++;
		} else if (m.tipo == TipoMovimiento::Corte) {
			cortes++;
		}
	}
	std::ostringstream st;
	st << cortes << " cortes\n" << rapidos << " desplaz.";
	if (estado->etqMovimientos) {
		gtk_label_set_text(GTK_LABEL(estado->etqMovimientos), st.str().c_str());
	}
}

static void dibujarCapaGerber(cairo_t *cr, int /*w*/, int /*h*/, double pixelMundo) {
	if (!estado->mostrarGerber || !estado->tieneArchivo || estado->resultado.primitives.empty()) {
		return;
	}
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	for (const auto &prim : estado->resultado.primitives) {
		bool dark = (prim.polaridad == gerber::Polarity::Dark);
		dark ? cairo_set_source_rgb(cr, 0, 1, 0) : cairo_set_source_rgb(cr, 1, 0, 0);
		switch (prim.type) {
		case gerber::PrimType::Segment: {
			const auto &s = prim.asSegment();
			cairo_set_line_width(cr, std::max(s.width, 2.0 * pixelMundo));
			cairo_move_to(cr, s.start.x, s.start.y);
			cairo_line_to(cr, s.end.x, s.end.y);
			cairo_stroke(cr);
			break;
		}
		case gerber::PrimType::Arc: {
			const auto &a = prim.asArc();
			cairo_set_line_width(cr, std::max(a.width, 2.0 * pixelMundo));
			double tol = std::max(0.001, a.width * 0.01);
			auto pts = gerber::geom::arcToPolyline(a.center, a.radius, a.startAngle, a.endAngle, a.ccw, tol);
			if (!pts.empty()) {
				cairo_move_to(cr, pts[0].x, pts[0].y);
				for (size_t i = 1; i < pts.size(); ++i) {
					cairo_line_to(cr, pts[i].x, pts[i].y);
				}
				cairo_stroke(cr);
			}
			break;
		}
		case gerber::PrimType::Flash: {
			auto poli = gerber::geom::flashToPolygon(prim.asFlash(), 45);
			if (!poli.empty()) {
				cairo_new_path(cr);
				cairo_move_to(cr, poli[0].x, poli[0].y);
				for (size_t i = 1; i < poli.size(); ++i) {
					cairo_line_to(cr, poli[i].x, poli[i].y);
				}
				cairo_close_path(cr);
				cairo_fill(cr);
			}
			break;
		}
		case gerber::PrimType::Region: {
			const auto &reg = prim.asRegion();
			if (!reg.outline.empty()) {
				cairo_new_path(cr);
				cairo_move_to(cr, reg.outline[0].x, reg.outline[0].y);
				for (size_t i = 1; i < reg.outline.size(); ++i) {
					cairo_line_to(cr, reg.outline[i].x, reg.outline[i].y);
				}
				cairo_close_path(cr);
				cairo_fill(cr);
			}
			break;
		}
		}
	}
}

static void dibujarCapaTaladro(cairo_t *cr, int /*w*/, int /*h*/, double pixelMundo) {
	if (!estado->mostrarTaladro || !estado->tieneTaladro || estado->resultadoTaladro.operations.empty()) {
		return;
	}
	cairo_set_source_rgb(cr, 1, 1, 0);
	cairo_set_line_width(cr, 2.5 * pixelMundo);
	for (const auto &op : estado->resultadoTaladro.operations) {
		double radioVis = std::max(op.diameter / 2.0, 1.5 * pixelMundo);
		cairo_arc(cr, op.x, op.y, radioVis, 0, 2 * M_PI);
		cairo_stroke(cr);
		double cruz = radioVis * 0.5;
		cairo_move_to(cr, op.x - cruz, op.y);
		cairo_line_to(cr, op.x + cruz, op.y);
		cairo_stroke(cr);
		cairo_move_to(cr, op.x, op.y - cruz);
		cairo_line_to(cr, op.x, op.y + cruz);
		cairo_stroke(cr);
	}
}

static void dibujarTrayectoriaGerber(cairo_t *cr, int /*w*/, int /*h*/, double pixelMundo) {
	if (!estado->mostrarTrayectoriaGerber || estado->trayectoria.empty()) {
		return;
	}
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	double ancho = 1.5 * pixelMundo;
	cairo_set_line_width(cr, ancho);
	for (const auto &mov : estado->trayectoria) {
		if (mov.tipo == TipoMovimiento::Viaje) {
			cairo_set_source_rgb(cr, 0.2, 1.0, 0.2);
			double guiones[] = { 3.0 * pixelMundo, 3.0 * pixelMundo };
			cairo_set_dash(cr, guiones, 2, 0);
		} else if (mov.tipo == TipoMovimiento::Corte) {
			cairo_set_source_rgb(cr, 1.0, 0.3, 0.3);
			cairo_set_dash(cr, nullptr, 0, 0);
		} else if (mov.tipo == TipoMovimiento::Subir) {
			cairo_set_source_rgb(cr, 0.0, 0.6, 1.0);
			cairo_set_dash(cr, nullptr, 0, 0);
			cairo_arc(cr, mov.desde.x, mov.desde.y, ancho * 2.5, 0, 2 * M_PI);
			cairo_stroke(cr);
			continue;
		} else if (mov.tipo == TipoMovimiento::Bajar) {
			cairo_set_source_rgb(cr, 1.0, 0.6, 0.0);
			cairo_set_dash(cr, nullptr, 0, 0);
			cairo_arc(cr, mov.desde.x, mov.desde.y, ancho * 2.5, 0, 2 * M_PI);
			cairo_stroke(cr);
			continue;
		}
		cairo_move_to(cr, mov.desde.x, mov.desde.y);
		cairo_line_to(cr, mov.hasta.x, mov.hasta.y);
		cairo_stroke(cr);
	}
	cairo_set_dash(cr, nullptr, 0, 0);
}

static void dibujarTrayectoriaTaladro(cairo_t *cr, int /*w*/, int /*h*/, double pixelMundo) {
	if (!estado->mostrarTrayectoriaTaladro || !estado->tieneTaladro || estado->resultadoTaladro.operations.empty()) {
		return;
	}
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_source_rgb(cr, 0.8, 0.6, 0.2);
	cairo_set_line_width(cr, 1.0 * pixelMundo);
	cairo_set_dash(cr, nullptr, 0, 0);
	cairo_move_to(cr, 0, 0);
	for (const auto &op : estado->resultadoTaladro.operations) {
		cairo_line_to(cr, op.x, op.y);
	}
	cairo_line_to(cr, 0, 0);
	cairo_stroke(cr);
	cairo_set_source_rgb(cr, 1.0, 0.8, 0.0);
	for (const auto &op : estado->resultadoTaladro.operations) {
		cairo_arc(cr, op.x, op.y, std::max(op.diameter / 2.0, 2.0 * pixelMundo), 0, 2 * M_PI);
		cairo_stroke(cr);
	}
}

static void alDibujar(GtkDrawingArea* /*area*/, cairo_t *cr, int w, int h, gpointer) {
	cairo_set_source_rgb(cr, 0.1, 0.1, 0.15);
	cairo_paint(cr);

	if ((!estado->tieneArchivo || estado->resultado.primitives.empty()) && (!estado->tieneTaladro || estado->resultadoTaladro.operations.empty())) {
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
		cairo_set_font_size(cr, 14);
		cairo_move_to(cr, 20, h / 2);
		cairo_show_text(cr, "No hay archivos cargados. Abra un Gerber y/o un Taladro.");
		return;
	}

	double minX, maxX, minY, maxY;
	bool limitesValidos = false;
	if (estado->tieneArchivo && estado->resultado.bounds.valid) {
		minX = estado->resultado.bounds.minX;
		maxX = estado->resultado.bounds.maxX;
		minY = estado->resultado.bounds.minY;
		maxY = estado->resultado.bounds.maxY;
		limitesValidos = true;
	} else if (estado->tieneTaladro && !estado->resultadoTaladro.operations.empty()) {
		minX = maxX = estado->resultadoTaladro.operations[0].x;
		minY = maxY = estado->resultadoTaladro.operations[0].y;
		for (const auto &op : estado->resultadoTaladro.operations) {
			minX = std::min(minX, op.x);
			maxX = std::max(maxX, op.x);
			minY = std::min(minY, op.y);
			maxY = std::max(maxY, op.y);
		}
		limitesValidos = true;
	}
	if (!limitesValidos) {
		cairo_set_source_rgb(cr, 1, 0.5, 0);
		cairo_move_to(cr, 20, 80);
		cairo_show_text(cr, "Datos invalidos");
		return;
	}

	double bw = std::max(maxX - minX, 1e-9);
	double bh = std::max(maxY - minY, 1e-9);
	double escBase = std::min(w * 0.8 / bw, h * 0.8 / bh);
	double cx = (minX + maxX) * 0.5, cy = (minY + maxY) * 0.5;

	cairo_save(cr);
	cairo_translate(cr, w * 0.5 + estado->desplX, h * 0.5 + estado->desplY);
	cairo_scale(cr, estado->zoom * escBase, -(estado->zoom * escBase));
	cairo_translate(cr, -cx, -cy);
	double pixelMundo = 1.0 / (estado->zoom * escBase);

	if (estado->mostrarLimites) {
		cairo_set_source_rgb(cr, 1, 0, 1);
		cairo_set_line_width(cr, 5.0 * pixelMundo);
		cairo_rectangle(cr, minX, minY, bw, bh);
		cairo_stroke(cr);
	}
	dibujarCapaGerber(cr, w, h, pixelMundo);
	dibujarCapaTaladro(cr, w, h, pixelMundo);
	dibujarTrayectoriaGerber(cr, w, h, pixelMundo);
	dibujarTrayectoriaTaladro(cr, w, h, pixelMundo);
	cairo_restore(cr);

	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 12);
	char buf[256];
	snprintf(buf, sizeof(buf), "Zoom: %.2f  Pan: (%.1f,%.1f)", estado->zoom, estado->desplX, estado->desplY);
	cairo_move_to(cr, 10, h - 30);
	cairo_show_text(cr, buf);
	snprintf(buf, sizeof(buf), "Limites: (%.2f,%.2f)-(%.2f,%.2f)", minX, minY, maxX, maxY);
	cairo_move_to(cr, 10, h - 15);
	cairo_show_text(cr, buf);
}

static gboolean alScroll(GtkEventControllerScroll*, double, double dy, gpointer) {
	double f = (dy < 0) ? 1.18 : (1.0 / 1.18);
	estado->zoom = std::clamp(estado->zoom * f, 0.01, 3000.0);
	gtk_widget_queue_draw(estado->areaDibujo);
	return TRUE;
}
static void alInicioArrastre(GtkGestureDrag*, double, double, gpointer) {
	estado->origenArrastreX = estado->desplX;
	estado->origenArrastreY = estado->desplY;
}
static void alActualizarArrastre(GtkGestureDrag*, double ox, double oy, gpointer) {
	estado->desplX = estado->origenArrastreX + ox;
	estado->desplY = estado->origenArrastreY + oy;
	gtk_widget_queue_draw(estado->areaDibujo);
}
static void alAjustarZoom(GtkButton*, gpointer) {
	estado->zoom = 1.0;
	estado->desplX = estado->desplY = 0.0;
	gtk_widget_queue_draw(estado->areaDibujo);
}

static gboolean alTecla(GtkEventControllerKey*, guint key, guint, GdkModifierType, gpointer) {
	switch (key) {
	case GDK_KEY_plus: {
	}
	case GDK_KEY_equal: {
		estado->zoom = std::min(estado->zoom * 1.25, 3000.0);
		gtk_widget_queue_draw(estado->areaDibujo);
		return TRUE;
	}
	case GDK_KEY_minus: {
		estado->zoom = std::max(estado->zoom / 1.25, 0.01);
		gtk_widget_queue_draw(estado->areaDibujo);
		return TRUE;
	}
	case GDK_KEY_0: {
	}
	case GDK_KEY_Home: {
		estado->zoom = 1.0;
		estado->desplX = estado->desplY = 0.0;
		gtk_widget_queue_draw(estado->areaDibujo);
		return TRUE;
	}
	default: {
		break;
	}
	}
	return FALSE;
}

static void alArchivoAbierto(GObject *src, GAsyncResult *res, gpointer) {
	GError *err = nullptr;
	GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, &err);
	if (!file) {
		if (err && err->code != GTK_DIALOG_ERROR_DISMISSED) {
			establecerEstado("Error: " + std::string(err->message));
		}
		if (err) {
			g_error_free(err);
		}
		return;
	}
	char *path = g_file_get_path(file);
	g_object_unref(file);
	if (!path) {
		return;
	}
	estado->rutaArchivo = path;
	g_free(path);
	establecerEstado("Cargando " + estado->rutaArchivo + " …");
	gerber::GerberParser parser;
	estado->resultado = parser.parseFile(estado->rutaArchivo);
	estado->tieneArchivo = estado->resultado.ok;
	if (estado->resultado.ok) {
		estado->zoom = 1.0;
		estado->desplX = estado->desplY = 0.0;
		estado->trayectoriaSucia = true;
		if (estado->mostrarTrayectoria) {
			SeleccionTrayectoria();
		}
		std::ostringstream st;
		st << "✓ " << estado->rutaArchivo << "  " << estado->resultado.primitives.size() << " prims  " << estado->resultado.unit;
		if (estado->resultado.bounds.valid) {
			st << std::fixed << std::setprecision(3) << "  " << estado->resultado.bounds.width() << " × " << estado->resultado.bounds.height() << " " << estado->resultado.unit;
		}
		establecerEstado(st.str());
		if (estado->cuaderno) {
			gtk_notebook_set_current_page(GTK_NOTEBOOK(estado->cuaderno), 0);
		}
	} else {
		establecerEstado("✗ " + estado->resultado.error);
	}
	actualizarPanelInfo();
	gtk_widget_queue_draw(estado->areaDibujo);
}

static void alAbrirArchivo(GtkButton*, gpointer) {
	GtkFileDialog *dlg = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dlg, "Abrir archivo Gerber");
	gtk_file_dialog_set_modal(dlg, TRUE);
	GListStore *store = g_list_store_new(GTK_TYPE_FILE_FILTER);
	GtkFileFilter *fGbr = gtk_file_filter_new();
	gtk_file_filter_set_name(fGbr, "Gerber (*.gbr *.gtl *.gbl *.ger …)");
	for (const char *p : { "*.gbr", "*.gtl", "*.gbl", "*.gtc", "*.gbc", "*.gts", "*.gbs", "*.ger", "*.gbp", "*.art" }) {
		gtk_file_filter_add_pattern(fGbr, p);
	}
	g_list_store_append(store, fGbr);
	g_object_unref(fGbr);
	GtkFileFilter *fAll = gtk_file_filter_new();
	gtk_file_filter_set_name(fAll, "Todos los archivos (*)");
	gtk_file_filter_add_pattern(fAll, "*");
	g_list_store_append(store, fAll);
	g_object_unref(fAll);
	gtk_file_dialog_set_filters(dlg, G_LIST_MODEL(store));
	g_object_unref(store);
	gtk_file_dialog_open(dlg, GTK_WINDOW(estado->ventanaPrincipal), nullptr, alArchivoAbierto, nullptr);
	g_object_unref(dlg);
}

static void alGcodeGuardado(GObject *src, GAsyncResult *res, gpointer) {
	GError *err = nullptr;
	GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &err);
	if (!file) {
		if (err && err->code != GTK_DIALOG_ERROR_DISMISSED) {
			establecerEstado("Error: " + std::string(err->message));
		}
		if (err) {
			g_error_free(err);
		}
		return;
	}
	char *path = g_file_get_path(file);
	g_object_unref(file);
	if (!path) {
		return;
	}
	gerber::GCodeConfig cfg = leerConfigGCode();
	gerber::GCodeGenerator gen(cfg);
	bool aislamiento = gtk_check_button_get_active(estado->chkAislamiento);
	double huelgo = gtk_spin_button_get_value(estado->spinHuelgo);
	std::string gcode = aislamiento ? gen.generateIsolation(estado->resultado, huelgo) : gen.generate(estado->resultado);
	std::ofstream out(path);
	if (out.is_open()) {
		out << gcode;
		out.close();
		size_t lines = std::count(gcode.begin(), gcode.end(), '\n');
		establecerEstado("✓ Guardado: " + std::string(path) + " (" + std::to_string(lines) + " lineas)");
	} else {
		establecerEstado("✗ No se pudo escribir: " + std::string(path));
	}
	g_free(path);
}

static void alGenerarGCode(GtkButton*, gpointer) {
	if (!estado->tieneArchivo) {
		establecerEstado("Abra un Gerber primero.");
		return;
	}
	GtkFileDialog *dlg = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dlg, "Guardar GCode");
	gtk_file_dialog_set_modal(dlg, TRUE);
	std::string bn = estado->rutaArchivo;
	size_t dot = bn.rfind('.');
	if (dot != std::string::npos) {
		bn = bn.substr(0, dot);
	}
	size_t sl = bn.rfind('/');
	if (sl != std::string::npos) {
		bn = bn.substr(sl + 1);
	}
	gtk_file_dialog_set_initial_name(dlg, (bn + ".gcode").c_str());
	GListStore *store = g_list_store_new(GTK_TYPE_FILE_FILTER);
	GtkFileFilter *fg = gtk_file_filter_new();
	gtk_file_filter_set_name(fg, "GCode (*.gcode *.nc *.tap)");
	gtk_file_filter_add_pattern(fg, "*.gcode");
	gtk_file_filter_add_pattern(fg, "*.nc");
	gtk_file_filter_add_pattern(fg, "*.tap");
	g_list_store_append(store, fg);
	g_object_unref(fg);
	gtk_file_dialog_set_filters(dlg, G_LIST_MODEL(store));
	g_object_unref(store);
	gtk_file_dialog_save(dlg, GTK_WINDOW(estado->ventanaPrincipal), nullptr, alGcodeGuardado, nullptr);
	g_object_unref(dlg);
}

//static void alPrevisualizarGCode(GtkButton*, gpointer) {
//	if (!estado->tieneArchivo) {
//		establecerEstado("Sin Gerber cargado.");
//		return;
//	}
//	gerber::GCodeConfig cfg = leerConfigGCode();
//	gerber::GCodeGenerator gen(cfg);
//	bool aislamiento = gtk_check_button_get_active(estado->chkAislamiento);
//	double huelgo = gtk_spin_button_get_value(estado->spinHuelgo);
//	std::string gcode = aislamiento ? gen.generateIsolation(estado->resultado, huelgo) : gen.generate(estado->resultado);
//	std::istringstream iss(gcode);
//	std::string line;
//	std::ostringstream preview;
//	int cnt = 0;
//	while (std::getline(iss, line) && cnt < 60) {
//		preview << line << "\n";
//		cnt++;
//	}
//	size_t total = std::count(gcode.begin(), gcode.end(), '\n');
//	if (total > 60) {
//		preview << "\n... (" << (total - 60) << " lineas mas)\n";
//	}
//
//	GtkWidget *dlg = gtk_window_new();
//	gtk_window_set_title(GTK_WINDOW(dlg), "Vista previa GCode");
//	gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(estado->ventanaPrincipal));
//	gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
//	gtk_window_set_default_size(GTK_WINDOW(dlg), 640, 500);
//	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
//	gtk_widget_set_margin_start(vbox, 12);
//	gtk_widget_set_margin_end(vbox, 12);
//	gtk_widget_set_margin_top(vbox, 12);
//	gtk_widget_set_margin_bottom(vbox, 12);
//	gtk_window_set_child(GTK_WINDOW(dlg), vbox);
//	GtkWidget *scr = gtk_scrolled_window_new();
//	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scr), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
//	gtk_widget_set_vexpand(scr, TRUE);
//	GtkWidget *tv = gtk_text_view_new();
//	gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
//	gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
//	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)), preview.str().c_str(), -1);
//	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scr), tv);
//	gtk_box_append(GTK_BOX(vbox), scr);
//	GtkWidget *btn = gtk_button_new_with_label("Cerrar");
//	gtk_widget_set_halign(btn, GTK_ALIGN_END);
//	g_signal_connect_swapped(btn, "clicked", G_CALLBACK(gtk_window_destroy), dlg);
//	gtk_box_append(GTK_BOX(vbox), btn);
//	gtk_window_present(GTK_WINDOW(dlg));
//}

// ─────────────────────────────────────────────────────────────
// Callbacks de toggles de capas
// ─────────────────────────────────────────────────────────────
static void alToggleGerber(GtkCheckButton *cb, gpointer) {
	estado->mostrarGerber = gtk_check_button_get_active(cb);
	gtk_widget_queue_draw(estado->areaDibujo);
}

static void alToggleTrayectoria(GtkCheckButton *cb, gpointer) {
	estado->mostrarTrayectoria = gtk_check_button_get_active(cb);
	if (estado->mostrarTrayectoria && estado->trayectoriaSucia && estado->tieneArchivo) {
		establecerEstado("Calculando recorrido CNC …");
		SeleccionTrayectoria();
		establecerEstado("Recorrido listo.");
	}
	gtk_widget_queue_draw(estado->areaDibujo);
}

static void alToggleTrayectoriaGerber(GtkCheckButton *cb, gpointer) {
	estado->mostrarTrayectoriaGerber = gtk_check_button_get_active(cb);
	if (estado->mostrarTrayectoriaGerber && estado->trayectoriaSucia && estado->tieneArchivo) {
		establecerEstado("Calculando recorrido CNC …");
		SeleccionTrayectoria();
		establecerEstado("Recorrido listo.");
	}
	gtk_widget_queue_draw(estado->areaDibujo);
}

static void alToggleLimites(GtkCheckButton *cb, gpointer) {
	estado->mostrarLimites = gtk_check_button_get_active(cb);
	gtk_widget_queue_draw(estado->areaDibujo);
}

static void alToggleCuadricula(GtkCheckButton *cb, gpointer) {
	estado->mostrarCuadricula = gtk_check_button_get_active(cb);
	gtk_widget_queue_draw(estado->areaDibujo);
}

static void alToggleTaladro(GtkCheckButton *cb, gpointer) {
	estado->mostrarTaladro = gtk_check_button_get_active(cb);
	gtk_widget_queue_draw(estado->areaDibujo);
}

static void alToggleTrayectoriaTaladro(GtkCheckButton *cb, gpointer) {
	estado->mostrarTrayectoriaTaladro = gtk_check_button_get_active(cb);
	gtk_widget_queue_draw(estado->areaDibujo);
}

//static void alCfgChanged(GtkSpinButton*, gpointer) {
//	estado->trayectoriaSucia = true;
//	if (estado->mostrarTrayectoria && estado->tieneArchivo) {
//		SeleccionTrayectoria();
//		gtk_widget_queue_draw(estado->areaDibujo);
//	}
//}
static void alCfgChanged(GtkSpinButton*, gpointer) {
	estado->trayectoriaSucia = true;
	solicitarRecalculo();
}

//static void alChkCfgChanged(GtkCheckButton*, gpointer) {
//	estado->trayectoriaSucia = true;
//	if (estado->mostrarTrayectoria && estado->tieneArchivo) {
//		SeleccionTrayectoria();
//		gtk_widget_queue_draw(estado->areaDibujo);
//	}
//}
static void alChkCfgChanged(GtkCheckButton*, gpointer) {
	estado->trayectoriaSucia = true;
	solicitarRecalculo();
}

//static void alReconstruirTrayectoria(GtkButton*, gpointer) {
//	if (!estado->tieneArchivo) {
//		establecerEstado("No hay archivo Gerber cargado.");
//		return;
//	}
//	establecerEstado("Recalculando recorrido CNC...");
//	SeleccionTrayectoria();
//	establecerEstado("Recorrido CNC recalculado.");
//	gtk_widget_queue_draw(estado->areaDibujo);
//}

static void alReconstruirTrayectoria(GtkButton*, gpointer) {
	if (!estado->tieneArchivo) {
		establecerEstado("No hay archivo Gerber cargado.");
		return;
	}
	establecerEstado("Recalculando recorrido CNC...");
	SeleccionTrayectoria();
	// Activar la visibilidad de ambas capas de recorrido sin tocar las casillas a mano
	gtk_check_button_set_active(estado->chkMostrarTrayectoriaGerber, TRUE);
	gtk_check_button_set_active(estado->chkMostrarTrayectoria, TRUE);
	establecerEstado("Recorrido CNC recalculado.");
	gtk_widget_queue_draw(estado->areaDibujo);
}

static void alTogglePerfiladoCobre(GtkCheckButton *cb, gpointer) {
	if (!gtk_check_button_get_active(cb)) {
		return;
	}
	if (!estado->tieneArchivo) {
		establecerEstado("No hay archivo Gerber cargado.");
		gtk_check_button_set_active(cb, FALSE);
		return;
	}
	estado->mostrarPerfiladoCobre = true;
	estado->mostrarAislamientoCorregido = false;
	ToolpathConfig cfg;
	cfg.toolDiameter = gtk_spin_button_get_value(estado->spinDiamHerramienta);
	cfg.clearance = gtk_spin_button_get_value(estado->spinHuelgo);
	cfg.stepover = cfg.toolDiameter * 0.35;
	cfg.optimizeOrder = true;
	cfg.useVerification = true;
	ToolpathGenerator generator(cfg);
	auto moves = generator.generate(estado->resultado, ToolpathMode::ProfileCopper);
	estado->trayectoria.clear();
	for (const auto &m : moves) {
		estado->trayectoria.push_back( { m.rapid ? TipoMovimiento::Viaje : TipoMovimiento::Corte, m.from, m.to });
	}
	establecerEstado("Perfilado completado (" + std::to_string(estado->trayectoria.size()) + " movimientos)");
	gtk_widget_queue_draw(estado->areaDibujo);
}

static void alTogglePerfiladoIslas(GtkCheckButton *cb, gpointer) {
	if (!gtk_check_button_get_active(cb)) {
		return;
	}
	if (!estado->tieneArchivo) {
		establecerEstado("No hay archivo Gerber cargado.");
		gtk_check_button_set_active(cb, FALSE);
		return;
	}
	estado->mostrarPerfiladoIslas = true;
	estado->mostrarPerfiladoCobre = false;
	estado->mostrarAislamientoCorregido = false;

	islas::ConfigIslas cfg;
	cfg.toolDiameter = gtk_spin_button_get_value(estado->spinDiamHerramienta);
	cfg.clearance = gtk_spin_button_get_value(estado->spinHuelgo);
	cfg.stepoverFactor = 0.35;
	cfg.maxPasadas = (int) gtk_spin_button_get_value(estado->spinPasadas);

	islas::DetectorIslas detector;
	auto movs = detector.generarTrayectoria(estado->resultado, cfg);

	estado->trayectoria.clear();
	for (const auto &m : movs) {
		estado->trayectoria.push_back( { m.viaje ? TipoMovimiento::Viaje : TipoMovimiento::Corte, m.desde, m.hasta });
	}
	estado->trayectoriaSucia = false;

	establecerEstado("Perfilado de islas completado (" + std::to_string(estado->trayectoria.size()) + " movimientos)");
	gtk_widget_queue_draw(estado->areaDibujo);
}

static void alToggleAislamientoCorregido(GtkCheckButton *cb, gpointer) {
	estado->mostrarAislamientoCorregido = gtk_check_button_get_active(cb);
	if (estado->mostrarAislamientoCorregido && estado->tieneArchivo) {
		establecerEstado("Calculando aislamiento corregido...");
		gerber::GCodeConfig cfg = leerConfigGCode();
		double huelgo = gtk_spin_button_get_value(estado->spinHuelgo);
		int maxPasadas = (int) gtk_spin_button_get_value(estado->spinPasadas);
		estado->trayectoria = construirTrayectoriaAislamiento(estado->resultado, cfg, huelgo, maxPasadas);
		establecerEstado("Aislamiento completado (" + std::to_string(estado->trayectoria.size()) + " movimientos)");
		estado->mostrarTrayectoria = false;
		estado->mostrarPerfiladoCobre = false;
	}
	gtk_widget_queue_draw(estado->areaDibujo);
}

// ─────────────────────────────────────────────────────────────
// Taladrado
// ─────────────────────────────────────────────────────────────
static void alArchivoTaladroAbierto(GObject *src, GAsyncResult *res, gpointer) {
	GError *err = nullptr;
	GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, &err);
	if (!file) {
		if (err && err->code != GTK_DIALOG_ERROR_DISMISSED) {
			establecerEstado("Error: " + std::string(err->message));
		}
		if (err) {
			g_error_free(err);
		}
		return;
	}
	char *path = g_file_get_path(file);
	g_object_unref(file);
	if (!path) {
		return;
	}
	estado->rutaTaladro = path;
	g_free(path);
	establecerEstado("Cargando taladros: " + estado->rutaTaladro + " …");
	drill::DrillParser parser;
	estado->resultadoTaladro = parser.parseFile(estado->rutaTaladro);
	estado->tieneTaladro = estado->resultadoTaladro.ok;
	if (estado->resultadoTaladro.ok) {
		std::ostringstream st;
		st << "✓ Taladros: " << estado->resultadoTaladro.operations.size() << " ops en " << estado->resultadoTaladro.unit;
		establecerEstado(st.str());
		gtk_check_button_set_active(estado->chkMostrarTaladro, TRUE);
		gtk_check_button_set_active(estado->chkMostrarTaladro, TRUE);
		gtk_check_button_set_active(estado->chkMostrarTrayectoriaTaladro, TRUE);
		gtk_widget_queue_draw(estado->areaDibujo);
	} else {
		establecerEstado("✗ Error: " + estado->resultadoTaladro.error);
	}
}

static void alAbrirArchivoTaladro(GtkButton*, gpointer) {
	GtkFileDialog *dlg = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dlg, "Abrir archivo de taladrado (.drl)");
	gtk_file_dialog_set_modal(dlg, TRUE);
	GListStore *store = g_list_store_new(GTK_TYPE_FILE_FILTER);
	GtkFileFilter *fDrl = gtk_file_filter_new();
	gtk_file_filter_set_name(fDrl, "Excellon (*.drl *.xln)");
	gtk_file_filter_add_pattern(fDrl, "*.drl");
	gtk_file_filter_add_pattern(fDrl, "*.xln");
	g_list_store_append(store, fDrl);
	g_object_unref(fDrl);
	GtkFileFilter *fAll = gtk_file_filter_new();
	gtk_file_filter_set_name(fAll, "Todos (*)");
	gtk_file_filter_add_pattern(fAll, "*");
	g_list_store_append(store, fAll);
	g_object_unref(fAll);
	gtk_file_dialog_set_filters(dlg, G_LIST_MODEL(store));
	g_object_unref(store);
	gtk_file_dialog_open(dlg, GTK_WINDOW(estado->ventanaPrincipal), nullptr, alArchivoTaladroAbierto, nullptr);
	g_object_unref(dlg);
}

//static void alGenerarGCodeTaladro(GtkButton*, gpointer) {
//	if (!estado->tieneTaladro) {
//		establecerEstado("Cargue un archivo de taladrado (.drl) primero.");
//		return;
//	}
//	gtk_check_button_set_active(estado->chkMostrarTaladro, TRUE);
//	gtk_check_button_set_active(estado->chkMostrarTrayectoriaTaladro, TRUE);
//	GtkFileDialog *dlg = gtk_file_dialog_new();
//	gtk_file_dialog_set_title(dlg, "Guardar GCode de Taladrado");
//	gtk_file_dialog_set_modal(dlg, TRUE);
//	std::string bn = estado->rutaTaladro;
//	size_t dot = bn.rfind('.');
//	if (dot != std::string::npos) {
//		bn = bn.substr(0, dot);
//	}
//	size_t sl = bn.rfind('/');
//	if (sl != std::string::npos) {
//		bn = bn.substr(sl + 1);
//	}
//	gtk_file_dialog_set_initial_name(dlg, (bn + "_taladro.gcode").c_str());
//	GListStore *store = g_list_store_new(GTK_TYPE_FILE_FILTER);
//	GtkFileFilter *fg = gtk_file_filter_new();
//	gtk_file_filter_set_name(fg, "GCode (*.gcode *.nc *.tap)");
//	gtk_file_filter_add_pattern(fg, "*.gcode");
//	gtk_file_filter_add_pattern(fg, "*.nc");
//	gtk_file_filter_add_pattern(fg, "*.tap");
//	g_list_store_append(store, fg);
//	g_object_unref(fg);
//	GtkFileFilter *fAll = gtk_file_filter_new();
//	gtk_file_filter_set_name(fAll, "Todos (*)");
//	gtk_file_filter_add_pattern(fAll, "*");
//	g_list_store_append(store, fAll);
//	g_object_unref(fAll);
//	gtk_file_dialog_set_filters(dlg, G_LIST_MODEL(store));
//	g_object_unref(store);
//	gtk_file_dialog_save(dlg, GTK_WINDOW(estado->ventanaPrincipal), nullptr, [](GObject *src, GAsyncResult *res, gpointer) {
//		GError *err = nullptr;
//		GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &err);
//		if (!file) {
//			if (err && err->code != GTK_DIALOG_ERROR_DISMISSED) {
//				establecerEstado("Error: " + std::string(err->message));
//			}
//			if (err) {
//				g_error_free(err);
//			}
//			return;
//		}
//		char *path = g_file_get_path(file);
//		g_object_unref(file);
//		if (!path)
//			return;
//		gerber::GCodeConfig cfg = leerConfigGCode();
//		std::string gcode = generarGCodeTaladro(estado->resultadoTaladro, cfg);
//		std::ofstream out(path);
//		if (out.is_open()) {
//			out << gcode;
//			out.close();
//			size_t lines = std::count(gcode.begin(), gcode.end(), '\n');
//			establecerEstado("✅ Taladrado guardado: " + std::string(path) + " (" + std::to_string(lines) + " lineas)");
//		} else {
//			establecerEstado("❌ Error al escribir: " + std::string(path));
//		}
//		g_free(path);
//	},
//	nullptr);
//	g_object_unref(dlg);
//}

static void alAbrirVentanaHerramientas(GtkButton*, gpointer) {
	auto *win = new ToolWindow();
	win->show();
}
static void alAbrirVentanaMateriales(GtkButton*, gpointer) {
	auto *win = new MaterialWindow();
	win->show();
}

//static GtkWidget* construirPanelLateral() {
//	GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
//	gtk_widget_set_margin_start(sidebar, 10);
//	gtk_widget_set_margin_end(sidebar, 8);
//	gtk_widget_set_margin_top(sidebar, 10);
//	gtk_widget_set_margin_bottom(sidebar, 8);
//
//	// — Marco Info ——————————————————————————————————————
//	{
//		GtkWidget *frame = gtk_frame_new("Archivo");
//		gtk_box_append(GTK_BOX(sidebar), frame);
//		GtkWidget *grid = gtk_grid_new();
//		gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
//		gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
//		gtk_widget_set_margin_start(grid, 8);
//		gtk_widget_set_margin_end(grid, 8);
//		gtk_widget_set_margin_top(grid, 6);
//		gtk_widget_set_margin_bottom(grid, 6);
//		gtk_frame_set_child(GTK_FRAME(frame), grid);
//		auto makeInfo = [&](int row, const char *label, GtkWidget **out) {
//			GtkWidget *l = gtk_label_new(label);
//			gtk_label_set_xalign(GTK_LABEL(l), 1.0);
//			gtk_widget_add_css_class(l, "dim-label");
//			gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
//			GtkWidget *v = gtk_label_new("—");
//			gtk_label_set_xalign(GTK_LABEL(v), 0.0);
//			gtk_label_set_ellipsize(GTK_LABEL(v), PANGO_ELLIPSIZE_END);
//			gtk_label_set_max_width_chars(GTK_LABEL(v), 22);
//			gtk_grid_attach(GTK_GRID(grid), v, 1, row, 1, 1);
//			if (out)
//				*out = v;
//		};
//		makeInfo(0, "Archivo:", &estado->etqArchivo);
//		makeInfo(1, "Unidad:", &estado->etqUnidad);
//		makeInfo(2, "Tamaño:", &estado->etqTamano);
//		makeInfo(3, "Primitivas:", &estado->etqPrimitivas);
//		GtkWidget *lblMov = gtk_label_new("Recorrido:");
//		gtk_label_set_xalign(GTK_LABEL(lblMov), 1.0);
//		gtk_widget_add_css_class(lblMov, "dim-label");
//		gtk_grid_attach(GTK_GRID(grid), lblMov, 0, 4, 1, 1);
//		estado->etqMovimientos = gtk_label_new("—");
//		gtk_label_set_xalign(GTK_LABEL(estado->etqMovimientos), 0.0);
//		gtk_widget_add_css_class(estado->etqMovimientos, "mono");
//		gtk_grid_attach(GTK_GRID(grid), estado->etqMovimientos, 1, 4, 1, 1);
//	}
//
//	// — Marco Capas ——————————————————————————————————————
//	{
//		GtkWidget *frame = gtk_frame_new("Capas visibles");
//		gtk_box_append(GTK_BOX(sidebar), frame);
//		GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
//		gtk_widget_set_margin_start(vb, 8);
//		gtk_widget_set_margin_end(vb, 8);
//		gtk_widget_set_margin_top(vb, 6);
//		gtk_widget_set_margin_bottom(vb, 6);
//		gtk_frame_set_child(GTK_FRAME(frame), vb);
//		auto makeChk = [&](const char *label, bool active, auto cb) -> GtkCheckButton* {
//			auto *c = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(label));
//			gtk_check_button_set_active(c, active);
//			g_signal_connect(c, "toggled", G_CALLBACK(cb), nullptr);
//			gtk_box_append(GTK_BOX(vb), GTK_WIDGET(c));
//			return c;
//		};
//		estado->chkMostrarGerber = makeChk("🟩 Capa Gerber (cobre / pads)", true, alToggleGerber);
//		estado->chkMostrarTrayectoria = makeChk("🟧 Recorrido CNC (trayectoria)", true, alToggleTrayectoria);
//		estado->chkMostrarLimites = makeChk("🟨 Bounding box", true, alToggleLimites);
//		estado->chkMostrarCuadricula = makeChk("⬜ Grilla", false, alToggleCuadricula);
//		estado->chkMostrarTaladro = makeChk("🔴 Taladros", true, alToggleTaladro);
//		estado->chkMostrarTrayectoriaGerber = makeChk("🟥 Trayectoria CNC (Pistas)", true, alToggleTrayectoriaGerber);
//		estado->chkMostrarTrayectoriaTaladro = makeChk("🟨 Trayectoria CNC (Taladros)", true, alToggleTrayectoriaTaladro);
//		GtkWidget *hint = gtk_label_new("Scroll: zoom │ Arrastrar: pan\n  + / − / 0 : zoom manual");
//		gtk_label_set_xalign(GTK_LABEL(hint), 0.0);
//		gtk_widget_add_css_class(hint, "dim-label");
//		gtk_box_append(GTK_BOX(vb), hint);
//	}
//
//	// — Marco Config GCode ——————————————————————————————
//	{
//		GtkWidget *frame = gtk_frame_new("Configuracion GCode");
//		gtk_box_append(GTK_BOX(sidebar), frame);
//		GtkWidget *grid = gtk_grid_new();
//		gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
//		gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
//		gtk_widget_set_margin_start(grid, 8);
//		gtk_widget_set_margin_end(grid, 8);
//		gtk_widget_set_margin_top(grid, 6);
//		gtk_widget_set_margin_bottom(grid, 6);
//		gtk_frame_set_child(GTK_FRAME(frame), grid);
//
//		auto makeSpin = [&](int row, const char *label, double mn, double mx, double step, double val, int dec) -> GtkSpinButton* {
//			GtkWidget *l = gtk_label_new(label);
//			gtk_label_set_xalign(GTK_LABEL(l), 1.0);
//			gtk_widget_add_css_class(l, "dim-label");
//			gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
//			GtkWidget *spin = gtk_spin_button_new(gtk_adjustment_new(val, mn, mx, step, step * 10, 0), step, dec);
//			gtk_widget_set_hexpand(spin, TRUE);
//			gtk_grid_attach(GTK_GRID(grid), spin, 1, row, 1, 1);
//			return GTK_SPIN_BUTTON(spin);
//		};
//		auto sep = [&](int &row) {
//			GtkWidget *s = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
//			gtk_widget_set_margin_top(s, 3);
//			gtk_widget_set_margin_bottom(s, 3);
//			gtk_grid_attach(GTK_GRID(grid), s, 0, row++, 2, 1);
//		};
//
//		int row = 0;
//		estado->spinAvance = makeSpin(row++, "Avance (u/min):", 1, 10000, 1, 10, 0);
//		estado->spinPenetracion = makeSpin(row++, "Penetracion (u/min):", 1, 10000, 1, 20, 0);
//		estado->spinZSeguro = makeSpin(row++, "Z Seguro (mm):", 0, 250, 0.1, 3.0, 2);
//		estado->spinZCorte = makeSpin(row++, "Z Corte (mm):", -10, 0, 0.01, -0.1, 3);
//		estado->spinDiamHerramienta = makeSpin(row++, "Ø herramienta (mm):", 0.01, 10, 0.01, 0.1, 3);
//		sep(row);
//
//		estado->chkAislamiento = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Enrutamiento de aislamiento"));
//		gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(estado->chkAislamiento), 0, row++, 2, 1);
//		g_signal_connect(estado->chkAislamiento, "toggled", G_CALLBACK(alChkCfgChanged), nullptr);
//		gtk_check_button_set_active(estado->chkAislamiento, TRUE);
//		estado->trayectoriaSucia = true;
//
//		estado->spinHuelgo = makeSpin(row++, "Huelgo (mm):", 0, 5, 0.01, 0.0, 3);
//		estado->spinPasadas = makeSpin(row++, "Pasadas aislamiento:", 1, 20, 1, 3, 0);
//		sep(row);
//
//		estado->chkOptimizar = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Optimizar trayectorias"));
//		gtk_check_button_set_active(estado->chkOptimizar, TRUE);
//		gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(estado->chkOptimizar), 0, row++, 2, 1);
//		g_signal_connect(estado->chkOptimizar, "toggled", G_CALLBACK(alChkCfgChanged), nullptr);
//
//		estado->chkComandosArco = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("G02/G03 nativos"));
//		gtk_check_button_set_active(estado->chkComandosArco, TRUE);
//		gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(estado->chkComandosArco), 0, row++, 2, 1);
//		g_signal_connect(estado->chkComandosArco, "toggled", G_CALLBACK(alChkCfgChanged), nullptr);
//
//		estado->chkMetrico = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Modo metrico (G21)"));
//		gtk_check_button_set_active(estado->chkMetrico, TRUE);
//		gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(estado->chkMetrico), 0, row++, 2, 1);
//
//		for (GtkSpinButton **sp : { &estado->spinAvance, &estado->spinPenetracion, &estado->spinZSeguro, &estado->spinZCorte, &estado->spinDiamHerramienta, &estado->spinHuelgo, &estado->spinPasadas }) {
//			g_signal_connect(*sp, "value-changed", G_CALLBACK(alCfgChanged), nullptr);
//		}
//	}
//
//	gtk_box_append(GTK_BOX(sidebar), gtk_label_new(""));
//	return sidebar;
//}

// ═══════════════════════════════════════════════════════════
// Helpers de construccion del panel lateral
// // Funciones estaticas en lugar de lambdas (copiables desde el chat)
// ═══════════════════════════════════════════════════════════

/**
 * @brief  Crea una fila "Etiqueta: Valor" en una rejilla.
 * @param  grid   Rejilla de la marco "Archivo".
 * @param  fila   Fila de la rejilla donde insertar.
 * @param  nombre Texto de la etiqueta (columna 0, alineada a la derecha).
 * @return El widget de valor (columna 1), para guardarlo en EstadoApp.
 */
static GtkWidget* hacerInfo(GtkWidget *grid, int fila, const char *nombre) {
	GtkWidget *l = gtk_label_new(nombre);
	gtk_label_set_xalign(GTK_LABEL(l), 1.0);
	gtk_widget_add_css_class(l, "dim-label");
	gtk_grid_attach(GTK_GRID(grid), l, 0, fila, 1, 1);
	GtkWidget *v = gtk_label_new("—");
	gtk_label_set_xalign(GTK_LABEL(v), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(v), PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars(GTK_LABEL(v), 22);
	gtk_grid_attach(GTK_GRID(grid), v, 1, fila, 1, 1);
	return v;
}

/**
 * @brief  Crea una casilla de verificación sincronizada con EstadoApp.
 * @details El estado inicial se toma del flag de EstadoApp, no de un literal,
 *          así la vista y el modelo coinciden desde el arranque. Se asigna
 *          "active" ANTES de conectar "toggled", para que la construcción
 *          no dispare callbacks de recálculo.
 * @param  contenedor Caja vertical donde añadir la casilla.
 * @param  texto      Etiqueta de la casilla.
 * @param  activa     Estado inicial (el flag correspondiente de EstadoApp).
 * @param  cb         Callback para "toggled" (puede ser nullptr).
 * @return La casilla creada, para guardarla en EstadoApp.
 */
static GtkCheckButton* hacerCasilla(GtkWidget *contenedor, const char *texto, bool activa, GCallback cb) {
	auto *c = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(texto));
	gtk_box_append(GTK_BOX(contenedor), GTK_WIDGET(c));
	gtk_check_button_set_active(c, activa ? TRUE : FALSE);
	if (cb) {
		g_signal_connect(c, "toggled", cb, nullptr);
	}
	return c;
}

/**
 * @brief  Crea una fila "Etiqueta + SpinButton" en una rejilla.
 * @return El GtkSpinButton creado y expandido en horizontal.
 */
static GtkSpinButton* hacerSpin(GtkWidget *grid, int fila, const char *nombre, double mn, double mx, double paso, double valor, int dec) {
	GtkWidget *l = gtk_label_new(nombre);
	gtk_label_set_xalign(GTK_LABEL(l), 1.0);
	gtk_widget_add_css_class(l, "dim-label");
	gtk_grid_attach(GTK_GRID(grid), l, 0, fila, 1, 1);
	GtkWidget *spin = gtk_spin_button_new(gtk_adjustment_new(valor, mn, mx, paso, paso * 10, 0), paso, dec);
	gtk_widget_set_hexpand(spin, TRUE);
	gtk_grid_attach(GTK_GRID(grid), spin, 1, fila, 1, 1);
	return GTK_SPIN_BUTTON(spin);
}

/** @brief Separador horizontal que ocupa las dos columnas de la rejilla. */
static void hacerSeparador(GtkWidget *grid, int &fila) {
	GtkWidget *s = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_margin_top(s, 3);
	gtk_widget_set_margin_bottom(s, 3);
	gtk_grid_attach(GTK_GRID(grid), s, 0, fila++, 2, 1);
}

// ═══════════════════════════════════════════════════════════
// Construccion del panel lateral
// ═══════════════════════════════════════════════════════════

/**
 * @brief  Construye la pestaña "Panel" con tres marcos: Archivo, Capas visibles
 *         y Configuracion GCode.
 * @details Las casillas se crean con el valor actual de sus flags de EstadoApp,
 *          de modo que no hace falta activarlas dos veces ni sincronizar a mano.
 *          El botón "Generar Recorrido" recalcula la trayectoria y activa la
 *          capa "Trayectoria CNC (Pistas)" desde el callback de la cabecera.
 * @return El GtkWidget raíz del panel (GtkBox vertical), listo para el ScrolledWindow.
 */
static GtkWidget* construirPanelLateral() {
	GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_start(sidebar, 10);
	gtk_widget_set_margin_end(sidebar, 8);
	gtk_widget_set_margin_top(sidebar, 10);
	gtk_widget_set_margin_bottom(sidebar, 8);

	// Marco Info
	{
		GtkWidget *frame = gtk_frame_new("Archivo");
		gtk_box_append(GTK_BOX(sidebar), frame);
		GtkWidget *grid = gtk_grid_new();
		gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
		gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
		gtk_widget_set_margin_start(grid, 8);
		gtk_widget_set_margin_end(grid, 8);
		gtk_widget_set_margin_top(grid, 6);
		gtk_widget_set_margin_bottom(grid, 6);
		gtk_frame_set_child(GTK_FRAME(frame), grid);
		estado->etqArchivo = hacerInfo(grid, 0, "Archivo:");
		estado->etqUnidad = hacerInfo(grid, 1, "Unidad:");
		estado->etqTamano = hacerInfo(grid, 2, "Tamaño:");
		estado->etqPrimitivas = hacerInfo(grid, 3, "Primitivas:");
		estado->etqMovimientos = hacerInfo(grid, 4, "Recorrido:");
		gtk_widget_add_css_class(estado->etqMovimientos, "mono");
	}

	// Marco Capas
	{
		GtkWidget *frame = gtk_frame_new("Capas visibles");
		gtk_box_append(GTK_BOX(sidebar), frame);
		GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
		gtk_widget_set_margin_start(vb, 8);
		gtk_widget_set_margin_end(vb, 8);
		gtk_widget_set_margin_top(vb, 6);
		gtk_widget_set_margin_bottom(vb, 6);
		gtk_frame_set_child(GTK_FRAME(frame), vb);

		// Estado inicial desde EstadoApp: sin desajustes al arranque
		estado->chkMostrarGerber = hacerCasilla(vb, "🟩 Capa Gerber (cobre / pads)", estado->mostrarGerber, G_CALLBACK(alToggleGerber));
		estado->chkMostrarTrayectoria = hacerCasilla(vb, "🟧 Recorrido CNC (trayectoria)", estado->mostrarTrayectoria, G_CALLBACK(alToggleTrayectoria));
		estado->chkMostrarLimites = hacerCasilla(vb, "🟨 Bounding box", estado->mostrarLimites, G_CALLBACK(alToggleLimites));
		estado->chkMostrarCuadricula = hacerCasilla(vb, "⬜ Grilla", estado->mostrarCuadricula, G_CALLBACK(alToggleCuadricula));
		estado->chkMostrarTaladro = hacerCasilla(vb, "🔴 Taladros", estado->mostrarTaladro, G_CALLBACK(alToggleTaladro));
		estado->chkMostrarTrayectoriaGerber = hacerCasilla(vb, "🟥 Trayectoria CNC (Pistas)", estado->mostrarTrayectoriaGerber, G_CALLBACK(alToggleTrayectoriaGerber));
		estado->chkMostrarTrayectoriaTaladro = hacerCasilla(vb, "🟨 Trayectoria CNC (Taladros)", estado->mostrarTrayectoriaTaladro, G_CALLBACK(alToggleTrayectoriaTaladro));

		// Generar sin depender de activar/desactivar casillas
		GtkWidget *btnGenRec = gtk_button_new_with_label("↺ Generar Recorrido");
		gtk_widget_set_tooltip_text(btnGenRec, "Recalcula la trayectoria CNC y activa su capa");
		g_signal_connect(btnGenRec, "clicked", G_CALLBACK(alReconstruirTrayectoria), nullptr);
		gtk_box_append(GTK_BOX(vb), btnGenRec);

		GtkWidget *hint = gtk_label_new("Scroll: zoom │ Arrastrar: pan\n  + / − / 0 : zoom manual");
		gtk_label_set_xalign(GTK_LABEL(hint), 0.0);
		gtk_widget_add_css_class(hint, "dim-label");
		gtk_box_append(GTK_BOX(vb), hint);
	}

	// Marco Config GCode
	{
		GtkWidget *frame = gtk_frame_new("Configuracion GCode");
		gtk_box_append(GTK_BOX(sidebar), frame);
		GtkWidget *grid = gtk_grid_new();
		gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
		gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
		gtk_widget_set_margin_start(grid, 8);
		gtk_widget_set_margin_end(grid, 8);
		gtk_widget_set_margin_top(grid, 6);
		gtk_widget_set_margin_bottom(grid, 6);
		gtk_frame_set_child(GTK_FRAME(frame), grid);

		int row = 0;
		estado->spinAvance = hacerSpin(grid, row++, "Avance (u/min):", 1, 10000, 1, 10, 0);
		estado->spinPenetracion = hacerSpin(grid, row++, "Penetracion (u/min):", 1, 10000, 1, 20, 0);
		estado->spinZSeguro = hacerSpin(grid, row++, "Z Seguro (mm):", 0, 250, 0.1, 3.0, 2);
		estado->spinZCorte = hacerSpin(grid, row++, "Z Corte (mm):", -10, 0, 0.01, -0.1, 3);
		estado->spinDiamHerramienta = hacerSpin(grid, row++, "Ø herramienta (mm):", 0.01, 10, 0.01, 0.1, 3);
		hacerSeparador(grid, row);

		estado->chkAislamiento = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Enrutamiento de aislamiento"));
		gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(estado->chkAislamiento), 0, row++, 2, 1);
		gtk_check_button_set_active(estado->chkAislamiento, TRUE);
		estado->trayectoriaSucia = true;
		g_signal_connect(estado->chkAislamiento, "toggled", G_CALLBACK(alChkCfgChanged), nullptr);

		estado->spinHuelgo = hacerSpin(grid, row++, "Huelgo (mm):", 0, 5, 0.01, 0.0, 3);
		hacerSeparador(grid, row);

		estado->chkOptimizar = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Optimizar trayectorias"));
		gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(estado->chkOptimizar), 0, row++, 2, 1);
		gtk_check_button_set_active(estado->chkOptimizar, TRUE);
		g_signal_connect(estado->chkOptimizar, "toggled", G_CALLBACK(alChkCfgChanged), nullptr);

		estado->chkComandosArco = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("G02/G03 nativos"));
		gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(estado->chkComandosArco), 0, row++, 2, 1);
		gtk_check_button_set_active(estado->chkComandosArco, TRUE);
		g_signal_connect(estado->chkComandosArco, "toggled", G_CALLBACK(alChkCfgChanged), nullptr);

		estado->chkMetrico = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Modo metrico (G21)"));
		gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(estado->chkMetrico), 0, row++, 2, 1);
		gtk_check_button_set_active(estado->chkMetrico, TRUE);
		g_signal_connect(estado->chkMetrico, "toggled", G_CALLBACK(alChkCfgChanged), nullptr);

		// Después de conectar: cualquier cambio repinta al instante
		for (GtkSpinButton **sp : { &estado->spinAvance, &estado->spinPenetracion, &estado->spinZSeguro, &estado->spinZCorte, &estado->spinDiamHerramienta, &estado->spinHuelgo }) {
			g_signal_connect(*sp, "value-changed", G_CALLBACK(alCfgChanged), nullptr);
		}
	} // ← aquí acaba el bloque "Marco Config GCode"

	gtk_box_append(GTK_BOX(sidebar), construirPanelExportacion());


	gtk_box_append(GTK_BOX(sidebar), gtk_label_new(""));
	return sidebar;
}

void construirUI(GtkApplication *app) {
	GtkCssProvider *css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(css, ".dim-label { color:alpha(currentColor,0.55); font-size:0.84em; }"
			".mono { font-family:monospace; font-size:0.84em; }"
			"textview text { background:#101418; color:#a8ffc0; }"
			"notebook tab:checked { background: alpha(@accent_bg_color,0.15); }");
	gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(css);

	GtkWidget *win = gtk_application_window_new(app);
	estado->ventanaPrincipal = GTK_APPLICATION_WINDOW(win);
	gtk_window_set_title(GTK_WINDOW(win), "Visor Gerber + CNC Toolpath (Isolation Routing Corregido)");
	gtk_window_set_default_size(GTK_WINDOW(win), 1280, 800);

	GtkWidget *header = gtk_header_bar_new();
	gtk_window_set_titlebar(GTK_WINDOW(win), header);

	auto packStart = [&](const char *label, const char *tip, auto cb) {
		GtkWidget *b = gtk_button_new_with_label(label);
		if (tip) {
			gtk_widget_set_tooltip_text(b, tip);
		}
		g_signal_connect(b, "clicked", G_CALLBACK(cb), nullptr);
		gtk_header_bar_pack_start(GTK_HEADER_BAR(header), b);
		return b;
	};
	auto packEnd = [&](const char *label, const char *tip, auto cb) {
		GtkWidget *b = gtk_button_new_with_label(label);
		if (tip)
			gtk_widget_set_tooltip_text(b, tip);
		g_signal_connect(b, "clicked", G_CALLBACK(cb), nullptr);
		gtk_header_bar_pack_end(GTK_HEADER_BAR(header), b);
		return b;
	};

	packStart("⊞ Abrir Gerber", nullptr, alAbrirArchivo);
	packStart("⊡ Ajustar", nullptr, alAjustarZoom);
	packStart("⚙ Taladros", nullptr, alAbrirArchivoTaladro);
	packStart("↺ Recorrido", "Recalcular recorrido CNC", alReconstruirTrayectoria);
	//	packStart("🔲 Perfilar Cobre", "Perfilar pads, vias y pistas", alTogglePerfiladoCobre);
	//	packStart("🏝 Perfilar Islas", "Perfilar cobre que no es pista: masas, pads y agujeros", alTogglePerfiladoIslas);
	//	packStart("⛔ Aislamiento Correcto", "Aislar solo zonas Clear", alToggleAislamientoCorregido);
	packStart("🔲 Perfilar Cobre", "Perfilar pads, vias y pistas", alPerfilarCobre);
	packStart("🏝 Perfilar Islas", "...", alPerfilarIslas);
	packStart("⛔ Aislamiento Correcto", "...", alAislamientoCorregido);
	packEnd("▶ Guardar GCode", nullptr, alGuardarGCode);   // alPrevisualizarGCode y alGenerarGCodeTaladro conservan el nombre
	packStart("🔧 Herramientas", nullptr, alAbrirVentanaHerramientas);

	auto *btnDrillGCode = packEnd("🔨 GCode Taladro", nullptr, alGenerarGCodeTaladro);
	gtk_widget_add_css_class(btnDrillGCode, "suggested-action");
	packEnd("◈ Ver GCode", nullptr, alPrevisualizarGCode);
	auto *btnGen = packEnd("▶ Guardar GCode", nullptr, alGenerarGCode);
	gtk_widget_add_css_class(btnGen, "suggested-action");
	packEnd("🧪 Materiales", nullptr, alAbrirVentanaMateriales);

	GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_window_set_child(GTK_WINDOW(win), root);
	estado->cuaderno = gtk_notebook_new();
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(estado->cuaderno), GTK_POS_TOP);
	gtk_widget_set_vexpand(estado->cuaderno, TRUE);
	gtk_box_append(GTK_BOX(root), estado->cuaderno);

	// Pestaña Vista
	estado->areaDibujo = gtk_drawing_area_new();
	gtk_widget_set_hexpand(estado->areaDibujo, TRUE);
	gtk_widget_set_vexpand(estado->areaDibujo, TRUE);
	gtk_widget_set_focusable(estado->areaDibujo, TRUE);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(estado->areaDibujo), alDibujar, nullptr, nullptr);
	auto *sc_ctrl = GTK_EVENT_CONTROLLER_SCROLL(gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL));
	g_signal_connect(sc_ctrl, "scroll", G_CALLBACK(alScroll), nullptr);
	gtk_widget_add_controller(estado->areaDibujo, GTK_EVENT_CONTROLLER(sc_ctrl));
	auto *drag = GTK_GESTURE_DRAG(gtk_gesture_drag_new());
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), 1);
	g_signal_connect(drag, "drag-begin", G_CALLBACK(alInicioArrastre), nullptr);
	g_signal_connect(drag, "drag-update", G_CALLBACK(alActualizarArrastre), nullptr);
	gtk_widget_add_controller(estado->areaDibujo, GTK_EVENT_CONTROLLER(drag));
	gtk_notebook_append_page(GTK_NOTEBOOK(estado->cuaderno), estado->areaDibujo, gtk_label_new("🖥 Vista"));

	// Pestaña Panel
	GtkWidget *scrollSide = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollSide), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand(scrollSide, TRUE);
	gtk_widget_set_vexpand(scrollSide, TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrollSide), construirPanelLateral());
	gtk_notebook_append_page(GTK_NOTEBOOK(estado->cuaderno), scrollSide, gtk_label_new("⚙ Panel"));

	gtk_notebook_set_current_page(GTK_NOTEBOOK(estado->cuaderno), 0);

	auto *key_ctrl = GTK_EVENT_CONTROLLER_KEY(gtk_event_controller_key_new());
	g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(alTecla), nullptr);
	gtk_widget_add_controller(win, GTK_EVENT_CONTROLLER(key_ctrl));

	gtk_box_append(GTK_BOX(root), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
	GtkWidget *statusBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_append(GTK_BOX(root), statusBox);
	estado->barraEstado = gtk_label_new("Listo. Abra un archivo Gerber para comenzar.");
	gtk_label_set_xalign(GTK_LABEL(estado->barraEstado), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(estado->barraEstado), PANGO_ELLIPSIZE_END);
	gtk_widget_set_margin_start(estado->barraEstado, 10);
	gtk_widget_set_margin_end(estado->barraEstado, 10);
	gtk_widget_set_margin_top(estado->barraEstado, 4);
	gtk_widget_set_margin_bottom(estado->barraEstado, 4);
	gtk_widget_add_css_class(estado->barraEstado, "dim-label");
	gtk_box_append(GTK_BOX(statusBox), estado->barraEstado);

	actualizarPanelInfo();
	gtk_window_present(GTK_WINDOW(win));
}
