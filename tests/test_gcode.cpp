/**
 * @file test_gcode.cpp
 * @brief Pruebas de generación de G-code, preprocesadores y multiherramienta.
 *
 * Cada `TEST()` describe un caso verificable; los nombres siguen el formato
 * `Suite.Caso` para filtrar con `--gtest_filter`.
 */
#include <gtest/gtest.h>

#include <string>

#include "flatcam/gcode.hpp"

using namespace fc;

namespace {

DrillJob sampleJob() {
    DrillJob job;
    DrillTool t1;
    t1.number = 1;
    t1.diameter = 0.8;
    t1.holes = {{10.0, 10.0}, {20.0, 10.0}};

    DrillTool t2;
    t2.number = 2;
    t2.diameter = 1.2;
    t2.holes = {{30.0, 20.0}};
    t2.slotSegments = {{{30.0, 20.0}, {40.0, 20.0}}};

    job.tools = {t1, t2};
    job.bounds = {9.6, 9.6, 40.6, 20.6};
    return job;
}

std::size_t countOccurrences(const std::string& haystack, const std::string& needle) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

}  // namespace

TEST(GCode, DrillingHeaderAndTools) {
    GCodeOptions opt;
    opt.units = Units::Mm;
    const std::string code = drillingGCode(sampleJob(), opt);

    EXPECT_NE(code.find("G21"), std::string::npos);
    EXPECT_NE(code.find("G90"), std::string::npos);
    EXPECT_NE(code.find("M03 S1000"), std::string::npos);
    EXPECT_NE(code.find("(Tool: 1 -> Dia: 0.8000)"), std::string::npos);
    EXPECT_NE(code.find("T1\n"), std::string::npos);
    EXPECT_NE(code.find("T2\n"), std::string::npos);
    EXPECT_NE(code.find("X10.0000 Y10.0000"), std::string::npos);
    EXPECT_NE(code.find("G01 Z-1.5000 F60.00"), std::string::npos);
    EXPECT_NE(code.find("G01 X40.0000 Y20.0000 F200.00"), std::string::npos);
    EXPECT_NE(code.find("M05"), std::string::npos);

    // Two drills for T1, one drill + one slot for T2 = 4 plunges.
    EXPECT_EQ(countOccurrences(code, "G01 Z-1.5000"), std::size_t{4});
}

TEST(GCode, InchUnitsAndOverrides) {
    GCodeOptions opt;
    opt.units = Units::Inch;
    opt.zCut = -0.06;
    opt.decimals = 4;

    DrillJob job;
    DrillTool tool;
    tool.number = 3;
    tool.diameter = 0.04;
    tool.holes = {{1.0, 2.0}};
    tool.zCut = -0.08;
    job.tools = {tool};

    const std::string code = drillingGCode(job, opt);
    EXPECT_NE(code.find("G20"), std::string::npos);
    EXPECT_NE(code.find("G01 Z-0.0800"), std::string::npos);
}

TEST(GCode, GrblFlavorHasNoMach3Pause) {
    GCodeOptions opt;
    opt.flavor = GCodeFlavor::Grbl;
    const std::string code = drillingGCode(sampleJob(), opt);
    EXPECT_NE(code.find("GRBL"), std::string::npos);
    EXPECT_EQ(code.find("M6\n"), std::string::npos);
    EXPECT_EQ(code.find("M0\n"), std::string::npos);
}

TEST(GCode, EmptyToolsAreSkipped) {
    DrillJob job;
    DrillTool empty;
    empty.number = 1;
    empty.diameter = 0.5;
    job.tools = {empty};
    const std::string code = drillingGCode(job, GCodeOptions{});
    EXPECT_EQ(code.find("T1\n"), std::string::npos);
    EXPECT_NE(code.find("M05"), std::string::npos);
}

TEST(GCode, MultiDepthPathJob) {
    GCodeOptions opt;
    opt.zCut = -1.0;
    opt.depthPerPass = 0.4;

    PathJob job;
    job.toolDiameter = 1.0;
    job.paths = {{{0.0, 0.0}, {10.0, 0.0}}};

    const std::string code = pathsGCode(job, opt);
    EXPECT_EQ(countOccurrences(code, "G01 Z-0.4000"), std::size_t{1});
    EXPECT_EQ(countOccurrences(code, "G01 Z-0.8000"), std::size_t{1});
    EXPECT_EQ(countOccurrences(code, "G01 Z-1.0000"), std::size_t{1});
    EXPECT_EQ(countOccurrences(code, "G01 X10.0000 Y0.0000"), std::size_t{3});
}

