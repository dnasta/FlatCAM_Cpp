/**
 * @file TextGeometry.cpp
 * @brief Texto vectorial SVG/DXF convertido a polígonos con la fuente del sistema.
 *
 * Interpreta las transformaciones de SVG (`translate`, `scale`, `rotate`,
 * `matrix`, `skewX`, `skewY`), compone la matriz del elemento de texto con la
 * pila de grupos y `<svg>` contenedores, y convierte los glifos de
 * `QPainterPath` en geometría GEOS. Los contadores de las letras (A, O, R…)
 * se conservan como agujeros aplicando una XOR sucesiva de subcaminos. Para
 * DXF se reutiliza el mismo enfoque con los datos ya transformados por el
 * parser (posición, altura, rotación y alineación).
 */
#include "TextGeometry.h"

#include <QFile>
#include <QFont>
#include <QFontMetricsF>
#include <QPainterPath>
#include <QPolygonF>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include "flatcam/importers.hpp"

#include <cmath>

using fc::Geom;
using fc::Ring;

namespace {
constexpr double kPi = 3.14159265358979323846;
}

/**
 * @brief Interpreta una lista de transformaciones SVG como un `QTransform`.
 *
 * Extrae cada `nombre(args)` con una expresión regular, convierte los
 * argumentos a números (separados por comas o espacios, ignorando los tramos no
 * numéricos) y compone cada matriz por la izquierda, que es la semántica de
 * SVG: en `translate(10,0) scale(2)` primero se escala y luego se traslada.
 *
 * Se soportan `translate(tx[,ty])`, `scale(sx[,sy])`, `rotate(a[,cx,cy])`
 * (rotación alrededor de un centro si se dan 3 argumentos), `matrix(a,b,c,d,e,f)`
 * y `skewX`/`skewY` en grados (convertidos con `tan`).
 *
 * @param text Valor del atributo `transform`; puede estar vacío.
 * @return Transformación equivalente; la identidad si no hay funciones
 *         reconocibles o los argumentos son insuficientes.
 * @note Las funciones desconocidas se ignoran silenciosamente y el orden de
 *       composición asume la convención de vectores fila de Qt.
 */
QTransform parseSvgTransformQt(const QString& text) {
    QTransform result;
    static const QRegularExpression fn(
        R"(([A-Za-z]+)\s*\(([^)]*)\))");
    auto it = fn.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString name = m.captured(1).toLower();
        const QStringList raw =
            m.captured(2).split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
        QVector<double> v;
        for (const QString& part : raw) {
            bool ok = false;
            const double value = part.toDouble(&ok);
            if (ok) v.push_back(value);
        }
        QTransform t;
        if (name == "translate" && v.size() >= 1)
            t.translate(v[0], v.size() > 1 ? v[1] : 0.0);
        else if (name == "scale" && v.size() >= 1)
            t.scale(v[0], v.size() > 1 ? v[1] : v[0]);
        else if (name == "rotate" && v.size() >= 1) {
            if (v.size() >= 3) {
                t.translate(v[1], v[2]);
                t.rotate(v[0]);
                t.translate(-v[1], -v[2]);
            } else {
                t.rotate(v[0]);
            }
        } else if (name == "matrix" && v.size() >= 6) {
            t = QTransform(v[0], v[1], v[2], v[3], v[4], v[5]);
        } else if (name == "skewx" && v.size() >= 1) {
            t.shear(std::tan(v[0] * kPi / 180.0), 0.0);
        } else if (name == "skewy" && v.size() >= 1) {
            t.shear(0.0, std::tan(v[0] * kPi / 180.0));
        }
        result = t * result;
    }
    return result;
}

/**
 * @brief Extrae el primer número de un atributo SVG.
 *
 * Acepta el formato de la especificación (signo, parte entera y/o decimal,
 * exponente) y descarta unidades y texto adicional, de modo que `"12.5mm"`,
 * `"10"` o `" 3e-2 px"` producen valores utilizables.
 *
 * @param value Cadena del atributo.
 * @param fallback Valor a devolver si la cadena está vacía o no contiene
 *        ningún número.
 * @return Número detectado o `fallback`.
 */
static double svgNumber(const QString& value, double fallback) {
    if (value.isEmpty()) return fallback;
    static const QRegularExpression number(
        R"re([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?)re");
    const auto m = number.match(value);
    return m.hasMatch() ? m.captured(0).toDouble() : fallback;
}

