/*
 * BaseDatos/DatabaseHerramienta.h
 *
 *  Created on: 14 abr 2026
 *      Author: DjSteker
 */

/**
 * @file BaseDatos/DatabaseHerramienta.h
 * @brief Persistencia de herramientas de corte en un archivo XML.
 *
 * @details
 * Este módulo gestiona la base de datos de herramientas de corte de la
 * aplicación (fresas, brocas y similares). Cada herramienta se guarda como
 * un elemento `<Herramienta>` dentro del archivo
 * `Datos/BaseDatos/DB_Herramientas.xml`, relativo al directorio de trabajo
 * del proceso. El archivo se crea vacío la primera vez que se necesita.
 *
 * A diferencia de la base de datos de materiales, esta clase no tiene
 * caché en memoria: toda operación recarga el XML completo desde el disco.
 * Es simple y consistente entre procesos, pero cada consulta relee el
 * archivo.
 *
 * ### Quién lo utiliza
 * - `Interfaces/ui_tools.cpp`: interfaz GTK de gestión de herramientas
 *   (listar, agregar, modificar, eliminar).
 * - `Interfaces/ui_gcode.cpp` y `docs/parche_on_draw.cpp`: leen la lista
 *   de herramientas para asociarlas a la generación de código G.
 *
 * ### Advertencias sobre el código actual
 * - Cada escritura reescribe el XML completo; no hay bloqueo ni control
 *   de concurrencia entre instancias de la aplicación.
 * - `GenerarIDAutomatico()` basa el Id en la hora actual con precisión de
 *   segundos: dos altas en el mismo segundo reciben el mismo Id.
 */

#ifndef BASEDATOS_DATABASEHERRAMIENTA_H_
#define BASEDATOS_DATABASEHERRAMIENTA_H_

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <tinyxml2.h>

namespace fs = std::filesystem;

/**
 * @brief Ficha completa de una herramienta de corte.
 *
 * @details
 * Agrupa la identificación comercial, las dimensiones físicas, la
 * geometría de corte, los parámetros de corte sugeridos y los datos de
 * gestión de la herramienta. Todos los campos se serializan al XML, así
 * que una herramienta recargada conserva la ficha completa.
 *
 * Quién la utiliza: la interfaz de herramientas para editar y listar, y
 * los módulos que calculan o limitan parámetros de corte según la
 * herramienta seleccionada.
 */
struct HerramientaProps {
	std::string id;          ///< Identificador único de la herramienta (por ejemplo, TOOL_<hex>).
	std::string fabricante;  ///< Fabricante de la herramienta.
	std::string modelo;       ///< Modelo o referencia del fabricante.
	std::string material;     ///< Material de la herramienta (por ejemplo, carburo o HSS).
	double costo;             ///< Coste de adquisición.

	// Dimensiones
	double diametroCorte;   ///< Diámetro de la parte cortante (mm).
	double diametroMango;   ///< Diámetro del mango o vástago (mm).
	double longitudCorte;   ///< Longitud útil de corte (mm).
	double longitudTotal;   ///< Longitud total de la herramienta (mm).

	// Geometría
	int numFlautas;          ///< Número de flautas o cortes.
	std::string tipoPunta;   ///< Tipo de punta (por ejemplo, plana o bola).
	std::string direccionHelice; ///< Sentido de la hélice (derecha o izquierda).
	bool esBroca;             ///< True si la herramienta es una broca.
	double anguloPunta;       ///< Ángulo de punta en grados; relevante sobre todo en brocas.
	std::string tipoBroca;    ///< Subtipo de broca, si aplica.

	// Parámetros de corte sugeridos
	double rpm;       ///< Revoluciones por minuto sugeridas para esta herramienta.
	double feedRate;  ///< Avance lineal sugerido (mm/min).
	double chipload;  ///< Carga por diente o chipload sugerida (mm).
	double stepdown;  ///< Profundidad de pasada axial sugerida (mm).
	double stepover;  ///< Ancho de paso radial sugerido (mm).

	// Gestión
	double horasUso;             ///< Horas de uso acumuladas; sirve para el control de desgaste.
	std::string ubicacion;       ///< Ubicación física de la herramienta (caja, carrusel...).
	std::string observaciones;   ///< Notas libres sobre la herramienta.
};

/**
 * @brief Base de datos de herramientas respaldada en XML, sin caché.
 *
 * @details
 * Clase de utilidades estáticas. Cada operación pública abre el XML, lo
 * modifica y lo vuelve a guardar; no mantiene estado entre llamadas.
 * `GenerarIDAutomatico()` proporciona un Id único-ish para altas nuevas.
 *
 * Quién la utiliza: las interfaces `ui_tools` y `ui_gcode`, y el parche de
 * dibujo que necesita la lista de herramientas.
 */
class DatabaseHerramienta {
private:
	/// Ruta del archivo XML relativa al directorio de trabajo del proceso.
	static inline const std::string relativePath = "/Datos/BaseDatos/DB_Herramientas.xml";

	/**
	 * @brief Ruta absoluta del archivo XML de herramientas.
	 * @return Ruta completa: directorio de trabajo actual + relativePath.
	 */
	static std::string getFullPath();

	/**
	 * @brief Crea el directorio `Datos/BaseDatos/` y el XML vacío si no existen.
	 * @details Escribe un aviso en `std::cerr` si no puede crear el archivo.
	 */
	static void CrearArchivoSiNoExiste();

