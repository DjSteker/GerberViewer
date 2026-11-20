/*
 * BaseDatos/DatabaseMaterial.h
 *
 *  Created on: 18 abr 2026
 *      Author: DjSteker
 */

/**
 * @file BaseDatos/DatabaseMaterial.h
 * @brief Persistencia de materiales de trabajo en un archivo XML.
 *
 * @details
 * Este módulo gestiona la base de datos de materiales de trabajo de la
 * aplicación. Cada material se guarda como un elemento `<Material>` dentro
 * del archivo `Datos/BaseDatos/DB_Materiales.xml` (relativo al directorio
 * de trabajo del proceso). El archivo se crea con un elemento raíz vacío
 * `<Materiales>` la primera vez que se necesita.
 *
 * Toda la clase es estática: no hay instancias ni estado de objeto; el
 * único estado compartido es la caché en memoria de los materiales leídos
 * del XML, que se carga bajo demanda en la primera consulta.
 *
 * ### Quién lo utiliza
 * - `Interfaces/ui_materials.cpp`: interfaz GTK de gestión de materiales
 *   (listar, agregar, eliminar).
 * - Cualquier módulo que necesite las propiedades físicas de un material
 *   para calcular parámetros de mecanizado.
 *
 * ### Advertencias sobre el código actual
 * - Los cambios se guardan siempre en el XML con `std::filesystem::rename`
 *   a través de tinyxml2; no hay transaccionalidad: si el proceso muere
 *   durante un `SaveFile`, el archivo puede quedar parcialmente escrito.
 * - El orden de los materiales en `ObtenerTodos()` es el del archivo XML,
 *   no alfabético.
 * - Los materiales almacenados solo conservan `id`, `nombre` y `tipo`;
 *   el resto de campos de `MaterialProps` no se escriben en el XML ni se
 *   leen de él, así que siempre valen 0 o cadena vacía al recargar. Está
 *   pendiente ampliar la serialización a todos los campos.
 * - `ObtenerPorID()` usa una caché que solo se refresca con
 *   `InvalidarCache()`.
 */

#ifndef BASEDATOS_DATABASEMATERIAL_H_
#define BASEDATOS_DATABASEMATERIAL_H_

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <unordered_map>
#include <tinyxml2.h>

namespace fs = std::filesystem;

/**
 * @brief Propiedades físicas y de mecanizado de un material de trabajo.
 *
 * @details
 * Agrupa la identificación del material con sus propiedades mecánicas,
 * térmicas, ópticas, eléctricas y de uso. Solo `id`, `nombre` y `tipo`
 * tienen persistencia XML en la implementación actual; el resto se usa
 * en memoria y no se conserva entre ejecuciones.
 *
 * Quién la utiliza: la interfaz de materiales para editar y listar, y los
 * cálculos de mecanizado que dependen de las propiedades del material.
 */
struct MaterialProps {
	std::string id;                 ///< Identificador único del material (clave en el XML y en la caché).
	std::string nombre;             ///< Nombre descriptivo visible en la interfaz.
	std::string tipo;               ///< Tipo o familia del material (por ejemplo, un metal o un plástico).

	double dureza;                  ///< Dureza del material (escala según el estándar usado).
	double resistenciaTraccion;     ///< Resistencia a la tracción (MPa).
	double limiteElasticidad;       ///< Límite elástico (MPa).
	double coefFriccion;            ///< Coeficiente de fricción con la herramienta (adimensional).
	double trabajabilidad;           ///< Índice de trabajabilidad; mayor valor indica un mecanizado más fácil.
	bool endurecimientoPorTrabajo;  ///< True si el material se endurece por deformación durante el corte.

	double puntoFusion;             ///< Temperatura de fusión (°C).
	double puntoEbullicion;         ///< Temperatura de ebullición (°C).
	double calorEspecifico;         ///< Calor específico (J/kg·K).
	double conductividadTermica;    ///< Conductividad térmica (W/m·K).
	double difusividadTermica;      ///< Difusividad térmica (m²/s).
	double coefExpansionTermica;     ///< Coeficiente de expansión térmica (1/K).

