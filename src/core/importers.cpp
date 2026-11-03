#include "flatcam/importers.hpp"

#include "flatcam/excellon.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace fc {
namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

double number(const std::string& text) {
    try {
        return std::stod(text);
    } catch (...) {
        return 0.0;
    }
}

std::vector<double> numbers(const std::string& text) {
    std::vector<double> out;
    std::string current;
    for (char c : text) {
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
            current.push_back(c);
        } else if (!current.empty()) {
            out.push_back(number(current));
            current.clear();
        }
    }
    if (!current.empty()) out.push_back(number(current));
    return out;
}

// Parses a very small subset of the SVG path grammar (M/L/H/V/Z plus the
// endpoint of C/Q/A commands).
void parsePathData(const std::string& data, std::vector<Geom>& out) {
    std::vector<std::string> tokens;
    std::string current;
    for (char c : data) {
        if (std::isalpha(static_cast<unsigned char>(c))) {
            if ((c == 'e' || c == 'E') && !current.empty()) {
                current.push_back(c);
            } else {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
                tokens.emplace_back(1, c);
            }
        } else if (c == '-' || c == '+' || c == '.' || (c >= '0' && c <= '9')) {
            current.push_back(c);
        } else if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) tokens.push_back(current);

    Ring pts;
    double x = 0.0, y = 0.0;
    bool closed = false;
    Pt lastCubic;
    Pt lastQuad;
    bool lastCubicSet = false;
    bool lastQuadSet = false;
    auto flush = [&] {
        if (pts.size() >= 3 && closed) out.push_back(Geom::polygon(pts).fixed());
        else if (pts.size() >= 2) out.push_back(Geom::line(pts));
        pts.clear();
        closed = false;
    };

    std::size_t i = 0;
    while (i < tokens.size()) {
        const std::string& token = tokens[i];
        if (token.size() == 1 && std::isalpha(static_cast<unsigned char>(token[0]))) {
            const char cmd = token[0];
            ++i;
            if (cmd == 'Z' || cmd == 'z') {
                closed = true;
                if (!pts.empty()) pts.push_back(pts.front());
                flush();
                continue;
            }
            const bool relative = std::islower(static_cast<unsigned char>(cmd));
            char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));
            if (upper == 'M' || upper == 'L') {
                if (i + 1 >= tokens.size()) break;
                double nx = number(tokens[i]);
                double ny = number(tokens[i + 1]);
                i += 2;
                if (relative) {
                    nx += x;
                    ny += y;
                }
                x = nx;
                y = ny;
                if (upper == 'M' && !pts.empty()) flush();
                pts.push_back({x, y});
            } else if (upper == 'H' || upper == 'V') {
                if (i >= tokens.size()) break;
                double value = number(tokens[i++]);
                if (upper == 'H') x = relative ? x + value : value;
                else y = relative ? y + value : value;
                pts.push_back({x, y});
            } else if (upper == 'C' || upper == 'Q' || upper == 'A' || upper == 'S' ||
                       upper == 'T') {
                const Pt p0{x, y};
                Pt p1 = p0;
                Pt c1 = p0;
                Pt c2 = p0;
                bool cubic = false;
                bool quadratic = false;
                if (upper == 'C') {
                    if (i + 5 >= tokens.size()) break;
                    c1 = {number(tokens[i++]), number(tokens[i++])};
                    c2 = {number(tokens[i++]), number(tokens[i++])};
                    p1 = {number(tokens[i++]), number(tokens[i++])};
                    if (relative) { c1.x += p0.x; c1.y += p0.y; c2.x += p0.x; c2.y += p0.y; p1.x += p0.x; p1.y += p0.y; }
                    cubic = true;
                } else if (upper == 'S') {
                    if (i + 3 >= tokens.size()) break;
                    c1 = lastCubicSet ? Pt{2 * p0.x - lastCubic.x, 2 * p0.y - lastCubic.y} : p0;
                    c2 = {number(tokens[i++]), number(tokens[i++])};
                    p1 = {number(tokens[i++]), number(tokens[i++])};
                    if (relative) { c2.x += p0.x; c2.y += p0.y; p1.x += p0.x; p1.y += p0.y; }
                    cubic = true;
                } else if (upper == 'Q') {
                    if (i + 3 >= tokens.size()) break;
                    c1 = {number(tokens[i++]), number(tokens[i++])};
                    p1 = {number(tokens[i++]), number(tokens[i++])};
                    if (relative) { c1.x += p0.x; c1.y += p0.y; p1.x += p0.x; p1.y += p0.y; }
                    quadratic = true;
                } else if (upper == 'T') {
                    if (i + 1 >= tokens.size()) break;
                    c1 = lastQuadSet ? Pt{2 * p0.x - lastQuad.x, 2 * p0.y - lastQuad.y} : p0;
                    p1 = {number(tokens[i++]), number(tokens[i++])};
                    if (relative) { p1.x += p0.x; p1.y += p0.y; }
                    quadratic = true;
                } else {  // A
                    if (i + 6 >= tokens.size()) break;
                    const double rx = number(tokens[i++]);
                    const double ry = number(tokens[i++]);
                    const double rotation = number(tokens[i++]);
                    const bool large = number(tokens[i++]) != 0.0;
                    const bool sweep = number(tokens[i++]) != 0.0;
                    p1 = {number(tokens[i++]), number(tokens[i++])};
                    if (relative) { p1.x += p0.x; p1.y += p0.y; }
                    (void)rotation;
                    const double dx = p1.x - p0.x;
                    const double dy = p1.y - p0.y;
                    const double chord = std::hypot(dx, dy);
                    const double radius = std::max({std::fabs(rx), std::fabs(ry), chord / 2.0});
                    double sagitta = radius - std::sqrt(std::max(0.0, radius * radius - chord * chord / 4.0));
                    if (large) sagitta = radius + std::sqrt(std::max(0.0, radius * radius - chord * chord / 4.0));
                    if (!sweep) sagitta = -sagitta;
                    const double nx = chord > 1e-9 ? -dy / chord : 0.0;
                    const double ny = chord > 1e-9 ? dx / chord : 0.0;
                    const Pt mid{(p0.x + p1.x) / 2.0, (p0.y + p1.y) / 2.0};
                    c1 = {mid.x + nx * 2.0 * sagitta, mid.y + ny * 2.0 * sagitta};
                    quadratic = true;
                }

                if (cubic) {
                    for (int k = 1; k <= 16; ++k) {
                        const double t = k / 16.0;
                        const double u = 1.0 - t;
                        const Pt p{u * u * u * p0.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * p1.x,
                                   u * u * u * p0.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * p1.y};
                        pts.push_back(p);
                    }
                    lastCubic = c2;
                    lastCubicSet = true;
                    lastQuadSet = false;
                } else if (quadratic) {
                    for (int k = 1; k <= 12; ++k) {
                        const double t = k / 12.0;
                        const double u = 1.0 - t;
                        const Pt p{u * u * p0.x + 2 * u * t * c1.x + t * t * p1.x,
                                   u * u * p0.y + 2 * u * t * c1.y + t * t * p1.y};
                        pts.push_back(p);
                    }
                    lastQuad = c1;
                    lastQuadSet = true;
                    lastCubicSet = false;
                }
                x = p1.x;
                y = p1.y;
            } else {
                ++i;
            }
        } else {
            ++i;
        }
    }
    if (!pts.empty()) flush();
}

