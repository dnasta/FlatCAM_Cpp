/**
 * @file ScriptApi.cpp
 * @brief Implementación de la API JavaScript (`QJSEngine`) de FlatCAM.
 *
 * `fc::ScriptApi` es una fachada con métodos `Q_INVOKABLE` que reutiliza el
 * núcleo CAM (parsers, aislamiento, corte, NCC, pintura, taladrado, utilidades
 * de herramientas y QR) para exponerlos a scripts `.js` y a la consola
 * integrada. Mantiene el último Gerber/Excellon y el trabajo activo, y puede
 * entregar cada capa generada a la GUI mediante un `LayerSink`.
 */

#include "ScriptApi.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>

#include <sstream>
#include <vector>

#include "flatcam/cutout.hpp"
#include "flatcam/isolation.hpp"
#include "flatcam/ncc.hpp"
#include "flatcam/paint.hpp"
#include "flatcam/tools.hpp"
#include "flatcam/qr.hpp"

#include <QJSEngine>
#include <QJSValue>

namespace fc {
namespace {

/**
 * @brief Aplana geometría de líneas en una lista de polilíneas.
 *
 * Convierte `LineString`/`LinearRing` a `std::vector<Pt>` y recurre sobre
 * `MultiLineString` y `GeometryCollection`.
 *
 * @param geom Geometría a recorrer.
 * @param out Lista de polilíneas resultante.
 *
 * @note Se usa en `mill` para transformar los recorridos "follow" del Gerber.
 * @note Ignora polígonos, geometrías vacías e inválidas.
 */
void collectLines(const Geom& geom, std::vector<std::vector<Pt>>& out) {
    if (!geom.valid() || geom.empty()) return;
    const int type = geom.typeId();
    if (type == GEOS_LINESTRING || type == GEOS_LINEARRING) {
        Ring ring = geom.coords();
        if (ring.size() >= 2) out.emplace_back(ring.begin(), ring.end());
        return;
    }
    if (type == GEOS_MULTILINESTRING || type == GEOS_GEOMETRYCOLLECTION) {
        for (int i = 0; i < geom.numGeometries(); ++i) collectLines(geom.geometryN(i), out);
    }
}

}  // namespace

/**
 * @brief Construye la API con estado vacío y sin receptor de capas.
 * @param parent Objeto padre de Qt (opcional).
 */
ScriptApi::ScriptApi(QObject* parent) : QObject(parent) {}

/**
 * @brief Abre un Gerber y lo deja como objeto activo.
 *
 * Parsea el archivo, guarda el resultado en `gerber_` y, si hay `LayerSink`,
 * entrega los sólidos y recorridos para que la GUI cree una capa. El nombre de
 * capa es el nombre de archivo sin ruta.
 *
 * @param path Ruta del Gerber.
 * @return `true` si se pudo parsear; `false` y `lastError()` con el mensaje en
 *         caso de excepción.
 *
 * @note Limpia `error_` en éxito.
 * @warning No reinicia `hasDrillJob_`: tras abrir un Gerber nuevo,
 *          `exportGcode` puede seguir exportando el taladrado del Excellon
 *          anterior.
 */
bool ScriptApi::openGerber(const QString& path) {
    try {
        gerber_ = GerberParser().parseFile(path.toStdString());
        hasGerber_ = true;
        hasDrillJob_ = false;
        hasPathJob_ = false;
        error_.clear();
        if (sink_) sink_(QFileInfo(path).fileName(), "gerber", gerber_.solids, gerber_.follows);
        return true;
    } catch (const std::exception& e) {
        error_ = QString::fromUtf8(e.what());
        return false;
    }
}

/**
 * @brief Abre un Excellon, lo deja activo y prepara el trabajo de taladrado.
 *
 * Además de guardar `excellon_`, construye el `DrillJob` con
 * `drillJobFromExcellon`, de modo que `exportGcode` pueda emitir taladrado
 * inmediatamente. Desactiva el `PathJob` anterior y entrega la geometría
 * (círculos y ranuras) al `LayerSink`.
 *
 * @param path Ruta del archivo Excellon.
 * @return `true` si se pudo parsear; `false` con `lastError()` en caso de error.
 *
 * @note Limpia `error_` en éxito.
 */
bool ScriptApi::openExcellon(const QString& path) {
    try {
        excellon_ = ExcellonParser().parseFile(path.toStdString());
        drills_ = drillJobFromExcellon(excellon_);
        hasExcellon_ = true;
        hasDrillJob_ = true;
        hasPathJob_ = false;
        error_.clear();
        if (sink_) sink_(QFileInfo(path).fileName(), "excellon", excellon_.geometry, {});
        return true;
    } catch (const std::exception& e) {
        error_ = QString::fromUtf8(e.what());
        return false;
    }
}

/**
 * @brief Calcula el aislamiento del Gerber activo y lo publica como capa.
 *
 * Convierte el resultado de `isolateCopper` en un `PathJob` (para una eventual
 * exportación) y entrega los recorridos al `LayerSink` con nombre `isolate`.
 *
 * @param toolDiameter Diámetro de fresa en mm.
 * @param passes Cantidad de pasadas.
 * @param overlapPercent Solape entre pasadas en porcentaje (se divide por 100).
 * @return Cantidad de recorridos, o -1 si no hay Gerber cargado.
 *
 * @note Descarta los recorridos con menos de 2 puntos.
 * @warning No actualiza `error_` en éxito, por lo que `lastError()` puede
 *          conservar un mensaje de una llamada anterior.
 */
int ScriptApi::isolate(double toolDiameter, int passes, double overlapPercent) {
    error_.clear();
    if (!hasGerber_) {
        error_ = "no gerber loaded";
        return -1;
    }
    IsolationOptions opt;
    opt.toolDiameter = toolDiameter;
    opt.passes = passes;
    opt.overlap = overlapPercent / 100.0;
    const IsolationResult result = isolateCopper(gerber_.solid, opt);
    paths_ = PathJob{};
    paths_.bounds = result.bounds;
    paths_.toolDiameter = toolDiameter;
    for (const Geom& g : result.paths) {
        Ring ring = g.coords();
        if (ring.size() >= 2) paths_.paths.emplace_back(ring.begin(), ring.end());
    }
    hasPathJob_ = true;
    hasDrillJob_ = false;
    if (sink_) sink_("isolate", "herramienta", {}, result.paths);
    return static_cast<int>(result.paths.size());
}

/**
 * @brief Calcula el corte de contorno del Gerber activo y lo publica.
 *
 * Usa `cutoutGeometry` con corte externo (valor por defecto de
 * `CutoutOptions`), puentes y ancho de puente opcionales. Guarda el resultado
 * como `PathJob` y entrega los recorridos al `LayerSink` con nombre `cutout`.
 *
 * @param toolDiameter Diámetro de fresa en mm.
 * @param margin Margen extra respecto del radio.
 * @param tabs Cantidad de puentes.
 * @param tabWidth Ancho de cada puente en mm.
 * @return Cantidad de recorridos, o -1 si no hay Gerber.
 *
 * @note La API no expone el corte interno ni el área de recorte.
 */
int ScriptApi::cutout(double toolDiameter, double margin, int tabs, double tabWidth) {
    error_.clear();
    if (!hasGerber_) {
        error_ = "no gerber loaded";
        return -1;
    }
    CutoutOptions opt;
    opt.toolDiameter = toolDiameter;
    opt.margin = margin;
    opt.tabCount = tabs;
    opt.tabWidth = tabWidth;
    const CutoutResult result = cutoutGeometry(gerber_.solid, opt);
    paths_ = PathJob{};
    paths_.bounds = result.bounds;
    paths_.toolDiameter = toolDiameter;
    for (const Geom& g : result.paths) {
        Ring ring = g.coords();
        if (ring.size() >= 2) paths_.paths.emplace_back(ring.begin(), ring.end());
    }
    hasPathJob_ = true;
    hasDrillJob_ = false;
    if (sink_) sink_("cutout", "herramienta", {}, result.paths);
    return static_cast<int>(result.paths.size());
}

/**
 * @brief Despeja el cobre sobrante (NCC) y publica los recorridos.
 *
 * Usa como área de trabajo el bounding box del Gerber expandido 1 mm y la
 * estrategia `Hatch` por defecto con ángulo 0.
 *
 * @param toolDiameter Diámetro de fresa en mm.
 * @param stepOverPercent Paso en porcentaje del diámetro (se divide por 100).
 * @return Cantidad de recorridos, o -1 sin Gerber.
 *
 * @note No se expone `clearance` ni el ángulo: para eso hay que usar la CLI o
 *       la GUI.
 */
int ScriptApi::ncc(double toolDiameter, double stepOverPercent) {
    error_.clear();
    if (!hasGerber_) {
        error_ = "no gerber loaded";
        return -1;
    }
    NccOptions opt;
    opt.toolDiameter = toolDiameter;
    opt.stepOver = stepOverPercent / 100.0;
    const BBox b = gerber_.bounds.expanded(1.0);
    const Geom area = b.valid ? Geom::box(b.xmin, b.ymin, b.xmax, b.ymax) : Geom();
    const NccResult result = clearCopper(gerber_.solid, area, opt);
    paths_ = PathJob{};
    paths_.bounds = result.bounds;
    paths_.toolDiameter = toolDiameter;
    for (const Geom& g : result.paths) {
        Ring ring = g.coords();
        if (ring.size() >= 2) paths_.paths.emplace_back(ring.begin(), ring.end());
    }
    hasPathJob_ = true;
    hasDrillJob_ = false;
    if (sink_) sink_("ncc", "herramienta", {}, result.paths);
    return static_cast<int>(result.paths.size());
}

/**
 * @brief Rellena el cobre activo con contornos paralelos y publica la capa.
 *
 * @param toolDiameter Diámetro de fresa en mm.
 * @param stepOverPercent Paso en porcentaje del diámetro.
 * @return Cantidad de recorridos, o -1 sin Gerber.
 *
 * @note Usa `PaintOptions` por defecto: margen 0, incluye huecos y sentido
 *       normal.
 */
int ScriptApi::paint(double toolDiameter, double stepOverPercent) {
    error_.clear();
    if (!hasGerber_) {
        error_ = "no gerber loaded";
        return -1;
    }
    PaintOptions opt;
    opt.toolDiameter = toolDiameter;
    opt.stepOver = stepOverPercent / 100.0;
    const PaintResult result = paintArea(gerber_.solid, opt);
    paths_ = PathJob{};
    paths_.bounds = result.bounds;
    paths_.toolDiameter = toolDiameter;
    for (const Geom& g : result.paths) {
        Ring ring = g.coords();
        if (ring.size() >= 2) paths_.paths.emplace_back(ring.begin(), ring.end());
    }
    hasPathJob_ = true;
    hasDrillJob_ = false;
    if (sink_) sink_("paint", "herramienta", {}, result.paths);
    return static_cast<int>(result.paths.size());
}

/**
 * @brief Prepara fresado siguiendo los recorridos "follow" del Gerber activo.
 *
 * Aplana `gerber_.follows` con `collectLines`, arma el `PathJob` y entrega las
 * líneas reconstruidas (`Geom::line`) al `LayerSink` con nombre `mill`.
 *
 * @param toolDiameter Diámetro de fresa en mm.
 * @return Cantidad de recorridos, o -1 sin Gerber.
 *
 * @note A diferencia de la CLI, no aplica optimización de orden; el orden es el
 *       del archivo Gerber.
 */
int ScriptApi::mill(double toolDiameter) {
    error_.clear();
    if (!hasGerber_) {
        error_ = "no gerber loaded";
        return -1;
    }
    std::vector<std::vector<Pt>> lines;
    for (const Geom& g : gerber_.follows) collectLines(g, lines);
    paths_ = PathJob{};
    paths_.paths = std::move(lines);
    paths_.bounds = gerber_.bounds;
    paths_.toolDiameter = toolDiameter;
    hasPathJob_ = true;
    hasDrillJob_ = false;
    std::vector<Geom> linesGeom;
    for (const auto& line : paths_.paths) linesGeom.push_back(Geom::line(line));
    if (sink_) sink_("mill", "herramienta", {}, linesGeom);
    return static_cast<int>(paths_.paths.size());
}

/**
 * @brief Extrae los taladros circulares del Gerber activo como Excellon.
 *
 * Reemplaza `excellon_` con `fc::extractDrills`, reactiva `hasExcellon_` y
 * reconstruye el `DrillJob`. Entrega la geometría resultante al `LayerSink` con
 * nombre `extract_drills`.
 *
 * @param maxDiameter Diámetro máximo de flash considerado taladro.
 * @return Cantidad de taladros extraídos, o -1 sin Gerber.
 *
 * @note La extracción se hace siempre en milímetros (`Units::Mm`), sin heredar
 *       las unidades del Gerber original.
 */
int ScriptApi::extractDrills(double maxDiameter) {
    error_.clear();
    if (!hasGerber_) {
        error_ = "no gerber loaded";
        return -1;
    }
    excellon_ = fc::extractDrills(gerber_, maxDiameter, Units::Mm);
    hasExcellon_ = true;
    drills_ = drillJobFromExcellon(excellon_);
    hasDrillJob_ = true;
    if (sink_) sink_("extract_drills", "excellon", excellon_.geometry, {});
    return static_cast<int>(excellon_.drillCount);
}

/**
 * @brief Resta los taladros del Excellon activo al cobre del Gerber activo.
 *
 * Genera el "punch" con `fc::punchGerber` y reemplaza la geometría del Gerber
 * en memoria (`solid`, `solids` y `bounds`) por el resultado, que además se
 * publica en el `LayerSink` con nombre `punch`.
 *
 * @param margin Holgura alrededor de cada taladro.
 * @return Cantidad de geometrías del resultado, o -1 si falta el Gerber, el
 *         Excellon o el resultado queda vacío.
 *
 * @warning Al reemplazar `gerber_.solids` se pierden los recorridos "follow"
 *          originales: `gerber_.follows` no se actualiza y queda desalineado
 *          respecto de la nueva geometría.
 * @warning `punchGerber` devuelve un único `Geom` que suele ser un
 *          `MultiPolygon`; si la implementación devolviera otra colección, el
 *          conteo de `numGeometries()` no equivale a la cantidad de piezas
 *          independientes.
 */
int ScriptApi::punch(double margin) {
    error_.clear();
    if (!hasGerber_ || !hasExcellon_) {
        error_ = "need gerber and excellon";
        return -1;
    }
    const Geom punched = fc::punchGerber(gerber_, excellon_, margin);
    if (!punched.valid() || punched.empty()) {
        error_ = "empty result";
        return -1;
    }
    gerber_.solid = punched;
    gerber_.solids = {punched};
    gerber_.bounds = punched.bounds();
    if (sink_) sink_("punch", "gerber", {punched}, {});
    return punched.numGeometries();
}

/**
 * @brief Paneliza el objeto activo rotándolo y repitiéndolo en una grilla.
 *
 * Rota cada sólido `angle` grados alrededor del centro del bounding box
 * original, calcula la caja del conjunto rotado y luego copia la pieza en
 * `rows × cols` posiciones, separadas por `ancho + gapX` y `alto + gapY`,
 * alineando la primera celda con el origen original. Entrega todas las copias
 * al `LayerSink` con nombre `panel`.
 *
 * @param rows Filas de la grilla (≥ 1).
 * @param cols Columnas de la grilla (≥ 1).
 * @param gapX Separación horizontal extra.
 * @param gapY Separación vertical extra.
 * @param angle Rotación en grados.
 * @return Cantidad de geometrías generadas, o -1 si falta el Gerber, las filas
 *         o columnas no son positivas, o la caja resultante no es válida.
 *
 * @note `rows < 1` o `cols < 1` informan `no gerber loaded`, mensaje engañoso
 *       que no describe el problema real.
 * @note Sólo se panelizan los sólidos; los recorridos "follow" no se replican.
 * @note La función no modifica `gerber_`: el panel existe únicamente en la capa
 *       entregada al `LayerSink`.
 */
int ScriptApi::panelize(int rows, int cols, double gapX, double gapY, double angle) {
    error_.clear();
    if (!hasGerber_ || rows < 1 || cols < 1) {
        error_ = "no gerber loaded";
        return -1;
    }
    const BBox b = gerber_.bounds;
    if (!b.valid) return -1;
    const Pt center{(b.xmin + b.xmax) * 0.5, (b.ymin + b.ymax) * 0.5};
    std::vector<Geom> rotated;
    BBox rb;
    for (const Geom& g : gerber_.solids) {
        const Geom r = g.rotateDeg(angle, center);
        rotated.push_back(r);
        const BBox gb = r.bounds();
        if (!gb.valid) continue;
        if (!rb.valid) rb = gb;
        else {
            rb.xmin = std::min(rb.xmin, gb.xmin);
            rb.ymin = std::min(rb.ymin, gb.ymin);
            rb.xmax = std::max(rb.xmax, gb.xmax);
            rb.ymax = std::max(rb.ymax, gb.ymax);
        }
    }
    if (!rb.valid) return -1;
    const double dx = rb.width() + gapX;
    const double dy = rb.height() + gapY;
    std::vector<Geom> out;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const double ox = b.xmin - rb.xmin + c * dx;
            const double oy = b.ymin - rb.ymin + r * dy;
            for (const Geom& g : rotated) out.push_back(g.translate(ox, oy));
        }
    }
    if (sink_) sink_("panel", "gerber", out, {});
    return static_cast<int>(out.size());
}

