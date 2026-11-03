#include "flatcam/pdf.hpp"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fc {
namespace {

constexpr double kPointsToMm = 25.4 / 72.0;

struct PdfMatrix {
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, e = 0.0, f = 0.0;
};

// Applies `outer` after `inner`.
PdfMatrix concat(const PdfMatrix& outer, const PdfMatrix& inner) {
    return PdfMatrix{
        outer.a * inner.a + outer.c * inner.b,
        outer.b * inner.a + outer.d * inner.b,
        outer.a * inner.c + outer.c * inner.d,
        outer.b * inner.c + outer.d * inner.d,
        outer.a * inner.e + outer.c * inner.f + outer.e,
        outer.b * inner.e + outer.d * inner.f + outer.f};
}

Pt transform(const PdfMatrix& m, double x, double y) {
    return {m.a * x + m.c * y + m.e, m.b * x + m.d * y + m.f};
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool inflateData(const std::string& input, std::string& output) {
    for (int windowBits : {15, -15}) {
        z_stream stream{};
        if (inflateInit2(&stream, windowBits) != Z_OK) continue;
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
        stream.avail_in = static_cast<uInt>(input.size());
        output.clear();
        char buffer[65536];
        int status = Z_OK;
        while (status == Z_OK) {
            stream.next_out = reinterpret_cast<Bytef*>(buffer);
            stream.avail_out = sizeof(buffer);
            status = inflate(&stream, Z_NO_FLUSH);
            output.append(buffer, sizeof(buffer) - stream.avail_out);
            if (output.size() > (64u << 20)) break;  // safety limit
        }
        inflateEnd(&stream);
        if ((status == Z_STREAM_END || status == Z_OK) && !output.empty()) return true;
    }
    return false;
}

bool looksLikeContent(const std::string& text) {
    if (text.size() < 4) return false;
    std::size_t printable = 0;
    for (unsigned char c : text) {
        if (c == '\n' || c == '\r' || c == '\t' || (c >= 32 && c < 127)) ++printable;
    }
    if (printable * 10 < text.size() * 9) return false;
    static const char* markers[] = {" re", " m", " l", " c", " cm", " S", " f", "\nS", "\nf"};
    for (const char* marker : markers) {
        if (text.find(marker) != std::string::npos) return true;
    }
    return false;
}

std::vector<std::string> pdfContentStreams(const std::string& data) {
    std::vector<std::string> streams;
    std::size_t pos = 0;
    while ((pos = data.find("stream", pos)) != std::string::npos) {
        std::size_t start = pos + 6;
        if (start < data.size() && data[start] == '\r') ++start;
        if (start < data.size() && data[start] == '\n') ++start;
        const std::size_t end = data.find("endstream", start);
        if (end == std::string::npos) break;
        std::string payload = data.substr(start, end - start);

        // The dictionary right before "stream" tells whether the payload is
        // Flate-compressed.
        const std::size_t dictStart = pos > 2048 ? pos - 2048 : 0;
        const std::string dictionary = data.substr(dictStart, pos - dictStart);
        if (dictionary.find("FlateDecode") != std::string::npos) {
            std::string inflated;
            if (inflateData(payload, inflated)) payload = std::move(inflated);
            else payload.clear();
        }
        if (!payload.empty() && looksLikeContent(payload))
            streams.push_back(std::move(payload));
        pos = end + 9;
    }
    return streams;
}

struct PdfState {
    PdfMatrix ctm;
    double lineWidth = 1.0;
};

struct PdfSubpath {
    Ring points;
    bool closed = false;
};

class ContentParser {
public:
    ContentParser(std::vector<Geom>& output, PdfState state)
        : output_(output), state_(std::move(state)) {}

    void parse(const std::string& content) {
        std::size_t i = 0;
        const std::size_t n = content.size();
        while (i < n) {
            const char c = content[i];
            if (std::isspace(static_cast<unsigned char>(c))) {
                ++i;
                continue;
            }
            if (c == '%') {
                while (i < n && content[i] != '\n') ++i;
                continue;
            }
            if (c == '(') {
                skipLiteralString(content, i);
                continue;
            }
            if (c == '<') {
                if (i + 1 < n && content[i + 1] == '<') skipDictionary(content, i);
                else skipHexString(content, i);
                continue;
            }
            if (c == '[') {
                skipArray(content, i);
                continue;
            }
            if (c == ']' || c == '>' || c == ')' || c == '{' || c == '}') {
                ++i;
                continue;
            }
            if (c == '/') {
                ++i;
                while (i < n && !isDelimiter(content[i])) ++i;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' ||
                c == '.') {
                char* end = nullptr;
                const double value = std::strtod(content.c_str() + i, &end);
                if (end && end > content.c_str() + i) {
                    operands_.push_back(value);
                    i = static_cast<std::size_t>(end - content.c_str());
                    continue;
                }
                ++i;
                continue;
            }
            std::size_t tokenStart = i;
            while (i < n && !isDelimiter(content[i])) ++i;
            const std::string token = content.substr(tokenStart, i - tokenStart);
            if (!token.empty()) dispatch(token);
        }
    }

private:
    static bool isDelimiter(char c) {
        return std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ')' ||
               c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' ||
               c == '/' || c == '%';
    }

    static void skipLiteralString(const std::string& text, std::size_t& i) {
        int depth = 0;
        while (i < text.size()) {
            if (text[i] == '\\') {
                i += 2;
                continue;
            }
            if (text[i] == '(') ++depth;
            else if (text[i] == ')') {
                --depth;
                if (depth <= 0) {
                    ++i;
                    return;
                }
            }
            ++i;
        }
    }

    static void skipHexString(const std::string& text, std::size_t& i) {
        while (i < text.size() && text[i] != '>') ++i;
        if (i < text.size()) ++i;
    }

    static void skipDictionary(const std::string& text, std::size_t& i) {
        int depth = 0;
        while (i < text.size()) {
            if (text[i] == '<' && i + 1 < text.size() && text[i + 1] == '<') {
                ++depth;
                i += 2;
                continue;
            }
            if (text[i] == '>' && i + 1 < text.size() && text[i + 1] == '>') {
                --depth;
                i += 2;
                if (depth <= 0) return;
                continue;
            }
            ++i;
        }
    }

    static void skipArray(const std::string& text, std::size_t& i) {
        int depth = 0;
        while (i < text.size()) {
            const char c = text[i];
            if (c == '(') {
                skipLiteralString(text, i);
                continue;
            }
            if (c == '[') ++depth;
            else if (c == ']') {
                --depth;
                ++i;
                if (depth <= 0) return;
                continue;
            }
            ++i;
        }
    }

    void dispatch(const std::string& op) {
        if (op == "q") {
            stack_.push_back(state_);
        } else if (op == "Q") {
            if (!stack_.empty()) {
                state_ = stack_.back();
                stack_.pop_back();
            }
        } else if (op == "cm") {
            if (operands_.size() >= 6) {
                const std::size_t base = operands_.size() - 6;
                const PdfMatrix m{operands_[base], operands_[base + 1], operands_[base + 2],
                                  operands_[base + 3], operands_[base + 4], operands_[base + 5]};
                state_.ctm = concat(m, state_.ctm);
            }
        } else if (op == "w") {
            if (!operands_.empty()) state_.lineWidth = operands_.back();
        } else if (op == "m") {
            if (operands_.size() >= 2) {
                const std::size_t base = operands_.size() - 2;
                newSubpath(mapPoint(operands_[base], operands_[base + 1]));
            }
        } else if (op == "l") {
            if (operands_.size() >= 2) {
                const std::size_t base = operands_.size() - 2;
                addPoint(mapPoint(operands_[base], operands_[base + 1]));
            }
        } else if (op == "c") {
            if (operands_.size() >= 6) {
                const std::size_t base = operands_.size() - 6;
                bezier(mapPoint(operands_[base], operands_[base + 1]),
                       mapPoint(operands_[base + 2], operands_[base + 3]),
                       mapPoint(operands_[base + 4], operands_[base + 5]));
            }
        } else if (op == "v" || op == "y") {
            if (operands_.size() >= 4) {
                const std::size_t base = operands_.size() - 4;
                const Pt third = mapPoint(operands_[base], operands_[base + 1]);
                const Pt end = mapPoint(operands_[base + 2], operands_[base + 3]);
                if (op == "v") bezier(currentPoint(), third, end);
                else bezier(third, end, end);
            }
        } else if (op == "re") {
            if (operands_.size() >= 4) {
                const std::size_t base = operands_.size() - 4;
                rectangle(operands_[base], operands_[base + 1], operands_[base + 2],
                          operands_[base + 3]);
            }
        } else if (op == "h") {
            closeSubpath();
        } else if (op == "S" || op == "s") {
            if (op == "s") closeSubpath();
            stroke();
            clearPath();
        } else if (op == "f" || op == "F" || op == "f*") {
            fill();
            clearPath();
        } else if (op == "B" || op == "B*" || op == "b" || op == "b*") {
            if (op == "b" || op == "b*") closeSubpath();
            fill();
            stroke();
            clearPath();
        } else if (op == "n") {
            clearPath();
        }
        // Unknown operators (text, colour, clipping, marks...) are ignored.
        operands_.clear();
    }

    Pt mapPoint(double x, double y) const {
        const Pt p = transform(state_.ctm, x, y);
        return {p.x * kPointsToMm, p.y * kPointsToMm};
    }

    Pt currentPoint() const {
        if (subpaths_.empty() || subpaths_.back().points.empty()) return {0.0, 0.0};
        return subpaths_.back().points.back();
    }

    void newSubpath(Pt p) {
        subpaths_.push_back(PdfSubpath{});
        subpaths_.back().points.push_back(p);
    }

    void addPoint(Pt p) {
        if (subpaths_.empty() || subpaths_.back().points.empty()) newSubpath(p);
        else subpaths_.back().points.push_back(p);
    }

    void closeSubpath() {
        if (subpaths_.empty() || subpaths_.back().points.size() < 2) return;
        PdfSubpath& path = subpaths_.back();
        if (std::hypot(path.points.front().x - path.points.back().x,
                       path.points.front().y - path.points.back().y) > 1e-9)
            path.points.push_back(path.points.front());
        path.closed = true;
    }

    void bezier(Pt c1, Pt c2, Pt end) {
        const Pt start = currentPoint();
        const float length = static_cast<float>(std::hypot(start.x - c1.x, start.y - c1.y) +
                                                std::hypot(c1.x - c2.x, c1.y - c2.y) +
                                                std::hypot(c2.x - end.x, c2.y - end.y));
        int steps = std::max(4, std::min(64, static_cast<int>(length / 0.5f)));
        for (int i = 1; i <= steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            const double u = 1.0 - t;
            const Pt p{u * u * u * start.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x +
                           t * t * t * end.x,
                       u * u * u * start.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y +
                           t * t * t * end.y};
            addPoint(p);
        }
    }

    void rectangle(double x, double y, double w, double h) {
        const Pt p1 = mapPoint(x, y);
        const Pt p2 = mapPoint(x + w, y);
        const Pt p3 = mapPoint(x + w, y + h);
        const Pt p4 = mapPoint(x, y + h);
        subpaths_.push_back(PdfSubpath{{p1, p2, p3, p4, p1}, true});
    }

    void fill() {
        std::vector<Geom> parts;
        for (const PdfSubpath& path : subpaths_) {
            if (path.points.size() < 3) continue;
            const Geom polygon = Geom::polygon(path.points).fixed();
            if (polygon.valid() && !polygon.empty()) parts.push_back(polygon);
        }
        if (parts.empty()) return;
        Geom filled = parts.front();
        for (std::size_t i = 1; i < parts.size(); ++i)
            filled = filled.unite(parts[i]).difference(filled.intersection(parts[i]));
        if (filled.valid() && !filled.empty()) output_.push_back(filled);
    }

    void stroke() {
        const double scale = std::sqrt(std::fabs(state_.ctm.a * state_.ctm.d -
                                                 state_.ctm.b * state_.ctm.c));
        const double width = state_.lineWidth * scale * kPointsToMm;
        std::vector<Geom> parts;
        for (const PdfSubpath& path : subpaths_) {
            if (path.points.size() < 2) continue;
            const Geom line = Geom::line(path.points);
            if (!line.valid() || line.empty()) continue;
            if (width > 0.02) {
                const Geom buffered = line.bufferStyle(width / 2.0, 8, 1, 1);
                if (buffered.valid() && !buffered.empty()) parts.push_back(buffered);
            } else {
                parts.push_back(line);
            }
        }
        if (parts.empty()) return;
        std::vector<Geom> lines;
        Geom solids = Geom::emptyPolygon();
        bool hasSolid = false;
        for (const Geom& part : parts) {
            if (part.typeId() == GEOS_POLYGON || part.typeId() == GEOS_MULTIPOLYGON) {
                solids = hasSolid ? solids.unite(part) : part;
                hasSolid = true;
            } else {
                lines.push_back(part);
            }
        }
        if (hasSolid && solids.valid() && !solids.empty()) output_.push_back(solids);
        output_.insert(output_.end(), lines.begin(), lines.end());
    }

    void clearPath() { subpaths_.clear(); }

    std::vector<Geom>& output_;
    PdfState state_;
    std::vector<PdfState> stack_;
    std::vector<PdfSubpath> subpaths_;
    std::vector<double> operands_;
};

}  // namespace

std::vector<Geom> importPdfVector(const std::string& path) {
    const std::string data = readFile(path);
    if (data.compare(0, 5, "%PDF-") != 0)
        throw std::runtime_error("not a PDF file");

    std::vector<Geom> output;
    const std::vector<std::string> streams = pdfContentStreams(data);
    for (const std::string& content : streams) {
        ContentParser parser(output, PdfState{});
        parser.parse(content);
    }
    return output;
}

}  // namespace fc
