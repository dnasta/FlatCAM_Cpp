/**
 * @file gerber.cpp
 * @brief Parser de Gerber RS-274X y del subconjunto X2 que hace falta.
 *
 * Tokeniza el archivo en sentencias terminadas en '*' y bloques entre '%',
 * mantiene un contexto de estado (formato FS, unidades, apertura activa,
 * interpolación, polaridad, región, step&repeat) y acumula la geometría en
 * "pending" para resolver la polaridad por diferencia. Al final convierte las
 * unidades del archivo a las de la aplicación y devuelve sólidos, recorridos,
 * aperturas, macros y advertencias.
 */
#include "flatcam/gerber.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fc {
namespace {

// ------------------------------------------------------------------ text utils

/// Prefijo barato sobre string_view.
bool startsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

/// Recorta espacios, tabs y saltos de línea en ambos extremos.
std::string_view trimView(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

// Content of a statement without the framing '%' and trailing '*'.
/**
 * @brief Devuelve el contenido de una sentencia sin los '%' ni el '*' externos.
 */
std::string_view body(std::string_view s) {
    s = trimView(s);
    if (!s.empty() && s.front() == '%') s.remove_prefix(1);
    if (!s.empty() && s.back() == '%') s.remove_suffix(1);
    s = trimView(s);
    if (!s.empty() && s.back() == '*') s.remove_suffix(1);
    return s;
}

/**
 * @brief Divide el texto Gerber en sentencias.
 *
 * Los bloques extendidos se agrupan desde un '%' hasta el siguiente; el resto
 * se corta en el '*' y se conserva el terminador en la cadena devuelta. Los
 * espacios y saltos de línea entre sentencias se descartan.
 * @param text Contenido completo del archivo.
 * @return Sentencias en orden, con su terminador.
 */
std::vector<std::string> splitStatements(std::string_view text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++i;
            continue;
        }
        if (c == '%') {
            const std::size_t end = text.find('%', i + 1);
            if (end == std::string_view::npos) {
                out.emplace_back(text.substr(i));
                break;
            }
            out.emplace_back(text.substr(i, end - i + 1));
            i = end + 1;
            continue;
        }
        const std::size_t end = text.find('*', i);
        if (end == std::string_view::npos) {
            out.emplace_back(text.substr(i));
            break;
        }
        out.emplace_back(text.substr(i, end - i + 1));
        i = end + 1;
    }
    return out;
}

/// Divide por un separador conservando los campos vacíos.
std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = s.find(sep, start);
        if (pos == std::string_view::npos) {
            out.emplace_back(s.substr(start));
            break;
        }
        out.emplace_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

/**
 * @brief Convierte texto a double con strtod.
 * @throw std::runtime_error si no hay ningún número al inicio del texto.
 */
double toDouble(std::string_view s) {
    std::string tmp(s);
    char* end = nullptr;
    const double v = std::strtod(tmp.c_str(), &end);
    if (end == tmp.c_str()) throw std::runtime_error("invalid number: " + tmp);
    return v;
}

/// Formatea con 17 dígitos significativos para una ida y vuelta exacta.
std::string formatNumber(double v) {
    std::ostringstream os;
    os.precision(17);
    os << v;
    return os.str();
}

// -------------------------------------------------------- expression parsing

/**
 * @brief Evaluador recursivo de expresiones de macros de apertura.
 *
 * Reconoce suma, resta, multiplicación (con '*' o 'x'), división, paréntesis y
 * signos unarios. Los números sólo admiten dígitos y punto, sin exponente, que
 * es lo que permite la especificación de macros Gerber.
 */
class ExprParser {
public:
    explicit ExprParser(std::string_view s) : s_(s) {}

    /**
     * @brief Evalúa la expresión completa.
     * @throw std::runtime_error si sobran caracteres sin consumir.
     */
    double parse() {
        const double v = expression();
        skip();
        if (pos_ != s_.size()) throw std::runtime_error("trailing characters in macro expression");
        return v;
    }

private:
    std::string_view s_;
    std::size_t pos_ = 0;

    /// Saltea espacios y tabs.
    void skip() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t')) ++pos_;
    }

    /// Consume el carácter @p c si es el próximo token, ignorando espacios.
    bool accept(char c) {
        skip();
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    /// Suma y resta de términos.
    double expression() {
        double v = term();
        while (true) {
            if (accept('+')) v += term();
            else if (accept('-')) v -= term();
            else return v;
        }
    }

    /// Producto y cociente de factores.
    double term() {
        double v = factor();
        while (true) {
            skip();
            if (pos_ < s_.size() && (s_[pos_] == '*' || s_[pos_] == 'x' || s_[pos_] == 'X')) {
                ++pos_;
                v *= factor();
            } else if (accept('/')) {
                const double d = factor();
                if (d == 0.0) throw std::runtime_error("division by zero in macro expression");
                v /= d;
            } else {
                return v;
            }
        }
    }

    /**
     * @brief Analiza un factor: signo, paréntesis o número.
     * @throw std::runtime_error si falta el paréntesis de cierre o el número.
     */
    double factor() {
        skip();
        if (accept('-')) return -factor();
        if (accept('+')) return factor();
        if (accept('(')) {
            const double v = expression();
            if (!accept(')')) throw std::runtime_error("unbalanced parenthesis in macro expression");
            return v;
        }
        const std::size_t start = pos_;
        bool dot = false;
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if ((c >= '0' && c <= '9') || c == '.') {
                dot = dot || c == '.';
                ++pos_;
            } else {
                break;
            }
        }
        if (start == pos_) throw std::runtime_error("expected number in macro expression");
        (void)dot;
        return toDouble(s_.substr(start, pos_ - start));
    }
};

/**
 * @brief Reemplaza $n y $nombre por su valor numérico.
 *
 * Los nombres son alfanuméricos con guión bajo. Una variable desconocida se
 * sustituye por 0, que es el criterio tolerante de la especificación.
 */