/**
 * @brief Genera un código QR como geometría de cobre.
 *
 * Llama a `fc::qrCodeGeometry` con origen (0,0) y nivel de corrección de errores
 * `'M'`, y publica el resultado con nombre `qr`.
 *
 * @param text Texto a codificar.
 * @param module Tamaño del módulo en mm.
 * @param border Borde en módulos.
 * @return Cantidad de geometrías generadas, o -1 si la cadena queda vacía.
 *
 * @note El nivel de corrección y el origen están fijos: para otras variantes hay
 *       que usar la herramienta de la GUI o la API C++ directamente.
 */
int ScriptApi::qrCode(const QString& text, double module, int border) {
    error_.clear();
    const std::vector<Geom> geometry =
        fc::qrCodeGeometry(text.toStdString(), module, border, 0.0, 0.0, 'M');
    if (geometry.empty()) {
        error_ = "empty qr";
        return -1;
    }
    if (sink_) sink_("qr", "gerber", geometry, {});
    return static_cast<int>(geometry.size());
}

/**
 * @brief Añade marcas de esquina en L a partir de los límites del Gerber.
 *
 * @param length Longitud de cada brazo en mm.
 * @param margin Separación al borde en mm.
 * @return Cantidad de marcas generadas, o -1 si falta el Gerber o sus límites
 *         no son válidos.
 *
 * @note Las marcas se piden con `outside = false`, es decir, hacia dentro de la
 *       caja del objeto; la API no permite elegir el lado.
 */
