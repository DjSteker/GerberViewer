/**
 * @file    ToolpathGenerator.h
 * @brief   Generador de trayectorias CNC (toolpaths) a partir de geometría Gerber.
 *
 * @details Este módulo traduce el modelo geométrico de un PCB (producido por
 *          @c gerber::GerberParser) a una secuencia ordenada de movimientos
 *          CNC (rápidos y de corte) lista para exportarse como G-code.
 *
 *          Se ofrecen tres modos de operación independientes, seleccionables
 *          mediante @ref ToolpathMode (ver el estado real de cada uno en la
 *          documentación del propio enumerado).
 *
 * @section users Quién lo utiliza
 *  - @c Nucleo/interfaz.cpp — En el callback @c alTogglePerfiladoCobre()
 *    del botón "🔲 Perfilar Cobre" de la barra de herramientas. Construye
 *    un @ref ToolpathConfig, instancia @ref ToolpathGenerator y llama a
 *    @ref ToolpathGenerator::generate() con @ref ToolpathMode::ProfileCopper.
 *    Nota: ahí el @c stepover se calcula como 35 % del diámetro de la
 *    herramienta, no con el valor por defecto de la configuración.
 *  - @c Mecanizado/mecanizado.cpp incluye esta cabecera pero hoy no usa
 *    nada de este módulo (implementa su propio aislamiento).
 *
 * @author  DjSteker
 * @date    7 may 2026
 * @version 1.0.0
 */

#ifndef PROCESOS_TOOLPATHGENERATOR_H_
#define PROCESOS_TOOLPATHGENERATOR_H_

#include <vector>

#include "gerber.h"
#include "verificacion_segura.h"

/**
 * @enum   ToolpathMode
 * @brief  Modos de generación de trayectoria CNC disponibles.
 *
 * @details Determina qué algoritmo se ejecutará dentro de
 *          @ref ToolpathGenerator::generate(). Los tres modos son
 *          excluyentes entre sí: cada llamada a @c generate() produce una
 *          única secuencia correspondiente al modo elegido.
 *
 * @warning Estado de implementación: solo @c ProfileCopper genera
 *          movimientos. @c Isolation y @c TracePaths están declarados
 *          pero sus constructores son esqueletos que devuelven un vector
 *          vacío (ver notas en cada valor).
 */
enum class ToolpathMode {
/**
 * @brief Perfila el cobre existente (offsets hacia adentro).
 *
 * @details Parte de cada polígono de cobre (@c Dark) y genera pasadas
 *          concéntricas hacia el interior: contornos reducidos con
 *          @c gerber::geom::offsetPolygon() a distancia
 *          @c radio + pasada * @ref ToolpathConfig::stepover, hasta
 *          @ref ToolpathConfig::maxPasses pasadas o hasta que el
 *          contorno colapsa. Entre contornos se añaden rápidos y al
 *          final un rápido de retorno al origen (0, 0).
 *
 * @note Ignora las primitivas @c Clear; opera únicamente sobre el
 *       cobre real.
 * @note La verificación de seguridad (@c isPointSafe) se aplica siempre
 *       en este modo, con independencia del valor de
 *       @ref ToolpathConfig::useVerification.
 * @warning Con la implementación actual, @c isPointSafe() rechaza todo
 *          punto que caiga dentro del propio cobre (distancia con signo
 *          negativa), que es justo donde trabaja el perfilado. En la
 *          práctica este modo puede devolver 0 movimientos; revisar
 *          antes de usarlo en producción.
 */
ProfileCopper,

/**
 * @brief Aísla las zonas de cobre de los huecos (@c Clear). (Pendiente)
 *
 * @details Objetivo: fresado alrededor de cada zona @c Clear con offset
 *          base @c toolDiameter/2 + @c clearance, usando los polígonos
 *          @c Dark como zonas de exclusión, para separar eléctricamente
 *          los conductores vecinos.
 *
 * @warning No implementado: @c buildIsolationTrajectory() es un
 *          esqueleto que devuelve un vector vacío. El botón de
 *          "aislamiento corregido" de la interfaz usa
 *          @c construirTrayectoriaAislamiento() de Mecanizado/ en su
 *          lugar.
 */
Isolation,

/**
 * @brief Recorre el centro de pistas y arcos sin offset. (Pendiente)
 *
 * @details Objetivo: que la herramienta siga exactamente la línea media
 *          de segmentos y arcos, sin aplicar offset ni verificación,
 *          para repaso de pistas ya fresadas o inspección visual.
 *
 * @warning No implementado: @c buildTraceTrajectory() es un esqueleto
 *          que devuelve un vector vacío.
 */
TracePaths
};

