/**
 * @file tools.cpp
 * @brief Utilidades CAM transversales: herramienta óptima, extracción de
 *        taladros, punch, edición de aperturas y marcas de esquina.
 *
 * Agrupa operaciones que trabajan sobre resultados ya parseados
 * (GerberResult, ExcellonResult) o sobre texto Gerber crudo, sin depender de
 * la GUI. La edición de aperturas es textual, por expresiones regulares sobre
 * el archivo fuente, porque el modelo no conserva el texto de cada comando.
 */
#include "flatcam/tools.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <set>
#include <tuple>

namespace fc {

namespace {

/**
 * @brief Une un vector de geometrías ignorando nulos y vacíos.
 * @return Unión; Geom nulo si no queda ninguna parte utilizable.
 */
Geom unionAll(const std::vector<Geom>& parts) {
    std::vector<Geom> valid;
    valid.reserve(parts.size());
    for (const Geom& g : parts) {
        if (g.valid() && !g.empty()) valid.push_back(g);
    }
    if (valid.empty()) return Geom();
    if (valid.size() == 1) return valid.front();
    return Geom::unionOf(valid);
}

}  // namespace

/**
 * @brief Calcula el diámetro de fresa más grande que todavía aísla el cobre.
 *
 * Modela cada isla de cobre y cada agujero como features independientes y
 * busca la menor distancia entre features que no pertenezcan al mismo dueño
 * (una isla y su propio agujero se tocan por definición y no limitan la fresa).
 * El diámetro devuelto es el 98 % de esa separación, para que la fresa no roce
 * ambas paredes.
 * @param copper Geometría del cobre (polígonos con huecos).
 * @param maxDiameter Tope superior y valor de retorno si no hay pares válidos.
 * @return Estructura con diámetro, separación, los dos puntos de la cota y la
 *         bandera valid.
 * @note Es una búsqueda cuadrática sobre features; aceptable para una capa
 *       típica, pero costosa en cobres con cientos de islas.
 */
OptimalTool optimalIsolationDiameter(const Geom& copper, double maxDiameter) {
    OptimalTool result;
    result.diameter = maxDiameter;
    if (!copper.valid() || copper.empty()) return result;

    // Each outer polygon is one copper island. Holes inside an island are
    // copper-free, so the web between two holes also limits the tool.
    struct Feature {
        Geom geom;
        int owner = -1;
        bool hole = false;
    };
    std::vector<Feature> features;
    int owner = 0;
    for (const PolygonContours& pc : copper.polygons()) {
        if (pc.outer.size() < 3) continue;
        features.push_back({Geom::polygon(pc.outer, pc.holes), owner, false});
        for (const Ring& hole : pc.holes) {
            if (hole.size() >= 3) features.push_back({Geom::polygon(hole), owner, true});
        }
        ++owner;
    }

    double best = maxDiameter;
    bool found = false;
    for (std::size_t i = 0; i < features.size(); ++i) {
        for (std::size_t j = i + 1; j < features.size(); ++j) {
            const Feature& a = features[i];
            const Feature& b = features[j];
            // A hole touches its own island boundary; that distance is zero
            // and says nothing about the tool width.
            if (a.hole && b.hole && a.owner == b.owner) continue;
            if (a.hole != b.hole && a.owner == b.owner) continue;
            if (!a.hole && !b.hole && a.owner == b.owner) continue;
            const double d = a.geom.distance(b.geom);
            if (d > 1e-7 && (!found || d < best)) {
                best = d;
                found = true;
                const std::pair<Pt, Pt> pts = a.geom.nearestPoints(b.geom);
                result.pointA = pts.first;
                result.pointB = pts.second;
            }
        }
    }

    result.valid = found;
    result.gap = found ? best : maxDiameter;
    // A tiny safety factor: a tool exactly as wide as the gap rubs both walls.
    result.diameter = found ? best * 0.98 : maxDiameter;
    return result;
}

/**
 * @brief Lista las separaciones de cobre que limitan el diámetro de fresa.
 *
 * Hace el mismo relevamiento de features que optimalIsolationDiameter pero
 * conserva todos los pares con separación menor que maxDiameter, los ordena de
 * menor a mayor y descarta los que caen a menos de minSeparation de otro par ya
 * elegido, para no dibujar cotas superpuestas.
 * @param copper Cobre a analizar.
 * @param maxDiameter Las separaciones mayores o iguales se descartan.
 * @param limit Cantidad máxima de pares devueltos.
 * @param minSeparation Distancia mínima entre cotas; 0 desactiva el filtro.
 * @return Pares ordenados por separación creciente.
 */
std::vector<OptimalTool> optimalIsolationGaps(const Geom& copper, double maxDiameter,
                                             std::size_t limit, double minSeparation) {
    std::vector<OptimalTool> gaps;
    if (!copper.valid() || copper.empty()) return gaps;
    struct Feature {
        Geom geom;
        int owner = -1;
        bool hole = false;
    };
    std::vector<Feature> features;
    int owner = 0;
    for (const PolygonContours& pc : copper.polygons()) {
        if (pc.outer.size() < 3) continue;
        features.push_back({Geom::polygon(pc.outer, pc.holes), owner, false});
        for (const Ring& hole : pc.holes) {
            if (hole.size() >= 3) features.push_back({Geom::polygon(hole), owner, true});
        }
        ++owner;
    }
    for (std::size_t i = 0; i < features.size(); ++i) {
        for (std::size_t j = i + 1; j < features.size(); ++j) {
            const Feature& a = features[i];
            const Feature& b = features[j];
            if (a.hole && b.hole && a.owner == b.owner) continue;
            if (a.hole != b.hole && a.owner == b.owner) continue;
            if (!a.hole && !b.hole && a.owner == b.owner) continue;
            const double d = a.geom.distance(b.geom);
            if (d <= 1e-7 || d >= maxDiameter) continue;
            OptimalTool gap;
            gap.gap = d;
            gap.diameter = d * 0.98;
            gap.valid = true;
            const std::pair<Pt, Pt> pts = a.geom.nearestPoints(b.geom);
            gap.pointA = pts.first;
            gap.pointB = pts.second;
            gaps.push_back(gap);
        }
    }
    std::sort(gaps.begin(), gaps.end(),
              [](const OptimalTool& a, const OptimalTool& b) { return a.gap < b.gap; });
    if (minSeparation > 0.0) {
        std::vector<OptimalTool> filtered;
        for (const OptimalTool& gap : gaps) {
            bool near = false;
            for (const OptimalTool& kept : filtered) {
                if (std::hypot(gap.pointA.x - kept.pointA.x, gap.pointA.y - kept.pointA.y) <
                    minSeparation) {
                    near = true;
                    break;
                }
            }
            if (!near) filtered.push_back(gap);
        }
        gaps = std::move(filtered);
    }
    if (gaps.size() > limit) gaps.resize(limit);
    return gaps;
}

/**
 * @brief Distancia mínima entre dos conjuntos de geometrías (dos capas).
 *
 * Une cada conjunto por separado para que las partes de una misma capa no
 * cuenten como obstáculo y mide entre los dos resultados.
 * @param a Primera capa.
 * @param b Segunda capa.
 * @param[out] nearestA Punto más cercano en la capa A (puede ser nullptr).
 * @param[out] nearestB Punto más cercano en la capa B (puede ser nullptr).
 * @return Distancia en mm, o -1 si alguna capa queda vacía o inválida.
 */
double layersDistance(const std::vector<Geom>& a, const std::vector<Geom>& b, Pt* nearestA,
                      Pt* nearestB) {
    const Geom ua = unionAll(a);
    const Geom ub = unionAll(b);
    if (!ua.valid() || !ub.valid() || ua.empty() || ub.empty()) return -1.0;
    const std::pair<Pt, Pt> pts = ua.nearestPoints(ub);
    if (nearestA) *nearestA = pts.first;
    if (nearestB) *nearestB = pts.second;
    return ua.distance(ub);
}

/**
 * @brief Deduce taladros a partir de los flashes circulares de un Gerber.
 *
 * Recorre las aperturas circulares, descarta las que superan maxDiameter y
 * examina su geometría: sólo acepta piezas aproximadamente cuadradas en su
 * caja (un flash redondo) y de tamaño coherente con el diámetro, con lo que
 * quedan afuera los trazos, que son cápsulas alargadas. Deduplica por
 * diámetro y centro redondeados a micrón y agrupa los taladros en herramientas
 * por diámetro.
 * @param gerber Resultado del parser Gerber con aperturas y geometría.
 * @param maxDiameter Diámetro máximo a considerar; los pads grandes no son
 *        taladros.
 * @param appUnits Unidades con las que se informa el Excellon resultante.
 * @param stepsPerCircle Segmentos por círculo de la geometría generada.
 * @return ExcellonResult con herramientas, taladros, geometría y límites.
 * @note Es una heurística geométrica: los pads cuadrados o rectangulares se
 *       descartan por la comprobación de caja, no por leer atributos.
 */
ExcellonResult extractDrills(const GerberResult& gerber, double maxDiameter, Units appUnits,
                             int stepsPerCircle) {
    ExcellonResult result;
    result.fileUnits = appUnits;
    result.unitsKnown = true;

    struct Hit {
        double diameter = 0.0;
        Pt center;
    };
    std::vector<Hit> hits;
    std::set<std::tuple<long long, long long, long long>> seen;

    for (const auto& [code, aperture] : gerber.apertures) {
        if (aperture.kind != GerberAperture::Kind::Circle) continue;
        double diameter = aperture.size > 0.0 ? aperture.size : aperture.diameter;
        if (diameter <= 1e-6) continue;
        if (diameter > maxDiameter + 1e-9) continue;

        for (const Geom& g : aperture.geometry) {
            if (!g.valid() || g.empty()) continue;
            const BBox b = g.bounds();
            if (!b.valid) continue;
            const double w = b.width();
            const double h = b.height();
            if (w <= 1e-6 || h <= 1e-6) continue;
            // Flashes are round/square; strokes are long capsules or traces.
            if (std::fabs(w - h) > std::max(0.03, diameter * 0.25)) continue;
            if (w > diameter * 1.6 || h > diameter * 1.6) continue;

            const long long kd = std::llround(diameter * 1000.0);
            const long long kx = std::llround(((b.xmin + b.xmax) * 0.5) * 1000.0);
            const long long ky = std::llround(((b.ymin + b.ymax) * 0.5) * 1000.0);
            if (!seen.insert({kd, kx, ky}).second) continue;
            hits.push_back({diameter, {(b.xmin + b.xmax) * 0.5, (b.ymin + b.ymax) * 0.5}});
        }
    }

    std::map<long long, int> toolForDiameter;
    int nextTool = 1;
    for (const Hit& hit : hits) {
        const long long key = std::llround(hit.diameter * 1000.0);
        int number = 0;
        const auto it = toolForDiameter.find(key);
        if (it == toolForDiameter.end()) {
            number = nextTool++;
            toolForDiameter[key] = number;
            ExcellonTool tool;
            tool.number = number;
            tool.diameter = hit.diameter;
            tool.diameterKnown = true;
            result.tools[number] = tool;
        } else {
            number = it->second;
        }
        ExcellonTool& tool = result.tools[number];
        tool.drills.push_back(hit.center);
        const Geom circle = Geom::circle(hit.center, hit.diameter / 2.0, stepsPerCircle);
        tool.geometry.push_back(circle);
        result.geometry.push_back(circle);
        ++result.drillCount;
    }

    if (!result.geometry.empty()) result.solid = Geom::unionOf(result.geometry);
    for (const Geom& g : result.geometry) {
        const BBox b = g.bounds();
        if (!b.valid) continue;
        if (!result.bounds.valid) {
            result.bounds = b;
        } else {
            result.bounds.xmin = std::min(result.bounds.xmin, b.xmin);
            result.bounds.ymin = std::min(result.bounds.ymin, b.ymin);
            result.bounds.xmax = std::max(result.bounds.xmax, b.xmax);
            result.bounds.ymax = std::max(result.bounds.ymax, b.ymax);
        }
    }
    if (hits.empty()) result.warnings.push_back("No circular flashes found below the diameter limit.");
    return result;
}

/**
 * @brief Resta los taladros al cobre (punch) para obtener el negativo.
 *
 * Aplica primero un buffer de holgura a los taladros y luego los descuenta del
 * sólido del Gerber.
 * @param gerber Gerber con el cobre; se usa gerber.solid.
 * @param drills Excellon con los taladros a restar.
 * @param margin Holgura en mm; si es <= 0 se resta el taladro exacto.
 * @return Cobre perforado, o el cobre original si no hay taladros.
 */
Geom punchGerber(const GerberResult& gerber, const ExcellonResult& drills, double margin) {
    if (!gerber.solid.valid() || gerber.solid.empty()) return Geom();
    if (!drills.solid.valid() || drills.solid.empty()) return gerber.solid;
    const Geom holes = margin > 0.0 ? drills.solid.buffer(margin, 16) : drills.solid;
    return gerber.solid.difference(holes);
}

/**
 * @brief Reescribe el diámetro de una apertura circular en el texto Gerber.
 *
 * Busca %ADD<code>C,<diámetro>[X<hueco>]*% y reemplaza el diámetro, respetando
 * el segundo valor X (hueco) si estaba presente. No toca otras aperturas ni
 * los comandos de dibujo.
 * @param gerber Texto Gerber completo.
 * @param code Código de apertura, por ejemplo "10".
 * @param diameter Nuevo diámetro en las unidades del archivo.
 * @param decimals Decimales con que se escribe el valor.
 * @param[out] changed Cantidad de reemplazos (puede ser nullptr).
 * @return Texto modificado, o el original si no hubo coincidencias.
 */
std::string editCircleAperture(std::string gerber, const std::string& code, double diameter,
                               int decimals, int* changed) {
    if (changed) *changed = 0;
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, diameter);
    const std::regex pattern("%ADD" + code + "C,([0-9.]+)(X[0-9.]+)?\\*%");
    const std::string replacement = "%ADD" + code + "C," + buffer + "$2*%";
    int count = 0;
    std::string out;
    out.reserve(gerber.size());
    auto begin = std::sregex_iterator(gerber.begin(), gerber.end(), pattern);
    auto end = std::sregex_iterator();
    std::size_t last = 0;
    for (auto it = begin; it != end; ++it) {
        const std::smatch& match = *it;
        out.append(gerber, last, static_cast<std::size_t>(match.position()) - last);
        out += "%ADD" + code + "C," + buffer + (match[2].matched ? match[2].str() : "") + "*%";
        last = static_cast<std::size_t>(match.position() + match.length());
        ++count;
    }
    out.append(gerber, last, std::string::npos);
    if (changed) *changed = count;
    return count > 0 ? out : gerber;
}

