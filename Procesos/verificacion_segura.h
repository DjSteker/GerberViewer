/*
 * Procesos/verificacion_segura.h
 *
 *  Created on: 6 may 2026
 *      Author: DjSteker
 */

/**
 * @file    verificacion_segura.h
 * @brief   Verificación de colisiones para el mecanizado CNC seguro.
 *
 * @details Este módulo responde a una pregunta clave antes de mover la
 *          fresa: \u00bfel centro de la herramienta puede estar en un
 *          punto sin invadir el cobre? Para ello mide la "holgura"
 *          (distancia libre) entre puntos y las primitivas del
 *          Gerber, con o sin el radio de la herramienta.
 *
 * @par     Concepto central: la holgura
 *          La holgura de un punto frente a una primitiva es la
 *          distancia mínima al cobre, descontando el grosor del
 *          trazo (segmento: distancia al eje menos
 *          @c width/2; región: distancia con signo al contorno).
 *          Positiva = hay material libre; negativa = el punto cae
 *          dentro del cobre.
 *
 * @par     Quién lo utiliza
 *          - @c Procesos/ToolpathGenerator.cpp: @c isPointSafe() usa
 *            @c distanciaFirmaPoligono() para validar cada punto del
 *            toolpath contra los polígonos de cobre y de zonas
 *            @c Clear.
 *          - Las funciones de verificación de posición y búsqueda de
 *            puntos de entrada están disponibles para los módulos de
 *            mecanizado, aunque hoy no tienen otros consumidores.
 *
 * @author  DjSteker
 * @date    6 may 2026
 */

#ifndef PROCESOS_VERIFICACION_SEGURA_H_
#define PROCESOS_VERIFICACION_SEGURA_H_

#include <optional>
#include <vector>

#include "gerber.h"

namespace gerber {
namespace verificacion {

// =============================================================================
// CONFIGURACIÓN DE VERIFICACIÓN
// =============================================================================

/**
 * @struct ConfiguracionVerificacion
 * @brief  Parámetros de las verificaciones de seguridad.
 *
 * @warning Definida pero sin uso actual: ninguna función del módulo
 *          la recibe como argumento (las verificaciones toman
 *          diámetro y tolerancia directamente).
 */
struct ConfiguracionVerificacion {
	double diametroHerramienta = 0.1;   ///< @brief Diámetro de la fresa (mm).
	double toleranciaSeguridad = 0.05;  ///< @brief Margen extra de seguridad (mm).
	int muestrasCirculo = 36;           ///< @brief Puntos muestreados en la verificación circular.
	bool verbose = false;               ///< @brief Reservado para logs detallados (sin efecto hoy).
};

// =============================================================================
// FUNCIONES GEOMÉTRICAS BÁSICAS
// =============================================================================
class VerificacionSegura {

public:
	/**
	 * @brief   Distancia mínima de un punto a un segmento recto.
	 * @details Proyección ortogonal del punto sobre la recta del
	 *          segmento, acotada a los extremos (si el segmento es
	 *          degenerado, devuelve la distancia al punto de inicio).
	 * @param   punto   Punto a medir.
	 * @param   inicio  Extremo inicial del segmento.
	 * @param   fin     Extremo final del segmento.
	 * @return  Distancia euclídea al segmento (siempre ≥ 0).
	 */
	static double distanciaPuntoSegmento(const Vec2 &punto, const Vec2 &inicio, const Vec2 &fin);

	/**
	 * @brief   Comprueba si un punto está dentro de un polígono.
	 * @details Algoritmo de ray casting: cuenta cuántas veces un rayo
	 *          horizontal que sale del punto cruza los bordes. Los
	 *          puntos sobre el borde son indeterminados.
	 * @param   punto     Punto a comprobar.
	 * @param   poligono  Contorno cerrado (mínimo 3 vértices).
	 * @return  @c true si el punto es interior.
	 */
	static bool puntoEnPoligono(const Vec2 &punto, const std::vector<Vec2> &poligono);

