/**
 * @file isolation.cpp
 * @brief Aislamiento de cobre por offsets sucesivos y rest machining.
 *
 * Reproduce el esquema de FlatCAM: el cobre se descompone en polígonos, cada
 * pasada dilata cada uno con `buffer(offset)` y une los resultados para obtener
 * la "envolvente" de esa pasada, de la que se extraen exteriores y huecos como
 * recorridos del centro de la fresa. Incluye además el rest machining
 * (`isolateRest`), que aísla sólo el cobre inalcanzable por una fresa mayor.
 */

#include "flatcam/isolation.hpp"

#include <algorithm>
#include <cmath>

namespace fc {
namespace {

/// @brief Copia `ring` invirtiendo el orden de sus vértices si `invert`.
Ring oriented(Ring ring, bool invert) {
    if (invert) std::reverse(ring.begin(), ring.end());
    return ring;
}

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

}  // namespace

/**
 * @brief Calcula los recorridos de aislamiento alrededor del cobre.
 *
 * Para cada pasada `i` (0-based) se usa la fórmula de FlatCAM:
 * `offset = Ø · (2i + 1)/2 − i · solape · Ø`, es decir, la distancia del centro
 * de la fresa al borde del cobre de la i-ésima pasada. El procedimiento es:
 *
 * 1. Descomponer el cobre en polígonos independientes (exterior + huecos).
 * 2. Por pasada, dilatar cada polígono `offset` mm y unir los resultados en una
 *    sola envolvente; con `avoidIntersections` se omite la pasada de un
 *    polígono cuyo buffer (`offset × 2`) choca con otro polígono distinto.
 * 3. Extraer de la envolvente el contorno exterior (salvo
 *    `interiorsOnly`) y los huecos (salvo `exteriorsOnly`) como recorridos,
 *    invirtiendo el sentido si `invert` (fresado en concordancia).
 *
 * @param copper Cobre de entrada (sólidos de un Gerber).
 * @param options Diámetro, pasadas, solape, filtros, sentido y rest machining.
 * @return Recorridos, envolventes por pasada, polígonos no aislados, longitud
 *         total y límites acumulados.
 *
 * @note `overlap` se acota a [0, 0.99] y `passes` a un mínimo de 1, de modo que
 *       valores fuera de rango no rompen el cálculo.
 * @note Las envolventes se guardan en `result.envelopes` para la
 *       previsualización y se acumulan sin unirlas entre pasadas.
 * @note `notIsolated` sólo se llena si `avoidIntersections` está activo: son
 *       los polígonos que no recibieron ninguna pasada.
 * @warning Con `exteriorsOnly` y `interiorsOnly` a la vez no se genera ningún
 *          recorrido, porque ambas condiciones de emisión quedan desactivadas.
 * @warning El chequeo de colisión dilata con `offset × 2`, no con `offset`;
 *          es una aproximación conservadora del comportamiento de FlatCAM y
 *          puede descartar pasadas que en realidad no chocaban.
 */
IsolationResult isolateCopper(const Geom& copper, const IsolationOptions& options) {
    IsolationResult result;
    if (!copper.valid() || copper.empty()) return result;

    const int passes = std::max(1, options.passes);
    const int quadSegs = std::max(2, options.quadrantSegments);
    const double overlap = std::clamp(options.overlap, 0.0, 0.99);

    // Split the copper into individual polygons once; FlatCAM buffers each one
    // and unions the result for every pass.
    struct SourcePolygon {
        Geom geom;
    };
    std::vector<SourcePolygon> sources;
    for (const PolygonContours& pc : copper.polygons()) {
        Geom poly = Geom::polygon(pc.outer, pc.holes);
        if (poly.valid() && !poly.empty()) sources.push_back({poly});
    }

    std::vector<std::size_t> perSourcePasses(sources.size(), 0);

    for (int pass = 0; pass < passes; ++pass) {
        const double offset = options.toolDiameter * ((2.0 * pass + 1.0) / 2.0) -
                              pass * overlap * options.toolDiameter;

        std::vector<Geom> buffers;
        buffers.reserve(sources.size());
        for (std::size_t i = 0; i < sources.size(); ++i) {
            const SourcePolygon& source = sources[i];
            if (options.avoidIntersections && offset > 0.0) {
                Geom check = source.geom.buffer(offset * 2.0, quadSegs);
                bool collides = false;
                for (std::size_t j = 0; j < sources.size() && !collides; ++j) {
                    if (i == j) continue;
                    if (check.intersects(sources[j].geom)) collides = true;
                }
                if (collides) continue;
            }
            Geom buffered = offset == 0.0 ? source.geom : source.geom.buffer(offset, quadSegs);
            if (buffered.valid() && !buffered.empty()) {
                buffers.push_back(std::move(buffered));
                ++perSourcePasses[i];
            }
        }
        if (buffers.empty()) continue;

        Geom envelope = buffers.size() == 1 ? buffers.front() : Geom::unionOf(buffers);
        if (!envelope.valid() || envelope.empty()) continue;
        result.envelopes.push_back(envelope);

        for (const PolygonContours& pc : envelope.polygons()) {
            if (!options.interiorsOnly) {
                Geom path = Geom::line(oriented(pc.outer, options.invert));
                if (path.valid() && !path.empty()) {
                    result.totalLength += path.length();
                    result.paths.push_back(std::move(path));
                }
            }
            if (!options.exteriorsOnly) {
                for (const Ring& hole : pc.holes) {
                    Geom path = Geom::line(oriented(hole, options.invert));
                    if (path.valid() && !path.empty()) {
                        result.totalLength += path.length();
                        result.paths.push_back(std::move(path));
                    }
                }
            }
        }
        addBounds(result.bounds, envelope.bounds());
    }

    if (options.avoidIntersections) {
        for (std::size_t i = 0; i < sources.size(); ++i) {
            if (perSourcePasses[i] == 0) result.notIsolated.push_back(sources[i].geom);
        }
    }

    return result;
}

/**
 * @brief Aísla únicamente el cobre que una herramienta mayor no pudo alcanzar.
 *
 * Divide el cobre en polígonos y marca como "resto" aquellos cuyo buffer de
 * `previousToolDiameter` intersecta algún otro polígono, es decir, los que
 * están demasiado cerca de otro feature como para que la fresa anterior pase
 * entre ambos. Si no hay resto devuelve un resultado vacío. En caso contrario
 * une esos polígonos y ejecuta `isolateCopper` con la herramienta menor,
 * desactivando `avoidIntersections` para no perder pasadas dentro del resto.
 *
 * @param copper Cobre original.
 * @param previousToolDiameter Diámetro de la fresa usada en la pasada previa;
 *                             debe ser > 0.
 * @param options Opciones de la herramienta menor.
 * @return Resultado del aislamiento del resto, con `notIsolated` igual a los
 *         polígonos considerados resto.
 *
 * @note El resultado devuelto descarta deliberadamente los `notIsolated` del
 *       cálculo interno y los reemplaza por la lista de polígonos de resto.
 * @note La unión previa (`unionOf`) evita que polígonos vecinos generen
 *       contornos duplicados en la misma pasada.
 * @note El chequeo usa `buffer(previousToolDiameter)`, igual que
 *       `buf_chek = iso_offset * 2` del `generate_rest_geometry` original de
 *       FlatCAM; se mantiene por paridad funcional.
 */
IsolationResult isolateRest(const Geom& copper, double previousToolDiameter,
                            const IsolationOptions& options) {
    IsolationResult result;
    if (!copper.valid() || copper.empty() || previousToolDiameter <= 0.0) return result;

    std::vector<Geom> sources;
    for (const PolygonContours& pc : copper.polygons()) {
        Geom poly = Geom::polygon(pc.outer, pc.holes);
        if (poly.valid() && !poly.empty()) sources.push_back(std::move(poly));
    }

    std::vector<Geom> rest;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        Geom check = sources[i].buffer(previousToolDiameter, std::max(2, options.quadrantSegments));
        bool collides = false;
        for (std::size_t j = 0; j < sources.size() && !collides; ++j) {
            if (i == j) continue;
            if (check.intersects(sources[j])) collides = true;
        }
        if (collides) rest.push_back(sources[i]);
    }
    if (rest.empty()) return result;

    Geom restGeometry = rest.size() == 1 ? rest.front() : Geom::unionOf(rest);
    IsolationOptions current = options;
    current.avoidIntersections = false;
    result = isolateCopper(restGeometry, current);
    result.notIsolated = std::move(rest);
    return result;
}

}  // namespace fc
