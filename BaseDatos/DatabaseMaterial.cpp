/*
 * BaseDatos/DatabaseMaterial.cpp
 *
 *  Created on: 18 abr 2026
 *      Author: DjSteker
 */

#include "DatabaseMaterial.h"

// ─────────────────────────────────────────────
// Cache
// ─────────────────────────────────────────────
std::unordered_map<std::string, MaterialProps> DatabaseMaterial::cache;
bool DatabaseMaterial::cacheCargada = false;

// ─────────────────────────────────────────────
// PATH
// ─────────────────────────────────────────────
std::string DatabaseMaterial::getFullPath() {
	return fs::current_path().string() + relativePath;
}

void DatabaseMaterial::CrearArchivoSiNoExiste() {
	std::string path = getFullPath();
	fs::path dir = fs::path(path).parent_path();

	if (!fs::exists(dir)) {
		fs::create_directories(dir);
	}

	if (!fs::exists(path)) {
		tinyxml2::XMLDocument doc;
		doc.InsertFirstChild(doc.NewDeclaration());
		doc.InsertEndChild(doc.NewElement("Materiales"));
		doc.SaveFile(path.c_str());
	}
}

// ─────────────────────────────────────────────
// Helpers XML
// ─────────────────────────────────────────────
void DatabaseMaterial::AgregarNodo(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent, const std::string &tag, const std::string &val) {
	auto *n = doc.NewElement(tag.c_str());
	n->SetText(val.c_str());
	parent->InsertEndChild(n);
}

void DatabaseMaterial::AgregarNodo(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent, const std::string &tag, double val) {
	auto *n = doc.NewElement(tag.c_str());
	n->SetText(val);
	parent->InsertEndChild(n);
}

std::string DatabaseMaterial::GetTextoSeguro(tinyxml2::XMLElement *p, const std::string &tag, const std::string &def) {
	auto *e = p->FirstChildElement(tag.c_str());
	return (e && e->GetText()) ? e->GetText() : def;
}

double DatabaseMaterial::GetDoubleSeguro(tinyxml2::XMLElement *p, const std::string &tag, double def) {
	auto *e = p->FirstChildElement(tag.c_str());
	double v = def;
	if (e && e->GetText()) {
		e->QueryDoubleText(&v);
	}
	return v;
}

// ─────────────────────────────────────────────
// CARGA CACHE
// ─────────────────────────────────────────────
void DatabaseMaterial::CargarCache() {
	if (cacheCargada) {
		return;
	}

	cache.clear();
	auto lista = ObtenerTodos();

	for (auto &m : lista) {
		cache[m.id] = m;
	}

	cacheCargada = true;
}

void DatabaseMaterial::InvalidarCache() {
	cacheCargada = false;
}

// ─────────────────────────────────────────────
// LECTURA
// ─────────────────────────────────────────────
std::vector<MaterialProps> DatabaseMaterial::ObtenerTodos() {
	CrearArchivoSiNoExiste();

	std::vector<MaterialProps> lista;
	tinyxml2::XMLDocument doc;

	if (doc.LoadFile(getFullPath().c_str()) != tinyxml2::XML_SUCCESS) {
		return lista;
	}

	auto *root = doc.RootElement();

	for (auto *e = root->FirstChildElement("Material"); e; e = e->NextSiblingElement("Material")) {
		MaterialProps m;

		m.id = GetTextoSeguro(e, "Id");
		m.nombre = GetTextoSeguro(e, "Nombre");
		m.tipo = GetTextoSeguro(e, "Tipo");

		m.dureza = GetDoubleSeguro(e, "Dureza");
		m.resistenciaTraccion = GetDoubleSeguro(e, "ResistenciaTraccion");
		m.limiteElasticidad = GetDoubleSeguro(e, "LimiteElasticidad");
		m.coefFriccion = GetDoubleSeguro(e, "CoefFriccion");
		m.trabajabilidad = GetDoubleSeguro(e, "Trabajabilidad");

		lista.push_back(m);
	}

	return lista;
}

bool DatabaseMaterial::ObtenerPorID(const std::string &id, MaterialProps &out) {
	CargarCache();

	auto it = cache.find(id);
	if (it == cache.end()) {
		return false;
	}

	out = it->second;
	return true;
}

// ─────────────────────────────────────────────
// XML OPERACIONES
// ─────────────────────────────────────────────
bool DatabaseMaterial::AgregarXML(const MaterialProps &item) {
	CrearArchivoSiNoExiste();

	tinyxml2::XMLDocument doc;
	if (doc.LoadFile(getFullPath().c_str()) != tinyxml2::XML_SUCCESS) {
		return false;
	}

	auto *root = doc.RootElement();

	auto *n = doc.NewElement("Material");

	AgregarNodo(doc, n, "Id", item.id);
	AgregarNodo(doc, n, "Nombre", item.nombre);
	AgregarNodo(doc, n, "Tipo", item.tipo);

	root->InsertEndChild(n);

	return doc.SaveFile(getFullPath().c_str()) == tinyxml2::XML_SUCCESS;
}

bool DatabaseMaterial::ModificarXML(const MaterialProps &item) {
	tinyxml2::XMLDocument doc;

	if (doc.LoadFile(getFullPath().c_str()) != tinyxml2::XML_SUCCESS) {
		return false;
	}

	auto *root = doc.RootElement();

	for (auto *e = root->FirstChildElement("Material"); e; e = e->NextSiblingElement("Material")) {
		auto *id = e->FirstChildElement("Id");

		if (id && id->GetText() == item.id) {
			e->FirstChildElement("Nombre")->SetText(item.nombre.c_str());
			return doc.SaveFile(getFullPath().c_str()) == tinyxml2::XML_SUCCESS;
		}
	}

	return false;
}

bool DatabaseMaterial::EliminarXML(const std::string &id) {
	tinyxml2::XMLDocument doc;

	if (doc.LoadFile(getFullPath().c_str()) != tinyxml2::XML_SUCCESS)
		return false;

	auto *root = doc.RootElement();

	for (auto *e = root->FirstChildElement("Material"); e; e = e->NextSiblingElement("Material")) {
		auto *idElem = e->FirstChildElement("Id");

		if (idElem && idElem->GetText() == id) {
			root->DeleteChild(e);
			return doc.SaveFile(getFullPath().c_str()) == tinyxml2::XML_SUCCESS;
		}
	}

	return false;
}

// ─────────────────────────────────────────────
// API PUBLICA
// ─────────────────────────────────────────────
bool DatabaseMaterial::Agregar(const MaterialProps &item) {
	CargarCache();

	if (cache.find(item.id) != cache.end()) {
		return false;
	}

	if (!AgregarXML(item)) {
		return false;
	}

	cache[item.id] = item;
	return true;
}

bool DatabaseMaterial::Modificar(const MaterialProps &item) {
	if (!ModificarXML(item)) {
		return false;
	}

	cache[item.id] = item;
	return true;
}

bool DatabaseMaterial::Eliminar(const std::string &id) {
	if (!EliminarXML(id)) {
		return false;
	}

	cache.erase(id);
	return true;
}
