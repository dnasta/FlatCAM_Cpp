#include <gtest/gtest.h>

#include <cmath>

#include "flatcam/excellon.hpp"

using namespace fc;

namespace {

const char* kSample = R"EXC(
M48
;FILE_FORMAT=3:3
METRIC,TZ
T1C0.800
T2C1.200
%
G90
T1
X010000Y010000
X020000Y010000
T2
X030000Y020000
X030000Y020000G85X040000Y020000
M30
)EXC";

}  // namespace

TEST(Excellon, ParsesToolsDrillsAndSlot) {
    ExcellonOptions opt;
    opt.appUnits = Units::Mm;
    const ExcellonResult r = ExcellonParser(opt).parseString(kSample);

    EXPECT_EQ(toString(r.fileUnits), "MM");
    ASSERT_EQ(r.tools.size(), std::size_t{2});
    EXPECT_NEAR(r.tools.at(1).diameter, 0.8, 1e-9);
    EXPECT_NEAR(r.tools.at(2).diameter, 1.2, 1e-9);
    EXPECT_EQ(r.drillCount, std::size_t{3});
    EXPECT_EQ(r.slotCount, std::size_t{1});

    ASSERT_TRUE(r.bounds.valid);
    EXPECT_NEAR(r.bounds.xmin, 9.6, 1e-6);
    EXPECT_NEAR(r.bounds.xmax, 40.6, 1e-6);
    EXPECT_NEAR(r.bounds.ymin, 9.6, 1e-6);
    EXPECT_NEAR(r.bounds.ymax, 20.6, 1e-6);

    const double circle = 0.5 * 64 * 0.25 * std::sin(2.0 * M_PI / 64.0);
    const double hole1 = circle * 0.8 * 0.8;
    const double hole2 = circle * 1.2 * 1.2;
    const double slot = 10.0 * 1.2 + hole2;
    // The drill at (30, 20) is the start of the slot, so its area is inside the slot.
    EXPECT_NEAR(r.solid.area(), 2 * hole1 + slot, 1e-6);
}

TEST(Excellon, DecimalCoordinatesAndInch) {
    const char* src = R"EXC(
M48
INCH,LZ
T01C0.0400
%
T01
X1.500Y2.250
X1.750Y2.250
M30
)EXC";
    ExcellonOptions opt;
    opt.appUnits = Units::Mm;
    const ExcellonResult r = ExcellonParser(opt).parseString(src);
    EXPECT_EQ(toString(r.fileUnits), "IN");
    EXPECT_EQ(r.drillCount, std::size_t{2});
    ASSERT_TRUE(r.bounds.valid);
    EXPECT_NEAR(r.bounds.xmin, 1.5 * 25.4 - 0.508, 1e-6);
    EXPECT_NEAR(r.bounds.xmax, 1.75 * 25.4 + 0.508, 1e-6);
    EXPECT_NEAR(r.bounds.ymin, 2.25 * 25.4 - 0.508, 1e-6);
}

TEST(Excellon, HeaderlessToolSelection) {
    const char* src = R"EXC(
T1C0.5
T1
X000000Y000000
X002500Y000000
M30
)EXC";
    const ExcellonResult r = ExcellonParser().parseString(src);
    EXPECT_EQ(r.drillCount, std::size_t{2});
    EXPECT_TRUE(r.tools.count(1) == 1);
    EXPECT_NEAR(r.tools.at(1).diameter, 0.5, 1e-9);
}
