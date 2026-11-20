/*
 * mecanizado.cpp
 *
 *  Created on: 14 may 2026
 *      Author: DjSteker
 *  Lógica de generación de toolpath / recorrido CNC
 *
 *  Aislamiento v3: offset(unión) = unión(offsets).
 *  Por cada pasada se expande cada figura de la pista, se extrae la
 *  frontera exterior de la unión (recortada contra las zonas prohibidas
 *  de las demás pistas), se encadena en caminos continuos, se sueldan los
 *  huecos numéricos y cada camino se fresa de una vez. Entre caminos la
 *  fresa viaja con Z arriba en línea recta: no hay que esquivar cobre.
 *
 *  Las tapas de las cápsulas se generan sobre una rejilla angular global,
 *  de modo que dos segmentos que comparten un extremo producen vértices
 *  idénticos en la circunferencia común para cualquier ángulo de la curva.
 */

#include "mecanizado.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────────
// Utilidades geométricas internas
// ─────────────────────────────────────────────────────────────

static double d2(const gerber::Vec2 &a, const gerber::Vec2 &b) {
	double dx = a.x - b.x, dy = a.y - b.y;
	return dx * dx + dy * dy;
}

static double areaPoligono(const std::vector<gerber::Vec2> &poly) {
	if (poly.size() < 3) {
		return 0.0;
	}
	double area = 0.0;
	for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
		area += (poly[j].x + poly[i].x) * (poly[j].y - poly[i].y);
	}
	return std::abs(area * 0.5);
}

static bool pip(const gerber::Vec2 &pt, const std::vector<gerber::Vec2> &poly) {
	if (poly.size() < 3) {
		return false;
	}
	bool inside = false;
	for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
		if (((poly[i].y > pt.y) != (poly[j].y > pt.y)) && (pt.x < (poly[j].x - poly[i].x) * (pt.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)) {
			inside = !inside;
		}
	}
	return inside;
}

// Deja el polígono en sentido antihorario (área con signo positiva).
// Devuelve true si lo ha invertido.
static bool asegurarCCW(std::vector<gerber::Vec2> &poly) {
	if (poly.size() < 3) {
		return false;
	}
	double area2 = 0.0;
	for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
		area2 += poly[j].x * poly[i].y - poly[i].x * poly[j].y;
	}
	if (area2 >= 0.0) {
		return false;
	}
	std::reverse(poly.begin(), poly.end());
	return true;
}

// Distancia al cuadrado de un punto al borde de un polígono
static double dist2Borde(const gerber::Vec2 &p, const Poligono &q) {
	double mejor = 1e300;
	for (size_t k = 0, m = q.size(); k < m; ++k) {
		const gerber::Vec2 &a = q[k], &b = q[(k + 1) % m];
		const double vx = b.x - a.x, vy = b.y - a.y;
		const double l2 = vx * vx + vy * vy;
		double t = (l2 > 1e-24) ? ((p.x - a.x) * vx + (p.y - a.y) * vy) / l2 : 0.0;
		t = std::clamp(t, 0.0, 1.0);
		const double ex = p.x - (a.x + vx * t), ey = p.y - (a.y + vy * t);
		mejor = std::min(mejor, ex * ex + ey * ey);
	}
	return mejor;
}

// ─────────────────────────────────────────────────────────────
// Conversión de primitivas a polígonos
// ─────────────────────────────────────────────────────────────

// Cápsula simple de un segmento (usada por construirTrayectoria, modo contorno)
static std::vector<gerber::Vec2> segmentoAPoligono(const gerber::Segment &s, int ptsTapa = 8) {
	gerber::Vec2 d = s.end - s.start;
	double largo = d.length();
	if (largo < 1e-12) {
		return {};
	}
	double r = s.width * 0.5;
	std::vector<gerber::Vec2> poli;
	for (int i = 0; i <= ptsTapa; ++i) {
		double a = M_PI * i / ptsTapa;
		poli.push_back( { s.end.x + r * (std::cos(a) * (d.x / largo) - std::sin(a) * (d.y / largo)), s.end.y + r * (std::cos(a) * (d.y / largo) + std::sin(a) * (d.x / largo)) });
	}
	for (int i = 0; i <= ptsTapa; ++i) {
		double a = M_PI + M_PI * i / ptsTapa;
		poli.push_back( { s.start.x + r * (std::cos(a) * (d.x / largo) - std::sin(a) * (d.y / largo)), s.start.y + r * (std::sin(a) * (d.x / largo) + std::cos(a) * (d.y / largo)) });
	}
	return poli;
}

static const int PASOS_TAPA = 24;   // pasos por semicircunferencia (7.5°)

// Arco de a0 a a1 (a0 < a1): vértice en a0, vértices de la rejilla global
// k*paso estrictamente en medio, y vértice en a1.
static void anadirArcoGrid(std::vector<gerber::Vec2> &out, const gerber::Vec2 &c, double r, double a0, double a1, double paso) {
	out.push_back( { c.x + r * std::cos(a0), c.y + r * std::sin(a0) });
	long k = (long) std::floor(a0 / paso + 1e-9) + 1;
	for (; k * paso < a1 - 1e-9 * paso; ++k) {
		out.push_back( { c.x + r * std::cos(k * paso), c.y + r * std::sin(k * paso) });
	}
	out.push_back( { c.x + r * std::cos(a1), c.y + r * std::sin(a1) });
}

// Cápsula con tapas alineadas a la rejilla angular global
static std::vector<gerber::Vec2> segmentoACapsula(const gerber::Segment &seg, double ancho, int pasos = PASOS_TAPA) {
	const double halfW = std::max(ancho * 0.5, 0.01);
	const double dx = seg.end.x - seg.start.x;
	const double dy = seg.end.y - seg.start.y;
	const double len = std::sqrt(dx * dx + dy * dy);
	const double PI = std::acos(-1.0);
	const double paso = PI / pasos;

	std::vector<gerber::Vec2> cap;
	cap.reserve(2 * pasos + 4);
	if (len < 1e-6) {
		for (int i = 0; i < 2 * pasos; ++i) {
			cap.push_back( { seg.start.x + halfW * std::cos(i * paso), seg.start.y + halfW * std::sin(i * paso) });
		}
		return cap;
	}
	const double angDir = std::atan2(dy, dx);
	anadirArcoGrid(cap, seg.end, halfW, angDir - PI / 2.0, angDir + PI / 2.0, paso);
	anadirArcoGrid(cap, seg.start, halfW, angDir + PI / 2.0, angDir + 3.0 * PI / 2.0, paso);
	return cap;
}

// ─────────────────────────────────────────────────────────────
// Cajas envolventes e intersecciones (declarados en mecanizado.h)
// ─────────────────────────────────────────────────────────────

Caja cajaDe(const Poligono &p) {
	Caja c { 1e300, 1e300, -1e300, -1e300 };
	for (const auto &v : p) {
		c.x0 = std::min(c.x0, v.x);
		c.y0 = std::min(c.y0, v.y);
		c.x1 = std::max(c.x1, v.x);
		c.y1 = std::max(c.y1, v.y);
	}
	return c;
}

bool cajasSolapan(const Caja &a, const Caja &b) {
	return !(a.x1 < b.x0 || b.x1 < a.x0 || a.y1 < b.y0 || b.y1 < a.y0);
}

bool puntoEnCaja(const Caja &c, const gerber::Vec2 &p) {
	return p.x >= c.x0 && p.x <= c.x1 && p.y >= c.y0 && p.y <= c.y1;
}

bool interseccionSegmentos(const gerber::Vec2 &a, const gerber::Vec2 &b, const gerber::Vec2 &c, const gerber::Vec2 &d, double &t) {
	const double rx = b.x - a.x, ry = b.y - a.y;
	const double sx = d.x - c.x, sy = d.y - c.y;
	const double den = rx * sy - ry * sx;
	if (std::abs(den) < 1e-14) {
		return false;
	}
	const double qx = c.x - a.x, qy = c.y - a.y;
	const double tt = (qx * sy - qy * sx) / den;
	const double u = (qx * ry - qy * rx) / den;
	if (tt <= 1e-9 || tt >= 1.0 - 1e-9 || u < 0.0 || u > 1.0) {
		return false;
	}
	t = tt;
	return true;
}

