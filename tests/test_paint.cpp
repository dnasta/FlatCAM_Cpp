#include <gtest/gtest.h>

#include "flatcam/paint.hpp"

using namespace fc;

TEST(Paint, FillsSquareWithContourParallelPaths) {
    PaintOptions opt;
    opt.toolDiameter = 1.0;
    opt.stepOver = 0.5;

    const PaintResult r = paintArea(Geom::box(0, 0, 10, 10), opt);
    EXPECT_GT(r.paths.size(), std::size_t{3});
    EXPECT_GT(r.totalLength, 100.0);
    ASSERT_TRUE(r.bounds.valid);
    EXPECT_NEAR(r.bounds.xmin, 0.0, 1e-6);
    EXPECT_NEAR(r.bounds.xmax, 10.0, 1e-6);

    // First path is the inward offset of the tool radius.
    const BBox first = r.paths.front().bounds();
    EXPECT_NEAR(first.xmin, 0.5, 1e-6);
    EXPECT_NEAR(first.xmax, 9.5, 1e-6);
}

TEST(Paint, EmptyArea) {
    const PaintResult r = paintArea(Geom(), PaintOptions{});
    EXPECT_TRUE(r.paths.empty());
}
