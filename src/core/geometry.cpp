#include "flatcam/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace fc {
namespace {

void geosNotice(const char*, ...) {}
void geosError(const char*, ...) {}


GEOSGeometry* createEmpty() {
    GEOSGeometry* g = GEOSGeom_createEmptyPolygon();
    if (!g) g = GEOSGeom_createEmptyCollection(GEOS_GEOMETRYCOLLECTION);
    return g;
}

Ring readRing(const GEOSGeometry* ring) {
    Ring out;
    const GEOSCoordSequence* cs = GEOSGeom_getCoordSeq(ring);
    if (!cs) return out;
    unsigned int n = 0;
    if (!GEOSCoordSeq_getSize(cs, &n)) return out;
    out.reserve(n);
    for (unsigned int i = 0; i < n; ++i) {
        double x = 0.0;
        double y = 0.0;
        if (GEOSCoordSeq_getXY(cs, i, &x, &y)) out.push_back({x, y});
    }
    return out;
}

GEOSGeometry* makeRing(const Ring& pts) {
    if (pts.size() < 3) throw std::invalid_argument("ring needs at least 3 points");
    Ring closed = pts;
    if (closed.front().x != closed.back().x || closed.front().y != closed.back().y) {
        closed.push_back(closed.front());
    }
    GEOSCoordSequence* seq = GEOSCoordSeq_create(static_cast<unsigned int>(closed.size()), 2);
    if (!seq) throw std::runtime_error("GEOSCoordSeq_create failed");
    for (std::size_t i = 0; i < closed.size(); ++i) {
        if (!GEOSCoordSeq_setXY(seq, static_cast<unsigned int>(i), closed[i].x, closed[i].y)) {
            GEOSCoordSeq_destroy(seq);
            throw std::runtime_error("GEOSCoordSeq_setXY failed");
        }
    }
    GEOSGeometry* ring = GEOSGeom_createLinearRing(seq);
    if (!ring) {
        GEOSCoordSeq_destroy(seq);
        throw std::runtime_error("GEOSGeom_createLinearRing failed");
    }
    return ring;
}

struct TransformData {
    const std::function<Pt(Pt)>* fn;
};

int transformCallback(double* x, double* y, void* userdata) {
    auto* data = static_cast<TransformData*>(userdata);
    const Pt p = (*data->fn)({*x, *y});
    *x = p.x;
    *y = p.y;
    return 1;
}

Geom require(GEOSGeometry* g) {
    if (!g) throw std::runtime_error("GEOS operation failed");
    return Geom::adopt(g);
}

}  // namespace

// ---------------------------------------------------------------- construction

void initGeosRuntime() {
    static const bool initialized = [] {
        initGEOS(geosNotice, geosError);
        return true;
    }();
    (void)initialized;
}

Geom Geom::adopt(GEOSGeometry* g) {
    initGeosRuntime();
    if (!g) return Geom();
    return Geom(Handle(g, GEOSGeom_destroy));
}

Geom Geom::emptyPolygon() {
    initGeosRuntime();
    return adopt(createEmpty());
}

Geom Geom::point(double x, double y) {
    initGeosRuntime();
    GEOSCoordSequence* seq = GEOSCoordSeq_create(1, 2);
    if (!seq) throw std::runtime_error("GEOSCoordSeq_create failed");
    if (!GEOSCoordSeq_setXY(seq, 0, x, y)) {
        GEOSCoordSeq_destroy(seq);
        throw std::runtime_error("GEOSCoordSeq_setXY failed");
    }
    GEOSGeometry* g = GEOSGeom_createPoint(seq);
    if (!g) {
        GEOSCoordSeq_destroy(seq);
        throw std::runtime_error("GEOSGeom_createPoint failed");
    }
    return adopt(g);
}

Geom Geom::line(const Ring& pts) {
    initGeosRuntime();
    if (pts.size() < 2) {
        GEOSGeometry* g = GEOSGeom_createEmptyLineString();
        if (!g) throw std::runtime_error("GEOSGeom_createEmptyLineString failed");
        return adopt(g);
    }
    GEOSCoordSequence* seq = GEOSCoordSeq_create(static_cast<unsigned int>(pts.size()), 2);
    if (!seq) throw std::runtime_error("GEOSCoordSeq_create failed");
    for (std::size_t i = 0; i < pts.size(); ++i) {
        if (!GEOSCoordSeq_setXY(seq, static_cast<unsigned int>(i), pts[i].x, pts[i].y)) {
            GEOSCoordSeq_destroy(seq);
            throw std::runtime_error("GEOSCoordSeq_setXY failed");
        }
    }
    GEOSGeometry* g = GEOSGeom_createLineString(seq);
    if (!g) {
        GEOSCoordSeq_destroy(seq);
        throw std::runtime_error("GEOSGeom_createLineString failed");
    }
    return adopt(g);
}