// ─────────────────────────────────────────────────────────────
// fronteraUnion: frontera exterior de la unión, encadenada en caminos
// ─────────────────────────────────────────────────────────────

std::vector<Poligono> fronteraUnion(const std::vector<Poligono> &formas, const std::vector<const Poligono*> &prohibidas) {
	struct Pieza {
		gerber::Vec2 a, b;
	};
	std::vector<Pieza> piezas;

	std::vector<Caja> cf, cp;
	for (const auto &f : formas) {
		// Pequeño margen: una arista coincidente nunca queda fuera de la caja por redondeo
		Caja c = cajaDe(f);
		c.x0 -= 1e-6;
		c.y0 -= 1e-6;
		c.x1 += 1e-6;
		c.y1 += 1e-6;
		cf.push_back(c);
	}
	for (const auto *f : prohibidas) {
		cp.push_back(cajaDe(*f));
	}

	auto cortes = [](const gerber::Vec2 &a, const gerber::Vec2 &b, const Caja &ce, const Poligono &q, const Caja &cq, std::vector<double> &ts) {
		if (!cajasSolapan(ce, cq)) {
			return;
		}
		for (size_t k = 0, m = q.size(); k < m; ++k) {
			double t;
			if (interseccionSegmentos(a, b, q[k], q[(k + 1) % m], t)) {
				ts.push_back(t);
			}
		}
	};

	// 1) Trocear cada borde por sus intersecciones y quedarse con lo exterior
	for (size_t i = 0; i < formas.size(); ++i) {
		const Poligono &P = formas[i];
		const size_t n = P.size();
		for (size_t e = 0; e < n; ++e) {
			const gerber::Vec2 a = P[e], b = P[(e + 1) % n];
			if (d2(a, b) < 1e-18) {
				continue;
			}
			const Caja ce { std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y) };

			std::vector<double> ts { 0.0, 1.0 };
			for (size_t j = 0; j < formas.size(); ++j) {
				if (j != i) {
					cortes(a, b, ce, formas[j], cf[j], ts);
				}
			}
			for (size_t k = 0; k < prohibidas.size(); ++k) {
				cortes(a, b, ce, *prohibidas[k], cp[k], ts);
			}
			std::sort(ts.begin(), ts.end());

			auto punto = [&](double t) -> gerber::Vec2 {
				if (t <= 0.0) {
					return a;
				}
				if (t >= 1.0) {
					return b;
				}
				return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
			};

			for (size_t s = 0; s + 1 < ts.size(); ++s) {
				if (ts[s + 1] - ts[s] < 1e-9) {
					continue;
				}
				const gerber::Vec2 p0 = punto(ts[s]), p1 = punto(ts[s + 1]);
				const gerber::Vec2 mid { (p0.x + p1.x) * 0.5, (p0.y + p1.y) * 0.5 };

				bool descartar = false;
				for (size_t j = 0; j < formas.size() && !descartar; ++j) {
					if (j == i || !puntoEnCaja(cf[j], mid)) {
						continue;
					}
					if (dist2Borde(mid, formas[j]) <= 1e-12) {
						// Arista coincidente (misma tapa en la unión): sobrevive solo la de menor índice
						if (j < i) {
							descartar = true;
						}
					} else if (pip(mid, formas[j])) {
						descartar = true;
					}
				}
				for (size_t k = 0; k < prohibidas.size() && !descartar; ++k) {
					if (puntoEnCaja(cp[k], mid) && pip(mid, *prohibidas[k])) {
						descartar = true;
					}
				}
				if (!descartar) {
					piezas.push_back( { p0, p1 });
				}
			}
		}
	}

	// 2) Encadenar piezas (fin de una == inicio de otra, formas CCW => mismo sentido)
	const size_t N = piezas.size();
	std::vector<Poligono> caminos;
	if (N == 0) {
		return caminos;
	}
	const double tol = 1e-6, celda = 1e-5;
	using Clave = std::pair<long long, long long>;
	auto clave = [&](const gerber::Vec2 &p) {
		return Clave { llround(p.x / celda), llround(p.y / celda) };
	};
	std::map<Clave, std::vector<size_t>> porInicio;
	for (size_t k = 0; k < N; ++k) {
		porInicio[clave(piezas[k].a)].push_back(k);
	}

	std::vector<long> sig(N, -1), ant(N, -1);
	for (size_t k = 0; k < N; ++k) {
		const Clave c = clave(piezas[k].b);
		double mejorD = tol * tol;
		long best = -1;
		for (int dx = -1; dx <= 1; ++dx) {
			for (int dy = -1; dy <= 1; ++dy) {
				auto it = porInicio.find( { c.first + dx, c.second + dy });
				if (it == porInicio.end()) {
					continue;
				}
				for (size_t idx : it->second) {
					if (idx == k || ant[idx] != -1) {
						continue;
					}
					const double dd = d2(piezas[idx].a, piezas[k].b);
					if (dd <= mejorD) {
						mejorD = dd;
						best = (long) idx;
					}
				}
			}
		}
		if (best >= 0) {
			sig[k] = best;
			ant[best] = (long) k;
		}
	}

	std::vector<char> visto(N, 0);
	auto seguir = [&](size_t k) {
		Poligono c;
		c.push_back(piezas[k].a);
		bool cerrado = false;
		while (true) {
			visto[k] = 1;
			c.push_back(piezas[k].b);
			const long s = sig[k];
			if (s < 0) {
				break;
			}
			if (visto[s]) {
				cerrado = true;
				break;
			}
			k = (size_t) s;
		}
		if (cerrado) {
			c.back() = c.front();
		}
		if (c.size() >= 2) {
			caminos.push_back(std::move(c));
		}
	};
	for (size_t k = 0; k < N; ++k) {   // primero los abiertos (cortados por zonas prohibidas)
		if (!visto[k] && ant[k] < 0) {
			seguir(k);
		}
	}
	for (size_t k = 0; k < N; ++k) {   // el resto son lazos cerrados
		if (!visto[k]) {
			seguir(k);
		}
	}
	return caminos;
}

// ─────────────────────────────────────────────────────────────
// construirGrupos: primitivas -> figuras -> pistas (Union-Find)
// ─────────────────────────────────────────────────────────────

