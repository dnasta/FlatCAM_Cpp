/**
 * @file test_ncc.cpp
 * @brief Pruebas de NCC (hatch/paint).
 *
 * Cada `TEST()` describe un caso verificable; los nombres siguen el formato
 * `Suite.Caso` para filtrar con `--gtest_filter`.
 */
#include <gtest/gtest.h>

#include <algorithm>

#include "flatcam/isolation.hpp"
#include "flatcam/ncc.hpp"

using namespace fc;

TEST(Ncc, ClearsAreaAroundCopper) {
    NccOptions opt;
    opt.toolDiameter = 0.5;
    opt.stepOver = 0.5;
    opt.clearance = 0.0;

    const Geom copper = Geom::box(4, 4, 6, 6);
    const Geom area = Geom::box(0, 0, 10, 10);
    const NccResult r = clearCopper(copper, area, opt);

    EXPECT_GT(r.paths.size(), std::size_t{10});
    EXPECT_GT(r.totalLength, 50.0);
    ASSERT_TRUE(r.bounds.valid);
    EXPECT_GE(r.bounds.xmin, -0.01);
    EXPECT_LE(r.bounds.xmax, 10.01);

    // Paths must keep the tool radius away from the copper.
    const Geom keepOut = copper.buffer(0.25, 16);
    for (std::size_t i = 0; i < std::min<std::size_t>(r.paths.size(), 5); ++i) {
        EXPECT_LT(r.paths[i].intersection(keepOut).length(), 1e-6);
    }
}

TEST(Ncc, AngleRotatesTheHatch) {
    NccOptions opt;
    opt.toolDiameter = 0.5;
    opt.stepOver = 0.8;
    opt.angleDeg = 45.0;

    const NccResult r = clearCopper(Geom::box(4, 4, 6, 6), Geom::box(0, 0, 10, 10), opt);
    EXPECT_GT(r.paths.size(), std::size_t{0});
    EXPECT_GT(r.totalLength, 0.0);
}

TEST(IsolationRest, SmallToolIsolatesCloseFeatures) {
    const Geom copper = Geom::unionOf({Geom::box(0, 0, 1, 1), Geom::box(2, 0, 3, 1)});

    IsolationOptions opt;
    opt.toolDiameter = 0.2;
    opt.passes = 1;
    opt.avoidIntersections = true;

    const IsolationResult normal = isolateCopper(copper, opt);
    EXPECT_EQ(normal.notIsolated.size(), std::size_t{0});
    EXPECT_EQ(normal.paths.size(), std::size_t{2});

    const IsolationResult rest = isolateRest(copper, 2.0, opt);
    EXPECT_GT(rest.paths.size(), std::size_t{0});
    EXPECT_EQ(rest.notIsolated.size(), std::size_t{2});
}

TEST(Ncc, BorderLeavesOuterBandUnprocessed) {
    NccOptions opt;
    opt.toolDiameter = 0.5;
    opt.stepOver = 0.8;
    opt.border = 2.0;

    const NccResult r = clearCopper(Geom::box(4, 4, 6, 6), Geom::box(0, 0, 10, 10), opt);
    ASSERT_TRUE(r.bounds.valid);
    EXPECT_GE(r.bounds.xmin, 1.99);
    EXPECT_GE(r.bounds.ymin, 1.99);
    EXPECT_LE(r.bounds.xmax, 8.01);
    EXPECT_LE(r.bounds.ymax, 8.01);
}

TEST(Ncc, PaintUsesFineOverlap) {
    NccOptions opt;
    opt.strategy = NccStrategy::Paint;
    opt.toolDiameter = 0.1;
    opt.stepOver = 0.2;
    const NccResult r = clearCopper(Geom::box(4, 4, 6, 6), Geom::box(0, 0, 10, 10), opt);
    EXPECT_GT(r.paths.size(), std::size_t{10});
}

TEST(Ncc, PaintFillsNarrowGapWithManyPasses) {
    NccOptions opt;
    opt.strategy = NccStrategy::Paint;
    opt.toolDiameter = 0.1;
    opt.stepOver = 0.2;
    const NccResult r = clearCopper(Geom::box(0, 0, 10, 10),
                                    Geom::box(-0.4, -0.4, 10.4, 10.4), opt);
    EXPECT_GT(r.paths.size(), std::size_t{10});
}


TEST(Ncc, PaintHalfMillimeterGapHasManyPasses) {
    NccOptions opt;
    opt.strategy = NccStrategy::Paint;
    opt.toolDiameter = 0.1;
    opt.stepOver = 0.2;
    const NccResult r = clearCopper(Geom::box(0, 0, 10, 10),
                                    Geom::box(-0.5, -0.5, 10.5, 10.5), opt);
    EXPECT_GT(r.paths.size(), std::size_t{15});
}