std::string substituteVariables(std::string_view text, const std::map<std::string, double>& vars) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] != '$') {
            out.push_back(text[i++]);
            continue;
        }
        std::size_t j = i + 1;
        while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_')) ++j;
        const std::string name(text.substr(i + 1, j - i - 1));
        const auto it = vars.find(name);
        out += it != vars.end() ? formatNumber(it->second) : "0";
        i = j;
    }
    return out;
}

/// Sustituye variables y evalúa la expresión resultante.
double evaluateExpression(std::string_view text, const std::map<std::string, double>& vars) {
    return ExprParser(substituteVariables(text, vars)).parse();
}

// --------------------------------------------------------------- aperture macro

/// Primitiva de macro ya evaluada; values[0] es el código (1, 20, 21...).
struct MacroPrimitive {
    std::vector<double> values;  // first element is the primitive code

    double at(std::size_t i) const { return i < values.size() ? values[i] : 0.0; }
};

/**
 * @brief Definición de una macro de apertura (%AM...%) y su construcción.
 *
 * Guarda el nombre y el texto crudo de las primitivas; build() las evalúa con
 * los modificadores de la apertura y devuelve la geometría combinada según la
 * exposición: las primitivas positivas se suman y las negativas se restan.
 */
class MacroDefinition {
public:
    std::string name;
    std::string raw;

    /**
     * @brief Construye la geometría de la macro para una apertura concreta.
     *
     * Recorre las líneas de la macro: las de comentario (empiezan con 0) se
     * saltean, las de forma $var=expr se evalúan y acumulan variables, y el
     * resto son primitivas cuyos parámetros se sustituyen y evalúan. Después
     * arma cada primitiva soportada: 1 círculo, 2 y 20 línea vectorial, 21
     * rectángulo centrado, 22 rectángulo desde la esquina, 4 contorno, 5
     * polígono regular, 6 moire y 7 thermal.
     * @param modifiers Parámetros $1..$n de la apertura ADD.
     * @param stepsPerCircle Segmentos por círculo; hoy sin uso, se fija 16.
     * @return Geometría de la macro; polígono vacío si no hay primitivas.
     */
    Geom build(const std::vector<double>& modifiers, int stepsPerCircle) const {
        std::map<std::string, double> vars;
        for (std::size_t i = 0; i < modifiers.size(); ++i) vars[std::to_string(i + 1)] = modifiers[i];

        std::vector<MacroPrimitive> primitives;
        for (const std::string& part : split(raw, '*')) {
            std::string_view p = trimView(part);
            if (p.empty()) continue;
            if (p.front() == '0') continue;  // comment
            if (p.front() == '$') {
                const std::size_t eq = p.find('=');
                if (eq == std::string_view::npos) continue;
                std::string varName(p.substr(1, eq - 1));
                while (!varName.empty() && std::isspace(static_cast<unsigned char>(varName.back())))
                    varName.pop_back();
                vars[varName] = evaluateExpression(p.substr(eq + 1), vars);
                continue;
            }
            if (!std::isdigit(static_cast<unsigned char>(p.front()))) continue;

            const std::string expanded = substituteVariables(p, vars);
            MacroPrimitive prim;
            for (const std::string& v : split(expanded, ',')) {
                if (trimView(v).empty()) continue;
                prim.values.push_back(evaluateExpression(v, {}));
            }
            if (!prim.values.empty()) primitives.push_back(std::move(prim));
        }

        Geom result = Geom::emptyPolygon();
        for (const MacroPrimitive& prim : primitives) {
            const int code = static_cast<int>(prim.at(0));
            Geom g;
            bool positive = true;
            switch (code) {
                case 1: {  // circle: exposure, diameter, x, y
                    positive = static_cast<int>(prim.at(1)) != 0;
                    const double dia = prim.at(2);
                    if (dia > 0.0) g = Geom::circle({prim.at(3), prim.at(4)}, dia / 2.0, 16);
                    break;
                }
                case 2:
                case 20: {  // vector line: exposure, width, xs, ys, xe, ye, rotation
                    positive = static_cast<int>(prim.at(1)) != 0;
                    const double w = prim.at(2);
                    if (w > 0.0) {
                        g = Geom::line({{prim.at(3), prim.at(4)}, {prim.at(5), prim.at(6)}})
                                .bufferStyle(w / 2.0, 16, 2 /*flat*/, 2 /*mitre*/, 5.0)
                                .rotateDeg(prim.at(7));
                    }
                    break;
                }
                case 21: {  // center line: exposure, width, height, x, y, rotation
                    positive = static_cast<int>(prim.at(1)) != 0;
                    const double w = prim.at(2);
                    const double h = prim.at(3);
                    const double x = prim.at(4);
                    const double y = prim.at(5);
                    if (w > 0.0 && h > 0.0)
                        g = Geom::box(x - w / 2.0, y - h / 2.0, x + w / 2.0, y + h / 2.0)
                                .rotateDeg(prim.at(6));
                    break;
                }
                case 22: {  // lower-left line: exposure, width, height, x, y, rotation
                    positive = static_cast<int>(prim.at(1)) != 0;
                    const double w = prim.at(2);
                    const double h = prim.at(3);
                    const double x = prim.at(4);
                    const double y = prim.at(5);
                    if (w > 0.0 && h > 0.0)
                        g = Geom::box(x, y, x + w, y + h).rotateDeg(prim.at(6));
                    break;
                }
                case 4: {  // outline: exposure, n, x0,y0,...,xn,yn, rotation
                    positive = static_cast<int>(prim.at(1)) != 0;
                    const int n = static_cast<int>(prim.at(2));
                    Ring pts;
                    for (int i = 0; i <= n; ++i) pts.push_back({prim.at(3 + 2 * i), prim.at(4 + 2 * i)});
                    if (pts.size() >= 3) g = Geom::polygon(pts).fixed().rotateDeg(prim.at(3 + 2 * (n + 1)));
                    break;
                }
                case 5: {  // regular polygon: exposure, n, x, y, dia, rotation
                    positive = static_cast<int>(prim.at(1)) != 0;
                    const int n = static_cast<int>(prim.at(2));
                    const double x = prim.at(3);
                    const double y = prim.at(4);
                    const double dia = prim.at(5);
                    g = Geom::regularPolygon({x, y}, dia, n).rotateDeg(prim.at(6));
                    break;
                }
                case 6: {  // moire: x, y, dia, thickness, gap, max rings, cross thickness, cross length, rotation
                    const double x = prim.at(1);
                    const double y = prim.at(2);
                    const double dia = prim.at(3);
                    const double th = prim.at(4);
                    const double gap = prim.at(5);
                    const int rings = std::max(1, static_cast<int>(prim.at(6)));
                    const double crossTh = prim.at(7);
                    const double crossLen = prim.at(8);
                    const double angle = prim.at(9);
                    double r = dia / 2.0 - th / 2.0;
                    for (int i = 0; i < rings && r > 0.0; ++i) {
                        Geom ringPoly = Geom::circle({x, y}, r, 16);
                        Geom ringLine = Geom::line(ringPoly.polygons().front().outer);
                        g = g.valid() ? g.unite(ringLine.buffer(th / 2.0, 16))
                                      : ringLine.buffer(th / 2.0, 16);
                        r -= th + gap;
                    }
                    if (crossTh > 0.0 && crossLen > 0.0) {
                        const double half = crossLen / 2.0;
                        Geom hor = Geom::line({{x - half, y}, {x + half, y}})
                                       .bufferStyle(crossTh / 2.0, 16, 2, 2, 5.0);
                        Geom ver = Geom::line({{x, y - half}, {x, y + half}})
                                       .bufferStyle(crossTh / 2.0, 16, 2, 2, 5.0);
                        g = g.valid() ? g.unite(hor).unite(ver) : hor.unite(ver);
                    }
                    if (g.valid()) g = g.rotateDeg(angle);
                    break;
                }
                case 7: {  // thermal: x, y, outer dia, inner dia, gap, rotation
                    const double x = prim.at(1);
                    const double y = prim.at(2);
                    const double dout = prim.at(3);
                    const double din = prim.at(4);
                    const double t = prim.at(5);
                    const double angle = prim.at(6);
                    Geom ring = Geom::circle({x, y}, dout / 2.0, 16)
                                    .difference(Geom::circle({x, y}, din / 2.0, 16));
                    Geom hline = Geom::line({{x - dout / 2.0, y}, {x + dout / 2.0, y}})
                                     .bufferStyle(t / 2.0, 16, 3 /*square*/, 2, 5.0);
                    Geom vline = Geom::line({{x, y - dout / 2.0}, {x, y + dout / 2.0}})
                                     .bufferStyle(t / 2.0, 16, 3, 2, 5.0);
                    g = ring.difference(hline.unite(vline)).rotateDeg(angle);
                    break;
                }
                default:
                    break;
            }
            if (!g.valid()) continue;
            result = positive ? (result.valid() ? result.unite(g) : g) : result.difference(g);
        }
        (void)stepsPerCircle;
        return result;
    }
};

