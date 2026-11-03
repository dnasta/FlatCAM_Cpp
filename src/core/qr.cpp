#include "flatcam/qr.hpp"

#include "flatcam/qrcodegen.hpp"

namespace fc {

std::vector<Geom> qrCodeGeometry(const std::string& text, double moduleSize, int borderModules,
                                 double originX, double originY, char ecc) {
    if (moduleSize <= 0.0) return {};
    qrcodegen::QrCode::Ecc level = qrcodegen::QrCode::Ecc::MEDIUM;
    switch (ecc) {
        case 'L': level = qrcodegen::QrCode::Ecc::LOW; break;
        case 'Q': level = qrcodegen::QrCode::Ecc::QUARTILE; break;
        case 'H': level = qrcodegen::QrCode::Ecc::HIGH; break;
        default: level = qrcodegen::QrCode::Ecc::MEDIUM; break;
    }
    const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(text.c_str(), level);
    const int size = qr.getSize();
    const int border = std::max(0, borderModules);
    const double origin = border * moduleSize;

    std::vector<Geom> runs;
    runs.reserve(static_cast<std::size_t>(size * size / 3));
    for (int y = 0; y < size; ++y) {
        int start = -1;
        for (int x = 0; x <= size; ++x) {
            const bool dark = x < size && qr.getModule(x, y);
            if (dark && start < 0) start = x;
            if (!dark && start >= 0) {
                // QR y grows downwards in the library; flip it for CAD.
                const double y0 = originY + origin + (size - 1 - y) * moduleSize;
                runs.push_back(Geom::box(originX + origin + start * moduleSize, y0,
                                         originX + origin + x * moduleSize, y0 + moduleSize));
                start = -1;
            }
        }
    }
    if (runs.empty()) return {};
    return {Geom::unionOf(runs)};
}

}  // namespace fc