void construirGrupos(const gerber::GerberResult &res, std::vector<std::vector<Info>> &gruposInfo, double tolConexion, double radioHerramienta) {
	const double tol2 = tolConexion * tolConexion * 4.0;
	std::vector<Info> infos;

	// Crear geometrías: cada primitiva Dark se convierte en cápsula(s) o contorno
	for (size_t i = 0; i < res.primitives.size(); ++i) {
		const auto &p = res.primitives[i];
		if (p.polaridad != gerber::Polarity::Dark) {
			continue;
		}

		if (p.type == gerber::PrimType::Segment) {
			const auto &s = p.asSegment();
			double w = s.width > 0 ? s.width : 0.08;
			Poligono cap = segmentoACapsula(s, w);
			if (cap.size() >= 3) {
				infos.push_back( { std::move(cap), (int) i, true, false });
			}
		} else if (p.type == gerber::PrimType::Arc) {
			// El arco se trocea en cápsulas, una por tramo de su polilínea:
			// así las pistas curvas se tratan igual que las rectas
			const auto &a = p.asArc();
			double w = a.width > 0 ? a.width : 0.08;
			auto pts = gerber::geom::arcToPolyline(a.center, a.radius, a.startAngle, a.endAngle, a.ccw, std::max(0.005, w * 0.05));
			for (size_t k = 0; k + 1 < pts.size(); ++k) {
				gerber::Segment s { pts[k], pts[k + 1], w };
				auto cap = segmentoACapsula(s, w);
				if (cap.size() >= 3) {
					infos.push_back( { std::move(cap), (int) i, true, false });
				}
			}
		} else if (p.type == gerber::PrimType::Flash) {
			auto poly = gerber::geom::flashToPolygon(p.asFlash(), 120);
			if (poly.size() >= 3) {
				infos.push_back( { std::move(poly), (int) i, true, false });
			}
		} else if (p.type == gerber::PrimType::Region) {
			auto poly = p.asRegion().outline;
			if (poly.size() >= 3) {
				const double umbral = radioHerramienta * radioHerramienta * 300.0;
				if (areaPoligono(poly) > umbral) {
					infos.push_back( { std::move(poly), (int) i, false, true });   // isla: solo referencia
				} else {
					infos.push_back( { std::move(poly), (int) i, true, false });
				}
			}
		}
	}

	// Union-Find: dos figuras se funden si algún par de vértices
	// (muestreados cada 2) está a menos de 2*tolConexion
	const int m = (int) infos.size();
	if (m == 0) {
		return;
	}

	std::vector<int> parent(m);
	for (int i = 0; i < m; ++i) {
		parent[i] = i;
	}

	auto find = [&](auto &self, int x) -> int {
		if (parent[x] != x) {
			parent[x] = self(self, parent[x]);
		}
		return parent[x];
	};

	for (int i = 0; i < m; ++i) {
		for (int j = i + 1; j < m; ++j) {
			bool conectados = false;
			for (size_t a = 0; a < infos[i].poly.size() && !conectados; a += 2) {
				for (size_t b = 0; b < infos[j].poly.size() && !conectados; b += 2) {
					if (d2(infos[i].poly[a], infos[j].poly[b]) <= tol2) {
						conectados = true;
					}
				}
			}
			if (conectados) {
				parent[find(find, i)] = find(find, j);
			}
		}
	}

	// Grupos finales (solo los que contienen alguna figura activa)
	std::map<int, std::vector<int>> mapRoot;
	for (int i = 0; i < m; ++i) {
		mapRoot[find(find, i)].push_back(i);
	}

	for (auto &par : mapRoot) {
		std::vector<Info> grupo;
		bool tieneActivo = false;
		for (int idx : par.second) {
			grupo.push_back(infos[idx]);
			if (infos[idx].esActivo) {
				tieneActivo = true;
			}
		}
		if (tieneActivo) {
			gruposInfo.push_back(std::move(grupo));
		}
	}
}

// ─────────────────────────────────────────────────────────────
// Figuras de cobre
//   Cada figura es una cápsula (pistas, arcos, pads redondos u
//   oblongos) o un polígono con anillos (pads, regiones/masas, con sus
//   huecos). Todas saben calcular la DISTANCIA CON SIGNO a su cobre.
//   El contorno de fresado a distancia d es el lugar donde la distancia
//   mínima al cobre de todo el grupo vale exactamente d: se genera la
//   curva de offset "cruda" de cada figura, se trocea en todos sus
//   cruces y se conservan solo los trozos válidos según esa distancia.
//   Así funciona igual para pistas, pads, masas con agujeros e islas.
// ─────────────────────────────────────────────────────────────

struct Figura {
	bool capsula = false;
	gerber::Vec2 a, b;                 // eje de la cápsula
	double r = 0.0;                    // radio base de la cápsula
	std::vector<Poligono> anillos;     // [0] exterior CCW; el resto, huecos CW (en cápsulas: contorno base)
	Caja caja = Caja { 0, 0, 0, 0 };   // caja del cobre
	bool complemento = false;          // true: la distancia se mide al revés (positiva DENTRO del cobre)
};

static Poligono sinDuplicados(const Poligono &P) {
	Poligono Q;
	for (const auto &v : P) {
		if (Q.empty() || d2(Q.back(), v) > 1e-18) {
			Q.push_back(v);
		}
	}
	while (Q.size() > 1 && d2(Q.front(), Q.back()) <= 1e-18) {
		Q.pop_back();
	}
	return Q;
}

static double dist2PuntoSeg(const gerber::Vec2 &p, const gerber::Vec2 &a, const gerber::Vec2 &b) {
	const double vx = b.x - a.x, vy = b.y - a.y;
	const double l2 = vx * vx + vy * vy;
	double t = (l2 > 1e-24) ? ((p.x - a.x) * vx + (p.y - a.y) * vy) / l2 : 0.0;
	t = std::clamp(t, 0.0, 1.0);
	const double ex = p.x - (a.x + vx * t), ey = p.y - (a.y + vy * t);
	return ex * ex + ey * ey;
}

// Distancia con signo al cobre de la figura: negativa dentro, positiva fuera
static double sdFigura(const Figura &f, const gerber::Vec2 &p) {
	if (f.capsula) {
		return std::sqrt(dist2PuntoSeg(p, f.a, f.b)) - f.r;
	}
	double mejor = 1e300;
	bool dentro = false;
	for (const auto &anillo : f.anillos) {
		const size_t n = anillo.size();
		for (size_t i = 0, j = n - 1; i < n; j = i++) {
			const gerber::Vec2 &vi = anillo[i], &vj = anillo[j];
			mejor = std::min(mejor, dist2PuntoSeg(p, vj, vi));
			if (((vi.y > p.y) != (vj.y > p.y)) && (p.x < (vj.x - vi.x) * (p.y - vi.y) / (vj.y - vi.y) + vi.x)) {
				dentro = !dentro;
			}
		}
	}
	const double d = std::sqrt(mejor);
	const double sd = dentro ? -d : d;
	return f.complemento ? -sd : sd;
}

static Caja expandirCaja(Caja c, double m) {
	c.x0 -= m;
	c.y0 -= m;
	c.x1 += m;
	c.y1 += m;
	return c;
}

static void calcularCaja(Figura &f) {
	Caja c { 1e300, 1e300, -1e300, -1e300 };
	for (const auto &anillo : f.anillos) {
		const Caja k = cajaDe(anillo);
		c.x0 = std::min(c.x0, k.x0);
		c.y0 = std::min(c.y0, k.y0);
		c.x1 = std::max(c.x1, k.x1);
		c.y1 = std::max(c.y1, k.y1);
	}
	f.caja = c;
}

// Curva de offset exterior "cruda" (d > 0) de un anillo orientado con el cobre a
// su izquierda: esquinas convexas en arco sobre la rejilla angular global; en las
// cóncavas se dejan los dos offsets de las aristas y ya se cruzarán entre sí.
static Poligono offsetCrudo(const Poligono &P, double d) {
	const Poligono Q = sinDuplicados(P);
	const size_t n = Q.size();
	Poligono out;
	if (n < 3) {
		return out;
	}
	const double PI = std::acos(-1.0), paso = PI / PASOS_TAPA;
	std::vector<gerber::Vec2> dir(n), nrm(n);
	for (size_t i = 0; i < n; ++i) {
		const gerber::Vec2 &p = Q[i], &q = Q[(i + 1) % n];
		const double dx = q.x - p.x, dy = q.y - p.y;
		const double l = std::sqrt(dx * dx + dy * dy);
		dir[i] = { dx / l, dy / l };
		nrm[i] = { dir[i].y, -dir[i].x };   // a la derecha: hacia el exterior del cobre
	}
	out.reserve(n * 2);
	for (size_t i = 0; i < n; ++i) {
		const size_t ip = (i + n - 1) % n;
		const gerber::Vec2 &e1 = dir[ip], &e2 = dir[i];
		const gerber::Vec2 &n1 = nrm[ip], &n2 = nrm[i];
		const double cruz = e1.x * e2.y - e1.y * e2.x;
		if (cruz > 1e-9) {
			const double a0 = std::atan2(n1.y, n1.x);
			double a1 = std::atan2(n2.y, n2.x);
			if (a1 < a0) {
				a1 += 2.0 * PI;
			}
			anadirArcoGrid(out, Q[i], d, a0, a1, paso);
		} else {
			const gerber::Vec2 p1 { Q[i].x + n1.x * d, Q[i].y + n1.y * d };
			const gerber::Vec2 p2 { Q[i].x + n2.x * d, Q[i].y + n2.y * d };
			out.push_back(p1);
			if (d2(p1, p2) > 1e-18) {
				out.push_back(p2);
			}
		}
	}
	return out;
}

