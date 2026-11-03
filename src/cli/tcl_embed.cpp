/**
 * @file tcl_embed.cpp
 * @brief Shell Tcl real embebido con libtcl.
 *
 * Se compila cuando `FC_HAVE_TCL` está definido. Crea un `Tcl_Interp`,
 * inicializa la biblioteca estándar (`set`, `puts`, `if`, `proc`, `expr`, …) y
 * registra los comandos CAM de FlatCAM como comandos Tcl nativos: `open_gerber`,
 * `open_excellon`, `isolate`, `cutout`, `ncc`, `paint`, `mill`, `export_gcode`,
 * `export_svg`, `export_dxf`, `thieving` e `info`, sobre un `ShellState` común.
 */

#ifdef FC_HAVE_TCL

#include <tcl.h>

#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "flatcam/cutout.hpp"
#include "flatcam/excellon.hpp"
#include "flatcam/gcode.hpp"
#include "flatcam/gerber.hpp"
#include "flatcam/isolation.hpp"
#include "flatcam/ncc.hpp"
#include "flatcam/paint.hpp"
#include "flatcam/dxf.hpp"
#include "flatcam/svg.hpp"
#include "flatcam/thieving.hpp"

namespace fc {
namespace {

/**
 * @brief Estado compartido por todos los comandos Tcl registrados.
 *
 * Mantiene el último Gerber y Excellon abiertos, sus banderas de validez y el
 * trabajo activo (`PathJob` o `DrillJob`) que exportará `export_gcode`. Los dos
 * trabajos son mutuamente excluyentes.
 */
struct ShellState {
    GerberResult gerber;
    bool hasGerber = false;
    ExcellonResult excellon;
    bool hasExcellon = false;
    PathJob paths;
    DrillJob drills;
    bool hasPathJob = false;
    bool hasDrillJob = false;
};

/**
 * @brief Devuelve un argumento Tcl como cadena, o `nullptr` si falta.
 * @param objv Vector de objetos del comando.
 * @param index Posición a leer.
 * @param count Cantidad de argumentos (`objc`).
 * @return Puntero interno al texto (válido mientras viva el objeto Tcl).
 *
 * @note Es código muerto en la versión actual: ningún comando lo usa y puede
 *       generar un aviso de función no utilizada según el compilador.
 */
const char* arg(Tcl_Interp*, Tcl_Obj* const objv[], int index, int count) {
    return index < count ? Tcl_GetString(objv[index]) : nullptr;
}

/**
 * @brief Lee un argumento numérico Tcl con valor de reserva.
 *
 * Usa `Tcl_GetDoubleFromObj`, que puede dejar un mensaje de error en el
 * intérprete si el objeto no es numérico; en ese caso se devuelve el valor de
 * reserva y el mensaje residual se sobrescribe al fijar el resultado del
 * comando.
 *
 * @param interp Intérprete Tcl.
 * @param objv Vector de objetos del comando.
 * @param index Posición a leer.
 * @param count Cantidad de argumentos.
 * @param fallback Valor a devolver si falta el argumento o no es numérico.
 * @return Número convertido o `fallback`.
 */
double numberArg(Tcl_Interp* interp, Tcl_Obj* const objv[], int index, int count, double fallback) {
    if (index >= count) return fallback;
    double value = fallback;
    if (Tcl_GetDoubleFromObj(interp, objv[index], &value) != TCL_OK) return fallback;
    return value;
}

/**
 * @brief Reemplaza el trabajo de recorridos y desactiva el de taladrado.
 * @param state Estado a modificar.
 * @param paths Recorridos producidos por la herramienta.
 * @param bounds Límites del trabajo.
 * @param tool Diámetro de herramienta informativo.
 *
 * @note Los recorridos con menos de 2 puntos se descartan.
 */
void setPaths(ShellState& state, const std::vector<Geom>& paths, const BBox& bounds, double tool) {
    state.paths = PathJob{};
    state.paths.bounds = bounds;
    state.paths.toolDiameter = tool;
    for (const Geom& g : paths) {
        Ring ring = g.coords();
        if (ring.size() >= 2) state.paths.paths.emplace_back(ring.begin(), ring.end());
    }
    state.hasPathJob = true;
    state.hasDrillJob = false;
}

/**
 * @brief Implementa `open_gerber <archivo>`.
 *
 * Parsea el Gerber con `GerberParser` y lo deja como objeto activo. Devuelve la
 * ruta como resultado Tcl.
 *
 * @param data Puntero al `ShellState`.
 * @param interp Intérprete Tcl.
 * @param objc Cantidad de argumentos.
 * @param objv Argumentos (`open_gerber`, ruta).
 * @return `TCL_OK` o `TCL_ERROR` con el mensaje de la excepción.
 *
 * @note No reinicia el trabajo de taladrado previo; ver la advertencia de
 *       `runTclScript`.
 */
int cmdOpenGerber(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (objc < 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("usage: open_gerber file", -1));
        return TCL_ERROR;
    }
    try {
        state->gerber = GerberParser().parseFile(Tcl_GetString(objv[1]));
        state->hasGerber = true;
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Tcl_GetString(objv[1]), -1));
        return TCL_OK;
    } catch (const std::exception& e) {
        Tcl_SetResult(interp, const_cast<char*>(e.what()), TCL_VOLATILE);
        return TCL_ERROR;
    }
}

