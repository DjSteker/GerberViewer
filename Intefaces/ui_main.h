/*
 * Intefaces/ui_main.h
 *
 *  Created on: 13 abr 2026
 *      Author: DjSteker
 */

/**
 * @file Interfaces/ui_main.h
 * @brief Ventana principal: menú de acceso a los módulos de la aplicación.
 *
 * @details
 * Define `MainWindow`, la ventana de entrada de la interfaz GTK4. Presenta
 * un menú con cuatro botones: gestión de materiales, gestor de
 * herramientas, generación de G-Code y perfiles de trabajo. Cada botón
 * abre la ventana de su módulo correspondiente.
 *
 * ### Quién lo utiliza
 * - `Nucleo/main.cpp` (vía `Nucleo/interfaz.cpp`): crea la ventana
 *   principal y arranca el bucle de eventos GTK.
 * - `ui_main.cpp` incluye aquí las ventanas hijas (`ui_materials`,
 *   `ui_tools`, `ui_gcode`).
 *
 * ### Advertencias sobre el código actual
 * - El botón "Perfiles de Trabajo" no abre ninguna ventana: solo
 *   imprime un mensaje en la salida estándar. La funcionalidad está
 *   pendiente.
 * - Las ventanas hijas se crean con `new` y solo se liberan cuando el
 *   usuario cierra su ventana; el programa no limita cuántas copias
 *   abiertas puede haber.
 */

#ifndef INTERFACES_UI_MAIN_H_
#define INTERFACES_UI_MAIN_H_

#include <gtk/gtk.h>

/// Declaración anticipada de la ventana de materiales (la usa un callback).
class MaterialWindow;

/**
 * @brief Ventana principal con el menú de módulos de la aplicación.
 *
 * @details
 * Construye la ventana GTK4 con una rejilla de cuatro botones y conecta
 * cada uno a su callback estático. La ventana se autodestruye (delete de
 * la instancia C++) cuando GTK emite la señal "destroy".
 */
class MainWindow {
private:
	GtkWidget *grid;             ///< Rejilla que ordena los botones del menú.
	GtkWidget *btn_materiales;   ///< Botón que abre la gestión de materiales.
	GtkWidget *btn_herramientas; ///< Botón que abre el gestor de herramientas.
	GtkWidget *btn_gcode;        ///< Botón que abre la generación de G-Code.
	GtkWidget *btn_perfiles;     ///< Botón de perfiles de trabajo (sin implementar).

public:
	GtkWidget *window;  ///< Ventana GTK4 de este objeto.

	/**
	 * @brief Construye la ventana y todos sus widgets.
	 * @details Crea la ventana con título "Gerber Manager - Menú
	 * Principal" (400x300), la rejilla de botones y conecta las señales.
	 */
	MainWindow();

	/**
	 * @brief Libera los recursos de la ventana.
	 * @details Destruye la ventana GTK solo si todavía tiene padre, para
	 * no duplicar la destrucción ya provocada por la señal "destroy".
	 */
	~MainWindow();

	/**
	 * @brief Presenta la ventana al usuario (gtk_window_present).
	 */
	void show();

	// Callbacks estáticos

	/**
	 * @brief Abre la ventana de gestión de materiales.
	 * @param button     Botón pulsado (no usado).
	 * @param user_data  No usado; la ventana se crea nueva.
	 */
	static void on_btn_materiales_clicked(GtkButton *button, gpointer user_data);

	/**
	 * @brief Abre el gestor de herramientas.
	 * @param button     Botón pulsado (no usado).
	 * @param user_data  No usado; la ventana se crea nueva.
	 */
	static void on_btn_herramientas_clicked(GtkButton *button, gpointer user_data);

	/**
	 * @brief Abre la ventana de generación de G-Code.
	 * @param button     Botón pulsado (no usado).
	 * @param user_data  No usado; la ventana se crea nueva.
	 */
	static void on_btn_gcode_clicked(GtkButton *button, gpointer user_data);

	/**
	 * @brief Maneja el botón de perfiles de trabajo.
	 * @warning Solo imprime "Abrir Perfiles de Trabajo..." por consola;
	 * no abre ninguna ventana.
	 * @param button     Botón pulsado (no usado).
	 * @param user_data  No usado.
	 */
	static void on_btn_perfiles_clicked(GtkButton *button, gpointer user_data);
};

#endif /* INTERFACES_UI_MAIN_H_ */