TEST(GCode, NearestNeighbourOrderReducesTravel) {
    DrillJob job;
    DrillTool tool;
    tool.number = 1;
    tool.diameter = 0.8;
    tool.holes = {{0, 0}, {10, 10}, {0, 10}, {10, 0}};
    job.tools = {tool};

    const double before = drillTravelDistance(job, {0, 0});
    optimizeDrillOrder(job, DrillOrder::NearestNeighbor, {0, 0});
    const double after = drillTravelDistance(job, {0, 0});

    EXPECT_NEAR(after, 30.0, 1e-9);
    EXPECT_LT(after, before);
}

TEST(GCode, LaserFlavorUsesM03AndNoZ) {
    GCodeOptions opt;
    opt.flavor = GCodeFlavor::GrblLaser;
    opt.laserPower = 300.0;
    opt.feedXY = 500.0;

    PathJob job;
    job.toolDiameter = 0.1;
    job.paths = {{{0, 0}, {10, 0}}};
    const std::string code = pathsGCode(job, opt);

    EXPECT_NE(code.find("GRBL_laser"), std::string::npos);
    EXPECT_NE(code.find("M03 S300.0"), std::string::npos);
    EXPECT_EQ(code.find("G01 Z-"), std::string::npos);
    EXPECT_EQ(code.find("G00 Z2"), std::string::npos);
}

TEST(GCode, PathOrderOptimizationReducesRapids) {
    PathJob job;
    job.paths = {
        {{10, 0}, {11, 0}},
        {{0, 0}, {1, 0}},
        {{5, 0}, {6, 0}},
    };
    optimizePathOrder(job, {0, 0});

    EXPECT_NEAR(job.paths[0].front().x, 0.0, 1e-9);
    EXPECT_NEAR(job.paths[1].front().x, 5.0, 1e-9);
    EXPECT_NEAR(job.paths[2].front().x, 10.0, 1e-9);
}

TEST(GCode, MarlinFlavorHasToolChangePause) {
    GCodeOptions opt;
    opt.flavor = GCodeFlavor::Marlin;
    const std::string code = drillingGCode(sampleJob(), opt);
    EXPECT_NE(code.find("Marlin"), std::string::npos);
    EXPECT_NE(code.find("M6"), std::string::npos);
}

TEST(GCode, DefaultNoM6OmitsToolChangePause) {
    GCodeOptions opt;
    opt.flavor = GCodeFlavor::DefaultNoM6;
    const std::string code = drillingGCode(sampleJob(), opt);
    EXPECT_EQ(code.find("M6\n"), std::string::npos);
}

TEST(GCode, DrillOffsetShiftsHoles) {
    DrillJob job;
    DrillTool tool;
    tool.number = 1;
    tool.diameter = 0.8;
    tool.holes = {{10.0, 20.0}};
    tool.offsetX = 1.0;
    tool.offsetY = -2.0;
    job.tools = {tool};
    GCodeOptions opt;
    const std::string code = drillingGCode(job, opt);
    EXPECT_NE(code.find("X11.0000"), std::string::npos);
    EXPECT_NE(code.find("Y18.0000"), std::string::npos);
}

TEST(GCode, ConnectPathsAvoidsRetract) {
    GCodeOptions opt;
    opt.connectPaths = true;
    opt.connectTolerance = 0.5;
    PathJob job;
    job.paths = {{{0, 0}, {10, 0}}, {{10.1, 0}, {20, 0}}};
    const std::string code = pathsGCode(job, opt);
    EXPECT_EQ(countOccurrences(code, "G00 Z2.0000"), std::size_t{2});
}

TEST(GCode, PeckDrillingUsesMultipleDepths) {
    GCodeOptions opt;
    opt.depthPerPass = 0.5;
    opt.zCut = -2.0;
    DrillJob job;
    DrillTool tool;
    tool.number = 1;
    tool.diameter = 0.8;
    tool.holes = {{0.0, 0.0}};
    job.tools = {tool};
    const std::string code = drillingGCode(job, opt);
    EXPECT_NE(code.find("Z-0.5000"), std::string::npos);
    EXPECT_NE(code.find("Z-1.0000"), std::string::npos);
    EXPECT_NE(code.find("Z-1.5000"), std::string::npos);
    EXPECT_NE(code.find("Z-2.0000"), std::string::npos);
}