/**
 * @brief Convierte todos los `<text>` de un SVG en polígonos rellenos.
 *
 * Lee el archivo en streaming con `QXmlStreamReader`, manteniendo una pila de
 * transformaciones: al entrar en `<svg>` o `<g>` apila su `transform` compuesta
 * con la del padre y al salir la descarta. Para cada `<text>` toma el contenido
 * (incluidos `tspan`), arma la fuente con familia (`Helvetica` por defecto),
 * peso negrita y un tamaño de referencia de 100 px, y genera el contorno con
 * `QPainterPath::addText`. Luego aplica: desplazamiento por `text-anchor`
 * (start/middle/end, midiendo con `QFontMetricsF`), traslación a `(x, y)`,
 * escala `fontSize/100` con inversión de Y (SVG crece hacia abajo), la
 * transformación propia del elemento y la heredada de los grupos.
 *
 * Los subcaminos resultantes se convierten en polígonos GEOS reparados y se
 * combinan con XOR sucesiva (`A ∪ B − A ∩ B`) para que los contadores queden
 * huecos en lugar de rellenos.
 *
 * @param path Ruta del archivo SVG.
 * @return Un polígono por elemento de texto no vacío; vector vacío si el
 *         archivo no se puede abrir, no es XML válido o no tiene texto.
 * @note El `font-size` se interpreta en unidades de usuario del SVG, sin
 *       resolver `em`, `%` ni hojas de estilo CSS. El tamaño de 100 px sólo es
 *       un pivote de escala para conservar precisión.
 */
std::vector<fc::Geom> svgTextGeometry(const QString& path) {
    std::vector<Geom> out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return out;

    QXmlStreamReader xml(&file);
    QVector<QTransform> stack{QTransform()};
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QString name = xml.name().toString().toLower();
            const QXmlStreamAttributes attrs = xml.attributes();
            if (name == "g" || name == "svg") {
                stack.push_back(parseSvgTransformQt(attrs.value("transform").toString()) *
                                stack.last());
                continue;
            }
            if (name != "text") continue;

            const QString text = xml.readElementText(QXmlStreamReader::IncludeChildElements);
            if (text.trimmed().isEmpty()) continue;

            QString family = attrs.value("font-family").toString();
            if (family.isEmpty()) family = QStringLiteral("Helvetica");
            QFont font(family);
            font.setPixelSize(100);
            font.setBold(attrs.value("font-weight").toString() == "bold");

            QPainterPath pathText;
            pathText.addText(0.0, 0.0, font, text);
            if (pathText.isEmpty()) continue;

            const double fontSize = svgNumber(attrs.value("font-size").toString(), 16.0);
            const double scale = fontSize / 100.0;
            const double x = svgNumber(attrs.value("x").toString(), 0.0);
            const double y = svgNumber(attrs.value("y").toString(), 0.0);
            const QString anchor = attrs.value("text-anchor").toString().toLower();
            double dx = x;
            if (anchor == "middle" || anchor == "end") {
                const double width =
                    QFontMetricsF(font).horizontalAdvance(text) * scale;
                dx -= anchor == "middle" ? width / 2.0 : width;
            }

            QTransform local;
            local.translate(dx, y);
            local.scale(scale, -scale);
            const QTransform total =
                local * parseSvgTransformQt(attrs.value("transform").toString()) * stack.last();
            const QPainterPath mapped = total.map(pathText);

            Geom filled = Geom::emptyPolygon();
            bool any = false;
            for (const QPolygonF& poly : mapped.toSubpathPolygons()) {
                if (poly.size() < 3) continue;
                Ring ring;
                for (const QPointF& p : poly) ring.push_back({p.x(), p.y()});
                const Geom part = Geom::polygon(ring).fixed();
                if (!part.valid() || part.empty()) continue;
                if (!any) {
                    filled = part;
                    any = true;
                } else {
                    filled = filled.unite(part).difference(filled.intersection(part));
                }
            }
            if (any) out.push_back(filled);
        } else if (xml.isEndElement()) {
            const QString name = xml.name().toString().toLower();
            if ((name == "g" || name == "svg") && stack.size() > 1) stack.pop_back();
        }
    }
    return out;
}