/**
 * @brief Implementa `open_excellon <archivo>`.
 *
 * Parsea el Excellon, construye el `DrillJob` y lo deja como trabajo activo,
 * desactivando cualquier `PathJob` anterior.
 *
 * @param data Puntero al `ShellState`.
 * @param interp Intérprete Tcl.
 * @param objc Cantidad de argumentos.
 * @param objv Argumentos (`open_excellon`, ruta).
 * @return `TCL_OK` o `TCL_ERROR` con el mensaje de la excepción.
 */
int cmdOpenExcellon(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (objc < 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("usage: open_excellon file", -1));
        return TCL_ERROR;
    }
    try {
        state->excellon = ExcellonParser().parseFile(Tcl_GetString(objv[1]));
        state->drills = drillJobFromExcellon(state->excellon);
        state->hasExcellon = true;
        state->hasDrillJob = true;
        state->hasPathJob = false;
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Tcl_GetString(objv[1]), -1));
        return TCL_OK;
    } catch (const std::exception& e) {
        Tcl_SetResult(interp, const_cast<char*>(e.what()), TCL_VOLATILE);
        return TCL_ERROR;
    }
}

/**
 * @brief Implementa `isolate <Ø> <pasadas> <solape%>`.
 *
 * Genera el aislamiento del Gerber activo y deja el resultado como `PathJob`.
 *
 * @param data Puntero al `ShellState`.
 * @param interp Intérprete Tcl.
 * @param objc Cantidad de argumentos.
 * @param objv Argumentos posicionales opcionales (Ø 0,2; pasadas 1; solape 0).
 * @return `TCL_OK` con la cantidad de recorridos, o `TCL_ERROR` si no hay
 *         Gerber cargado.
 *
 * @warning No captura excepciones de GEOS: si el cálculo falla, la excepción
 *          atraviesa el callback de Tcl (comportamiento indefinido) en lugar de
 *          convertirse en `TCL_ERROR`.
 */
int cmdIsolate(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (!state->hasGerber) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("no gerber loaded", -1));
        return TCL_ERROR;
    }
    IsolationOptions opt;
    opt.toolDiameter = numberArg(interp, objv, 1, objc, 0.2);
    opt.passes = static_cast<int>(numberArg(interp, objv, 2, objc, 1));
    opt.overlap = numberArg(interp, objv, 3, objc, 0.0) / 100.0;
    const IsolationResult result = isolateCopper(state->gerber.solid, opt);
    setPaths(*state, result.paths, result.bounds, opt.toolDiameter);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(static_cast<int>(result.paths.size())));
    return TCL_OK;
}

