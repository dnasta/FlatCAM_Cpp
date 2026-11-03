/**
 * @file main.cpp
 * @brief Punto de entrada de `flatcam-cli`: despacho de subcomandos CAM.
 *
 * La CLI interpreta un primer argumento (`gerber`, `excellon`, `gcode`,
 * `isolate`, `cutout`, `ncc`, `paint`, `mill`, `svg`, `dxf`, `film`,
 * `import-svg`, `import-dxf`, `thieving`, `project`, `tcl`) y ejecuta el parser
 * o la herramienta correspondiente. Las opciones se comparten entre comandos
 * mediante una única estructura `Args` y cada ejecución termina en una función
 * `runXxx` que imprime estadísticas en texto o JSON o escribe el archivo de
 * salida pedido.
 */

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "flatcam/excellon.hpp"
#include "flatcam/gcode.hpp"
#include "flatcam/gerber.hpp"
#include "flatcam/cutout.hpp"
#include "flatcam/isolation.hpp"
#include "flatcam/ncc.hpp"
#include "flatcam/paint.hpp"
#include "flatcam/svg.hpp"
#include "flatcam/dxf.hpp"
#include "flatcam/thieving.hpp"
#include "flatcam/importers.hpp"
#include "flatcam/project.hpp"

namespace fc {
/// @brief Ejecuta un script Tcl (implementado en `tcl.cpp` o `tcl_embed.cpp`).
int runTclScript(const std::string& path);
}

namespace {

/// @brief Imprime `v` con 12 cifras y normaliza los valores casi nulos a 0.
void printNumber(double v) {
    if (std::fabs(v) < 1e-12) v = 0.0;
    std::cout << std::setprecision(12) << v;
}

/**
 * @brief Escribe una caja en formato JSON (`null` si es inválida).
 * @param b Caja a serializar.
 */
void printBBox(const fc::BBox& b) {
    if (!b.valid) {
        std::cout << "null";
        return;
    }
    std::cout << "{\"xmin\":";
    printNumber(b.xmin);
    std::cout << ",\"ymin\":";
    printNumber(b.ymin);
    std::cout << ",\"xmax\":";
    printNumber(b.xmax);
    std::cout << ",\"ymax\":";
    printNumber(b.ymax);
    std::cout << "}";
}

/**
 * @brief Imprime la ayuda de uso y devuelve el código de salida 2.
 *
 * La ayuda cubre los subcomandos principales y sus opciones habituales.
 *
 * @return Siempre 2 (error de uso), para que `main` lo propague.
 *
 * @note La lista no está completa: `--z-toolchange`, `--dwell`, `--power`,
 *       `--z-focus`, `--strategy`, `--previous-tool-dia` y `--extract` se
 *       aceptan pero no se documentan aquí.
 */
int usage() {
    std::cerr << "usage:\n"
                 "  flatcam-cli gerber <file> [--units mm|in] [--json] [--flatcam-compat]\n"
                 "  flatcam-cli excellon <file> [--units mm|in] [--json]\n"
                 "  flatcam-cli isolate <file.gbr> [--tool-dia D] [--passes N] [--overlap PCT]\n"
                 "      [--quad-segs N] [--invert] [--exteriors-only] [--interiors-only]\n"
                 "      [--z-cut X] [--feed-xy X] [-o out.nc] [--json]\n"
                 "  flatcam-cli cutout <file.gbr> [--tool-dia D] [--margin M] [--internal]\n"
                 "      [--tabs N] [--tab-width W] [--area xmin,ymin,xmax,ymax]\n"
                 "      [--z-cut X] [--depth-per-pass X] [--feed-xy X] [-o out.nc] [--json]\n"
                 "  flatcam-cli ncc <file.gbr> [--tool-dia D] [--superposition PCT] [--step-over F] [--clearance C]\n"
                 "      [--angle A] [--margin M] [--area xmin,ymin,xmax,ymax] [-o out.nc] [--json]\n"
                 "  flatcam-cli project <file.FlatPrj> [--info] [--summarize] [--object N]\n"
                 "      [--export-wkt F] [--export-all DIR] [--json]\n"
                 "  flatcam-cli project --create <out.FlatPrj> <file> [file...]\n"
                 "  flatcam-cli paint <file.gbr> [--tool-dia D] [--step-over F] [--margin M]\n"
                 "      [--preprocessor default|grbl|grbl_laser] [-o out.nc] [--json]\n"
                 "  flatcam-cli mill <file.gbr> [--tool-dia D] [--z-cut X] [-o out.nc]\n"
                 "  flatcam-cli svg <file.gbr> -o out.svg\n"
                 "  flatcam-cli dxf <file.gbr> -o out.dxf\n"
                 "  flatcam-cli film <gbr...> [--margin M] [--negative] -o out.svg\n"
                 "  flatcam-cli thieving <file.gbr> [--tool-dia D] [--spacing S] [--clearance C] -o out.gbr\n"
                 "  flatcam-cli import-svg <file.svg> -o out.gbr [--units mm|in]\n"
                 "  flatcam-cli import-dxf <file.dxf> -o out.gbr [--units mm|in]\n"
                 "  flatcam-cli tcl [script.tcl]\n"
                 "  flatcam-cli gcode <file.drl> [--preprocessor default|grbl]\n"
                 "      [--units mm|in] [--z-cut X] [--z-move X] [--z-start X] [--z-end X]\n"
                 "      [--feed-z X] [--feed-xy X] [--spindle N] [--decimals N] [--optimize]\n"
                 "      [--tsp-exact]\n"
                 "      [-o out.nc]\n";
    return 2;
}

/**
 * @brief Decide si una ruta corresponde a un archivo de taladrado Excellon.
 *
 * Compara la extensión en minúsculas contra `drl`, `xln`, `exc` y `ncd`. Se
 * usa para separar Gerber de Excellon en `film`, `project --create` y `gcode`.
 *
 * @param path Ruta a evaluar.
 * @return `true` si la extensión es una de las reconocidas.
 *
 * @note Una ruta sin punto se considera Gerber.
 * @note Depende de `std::tolower` sin incluir `<cctype>`; hoy funciona por
 *       inclusión transitiva de la biblioteca estándar.
 */
bool isExcellonPath(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == "drl" || ext == "xln" || ext == "exc" || ext == "ncd";
}

/**
 * @brief Opciones acumuladas de la línea de comandos.
 *
 * Estructura única compartida por todos los subcomandos: contiene el tipo de
 * comando, los archivos de entrada, la unidad de trabajo y una instancia de las
 * opciones de cada herramienta (`GCodeOptions`, `IsolationOptions`,
 * `CutoutOptions`, `NccOptions`, `PaintOptions`, `ThievingOptions`).
 *
 * Como las banderas tienen un único nombre, algunas afectan a varias
 * herramientas a la vez (por ejemplo `--margin` escribe el margen de corte,
 * NCC, pintura y thieving).
 */
struct Args {
    std::string kind;
    std::string file;
    std::vector<std::string> inputs;
    fc::Units units = fc::Units::Mm;
    bool json = false;
    bool compat = false;
    std::string output;
    std::string preprocessor = "default";
    fc::GCodeOptions gcode;
    fc::IsolationOptions isolation;
    fc::CutoutOptions cutout;
    fc::NccOptions ncc;
    fc::PaintOptions paint;
    fc::ThievingOptions thieving;
    bool rest = false;
    bool optimize = false;
    bool tspExact = false;
    bool create = false;
    bool projectInfo = false;
    bool negative = false;
    bool summarize = false;
    std::string exportAll;
    int objectIndex = -1;
    std::string exportWkt;
    std::string extractDir;
    double previousToolDia = 0.0;
};

/**
 * @brief Interpreta los argumentos de la línea de comandos.
 *
 * El primer argumento es el subcomando y el resto son opciones. Acepta tanto
 * `--opcion valor` como `--opcion=valor`. Los argumentos que no empiezan con
 * `-` se acumulan como entradas (`inputs`); el primero queda además como
 * `file` para los comandos de archivo único (isolate, cutout, ncc, ...).
 *
 * Tras el bucle se fija la unidad de los programas G-code y se traduce el nombre
 * del preprocesador a `GCodeFlavor`, con estos alias:
 * - `grbl` / `grbl_11` → `Grbl`;
 * - `grbl_no_m6` / `grbl_11_no_m6` → `Grbl` (sin `M6`, por la tabla de perfiles);
 * - `grbl_laser` → `GrblLaser`;
 * - `marlin` / `marlin_laser` → `Marlin`;
 * - cualquier otro → `Default`.
 *
 * @param argc Cantidad de argumentos.
 * @param argv Vector de argumentos del sistema.
 * @param args Estructura a completar.
 * @return `true` si la línea es válida; `false` si falta el subcomando, una
 *         opción es desconocida o el comando no es `tcl` y no hay entradas.
 *
 * @note Los porcentajes se convierten a fracción en el acto: `--overlap 20`
 *       produce `0.2` y `--superposition 80` produce `0.2` de paso.
 * @note `--power` copia el valor también a `spindleSpeed`, porque el láser usa
 *       ese campo como potencia en los perfiles láser.
 * @note `--area` se guarda en `cutout.area`, pero `runNcc` también lo lee.
 * @warning `--area` exige exactamente cuatro números separados por comas; una
 *          cantidad distinta hace que `std::stod` lance `invalid_argument`, que
 *          sólo se captura en `main` y termina con un mensaje genérico.
 * @warning Las opciones numéricas usan `std::stod`/`std::stoi` sin validar el
 *          texto; un valor no numérico también aborta con el mensaje genérico.
 * @warning Los alias `grbl_no_m6` y `grbl_11_no_m6` se traducen al mismo
 *          `Grbl` que `grbl`, de modo que son redundantes: el perfil GRBL nunca
 *          emite `M6` (ver `usesM6` en `gcode.cpp`), por lo que la variante "sin
 *          M6" no cambia el programa generado.
 */
bool parseArgs(int argc, char** argv, Args& args) {
    if (argc < 2) return false;
    args.kind = argv[1];

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        std::string inlineValue;
        bool hasInlineValue = false;
        if (arg.rfind("--", 0) == 0) {
            const std::size_t eq = arg.find('=');
            if (eq != std::string::npos) {
                inlineValue = arg.substr(eq + 1);
                arg = arg.substr(0, eq);
                hasInlineValue = true;
            }
        }
        auto value = [&](const char* name) -> std::string {
            if (hasInlineValue) return inlineValue;
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << name << "\n";
                return {};
            }
            return argv[++i];
        };