	double emisividad;              ///< Emisividad térmica superficial (0 a 1).
	double reflectividad;           ///< Reflectividad óptica superficial (0 a 1).
	double absortividad;            ///< Absortividad óptica superficial (0 a 1).
	double indiceRefraction;        ///< Índice de refracción de la superficie.
	double permitividadRelativa;    ///< Permitividad eléctrica relativa.
	double permeabilidadRelativa;    ///< Permeabilidad magnética relativa.
	double conductividadElectrica;   ///< Conductividad eléctrica (S/m).
	double freqResonanciaPlasma;     ///< Frecuencia de resonancia del plasma (Hz).

	std::string colorSuperficie;    ///< Color de la superficie, relevante para la absorción de energía.
	double tempMaxEmision;           ///< Temperatura máxima de emisión útil (°C).

	double abrasividad;              ///< Abrasividad del material frente a la herramienta.
	bool tendenciaAdhesion;          ///< True si tiende a adherirse a la herramienta durante el corte.

	std::string proveedor;           ///< Proveedor del material.
	std::string observaciones;       ///< Notas libres sobre el material.
	std::string fechaActualizacion;  ///< Fecha de la última actualización de la ficha.
};

/**
 * @brief Base de datos de materiales respaldada en XML, con caché en memoria.
 *
 * @details
 * Clase de utilidades estáticas. Las operaciones de lectura
 * (`ObtenerTodos`, `ObtenerPorID`) consultan el archivo XML y, en el caso
 * de `ObtenerPorID`, una caché en memoria. Las operaciones de escritura
 * (`Agregar`, `Modificar`, `Eliminar`) actualizan el XML y la caché.
 *
 * Las escrituras serializan el XML completo en cada operación, así que no
 * conviene usarla para actualizaciones masivas de alto ritmo; está pensada
 * para el volumen bajo de una interfaz de gestión manual.
 *
 * Quién la utiliza: la interfaz de materiales (`ui_materials`).
 */
class DatabaseMaterial {
private:
	/// Ruta del archivo XML relativa al directorio de trabajo del proceso.
	static inline const std::string relativePath = "/Datos/BaseDatos/DB_Materiales.xml";

	// Helpers XML

	/**
	 * @brief Ruta absoluta del archivo XML de materiales.
	 * @return Ruta completa: directorio de trabajo actual + relativePath.
	 */
	static std::string getFullPath();

	/**
	 * @brief Crea el directorio `Datos/BaseDatos/` y el XML vacío si no existen.
	 */
	static void CrearArchivoSiNoExiste();

	/**
	 * @brief Añade un subelemento de texto a un elemento XML.
	 * @param doc     Documento propietario de los nodos.
	 * @param parent  Elemento dentro del cual se añade el nuevo nodo.
	 * @param tag     Nombre de la etiqueta del subelemento.
	 * @param val     Valor de texto del subelemento.
	 */
	static void AgregarNodo(tinyxml2::XMLDocument&, tinyxml2::XMLElement*, const std::string&, const std::string&);

	/**
	 * @brief Añade un subelemento numérico a un elemento XML.
	 * @param doc     Documento propietario de los nodos.
	 * @param parent  Elemento dentro del cual se añade el nuevo nodo.
	 * @param tag     Nombre de la etiqueta del subelemento.
	 * @param val     Valor numérico del subelemento.
	 */
	static void AgregarNodo(tinyxml2::XMLDocument&, tinyxml2::XMLElement*, const std::string&, double);

	/**
	 * @brief Lee el texto de un subelemento con valor por defecto.
	 * @param p     Elemento XML padre.
	 * @param tag   Nombre del subelemento a leer.
	 * @param def   Valor devuelto si el subelemento no existe o no tiene texto.
	 * @return Texto leído o def.
	 */
	static std::string GetTextoSeguro(tinyxml2::XMLElement*, const std::string&, const std::string& = "");

