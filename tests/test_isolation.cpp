/**
 * @file test_isolation.cpp
 * @brief Pruebas de aislamiento de cobre.
 *
 * Cada `TEST()` describe un caso verificable; los nombres siguen el formato
 * `Suite.Caso` para filtrar con `--gtest_filter`.
 */
#include <gtest/gtest.h>

#include <cmath>

#include "flatcam/isolation.hpp"

using namespace fc;

namespace {

Geom squareWithHole() {
    const Ring outer = {{0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}};
    const Ring hole = {{3, 3}, {7, 3}, {7, 7}, {3, 7}, {3, 3}};
    return Geom::polygon(outer, {hole});
}

}  // namespace

TEST(Isolation, SingleSquareSinglePass) {
    IsolationOptions opt;
    opt.toolDiameter = 1.0;
    opt.passes = 1;

    const IsolationResult r = isolateCopper(Geom::box(0, 0, 10, 10), opt);
    ASSERT_EQ(r.paths.size(), std::size_t{1});
    EXPECT_EQ(r.envelopes.size(), std::size_t{1});
    // 4 sides of 10 mm plus a rounded corner offset of 0.5 mm.
    EXPECT_NEAR(r.totalLength, 40.0 + M_PI, 0.01);

    ASSERT_TRUE(r.bounds.valid);
    EXPECT_NEAR(r.bounds.xmin, -0.5, 1e-9);
    EXPECT_NEAR(r.bounds.xmax, 10.5, 1e-9);
}

TEST(Isolation, TwoPasses) {
    IsolationOptions opt;
    opt.toolDiameter = 1.0;
    opt.passes = 2;

    const IsolationResult r = isolateCopper(Geom::box(0, 0, 10, 10), opt);
    ASSERT_EQ(r.paths.size(), std::size_t{2});
    EXPECT_NEAR(r.bounds.xmin, -1.5, 1e-9);
    EXPECT_NEAR(r.bounds.xmax, 11.5, 1e-9);
    EXPECT_NEAR(r.totalLength, (40.0 + M_PI) + (40.0 + 3.0 * M_PI), 0.02);
}

TEST(Isolation, HoleProducesInnerPath) {
    IsolationOptions opt;
    opt.toolDiameter = 1.0;
    opt.passes = 1;

    const IsolationResult r = isolateCopper(squareWithHole(), opt);
    ASSERT_EQ(r.paths.size(), std::size_t{2});
    // Outer contour plus the shrunk hole (3 x 3 mm).
    EXPECT_NEAR(r.totalLength, (40.0 + M_PI) + 12.0, 0.02);
}

TEST(Isolation, OverlapReducesSecondPassOffset) {
    IsolationOptions opt;
    opt.toolDiameter = 1.0;
    opt.passes = 2;
    opt.overlap = 0.5;  // 50 %

    const IsolationResult r = isolateCopper(Geom::box(0, 0, 10, 10), opt);
    ASSERT_EQ(r.paths.size(), std::size_t{2});
    // Second pass offset = 1.5 - 0.5 = 1.0 mm.
    EXPECT_NEAR(r.bounds.xmin, -1.0, 1e-9);
}

TEST(Isolation, FiltersAndInvert) {
    IsolationOptions opt;
    opt.toolDiameter = 1.0;
    opt.interiorsOnly = true;
    const IsolationResult inner = isolateCopper(squareWithHole(), opt);
    EXPECT_EQ(inner.paths.size(), std::size_t{1});

    opt.interiorsOnly = false;
    opt.exteriorsOnly = true;
    const IsolationResult outer = isolateCopper(squareWithHole(), opt);
    EXPECT_EQ(outer.paths.size(), std::size_t{1});

    opt.exteriorsOnly = false;
    opt.invert = true;
    const IsolationResult inverted = isolateCopper(Geom::box(0, 0, 10, 10), opt);
    ASSERT_EQ(inverted.paths.size(), std::size_t{1});
    EXPECT_NEAR(inverted.totalLength, 40.0 + M_PI, 0.01);
}

TEST(Isolation, EmptyCopper) {
    const IsolationResult r = isolateCopper(Geom(), IsolationOptions{});
    EXPECT_TRUE(r.paths.empty());
    EXPECT_FALSE(r.bounds.valid);
}