        if (arg == "--units") {
            const std::string u = value("--units");
            args.units = (u == "in" || u == "inch" || u == "IN") ? fc::Units::Inch : fc::Units::Mm;
        } else if (arg == "--json") {
            args.json = true;
        } else if (arg == "--flatcam-compat") {
            args.compat = true;
        } else if (arg == "-o" || arg == "--output") {
            args.output = value("-o");
        } else if (arg == "--preprocessor") {
            args.preprocessor = value("--preprocessor");
        } else if (arg == "--z-cut") {
            args.gcode.zCut = std::stod(value("--z-cut"));
        } else if (arg == "--z-move") {
            args.gcode.zMove = std::stod(value("--z-move"));
        } else if (arg == "--z-start") {
            args.gcode.zStart = std::stod(value("--z-start"));
        } else if (arg == "--z-end") {
            args.gcode.zEnd = std::stod(value("--z-end"));
        } else if (arg == "--z-toolchange") {
            args.gcode.zToolchange = std::stod(value("--z-toolchange"));
        } else if (arg == "--feed-z") {
            args.gcode.feedZ = std::stod(value("--feed-z"));
        } else if (arg == "--feed-xy") {
            args.gcode.feedXY = std::stod(value("--feed-xy"));
        } else if (arg == "--spindle") {
            args.gcode.spindleSpeed = std::stod(value("--spindle"));
        } else if (arg == "--power") {
            args.gcode.laserPower = std::stod(value("--power"));
            args.gcode.spindleSpeed = args.gcode.laserPower;
        } else if (arg == "--z-focus") {
            args.gcode.zFocus = std::stod(value("--z-focus"));
        } else if (arg == "--decimals") {
            args.gcode.decimals = std::stoi(value("--decimals"));
        } else if (arg == "--dwell") {
            args.gcode.dwellTime = std::stod(value("--dwell"));
        } else if (arg == "--depth-per-pass") {
            args.gcode.depthPerPass = std::stod(value("--depth-per-pass"));
        } else if (arg == "--margin") {
            args.cutout.margin = std::stod(value("--margin"));
            args.ncc.margin = args.cutout.margin;
            args.paint.margin = args.cutout.margin;
            args.thieving.margin = args.cutout.margin;
        } else if (arg == "--internal") {
            args.cutout.external = false;
        } else if (arg == "--tabs") {
            args.cutout.tabCount = std::stoi(value("--tabs"));
        } else if (arg == "--tab-width") {
            args.cutout.tabWidth = std::stod(value("--tab-width"));
        } else if (arg == "--area") {
            const std::string box = value("--area");
            std::array<double, 4> v{};
            std::size_t start = 0;
            for (int k = 0; k < 4; ++k) {
                const std::size_t comma = box.find(',', start);
                v[k] = std::stod(box.substr(start, comma - start));
                start = comma == std::string::npos ? box.size() : comma + 1;
            }
            args.cutout.area = fc::BBox{v[0], v[1], v[2], v[3]};
        } else if (arg == "--tool-dia") {
            args.isolation.toolDiameter = std::stod(value("--tool-dia"));
            args.cutout.toolDiameter = args.isolation.toolDiameter;
            args.ncc.toolDiameter = args.isolation.toolDiameter;
            args.paint.toolDiameter = args.isolation.toolDiameter;
            args.thieving.dotDiameter = args.isolation.toolDiameter;
        } else if (arg == "--passes") {
            args.isolation.passes = std::stoi(value("--passes"));
        } else if (arg == "--overlap") {
            args.isolation.overlap = std::stod(value("--overlap")) / 100.0;
        } else if (arg == "--quad-segs") {
            args.isolation.quadrantSegments = std::stoi(value("--quad-segs"));
        } else if (arg == "--invert") {
            args.isolation.invert = true;
        } else if (arg == "--exteriors-only") {
            args.isolation.exteriorsOnly = true;
        } else if (arg == "--interiors-only") {
            args.isolation.interiorsOnly = true;
        } else if (arg == "--rest") {
            args.rest = true;
        } else if (arg == "--optimize") {
            args.optimize = true;
        } else if (arg == "--tsp-exact") {
            args.optimize = true;
            args.tspExact = true;
        } else if (arg == "--extract") {
            args.extractDir = value("--extract");
        } else if (arg == "--create") {
            args.create = true;
        } else if (arg == "--negative") {
            args.negative = true;
        } else if (arg == "--info") {
            args.projectInfo = true;
        } else if (arg == "--summarize") {
            args.summarize = true;
        } else if (arg == "--export-all") {
            args.exportAll = value("--export-all");
        } else if (arg == "--object") {
            args.objectIndex = std::stoi(value("--object"));
        } else if (arg == "--export-wkt") {
            args.exportWkt = value("--export-wkt");
        } else if (arg == "--previous-tool-dia") {
            args.previousToolDia = std::stod(value("--previous-tool-dia"));
        } else if (arg == "--strategy") {
            const std::string strategy = value("--strategy");
            args.ncc.strategy = strategy == "paint" ? fc::NccStrategy::Paint : fc::NccStrategy::Hatch;
        } else if (arg == "--superposition") {
            const double overlap = std::stod(value("--superposition"));
            args.ncc.stepOver = (100.0 - overlap) / 100.0;
            args.paint.stepOver = args.ncc.stepOver;
        } else if (arg == "--step-over") {
            args.ncc.stepOver = std::stod(value("--step-over"));
            args.paint.stepOver = args.ncc.stepOver;
        } else if (arg == "--clearance") {
            args.ncc.clearance = std::stod(value("--clearance"));
            args.thieving.clearance = args.ncc.clearance;
        } else if (arg == "--angle") {
            args.ncc.angleDeg = std::stod(value("--angle"));
        } else if (arg == "--spacing") {
            args.thieving.spacing = std::stod(value("--spacing"));
        } else if (!arg.empty() && arg.front() == '-') {
            std::cerr << "unknown option: " << arg << "\n";
            return false;
        } else {
            args.inputs.push_back(arg);
        }
    }

    if (args.inputs.empty()) {
        if (args.kind == "tcl") {
            args.file.clear();
        } else {
            return false;
        }
    } else {
        args.file = args.inputs.front();
    }

    args.gcode.units = args.units;
    if (args.preprocessor == "grbl" || args.preprocessor == "grbl_11") {
        args.gcode.flavor = fc::GCodeFlavor::Grbl;
    } else if (args.preprocessor == "grbl_laser") {
        args.gcode.flavor = fc::GCodeFlavor::GrblLaser;
    } else if (args.preprocessor == "marlin" || args.preprocessor == "marlin_laser") {
        args.gcode.flavor = fc::GCodeFlavor::Marlin;
    } else if (args.preprocessor == "grbl_no_m6" || args.preprocessor == "grbl_11_no_m6") {
        args.gcode.flavor = fc::GCodeFlavor::GrblNoM6;
    } else {
        args.gcode.flavor = fc::GCodeFlavor::Default;
    }
    return true;
}