// ---------------------------------------------------------------- arc support

/**
 * @brief Genera los puntos de un arco circular.
 *
 * Normaliza el ángulo final según el sentido, sumando o restando 2 pi, y
 * muestrea con stepsPerCircle segmentos por vuelta, con un mínimo de 2.
 * @param center Centro del arco.
 * @param radius Radio.
 * @param start Ángulo inicial en radianes.
 * @param stop Ángulo final en radianes.
 * @param ccw true para sentido antihorario (G03).
 * @param stepsPerCircle Resolución de la aproximación.
 * @return Puntos del arco, incluidos ambos extremos.
 */
std::vector<Pt> arcPoints(const Pt& center, double radius, double start, double stop, bool ccw,
                          int stepsPerCircle) {
    if (ccw && stop <= start) stop += 2.0 * M_PI;
    if (!ccw && stop >= start) stop -= 2.0 * M_PI;
    const double angle = std::fabs(stop - start);
    const int steps = std::max(2, static_cast<int>(std::ceil(angle / (2.0 * M_PI) * stepsPerCircle)));
    const double delta = (ccw ? 1.0 : -1.0) * angle / steps;
    std::vector<Pt> pts;
    pts.reserve(static_cast<std::size_t>(steps) + 1);
    for (int i = 0; i <= steps; ++i) {
        const double theta = start + delta * i;
        pts.push_back({center.x + radius * std::cos(theta), center.y + radius * std::sin(theta)});
    }
    return pts;
}

/**
 * @brief Amplitud angular recorrida entre dos ángulos en un sentido dado.
 * @return Ángulo en radianes, siempre positivo.
 */
double arcAngle(double start, double stop, bool ccw) {
    if (ccw && stop <= start) stop += 2.0 * M_PI;
    if (!ccw && stop >= start) stop -= 2.0 * M_PI;
    return std::fabs(stop - start);
}

// ------------------------------------------------------------------- parser

/// Sentencia ya tokenizada; hoy no se usa y se conserva por compatibilidad.
struct Word {
    std::string_view text;
};

/// Campos reconocidos de una sentencia: ejes, G, D y M.
struct Words {
    std::optional<std::string> x, y, i, j;
    int g = -1;
    int d = -1;
    int m = -1;
};

/// Lee un número con signo opcional, formado por dígitos y punto.
std::string readToken(std::string_view s, std::size_t& i) {
    const std::size_t start = i;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.')) ++i;
    if (start == i) return {};
    return std::string(s.substr(start, i - start));
}

/**
 * @brief Lee un entero de la sentencia.
 * @return El valor, o -1 si no hay dígitos.
 * @note -1 es el centinela de "campo ausente" en Words.
 */
int readInt(std::string_view s, std::size_t& i) {
    const std::string t = readToken(s, i);
    if (t.empty()) return -1;
    return std::atoi(t.c_str());
}

/**
 * @brief Extrae los campos de una sentencia Gerber.
 *
 * Reconoce G (interpolación), X, Y, I y J (coordenadas), D (operación o
 * apertura) y M (fin de programa); cualquier otro carácter se saltea para
 * tolerar formatos con prefijos como G54.
 * @param s Cuerpo de la sentencia, sin '%' ni '*'.
 * @return Palabras encontradas; los campos ausentes quedan sin valor o en -1.
 */
