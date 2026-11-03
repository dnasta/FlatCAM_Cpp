/**
 * @file units.hpp
 * @brief Unidades, conversiones y lectura de coordenadas Gerber.
 *
 * FlatCAM C++ almacena toda la geometría en milímetros. La interfaz puede
 * mostrar pulgadas, pero las conversiones se hacen explícitamente al entrar y
 * salir de los parsers y de los controles de usuario.
 */
#pragma once

#include <string>
#include <string_view>

namespace fc {

/// Unidad de trabajo de un archivo o de la aplicación.
enum class Units {
    Mm,   ///< Milímetros.
    Inch  ///< Pulgadas.
};

/// Factor exacto de conversión pulgadas → milímetros.
constexpr double kInchToMm = 25.4;

/**
 * @brief Factor multiplicativo para pasar de una unidad a otra.
 * @param from Unidad de origen.
 * @param to Unidad de destino.
 * @return 1, 25.4 o 1/25.4 según corresponda.
 */
inline double conversionFactor(Units from, Units to) {
    if (from == to) return 1.0;
    return from == Units::Inch ? kInchToMm : 1.0 / kInchToMm;
}

/**
 * @brief Convierte una longitud entre unidades.
 * @param value Valor numérico.
 * @param from Unidad de origen.
 * @param to Unidad de destino.
 * @return Valor convertido.
 */
inline double convert(double value, Units from, Units to) {
    return value * conversionFactor(from, to);
}

/// @return Etiqueta corta de la unidad (`"MM"` o `"IN"`).
inline std::string toString(Units u) { return u == Units::Mm ? "MM" : "IN"; }

/**
 * @brief Interpreta una coordenada Gerber con supresión de ceros.
 *
 * Reproduce `parse_gerber_number()` de FlatCAM:
 * - `zeros == 'L'` o `'D'`: se omiten ceros a la izquierda; el número se
 *   escala por `10^-fracDigits`.
 * - `zeros == 'T'`: se omiten ceros a la derecha; se rellenan los dígitos
 *   faltantes antes de escalar.
 *
 * @param text Texto de la coordenada (puede llevar signo).
 * @param intDigits Dígitos enteros declarados en `FS`.
 * @param fracDigits Dígitos fraccionarios declarados en `FS`.
 * @param zeros Modo de supresión (`'L'`, `'T'` o `'D'`).
 * @return Coordenada en las unidades del archivo.
 */
double parseGerberNumber(std::string_view text, int intDigits, int fracDigits, char zeros);

}  // namespace fc
