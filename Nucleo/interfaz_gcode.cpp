
/*
 * Nucleo/interfaz_gcode.cpp
 *
 * Exportación del recorrido visible y cálculo asíncrono de trayectorias CNC.
 */
#include "interfaz_gcode.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

#include "../Intefaces/interfaz.h"
#include "../Procesos/exportar_gcode.hpp"

namespace {
GtkCheckButton *chkOrigen = nullptr;
GtkCheckButton *chkEspejo = nullptr;
GtkSpinButton *spinTolerancia = nullptr;
GtkSpinButton *spinHusillo = nullptr;
GtkSpinButton *spinPeck = nullptr;

struct Tarea {
	ModoCalculo modo;
	gerber::GerberResult res;
	gerber::GCodeConfig cfg;
	bool aislamiento;
	double huelgo;
	int pasadas;
	unsigned generacion;
	std::vector<MovimientoHerramienta> salida;
};

unsigned generacionActual = 0;
GSource *temporizador = nullptr;
bool calculando = false;
bool hayPendiente = false;
ModoCalculo modoPendiente = ModoCalculo::Seleccion;

// El parser Gerber no conserva nombres semánticos de capas (p. ej. Edge.Cuts).
// En ausencia de un contorno de placa identificable, se mecaniza la envolvente
// del archivo como perímetro rectangular aproximado.
std::vector<MovimientoHerramienta> construirTrayectoriaPCB(const gerber::GerberResult &res,
		const gerber::GCodeConfig &cfg, double huelgo) {
	std::vector<MovimientoHerramienta> movimientos;
	if (!res.ok || !res.bounds.valid || cfg.toolDiameter <= 0.0 || huelgo < 0.0) {
		return movimientos;
	}
	const auto &b = res.bounds;
	if (b.maxX <= b.minX || b.maxY <= b.minY) {
		return movimientos;
	}

	// Trayectoria del centro de la fresa: offset exterior igual al radio
	// más la holgura. Los cuartos de círculo resuelven las esquinas del offset.
	const double r = cfg.toolDiameter * 0.5 + huelgo;
	if (r <= 0.0) {
		return movimientos;
	}
	const double pi = std::acos(-1.0);
	const int puntosArco = std::max(4, Mecanizado::PASOS_TAPA / 2);
	std::vector<gerber::Vec2> camino;
	camino.reserve(static_cast<size_t>(4 * (puntosArco + 2) + 1));
	camino.push_back({ b.minX, b.minY - r });
	camino.push_back({ b.maxX, b.minY - r });
	auto anadirArco = [&](double cx, double cy, double a0) {
		for (int i = 1; i <= puntosArco; ++i) {
			const double a = a0 + (pi * 0.5) * static_cast<double>(i) / puntosArco;
			camino.push_back({ cx + r * std::cos(a), cy + r * std::sin(a) });
		}
	};
	anadirArco(b.maxX, b.minY, -pi * 0.5);
	camino.push_back({ b.maxX + r, b.maxY });
	anadirArco(b.maxX, b.maxY, 0.0);
	camino.push_back({ b.minX, b.maxY + r });
	anadirArco(b.minX, b.maxY, pi * 0.5);
	camino.push_back({ b.minX - r, b.minY });
	anadirArco(b.minX, b.minY, pi);
	camino.push_back(camino.front());

	const gerber::Vec2 origen { 0.0, 0.0 };
	movimientos.push_back({ TipoMovimiento::Subir, origen, origen });
	movimientos.push_back({ TipoMovimiento::Viaje, origen, camino.front() });
	movimientos.push_back({ TipoMovimiento::Bajar, camino.front(), camino.front() });
	for (size_t i = 1; i < camino.size(); ++i) {
		movimientos.push_back({ TipoMovimiento::Corte, camino[i - 1], camino[i] });
	}
	movimientos.push_back({ TipoMovimiento::Subir, camino.back(), camino.back() });
	movimientos.push_back({ TipoMovimiento::Viaje, camino.back(), origen });
	return movimientos;
}

Transformacion transformacionActual() {
	double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
	bool hay = false;
	if (estado->tieneArchivo && estado->resultado.bounds.valid) {
		const auto &b = estado->resultado.bounds;
		minX = b.minX; minY = b.minY; maxX = b.maxX; maxY = b.maxY;
		hay = true;
	} else if (estado->tieneTaladro && !estado->resultadoTaladro.operations.empty()) {
		minX = minY = 1e300; maxX = maxY = -1e300;
		for (const auto &op : estado->resultadoTaladro.operations) {
			minX = std::min({ minX, op.x, op.ranura ? op.x2 : op.x });
			maxX = std::max({ maxX, op.x, op.ranura ? op.x2 : op.x });
			minY = std::min({ minY, op.y, op.ranura ? op.y2 : op.y });
			maxY = std::max({ maxY, op.y, op.ranura ? op.y2 : op.y });
		}
		hay = true;
	}
	Transformacion t;
	t.espejoX = chkEspejo && gtk_check_button_get_active(chkEspejo);
	if (hay) {
		if (!chkOrigen || gtk_check_button_get_active(chkOrigen)) {
			t.ox = minX; t.oy = minY; t.ancho = maxX - minX;
		} else {
			t.ancho = minX + maxX;
		}
	}
	return t;
}

OpcionesGCode opcionesActuales(const std::string &titulo) {
	const gerber::GCodeConfig c = Interfaz::leerConfigGCode();
	OpcionesGCode o;
	o.tr = transformacionActual();
	o.zSeguro = c.safeZ; o.zCorte = c.cutZ; o.avance = c.feedRate; o.penetracion = c.plungeRate;
	o.metrico = c.metricMode; o.usarArcos = c.useArcCommands;
	o.tolerancia = spinTolerancia ? gtk_spin_button_get_value(spinTolerancia) : 0.002;
	o.husillo = spinHusillo ? static_cast<int>(gtk_spin_button_get_value(spinHusillo)) : 10000;
	o.titulo = titulo;
	return o;
}

void hiloCalculo(GTask *task, gpointer, gpointer datos, GCancellable*) {
	auto *t = static_cast<Tarea*>(datos);
	switch (t->modo) {
	case ModoCalculo::PerfilPCB:
		t->salida = construirTrayectoriaPCB(t->res, t->cfg, t->huelgo);
		break;
	case ModoCalculo::Islas:
		// Se conserva exactamente el algoritmo que antes ejecutaba Perfil Cobre.
		t->salida = Mecanizado::construirTrayectoriaPerfilado(t->res, t->cfg, t->huelgo, t->pasadas, false);
		break;
	case ModoCalculo::AislamientoCorregido:
		t->salida = Mecanizado::construirTrayectoriaAislamiento(t->res, t->cfg, t->huelgo, t->pasadas);
		break;
	case ModoCalculo::Seleccion:
		if (t->aislamiento) {
			t->salida = Mecanizado::construirTrayectoriaAislamiento(t->res, t->cfg, t->huelgo, t->pasadas);
		} else {
			t->salida = Mecanizado::construirTrayectoria(t->res, t->cfg, false, t->huelgo);
		}
		break;
	}
	g_task_return_boolean(task, TRUE);
}

void alTerminarCalculo(GObject*, GAsyncResult *res, gpointer) {
	struct AlSalir {
		~AlSalir() {
			calculando = false;
			if (hayPendiente) { hayPendiente = false; lanzarCalculo(modoPendiente); }
		}
	} alSalir;
	auto *t = static_cast<Tarea*>(g_task_get_task_data(G_TASK(res)));
	if (!t || t->generacion != generacionActual) return;
	estado->trayectoria = std::move(t->salida);
	estado->trayectoriaSucia = false;
	estado->mostrarPerfiladoCobre = (t->modo == ModoCalculo::PerfilPCB);
	estado->mostrarPerfiladoIslas = (t->modo == ModoCalculo::Islas);
	estado->mostrarAislamientoCorregido = (t->modo == ModoCalculo::AislamientoCorregido);
	size_t rapidos = 0, cortes = 0;
	for (const auto &m : estado->trayectoria) {
		if (m.tipo == TipoMovimiento::Viaje) ++rapidos;
		else if (m.tipo == TipoMovimiento::Corte) ++cortes;
	}
	if (estado->etqMovimientos) {
		gtk_label_set_text(GTK_LABEL(estado->etqMovimientos),
				(std::to_string(cortes) + " cortes\n" + std::to_string(rapidos) + " desplaz.").c_str());
	}
	Interfaz::establecerEstado("Trayectoria lista (" + std::to_string(estado->trayectoria.size()) + " movimientos)");
	if (estado->chkMostrarTrayectoriaGerber) gtk_check_button_set_active(estado->chkMostrarTrayectoriaGerber, TRUE);
	gtk_widget_queue_draw(estado->areaDibujo);
}

ModoCalculo modoActual() {
	if (estado->mostrarPerfiladoCobre) return ModoCalculo::PerfilPCB;
	if (estado->mostrarPerfiladoIslas) return ModoCalculo::Islas;
	if (estado->mostrarAislamientoCorregido) return ModoCalculo::AislamientoCorregido;
	return ModoCalculo::Seleccion;
}

std::string tituloTrayectoria() {
	if (estado->mostrarPerfiladoCobre) return "Perfilado PCB (perímetro aproximado)";
	if (estado->mostrarPerfiladoIslas) return "Perfilar Islas";
	return "Aislamiento";
}

std::string gcodeVisible(EstadisticasGCode *est) {
	const bool esModoPerfilado = estado->mostrarPerfiladoCobre || estado->mostrarPerfiladoIslas;
	if (estado->trayectoria.empty() || (estado->trayectoriaSucia && !esModoPerfilado)) {
		Interfaz::SeleccionTrayectoria();
	}
	if (estado->trayectoria.empty()) return "";
	return ExportarGcode::generarGCodeTrayectoria(estado->trayectoria, opcionesActuales(tituloTrayectoria()), est);
}

std::string resumen(const EstadisticasGCode &e) {
	std::ostringstream os;
	os << e.lineas << " lineas, " << e.arcos << " arcos, corte " << static_cast<int>(e.largoCorte + 0.5) << " mm";
	return os.str();
}

void alGcodeGuardado(GObject *src, GAsyncResult *res, gpointer) {
	GError *err = nullptr;
	GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &err);
	if (!file) {
		if (err && err->code != GTK_DIALOG_ERROR_DISMISSED) Interfaz::establecerEstado("Error: " + std::string(err->message));
		if (err) g_error_free(err);
		return;
	}
	char *path = g_file_get_path(file); g_object_unref(file);
	if (!path) return;
	EstadisticasGCode est;
	const std::string gcode = gcodeVisible(&est);
	if (gcode.empty()) Interfaz::establecerEstado("No hay trayectoria que guardar.");
	else {
		std::ofstream out(path);
		if (out.is_open()) { out << gcode; Interfaz::establecerEstado("✓ Guardado: " + std::string(path) + " (" + resumen(est) + ")"); }
		else Interfaz::establecerEstado("✗ No se pudo escribir: " + std::string(path));
	}
	g_free(path);
}