Words scanWords(std::string_view s) {
    Words w;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == 'G') {
            ++i;
            const int v = readInt(s, i);
            if (v >= 0) w.g = v;
        } else if (c == 'X' || c == 'Y' || c == 'I' || c == 'J') {
            ++i;
            std::string t = readToken(s, i);
            if (!t.empty()) {
                switch (c) {
                    case 'X': w.x = std::move(t); break;
                    case 'Y': w.y = std::move(t); break;
                    case 'I': w.i = std::move(t); break;
                    default: w.j = std::move(t); break;
                }
            }
        } else if (c == 'D') {
            ++i;
            const int v = readInt(s, i);
            if (v >= 0) w.d = v;
        } else if (c == 'M') {
            ++i;
            const int v = readInt(s, i);
            if (v >= 0) w.m = v;
        } else {
            ++i;
        }
    }
    return w;
}

/**
 * @brief Estado completo del parser durante una pasada.
 *
 * Incluye la posición actual, el modo de interpolación y de región, el formato
 * de coordenadas, la apertura activa, los acumuladores de geometría pendiente
 * y de polaridad, el step&repeat y la lista de recorridos.
 */
struct Context {
    const GerberOptions& opt;
    GerberResult& res;

    double x = 0.0;
    double y = 0.0;
    int interpolation = 1;  // 1 linear, 2 cw, 3 ccw
    int opcode = -1;
    bool regionMode = false;
    bool singleQuadrant = false;
    bool quadrantKnown = false;
    bool incremental = false;
    bool lpc = false;

    std::string aperture;
    std::string lastAperture;

    std::vector<Pt> path;
    std::vector<std::vector<Pt>> regionContours;
    std::vector<std::vector<Pt>> contours;

    std::vector<Geom> pending;
    bool pendingDark = true;
    bool hasAccumulated = false;
    Geom accumulated;

    std::vector<Geom> follows;

    int srX = 1;
    int srY = 1;
    double srI = 0.0;
    double srJ = 0.0;
    bool srActive = false;

    Context(const GerberOptions& o, GerberResult& r) : opt(o), res(r) {}
};

/**
 * @brief Convierte un campo de coordenada a unidades del archivo.
 *
 * Si el texto ya trae punto decimal se usa tal cual (formato explícito); si no,
 * aplica parseGerberNumber con los dígitos de FS y el modo de ceros vigente.
 */
double coordinate(const Context& ctx, const std::string& text) {
    if (text.find('.') != std::string::npos) return toDouble(text);
    return parseGerberNumber(text, ctx.res.intDigits, ctx.res.fracDigits, ctx.res.zeros);
}

/**
 * @brief Normaliza un código de apertura a su forma decimal sin ceros.
 * @note "010" y "D10" terminan siendo la misma clave "10".
 */
std::string normalizeApertureId(const std::string& id) {
    const int v = std::atoi(id.c_str());
    return std::to_string(v);
}

/**
 * @brief Vuelca la geometría pendiente al acumulador según la polaridad.
 *
 * Une lo pendiente en un solo Geom y, si la polaridad es oscura, lo suma al
 * acumulado; si es clara, lo resta. Marca hasAccumulated para que la primera
 * operación sepa que debe partir de cero.
 */
void flushPending(Context& ctx) {
    if (ctx.pending.empty()) return;
    Geom u = Geom::unionOf(ctx.pending);
    ctx.pending.clear();
    if (!u.valid()) return;
    if (ctx.pendingDark) {
        ctx.accumulated = ctx.hasAccumulated ? ctx.accumulated.unite(u) : u;
    } else if (ctx.hasAccumulated) {
        ctx.accumulated = ctx.accumulated.difference(u);
    }
    ctx.hasAccumulated = true;
}

/**
 * @brief Cambia la polaridad (LP) vaciando antes lo pendiente.
 *
 * El orden importa: la geometría dibujada antes del cambio debe resolverse con
 * la polaridad anterior, así que se hace el flush primero.
 * @param clear true para polaridad clara (resta), false para oscura (suma).
 */
void setPolarity(Context& ctx, bool clear) {
    if (ctx.lpc == clear) return;
    flushPending(ctx);
    ctx.lpc = clear;
    ctx.pendingDark = !clear;
}

/**
 * @brief Encola geometría aplicando step&repeat si está activo.
 *
 * Con SR activo y más de una repetición clona la geometría en cada celda,
 * desplazándola por (i·I, j·J); si no, la encola una sola vez.
 */
void addGeometry(Context& ctx, Geom g) {
    if (!g.valid() || g.empty()) return;
    std::vector<Geom> copies;
    if (ctx.srActive && (ctx.srX > 1 || ctx.srY > 1)) {
        for (int iy = 0; iy < ctx.srY; ++iy) {
            for (int ix = 0; ix < ctx.srX; ++ix) {
                copies.push_back(g.translate(ix * ctx.srI, iy * ctx.srJ));
            }
        }
    } else {
        copies.push_back(std::move(g));
    }
    for (Geom& c : copies) ctx.pending.push_back(std::move(c));
}

/**
 * @brief Asocia una geometría a la apertura activa.
 *
 * Alimenta las estadísticas por apertura y las herramientas que trabajan sobre
 * apertura.geometry, como la extracción de taladros.
 */
void recordApertureGeometry(Context& ctx, const Geom& g) {
    if (ctx.aperture.empty()) return;
    auto it = ctx.res.apertures.find(ctx.aperture);
    if (it != ctx.res.apertures.end()) it->second.geometry.push_back(g);
}

/**
 * @brief Construye la geometría de un flash (D03) según la apertura.
 *
 * Círculo, rectángulo, obround y polígono usan el tamaño declarado; las macro
 * se construyen y se trasladan al punto del flash.
 * @return Geometría del flash, o nula si la macro referenciada no existe.
 */
