/*
 * Procesos/exportar_gcode.cpp
 *
 *  Created on: 20 nov 2026
 *      Author: usuario001
 */

#include "exportar_gcode.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>

namespace {

using gerber::Vec2;

double dist(const Vec2 &a, const Vec2 &b) {
	return std::hypot(a.x - b.x, a.y - b.y);
}

// Número con decimales fijos, independiente del locale
std::string num(double v, int dec) {
	if (std::abs(v) < 0.5 * std::pow(10.0, -dec)) {
		v = 0.0;
	}
	char buf[64];
	auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, dec);
	return std::string(buf, r.ptr);
}

double dist2PuntoSeg(const Vec2 &p, const Vec2 &a, const Vec2 &b) {
	const double vx = b.x - a.x, vy = b.y - a.y, l2 = vx * vx + vy * vy;
	double t = (l2 > 1e-24) ? ((p.x - a.x) * vx + (p.y - a.y) * vy) / l2 : 0.0;
	t = std::clamp(t, 0.0, 1.0);
	const double ex = p.x - (a.x + vx * t), ey = p.y - (a.y + vy * t);
	return ex * ex + ey * ey;
}

// Ramer-Douglas-Peucker iterativo; conserva siempre el primer y el último punto
std::vector<Vec2> simplificar(const std::vector<Vec2> &p, double tol) {
	const size_t n = p.size();
	if (n < 3) {
		return p;
	}
	std::vector<char> keep(n, 0);
	keep[0] = keep[n - 1] = 1;
	std::vector<std::pair<size_t, size_t>> pila { { 0, n - 1 } };
	const double tol2 = tol * tol;
	while (!pila.empty()) {
		const auto [i, j] = pila.back();
		pila.pop_back();
		double mx = -1.0;
		size_t k = 0;
		for (size_t m = i + 1; m < j; ++m) {
			const double d = dist2PuntoSeg(p[m], p[i], p[j]);
			if (d > mx) {
				mx = d;
				k = m;
			}
		}
		if (mx > tol2) {
			keep[k] = 1;
			pila.push_back( { i, k });
			pila.push_back( { k, j });
		}
	}
	std::vector<Vec2> out;
	for (size_t i = 0; i < n; ++i) {
		if (keep[i]) {
			out.push_back(p[i]);
		}
	}
	return out;
}

// ¿Los puntos i..j caen en un arco de circunferencia (dentro de tol)?
bool ajustarArco(const std::vector<Vec2> &p, size_t i, size_t j, double tol, Vec2 &c, bool &ccw) {
	const Vec2 &a = p[i], &b = p[(i + j) / 2], &d = p[j];
	const double D = 2.0 * (a.x * (b.y - d.y) + b.x * (d.y - a.y) + d.x * (a.y - b.y));
	if (std::abs(D) < 1e-12) {
		return false;
	}
	const double a2 = a.x * a.x + a.y * a.y, b2 = b.x * b.x + b.y * b.y, d2 = d.x * d.x + d.y * d.y;
	c.x = (a2 * (b.y - d.y) + b2 * (d.y - a.y) + d2 * (a.y - b.y)) / D;
	c.y = (a2 * (d.x - b.x) + b2 * (a.x - d.x) + d2 * (b.x - a.x)) / D;
	const double r = dist(c, a);
	if (r < 0.02 || r > 2000.0) {
		return false;
	}
	const double cruz = (b.x - a.x) * (d.y - b.y) - (b.y - a.y) * (d.x - b.x);
	ccw = cruz > 0.0;
	const double PI = std::acos(-1.0);
	double barrido = 0.0;
	double ang0 = std::atan2(p[i].y - c.y, p[i].x - c.x);
	for (size_t k = i; k <= j; ++k) {
		if (std::abs(dist(p[k], c) - r) > 0.5 * tol) {
			return false;                            // el vértice no está sobre la circunferencia
		}
		if (k > i) {
			const double L = dist(p[k], p[k - 1]);
			if (r - std::sqrt(std::max(0.0, r * r - 0.25 * L * L)) > 0.5 * tol) {
				return false;                        // cuerda larga: el arco se separaría de la poligonal
			}
			const double ang1 = std::atan2(p[k].y - c.y, p[k].x - c.x);
			double dl = ang1 - ang0;
			while (dl > PI) {
				dl -= 2.0 * PI;
			}
			while (dl < -PI) {
				dl += 2.0 * PI;
			}
			if ((ccw && dl <= 0.0) || (!ccw && dl >= 0.0) || std::abs(dl) > 1.2) {
				return false;                        // sentido contrario o paso demasiado grande
			}
			barrido += std::abs(dl);
			ang0 = ang1;
		}
	}
	return barrido < 2.0 * PI * 0.97;
}

class Escritor {
public:
	Escritor(const OpcionesGCode &o) :
			o_(o), esc_(o.metrico ? 1.0 : 1.0 / 25.4) {
	}

	std::string texto;
	EstadisticasGCode est;