/**
 * @brief Implementa `cutout <Ø> <margen> <puentes> <ancho>`.
 *
 * Calcula el corte de contorno del Gerber activo y lo deja como `PathJob`.
 *
 * @param data Puntero al `ShellState`.
 * @param interp Intérprete Tcl.
 * @param objc Cantidad de argumentos.
 * @param objv Argumentos posicionales (Ø 1,0; margen 0; puentes 0; ancho 0).
 * @return `TCL_OK` con la cantidad de recorridos, o `TCL_ERROR` sin Gerber.
 *
 * @note El corte es siempre externo: este comando no expone `external` ni
 *       `invert`.
 * @warning Igual que `cmdIsolate`, no captura excepciones de GEOS.
 */
int cmdCutout(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (!state->hasGerber) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("no gerber loaded", -1));
        return TCL_ERROR;
    }
    CutoutOptions opt;
    opt.toolDiameter = numberArg(interp, objv, 1, objc, 1.0);
    opt.margin = numberArg(interp, objv, 2, objc, 0.0);
    opt.tabCount = static_cast<int>(numberArg(interp, objv, 3, objc, 0));
    opt.tabWidth = numberArg(interp, objv, 4, objc, 0.0);
    const CutoutResult result = cutoutGeometry(state->gerber.solid, opt);
    setPaths(*state, result.paths, result.bounds, opt.toolDiameter);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(static_cast<int>(result.paths.size())));
    return TCL_OK;
}

/**
 * @brief Implementa `ncc <Ø> <paso%>`.
 *
 * Despeja el cobre sobrante usando como área de trabajo el bounding box del
 * Gerber expandido 1 mm. Estrategia `Hatch` con el ángulo y el clearance por
 * defecto.
 *
 * @param data Puntero al `ShellState`.
 * @param interp Intérprete Tcl.
 * @param objc Cantidad de argumentos.
 * @param objv Argumentos posicionales (Ø 0,5; paso 50 %).
 * @return `TCL_OK` con la cantidad de recorridos, o `TCL_ERROR` sin Gerber.
 *
 * @note El argumento es porcentaje de paso (`50` = 50 % del diámetro), no
 *       superposición, a diferencia de la UI.
 * @warning No captura excepciones de GEOS.
 */
int cmdNcc(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (!state->hasGerber) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("no gerber loaded", -1));
        return TCL_ERROR;
    }
    NccOptions opt;
    opt.toolDiameter = numberArg(interp, objv, 1, objc, 0.5);
    opt.stepOver = numberArg(interp, objv, 2, objc, 50.0) / 100.0;
    const BBox b = state->gerber.bounds.expanded(1.0);
    const Geom area = b.valid ? Geom::box(b.xmin, b.ymin, b.xmax, b.ymax) : Geom();
    const NccResult result = clearCopper(state->gerber.solid, area, opt);
    setPaths(*state, result.paths, result.bounds, opt.toolDiameter);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(static_cast<int>(result.paths.size())));
    return TCL_OK;
}

/**
 * @brief Implementa `paint <Ø> <paso%>`.
 *
 * Rellena el cobre activo con contornos paralelos y deja el resultado como
 * `PathJob`.
 *
 * @param data Puntero al `ShellState`.
 * @param interp Intérprete Tcl.
 * @param objc Cantidad de argumentos.
 * @param objv Argumentos posicionales (Ø 0,1; paso 50 %).
 * @return `TCL_OK` con la cantidad de recorridos, o `TCL_ERROR` sin Gerber.
 *
 * @warning No captura excepciones de GEOS.
 */
int cmdPaint(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (!state->hasGerber) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("no gerber loaded", -1));
        return TCL_ERROR;
    }
    PaintOptions opt;
    opt.toolDiameter = numberArg(interp, objv, 1, objc, 0.1);
    opt.stepOver = numberArg(interp, objv, 2, objc, 50.0) / 100.0;
    const PaintResult result = paintArea(state->gerber.solid, opt);
    setPaths(*state, result.paths, result.bounds, opt.toolDiameter);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(static_cast<int>(result.paths.size())));
    return TCL_OK;
}