Geom flashGeometry(const Context& ctx, const Pt& loc, const GerberAperture& ap) {
    const int steps = ctx.opt.stepsPerCircle;
    const int quad = std::max(2, steps / 4);
    switch (ap.kind) {
        case GerberAperture::Kind::Circle:
            return Geom::circle(loc, ap.size / 2.0, quad);
        case GerberAperture::Kind::Rectangle:
            return Geom::box(loc.x - ap.width / 2.0, loc.y - ap.height / 2.0,
                             loc.x + ap.width / 2.0, loc.y + ap.height / 2.0);
        case GerberAperture::Kind::Obround:
            return Geom::obround(loc, ap.width, ap.height, quad);
        case GerberAperture::Kind::Polygon:
            return Geom::regularPolygon(loc, ap.diameter, ap.vertices,
                                        ap.hasRotation ? ap.rotation : 0.0);
        case GerberAperture::Kind::Macro: {
            const auto it = ctx.res.macros.find(ap.macroName);
            if (it == ctx.res.macros.end()) return Geom();
            MacroDefinition def{it->second.name, it->second.raw};
            Geom g = def.build(ap.modifiers, steps);
            if (!g.valid()) return Geom();
            return g.translate(loc.x, loc.y);
        }
        default:
            return Geom();
    }
}

/**
 * @brief Cierra el trazo actual y lo convierte en geometría.
 *
 * Siempre guarda el camino como recorrido (follow). Para el sólido usa la
 * apertura del trazo: con apertura rectangular aplica la suma de Minkowski del
 * segmento (envolvente convexa de los dos rectángulos extremos) o, con
 * legacyRectStroke, el rectángulo único entre los dos primeros puntos para
 * reproducir a FlatCAM; con apertura circular aplica un buffer de radio
 * diámetro/1,999.
 * @note El 1,999 es un ajuste histórico para evitar que GEOS fusione trazos
 *       consecutivos por el redondeo del buffer.
 */
void finishPath(Context& ctx) {
    if (ctx.path.size() < 2) {
        ctx.path.clear();
        return;
    }

    const Geom line = Geom::line(ctx.path);
    if (!ctx.path.empty()) ctx.follows.push_back(line);

    const auto apIt = ctx.res.apertures.find(ctx.lastAperture.empty() ? ctx.aperture : ctx.lastAperture);
    if (apIt != ctx.res.apertures.end()) {
        const GerberAperture& ap = apIt->second;
        if (ap.kind == GerberAperture::Kind::Rectangle && ap.width > 0.0 && ap.height > 0.0) {
            const double hw = ap.width / 2.0;
            const double hh = ap.height / 2.0;
            auto rectAt = [hw, hh](const Pt& p) {
                return Geom::box(p.x - hw, p.y - hh, p.x + hw, p.y + hh);
            };
            if (ctx.opt.legacyRectStroke) {
                // Bit-for-bit FlatCAM behaviour: one box from the first two
                // points of the path, regardless of later segments.
                if (ctx.path.size() >= 2) {
                    const Pt& a = ctx.path[0];
                    const Pt& b = ctx.path[1];
                    addGeometry(ctx, Geom::box(std::min(a.x, b.x) - hw, std::min(a.y, b.y) - hh,
                                               std::max(a.x, b.x) + hw, std::max(a.y, b.y) + hh));
                }
            } else {
                // Exact Minkowski sum of the segment with the rectangle: the
                // convex hull of the two endpoint rectangles.
                for (std::size_t i = 1; i < ctx.path.size(); ++i) {
                    const Geom swept = rectAt(ctx.path[i - 1]).unite(rectAt(ctx.path[i])).convexHull();
                    addGeometry(ctx, swept);
                }
            }
        } else if (ap.size > 0.0) {
            // FlatCAM usa `diámetro / 1.999` (no /2) para que el buffer no
            // pierda el contacto por redondeo en trazos muy finos; se mantiene
            // por paridad geométrica.
            Geom buffered = line.buffer(ap.size / 1.999, std::max(2, ctx.opt.stepsPerCircle / 4));
            addGeometry(ctx, buffered);
            recordApertureGeometry(ctx, buffered);
        }
    }
    ctx.path = {{ctx.x, ctx.y}};
}

/**
 * @brief Cierra un contorno de región (G36) y lo agrega como polígono.
 *
 * Repara el polígono si GEOS lo considera inválido, registra su anillo
 * exterior como recorrido, lo agrega a la geometría pendiente y cuenta la
 * región. Los contornos con menos de 3 puntos se descartan.
 */
void finishRegionContour(Context& ctx) {
    if (ctx.path.size() >= 3) {
        Geom poly = Geom::polygon(ctx.path);
        if (!poly.valid() || !poly.isValid()) poly = poly.fixed();
        if (poly.valid() && !poly.empty()) {
            const auto polys = poly.polygons();
            if (!polys.empty()) ctx.follows.push_back(Geom::line(polys.front().outer));
            addGeometry(ctx, poly);
            recordApertureGeometry(ctx, poly);
            ++ctx.res.regions;
        }
    }
    ctx.path.clear();
}

/**
 * @brief Parsea una definición de apertura (%ADD...%).
 *
 * Separa código, tipo y parámetros, convirtiendo las 'X' en separadores, y
 * llena la estructura según C, R, O y P. Cualquier otro tipo se busca entre las
 * macros ya definidas; si no existe se emite una advertencia y se ignora.
 * @note En R y O también se guarda size como la hipotenusa de ancho y alto, que
 *       es el valor que usa el buffer de los trazos.
 */