/**
 * @brief Parsea un Gerber e informa aperturas, flashes, segmentos y área.
 *
 * Con `--json` emite un objeto JSON de una línea; sin ella imprime un informe
 * legible con unidades del archivo, conteos, área y límites, y lista cada aviso
 * del parser.
 *
 * @param args Opciones ya interpretadas (`file`, `units`, `json`, `compat`).
 * @return 0 si el parseo se completa (aunque haya avisos); propaga la excepción
 *         del parser en caso de archivo ilegible.
 *
 * @note `--flatcam-compat` activa `legacyRectStroke`, que reproduce el trazo
 *       rectangular del FlatCAM original en lugar de la suma de Minkowski.
 */
int runGerber(const Args& args) {
    fc::GerberOptions opt;
    opt.appUnits = args.units;
    opt.legacyRectStroke = args.compat;
    const fc::GerberResult r = fc::GerberParser(opt).parseFile(args.file);

    if (args.json) {
        std::cout << "{\"type\":\"gerber\",\"file\":\"" << args.file << "\",\"units\":\""
                  << fc::toString(args.units) << "\",\"file_units\":\"" << fc::toString(r.fileUnits)
                  << "\",\"apertures\":" << r.apertures.size() << ",\"flashes\":" << r.flashes
                  << ",\"segments\":" << r.segments << ",\"regions\":" << r.regions << ",\"area\":";
        printNumber(r.solid.area());
        std::cout << ",\"bounds\":";
        printBBox(r.bounds);
        std::cout << ",\"warnings\":" << r.warnings.size() << "}\n";
        return 0;
    }

    std::cout << "Gerber: " << args.file << "\n"
              << "  file units: " << fc::toString(r.fileUnits) << "\n"
              << "  apertures : " << r.apertures.size() << "\n"
              << "  flashes   : " << r.flashes << "\n"
              << "  segments  : " << r.segments << "\n"
              << "  regions   : " << r.regions << "\n"
              << "  area      : " << r.solid.area() << " " << fc::toString(args.units) << "^2\n";
    if (r.bounds.valid) {
        std::cout << "  bounds    : " << r.bounds.xmin << ", " << r.bounds.ymin << " .. "
                  << r.bounds.xmax << ", " << r.bounds.ymax << "\n";
    }
    for (const std::string& w : r.warnings) std::cout << "  warning   : " << w << "\n";
    return 0;
}

/**
 * @brief Parsea un Excellon y resume herramientas, taladros, ranuras y área.
 *
 * Igual que `runGerber`, soporta salida legible o `--json`. El área y los
 * límites corresponden a la geometría sólida generada (círculos de taladro y
 * ranuras redondeadas).
 *
 * @param args Opciones ya interpretadas.
 * @return 0 si el parseo termina correctamente.
 *
 * @note El conteo de taladros y ranuras viene del parser; no se recalcula a
 *       partir de la geometría.
 */
int runExcellon(const Args& args) {
    fc::ExcellonOptions opt;
    opt.appUnits = args.units;
    const fc::ExcellonResult r = fc::ExcellonParser(opt).parseFile(args.file);

    if (args.json) {
        std::cout << "{\"type\":\"excellon\",\"file\":\"" << args.file << "\",\"units\":\""
                  << fc::toString(args.units) << "\",\"file_units\":\"" << fc::toString(r.fileUnits)
                  << "\",\"tools\":" << r.tools.size() << ",\"drills\":" << r.drillCount
                  << ",\"slots\":" << r.slotCount << ",\"area\":";
        printNumber(r.solid.area());
        std::cout << ",\"bounds\":";
        printBBox(r.bounds);
        std::cout << ",\"warnings\":" << r.warnings.size() << "}\n";
        return 0;
    }

    std::cout << "Excellon: " << args.file << "\n"
              << "  file units: " << fc::toString(r.fileUnits) << "\n"
              << "  tools     : " << r.tools.size() << "\n"
              << "  drills    : " << r.drillCount << "\n"
              << "  slots     : " << r.slotCount << "\n"
              << "  area      : " << r.solid.area() << " " << fc::toString(args.units) << "^2\n";
    if (r.bounds.valid) {
        std::cout << "  bounds    : " << r.bounds.xmin << ", " << r.bounds.ymin << " .. "
                  << r.bounds.xmax << ", " << r.bounds.ymax << "\n";
    }
    for (const std::string& w : r.warnings) std::cout << "  warning   : " << w << "\n";
    return 0;
}

/**
 * @brief Lee, inspecciona, crea o exporta proyectos `.FlatPrj`.
 *
 * El comportamiento depende de las banderas y sigue este orden de prioridad:
 *
 * 1. `--create`: el primer `input` es el archivo de salida y el resto son
 *    Gerber/Excellon que se parsean y empaquetan en un proyecto nuevo.
 * 2. `--summarize` o `--export-all DIR`: recorrido en streaming
 *    (`forEachProjectObject`) que informa un resumen por objeto y, si se pidió,
 *    escribe un `.wkt` por objeto con sus sólidos y recorridos.
 * 3. `--object N`: carga sólo ese objeto del proyecto (sin materializar el
 *    resto), informa su área y límites y, si se pide, exporta el WKT.
 * 4. `--info`: lee únicamente los metadatos con `inspectProject`, apto para
 *    proyectos comprimidos de varios cientos de MB.
 * 5. Sin banderas: carga completa con `loadProject` e informa cada objeto;
 *    `--extract DIR` escribe además un `.wkt` por sólido.
 *
 * @param args Opciones ya interpretadas.
 * @return 0 en éxito; 1 si no se puede escribir un archivo pedido; 2 si
 *         `--create` recibe menos de dos entradas.
 *
 * @note Todos los modos aceptan `--json`, salvo `--create` (que sólo escribe
 *       un mensaje de progreso por `stderr`).
 * @warning La carga completa del modo 5 puede consumir mucha memoria en
 *          proyectos reales; el módulo de proyecto limita el tamaño y sugiere
 *          `--info` en ese caso.
 * @warning `--extract` sólo se aplica al modo 5; combinado con `--object` se
 *          ignora silenciosamente.
 */