	void linea(const std::string &s) {
		texto += s;
		texto += '\n';
		++est.lineas;
	}
	void comentario(const std::string &s) {
		if (o_.comentarios) {
			linea("; " + s);
		}
	}
	std::string c(double v) const {          // coordenada/medida en la unidad de salida
		return num(v * esc_, o_.decimales);
	}
	std::string xy(const Vec2 &p) const {
		return "X" + c(p.x) + " Y" + c(p.y);
	}
	std::string feed(double f) const {
		return " F" + num(f * esc_, 1);
	}

	void cabecera() {
		comentario(o_.titulo + " - generado por Visor Gerber");
		linea(o_.metrico ? "G21" : "G20");
		linea("G17");
		linea("G90");
		linea("G94");
		linea("G00 Z" + c(o_.zSeguro));
		linea("M03 S" + std::to_string(o_.husillo));
		arriba = true;
	}
	void pie() {
		subir();
		linea("G00 X" + c(0) + " Y" + c(0));
		linea("M05");
		linea("M30");
	}

	void subir() {
		if (!arriba) {
			linea("G00 Z" + c(o_.zSeguro));
			arriba = true;
		}
	}
	void rapido(const Vec2 &p) {
		subir();
		linea("G00 " + xy(p));
		pos = p;
		tienePos = true;
	}
	void bajar(const Vec2 &p) {
		if (!tienePos || dist(pos, p) > 0.5 * std::pow(10.0, -o_.decimales)) {
			rapido(p);
		}
		if (arriba) {
			linea("G01 Z" + c(o_.zCorte) + feed(o_.penetracion));
			arriba = false;
			faltaF = true;
		}
	}

	// Corte a lo largo de una polilínea (la fresa ya está abajo en pts[0])
	void polilinea(const std::vector<Vec2> &pts) {
		size_t i = 0;
		std::vector<Vec2> tramo { pts[0] };
		auto volcar = [&]() {
			const std::vector<Vec2> s = simplificar(tramo, o_.tolerancia);
			for (size_t k = 1; k < s.size(); ++k) {
				recta(s[k]);
			}
			tramo.assign(1, pos);
		};
		const size_t n = pts.size();
		while (i + 1 < n) {
			size_t mejor = 0;
			Vec2 cm;
			bool ccwM = false;
			if (o_.usarArcos) {
				int fallos = 0;
				for (size_t j = i + 3; j < n && fallos < 3; ++j) {
					Vec2 cc;
					bool cw;
					if (ajustarArco(pts, i, j, o_.tolerancia, cc, cw)) {
						mejor = j;
						cm = cc;
						ccwM = cw;
						fallos = 0;
					} else {
						++fallos;
					}
				}
			}
			if (mejor) {
				volcar();
				arco(pts[mejor], cm, ccwM);
				tramo.assign(1, pos);
				i = mejor;
			} else {
				tramo.push_back(pts[i + 1]);
				++i;
			}
		}
		volcar();
	}

	bool arriba = true;
	Vec2 pos;
	bool tienePos = false;

private:
	void recta(const Vec2 &p) {
		if (dist(p, pos) < 0.5 * std::pow(10.0, -o_.decimales)) {
			return;
		}
		est.largoCorte += dist(p, pos);
		linea("G01 " + xy(p) + (faltaF ? feed(o_.avance) : ""));
		faltaF = false;
		pos = p;
	}
	void arco(const Vec2 &fin, const Vec2 &centro, bool ccw) {
		// I,J relativos al punto de partida ya redondeado: el radio sale coherente
		const Vec2 ini { std::round(pos.x * esc_ * std::pow(10.0, o_.decimales)) / (esc_ * std::pow(10.0, o_.decimales)), std::round(pos.y * esc_ * std::pow(10.0, o_.decimales))
				/ (esc_ * std::pow(10.0, o_.decimales)) };
		{
			const double a0 = std::atan2(pos.y - centro.y, pos.x - centro.x), a1 = std::atan2(fin.y - centro.y, fin.x - centro.x);
			double dl = a1 - a0;
			const double PI = std::acos(-1.0);
			if (ccw) {
				while (dl <= 0.0) {
					dl += 2.0 * PI;
				}
			} else {
				while (dl >= 0.0) {
					dl -= 2.0 * PI;
				}
			}
			est.largoCorte += std::abs(dl) * dist(centro, pos);
		}
		linea(std::string(ccw ? "G03 " : "G02 ") + xy(fin) + " I" + c(centro.x - ini.x) + " J" + c(centro.y - ini.y) + (faltaF ? feed(o_.avance) : ""));
		faltaF = false;
		++est.arcos;
		pos = fin;
	}

	const OpcionesGCode &o_;
	double esc_;
	bool faltaF = true;
};

}  // namespace

