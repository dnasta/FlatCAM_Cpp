/**
 * @file test_pdf.cpp
 * @brief Pruebas del importador PDF vectorial.
 *
 * Cada `TEST()` describe un caso verificable; los nombres siguen el formato
 * `Suite.Caso` para filtrar con `--gtest_filter`.
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "flatcam/pdf.hpp"

using namespace fc;

namespace {

std::filesystem::path writePdf(const std::string& name, const std::string& content) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path);
    out << "%PDF-1.4\n1 0 obj\n<< /Type /Catalog >>\nendobj\n"
        << "4 0 obj\n<< /Length " << content.size() << " >>\nstream\n"
        << content << "\nendstream\nendobj\ntrailer\n<< /Root 1 0 R >>\n%%EOF\n";
    return path;
}

}  // namespace

TEST(Pdf, FilledRectangle) {
    const auto path = writePdf("flatcam_fill.pdf", "100 100 100 100 re f");
    const std::vector<Geom> geoms = importPdfVector(path.string());
    ASSERT_EQ(geoms.size(), std::size_t{1});
    const double side = 100.0 * 25.4 / 72.0;
    EXPECT_NEAR(geoms.front().area(), side * side, 1e-6);
    std::filesystem::remove(path);
}

TEST(Pdf, StrokedPolylineGetsWidth) {
    const auto path = writePdf("flatcam_stroke.pdf",
        "2 w 100 100 m 300 100 l 300 300 l 100 300 l h S");
    const std::vector<Geom> geoms = importPdfVector(path.string());
    ASSERT_EQ(geoms.size(), std::size_t{1});
    const double side = 200.0 * 25.4 / 72.0;
    const double width = 2.0 * 25.4 / 72.0;
    // Ring area is roughly perimeter times stroke width.
    EXPECT_NEAR(geoms.front().area(), 4.0 * side * width, side * width * 0.1);
    const BBox box = geoms.front().bounds();
    EXPECT_NEAR(box.width(), side + width, 0.01);
    std::filesystem::remove(path);
}

TEST(Pdf, BezierAndTransform) {
    const auto path = writePdf("flatcam_bezier.pdf",
        "q 2 0 0 2 0 0 cm 200 100 m 300 100 300 200 200 200 c S Q");
    const std::vector<Geom> geoms = importPdfVector(path.string());
    ASSERT_EQ(geoms.size(), std::size_t{1});
    const BBox box = geoms.front().bounds();
    // Scale 2: the curve spans 75 pt in X and 100 pt in Y; the default 1 pt
    // stroke also scales by 2, adding 2 pt on each axis.
    EXPECT_NEAR(box.width(), 152.0 * 25.4 / 72.0, 0.05);
    EXPECT_NEAR(box.height(), 202.0 * 25.4 / 72.0, 0.05);
    std::filesystem::remove(path);
}

TEST(Pdf, NestedTransformOrder) {
    const auto path = writePdf("flatcam_ctm.pdf",
        "0 w q 1 0 0 1 100 0 cm q 2 0 0 2 0 0 cm 0 20 m 50 20 l S Q Q");
    const std::vector<Geom> geoms = importPdfVector(path.string());
    ASSERT_EQ(geoms.size(), std::size_t{1});
    const BBox box = geoms.front().bounds();
    // The inner scale applies in the translated coordinate system: the line
    // runs from (100, 40) to (200, 40) points, not from (200, 40) to (300, 40).
    EXPECT_NEAR(box.xmin, 100.0 * 25.4 / 72.0, 1e-6);
    EXPECT_NEAR(box.xmax, 200.0 * 25.4 / 72.0, 1e-6);
    EXPECT_NEAR(box.ymin, 40.0 * 25.4 / 72.0, 1e-6);
    EXPECT_NEAR(box.ymax, 40.0 * 25.4 / 72.0, 1e-6);
    std::filesystem::remove(path);
}