void dialogoGuardar(const char *titulo, const std::string &nombre, GAsyncReadyCallback cb) {
	GtkFileDialog *dlg = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dlg, titulo); gtk_file_dialog_set_modal(dlg, TRUE);
	gtk_file_dialog_set_initial_name(dlg, nombre.c_str());
	GListStore *store = g_list_store_new(GTK_TYPE_FILE_FILTER);
	GtkFileFilter *fg = gtk_file_filter_new();
	gtk_file_filter_set_name(fg, "GCode (*.gcode *.nc *.tap)");
	gtk_file_filter_add_pattern(fg, "*.gcode"); gtk_file_filter_add_pattern(fg, "*.nc"); gtk_file_filter_add_pattern(fg, "*.tap");
	g_list_store_append(store, fg); g_object_unref(fg);
	GtkFileFilter *fAll = gtk_file_filter_new();
	gtk_file_filter_set_name(fAll, "Todos (*)"); gtk_file_filter_add_pattern(fAll, "*");
	g_list_store_append(store, fAll); g_object_unref(fAll);
	gtk_file_dialog_set_filters(dlg, G_LIST_MODEL(store)); g_object_unref(store);
	gtk_file_dialog_save(dlg, GTK_WINDOW(estado->ventanaPrincipal), nullptr, cb, nullptr);
	g_object_unref(dlg);
}

std::string nombreBase(const std::string &ruta) {
	std::string bn = ruta;
	const size_t dot = bn.rfind('.'); if (dot != std::string::npos) bn = bn.substr(0, dot);
	const size_t sl = bn.rfind('/'); if (sl != std::string::npos) bn = bn.substr(sl + 1);
	return bn;
}

void alTaladroGuardado(GObject *src, GAsyncResult *res, gpointer) {
	GError *err = nullptr;
	GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &err);
	if (!file) {
		if (err && err->code != GTK_DIALOG_ERROR_DISMISSED) Interfaz::establecerEstado("Error: " + std::string(err->message));
		if (err) g_error_free(err);
		return;
	}
	char *path = g_file_get_path(file); g_object_unref(file);
	if (!path) return;
	const double peck = spinPeck ? gtk_spin_button_get_value(spinPeck) : 0.0;
	const std::string gcode = ExportarGcode::generarGCodeTaladroT(estado->resultadoTaladro, opcionesActuales("Taladrado"), peck);
	std::ofstream out(path);
	if (out.is_open()) { out << gcode; Interfaz::establecerEstado("✅ Taladrado guardado: " + std::string(path)); }
	else Interfaz::establecerEstado("❌ Error al escribir: " + std::string(path));
	g_free(path);
}
} // namespace

