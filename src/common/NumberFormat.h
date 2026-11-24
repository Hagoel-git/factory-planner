#ifndef NUMBERFORMAT_H
#define NUMBERFORMAT_H

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace numfmt {

inline constexpr double kDisplayZeroEpsilon = 1e-9;

enum class DisplayPrecision : int {
    Auto = -1,
    Fixed0 = 0,
    Fixed1 = 1,
    Fixed2 = 2,
    Fixed3 = 3,
    Fixed4 = 4
};

inline std::string displayPrecisionToString(DisplayPrecision precision) {
    switch (precision) {
        case DisplayPrecision::Auto:   return "auto";
        case DisplayPrecision::Fixed0: return "0";
        case DisplayPrecision::Fixed1: return "1";
        case DisplayPrecision::Fixed2: return "2";
        case DisplayPrecision::Fixed3: return "3";
        case DisplayPrecision::Fixed4: return "4";
    }
    return "auto";
}

inline std::optional<DisplayPrecision> displayPrecisionFromString(std::string_view s) {
    if (s == "auto") return DisplayPrecision::Auto;
    if (s == "0")    return DisplayPrecision::Fixed0;
    if (s == "1")    return DisplayPrecision::Fixed1;
    if (s == "2")    return DisplayPrecision::Fixed2;
    if (s == "3")    return DisplayPrecision::Fixed3;
    if (s == "4")    return DisplayPrecision::Fixed4;
    return std::nullopt;
}

inline std::optional<DisplayPrecision> displayPrecisionFromInt(int v) {
    switch (v) {
        case -1: return DisplayPrecision::Auto;
        case 0:  return DisplayPrecision::Fixed0;
        case 1:  return DisplayPrecision::Fixed1;
        case 2:  return DisplayPrecision::Fixed2;
        case 3:  return DisplayPrecision::Fixed3;
        case 4:  return DisplayPrecision::Fixed4;
        default: return std::nullopt;
    }
}

inline const char* displayPrecisionLabel(DisplayPrecision precision) {
    switch (precision) {
        case DisplayPrecision::Auto:   return "Auto";
        case DisplayPrecision::Fixed0: return "0 decimals";
        case DisplayPrecision::Fixed1: return "1 decimal";
        case DisplayPrecision::Fixed2: return "2 decimals";
        case DisplayPrecision::Fixed3: return "3 decimals";
        case DisplayPrecision::Fixed4: return "4 decimals";
    }
    return "Auto";
}

namespace detail {

inline void normalizeNegativeZero(std::string& s) {
    if (s.empty() || s.front() != '-') {
        return;
    }
    for (size_t i = 1; i < s.size(); ++i) {
        if (s[i] >= '1' && s[i] <= '9') {
            return;
        }
    }
    s.erase(s.begin());
}

inline void trimTrailingZeros(std::string& s) {
    if (s.find('e') != std::string::npos || s.find('E') != std::string::npos) {
        return;
    }
    const size_t dot = s.find('.');
    if (dot == std::string::npos) {
        return;
    }
    while (s.size() > dot + 1 && s.back() == '0') {
        s.pop_back();
    }
    if (!s.empty() && s.back() == '.') {
        s.pop_back();
    }
}

// Normalizes platform-dependent scientific exponents (e.g. "4.2e-05" / "4.2e-005" -> "4.2e-5").
inline void compactScientificExponent(std::string& s) {
    size_t ePos = s.find('e');
    if (ePos == std::string::npos) {
        ePos = s.find('E');
        if (ePos == std::string::npos) {
            return;
        }
        s[ePos] = 'e';
    }
    size_t digitStart = ePos + 1;
    if (digitStart < s.size() && (s[digitStart] == '+' || s[digitStart] == '-')) {
        ++digitStart;
    }
    size_t firstNonZero = digitStart;
    while (firstNonZero + 1 < s.size() && s[firstNonZero] == '0') {
        ++firstNonZero;
    }
    if (firstNonZero > digitStart) {
        s.erase(digitStart, firstNonZero - digitStart);
    }
}

} // namespace detail

inline std::string formatNumber(double value, DisplayPrecision precision) {
    if (std::isnan(value)) {
        return "NaN";
    }
    if (std::isinf(value)) {
        return std::signbit(value) ? "-Inf" : "Inf";
    }
    if (std::abs(value) < kDisplayZeroEpsilon) {
        value = 0.0;
    }

    char buf[128];
    const double absVal = std::abs(value);

    // Guard against extreme magnitudes overflowing fixed-point buffers.
    if (absVal >= 1e15) {
        std::snprintf(buf, sizeof(buf), "%.4g", value);
        std::string out(buf);
        detail::compactScientificExponent(out);
        detail::normalizeNegativeZero(out);
        return out;
    }

    if (precision != DisplayPrecision::Auto) {
        const int decimals = std::clamp(static_cast<int>(precision), 0, 4);
        std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
        std::string out(buf);
        detail::normalizeNegativeZero(out);
        return out;
    }

    // Auto precision: trim trailing zeros; expand precision for small active values (< 0.01).
    if (value == 0.0) {
        return "0";
    }
    if (absVal >= 0.01) {
        std::snprintf(buf, sizeof(buf), "%.2f", value);
        std::string out(buf);
        detail::trimTrailingZeros(out);
        detail::normalizeNegativeZero(out);
        return out;
    }
    if (absVal >= 1e-4) {
        const int exp10 = static_cast<int>(std::floor(std::log10(absVal)));
        const int decimals = std::clamp(-exp10 + 1, 2, 6); // 2 significant digits
        std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
        std::string out(buf);
        detail::trimTrailingZeros(out);
        detail::normalizeNegativeZero(out);
        return out;
    }

    std::snprintf(buf, sizeof(buf), "%.2g", value);
    std::string out(buf);
    detail::compactScientificExponent(out);
    detail::normalizeNegativeZero(out);
    return out;
}

enum class SiFamily {
    Power,  // W, kW, MW, GW, TW
    Energy  // J, kJ, MJ, GJ, TJ
};

struct SiUnitSpec {
    SiFamily family = SiFamily::Power;
    int prefixIndex = 0; // 0 = base, 1 = k, 2 = M, 3 = G, 4 = T

    bool operator==(const SiUnitSpec&) const = default;
};

inline constexpr int kMaxSiPrefixIndex = 4;

inline double siScaleFactor(int prefixIndex) {
    static constexpr double kScales[kMaxSiPrefixIndex + 1] = {
        1.0, 1e3, 1e6, 1e9, 1e12
    };
    const int idx = std::clamp(prefixIndex, 0, kMaxSiPrefixIndex);
    return kScales[idx];
}

inline const char* siUnitName(SiFamily family, int prefixIndex) {
    static constexpr const char* kPowerUnits[kMaxSiPrefixIndex + 1] = {
        "W", "kW", "MW", "GW", "TW"
    };
    static constexpr const char* kEnergyUnits[kMaxSiPrefixIndex + 1] = {
        "J", "kJ", "MJ", "GJ", "TJ"
    };
    const int idx = std::clamp(prefixIndex, 0, kMaxSiPrefixIndex);
    return (family == SiFamily::Power) ? kPowerUnits[idx] : kEnergyUnits[idx];
}

inline std::optional<SiUnitSpec> parseSiUnit(std::string_view unit) {
    if (unit == "W")  return SiUnitSpec{SiFamily::Power, 0};
    if (unit == "kW") return SiUnitSpec{SiFamily::Power, 1};
    if (unit == "MW") return SiUnitSpec{SiFamily::Power, 2};
    if (unit == "GW") return SiUnitSpec{SiFamily::Power, 3};
    if (unit == "TW") return SiUnitSpec{SiFamily::Power, 4};

    if (unit == "J")  return SiUnitSpec{SiFamily::Energy, 0};
    if (unit == "kJ") return SiUnitSpec{SiFamily::Energy, 1};
    if (unit == "MJ") return SiUnitSpec{SiFamily::Energy, 2};
    if (unit == "GJ") return SiUnitSpec{SiFamily::Energy, 3};
    if (unit == "TJ") return SiUnitSpec{SiFamily::Energy, 4};

    return std::nullopt;
}

struct FormattedValueWithUnit {
    std::string number;
    std::string unit;

    std::string combined() const {
        if (unit.empty()) {
            return number;
        }
        if (unit == "%" || unit.front() == '/') {
            return number + unit;
        }
        return number + " " + unit;
    }
};

// Auto-scales a value in `declaredUnit` across W/kW/MW/GW/TW or J/kJ/MJ/GJ/TJ.
// Zero (or |value| < kDisplayZeroEpsilon) stays in `declaredUnit` so totals stay visually anchored.
inline FormattedValueWithUnit formatSiScaled(
    double valueInDeclaredUnit,
    const SiUnitSpec& declaredUnit,
    DisplayPrecision precision) {
    if (!std::isfinite(valueInDeclaredUnit) || std::abs(valueInDeclaredUnit) < kDisplayZeroEpsilon) {
        const double v = std::isfinite(valueInDeclaredUnit) ? 0.0 : valueInDeclaredUnit;
        return FormattedValueWithUnit{
            formatNumber(v, precision),
            siUnitName(declaredUnit.family, declaredUnit.prefixIndex)
        };
    }

    const double baseVal = valueInDeclaredUnit * siScaleFactor(declaredUnit.prefixIndex);
    int idx = 0;
    double scaled = baseVal;
    while (idx < kMaxSiPrefixIndex && std::abs(scaled) >= 1000.0) {
        scaled /= 1000.0;
        ++idx;
    }

    // Rounding carry-over guard: e.g. 999.999 kW at 2 decimals must render as 1 MW, not 1000 kW.
    if (idx < kMaxSiPrefixIndex) {
        const int decimals = (precision == DisplayPrecision::Auto)
            ? 2
            : std::clamp(static_cast<int>(precision), 0, 4);
        const double factor = std::pow(10.0, decimals);
        if (std::round(std::abs(scaled) * factor) >= 1000.0 * factor) {
            scaled /= 1000.0;
            ++idx;
        }
    }

    return FormattedValueWithUnit{
        formatNumber(scaled, precision),
        siUnitName(declaredUnit.family, idx)
    };
}

// Parses a user-typed number with an optional SI suffix (e.g. "40", "500k", "500 kW", "1.2 GW")
// and converts it into `defaultUnit`. Bare numbers are interpreted in `defaultUnit`.
inline std::optional<double> parseSiInput(std::string_view rawInput, const SiUnitSpec& defaultUnit) {
    size_t start = 0;
    while (start < rawInput.size() && std::isspace(static_cast<unsigned char>(rawInput[start]))) {
        ++start;
    }
    size_t end = rawInput.size();
    while (end > start && std::isspace(static_cast<unsigned char>(rawInput[end - 1]))) {
        --end;
    }
    if (start >= end) {
        return std::nullopt;
    }

    const std::string trimmed(rawInput.substr(start, end - start));
    char* endPtr = nullptr;
    const double val = std::strtod(trimmed.c_str(), &endPtr);
    if (endPtr == trimmed.c_str() || !std::isfinite(val)) {
        return std::nullopt;
    }

    const char* p = endPtr;
    while (*p != '\0' && std::isspace(static_cast<unsigned char>(*p))) {
        ++p;
    }
    if (*p == '\0') {
        return val;
    }

    std::string suf;
    while (*p != '\0') {
        if (!std::isspace(static_cast<unsigned char>(*p))) {
            suf.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
        }
        ++p;
    }

    const char baseChar = (defaultUnit.family == SiFamily::Power) ? 'w' : 'j';
    int targetIdx = -1;
    if (suf.size() == 1) {
        if (suf[0] == baseChar) targetIdx = 0;
        else if (suf[0] == 'k') targetIdx = 1;
        else if (suf[0] == 'm') targetIdx = 2;
        else if (suf[0] == 'g') targetIdx = 3;
        else if (suf[0] == 't') targetIdx = 4;
    } else if (suf.size() == 2 && suf[1] == baseChar) {
        if (suf[0] == 'k')      targetIdx = 1;
        else if (suf[0] == 'm') targetIdx = 2;
        else if (suf[0] == 'g') targetIdx = 3;
        else if (suf[0] == 't') targetIdx = 4;
    }

    if (targetIdx < 0) {
        return std::nullopt;
    }

    const double scaleRatio = siScaleFactor(targetIdx) / siScaleFactor(defaultUnit.prefixIndex);
    const double result = val * scaleRatio;
    return std::isfinite(result) ? std::optional<double>(result) : std::nullopt;
}

// Inspects a printf-style metric format string:
// - `isIntegerFormatSpec`: returns true for "%d", "%i", "%.0f", "%.0F" (no surrounding literal text).
// - `isPlainNumericFormatSpec`: returns true when empty or a single numeric float conversion
//   with no surrounding literal text (e.g. "%.2f", "%.3g", "%.1f").
inline bool isIntegerFormatSpec(std::string_view fmt) {
    if (fmt.size() < 2 || fmt.front() != '%') {
        return false;
    }
    size_t i = 1;
    while (i < fmt.size() &&
           (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' || fmt[i] == '#' || fmt[i] == '0')) {
        ++i;
    }
    while (i < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[i]))) {
        ++i;
    }
    bool hasZeroPrec = false;
    bool hasOtherPrec = false;
    if (i < fmt.size() && fmt[i] == '.') {
        ++i;
        const size_t precStart = i;
        while (i < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[i]))) {
            ++i;
        }
        const std::string_view precDigits = fmt.substr(precStart, i - precStart);
        if (precDigits.empty() || precDigits == "0") {
            hasZeroPrec = true;
        } else {
            hasOtherPrec = true;
        }
    }
    while (i < fmt.size() && fmt[i] == 'l') {
        ++i;
    }
    if (i + 1 != fmt.size()) {
        return false;
    }
    const char conv = fmt[i];
    if (conv == 'd' || conv == 'i') {
        return true;
    }
    if ((conv == 'f' || conv == 'F') && hasZeroPrec && !hasOtherPrec) {
        return true;
    }
    return false;
}

inline bool isPlainNumericFormatSpec(std::string_view fmt) {
    if (fmt.empty()) {
        return true;
    }
    if (fmt.size() < 2 || fmt.front() != '%') {
        return false;
    }
    size_t i = 1;
    while (i < fmt.size() &&
           (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' || fmt[i] == '#' || fmt[i] == '0')) {
        ++i;
    }
    while (i < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[i]))) {
        ++i;
    }
    if (i < fmt.size() && fmt[i] == '.') {
        ++i;
        while (i < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[i]))) {
            ++i;
        }
    }
    while (i < fmt.size() && fmt[i] == 'l') {
        ++i;
    }
    if (i + 1 != fmt.size()) {
        return false;
    }
    const char conv = fmt[i];
    return conv == 'f' || conv == 'F' || conv == 'g' || conv == 'G' || conv == 'e' || conv == 'E';
}

} // namespace numfmt

#endif // NUMBERFORMAT_H
