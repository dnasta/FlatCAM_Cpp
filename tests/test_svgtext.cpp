/**
 * @file test_svgtext.cpp
 * @brief Pruebas de texto SVG/DXF y del campo de unidades.
 *
 * Cada `TEST()` describe un caso verificable; los nombres siguen el formato
 * `Suite.Caso` para filtrar con `--gtest_filter`.
 */
#include "TextGeometry.h"
#include "UnitSpinBox.h"

#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QTransform>
#include <gtest/gtest.h>

#include <cmath>

namespace {

QString writeSvg(const QTemporaryDir& dir, const QString& name, const QString& body) {
    const QString path = dir.filePath(name);
    QFile file(path);
    (void)file.open(QIODevice::WriteOnly | QIODevice::Text);
    file.write(body.toUtf8());
    file.close();
    return path;
}

} // namespace

TEST(SvgText, ParseTransformTranslateScale) {
    const QTransform t = parseSvgTransformQt("translate(10,20) scale(2)");
    const QPointF p = t.map(QPointF(1.0, 1.0));
    EXPECT_NEAR(p.x(), 12.0, 1e-9);
    EXPECT_NEAR(p.y(), 22.0, 1e-9);
}

TEST(SvgText, TextBecomesFilledGeometry) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeSvg(dir, "text.svg",
        R"(<svg xmlns="http://www.w3.org/2000/svg">
  <text x="40" y="18" font-size="8" text-anchor="middle">FlatCAM</text>
</svg>)");
    const std::vector<fc::Geom> geoms = svgTextGeometry(path);
    ASSERT_EQ(geoms.size(), std::size_t{1});
    ASSERT_TRUE(geoms.front().valid());
    EXPECT_GT(geoms.front().area(), 1.0);
    const fc::BBox box = geoms.front().bounds();
    // Middle anchor keeps the glyphs centred on x = 40 while y = 18 is the
    // baseline, so the glyphs sit above it (larger Y in plot coordinates).
    EXPECT_NEAR((box.xmin + box.xmax) / 2.0, 40.0, 1.0);
    EXPECT_NEAR(box.ymin, 18.0, 0.5);
    EXPECT_GT(box.ymax, 21.0);
    EXPECT_LT(box.ymax, 27.0);
}

TEST(SvgText, CountersSurviveAsHoles) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeSvg(dir, "o.svg",
        R"(<svg xmlns="http://www.w3.org/2000/svg">
  <text x="0" y="10" font-size="10">O</text>
</svg>)");
    const std::vector<fc::Geom> geoms = svgTextGeometry(path);
    ASSERT_EQ(geoms.size(), std::size_t{1});
    const fc::BBox box = geoms.front().bounds();
    const double boxArea = box.width() * box.height();
    ASSERT_GT(boxArea, 0.0);
    // An "O" is a ring: the filled area is clearly smaller than its bbox.
    EXPECT_LT(geoms.front().area(), boxArea * 0.75);
}

TEST(SvgText, GroupTransformIsInherited) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeSvg(dir, "group.svg",
        R"svg(<svg xmlns="http://www.w3.org/2000/svg">
  <g transform="translate(100,0)">
    <g transform="scale(2)">
      <text x="0" y="10" font-size="5">Hi</text>
    </g>
  </g>
</svg>)svg");
    const std::vector<fc::Geom> geoms = svgTextGeometry(path);
    ASSERT_EQ(geoms.size(), std::size_t{1});
    const fc::BBox box = geoms.front().bounds();
    EXPECT_NEAR(box.xmin, 100.0, 1.0);
    // Scale(2) doubles the 5 unit font, so the glyphs are about 10 tall.
    EXPECT_GT(box.ymax - box.ymin, 5.0);
}

TEST(UnitSpin, MillimetreAndInchSuffix) {
    UnitSpinBox spin;
    spin.setRange(0.0, 100.0);
    spin.setDecimals(3);
    spin.setValue(25.4);
    spin.setSuffix(" mm");
    ASSERT_DOUBLE_EQ(spin.value(), 25.4);
    EXPECT_EQ(spin.suffix(), QStringLiteral(" mm"));

    UnitSpinBox::setDisplayInch(true);
    EXPECT_EQ(spin.suffix(), QStringLiteral(" in"));
    EXPECT_NEAR(spin.value(), 25.4, 1e-9);
    EXPECT_EQ(spin.decimals(), 7);

    UnitSpinBox angle;
    angle.setRange(0.0, 360.0);
    angle.setValue(60.0);
    angle.setSuffix(" °");
    EXPECT_EQ(angle.suffix(), QStringLiteral(" °"));
    EXPECT_DOUBLE_EQ(angle.value(), 60.0);
    UnitSpinBox::setDisplayInch(false);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(DxfText, RendersAtPositionAndHeight) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeSvg(dir, "text.dxf",
        "0\nSECTION\n2\nENTITIES\n"
        "0\nTEXT\n10\n10\n20\n20\n40\n5\n1\nHi\n"
        "0\nENDSEC\n0\nEOF\n");
    const std::vector<fc::Geom> geoms = dxfTextGeometry(path);
    ASSERT_EQ(geoms.size(), std::size_t{1});
    const fc::BBox box = geoms.front().bounds();
    EXPECT_GT(box.xmin, 9.0);
    EXPECT_LT(box.xmin, 13.0);
    EXPECT_NEAR(box.ymin, 20.0, 0.5);
    EXPECT_GT(box.ymax, 24.0);
    EXPECT_LT(box.ymax, 26.0);
}

TEST(DxfText, RotationIsApplied) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeSvg(dir, "rotated.dxf",
        "0\nSECTION\n2\nENTITIES\n"
        "0\nTEXT\n10\n0\n20\n0\n40\n5\n50\n90\n1\nHi\n"
        "0\nENDSEC\n0\nEOF\n");
    const std::vector<fc::Geom> geoms = dxfTextGeometry(path);
    ASSERT_EQ(geoms.size(), std::size_t{1});
    const fc::BBox box = geoms.front().bounds();
    // Rotated 90 degrees: the text advances upwards, glyph height across X.
    EXPECT_GT(box.ymax - box.ymin, box.xmax - box.xmin);
    EXPECT_LT(box.xmax, 0.5);
    EXPECT_LT(box.xmin, -4.0);
}