static Figura nuevaCapsula(const gerber::Vec2 &a, const gerber::Vec2 &b, double r) {
	Figura f;
	f.capsula = true;
	f.a = a;
	f.b = b;
	f.r = std::max(r, 0.01);
	const gerber::Segment s { a, b, 2.0 * f.r };
	f.anillos.push_back(segmentoACapsula(s, s.width));
	calcularCaja(f);
	return f;
}

static Figura nuevoPoligono(Poligono poly) {
	Figura f;
	asegurarCCW(poly);
	f.anillos.push_back(std::move(poly));
	calcularCaja(f);
	return f;
}

// Pads: círculos y oblongos son cápsulas exactas; el resto, polígono
static bool flashAFigura(const gerber::Flash &fl, Figura &out) {
	const gerber::Aperture &ap = fl.aperture;
	const gerber::Vec2 c { fl.pos.x + ap.centerX, fl.pos.y + ap.centerY };
	if (ap.type == gerber::ApertureType::Circle && ap.outer > 0.0) {
		out = nuevaCapsula(c, c, ap.outer * 0.5);
		return true;
	}
	if (ap.type == gerber::ApertureType::Oblong && ap.outer > 0.0) {
		const double W = ap.outer, H = (ap.height > 1e-9 ? ap.height : ap.outer);
		if (W >= H) {
			const double dx = (W - H) * 0.5;
			out = nuevaCapsula( { c.x - dx, c.y }, { c.x + dx, c.y }, H * 0.5);
		} else {
			const double dy = (H - W) * 0.5;
			out = nuevaCapsula( { c.x, c.y - dy }, { c.x, c.y + dy }, W * 0.5);
		}
		return true;
	}
	Poligono poly = sinDuplicados(gerber::geom::flashToPolygon(fl, 48));
	if (poly.size() < 3) {
		return false;
	}
	out = nuevoPoligono(std::move(poly));
	return true;
}

// Curvas crudas de offset de la figura a distancia d
static void rawDeFigura(const Figura &f, double d, std::vector<Poligono> &out) {
	if (f.capsula) {
		const gerber::Segment s { f.a, f.b, 2.0 * (f.r + d) };
		out.push_back(segmentoACapsula(s, s.width));
		return;
	}
	for (const auto &anillo : f.anillos) {
		Poligono o = offsetCrudo(anillo, d);
		if (o.size() >= 3) {
			out.push_back(std::move(o));
		}
	}
}

// Primitivas -> figuras -> pistas/islas (Union-Find)
static std::vector<std::vector<Figura>> agruparFiguras(const gerber::GerberResult &res, double tolConexion, bool soloIslas = false) {
	std::vector<Figura> figs;
	std::vector<size_t> regiones;   // índices de figuras polígono (para recibir huecos Clear)

	// Un polígono Clear dentro de una región Dark anterior es un hueco de esa región
	auto anadirHueco = [&](Poligono h) {
		h = sinDuplicados(h);
		if (h.size() < 3) {
			return;
		}
		asegurarCCW(h);
		std::reverse(h.begin(), h.end());   // hueco: sentido horario
		gerber::Vec2 c { 0, 0 };
		for (const auto &v : h) {
			c.x += v.x;
			c.y += v.y;
		}
		c.x /= h.size();
		c.y /= h.size();
		for (size_t idx : regiones) {
			if (sdFigura(figs[idx], c) < 0.0) {
				figs[idx].anillos.push_back(std::move(h));
				return;
			}
		}
	};

	for (const auto &p : res.primitives) {
		if (p.polaridad == gerber::Polarity::Clear) {
			if (p.type == gerber::PrimType::Flash) {
				anadirHueco(gerber::geom::flashToPolygon(p.asFlash(), 48));
			} else if (p.type == gerber::PrimType::Region) {
				anadirHueco(p.asRegion().outline);
			}
			continue;
		}
		if (soloIslas && (p.type == gerber::PrimType::Segment || p.type == gerber::PrimType::Arc)) {
			continue;   // las pistas no son islas
		}
		if (p.type == gerber::PrimType::Segment) {
			const auto &s = p.asSegment();
			figs.push_back(nuevaCapsula(s.start, s.end, (s.width > 0 ? s.width : 0.08) * 0.5));
		} else if (p.type == gerber::PrimType::Arc) {
			const auto &a = p.asArc();
			const double w = a.width > 0 ? a.width : 0.08;
			auto pts = gerber::geom::arcToPolyline(a.center, a.radius, a.startAngle, a.endAngle, a.ccw, std::max(0.005, w * 0.05));
			for (size_t k = 0; k + 1 < pts.size(); ++k) {
				figs.push_back(nuevaCapsula(pts[k], pts[k + 1], w * 0.5));
			}
		} else if (p.type == gerber::PrimType::Flash) {
			Figura f;
			if (flashAFigura(p.asFlash(), f)) {
				figs.push_back(std::move(f));
			}
		} else if (p.type == gerber::PrimType::Region) {
			Poligono poly = sinDuplicados(p.asRegion().outline);
			if (poly.size() >= 3) {
				Figura f = nuevoPoligono(std::move(poly));
				for (const auto &h : p.asRegion().holes) {
					Poligono hh = sinDuplicados(h);
					if (hh.size() >= 3) {
						asegurarCCW(hh);
						std::reverse(hh.begin(), hh.end());
						f.anillos.push_back(std::move(hh));
					}
				}
				figs.push_back(std::move(f));
				regiones.push_back(figs.size() - 1);
			}
		}
	}

	const size_t m = figs.size();
	std::vector<std::vector<Figura>> grupos;
	if (m == 0) {
		return grupos;
	}

	const double tolDist = 2.0 * tolConexion, tol2 = tolDist * tolDist;
	std::vector<Caja> cajas(m);
	for (size_t i = 0; i < m; ++i) {
		calcularCaja(figs[i]);
		cajas[i] = expandirCaja(figs[i].caja, tolDist);
	}

	auto conectadas = [&](size_t i, size_t j) {
		const Figura &FA = figs[i], &FB = figs[j];
		for (const auto &A : FA.anillos) {
			for (const auto &B : FB.anillos) {
				for (const auto &va : A) {
					for (const auto &vb : B) {
						if (d2(va, vb) <= tol2) {
							return true;
						}
					}
				}
				for (size_t k = 0, ka = A.size(); k < ka; ++k) {          // bordes que se cruzan
					const gerber::Vec2 &a0 = A[k], &a1 = A[(k + 1) % ka];
					for (size_t l = 0, lb = B.size(); l < lb; ++l) {
						double t;
						if (interseccionSegmentos(a0, a1, B[l], B[(l + 1) % lb], t)) {
							return true;
						}
					}
				}
			}
		}
		return sdFigura(FB, FA.anillos[0][0]) < 0.0 || sdFigura(FA, FB.anillos[0][0]) < 0.0;   // uno dentro del otro
	};

	std::vector<size_t> parent(m);
	for (size_t i = 0; i < m; ++i) {
		parent[i] = i;
	}
	auto find = [&](size_t x) {
		while (parent[x] != x) {
			parent[x] = parent[parent[x]];
			x = parent[x];
		}
		return x;
	};
	for (size_t i = 0; i < m; ++i) {
		for (size_t j = i + 1; j < m; ++j) {
			if (find(i) == find(j) || !cajasSolapan(cajas[i], cajas[j])) {
				continue;
			}
			if (conectadas(i, j)) {
				parent[find(i)] = find(j);
			}
		}
	}

	std::map<size_t, std::vector<size_t>> porRaiz;
	for (size_t i = 0; i < m; ++i) {
		porRaiz[find(i)].push_back(i);
	}
	for (auto &par : porRaiz) {
		std::vector<Figura> g;
		for (size_t idx : par.second) {
			g.push_back(std::move(figs[idx]));
		}
		grupos.push_back(std::move(g));
	}
	return grupos;
}

