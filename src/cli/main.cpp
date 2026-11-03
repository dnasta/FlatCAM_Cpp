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
int runTclScript(const std::string& path);
}

namespace {

void printNumber(double v) {
    if (std::fabs(v) < 1e-12) v = 0.0;
    std::cout << std::setprecision(12) << v;
}

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

bool isExcellonPath(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == "drl" || ext == "xln" || ext == "exc" || ext == "ncd";
}

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
        args.gcode.flavor = fc::GCodeFlavor::Grbl;
    } else {
        args.gcode.flavor = fc::GCodeFlavor::Default;
    }
    return true;
}

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
