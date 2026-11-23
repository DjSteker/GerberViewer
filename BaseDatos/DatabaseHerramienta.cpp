/*
 * BaseDatos/DatabaseHerramienta.cpp
 *
 *  Created on: 14 abr 2026
 *      Author: DjSteker
 */

#include "DatabaseHerramienta.h"
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

std::string DatabaseHerramienta::getFullPath() {
	return fs::current_path().string() + relativePath;
}

void DatabaseHerramienta::CrearArchivoSiNoExiste() {
	std::string path = getFullPath();
	fs::path dir = fs::path(path).parent_path();

	if (!fs::exists(dir)) {
		fs::create_directories(dir);
	}

	if (!fs::exists(path)) {
		tinyxml2::XMLDocument doc;
		auto *decl = doc.NewDeclaration();
		doc.InsertFirstChild(decl);
		auto *root = doc.NewElement("Herramientas");
		doc.InsertEndChild(root);

		if (doc.SaveFile(path.c_str()) != tinyxml2::XML_SUCCESS) {
			std::cerr << "Error: No se pudo crear el archivo XML de herramientas.\n";
		}
	}
}

// Helpers para agregar nodos
void DatabaseHerramienta::AgregarNodo(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent, const std::string &tag, const std::string &valor) {
	tinyxml2::XMLElement *node = doc.NewElement(tag.c_str());
	node->SetText(valor.c_str());
	parent->InsertEndChild(node);
}

void DatabaseHerramienta::AgregarNodo(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent, const std::string &tag, double valor) {
	tinyxml2::XMLElement *node = doc.NewElement(tag.c_str());
	node->SetText(valor);
	parent->InsertEndChild(node);
}

void DatabaseHerramienta::AgregarNodo(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent, const std::string &tag, int valor) {
	tinyxml2::XMLElement *node = doc.NewElement(tag.c_str());
	node->SetText(valor);
	parent->InsertEndChild(node);
}

void DatabaseHerramienta::AgregarNodo(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent, const std::string &tag, bool valor) {
	tinyxml2::XMLElement *node = doc.NewElement(tag.c_str());
	node->SetText(valor ? "true" : "false");
	parent->InsertEndChild(node);
}

// Helpers para leer nodos
std::string DatabaseHerramienta::GetTextoSeguro(tinyxml2::XMLElement *parent, const std::string &tag, const std::string &def) {
	tinyxml2::XMLElement *child = parent->FirstChildElement(tag.c_str());
	return (child && child->GetText()) ? child->GetText() : def;
}

double DatabaseHerramienta::GetDoubleSeguro(tinyxml2::XMLElement *parent, const std::string &tag, double def) {
	tinyxml2::XMLElement *child = parent->FirstChildElement(tag.c_str());
	double val = def;
	if (child && child->GetText())
		child->QueryDoubleText(&val);
	return val;
}

int DatabaseHerramienta::GetIntSeguro(tinyxml2::XMLElement *parent, const std::string &tag, int def) {
	tinyxml2::XMLElement *child = parent->FirstChildElement(tag.c_str());
	int val = def;
	if (child && child->GetText())
		child->QueryIntText(&val);
	return val;
}

bool DatabaseHerramienta::GetBoolSeguro(tinyxml2::XMLElement *parent, const std::string &tag, bool def) {
	tinyxml2::XMLElement *child = parent->FirstChildElement(tag.c_str());
	if (child && child->GetText()) {
		return std::string(child->GetText()) == "true";
	}
	return def;
}