/**
 * @struct ToolpathConfig
 * @brief  Parámetros de configuración del generador de trayectorias.
 *
 * @details Todos los campos se expresan en las mismas unidades que el
 *          archivo Gerber de origen (típicamente milímetros).
 *
 *          Se recomienda construir un @c ToolpathConfig por trabajo y
 *          reutilizarlo mientras no cambien las condiciones de
 *          mecanizado.
 *
 * Ejemplo:
 * @code
 *   ToolpathConfig cfg;
 *   cfg.toolDiameter = 0.2;   // fresa de 0.2 mm
 *   cfg.clearance    = 0.05;  // 50 µm de holgura extra
 *   cfg.stepover     = 0.07;  // solape ~65 %
 *   cfg.maxPasses    = 20;
 * @endcode
 */
struct ToolpathConfig {
/**
 * @brief Diámetro de la fresa/herramienta en milímetros.
 *
 * @note Determina el radio de offset base (@c toolDiameter/2) y la
 *       separación mínima entre pasadas seguras.
 * @note Valor por defecto: @c 0.1 mm (fresa de grabado típica).
 */
double toolDiameter = 0.1;

/**
 * @brief Holgura adicional de seguridad en milímetros.
 *
 * @details Se suma al radio de la herramienta al calcular la distancia
 *          mínima que debe mantenerse respecto a otros conductores. Un
 *          valor @c > 0 evita rozaduras laterales en el cobre vecino.
 *
 * @note Valor por defecto: @c 0.0 mm (sin holgura extra).
 */
double clearance = 0.0;

/**
 * @brief Paso entre pasadas sucesivas (offset incremental), en mm.
 *
 * @details Define cuánto se "adentra" la herramienta en cada pasada
 *          respecto a la anterior. Un @c stepover menor produce más
 *          pasadas y mejor acabado superficial.
 *
 * @note Valor por defecto: @c 0.35 mm. Con la fresa por defecto
 *       (0.1 mm) este paso es mayor que el diámetro: para solapar
 *       pasadas debe ser inferior al diámetro (p. ej. 0.035 mm para
 *       un solape del 65 %). La interfaz lo calcula como 35 % del
 *       diámetro de la herramienta.
 */
double stepover = 0.35;

/**
 * @brief Número máximo de pasadas/offsets progresivos.
 *
 * @details Cota de seguridad para evitar bucles infinitos cuando el
 *          offset se vuelve degenerado (polígono que se autointersecta
 *          o colapsa a un punto).
 *
 * @note Valor por defecto: @c 15 pasadas.
 */
int maxPasses = 15;

/**
 * @brief Activa la ordenación de movimientos por vecino más cercano.
 *
 * @warning Sin efecto actualmente: @c optimizeOrder() es un
 *          *placeholder* que devuelve la secuencia sin cambios.
 * @note Valor por defecto: @c true.
 */
bool optimizeOrder = true;

/**
 * @brief Reservado para activar la verificación de colisiones.
 *
 * @warning Sin efecto actualmente: la bandera nunca se consulta; la
 *          verificación con @c isPointSafe() se aplica siempre que el
 *          algoritmo la usa, esté activa o no.
 * @note Valor por defecto: @c true.
 */
bool useVerification = true;
};

/**
 * @struct ToolpathMove
 * @brief  Movimiento elemental de la herramienta CNC.
 *
 * @details Es la unidad mínima que produce @ref ToolpathGenerator. Cada
 *          instancia representa un tramo recto entre @ref from y @ref to.
 *          La conversión a G-code añade el eje Z y los códigos modales
 *          correspondientes (@c G00 para rápidos, @c G01 para cortes).
 *
 * @note Este struct no incluye Z: el generador de G-code decide si el
 *       movimiento ocurre con la herramienta arriba o abajo según el
 *       campo @ref rapid.
 */
struct ToolpathMove {
/**
 * @brief @c true si es un desplazamiento rápido (G00);
 *        @c false si es un corte (G01).
 */
bool rapid;

/**
 * @brief Punto de partida del tramo (coordenadas de trabajo).
 */
gerber::Vec2 from;

/**
 * @brief Punto de llegada del tramo (coordenadas de trabajo).
 */
gerber::Vec2 to;
};

