/**
 * @file paint.cpp
 * @brief Relleno de áreas por contornos paralelos (pocket/paint).
 *
 * Implementa `fc::paintArea`: parte de la región a pintar, la contrae un radio
 * de herramienta y luego la encoge repetidamente en `paso` para obtener
 * anillos concéntricos que el centro de la fresa puede recorrer sin salirse
 * del material. Cada anillo (y opcionalmente cada hueco) se emite como un
 * recorrido independiente.
 */

#include "flatcam/paint.hpp"

#include <algorithm>
#include <cmath>

namespace fc {
namespace {

/// @brief Acumula `b` dentro de `target`, ignorando cajas inválidas.
void addBounds(BBox& target, const BBox& b) {
    if (!b.valid) return;
    if (!target.valid) {
        target = b;
        return;
    }
    target.xmin = std::min(target.xmin, b.xmin);
    target.ymin = std::min(target.ymin, b.ymin);
    target.xmax = std::max(target.xmax, b.xmax);
    target.ymax = std::max(target.ymax, b.ymax);
}

/// @brief Copia `ring` invirtiendo el orden de sus vértices si `invert`.
Ring oriented(Ring ring, bool invert) {
    if (invert) std::reverse(ring.begin(), ring.end());
    return ring;
}

}  // namespace

/**
 * @brief Rellena una región con recorridos concéntricos paralelos al contorno.
 *
 * Pasos del algoritmo:
 * 1. Se expande la región con `buffer(margin)` si el margen es distinto de 0.
 * 2. Se contrae un radio de herramienta para que el centro de la fresa quede
 *    siempre dentro del área original (compensación de radio).
 * 3. En cada iteración se emiten el contorno exterior y —si
 *    `options.includeHoles`— los huecos de cada polígono, y luego se contrae
 *    la región otro `paso`.
 * 4. El bucle corta cuando la región queda vacía, cuando el área deja de
 *    disminuir (delta menor que 1e-9 relativo al área inicial, señal de
 *    colapso numérico) o al alcanzar 10000 iteraciones.
 *
 * @param area Región a pintar (normalmente el cobre de un Gerber).
 * @param options Diámetro de herramienta, superposición, margen, sentido y
 *                si se pintan los huecos.
 * @return Recorridos del centro de la fresa, longitud total y límites.
 *
 * @note El paso efectivo es `stepOver × toolDiameter` con un mínimo de
 *       0,01 mm; `stepOver` es una fracción (0,5 = 50 % de superposición).
 * @note `result.bounds` corresponde a la región con margen, no al conjunto de
 *       recorridos, por lo que puede ser algo mayor que el área realmente
 *       mecanizada.
 * @warning Si el diámetro es mayor que la región, el buffer negativo la vacía
 *          y el resultado queda sin recorridos (no es un error).
 */
PaintResult paintArea(const Geom& area, const PaintOptions& options) {
    PaintResult result;
    if (!area.valid() || area.empty()) return result;

    const double radius = std::max(0.0, options.toolDiameter / 2.0);
    const double step = std::max(0.01, options.stepOver * options.toolDiameter);
    const int quad = std::max(2, options.quadrantSegments);

    Geom region = options.margin != 0.0 ? area.buffer(options.margin, quad) : area;
    if (!region.valid() || region.empty()) return result;
    addBounds(result.bounds, region.bounds());

    // The tool centre must stay one radius inside the region.
    Geom current = radius > 0.0 ? region.buffer(-radius, quad) : region;
    if (!current.valid() || current.empty()) return result;

    const double initialArea = current.area();
    for (int iteration = 0; iteration < 10000; ++iteration) {
        if (!current.valid() || current.empty()) break;

        for (const PolygonContours& pc : current.polygons()) {
            Geom outer = Geom::line(oriented(pc.outer, options.invert));
            if (outer.valid() && !outer.empty()) {
                result.totalLength += outer.length();
                result.paths.push_back(std::move(outer));
            }
            if (options.includeHoles) {
                for (const Ring& hole : pc.holes) {
                    Geom path = Geom::line(oriented(hole, options.invert));
                    if (path.valid() && !path.empty()) {
                        result.totalLength += path.length();
                        result.paths.push_back(std::move(path));
                    }
                }
            }
        }

        Geom next = current.buffer(-step, quad);
        if (!next.valid() || next.empty()) break;
        if (std::fabs(next.area() - current.area()) < 1e-9 * std::max(1.0, initialArea)) break;
        current = std::move(next);
    }

    return result;
}

}  // namespace fc
