/*
 * Procesos/gerber.cpp
 *
 *  Created on: 13 abr 2026
 *      Author: DjSteker
 */

// =============================================================================
//  gerber.cpp  –  Implementación RS-274X Gerber Parser / GCode Generator
//  Sin dependencias externas, C++17, Linux
// =============================================================================
#include "gerber.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <cassert>
#include <stdexcept>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace gerber {

// =============================================================================
//  GerberResult  –  iteradores de conveniencia
// =============================================================================

void GerberResult::forEachSegment(std::function<void(const Segment&, Polarity)> cb) const {
	for (const auto &p : primitives)
		if (p.type == PrimType::Segment) {
			cb(p.asSegment(), p.polaridad);
		}
}

void GerberResult::forEachArc(std::function<void(const Arc&, Polarity)> cb) const {
	for (const auto &p : primitives)
		if (p.type == PrimType::Arc) {
			cb(p.asArc(), p.polaridad);
		}
}

void GerberResult::forEachFlash(std::function<void(const Flash&, Polarity)> cb) const {
	for (const auto &p : primitives)
		if (p.type == PrimType::Flash) {
			cb(p.asFlash(), p.polaridad);
		}
}

void GerberResult::forEachRegion(std::function<void(const Region&, Polarity)> cb) const {
	for (const auto &p : primitives)
		if (p.type == PrimType::Region) {
			cb(p.asRegion(), p.polaridad);
		}
}

// =============================================================================
//  GerberParser  –  utilidades internas
// =============================================================================

// -----------------------------------------------------------------------------
//  parseCoord
//  Convierte la cadena de coordenada Gerber (formato "FSLAX46Y46") a mm.
//  Ejemplo: "41500000" con decD=6  →  41.500000 mm
//           "-26365000" con decD=6 → -26.365000 mm
// -----------------------------------------------------------------------------
double GerberParser::parseCoord(const std::string &s, int /*intD*/, int decD) {
	if (s.empty()) {
		return 0.0;
	}
	bool neg = (s[0] == '-');
	const std::string digits = neg ? s.substr(1) : s;
	if (digits.empty()) {
		return 0.0;
	}
	double raw = static_cast<double>(std::stoll(digits));
	double divisor = std::pow(10.0, static_cast<double>(decD));
	return neg ? -(raw / divisor) : (raw / divisor);
}

// -----------------------------------------------------------------------------
//  splitByX
//  Divide una cadena de parámetros Gerber usando 'X' como separador.
//  Los valores negativos (empiezan por '-') se respetan correctamente.
//  splitByX - VERSIÓN ROBUSTA SIN DEPENDENCIA DE LOCALE
// ─────────────────────────────────────────────────────────────────────────────
static std::vector<double> splitByX(const std::string &s) {
	std::vector<double> vals;
	size_t pos = 0;
	const size_t N = s.size();

	printf("  splitByX: parseando '%s'\n", s.c_str());

	while (pos < N) {
		// Localizar fin del token (próximo 'X' o fin de string)
		size_t end = N;
		for (size_t k = pos; k < N; ++k) {
			if (s[k] == 'X') {
				end = k;
				break;
			}
		}

		std::string tok = s.substr(pos, end - pos);

		// Parser manual: no depende de locale
		if (!tok.empty()) {
			double val = 0.0;
			bool neg = false;
			size_t i = 0;

			// Signo
			if (tok[0] == '-') {
				neg = true;
				i = 1;
			}

			// Parte entera
			while (i < tok.size() && tok[i] >= '0' && tok[i] <= '9') {
				val = val * 10.0 + (tok[i] - '0');
				i++;
			}

			// Punto decimal
			if (i < tok.size() && (tok[i] == '.' || tok[i] == ',')) {
				i++;
				double frac = 0.1;
				while (i < tok.size() && tok[i] >= '0' && tok[i] <= '9') {
					val += (tok[i] - '0') * frac;
					frac *= 0.1;
					i++;
				}
			}

			if (neg) {
				val = -val;
			}

			printf("    Token '%s' → %.10f\n", tok.c_str(), val);
			vals.push_back(val);
		}

		pos = (end < N) ? end + 1 : N;
	}

	return vals;
}

// -----------------------------------------------------------------------------
//  parseAperture
//  def: parte de la definición ADD sin el número de apertura.
//  Ejemplos:
//    "C,2.400000"
//    "R,1.050000X1.500000"
//    "O,1.050000X1.500000"
//    "P,1.6X6X0.0"
//    "RoundRect,0.25X-0.55X0.55X-0.55X-0.55X0.55X-0.55X0.55X0.55X0"
//
//  Para el macro RoundRect de KiCad los 10 parámetros son:
//    $1        = radio de esquina
//    $2,$3     = X,Y esquina 1
//    $4,$5     = X,Y esquina 2
//    $6,$7     = X,Y esquina 3
//    $8,$9     = X,Y esquina 4
//    $10       = ángulo de rotación (grados)
// -----------------------------------------------------------------------------