// ─────────────────────────────────────────────────────────────
// Frontera de la unión por distancia
//   raw: curvas crudas de offset de las figuras del grupo (a distancia d)
//   figs: figuras del grupo (RawForma::fig indexa aquí)
//   prohFig/prohRaw: cobre de otras pistas (distancia mínima dProh) y su
//   curva cruda, esta última solo para trocear
// ─────────────────────────────────────────────────────────────

struct ProhFig {
	const Figura *f;
	const std::vector<Poligono> *zona;   // curva cruda de la figura a distancia base
};

// ¿p invade el cobre de otra pista (a menos de dProh)?  Las cápsulas usan su
// polígono de zona (coherente con los cortes); el resto, la distancia exacta.
static bool dentroZona(const ProhFig &z, const gerber::Vec2 &p, double dProh, double eps) {
	if (z.f->capsula) {
		return pip(p, (*z.zona)[0]);
	}
	return sdFigura(*z.f, p) < dProh - eps;
}

struct RawForma {
	Poligono p;
	size_t fig;      // índice de la figura dentro del grupo
	bool convexo;    // cápsula: no necesita autointersecciones
};

static std::vector<Poligono> fronteraUnionSD(const std::vector<RawForma> &raw, const std::vector<const Figura*> &figs, double d, double eps, const std::vector<ProhFig> &proh, double dProh) {
	std::vector<const Poligono*> prohRaw;
	for (const auto &z : proh) {
		for (const auto &p : *z.zona) {
			prohRaw.push_back(&p);
		}
	}
	std::vector<size_t> capRaw(figs.size(), (size_t) -1);   // polígono crudo de cada cápsula
	for (size_t i = 0; i < raw.size(); ++i) {
		if (raw[i].convexo) {
			capRaw[raw[i].fig] = i;
		}
	}
	struct Pieza {
		gerber::Vec2 a, b;
	};
	std::vector<Pieza> piezas;

	const size_t R = raw.size();
	std::vector<Caja> cr(R), cp(prohRaw.size()), cfg(figs.size()), cpf(proh.size());
	for (size_t i = 0; i < R; ++i) {
		cr[i] = expandirCaja(cajaDe(raw[i].p), 1e-6);
	}
	for (size_t k = 0; k < prohRaw.size(); ++k) {
		cp[k] = cajaDe(*prohRaw[k]);
	}
	for (size_t g = 0; g < figs.size(); ++g) {
		cfg[g] = expandirCaja(figs[g]->caja, d + eps);
	}
	for (size_t k = 0; k < proh.size(); ++k) {
		cpf[k] = expandirCaja(proh[k].f->caja, dProh + eps);
	}

	auto cortes = [](const gerber::Vec2 &a, const gerber::Vec2 &b, const Caja &ce, const Poligono &q, std::vector<double> &ts) {
		for (size_t k = 0, m = q.size(); k < m; ++k) {
			const gerber::Vec2 &c = q[k], &e = q[(k + 1) % m];
			if (std::max(c.x, e.x) < ce.x0 || std::min(c.x, e.x) > ce.x1 || std::max(c.y, e.y) < ce.y0 || std::min(c.y, e.y) > ce.y1) {
				continue;
			}
			double t;
			if (interseccionSegmentos(a, b, c, e, t)) {
				ts.push_back(t);
			}
		}
	};

	// ¿El punto m (de la curva de la figura fi) está en la frontera de la unión?
	auto valida = [&](const gerber::Vec2 &m, size_t fi) -> bool {
		for (size_t g = 0; g < figs.size(); ++g) {
			if (!puntoEnCaja(cfg[g], m)) {
				continue;
			}
			if (figs[g]->capsula) {
				if (g == fi) {
					continue;
				}
				const Poligono &Q = raw[capRaw[g]].p;       // cápsula: se compara con su polígono
				if (!puntoEnCaja(cr[capRaw[g]], m)) {
					continue;
				}
				if (dist2Borde(m, Q) <= 1e-12) {
					if (g < fi) {
						return false;                       // arista coincidente: manda la de menor índice
					}
				} else if (pip(m, Q)) {
					return false;
				}
			} else if (sdFigura(*figs[g], m) < d - eps) {
				return false;                               // polígono/masa: distancia exacta
			}
		}
		for (size_t k = 0; k < proh.size(); ++k) {
			if (puntoEnCaja(cpf[k], m) && dentroZona(proh[k], m, dProh, eps)) {
				return false;                               // invade el cobre de otra pista
			}
		}
		return true;
	};

	// 1) Trocear cada borde por todos sus cruces y quedarse con los trozos válidos
	for (size_t i = 0; i < R; ++i) {
		const Poligono &P = raw[i].p;
		const size_t n = P.size();
		for (size_t e = 0; e < n; ++e) {
			const gerber::Vec2 a = P[e], b = P[(e + 1) % n];
			if (d2(a, b) < 1e-18) {
				continue;
			}
			const Caja ce { std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y) };

			std::vector<double> ts { 0.0, 1.0 };
			for (size_t j = 0; j < R; ++j) {
				if (j != i && cajasSolapan(ce, cr[j])) {
					cortes(a, b, ce, raw[j].p, ts);
				}
			}
			if (!raw[i].convexo) {                   // autointersecciones (cóncavos, huecos, cortes finos)
				for (size_t k = 0; k < n; ++k) {
					if (k == e || k == (e + 1) % n || (k + 1) % n == e) {
						continue;
					}
					const gerber::Vec2 &c = P[k], &f = P[(k + 1) % n];
					if (std::max(c.x, f.x) < ce.x0 || std::min(c.x, f.x) > ce.x1 || std::max(c.y, f.y) < ce.y0 || std::min(c.y, f.y) > ce.y1) {
						continue;
					}
					double t;
					if (interseccionSegmentos(a, b, c, f, t)) {
						ts.push_back(t);
					}
				}
			}
			for (size_t k = 0; k < prohRaw.size(); ++k) {
				if (cajasSolapan(ce, cp[k])) {
					cortes(a, b, ce, *prohRaw[k], ts);
				}
			}
			std::sort(ts.begin(), ts.end());

			auto punto = [&](double t) -> gerber::Vec2 {
				if (t <= 0.0) {
					return a;
				}
				if (t >= 1.0) {
					return b;
				}
				return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
			};

			for (size_t s = 0; s + 1 < ts.size(); ++s) {
				if (ts[s + 1] - ts[s] < 1e-9) {
					continue;
				}
				const gerber::Vec2 p0 = punto(ts[s]), p1 = punto(ts[s + 1]);
				const gerber::Vec2 mid { (p0.x + p1.x) * 0.5, (p0.y + p1.y) * 0.5 };
				if (valida(mid, raw[i].fig)) {
					piezas.push_back( { p0, p1 });
				}
			}
		}
	}

	// 2) Encadenar piezas (fin de una == inicio de otra)
	const size_t N = piezas.size();
	std::vector<Poligono> caminos;
	if (N == 0) {
		return caminos;
	}
	const double tol = 1e-6, celda = 1e-5;
	using Clave = std::pair<long long, long long>;
	auto clave = [&](const gerber::Vec2 &p) {
		return Clave { llround(p.x / celda), llround(p.y / celda) };
	};
	std::map<Clave, std::vector<size_t>> porInicio;
	for (size_t k = 0; k < N; ++k) {
		porInicio[clave(piezas[k].a)].push_back(k);
	}

	std::vector<long> sig(N, -1), ant(N, -1);
	for (size_t k = 0; k < N; ++k) {
		const Clave c = clave(piezas[k].b);
		double mejorD = tol * tol;
		long best = -1;
		for (int dx = -1; dx <= 1; ++dx) {
			for (int dy = -1; dy <= 1; ++dy) {
				auto it = porInicio.find( { c.first + dx, c.second + dy });
				if (it == porInicio.end()) {
					continue;
				}
				for (size_t idx : it->second) {
					if (idx == k || ant[idx] != -1) {
						continue;
					}
					const double dd = d2(piezas[idx].a, piezas[k].b);
					if (dd <= mejorD) {
						mejorD = dd;
						best = (long) idx;
					}
				}
			}
		}
		if (best >= 0) {
			sig[k] = best;
			ant[best] = (long) k;
		}
	}

	std::vector<char> visto(N, 0);
	auto seguir = [&](size_t k) {
		Poligono c;
		c.push_back(piezas[k].a);
		bool cerrado = false;
		while (true) {
			visto[k] = 1;
			c.push_back(piezas[k].b);
			const long s = sig[k];
			if (s < 0) {
				break;
			}
			if (visto[s]) {
				cerrado = true;
				break;
			}
			k = (size_t) s;
		}
		if (cerrado) {
			c.back() = c.front();
		}
		if (c.size() >= 2) {
			caminos.push_back(std::move(c));
		}
	};
	for (size_t k = 0; k < N; ++k) {   // primero los abiertos
		if (!visto[k] && ant[k] < 0) {
			seguir(k);
		}
	}
	for (size_t k = 0; k < N; ++k) {   // el resto son lazos cerrados
		if (!visto[k]) {
			seguir(k);
		}
	}
	return caminos;
}

