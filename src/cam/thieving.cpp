#include "flatcam/thieving.hpp"

#include <algorithm>
#include <cmath>

namespace fc {

std::vector<Pt> thievingPoints(const Geom& copper, const BBox& area,
                               const ThievingOptions& options) {
    std::vector<Pt> points;
    if (!area.valid) return points;

    const double radius = options.dotDiameter / 2.0;
    const double spacing = std::max(0.1, options.spacing);
    const Geom keepOut = copper.valid() ? copper.buffer(radius + options.clearance, 8) : Geom();

    const double margin = options.margin;
    for (double y = area.ymin + margin + spacing / 2.0; y <= area.ymax - margin; y += spacing) {
        for (double x = area.xmin + margin + spacing / 2.0; x <= area.xmax - margin; x += spacing) {
            const Geom dot = Geom::circle({x, y}, radius, 8);
            if (keepOut.valid() && keepOut.intersects(dot)) continue;
            points.push_back({x, y});
        }
    }
    return points;
}

}  // namespace fc