// ─────────────────────────────────────────────────────────────────────────────
//  parseAperture (VERSIÓN CON DEBUG)
//  Imprime cada paso de parseo para diagnosticar el problema
// ─────────────────────────────────────────────────────────────────────────────
Aperture GerberParser::parseAperture(const std::string &def, const std::unordered_map<std::string, std::string> &macros) {
	Aperture ap;
	ap.type = ApertureType::Circle;

	// ========== DEBUG ENTRADA ==========
	printf("\n╔════════════════════════════════════════════════════════╗\n");
	printf("║         parseAperture ENTRADA                         ║\n");
	printf("╚════════════════════════════════════════════════════════╝\n");
	printf("  def = '%s'\n", def.c_str());
	printf("  def.length() = %zu bytes\n", def.size());
	printf("  Bytes: ");
	for (size_t i = 0; i < def.size() && i < 100; i++) {
		printf("%02X ", (unsigned char) def[i]);
	}
	printf("\n");

	size_t comma = def.find(',');
	std::string shape = (comma != std::string::npos) ? def.substr(0, comma) : def;
	std::string params = (comma != std::string::npos) ? def.substr(comma + 1) : "";

	printf("\n  Parsing:\n");
	printf("    shape = '%s'\n", shape.c_str());
	printf("    params = '%s' (%zu bytes)\n", params.c_str(), params.size());

	// ========== PARSEAR VALORES ==========
	std::vector<double> vals = splitByX(params);

	printf("\n  splitByX() returned %zu values:\n", vals.size());
	for (size_t i = 0; i < vals.size(); i++) {
		printf("    [%zu] = %.10f  (hex: %a)\n", i, vals[i], vals[i]);
	}

	auto get = [&](size_t i) -> double {
		double v = (i < vals.size()) ? vals[i] : 0.0;
		return v;
	};

	// ========== RoundRect (KiCad macro) ==========
	//    if (shape == "RoundRect" || def.find("RoundRect") != std::string::npos) {
	//        printf("\n  ✓ RoundRect detectado\n");
	//        ap.type = ApertureType::RoundRect;
	//        ap.cornerRadius = std::abs(get(0));
	//
	//        if (vals.size() >= 9) {
	//            double x1 = get(1), y1 = get(2);
	//            double x2 = get(3), y2 = get(4);
	//            double x3 = get(5), y3 = get(6);
	//            double x4 = get(7), y4 = get(8);
	//
	//            double w = std::abs(x2 - x1) + 2 * ap.cornerRadius;
	//            double h = std::abs(y3 - y1) + 2 * ap.cornerRadius;
	//
	//            ap.outer = w;
	//            ap.height = h;
	//            ap.centerX = (x1 + x2 + x3 + x4) / 8.0;
	//            ap.centerY = (y1 + y2 + y3 + y4) / 8.0;
	//
	//            printf("    cornerRadius = %.10f\n", ap.cornerRadius);
	//            printf("    outer (ancho) = %.10f mm\n", ap.outer);
	//            printf("    height (alto) = %.10f mm\n", ap.height);
	//            printf("    centerX = %.10f, centerY = %.10f\n", ap.centerX, ap.centerY);
	// ========== RoundRect (KiCad macro) ==========
	if (shape == "RoundRect" || def.find("RoundRect") != std::string::npos) {
		printf("\n  ✓ RoundRect detectado\n");
		ap.type = ApertureType::RoundRect;
		ap.cornerRadius = std::abs(get(0));

		if (vals.size() >= 9) {
			double x1 = get(1), y1 = get(2);
			double x2 = get(3), y2 = get(4);
			double x3 = get(5), y3 = get(6);
			double x4 = get(7), y4 = get(8);

			printf("    ╔═══════════════════════════════════════════════════════╗\n");
			printf("    ║  VÉRTICES CRUDOS (corner centers):                   ║\n");
			printf("    ╠═══════════════════════════════════════════════════════╣\n");
			printf("    ║  corner1: x=%.10f, y=%.10f                    ║\n", x1, y1);
			printf("    ║  corner2: x=%.10f, y=%.10f                    ║\n", x2, y2);
			printf("    ║  corner3: x=%.10f, y=%.10f                    ║\n", x3, y3);
			printf("    ║  corner4: x=%.10f, y=%.10f                    ║\n", x4, y4);
			printf("    ╚═══════════════════════════════════════════════════════╝\n");

			// Encontrar extremos REALES (mejor que asumir orden)
			double minX = std::min( { x1, x2, x3, x4 });
			double maxX = std::max( { x1, x2, x3, x4 });
			double minY = std::min( { y1, y2, y3, y4 });
			double maxY = std::max( { y1, y2, y3, y4 });

			printf("    Extremos: X=[%.10f, %.10f], Y=[%.10f, %.10f]\n", minX, maxX, minY, maxY);

			// ← AHORA USAMOS LOS EXTREMOS REALES, NO INDICES ESPECÍFICOS
			double w = (maxX - minX) + 2 * ap.cornerRadius;
			double h = (maxY - minY) + 2 * ap.cornerRadius;

			ap.outer = w;
			ap.height = h;
			ap.centerX = (minX + maxX) / 2.0;
			ap.centerY = (minY + maxY) / 2.0;

			printf("    outer (ancho) = %.10f mm\n", ap.outer);
			printf("    height (alto) = %.10f mm\n", ap.height);
			printf("    cornerRadius = %.10f mm\n", ap.cornerRadius);
			printf("    centerX = %.10f, centerY = %.10f\n", ap.centerX, ap.centerY);
		} else if (vals.size() >= 3) {
			ap.outer = get(1);
			ap.height = get(2);
			printf("    outer = %.10f mm, height = %.10f mm (fallback)\n", ap.outer, ap.height);
		}
		printf("╚════════════════════════════════════════════════════════╝\n\n");
		return ap;
	}

	// ========== Oblong (O) - Cápsula ==========
	if (shape == "O") {
		printf("\n  ✓ Oblong (cápsula) detectado\n");
		ap.type = ApertureType::Oblong;
		ap.outer = get(0);
		ap.height = (vals.size() > 1) ? get(1) : get(0);
		printf("    outer = %.10f mm\n", ap.outer);
		printf("    height = %.10f mm\n", ap.height);
		printf("╚════════════════════════════════════════════════════════╝\n\n");
		return ap;
	}

	// ========== Rectangle (R) ==========
	if (shape == "R") {
		printf("\n  ✓ Rectangle detectado\n");
		ap.type = ApertureType::Rectangle;
		ap.outer = get(0);
		ap.height = (vals.size() > 1) ? get(1) : get(0);
		printf("    outer (ancho) = %.10f mm\n", ap.outer);
		printf("    height (alto) = %.10f mm\n", ap.height);
		printf("╚════════════════════════════════════════════════════════╝\n\n");
		return ap;
	}

	// ========== Circle (C) - MÁS IMPORTANTE ==========
	if (shape == "C") {
		printf("\n  ✓ Circle detectado\n");
		ap.type = ApertureType::Circle;
		ap.outer = get(0);
		printf("    outer (diámetro) = %.10f mm\n", ap.outer);
		printf("    radio = %.10f mm\n", ap.outer / 2.0);
		printf("╚════════════════════════════════════════════════════════╝\n\n");
		return ap;
	}

	// ========== Polygon (P) ==========
	if (shape == "P") {
		printf("\n  ✓ Polygon detectado\n");
		ap.type = ApertureType::Polygon;
		ap.outer = get(0);
		ap.sides = (vals.size() > 1) ? (int) get(1) : 3;
		ap.rot = (vals.size() > 2) ? get(2) : 0.0;
		printf("    outer = %.10f mm\n", ap.outer);
		printf("    sides = %d\n", ap.sides);
		printf("    rotation = %.10f°\n", ap.rot);
		printf("╚════════════════════════════════════════════════════════╝\n\n");
		return ap;
	}

	// ========== Fallback ==========
	printf("\n  ⚠ Shape DESCONOCIDO: '%s' (usando fallback Circle)\n", shape.c_str());
	ap.outer = get(0);
	printf("    outer = %.10f mm\n", ap.outer);
	printf("╚════════════════════════════════════════════════════════╝\n\n");
	return ap;
}