// Une extremo->inicio de caminos abiertos separados por menos de 'tol'
// (huecos numéricos) si el puente no invade el cobre de otras pistas.
static void soldarCaminos(std::vector<Poligono> &caminos, double tol, const std::vector<ProhFig> &proh, double dProh, double eps) {
	const double tol2 = tol * tol;
	auto cerrado = [](const Poligono &p) {
		return p.size() > 3 && d2(p.front(), p.back()) < 1e-12;
	};
	auto puenteLibre = [&](const gerber::Vec2 &a, const gerber::Vec2 &b) {
		for (double t : { 0.25, 0.5, 0.75 }) {
			const gerber::Vec2 m { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
			for (const auto &z : proh) {
				if (dentroZona(z, m, dProh, eps)) {
					return false;
				}
			}
		}
		return true;
	};

	bool cambio = true;
	while (cambio) {
		cambio = false;
		for (size_t i = 0; i < caminos.size() && !cambio; ++i) {
			if (cerrado(caminos[i])) {
				continue;
			}
			size_t mejor = caminos.size();
			double mejorD = tol2;
			for (size_t j = 0; j < caminos.size(); ++j) {
				if (cerrado(caminos[j]) || (j == i && caminos[i].size() < 3)) {
					continue;
				}
				const double dd = d2(caminos[i].back(), caminos[j].front());
				if (dd <= mejorD && puenteLibre(caminos[i].back(), caminos[j].front())) {
					mejorD = dd;
					mejor = j;
				}
			}
			if (mejor == caminos.size()) {
				continue;
			}
			if (mejor == i) {
				const gerber::Vec2 f = caminos[i].front();
				caminos[i].push_back(f);
			} else {
				Poligono &A = caminos[i];
				const Poligono B = caminos[mejor];
				A.insert(A.end(), B.begin() + (mejorD < 1e-12 ? 1 : 0), B.end());
				caminos.erase(caminos.begin() + mejor);
			}
			cambio = true;
		}
	}
}

// ─────────────────────────────────────────────────────────────
// Ordena y emite los caminos: siguiente tramo más cercano entre todos; los saltos
// cortos se hacen con la fresa abajo si 'puenteSeguro' lo permite.
// ─────────────────────────────────────────────────────────────
struct Tramo {
	Poligono p;
	bool cerrado;
	Caja caja;
};

static void rutaTramos(std::vector<Tramo> &tramos, std::vector<MovimientoHerramienta> &movimientos, gerber::Vec2 &actual, bool &abajo, double distPuente,
		const std::function<bool(const gerber::Vec2&, const gerber::Vec2&)> &puenteSeguro) {
	auto distCaja2 = [](const Caja &c, const gerber::Vec2 &p) {
		const double dx = std::max( { c.x0 - p.x, 0.0, p.x - c.x1 });
		const double dy = std::max( { c.y0 - p.y, 0.0, p.y - c.y1 });
		return dx * dx + dy * dy;
	};
	std::vector<char> hecho(tramos.size(), 0);
	for (size_t n = 0; n < tramos.size(); ++n) {
		size_t mejor = tramos.size(), mejorIdx = 0;
		bool invertir = false;
		double mejorD = 1e300;
		for (size_t t = 0; t < tramos.size(); ++t) {
			if (hecho[t] || distCaja2(tramos[t].caja, actual) >= mejorD) {
				continue;
			}
			const Poligono &p = tramos[t].p;
			if (tramos[t].cerrado) {
				for (size_t v = 0; v + 1 < p.size(); ++v) {
					const double dd = d2(actual, p[v]);
					if (dd < mejorD) {
						mejorD = dd;
						mejor = t;
						mejorIdx = v;
						invertir = false;
					}
				}
			} else {
				const double d0 = d2(actual, p.front()), d1 = d2(actual, p.back());
				if (d0 < mejorD) {
					mejorD = d0;
					mejor = t;
					invertir = false;
				}
				if (d1 < mejorD) {
					mejorD = d1;
					mejor = t;
					invertir = true;
				}
			}
		}
		if (mejor == tramos.size()) {
			break;
		}
		hecho[mejor] = 1;

		Poligono c = tramos[mejor].p;
		if (tramos[mejor].cerrado) {                 // lazo: se empieza en el vértice más cercano y ya vuelve solo a él
			c.pop_back();
			std::rotate(c.begin(), c.begin() + mejorIdx, c.end());
			c.push_back(c.front());
		} else if (invertir) {
			std::reverse(c.begin(), c.end());
		}

		// Llegada al inicio del tramo: puente corto con la fresa abajo, o subir-viajar-bajar
		const double dEntrada = std::sqrt(d2(actual, c.front()));
		if (dEntrada > 1e-3) {
			if (abajo && dEntrada <= distPuente && puenteSeguro(actual, c.front())) {
				movimientos.push_back( { TipoMovimiento::Corte, actual, c.front() });
			} else {
				if (abajo) {
					movimientos.push_back( { TipoMovimiento::Subir, actual, actual });
					abajo = false;
				}
				movimientos.push_back( { TipoMovimiento::Viaje, actual, c.front() });
			}
			actual = c.front();
		}
		if (!abajo) {
			movimientos.push_back( { TipoMovimiento::Bajar, actual, actual });
			abajo = true;
		}
		for (size_t k = 1; k < c.size(); ++k) {
			movimientos.push_back( { TipoMovimiento::Corte, actual, c[k] });
			actual = c[k];
		}
		// No se sube aquí: se decide en el tramo siguiente (o al terminar)
	}
}

// ─────────────────────────────────────────────────────────────
// construirTrayectoria (offset simple, modo contorno)
// ─────────────────────────────────────────────────────────────
std::vector<MovimientoHerramienta> construirTrayectoria(const gerber::GerberResult &res, const gerber::GCodeConfig &cfg, bool /*aislamiento*/, double huelgo) {
	std::vector<MovimientoHerramienta> movimientos;
	if (!res.ok) {
		return movimientos;
	}

	const double offsetBase = cfg.toolDiameter * 0.5 + huelgo;
	const double paso = cfg.toolDiameter * 0.8;
	const size_t maxPasadas = 1;
	gerber::Vec2 actual { 0, 0 };

	std::vector<std::vector<gerber::Vec2>> geometriasCobre;
	for (const auto &prim : res.primitives) {
		if (prim.polaridad != gerber::Polarity::Dark) {
			continue;
		}
		std::vector<gerber::Vec2> poli;
		if (prim.type == gerber::PrimType::Segment) {
			poli = segmentoAPoligono(prim.asSegment(), 20);
		} else if (prim.type == gerber::PrimType::Flash) {
			poli = gerber::geom::flashToPolygon(prim.asFlash(), 36);
		} else if (prim.type == gerber::PrimType::Region) {
			poli = prim.asRegion().outline;
		}
		if (poli.size() >= 3) {
			geometriasCobre.push_back(poli);
		}
	}
	if (geometriasCobre.empty()) {
		return movimientos;
	}

	auto esZonaProtegida = [&](gerber::Vec2 p) -> bool {
		for (const auto &poli : geometriasCobre) {
			if (pip(p, poli)) {
				return true;
			}
		}
		return false;
	};

	bool herramientaAbajo = false;
	auto cortarPoligonoSeguro = [&](const std::vector<gerber::Vec2> &poli) {
		if (poli.size() < 3) {
			return;
		}
		movimientos.push_back( { TipoMovimiento::Viaje, actual, poli[0] });
		actual = poli[0];
		for (size_t i = 0; i < poli.size(); ++i) {
			gerber::Vec2 sig = poli[(i + 1) % poli.size()];
			gerber::Vec2 medio = { (actual.x + sig.x) * 0.5, (actual.y + sig.y) * 0.5 };
			if (esZonaProtegida(medio) || esZonaProtegida(actual) || esZonaProtegida(sig)) {
				if (herramientaAbajo) {
					movimientos.push_back( { TipoMovimiento::Subir, actual, actual });
					herramientaAbajo = false;
				}
				movimientos.push_back( { TipoMovimiento::Viaje, actual, sig });
			} else {
				if (!herramientaAbajo) {
					movimientos.push_back( { TipoMovimiento::Bajar, actual, actual });
					herramientaAbajo = true;
				}
				movimientos.push_back( { TipoMovimiento::Corte, actual, sig });
			}
			actual = sig;
		}
		if (herramientaAbajo) {
			movimientos.push_back( { TipoMovimiento::Subir, actual, actual });
			herramientaAbajo = false;
		}
	};

	movimientos.push_back( { TipoMovimiento::Subir, actual, actual });
	for (size_t pasada = 0; pasada < maxPasadas; ++pasada) {
		const double offset = offsetBase + pasada * paso;
		for (const auto &poli : geometriasCobre) {
			auto poliOffset = gerber::geom::offsetPolygon(poli, offset);
			if (poliOffset.size() >= 3) {
				cortarPoligonoSeguro(poliOffset);
			}
		}
	}
	movimientos.push_back( { TipoMovimiento::Viaje, actual, { 0, 0 } });
	return movimientos;
}

// ─────────────────────────────────────────────────────────────
// construirTrayectoriaAislamiento
//   figuras (pistas, pads, masas, islas) -> curva de offset -> frontera de
//   la unión por distancia -> caminos continuos -> ruta de fresa
// ─────────────────────────────────────────────────────────────

std::vector<MovimientoHerramienta> construirTrayectoriaAislamiento(const gerber::GerberResult &res, const gerber::GCodeConfig &cfg, double huelgo, int maxPasadas) {
	std::vector<MovimientoHerramienta> movimientos;
	if (!res.ok) {
		return movimientos;
	}

	const double radioHerramienta = cfg.toolDiameter * 0.5;
	const double distBase = radioHerramienta + huelgo;
	const double pasoPasada = cfg.toolDiameter * 0.45;
	if (maxPasadas < 1) {
		maxPasadas = 1;
	}
	const double tolConexion = std::max(0.05, cfg.toolDiameter * 0.15);

	std::vector<std::vector<Figura>> grupos = agruparFiguras(res, tolConexion);
	if (grupos.empty()) {
		return movimientos;
	}
	const size_t G = grupos.size();

	// Tolerancia de comparación de distancias: cubre la flecha de las cuerdas de las tapas
	double rMax = 0.0;
	for (const auto &g : grupos) {
		for (const auto &f : g) {
			if (f.capsula) {
				rMax = std::max(rMax, f.r);
			}
		}
	}
	const double distMax = distBase + (maxPasadas - 1) * pasoPasada;
	const double eps = 1e-3 + 0.0025 * (rMax + distMax);

	// Todas las figuras con su curva cruda de zona prohibida (distancia base)
	struct FigG {
		const Figura *f;
		size_t g;
		std::vector<Poligono> zonaRaw;
	};
	std::vector<FigG> todas;
	for (size_t g = 0; g < G; ++g) {
		for (const auto &f : grupos[g]) {
			FigG fg { &f, g, { } };
			rawDeFigura(f, distBase, fg.zonaRaw);
			todas.push_back(std::move(fg));
		}
	}

	const gerber::Vec2 origen { 0, 0 };
	gerber::Vec2 actual = origen;
	bool abajo = false;

	std::vector<Tramo> tramos;

	auto esCerrado = [](const Poligono &p) {
		return p.size() > 3 && d2(p.front(), p.back()) < 1e-12;
	};
	auto longitud = [](const Poligono &p) {
		double l = 0.0;
		for (size_t k = 1; k < p.size(); ++k) {
			l += std::sqrt(d2(p[k - 1], p[k]));
		}
		return l;
	};

	for (size_t g = 0; g < G; ++g) {
		std::vector<const Figura*> figs;
		Caja cg { 1e300, 1e300, -1e300, -1e300 };
		for (const auto &f : grupos[g]) {
			figs.push_back(&f);
			cg.x0 = std::min(cg.x0, f.caja.x0);
			cg.y0 = std::min(cg.y0, f.caja.y0);
			cg.x1 = std::max(cg.x1, f.caja.x1);
			cg.y1 = std::max(cg.y1, f.caja.y1);
		}
		cg = expandirCaja(cg, distMax + 1.0);

		// Cobre de OTRAS pistas que puede afectar a esta
		std::vector<ProhFig> proh;
		for (const auto &fg : todas) {
			if (fg.g != g && cajasSolapan(expandirCaja(fg.f->caja, distBase), cg) && !fg.zonaRaw.empty()) {
				proh.push_back( { fg.f, &fg.zonaRaw });
			}
		}

		for (int pasada = 0; pasada < maxPasadas; ++pasada) {
			const double dist = distBase + pasada * pasoPasada;

			// 1) Curvas crudas de offset de las figuras de la pista
			std::vector<RawForma> raw;
			for (size_t i = 0; i < figs.size(); ++i) {
				std::vector<Poligono> polys;
				rawDeFigura(*figs[i], dist, polys);
				for (auto &p : polys) {
					raw.push_back( { std::move(p), i, figs[i]->capsula });
				}
			}
			if (raw.empty()) {
				continue;
			}

			// 2) Frontera de la unión -> caminos continuos
			std::vector<Poligono> caminos = fronteraUnionSD(raw, figs, dist, eps, proh, distBase);
			soldarCaminos(caminos, std::max(0.01, cfg.toolDiameter * 0.2), proh, distBase, eps);
			caminos.erase(std::remove_if(caminos.begin(), caminos.end(), [&](const Poligono &p) {
				return longitud(p) < std::max(0.02, 0.5 * cfg.toolDiameter);
			}), caminos.end());

			// 3) Se guardan los caminos; el orden de fresado se decide al final, globalmente
			for (auto &c : caminos) {
				Tramo t;
				t.cerrado = esCerrado(c);
				t.caja = cajaDe(c);
				t.p = std::move(c);
				tramos.push_back(std::move(t));
			}
		}
	}

	// ── Orden global de fresado (compartido con los perfilados) ──
	// Un puente con la fresa abajo solo es seguro si no pisa la zona de ningún cobre
	auto puenteSeguro = [&](const gerber::Vec2 &a, const gerber::Vec2 &b) {
		const Caja cb = expandirCaja(Caja { std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y) }, distBase + eps);
		const int n = std::max(1, (int) std::ceil(std::sqrt(d2(a, b)) / 0.05));
		for (const auto &fg : todas) {
			if (fg.zonaRaw.empty() || !cajasSolapan(cb, expandirCaja(fg.f->caja, 0.0))) {
				continue;
			}
			const ProhFig z { fg.f, &fg.zonaRaw };
			for (int i = 0; i <= n; ++i) {
				const double t = (double) i / n;
				const gerber::Vec2 p { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
				if (dentroZona(z, p, distBase, eps)) {
					return false;
				}
			}
		}
		return true;
	};

	rutaTramos(tramos, movimientos, actual, abajo, std::max(2.0 * cfg.toolDiameter, 0.2), puenteSeguro);

	if (abajo) {
		movimientos.push_back( { TipoMovimiento::Subir, actual, actual });
	}
	if (d2(actual, origen) > 1e-6) {
		movimientos.push_back( { TipoMovimiento::Viaje, actual, origen });
	}
	return movimientos;
}

// ─────────────────────────────────────────────────────────────
// construirTrayectoriaPerfilado
//   Pasadas concéntricas HACIA DENTRO del cobre (pistas, pads, masas, islas).
//   1) contorno de la unión de las figuras de cada grupo (con sus huecos)
//   2) offset interior de ese contorno, recortado por distancia: nunca se
//      sale del cobre ni cruza entre figuras solapadas
//   soloIslas: solo pads y regiones (el cobre que no es pista)
// ─────────────────────────────────────────────────────────────

std::vector<MovimientoHerramienta> construirTrayectoriaPerfilado(const gerber::GerberResult &res, const gerber::GCodeConfig &cfg, double huelgo, int maxPasadas, bool soloIslas) {
	std::vector<MovimientoHerramienta> movimientos;
	if (!res.ok) {
		return movimientos;
	}
	const double e0 = cfg.toolDiameter * 0.5 + huelgo;
	const double paso = std::max(cfg.toolDiameter * 0.35, 1e-3);
	if (maxPasadas < 1) {
		maxPasadas = 1;
	}

	std::vector<std::vector<Figura>> grupos = agruparFiguras(res, std::max(0.05, cfg.toolDiameter * 0.15), soloIslas);
	if (grupos.empty()) {
		return movimientos;
	}
	double rMax = 0.0;
	for (const auto &g : grupos) {
		for (const auto &f : g) {
			if (f.capsula) {
				rMax = std::max(rMax, f.r);
			}
		}
	}
	const double eps = 1e-3 + 0.0025 * (rMax + e0 + maxPasadas * paso);

	auto esCerrado = [](const Poligono &p) {
		return p.size() > 3 && d2(p.front(), p.back()) < 1e-12;
	};
	auto longitud = [](const Poligono &p) {
		double l = 0.0;
		for (size_t k = 1; k < p.size(); ++k) {
			l += std::sqrt(d2(p[k - 1], p[k]));
		}
		return l;
	};

	std::vector<Tramo> tramos;
	std::vector<Figura> cobre;            // un "cobre unido" por grupo (para validar puentes)
	cobre.reserve(grupos.size());

	for (const auto &g : grupos) {
		// 1) Contorno de la unión: lazos cerrados (exterior CCW, huecos CW)
		std::vector<const Figura*> figs;
		std::vector<RawForma> raw0;
		for (size_t i = 0; i < g.size(); ++i) {
			figs.push_back(&g[i]);
			std::vector<Poligono> polys;
			rawDeFigura(g[i], 0.0, polys);
			for (auto &p : polys) {
				raw0.push_back( { std::move(p), i, g[i].capsula });
			}
		}
		const std::vector<Poligono> lazos = fronteraUnionSD(raw0, figs, 0.0, eps, { }, 0.0);

		Figura fu;                       // el cobre unido, con la distancia medida hacia dentro
		fu.complemento = true;
		for (const auto &l : lazos) {
			if (!esCerrado(l)) {
				continue;
			}
			Poligono anillo(l.begin(), l.end() - 1);
			if (anillo.size() >= 3) {
				std::reverse(anillo.begin(), anillo.end());   // el hueco queda "a la izquierda": el offset entra en el cobre
				fu.anillos.push_back(std::move(anillo));
			}
		}
		if (fu.anillos.empty()) {
			continue;
		}
		calcularCaja(fu);

		// 2) Pasadas hacia dentro hasta que ya no cabe la fresa
		for (int k = 0; k < maxPasadas; ++k) {
			const double e = e0 + k * paso;
			std::vector<RawForma> raw;
			for (const auto &anillo : fu.anillos) {
				Poligono o = offsetCrudo(anillo, e);
				if (o.size() >= 3) {
					raw.push_back( { std::move(o), 0, false });
				}
			}
			std::vector<Poligono> caminos = fronteraUnionSD(raw, { &fu }, e, eps, { }, 0.0);
			caminos.erase(std::remove_if(caminos.begin(), caminos.end(), [&](const Poligono &p) {
				return longitud(p) < std::max(0.02, 0.5 * cfg.toolDiameter);
			}), caminos.end());
			if (caminos.empty()) {
				break;
			}
			for (auto &c : caminos) {
				Tramo t;
				t.cerrado = esCerrado(c);
				t.caja = cajaDe(c);
				t.p = std::move(c);
				tramos.push_back(std::move(t));
			}
		}
		cobre.push_back(std::move(fu));
	}

	// Un puente con la fresa abajo solo vale si todo él queda dentro del cobre, a la distancia de la primera pasada
	auto puenteSeguro = [&](const gerber::Vec2 &a, const gerber::Vec2 &b) {
		const int n = std::max(1, (int) std::ceil(std::sqrt(d2(a, b)) / 0.05));
		for (int i = 0; i <= n; ++i) {
			const double t = (double) i / n;
			const gerber::Vec2 p { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
			bool dentro = false;
			for (const auto &fu : cobre) {
				if (puntoEnCaja(fu.caja, p) && sdFigura(fu, p) >= e0 - eps) {
					dentro = true;
					break;
				}
			}
			if (!dentro) {
				return false;
			}
		}
		return true;
	};

	const gerber::Vec2 origen { 0, 0 };
	gerber::Vec2 actual = origen;
	bool abajo = false;
	rutaTramos(tramos, movimientos, actual, abajo, std::max(2.0 * cfg.toolDiameter, 0.2), puenteSeguro);
	if (abajo) {
		movimientos.push_back( { TipoMovimiento::Subir, actual, actual });
	}
	if (d2(actual, origen) > 1e-6) {
		movimientos.push_back( { TipoMovimiento::Viaje, actual, origen });
	}
	return movimientos;
}

// ─────────────────────────────────────────────────────────────
// generarGCodeTaladro
// ─────────────────────────────────────────────────────────────
std::string generarGCodeTaladro(const drill::DrillResult &resTal, const gerber::GCodeConfig &cfg) {
	std::string out;
	if (!resTal.ok || resTal.operations.empty()) {
		return "; Error: No hay operaciones de taladrado\nM30\n";
	}

	out += "; === Taladrado CNC ===\n";
	out += (resTal.unit == "IN") ? "G20\n" : "G21\n";
	out += "G17\nG90\nG94\n";
	std::ostringstream z_ss;
	z_ss << std::fixed << std::setprecision(4) << cfg.safeZ;
	out += "G00 Z" + z_ss.str() + "\n";
	out += "M03 S1000\n";

	std::map<double, std::vector<const drill::DrillOperation*>> porDiametro;
	for (const auto &op : resTal.operations) {
		porDiametro[op.diameter].push_back(&op);
	}

	int toolCnt = 1;
	for (const auto& [diam, ops] : porDiametro) {
		std::ostringstream dia_ss;
		dia_ss << std::fixed << std::setprecision(3) << diam;
		out += "; === Cambio a broca Ø" + dia_ss.str() + " ===\n";
		out += "M06 T" + std::to_string(toolCnt++) + "\n";
		for (const auto *op : ops) {
			std::ostringstream x_ss, y_ss, cut_z_ss;
			x_ss << std::fixed << std::setprecision(3) << op->x;
			y_ss << std::fixed << std::setprecision(3) << op->y;
			cut_z_ss << std::fixed << std::setprecision(4) << cfg.cutZ;
			out += "G00 X" + x_ss.str() + " Y" + y_ss.str() + "\n";
			out += "G00 Z" + z_ss.str() + "\n";
			out += "G01 Z" + cut_z_ss.str() + " F" + std::to_string((int) cfg.plungeRate) + "\n";
			out += "G00 Z" + z_ss.str() + "\n";
		}
	}
	out += "; === Fin del taladrado ===\nG00 X0 Y0\nG00 Z" + z_ss.str() + "\nM05\nM30\n";
	return out;
}
