/**
 * @file cutout.cpp
 * @brief Corte de contorno con compensación de radio, área y puentes (tabs).
 *
 * Calcula la trayectoria del centro de la fresa para separar la placa del
 * material sobrante. El contorno se une, se desplaza `margen ± Ø/2` según el
 * corte sea externo o interno, se recorta opcionalmente contra un área
 * rectangular y finalmente se parte en tramos abiertos para dejar puentes que
 * sostienen la pieza durante el mecanizado.
 */

#include "flatcam/cutout.hpp"

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

/// @brief Longitud del contorno abierto de `ring` (sin cerrar el último lado).
double ringLength(const Ring& ring) {
    double total = 0.0;
    for (std::size_t i = 1; i < ring.size(); ++i) {
        total += std::hypot(ring[i].x - ring[i - 1].x, ring[i].y - ring[i - 1].y);
    }
    if (ring.size() >= 2) {
        const Pt& first = ring.front();
        const Pt& last = ring.back();
        const double closing = std::hypot(first.x - last.x, first.y - last.y);
        if (closing > 1e-9) total += closing;
    }
    return total;
}

/**
 * @brief Parte un contorno cerrado en tramos abiertos dejando puentes (tabs).
 *
 * El contorno se parametriza por longitud de arco `s` y los `count` puentes se
 * centran en `(k + 0,5) · L / count`, de forma que quedan repartidos
 * uniformemente y nunca en el vértice inicial. De cada puente se elimina el
 * intervalo `[centro − ancho/2, centro + ancho/2]` (con ajuste modular si
 * cruza el origen) y se reconstruye el tramo complementario, insertando los
 * vértices originales que caen dentro del intervalo para no perder la forma.
 *
 * @param closed Anillo cerrado de entrada (puede repetir el primer punto al
 *               final; se elimina la duplicación).
 * @param width Ancho de cada puente en milímetros.
 * @param count Cantidad de puentes.
 * @param totalLength Perímetro aproximado, usado sólo para validar que los
 *                    puentes no consuman toda la pieza.
 * @return Lista de polilíneas abiertas. Si no se puede aplicar puentes
 *         devuelve un único elemento con el anillo original intacto.
 *
 * @note Casos de salida temprana: `count <= 0`, `width <= 0`, menos de 4
 *       puntos, longitud no positiva, o `width × count ≥ 90 %` del perímetro.
 *       En todos ellos se devuelve el contorno completo para no destruir la
 *       pieza.
 * @note La longitud real se recalcula internamente (`L`); `totalLength` sólo
 *       participa del chequeo de saturación.
 * @warning Si los puentes se solapan (ancho grande respecto de la separación)
 *          el tramo puede quedar degenerado; el límite del 90 % amortigua pero
 *          no elimina el caso cuando `count` es grande y el ancho moderado.
 */
std::vector<Ring> splitWithTabs(const Ring& closed, double width, int count, double totalLength,
                                TabSides sides, const std::vector<double>& manualPositions) {
    std::vector<Ring> out;
    if ((count <= 0 && manualPositions.empty()) || width <= 0.0 || closed.size() < 4) {
        out.push_back(closed);
        return out;
    }
    if (totalLength <= 0.0) {
        out.push_back(closed);
        return out;
    }

    Ring pts = closed;
    if (pts.front().x == pts.back().x && pts.front().y == pts.back().y) pts.pop_back();
    const std::size_t n = pts.size();
    if (n < 3) {
        out.push_back(closed);
        return out;
    }

    std::vector<double> s(n + 1, 0.0);
    for (std::size_t i = 1; i <= n; ++i) {
        const Pt& a = pts[i - 1];
        const Pt& b = pts[i % n];
        s[i] = s[i - 1] + std::hypot(b.x - a.x, b.y - a.y);
    }
    const double length = s[n];

    auto pointAt = [&](double t) {
        t = std::fmod(t, length);
        if (t < 0.0) t += length;
        auto it = std::upper_bound(s.begin(), s.end(), t);
        std::size_t i = static_cast<std::size_t>(std::max<std::ptrdiff_t>(1, it - s.begin()));
        if (i > n) i = n;
        const double segLen = s[i] - s[i - 1];
        const double f = segLen > 0.0 ? (t - s[i - 1]) / segLen : 0.0;
        const Pt& a = pts[i - 1];
        const Pt& b = pts[i % n];
        return Pt{a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f};
    };

    std::vector<double> centers;
    if (!manualPositions.empty()) {
        centers.reserve(manualPositions.size());
        for (double fraction : manualPositions)
            centers.push_back(std::clamp(fraction, 0.0, 1.0) * length);
    } else {
        for (int k = 0; k < count; ++k)
            centers.push_back((k + 0.5) * length / count);
        if (sides != TabSides::All) {
            BBox box;
            for (const Pt& p : pts) {
                if (!box.valid) box = BBox{p.x, p.y, p.x, p.y};
                else {
                    box.xmin = std::min(box.xmin, p.x);
                    box.ymin = std::min(box.ymin, p.y);
                    box.xmax = std::max(box.xmax, p.x);
                    box.ymax = std::max(box.ymax, p.y);
                }
            }
            const double tolerance = std::max(0.01, length * 0.001);
            std::vector<double> filtered;
            for (double center : centers) {
                const Pt p = pointAt(center);
                const bool topBottom = std::fabs(p.y - box.ymax) <= tolerance ||
                                       std::fabs(p.y - box.ymin) <= tolerance;
                const bool leftRight = std::fabs(p.x - box.xmin) <= tolerance ||
                                       std::fabs(p.x - box.xmax) <= tolerance;
                if ((sides == TabSides::TopBottom && topBottom) ||
                    (sides == TabSides::LeftRight && leftRight))
                    filtered.push_back(center);
            }
            if (!filtered.empty()) centers = std::move(filtered);
        }
    }
    if (width * static_cast<double>(centers.size()) >= length * 0.9) {
        out.push_back(closed);
        return out;
    }

    struct Span {
        double start;
        double end;
    };
    std::vector<Span> tabs;
    tabs.reserve(centers.size());
    for (double center : centers) {
        double start = center - width / 2.0;
        double end = center + width / 2.0;
        while (start < 0.0) {
            start += length;
            end += length;
        }
        if (end > length) end -= length;
        tabs.push_back({start, end});
    }
    std::sort(tabs.begin(), tabs.end(), [](const Span& a, const Span& b) { return a.start < b.start; });

    for (std::size_t k = 0; k < tabs.size(); ++k) {
        const Span& tab = tabs[k];
        const Span& next = tabs[(k + 1) % tabs.size()];
        double a = tab.end;
        double b = next.start;
        if (b <= a) b += length;

        Ring seg;
        seg.push_back(pointAt(a));
        std::vector<std::pair<double, Pt>> candidates;
        for (std::size_t i = 0; i < n; ++i) {
            for (double offset : {0.0, length}) {
                const double t = s[i] + offset;
                if (t > a && t < b) candidates.emplace_back(t, pts[i]);
            }
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const auto& p1, const auto& p2) { return p1.first < p2.first; });
        for (const auto& [t, point] : candidates) {
            (void)t;
            seg.push_back(point);
        }
        seg.push_back(pointAt(b));
        if (seg.size() >= 2) out.push_back(std::move(seg));
    }
    return out;
}

