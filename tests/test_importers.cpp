#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "flatcam/gerber.hpp"
#include "flatcam/importers.hpp"

using namespace fc;

namespace {

std::filesystem::path writeTemp(const std::string& name, const std::string& content) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path) << content;
    return path;
}

}  // namespace

TEST(Importers, SvgPolygonsAndPaths) {
    const auto path = writeTemp("flatcam_test.svg",
        R"(<svg><rect x="0" y="0" width="10" height="5"/>)"
        R"(<polygon points="20,0 25,0 25,5"/>)"
        R"(<path d="M 30 0 L 35 0 L 35 5 Z"/>)"
        R"(<circle cx="50" cy="0" r="2"/></svg>)");
    const std::vector<Geom> geoms = importSvg(path.string());
    ASSERT_EQ(geoms.size(), std::size_t{4});
    EXPECT_NEAR(geoms[0].area(), 50.0, 1e-6);
    EXPECT_NEAR(geoms[1].area(), 12.5, 1e-6);
    EXPECT_NEAR(geoms[2].area(), 12.5, 1e-6);
    EXPECT_GT(geoms[3].area(), 0.0);
    std::filesystem::remove(path);
}

TEST(Importers, DxfLinesAndPolyline) {
    const std::string dxf =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nLWPOLYLINE\n90\n4\n70\n1\n"
        "10\n0\n20\n0\n10\n10\n20\n0\n10\n10\n20\n10\n10\n0\n20\n10\n"
        "0\nLINE\n10\n0\n20\n0\n11\n5\n21\n5\n"
        "0\nENDSEC\n0\nEOF\n";
    const auto path = writeTemp("flatcam_test.dxf", dxf);
    const std::vector<Geom> geoms = importDxf(path.string());
    ASSERT_EQ(geoms.size(), std::size_t{2});
    EXPECT_NEAR(geoms[0].area(), 100.0, 1e-6);
    std::filesystem::remove(path);
}

TEST(Importers, GerberRoundTrip) {
    const auto path = writeTemp("flatcam_roundtrip.gbr", "");
    writeGerber(path.string(), {Geom::box(0, 0, 10, 5)}, {}, Units::Mm);
    const GerberResult result = GerberParser().parseFile(path.string());
    EXPECT_NEAR(result.solid.area(), 50.0, 1e-3);
    std::filesystem::remove(path);
}

TEST(Importers, HpglPolylineAndCircle) {
    const std::string path = "/tmp/flatcam_test.hpgl";
    std::ofstream out(path);
    out << "IN;SP1;PA0,0;PD;PA1000,0;PA1000,1000;PA0,1000;PA0,0;PU;PA2000,2000;CI500;SP0;";
    out.close();
    const std::vector<Geom> geoms = importHpgl(path);
    ASSERT_GE(geoms.size(), std::size_t{2});
    EXPECT_GT(geoms.front().length(), 50.0);
    std::filesystem::remove(path);
}

TEST(Importers, DxfArcAndEllipse) {
    const std::string dxf =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nARC\n10\n0\n20\n0\n40\n10\n50\n0\n51\n90\n"
        "0\nELLIPSE\n10\n0\n20\n0\n11\n20\n21\n0\n40\n0.5\n41\n0\n42\n6.283185\n"
        "0\nENDSEC\n0\nEOF\n";
    const auto path = writeTemp("flatcam_arc.dxf", dxf);
    const std::vector<Geom> geoms = importDxf(path.string());
    ASSERT_EQ(geoms.size(), std::size_t{2});
    EXPECT_GT(geoms[0].length(), 10.0);
    EXPECT_GT(geoms[1].length(), 50.0);
    std::filesystem::remove(path);
}

TEST(Importers, SvgTransformEllipseAndLine) {
    const auto path = writeTemp("flatcam_transform.svg",
        R"svg(<svg xmlns="http://www.w3.org/2000/svg">
<rect x="0" y="0" width="10" height="5" transform="translate(20,30)"/>
<ellipse cx="0" cy="0" rx="10" ry="5" transform="scale(2)"/>
<line x1="0" y1="0" x2="5" y2="5"/>
</svg>)svg");
    const std::vector<Geom> geoms = importSvg(path.string());
    ASSERT_EQ(geoms.size(), std::size_t{3});
    const BBox b = geoms[0].bounds();
    EXPECT_NEAR(b.xmin, 20.0, 1e-6);
    EXPECT_NEAR(b.ymin, 30.0, 1e-6);
    EXPECT_NEAR(geoms[1].bounds().xmax, 20.0, 1e-6);
    EXPECT_NEAR(geoms[2].length(), std::sqrt(50.0), 1e-6);
    std::filesystem::remove(path);
}

TEST(Importers, DxfSplineApproximation) {
    const std::string dxf =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nSPLINE\n70\n0\n"
        "10\n0\n20\n0\n10\n5\n20\n5\n10\n10\n20\n0\n"
        "0\nENDSEC\n0\nEOF\n";
    const auto path = writeTemp("flatcam_spline.dxf", dxf);
    const std::vector<Geom> geoms = importDxf(path.string());
    ASSERT_EQ(geoms.size(), std::size_t{1});
    EXPECT_GT(geoms[0].length(), 10.0);
    std::filesystem::remove(path);
}