	/**
	 * @brief Añade un subelemento de texto a un elemento XML.
	 * @param doc     Documento propietario de los nodos.
	 * @param parent  Elemento dentro del cual se añade el nuevo nodo.
	 * @param tag     Nombre de la etiqueta del subelemento.
	 * @param valor   Valor de texto del subelemento.
	 */
	static void AgregarNodo(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent, const std::string &tag, const std::string &valor);

	/**
	 * @brief Añade un subelemento numérico (double) a un elemento XML.
	 * @param doc     Documento propietario de los nodos.
	 * @param parent  Elemento dentro del cual se añade el nuevo nodo.
	 * @param tag     Nombre de la etiqueta del subelemento.
	 * @param valor   Valor numérico del subelemento.
	 */
	static void AgregarNodo(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent, const std::string &tag, double valor);

	/**
	 * @brief Añade un subelemento numérico (int) a un elemento XML.
	 * @param doc     Documento propietario de los nodos.
	 * @param parent  Elemento dentro del cual se añade el nuevo nodo.
	 * @param tag     Nombre de la etiqueta del subelemento.
	 * @param valor   Valor entero del subelemento.
	 */
	static void AgregarNodo(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent, const std::string &tag, int valor);

	/**
	 * @brief Añade un subelemento booleano a un elemento XML.
	 * @details El valor se guarda como el texto "true" o "false".
	 * @param doc     Documento propietario de los nodos.
	 * @param parent  Elemento dentro del cual se añade el nuevo nodo.
	 * @param tag     Nombre de la etiqueta del subelemento.
	 * @param valor   Valor booleano del subelemento.
	 */
	static void AgregarNodo(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent, const std::string &tag, bool valor);

	/**
	 * @brief Lee el texto de un subelemento con valor por defecto.
	 * @param parent  Elemento XML padre.
	 * @param tag     Nombre del subelemento a leer.
	 * @param def     Valor devuelto si el subelemento no existe o no tiene texto.
	 * @return Texto leído o def.
	 */
	static std::string GetTextoSeguro(tinyxml2::XMLElement *parent, const std::string &tag, const std::string &def = "");

	/**
	 * @brief Lee un número (double) de un subelemento con valor por defecto.
	 * @param parent  Elemento XML padre.
	 * @param tag     Nombre del subelemento a leer.
	 * @param def     Valor devuelto si el subelemento no existe o no es numérico.
	 * @return Valor leído o def.
	 */
	static double GetDoubleSeguro(tinyxml2::XMLElement *parent, const std::string &tag, double def = 0.0);

	/**
	 * @brief Lee un número (int) de un subelemento con valor por defecto.
	 * @param parent  Elemento XML padre.
	 * @param tag     Nombre del subelemento a leer.
	 * @param def     Valor devuelto si el subelemento no existe o no es numérico.
	 * @return Valor leído o def.
	 */
	static int GetIntSeguro(tinyxml2::XMLElement *parent, const std::string &tag, int def = 0);

	/**
	 * @brief Lee un booleano de un subelemento con valor por defecto.
	 * @details Reconoce el texto "true"; cualquier otra cosa cuenta como false
	 * cuando el nodo existe pero no es "true".
	 * @param parent  Elemento XML padre.
	 * @param tag     Nombre del subelemento a leer.
	 * @param def     Valor devuelto si el subelemento no existe o no tiene texto.
	 * @return Valor leído o def.
	 */
	static bool GetBoolSeguro(tinyxml2::XMLElement *parent, const std::string &tag, bool def = false);

public:
	/**
	 * @brief Devuelve todas las herramientas del XML, en el orden del archivo.
	 * @details Crea el archivo si no existe. Si el XML no se puede cargar,
	 * escribe un aviso en `std::cerr` y devuelve una lista vacía.
	 * @return Vector con todas las herramientas.
	 */
	static std::vector<HerramientaProps> ObtenerTodos();

	/**
	 * @brief Añade una herramienta nueva al XML.
	 * @details No comprueba si el Id ya existe: dos altas con el mismo Id
	 * conviven en el archivo y `Modificar`/`Eliminar` solo afectan a la
	 * primera coincidencia.
	 * @param item Herramienta a añadir.
	 * @return true si se añadió y se guardó el archivo.
	 */
	static bool Agregar(const HerramientaProps &item);

	/**
	 * @brief Actualiza en el XML todos los campos de una herramienta existente.
	 * @details Localiza el primer `<Herramienta>` con el mismo Id y actualiza
	 * sus subelementos con los valores de item. Si un subelemento no existe
	 * en el XML, se omite en silencio.
	 * @param item Herramienta con el Id buscado y los nuevos valores.
	 * @return true si se encontró y se guardó el cambio.
	 */
	static bool Modificar(const HerramientaProps &item);

	/**
	 * @brief Elimina del XML la primera herramienta cuyo Id coincide.
	 * @param id Identificador de la herramienta a eliminar.
	 * @return true si se encontró, se borró y se guardó el archivo.
	 */
	static bool Eliminar(const std::string &id);

	/**
	 * @brief Genera un Id de herramienta a partir de la hora actual.
	 * @details Produce una cadena con el formato `TOOL_<hora en hexadecimal>`.
	 * @warning Dos llamadas dentro del mismo segundo devuelven el mismo Id;
	 * no es un generador de Ids únicos estricto.
	 * @return Id generado.
	 */
	static std::string GenerarIDAutomatico();
};

#endif /* BASEDATOS_DATABASEHERRAMIENTA_H_ */

