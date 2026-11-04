/**
 * @file ncc.hpp
 * @brief NCC (Non-Copper Clear): remoción de cobre sobrante.
 */
#pragma once

#include <vector>

#include "flatcam/geometry.hpp"

namespace fc {

/// Estrategia de despeje.
enum class NccStrategy {
    Hatch, ///< Líneas paralelas.
    Paint  ///< Contornos paralelos.
};

/**
 * @brief Parámetros de NCC.
 */
struct NccOptions {
    NccStrategy strategy = NccStrategy::Hatch; ///< Hatch o paint.
    double toolDiameter = 0.2; ///< Diámetro de la fresa.
    double stepOver = 0.5;     ///< Paso como fracción del diámetro.
    double clearance = 0.0;    ///< Distancia extra de seguridad al cobre.
    double angleDeg = 0.0;     ///< Dirección del hatch.
    double margin = 0.0;       ///< Expansión del área de trabajo.
    double border = 0.0;       ///< Banda exterior sin despejar.
    int quadrantSegments = 16; ///< Segmentos por cuadrante.
};

/**
 * @brief Resultado de NCC.
 */
struct NccResult {
    std::vector<Geom> paths; ///< Recorridos de despeje.
    Geom clearedArea;        ///< Área que se despeja.
    double totalLength = 0.0;///< Longitud total.
    BBox bounds;             ///< Límites.
};

/**
 * @brief Despeja el área que no es cobre.
 *
 * Construye el área `area - buffer(copper, radio + clearance)` y la recorre
 * con líneas paralelas separadas `stepOver * diámetro`.
 *
 * @param copper Cobre a conservar.
 * @param area Área de trabajo.
 * @param options Estrategia, diámetro, paso, clearance y ángulo.
 * @return Recorridos y área despejada.
 */
NccResult clearCopper(const Geom& copper, const Geom& area, const NccOptions& options);

}  // namespace fc
