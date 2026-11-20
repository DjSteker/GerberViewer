/*
 * Procesos/drill.h
 *
 *  Created on: 14 abr 2026
 *      Author: DjSteker
 */

/**
 * @file    drill.h
 * @brief   Parser de archivos de taladrado Excellon (.drl / .xln).
 *
 * @details Librería independiente del Gerber: lee un archivo de taladro
 *          en formato Excellon (el que genera KiCad) y produce una lista
 *          de operaciones de taladro con su posición, diámetro y
 *          herramienta. Ese resultado alimenta el visor y el generador de
 *          GCode de taladrado.
 *
 * @par     Proceso general
 *          1. @c DrillParser::parseFile() recorre el archivo línea a
 *             línea: detecta las unidades (METRIC/INCH), registra las
 *             herramientas con su diámetro (líneas "T1C0.600") y genera
 *             una @c DrillOperation por cada línea de coordenadas
 *             "X..Y..", asignándole el diámetro de la herramienta
 *             seleccionada en ese momento.
 *          2. El @c DrillResult viaja a la interfaz, que dibuja los
 *             taladros, y a @c generarGCodeTaladro() (@c Mecanizado),
 *             que agrupa por diámetro y emite el programa CNC.
 *
 * @par     Quién lo utiliza
 *          - @c Nucleo/interfaz.h: @c EstadoApp guarda
 *            @c resultadoTaladro y lo rellena el botón "Taladros".
 *          - @c Nucleo/interfaz.cpp: dibuja la capa de taladros y su
 *            recorrido, y exporta el GCode de taladrado.
 *          - @c Mecanizado/mecanizado.h: incluye esta cabecera para el
 *            tipo @c DrillResult de @c generarGCodeTaladro().
 *
 * @note    Unidades: @c DrillResult::unit indica "MM" o "IN"; las
 *          coordenadas de las operaciones están en esa unidad.
 * @note    Sin dependencias externas: solo STL de C++17.
 *
 * @author  DjSteker
 * @date    14 abr 2026
 */

#ifndef DRILL_H_
#define DRILL_H_

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cmath>
#include <optional>
#include <unordered_map>

namespace drill {

/**
 * @struct DrillOperation
 * @brief  Un agujero de taladro leído del archivo.
 *
 * @details Cada línea de coordenadas del Excellon produce una de estas
 *          operaciones, con el diámetro de la herramienta vigente en el
 *          momento de leerla.
 */
struct DrillOperation {
    double x = 0.0;         ///< Coordenada X del centro del agujero.
    double y = 0.0;         ///< Coordenada Y del centro del agujero.
    double diameter = 0.0;  ///< Diámetro de la broca (mm o pulgadas según @c unit).
    double depth = 0.0;     ///< Profundidad del agujero. Hoy no se usa: el GCode de taladro corta a @c GCodeConfig::cutZ.
    int toolNumber = 0;     ///< Número de herramienta (Tnn) que lo taladra.
    std::string unit;       ///< Unidades de esta operación ("MM" o "IN"), copia de la del archivo.
  	bool ranura = false;     ///< true = ranura G85
  	double x2 = 0.0;         ///< Extremo final de la ranura
  	double y2 = 0.0;
};

/**
 * @struct DrillResult
 * @brief  Resultado completo del parseo de un archivo de taladro.
 *
 * @details Es el equivalente a @c gerber::GerberResult para la capa de
 *          taladrado: todas las operaciones leídas, las unidades del
 *          archivo y el estado del parseo.
 */
struct DrillResult {
    std::vector<DrillOperation> operations;  ///< Operaciones de taladro en orden de aparición.
    std::string unit = "MM";                 ///< Unidades del archivo ("MM" o "IN").
    bool ok = false;                         ///< @c true si el parseo encontró al menos un taladro.
    std::string error;                       ///< Mensaje de error cuando @c ok es @c false.
};

/**
 * @class DrillParser
 * @brief  Parser del formato Excellon (archivos .drl de KiCad).
 *
 * @details El parseo real ocurre en @c parseFile(), que recorre el
 *          archivo línea a línea:
 *          - "METRIC"/"INCH" fijan las unidades.
 *          - "TnnCdia" registra el diámetro de cada herramienta.
 *          - "Tnn" selecciona la herramienta vigente.
 *          - Líneas con X e Y generan una operación con el diámetro
 *            vigente.
 *          - Comentarios (';'), cabecera (M48, FMAT, G90, G05) y pie
 *            (M30) se ignoran.
 *
 * @note    Fuerza el locale clásico al empezar para que las conversiones
 *          numéricas usen el punto decimal independientemente del sistema.
 */
class DrillParser {
public:
    /**
     * @brief   Parsea un archivo Excellon desde disco.
     * @param   path Ruta del archivo .drl o .xln.
     * @return  Resultado con las operaciones y el estado (@c ok / @c error).
     *          Si el archivo no se puede abrir o no contiene taladros,
     *          @c ok queda a @c false con el mensaje correspondiente.
     */
    DrillResult parseFile(const std::string &path);