std::string attribute(const std::string& tag, const std::string& name) {
    const std::regex re(name + R"re(\s*=\s*"([^"]*)")re");
    std::smatch match;
    if (std::regex_search(tag, match, re)) return match[1];
    return {};
}

struct SvgMatrix {
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, e = 0.0, f = 0.0;
    Pt apply(const Pt& p) const {
        return {a * p.x + c * p.y + e, b * p.x + d * p.y + f};
    }
    SvgMatrix operator*(const SvgMatrix& o) const {
        return {a * o.a + c * o.b, b * o.a + d * o.b,
                a * o.c + c * o.d, b * o.c + d * o.d,
                a * o.e + c * o.f + e, b * o.e + d * o.f + f};
    }
};

SvgMatrix parseSvgTransform(const std::string& text) {
    SvgMatrix result;
    const std::regex op(R"((translate|scale|rotate|matrix)\s*\(([^)]*)\))");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), op);
         it != std::sregex_iterator(); ++it) {
        const std::string name = (*it)[1];
        const std::vector<double> v = numbers((*it)[2]);
        SvgMatrix m;
        if (name == "translate" && !v.empty()) {
            m.e = v[0];
            m.f = v.size() > 1 ? v[1] : 0.0;
        } else if (name == "scale" && !v.empty()) {
            m.a = v[0];
            m.d = v.size() > 1 ? v[1] : v[0];
        } else if (name == "rotate" && !v.empty()) {
            const double angle = v[0] * M_PI / 180.0;
            const double cs = std::cos(angle);
            const double sn = std::sin(angle);
            if (v.size() >= 3) {
                m = SvgMatrix{1, 0, 0, 1, v[1], v[2]} *
                    SvgMatrix{cs, sn, -sn, cs, 0, 0} *
                    SvgMatrix{1, 0, 0, 1, -v[1], -v[2]};
            } else {
                m = SvgMatrix{cs, sn, -sn, cs, 0, 0};
            }
        } else if (name == "matrix" && v.size() >= 6) {
            m = SvgMatrix{v[0], v[1], v[2], v[3], v[4], v[5]};
        }
        result = result * m;
    }
    return result;
}

}  // namespace