namespace {

/**
 * @brief Convierte una ruta Qt en un único polígono GEOS con agujeros.
 *
 * Toma cada subcamino cerrado con al menos 3 puntos, lo convierte en polígono,
 * lo repara (`fixed()`) y lo acumula con XOR sucesiva para que los contornos
 * internos (contadores de letras, huecos de glifos) resten área en vez de
 * sumarla.
 *
 * @param path Ruta con el texto ya transformado.
 * @return Vector con un polígono, o vacío si ningún subcamino es utilizable.
 * @note La XOR es conmutativa respecto del orden de los subcaminos, lo que
 *       evita depender del orden en que Qt los devuelve.
 */
std::vector<Geom> pathToGeometry(const QPainterPath& path) {
    std::vector<Geom> out;
    Geom filled = Geom::emptyPolygon();
    bool any = false;
    for (const QPolygonF& poly : path.toSubpathPolygons()) {
        if (poly.size() < 3) continue;
        Ring ring;
        for (const QPointF& p : poly) ring.push_back({p.x(), p.y()});
        const Geom part = Geom::polygon(ring).fixed();
        if (!part.valid() || part.empty()) continue;
        if (!any) {
            filled = part;
            any = true;
        } else {
            filled = filled.unite(part).difference(filled.intersection(part));
        }
    }
    if (any) out.push_back(filled);
    return out;
}

} // namespace

/**
 * @brief Convierte los TEXT/MTEXT de un DXF en polígonos rellenos.
 *
 * El parser DXF entrega `fc::DxfText` con posición, altura, rotación y
 * alineación ya transformadas (incluidos INSERT y bloques). Para cada texto se
 * separa el contenido por saltos de línea, se mide la altura de mayúscula con
 * `QFontMetricsF` para derivar la escala (`altura / capHeight`, con 72 px como
 * respaldo) y se avanza 1.4 veces la altura entre líneas. La alineación
 * horizontal (0 izquierda, 1 centro, 2 derecha) y la vertical (0 línea base,
 * 1 abajo, 2 medio, 3 arriba) se traducen en desplazamientos previos a la
 * rotación y la traslación final.
 *
 * @param path Ruta del archivo DXF.
 * @return Un polígono por línea de texto no vacía; vector vacío si la lectura
 *         falla o no hay textos.
 * @note Siempre usa Helvetica: el DXF no siempre trae una fuente disponible y
 *       el original de FlatCAM tampoco la respeta. Si la altura es 0 se usa una
 *       escala de 0.01 para no degenerar.
 * @warning Un fallo del parser se traga con `catch (const std::exception&)` y se
 *          informa como "sin textos", sin mensaje al usuario.
 */
std::vector<fc::Geom> dxfTextGeometry(const QString& path) {
    std::vector<fc::Geom> out;
    std::vector<fc::DxfText> texts;
    try {
        texts = fc::importDxfTexts(path.toStdString());
    } catch (const std::exception&) {
        return out;
    }
    for (const fc::DxfText& info : texts) {
        const QStringList lines = QString::fromUtf8(info.text.c_str()).split('\n');
        if (lines.isEmpty()) continue;
        QFont font(QStringLiteral("Helvetica"));
        font.setPixelSize(100);
        const QFontMetricsF metrics(font);
        const double capHeight = metrics.capHeight() > 0.0 ? metrics.capHeight() : 72.0;
        const double scale = info.height > 0.0 ? info.height / capHeight : 0.01;
        const double lineStep = info.height * 1.4;
        for (int line = 0; line < lines.size(); ++line) {
            if (lines[line].isEmpty()) continue;
            QPainterPath pathText;
            pathText.addText(0.0, 0.0, font, lines[line]);
            if (pathText.isEmpty()) continue;

            double dx = 0.0;
            if (info.hAlign == 1)
                dx = -metrics.horizontalAdvance(lines[line]) * scale / 2.0;
            else if (info.hAlign == 2)
                dx = -metrics.horizontalAdvance(lines[line]) * scale;

            double dy = -line * lineStep;
            if (info.vAlign == 1) dy += metrics.descent() * scale;
            else if (info.vAlign == 2)
                dy -= (capHeight - metrics.descent()) * scale / 2.0;
            else if (info.vAlign == 3) dy -= capHeight * scale;

            QTransform glyphScale;
            glyphScale.scale(scale, -scale);
            QTransform offset;
            offset.translate(dx, dy);
            QTransform textRotate;
            textRotate.rotate(info.rotation);
            QTransform anchor;
            anchor.translate(info.x, info.y);
            const QTransform total = glyphScale * offset * textRotate * anchor;
            const std::vector<Geom> parts = pathToGeometry(total.map(pathText));
            out.insert(out.end(), parts.begin(), parts.end());
        }
    }
    return out;
}
