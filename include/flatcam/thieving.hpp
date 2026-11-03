/**
 * @file thieving.hpp
 * @brief Copper thieving: grilla de puntos de cobre.
 */
#pragma once

#include <vector>

#include "flatcam/geometry.hpp"

namespace fc {

/**
 * @brief Parámetros de la grilla de thieving.
 */
struct ThievingOptions {
    double dotDiameter = 0.5; ///< Diámetro de cada punto.
    double spacing = 2.0;     ///< Separación entre puntos.
    double clearance = 0.5;   ///< Distancia mínima al cobre existente.
    double margin = 0.5;      ///< Margen del área de trabajo.
};

/**
 * @brief Calcula los centros de una grilla de puntos de cobre.
 *
 * Los puntos se colocan dentro de `area` y se descartan si quedan a menos de
 * `clearance + radio` del cobre existente.
 *
 * @param copper Cobre a evitar.
 * @param area Área de trabajo.
 * @param options Diámetro, separación, distancia y margen.
 * @return Centros válidos.
 */
std::vector<Pt> thievingPoints(const Geom& copper, const BBox& area,
                               const ThievingOptions& options);

}  // namespace fc