/**
 * @brief Cambia el tamaño de una apertura circular, rectangular u obround.
 *
 * Intenta primero la variante circular; si no hay coincidencias prueba R y O,
 * escalando ambos lados para conservar la relación de aspecto con el lado
 * mayor como referencia, y por último P (polígono), donde cambia el diámetro y
 * conserva vértices y rotación.
 * @param gerber Texto Gerber completo.
 * @param code Código de apertura.
 * @param size Tamaño objetivo en las unidades del archivo.
 * @param decimals Decimales de salida.
 * @param[out] changed Cantidad de aperturas modificadas.
 * @return Texto modificado, o el original si no hubo coincidencias.
 */
std::string editApertureSize(std::string gerber, const std::string& code, double size,
                             int decimals, int* changed) {
    if (changed) *changed = 0;
    int count = 0;
    std::string out = editCircleAperture(gerber, code, size, decimals, &count);
    if (count > 0) {
        if (changed) *changed = count;
        return out;
    }

    auto replaceMatches = [&](const std::regex& pattern,
                              const std::function<std::string(const std::smatch&)>& make) {
        std::string result;
        result.reserve(gerber.size());
        std::size_t last = 0;
        int local = 0;
        for (auto it = std::sregex_iterator(gerber.begin(), gerber.end(), pattern);
             it != std::sregex_iterator(); ++it) {
            const std::smatch& match = *it;
            result.append(gerber, last, static_cast<std::size_t>(match.position()) - last);
            result += make(match);
            last = static_cast<std::size_t>(match.position() + match.length());
            ++local;
        }
        result.append(gerber, last, std::string::npos);
        if (local > 0) {
            gerber = std::move(result);
            count += local;
        }
    };

    char buffer[64];
    const std::regex rect("%ADD" + code + "([RO]),([0-9.]+)X([0-9.]+)\\*%");
    replaceMatches(rect, [&](const std::smatch& match) {
        const double w = std::stod(match[2].str());
        const double h = std::stod(match[3].str());
        const double old = std::max(w, h);
        const double factor = old > 0.0 ? size / old : 1.0;
        std::snprintf(buffer, sizeof(buffer), "%%ADD%s%s,%.*fX%.*f*%%", code.c_str(),
                      match[1].str().c_str(), decimals, w * factor, decimals, h * factor);
        return std::string(buffer);
    });
    const std::regex polygon("%ADD" + code + "P,([0-9.]+)X(\\d+)(X[0-9.]+)?\\*%");
    replaceMatches(polygon, [&](const std::smatch& match) {
        std::snprintf(buffer, sizeof(buffer), "%%ADD%sP,%.*fX%s%s*%%", code.c_str(), decimals,
                      size, match[2].str().c_str(),
                      match[3].matched ? match[3].str().c_str() : "");
        return std::string(buffer);
    });

    if (changed) *changed = count;
    return count > 0 ? gerber : out;
}