// -----------------------------------------------------------------------------
//  evaluateMacro
// -----------------------------------------------------------------------------
Aperture GerberParser::evaluateMacro(const std::string &name, const std::vector<double> &params, const std::string& /*macroDef*/) {
	if (name == "RoundRect" && params.size() >= 9) {
		Aperture ap;
		ap.type = ApertureType::RoundRect;
		ap.cornerRadius = std::abs(params[0]);

		double xs[4] = { params[1], params[3], params[5], params[7] };
		double ys[4] = { params[2], params[4], params[6], params[8] };

		double minX = *std::min_element(xs, xs + 4);
		double maxX = *std::max_element(xs, xs + 4);
		double minY = *std::min_element(ys, ys + 4);
		double maxY = *std::max_element(ys, ys + 4);

		ap.outer = maxX - minX;
		ap.height = maxY - minY;
		ap.centerX = (minX + maxX) * 0.5;
		ap.centerY = (minY + maxY) * 0.5;
		ap.rot = (params.size() > 9) ? params[9] : 0.0;
		return ap;
	}

	Aperture fallback;
	fallback.type = ApertureType::Circle;
	fallback.outer = params.empty() ? 1.0 : std::abs(params[0]);
	return fallback;
}

// -----------------------------------------------------------------------------
//  commitSegment / commitArc
// -----------------------------------------------------------------------------
void GerberParser::commitSegment(const Vec2 &from, const Vec2 &to, double w, State &st) {
	Primitive p;
	p.type = PrimType::Segment;
	p.polaridad = st.polaridad;
	p.layer = st.layer;
	Segment seg { from, to, w };
	p.data = seg;
	computeBounds(p, st.bounds);
	st.result.primitives.push_back(std::move(p));
}

void GerberParser::commitArc(const Vec2 &from, const Vec2 &to, const Vec2 &offset, bool ccw, double w, State &st) {
	Vec2 center = { from.x + offset.x, from.y + offset.y };
	double radius = (center - from).length();

	double rEnd = (center - to).length();
	if (std::abs(radius - rEnd) > 1e-3) {
		radius = (radius + rEnd) * 0.5;
	}

	double startAngle = std::atan2(from.y - center.y, from.x - center.x);
	double endAngle = std::atan2(to.y - center.y, to.x - center.x);

	if (ccw) {
		while (endAngle < startAngle - 1e-9) {
			endAngle += 2.0 * M_PI;
		}
	} else {
		while (endAngle > startAngle + 1e-9) {
			endAngle -= 2.0 * M_PI;
		}
	}

	if (from.x == to.x && from.y == to.y && (offset.x != 0.0 || offset.y != 0.0)) {
		endAngle = startAngle + (ccw ? 2.0 * M_PI : -2.0 * M_PI);
	}

	Primitive p;
	p.type = PrimType::Arc;
	p.polaridad = st.polaridad;
	p.layer = st.layer;
	Arc arc;
	arc.start = from;
	arc.end = to;
	arc.center = center;
	arc.radius = radius;
	arc.startAngle = startAngle;
	arc.endAngle = endAngle;
	arc.ccw = ccw;
	arc.width = w;
	p.data = arc;
	computeBounds(p, st.bounds);
	st.result.primitives.push_back(std::move(p));
}

// -----------------------------------------------------------------------------
//  computeBounds
// -----------------------------------------------------------------------------
void GerberParser::computeBounds(const Primitive &p, BBox &bb) {
	switch (p.type) {
	case PrimType::Segment: {
		const auto &s = p.asSegment();
		double hw = s.width / 2.0;
		bb.expand(s.start.x - hw, s.start.y - hw);
		bb.expand(s.start.x + hw, s.start.y + hw);
		bb.expand(s.end.x - hw, s.end.y - hw);
		bb.expand(s.end.x + hw, s.end.y + hw);
		break;
	}
	case PrimType::Arc: {
		const auto &a = p.asArc();
		double r = a.radius + a.width / 2.0;
		bb.expand(a.center.x - r, a.center.y - r);
		bb.expand(a.center.x + r, a.center.y + r);
		break;
	}
	case PrimType::Flash: {
		const auto &f = p.asFlash();
		double hw = f.aperture.outer / 2.0;
		double hh = (f.aperture.height > 0 ? f.aperture.height : f.aperture.outer) / 2.0;
		double ox = f.pos.x + f.aperture.centerX;
		double oy = f.pos.y + f.aperture.centerY;
		bb.expand(ox - hw, oy - hh);
		bb.expand(ox + hw, oy + hh);
		break;
	}
	case PrimType::Region: {
		const auto &reg = p.asRegion();
		for (const auto &v : reg.outline)
			bb.expand(v);
		for (const auto &hole : reg.holes) {
			for (const auto &v : hole) {
				bb.expand(v);
			}
		}
		break;
	}
	}
}

// =============================================================================
//  GerberParser  –  processParam  (bloques %...%)
// =============================================================================
void GerberParser::processParam(const std::string &param, State &st) {
	if (param.size() < 2) {
		return;
	}

	// FS: Format Specification
	if (param[0] == 'F' && param[1] == 'S') {
		st.trailingZero = (param.size() > 2 && param[2] == 'T');
		st.absolute = (param.find('A') != std::string::npos);
		size_t xp = param.find('X');
		if (xp != std::string::npos && xp + 2 < param.size()) {
			st.intDigits = param[xp + 1] - '0';
			st.decDigits = param[xp + 2] - '0';
		}
		return;
	}

	// MO: Mode (units)
	if (param[0] == 'M' && param[1] == 'O' && param.size() > 2) {
		st.result.unit = param.substr(2);
		return;
	}

	// LP: Layer Polarity
	if (param[0] == 'L' && param[1] == 'P' && param.size() > 2) {
		st.polaridad = (param[2] == 'D') ? Polarity::Dark : Polarity::Clear;
		return;
	}

	// LN: Layer Name (obsoleto)
	if (param[0] == 'L' && param[1] == 'N') {
		return;
	}

	// AD: Aperture Definition
	if (param[0] == 'A' && param[1] == 'D' && param.size() > 3 && param[2] == 'D') {
		size_t i = 3;
		while (i < param.size() && std::isdigit(param[i]))
			++i;
		if (i == 3) {
			return;
		}

		int apNum = std::stoi(param.substr(3, i - 3));
		std::string def = param.substr(i);
		while (!def.empty() && def.back() == '*') {
			def.pop_back();
		}

		st.apertures[apNum] = parseAperture(def, st.apertureMacros);
		return;
	}

	// AM: Aperture Macro
	if (param[0] == 'A' && param[1] == 'M') {
		size_t star = param.find('*');
		if (star == std::string::npos) {
			return;
		}
		std::string name = param.substr(2, star - 2);
		st.apertureMacros[name] = param;
		return;
	}

	// SR: Step and Repeat (ignorar)
	if (param[0] == 'S' && param[1] == 'R') {
		return;
	}

	// TF/TA/TO/TD: Atributos (ignorar)
	if (param[0] == 'T' && (param[1] == 'F' || param[1] == 'A' || param[1] == 'O' || param[1] == 'D')) {
		return;
	}
}

