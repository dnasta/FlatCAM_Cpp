/**
 * @file paint.hpp
 * @brief Relleno de áreas con recorridos paralelos (pocket/paint).
 */
#pragma once

#include <vector>

#include "flatcam/geometry.hpp"

namespace fc {

/**
 * @brief Parámetros de la pintura.
 */
struct PaintOptions {
    double toolDiameter = 0.1; ///< Diámetro de la fresa.
    double stepOver = 0.5;     ///< Paso como fracción del diámetro.
    double margin = 0.0;       ///< Expansión previa de la región.
    int quadrantSegments = 16; ///< Segmentos por cuadrante.
    bool invert = false;       ///< Invierte el sentido.
    bool includeHoles = true;  ///< Pinta también dentro de los agujeros.
};

/**
 * @brief Resultado de la pintura.
 */
struct PaintResult {
    std::vector<Geom> paths; ///< Recorridos paralelos al contorno.
    double totalLength = 0.0;///< Longitud total.
    BBox bounds;             ///< Límites.
};

/**
 * @brief Rellena una región con offsets paralelos al contorno.
 * @param area Región a pintar.
 * @param options Diámetro, paso, margen y agujeros.
 * @return Recorridos de pintura.
 */
PaintResult paintArea(const Geom& area, const PaintOptions& options);

}  // namespace fc
