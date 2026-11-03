#ifndef FC_HAVE_TCL
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
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

namespace fc {

namespace {

std::string trim(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::vector<std::string> splitCommand(const std::string& line) {
    std::vector<std::string> out;
    std::string current;
    bool quoted = false;
    for (char c : line) {
        if (c == '"') {
            quoted = !quoted;
            continue;
        }
        if (!quoted && (c == ' ' || c == '\t')) {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

std::string substitute(const std::string& token, const std::map<std::string, std::string>& vars) {
    if (token.empty() || token[0] != '$') return token;
    const auto it = vars.find(token.substr(1));
    return it == vars.end() ? std::string() : it->second;
}

double toNumber(const std::vector<std::string>& tokens, std::size_t index, double fallback) {
    if (index >= tokens.size()) return fallback;
    try {
        return std::stod(tokens[index]);
    } catch (...) {
        return fallback;
    }
}

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

std::string describe(const ShellState& state) {
    std::ostringstream os;
    if (state.hasGerber) {
        os << "gerber: " << state.gerber.apertures.size() << " aperturas, área "
           << state.gerber.solid.area() << "\n";
    }
    if (state.hasExcellon) {
        os << "excellon: " << state.excellon.drillCount << " taladros, "
           << state.excellon.slotCount << " ranuras\n";
    }
    if (state.hasPathJob) os << "recorridos: " << state.paths.paths.size() << "\n";
    if (state.hasDrillJob) os << "taladros en job: " << state.drills.tools.size() << " herramientas\n";
    return os.str();
}

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

}  // namespace

int runTclScript(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "cannot open script: " << path << "\n";
        return 1;
    }

    ShellState state;
    std::map<std::string, std::string> vars;
    std::string line;

    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        std::vector<std::string> tokens = splitCommand(line);
        if (tokens.empty()) continue;
        const std::string command = tokens[0];

        try {
            if (command == "set") {
                if (tokens.size() < 2) continue;
                std::string value;
                for (std::size_t i = 2; i < tokens.size(); ++i) {
                    if (i > 2) value += " ";
                    value += substitute(tokens[i], vars);
                }
                vars[tokens[1]] = value;
            } else if (command == "puts") {
                std::string text;
                for (std::size_t i = 1; i < tokens.size(); ++i) {
                    if (i > 1) text += " ";
                    text += substitute(tokens[i], vars);
                }
                std::cout << text << "\n";
            } else if (command == "open_gerber") {
                if (tokens.size() < 2) continue;
                const std::string file = substitute(tokens[1], vars);
                state.gerber = GerberParser().parseFile(file);
                state.hasGerber = true;
                std::cout << "gerber: " << file << " (" << state.gerber.solid.area() << ")\n";
            } else if (command == "open_excellon") {
                if (tokens.size() < 2) continue;
                const std::string file = substitute(tokens[1], vars);
                state.excellon = ExcellonParser().parseFile(file);
                state.drills = drillJobFromExcellon(state.excellon);
                state.hasExcellon = true;
                state.hasDrillJob = true;
                state.hasPathJob = false;
                std::cout << "excellon: " << file << " (" << state.excellon.drillCount << ")\n";
            } else if (command == "isolate") {
                if (!state.hasGerber) throw std::runtime_error("no gerber loaded");
                IsolationOptions opt;
                opt.toolDiameter = toNumber(tokens, 1, 0.2);
                opt.passes = static_cast<int>(toNumber(tokens, 2, 1));
                opt.overlap = toNumber(tokens, 3, 0.0) / 100.0;
                const IsolationResult result = isolateCopper(state.gerber.solid, opt);
                setPaths(state, result.paths, result.bounds, opt.toolDiameter);
                std::cout << "isolate: " << result.paths.size() << " recorridos\n";
            } else if (command == "cutout") {
                if (!state.hasGerber) throw std::runtime_error("no gerber loaded");
                CutoutOptions opt;
                opt.toolDiameter = toNumber(tokens, 1, 1.0);
                opt.margin = toNumber(tokens, 2, 0.0);
                opt.tabCount = static_cast<int>(toNumber(tokens, 3, 0));
                opt.tabWidth = toNumber(tokens, 4, 0.0);
                const CutoutResult result = cutoutGeometry(state.gerber.solid, opt);
                setPaths(state, result.paths, result.bounds, opt.toolDiameter);
                std::cout << "cutout: " << result.paths.size() << " recorridos\n";
            } else if (command == "ncc") {
                if (!state.hasGerber) throw std::runtime_error("no gerber loaded");
                NccOptions opt;
                opt.toolDiameter = toNumber(tokens, 1, 0.5);
                opt.stepOver = toNumber(tokens, 2, 50.0) / 100.0;
                const BBox b = state.gerber.bounds.expanded(1.0);
                const Geom area = b.valid ? Geom::box(b.xmin, b.ymin, b.xmax, b.ymax) : Geom();
                const NccResult result = clearCopper(state.gerber.solid, area, opt);
                setPaths(state, result.paths, result.bounds, opt.toolDiameter);
                std::cout << "ncc: " << result.paths.size() << " recorridos\n";
            } else if (command == "paint") {
                if (!state.hasGerber) throw std::runtime_error("no gerber loaded");
                PaintOptions opt;
                opt.toolDiameter = toNumber(tokens, 1, 0.1);
                opt.stepOver = toNumber(tokens, 2, 50.0) / 100.0;
                const PaintResult result = paintArea(state.gerber.solid, opt);
                setPaths(state, result.paths, result.bounds, opt.toolDiameter);
                std::cout << "paint: " << result.paths.size() << " recorridos\n";
            } else if (command == "export_gcode") {
                if (tokens.size() < 2) continue;
                const std::string file = substitute(tokens[1], vars);
                GCodeOptions opt;
                std::string code;
                if (state.hasDrillJob) {
                    code = drillingGCode(state.drills, opt);
                } else if (state.hasPathJob) {
                    code = pathsGCode(state.paths, opt);
                } else {
                    throw std::runtime_error("nothing to export");
                }
                std::ofstream out(file, std::ios::binary);
                if (!out) throw std::runtime_error("cannot write " + file);
                out << code;
                std::cout << "wrote " << file << "\n";
            } else if (command == "info") {
                std::cout << describe(state);
            } else if (command == "quit" || command == "exit") {
                break;
            } else {
                std::cerr << "unknown command: " << command << "\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "error in '" << command << "': " << e.what() << "\n";
        }
    }
    return 0;
}

}  // namespace fc

#endif  // FC_HAVE_TCL