// =============================================================================
//  GerberParser  –  processCommand  (bloques de datos, terminados en '*')
// =============================================================================
void GerberParser::processCommand(const std::string &cmd, State &st) {
	if (cmd.empty()) {
		return;
	}
	if (cmd.size() >= 3 && cmd[0] == 'G' && cmd[1] == '0' && cmd[2] == '4') {
		return;
	}
	if (cmd == "M02" || cmd == "M00" || cmd == "M01") {
		return;
	}

	int gcode = -1, dcode = -1;
	bool hasX = false, hasY = false, hasI = false, hasJ = false;
	std::string sX, sY, sI, sJ;

	auto readNum = [&](size_t &pos) -> std::string {
		size_t j = pos;
		if (j < cmd.size() && cmd[j] == '-') {
			++j;
		}
		while (j < cmd.size() && std::isdigit(cmd[j])) {
			++j;
		}
		std::string tok = cmd.substr(pos, j - pos);
		pos = j;
		return tok;
	};

	size_t i = 0;
	while (i < cmd.size()) {
		char c = cmd[i++];
		switch (c) {
		case 'G': {
			size_t j = i;
			while (j < cmd.size() && std::isdigit(cmd[j]))
				++j;
			if (j > i) {
				gcode = std::stoi(cmd.substr(i, j - i));
			}
			i = j;
			break;
		}
		case 'X': {
			sX = readNum(i);
			hasX = !sX.empty();
			break;
		}
		case 'Y': {
			sY = readNum(i);
			hasY = !sY.empty();
			break;
		}
		case 'I': {
			sI = readNum(i);
			hasI = !sI.empty();
			break;
		}
		case 'J': {
			sJ = readNum(i);
			hasJ = !sJ.empty();
			break;
		}
		case 'D': {
			size_t j = i;
			while (j < cmd.size() && std::isdigit(cmd[j]))
				++j;
			if (j > i) {
				dcode = std::stoi(cmd.substr(i, j - i));
			}
			i = j;
			break;
		}
		case 'M': {
			while (i < cmd.size() && std::isdigit(cmd[i]))
				++i;
			break;
		}
		default:
			break;
		}
	}

	// G-code: cambios de estado
	if (gcode >= 0) {
		switch (gcode) {
		case 1:
			st.interpMode = 1;
			break;
		case 2:
			st.interpMode = 2;
			break;
		case 3:
			st.interpMode = 3;
			break;
		case 36:
			st.inRegion = true;
			st.regionContour.clear();
			break;
		case 37: {
			if (!st.regionContour.empty()) {
				Primitive p;
				p.type = PrimType::Region;
				p.polaridad = st.polaridad;
				p.layer = st.layer;
				Region reg;
				reg.outline = std::move(st.regionContour);
				p.data = reg;
				computeBounds(p, st.bounds);
				st.result.primitives.push_back(std::move(p));
			}
			st.inRegion = false;
			st.regionContour.clear();
			break;
		}
		case 70:
			st.result.unit = "IN";
			break;
		case 71:
			st.result.unit = "MM";
			break;
		case 74:
			st.multiQuadrant = false;
			break;
		case 75:
			st.multiQuadrant = true;
			break;
		case 90:
			st.absolute = true;
			break;
		case 91:
			st.absolute = false;
			break;
		default:
			break;
		}
	}

	// Selección de apertura (D10+)
	if (dcode >= 10) {
		if (st.apertures.count(dcode)) {
			st.currentAperture = dcode;
		}
		return;
	}

	// Calcular posición destino
	Vec2 target = st.pos;
	if (hasX) {
		target.x = parseCoord(sX, st.intDigits, st.decDigits);
	}
	if (hasY) {
		target.y = parseCoord(sY, st.intDigits, st.decDigits);
	}
	if (!st.absolute) {
		if (hasX) {
			target.x += st.pos.x;
		}
		if (hasY) {
			target.y += st.pos.y;
		}
	}

	Vec2 arcOffset = { 0.0, 0.0 };
	if (hasI) {
		arcOffset.x = parseCoord(sI, st.intDigits, st.decDigits);
	}
	if (hasJ) {
		arcOffset.y = parseCoord(sJ, st.intDigits, st.decDigits);
	}

	// D01: Dibujar
	if (dcode == 1) {
		double w = 0.0;
		auto it = st.apertures.find(st.currentAperture);
		if (it != st.apertures.end()) {
			w = it->second.outer;
		}

		if (st.inRegion) {
			if (st.regionContour.empty()) {
				st.regionContour.push_back(st.pos);
			}
			st.regionContour.push_back(target);
		} else {
			if (st.interpMode == 1) {
				commitSegment(st.pos, target, w, st);
			} else {
				bool ccw = (st.interpMode == 3);
				commitArc(st.pos, target, arcOffset, ccw, w, st);
			}
		}
		st.pos = target;
		return;
	}

	// D02: Mover sin dibujar
	if (dcode == 2) {
		if (st.inRegion && !st.regionContour.empty()) {
			Primitive p;
			p.type = PrimType::Region;
			p.polaridad = st.polaridad;
			p.layer = st.layer;
			Region reg;
			reg.outline = std::move(st.regionContour);
			p.data = reg;
			computeBounds(p, st.bounds);
			st.result.primitives.push_back(std::move(p));
			st.regionContour.clear();
		}
		st.pos = target;
		return;
	}

	// D03: Flash
	if (dcode == 3) {
		auto it = st.apertures.find(st.currentAperture);
		if (it != st.apertures.end()) {
			Primitive p;
			p.type = PrimType::Flash;
			p.polaridad = st.polaridad;
			p.layer = st.layer;
			Flash f { target, it->second };
			p.data = f;
			computeBounds(p, st.bounds);
			st.result.primitives.push_back(std::move(p));
		}
		st.pos = target;
		return;
	}

	// D-code implícito dentro de región
	if (dcode == -1 && (hasX || hasY) && st.inRegion) {
		if (st.regionContour.empty()) {
			st.regionContour.push_back(st.pos);
		}
		st.regionContour.push_back(target);
		st.pos = target;
	}
}