/**
 * @brief Implementa `mill <Ø>`: fresado siguiendo los recorridos del Gerber.
 *
 * Recorre `gerber.follows` y convierte en `PathJob` los elementos que sean
 * `LineString` o `LinearRing` de primer nivel.
 *
 * @param data Puntero al `ShellState`.
 * @param interp Intérprete Tcl.
 * @param objc Cantidad de argumentos.
 * @param objv Argumento opcional con el diámetro (0,2 por defecto).
 * @return `TCL_OK` con la cantidad de recorridos, o `TCL_ERROR` sin Gerber.
 *
 * @warning A diferencia de `collectLineStrings` de la CLI, aquí no se recurre
 *          en `MultiLineString` ni `GeometryCollection`: esos recorridos se
 *          pierden silenciosamente y el fresado queda incompleto.
 */
int cmdMill(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (!state->hasGerber) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("no gerber loaded", -1));
        return TCL_ERROR;
    }
    std::vector<std::vector<Pt>> lines;
    // Recorre colecciones anidadas (MULTILINESTRING, GEOMETRYCOLLECTION) para
    // no perder recorridos al fresar.
    std::function<void(const Geom&)> collect = [&](const Geom& g) {
        if (!g.valid() || g.empty()) return;
        const int type = g.typeId();
        if (type == GEOS_LINESTRING || type == GEOS_LINEARRING) {
            Ring ring = g.coords();
            if (ring.size() >= 2) lines.emplace_back(ring.begin(), ring.end());
            return;
        }
        if (type == GEOS_MULTILINESTRING || type == GEOS_GEOMETRYCOLLECTION ||
            type == GEOS_MULTIPOLYGON) {
            for (int i = 0; i < g.numGeometries(); ++i) collect(g.geometryN(i));
        }
    };
    for (const Geom& g : state->gerber.follows) collect(g);
    state->paths = PathJob{};
    state->paths.paths = std::move(lines);
    state->paths.bounds = state->gerber.bounds;
    state->paths.toolDiameter = numberArg(interp, objv, 1, objc, 0.2);
    state->hasPathJob = true;
    state->hasDrillJob = false;
    Tcl_SetObjResult(interp, Tcl_NewIntObj(static_cast<int>(state->paths.paths.size())));
    return TCL_OK;
}

/**
 * @brief Implementa `export_svg <archivo>`.
 *
 * Exporta el Gerber activo (sólidos, recorridos y límites) mediante `exportSvg`.
 *
 * @param data Puntero al `ShellState`.
 * @param interp Intérprete Tcl.
 * @param objc Cantidad de argumentos.
 * @param objv Argumentos (`export_svg`, ruta).
 * @return `TCL_OK`, o `TCL_ERROR` sin Gerber o sin ruta.
 *
 * @note No devuelve texto como resultado (queda vacío), por lo que un script no
 *       puede verificar el éxito más allá del código de error de Tcl.
 */
int cmdExportSvg(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (!state->hasGerber || objc < 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("usage: export_svg file", -1));
        return TCL_ERROR;
    }
    exportSvg(Tcl_GetString(objv[1]), state->gerber.solids, state->gerber.follows,
              state->gerber.bounds);
    return TCL_OK;
}

/**
 * @brief Implementa `export_dxf <archivo>`.
 *
 * Exporta los sólidos y recorridos del Gerber activo a DXF AC1015.
 *
 * @param data Puntero al `ShellState`.
 * @param interp Intérprete Tcl.
 * @param objc Cantidad de argumentos.
 * @param objv Argumentos (`export_dxf`, ruta).
 * @return `TCL_OK`, o `TCL_ERROR` sin Gerber o sin ruta.
 *
 * @note No informa del error de escritura: `exportDxf` no devuelve estado, de
 *       modo que un fallo de disco produce `TCL_OK` igualmente.
 */
int cmdExportDxf(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (!state->hasGerber || objc < 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("usage: export_dxf file", -1));
        return TCL_ERROR;
    }
    exportDxf(Tcl_GetString(objv[1]), state->gerber.solids, state->gerber.follows);
    return TCL_OK;
}