void parseApertureDefinition(Context& ctx, std::string_view content) {
    // content: ADD10C,0.5  /  ADD14THERMAL,0.1X0.2
    if (!startsWith(content, "ADD")) return;
    std::size_t i = 3;
    const std::size_t start = i;
    while (i < content.size() && std::isdigit(static_cast<unsigned char>(content[i]))) ++i;
    if (i == start) return;
    const std::string id = normalizeApertureId(std::string(content.substr(start, i - start)));

    const std::size_t typeStart = i;
    while (i < content.size() &&
           (std::isalnum(static_cast<unsigned char>(content[i])) || content[i] == '_' ||
            content[i] == '.' || content[i] == '$' || content[i] == '-')) {
        ++i;
    }
    const std::string typeName(content.substr(typeStart, i - typeStart));
    if (typeName.empty()) return;

    std::string params(content.substr(i));
    if (!params.empty() && params.front() == ',') params.erase(params.begin());
    std::replace(params.begin(), params.end(), 'X', ',');
    std::vector<std::string> parts = split(params, ',');
    std::vector<double> values;
    for (const std::string& p : parts) {
        if (trimView(p).empty()) continue;
        values.push_back(toDouble(trimView(p)));
    }

    GerberAperture ap;
    const char type = static_cast<char>(std::toupper(static_cast<unsigned char>(typeName[0])));
    const bool standard = typeName.size() == 1;
    if (standard && type == 'C') {
        if (values.empty()) return;
        ap.kind = GerberAperture::Kind::Circle;
        ap.size = values[0];
    } else if (standard && type == 'R') {
        if (values.size() < 2) return;
        ap.kind = GerberAperture::Kind::Rectangle;
        ap.width = values[0];
        ap.height = values[1];
        ap.size = std::hypot(ap.width, ap.height);
    } else if (standard && type == 'O') {
        if (values.size() < 2) return;
        ap.kind = GerberAperture::Kind::Obround;
        ap.width = values[0];
        ap.height = values[1];
        ap.size = std::hypot(ap.width, ap.height);
    } else if (standard && type == 'P') {
        if (values.size() < 2) return;
        ap.kind = GerberAperture::Kind::Polygon;
        ap.diameter = values[0];
        ap.vertices = static_cast<int>(values[1]);
        ap.size = values[0];
        if (values.size() >= 3) {
            ap.rotation = values[2];
            ap.hasRotation = true;
        }
    } else {
        if (ctx.res.macros.find(typeName) == ctx.res.macros.end()) {
            ctx.res.warnings.push_back("unsupported aperture type: " + typeName);
            return;
        }
        ap.kind = GerberAperture::Kind::Macro;
        ap.macroName = typeName;
        ap.modifiers = values;
    }
    ctx.res.apertures[id] = std::move(ap);
}

/**
 * @brief Registra una macro de apertura (%AM...%) con nombre y texto crudo.
 */
void parseMacroDefinition(Context& ctx, std::string_view content) {
    // content: AMNAME*raw
    std::string_view rest = content.substr(2);
    const std::size_t star = rest.find('*');
    if (star == std::string_view::npos) return;
    GerberMacro macro;
    macro.name = std::string(rest.substr(0, star));
    macro.raw = std::string(rest.substr(star + 1));
    ctx.res.macros[macro.name] = std::move(macro);
}

/**
 * @brief Interpreta el comando FS: dígitos y modo de ceros.
 *
 * Espera FS, el modo de ceros (L, T o D) y absoluto o incremental (A o I), y
 * toma los dos dígitos que siguen a la X como enteros y fraccionarios. Al final
 * vuelve a mirar A e I para fijar el modo incremental.
 */
void parseFormatSpec(Context& ctx, std::string_view content) {
    // FSLAX34Y34  /  FSTIX24Y24
    if (content.size() < 4) return;
    std::size_t i = 2;
    if (i < content.size() && (content[i] == 'L' || content[i] == 'T' || content[i] == 'D'))
        ctx.res.zeros = content[i++];
    if (i < content.size() && (content[i] == 'A' || content[i] == 'I'))
        ctx.incremental = content[i++] == 'I';
    const std::size_t x = content.find('X', i);
    if (x != std::string_view::npos && x + 2 < content.size()) {
        ctx.res.intDigits = content[x + 1] - '0';
        ctx.res.fracDigits = content[x + 2] - '0';
    }
    const std::size_t a = content.find('A', i);
    if (a != std::string_view::npos) ctx.incremental = false;
    const std::size_t inc = content.find('I', i);
    if (inc != std::string_view::npos) ctx.incremental = true;
}

/**
 * @brief Cambia la apertura activa cerrando antes el trazo en curso.
 *
 * Cerrar primero es obligatorio: la geometría del trazo pertenece a la apertura
 * anterior.
 */
void switchAperture(Context& ctx, const std::string& id) {
    finishPath(ctx);
    ctx.aperture = normalizeApertureId(id);
    ctx.lastAperture = ctx.aperture;
}

/**
 * @brief Procesa un movimiento con interpolación circular (G02/G03).
 *
 * Calcula el punto final, absoluto o incremental, y el centro a partir de I y
 * J, y genera el arco muestreado. En modo multiquadrante el centro es directo;
 * en modo simple (G74) prueba las cuatro combinaciones de signos y elige la que
 * cierra con el radio y no supera 90 grados. Dentro de una región sólo agrega
 * puntos al contorno; fuera, completa el trazo y cuenta el segmento.
 * @note Sin G74/G75 previo se asume multiquadrante y se avisa una vez. Un D03
 *       dentro de un arco se ignora con advertencia.
 */
