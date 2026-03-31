#pragma once

#include "AppSettings.hpp"
#include "Units.hpp"

// Per-project settings.  Initialized from AppSettings when a new project is
// started, then independently adjustable.
struct Project {
  Unit defaultUnit = Unit::Millimeters;
  float gridSpacing = 10.0f;   // mm
  float gridExtent = 100.0f;   // mm
  Unit exportUnit = Unit::Millimeters;

  // Reset this project's settings from application defaults.
  void initFromAppSettings(const AppSettings& s) {
    defaultUnit = s.defaultUnit;
    gridSpacing = s.gridSpacing;
    gridExtent = s.gridExtent;
    exportUnit = s.exportUnit;
  }
};