std::vector<Geom> importSvg(const std::string& path) {
    const std::string content = readFile(path);
    std::vector<Geom> out;

    const std::regex tag(
        R"(<\s*(/?)\s*(g|svg|path|polygon|polyline|rect|circle|ellipse|line)\b([^>]*)>)",
        std::regex::icase);
    std::vector<SvgMatrix> stack{SvgMatrix{}};
    for (auto it = std::sregex_iterator(content.begin(), content.end(), tag);
         it != std::sregex_iterator(); ++it) {
        const bool closing = !(*it)[1].str().empty();
        std::string type = (*it)[2].str();
        std::transform(type.begin(), type.end(), type.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::string attributes = (*it)[3];
        if (type == "g" || type == "svg") {
            if (closing) {
                if (stack.size() > 1) stack.pop_back();
            } else {
                stack.push_back(stack.back() * parseSvgTransform(attribute(attributes, "transform")));
            }
            continue;
        }
        if (closing) continue;

        std::vector<Geom> local;
        if (type == "path") {
            parsePathData(attribute(attributes, "d"), local);
        } else if (type == "polygon" || type == "polyline") {
            Ring ring;
            const std::vector<double> v = numbers(attribute(attributes, "points"));
            for (std::size_t i = 0; i + 1 < v.size(); i += 2) ring.push_back({v[i], v[i + 1]});
            if (ring.size() >= 3 && type == "polygon") local.push_back(Geom::polygon(ring).fixed());
            else if (ring.size() >= 2) local.push_back(Geom::line(ring));
        } else if (type == "rect") {
            const double x = number(attribute(attributes, "x"));
            const double y = number(attribute(attributes, "y"));
            const double w = number(attribute(attributes, "width"));
            const double h = number(attribute(attributes, "height"));
            if (w > 0 && h > 0) local.push_back(Geom::box(x, y, x + w, y + h));
        } else if (type == "circle") {
            const double cx = number(attribute(attributes, "cx"));
            const double cy = number(attribute(attributes, "cy"));
            const double r = number(attribute(attributes, "r"));
            if (r > 0) local.push_back(Geom::circle({cx, cy}, r, 32));
        } else if (type == "ellipse") {
            const double cx = number(attribute(attributes, "cx"));
            const double cy = number(attribute(attributes, "cy"));
            const double rx = number(attribute(attributes, "rx"));
            const double ry = number(attribute(attributes, "ry"));
            if (rx > 0 && ry > 0)
                local.push_back(Geom::circle({cx, cy}, 1.0, 64).scale(rx, ry, {cx, cy}));
        } else if (type == "line") {
            const double x1 = number(attribute(attributes, "x1"));
            const double y1 = number(attribute(attributes, "y1"));
            const double x2 = number(attribute(attributes, "x2"));
            const double y2 = number(attribute(attributes, "y2"));
            local.push_back(Geom::line({{x1, y1}, {x2, y2}}));
        }
        const SvgMatrix matrix =
            stack.back() * parseSvgTransform(attribute(attributes, "transform"));
        for (const Geom& g : local) {
            out.push_back(g.transformXY([&matrix](Pt p) { return matrix.apply(p); }));
        }
    }
    return out;
}

namespace {

struct DxfEntity {
    std::string type;
    std::vector<std::pair<int, std::string>> data;
};

struct DxfBlock {
    double bx = 0.0;
    double by = 0.0;
    std::vector<DxfEntity> entities;
};

// Affine matrix in column-vector form: p' = M * p.
struct DxfAffine {
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, e = 0.0, f = 0.0;
};

Pt dxfApply(const DxfAffine& m, Pt p) {
    return {m.a * p.x + m.c * p.y + m.e, m.b * p.x + m.d * p.y + m.f};
}

// Returns the transform that applies inner first and outer afterwards.
DxfAffine dxfCompose(const DxfAffine& outer, const DxfAffine& inner) {
    return DxfAffine{
        outer.a * inner.a + outer.c * inner.b,
        outer.b * inner.a + outer.d * inner.b,
        outer.a * inner.c + outer.c * inner.d,
        outer.b * inner.c + outer.d * inner.d,
        outer.a * inner.e + outer.c * inner.f + outer.e,
        outer.b * inner.e + outer.d * inner.f + outer.f};
}

DxfAffine dxfTranslate(double dx, double dy) {
    return DxfAffine{1.0, 0.0, 0.0, 1.0, dx, dy};
}

DxfAffine dxfScale(double sx, double sy) {
    return DxfAffine{sx, 0.0, 0.0, sy, 0.0, 0.0};
}

DxfAffine dxfRotate(double degrees) {
    const double r = degrees * M_PI / 180.0;
    return DxfAffine{std::cos(r), std::sin(r), -std::sin(r), std::cos(r), 0.0, 0.0};
}

std::string dxfValue(const DxfEntity& entity, int code, const std::string& fallback = {}) {
    for (const auto& [c, value] : entity.data)
        if (c == code) return value;
    return fallback;
}

double dxfNumber(const DxfEntity& entity, int code, double fallback) {
    const std::string value = dxfValue(entity, code);
    return value.empty() ? fallback : number(value);
}

int dxfInt(const DxfEntity& entity, int code, int fallback) {
    const double value = dxfNumber(entity, code, static_cast<double>(fallback));
    return static_cast<int>(std::lround(value));
}

// Extracts %%-style TEXT escape sequences and strips MTEXT formatting codes.
std::string dxfCleanText(std::string text, bool mtext) {
    auto replaceAll = [](std::string& s, const std::string& from, const std::string& to) {
        std::size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    replaceAll(text, "%%d", "\xC2\xB0");
    replaceAll(text, "%%D", "\xC2\xB0");
    replaceAll(text, "%%p", "\xC2\xB1");
    replaceAll(text, "%%P", "\xC2\xB1");
    replaceAll(text, "%%c", "\xC3\x98");
    replaceAll(text, "%%C", "\xC3\x98");
    replaceAll(text, "%%%", "%");
    replaceAll(text, "\\P", "\n");
    replaceAll(text, "\\~", " ");
    if (mtext) {
        static const std::regex formatting(R"(\\[A-Za-z][^;\\]*;)");
        text = std::regex_replace(text, formatting, "");
        static const std::regex escapes(R"(\\[A-Za-z])");
        text = std::regex_replace(text, escapes, "");
        text.erase(std::remove(text.begin(), text.end(), '{'), text.end());
        text.erase(std::remove(text.begin(), text.end(), '}'), text.end());
        replaceAll(text, "\\", "\n");
    }
    return text;
}

struct DxfParseResult {
    std::vector<Geom> geoms;
    std::vector<DxfText> texts;
};

void dxfParseEntity(const DxfEntity& entity, const DxfAffine& transform,
                    const std::map<std::string, DxfBlock>& blocks, int depth,
                    DxfParseResult& result);

void dxfParseEntities(const std::vector<DxfEntity>& entities, const DxfAffine& transform,
                      const std::map<std::string, DxfBlock>& blocks, int depth,
                      DxfParseResult& result) {
    for (const DxfEntity& entity : entities)
        dxfParseEntity(entity, transform, blocks, depth, result);
}

void dxfParseEntity(const DxfEntity& entity, const DxfAffine& transform,
                    const std::map<std::string, DxfBlock>& blocks, int depth,
                    DxfParseResult& result) {
    if (depth > 8) return;
    const std::string& type = entity.type;

    auto emit = [&](Geom geometry) {
        if (!geometry.valid()) return;
        result.geoms.push_back(
            geometry.transformXY([&transform](Pt p) { return dxfApply(transform, p); }));
    };

    if (type == "LINE") {
        emit(Geom::line({{dxfNumber(entity, 10, 0.0), dxfNumber(entity, 20, 0.0)},
                         {dxfNumber(entity, 11, 0.0), dxfNumber(entity, 21, 0.0)}}));
    } else if (type == "CIRCLE") {
        const double r = dxfNumber(entity, 40, 0.0);
        if (r > 0.0)
            emit(Geom::circle({dxfNumber(entity, 10, 0.0), dxfNumber(entity, 20, 0.0)}, r, 48));
    } else if (type == "ARC") {
        const double r = dxfNumber(entity, 40, 0.0);
        if (r > 0.0) {
            const double cx = dxfNumber(entity, 10, 0.0);
            const double cy = dxfNumber(entity, 20, 0.0);
            double a0 = dxfNumber(entity, 50, 0.0);
            double a1 = dxfNumber(entity, 51, 360.0);
            if (a1 <= a0) a1 += 360.0;
            const int steps = std::max(8, static_cast<int>((a1 - a0) / 5.0));
            Ring arc;
            for (int k = 0; k <= steps; ++k) {
                const double a = (a0 + (a1 - a0) * k / steps) * M_PI / 180.0;
                arc.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
            }
            emit(Geom::line(arc));
        }
    } else if (type == "ELLIPSE") {
        const double mx = dxfNumber(entity, 11, 0.0);
        const double my = dxfNumber(entity, 21, 0.0);
        if (mx != 0.0 || my != 0.0) {
            const double cx = dxfNumber(entity, 10, 0.0);
            const double cy = dxfNumber(entity, 20, 0.0);
            const double ratio = dxfNumber(entity, 40, 1.0);
            double a0 = dxfNumber(entity, 41, 0.0) * 180.0 / M_PI;
            double a1 = dxfNumber(entity, 42, 2.0 * M_PI) * 180.0 / M_PI;
            if (a1 <= a0) a1 += 360.0;
            const int steps = std::max(12, static_cast<int>((a1 - a0) / 5.0));
            Ring curve;
            for (int k = 0; k <= steps; ++k) {
                const double a = (a0 + (a1 - a0) * k / steps) * M_PI / 180.0;
                curve.push_back({cx + mx * std::cos(a) - my * ratio * std::sin(a),
                                 cy + mx * std::sin(a) + my * ratio * std::cos(a)});
            }
            emit(Geom::line(curve));
        }
    } else if (type == "LWPOLYLINE" || type == "SPLINE") {
        Ring ring;
        double pendingX = 0.0;
        bool hasX = false;
        for (const auto& [code, value] : entity.data) {
            if (code == 10) {
                pendingX = number(value);
                hasX = true;
            } else if (code == 20 && hasX) {
                ring.push_back({pendingX, number(value)});
                hasX = false;
            }
        }
        const bool closed = (dxfInt(entity, 70, 0) & 1) != 0;
        if (ring.size() >= 3 && closed && type == "LWPOLYLINE")
            emit(Geom::polygon(ring).fixed());
        else if (ring.size() >= 2)
            emit(Geom::line(ring));
    } else if (type == "INSERT") {
        const std::string name = dxfValue(entity, 2);
        const auto it = blocks.find(name);
        if (it == blocks.end()) return;
        const DxfBlock& block = it->second;
        const DxfAffine placement = dxfCompose(
            dxfTranslate(dxfNumber(entity, 10, 0.0), dxfNumber(entity, 20, 0.0)),
            dxfRotate(dxfNumber(entity, 50, 0.0)));
        const DxfAffine scale = dxfScale(dxfNumber(entity, 41, 1.0), dxfNumber(entity, 42, 1.0));
        const int columns = std::max(1, dxfInt(entity, 70, 1));
        const int rows = std::max(1, dxfInt(entity, 71, 1));
        const double spacingX = dxfNumber(entity, 44, 0.0);
        const double spacingY = dxfNumber(entity, 45, 0.0);
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const DxfAffine local = dxfCompose(
                    dxfCompose(scale, dxfTranslate(column * spacingX, row * spacingY)),
                    dxfTranslate(-block.bx, -block.by));
                dxfParseEntities(block.entities, dxfCompose(placement, local), blocks,
                                 depth + 1, result);
            }
        }
    } else if (type == "TEXT" || type == "MTEXT") {
        std::string text = dxfValue(entity, 1);
        if (type == "MTEXT") {
            std::string joined;
            for (const auto& [code, value] : entity.data)
                if (code == 3) joined += value;
            text = joined + text;
        }
        text = dxfCleanText(text, type == "MTEXT");
        if (text.empty()) return;

        double x = dxfNumber(entity, 10, 0.0);
        double y = dxfNumber(entity, 20, 0.0);
        int hAlign = 0;
        int vAlign = 0;
        if (type == "TEXT") {
            hAlign = dxfInt(entity, 72, 0);
            vAlign = dxfInt(entity, 73, 0);
            if ((hAlign != 0 || vAlign != 0) && !dxfValue(entity, 11).empty())
                x = dxfNumber(entity, 11, x), y = dxfNumber(entity, 21, y);
        } else {
            const int attachment = dxfInt(entity, 71, 1);
            hAlign = (attachment - 1) % 3;             // 0 left, 1 centre, 2 right
            const int row = (attachment - 1) / 3;      // 0 top, 1 middle, 2 bottom
            vAlign = row == 0 ? 3 : (row == 1 ? 2 : 1);
        }

        const Pt anchor = dxfApply(transform, {x, y});
        const double determinant = transform.a * transform.d - transform.b * transform.c;
        const double scaleFactor = std::sqrt(std::fabs(determinant));
        const double baseRotation = std::atan2(transform.b, transform.a) * 180.0 / M_PI;
        DxfText info;
        info.text = text;
        info.x = anchor.x;
        info.y = anchor.y;
        info.height = dxfNumber(entity, 40, 2.5) * (scaleFactor > 0.0 ? scaleFactor : 1.0);
        info.rotation = baseRotation + dxfNumber(entity, 50, 0.0);
        info.hAlign = hAlign;
        info.vAlign = vAlign;
        result.texts.push_back(std::move(info));
    }
}

DxfParseResult dxfParseFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);

    std::vector<std::pair<int, std::string>> pairs;
    std::string codeLine, valueLine;
    while (std::getline(in, codeLine) && std::getline(in, valueLine)) {
        try {
            pairs.emplace_back(std::stoi(codeLine), valueLine);
        } catch (...) {
            continue;
        }
    }

    std::vector<DxfEntity> entities;
    std::map<std::string, DxfBlock> blocks;

    std::size_t i = 0;
    while (i < pairs.size()) {
        if (pairs[i].first != 0 || pairs[i].second != "SECTION" ||
            i + 1 >= pairs.size() || pairs[i + 1].first != 2) {
            ++i;
            continue;
        }
        const std::string section = pairs[i + 1].second;
        std::size_t end = i + 2;
        while (end < pairs.size() &&
               !(pairs[end].first == 0 && pairs[end].second == "ENDSEC"))
            ++end;
        std::size_t k = i + 2;
        if (section == "BLOCKS") {
            while (k < end) {
                if (!(pairs[k].first == 0 && pairs[k].second == "BLOCK")) {
                    ++k;
                    continue;
                }
                ++k;
                DxfBlock block;
                std::string name;
                while (k < end && pairs[k].first != 0) {
                    if (pairs[k].first == 2) name = pairs[k].second;
                    else if (pairs[k].first == 10) block.bx = number(pairs[k].second);
                    else if (pairs[k].first == 20) block.by = number(pairs[k].second);
                    ++k;
                }
                while (k < end && !(pairs[k].first == 0 && pairs[k].second == "ENDBLK")) {
                    if (pairs[k].first != 0) {
                        ++k;
                        continue;
                    }
                    DxfEntity entity;
                    entity.type = pairs[k].second;
                    ++k;
                    while (k < end && pairs[k].first != 0) {
                        entity.data.push_back(pairs[k]);
                        ++k;
                    }
                    block.entities.push_back(std::move(entity));
                }
                if (k < end) {
                    ++k;
                    while (k < end && pairs[k].first != 0) ++k;
                }
                if (!name.empty()) blocks[name] = std::move(block);
            }
        } else if (section == "ENTITIES") {
            while (k < end) {
                if (pairs[k].first != 0) {
                    ++k;
                    continue;
                }
                DxfEntity entity;
                entity.type = pairs[k].second;
                ++k;
                while (k < end && pairs[k].first != 0) {
                    entity.data.push_back(pairs[k]);
                    ++k;
                }
                entities.push_back(std::move(entity));
            }
        }
        i = end + 1;
    }

    DxfParseResult result;
    dxfParseEntities(entities, DxfAffine{}, blocks, 0, result);
    return result;
}

} // namespace

