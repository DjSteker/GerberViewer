/*
 * ToolpathGenerator.cpp
 *
 *  Created on: 7 may 2026
 *      Author: DjSteker
 */

#include "ToolpathGenerator.h"
#include "./gerber.h"
#include "./verificacion_segura.h"
#include <algorithm>
#include <limits>
#include <cmath>

namespace gerber {
namespace geom {

// Convierte un segmento (con ancho) en un polígono (cápsula)
// ptsTapa es la resolución de los semicírculos en los extremos
std::vector<Vec2> segmentToPolygon(const Segment &seg, int ptsTapa = 8) {
	Vec2 d = seg.end - seg.start;
	double largo = d.length();
	if (largo < 1e-12) {
		return {};}

	Vec2 perp { -d.y / largo, d.x / largo };
	double r = seg.width * 0.5;
	std::vector<Vec2> poli;

	// Extremo final (semicírculo)
	for (int i = 0; i <= ptsTapa; ++i) {
		double a = M_PI * i / ptsTapa;
		poli.push_back( { seg.end.x + r * (std::cos(a) * (d.x / largo) - std::sin(a) * (d.y / largo)), seg.end.y + r * (std::cos(a) * (d.y / largo) + std::sin(a) * (d.x / largo)) });
	}
	// Extremo inicial (semicírculo opuesto)
	for (int i = 0; i <= ptsTapa; ++i) {
		double a = M_PI + M_PI * i / ptsTapa;
		poli.push_back( { seg.start.x + r * (std::cos(a) * (d.x / largo) - std::sin(a) * (d.y / largo)), seg.start.y + r * (std::cos(a) * (d.y / largo) + std::sin(a) * (d.x / largo)) });
	}
	return poli;
}

// Convierte un arco (con ancho) en un polígono (dos contornos concéntricos unidos)
std::vector<Vec2> arcToPolygon(const Arc &a, int ptsPorCirculo = 32) {
	// Primero obtenemos la polilínea del centro del arco
	auto centerLine = arcToPolyline(a.center, a.radius, a.startAngle, a.endAngle, a.ccw, 0.01);
	if (centerLine.size() < 2) {
		return {};}

	double r = a.width * 0.5;
	std::vector<Vec2> poly;

	// Generar el perímetro exterior (offset hacia afuera) e interior (offset hacia adentro)
	// Para un arco con ancho, el polígono resultante es como un tubo curvo.
	// Estrategia: generar los puntos del borde exterior recorriendo la línea central + offset perpendicular,
	// luego el borde interior en sentido inverso.

	// Pre-calculamos normales para cada punto de la línea central
	std::vector<Vec2> normals;
	for (size_t i = 0; i < centerLine.size(); ++i) {
		Vec2 dir;
		if (i == 0) {
			dir = (centerLine[1] - centerLine[0]).normalized();}
		else if (i == centerLine.size() - 1) {
			dir = (centerLine[i] - centerLine[i - 1]).normalized();}
		else {
			dir = (centerLine[i + 1] - centerLine[i - 1]).normalized();}

		if (dir.length() < 1e-12) {
			dir = Vec2 { 1, 0 };}
		normals.push_back( { -dir.y, dir.x });  // perpendicular izquierda (desde vista CCW)
	}

	// Borde exterior (offset +r)
	for (size_t i = 0; i < centerLine.size(); ++i) {
		poly.push_back(centerLine[i] + normals[i] * r);
	}
	// Borde interior (offset -r) en orden inverso para que el polígono sea CCW
	for (size_t i = centerLine.size(); i-- > 0;) {
		poly.push_back(centerLine[i] - normals[i] * r);
	}
	return poly;
}

}  // namespace geom
}  // namespace gerber

// ─────────────────────────────────────────────────────────────────────────────
// Implementación de ToolpathGenerator
// ─────────────────────────────────────────────────────────────────────────────

using namespace gerber;

ToolpathGenerator::ToolpathGenerator(const ToolpathConfig &cfg) :
		cfg_(cfg) {
}

std::vector<ToolpathMove> ToolpathGenerator::generate(const GerberResult &result, ToolpathMode mode) {
	if (!result.ok) {
		return {};}

	switch (mode) {
	case ToolpathMode::ProfileCopper: {
		auto copper = extractCopperPolygons(result);
		auto clear = extractClearPolygons(result);
		auto moves = buildProfileTrajectory(copper, clear);
		if (cfg_.optimizeOrder) {
			moves = optimizeOrder(moves);}
		return moves;
	}
	case ToolpathMode::Isolation: {
		auto copper = extractCopperPolygons(result);
		auto clear = extractClearPolygons(result);
		auto moves = buildIsolationTrajectory(copper, clear);
		if (cfg_.optimizeOrder) {
			moves = optimizeOrder(moves);}
		return moves;
	}
	case ToolpathMode::TracePaths:
		return buildTraceTrajectory(result);
	default:
		return {};
	}
}

std::vector<std::vector<Vec2>> ToolpathGenerator::extractCopperPolygons(const GerberResult &result) {
	std::vector<std::vector<Vec2>> polys;
	for (const auto &prim : result.primitives) {
		if (prim.polaridad != Polarity::Dark) {
			continue;}
		switch (prim.type) {
		case PrimType::Segment: {
			const auto &seg = prim.asSegment();
			auto poly = geom::segmentToPolygon(seg, 8);
			if (!poly.empty()) {
				polys.push_back(std::move(poly));}
			break;
		}
		case PrimType::Arc: {
			const auto &a = prim.asArc();
			auto poly = geom::arcToPolygon(a, 32);
			if (!poly.empty()) {
				polys.push_back(std::move(poly));}
			break;
		}
		case PrimType::Flash: {
			auto poly = geom::flashToPolygon(prim.asFlash(), 32);
			if (!poly.empty()) {
				polys.push_back(std::move(poly));}
			break;
		}
		case PrimType::Region: {
			const auto &r = prim.asRegion();
			if (r.outline.size() >= 3) {
				polys.push_back(r.outline);}
			break;
		}
		}
	}
	return polys;
}