/// @brief Agrega `ring` como recorrido, invirtiendo el sentido si `invert`.
///
/// Rechaza geometrías inválidas o vacías y acumula la longitud real del
/// recorrido en `result.totalLength`.
void appendPath(CutoutResult& result, const Ring& ring, bool invert) {
    Ring pts = ring;
    if (invert) std::reverse(pts.begin(), pts.end());
    Geom path = Geom::line(pts);
    if (!path.valid() || path.empty()) return;
    result.totalLength += path.length();
    result.paths.push_back(std::move(path));
}

}  // namespace

/**
 * @brief Calcula los recorridos de corte de un contorno de placa.
 *
 * El desplazamiento del contorno se calcula como:
 * - corte externo: `margin + Ø/2` (la fresa pasa por fuera, dejando la pieza
 *   al tamaño nominal más el margen);
 * - corte interno: `margin − Ø/2` (recorte dentro del contorno).
 *
 * Luego une el contorno consigo mismo (`unaryUnion`) para eliminar solapes y
 * multiplicidades, aplica el buffer, recorta contra `options.area` si existe y
 * extrae los recorridos: primero el contorno exterior partido por puentes y,
 * si `includeHoles`, también los agujeros internos partidos de la misma forma.
 *
 * @param outline Contorno de la placa (sólidos del Gerber de borde).
 * @param options Herramienta, margen, tipo de corte, puentes, área e inversión.
 * @return Envolvente desplazada, recorridos del centro de la fresa, longitud
 *         total y límites de la envolvente.
 *
 * @note Si `unaryUnion` falla o queda vacía se usa el contorno original, de
 *       modo que el corte sigue siendo posible con geometría problemática.
 * @note `includeHoles` está desactivado por defecto: los contornos internos
 *       del trazo de borde no deben cortarse salvo pedido explícito.
 * @note `totalLength` cuenta cada tramo por separado; no incluye los rápidos
 *       entre tramos.
 * @note `ringLength` suma el lado de cierre cuando el anillo no repite el
 *       primer punto, de modo que el perímetro usado para repartir los puentes
 *       coincide con el real.
 * @warning Con `external == false` y un margen menor que el radio, el buffer
 *          negativo puede vaciar la geometría y el resultado queda vacío.
 */
CutoutResult cutoutGeometry(const Geom& outline, const CutoutOptions& options) {
    CutoutResult result;
    if (!outline.valid() || outline.empty()) return result;

    const double radius = options.toolDiameter / 2.0;
    const double offset = options.external ? options.margin + radius : options.margin - radius;

    Geom merged = outline.unaryUnion();
    if (!merged.valid() || merged.empty()) merged = outline;

    Geom envelope = offset == 0.0 ? merged : merged.buffer(offset, std::max(2, options.quadrantSegments));
    if (!envelope.valid() || envelope.empty()) return result;

    if (options.area) {
        envelope = envelope.intersection(Geom::box(options.area->xmin, options.area->ymin,
                                                   options.area->xmax, options.area->ymax));
        if (!envelope.valid() || envelope.empty()) return result;
    }
    result.envelope = envelope;

    for (const PolygonContours& pc : envelope.polygons()) {
        const double outerLength = ringLength(pc.outer);
        for (const Ring& segment : splitWithTabs(pc.outer, options.tabWidth, options.tabCount,
                                                outerLength, options.tabSides,
                                                options.tabPositions)) {
            appendPath(result, segment, options.invert);
        }
        if (options.includeHoles) {
            for (const Ring& holeRing : pc.holes) {
                const double holeLength = ringLength(holeRing);
                for (const Ring& segment : splitWithTabs(holeRing, options.tabWidth,
                                                         options.tabCount, holeLength,
                                                         options.tabSides,
                                                         options.tabPositions)) {
                    appendPath(result, segment, options.invert);
                }
            }
        }
    }

    addBounds(result.bounds, envelope.bounds());
    return result;
}

}  // namespace fc
