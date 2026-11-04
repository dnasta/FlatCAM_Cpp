/**
 * @file test_cutout.cpp
 * @brief Pruebas de corte de contorno, puentes y multipasada.
 *
 * Cada `TEST()` describe un caso verificable; los nombres siguen el formato
 * `Suite.Caso` para filtrar con `--gtest_filter`.
 */
#include <gtest/gtest.h>

#include <cmath>

#include "flatcam/cutout.hpp"

using namespace fc;

TEST(Cutout, ExternalSquare) {
    CutoutOptions opt;
    opt.toolDiameter = 1.0;
    opt.margin = 0.5;  // tool centre 1.0 mm outside the board

    const CutoutResult r = cutoutGeometry(Geom::box(0, 0, 50, 50), opt);
    ASSERT_EQ(r.paths.size(), std::size_t{1});
    EXPECT_NEAR(r.totalLength, 208.0, 0.02);  // 52 x 52 con esquinas en inglete
    ASSERT_TRUE(r.bounds.valid);
    EXPECT_NEAR(r.bounds.xmin, -1.0, 1e-9);
    EXPECT_NEAR(r.bounds.xmax, 51.0, 1e-9);
}

TEST(Cutout, InternalPocket) {
    CutoutOptions opt;
    opt.toolDiameter = 1.0;
    opt.margin = 0.5;
    opt.external = false;  // offset = 0.5 - 0.5 = 0

    const CutoutResult r = cutoutGeometry(Geom::box(0, 0, 50, 50), opt);
    ASSERT_EQ(r.paths.size(), std::size_t{1});
    EXPECT_NEAR(r.totalLength, 200.0, 1e-6);
    EXPECT_NEAR(r.bounds.xmin, 0.0, 1e-9);
}

TEST(Cutout, ShrinksWhenMarginIsSmallerThanRadius) {
    CutoutOptions opt;
    opt.toolDiameter = 2.0;
    opt.margin = 0.0;
    opt.external = false;  // offset = -1.0

    const CutoutResult r = cutoutGeometry(Geom::box(0, 0, 50, 50), opt);
    ASSERT_EQ(r.paths.size(), std::size_t{1});
    EXPECT_NEAR(r.totalLength, 192.0, 1e-6);
    EXPECT_NEAR(r.bounds.xmin, 1.0, 1e-9);
}

TEST(Cutout, InvertKeepsLength) {
    CutoutOptions opt;
    opt.toolDiameter = 1.0;
    CutoutResult normal = cutoutGeometry(Geom::box(0, 0, 50, 50), opt);
    opt.invert = true;
    CutoutResult inverted = cutoutGeometry(Geom::box(0, 0, 50, 50), opt);
    EXPECT_NEAR(normal.totalLength, inverted.totalLength, 1e-9);
}

TEST(Cutout, EmptyOutline) {
    const CutoutResult r = cutoutGeometry(Geom(), CutoutOptions{});
    EXPECT_TRUE(r.paths.empty());
    EXPECT_FALSE(r.bounds.valid);
}

TEST(Cutout, TabsSplitThePath) {
    CutoutOptions opt;
    opt.toolDiameter = 1.0;
    opt.tabCount = 2;
    opt.tabWidth = 5.0;

    const CutoutResult r = cutoutGeometry(Geom::box(0, 0, 50, 50), opt);
    ASSERT_EQ(r.paths.size(), std::size_t{2});
    // Perímetro 51 x 51 con esquinas en inglete, menos dos puentes de 5 mm.
    EXPECT_NEAR(r.totalLength, 204.0 - 10.0, 0.05);
}

TEST(Cutout, AreaClipsTheEnvelope) {
    CutoutOptions opt;
    opt.toolDiameter = 1.0;
    opt.area = BBox{-10.0, -10.0, 60.0, 10.0};

    const CutoutResult r = cutoutGeometry(Geom::box(0, 0, 50, 50), opt);
    ASSERT_TRUE(r.bounds.valid);
    EXPECT_NEAR(r.bounds.xmin, -0.5, 1e-6);
    EXPECT_NEAR(r.bounds.xmax, 50.5, 1e-6);
    EXPECT_NEAR(r.bounds.ymax, 10.0, 1e-6);
    EXPECT_GT(r.paths.size(), std::size_t{0});
}

TEST(Cutout, TabsCanBeLimitedToSidesOrManualPositions) {
    CutoutOptions opt;
    opt.toolDiameter = 0.0;
    opt.margin = 0.0;
    opt.tabCount = 4;
    opt.tabWidth = 2.0;

    CutoutResult all = cutoutGeometry(Geom::box(0, 0, 50, 50), opt);
    ASSERT_EQ(all.paths.size(), std::size_t{4});

    opt.tabSides = TabSides::TopBottom;
    CutoutResult topBottom = cutoutGeometry(Geom::box(0, 0, 50, 50), opt);
    EXPECT_EQ(topBottom.paths.size(), std::size_t{2});

    opt.tabSides = TabSides::All;
    opt.tabCount = 0;
    opt.tabPositions = {0.25, 0.75};
    CutoutResult manual = cutoutGeometry(Geom::box(0, 0, 50, 50), opt);
    EXPECT_EQ(manual.paths.size(), std::size_t{2});
}