	/**
	 * @brief   Distancia con signo de un punto a un polígono.
	 * @details Combina @c puntoEnPoligono() y @c distanciaPuntoSegmento():
	 *          devuelve la distancia mínima al contorno, con signo
	 *          negativo si el punto está dentro del polígono.
	 *
	 *          Positiva = fuera (distancia al borde); negativa = dentro
	 *          (penetración). Es la base de @c holguraAPrimitiva() para
	 *          regiones y de la verificación de @c ToolpathGenerator.
	 * @param   punto     Punto a medir.
	 * @param   poligono  Contorno cerrado.
	 * @return  Distancia con signo (0 = exactamente en el borde).
	 */
	static double distanciaFirmaPoligono(const Vec2 &punto, const std::vector<Vec2> &poligono);

	/**
	 * @brief   Holgura de un punto frente a una primitiva de cobre.
	 * @details Según el tipo de primitiva:
	 *          - Segment: distancia al eje del trazo menos @c width/2
	 *            (descuenta el grosor del trazo).
	 *          - Region: @c distanciaFirmaPoligono() contra el contorno.
	 *          - Flash: distancia al centro menos 0.5.
	 *          - Arc y demás tipos: 1e18 (sin límite).
	 *
	 * @warning El caso @c Flash asume un flash circular de radio 0.5
	 *          fijo: NO usa el diámetro real de la apertura. Revisar si
	 *          se verifican pads pequeños o grandes.
	 *
	 * @param   punto      Punto a medir (típicamente el centro de la fresa).
	 * @param   primitiva  Primitiva Dark contra la que medir.
	 * @return  Holgura en unidades del archivo: > 0 libre, < 0 dentro
	 *          del cobre.
	 */
	static double holguraAPrimitiva(const Vec2 &punto, const Primitive &primitiva);

// =============================================================================
// VERIFICACIÓN DE POSICIÓN SEGURA
// =============================================================================

	/**
	 * @brief   Comprueba si el centro de la herramienta está libre de colisiones.
	 * @details La posición es segura si la holgura mínima a todos los
	 *          conductores (excepto el excluido) es mayor o igual que
	 *          @c diámetro/2 + tolerancia, es decir, la fresa no invade
	 *          ningún cobre.
	 * @param   centro                Centro de la herramienta a comprobar.
	 * @param   resultado             Geometría Gerber contra la que verificar.
	 * @param   diametroHerramienta   Diámetro de la fresa.
	 * @param   tolerancia            Margen extra de seguridad.
	 * @param   excluirPrim           Primitiva a ignorar (la que se está
	 *                                mecanizando); @c nullptr para no
	 *                                excluir ninguna.
	 * @return  @c true si la herramienta no invade ningún cobre.
	 */
	static bool posicionHerramientaSegura(const Vec2 &centro, const GerberResult &resultado, double diametroHerramienta, double tolerancia, const Primitive *excluirPrim = nullptr);

	/**
	 * @brief   Verificación reforzada: muestrea 360° alrededor del centro.
	 * @details Además del propio centro (con @c posicionHerramientaSegura()),
	 *          comprueba @p numMuestras puntos en la circunferencia de
	 *          radio @c diámetro/2 + tolerancia; para ellos basta con que
	 *          no caigan dentro del cobre (holgura > 0.001). Detecta
	 *          colisiones que un único punto central pasa por alto.
	 * @param   centro                Centro de la herramienta.
	 * @param   resultado             Geometría Gerber contra la que verificar.
	 * @param   diametroHerramienta   Diámetro de la fresa.
	 * @param   tolerancia            Margen extra de seguridad.
	 * @param   numMuestras           Puntos del círculo (36 por defecto).
	 * @param   excluirPrim           Primitiva a ignorar; @c nullptr para
	 *                                no excluir ninguna.
	 * @return  @c true si el centro y toda la periferia están libres.
	 */
	static bool posicionHerramientaSeguraCirculo(const Vec2 &centro, const GerberResult &resultado, double diametroHerramienta, double tolerancia, int numMuestras = 36, const Primitive *excluirPrim = nullptr);