/**
 * @class ToolpathGenerator
 * @brief  Genera trayectorias CNC a partir de un resultado Gerber.
 *
 * @details La clase es sin estado entre llamadas a @ref generate():
 *          toda la configuración vive en @ref cfg_ y se copia en el
 *          constructor. Esto permite:
 *          - Instanciar varios generadores con configuraciones distintas.
 *          - Reutilizar un mismo generador para varios @c GerberResult
 *            sin efectos colaterales.
 *          - Llamar a @ref generate() de forma segura desde distintos
 *            hilos siempre que no compartan el mismo objeto
 *            @c GerberResult.
 *
 * Flujo típico:
 * @code
 *   ToolpathConfig cfg;
 *   cfg.toolDiameter = 0.2;
 *   cfg.stepover     = 0.07;
 *
 *   ToolpathGenerator gen(cfg);
 *   auto moves = gen.generate(result, ToolpathMode::ProfileCopper);
 *   // 'moves' contiene la secuencia lista para exportar a G-code
 * @endcode
 *
 * @see ToolpathConfig, ToolpathMode, ToolpathMove
 */
class ToolpathGenerator {
public:
/**
 * @brief Constructor.
 *
 * @param cfg Configuración de mecanizado. Se copia internamente; el
 *            objeto externo puede destruirse sin afectar al generador.
 *
 * @note Si se omite @p cfg se usa @ref ToolpathConfig() con sus
 *       valores por defecto (fresa de 0.1 mm, stepover 0.35 mm,
 *       15 pasadas, optimización y verificación activas).
 */
explicit ToolpathGenerator(const ToolpathConfig &cfg = ToolpathConfig());

/**
 * @brief Genera la trayectoria para el modo indicado.
 *
 * @param result Resultado del parser Gerber. Debe tener
 *               @c result.ok == @c true.
 * @param mode   Modo de generación (ver @ref ToolpathMode y su estado
 *               de implementación).
 *
 * @return Vector de movimientos elementales. Estará **vacío** si
 *         @p result no es válido (@c ok == @c false), si el modo no
 *         tiene implementación (@c Isolation, @c TracePaths) o si no
 *         hay primitivas procesables.
 *
 * @note En @c ProfileCopper, si @ref ToolpathConfig::optimizeOrder está
 *       activo se pasa el resultado por @c optimizeOrder() (hoy sin
 *       efecto). El último movimiento termina en (0, 0) (retorno al
 *       origen).
 */
std::vector<ToolpathMove> generate(const gerber::GerberResult &result, ToolpathMode mode);

private:
/**
 * @brief Configuración activa del generador (copia del constructor).
 *
 * @note No se modifica después de la construcción; se fija una sola
 *       vez.
 */
ToolpathConfig cfg_;

/**
 * @brief Extrae los polígonos correspondientes a primitivas @c Dark.
 *
 * @details Convierte cada primitiva de cobre en un polígono cerrado:
 *  - Segmentos → "cápsulas" (rectángulo + semicírculos).
 *  - Arcos     → tubos curvos (dos contornos concéntricos).
 *  - Flashes   → polígono de su apertura (vía @c flashToPolygon).
 *  - Regiones  → contorno tal cual.
 *
 * @param result Resultado Gerber.
 * @return Lista de polígonos en coordenadas de trabajo.
 *
 * @note El orden de los polígonos preserva el orden de las primitivas
 *       en @p result.
 */
std::vector<std::vector<gerber::Vec2>> extractCopperPolygons(const gerber::GerberResult &result);

/**
 * @brief Extrae los polígonos correspondientes a primitivas @c Clear.
 *
 * @details Mismo proceso que @ref extractCopperPolygons() pero
 *          filtrando solo las primitivas con polaridad @c Clear. Estos
 *          polígonos representan zonas a evitar por la herramienta
 *          (huecos, ventanas).
 *
 * @param result Resultado Gerber.
 * @return Lista de polígonos que deben evitarse.
 */
std::vector<std::vector<gerber::Vec2>> extractClearPolygons(const gerber::GerberResult &result);

/**
 * @brief Construye la trayectoria de perfilado de cobre.
 *
 * @details Para cada polígono de cobre genera offsets sucesivos hacia
 *          el interior (uno por pasada, distancia
 *          @c radio + pasada * stepover) con
 *          @c gerber::geom::offsetPolygon(). Cada contorno se recorre
 *          con movimientos de corte; entre contornos se añaden
 *          rápidos, y al final un rápido al origen (0, 0).
 *
 * @param copperPolys Polígonos de cobre (@c Dark).
 * @param clearPolys  Polígonos a evitar (@c Clear).
 * @return Movimientos de perfilado.
 *
 * @note Cada punto del contorno se valida con @c isPointSafe() antes
 *       de aceptar el contorno; si falla, reintenta con un offset
 *       ligeramente mayor (20 % del stepover extra).
 * @see La advertencia en @ref ToolpathMode::ProfileCopper sobre el
 *      comportamiento de esa validación.
 */
std::vector<ToolpathMove> buildProfileTrajectory(const std::vector<std::vector<gerber::Vec2>> &copperPolys, const std::vector<std::vector<gerber::Vec2>> &clearPolys);

/**
 * @brief Construye la trayectoria de aislamiento. (Pendiente)
 *
 * @details Objetivo: offsets hacia afuera de las zonas @c Clear sin
 *          invadir los polígonos de cobre, dejando un canal de
 *          separación alrededor de cada hueco.
 *
 * @warning Esqueleto sin implementar: devuelve siempre un vector
 *          vacío.
 */
std::vector<ToolpathMove> buildIsolationTrajectory(const std::vector<std::vector<gerber::Vec2>> &copperPolys, const std::vector<std::vector<gerber::Vec2>> &clearPolys);

/**
 * @brief Construye la trayectoria siguiendo la línea media. (Pendiente)
 *
 * @details Objetivo: recorrer segmentos y arcos tal cual, sin aplicar
 *          offset ni verificación, para repaso o inspección.
 *
 * @warning Esqueleto sin implementar: devuelve siempre un vector
 *          vacío.
 */
std::vector<ToolpathMove> buildTraceTrajectory(const gerber::GerberResult &result);

/**
 * @brief Comprueba si un punto (centro de herramienta) es seguro.
 *
 * @details Un punto es seguro si su distancia con signo a cada polígono
 *          de cobre y a cada polígono @c Clear es mayor o igual que
 *          @c toolDiameter/2 + @c clearance. La distancia se calcula
 *          con @c gerber::verificacion::distanciaFirmaPoligono(),
 *          que devuelve negativo si el punto está dentro del polígono
 *          (los puntos interiores nunca son seguros).
 *
 * @param point       Centro de herramienta a comprobar.
 * @param copperPolys Polígonos de cobre.
 * @param clearPolys  Polígonos @c Clear.
 * @param excludePrim Reservado (primitiva a excluir); hoy no se usa.
 * @return @c true si la herramienta no invade cobre ni @c Clear dentro
 *         del radio + holgura.
 */
bool isPointSafe(const gerber::Vec2 &point, const std::vector<std::vector<gerber::Vec2>> &copperPolys, const std::vector<std::vector<gerber::Vec2>> &clearPolys,
const gerber::Primitive *excludePrim = nullptr) const;

/**
 * @brief Reordena los movimientos para minimizar desplazamientos. (Pendiente)
 *
 * @details Objetivo: agrupar los cortes por contorno y elegir el
 *          siguiente contorno según vecino más cercano desde la
 *          posición actual, reduciendo los rápidos.
 *
 * @warning *Placeholder*: devuelve la secuencia de entrada sin
 *          cambios.
 */
std::vector<ToolpathMove> optimizeOrder(const std::vector<ToolpathMove> &moves);

/**
 * @brief Convierte un polígono cerrado en una secuencia de cortes. (Pendiente)
 *
 * @details Objetivo: rápido inicial hasta el primer vértice, cortes
 *          entre vértices consecutivos y cierre del contorno,
 *          actualizando la posición actual.
 *
 * @warning Solo está declarado: no tiene implementación en el .cpp y
 *          no se usa (el perfilado genera los cortes directamente).
 *          Puede eliminarse o implementarse.
 */
std::vector<ToolpathMove> polygonToMoves(const std::vector<gerber::Vec2> &poly, gerber::Vec2 &currentPos);
};

#endif /* PROCESOS_TOOLPATHGENERATOR_H_ */