int ScriptApi::cornerMarks(double length, double margin) {
    error_.clear();
    if (!hasGerber_ || !gerber_.bounds.valid) {
        error_ = "no gerber loaded";
        return -1;
    }
    const std::vector<Geom> marks = fc::cornerMarks(gerber_.bounds, length, margin, false);
    if (sink_) sink_("corners", "herramienta", {}, marks);
    return static_cast<int>(marks.size());
}

/**
 * @brief Calcula el diámetro de fresa más grande que todavía aísla el cobre.
 *
 * Envuelve `optimalIsolationDiameter`, que busca la separación mínima entre
 * features de cobre y deduce el diámetro máximo utilizable.
 *
 * @param maxDiameter Tope superior de búsqueda en mm.
 * @return Diámetro óptimo; si no se encontró un límite válido devuelve
 *         `maxDiameter`.
 *
 * @note El valor de reserva no distingue entre "no hay separación limitante" y
 *       "el cálculo no encontró nada"; el llamador no puede saberlo desde JS.
 * @note No modifica `error_` en el camino de fallo.
 */
double ScriptApi::optimalToolDiameter(double maxDiameter) {
    error_.clear();
    if (!hasGerber_) {
        error_ = "no gerber loaded";
        return -1.0;
    }
    const OptimalTool opt = fc::optimalIsolationDiameter(gerber_.solid, maxDiameter);
    return opt.valid ? opt.diameter : maxDiameter;
}

