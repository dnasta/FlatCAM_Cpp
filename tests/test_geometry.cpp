/**
 * @file test_geometry.cpp
 * @brief Pruebas del núcleo geométrico GEOS (áreas, buffers, agujeros, transformaciones).
 *
 * Cada `TEST()` describe un caso verificable; los nombres siguen el formato
 * `Suite.Caso` para filtrar con `--gtest_filter`.
 */
#include <gtest/gtest.h>

#include <cmath>

#include "flatcam/geometry.hpp"

using namespace fc;

TEST(Geometry, CircleAreaMatchesPolygon) {
    const Geom c = Geom::circle({0, 0}, 0.5, 16);
    // 64-gon inscribed in a circle of radius 0.5
    const double expected = 0.5 * 64 * 0.25 * std::sin(2.0 * M_PI / 64.0);
    EXPECT_NEAR(c.area(), expected, 1e-9);
    EXPECT_TRUE(c.isValid());
    EXPECT_FALSE(c.empty());
}

TEST(Geometry, BoxAreaAndBounds) {
    const Geom b = Geom::box(1, 2, 4, 6);
    EXPECT_NEAR(b.area(), 12.0, 1e-12);
    const BBox bb = b.bounds();
    ASSERT_TRUE(bb.valid);
    EXPECT_DOUBLE_EQ(bb.xmin, 1);
    EXPECT_DOUBLE_EQ(bb.ymin, 2);
    EXPECT_DOUBLE_EQ(bb.xmax, 4);
    EXPECT_DOUBLE_EQ(bb.ymax, 6);
}

TEST(Geometry, UnionAndDifference) {
    const Geom a = Geom::box(0, 0, 10, 10);
    const Geom b = Geom::box(5, 0, 15, 10);
    EXPECT_NEAR(a.unite(b).area(), 150.0, 1e-9);
    EXPECT_NEAR(a.intersection(b).area(), 50.0, 1e-9);
    EXPECT_NEAR(a.difference(b).area(), 50.0, 1e-9);
}

TEST(Geometry, UnionOfMany) {
    std::vector<Geom> boxes;
    for (int i = 0; i < 10; ++i) boxes.push_back(Geom::box(i, 0, i + 1, 1));
    const Geom u = Geom::unionOf(boxes);
    EXPECT_NEAR(u.area(), 10.0, 1e-9);
    EXPECT_EQ(u.polygons().size(), std::size_t{1});
}

TEST(Geometry, DistanceAndTransform) {
    const Geom b = Geom::box(0, 0, 2, 2).translate(3, 4);
    const BBox bb = b.bounds();
    ASSERT_TRUE(bb.valid);
    EXPECT_NEAR(bb.xmin, 3, 1e-12);
    EXPECT_NEAR(bb.ymin, 4, 1e-12);

    const Geom r = Geom::box(0, -1, 2, 1).rotateDeg(90, {1, 0});
    const BBox rb = r.bounds();
    ASSERT_TRUE(rb.valid);
    EXPECT_NEAR(rb.xmin, 0, 1e-9);
    EXPECT_NEAR(rb.xmax, 2, 1e-9);
    EXPECT_NEAR(rb.ymin, -1, 1e-9);
    EXPECT_NEAR(rb.ymax, 1, 1e-9);
}

TEST(Geometry, SpatialIndexQuery) {
    SpatialIndex index;
    for (int i = 0; i < 100; ++i) index.insert(Geom::box(i, 0, i + 0.5, 1), static_cast<std::uint32_t>(i));
    index.build();
    const std::vector<std::uint32_t> hits = index.query({10.2, -1, 12.2, 2});
    EXPECT_EQ(hits.size(), std::size_t{3});
}

TEST(Geometry, PolygonWithHole) {
    const Ring outer = {{0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}};
    const Ring hole = {{3, 3}, {7, 3}, {7, 7}, {3, 7}, {3, 3}};
    const Geom g = Geom::polygon(outer, {hole});
    EXPECT_NEAR(g.area(), 84.0, 1e-9);
    const auto polys = g.polygons();
    ASSERT_EQ(polys.size(), std::size_t{1});
    EXPECT_EQ(polys[0].holes.size(), std::size_t{1});
}