int runProject(const Args& args) {
    if (args.create) {
        if (args.inputs.size() < 2) {
            std::cerr << "usage: flatcam-cli project --create <out.FlatPrj> <file> [file...]\n";
            return 2;
        }
        const std::string output = args.inputs.front();
        fc::Project project;
        for (std::size_t i = 1; i < args.inputs.size(); ++i) {
            const std::string& input = args.inputs[i];
            fc::ProjectObject obj;
            obj.name = std::filesystem::path(input).stem().string();
            if (isExcellonPath(input)) {
                fc::ExcellonOptions eopt;
                eopt.appUnits = args.units;
                const fc::ExcellonResult r = fc::ExcellonParser(eopt).parseFile(input);
                obj.kind = "excellon";
                obj.units = eopt.appUnits;
                obj.solids = r.geometry;
            } else {
                fc::GerberOptions gopt;
                gopt.appUnits = args.units;
                const fc::GerberResult r = fc::GerberParser(gopt).parseFile(input);
                obj.kind = "gerber";
                obj.units = gopt.appUnits;
                obj.solids = r.solids;
                obj.follows = r.follows;
            }
            project.objects.push_back(std::move(obj));
        }
        fc::saveProject(output, project);
        std::cerr << "wrote " << output << " (" << project.objects.size() << " objects)\n";
        return 0;
    }

    if (args.summarize || !args.exportAll.empty()) {
        std::filesystem::create_directories(args.exportAll.empty() ? "." : args.exportAll);
        const auto summaries = fc::forEachProjectObject(args.file, [&](fc::ProjectObject&& obj) {
            if (!args.exportAll.empty()) {
                const std::string base = args.exportAll + "/" + obj.name;
                std::ofstream out(base + ".wkt", std::ios::binary);
                if (out) {
                    for (const fc::Geom& g : obj.solids) out << g.toWkt(9) << "\n";
                    for (const fc::Geom& g : obj.follows) out << g.toWkt(9) << "\n";
                }
            }
            if (args.json) {
                std::cout << "{\"kind\":\"" << obj.kind << "\",\"name\":\"" << obj.name
                          << "\",\"units\":\"" << fc::toString(obj.units) << "\",\"solids\":"
                          << obj.solids.size() << ",\"follows\":" << obj.follows.size()
                          << ",\"area\":";
                double area = 0.0;
                for (const fc::Geom& g : obj.solids) area += g.area();
                printNumber(area);
                std::cout << "}\n";
            }
            return true;
        });

        if (!args.json) {
            std::cout << "Project: " << args.file << "  (" << summaries.size() << " objetos)\n";
            for (const fc::ProjectObjectSummary& s : summaries) {
                std::cout << "  [" << s.kind << "] " << s.name << "  (" << fc::toString(s.units)
                          << ", " << s.solids << " sólidos, área " << s.area << ")";
                if (s.bounds.valid) {
                    std::cout << "  [" << s.bounds.xmin << "," << s.bounds.ymin << " .. "
                              << s.bounds.xmax << "," << s.bounds.ymax << "]";
                }
                std::cout << "\n";
            }
        }
        return 0;
    }

    if (args.objectIndex >= 0) {
        const fc::ProjectObject obj =
            fc::loadProjectObject(args.file, static_cast<std::size_t>(args.objectIndex));
        double area = 0.0;
        fc::BBox bounds;
        for (const fc::Geom& g : obj.solids) {
            area += g.area();
            const fc::BBox b = g.bounds();
            if (!b.valid) continue;
            if (!bounds.valid) {
                bounds = b;
            } else {
                bounds.xmin = std::min(bounds.xmin, b.xmin);
                bounds.ymin = std::min(bounds.ymin, b.ymin);
                bounds.xmax = std::max(bounds.xmax, b.xmax);
                bounds.ymax = std::max(bounds.ymax, b.ymax);
            }
        }
        if (args.json) {
            std::cout << "{\"kind\":\"" << obj.kind << "\",\"name\":\"" << obj.name
                      << "\",\"units\":\"" << fc::toString(obj.units) << "\",\"solids\":"
                      << obj.solids.size() << ",\"follows\":" << obj.follows.size() << ",\"area\":";
            printNumber(area);
            std::cout << ",\"bounds\":";
            printBBox(bounds);
            std::cout << "}\n";
        } else {
            std::cout << "Object " << args.objectIndex << ": [" << obj.kind << "] " << obj.name
                      << "  (" << fc::toString(obj.units) << ")\n"
                      << "  sólidos   : " << obj.solids.size() << "\n"
                      << "  recorridos: " << obj.follows.size() << "\n"
                      << "  área      : " << area << "\n";
            if (bounds.valid) {
                std::cout << "  bounds    : " << bounds.xmin << ", " << bounds.ymin << " .. "
                          << bounds.xmax << ", " << bounds.ymax << "\n";
            }
        }
        if (!args.exportWkt.empty()) {
            std::ofstream out(args.exportWkt, std::ios::binary);
            if (!out) {
                std::cerr << "cannot write " << args.exportWkt << "\n";
                return 1;
            }
            for (const fc::Geom& g : obj.solids) out << g.toWkt(9) << "\n";
            for (const fc::Geom& g : obj.follows) out << g.toWkt(9) << "\n";
        }
        return 0;
    }

    if (args.projectInfo) {
        const fc::ProjectInfo info = fc::inspectProject(args.file);
        if (args.json) {
            std::cout << "{\"type\":\"project\",\"version\":\"" << info.version
                      << "\",\"compressed\":" << (info.compressed ? "true" : "false")
                      << ",\"objects\":[";
            for (std::size_t i = 0; i < info.objects.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << "{\"kind\":\"" << info.objects[i].kind << "\",\"name\":\""
                          << info.objects[i].name << "\",\"units\":\"" << info.objects[i].units
                          << "\"}";
            }
            std::cout << "]}\n";
        } else {
            std::cout << "Project: " << args.file << "  (FlatCAM " << info.version
                      << (info.compressed ? ", comprimido lzma" : "") << ")\n";
            for (const fc::ProjectObjectInfo& obj : info.objects) {
                std::cout << "  [" << obj.kind << "] " << obj.name;
                if (!obj.units.empty()) std::cout << "  (" << obj.units << ")";
                std::cout << "\n";
            }
        }
        return 0;
    }

    const fc::Project project = fc::loadProject(args.file);

    if (args.json) {
        std::cout << "{\"type\":\"project\",\"version\":\"" << project.version << "\",\"objects\":[";
        bool first = true;
        for (const fc::ProjectObject& obj : project.objects) {
            if (!first) std::cout << ",";
            first = false;
            double area = 0.0;
            for (const fc::Geom& g : obj.solids) area += g.area();
            std::cout << "{\"kind\":\"" << obj.kind << "\",\"name\":\"" << obj.name
                      << "\",\"units\":\"" << fc::toString(obj.units) << "\",\"solids\":"
                      << obj.solids.size() << ",\"follows\":" << obj.follows.size()
                      << ",\"area\":";
            printNumber(area);
            std::cout << "}";
        }
        std::cout << "]}\n";
        return 0;
    }

    std::cout << "Project: " << args.file << "  (FlatCAM " << project.version << ")\n";
    for (const fc::ProjectObject& obj : project.objects) {
        double area = 0.0;
        for (const fc::Geom& g : obj.solids) area += g.area();
        std::cout << "  [" << obj.kind << "] " << obj.name << "  (" << fc::toString(obj.units)
                  << ", " << obj.solids.size() << " polígonos, área " << area << ")\n";
    }

    if (!args.extractDir.empty()) {
        for (std::size_t i = 0; i < project.objects.size(); ++i) {
            const fc::ProjectObject& obj = project.objects[i];
            const std::string base = args.extractDir + "/" + obj.name;
            for (std::size_t k = 0; k < obj.solids.size(); ++k) {
                std::ofstream out(base + "_solid" + std::to_string(k) + ".wkt");
                if (out) out << obj.solids[k].toWkt(9) << "\n";
            }
        }
        std::cout << "  WKT extraído en " << args.extractDir << "\n";
    }
    return 0;
}