/**
 * @brief Comprueba la separación entre el cobre del Gerber y los taladros.
 *
 * Calcula la distancia mínima entre `gerber_.solid` y `excellon_.solid` y la
 * compara con el valor pedido.
 *
 * @param clearance Separación mínima exigida en mm.
 * @return 1 si hay violación (distancia menor que `clearance`), 0 si cumple, y
 *         -1 si falta el Gerber o el Excellon.
 *
 * @note Sólo evalúa cobre contra taladros, no anillo anular ni ancho de traza.
 * @note Un 0 no actualiza `error_`: un error previo puede quedar visible en
 *       `lastError()` aunque la comprobación haya pasado.
 */
int ScriptApi::rulesCheck(double clearance) {
    error_.clear();
    if (!hasGerber_ || !hasExcellon_) {
        error_ = "need gerber and excellon";
        return -1;
    }
    const double d = gerber_.solid.distance(excellon_.solid);
    return d < clearance ? 1 : 0;
}

/**
 * @brief Exporta el trabajo activo a un archivo G-code.
 *
 * Si hay `DrillJob` genera taladrado; si no, los recorridos de geometría. Usa
 * `GCodeOptions` por defecto (milímetros, preprocesador `Default`).
 *
 * @param path Archivo de destino.
 * @return `true` si se escribió; `false` con `lastError()` si no hay trabajo,
 *         no se puede abrir el archivo o el generador lanza una excepción.
 *
 * @note La escritura usa `QIODevice::Text`, de modo que en Windows el texto
 *       puede convertirse a CRLF (los firmwares suelen aceptarlo).
 * @warning No hay parámetros de máquina expuestos: avances, Z, husillo y
 *          postprocesador no se pueden configurar desde el script.
 * @warning `QFile::write(code.c_str())` depende del terminador nulo y no pasa la
 *          longitud; es correcto para G-code sin bytes nulos, pero no es la
 *          forma recomendada de escribir datos binarios.
 */
