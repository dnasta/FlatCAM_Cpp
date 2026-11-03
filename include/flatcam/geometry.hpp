#pragma once

#include <geos_c.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fc {

struct Pt {
    double x = 0.0;
    double y = 0.0;
};

using Ring = std::vector<Pt>;

struct BBox {
    double xmin = 0.0;
    double ymin = 0.0;
    double xmax = 0.0;
    double ymax = 0.0;
    bool valid = false;

    BBox() = default;
    BBox(double x0, double y0, double x1, double y1) : xmin(x0), ymin(y0), xmax(x1), ymax(y1), valid(true) {}

    double width() const { return xmax - xmin; }
    double height() const { return ymax - ymin; }
    bool contains(const Pt& p) const {
        return valid && p.x >= xmin && p.x <= xmax && p.y >= ymin && p.y <= ymax;
    }
    BBox expanded(double margin) const {
        if (!valid) return *this;
        return BBox{xmin - margin, ymin - margin, xmax + margin, ymax + margin};
    }
};

struct PolygonContours {
    Ring outer;
    std::vector<Ring> holes;
};

// Immutable, shared owning wrapper around a GEOS geometry.
// Copies are cheap; every operation returns a new object.
class Geom {
public:
    Geom() = default;

    static Geom adopt(GEOSGeometry* g);
    static Geom emptyPolygon();  // empty polygon

    static Geom point(double x, double y);
    static Geom line(const Ring& pts);
    static Geom polygon(const Ring& shell, const std::vector<Ring>& holes = {});
    static Geom box(double xmin, double ymin, double xmax, double ymax);
    static Geom circle(const Pt& center, double radius, int quadSegs = 16);
    static Geom obround(const Pt& center, double width, double height, int quadSegs = 16);
    static Geom regularPolygon(const Pt& center, double diameter, int vertices, double rotationDeg = 0.0);
    static Geom collection(const std::vector<Geom>& parts, int type = GEOS_MULTIPOLYGON);
    static Geom unionOf(const std::vector<Geom>& parts);
    static Geom fromWkt(const std::string& wkt);

    bool valid() const { return static_cast<bool>(h_); }
    bool empty() const;
    double area() const;
    double length() const;
    BBox bounds() const;
    bool isValid() const;
    int typeId() const;
    int numGeometries() const;
    Geom geometryN(int i) const;
    int numPoints() const;
    Ring coords() const;

    std::vector<PolygonContours> polygons() const;

    Geom buffer(double distance, int quadSegs = 16) const;
    Geom bufferStyle(double distance, int quadSegs, int capStyle, int joinStyle,
                     double mitreLimit = 5.0) const;
    Geom unite(const Geom& other) const;
    Geom difference(const Geom& other) const;
    Geom intersection(const Geom& other) const;
    bool intersects(const Geom& other) const;
    double distance(const Geom& other) const;
    // Closest pair of points between this geometry and another one.
    std::pair<Pt, Pt> nearestPoints(const Geom& other) const;
    Geom unaryUnion() const;
    Geom convexHull() const;
    Geom simplify(double tolerance) const;
    Geom fixed() const;  // buffer(0): repairs self-intersecting polygons

    Geom transformXY(const std::function<Pt(Pt)>& fn) const;
    Geom translate(double dx, double dy) const;
    Geom rotateDeg(double angle, const Pt& origin = {0.0, 0.0}) const;
    Geom scale(double sx, double sy, const Pt& origin = {0.0, 0.0}) const;

    std::string toWkt(int decimals = 9) const;
    GEOSGeometry* raw() const { return h_.get(); }

private:
    using Handle = std::shared_ptr<GEOSGeometry>;
    Handle h_;

    explicit Geom(Handle h) : h_(std::move(h)) {}
};

// Spatial index wrapping GEOS' STRtree.
class SpatialIndex {
public:
    SpatialIndex();
    ~SpatialIndex();
    SpatialIndex(const SpatialIndex&) = delete;
    SpatialIndex& operator=(const SpatialIndex&) = delete;

    void insert(const Geom& g, std::uint32_t id);
    void build();
    std::vector<std::uint32_t> query(const BBox& box) const;

private:
    GEOSSTRtree* tree_ = nullptr;
    std::vector<Geom> keep_;
    std::vector<std::uint32_t> ids_;
    bool built_ = false;
};

std::string geometryTypeName(int typeId);

// Initializes the (non-reentrant) GEOS runtime. Called automatically by every factory.
void initGeosRuntime();

}  // namespace fc
