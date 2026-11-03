/**
 * @file units.cpp
 * @brief Conversión de números Gerber de formato implícito a valores reales.
 *
 * Implementa la regla RS-274X que usa FlatCAM: los enteros del archivo llevan
 * la coma decimal implícita y la cantidad de dígitos la fija el comando FS,
 * según se supriman ceros a la izquierda (L), a la derecha (T) o haya coma
 * explícita (D). Es la base del parser Gerber.
 */
#include "flatcam/units.hpp"

#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace fc {

/**
 * @brief Interpreta un campo de coordenada Gerber según formato y ceros.
 *
 * Acepta un signo opcional, valida que el resto sean sólo dígitos y arma el
 * valor en tres modos:
 * - 'L' o 'D': el texto ya trae todos los dígitos, se divide por 10^fracDigits.
 * - 'T': los ceros finales se omitieron, así que se escala por
 *   10^(intDigits + fracDigits - len - fracDigits).
 * Cualquier otro modo es error.
 * @param text Campo tal como aparece en el archivo (sin el carácter de eje).
 * @param intDigits Dígitos enteros declarados en FS (3 en X34).
 * @param fracDigits Dígitos fraccionarios declarados en FS (4 en X34).
 * @param zeros Modo de ceros: 'L', 'T' o 'D'.
 * @return Valor con signo, en las unidades del archivo.
 * @throw std::invalid_argument si el campo está vacío, contiene caracteres no
 *        numéricos o el modo de ceros es desconocido.
 * @note Un campo con sólo "+" o "-" es válido y produce 0.
 */
double parseGerberNumber(std::string_view text, int intDigits, int fracDigits, char zeros) {
    if (text.empty()) throw std::invalid_argument("empty gerber number");

    bool negative = false;
    std::size_t i = 0;
    if (text[i] == '+' || text[i] == '-') {
        negative = text[i] == '-';
        ++i;
    }
    const std::string_view digits = text.substr(i);
    for (char c : digits) {
        if (c < '0' || c > '9') throw std::invalid_argument("invalid gerber number: " + std::string(text));
    }
    const long long raw = std::strtoll(std::string(digits).c_str(), nullptr, 10);

    double value = 0.0;
    if (zeros == 'L' || zeros == 'D') {
        value = static_cast<double>(raw) * std::pow(10.0, -fracDigits);
    } else if (zeros == 'T') {
        const int total = intDigits + fracDigits;
        const int len = static_cast<int>(digits.size());
        value = static_cast<double>(raw) * std::pow(10.0, total - len - fracDigits);
    } else {
        throw std::invalid_argument("unknown zero suppression mode");
    }
    return negative ? -value : value;
}

}  // namespace fc
