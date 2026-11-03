/**
 * @file qr.hpp
 * @brief Generación de geometría de códigos QR.
 */
#pragma once

#include <string>
#include <vector>

#include "flatcam/geometry.hpp"

namespace fc {

/**
 * @brief Convierte un texto en la geometría de un código QR.
 *
 * Los módulos oscuros se agrupan y unen en polígonos. La esquina inferior
 * izquierda del QR queda en `(originX, originY)`.
 *
 * @param text Contenido.
 * @param moduleSize Tamaño de cada módulo en mm.
 * @param borderModules Módulos de margen (quiet zone).
 * @param originX Origen X en mm.
 * @param originY Origen Y en mm.
 * @param ecc Nivel de corrección (`L`, `M`, `Q`, `H`).
 * @return Polígonos de los módulos oscuros.
 */
std::vector<Geom> qrCodeGeometry(const std::string& text, double moduleSize,
                                 int borderModules = 4, double originX = 0.0,
                                 double originY = 0.0, char ecc = 'M');

}  // namespace fc