void handleArc(Context& ctx, const Words& w) {
    const bool ccw = ctx.interpolation == 3;

    double endX = w.x ? coordinate(ctx, *w.x) : ctx.x;
    double endY = w.y ? coordinate(ctx, *w.y) : ctx.y;
    if (ctx.incremental) {
        if (w.x) endX += ctx.x;
        if (w.y) endY += ctx.y;
    }
    double i = w.i ? coordinate(ctx, *w.i) : 0.0;
    double j = w.j ? coordinate(ctx, *w.j) : 0.0;

    if (!ctx.quadrantKnown) {
        ctx.res.warnings.push_back("arc without G74/G75, assuming multi-quadrant");
        ctx.quadrantKnown = true;
        ctx.singleQuadrant = false;
    }

    if (w.d >= 0 && w.d <= 3) ctx.opcode = w.d;

    if (ctx.opcode == 2) {
        finishPath(ctx);
        ctx.x = endX;
        ctx.y = endY;
        ctx.path = {{ctx.x, ctx.y}};
        return;
    }
    if (ctx.opcode == 3) {
        ctx.res.warnings.push_back("flash (D03) inside an arc, ignored");
        return;
    }

    std::vector<Pt> points;
    if (!ctx.singleQuadrant) {
        const Pt center{ctx.x + i, ctx.y + j};
        const double radius = std::hypot(i, j);
        const double start = std::atan2(-j, -i);
        double stop = start;
        if (!(endX == ctx.x && endY == ctx.y))
            stop = std::atan2(endY - center.y, endX - center.x);
        points = arcPoints(center, radius, start, stop, ccw, ctx.opt.stepsPerCircle);
        if (!points.empty()) points.back() = {endX, endY};
    } else {
        const std::pair<double, double> candidates[4] = {
            {i, j}, {-i, j}, {i, -j}, {-i, -j}};
        const double radius = std::hypot(i, j);
        bool found = false;
        for (const auto& [ci, cj] : candidates) {
            const Pt center{ctx.x + ci, ctx.y + cj};
            const double radius2 = std::hypot(center.x - endX, center.y - endY);
            if (radius2 < radius * 0.95 || radius2 > radius * 1.05) continue;
            const double start = std::atan2(-cj, -ci);
            const double stop = std::atan2(endY - center.y, endX - center.x);
            if (arcAngle(start, stop, ccw) > (M_PI + 1e-6) / 2.0) continue;
            points = arcPoints(center, radius, start, stop, ccw, ctx.opt.stepsPerCircle);
            if (!points.empty()) points.back() = {endX, endY};
            found = true;
            break;
        }
        if (!found) {
            ctx.res.warnings.push_back("invalid single-quadrant arc");
            return;
        }
    }

    if (ctx.regionMode) {
        for (const Pt& p : points) ctx.path.push_back(p);
    } else {
        if (ctx.path.empty()) ctx.path.push_back({ctx.x, ctx.y});
        ctx.path.insert(ctx.path.end(), points.begin(), points.end());
        ctx.lastAperture = ctx.aperture;
        ++ctx.res.segments;
    }
    ctx.x = endX;
    ctx.y = endY;
}

/**
 * @brief Procesa D01, D02 y D03 con coordenadas lineales.
 *
 * D01 extiende el trazo, o el contorno de región; D02 cierra el trazo y mueve
 * el cursor; D03 hace un flash con la apertura activa. Los códigos D mayores o
 * iguales a 10 se interpretan como selección de apertura.
 */
void handleMoveOrDraw(Context& ctx, const Words& w) {
    double nx = w.x ? coordinate(ctx, *w.x) : ctx.x;
    double ny = w.y ? coordinate(ctx, *w.y) : ctx.y;
    if (ctx.incremental) {
        if (w.x) nx += ctx.x;
        if (w.y) ny += ctx.y;
    }

    if (w.d >= 0 && w.d <= 3) ctx.opcode = w.d;
    if (w.d >= 10) {
        switchAperture(ctx, std::to_string(w.d));
        return;
    }

    switch (ctx.opcode) {
        case 1:
            if (ctx.regionMode) {
                if (ctx.path.empty()) ctx.path.push_back({ctx.x, ctx.y});
                ctx.path.push_back({nx, ny});
            } else {
                if (ctx.path.empty()) ctx.path.push_back({ctx.x, ctx.y});
                ctx.path.push_back({nx, ny});
                ctx.lastAperture = ctx.aperture;
                ++ctx.res.segments;
            }
            break;
        case 2:
            if (ctx.regionMode) {
                finishRegionContour(ctx);
            } else {
                finishPath(ctx);
            }
            ctx.x = nx;
            ctx.y = ny;
            ctx.path = {{ctx.x, ctx.y}};
            return;
        case 3: {
            finishPath(ctx);
            const auto it = ctx.res.apertures.find(ctx.aperture);
            if (it != ctx.res.apertures.end()) {
                Geom flash = flashGeometry(ctx, {nx, ny}, it->second);
                if (flash.valid() && !flash.empty()) {
                    addGeometry(ctx, flash);
                    recordApertureGeometry(ctx, flash);
                    ctx.follows.push_back(Geom::point(nx, ny));
                    ++ctx.res.flashes;
                }
            } else {
                ctx.res.warnings.push_back("flash without a valid aperture");
            }
            ctx.x = nx;
            ctx.y = ny;
            ctx.path = {{ctx.x, ctx.y}};
            return;
        }
        default:
            ctx.x = nx;
            ctx.y = ny;
            if (ctx.path.empty()) ctx.path.push_back({ctx.x, ctx.y});
            return;
    }
    ctx.x = nx;
    ctx.y = ny;
}

/**
 * @brief Despacha una sentencia Gerber al manejador correspondiente.
 *
 * Reconoce los comandos extendidos (FS, MO, AD, AM, LP, TD, SR, AB), los de
 * unidad y modo (G70/G71, G90/G91, G74/G75), las regiones (G36/G37) y, por
 * último, las sentencias con palabras (G01/G02/G03, coordenadas y D). Los
 * comentarios G04 y los atributos X2 (TF, TA, TO, AS, IN, LN) se ignoran.
 */
