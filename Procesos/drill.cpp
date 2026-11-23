/*
 * Procesos/drill.cpp
 *
 *  Created on: 14 abr 2026
 *      Author: DjSteker
 */

#include "drill.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unordered_map>

namespace drill {

namespace {

// Número sin depender del locale (GTK deja LC_NUMERIC con coma decimal en es_ES).
// Avanza 'i' hasta el primer carácter que no es parte del número.
bool leerNumero(const std::string &s, size_t &i, double &out) {
	size_t j = i;
	bool neg = false;
	if (j < s.size() && (s[j] == '-' || s[j] == '+')) {
		neg = (s[j] == '-');
		++j;
	}
	double v = 0.0;
	bool hay = false;
	while (j < s.size() && std::isdigit((unsigned char) s[j])) {
		v = v * 10.0 + (s[j] - '0');
		hay = true;
		++j;
	}
	if (j < s.size() && (s[j] == '.' || s[j] == ',')) {
		++j;
		double f = 0.1;
		while (j < s.size() && std::isdigit((unsigned char) s[j])) {
			v += (s[j] - '0') * f;
			f *= 0.1;
			hay = true;
			++j;
		}
	}
	if (!hay) {
		return false;
	}
	out = neg ? -v : v;
	i = j;
	return true;
}

struct Formato {
	int enteros = 3;      // dígitos enteros (sin punto decimal)
	int decimales = 3;
	bool ceroInicial = false;   // LZ explícito: se omiten los ceros finales
};

// Coordenada: con punto decimal se lee tal cual; sin él se aplica el formato
bool leerCoordenada(const std::string &s, size_t &i, const Formato &f, double &out) {
	size_t j = i;
	if (j < s.size() && (s[j] == '-' || s[j] == '+')) {
		++j;
	}
	size_t k = j;
	bool decimal = false;
	while (k < s.size() && (std::isdigit((unsigned char) s[k]) || s[k] == '.' || s[k] == ',')) {
		if (s[k] == '.' || s[k] == ',') {
			decimal = true;
		}
		++k;
	}
	if (k == j) {
		return false;
	}
	if (decimal) {
		return leerNumero(s, i, out);
	}
	const bool neg = (s[i] == '-');
	std::string dig = s.substr(j, k - j);
	const size_t total = (size_t) (f.enteros + f.decimales);
	if (f.ceroInicial && dig.size() < total) {
		dig.append(total - dig.size(), '0');   // LZ: faltan ceros por la derecha
	}
	double v = 0.0;
	for (char c : dig) {
		v = v * 10.0 + (c - '0');
	}
	v /= std::pow(10.0, f.decimales);
	i = k;
	out = neg ? -v : v;
	return true;
}

}  // namespace

DrillResult DrillParser::parseFile(const std::string &path) {
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		DrillResult r;
		r.ok = false;
		r.error = "No se pudo abrir el archivo: " + path;
		return r;
	}
	std::string src((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	return parseString(src);
}

DrillResult DrillParser::parseString(const std::string &src) {
	DrillResult res;
	res.unit = "MM";
	std::string unidad = "MM";
	Formato fmt;
	bool formatoFijado = false;
	std::unordered_map<int, double> diametros;
	int herramienta = 0;
	double diam = 0.3;

	std::istringstream in(src);
	std::string linea;
	while (std::getline(in, linea)) {
		const size_t a = linea.find_first_not_of(" \t\r\n");
		if (a == std::string::npos) {
			continue;
		}
		linea = linea.substr(a, linea.find_last_not_of(" \t\r\n") - a + 1);
		if (linea[0] == ';' || linea[0] == '%') {
			continue;
		}

		// Unidades y formato: METRIC,TZ  /  INCH,LZ  /  M71 / M72
		if (linea.rfind("METRIC", 0) == 0 || linea.rfind("INCH", 0) == 0) {
			unidad = (linea[0] == 'M') ? "MM" : "IN";
			if (!formatoFijado) {
				fmt.enteros = (unidad == "MM") ? 3 : 2;
				fmt.decimales = (unidad == "MM") ? 3 : 4;
			}
			fmt.ceroInicial = (linea.find("LZ") != std::string::npos);
			continue;
		}
		if (linea == "M71" || linea == "M72") {
			unidad = (linea == "M71") ? "MM" : "IN";
			continue;
		}
		if (linea.rfind("M48", 0) == 0 || linea.rfind("M30", 0) == 0 || linea.rfind("FMAT", 0) == 0 || linea.rfind("VER", 0) == 0 || linea.rfind("G90", 0) == 0 || linea.rfind("G05", 0) == 0
				|| linea.rfind("G93", 0) == 0 || linea.rfind("M95", 0) == 0 || linea.rfind("ICI", 0) == 0) {
			continue;
		}

		// Herramienta: T1C0.600 (definición) o T1 (selección)
		if (linea[0] == 'T') {
			size_t i = 1;
			double n = 0;
			if (!leerNumero(linea, i, n)) {
				continue;
			}
			const int num = (int) n;
			if (i < linea.size() && linea[i] == 'C') {
				++i;
				double d = 0;
				if (leerNumero(linea, i, d)) {
					diametros[num] = d;
				}
			} else {
				herramienta = num;
				auto it = diametros.find(num);
				if (it != diametros.end()) {
					diam = it->second;
				}
			}
			continue;
		}

		// Coordenadas: X..Y..   o ranura  X..Y..G85X..Y..
		if (linea[0] == 'X' || linea[0] == 'Y' || linea.rfind("G85", 0) == 0) {
			const size_t g85 = linea.find("G85");
			const std::string p1 = (g85 == std::string::npos) ? linea : linea.substr(0, g85);
			auto leerXY = [&](const std::string &s, double &x, double &y) {
				bool hx = false, hy = false;
				for (size_t i = 0; i < s.size();) {
					if (s[i] == 'X') {
						++i;
						hx = leerCoordenada(s, i, fmt, x);
					} else if (s[i] == 'Y') {
						++i;
						hy = leerCoordenada(s, i, fmt, y);
					} else {
						++i;
					}
				}
				return hx && hy;
			};
			DrillOperation op;
			if (!leerXY(p1, op.x, op.y)) {
				continue;
			}
			if (g85 != std::string::npos) {
				if (!leerXY(linea.substr(g85 + 3), op.x2, op.y2)) {
					continue;
				}
				op.ranura = true;
			}
			op.diameter = diam;
			op.toolNumber = herramienta;
			op.unit = unidad;
			res.operations.push_back(op);
		}
	}

	res.unit = unidad;
	res.ok = !res.operations.empty();
	if (!res.ok) {
		res.error = "No se encontraron taladros en el archivo";
	}
	return res;
}

double utils::mmToInch(double mm) {
	return mm / 25.4;
}
double utils::inchToMm(double inch) {
	return inch * 25.4;
}

}  // namespace drill
