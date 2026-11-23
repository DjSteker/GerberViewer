/*
 * Procesos/IslasCobre.cpp
 *
 *  Created on: 16 nov 2026
 *      Author: DjSteker
 */

#include "IslasCobre.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <unordered_map>

namespace islas {

namespace {

/// @brief Tolerancia geométrica general (evita comparaciones exactas de coma flotante).
constexpr double TOL_GEOM = 1e-9;

/// @brief Longitud mínima de un tramo para emitirlo (filtra movimientos nulos).
constexpr double TRAMO_MINIMO = 1e-6;

/**
 * @brief Distancia de un punto a un segmento (proyección acotada a [0,1]).
 */
double distanciaPuntoSegmento(const gerber::Vec2 &p, const gerber::Vec2 &a, const gerber::Vec2 &b) {
	const double dx = b.x - a.x;
	const double dy = b.y - a.y;
	const double largo2 = dx * dx + dy * dy;
	if (largo2 < TOL_GEOM * TOL_GEOM) {
		return (p - a).length();
	}
	double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / largo2;
	t = std::max(0.0, std::min(1.0, t));
	const gerber::Vec2 proy { a.x + t * dx, a.y + t * dy };
	return (p - proy).length();
}

/**
 * @brief Test de punto en polígono (ray casting); bordes ambiguos son irrelevante aquí.
 */
bool puntoEnPoligono(const gerber::Vec2 &p, const std::vector<gerber::Vec2> &poli) {
	if (poli.size() < 3) {
		return false;
	}
	bool dentro = false;
	const size_t n = poli.size();
	for (size_t i = 0, j = n - 1; i < n; j = i++) {
		const bool cruce = ((poli[i].y > p.y) != (poli[j].y > p.y));
		if (!cruce) {
			continue;
		}
		const double dy = poli[i].y - poli[j].y;
		if (std::abs(dy) < TOL_GEOM) {
			continue;
		}
		const double xInter = poli[j].x + (p.y - poli[j].y) * (poli[i].x - poli[j].x) / dy;
		if (p.x < xInter) {
			dentro = !dentro;
		}
	}
	return dentro;
}

/**
 * @brief Distancia con signo a un polígono: negativa dentro, positiva fuera.
 * @note  Implementación propia para no depender de verificacion_segura.h.
 */
double distanciaFirmaPoligono(const gerber::Vec2 &p, const std::vector<gerber::Vec2> &poli) {
	if (poli.size() < 3) {
		return 1e18;
	}
	double minDist = 1e18;
	for (size_t i = 0; i < poli.size(); ++i) {
		const gerber::Vec2 &a = poli[i];
		const gerber::Vec2 &b = poli[(i + 1) % poli.size()];
		minDist = std::min(minDist, distanciaPuntoSegmento(p, a, b));
	}
	if (puntoEnPoligono(p, poli)) {
		return -minDist;
	}
	return minDist;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  Detección de islas
// ─────────────────────────────────────────────────────────────────────────────

std::vector<IslaCobre> DetectorIslas::detectar(const gerber::GerberResult &resultado, const ConfigIslas &cfg) {
	/**
	 * @brief Figura candidata a isla (solo Flash y Region Dark) con su envolvente.
	 */
	struct Candidato {
		std::vector<gerber::Vec2> poligono;
		std::vector<std::vector<gerber::Vec2>> agujeros;
		double minX = 0.0;
		double minY = 0.0;
		double maxX = 0.0;
		double maxY = 0.0;
	};

	// 1) Reunir candidatos: Flash (pads/vías) y Region (masas G36/G37) de cobre Dark.
	std::vector<Candidato> candidatos;
	for (const auto &prim : resultado.primitives) {
		if (prim.polaridad != gerber::Polarity::Dark) {
			continue;
		}
		Candidato cand;
		if (prim.type == gerber::PrimType::Flash) {
			cand.poligono = gerber::geom::flashToPolygon(prim.asFlash(), 32);
		} else if (prim.type == gerber::PrimType::Region) {
			const auto &reg = prim.asRegion();
			if (reg.outline.size() >= 3) {
				cand.poligono = reg.outline;
				cand.agujeros = reg.holes;
			}
		} else {
			// Segment y Arc son pistas: las tratan Mecanizado y ToolpathGenerator.
			continue;
		}
		if (cand.poligono.size() < 3) {
			continue;
		}
		if (std::abs(gerber::geom::signedArea(cand.poligono)) < cfg.areaMinima) {
			continue;
		}
		cand.poligono = gerber::geom::ensureCCW(cand.poligono);
		cand.minX = cand.maxX = cand.poligono[0].x;
		cand.minY = cand.maxY = cand.poligono[0].y;
		for (const auto &v : cand.poligono) {
			cand.minX = std::min(cand.minX, v.x);
			cand.minY = std::min(cand.minY, v.y);
			cand.maxX = std::max(cand.maxX, v.x);
			cand.maxY = std::max(cand.maxY, v.y);
		}
		candidatos.push_back(std::move(cand));
	}

	const size_t n = candidatos.size();

	// 2) Union-Find: dos candidatos en la misma isla si se tocan.
	std::vector<int> padre(static_cast<size_t>(n));
	for (size_t i = 0; i < n; ++i) {
		padre[i] = static_cast<int>(i);
	}
	auto encontrar = [&padre](int x) -> int {
		while (padre[static_cast<size_t>(x)] != x) {
			padre[static_cast<size_t>(x)] = padre[static_cast<size_t>(padre[static_cast<size_t>(x)])];
			x = padre[static_cast<size_t>(x)];
		}
		return x;
	};
	auto unir = [&encontrar, &padre](int a, int b) -> void {
		const int ra = encontrar(a);
		const int rb = encontrar(b);
		if (ra != rb) {
			padre[static_cast<size_t>(rb)] = ra;
		}
	};
	auto seTocan = [&cfg](const Candidato &a, const Candidato &b) -> bool {
		// Filtro rápido por cajas envolventes (con margen de tolerancia).
		if (a.maxX + cfg.tolConexion < b.minX || b.maxX + cfg.tolConexion < a.minX) {
			return false;
		}
		if (a.maxY + cfg.tolConexion < b.minY || b.maxY + cfg.tolConexion < a.minY) {
			return false;
		}
		// Vértices de A (muestreados cada 2) cerca/inside de B y viceversa.
		for (size_t i = 0; i < a.poligono.size(); i += 2) {
			if (distanciaFirmaPoligono(a.poligono[i], b.poligono) <= cfg.tolConexion) {
				return true;
			}
		}
		for (size_t i = 0; i < b.poligono.size(); i += 2) {
			if (distanciaFirmaPoligono(b.poligono[i], a.poligono) <= cfg.tolConexion) {
				return true;
			}
		}
		return false;
	};
	for (size_t i = 0; i < n; ++i) {
		for (size_t j = i + 1; j < n; ++j) {
			if (seTocan(candidatos[i], candidatos[j])) {
				unir(static_cast<int>(i), static_cast<int>(j));
			}
		}
	}

	// 3) Agrupar por raíz y construir las islas.
	std::unordered_map<int, std::vector<size_t>> grupos;
	for (size_t i = 0; i < n; ++i) {
		grupos[encontrar(static_cast<int>(i))].push_back(i);
	}

	std::vector < IslaCobre > islas;
	for (const auto &par : grupos) {
		IslaCobre isla;
		for (size_t idx : par.second) {
			const Candidato &c = candidatos[idx];
			isla.contornos.push_back(c.poligono);
			for (const auto &h : c.agujeros) {
				if (h.size() >= 3) {
					isla.agujeros.push_back(h);
				}
			}
			isla.area += std::abs(gerber::geom::signedArea(c.poligono));
		}
		for (const auto &h : isla.agujeros) {
			isla.area -= std::abs(gerber::geom::signedArea(h));
		}
		isla.area = std::max(isla.area, 0.0);
		if (!isla.contornos.empty()) {
			islas.push_back(std::move(isla));
		}
	}
	return islas;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Trayectoria de perfilado
// ─────────────────────────────────────────────────────────────────────────────

std::vector<MovimientoIslas> DetectorIslas::generarTrayectoria(const gerber::GerberResult &resultado, const ConfigIslas &cfg) {
	std::vector < MovimientoIslas > movs;
	gerber::Vec2 actual { 0.0, 0.0 };

	const double radio = cfg.toolDiameter * 0.5 + cfg.clearance;
	const double paso = std::max(cfg.toolDiameter * cfg.stepoverFactor, TOL_GEOM);
	const std::vector<IslaCobre> islasDetectadas = detectar(resultado, cfg);

	/**
	 * @brief Emite un anillo cerrado: rápido hasta el inicio y cortes alrededor.
	 */
	auto emitirAnillo = [&movs, &actual](const std::vector<gerber::Vec2> &anillo) -> void {
		if (anillo.size() < 3) {
			return;
		}
		if ((anillo[0] - actual).length() > TRAMO_MINIMO) {
			movs.push_back(MovimientoIslas { true, actual, anillo[0] });
		}
		for (size_t i = 1; i < anillo.size(); ++i) {
			if ((anillo[i] - anillo[i - 1]).length() > TRAMO_MINIMO) {
				movs.push_back(MovimientoIslas { false, anillo[i - 1], anillo[i] });
			}
		}
		if ((anillo.front() - anillo.back()).length() > TRAMO_MINIMO) {
			movs.push_back(MovimientoIslas { false, anillo.back(), anillo.front() });
		}
		actual = anillo[0];
	};

	/**
	 * @brief Valida un anillo: todos sus puntos dentro del cobre de la isla
	 *        y fuera de TODOS sus agujeros.
	 */
	auto anilloValido = [](const std::vector<gerber::Vec2> &anillo, const IslaCobre &isla) -> bool {
		if (anillo.size() < 3) {
			return false;
		}
		for (const auto &pt : anillo) {
			bool dentroCobre = false;
			for (const auto &cont : isla.contornos) {
				if (distanciaFirmaPoligono(pt, cont) < 0.0) {
					dentroCobre = true;
					break;
				}
			}
			if (!dentroCobre) {
				return false;
			}
			for (const auto &agu : isla.agujeros) {
				if (distanciaFirmaPoligono(pt, agu) < 0.0) {
					return false;
				}
			}
		}
		return true;
	};

	for (const auto &isla : islasDetectadas) {
		// a) Pasadas concéntricas HACIA ADENTRO en cada contorno de la isla.
		for (const auto &contorno : isla.contornos) {
			const std::vector<gerber::Vec2> base = gerber::geom::ensureCCW(contorno);
			if (base.size() < 3) {
				continue;
			}
			double offset = radio;
			for (int pasada = 0; pasada < cfg.maxPasadas; ++pasada) {
				auto anillo = gerber::geom::offsetPolygon(base, -offset);
				if (anillo.size() < 3) {
					break;
				}
				if (!anilloValido(anillo, isla)) {
					break;
				}
				emitirAnillo(anillo);
				offset += paso;
			}
		}

		// b) Anillos concéntricos ALREDEDOR de cada agujero de la masa
		//    (primera pasada a radio de herramienta + clearance del borde
		//    del hueco, siguientes hacia el interior del cobre).
		for (const auto &agujero : isla.agujeros) {
			const std::vector<gerber::Vec2> base = gerber::geom::ensureCCW(agujero);
			if (base.size() < 3) {
				continue;
			}
			double offset = radio;
			for (int pasada = 0; pasada < cfg.maxPasadas; ++pasada) {
				auto anillo = gerber::geom::offsetPolygon(base, offset);
				if (anillo.size() < 3) {
					break;
				}
				if (!anilloValido(anillo, isla)) {
					break;
				}
				emitirAnillo(anillo);
				offset += paso;
			}
		}
	}

	// Retorno al origen en rápido.
	if ((actual - gerber::Vec2 { 0.0, 0.0 }).length() > TRAMO_MINIMO) {
		movs.push_back(MovimientoIslas { true, actual, gerber::Vec2 { 0.0, 0.0 } });
	}
	return movs;
}

}  // namespace islas
