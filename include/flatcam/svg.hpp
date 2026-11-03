/**
 * @file svg.hpp
 * @brief Exportación de geometría a SVG.
 */
#pragma once

#include <string>
#include <vector>

#include "flatcam/geometry.hpp"

namespace fc {

/**
 * @brief Exporta sólidos y recorridos a un SVG plano.
 * @param path Archivo de salida.
 * @param solids Polígonos rellenos (con regla even-odd para agujeros).
 * @param follows Trazos.
 * @param bounds Límites para el `viewBox`.
 */
void exportSvg(const std::string& path, const std::vector<Geom>& solids,
               const std::vector<Geom>& follows, const BBox& bounds);

}  // namespace fc