	/**
	 * @brief Lee un número de un subelemento con valor por defecto.
	 * @param p     Elemento XML padre.
	 * @param tag   Nombre del subelemento a leer.
	 * @param def   Valor devuelto si el subelemento no existe o no es numérico.
	 * @return Valor leído o def.
	 */
	static double GetDoubleSeguro(tinyxml2::XMLElement*, const std::string&, double = 0.0);

	// Operaciones XML internas

	/**
	 * @brief Añade un `<Material>` al final del XML.
	 * @warning Solo serializa Id, Nombre y Tipo; el resto de campos se pierden.
	 * @param item Material a añadir.
	 * @return true si se escribió el archivo con éxito.
	 */
	static bool AgregarXML(const MaterialProps&);

	/**
	 * @brief Modifica el Nombre de un `<Material>` existente por Id.
	 * @warning Solo actualiza el Nombre; el resto de campos no se tocan.
	 * @param item Material con el Id buscado y el nuevo Nombre.
	 * @return true si se encontró y se guardó el cambio.
	 */
	static bool ModificarXML(const MaterialProps&);

	/**
	 * @brief Elimina el `<Material>` cuyo Id coincide.
	 * @param id Identificador del material a eliminar.
	 * @return true si se encontró, se borró y se guardó el archivo.
	 */
	static bool EliminarXML(const std::string&);

	// Caché

	static std::unordered_map<std::string, MaterialProps> cache;  ///< Materiales indexados por Id.
	static bool cacheCargada;  ///< Indica si la caché ya se ha cargado desde el XML.

	/**
	 * @brief Carga la caché desde el XML la primera vez que se necesita.
	 * @details Si la caché ya estaba cargada, no hace nada.
	 */
	static void CargarCache();

public:
	/**
	 * @brief Devuelve todos los materiales del XML, en el orden del archivo.
	 * @details Crea el archivo si no existe. Si el XML no se puede cargar,
	 * devuelve una lista vacía.
	 * @return Vector con todos los materiales.
	 */
	static std::vector<MaterialProps> ObtenerTodos();

	/**
	 * @brief Busca un material por su Id usando la caché en memoria.
	 * @details La primera llamada carga la caché desde el XML; los cambios
	 * externos al archivo no se ven hasta llamar a `InvalidarCache()`.
	 * @param id   Identificador del material buscado.
	 * @param out  Material encontrado; queda sin modificar si no se encuentra.
	 * @return true si se encontró el material.
	 */
	static bool ObtenerPorID(const std::string &id, MaterialProps &out);

	/**
	 * @brief Añade un material nuevo al XML y a la caché.
	 * @details Rechaza el alta si ya existe otro material con el mismo Id.
	 * @warning Solo persiste Id, Nombre y Tipo.
	 * @param item Material a añadir.
	 * @return true si se añadió; false si el Id ya existía o falló la escritura.
	 */
	static bool Agregar(const MaterialProps &item);

	/**
	 * @brief Modifica el Nombre de un material existente.
	 * @warning Solo actualiza el campo Nombre del XML.
	 * @param item Material con el Id buscado y el nuevo Nombre.
	 * @return true si se encontró y se guardó el cambio.
	 */
	static bool Modificar(const MaterialProps &item);

	/**
	 * @brief Elimina un material del XML y de la caché.
	 * @param id Identificador del material a eliminar.
	 * @return true si se eliminó; false si no existía o falló la escritura.
	 */
	static bool Eliminar(const std::string &id);

	/**
	 * @brief Marca la caché como no cargada para que se relea del XML.
	 * @details Útil tras modificar el archivo desde fuera de la aplicación
	 * o para forzar una recarga completa.
	 */
	static void InvalidarCache();
};

#endif /* BASEDATOS_DATABASEMATERIAL_H_ */

