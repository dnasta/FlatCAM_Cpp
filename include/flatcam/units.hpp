#pragma once

#include <string>
#include <string_view>

namespace fc {

enum class Units { Mm, Inch };

constexpr double kInchToMm = 25.4;

inline double conversionFactor(Units from, Units to) {
    if (from == to) return 1.0;
    return from == Units::Inch ? kInchToMm : 1.0 / kInchToMm;
}

inline double convert(double value, Units from, Units to) {
    return value * conversionFactor(from, to);
}

inline std::string toString(Units u) { return u == Units::Mm ? "MM" : "IN"; }

// Gerber coordinate with explicit zero suppression.
// Mirrors FlatCAM's parse_gerber_number():
//   'L'/'D' -> leading zeros omitted, number is scaled by 10^-frac
//   'T'     -> trailing zeros omitted
double parseGerberNumber(std::string_view text, int intDigits, int fracDigits, char zeros);

}  // namespace fc
