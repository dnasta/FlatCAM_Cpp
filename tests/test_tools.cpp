/**
 * @file test_tools.cpp
 * @brief Pruebas de utilidades CAM (óptima, punch, reglas, aperturas).
 *
 * Cada `TEST()` describe un caso verificable; los nombres siguen el formato
 * `Suite.Caso` para filtrar con `--gtest_filter`.
 */
#include <gtest/gtest.h>

#include <cmath>

#include "flatcam/gerber.hpp"
#include "flatcam/tools.hpp"

using namespace fc;

TEST(Tools, OptimalDiameterFindsGap) {
    const Geom copper = Geom::box(0, 0, 2, 2).unite(Geom::box(3, 0, 5, 2));
    const OptimalTool opt = optimalIsolationDiameter(copper, 10.0);
    ASSERT_TRUE(opt.valid);
    EXPECT_NEAR(opt.gap, 1.0, 1e-6);
    EXPECT_NEAR(opt.diameter, 0.98, 1e-6);
}

TEST(Tools, OptimalDiameterExposesPoints) {
    const Geom copper = Geom::box(0, 0, 2, 2).unite(Geom::box(3, 0, 5, 2));
    const OptimalTool opt = optimalIsolationDiameter(copper, 10.0);
    ASSERT_TRUE(opt.valid);
    EXPECT_NEAR(opt.pointA.x, 2.0, 1e-6);
    EXPECT_NEAR(opt.pointB.x, 3.0, 1e-6);
}

TEST(Tools, LayersDistanceAndNearestPoints) {
    const std::vector<Geom> a{Geom::box(0, 0, 1, 1)};
    const std::vector<Geom> b{Geom::box(2, 0, 3, 1)};
    Pt pa{};
    Pt pb{};
    const double d = layersDistance(a, b, &pa, &pb);
    EXPECT_NEAR(d, 1.0, 1e-9);
    EXPECT_NEAR(pa.x, 1.0, 1e-9);
    EXPECT_NEAR(pb.x, 2.0, 1e-9);
}

TEST(Tools, CornerMarksCount) {
    const std::vector<Geom> marks = cornerMarks(BBox{0, 0, 10, 10}, 3.0, 1.0, false);
    EXPECT_EQ(marks.size(), std::size_t{8});
    const BBox b = marks.front().bounds();
    EXPECT_TRUE(b.valid);
}

TEST(Tools, ExtractDrillsFromFlashes) {
    const std::string gerber =
        "G04 test*\n"
        "%FSLAX34Y34*%\n"
        "%MOMM*%\n"
        "%ADD10C,0.800*%\n"
        "%ADD11C,3.000*%\n"
        "D10*\n"
        "X1000000Y1000000D03*\n"
        "X5000000Y1000000D03*\n"
        "D11*\n"
        "X3000000Y5000000D03*\n"
        "M02*\n";
    const GerberResult parsed = GerberParser().parseString(gerber);
    const ExcellonResult drills = extractDrills(parsed, 1.0, Units::Mm);
    EXPECT_EQ(drills.tools.size(), std::size_t{1});
    EXPECT_EQ(drills.drillCount, std::size_t{2});
    ASSERT_FALSE(drills.tools.empty());
    EXPECT_NEAR(drills.tools.begin()->second.diameter, 0.8, 1e-6);
}

TEST(Tools, EditCircleAperture) {
    const std::string gerber =
        "%FSLAX34Y34*%\n%MOMM*%\n%ADD10C,0.800*%\n%ADD11C,0.800X0.400*%\nD10*\nX0Y0D03*\n";
    int changed = 0;
    const std::string edited = editCircleAperture(gerber, "10", 1.2, 4, &changed);
    EXPECT_EQ(changed, 1);
    EXPECT_NE(edited.find("%ADD10C,1.2000*%"), std::string::npos);
    EXPECT_NE(edited.find("%ADD11C,0.800X0.400*%"), std::string::npos);
}

TEST(Tools, EditRectApertureKeepsAspect) {
    const std::string gerber =
        "%FSLAX34Y34*%\n%MOMM*%\n%ADD10R,1.000X0.500*%\n%ADD11O,0.600X0.300*%\n";
    int changed = 0;
    const std::string edited = editApertureSize(gerber, "10", 2.0, 3, &changed);
    EXPECT_EQ(changed, 1);
    EXPECT_NE(edited.find("%ADD10R,2.000X1.000*%"), std::string::npos);
    EXPECT_NE(edited.find("%ADD11O,0.600X0.300*%"), std::string::npos);
}

TEST(Tools, PunchRemovesHoles) {
    const GerberResult copper = [] {
        GerberResult r;
        r.solid = Geom::box(0, 0, 10, 10);
        return r;
    }();
    ExcellonResult drills;
    ExcellonTool tool;
    tool.number = 1;
    tool.diameter = 2.0;
    tool.drills.push_back({5.0, 5.0});
    tool.geometry.push_back(Geom::circle({5.0, 5.0}, 1.0, 32));
    drills.tools[1] = tool;
    drills.geometry = tool.geometry;
    drills.solid = Geom::unionOf(drills.geometry);

    const Geom punched = punchGerber(copper, drills, 0.0);
    ASSERT_TRUE(punched.valid());
    EXPECT_LT(punched.area(), 100.0);
    EXPECT_FALSE(punched.intersects(Geom::circle({5.0, 5.0}, 0.4, 24)));
}