void handleStatement(Context& ctx, const std::string& statement) {
    const std::string_view raw = trimView(statement);
    if (raw.empty()) return;
    const std::string_view b = body(raw);

    if (startsWith(b, "G04") || startsWith(b, "G4") || startsWith(b, "0 ")) return;
    if (b == "M02" || b == "M00" || b == "M2") return;
    if (startsWith(b, "FS")) {
        parseFormatSpec(ctx, b);
        return;
    }
    if (startsWith(b, "MO")) {
        if (b.size() >= 4) {
            const std::string_view u = b.substr(2, 2);
            ctx.res.fileUnits = (u == "IN") ? Units::Inch : Units::Mm;
        }
        return;
    }
    if (startsWith(b, "AD")) {
        parseApertureDefinition(ctx, b);
        return;
    }
    if (startsWith(b, "AM")) {
        parseMacroDefinition(ctx, b);
        return;
    }
    if (startsWith(b, "LP")) {
        setPolarity(ctx, b.size() >= 3 && b[2] == 'C');
        return;
    }
    if (startsWith(b, "IP")) return;
    if (startsWith(b, "TD")) {
        std::string id = normalizeApertureId(std::string(b.substr(2)));
        ctx.res.apertures.erase(id);
        return;
    }
    if (startsWith(b, "TF") || startsWith(b, "TA") || startsWith(b, "TO") || startsWith(b, "AS") ||
        startsWith(b, "IN") || startsWith(b, "LN")) {
        return;
    }
    if (startsWith(b, "SR")) {
        flushPending(ctx);  // order matters: flush before switching repeat mode
        if (b.size() == 2) {
            ctx.srActive = false;
            ctx.srX = ctx.srY = 1;
            return;
        }
        std::size_t i = 2;
        while (i < b.size()) {
            const char c = b[i++];
            if (c == 'X') {
                const std::string t = readToken(b, i);
                if (!t.empty()) ctx.srX = std::atoi(t.c_str());
            } else if (c == 'Y') {
                const std::string t = readToken(b, i);
                if (!t.empty()) ctx.srY = std::atoi(t.c_str());
            } else if (c == 'I') {
                const std::string t = readToken(b, i);
                if (!t.empty()) ctx.srI = toDouble(t);
            } else if (c == 'J') {
                const std::string t = readToken(b, i);
                if (!t.empty()) ctx.srJ = toDouble(t);
            } else if (c == '*') {
                break;
            }
        }
        ctx.srActive = true;
        return;
    }
    if (startsWith(b, "AB")) {
        ctx.res.warnings.push_back("aperture blocks (AB) are not supported");
        return;
    }
    if (b == "G36") {
        finishPath(ctx);
        ctx.regionMode = true;
        ctx.path.clear();
        return;
    }
    if (b == "G37") {
        finishRegionContour(ctx);
        ctx.regionMode = false;
        ctx.path.clear();
        return;
    }
    if (b == "G70" || b == "G71") {
        ctx.res.fileUnits = (b == "G70") ? Units::Inch : Units::Mm;
        return;
    }
    if (b == "G90" || b == "G91") {
        ctx.incremental = (b == "G91");
        return;
    }
    if (b == "G74" || b == "G75") {
        ctx.singleQuadrant = (b == "G74");
        ctx.quadrantKnown = true;
        return;
    }
    if (startsWith(b, "G54")) {
        // deprecated tool selection prefix, falls through to D-code scan
    }

    const Words w = scanWords(b);
    if (w.g == 1 || w.g == 2 || w.g == 3) {
        ctx.interpolation = w.g;
        if (w.x || w.y || w.i || w.j || w.d >= 0) {
            if (ctx.interpolation == 1)
                handleMoveOrDraw(ctx, w);
            else
                handleArc(ctx, w);
        }
        return;
    }
    if (w.d >= 10) {
        switchAperture(ctx, std::to_string(w.d));
        return;
    }
    if (w.x || w.y || w.i || w.j || (w.d >= 0 && w.d <= 3)) {
        if (ctx.interpolation == 2 || ctx.interpolation == 3) {
            handleArc(ctx, w);
        } else {
            handleMoveOrDraw(ctx, w);
        }
        return;
    }
    if (w.m == 2) return;
}

/**
 * @brief Cierra el parseo: vuelca, convierte unidades y arma el resultado.
 *
 * Vuelca la región abierta, el trazo en curso y la geometría pendiente; aplica
 * el factor de conversión de las unidades del archivo a las de la aplicación,
 * simplifica si está configurado, calcula los límites (desde el sólido o, si no
 * hay, desde los recorridos) y reescala tamaños de apertura y sus geometrías.
 */
void finalize(Context& ctx) {
    if (ctx.regionMode) finishRegionContour(ctx);
    finishPath(ctx);
    flushPending(ctx);

    const double factor = conversionFactor(ctx.res.fileUnits, ctx.opt.appUnits);
    auto convertGeom = [factor](const Geom& g) {
        return factor == 1.0 ? g : g.scale(factor, factor);
    };

    if (ctx.hasAccumulated && ctx.accumulated.valid() && !ctx.accumulated.empty()) {
        Geom solid = convertGeom(ctx.accumulated);
        if (ctx.opt.simplification)
            solid = solid.simplify(ctx.opt.simplifyTolerance * factor);
        ctx.res.solid = solid;
        ctx.res.solids.push_back(solid);
        ctx.res.bounds = solid.bounds();
    }

    for (const Geom& g : ctx.follows) {
        if (g.valid() && !g.empty()) ctx.res.follows.push_back(convertGeom(g));
    }
    if (!ctx.res.bounds.valid) {
        for (const Geom& g : ctx.res.follows) {
            const BBox b = g.bounds();
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
    }

    for (auto& [id, ap] : ctx.res.apertures) {
        if (factor == 1.0) continue;
        ap.size *= factor;
        ap.width *= factor;
        ap.height *= factor;
        ap.diameter *= factor;
        for (Geom& g : ap.geometry) g = g.scale(factor, factor);
    }
}

}  // namespace

/// Guarda las opciones de parseo.
GerberParser::GerberParser(GerberOptions options) : opt_(options) {}

/**
 * @brief Parsea un Gerber que ya está en memoria.
 * @param source Texto completo del archivo.
 * @return Resultado con sólido, recorridos, aperturas, macros y avisos.
 * @note Las unidades iniciales se asumen pulgadas hasta que aparezca MO,
 *       G70 o G71.
 */
GerberResult GerberParser::parseString(std::string_view source) const {
    GerberResult result;
    result.fileUnits = Units::Inch;
    Context ctx(opt_, result);
    for (const std::string& statement : splitStatements(source)) handleStatement(ctx, statement);
    finalize(ctx);
    return result;
}

/**
 * @brief Lee y parsea un archivo Gerber.
 * @throw std::runtime_error si no se puede abrir.
 */
GerberResult GerberParser::parseFile(const std::string& path) const {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open Gerber file: " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return parseString(ss.str());
}

}  // namespace fc
