#include "flatcam/dxf.hpp"

#include <fstream>
#include <sstream>

namespace fc {
namespace {

void writePolyline(std::ostringstream& out, const Ring& pts, bool closed) {
    if (pts.size() < 2) return;
    out << "0\nLWPOLYLINE\n8\n0\n90\n" << pts.size() << "\n70\n" << (closed ? 1 : 0) << "\n";
    for (const Pt& p : pts) {
        out << "10\n" << p.x << "\n20\n" << p.y << "\n";
    }
}

}  // namespace

void exportDxf(const std::string& path, const std::vector<Geom>& solids,
               const std::vector<Geom>& follows) {
    std::ostringstream out;
    out << "0\nSECTION\n2\nHEADER\n";
    out << "9\n$ACADVER\n1\nAC1015\n";
    out << "9\n$INSUNITS\n70\n4\n";
    out << "0\nENDSEC\n";
    out << "0\nSECTION\n2\nENTITIES\n";

    for (const Geom& g : solids) {
        for (const PolygonContours& pc : g.polygons()) {
            writePolyline(out, pc.outer, true);
            for (const Ring& hole : pc.holes) writePolyline(out, hole, true);
        }
    }
    for (const Geom& g : follows) {
        if (!g.valid() || g.empty()) continue;
        if (g.typeId() == GEOS_LINESTRING || g.typeId() == GEOS_LINEARRING) {
            writePolyline(out, g.coords(), false);
        } else {
            for (int i = 0; i < g.numGeometries(); ++i) {
                const Geom part = g.geometryN(i);
                if (part.typeId() == GEOS_LINESTRING || part.typeId() == GEOS_LINEARRING)
                    writePolyline(out, part.coords(), false);
            }
        }
    }

    out << "0\nENDSEC\n0\nEOF\n";

    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot write DXF: " + path);
    file << out.str();
}

}  // namespace fc