Geom Geom::polygon(const Ring& shell, const std::vector<Ring>& holes) {
    initGeosRuntime();
    if (shell.size() < 3) return emptyPolygon();
    GEOSGeometry* shellRing = makeRing(shell);
    std::vector<GEOSGeometry*> holeRings;
    holeRings.reserve(holes.size());
    try {
        for (const Ring& h : holes) {
            if (h.size() >= 3) holeRings.push_back(makeRing(h));
        }
    } catch (...) {
        for (GEOSGeometry* h : holeRings) GEOSGeom_destroy(h);
        GEOSGeom_destroy(shellRing);
        throw;
    }
    GEOSGeometry* g = GEOSGeom_createPolygon(
        shellRing, holeRings.empty() ? nullptr : holeRings.data(),
        static_cast<unsigned int>(holeRings.size()));
    if (!g) {
        GEOSGeom_destroy(shellRing);
        for (GEOSGeometry* h : holeRings) GEOSGeom_destroy(h);
        throw std::runtime_error("GEOSGeom_createPolygon failed");
    }
    return adopt(g);
}

Geom Geom::box(double xmin, double ymin, double xmax, double ymax) {
    return polygon({{xmin, ymin}, {xmax, ymin}, {xmax, ymax}, {xmin, ymax}, {xmin, ymin}});
}

Geom Geom::circle(const Pt& center, double radius, int quadSegs) {
    return point(center.x, center.y).buffer(radius, quadSegs);
}

Geom Geom::obround(const Pt& center, double width, double height, int quadSegs) {
    if (width <= 0.0 || height <= 0.0) return emptyPolygon();
    const double r = std::min(width, height) / 2.0;
    Pt a = center;
    Pt b = center;
    if (width > height) {
        a.x += (width - height) / 2.0;
        b.x -= (width - height) / 2.0;
    } else {
        a.y += (height - width) / 2.0;
        b.y -= (height - width) / 2.0;
    }
    return circle(a, r, quadSegs).unite(circle(b, r, quadSegs)).convexHull();
}

Geom Geom::regularPolygon(const Pt& center, double diameter, int vertices, double rotationDeg) {
    if (vertices < 3 || diameter <= 0.0) return emptyPolygon();
    Ring pts;
    pts.reserve(static_cast<std::size_t>(vertices));
    const double r = diameter / 2.0;
    for (int i = 0; i < vertices; ++i) {
        const double a = 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(vertices);
        pts.push_back({center.x + r * std::cos(a), center.y + r * std::sin(a)});
    }
    Geom g = polygon(pts);
    if (rotationDeg != 0.0) g = g.rotateDeg(rotationDeg, center);
    return g;
}

Geom Geom::fromWkt(const std::string& wkt) {
    initGeosRuntime();
    GEOSWKTReader* reader = GEOSWKTReader_create();
    if (!reader) throw std::runtime_error("GEOSWKTReader_create failed");
    GEOSGeometry* g = GEOSWKTReader_read(reader, wkt.c_str());
    GEOSWKTReader_destroy(reader);
    if (!g) throw std::runtime_error("invalid WKT geometry");
    return adopt(g);
}

std::string Geom::toWkt(int decimals) const {
    if (!h_) return "POLYGON EMPTY";
    initGeosRuntime();
    GEOSWKTWriter* writer = GEOSWKTWriter_create();
    if (!writer) throw std::runtime_error("GEOSWKTWriter_create failed");
    GEOSWKTWriter_setTrim(writer, 1);
    GEOSWKTWriter_setRoundingPrecision(writer, decimals);
    char* text = GEOSWKTWriter_write(writer, h_.get());
    GEOSWKTWriter_destroy(writer);
    if (!text) throw std::runtime_error("GEOSWKTWriter_write failed");
    std::string out(text);
    GEOSFree(text);
    return out;
}

Geom Geom::collection(const std::vector<Geom>& parts, int type) {
    std::vector<GEOSGeometry*> raw;
    raw.reserve(parts.size());
    for (const Geom& p : parts) {
        if (p.valid()) raw.push_back(GEOSGeom_clone(p.raw()));
    }
    if (raw.empty()) return emptyPolygon();
    GEOSGeometry* g = GEOSGeom_createCollection(type, raw.data(), static_cast<unsigned int>(raw.size()));
    if (!g) {
        for (GEOSGeometry* p : raw) GEOSGeom_destroy(p);
        throw std::runtime_error("GEOSGeom_createCollection failed");
    }
    return adopt(g);
}