/**
 * @brief Calcula el aislamiento de una capa de cobre y exporta su G-code.
 *
 * Parsea el Gerber y llama a `isolateRest` si se pidió `--rest` con un diámetro
 * previo positivo; en caso contrario a `isolateCopper`. Sin `-o` imprime
 * estadísticas; con `-o` convierte cada recorrido en una polilínea, aplica
 * opcionalmente `optimizePathOrder` y escribe el programa con `pathsGCode`.
 *
 * @param args Opciones de aislamiento, G-code y salida.
 * @return 0 en éxito; 1 si no puede escribir el archivo de salida.
 *
 * @note Los recorridos se obtienen con `Geom::coords()`; los que tienen menos
 *       de 2 puntos se descartan antes de generar el programa.
 * @note El JSON informa también `not_isolated`, útil para diagnosticar rest
 *       machining.
 * @warning Las unidades del G-code dependen de `--units`, pero la geometría ya
 *       viene escalada por el parser, así que no hay conversión adicional al
 *       escribir: si el Gerber está en pulgadas y se pide `--units mm`, el
 *       programa queda en mm.
 */
int runIsolate(const Args& args) {
    fc::GerberOptions gopt;
    gopt.appUnits = args.units;
    gopt.legacyRectStroke = args.compat;
    const fc::GerberResult gerber = fc::GerberParser(gopt).parseFile(args.file);

    fc::IsolationResult iso;
    if (args.rest && args.previousToolDia > 0.0) {
        iso = fc::isolateRest(gerber.solid, args.previousToolDia, args.isolation);
    } else {
        iso = fc::isolateCopper(gerber.solid, args.isolation);
    }

    if (args.json) {
        std::cout << "{\"type\":\"isolation\",\"file\":\"" << args.file << "\",\"paths\":"
                  << iso.paths.size() << ",\"not_isolated\":" << iso.notIsolated.size()
                  << ",\"passes\":" << args.isolation.passes
                  << ",\"tool_dia\":" << args.isolation.toolDiameter << ",\"length\":";
        printNumber(iso.totalLength);
        std::cout << ",\"bounds\":";
        printBBox(iso.bounds);
        std::cout << "}\n";
        return 0;
    }

    if (args.output.empty() || args.output == "-") {
        std::cout << "Isolation: " << args.file << "\n"
                  << "  tool      : Ø" << args.isolation.toolDiameter << " "
                  << fc::toString(args.units) << "\n"
                  << "  passes    : " << args.isolation.passes << "\n"
                  << "  paths     : " << iso.paths.size() << "\n"
                  << "  length    : " << iso.totalLength << " " << fc::toString(args.units) << "\n";
        if (iso.bounds.valid) {
            std::cout << "  bounds    : " << iso.bounds.xmin << ", " << iso.bounds.ymin << " .. "
                      << iso.bounds.xmax << ", " << iso.bounds.ymax << "\n";
        }
        return 0;
    }

    fc::PathJob job;
    job.bounds = gerber.bounds;
    job.toolDiameter = args.isolation.toolDiameter;
    for (const fc::Geom& path : iso.paths) {
        fc::Ring ring = path.coords();
        if (ring.size() < 2) continue;
        std::vector<fc::Pt> points;
        points.reserve(ring.size());
        for (const fc::Pt& p : ring) points.push_back(p);
        job.paths.push_back(std::move(points));
    }

    if (args.optimize) fc::optimizePathOrder(job);
    const std::string code = fc::pathsGCode(job, args.gcode);
    std::ofstream out(args.output, std::ios::binary);
    if (!out) {
        std::cerr << "cannot write " << args.output << "\n";
        return 1;
    }
    out << code;
    std::cerr << "wrote " << args.output << " (" << job.paths.size() << " paths, "
              << iso.totalLength << " " << fc::toString(args.units) << ")\n";
    return 0;
}

namespace {

/**
 * @brief Aplana geometría de líneas en una lista de polilíneas.
 *
 * Acepta `LineString`/`LinearRing` directamente y recurre sobre
 * `MultiLineString` y `GeometryCollection`. Es el equivalente local del helper
 * de `ncc.cpp`, usado aquí para los recorridos "follow" del Gerber.
 *
 * @param geom Geometría a recorrer.
 * @param out Lista de polilíneas de salida.
 *
 * @note Se ignoran las geometrías vacías, inválidas y los tipos que no son de
 *       línea (los polígonos no se convierten a su contorno).
 */
void collectLineStrings(const fc::Geom& geom, std::vector<std::vector<fc::Pt>>& out) {
    if (!geom.valid() || geom.empty()) return;
    const int type = geom.typeId();
    if (type == GEOS_LINESTRING || type == GEOS_LINEARRING) {
        fc::Ring ring = geom.coords();
        if (ring.size() >= 2) out.emplace_back(ring.begin(), ring.end());
        return;
    }
    if (type == GEOS_MULTILINESTRING || type == GEOS_GEOMETRYCOLLECTION) {
        const int n = geom.numGeometries();
        for (int i = 0; i < n; ++i) collectLineStrings(geom.geometryN(i), out);
    }
}

}  // namespace

/**
 * @brief Genera G-code de fresado siguiendo los recorridos del Gerber.
 *
 * Extrae las polilíneas de `gerber.follows` con `collectLineStrings` y, si se
 * pidió salida, las mecaniza con `pathsGCode` (optimizando el orden antes si
 * corresponde). Sin `-o` sólo informa cuántos recorridos hay.
 *
 * @param args Opciones de G-code y salida.
 * @return 0 en éxito; 1 si no puede escribir el archivo.
 *
 * @note `job.bounds` usa los límites del Gerber completo, incluidos los
 *       sólidos, no sólo los recorridos.
 * @note El diámetro de herramienta se toma de `--tool-dia`, compartido con el
 *       aislamiento; sólo afecta a los comentarios del programa.
 */