// =============================================================================
//  GerberParser  –  parseString  (parser principal)
// =============================================================================
GerberResult GerberParser::parseString(const std::string &src) {
	State st;
	st.result.unit = "MM";

	std::string s;
	s.reserve(src.size());
	for (char c : src)
		if (c != '\r') {
			s += c;
		}

	const size_t N = s.size();
	size_t pos = 0;

	while (pos < N) {
		while (pos < N && (s[pos] == ' ' || s[pos] == '\n' || s[pos] == '\t'))
			++pos;
		if (pos >= N) {
			break;
		}

		if (s[pos] == '%') {
			++pos;
			size_t end = s.find('%', pos);
			if (end == std::string::npos) {
				break;
			}
			std::string block = s.substr(pos, end - pos);
			pos = end + 1;

			size_t bpos = 0;
			while (bpos < block.size()) {
				size_t star = block.find('*', bpos);
				std::string sub = (star != std::string::npos) ? block.substr(bpos, star - bpos) : block.substr(bpos);
				while (!sub.empty() && (sub.back() == '\n' || sub.back() == '\r' || sub.back() == ' ')) {
					sub.pop_back();
				}
				if (!sub.empty()) {
					processParam(sub, st);
				}
				if (star == std::string::npos) {
					break;
				}
				bpos = star + 1;
			}
		} else {
			size_t star = s.find('*', pos);
			if (star == std::string::npos) {
				break;
			}
			std::string cmd = s.substr(pos, star - pos);
			pos = star + 1;
			while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r' || cmd.back() == ' ')) {
				cmd.pop_back();
			}
			if (!cmd.empty()) {
				processCommand(cmd, st);
			}
		}
	}

	st.result.bounds = st.bounds;
	st.result.ok = st.result.error.empty();
	return st.result;
}

GerberResult GerberParser::parseFile(const std::string &path) {
	std::ifstream f(path, std::ios::binary);
	if (!f.is_open()) {
		GerberResult r;
		r.error = "No se puede abrir el archivo: " + path;
		return r;
	}
	std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	return parseString(content);
}

