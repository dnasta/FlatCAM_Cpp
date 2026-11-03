#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "flatcam/svg.hpp"
#include "flatcam/thieving.hpp"

using namespace fc;

TEST(Thieving, DotsAvoidCopper) {
    ThievingOptions opt;
    opt.dotDiameter = 0.5;
    opt.spacing = 2.0;
    opt.clearance = 0.5;

    const Geom copper = Geom::box(8, 8, 12, 12);
    const BBox area{0, 0, 20, 20};
    const std::vector<Pt> points = thievingPoints(copper, area, opt);

    EXPECT_GT(points.size(), std::size_t{20});
    const Geom keepOut = copper.buffer(0.25 + 0.5, 8);
    for (const Pt& p : points) {
        EXPECT_FALSE(keepOut.intersects(Geom::circle(p, 0.25, 8)));
    }
}

TEST(Svg, WritesPolygons) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "flatcam_cpp_test.svg";
    exportSvg(path.string(), {Geom::box(0, 0, 10, 10)}, {}, BBox{0, 0, 10, 10});

    std::ifstream in(path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("<svg"), std::string::npos);
    EXPECT_NE(content.find("path d=\"M"), std::string::npos);
    std::filesystem::remove(path);
}
