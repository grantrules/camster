#pragma once

#include "Units.hpp"

// Per-project settings.  Will grow as features are added (e.g. grid spacing,
// snap tolerance, default construction plane).
struct Project {
  Unit defaultUnit = Unit::Millimeters;
};
