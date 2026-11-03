#include <gtest/gtest.h>

#include "flatcam/gerber.hpp"

using namespace fc;

namespace {

const char* kSample = R"GERBER(
G04 FlatCAM C++ test file*
%FSLAX34Y34*%
%MOMM*%
%ADD10C,1.000*%
%ADD11R,2.000X1.000*%
%ADD12O,2.000X1.000*%
%ADD13P,2.000X6*%
%AMTHERMAL*7,0,0,3.0,1.5,0.5,0*%
%ADD14THERMAL*%
G01*
D10*
X100000Y100000D03*
D11*
X300000Y100000D03*
D12*
X500000Y100000D03*
D13*
X700000Y100000D03*
D14*
X900000Y100000D03*
D10*
X0Y0D02*
X100000Y0D01*
X100000Y100000D01*
G36*
X200000Y200000D02*
X300000Y200000D01*
X300000Y300000D01*
X200000Y300000D01*
X200000Y200000D01*
G37*
M02*
)GERBER";

}  // namespace

TEST(Gerber, ParsesCoreConstructs) {
    GerberOptions opt;
    opt.appUnits = Units::Mm;
    const GerberResult r = GerberParser(opt).parseString(kSample);

    EXPECT_EQ(toString(r.fileUnits), "MM");
    EXPECT_EQ(r.intDigits, 3);
    EXPECT_EQ(r.fracDigits, 4);
    EXPECT_EQ(r.apertures.size(), std::size_t{5});
    EXPECT_EQ(r.flashes, std::size_t{5});
    EXPECT_EQ(r.segments, std::size_t{2});
    EXPECT_EQ(r.regions, std::size_t{1});

    ASSERT_TRUE(r.bounds.valid);
    EXPECT_NEAR(r.bounds.xmin, -0.50025, 1e-5);
    EXPECT_NEAR(r.bounds.ymin, -0.50025, 1e-5);
    EXPECT_NEAR(r.bounds.xmax, 91.5, 0.05);
    EXPECT_NEAR(r.bounds.ymax, 30.0, 1e-6);

    // 1 mm circle, 2x1 rectangle, 2x1 obround, 6-gon of 2 mm, a 10x10 region
    // and a 20 mm stroke with a 1 mm aperture.
    const double circle = 0.5 * 64 * 0.25 * std::sin(2.0 * M_PI / 64.0);
    const double hexagon = 1.5 * std::sqrt(3.0);
    const double stroke = 20.0 * (1.0 / 1.999) * 2.0 + M_PI * std::pow(0.5 / 1.0, 2) * 1.0;
    const double expected = circle + 2.0 + (1.0 + circle) + hexagon + 100.0 + stroke;
    EXPECT_NEAR(r.solid.area(), expected + 2.94, 0.15);  // thermal ring with crosshair
}

TEST(Gerber, RegionsAndPolarity) {
    const char* src = R"G(
%FSLAX34Y34*%
%MOMM*%
%ADD10C,0.1*%
G36*
X0Y0D02*
X100000Y0D01*
X100000Y100000D01*
X0Y100000D01*
X0Y0D01*
G37*
%LPC*%
G36*
X25000Y25000D02*
X75000Y25000D01*
X75000Y75000D01*
X25000Y75000D01*
X25000Y25000D01*
G37*
%LPD*%
M02*
)G";
    const GerberResult r = GerberParser().parseString(src);
    EXPECT_NEAR(r.solid.area(), 100.0 - 25.0, 1e-6);
}

TEST(Gerber, IncrementalCoordinates) {
    const char* src = R"G(
%FSLIX24Y24*%
%MOIN*%
%ADD10C,0.010*%
D10*
X0Y0D02*
X10000Y0D01*
X0Y10000D01*
M02*
)G";
    GerberOptions opt;
    opt.appUnits = Units::Inch;
    const GerberResult r = GerberParser(opt).parseString(src);
    ASSERT_TRUE(r.bounds.valid);
    EXPECT_NEAR(r.bounds.xmax, 1.005, 1e-5);
    EXPECT_NEAR(r.bounds.ymax, 1.005, 1e-5);
}

TEST(Gerber, ApertureMacroCircleAndOutline) {
    const char* src = R"G(
%FSLAX34Y34*%
%MOMM*%
%AMCIRC*1,1,2.0,0,0*%
%AMOUT*4,1,4,0,0,2,0,2,2,0,2,0,0,0*%
%ADD10CIRC*%
%ADD11OUT*%
D10*
X0Y0D03*
D11*
X10000Y0D03*
M02*
)G";
    const GerberResult r = GerberParser().parseString(src);
    EXPECT_EQ(r.flashes, std::size_t{2});
    EXPECT_NEAR(r.solid.area(), M_PI + 4.0, 0.1);
}

TEST(Gerber, ParsesFileWithArc) {
    const char* src = R"G(
%FSLAX34Y34*%
%MOMM*%
%ADD10C,0.2*%
G75*
G03*
D10*
X0Y0D02*
X100000Y0D01*
X0Y0I-50000J0D01*
M02*
)G";
    const GerberResult r = GerberParser().parseString(src);
    ASSERT_TRUE(r.bounds.valid);
    EXPECT_NEAR(r.bounds.xmin, -0.1, 0.1);
    EXPECT_NEAR(r.bounds.xmax, 10.1, 0.1);
    EXPECT_NEAR(r.bounds.ymax, 5.1, 0.2);
    EXPECT_GT(r.solid.area(), 0.0);
}