Geom Geom::unionOf(const std::vector<Geom>& parts) {
    if (parts.empty()) return emptyPolygon();
    if (parts.size() == 1) return parts.front();
    std::vector<Geom> valid;
    valid.reserve(parts.size());
    for (const Geom& p : parts) {
        if (p.valid() && !p.empty()) valid.push_back(p);
    }
    if (valid.empty()) return emptyPolygon();
    if (valid.size() == 1) return valid.front();
    return Geom::collection(valid, GEOS_MULTIPOLYGON).unaryUnion();
}

// ---------------------------------------------------------------- properties

bool Geom::empty() const { return !h_ || GEOSisEmpty(h_.get()) == 1; }

double Geom::area() const {
    if (!h_) return 0.0;
    double a = 0.0;
    if (!GEOSArea(h_.get(), &a)) return 0.0;
    return a;
}

double Geom::length() const {
    if (!h_) return 0.0;
    double l = 0.0;
    if (!GEOSLength(h_.get(), &l)) return 0.0;
    return l;
}

BBox Geom::bounds() const {
    BBox b;
    if (!h_ || GEOSisEmpty(h_.get()) == 1) return b;
    double xmin = 0.0, ymin = 0.0, xmax = 0.0, ymax = 0.0;
    if (GEOSGeom_getXMin(h_.get(), &xmin) && GEOSGeom_getYMin(h_.get(), &ymin) &&
        GEOSGeom_getXMax(h_.get(), &xmax) && GEOSGeom_getYMax(h_.get(), &ymax)) {
        b = BBox{xmin, ymin, xmax, ymax};
    }
    return b;
}

bool Geom::isValid() const { return h_ && GEOSisValid(h_.get()) == 1; }

int Geom::typeId() const { return h_ ? GEOSGeomTypeId(h_.get()) : -1; }

int Geom::numGeometries() const { return h_ ? GEOSGetNumGeometries(h_.get()) : 0; }

Geom Geom::geometryN(int i) const {
    if (!h_) return Geom();
    const GEOSGeometry* g = GEOSGetGeometryN(h_.get(), i);
    if (!g) return Geom();
    return adopt(GEOSGeom_clone(g));
}

int Geom::numPoints() const {
    if (!h_) return 0;
    const GEOSGeometry* g = h_.get();
    const GEOSGeometry* target = g;
    if (GEOSGeomTypeId(g) == GEOS_POLYGON) target = GEOSGetExteriorRing(g);
    const GEOSCoordSequence* cs = GEOSGeom_getCoordSeq(target);
    if (!cs) return 0;
    unsigned int n = 0;
    GEOSCoordSeq_getSize(cs, &n);
    return static_cast<int>(n);
}

Ring Geom::coords() const {
    if (!h_) return {};
    const GEOSGeometry* g = h_.get();
    const GEOSGeometry* target = g;
    if (GEOSGeomTypeId(g) == GEOS_POLYGON) target = GEOSGetExteriorRing(g);
    return readRing(target);
}

std::vector<PolygonContours> Geom::polygons() const {
    std::vector<PolygonContours> out;
    if (!h_ || GEOSisEmpty(h_.get()) == 1) return out;

    switch (GEOSGeomTypeId(h_.get())) {
        case GEOS_POLYGON: {
            const GEOSGeometry* ext = GEOSGetExteriorRing(h_.get());
            if (!ext) break;
            PolygonContours pc;
            pc.outer = readRing(ext);
            if (pc.outer.size() >= 3) {
                const int n = GEOSGetNumInteriorRings(h_.get());
                for (int i = 0; i < n; ++i) {
                    const GEOSGeometry* inner = GEOSGetInteriorRingN(h_.get(), i);
                    if (inner) {
                        Ring hole = readRing(inner);
                        if (hole.size() >= 3) pc.holes.push_back(std::move(hole));
                    }
                }
                out.push_back(std::move(pc));
            }
            break;
        }
        case GEOS_MULTIPOLYGON:
        case GEOS_GEOMETRYCOLLECTION: {
            const int n = numGeometries();
            for (int i = 0; i < n; ++i) {
                const GEOSGeometry* part = GEOSGetGeometryN(h_.get(), i);
                if (!part) continue;
                std::vector<PolygonContours> sub = adopt(GEOSGeom_clone(part)).polygons();
                out.insert(out.end(), sub.begin(), sub.end());
            }
            break;
        }
        default:
            break;
    }
    return out;
}