std::string ExportarGcode::generarGCodeTrayectoria(const std::vector<MovimientoHerramienta> &mov, const OpcionesGCode &op, EstadisticasGCode *est) {
	Escritor w(op);
	w.cabecera();

	// Si el último movimiento es el retorno al origen del Gerber, se sustituye por el X0 Y0 de la máquina
	size_t n = mov.size();
	if (n > 0 && mov[n - 1].tipo == TipoMovimiento::Viaje && std::abs(mov[n - 1].hasta.x) < 1e-9 && std::abs(mov[n - 1].hasta.y) < 1e-9) {
		--n;
	}

	size_t i = 0;
	while (i < n) {
		const MovimientoHerramienta &m = mov[i];
		switch (m.tipo) {
		case TipoMovimiento::Subir:
			w.subir();
			++i;
			break;
		case TipoMovimiento::Bajar:
			w.bajar(op.tr.aplicar(m.desde));
			++i;
			break;
		case TipoMovimiento::Viaje: {
			const Vec2 p = op.tr.aplicar(m.hasta);
			if (!w.tienePos || dist(p, w.pos) > 1e-6 || !w.arriba) {
				w.rapido(p);
			}
			++i;
			break;
		}
		case TipoMovimiento::Corte: {
			std::vector<Vec2> pts { op.tr.aplicar(m.desde) };
			while (i < n && mov[i].tipo == TipoMovimiento::Corte && (pts.size() == 1 || dist(op.tr.aplicar(mov[i].desde), pts.back()) < 1e-6)) {
				pts.push_back(op.tr.aplicar(mov[i].hasta));
				++i;
			}
			w.bajar(pts[0]);          // sube/viaja/baja solo si hace falta (p. ej. perfilados sin Z)
			w.polilinea(pts);
			break;
		}
		}
	}
	w.pie();
	w.comentario("Fin");
	if (est) {
		*est = w.est;
	}
	return w.texto;
}

std::string ExportarGcode::generarGCodeTaladroT(const drill::DrillResult &res, const OpcionesGCode &op, double peck) {
	Escritor w(op);
	if (!res.ok || res.operations.empty()) {
		return "; Error: No hay operaciones de taladrado\nM30\n";
	}
	OpcionesGCode o = op;
	o.titulo = "Taladrado";
	Escritor t(o);
	t.cabecera();

	std::map<double, std::vector<const drill::DrillOperation*>> porDiametro;
	for (const auto &d : res.operations) {
		porDiametro[d.diameter].push_back(&d);
	}

	const double profundidad = std::abs(o.zCorte);
	// Niveles de profundidad (negativos): uno solo, o varios pasos de 'peck'
	std::vector<double> niveles;
	if (peck > 1e-6 && profundidad > peck) {
		for (double z = peck; z < profundidad - 1e-9; z += peck) {
			niveles.push_back(-z);
		}
	}
	niveles.push_back(-profundidad);

	Vec2 actual { 0, 0 };
	int herr = 1;
	for (auto& [diam, ops] : porDiametro) {
		t.comentario("Broca " + num(diam, 3) + " mm");
		t.subir();
		t.linea("M06 T" + std::to_string(herr++));

		// vecino más cercano
		std::vector<const drill::DrillOperation*> pend = ops, orden;
		while (!pend.empty()) {
			size_t b = 0;
			double bd = 1e300;
			for (size_t k = 0; k < pend.size(); ++k) {
				const double dd = dist(actual, o.tr.aplicar( { pend[k]->x, pend[k]->y }));
				if (dd < bd) {
					bd = dd;
					b = k;
				}
			}
			orden.push_back(pend[b]);
			const auto *d = pend[b];
			actual = o.tr.aplicar(d->ranura ? Vec2 { d->x2, d->y2 } : Vec2 { d->x, d->y });
			pend.erase(pend.begin() + (long) b);
		}

		for (const auto *d : orden) {
			const Vec2 p = o.tr.aplicar( { d->x, d->y });
			t.rapido(p);
			if (!d->ranura) {
				for (double z : niveles) {
					t.linea("G01 Z" + t.c(z) + t.feed(o.penetracion));
					if (z != niveles.back()) {
						t.linea("G00 Z" + t.c(o.zSeguro > 1.0 ? 1.0 : o.zSeguro));   // sale a vaciar virutas
					}
				}
				t.linea("G00 Z" + t.c(o.zSeguro));
			} else {
				const Vec2 q = o.tr.aplicar( { d->x2, d->y2 });
				bool enP = true;
				for (double z : niveles) {
					t.linea("G01 Z" + t.c(z) + t.feed(o.penetracion));
					t.linea("G01 " + t.xy(enP ? q : p) + t.feed(o.avance));
					enP = !enP;
				}
				t.linea("G00 Z" + t.c(o.zSeguro));
				t.pos = enP ? p : q;
			}
			t.arriba = true;
			t.pos = d->ranura ? o.tr.aplicar( { d->x2, d->y2 }) : p;
			if (d->ranura) {
				t.tienePos = true;
			}
		}
	}
	t.pie();
	return t.texto;
}