GtkWidget* construirPanelExportacion() {
	// Los botones de la cabecera ya existen en este punto. Actualizamos sus
	// nombres y ayudas manteniendo sus conexiones originales.
	if (estado->ventanaPrincipal) {
		GtkWidget *header = gtk_window_get_titlebar(GTK_WINDOW(estado->ventanaPrincipal));
		for (GtkWidget *child = gtk_widget_get_first_child(header); child; child = gtk_widget_get_next_sibling(child)) {
			if (!GTK_IS_BUTTON(child)) continue;
			const char *label = gtk_button_get_label(GTK_BUTTON(child));
			if (!label) continue;
			if (std::string(label).find("Perfilar Cobre") != std::string::npos) {
				gtk_button_set_label(GTK_BUTTON(child), "🪚 Perfilar PCB");
				gtk_widget_set_tooltip_text(child, "Corta el perímetro aproximado definido por los límites Gerber");
			} else if (std::string(label).find("Perfilar Islas") != std::string::npos) {
				gtk_button_set_label(GTK_BUTTON(child), "🏝 Perfilar Islas");
				gtk_widget_set_tooltip_text(child, "Conserva el recorrido anterior de Perfilar Cobre");
			}
		}
	}
	GtkWidget *frame = gtk_frame_new("Exportación GCode");
	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 5); gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
	gtk_widget_set_margin_start(grid, 8); gtk_widget_set_margin_end(grid, 8);
	gtk_widget_set_margin_top(grid, 6); gtk_widget_set_margin_bottom(grid, 6);
	gtk_frame_set_child(GTK_FRAME(frame), grid);
	int row = 0;
	chkOrigen = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Origen en la esquina de la placa"));
	gtk_check_button_set_active(chkOrigen, TRUE);
	gtk_widget_set_tooltip_text(GTK_WIDGET(chkOrigen), "X0 Y0 = esquina inferior izquierda de la placa (también para el taladro)");
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(chkOrigen), 0, row++, 2, 1);
	chkEspejo = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Espejo en X (cara inferior)"));
	gtk_widget_set_tooltip_text(GTK_WIDGET(chkEspejo), "Aplica el mismo espejo al aislamiento y al taladro");
	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(chkEspejo), 0, row++, 2, 1);
	auto spin = [&](const char *label, double min, double max, double step, double value, int decimals) {
		GtkWidget *l = gtk_label_new(label); gtk_label_set_xalign(GTK_LABEL(l), 1.0);
		gtk_widget_add_css_class(l, "dim-label"); gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
		GtkWidget *s = gtk_spin_button_new(gtk_adjustment_new(value, min, max, step, step * 10, 0), step, decimals);
		gtk_widget_set_hexpand(s, TRUE); gtk_grid_attach(GTK_GRID(grid), s, 1, row++, 1, 1);
		return GTK_SPIN_BUTTON(s);
	};
	spinTolerancia = spin("Tolerancia (mm):", 0.0005, 0.05, 0.0005, 0.002, 4);
	gtk_widget_set_tooltip_text(GTK_WIDGET(spinTolerancia), "Desviación máxima al simplificar la trayectoria y ajustar arcos G02/G03");
	spinHusillo = spin("Husillo (S):", 0, 60000, 100, 10000, 0);
	spinPeck = spin("Taladro por pasos (mm):", 0, 5, 0.1, 0, 2);
	gtk_widget_set_tooltip_text(GTK_WIDGET(spinPeck), "Profundidad de cada paso al taladrar; 0 = un solo paso");
	return frame;
}

void lanzarCalculo(ModoCalculo modo) {
	if (!estado->tieneArchivo) { Interfaz::establecerEstado("No hay archivo Gerber cargado."); return; }
	if (calculando) { hayPendiente = true; modoPendiente = modo; return; }
	calculando = true;
	auto *t = new Tarea { modo, estado->resultado, Interfaz::leerConfigGCode(),
		gtk_check_button_get_active(estado->chkAislamiento) != FALSE,
		gtk_spin_button_get_value(estado->spinHuelgo),
		estado->spinPasadas ? static_cast<int>(gtk_spin_button_get_value(estado->spinPasadas)) : 3,
		++generacionActual, {} };
	Interfaz::establecerEstado("Calculando trayectoria...");
	GTask *task = g_task_new(nullptr, nullptr, alTerminarCalculo, nullptr);
	g_task_set_task_data(task, t, [](gpointer p) { delete static_cast<Tarea*>(p); });
	g_task_run_in_thread(task, hiloCalculo); g_object_unref(task);
}

void solicitarRecalculo() {
	if (!estado->tieneArchivo || !(estado->mostrarTrayectoria || estado->mostrarTrayectoriaGerber)) return;
	if (temporizador) g_source_destroy(temporizador);
	temporizador = g_timeout_source_new(300);
	g_source_set_callback(temporizador, [](gpointer) -> gboolean {
		temporizador = nullptr; lanzarCalculo(modoActual()); return G_SOURCE_REMOVE;
	}, nullptr, nullptr);
	g_source_attach(temporizador, nullptr);
}

void alPerfilarCobre(GtkButton*, gpointer) { lanzarCalculo(ModoCalculo::PerfilPCB); }
void alPerfilarIslas(GtkButton*, gpointer) { lanzarCalculo(ModoCalculo::Islas); }
void alAislamientoCorregido(GtkButton*, gpointer) { lanzarCalculo(ModoCalculo::AislamientoCorregido); }

void alGuardarGCode(GtkButton*, gpointer) {
	if (!estado->tieneArchivo) { Interfaz::establecerEstado("Abra un Gerber primero."); return; }
	dialogoGuardar("Guardar GCode", nombreBase(estado->rutaArchivo) + ".gcode", alGcodeGuardado);
}

void alPrevisualizarGCode(GtkButton*, gpointer) {
	if (!estado->tieneArchivo) { Interfaz::establecerEstado("Sin Gerber cargado."); return; }
	EstadisticasGCode est; const std::string gcode = gcodeVisible(&est);
	if (gcode.empty()) { Interfaz::establecerEstado("No hay trayectoria que mostrar."); return; }
	std::istringstream iss(gcode); std::string line; std::ostringstream preview; int count = 0;
	while (count < 80 && std::getline(iss, line)) { preview << line << "\n"; ++count; }
	if (est.lineas > 80) preview << "\n... (" << (est.lineas - 80) << " lineas mas)\n";
	GtkWidget *dlg = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(dlg), ("Vista previa GCode - " + resumen(est)).c_str());
	gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(estado->ventanaPrincipal));
	gtk_window_set_modal(GTK_WINDOW(dlg), TRUE); gtk_window_set_default_size(GTK_WINDOW(dlg), 640, 500);
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start(vbox, 12); gtk_widget_set_margin_end(vbox, 12);
	gtk_widget_set_margin_top(vbox, 12); gtk_widget_set_margin_bottom(vbox, 12);
	gtk_window_set_child(GTK_WINDOW(dlg), vbox);
	GtkWidget *scr = gtk_scrolled_window_new(); gtk_widget_set_vexpand(scr, TRUE);
	GtkWidget *tv = gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)), preview.str().c_str(), -1);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scr), tv); gtk_box_append(GTK_BOX(vbox), scr);
	GtkWidget *btn = gtk_button_new_with_label("Cerrar"); gtk_widget_set_halign(btn, GTK_ALIGN_END);
	g_signal_connect_swapped(btn, "clicked", G_CALLBACK(gtk_window_destroy), dlg);
	gtk_box_append(GTK_BOX(vbox), btn); gtk_window_present(GTK_WINDOW(dlg));
}

void alGenerarGCodeTaladro(GtkButton*, gpointer) {
	if (!estado->tieneTaladro) { Interfaz::establecerEstado("Cargue un archivo de taladrado (.drl) primero."); return; }
	gtk_check_button_set_active(estado->chkMostrarTaladro, TRUE);
	gtk_check_button_set_active(estado->chkMostrarTrayectoriaTaladro, TRUE);
	dialogoGuardar("Guardar GCode de Taladrado", nombreBase(estado->rutaTaladro) + "_taladro.gcode", alTaladroGuardado);
}


/*
 * Nucleo/interfaz_gcode.cpp
 *
 *  Created on: 14 may 2026
 *      Author: DjSteker
 */

