#include "flatcam/paint.hpp"

#include <algorithm>
#include <cmath>

namespace fc {
namespace {

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

Ring oriented(Ring ring, bool invert) {
    if (invert) std::reverse(ring.begin(), ring.end());
    return ring;
}

}  // namespace

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
