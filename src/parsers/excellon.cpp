#include "flatcam/excellon.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace fc {
namespace {

bool startsWith(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::string upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::string_view inner(std::string_view s) {
    s = trim(s);
    if (!s.empty() && s.front() == '%') s.remove_prefix(1);
    if (!s.empty() && s.back() == '%') s.remove_suffix(1);
    return trim(s);
}

struct Ctx {
    const ExcellonOptions& opt;
    ExcellonResult& res;

    bool inHeader = false;
    bool headerless = false;
    int tool = 0;
    double x = 0.0;
    double y = 0.0;
    bool incremental = false;
    bool routingDown = false;
    Pt routeStart;

    int repeat = 0;
    double repeatX = 0.0;
    double repeatY = 0.0;

    Ctx(const ExcellonOptions& o, ExcellonResult& r) : opt(o), res(r) {}
};

int countDigits(std::string_view s) {
    int n = 0;
    for (char c : s) {
        if (c >= '0' && c <= '9') ++n;
    }
    return n;
}

double parseCoordinate(const Ctx& ctx, std::string_view text) {
    std::string t(text);
    if (t.empty()) return 0.0;
    if (t.find('.') != std::string::npos) return std::strtod(t.c_str(), nullptr);

    const bool negative = t.front() == '-';
    if (!t.empty() && (t.front() == '+' || t.front() == '-')) t.erase(t.begin());
    const long long raw = std::llabs(std::strtoll(t.c_str(), nullptr, 10));
    const int len = countDigits(t);

    const bool inch = ctx.res.fileUnits == Units::Inch;
    const int intDigits = inch ? ctx.res.formatUpperIn : ctx.res.formatUpperMm;
    const int fracDigits = inch ? ctx.res.formatLowerIn : ctx.res.formatLowerMm;

    double v = 0.0;
    if (ctx.res.zeros == 'T') {
        v = static_cast<double>(raw) / std::pow(10.0, fracDigits);
    } else {
        v = static_cast<double>(raw) / std::pow(10.0, std::max(0, len - intDigits));
    }
    return negative ? -v : v;
}

ExcellonTool& toolFor(Ctx& ctx, int number) {
    auto it = ctx.res.tools.find(number);
    if (it == ctx.res.tools.end()) {
        ExcellonTool t;
        t.number = number;
        // FlatCAM gives a bogus, slightly increasing diameter to diameterless files.
        t.diameter = 1.0 + (number - 1) / 100.0;
        t.diameterKnown = false;
        it = ctx.res.tools.emplace(number, std::move(t)).first;
        ctx.res.warnings.push_back("tool T" + std::to_string(number) + " has no diameter definition");
    }
    return it->second;
}

void parseUnits(Ctx& ctx, std::string_view line) {
    const std::string u = upper(inner(line));
    if (startsWith(u, "METRIC") || startsWith(u, "M71")) {
        ctx.res.fileUnits = Units::Mm;
        ctx.res.unitsKnown = true;
        ctx.res.formatUpperMm = 3;
        ctx.res.formatLowerMm = 3;
    } else if (startsWith(u, "INCH") || startsWith(u, "M72")) {
        ctx.res.fileUnits = Units::Inch;
        ctx.res.unitsKnown = true;
        ctx.res.formatUpperIn = 2;
        ctx.res.formatLowerIn = 4;
    } else {
        return;
    }

    const std::size_t comma = u.find(',');
    if (comma == std::string::npos) return;
    const std::string spec = u.substr(comma + 1);
    if (spec.find("TZ") != std::string::npos) ctx.res.zeros = 'T';
    if (spec.find("LZ") != std::string::npos) ctx.res.zeros = 'L';

    // Explicit decimal format, e.g. "000.000" or "00.0000".
    const std::size_t dot = spec.find('.');
    if (dot != std::string::npos) {
        int intDigits = 0;
        for (std::size_t i = 0; i < dot; ++i) {
            if (isdigit(static_cast<unsigned char>(spec[i]))) ++intDigits;
        }
        int fracDigits = 0;
        for (std::size_t i = dot + 1; i < spec.size(); ++i) {
            if (isdigit(static_cast<unsigned char>(spec[i]))) ++fracDigits;
        }
        if (intDigits > 0 && fracDigits > 0) {
            if (ctx.res.fileUnits == Units::Mm) {
                ctx.res.formatUpperMm = intDigits;
                ctx.res.formatLowerMm = fracDigits;
            } else {
                ctx.res.formatUpperIn = intDigits;
                ctx.res.formatLowerIn = fracDigits;
            }
        }
    }
}

// T1C0.8 / T01C0.800 / T1
void parseToolLine(Ctx& ctx, std::string_view line) {
    std::size_t i = 1;
    const std::size_t start = i;
    while (i < line.size() && isdigit(static_cast<unsigned char>(line[i]))) ++i;
    if (i == start) return;
    const int number = std::atoi(std::string(line.substr(start, i - start)).c_str());
    const std::size_t c = line.find('C', i);
    if (c == std::string_view::npos) {
        ctx.tool = number;
        toolFor(ctx, number);
        return;
    }
    const std::string_view diamText = line.substr(c + 1);
    std::string d(diamText);
    const double dia = std::strtod(d.c_str(), nullptr);
    if (dia <= 0.0) return;

    auto it = ctx.res.tools.find(number);
    if (it == ctx.res.tools.end()) {
        ExcellonTool tool;
        tool.number = number;
        it = ctx.res.tools.emplace(number, std::move(tool)).first;
    }
    it->second.diameter = dia;
    it->second.diameterKnown = true;
    ctx.tool = number;
}

struct Coords {
    std::optional<std::string> x, y;
};

Coords readCoords(std::string_view s) {
    Coords c;
    std::size_t i = 0;
    while (i < s.size()) {
        const char ch = s[i];
        if (ch == 'X' || ch == 'Y') {
            ++i;
            const std::size_t st = i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            while (i < s.size() && (isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) ++i;
            if (i > st) {
                if (ch == 'X') c.x = std::string(s.substr(st, i - st));
                else c.y = std::string(s.substr(st, i - st));
            }
        } else {
            ++i;
        }
    }
    return c;
}

void addDrill(Ctx& ctx, double x, double y) {
    if (ctx.tool == 0) {
        ctx.res.warnings.push_back("drill before tool selection");
        return;
    }
    ExcellonTool& t = toolFor(ctx, ctx.tool);
    t.drills.push_back({x, y});
    ++ctx.res.drillCount;
}

void addSlot(Ctx& ctx, const Pt& a, const Pt& b) {
    if (ctx.tool == 0) {
        ctx.res.warnings.push_back("slot before tool selection");
        return;
    }
    ExcellonTool& t = toolFor(ctx, ctx.tool);
    t.slotSegments.emplace_back(a, b);
    ++ctx.res.slotCount;
}

void handleBodyLine(Ctx& ctx, std::string_view line) {
    const std::string u = upper(trim(line));
    if (u.empty()) return;
    if (u.front() == ';') return;
    if (startsWith(u, "M48")) {
        ctx.inHeader = true;
        return;
    }
    if (u == "%" || u == "M95") {
        ctx.inHeader = false;
        return;
    }
    if (startsWith(u, "M30") || startsWith(u, "M00") || startsWith(u, "M02")) return;
    if (u.rfind("M71", 0) == 0 || u.rfind("M72", 0) == 0) {
        parseUnits(ctx, u);
        return;
    }
    if (startsWith(u, "G90")) {
        ctx.incremental = false;
        return;
    }
    if (startsWith(u, "G91")) {
        ctx.incremental = true;
        return;
    }
    if (startsWith(u, "METRIC") || startsWith(u, "INCH")) {
        parseUnits(ctx, u);
        return;
    }
    if (u.front() == 'T' && u.size() > 1 && isdigit(static_cast<unsigned char>(u[1]))) {
        parseToolLine(ctx, u);
        return;
    }
    if (u.front() == ';') return;
    if (startsWith(u, "M15") || startsWith(u, "M16") || startsWith(u, "M17")) {
        if (startsWith(u, "M15")) {
            ctx.routingDown = true;
            ctx.routeStart = {ctx.x, ctx.y};
        } else {
            ctx.routingDown = false;
        }
        return;
    }
    if (startsWith(u, "G00") || startsWith(u, "G0") || startsWith(u, "G01") || startsWith(u, "G1")) {
        const Coords c = readCoords(u);
        double nx = c.x ? parseCoordinate(ctx, *c.x) : ctx.x;
        double ny = c.y ? parseCoordinate(ctx, *c.y) : ctx.y;
        if (ctx.incremental) {
            if (c.x) nx += ctx.x;
            if (c.y) ny += ctx.y;
        }
        if (ctx.routingDown && (startsWith(u, "G01") || startsWith(u, "G1"))) {
            addSlot(ctx, ctx.routeStart, {nx, ny});
            ctx.routeStart = {nx, ny};
        }
        ctx.x = nx;
        ctx.y = ny;
        return;
    }

    // Repeat prefix: R3X1.0Y2.0
    if (u.front() == 'R' && u.size() > 1 && isdigit(static_cast<unsigned char>(u[1]))) {
        const Coords c = readCoords(u);
        std::size_t i = 1;
        while (i < u.size() && isdigit(static_cast<unsigned char>(u[i]))) ++i;
        ctx.repeat = std::atoi(u.substr(1, i - 1).c_str());
        ctx.repeatX = c.x ? std::strtod(c.x->c_str(), nullptr) : 0.0;
        ctx.repeatY = c.y ? std::strtod(c.y->c_str(), nullptr) : 0.0;
        return;
    }

    // G85 slot: X..Y..G85X..Y..
    const std::size_t g85 = u.find("G85");
    if (g85 != std::string::npos) {
        const Coords startCoords = readCoords(u.substr(0, g85));
        const Coords stopCoords = readCoords(u.substr(g85 + 3));
        double sx = startCoords.x ? parseCoordinate(ctx, *startCoords.x) : ctx.x;
        double sy = startCoords.y ? parseCoordinate(ctx, *startCoords.y) : ctx.y;
        double ex = stopCoords.x ? parseCoordinate(ctx, *stopCoords.x) : sx;
        double ey = stopCoords.y ? parseCoordinate(ctx, *stopCoords.y) : sy;
        if (ctx.incremental) {
            if (startCoords.x) sx += ctx.x;
            if (startCoords.y) sy += ctx.y;
            if (stopCoords.x) ex += sx;
            if (stopCoords.y) ey += sy;
        }
        addSlot(ctx, {sx, sy}, {ex, ey});
        ctx.x = ex;
        ctx.y = ey;
        return;
    }

    const Coords c = readCoords(u);
    if (!c.x && !c.y) return;

    double x = c.x ? parseCoordinate(ctx, *c.x) : ctx.x;
    double y = c.y ? parseCoordinate(ctx, *c.y) : ctx.y;
    if (ctx.incremental) {
        if (c.x) x += ctx.x;
        if (c.y) y += ctx.y;
    }

    if (ctx.routingDown) {
        addSlot(ctx, ctx.routeStart, {x, y});
        ctx.routeStart = {x, y};
    } else {
        addDrill(ctx, x, y);
        for (int i = 1; i <= ctx.repeat; ++i) {
            addDrill(ctx, x + i * ctx.repeatX, y + i * ctx.repeatY);
        }
        ctx.repeat = 0;
    }
    ctx.x = x;
    ctx.y = y;
}

void finalize(Ctx& ctx) {
    const double factor = conversionFactor(ctx.res.fileUnits, ctx.opt.appUnits);
    const int quad = std::max(2, ctx.opt.stepsPerCircle / 4);

    for (auto& [number, tool] : ctx.res.tools) {
        for (const Pt& d : tool.drills) {
            Geom g = Geom::circle(d, tool.diameter / 2.0, quad);
            tool.geometry.push_back(g);
            ctx.res.geometry.push_back(g);
        }
        for (const auto& [a, b] : tool.slotSegments) {
            Geom g = (a.x == b.x && a.y == b.y)
                         ? Geom::circle(a, tool.diameter / 2.0, quad)
                         : Geom::line({{a.x, a.y}, {b.x, b.y}}).buffer(tool.diameter / 2.0, quad);
            tool.geometry.push_back(g);
            ctx.res.geometry.push_back(g);
        }
    }

    std::vector<Geom> converted;
    converted.reserve(ctx.res.geometry.size());
    for (const Geom& g : ctx.res.geometry) {
        Geom c = factor == 1.0 ? g : g.scale(factor, factor);
        converted.push_back(c);
        const BBox b = c.bounds();
        if (!b.valid) continue;
        if (!ctx.res.bounds.valid) {
            ctx.res.bounds = b;
        } else {
            ctx.res.bounds.xmin = std::min(ctx.res.bounds.xmin, b.xmin);
            ctx.res.bounds.ymin = std::min(ctx.res.bounds.ymin, b.ymin);
            ctx.res.bounds.xmax = std::max(ctx.res.bounds.xmax, b.xmax);
            ctx.res.bounds.ymax = std::max(ctx.res.bounds.ymax, b.ymax);
        }
    }
    ctx.res.geometry = std::move(converted);

    if (!ctx.res.geometry.empty()) ctx.res.solid = Geom::unionOf(ctx.res.geometry);

    for (auto& [number, tool] : ctx.res.tools) {
        if (factor != 1.0) {
            tool.diameter *= factor;
            for (Geom& g : tool.geometry) g = g.scale(factor, factor);
        }
    }
}

}  // namespace

ExcellonParser::ExcellonParser(ExcellonOptions options) : opt_(options) {}

ExcellonResult ExcellonParser::parseString(std::string_view source) const {
    ExcellonResult result;
    result.zeros = opt_.defaultZeros;
    result.fileUnits = opt_.appUnits;
    Ctx ctx(opt_, result);

    std::size_t start = 0;
    while (start <= source.size()) {
        const std::size_t end = source.find('\n', start);
        const std::string_view line = (end == std::string_view::npos)
                                          ? source.substr(start)
                                          : source.substr(start, end - start);
        handleBodyLine(ctx, line);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    finalize(ctx);
    return result;
}

ExcellonResult ExcellonParser::parseFile(const std::string& path) const {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open Excellon file: " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return parseString(ss.str());
}

}  // namespace fc