std::vector<Geom> importDxf(const std::string& path) {
    return dxfParseFile(path).geoms;
}

std::vector<DxfText> importDxfTexts(const std::string& path) {
    return dxfParseFile(path).texts;
}

std::vector<Geom> importHpgl(const std::string& path) {
    const std::string content = readFile(path);
    std::vector<Geom> out;
    Ring current;
    Pt pos{0.0, 0.0};
    bool absolute = true;
    bool penDown = false;
    double unit = 0.025;  // HPGL plotter unit in millimetres
    // SC xmin,xmax,ymin,ymax maps a user-unit range onto the default plotting
    // area (about 10365 x 7962 plotter units, roughly A4 at 0.025 mm/unit).
    bool scaled = false;
    double originX = 0.0;
    double originY = 0.0;

    auto flush = [&] {
        if (current.size() >= 2) out.push_back(Geom::line(current));
        current.clear();
    };

    auto convert = [&](double value, bool isX, bool isRelative) {
        if (isRelative) return value * unit;
        const double origin = isX ? originX : originY;
        return scaled ? (value - origin) * unit : value * unit;
    };

    auto numbers = [](const std::string& args) {
        std::vector<double> values;
        std::stringstream ss(args);
        std::string token;
        while (std::getline(ss, token, ',')) {
            token.erase(0, token.find_first_not_of(" \t"));
            token.erase(token.find_last_not_of(" \t") + 1);
            if (token.empty()) continue;
            try {
                values.push_back(std::stod(token));
            } catch (...) {
                break;
            }
        }
        return values;
    };

    auto addArc = [&](double cx, double cy, double startAngle, double sweep,
                      bool relativeCenter) {
        const double centerX = relativeCenter ? pos.x + cx * unit : convert(cx, true, !absolute);
        const double centerY = relativeCenter ? pos.y + cy * unit : convert(cy, false, !absolute);
        const double radius = std::hypot(pos.x - centerX, pos.y - centerY);
        if (radius <= 0.0) return;
        Ring arc;
        const int steps = std::max(4, static_cast<int>(std::fabs(sweep) / 0.1));
        for (int i = 0; i <= steps; ++i) {
            const double a = startAngle + sweep * i / steps;
            arc.push_back({centerX + radius * std::cos(a), centerY + radius * std::sin(a)});
        }
        if (arc.size() >= 2) {
            if (penDown && !current.empty()) current.pop_back();
            if (penDown) current.insert(current.end(), arc.begin(), arc.end());
            else out.push_back(Geom::line(arc));
        }
        pos = arc.back();
    };

    auto addRectangle = [&](const Pt& corner, bool filled) {
        const Pt a = pos;
        const Pt b = corner;
        Ring ring{{a.x, a.y}, {b.x, a.y}, {b.x, b.y}, {a.x, b.y}, {a.x, a.y}};
        if (filled) out.push_back(Geom::polygon(ring).fixed());
        else out.push_back(Geom::line(ring));
        pos = b;
    };

    std::size_t start = 0;
    while (start < content.size()) {
        std::size_t end = content.find(';', start);
        if (end == std::string::npos) end = content.size();
        std::string command = content.substr(start, end - start);
        start = end + 1;
        command.erase(0, command.find_first_not_of(" \t\r\n"));
        if (command.empty()) continue;
        command.erase(command.find_last_not_of(" \t\r\n") + 1);
        if (command.size() < 2) continue;
        const std::string op = command.substr(0, 2);
        std::string args = command.substr(2);
        args.erase(0, args.find_first_not_of(" \t"));

        auto moveTo = [&](const std::vector<double>& values) {
            for (std::size_t i = 0; i + 1 < values.size(); i += 2) {
                const double x = convert(values[i], true, !absolute);
                const double y = convert(values[i + 1], false, !absolute);
                pos = {x, y};
                if (penDown) {
                    if (current.empty()) current.push_back(pos);
                    else current.push_back(pos);
                }
            }
        };

        if (op == "IN") {
            flush();
            pos = {0.0, 0.0};
            penDown = false;
            absolute = true;
            scaled = false;
            unit = 0.025;
        } else if (op == "SC") {
            const std::vector<double> values = numbers(args);
            if (values.size() >= 4) {
                const double xmin = values[0];
                const double xmax = values[1];
                const double ymin = values[2];
                const double ymax = values[3];
                if (xmax > xmin) {
                    // 10365 plotter units is the default A4 width.
                    unit = 0.025 * 10365.0 / (xmax - xmin);
                    originX = xmin;
                    originY = ymin;
                    scaled = true;
                    if (ymax <= ymin) originY = 0.0;
                }
            }
        } else if (op == "PA") {
            absolute = true;
            moveTo(numbers(args));
        } else if (op == "PR") {
            absolute = false;
            moveTo(numbers(args));
        } else if (op == "PU") {
            flush();
            penDown = false;
            moveTo(numbers(args));
        } else if (op == "PD") {
            penDown = true;
            if (current.empty()) current.push_back(pos);
            moveTo(numbers(args));
        } else if (op == "CI") {
            const std::vector<double> values = numbers(args);
            if (!values.empty() && values[0] > 0.0) {
                const double radius = values[0] * unit;
                Ring circle;
                for (int i = 0; i < 48; ++i) {
                    const double a = 2.0 * M_PI * i / 48.0;
                    circle.push_back({pos.x + radius * std::cos(a),
                                      pos.y + radius * std::sin(a)});
                }
                circle.push_back(circle.front());
                out.push_back(Geom::line(circle));
            }
        } else if (op == "AA" || op == "AR") {
            const std::vector<double> values = numbers(args);
            if (values.size() >= 4) {
                const double startAngle = values[2] * M_PI / 180.0;
                const double sweep = values[3] * M_PI / 180.0;
                addArc(values[0], values[1], startAngle, sweep, op == "AR");
            }
        } else if (op == "EA" || op == "ER") {
            const std::vector<double> values = numbers(args);
            if (values.size() >= 2) {
                const bool fill = false;
                const Pt corner{convert(values[0], true, op == "ER"),
                                convert(values[1], false, op == "ER")};
                addRectangle(corner, fill);
            }
        } else if (op == "RA" || op == "RR") {
            const std::vector<double> values = numbers(args);
            if (values.size() >= 2) {
                const Pt corner{convert(values[0], true, op == "RR"),
                                convert(values[1], false, op == "RR")};
                addRectangle(corner, true);
            }
        }
    }
    flush();
    return out;
}