/**
 * @brief Implementa `thieving <Ø> <separación> <clearance>`.
 *
 * Calcula los puntos de cobre que no invaden el cobre existente y devuelve sólo
 * la cantidad.
 *
 * @param data Puntero al `ShellState`.
 * @param interp Intérprete Tcl.
 * @param objc Cantidad de argumentos.
 * @param objv Argumentos posicionales (Ø 0,5; separación 2,0; clearance 0,5).
 * @return `TCL_OK` con la cantidad de puntos, o `TCL_ERROR` sin Gerber.
 *
 * @warning El comando no guarda ni escribe los puntos: es informativo y no
 *          permite exportar el Gerber de thieving desde Tcl (a diferencia de la
 *          CLI, que sí lo hace).
 */
int cmdThieving(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (!state->hasGerber) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("no gerber loaded", -1));
        return TCL_ERROR;
    }
    ThievingOptions opt;
    opt.dotDiameter = numberArg(interp, objv, 1, objc, 0.5);
    opt.spacing = numberArg(interp, objv, 2, objc, 2.0);
    opt.clearance = numberArg(interp, objv, 3, objc, 0.5);
    const std::vector<Pt> dots = thievingPoints(state->gerber.solid, state->gerber.bounds, opt);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(static_cast<int>(dots.size())));
    return TCL_OK;
}

/**
 * @brief Implementa `export_gcode <archivo>`.
 *
 * Exporta el trabajo activo: si hay `DrillJob` genera taladrado y, si no, los
 * recorridos de geometría. Usa `GCodeOptions` por defecto (milímetros,
 * preprocesador `Default`, Z estándar).
 *
 * @param data Puntero al `ShellState`.
 * @param interp Intérprete Tcl.
 * @param objc Cantidad de argumentos.
 * @param objv Argumentos (`export_gcode`, ruta).
 * @return `TCL_OK`, o `TCL_ERROR` si no hay trabajo, falta la ruta o no se
 *         puede escribir el archivo.
 *
 * @warning No hay forma de pasar parámetros de máquina desde Tcl: avances, Z,
 *          husillo y postprocesador quedan siempre en los valores por defecto
 *          de `GCodeOptions`.
 */
int cmdExportGcode(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (objc < 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("usage: export_gcode file", -1));
        return TCL_ERROR;
    }
    GCodeOptions opt;
    std::string code;
    if (state->hasDrillJob) code = drillingGCode(state->drills, opt);
    else if (state->hasPathJob) code = pathsGCode(state->paths, opt);
    else {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("nothing to export", -1));
        return TCL_ERROR;
    }
    std::ofstream out(Tcl_GetString(objv[1]), std::ios::binary);
    if (!out) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("cannot write file", -1));
        return TCL_ERROR;
    }
    out << code;
    return TCL_OK;
}

/**
 * @brief Implementa `info`: resumen del estado actual.
 *
 * Emite una línea por dato disponible: área del Gerber, cantidad de taladros,
 * recorridos y herramientas.
 *
 * @param data Puntero al `ShellState`.
 * @param interp Intérprete Tcl.
 * @return Siempre `TCL_OK`.
 *
 * @note Ignora los argumentos: `info algo` no produce error.
 */
int cmdInfo(ClientData data, Tcl_Interp* interp, int, Tcl_Obj* const[]) {
    auto* state = static_cast<ShellState*>(data);
    std::ostringstream os;
    if (state->hasGerber) os << "gerber area=" << state->gerber.solid.area() << "\n";
    if (state->hasExcellon)
        os << "excellon drills=" << state->excellon.drillCount << "\n";
    if (state->hasPathJob) os << "paths=" << state->paths.paths.size() << "\n";
    if (state->hasDrillJob) os << "tools=" << state->drills.tools.size() << "\n";
    const std::string text = os.str();
    Tcl_SetObjResult(interp, Tcl_NewStringObj(text.c_str(), -1));
    return TCL_OK;
}

}  // namespace