bool ScriptApi::exportGcode(const QString& path) {
    error_.clear();
    try {
        GCodeOptions opt;
        std::string code;
        if (hasDrillJob_) code = drillingGCode(drills_, opt);
        else if (hasPathJob_) code = pathsGCode(paths_, opt);
        else {
            error_ = "nothing to export";
            return false;
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            error_ = "cannot write " + path;
            return false;
        }
        file.write(code.c_str());
        return true;
    } catch (const std::exception& e) {
        error_ = QString::fromUtf8(e.what());
        return false;
    }
}

/**
 * @brief Devuelve un resumen de una línea con el estado de la API.
 *
 * Incluye área del Gerber, cantidad de taladros, cantidad de recorridos y
 * cantidad de herramientas, cada uno sólo si el objeto correspondiente está
 * cargado.
 *
 * @return Texto terminado en espacio (sin salto de línea).
 *
 * @note Está pensado para depuración y consola, no para consumo programático
 *       estable.
 */
QString ScriptApi::info() const {
    std::ostringstream os;
    if (hasGerber_) os << "gerber area=" << gerber_.solid.area() << " ";
    if (hasExcellon_) os << "drills=" << excellon_.drillCount << " ";
    if (hasPathJob_) os << "paths=" << paths_.paths.size() << " ";
    if (hasDrillJob_) os << "tools=" << drills_.tools.size() << " ";
    return QString::fromStdString(os.str());
}

/**
 * @brief Escribe `text` en el log de Qt (`qInfo`) sin comillas.
 *
 * Es la implementación del `print()` que se instala en el motor JavaScript.
 *
 * @param text Mensaje a registrar.
 */
void ScriptApi::log(const QString& text) { qInfo().noquote() << text; }

/**
 * @brief Instala la API en un motor JavaScript como objeto global `flatcam`.
 *
 * Registra `api` con `QJSEngine::newQObject` y define la función global
 * `print(s)`, que delega en `flatcam.log(String(s))`.
 *
 * @param engine Motor JavaScript destino.
 * @param api Instancia de la API; el llamador conserva su propiedad.
 *
 * @note Se fija `CppOwnership` antes de exponer el objeto: la instancia vive en
 *       la pila del llamador y el motor no debe destruirla.
 * @note La función `print` se evalúa una sola vez; si el script la redefine,
 *       prevalece la del script.
 */
void installScriptApi(QJSEngine& engine, ScriptApi* api) {
    QJSEngine::setObjectOwnership(api, QJSEngine::CppOwnership);
    engine.globalObject().setProperty("flatcam", engine.newQObject(api));
    engine.evaluate("function print(s) { flatcam.log(String(s)); }");
}

}  // namespace fc