void writeGerber(const std::string& path, const std::vector<Geom>& solids,
                 const std::vector<Geom>& follows, Units units) {
    const double scale = units == Units::Inch ? 10000.0 : 10000.0;
    const int intDigits = units == Units::Inch ? 2 : 3;
    const int fracDigits = units == Units::Inch ? 4 : 4;

    auto coord = [scale](double v) {
        return static_cast<long long>(std::llround(v * scale));
    };

    std::ostringstream out;
    out << "G04 FlatCAM C++ imported geometry*\n";
    out << "%FSLAX" << intDigits << fracDigits << "Y" << intDigits << fracDigits << "*%\n";
    out << (units == Units::Inch ? "%MOIN*%\n" : "%MOMM*%\n");
    out << "%ADD10C,0.100*%\n";
    out << "G01*\nD10*\n%LPD*%\n";

    for (const Geom& solid : solids) {
        for (const PolygonContours& pc : solid.polygons()) {
            if (pc.outer.size() < 3) continue;
            out << "G36*\n";
            out << "X" << coord(pc.outer.front().x) << "Y" << coord(pc.outer.front().y) << "D02*\n";
            for (std::size_t i = 1; i < pc.outer.size(); ++i) {
                out << "X" << coord(pc.outer[i].x) << "Y" << coord(pc.outer[i].y) << "D01*\n";
            }
            out << "G37*\n";

            if (!pc.holes.empty()) {
                out << "%LPC*%\n";
                for (const Ring& hole : pc.holes) {
                    if (hole.size() < 3) continue;
                    out << "G36*\n";
                    out << "X" << coord(hole.front().x) << "Y" << coord(hole.front().y) << "D02*\n";
                    for (std::size_t i = 1; i < hole.size(); ++i) {
                        out << "X" << coord(hole[i].x) << "Y" << coord(hole[i].y) << "D01*\n";
                    }
                    out << "G37*\n";
                }
                out << "%LPD*%\n";
            }
        }
    }

    for (const Geom& line : follows) {
        if (!line.valid() || line.empty()) continue;
        const Ring pts = line.coords();
        if (pts.size() < 2) continue;
        out << "X" << coord(pts.front().x) << "Y" << coord(pts.front().y) << "D02*\n";
        for (std::size_t i = 1; i < pts.size(); ++i) {
            out << "X" << coord(pts[i].x) << "Y" << coord(pts[i].y) << "D01*\n";
        }
    }
    out << "M02*\n";

    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot write " + path);
    file << out.str();
}

