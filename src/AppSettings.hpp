#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "Units.hpp"

// Application-wide settings that persist across sessions.  Saved as a simple
// key=value text file next to the executable.
struct AppSettings {
  Unit defaultUnit = Unit::Millimeters;
  float gridSpacing = 10.0f;   // mm
  float gridExtent = 100.0f;   // mm
  Unit exportUnit = Unit::Millimeters;

  // Derive the config file path from the executable directory.
  static std::filesystem::path configPath();

  bool save() const;
  bool load();
};
