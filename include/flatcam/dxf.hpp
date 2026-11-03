/**
 * @file dxf.hpp
 * @brief Exportador DXF mínimo (AC1015).
 */
#pragma once

#include <string>
#include <vector>

#include "flatcam/geometry.hpp"

namespace fc {

/**
 * @brief Exporta geometría a DXF AC1015.
 *
 * Los sólidos se escriben como `LWPOLYLINE` cerradas (con agujeros como
 * contornos adicionales) y los recorridos como `LWPOLYLINE` abiertas.
 * Las unidades del archivo son milímetros.
 *
 * @param path Archivo de salida.
 * @param solids Polígonos rellenos.
 * @param follows Trazos y recorridos.
 */
void exportDxf(const std::string& path, const std::vector<Geom>& solids,
               const std::vector<Geom>& follows);

}  // namespace fc