    /**
     * @brief   Parsea el contenido de un archivo desde una cadena.
     * @warning No implementado: siempre devuelve un resultado con
     *          @c ok a @c false y el error "parseString no implementado;
     *          use parseFile". Existe por simetría con
     *          @c gerber::GerberParser::parseString().
     * @param   src Contenido completo del archivo.
     * @return  Resultado vacío con error.
     */
    DrillResult parseString(const std::string &src);

private:
    DrillResult result_;      ///< Resultado que se va construyendo y se devuelve por copia.
    std::string unit_ = "MM"; ///< Unidades vigentes durante el parseo ("MM" por defecto).

    /**
     * @brief   Procesa una línea del archivo. (Obsoleto)
     * @warning Método legado: el parseo real lo hace @c parseFile()
     *          inline. Cuerpo vacío; se conserva por compatibilidad.
     */
    void processLine(const std::string &line, std::vector<DrillOperation> &ops);

    /**
     * @brief   Detecta las unidades en la cabecera. (Obsoleto)
     * @warning Método legado sin consumidores: @c parseFile() detecta
     *          "METRIC"/"INCH" directamente. Solo mapea M48→MM y M72→IN.
     */
    void processHeader(const std::string &line);

    /**
     * @brief   Procesa el pie del archivo. (Obsoleto)
     * @warning Método legado con cuerpo vacío; no hace nada.
     */
    void processFooter(const std::string &line);

    /**
     * @brief   Convierte una coordenada textual a número. (Obsoleto)
     * @warning Método legado: @c parseFile() usa @c std::strtod
     *          directamente. Envuelve @c std::stod y devuelve 0.0 si falla.
     */
    double parseCoordinate(const std::string &coord);

    /**
     * @brief   Convierte un diámetro textual a número. (Obsoleto)
     * @warning Método legado: @c parseFile() usa @c std::strtod.
     *          Devuelve 0.3 (mm por defecto) si la cadena es vacía o
     *          inválida.
     */
    double parseDiameter(const std::string &dia);

    /**
     * @brief   Convierte una profundidad textual a número. (Obsoleto)
     * @warning Método legado sin consumidores. Devuelve -0.1 por defecto.
     */
    double parseDepth(const std::string &dep);
};

/**
 * @brief   Conversiones de unidades entre milímetros y pulgadas.
 *
 * @details Utilidades sueltas de apoyo; hoy no tienen consumidores en el
 *          proyecto, pero quedan disponibles para normalizar archivos en
 *          pulgadas frente a configuraciones en mm.
 */
namespace utils {

/**
 * @brief   Convierte milímetros a pulgadas.
 * @param   mm Valor en milímetros.
 * @return  Valor equivalente en pulgadas (mm / 25.4).
 */
double mmToInch(double mm);

/**
 * @brief   Convierte pulgadas a milímetros.
 * @param   inch Valor en pulgadas.
 * @return  Valor equivalente en milímetros (inch * 25.4).
 */
double inchToMm(double inch);

}  // namespace utils

}  // namespace drill

#endif /* DRILL_H_ */