void exportHpgl(const std::string& path, const std::vector<Geom>& solids,
                const std::vector<Geom>& follows) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write HPGL: " + path);
    const double scale = 40.0;  // 1 mm = 40 plotter units
    auto coord = [scale](double value) { return std::llround(value * scale); };
    out << "IN;SP1;PU;\n";
    auto writeRing = [&](const Ring& ring) {
        if (ring.size() < 2) return;
        out << "PU;PA" << coord(ring.front().x) << "," << coord(ring.front().y) << ";PD;\n";
        for (std::size_t i = 1; i < ring.size(); ++i)
            out << "PA" << coord(ring[i].x) << "," << coord(ring[i].y) << ";\n";
    };
    for (const Geom& g : solids) {
        if (!g.valid()) continue;
        for (const PolygonContours& pc : g.polygons()) {
            writeRing(pc.outer);
            for (const Ring& hole : pc.holes) writeRing(hole);
        }
    }
    for (const Geom& g : follows) {
        if (!g.valid()) continue;
        const int type = g.typeId();
        if (type == GEOS_LINESTRING || type == GEOS_LINEARRING) {
            writeRing(g.coords());
        } else {
            for (int i = 0; i < g.numGeometries(); ++i) {
                const Geom part = g.geometryN(i);
                if (part.typeId() == GEOS_LINESTRING || part.typeId() == GEOS_LINEARRING)
                    writeRing(part.coords());
            }
        }
    }
    out << "PU;SP0;\n";
}