//#include "interfaz_gcode.hpp"
//
//#include <algorithm>
//#include <fstream>
//#include <sstream>
//
//#include "../Intefaces/interfaz.h"
//#include "../Procesos/exportar_gcode.hpp"
//
//// ─────────────────────────────────────────────────────────────
//// Controles propios del panel de exportación
//// ─────────────────────────────────────────────────────────────
//static GtkCheckButton *chkOrigen = nullptr;
//static GtkCheckButton *chkEspejo = nullptr;
//static GtkSpinButton *spinTolerancia = nullptr;
//static GtkSpinButton *spinHusillo = nullptr;
//static GtkSpinButton *spinPeck = nullptr;
//
//GtkWidget* construirPanelExportacion() {
//	GtkWidget *frame = gtk_frame_new("Exportación GCode");
//	GtkWidget *grid = gtk_grid_new();
//	gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
//	gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
//	gtk_widget_set_margin_start(grid, 8);
//	gtk_widget_set_margin_end(grid, 8);
//	gtk_widget_set_margin_top(grid, 6);
//	gtk_widget_set_margin_bottom(grid, 6);
//	gtk_frame_set_child(GTK_FRAME(frame), grid);
//
//	int row = 0;
//	chkOrigen = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Origen en la esquina de la placa"));
//	gtk_check_button_set_active(chkOrigen, TRUE);
//	gtk_widget_set_tooltip_text(GTK_WIDGET(chkOrigen), "X0 Y0 = esquina inferior izquierda de la placa (también para el taladro)");
//	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(chkOrigen), 0, row++, 2, 1);
//
//	chkEspejo = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Espejo en X (cara inferior)"));
//	gtk_widget_set_tooltip_text(GTK_WIDGET(chkEspejo), "Aplica el mismo espejo al aislamiento y al taladro");
//	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(chkEspejo), 0, row++, 2, 1);
//
//	auto spin = [&](const char *etiqueta, double mn, double mx, double paso, double val, int dec) {
//		GtkWidget *l = gtk_label_new(etiqueta);
//		gtk_label_set_xalign(GTK_LABEL(l), 1.0);
//		gtk_widget_add_css_class(l, "dim-label");
//		gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
//		GtkWidget *s = gtk_spin_button_new(gtk_adjustment_new(val, mn, mx, paso, paso * 10, 0), paso, dec);
//		gtk_widget_set_hexpand(s, TRUE);
//		gtk_grid_attach(GTK_GRID(grid), s, 1, row++, 1, 1);
//		return GTK_SPIN_BUTTON(s);
//	};
//	spinTolerancia = spin("Tolerancia (mm):", 0.0005, 0.05, 0.0005, 0.002, 4);
//	gtk_widget_set_tooltip_text(GTK_WIDGET(spinTolerancia), "Desviación máxima al simplificar la trayectoria y ajustar arcos G02/G03");
//	spinHusillo = spin("Husillo (S):", 0, 60000, 100, 10000, 0);
//	spinPeck = spin("Taladro por pasos (mm):", 0, 5, 0.1, 0, 2);
//	gtk_widget_set_tooltip_text(GTK_WIDGET(spinPeck), "Profundidad de cada paso al taladrar; 0 = un solo paso");
//	return frame;
//}
//
//// ─────────────────────────────────────────────────────────────
//// Opciones de exportación
//// ─────────────────────────────────────────────────────────────
//
//// Origen y espejo comunes al aislamiento y al taladro
//static Transformacion transformacionActual() {
//	double minX = 0, minY = 0, maxX = 0, maxY = 0;
//	bool hay = false;
//	if (estado->tieneArchivo && estado->resultado.bounds.valid) {
//		const auto &b = estado->resultado.bounds;
//		minX = b.minX;
//		minY = b.minY;
//		maxX = b.maxX;
//		maxY = b.maxY;
//		hay = true;
//	} else if (estado->tieneTaladro && !estado->resultadoTaladro.operations.empty()) {
//		minX = minY = 1e300;
//		maxX = maxY = -1e300;
//		for (const auto &op : estado->resultadoTaladro.operations) {
//			minX = std::min( { minX, op.x, op.ranura ? op.x2 : op.x });
//			maxX = std::max( { maxX, op.x, op.ranura ? op.x2 : op.x });
//			minY = std::min( { minY, op.y, op.ranura ? op.y2 : op.y });
//			maxY = std::max( { maxY, op.y, op.ranura ? op.y2 : op.y });
//		}
//		hay = true;
//	}
//	Transformacion t;
//	t.espejoX = chkEspejo && gtk_check_button_get_active(chkEspejo);
//	if (hay) {
//		if (!chkOrigen || gtk_check_button_get_active(chkOrigen)) {
//			t.ox = minX;
//			t.oy = minY;
//			t.ancho = maxX - minX;
//		} else {
//			t.ancho = minX + maxX;   // espejo respecto al centro de la placa
//		}
//	}
//	return t;
//}
//
//static OpcionesGCode opcionesActuales(const std::string &titulo) {
//	const gerber::GCodeConfig c = Interfaz::leerConfigGCode();
//	OpcionesGCode o;
//	o.tr = transformacionActual();
//	o.zSeguro = c.safeZ;
//	o.zCorte = c.cutZ;
//	o.avance = c.feedRate;
//	o.penetracion = c.plungeRate;
//	o.metrico = c.metricMode;
//	o.usarArcos = c.useArcCommands;
//	o.tolerancia = spinTolerancia ? gtk_spin_button_get_value(spinTolerancia) : 0.002;
//	o.husillo = spinHusillo ? (int) gtk_spin_button_get_value(spinHusillo) : 10000;
//	o.titulo = titulo;
//	return o;
//}
//
//// ─────────────────────────────────────────────────────────────
//// Cálculo de trayectorias en segundo plano
//// ─────────────────────────────────────────────────────────────
//namespace {
//
//struct Tarea {
//	ModoCalculo modo;
//	gerber::GerberResult res;       // copia: el hilo no toca el estado de la interfaz
//	gerber::GCodeConfig cfg;
//	bool aislamiento;
//	double huelgo;
//	int pasadas;
//	unsigned generacion;
//	std::vector<MovimientoHerramienta> salida;
//};
//
//unsigned generacionActual = 0;
//guint temporizador = 0;
//bool calculando = false;
//bool hayPendiente = false;
//ModoCalculo modoPendiente = ModoCalculo::Seleccion;
//
////void hiloCalculo(GTask *task, gpointer, gpointer datos, GCancellable*) {
////	Tarea *t = static_cast<Tarea*>(datos);
////	switch (t->modo) {
////	case ModoCalculo::PerfilCobre:
////		// Limpieza global de las zonas sin cobre.
////		t->salida = construirTrayectoriaPerfilado(t->res, t->cfg, t->huelgo, t->pasadas, false);
////		break;
////	case ModoCalculo::Islas:
////		// El botón renombrado "Limpieza" debe usar la misma unión global de cobre
////		// y las pasadas concéntricas de construirLimpieza; no perfilar cada isla
////		// hacia dentro de forma independiente.
////		t->salida = construirTrayectoriaPerfilado(t->res, t->cfg, t->huelgo, t->pasadas, false);
////		break;
////	case ModoCalculo::AislamientoCorregido:
////		t->salida = construirTrayectoriaAislamiento(t->res, t->cfg, t->huelgo, t->pasadas);
////		break;
////	case ModoCalculo::Seleccion:
////		t->salida = t->aislamiento ? construirTrayectoriaAislamiento(t->res, t->cfg, t->huelgo, t->pasadas) : construirTrayectoria(t->res, t->cfg, false, t->huelgo);
////		break;
////	}
////	g_task_return_boolean(task, TRUE);
////}
//
///**
// * @brief Ejecuta el cálculo de una trayectoria en segundo plano.
// *
// * @param task Tarea de GLib que notificará la finalización.
// * @param datos Puntero a la estructura Tarea con las entradas y la salida.
// *
// * @details
// * Selecciona el generador según el modo solicitado y almacena
// * los movimientos resultantes en Tarea::salida.
// *
// * Los generadores se invocan como métodos estáticos de Mecanizado.
// * Esta función no actualiza los widgets de la interfaz.
// */
//void hiloCalculo(GTask *task, gpointer, gpointer datos, GCancellable*) {
//	Tarea *t = static_cast<Tarea*>(datos);
//
//	switch (t->modo) {
//	case ModoCalculo::PerfilCobre:
//		// Limpieza global de las zonas sin cobre.
//		t->salida = Mecanizado::construirTrayectoriaPerfilado(t->res, t->cfg, t->huelgo, t->pasadas, false);
//		break;
//
//	case ModoCalculo::Islas:
//		// El botón renombrado "Limpieza" utiliza la unión global
//		// del cobre, no el perfilado interior de islas individuales.
//		t->salida = Mecanizado::construirTrayectoriaPerfilado(t->res, t->cfg, t->huelgo, t->pasadas, false);
//		break;
//
//	case ModoCalculo::AislamientoCorregido:
//		t->salida = Mecanizado::construirTrayectoriaAislamiento(t->res, t->cfg, t->huelgo, t->pasadas);
//		break;
//
//	case ModoCalculo::Seleccion:
//		if (t->aislamiento) {
//			t->salida = Mecanizado::construirTrayectoriaAislamiento(t->res, t->cfg, t->huelgo, t->pasadas);
//		} else {
//			t->salida = Mecanizado::construirTrayectoria(t->res, t->cfg, false, t->huelgo);
//		}
//		break;
//	}
//
//	g_task_return_boolean(task, TRUE);
//}
//
//void alTerminarCalculo(GObject*, GAsyncResult *res, gpointer) {
//	struct AlSalir {
//		~AlSalir() {
//			calculando = false;
//			if (hayPendiente) {
//				hayPendiente = false;
//				lanzarCalculo(modoPendiente);
//			}
//		}
//	} alSalir;
//	Tarea *t = static_cast<Tarea*>(g_task_get_task_data(G_TASK(res)));
//	if (!t || t->generacion != generacionActual) {
//		return;   // llegó tarde: ya hay un cálculo más reciente en marcha
//	}
//	estado->trayectoria = std::move(t->salida);
//	estado->trayectoriaSucia = false;
//	estado->mostrarPerfiladoCobre = (t->modo == ModoCalculo::PerfilCobre);
//	estado->mostrarPerfiladoIslas = (t->modo == ModoCalculo::Islas);
//	estado->mostrarAislamientoCorregido = (t->modo == ModoCalculo::AislamientoCorregido);
//
//	size_t rapidos = 0, cortes = 0;
//	for (const auto &m : estado->trayectoria) {
//		if (m.tipo == TipoMovimiento::Viaje) {
//			++rapidos;
//		} else if (m.tipo == TipoMovimiento::Corte) {
//			++cortes;
//		}
//	}
//	if (estado->etqMovimientos) {
//		gtk_label_set_text(GTK_LABEL(estado->etqMovimientos), (std::to_string(cortes) + " cortes\n" + std::to_string(rapidos) + " desplaz.").c_str());
//	}
//	Interfaz::establecerEstado("Trayectoria lista (" + std::to_string(estado->trayectoria.size()) + " movimientos)");
//
//	// Se hace visible sin tocar casillas a mano (la trayectoria ya no está sucia: no se recalcula)
//	if (estado->chkMostrarTrayectoriaGerber) {
//		gtk_check_button_set_active(estado->chkMostrarTrayectoriaGerber, TRUE);
//	}
//	gtk_widget_queue_draw(estado->areaDibujo);
//}
//
//ModoCalculo modoActual() {
//	if (estado->mostrarPerfiladoCobre) {
//		return ModoCalculo::PerfilCobre;
//	}
//	if (estado->mostrarPerfiladoIslas) {
//		return ModoCalculo::Islas;
//	}
//	if (estado->mostrarAislamientoCorregido) {
//		return ModoCalculo::AislamientoCorregido;
//	}
//	return ModoCalculo::Seleccion;
//}
//
//}  // namespace
//
//void lanzarCalculo(ModoCalculo modo) {
//	if (!estado->tieneArchivo) {
//		Interfaz::establecerEstado("No hay archivo Gerber cargado.");
//		return;
//	}
//	if (calculando) {
//		hayPendiente = true;
//		modoPendiente = modo;
//		return;
//	}
//	calculando = true;
//	auto *t = new Tarea { modo, estado->resultado, Interfaz::leerConfigGCode(), gtk_check_button_get_active(estado->chkAislamiento) != FALSE, gtk_spin_button_get_value(estado->spinHuelgo),
//			estado->spinPasadas ? (int) gtk_spin_button_get_value(estado->spinPasadas) : 3, ++generacionActual, { } };
//	Interfaz::establecerEstado("Calculando trayectoria...");
//	GTask *task = g_task_new(nullptr, nullptr, alTerminarCalculo, nullptr);
//	g_task_set_task_data(task, t, [](gpointer p) {
//		delete static_cast<Tarea*>(p);
//	});
//	g_task_run_in_thread(task, hiloCalculo);
//	g_object_unref(task);
//}
//
//void solicitarRecalculo() {
//	if (!estado->tieneArchivo || !(estado->mostrarTrayectoria || estado->mostrarTrayectoriaGerber)) {
//		return;
//	}
//	if (temporizador) {
//		g_source_remove(temporizador);
//	}
//	temporizador = g_timeout_add(300, [](gpointer) -> gboolean {
//		temporizador = 0;
//		lanzarCalculo(modoActual());
//		return G_SOURCE_REMOVE;
//	}, nullptr);
//}
//
//// ─────────────────────────────────────────────────────────────
//// Botones de perfilado (señal "clicked" de un GtkButton)
//// ─────────────────────────────────────────────────────────────
//void alPerfilarCobre(GtkButton*, gpointer) {
//	lanzarCalculo(ModoCalculo::PerfilCobre);
//}
//void alPerfilarIslas(GtkButton*, gpointer) {
//	lanzarCalculo(ModoCalculo::Islas);
//}
//void alAislamientoCorregido(GtkButton*, gpointer) {
//	lanzarCalculo(ModoCalculo::AislamientoCorregido);
//}
//
//// ─────────────────────────────────────────────────────────────
//// GCode de la trayectoria visible
//// ─────────────────────────────────────────────────────────────
//static std::string tituloTrayectoria() {
//	if (estado->mostrarPerfiladoCobre) {
//		return "Perfilado de cobre";
//	}
//	if (estado->mostrarPerfiladoIslas) {
//		return "Limpieza";
//	}
//	return "Aislamiento";
//}
//
//// Texto GCode de lo que se ve en pantalla; vacío si no hay trayectoria
//static std::string gcodeVisible(EstadisticasGCode *est) {
//	const bool esAislamiento = !estado->mostrarPerfiladoCobre && !estado->mostrarPerfiladoIslas;
//	if (estado->trayectoria.empty() || (estado->trayectoriaSucia && esAislamiento)) {
//		Interfaz::SeleccionTrayectoria();   // síncrono: hace falta el resultado ya
//	}
//	if (estado->trayectoria.empty()) {
//		return "";
//	}
//	return ExportarGcode::generarGCodeTrayectoria(estado->trayectoria, opcionesActuales(tituloTrayectoria()), est);
//}
//
//static std::string resumen(const EstadisticasGCode &e) {
//	std::ostringstream os;
//	os << e.lineas << " lineas, " << e.arcos << " arcos, corte " << (int) (e.largoCorte + 0.5) << " mm";
//	return os.str();
//}
//
//static void alGcodeGuardado(GObject *src, GAsyncResult *res, gpointer) {
//	GError *err = nullptr;
//	GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &err);
//	if (!file) {
//		if (err && err->code != GTK_DIALOG_ERROR_DISMISSED) {
//			Interfaz::establecerEstado("Error: " + std::string(err->message));
//		}
//		if (err) {
//			g_error_free(err);
//		}
//		return;
//	}
//	char *path = g_file_get_path(file);
//	g_object_unref(file);
//	if (!path) {
//		return;
//	}
//	EstadisticasGCode est;
//	const std::string gcode = gcodeVisible(&est);
//	if (gcode.empty()) {
//		Interfaz::establecerEstado("No hay trayectoria que guardar.");
//	} else {
//		std::ofstream out(path);
//		if (out.is_open()) {
//			out << gcode;
//			Interfaz::establecerEstado("✓ Guardado: " + std::string(path) + " (" + resumen(est) + ")");
//		} else {
//			Interfaz::establecerEstado("✗ No se pudo escribir: " + std::string(path));
//		}
//	}
//	g_free(path);
//}
//
//static void dialogoGuardar(const char *titulo, const std::string &nombre, GAsyncReadyCallback cb) {
//	GtkFileDialog *dlg = gtk_file_dialog_new();
//	gtk_file_dialog_set_title(dlg, titulo);
//	gtk_file_dialog_set_modal(dlg, TRUE);
//	gtk_file_dialog_set_initial_name(dlg, nombre.c_str());
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
//	gtk_file_dialog_save(dlg, GTK_WINDOW(estado->ventanaPrincipal), nullptr, cb, nullptr);
//	g_object_unref(dlg);
//}
//
//static std::string nombreBase(const std::string &ruta) {
//	std::string bn = ruta;
//	const size_t dot = bn.rfind('.');
//	if (dot != std::string::npos) {
//		bn = bn.substr(0, dot);
//	}
//	const size_t sl = bn.rfind('/');
//	if (sl != std::string::npos) {
//		bn = bn.substr(sl + 1);
//	}
//	return bn;
//}
//
//void alGuardarGCode(GtkButton*, gpointer) {
//	if (!estado->tieneArchivo) {
//		Interfaz::establecerEstado("Abra un Gerber primero.");
//		return;
//	}
//	dialogoGuardar("Guardar GCode", nombreBase(estado->rutaArchivo) + ".gcode", alGcodeGuardado);
//}
//
//void alPrevisualizarGCode(GtkButton*, gpointer) {
//	if (!estado->tieneArchivo) {
//		Interfaz::establecerEstado("Sin Gerber cargado.");
//		return;
//	}
//	EstadisticasGCode est;
//	const std::string gcode = gcodeVisible(&est);
//	if (gcode.empty()) {
//		Interfaz::establecerEstado("No hay trayectoria que mostrar.");
//		return;
//	}
//	std::istringstream iss(gcode);
//	std::string line;
//	std::ostringstream preview;
//	int cnt = 0;
//	while (cnt < 80 && std::getline(iss, line)) {
//		preview << line << "\n";
//		++cnt;
//	}
//	if (est.lineas > 80) {
//		preview << "\n... (" << (est.lineas - 80) << " lineas mas)\n";
//	}
//
//	GtkWidget *dlg = gtk_window_new();
//	gtk_window_set_title(GTK_WINDOW(dlg), ("Vista previa GCode - " + resumen(est)).c_str());
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
//
//// ─────────────────────────────────────────────────────────────
//// GCode de taladrado
//// ─────────────────────────────────────────────────────────────
//static void alTaladroGuardado(GObject *src, GAsyncResult *res, gpointer) {
//	GError *err = nullptr;
//	GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &err);
//	if (!file) {
//		if (err && err->code != GTK_DIALOG_ERROR_DISMISSED) {
//			Interfaz::establecerEstado("Error: " + std::string(err->message));
//		}
//		if (err) {
//			g_error_free(err);
//		}
//		return;
//	}
//	char *path = g_file_get_path(file);
//	g_object_unref(file);
//	if (!path) {
//		return;
//	}
//	const double peck = spinPeck ? gtk_spin_button_get_value(spinPeck) : 0.0;
//	const std::string gcode = ExportarGcode::generarGCodeTaladroT(estado->resultadoTaladro, opcionesActuales("Taladrado"), peck);
//	//const std::string gcode = ExportarGcode::
//	std::ofstream out(path);
//	if (out.is_open()) {
//		out << gcode;
//		const size_t lines = std::count(gcode.begin(), gcode.end(), '\n');
//		Interfaz::establecerEstado("✅ Taladrado guardado: " + std::string(path) + " (" + std::to_string(lines) + " lineas)");
//	} else {
//		Interfaz::establecerEstado("❌ Error al escribir: " + std::string(path));
//	}
//	g_free(path);
//}
//
//void alGenerarGCodeTaladro(GtkButton*, gpointer) {
//	if (!estado->tieneTaladro) {
//		Interfaz::establecerEstado("Cargue un archivo de taladrado (.drl) primero.");
//		return;
//	}
//	gtk_check_button_set_active(estado->chkMostrarTaladro, TRUE);
//	gtk_check_button_set_active(estado->chkMostrarTrayectoriaTaladro, TRUE);
//	dialogoGuardar("Guardar GCode de Taladrado", nombreBase(estado->rutaTaladro) + "_taladro.gcode", alTaladroGuardado);
//}