TEST(GCode, CustomToolchangeUsesTemplate) {
    GCodeOptions opt;
    opt.flavor = GCodeFlavor::ToolchangeCustom;
    opt.customToolchange = "T{tool} M6 ; dia {diameter} ; z {z}";
    const std::string code = drillingGCode(sampleJob(), opt);
    EXPECT_NE(code.find("T1 M6 ; dia 0.8"), std::string::npos);
    EXPECT_NE(code.find("T2 M6 ; dia 1.2"), std::string::npos);
    EXPECT_NE(code.find("z 10"), std::string::npos);
}

TEST(GCode, LaserFlavorsUseTheirOwnCommands) {
    GCodeOptions opt;
    opt.flavor = GCodeFlavor::MarlinLaserFan;
    PathJob job;
    job.paths = {{{0, 0}, {10, 0}}};
    job.toolDiameter = 0.2;
    const std::string fan = pathsGCode(job, opt);
    EXPECT_NE(fan.find("M106 S"), std::string::npos);
    EXPECT_EQ(fan.find("M03 S"), std::string::npos);
}

TEST(GCode, ExactTspMatchesBruteForceForSevenPoints) {
    std::vector<Pt> points = {{0, 0}, {4, 1}, {2, 5}, {7, 3}, {1, 7}, {6, 6}, {3, 3}};
    const Pt start{10, 10};

    DrillJob job;
    DrillTool tool;
    tool.number = 1;
    tool.holes = points;
    job.tools = {tool};
    optimizeDrillOrder(job, DrillOrder::NearestNeighbor, start);
    const std::vector<Pt>& optimized = job.tools[0].holes;

    auto length = [&start](const std::vector<Pt>& order) {
        double total = 0.0;
        Pt current = start;
        for (const Pt& p : order) {
            total += std::hypot(p.x - current.x, p.y - current.y);
            current = p;
        }
        return total;
    };

    std::vector<Pt> permutation = points;
    std::sort(permutation.begin(), permutation.end(),
              [](const Pt& a, const Pt& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    double brute = 1e18;
    do {
        brute = std::min(brute, length(permutation));
    } while (std::next_permutation(permutation.begin(), permutation.end(),
                                   [](const Pt& a, const Pt& b) {
                                       return a.x < b.x || (a.x == b.x && a.y < b.y);
                                   }));

    EXPECT_NEAR(length(optimized), brute, 1e-6);
}

TEST(GCode, MultiToolPathsUsePerToolFeedsAndDepths) {
    PathToolJob t1;
    t1.number = 1;
    t1.diameter = 0.2;
    t1.options.feedXY = 100.0;
    t1.options.zCut = -0.1;
    t1.options.spindleSpeed = 10000.0;
    t1.paths = {{{0, 0}, {5, 0}}};

    PathToolJob t2;
    t2.number = 2;
    t2.diameter = 0.5;
    t2.options.feedXY = 250.0;
    t2.options.zCut = -0.3;
    t2.options.spindleSpeed = 15000.0;
    t2.paths = {{{10, 0}, {15, 0}}};

    GCodeOptions header = t1.options;
    const std::string code = pathsGCodeMulti({t1, t2}, header);
    EXPECT_NE(code.find("T1"), std::string::npos);
    EXPECT_NE(code.find("T2"), std::string::npos);
    EXPECT_NE(code.find("F100.00"), std::string::npos);
    EXPECT_NE(code.find("F250.00"), std::string::npos);
    EXPECT_NE(code.find("G01 Z-0.1000"), std::string::npos);
    EXPECT_NE(code.find("G01 Z-0.3000"), std::string::npos);
    EXPECT_NE(code.find("M03 S15000"), std::string::npos);
}

TEST(GCode, DrillPerToolSpindleAndFeed) {
    DrillJob job;
    DrillTool t1;
    t1.number = 1;
    t1.diameter = 0.8;
    t1.holes = {{0, 0}};
    t1.spindleSpeed = 12000.0;
    t1.feedXY = 80.0;
    DrillTool t2;
    t2.number = 2;
    t2.diameter = 3.0;
    t2.holes = {{10, 0}};
    t2.spindleSpeed = 8000.0;
    t2.feedXY = 150.0;
    job.tools = {t1, t2};

    const std::string code = drillingGCode(job, GCodeOptions{});
    EXPECT_NE(code.find("M03 S12000"), std::string::npos);
    EXPECT_NE(code.find("M03 S8000"), std::string::npos);
}

TEST(GCode, LaserTurnsOffDuringRapids) {
    PathJob job;
    job.paths = {{Pt{0, 0}, Pt{10, 0}}, {Pt{20, 10}, Pt{30, 10}}};
    job.bounds = BBox{0, 0, 30, 10};
    GCodeOptions opt;
    opt.flavor = GCodeFlavor::GrblLaser;
    opt.laserPower = 250.0;
    const std::string code = pathsGCode(job, opt);

    // Each path must be preceded by a laser-off and a rapid; the beam is only
    // enabled after positioning at the path start.
    const std::size_t firstM5 = code.find("M5");
    const std::size_t firstG00 = code.find("G00 X");
    const std::size_t firstM3 = code.find("M03");
    ASSERT_NE(firstM5, std::string::npos);
    ASSERT_NE(firstG00, std::string::npos);
    ASSERT_NE(firstM3, std::string::npos);
    EXPECT_LT(firstM5, firstG00);
    EXPECT_LT(firstG00, firstM3);

    const std::size_t secondG00 = code.find("G00 X", firstM3);
    ASSERT_NE(secondG00, std::string::npos);
    const std::size_t secondM5 = code.rfind("M5", secondG00);
    ASSERT_NE(secondM5, std::string::npos);
    EXPECT_LT(secondM5, secondG00);
}

TEST(GCode, LaserMultiToolSkipsEmptyPathsSafely) {
    PathToolJob tool;
    tool.number = 1;
    tool.diameter = 0.2;
    tool.options.flavor = GCodeFlavor::GrblLaser;
    tool.paths = {{}, {Pt{1, 1}, Pt{2, 2}}};
    const std::string code = pathsGCodeMulti({tool}, tool.options);
    EXPECT_NE(code.find("M03"), std::string::npos);
    EXPECT_NE(code.find("M5"), std::string::npos);
    // The empty first path must not produce a rapid to an invalid point.
    EXPECT_EQ(code.find("G00 XY"), std::string::npos);
}

TEST(GCode, GrblLaserAvoidsRedundantM5) {
    GCodeOptions opt;
    opt.flavor = GCodeFlavor::GrblLaser;
    opt.laserPower = 300.0;
    PathJob job;
    job.paths = {{{0, 0}, {10, 0}}, {{0, 1}, {10, 1}}, {{0, 2}, {10, 2}}};
    const std::string code = pathsGCode(job, opt);
    EXPECT_EQ(countOccurrences(code, "M5"), std::size_t{2});   // inicio y fin
    EXPECT_EQ(countOccurrences(code, "M03"), std::size_t{1});  // una sola activación
}

TEST(GCode, GrblLaserMultiToolAvoidsRedundantM5) {
    GCodeOptions header;
    header.flavor = GCodeFlavor::GrblLaser;
    header.laserPower = 300.0;
    PathToolJob tool;
    tool.number = 1;
    tool.diameter = 0.2;
    tool.options = header;
    tool.paths = {{{0, 0}, {5, 0}}, {{0, 1}, {5, 1}}};
    const std::string code = pathsGCodeMulti({tool}, header);
    EXPECT_EQ(countOccurrences(code, "M5"), std::size_t{2});
    EXPECT_EQ(countOccurrences(code, "\nM03 "), std::size_t{1});
}

TEST(GCode, DefaultFlavorIsGrblAndHeaderListsParameters) {
    GCodeOptions opt;
    EXPECT_EQ(opt.flavor, GCodeFlavor::Grbl);
    PathJob job;
    job.paths = {{{0, 0}, {10, 0}}};
    job.toolDiameter = 0.2;
    const std::string code = pathsGCode(job, opt);
    EXPECT_NE(code.find("(Postprocessor: GRBL)"), std::string::npos);
    EXPECT_NE(code.find("(Feedrate XY:"), std::string::npos);
    EXPECT_NE(code.find("(Z cut:"), std::string::npos);
}