void writeExcellon(const std::string& path, const ExcellonResult& result, Units units) {
    const bool metric = units == Units::Mm;
    const int intDigits = metric ? 3 : 2;
    const int fracDigits = metric ? 3 : 4;

    auto coord = [units](double value) {
        const double scale = units == Units::Mm ? 1000.0 : 10000.0;
        const long long v = static_cast<long long>(std::llround(value * scale));
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%0*lld", 6, v);
        return std::string(buffer);
    };

    std::ostringstream out;
    out << "M48\n";
    out << "; DRILL file generated by FlatCAM C++\n";
    out << "; FORMAT={-" << intDigits << ":-" << fracDigits << "/ absolute / "
        << (metric ? "metric" : "inch") << " / decimal}\n";
    out << (metric ? "METRIC,TZ\n" : "INCH,LZ\n");
    for (const auto& [number, tool] : result.tools) {
        out << "T" << number << "C" << tool.diameter << "\n";
    }
    out << "%\nG90\n";

    for (const auto& [number, tool] : result.tools) {
        if (tool.drills.empty() && tool.slotSegments.empty()) continue;
        out << "T" << number << "\n";
        for (const Pt& hole : tool.drills) {
            out << "X" << coord(hole.x) << "Y" << coord(hole.y) << "\n";
        }
        for (const auto& [a, b] : tool.slotSegments) {
            out << "X" << coord(a.x) << "Y" << coord(a.y) << "G85X" << coord(b.x) << "Y"
                << coord(b.y) << "\n";
        }
    }
    out << "M30\n";

    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot write " + path);
    file << out.str();
}

}  // namespace fc