// ---------------------------------------------------------------- operations

Geom Geom::buffer(double distance, int quadSegs) const {
    if (!h_) return Geom();
    return require(GEOSBuffer(h_.get(), distance, quadSegs));
}

Geom Geom::bufferStyle(double distance, int quadSegs, int capStyle, int joinStyle,
                       double mitreLimit) const {
    if (!h_) return Geom();
    return require(GEOSBufferWithStyle(h_.get(), distance, quadSegs, capStyle, joinStyle, mitreLimit));
}

Geom Geom::unite(const Geom& other) const {
    if (!h_ || !other.h_) return {};
    return require(GEOSUnion(h_.get(), other.h_.get()));
}

Geom Geom::difference(const Geom& other) const {
    if (!h_ || !other.h_) return {};
    return require(GEOSDifference(h_.get(), other.h_.get()));
}

Geom Geom::intersection(const Geom& other) const {
    if (!h_ || !other.h_) return {};
    return require(GEOSIntersection(h_.get(), other.h_.get()));
}

std::pair<Pt, Pt> Geom::nearestPoints(const Geom& other) const {
    initGeosRuntime();
    if (!h_ || !other.h_) return {{0.0, 0.0}, {0.0, 0.0}};
    GEOSCoordSequence* seq = GEOSNearestPoints(h_.get(), other.h_.get());
    if (!seq) return {{0.0, 0.0}, {0.0, 0.0}};
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
    GEOSCoordSeq_getXY(seq, 0, &x0, &y0);
    GEOSCoordSeq_getXY(seq, 1, &x1, &y1);
    GEOSCoordSeq_destroy(seq);
    return {{x0, y0}, {x1, y1}};
}

double Geom::distance(const Geom& other) const {
    if (!h_ || !other.h_) return 0.0;
    double d = 0.0;
    if (!GEOSDistance(h_.get(), other.h_.get(), &d)) return 0.0;
    return d;
}

bool Geom::intersects(const Geom& other) const {
    if (!h_ || !other.h_) return false;
    return GEOSIntersects(h_.get(), other.h_.get()) == 1;
}

Geom Geom::unaryUnion() const {
    if (!h_) return {};
    return require(GEOSUnaryUnion(h_.get()));
}

Geom Geom::convexHull() const {
    if (!h_) return {};
    return require(GEOSConvexHull(h_.get()));
}

Geom Geom::simplify(double tolerance) const {
    if (!h_) return {};
    return require(GEOSSimplify(h_.get(), tolerance));
}

Geom Geom::fixed() const {
    if (!h_) return {};
    if (isValid()) return *this;
    return buffer(0.0, 8);
}

Geom Geom::translate(double dx, double dy) const {
    return transformXY([dx, dy](Pt p) { return Pt{p.x + dx, p.y + dy}; });
}

Geom Geom::rotateDeg(double angle, const Pt& origin) const {
    const double a = angle * M_PI / 180.0;
    const double c = std::cos(a);
    const double s = std::sin(a);
    return transformXY([c, s, origin](Pt p) {
        const double dx = p.x - origin.x;
        const double dy = p.y - origin.y;
        return Pt{origin.x + dx * c - dy * s, origin.y + dx * s + dy * c};
    });
}

Geom Geom::scale(double sx, double sy, const Pt& origin) const {
    return transformXY([sx, sy, origin](Pt p) {
        return Pt{origin.x + (p.x - origin.x) * sx, origin.y + (p.y - origin.y) * sy};
    });
}

// transformXY is used by translate/rotate/scale and by the parsers.
Geom Geom::transformXY(const std::function<Pt(Pt)>& fn) const {
    if (!h_) return {};
    TransformData data{&fn};
    return require(GEOSGeom_transformXY(h_.get(), transformCallback, &data));
}

std::string geometryTypeName(int typeId) {
    switch (typeId) {
        case GEOS_POINT: return "Point";
        case GEOS_LINESTRING: return "LineString";
        case GEOS_LINEARRING: return "LinearRing";
        case GEOS_POLYGON: return "Polygon";
        case GEOS_MULTIPOINT: return "MultiPoint";
        case GEOS_MULTILINESTRING: return "MultiLineString";
        case GEOS_MULTIPOLYGON: return "MultiPolygon";
        case GEOS_GEOMETRYCOLLECTION: return "GeometryCollection";
        default: return "Unknown";
    }
}

}  // namespace fc