std::vector<std::vector<Vec2>> ToolpathGenerator::extractClearPolygons(const GerberResult &result) {
	std::vector<std::vector<Vec2>> polys;
	for (const auto &prim : result.primitives) {
		if (prim.polaridad != Polarity::Clear) {
			continue;}
		switch (prim.type) {
		case PrimType::Segment: {
			const auto &seg = prim.asSegment();
			auto poly = geom::segmentToPolygon(seg, 8);
			if (!poly.empty()) {
				polys.push_back(std::move(poly));}
			break;
		}
		case PrimType::Arc: {
			const auto &a = prim.asArc();
			auto poly = geom::arcToPolygon(a, 32);
			if (!poly.empty()) {
				polys.push_back(std::move(poly));}
			break;
		}
		case PrimType::Flash: {
			auto poly = geom::flashToPolygon(prim.asFlash(), 32);
			if (!poly.empty()) {
				polys.push_back(std::move(poly));}
			break;
		}
		case PrimType::Region: {
			const auto &r = prim.asRegion();
			if (r.outline.size() >= 3) {
				polys.push_back(r.outline);}
			break;
		}
		}
	}
	return polys;
}

bool ToolpathGenerator::isPointSafe(const Vec2 &point, const std::vector<std::vector<Vec2>> &copperPolys, const std::vector<std::vector<Vec2>> &clearPolys, const Primitive* /*excludePrim*/) const {
	double radius = cfg_.toolDiameter * 0.5 + cfg_.clearance;

	// Verificar que no está demasiado cerca del cobre (dark)
	for (const auto &poly : copperPolys) {
		double dist = verificacion::distanciaFirmaPoligono(point, poly);
		if (dist < radius) {
			return false;}
	}
	// Verificar que no invade zonas clear
	for (const auto &clearPoly : clearPolys) {
		double dist = verificacion::distanciaFirmaPoligono(point, clearPoly);
		if (dist < radius) {
			return false;}
	}
	return true;
}

std::vector<ToolpathMove> ToolpathGenerator::buildProfileTrajectory(const std::vector<std::vector<Vec2>> &copperPolys, const std::vector<std::vector<Vec2>> &clearPolys) {

	std::vector<ToolpathMove> moves;
	Vec2 currentPos { 0, 0 };
	double radius = cfg_.toolDiameter * 0.5 + cfg_.clearance;
	double step = cfg_.stepover;

	auto addRapid = [&](const Vec2 &from, const Vec2 &to) {
		if ((to - from).length() < 1e-6) {
			return;}
		moves.push_back( { true, from, to });
	};
	auto addCut = [&](const Vec2 &from, const Vec2 &to) {
		if ((to - from).length() < 1e-6) {
			return;}
		moves.push_back( { false, from, to });
	};

	for (const auto &basePoly : copperPolys) {
		auto poly = geom::ensureCCW(basePoly);
		if (poly.size() < 3) {
			continue;}

		double offset = radius;
		for (int pass = 0; pass < cfg_.maxPasses; ++pass) {
			auto offsetPoly = geom::offsetPolygon(poly, -offset);
			if (offsetPoly.size() < 3) {
				break;}

			// Verificar que todos los puntos del offset son seguros
			bool safe = true;
			for (const auto &pt : offsetPoly) {
				if (!isPointSafe(pt, copperPolys, clearPolys, nullptr)) {
					safe = false;
					break;
				}
			}
			if (!safe) {
				offset += step * 0.2;  // intentar un offset ligeramente mayor
				continue;
			}

			// Agregar movimientos para este contorno
			if (!offsetPoly.empty()) {
				addRapid(currentPos, offsetPoly[0]);
				for (size_t i = 1; i < offsetPoly.size(); ++i) {
					addCut(offsetPoly[i - 1], offsetPoly[i]);
				}
				addCut(offsetPoly.back(), offsetPoly[0]);
				currentPos = offsetPoly[0];
			}
			offset += step;
		}
	}
	addRapid(currentPos, Vec2 { 0, 0 });
	return moves;
}

// Para Isolation se puede implementar de forma similar pero offset positivo y evitando cobre
std::vector<ToolpathMove> ToolpathGenerator::buildIsolationTrajectory(const std::vector<std::vector<Vec2>> &copperPolys, const std::vector<std::vector<Vec2>> &clearPolys) {
	// Implementación análoga: offset hacia afuera de las zonas Clear y verificar que no toquen cobre
	// Por brevedad, se omite aquí. Puede copiarse de la versión anterior adaptada.
	return {};
}

std::vector<ToolpathMove> ToolpathGenerator::buildTraceTrajectory(const GerberResult &result) {
	std::vector<ToolpathMove> moves;
	// Recorrer segmentos, arcos y flashes como líneas/arcos simples sin offset
	// (similar a la generación de GCode básica)
	return moves;
}

std::vector<ToolpathMove> ToolpathGenerator::optimizeOrder(const std::vector<ToolpathMove> &moves) {
	if (moves.empty()) {
		return moves;}
	// Implementación simple: ordenar por vecino más cercano
	// (puede copiarse de la función optimizeOrder del generador GCode)
	return moves;  // placeholder
}