// =============================================================================
//  namespace geom  –  utilidades geométricas
// =============================================================================
namespace geom {

// -----------------------------------------------------------------------------
//  arcToPolyline
// -----------------------------------------------------------------------------
std::vector<Vec2> arcToPolyline(const Vec2 &center, double radius, double startAngle, double endAngle, bool ccw, double tolerance) {
	std::vector<Vec2> pts;
	if (radius <= 0.0) {
		return pts;
	}

	double span = endAngle - startAngle;
	if (ccw && span < 0.0) {
		span += 2.0 * M_PI;
	}
	if (!ccw && span > 0.0) {
		span -= 2.0 * M_PI;
	}

	double halfStep = std::acos(std::max(-1.0, std::min(1.0, 1.0 - tolerance / radius)));
	int nSegs = std::max(4, static_cast<int>(std::ceil(std::abs(span) / (2.0 * halfStep))));

	pts.reserve(nSegs + 1);
	for (int k = 0; k <= nSegs; ++k) {
		double t = static_cast<double>(k) / nSegs;
		double a = startAngle + t * span;
		pts.push_back( { center.x + radius * std::cos(a), center.y + radius * std::sin(a) });
	}
	return pts;
}

// -----------------------------------------------------------------------------
//  signedArea  (positivo = CCW)
// -----------------------------------------------------------------------------
double signedArea(const std::vector<Vec2> &poly) {
	double area = 0.0;
	const size_t n = poly.size();
	for (size_t i = 0; i < n; ++i) {
		const size_t j = (i + 1) % n;
		area += poly[i].x * poly[j].y;
		area -= poly[j].x * poly[i].y;
	}
	return area / 2.0;
}

std::vector<Vec2> ensureCCW(std::vector<Vec2> poly) {
	if (signedArea(poly) < 0.0) {
		std::reverse(poly.begin(), poly.end());
	}
	return poly;
}

double angleBetween(const Vec2 &a, const Vec2 &b) {
	double la = a.length(), lb = b.length();
	if (la < 1e-12 || lb < 1e-12) {
		return 0.0;
	}
	double dot = (a.x * b.x + a.y * b.y) / (la * lb);
	return std::acos(std::max(-1.0, std::min(1.0, dot)));
}

// -----------------------------------------------------------------------------
//  rotatePolygon
// -----------------------------------------------------------------------------
std::vector<Vec2> rotatePolygon(std::vector<Vec2> poly, double angleRad, const Vec2 &pivot) {
	std::vector<Vec2> out;
	out.reserve(poly.size());
	double cosA = std::cos(angleRad);
	double sinA = std::sin(angleRad);
	for (const auto &v : poly) {
		double dx = v.x - pivot.x;
		double dy = v.y - pivot.y;
		out.push_back( { pivot.x + dx * cosA - dy * sinA, pivot.y + dx * sinA + dy * cosA });
	}
	return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// offsetPolygon mejorado - Mantiene distancia constante
// ─────────────────────────────────────────────────────────────────────────────

std::vector<gerber::Vec2> offsetPolygon(const std::vector<gerber::Vec2> &poly, double offset) {
	const size_t n = poly.size();
	if (n < 3)
		return poly;

	std::vector<gerber::Vec2> result;
	result.reserve(n);

	for (size_t i = 0; i < n; ++i) {
		const size_t prev = (i + n - 1) % n;
		const size_t next = (i + 1) % n;

		Vec2 d1 = (poly[i] - poly[prev]).normalized();
		Vec2 d2 = (poly[next] - poly[i]).normalized();

		Vec2 n1 = { -d1.y, d1.x };
		Vec2 n2 = { -d2.y, d2.x };

		// Bisector para mantener distancia constante
		Vec2 bisector = { n1.x + n2.x, n1.y + n2.y };
		double len = bisector.length();

		if (len < 1e-12) {
			// Ángulo muy agudo, usar normal promedio
			result.push_back( { poly[i].x + n1.x * offset, poly[i].y + n1.y * offset });
		} else {
			bisector.x /= len;
			bisector.y /= len;

			// Escalar para mantener distancia perpendicular constante
			double dot = n1.x * bisector.x + n1.y * bisector.y;
			double scale = (std::abs(dot) > 1e-9) ? offset / dot : offset;

			// Limitar para evitar explosión en ángulos agudos
			if (std::abs(scale) > std::abs(offset) * 5.0) {
				scale = offset * (scale > 0 ? 5.0 : -5.0);
			}

			result.push_back( { poly[i].x + bisector.x * scale, poly[i].y + bisector.y * scale });
		}
	}
	return result;
}

// =============================================================================
//  flashToPolygon
//  Convierte una aperture Flash a polígono (orientación CCW).
//
//  CORRECCIONES respecto a la versión anterior:
//
//  1. Rectangle:
//     - Vértices en el orden correcto (CCW) y con dimensiones exactas.
//       ap.outer = ancho (X), ap.height = alto (Y).
//
//  2. Oblong  (cápsula):
//     - Dos semicírculos más dos segmentos rectos.
//     - Si outer >= height → cápsula horizontal (r = height/2).
//     - Si height > outer  → cápsula vertical   (r = outer/2).
//
//  3. RoundRect:
//     TABLA CORREGIDA (orden CCW, ángulos validados):
//     ┌────────────────┬──────────────────────┬──────────────┐
//     │ Esquina        │ Centro arco (local)  │  startAngle  │
//     ├────────────────┼──────────────────────┼──────────────┤
//     │ top-right      │ (+hw-r,  +hh-r)      │    0°        │
//     │ top-left       │ (-hw+r,  +hh-r)      │   90°        │
//     │ bottom-left    │ (-hw+r,  -hh+r)      │  180°        │
//     │ bottom-right   │ (+hw-r,  -hh+r)      │  270°        │
//     └────────────────┴──────────────────────┴──────────────┘
//     Cada arco recorre +90° (π/2).  Los tramos rectos entre arcos
//     quedan implícitos en los bordes del polígono.
//
//     Continuidad comprobada:
//       end  top-right   (hw,   hh-r) → start top-left    (-hw+r, hh)  ✓ borde superior
//       end  top-left    (-hw,  hh-r) → start bottom-left (-hw, -hh+r) ✓ borde izquierdo
//       end  bottom-left (-hw+r,-hh)  → start bottom-right(hw-r, -hh)  ✓ borde inferior
//       end  bottom-right(hw,  -hh+r) → start top-right   (hw,   hh-r) ✓ borde derecho
//
//  4. Polygon (P):
//     - Soporte real con lados, radio y rotación correctos.
//
//  5. Se aplica ap.centerX / ap.centerY para macros con centro desplazado.
//  6. Se aplica rotación (ap.rot) al polígono completo para RoundRect y Polygon.
// =============================================================================
std::vector<Vec2> flashToPolygon(const Flash &f, int segsPerCircle) {
	std::vector<Vec2> poly;
	const Aperture &ap = f.aperture;

	// Centro efectivo de la apertura en coordenadas de placa
	const Vec2 c = { f.pos.x + ap.centerX, f.pos.y + ap.centerY };

	switch (ap.type) {

	// ── Circulo ────────────────────────────────────────────────────────────
	case ApertureType::Circle: {
		double r = ap.outer / 2.0;
		printf("DEBUG Circle: outer=%.4f mm, radio=%.4f mm, puntos=%d\n", ap.outer, r, segsPerCircle);
		if (r <= 0.0) {
			printf("DEBUG Circle: con radio cero");
			break;
		}
		poly.reserve(segsPerCircle);
		for (int k = 0; k < segsPerCircle; ++k) {
			double a = 2.0 * M_PI * k / segsPerCircle;
			poly.push_back( { c.x + r * std::cos(a), c.y + r * std::sin(a) });
		}
		break;
	}

		// ── Rectangulo ─────────────────────────────────────────────────────────
		//   ap.outer  = ancho total  (dimensión en X)
		//   ap.height = alto total   (dimensión en Y)
	case ApertureType::Rectangle: {
		double hw = ap.outer / 2.0;
		double hh = (ap.height > 1e-9 ? ap.height : ap.outer) / 2.0;
		// 4 vértices en orden CCW (bottom-right → top-right → top-left → bottom-left)
		poly = { { c.x + hw, c.y - hh }, { c.x + hw, c.y + hh }, { c.x - hw, c.y + hh }, { c.x - hw, c.y - hh }, };
		break;
	}

		// ── Oblongo (capsula) ──────────────────────────────────────────────────
		//   La forma es un rectángulo con semicírculos en los extremos cortos.
		//   ap.outer  = ancho   (dimensión X)
		//   ap.height = alto    (dimensión Y)
		//   Si outer >= height → capsula horizontal, r = height/2
		//   Si height > outer  → capsula vertical,   r = outer/2
	case ApertureType::Oblong: {
		double W = ap.outer;
		double H = (ap.height > 1e-9 ? ap.height : ap.outer);
		int hs = std::max(8, segsPerCircle / 2);  // semgmentos por semicirculo

		poly.reserve(2 * (hs + 1));

		if (W >= H) {
			// ── Capsula horizontal ────────────────────────────────────────
			double r = H / 2.0;
			double dx = (W - H) / 2.0;  // desplazamiento de los centros
			// Semicirculo derecho: de -90° a +90° (CCW)
			for (int k = 0; k <= hs; ++k) {
				double a = -M_PI / 2.0 + M_PI * k / hs;
				poly.push_back( { c.x + dx + r * std::cos(a), c.y + r * std::sin(a) });
			}
			// Semicirculo izquierdo: de +90° a +270° (CCW)
			for (int k = 0; k <= hs; ++k) {
				double a = M_PI / 2.0 + M_PI * k / hs;
				poly.push_back( { c.x - dx + r * std::cos(a), c.y + r * std::sin(a) });
			}
		} else {
			// ── Capsula vertical ──────────────────────────────────────────
			double r = W / 2.0;
			double dy = (H - W) / 2.0;
			// Semicirculo superior: de 0° a 180° (CCW)
			for (int k = 0; k <= hs; ++k) {
				double a = M_PI * k / hs;
				poly.push_back( { c.x + r * std::cos(a), c.y + dy + r * std::sin(a) });
			}
			// Semicirculo inferior: de 180° a 360° (CCW)
			for (int k = 0; k <= hs; ++k) {
				double a = M_PI + M_PI * k / hs;
				poly.push_back( { c.x + r * std::cos(a), c.y - dy + r * std::sin(a) });
			}
		}
		break;
	}

		// ── Rectangulo con esquinas redondeadas ────────────────────────────────
		//   ap.outer        = ancho total
		//   ap.height       = alto total
		//   ap.cornerRadius = radio de esquina
		//   ap.rot          = rotación (grados), aplicada al final
	case ApertureType::RoundRect: {
		double hw = ap.outer / 2.0;
		double hh = (ap.height > 1e-9 ? ap.height : ap.outer) / 2.0;
		// El radio no puede superar la mitad de ninguna dimensión
		double r = std::min( { ap.cornerRadius, hw, hh });
		if (r < 1e-9) {
			r = 0.0;
		}

		const int cs = 8;  // segmentos por arco de esquina

		// Definición de las 4 esquinas en orden estrictamente CCW:
		//
		//   Esquina       Centro del arco (coords locales)   Angulo inicial
		//   top-right     ( hw-r,  hh-r)                       0°
		//   top-left      (-hw+r,  hh-r)                      90°
		//   bottom-left   (-hw+r, -hh+r)                     180°
		//   bottom-right  ( hw-r, -hh+r)                     270°
		//
		struct CornerDef {
			double cx, cy, startA;
		};
		const CornerDef corners[4] = { { hw - r, hh - r, 0.0 }, { -hw + r, hh - r, M_PI / 2.0 }, { -hw + r, -hh + r, M_PI }, { hw - r, -hh + r, 3.0 * M_PI / 2.0 } };

		poly.reserve(4 * (cs + 1));
		for (const auto &corner : corners) {
			for (int k = 0; k <= cs; ++k) {
				double t = static_cast<double>(k) / cs;
				double a = corner.startA + t * (M_PI / 2.0);
				poly.push_back( { c.x + corner.cx + r * std::cos(a), c.y + corner.cy + r * std::sin(a) });
			}
		}
		break;
	}

		// ── Poligono regular (apertura tipo P) ─────────────────────────────────
		//   ap.outer = diametro del circulo circunscrito
		//   ap.sides = numero de lados (>= 3)
		//   ap.rot   = rotacion inicial (grados), aplicada al final
	case ApertureType::Polygon: {
		double r = ap.outer / 2.0;
		int n = std::max(3, ap.sides);
		double rot = ap.rot * M_PI / 180.0;
		poly.reserve(n);
		for (int k = 0; k < n; ++k) {
			double a = rot + 2.0 * M_PI * k / n;
			poly.push_back( { c.x + r * std::cos(a), c.y + r * std::sin(a) });
		}
		break;
	}

		// ── Macro generica u otro tipo desconocido ─────────────────────────────
	default: {
		double r = (ap.outer > 0 ? ap.outer : 1.0) / 2.0;
		const int n = std::max(8, segsPerCircle);
		poly.reserve(n);
		for (int k = 0; k < n; ++k) {
			double a = 2.0 * M_PI * k / n;
			poly.push_back( { c.x + r * std::cos(a), c.y + r * std::sin(a) });
		}
		break;
	}

	}  // switch

	// Aplicar rotacion del aperture (solo RoundRect y Polygon pueden tenerla)
	if (std::abs(ap.rot) > 1e-6 && (ap.type == ApertureType::Polygon || ap.type == ApertureType::RoundRect)) {
		poly = rotatePolygon(std::move(poly), ap.rot * M_PI / 180.0, c);
	}

	return poly;
}

}  // namespace geom

// =============================================================================
//  GCodeGenerator  –  helpers de bajo nivel
// =============================================================================

std::string GCodeGenerator::fmt(double v) const {
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(cfg_.decimalPlaces) << v;
	return oss.str();
}

void GCodeGenerator::emit(const std::string &line) {
	out_ += line + "\n";
}

void GCodeGenerator::comment(const std::string &s) {
	if (cfg_.addComments) {
		emit("; " + s);
	}
}

void GCodeGenerator::rapid(double x, double y, double z) {
	std::string line = "G00 X" + fmt(x) + " Y" + fmt(y);
	if (z > -1e17) {
		line += " Z" + fmt(z);
	}
	emit(line);
	curPos_ = { x, y };
}

void GCodeGenerator::move(double x, double y) {
	emit("G01 X" + fmt(x) + " Y" + fmt(y) + " F" + fmt(cfg_.feedRate));
	curPos_ = { x, y };
}

void GCodeGenerator::moveZ(double z) {
	emit("G01 Z" + fmt(z) + " F" + fmt(cfg_.plungeRate));
}

void GCodeGenerator::arc(double x, double y, double i, double j, bool ccw) {
	emit(std::string(ccw ? "G03" : "G02") + " X" + fmt(x) + " Y" + fmt(y) + " I" + fmt(i) + " J" + fmt(j) + " F" + fmt(cfg_.feedRate));
	curPos_ = { x, y };
}

void GCodeGenerator::plunge() {
	moveZ(cfg_.traceZ);
}
void GCodeGenerator::retract() {
	emit("G00 Z" + fmt(cfg_.safeZ));
}

void GCodeGenerator::header() {
	comment("=== Gerber → GCode  generado por gerber::GCodeGenerator ===");
	emit(cfg_.metricMode ? "G21" : "G20");
	emit("G17");
	emit("G90");
	emit("G94");
	emit("G00 Z" + fmt(cfg_.safeZ));
	emit("M03 S1000");
	spindle_ = true;
}

void GCodeGenerator::footer() {
	comment("=== Fin de programa ===");
	retract();
	emit("G00 X" + fmt(0.0) + " Y" + fmt(0.0));
	emit("M05");
	emit("M30");
}

// =============================================================================
//  GCodeGenerator  –  mecanizado de primitivas
// =============================================================================

void GCodeGenerator::machineSegment(const Segment &seg) {
	rapid(tx(seg.start.x), ty(seg.start.y), cfg_.safeZ);
	plunge();
	move(tx(seg.end.x), ty(seg.end.y));
	retract();
}

void GCodeGenerator::machineArc(const Arc &a) {
	double x1 = tx(a.start.x), y1 = ty(a.start.y);
	double x2 = tx(a.end.x), y2 = ty(a.end.y);
	double ci = tx(a.center.x) - x1;
	double cj = ty(a.center.y) - y1;

	rapid(x1, y1, cfg_.safeZ);
	plunge();
	if (cfg_.useArcCommands) {
		arc(x2, y2, ci, cj, a.ccw);
	} else {
		auto pts = geom::arcToPolyline(a.center, a.radius, a.startAngle, a.endAngle, a.ccw, cfg_.arcTolerance);
		for (size_t k = 1; k < pts.size(); ++k)
			move(tx(pts[k].x), ty(pts[k].y));
	}
	retract();
}

void GCodeGenerator::machineFlash(const Flash &f) {
	double cx = tx(f.pos.x), cy = ty(f.pos.y);
	double r = (f.aperture.outer * cfg_.scale) / 2.0;
	double td = cfg_.toolDiameter / 2.0;

	if (r <= td + 1e-6) {
		comment("Flash – plunge drill");
		rapid(cx, cy, cfg_.safeZ);
		moveZ(cfg_.cutZ);
		retract();
	} else {
		comment("Flash – mill circle r=" + fmt(r));
		double sx = cx + (r - td);
		rapid(sx, cy, cfg_.safeZ);
		plunge();
		arc(sx, cy, -(r - td), 0.0, true);
		retract();
	}
}

void GCodeGenerator::machineRegion(const Region &r) {
	if (r.outline.empty()) {
		return;
	}
	comment("Region contour");
	double x0 = tx(r.outline[0].x), y0 = ty(r.outline[0].y);
	rapid(x0, y0, cfg_.safeZ);
	plunge();
	for (size_t k = 1; k < r.outline.size(); ++k) {
		move(tx(r.outline[k].x), ty(r.outline[k].y));
	}
	move(x0, y0);
	retract();
}

// =============================================================================
//  GCodeGenerator  –  optimización de rutas (greedy nearest-neighbor)
// =============================================================================

std::vector<GCodeGenerator::PathOp> GCodeGenerator::buildOps(const GerberResult &res) {
	std::vector<PathOp> ops;
	ops.reserve(res.primitives.size());

	for (size_t idx = 0; idx < res.primitives.size(); ++idx) {
		const auto &prim = res.primitives[idx];
		PathOp op;
		op.idx = idx;

		switch (prim.type) {
		case PrimType::Segment:
			op.kind = PathOp::Kind::Segment;
			op.startPt = prim.asSegment().start;
			break;
		case PrimType::Arc:
			op.kind = PathOp::Kind::Arc;
			op.startPt = prim.asArc().start;
			break;
		case PrimType::Flash:
			op.kind = PathOp::Kind::Flash;
			op.startPt = prim.asFlash().pos;
			break;
		case PrimType::Region:
			op.kind = PathOp::Kind::Region;
			op.startPt = prim.asRegion().outline.empty() ? Vec2 { 0, 0 } : prim.asRegion().outline[0];
			break;
		}
		ops.push_back(op);
	}
	return ops;
}

std::vector<size_t> GCodeGenerator::optimizeOrder(const std::vector<PathOp> &ops) {
	const size_t n = ops.size();
	std::vector<size_t> order;
	order.reserve(n);
	std::vector<bool> used(n, false);

	Vec2 cur = { 0.0, 0.0 };
	for (size_t step = 0; step < n; ++step) {
		size_t best = n;
		double bestDist = 1e30;
		for (size_t k = 0; k < n; ++k) {
			if (used[k]) {
				continue;
			}
			Vec2 d = ops[k].startPt - cur;
			double dist = d.x * d.x + d.y * d.y;
			if (dist < bestDist) {
				bestDist = dist;
				best = k;
			}
		}
		if (best < n) {
			used[best] = true;
			order.push_back(best);
			cur = ops[best].startPt;
		}
	}
	return order;
}

// =============================================================================
//  GCodeGenerator  –  generate
// =============================================================================
std::string GCodeGenerator::generate(const GerberResult &res) {
	out_.clear();
	curPos_ = { 0.0, 0.0 };
	spindle_ = false;

	header();

	std::vector<size_t> order;
	if (cfg_.optimizePaths) {
		auto ops = buildOps(res);
		order = optimizeOrder(ops);
	} else {
		order.resize(res.primitives.size());
		std::iota(order.begin(), order.end(), 0);
	}

	for (size_t idx : order) {
		const auto &prim = res.primitives[idx];
		if (prim.polaridad == Polarity::Clear) {
			continue;
		}

		switch (prim.type) {
		case PrimType::Segment:
			comment("--- Segment ---");
			machineSegment(prim.asSegment());
			break;
		case PrimType::Arc:
			comment("--- Arc ---");
			machineArc(prim.asArc());
			break;
		case PrimType::Flash:
			comment("--- Flash ---");
			machineFlash(prim.asFlash());
			break;
		case PrimType::Region:
			machineRegion(prim.asRegion());
			break;
		}
	}

	footer();
	return out_;
}

// =============================================================================
//  GCodeGenerator  –  generateIsolation
// =============================================================================

std::string GCodeGenerator::generateIsolation(const GerberResult &res, double clearance) {
	out_.clear();
	curPos_ = { 0.0, 0.0 };
	spindle_ = false;

	comment("=== Isolation routing ===");
	comment("Clearance: " + fmt(clearance) + "  Tool diam: " + fmt(cfg_.toolDiameter));
	header();

	res.forEachSegment([&](const Segment &seg, Polarity pol) {
		if (pol == Polarity::Clear) {
			return;
		}
		double offset = seg.width / 2.0 + clearance + cfg_.toolDiameter / 2.0;
		Vec2 d = (seg.end - seg.start).normalized();
		Vec2 n = { -d.y, d.x };
		comment("Isolation segment, offset=" + fmt(offset));
		for (int side = -1; side <= 1; side += 2) {
			rapid(tx(seg.start.x + n.x * offset * side), ty(seg.start.y + n.y * offset * side), cfg_.safeZ);
			plunge();
			move(tx(seg.end.x + n.x * offset * side), ty(seg.end.y + n.y * offset * side));
			retract();
		}
	});

	res.forEachArc([&](const Arc &a, Polarity pol) {
		if (pol == Polarity::Clear) {
			return;
		}
		double offset = a.width / 2.0 + clearance + cfg_.toolDiameter / 2.0;
		comment("Isolation arc");
		for (int side = -1; side <= 1; side += 2) {
			double r = a.radius + offset * side;
			if (r <= 0.0) {
				continue;
			}
			auto pts = geom::arcToPolyline(a.center, r, a.startAngle, a.endAngle, a.ccw, cfg_.arcTolerance);
			if (pts.empty()) {
				continue;
			}
			rapid(tx(pts[0].x), ty(pts[0].y), cfg_.safeZ);
			plunge();
			for (size_t k = 1; k < pts.size(); ++k) {
				move(tx(pts[k].x), ty(pts[k].y));
			}
			retract();
		}
	});

	res.forEachFlash([&](const Flash &f, Polarity pol) {
		if (pol == Polarity::Clear) {
			return;
		}
		auto outline = geom::flashToPolygon(f, 32);
		double off = clearance + cfg_.toolDiameter / 2.0;
		auto offsetOutline = geom::offsetPolygon(outline, off);  // ✅ Corregido
		if (offsetOutline.empty()) {
			return;
		}
		comment("Isolation flash");
		double x0 = tx(offsetOutline[0].x), y0 = ty(offsetOutline[0].y);
		rapid(x0, y0, cfg_.safeZ);
		plunge();
		for (size_t k = 1; k < offsetOutline.size(); k++) {
			move(tx(offsetOutline[k].x), ty(offsetOutline[k].y));
		}
		move(x0, y0);
		retract();
	});

	res.forEachRegion([&](const Region &reg, Polarity pol) {
		if (pol == Polarity::Clear) {
			return;
		}
		double off = clearance + cfg_.toolDiameter / 2.0;
		auto outline = geom::offsetPolygon(reg.outline, off);
		if (outline.empty()) {
			return;
		}
		comment("Isolation region");
		double x0 = tx(outline[0].x), y0 = ty(outline[0].y);
		rapid(x0, y0, cfg_.safeZ);
		plunge();
		for (size_t k = 1; k < outline.size(); ++k) {
			move(tx(outline[k].x), ty(outline[k].y));
		}
		move(x0, y0);
		retract();
	});

	footer();
	return out_;
}