int runMill(const Args& args) {
    fc::GerberOptions gopt;
    gopt.appUnits = args.units;
    gopt.legacyRectStroke = args.compat;
    const fc::GerberResult gerber = fc::GerberParser(gopt).parseFile(args.file);

    std::vector<std::vector<fc::Pt>> lines;
    for (const fc::Geom& g : gerber.follows) collectLineStrings(g, lines);

    if (args.json) {
        std::cout << "{\"type\":\"mill\",\"paths\":" << lines.size() << "}\n";
        return 0;
    }

    if (args.output.empty() || args.output == "-") {
        std::cout << "Mill: " << args.file << "\n  recorridos: " << lines.size() << "\n";
        return 0;
    }

    fc::PathJob job;
    job.paths = std::move(lines);
    job.bounds = gerber.bounds;
    job.toolDiameter = args.isolation.toolDiameter;
    if (args.optimize) fc::optimizePathOrder(job);

    const std::string code = fc::pathsGCode(job, args.gcode);
    std::ofstream out(args.output, std::ios::binary);
    if (!out) {
        std::cerr << "cannot write " << args.output << "\n";
        return 1;
    }
    out << code;
    std::cerr << "wrote " << args.output << " (" << job.paths.size() << " paths)\n";
    return 0;
}

/**
 * @brief Exporta un Gerber a DXF mediante `fc::exportDxf`.
 * @param args Debe incluir `file` y `-o out.dxf`.
 * @return 0 en éxito; 2 si falta la salida.
 *
 * @note A diferencia de `runSvg`, no admite escribir en la salida estándar:
 *       `exportDxf` necesita una ruta de archivo.
 */
int runDxf(const Args& args) {
    fc::GerberOptions gopt;
    gopt.appUnits = args.units;
    const fc::GerberResult gerber = fc::GerberParser(gopt).parseFile(args.file);
    if (args.output.empty()) {
        std::cerr << "dxf: -o out.dxf is required\n";
        return 2;
    }
    fc::exportDxf(args.output, gerber.solids, gerber.follows);
    std::cerr << "wrote " << args.output << "\n";
    return 0;
}

/**
 * @brief Importa un SVG o DXF y lo convierte a Gerber.
 *
 * @param args `file` y `-o out.gbr`.
 * @param svg `true` para SVG, `false` para DXF.
 * @return 0 en éxito; 1 si el importador lanza una excepción; 2 si falta `-o`.
 *
 * @note El texto de la excepción se imprime tal cual por `stderr`, sin prefijo,
 *       para no ocultar el diagnóstico del parser.
 * @note La geometría se escribe con `writeGerber` sin recorridos "follow": el
 *       resultado es sólo cobre sólido.
 */