std::vector<HerramientaProps> DatabaseHerramienta::ObtenerTodos() {
	CrearArchivoSiNoExiste();
	std::string path = getFullPath();
	std::vector<HerramientaProps> lista;

	tinyxml2::XMLDocument doc;
	if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) {
		std::cerr << "Error: No se pudo cargar DB_Herramientas.xml\n";
		return lista;
	}

	tinyxml2::XMLElement *root = doc.RootElement();
	for (tinyxml2::XMLElement *elem = root->FirstChildElement("Herramienta"); elem; elem = elem->NextSiblingElement("Herramienta")) {

		HerramientaProps item;
		item.id = GetTextoSeguro(elem, "Id");
		item.fabricante = GetTextoSeguro(elem, "Fabricante");
		item.modelo = GetTextoSeguro(elem, "Modelo");
		item.material = GetTextoSeguro(elem, "Material");
		item.costo = GetDoubleSeguro(elem, "Costo");

		item.diametroCorte = GetDoubleSeguro(elem, "DiametroCorte");
		item.diametroMango = GetDoubleSeguro(elem, "DiametroMango");
		item.longitudCorte = GetDoubleSeguro(elem, "LongitudCorte");
		item.longitudTotal = GetDoubleSeguro(elem, "LongitudTotal");

		item.numFlautas = GetIntSeguro(elem, "NumFlautas");
		item.tipoPunta = GetTextoSeguro(elem, "TipoPunta");
		item.direccionHelice = GetTextoSeguro(elem, "DireccionHelice");
		item.esBroca = GetBoolSeguro(elem, "EsBroca");
		item.anguloPunta = GetDoubleSeguro(elem, "AnguloPunta");
		item.tipoBroca = GetTextoSeguro(elem, "TipoBroca");

		item.rpm = GetDoubleSeguro(elem, "RPM");
		item.feedRate = GetDoubleSeguro(elem, "FeedRate");
		item.chipload = GetDoubleSeguro(elem, "Chipload");
		item.stepdown = GetDoubleSeguro(elem, "Stepdown");
		item.stepover = GetDoubleSeguro(elem, "Stepover");

		item.horasUso = GetDoubleSeguro(elem, "HorasUso");
		item.ubicacion = GetTextoSeguro(elem, "Ubicacion");
		item.observaciones = GetTextoSeguro(elem, "Observaciones");

		lista.push_back(item);
	}
	return lista;
}

bool DatabaseHerramienta::Agregar(const HerramientaProps &item) {
	CrearArchivoSiNoExiste();
	std::string path = getFullPath();
	tinyxml2::XMLDocument doc;

	if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS)
		return false;

	tinyxml2::XMLElement *root = doc.RootElement();
	tinyxml2::XMLElement *nuevo = doc.NewElement("Herramienta");

	AgregarNodo(doc, nuevo, "Id", item.id);
	AgregarNodo(doc, nuevo, "Fabricante", item.fabricante);
	AgregarNodo(doc, nuevo, "Modelo", item.modelo);
	AgregarNodo(doc, nuevo, "Material", item.material);
	AgregarNodo(doc, nuevo, "Costo", item.costo);

	AgregarNodo(doc, nuevo, "DiametroCorte", item.diametroCorte);
	AgregarNodo(doc, nuevo, "DiametroMango", item.diametroMango);
	AgregarNodo(doc, nuevo, "LongitudCorte", item.longitudCorte);
	AgregarNodo(doc, nuevo, "LongitudTotal", item.longitudTotal);

	AgregarNodo(doc, nuevo, "NumFlautas", item.numFlautas);
	AgregarNodo(doc, nuevo, "TipoPunta", item.tipoPunta);
	AgregarNodo(doc, nuevo, "DireccionHelice", item.direccionHelice);
	AgregarNodo(doc, nuevo, "EsBroca", item.esBroca);
	AgregarNodo(doc, nuevo, "AnguloPunta", item.anguloPunta);
	AgregarNodo(doc, nuevo, "TipoBroca", item.tipoBroca);

	AgregarNodo(doc, nuevo, "RPM", item.rpm);
	AgregarNodo(doc, nuevo, "FeedRate", item.feedRate);
	AgregarNodo(doc, nuevo, "Chipload", item.chipload);
	AgregarNodo(doc, nuevo, "Stepdown", item.stepdown);
	AgregarNodo(doc, nuevo, "Stepover", item.stepover);

	AgregarNodo(doc, nuevo, "HorasUso", item.horasUso);
	AgregarNodo(doc, nuevo, "Ubicacion", item.ubicacion);
	AgregarNodo(doc, nuevo, "Observaciones", item.observaciones);

	root->InsertEndChild(nuevo);
	return doc.SaveFile(path.c_str()) == tinyxml2::XML_SUCCESS;
}