TEST(Importers, SvgCurvesAndArcAreFlattened) {
    const auto path = writeTemp("flatcam_curves.svg",
        R"svg(<svg xmlns="http://www.w3.org/2000/svg">
<path d="M0 0 C10 20 20 20 30 0"/>
<path d="M0 30 A10 10 0 0 1 20 30"/>
</svg>)svg");
    const std::vector<Geom> geoms = importSvg(path.string());
    ASSERT_EQ(geoms.size(), std::size_t{2});
    EXPECT_GT(geoms[0].bounds().ymax, 10.0);
    EXPECT_GT(geoms[1].bounds().ymax, 32.0);
    std::filesystem::remove(path);
}

TEST(Importers, SvgGroupTransformInherited) {
    const auto path = writeTemp("flatcam_group.svg",
        R"svg(<svg xmlns="http://www.w3.org/2000/svg">
<g transform="translate(100,200)">
  <rect x="0" y="0" width="10" height="5"/>
  <g transform="scale(2)">
    <circle cx="0" cy="0" r="5"/>
  </g>
</g>
</svg>)svg");
    const std::vector<Geom> geoms = importSvg(path.string());
    ASSERT_EQ(geoms.size(), std::size_t{2});
    const BBox rect = geoms[0].bounds();
    EXPECT_NEAR(rect.xmin, 100.0, 1e-6);
    EXPECT_NEAR(rect.ymin, 200.0, 1e-6);
    const BBox circle = geoms[1].bounds();
    EXPECT_NEAR(circle.xmax, 110.0, 1e-6);
    std::filesystem::remove(path);
}

TEST(Importers, DxfInsertBlockIsTransformed) {
    const std::string dxf =
        "0\nSECTION\n2\nBLOCKS\n"
        "0\nBLOCK\n2\nMYBLOCK\n10\n0\n20\n0\n"
        "0\nLINE\n10\n0\n20\n0\n11\n10\n21\n0\n"
        "0\nENDBLK\n"
        "0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n"
        "0\nINSERT\n2\nMYBLOCK\n10\n100\n20\n50\n41\n2\n42\n2\n50\n90\n"
        "0\nENDSEC\n"
        "0\nEOF\n";
    const auto path = writeTemp("flatcam_insert.dxf", dxf);
    const std::vector<Geom> geoms = importDxf(path.string());
    ASSERT_EQ(geoms.size(), std::size_t{1});
    const BBox box = geoms.front().bounds();
    EXPECT_NEAR(box.xmin, 100.0, 1e-6);
    EXPECT_NEAR(box.xmax, 100.0, 1e-6);
    EXPECT_NEAR(box.ymin, 50.0, 1e-6);
    EXPECT_NEAR(box.ymax, 70.0, 1e-6);
    std::filesystem::remove(path);
}

TEST(Importers, DxfBlockArrayAndText) {
    const std::string dxf =
        "0\nSECTION\n2\nBLOCKS\n"
        "0\nBLOCK\n2\nLABEL\n10\n0\n20\n0\n"
        "0\nTEXT\n10\n1\n20\n2\n40\n3\n1\nHi\n"
        "0\nENDBLK\n"
        "0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n"
        "0\nINSERT\n2\nLABEL\n10\n10\n20\n20\n41\n2\n42\n2\n"
        "70\n2\n71\n1\n44\n10\n45\n0\n"
        "0\nENDSEC\n"
        "0\nEOF\n";
    const auto path = writeTemp("flatcam_text.dxf", dxf);
    const std::vector<DxfText> texts = importDxfTexts(path.string());
    ASSERT_EQ(texts.size(), std::size_t{2});
    EXPECT_EQ(texts[0].text, "Hi");
    EXPECT_NEAR(texts[0].x, 12.0, 1e-6);
    EXPECT_NEAR(texts[0].y, 24.0, 1e-6);
    EXPECT_NEAR(texts[0].height, 6.0, 1e-6);
    EXPECT_NEAR(texts[1].x, 32.0, 1e-6);
    std::filesystem::remove(path);
}

TEST(Importers, HpglScaleCommand) {
    const auto path = writeTemp("flatcam_scale.hpgl",
        "IN;SC0,10000,0,10000;PA0,0;PD;PA10000,0;PU;");
    const std::vector<Geom> geoms = importHpgl(path.string());
    ASSERT_EQ(geoms.size(), std::size_t{1});
    const BBox box = geoms.front().bounds();
    EXPECT_NEAR(box.xmin, 0.0, 1e-6);
    // 10000 user units over the default A4 plotting width.
    EXPECT_NEAR(box.xmax, 259.125, 0.01);
    std::filesystem::remove(path);
}

TEST(Importers, HpglRelativeArcAndRectangle) {
    const auto path = writeTemp("flatcam_arc.hpgl",
        "IN;PA0,0;PD;AR500,0,0,180;PU;ER1000,1000;");
    const std::vector<Geom> geoms = importHpgl(path.string());
    ASSERT_EQ(geoms.size(), std::size_t{2});
    const BBox arc = geoms[0].bounds();
    // Centre 12.5 mm to the right, sweeping 0..180 degrees.
    EXPECT_NEAR(arc.xmin, 0.0, 1e-6);
    EXPECT_NEAR(arc.xmax, 25.0, 1e-6);
    EXPECT_NEAR(arc.ymax, 12.5, 0.05);
    EXPECT_NEAR(geoms[0].length(), M_PI * 12.5, 0.5);
    const BBox rect = geoms[1].bounds();
    EXPECT_NEAR(rect.xmax - rect.xmin, 25.0, 1e-6);
    std::filesystem::remove(path);
}
