/**
 * @file Theme.h
 * @brief Tema visual moderno para la GUI Qt.
 *
 * Centraliza la hoja de estilo, la paleta y el estilo de widgets. La misma
 * función se usa al arrancar y al cambiar de tema en caliente.
 */
#pragma once

#include <QString>

class QApplication;

namespace fc::theme {

/// @return Hoja de estilo QSS correspondiente al tema indicado.
QString styleSheet(bool dark);

/// @return `true` si el último tema aplicado es oscuro.
bool isDark();

/// @brief Aplica estilo Fusion, paleta y hoja de estilo a la aplicación.
void apply(QApplication& app, bool dark);

}  // namespace fc::theme
