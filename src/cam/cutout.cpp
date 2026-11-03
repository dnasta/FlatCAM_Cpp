#include "flatcam/cutout.hpp"

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

double ringLength(const Ring& ring) {
    double total = 0.0;
    for (std::size_t i = 1; i < ring.size(); ++i) {
        total += std::hypot(ring[i].x - ring[i - 1].x, ring[i].y - ring[i - 1].y);
    }
    return total;
}

// Splits a closed ring into open polylines, removing `count` spans of
// `width` millimetres evenly distributed along the perimeter (tabs).
std::vector<Ring> splitWithTabs(const Ring& closed, double width, int count, double totalLength) {
    std::vector<Ring> out;
    if (count <= 0 || width <= 0.0 || closed.size() < 4) {
        out.push_back(closed);
        return out;
    }
    if (totalLength <= 0.0 || width * count >= totalLength * 0.9) {
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

    struct Span {
        double start;
        double end;
    };
    std::vector<Span> tabs;
    tabs.reserve(static_cast<std::size_t>(count));
    for (int k = 0; k < count; ++k) {
        const double center = (k + 0.5) * length / count;
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

void appendPath(CutoutResult& result, const Ring& ring, bool invert) {
    Ring pts = ring;
    if (invert) std::reverse(pts.begin(), pts.end());
    Geom path = Geom::line(pts);
    if (!path.valid() || path.empty()) return;
    result.totalLength += path.length();
    result.paths.push_back(std::move(path));
}

}  // namespace

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
        for (const Ring& segment : splitWithTabs(pc.outer, options.tabWidth, options.tabCount, outerLength)) {
            appendPath(result, segment, options.invert);
        }
        if (options.includeHoles) {
            for (const Ring& holeRing : pc.holes) {
                const double holeLength = ringLength(holeRing);
                for (const Ring& segment :
                     splitWithTabs(holeRing, options.tabWidth, options.tabCount, holeLength)) {
                    appendPath(result, segment, options.invert);
                }
            }
        }
    }

    addBounds(result.bounds, envelope.bounds());
    return result;
}

}  // namespace fc