/**
 * @brief Crea el intérprete Tcl, registra los comandos y ejecuta el script.
 *
 * Flujo: `Tcl_FindExecutable`, `Tcl_CreateInterp`, `Tcl_Init` (un fallo sólo se
 * informa como advertencia, porque los comandos del núcleo siguen
 * disponibles), registro de los doce comandos CAM con `&state` como
 * `ClientData` y ejecución:
 * - con `path` vacío o `-`: REPL por `stdin`, evaluando línea por línea;
 * - en otro caso: `Tcl_EvalFile` del script completo.
 *
 * @param path Ruta del script o cadena vacía (o `-`) para modo interactivo.
 * @return 0 en éxito; 1 si no se puede crear el intérprete o el script falla.
 *
 * @note El estado vive en la pila de esta función y el intérprete se destruye
 *       antes de salir, por lo que ningún comando conserva punteros colgantes.
 * @note En el REPL los errores se informan y el bucle continúa; en modo script
 *       el primer error aborta con código 1.
 * @warning El REPL evalúa una línea por vez, de modo que una definición
 *          multilínea (`proc f {} {` … `}`) falla por script incompleto.
 * @warning `Tcl_Init` depende de `TCL_LIBRARY` o del framework detectado; si el
 *          entorno no lo encuentra, `proc`, `if` y `expr` no estarán
 *          disponibles aunque la advertencia sugiera lo contrario.
 */
int runTclScript(const std::string& path) {
    Tcl_FindExecutable(nullptr);
    Tcl_Interp* interp = Tcl_CreateInterp();
    if (!interp) {
        std::cerr << "cannot create Tcl interpreter\n";
        return 1;
    }
    if (Tcl_Init(interp) != TCL_OK) {
        // Core commands (set, puts, if, proc...) still work.
        std::cerr << "tcl init warning: " << Tcl_GetStringResult(interp) << "\n";
    }

    ShellState state;
    Tcl_CreateObjCommand(interp, "open_gerber", cmdOpenGerber, &state, nullptr);
    Tcl_CreateObjCommand(interp, "open_excellon", cmdOpenExcellon, &state, nullptr);
    Tcl_CreateObjCommand(interp, "isolate", cmdIsolate, &state, nullptr);
    Tcl_CreateObjCommand(interp, "cutout", cmdCutout, &state, nullptr);
    Tcl_CreateObjCommand(interp, "ncc", cmdNcc, &state, nullptr);
    Tcl_CreateObjCommand(interp, "paint", cmdPaint, &state, nullptr);
    Tcl_CreateObjCommand(interp, "export_gcode", cmdExportGcode, &state, nullptr);
    Tcl_CreateObjCommand(interp, "mill", cmdMill, &state, nullptr);
    Tcl_CreateObjCommand(interp, "export_svg", cmdExportSvg, &state, nullptr);
    Tcl_CreateObjCommand(interp, "export_dxf", cmdExportDxf, &state, nullptr);
    Tcl_CreateObjCommand(interp, "thieving", cmdThieving, &state, nullptr);
    Tcl_CreateObjCommand(interp, "info", cmdInfo, &state, nullptr);

    if (path.empty() || path == "-") {
        std::string line;
        while (std::getline(std::cin, line)) {
            const int rc = Tcl_Eval(interp, line.c_str());
            const char* result = Tcl_GetStringResult(interp);
            if (rc != TCL_OK) std::cerr << "error: " << result << "\n";
            else if (result && *result) std::cout << result << "\n";
        }
        Tcl_DeleteInterp(interp);
        return 0;
    }

    const int rc = Tcl_EvalFile(interp, path.c_str());
    if (rc != TCL_OK) {
        std::cerr << "tcl error: " << Tcl_GetStringResult(interp) << "\n";
        Tcl_DeleteInterp(interp);
        return 1;
    }
    Tcl_DeleteInterp(interp);
    return 0;
}

}  // namespace fc

#endif  // FC_HAVE_TCL
