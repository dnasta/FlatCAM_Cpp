#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "flatcam/geometry.hpp"
#include "flatcam/units.hpp"

namespace fc {

enum class GCodeFlavor {
    Default,          // MACH3
    DefaultNoM6,      // MACH3 sin M6
    Grbl,             // GRBL 1.1
    GrblNoM6,         // GRBL 1.1 sin M6
    GrblLaser,        // GRBL láser (M03/M05)
    Marlin,           // Marlin
    MarlinLaserFan,   // Marlin láser en pin ventilador (M106/M107)
    MarlinLaserSpindle, // Marlin láser en pin spindle (M3/M5)
    Repetier,         // Repetier
    Berta,            // Berta CNC
    Isel,             // ISEL CNC
    IselIcp,          // ISEL ICP CNC
    Roland,           // Roland MDX-20
    ToolchangeManual, // MACH3 cambio manual
    ToolchangeProbe,  // MACH3 con sonda
    ZLaser,          // láser controlado por Z
    Hpgl,            // plotter HPGL
    LineXyz,         // G-code con Z en cada movimiento lineal
    ToolchangeCustom // toolchange definido por el usuario
};

struct GCodeOptions {
    Units units = Units::Mm;
    int decimals = 4;
    int feedDecimals = 2;

    double toolDiameter = 0.0;  // informational, used by the GUI exports
    double feedXY = 200.0;
    double feedZ = 60.0;
    double zMove = 2.0;   // travel height
    double zCut = -1.5;
    double zStart = 5.0;
    double zEnd = 5.0;
    double depthPerPass = 0.0;  // 0 = single plunge to zCut

    double spindleSpeed = 1000.0;
    bool spindleCw = true;

    bool includeToolChange = true;
    double zToolchange = 10.0;
    std::optional<Pt> toolchangeXY;
    std::optional<Pt> endXY;

    bool connectPaths = false;       // join close paths without retracting
    double connectTolerance = 0.5;   // mm
    double dwellTime = 0.0;
    double laserPower = 300.0;
    double zFocus = 0.0;
    GCodeFlavor flavor = GCodeFlavor::Default;
    // Template for ToolchangeCustom. Placeholders: {tool}, {diameter}, {z}, {x}, {y}.
    std::string customToolchange;
};

struct DrillTool {
    int number = 1;
    double diameter = 0.8;
    std::vector<Pt> holes;
    std::vector<std::pair<Pt, Pt>> slotSegments;
    std::optional<double> zCut;          // per-tool override
    std::optional<double> feedZ;         // per-tool override
    std::optional<double> feedXY;        // per-tool override (slots)
    std::optional<double> spindleSpeed;  // per-tool override
    std::optional<double> zMove;         // per-tool override (travel)
    std::optional<double> dwell;         // per-tool override
    double offsetX = 0.0;                // drill position offset
    double offsetY = 0.0;
};

struct DrillJob {
    std::vector<DrillTool> tools;
    BBox bounds;
};

enum class DrillOrder { AsIs, NearestNeighbor };

// Total XY travel for the job (tool order and hole order as stored).
double drillTravelDistance(const DrillJob& job, const Pt& start = {0.0, 0.0});

// Reorders holes and slots per tool (nearest neighbour + 2-opt) to reduce
// rapids.
void optimizeDrillOrder(DrillJob& job, DrillOrder order = DrillOrder::NearestNeighbor,
                        const Pt& start = {0.0, 0.0});


struct PasteJob {
    std::vector<Pt> points;  // pad centres
    std::vector<double> padSizes;       // optional, parallel to points
    std::vector<int> nozzles;           // optional, index into nozzleDiameters
    std::vector<double> nozzleDiameters;
    std::vector<double> nozzleDwells;
    double nozzleDiameter = 0.5;
    BBox bounds;
    bool reverse = false;
};

// G-code for solder-paste dispensing: travel, lower, dwell, retract.
std::string pasteGCode(const PasteJob& job, const GCodeOptions& options);

struct PathJob {
    std::vector<std::vector<Pt>> paths;
    BBox bounds;
    double toolDiameter = 0.0;
};

// Reorders the paths of a job by nearest start point (greedy + 2-opt).
void optimizePathOrder(PathJob& job, const Pt& start = {0.0, 0.0});

// Generates a drilling G-code program compatible with FlatCAM's preprocessor
// layout (header comments, tool changes, per-hole plunge/retract).
std::string drillingGCode(const DrillJob& job, const GCodeOptions& options);

// Generates G-code for a geometry job (isolation, cutout, follow...).
std::string pathsGCode(const PathJob& job, const GCodeOptions& options);

struct PathToolJob {
    int number = 1;
    double diameter = 0.0;
    GCodeOptions options;
    std::vector<std::vector<Pt>> paths;
    BBox bounds;
};

// G-code for a geometry operation that uses several tools (tool changes).
std::string pathsGCodeMulti(const std::vector<PathToolJob>& tools, const GCodeOptions& header);

// Builds a drilling job from a parsed Excellon object (app units).
DrillJob drillJobFromExcellon(const struct ExcellonResult& result);

}  // namespace fc
