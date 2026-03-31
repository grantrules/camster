#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>

// Supported measurement units.  Internal storage is always millimeters;
// these are used for display and dimension-input parsing.
enum class Unit { Millimeters, Centimeters, Meters, Inches, Feet };

inline constexpr int kUnitCount = 5;

inline const char* unitSuffix(Unit u) {
  switch (u) {
    case Unit::Millimeters: return "mm";
    case Unit::Centimeters: return "cm";
    case Unit::Meters:      return "m";
    case Unit::Inches:      return "in";
    case Unit::Feet:        return "ft";
  }
  return "";
}

inline const char* unitLabel(Unit u) {
  switch (u) {
    case Unit::Millimeters: return "Millimeters (mm)";
    case Unit::Centimeters: return "Centimeters (cm)";
    case Unit::Meters:      return "Meters (m)";
    case Unit::Inches:      return "Inches (in)";
    case Unit::Feet:        return "Feet (ft)";
  }
  return "";
}

// Convert a value in the given unit to millimeters.
inline float toMm(float value, Unit u) {
  switch (u) {
    case Unit::Millimeters: return value;
    case Unit::Centimeters: return value * 10.0f;
    case Unit::Meters:      return value * 1000.0f;
    case Unit::Inches:      return value * 25.4f;
    case Unit::Feet:        return value * 304.8f;
  }
  return value;
}

// Convert a millimeter value to the given display unit.
inline float fromMm(float mm, Unit u) {
  switch (u) {
    case Unit::Millimeters: return mm;
    case Unit::Centimeters: return mm / 10.0f;
    case Unit::Meters:      return mm / 1000.0f;
    case Unit::Inches:      return mm / 25.4f;
    case Unit::Feet:        return mm / 304.8f;
  }
  return mm;
}

// Result of parsing a dimension string.  Always stored in mm.
struct ParsedDimension {
  float valueMm;
};

// Parse a dimension string like "10", "10mm", "2.5in", "1.5cm".
// If no unit suffix is given, defaultUnit is used.
inline std::optional<ParsedDimension> parseDimension(const std::string& input, Unit defaultUnit) {
  if (input.empty()) return std::nullopt;

  const char* str = input.c_str();
  char* end = nullptr;
  const float value = std::strtof(str, &end);
  if (end == str) return std::nullopt;

  // Extract and lowercase the suffix.
  std::string suffix(end);
  while (!suffix.empty() && std::isspace(static_cast<unsigned char>(suffix.front())))
    suffix.erase(suffix.begin());
  while (!suffix.empty() && std::isspace(static_cast<unsigned char>(suffix.back())))
    suffix.pop_back();
  for (char& c : suffix)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  Unit unit = defaultUnit;
  if (!suffix.empty()) {
    if (suffix == "mm")       unit = Unit::Millimeters;
    else if (suffix == "cm")  unit = Unit::Centimeters;
    else if (suffix == "m")   unit = Unit::Meters;
    else if (suffix == "in" || suffix == "\"") unit = Unit::Inches;
    else if (suffix == "ft" || suffix == "'")  unit = Unit::Feet;
    else return std::nullopt;
  }

  return ParsedDimension{toMm(value, unit)};
}