/**
 * @brief Genera las marcas en L de las cuatro esquinas de una caja.
 *
 * Cada esquina aporta dos segmentos de largo @p length. Con outside en false
 * las marcas quedan hacia adentro de la caja, desplazadas margin; con outside
 * en true se dibujan hacia afuera e invertidas, para usarlas como referencia de
 * alineación fuera del contorno.
 * @param b Caja de referencia; si no es válida devuelve vacío.
 * @param length Largo de cada brazo en mm.
 * @param margin Separación al borde en mm.
 * @param outside true para marcas exteriores.
 * @return Hasta 8 LineString, dos por esquina.
 */
std::vector<Geom> cornerMarks(const BBox& b, double length, double margin, bool outside) {
    std::vector<Geom> out;
    if (!b.valid || length <= 0.0) return out;
    const double k = outside ? -margin : margin;
    const double x0 = b.xmin + k;
    const double y0 = b.ymin + k;
    const double x1 = b.xmax - k;
    const double y1 = b.ymax - k;
    const double dx = outside ? -length : length;
    const double dy = outside ? -length : length;

    auto add = [&out](Pt a, Pt c) { out.push_back(Geom::line({a, c})); };
    // Bottom-left
    add({x0, y0}, {x0 + dx, y0});
    add({x0, y0}, {x0, y0 + dy});
    // Bottom-right
    add({x1, y0}, {x1 - dx, y0});
    add({x1, y0}, {x1, y0 + dy});
    // Top-left
    add({x0, y1}, {x0 + dx, y1});
    add({x0, y1}, {x0, y1 - dy});
    // Top-right
    add({x1, y1}, {x1 - dx, y1});
    add({x1, y1}, {x1, y1 - dy});
    return out;
}

}  // namespace fc