///*
// * Nucleo/interfaz_gcode.cpp
// *
// *  Created on: 14 may 2026
// *      Author: DjSteker
// */
//
//#include "interfaz_gcode.hpp"
//
//#include <algorithm>
//#include <fstream>
//#include <sstream>
//
//#include "../interfaz.h"
//#include "../Procesos/exportar_gcode.hpp"
//
//// ─────────────────────────────────────────────────────────────
//// Controles propios del panel de exportación
//// ─────────────────────────────────────────────────────────────
//static GtkCheckButton *chkOrigen = nullptr;
//static GtkCheckButton *chkEspejo = nullptr;
//static GtkSpinButton *spinTolerancia = nullptr;
//static GtkSpinButton *spinHusillo = nullptr;
//static GtkSpinButton *spinPeck = nullptr;
//
//GtkWidget* construirPanelExportacion() {
//	GtkWidget *frame = gtk_frame_new("Exportación GCode");
//	GtkWidget *grid = gtk_grid_new();
//	gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
//	gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
//	gtk_widget_set_margin_start(grid, 8);
//	gtk_widget_set_margin_end(grid, 8);
//	gtk_widget_set_margin_top(grid, 6);
//	gtk_widget_set_margin_bottom(grid, 6);
//	gtk_frame_set_child(GTK_FRAME(frame), grid);
//
//	int row = 0;
//	chkOrigen = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Origen en la esquina de la placa"));
//	gtk_check_button_set_active(chkOrigen, TRUE);
//	gtk_widget_set_tooltip_text(GTK_WIDGET(chkOrigen), "X0 Y0 = esquina inferior izquierda de la placa (también para el taladro)");
//	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(chkOrigen), 0, row++, 2, 1);
//
//	chkEspejo = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Espejo en X (cara inferior)"));
//	gtk_widget_set_tooltip_text(GTK_WIDGET(chkEspejo), "Aplica el mismo espejo al aislamiento y al taladro");
//	gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(chkEspejo), 0, row++, 2, 1);
//
//	auto spin = [&](const char *etiqueta, double mn, double mx, double paso, double val, int dec) {
//		GtkWidget *l = gtk_label_new(etiqueta);
//		gtk_label_set_xalign(GTK_LABEL(l), 1.0);
//		gtk_widget_add_css_class(l, "dim-label");
//		gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
//		GtkWidget *s = gtk_spin_button_new(gtk_adjustment_new(val, mn, mx, paso, paso * 10, 0), paso, dec);
//		gtk_widget_set_hexpand(s, TRUE);
//		gtk_grid_attach(GTK_GRID(grid), s, 1, row++, 1, 1);
//		return GTK_SPIN_BUTTON(s);
//	};
//	spinTolerancia = spin("Tolerancia (mm):", 0.0005, 0.05, 0.0005, 0.002, 4);
//	gtk_widget_set_tooltip_text(GTK_WIDGET(spinTolerancia), "Desviación máxima al simplificar la trayectoria y ajustar arcos G02/G03");
//	spinHusillo = spin("Husillo (S):", 0, 60000, 100, 10000, 0);
//	spinPeck = spin("Taladro por pasos (mm):", 0, 5, 0.1, 0, 2);
//	gtk_widget_set_tooltip_text(GTK_WIDGET(spinPeck), "Profundidad de cada paso al taladrar; 0 = un solo paso");
//	return frame;
//}
//
//// ─────────────────────────────────────────────────────────────
//// Opciones de exportación
//// ─────────────────────────────────────────────────────────────
//
//// Origen y espejo comunes al aislamiento y al taladro
//static Transformacion transformacionActual() {
//	double minX = 0, minY = 0, maxX = 0, maxY = 0;
//	bool hay = false;
//	if (estado->tieneArchivo && estado->resultado.bounds.valid) {
//		const auto &b = estado->resultado.bounds;
//		minX = b.minX;
//		minY = b.minY;
//		maxX = b.maxX;
//		maxY = b.maxY;
//		hay = true;
//	} else if (estado->tieneTaladro && !estado->resultadoTaladro.operations.empty()) {
//		minX = minY = 1e300;
//		maxX = maxY = -1e300;
//		for (const auto &op : estado->resultadoTaladro.operations) {
//			minX = std::min( { minX, op.x, op.ranura ? op.x2 : op.x });
//			maxX = std::max( { maxX, op.x, op.ranura ? op.x2 : op.x });
//			minY = std::min( { minY, op.y, op.ranura ? op.y2 : op.y });
//			maxY = std::max( { maxY, op.y, op.ranura ? op.y2 : op.y });
//		}
//		hay = true;
//	}
//	Transformacion t;
//	t.espejoX = chkEspejo && gtk_check_button_get_active(chkEspejo);
//	if (hay) {
//		if (!chkOrigen || gtk_check_button_get_active(chkOrigen)) {
//			t.ox = minX;
//			t.oy = minY;
//			t.ancho = maxX - minX;
//		} else {
//			t.ancho = minX + maxX;   // espejo respecto al centro de la placa
//		}
//	}
//	return t;
//}
//
//static OpcionesGCode opcionesActuales(const std::string &titulo) {
//	const gerber::GCodeConfig c = leerConfigGCode();
//	OpcionesGCode o;
//	o.tr = transformacionActual();
//	o.zSeguro = c.safeZ;
//	o.zCorte = c.cutZ;
//	o.avance = c.feedRate;
//	o.penetracion = c.plungeRate;
//	o.metrico = c.metricMode;
//	o.usarArcos = c.useArcCommands;
//	o.tolerancia = spinTolerancia ? gtk_spin_button_get_value(spinTolerancia) : 0.002;
//	o.husillo = spinHusillo ? (int) gtk_spin_button_get_value(spinHusillo) : 10000;
//	o.titulo = titulo;
//	return o;
//}
//
//// ─────────────────────────────────────────────────────────────
//// Cálculo de trayectorias en segundo plano
//// ─────────────────────────────────────────────────────────────
//namespace {
//
//struct Tarea {
//	ModoCalculo modo;
//	gerber::GerberResult res;       // copia: el hilo no toca el estado de la interfaz
//	gerber::GCodeConfig cfg;
//	bool aislamiento;
//	double huelgo;
//	int pasadas;
//	unsigned generacion;
//	std::vector<MovimientoHerramienta> salida;
//};
//
//unsigned generacionActual = 0;
//guint temporizador = 0;
//
//void hiloCalculo(GTask *task, gpointer, gpointer datos, GCancellable*) {
//	Tarea *t = static_cast<Tarea*>(datos);
//	switch (t->modo) {
//	case ModoCalculo::PerfilCobre:
//		//t->salida = construirTrayectoriaPerfilado(t->res, t->cfg, t->huelgo, t->pasadas, false);   // todo el cobre
//		t->salida = construirTrayectoriaPerfilado(t->res, t->cfg, t->huelgo, t->pasadas, false);   // todo el cobre
//		break;
//	case ModoCalculo::Islas:
//		//t->salida = construirTrayectoriaPerfilado(t->res, t->cfg, t->huelgo, t->pasadas, true);    // solo pads y masas
//		t->salida = construirTrayectoriaPerfilado(t->res, t->cfg, t->huelgo, t->pasadas, true);    // solo pads y masas
//		break;
//	case ModoCalculo::AislamientoCorregido:
//		t->salida = construirTrayectoriaAislamiento(t->res, t->cfg, t->huelgo, t->pasadas);
//		break;
//	case ModoCalculo::Seleccion:
//		t->salida = t->aislamiento ? construirTrayectoriaAislamiento(t->res, t->cfg, t->huelgo, t->pasadas) : construirTrayectoria(t->res, t->cfg, false, t->huelgo);
//		break;
//	}
//	g_task_return_boolean(task, TRUE);
//}
//
//void alTerminarCalculo(GObject*, GAsyncResult *res, gpointer) {
//	Tarea *t = static_cast<Tarea*>(g_task_get_task_data(G_TASK(res)));
//	if (!t || t->generacion != generacionActual) {
//		return;   // llegó tarde: ya hay un cálculo más reciente en marcha
//	}
//	estado->trayectoria = std::move(t->salida);
//	estado->trayectoriaSucia = false;
//	estado->mostrarPerfiladoCobre = (t->modo == ModoCalculo::PerfilCobre);
//	estado->mostrarPerfiladoIslas = (t->modo == ModoCalculo::Islas);
//	estado->mostrarAislamientoCorregido = (t->modo == ModoCalculo::AislamientoCorregido);
//
//	size_t rapidos = 0, cortes = 0;
//	for (const auto &m : estado->trayectoria) {
//		if (m.tipo == TipoMovimiento::Viaje) {
//			++rapidos;
//		} else if (m.tipo == TipoMovimiento::Corte) {
//			++cortes;
//		}
//	}
//	if (estado->etqMovimientos) {
//		gtk_label_set_text(GTK_LABEL(estado->etqMovimientos), (std::to_string(cortes) + " cortes\n" + std::to_string(rapidos) + " desplaz.").c_str());
//	}
//	establecerEstado("Trayectoria lista (" + std::to_string(estado->trayectoria.size()) + " movimientos)");
//
//	// Se hace visible sin tocar casillas a mano (la trayectoria ya no está sucia: no se recalcula)
//	if (estado->chkMostrarTrayectoriaGerber) {
//		gtk_check_button_set_active(estado->chkMostrarTrayectoriaGerber, TRUE);
//	}
//	gtk_widget_queue_draw(estado->areaDibujo);
//}
//
//ModoCalculo modoActual() {
//	if (estado->mostrarPerfiladoCobre) {
//		return ModoCalculo::PerfilCobre;
//	}
//	if (estado->mostrarPerfiladoIslas) {
//		return ModoCalculo::Islas;
//	}
//	if (estado->mostrarAislamientoCorregido) {
//		return ModoCalculo::AislamientoCorregido;
//	}
//	return ModoCalculo::Seleccion;
//}
//
//}  // namespace
//
//void lanzarCalculo(ModoCalculo modo) {
//	if (!estado->tieneArchivo) {
//		establecerEstado("No hay archivo Gerber cargado.");
//		return;
//	}
//	auto *t = new Tarea { modo, estado->resultado, leerConfigGCode(), gtk_check_button_get_active(estado->chkAislamiento) != FALSE, gtk_spin_button_get_value(estado->spinHuelgo),
//			estado->spinPasadas ? (int) gtk_spin_button_get_value(estado->spinPasadas) : 3, ++generacionActual, { } };
//	establecerEstado("Calculando trayectoria...");
//	GTask *task = g_task_new(nullptr, nullptr, alTerminarCalculo, nullptr);
//	g_task_set_task_data(task, t, [](gpointer p) {
//		delete static_cast<Tarea*>(p);
//	});
//	g_task_run_in_thread(task, hiloCalculo);
//	g_object_unref(task);
//}
//
//void solicitarRecalculo() {
//	if (!estado->tieneArchivo || !(estado->mostrarTrayectoria || estado->mostrarTrayectoriaGerber)) {
//		return;
//	}
//	if (temporizador) {
//		g_source_remove(temporizador);
//	}
//	temporizador = g_timeout_add(300, [](gpointer) -> gboolean {
//		temporizador = 0;
//		lanzarCalculo(modoActual());
//		return G_SOURCE_REMOVE;
//	}, nullptr);
//}
//
//// ─────────────────────────────────────────────────────────────
//// Botones de perfilado (señal "clicked" de un GtkButton)
//// ─────────────────────────────────────────────────────────────
//void alPerfilarCobre(GtkButton*, gpointer) {
//	lanzarCalculo(ModoCalculo::PerfilCobre);
//}
//void alPerfilarIslas(GtkButton*, gpointer) {
//	lanzarCalculo(ModoCalculo::Islas);
//}
//void alAislamientoCorregido(GtkButton*, gpointer) {
//	lanzarCalculo(ModoCalculo::AislamientoCorregido);
//}
//
//// ─────────────────────────────────────────────────────────────
//// GCode de la trayectoria visible
//// ─────────────────────────────────────────────────────────────
//static std::string tituloTrayectoria() {
//	if (estado->mostrarPerfiladoCobre) {
//		return "Perfilado de cobre";
//	}
//	if (estado->mostrarPerfiladoIslas) {
//		return "Perfilado de islas";
//	}
//	return "Aislamiento";
//}
//
//// Texto GCode de lo que se ve en pantalla; vacío si no hay trayectoria
//static std::string gcodeVisible(EstadisticasGCode *est) {
//	const bool esAislamiento = !estado->mostrarPerfiladoCobre && !estado->mostrarPerfiladoIslas;
//	if (estado->trayectoria.empty() || (estado->trayectoriaSucia && esAislamiento)) {
//		SeleccionTrayectoria();   // síncrono: hace falta el resultado ya
//	}
//	if (estado->trayectoria.empty()) {
//		return "";
//	}
//	return generarGCodeTrayectoria(estado->trayectoria, opcionesActuales(tituloTrayectoria()), est);
//}
//
//static std::string resumen(const EstadisticasGCode &e) {
//	std::ostringstream os;
//	os << e.lineas << " lineas, " << e.arcos << " arcos, corte " << (int) (e.largoCorte + 0.5) << " mm";
//	return os.str();
//}
//
//static void alGcodeGuardado(GObject *src, GAsyncResult *res, gpointer) {
//	GError *err = nullptr;
//	GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &err);
//	if (!file) {
//		if (err && err->code != GTK_DIALOG_ERROR_DISMISSED) {
//			establecerEstado("Error: " + std::string(err->message));
//		}
//		if (err) {
//			g_error_free(err);
//		}
//		return;
//	}
//	char *path = g_file_get_path(file);
//	g_object_unref(file);
//	if (!path) {
//		return;
//	}
//	EstadisticasGCode est;
//	const std::string gcode = gcodeVisible(&est);
//	if (gcode.empty()) {
//		establecerEstado("No hay trayectoria que guardar.");
//	} else {
//		std::ofstream out(path);
//		if (out.is_open()) {
//			out << gcode;
//			establecerEstado("✓ Guardado: " + std::string(path) + " (" + resumen(est) + ")");
//		} else {
//			establecerEstado("✗ No se pudo escribir: " + std::string(path));
//		}
//	}
//	g_free(path);
//}
//
//static void dialogoGuardar(const char *titulo, const std::string &nombre, GAsyncReadyCallback cb) {
//	GtkFileDialog *dlg = gtk_file_dialog_new();
//	gtk_file_dialog_set_title(dlg, titulo);
//	gtk_file_dialog_set_modal(dlg, TRUE);
//	gtk_file_dialog_set_initial_name(dlg, nombre.c_str());
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
//	gtk_file_dialog_save(dlg, GTK_WINDOW(estado->ventanaPrincipal), nullptr, cb, nullptr);
//	g_object_unref(dlg);
//}
//
//static std::string nombreBase(const std::string &ruta) {
//	std::string bn = ruta;
//	const size_t dot = bn.rfind('.');
//	if (dot != std::string::npos) {
//		bn = bn.substr(0, dot);
//	}
//	const size_t sl = bn.rfind('/');
//	if (sl != std::string::npos) {
//		bn = bn.substr(sl + 1);
//	}
//	return bn;
//}
//
//void alGuardarGCode(GtkButton*, gpointer) {
//	if (!estado->tieneArchivo) {
//		establecerEstado("Abra un Gerber primero.");
//		return;
//	}
//	dialogoGuardar("Guardar GCode", nombreBase(estado->rutaArchivo) + ".gcode", alGcodeGuardado);
//}
//
//void alPrevisualizarGCode(GtkButton*, gpointer) {
//	if (!estado->tieneArchivo) {
//		establecerEstado("Sin Gerber cargado.");
//		return;
//	}
//	EstadisticasGCode est;
//	const std::string gcode = gcodeVisible(&est);
//	if (gcode.empty()) {
//		establecerEstado("No hay trayectoria que mostrar.");
//		return;
//	}
//	std::istringstream iss(gcode);
//	std::string line;
//	std::ostringstream preview;
//	int cnt = 0;
//	while (cnt < 80 && std::getline(iss, line)) {
//		preview << line << "\n";
//		++cnt;
//	}
//	if (est.lineas > 80) {
//		preview << "\n... (" << (est.lineas - 80) << " lineas mas)\n";
//	}
//
//	GtkWidget *dlg = gtk_window_new();
//	gtk_window_set_title(GTK_WINDOW(dlg), ("Vista previa GCode - " + resumen(est)).c_str());
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
//
//// ─────────────────────────────────────────────────────────────
//// GCode de taladrado
//// ─────────────────────────────────────────────────────────────
//static void alTaladroGuardado(GObject *src, GAsyncResult *res, gpointer) {
//	GError *err = nullptr;
//	GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &err);
//	if (!file) {
//		if (err && err->code != GTK_DIALOG_ERROR_DISMISSED) {
//			establecerEstado("Error: " + std::string(err->message));
//		}
//		if (err) {
//			g_error_free(err);
//		}
//		return;
//	}
//	char *path = g_file_get_path(file);
//	g_object_unref(file);
//	if (!path) {
//		return;
//	}
//	const double peck = spinPeck ? gtk_spin_button_get_value(spinPeck) : 0.0;
//	const std::string gcode = generarGCodeTaladroT(estado->resultadoTaladro, opcionesActuales("Taladrado"), peck);
//	std::ofstream out(path);
//	if (out.is_open()) {
//		out << gcode;
//		const size_t lines = std::count(gcode.begin(), gcode.end(), '\n');
//		establecerEstado("✅ Taladrado guardado: " + std::string(path) + " (" + std::to_string(lines) + " lineas)");
//	} else {
//		establecerEstado("❌ Error al escribir: " + std::string(path));
//	}
//	g_free(path);
//}
//
//void alGenerarGCodeTaladro(GtkButton*, gpointer) {
//	if (!estado->tieneTaladro) {
//		establecerEstado("Cargue un archivo de taladrado (.drl) primero.");
//		return;
//	}
//	gtk_check_button_set_active(estado->chkMostrarTaladro, TRUE);
//	gtk_check_button_set_active(estado->chkMostrarTrayectoriaTaladro, TRUE);
//	dialogoGuardar("Guardar GCode de Taladrado", nombreBase(estado->rutaTaladro) + "_taladro.gcode", alTaladroGuardado);
//}
