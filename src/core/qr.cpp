/**
 * @file qr.cpp
 * @brief Generación de la geometría de un código QR lista para fresar.
 *
 * Envoltorio de la librería qrcodegen (Nayuki): codifica el texto, agrupa los
 * módulos oscuros contiguos de cada fila en rectángulos y los une en un único
 * polígono. La salida está en milímetros, con origen configurable y borde
 * opcional, de modo que se puede usar como capa de cobre.
 */
#include "flatcam/qr.hpp"

#include "flatcam/qrcodegen.hpp"

namespace fc {

/**
 * @brief Convierte un texto en la geometría de un QR en coordenadas CAD.
 *
 * Elige la corrección de errores desde @p ecc ('L', 'Q' o 'H'; cualquier otro
 * valor, incluido 'M', cae en MEDIUM), codifica con qrcodegen y recorre las
 * filas agrupando módulos oscuros consecutivos en cajas de moduleSize. Cada
 * caja se desplaza por el borde y el eje Y se invierte porque la librería
 * numera las filas de arriba hacia abajo. Al final une todas las cajas con
 * Geom::unionOf para entregar un solo polígono.
 * @param text Contenido a codificar.
 * @param moduleSize Lado del módulo en mm; si es <= 0 devuelve vacío.
 * @param borderModules Borde en módulos (se satura a >= 0).
 * @param originX Coordenada X de la esquina inferior izquierda (mm).
 * @param originY Ídem en Y (mm).
 * @param ecc Nivel de corrección: 'L', 'M', 'Q' o 'H'.
 * @return Vector con un único Geom; vacío si no hay módulos oscuros o si el
 *         tamaño de módulo no es positivo.
 * @note qrcodegen lanza excepción si el texto no cabe en el nivel pedido; este
 *       módulo no la captura.
 */
std::vector<Geom> qrCodeGeometry(const std::string& text, double moduleSize, int borderModules,
                                 double originX, double originY, char ecc) {
    if (moduleSize <= 0.0) return {};
    qrcodegen::QrCode::Ecc level = qrcodegen::QrCode::Ecc::MEDIUM;
    switch (ecc) {
        case 'L': level = qrcodegen::QrCode::Ecc::LOW; break;
        case 'Q': level = qrcodegen::QrCode::Ecc::QUARTILE; break;
        case 'H': level = qrcodegen::QrCode::Ecc::HIGH; break;
        default: level = qrcodegen::QrCode::Ecc::MEDIUM; break;
    }
    const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(text.c_str(), level);
    const int size = qr.getSize();
    const int border = std::max(0, borderModules);
    const double origin = border * moduleSize;

    std::vector<Geom> runs;
    runs.reserve(static_cast<std::size_t>(size * size / 3));
    for (int y = 0; y < size; ++y) {
        int start = -1;
        for (int x = 0; x <= size; ++x) {
            const bool dark = x < size && qr.getModule(x, y);
            if (dark && start < 0) start = x;
            if (!dark && start >= 0) {
                // QR y grows downwards in the library; flip it for CAD.
                const double y0 = originY + origin + (size - 1 - y) * moduleSize;
                runs.push_back(Geom::box(originX + origin + start * moduleSize, y0,
                                         originX + origin + x * moduleSize, y0 + moduleSize));
                start = -1;
            }
        }
    }
    if (runs.empty()) return {};
    return {Geom::unionOf(runs)};
}

}  // namespace fc
