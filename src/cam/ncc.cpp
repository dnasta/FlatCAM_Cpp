/**
 * @file ncc.cpp
 * @brief NCC (Non-Copper Clear): despeje del cobre sobrante.
 *
 * Implementa `fc::clearCopper`. Primero calcula el área mecanizable como
 * `área de trabajo − buffer(cobre, radio + clearance)` y después la recorre
 * con una de dos estrategias: líneas paralelas recortadas contra la región
 * (`Hatch`, la implementación propia de este archivo) o contornos paralelos
 * delegando en `fc::paintArea` (`Paint`).
 */

#include "flatcam/ncc.hpp"

#include "flatcam/paint.hpp"

#include <algorithm>
#include <cmath>

namespace fc {
namespace {

/**
 * @brief Aplana el resultado de una intersección en una lista de cadenas.
 *
 * GEOS devuelve `LineString`, `MultiLineString`, `GeometryCollection` o incluso
 * `MultiPolygon` según el caso, de modo que la función recorre el árbol y
 * agrega únicamente las geometrías de tipo línea no vacías.
 *
 * @param geom Geometría a recorrer.
 * @param out Lista donde se acumulan las cadenas encontradas.
 */
void collectLines(const Geom& geom, std::vector<Geom>& out) {
    if (!geom.valid() || geom.empty()) return;
    const int type = geom.typeId();
    if (type == GEOS_LINESTRING || type == GEOS_LINEARRING) {
        out.push_back(geom);
        return;
    }
    if (type == GEOS_MULTILINESTRING || type == GEOS_GEOMETRYCOLLECTION ||
        type == GEOS_MULTIPOLYGON) {
        const int n = geom.numGeometries();
        for (int i = 0; i < n; ++i) collectLines(geom.geometryN(i), out);
    }
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
 * @brief Despeja todo el cobre que no pertenece a la geometría de referencia.
 *
 * Construye la región mecanizable como `workArea.difference(keepOut)`, donde
 * `keepOut = copper.buffer(radius + clearance)` es el cobre protegido con su
 * distancia de seguridad. Si no se pasa área de trabajo se usa el bounding box
 * del cobre expandido por `margin + spacing`.
 *
 * Con `NccStrategy::Paint` delega en `fc::paintArea` sobre esa región. Con
 * `NccStrategy::Hatch` (predeterminada) genera líneas paralelas separadas
 * `stepOver × diámetro`, las rota `angleDeg` alrededor del centro del área de
 * trabajo, las extiende ±diagonal y las recorta con `intersection(region)`;
 * cada fragmento de longitud ≥ 1e-6 mm se convierte en un recorrido
 * independiente.
 *
 * @param copper Cobre que debe conservarse.
 * @param area Área rectangular de trabajo; si está vacía se deriva del cobre.
 * @param options Estrategia, diámetro, paso, clearance, ángulo, margen y
 *                segmentos por cuadrante.
 * @return Recorridos, área despejada, longitud total y límites.
 *
 * @note `clearedArea` se informa también en modo Paint, aunque en ese caso el
 *       recorte real lo hace `paintArea` con su propio margen.
 * @note `spacing` se satura a 0,05 mm como mínimo para evitar bucles infinitos.
 * @warning En modo Hatch el barrido en Y va desde `bounds.ymin − margin` hasta
 *          `bounds.ymax + margin`, pero `bounds` ya deriva de un área que
 *          incluye `margin + spacing`. Las líneas sobrantes se recortan contra
 *          la región, así que el resultado es correcto, aunque se generan y
 *          recortan segmentos de más (trabajo redundante).
 * @note El barrido usa offsets perpendiculares a la dirección del hatch, de
 *       modo que la separación efectiva es `spacing` para cualquier ángulo.
 * @note `lineCount` se acumula sólo con fines de depuración y luego se
 *       descarta con `(void)lineCount`; no se informa al llamador.
 */
NccResult clearCopper(const Geom& copper, const Geom& area, const NccOptions& options) {
    NccResult result;
    if (!copper.valid() || copper.empty()) return result;

    const double radius = options.toolDiameter / 2.0;
    const double clearance = std::max(0.0, options.clearance);
    const double spacing = std::max(0.005, options.stepOver * options.toolDiameter);

    Geom workArea = area;
    if (!workArea.valid() || workArea.empty()) {
        const BBox b = copper.bounds();
        if (!b.valid) return result;
        const double m = options.margin + spacing;
        workArea = Geom::box(b.xmin - m, b.ymin - m, b.xmax + m, b.ymax + m);
    }

    // Deja una banda perimetral sin mecanizar para no debilitar el borde de la
    // placa ni cortar el material que la sostiene.
    if (options.border > 0.0) {
        Geom inset = workArea.bufferStyle(-options.border, std::max(2, options.quadrantSegments),
                                          GEOSBUF_CAP_FLAT, GEOSBUF_JOIN_MITRE, 10.0);
        if (inset.valid() && !inset.empty()) workArea = inset;
    }

    Geom keepOut = copper.buffer(radius + clearance + std::max(0.0, options.compensation),
                                 std::max(2, options.quadrantSegments));
    Geom region = workArea.difference(keepOut);
    if (!region.valid() || region.empty()) return result;
    result.clearedArea = region;

    auto paintRegion = [&]() {
        PaintOptions paint;
        paint.toolDiameter = options.toolDiameter;
        paint.stepOver = options.stepOver;
        paint.quadrantSegments = options.quadrantSegments;
        return paintArea(region, paint);
    };
    if (options.strategy == NccStrategy::Paint || options.strategy == NccStrategy::Seed) {
        const PaintResult painted = paintRegion();
        result.paths = painted.paths;
        result.totalLength = painted.totalLength;
        result.bounds = painted.bounds;
        return result;
    }

    const BBox bounds = workArea.bounds();
    if (!bounds.valid) return result;

    const double cx = (bounds.xmin + bounds.xmax) / 2.0;
    const double cy = (bounds.ymin + bounds.ymax) / 2.0;
    const double diag = std::hypot(bounds.width(), bounds.height()) + 4.0 * spacing;

    const double angle = options.angleDeg * M_PI / 180.0;
    const double c = std::cos(angle);
    const double s = std::sin(angle);

    int lineCount = 0;
    // Barre offsets perpendiculares a la dirección del hatch. Usar el eje Y
    // sin rotar dejaba separaciones efectivas mayores y esquinas sin cubrir
    // con ángulos de 45°.
    const double half = diag / 2.0;
    const int steps = std::max(1, static_cast<int>(std::ceil((2.0 * half) / spacing)));
    for (int k = 0; k <= steps; ++k) {
        const double offset = -half + k * spacing;
        const Pt mid{cx - s * offset, cy + c * offset};
        const Pt p0{mid.x - c * half, mid.y - s * half};
        const Pt p1{mid.x + c * half, mid.y + s * half};

        Geom line = Geom::line({p0, p1});
        if (!line.valid() || line.empty()) continue;
        Geom clipped = line.intersection(region);
        std::vector<Geom> pieces;
        collectLines(clipped, pieces);
        for (Geom& piece : pieces) {
            const double len = piece.length();
            if (len < 1e-6) continue;
            result.totalLength += len;
            addBounds(result.bounds, piece.bounds());
            result.paths.push_back(std::move(piece));
            ++lineCount;
        }
    }
    (void)lineCount;

    if (options.strategy == NccStrategy::Combo) {
        const PaintResult painted = paintRegion();
        result.paths.insert(result.paths.end(), painted.paths.begin(), painted.paths.end());
        result.totalLength += painted.totalLength;
        addBounds(result.bounds, painted.bounds);
    }

    if (!options.climb) {
        for (Geom& path : result.paths) {
            Ring ring = path.coords();
            std::reverse(ring.begin(), ring.end());
            path = Geom::line(ring);
        }
    }

    return result;
}

}  // namespace fc
