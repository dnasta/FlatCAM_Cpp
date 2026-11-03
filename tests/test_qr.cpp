/**
 * @file test_qr.cpp
 * @brief Pruebas de generación de códigos QR.
 *
 * Cada `TEST()` describe un caso verificable; los nombres siguen el formato
 * `Suite.Caso` para filtrar con `--gtest_filter`.
 */
#include <gtest/gtest.h>

#include "flatcam/qr.hpp"

using namespace fc;

TEST(Qr, GeneratesGeometry) {
    const std::vector<Geom> geom = qrCodeGeometry("FlatCAM C++", 0.5, 4, 0.0, 0.0, 'M');
    ASSERT_EQ(geom.size(), std::size_t{1});
    ASSERT_TRUE(geom.front().valid());
    const BBox b = geom.front().bounds();
    EXPECT_GT(b.width(), 10.0);
    EXPECT_NEAR(b.width(), b.height(), 1e-6);
    EXPECT_GT(geom.front().area(), 10.0);
}
