/**
 * @file cutout.hpp
 * @brief Corte de contorno: compensación de fresa, puentes y multipasada.
 *
 * La herramienta de corte calcula el recorrido del centro de la fresa a partir
 * del contorno de la placa. El margen y el radio de la herramienta deciden si
 * el corte queda por fuera (placa nominal) o por dentro (recorte).
 */
#pragma once

#include <optional>
#include <vector>

#include "flatcam/geometry.hpp"

namespace fc {

/**
 * @brief Parámetros del corte de contorno.
 */
struct CutoutOptions {
    double toolDiameter = 1.0;   ///< Diámetro de la fresa.
    double margin = 0.0;         ///< Margen extra más allá del radio.
    bool external = true;        ///< `true` corta por fuera; `false` por dentro.
    int quadrantSegments = 16;   ///< Segmentos por cuadrante de las curvas.
    bool invert = false;         ///< Invierte el sentido del recorrido.
    int tabCount = 0;            ///< Puentes que sostienen la pieza; 0 = ninguno.
    double tabWidth = 0.0;       ///< Ancho de cada puente (mm).
    bool includeHoles = false;   ///< Corta también contornos internos.
    std::optional<BBox> area;    ///< Área rectangular opcional de trabajo.
};

/**
 * @brief Resultado del cálculo de corte.
 */
struct CutoutResult {
    std::vector<Geom> paths; ///< Recorridos del centro de la fresa.
    Geom envelope;           ///< Polígono desplazado, para previsualización.
    double totalLength = 0.0;///< Longitud total de los recorridos.
    BBox bounds;             ///< Límites.
};

/**
 * @brief Calcula los recorridos de corte de un contorno.
 *
 * Desplaza el contorno por `margen ± radio` (externo: margen + Ø/2; interno:
 * margen − Ø/2), recorta contra `area` si existe, aplica puentes y devuelve
 * los recorridos del centro de la fresa.
 *
 * @param outline Contorno de la placa.
 * @param options Parámetros de corte.
 * @return Recorridos, envolvente y longitud total.
 */
CutoutResult cutoutGeometry(const Geom& outline, const CutoutOptions& options);

}  // namespace fc