int runImport(const Args& args, bool svg) {
    if (args.output.empty()) {
        std::cerr << "import: -o out.gbr is required\n";
        return 2;
    }
    std::vector<fc::Geom> geoms;
    try {
        geoms = svg ? fc::importSvg(args.file) : fc::importDxf(args.file);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    fc::writeGerber(args.output, geoms, {}, args.units);
    std::cerr << "wrote " << args.output << " (" << geoms.size() << " objetos)\n";
    return 0;
}

/**
 * @brief Compone varias capas en un SVG tipo "film" con marco opcional.
 *
 * Parsea cada entrada como Gerber o Excellon (según la extensión), une todos los
 * sólidos, calcula un marco expandido por el margen y exporta el SVG. Con
 * `--negative` el contenido es `marco − cobre` (película negativa); sin esa
 * bandera se dibujan el cobre unido y el rectángulo del marco.
 *
 * @param args Entradas, `--margin`, `--negative` y `-o out.svg`.
 * @return 0 en éxito; 1 si la geometría resultante está vacía; 2 si faltan
 *         entradas o la salida.
 *
 * @note Si el margen es 0 se usa 2 mm por defecto para que el marco no quede
 *       pegado al cobre.
 * @note Los recorridos "follow" se acumulan y se pasan a `exportSvg` aparte de
 *       los sólidos.
 * @warning En modo negativo los recorridos "follow" se siguen dibujando, de
 *          modo que pueden aparecer trazos sobre la zona que debería quedar
 *          transparente.
 */
int runFilm(const Args& args) {
    if (args.inputs.empty()) {
        std::cerr << "film: at least one input is required\n";
        return 2;
    }

    std::vector<fc::Geom> solids;
    std::vector<fc::Geom> follows;
    for (const std::string& input : args.inputs) {
        if (isExcellonPath(input)) {
            fc::ExcellonOptions eopt;
            eopt.appUnits = args.units;
            const fc::ExcellonResult drl = fc::ExcellonParser(eopt).parseFile(input);
            solids.insert(solids.end(), drl.geometry.begin(), drl.geometry.end());
        } else {
            fc::GerberOptions gopt;
            gopt.appUnits = args.units;
            const fc::GerberResult gerber = fc::GerberParser(gopt).parseFile(input);
            solids.insert(solids.end(), gerber.solids.begin(), gerber.solids.end());
            follows.insert(follows.end(), gerber.follows.begin(), gerber.follows.end());
        }
    }

    const fc::Geom merged = fc::Geom::unionOf(solids);
    const fc::BBox b = merged.bounds();
    if (!b.valid) {
        std::cerr << "film: empty geometry\n";
        return 1;
    }
    const double margin = args.cutout.margin == 0.0 ? 2.0 : args.cutout.margin;
    const fc::BBox frame = b.expanded(margin);
    const fc::Geom box = fc::Geom::box(frame.xmin, frame.ymin, frame.xmax, frame.ymax);

    std::vector<fc::Geom> outSolids;
    if (args.negative) {
        outSolids.push_back(box.difference(merged));
    } else {
        outSolids.push_back(merged);
        outSolids.push_back(fc::Geom::line({
            {frame.xmin, frame.ymin}, {frame.xmax, frame.ymin},
            {frame.xmax, frame.ymax}, {frame.xmin, frame.ymax}, {frame.xmin, frame.ymin}}));
    }

    if (args.output.empty()) {
        std::cerr << "film: -o out.svg is required\n";
        return 2;
    }
    fc::exportSvg(args.output, outSolids, follows, frame);
    std::cerr << "wrote " << args.output << "\n";
    return 0;
}

/**
 * @brief Exporta un único Gerber a SVG con sus sólidos y recorridos.
 * @param args `file` y `-o out.svg`.
 * @return 0 en éxito; 2 si falta la salida.
 *
 * @note Usa `gerber.bounds` como viewBox, por lo que los trazos que sobresalgan
 *       del bounding box calculado por el parser quedarían recortados.
 */
int runSvg(const Args& args) {
    fc::GerberOptions gopt;
    gopt.appUnits = args.units;
    const fc::GerberResult gerber = fc::GerberParser(gopt).parseFile(args.file);
    if (args.output.empty()) {
        std::cerr << "svg: -o out.svg is required\n";
        return 2;
    }
    fc::exportSvg(args.output, gerber.solids, gerber.follows, gerber.bounds);
    std::cerr << "wrote " << args.output << "\n";
    return 0;
}

/**
 * @brief Genera una grilla de puntos de cobre (thieving) y la exporta.
 *
 * Calcula los centros con `thievingPoints` sobre el bounding box del cobre y,
 * si se pidió salida, escribe un Gerber mínimo con un único flash `D03` por
 * punto usando la apertura `D10` de diámetro `--tool-dia`.
 *
 * @param args `file`, `--tool-dia`, `--spacing`, `--clearance`, `--margin`,
 *             `--json` y `-o out.gbr`.
 * @return 0 en éxito; 1 si no puede escribir la salida.
 *
 * @note Sin `-o` y sin `--json` sólo informa la cantidad de puntos.
 * @note Los puntos se emiten con formato fijo `%FSLAX34Y34*%` (4 decimales) y
 *       se redondean a 1/10000 mm con `std::llround`.
 * @warning El encabezado declara siempre `%MOMM*%` y multiplica por 10000, sin
 *          mirar `--units`: con `--units in` las coordenadas siguen escritas
 *          como si fueran milímetros.
 * @warning El área de siembra es exactamente `gerber.bounds`; el margen de
 *          `ThievingOptions` se aplica hacia dentro, por lo que no se agregan
 *          puntos fuera del cobre.
 */
int runThieving(const Args& args) {
    fc::GerberOptions gopt;
    gopt.appUnits = args.units;
    const fc::GerberResult gerber = fc::GerberParser(gopt).parseFile(args.file);

    const std::vector<fc::Pt> points =
        fc::thievingPoints(gerber.solid, gerber.bounds, args.thieving);

    if (args.json) {
        std::cout << "{\"type\":\"thieving\",\"dots\":" << points.size() << "}\n";
        return 0;
    }
    if (args.output.empty()) {
        std::cout << "Thieving: " << args.file << "\n  puntos: " << points.size() << "\n";
        return 0;
    }

    std::ostringstream out;
    out << "G04 Copper thieving generated by FlatCAM C++*\n";
    out << "%FSLAX34Y34*%\n%MOMM*%\n";
    out << "%ADD10C," << args.thieving.dotDiameter << "*%\n";
    out << "D10*\n";
    for (const fc::Pt& p : points) {
        out << "X" << static_cast<long long>(std::llround(p.x * 10000.0)) << "Y"
            << static_cast<long long>(std::llround(p.y * 10000.0)) << "D03*\n";
    }
    out << "M02*\n";

    std::ofstream file(args.output, std::ios::binary);
    if (!file) {
        std::cerr << "cannot write " << args.output << "\n";
        return 1;
    }
    file << out.str();
    std::cerr << "wrote " << args.output << " (" << points.size() << " dots)\n";
    return 0;
}

/**
 * @brief Genera el relleno por contornos paralelos de una capa de cobre.
 *
 * Llama a `paintArea` y, si hay salida, arma un `PathJob`, ejecuta
 * opcionalmente la optimización de orden y escribe el G-code.
 *
 * @param args `file`, `--tool-dia`, `--step-over`/`--superposition`, `--margin`,
 *             `--preprocessor`, `--json` y `-o out.nc`.
 * @return 0 en éxito; 1 si no puede escribir la salida.
 *
 * @note La impresión de estadísticas muestra `stepOver` como porcentaje.
 * @warning La optimización de orden se invoca dos veces seguidas (bloque
 *          duplicado en la implementación): el segundo recorrido no aporta
 *          mejoras y cuesta O(n²) adicional sobre todos los recorridos.
 */
int runPaint(const Args& args) {
    fc::GerberOptions gopt;
    gopt.appUnits = args.units;
    gopt.legacyRectStroke = args.compat;
    const fc::GerberResult gerber = fc::GerberParser(gopt).parseFile(args.file);

    const fc::PaintResult painted = fc::paintArea(gerber.solid, args.paint);

    if (args.json) {
        std::cout << "{\"type\":\"paint\",\"file\":\"" << args.file << "\",\"paths\":"
                  << painted.paths.size() << ",\"length\":";
        printNumber(painted.totalLength);
        std::cout << ",\"bounds\":";
        printBBox(painted.bounds);
        std::cout << "}\n";
        return 0;
    }

    if (args.output.empty() || args.output == "-") {
        std::cout << "Paint: " << args.file << "\n"
                  << "  tool      : Ø" << args.paint.toolDiameter << " " << fc::toString(args.units)
                  << "\n"
                  << "  step-over : " << args.paint.stepOver * 100.0 << " %\n"
                  << "  recorridos: " << painted.paths.size() << "\n"
                  << "  longitud  : " << painted.totalLength << " " << fc::toString(args.units)
                  << "\n";
        return 0;
    }

    fc::PathJob job;
    job.bounds = painted.bounds;
    job.toolDiameter = args.paint.toolDiameter;
    for (const fc::Geom& path : painted.paths) {
        fc::Ring ring = path.coords();
        if (ring.size() < 2) continue;
        job.paths.emplace_back(ring.begin(), ring.end());
    }
    if (args.optimize) fc::optimizePathOrder(job);
    const std::string code = fc::pathsGCode(job, args.gcode);
    std::ofstream out(args.output, std::ios::binary);
    if (!out) {
        std::cerr << "cannot write " << args.output << "\n";
        return 1;
    }
    out << code;
    std::cerr << "wrote " << args.output << " (" << job.paths.size() << " paths)\n";
    return 0;
}

/**
 * @brief Calcula el despeje de cobre (NCC) y exporta su G-code.
 *
 * Construye el área de trabajo desde `--area` (guardada en `cutout.area`) si
 * existe; si no, `clearCopper` la deriva del bounding box del cobre. Con `-o`
 * convierte los recorridos a polilíneas, optimiza opcionalmente y escribe el
 * programa.
 *
 * @param args Opciones de NCC, G-code y salida.
 * @return 0 en éxito; 1 si no puede escribir la salida.
 *
 * @note `--strategy paint` delega en el relleno por contornos; `hatch` usa la
 *       implementación de líneas paralelas.
 * @note La superposición de FlatCAM se traduce a paso con
 *       `--superposition`: `paso = (100 − superposición) / 100`.
 */
int runNcc(const Args& args) {
    fc::GerberOptions gopt;
    gopt.appUnits = args.units;
    gopt.legacyRectStroke = args.compat;
    const fc::GerberResult gerber = fc::GerberParser(gopt).parseFile(args.file);

    fc::Geom area;
    if (args.cutout.area) {
        area = fc::Geom::box(args.cutout.area->xmin, args.cutout.area->ymin,
                             args.cutout.area->xmax, args.cutout.area->ymax);
    }
    const fc::NccResult nccResult = fc::clearCopper(gerber.solid, area, args.ncc);

    if (args.json) {
        std::cout << "{\"type\":\"ncc\",\"file\":\"" << args.file << "\",\"paths\":"
                  << nccResult.paths.size() << ",\"length\":";
        printNumber(nccResult.totalLength);
        std::cout << ",\"bounds\":";
        printBBox(nccResult.bounds);
        std::cout << "}\n";
        return 0;
    }

    if (args.output.empty() || args.output == "-") {
        std::cout << "NCC: " << args.file << "\n"
                  << "  tool      : Ø" << args.ncc.toolDiameter << " " << fc::toString(args.units)
                  << "\n"
                  << "  step-over : " << args.ncc.stepOver * 100.0 << " %\n"
                  << "  clearance : " << args.ncc.clearance << "\n"
                  << "  recorridos: " << nccResult.paths.size() << "\n"
                  << "  longitud  : " << nccResult.totalLength << " " << fc::toString(args.units)
                  << "\n";
        if (nccResult.bounds.valid) {
            std::cout << "  bounds    : " << nccResult.bounds.xmin << ", " << nccResult.bounds.ymin
                      << " .. " << nccResult.bounds.xmax << ", " << nccResult.bounds.ymax << "\n";
        }
        return 0;
    }

    fc::PathJob job;
    job.bounds = nccResult.bounds;
    job.toolDiameter = args.ncc.toolDiameter;
    for (const fc::Geom& path : nccResult.paths) {
        fc::Ring ring = path.coords();
        if (ring.size() < 2) continue;
        job.paths.emplace_back(ring.begin(), ring.end());
    }

    if (args.optimize) fc::optimizePathOrder(job);
    const std::string code = fc::pathsGCode(job, args.gcode);
    std::ofstream out(args.output, std::ios::binary);
    if (!out) {
        std::cerr << "cannot write " << args.output << "\n";
        return 1;
    }
    out << code;
    std::cerr << "wrote " << args.output << " (" << job.paths.size() << " paths)\n";
    return 0;
}

/**
 * @brief Calcula el corte de contorno con puentes y exporta su G-code.
 *
 * Llama a `cutoutGeometry` con las opciones de corte (herramienta, margen,
 * interno/externo, puentes, área) y, si hay salida, escribe el programa con
 * multipasada según `--depth-per-pass`.
 *
 * @param args Opciones de corte, G-code y salida.
 * @return 0 en éxito; 1 si no puede escribir la salida.
 *
 * @note `--internal` invierte el lado del corte; el valor por defecto es
 *       externo.
 * @note El informe de texto incluye el tipo de corte elegido y la longitud
 *       total de los tramos (sin contar los rápidos).
 */
int runCutout(const Args& args) {
    fc::GerberOptions gopt;
    gopt.appUnits = args.units;
    gopt.legacyRectStroke = args.compat;
    const fc::GerberResult gerber = fc::GerberParser(gopt).parseFile(args.file);

    const fc::CutoutResult cut = fc::cutoutGeometry(gerber.solid, args.cutout);

    if (args.json) {
        std::cout << "{\"type\":\"cutout\",\"file\":\"" << args.file << "\",\"paths\":"
                  << cut.paths.size() << ",\"length\":";
        printNumber(cut.totalLength);
        std::cout << ",\"bounds\":";
        printBBox(cut.bounds);
        std::cout << "}\n";
        return 0;
    }

    if (args.output.empty() || args.output == "-") {
        std::cout << "Cutout: " << args.file << "\n"
                  << "  tool      : Ø" << args.cutout.toolDiameter << " " << fc::toString(args.units)
                  << "\n"
                  << "  margen    : " << args.cutout.margin << "\n"
                  << "  tipo      : " << (args.cutout.external ? "externo" : "interno") << "\n"
                  << "  recorridos: " << cut.paths.size() << "\n"
                  << "  longitud  : " << cut.totalLength << " " << fc::toString(args.units) << "\n";
        if (cut.bounds.valid) {
            std::cout << "  bounds    : " << cut.bounds.xmin << ", " << cut.bounds.ymin << " .. "
                      << cut.bounds.xmax << ", " << cut.bounds.ymax << "\n";
        }
        return 0;
    }

    fc::PathJob job;
    job.bounds = cut.bounds;
    job.toolDiameter = args.cutout.toolDiameter;
    for (const fc::Geom& path : cut.paths) {
        fc::Ring ring = path.coords();
        if (ring.size() < 2) continue;
        job.paths.emplace_back(ring.begin(), ring.end());
    }

    if (args.optimize) fc::optimizePathOrder(job);
    const std::string code = fc::pathsGCode(job, args.gcode);
    std::ofstream out(args.output, std::ios::binary);
    if (!out) {
        std::cerr << "cannot write " << args.output << "\n";
        return 1;
    }
    out << code;
    std::cerr << "wrote " << args.output << " (" << job.paths.size() << " paths, "
              << cut.totalLength << " " << fc::toString(args.units) << ")\n";
    return 0;
}

/**
 * @brief Convierte un Excellon en un programa de taladrado.
 *
 * Verifica que la entrada sea Excellon (por extensión), la parsea, construye el
 * `DrillJob` y valida el límite del TSP exacto cuando se pide `--tsp-exact`
 * (18 taladros por herramienta como máximo). Luego optimiza el orden si
 * corresponde y escribe el G-code en el archivo o en la salida estándar.
 *
 * @param args `file`, opciones de G-code, `--optimize`/`--tsp-exact` y `-o`.
 * @return 0 en éxito; 1 si no hay taladros/ranuras o falla la escritura; 2 si
 *         la extensión no es de Excellon o se supera el límite del TSP exacto.
 *
 * @note Sin `-o` (o con `-o -`) el programa se imprime por `stdout`, lo que
 *       permite encadenarlo con otras herramientas.
 * @note El mensaje final por `stderr` incluye herramientas, taladros y ranuras.
 * @warning `--tsp-exact` sólo valida el límite; no fuerza un solver distinto,
 *          porque `optimizeDrillOrder` ya usa Held-Karp hasta 18 puntos y
 *          heurísticas por encima.
 */
int runGCode(const Args& args) {
    if (!isExcellonPath(args.file)) {
        std::cerr << "gcode: this milestone only converts Excellon drilling files "
                     "(.drl/.xln/.exc/.ncd)\n";
        return 2;
    }

    fc::ExcellonOptions opt;
    opt.appUnits = args.units;
    const fc::ExcellonResult r = fc::ExcellonParser(opt).parseFile(args.file);
    if (r.drillCount == 0 && r.slotCount == 0) {
        std::cerr << "gcode: no drills or slots found\n";
        return 1;
    }

    fc::DrillJob job = fc::drillJobFromExcellon(r);
    if (args.tspExact) {
        for (const fc::DrillTool& tool : job.tools) {
            if (tool.holes.size() > 18) {
                std::cerr << "tsp-exact: T" << tool.number << " has " << tool.holes.size()
                          << " holes; exact TSP is limited to 18\n";
                return 2;
            }
        }
    }
    if (args.optimize) fc::optimizeDrillOrder(job);
    const std::string code = fc::drillingGCode(job, args.gcode);

    if (args.output.empty() || args.output == "-") {
        std::cout << code;
        return 0;
    }

    std::ofstream out(args.output, std::ios::binary);
    if (!out) {
        std::cerr << "cannot write " << args.output << "\n";
        return 1;
    }
    out << code;
    std::cerr << "wrote " << args.output << " (" << job.tools.size() << " tools, "
              << r.drillCount << " drills, " << r.slotCount << " slots)\n";
    return 0;
}

}  // namespace

