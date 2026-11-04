/**
 * @file Theme.h
 * @brief Temas visuales de la GUI Qt.
 *
 * Hay dos familias y dos variantes por familia:
 * - **Moderno**: estilo Fusion con paleta y QSS propios de FlatCAM C++.
 * - **Sistema**: estilo nativo de la plataforma (macOS, Windows, etc.) con la
 *   apariencia clara u oscura del sistema operativo.
 *
 * La misma función se usa al arrancar y al cambiar de tema en caliente.
 */
#pragma once

#include <QString>

class QApplication;

namespace fc::theme {

/// Temas disponibles en `Opciones → Temas`.
enum class Theme {
    ModernLight,  ///< Tema moderno de FlatCAM C++ en versión clara.
    ModernDark,   ///< Tema moderno de FlatCAM C++ en versión oscura.
    SystemLight,  ///< Apariencia nativa del sistema en versión clara.
    SystemDark    ///< Apariencia nativa del sistema en versión oscura.
};

/// @return Hoja de estilo QSS del tema moderno indicado.
QString styleSheet(bool dark);

/// @return `true` si el último tema aplicado es oscuro.
bool isDark();

/// @return `true` si la variante del tema es oscura.
bool isDark(Theme theme);

/// @return Clave estable usada en `QSettings`.
QString themeKey(Theme theme);

/// @brief Convierte una clave de `QSettings` en un tema.
Theme themeFromKey(const QString& key, Theme fallback = Theme::ModernLight);

/// @brief Lee el tema guardado, con compatibilidad para `preferences/darkTheme`.
Theme themeFromSettings();

/// @brief Aplica el tema indicado: estilo, paleta y hoja de estilo.
void apply(QApplication& app, Theme theme);

}  // namespace fc::theme
