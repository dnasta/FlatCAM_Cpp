#include "flatcam/isolation.hpp"

#include <algorithm>
#include <cmath>

namespace fc {
namespace {

Ring oriented(Ring ring, bool invert) {
    if (invert) std::reverse(ring.begin(), ring.end());
    return ring;
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
