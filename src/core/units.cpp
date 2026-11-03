#include "flatcam/units.hpp"

#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace fc {

double parseGerberNumber(std::string_view text, int intDigits, int fracDigits, char zeros) {
    if (text.empty()) throw std::invalid_argument("empty gerber number");

    bool negative = false;
    std::size_t i = 0;
    if (text[i] == '+' || text[i] == '-') {
        negative = text[i] == '-';
        ++i;
    }
    const std::string_view digits = text.substr(i);
    for (char c : digits) {
        if (c < '0' || c > '9') throw std::invalid_argument("invalid gerber number: " + std::string(text));
    }
    const long long raw = std::strtoll(std::string(digits).c_str(), nullptr, 10);

    double value = 0.0;
    if (zeros == 'L' || zeros == 'D') {
        value = static_cast<double>(raw) * std::pow(10.0, -fracDigits);
    } else if (zeros == 'T') {
        const int total = intDigits + fracDigits;
        const int len = static_cast<int>(digits.size());
        value = static_cast<double>(raw) * std::pow(10.0, total - len - fracDigits);
    } else {
        throw std::invalid_argument("unknown zero suppression mode");
    }
    return negative ? -value : value;
}

}  // namespace fc