	/**
	 * @brief   Holgura mínima de un punto a todos los conductores.
	 * @details Recorre las primitivas @c Dark (excepto la excluida) y
	 *          devuelve la menor holgura según @c holguraAPrimitiva().
	 *          Es la medida que usan las verificaciones de posición.
	 * @param   punto       Punto a medir.
	 * @param   resultado   Geometría Gerber.
	 * @param   excluirPrim Primitiva a ignorar; @c nullptr para ninguna.
	 * @return  La holgura mínima (1e18 si no hay conductores).
	 */
	static double holguraMinimaConductores(const Vec2 &punto, const GerberResult &resultado, const Primitive *excluirPrim = nullptr);

// =============================================================================
// BÚSQUEDA DE PUNTOS DE ENTRADA SEGUROS
// =============================================================================

	/**
	 * @brief   Encuentra un punto de entrada seguro cerca del objetivo.
	 * @details Explora en anillos concéntricos alrededor del punto
	 *          objetivo (5 anillos de 16 puntos cada uno, separados
	 *          @p radioBusqueda/5) y devuelve el primer candidato que
	 *          pasa @c posicionHerramientaSegura().
	 *
	 * @warning No comprueba el propio punto objetivo: empieza en el
	 *          primer anillo. Si el objetivo ya es seguro, devolverá un
	 *          punto a @p radioBusqueda/5 de él.
	 * @warning Si ningún punto de los anillos es seguro, devuelve
	 *          @c std::nullopt sin considerar radios mayores.
	 *
	 * @param   objetivo              Punto de referencia deseado.
	 * @param   resultado             Geometría Gerber contra la que verificar.
	 * @param   diametroHerramienta   Diámetro de la fresa.
	 * @param   tolerancia            Margen extra de seguridad.
	 * @param   radioBusqueda         Radio máximo de exploración (5.0 por
	 *                                defecto).
	 * @return  El primer punto seguro encontrado, o @c std::nullopt.
	 */
	static std::optional<Vec2> buscarPuntoEntradaSeguro(const Vec2 &objetivo, const GerberResult &resultado, double diametroHerramienta, double tolerancia, double radioBusqueda = 5.0);

// =============================================================================
// UTILIDADES POLIGONALES
// =============================================================================

	/**
	 * @brief   Construye la cápsula (rectángulo con tapas redondeadas) de un tramo. (Pendiente)
	 *
	 * @details Objetivo: dado un tramo con radio, generar el polígono que
	 *          ocupa la herramienta al recorrerlo.
	 *
	 * @warning Solo está declarada: no tiene implementación en el .cpp y
	 *          nadie la llama.
	 *
	 * @param   inicio     Extremo inicial del tramo.
	 * @param   fin        Extremo final del tramo.
	 * @param   radio      Radio de la herramienta/trazo.
	 * @param   segmentos  Resolución de las tapas semicirculares.
	 * @return  (Sin implementación.)
	 */
	static std::vector<Vec2> poligonoCapsula(const Vec2 &inicio, const Vec2 &fin, double radio, int segmentos = 8);

	/**
	 * @brief   Convierte una primitiva en su polígono equivalente. (Pendiente)
	 *
	 * @warning Solo está declarada: no tiene implementación en el .cpp y
	 *          nadie la llama. Para convertir primitivas a polígono se
	 *          usa @c gerber::geom (flashToPolygon, offsetPolygon...).
	 *
	 * @param   primitiva  Primitiva a convertir.
	 * @param   segmentos  Resolución de las curvas.
	 * @return  (Sin implementación.)
	 */
	static std::vector<Vec2> poligonoPrimitiva(const Primitive &primitiva, int segmentos = 48);
};
}  // namespace verificacion
}  // namespace gerber

#endif /* PROCESOS_VERIFICACION_SEGURA_H_ */