bool DatabaseHerramienta::Modificar(const HerramientaProps &item) {
	CrearArchivoSiNoExiste();
	std::string path = getFullPath();
	tinyxml2::XMLDocument doc;

	if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS)
		return false;

	tinyxml2::XMLElement *root = doc.RootElement();

	for (tinyxml2::XMLElement *elem = root->FirstChildElement("Herramienta"); elem; elem = elem->NextSiblingElement("Herramienta")) {

		tinyxml2::XMLElement *idElem = elem->FirstChildElement("Id");
		if (idElem && idElem->GetText()) {
			if (std::string(idElem->GetText()) == item.id) {
				// Actualizar campos (simplificado: borra y recrea o actualiza texto)
				// Para simplificar, actualizaremos solo los textos existentes
				auto actualizar = [&](const char *tag, const std::string &val) {
					tinyxml2::XMLElement *child = elem->FirstChildElement(tag);
					if (child)
						child->SetText(val.c_str());
				};
				auto actualizarD = [&](const char *tag, double val) {
					tinyxml2::XMLElement *child = elem->FirstChildElement(tag);
					if (child)
						child->SetText(val);
				};
				auto actualizarI = [&](const char *tag, int val) {
					tinyxml2::XMLElement *child = elem->FirstChildElement(tag);
					if (child)
						child->SetText(val);
				};
				auto actualizarB = [&](const char *tag, bool val) {
					tinyxml2::XMLElement *child = elem->FirstChildElement(tag);
					if (child)
						child->SetText(val ? "true" : "false");
				};

				actualizar("Fabricante", item.fabricante);
				actualizar("Modelo", item.modelo);
				actualizar("Material", item.material);
				actualizarD("Costo", item.costo);
				actualizarD("DiametroCorte", item.diametroCorte);
				actualizarD("DiametroMango", item.diametroMango);
				actualizarD("LongitudCorte", item.longitudCorte);
				actualizarD("LongitudTotal", item.longitudTotal);
				actualizarI("NumFlautas", item.numFlautas);
				actualizar("TipoPunta", item.tipoPunta);
				actualizar("DireccionHelice", item.direccionHelice);
				actualizarB("EsBroca", item.esBroca);
				actualizarD("AnguloPunta", item.anguloPunta);
				actualizar("TipoBroca", item.tipoBroca);
				actualizarD("RPM", item.rpm);
				actualizarD("FeedRate", item.feedRate);
				actualizarD("Chipload", item.chipload);
				actualizarD("Stepdown", item.stepdown);
				actualizarD("Stepover", item.stepover);
				actualizarD("HorasUso", item.horasUso);
				actualizar("Ubicacion", item.ubicacion);
				actualizar("Observaciones", item.observaciones);

				return doc.SaveFile(path.c_str()) == tinyxml2::XML_SUCCESS;
			}
		}
	}
	return false;
}

bool DatabaseHerramienta::Eliminar(const std::string &id) {
	CrearArchivoSiNoExiste();
	std::string path = getFullPath();
	tinyxml2::XMLDocument doc;

	if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) {
		return false;
	}

	tinyxml2::XMLElement *root = doc.RootElement();
	tinyxml2::XMLElement *elemAEliminar = nullptr;

	for (tinyxml2::XMLElement *elem = root->FirstChildElement("Herramienta"); elem; elem = elem->NextSiblingElement("Herramienta")) {
		tinyxml2::XMLElement *idElem = elem->FirstChildElement("Id");
		if (idElem && idElem->GetText()) {
			if (std::string(idElem->GetText()) == id) {
				elemAEliminar = elem;
				break;
			}
		}
	}

	if (!elemAEliminar) {
		return false;
	}

	root->DeleteChild(elemAEliminar);
	return doc.SaveFile(path.c_str()) == tinyxml2::XML_SUCCESS;
}

std::string DatabaseHerramienta::GenerarIDAutomatico() {
	std::time_t now = std::time(nullptr);
	std::stringstream ss;
	ss << "TOOL_" << std::hex << now;
	return ss.str();
}
