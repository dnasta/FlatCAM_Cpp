#include "flatcam/ncc.hpp"

#include "flatcam/paint.hpp"

#include <algorithm>
#include <cmath>

namespace fc {
namespace {

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

NccResult clearCopper(const Geom& copper, const Geom& area, const NccOptions& options) {
    NccResult result;
    if (!copper.valid() || copper.empty()) return result;

    const double radius = options.toolDiameter / 2.0;
    const double clearance = std::max(0.0, options.clearance);
    const double spacing = std::max(0.05, options.stepOver * options.toolDiameter);

    Geom workArea = area;
    if (!workArea.valid() || workArea.empty()) {
        const BBox b = copper.bounds();
        if (!b.valid) return result;
        const double m = options.margin + spacing;
        workArea = Geom::box(b.xmin - m, b.ymin - m, b.xmax + m, b.ymax + m);
    }

    Geom keepOut = copper.buffer(radius + clearance, std::max(2, options.quadrantSegments));
    Geom region = workArea.difference(keepOut);
    if (!region.valid() || region.empty()) return result;
    result.clearedArea = region;

    if (options.strategy == NccStrategy::Paint) {
        PaintOptions paint;
        paint.toolDiameter = options.toolDiameter;
        paint.stepOver = options.stepOver;
        paint.quadrantSegments = options.quadrantSegments;
        const PaintResult painted = paintArea(region, paint);
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
    for (double y = bounds.ymin - options.margin; y <= bounds.ymax + options.margin + 1e-9;
         y += spacing) {
        const double localY = y - cy;
        const double x0 = -diag;
        const double x1 = diag;
        // Rotate the line around the centre of the work area.
        const Pt p0{cx + (x0 * c - localY * s), cy + (x0 * s + localY * c)};
        const Pt p1{cx + (x1 * c - localY * s), cy + (x1 * s + localY * c)};

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

    return result;
}

}  // namespace fc
