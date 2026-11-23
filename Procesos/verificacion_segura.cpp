/*
 * Procesos/verificacion_segura.cpp
 *
 *  Created on: 6 may 2026
 *      Author: DjSteker
 */

#include "verificacion_segura.h"
#include <cmath>
#include <algorithm>

namespace gerber {
namespace verificacion {

// Función auxiliar interna para cálculos de distancia
double VerificacionSegura::distanciaPuntoSegmento(const Vec2 &punto, const Vec2 &inicio, const Vec2 &fin) {
	Vec2 vectorSegmento = { fin.x - inicio.x, fin.y - inicio.y };
	Vec2 vectorPuntoInicio = { punto.x - inicio.x, punto.y - inicio.y };

	double longitudCuadrado = vectorSegmento.x * vectorSegmento.x + vectorSegmento.y * vectorSegmento.y;

	if (longitudCuadrado == 0) {
		return std::sqrt(vectorPuntoInicio.x * vectorPuntoInicio.x + vectorPuntoInicio.y * vectorPuntoInicio.y);
	}

	double parametro = (vectorPuntoInicio.x * vectorSegmento.x + vectorPuntoInicio.y * vectorSegmento.y) / longitudCuadrado;
	parametro = std::max(0.0, std::min(1.0, parametro));

	Vec2 proyeccion = { inicio.x + parametro * vectorSegmento.x, inicio.y + parametro * vectorSegmento.y };

	double dx = punto.x - proyeccion.x;
	double dy = punto.y - proyeccion.y;
	return std::sqrt(dx * dx + dy * dy);
}

bool VerificacionSegura::puntoEnPoligono(const Vec2 &punto, const std::vector<Vec2> &poligono) {
	int numVertices = static_cast<int>(poligono.size());
	bool interior = false;
	for (int i = 0, j = numVertices - 1; i < numVertices; j = i++) {
		if (((poligono[i].y > punto.y) != (poligono[j].y > punto.y)) && (punto.x < (poligono[j].x - poligono[i].x) * (punto.y - poligono[i].y) / (poligono[j].y - poligono[i].y) + poligono[i].x)) {
			interior = !interior;
		}
	}
	return interior;
}

double VerificacionSegura::distanciaFirmaPoligono(const Vec2 &punto, const std::vector<Vec2> &poligono) {
	double minDist = 1e18;
	for (size_t i = 0; i < poligono.size(); ++i) {
		size_t j = (i == 0) ? poligono.size() - 1 : i - 1;
		minDist = std::min(minDist, distanciaPuntoSegmento(punto, poligono[j], poligono[i]));
	}
	return puntoEnPoligono(punto, poligono) ? -minDist : minDist;
}

double VerificacionSegura::holguraAPrimitiva(const Vec2 &punto, const Primitive &primitiva) {
	switch (primitiva.type) {
	case PrimType::Segment: {
		const auto &seg = primitiva.asSegment();
		double d = distanciaPuntoSegmento(punto, seg.start, seg.end);
		return d - (seg.width * 0.5);
	}
	case PrimType::Flash: {
		const auto &flash = primitiva.asFlash();
		double dx = punto.x - flash.pos.x;
		double dy = punto.y - flash.pos.y;
		double distCentro = std::sqrt(dx * dx + dy * dy);
		// Asumiendo flash circular para simplificar, o usar su bounding box
		return distCentro - 0.5;  // Ajustar según lógica de apertura
	}
	case PrimType::Region: {
		return distanciaFirmaPoligono(punto, primitiva.asRegion().outline);
	}
	default:
		return 1e18;
	}
}

double VerificacionSegura::holguraMinimaConductores(const Vec2 &punto, const GerberResult &resultado, const Primitive *excluirPrim) {
	double minH = 1e18;
	for (const auto &p : resultado.primitives) {
		if (p.polaridad != Polarity::Dark) {
			continue;
		}
		if (&p == excluirPrim) {
			continue;
		}

		minH = std::min(minH, holguraAPrimitiva(punto, p));
	}
	return minH;
}

bool VerificacionSegura::posicionHerramientaSegura(const Vec2 &centro, const GerberResult &resultado, double diametroHerramienta, double tolerancia, const Primitive *excluirPrim) {
	double holguraReq = (diametroHerramienta * 0.5) + tolerancia;
	double holguraReal = holguraMinimaConductores(centro, resultado, excluirPrim);
	return holguraReal >= holguraReq;
}

bool VerificacionSegura::posicionHerramientaSeguraCirculo(const Vec2 &centro, const GerberResult &resultado, double diametroHerramienta, double tolerancia, int numMuestras, const Primitive *excluirPrim) {
	double radioMuestreo = (diametroHerramienta * 0.5) + tolerancia;

	// 1. Verificar el centro
	if (!posicionHerramientaSegura(centro, resultado, diametroHerramienta, tolerancia, excluirPrim))
		return false;

	// 2. Verificar puntos en la periferia
	for (int i = 0; i < numMuestras; ++i) {
		double angulo = 2.0 * M_PI * i / numMuestras;
		Vec2 puntoPrueba = { centro.x + radioMuestreo * std::cos(angulo), centro.y + radioMuestreo * std::sin(angulo) };
		// Para los puntos periféricos basta ver que no estén DENTRO del cobre (holgura > 0)
		if (holguraMinimaConductores(puntoPrueba, resultado, excluirPrim) < 0.001) {
			return false;
		}
	}
	return true;
}

std::optional<Vec2> VerificacionSegura::buscarPuntoEntradaSeguro(const Vec2 &objetivo, const GerberResult &resultado, double diametroHerramienta, double tolerancia, double radioBusqueda) {
	const int pasosAnillo = 16;
	const int numAnillos = 5;

	for (int a = 1; a <= numAnillos; ++a) {
		double r = (radioBusqueda / numAnillos) * a;
		for (int i = 0; i < pasosAnillo; ++i) {
			double ang = 2.0 * M_PI * i / pasosAnillo;
			Vec2 candidato = { objetivo.x + std::cos(ang) * r, objetivo.y + std::sin(ang) * r };
			if (posicionHerramientaSegura(candidato, resultado, diametroHerramienta, tolerancia)) {
				return candidato;
			}
		}
	}
	return std::nullopt;
}

}  // namespace verificacion
}  // namespace gerber