/**
 * @brief Punto de entrada: interpreta los argumentos y despacha el subcomando.
 *
 * Envuelve `parseArgs` y el despacho en un `try/catch` que convierte cualquier
 * excepción de la biblioteca estándar o del parser en un mensaje `error: ...`
 * con código de salida 1.
 *
 * @param argc Cantidad de argumentos.
 * @param argv Vector de argumentos.
 * @return 0 en éxito; 1 si una excepción interrumpe la ejecución; 2 si la línea
 *         de comandos es inválida o el subcomando es desconocido.
 *
 * @note El subcomando `tcl` se delega en `fc::runTclScript`, cuya
 *       implementación depende de si el build encontró libtcl.
 * @note Cada `runXxx` recibe la estructura completa de opciones; no hay
 *       validación cruzada entre el subcomando y las banderas, de modo que
 *       opciones irrelevantes se aceptan y se ignoran.
 */
int main(int argc, char** argv) {
    Args args;
    if (!parseArgs(argc, argv, args)) return usage();

    try {
        if (args.kind == "gerber") return runGerber(args);
        if (args.kind == "excellon") return runExcellon(args);
        if (args.kind == "gcode") return runGCode(args);
        if (args.kind == "isolate") return runIsolate(args);
        if (args.kind == "cutout") return runCutout(args);
        if (args.kind == "ncc") return runNcc(args);
        if (args.kind == "paint") return runPaint(args);
        if (args.kind == "mill") return runMill(args);
        if (args.kind == "svg") return runSvg(args);
        if (args.kind == "dxf") return runDxf(args);
        if (args.kind == "film") return runFilm(args);
        if (args.kind == "import-svg") return runImport(args, true);
        if (args.kind == "import-dxf") return runImport(args, false);
        if (args.kind == "thieving") return runThieving(args);
        if (args.kind == "project") return runProject(args);
        if (args.kind == "tcl") return fc::runTclScript(args.file);
        return usage();
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
