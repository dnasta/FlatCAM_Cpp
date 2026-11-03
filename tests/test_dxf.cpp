/**
 * @file test_dxf.cpp
 * @brief Pruebas de exportación DXF.
 *
 * Cada `TEST()` describe un caso verificable; los nombres siguen el formato
 * `Suite.Caso` para filtrar con `--gtest_filter`.
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "flatcam/dxf.hpp"

using namespace fc;

TEST(Dxf, WritesEntities) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "flatcam_cpp_test.dxf";
    exportDxf(path.string(), {Geom::box(0, 0, 10, 10)},
              {Geom::line({{0, 0}, {5, 5}})});

    std::ifstream in(path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("LWPOLYLINE"), std::string::npos);
    EXPECT_NE(content.find("AC1015"), std::string::npos);
    EXPECT_NE(content.find("EOF"), std::string::npos);
    std::filesystem::remove(path);
}
