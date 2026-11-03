#include "flatcam/svg.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fc {

void exportSvg(const std::string& path, const std::vector<Geom>& solids,
               const std::vector<Geom>& follows, const BBox& bounds) {
    BBox b = bounds;
    if (!b.valid) {
        for (const Geom& g : solids) {
            const BBox gb = g.bounds();
            if (!gb.valid) continue;
            if (!b.valid) b = gb;
            else {
                b.xmin = std::min(b.xmin, gb.xmin);
                b.ymin = std::min(b.ymin, gb.ymin);
                b.xmax = std::max(b.xmax, gb.xmax);
                b.ymax = std::max(b.ymax, gb.ymax);
            }
        }
    }
    if (!b.valid) return;

    const double w = std::max(1e-6, b.width());
    const double h = std::max(1e-6, b.height());

    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << w << "mm\" height=\"" << h
        << "mm\" viewBox=\"" << b.xmin << " " << b.ymin << " " << w << " " << h << "\">\n";
    out << "<g transform=\"translate(0 " << (b.ymin + b.ymax) << ") scale(1 -1)\">\n";

    out << "<g fill=\"#c8952e\" fill-rule=\"evenodd\" stroke=\"none\">\n";
    for (const Geom& g : solids) {
        for (const PolygonContours& pc : g.polygons()) {
            if (pc.outer.size() < 3) continue;
            out << "<path d=\"M";
            for (std::size_t i = 0; i < pc.outer.size(); ++i) {
                out << (i ? " L" : " ") << pc.outer[i].x << " " << pc.outer[i].y;
            }
            out << " Z";
            for (const Ring& hole : pc.holes) {
                if (hole.size() < 3) continue;
                out << " M";
                for (std::size_t i = 0; i < hole.size(); ++i) {
                    out << (i ? " L" : " ") << hole[i].x << " " << hole[i].y;
                }
                out << " Z";
            }
            out << "\"/>\n";
        }
    }
    out << "</g>\n";

    out << "<g fill=\"none\" stroke=\"#4a90d9\" stroke-width=\"0.1\">\n";
    for (const Geom& g : follows) {
        if (!g.valid()) continue;
        const Ring pts = g.coords();
        if (pts.size() < 2) continue;
        out << "<polyline points=\"";
        for (const Pt& p : pts) out << p.x << "," << p.y << " ";
        out << "\"/>\n";
    }
    out << "</g>\n</g>\n</svg>\n";

    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot write SVG: " + path);
    file << out.str();
}

}  // namespace fc
