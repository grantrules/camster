#pragma once

// Shared plain-data types that live outside main.cpp so that History.hpp and
// NutFile.hpp can reference them without pulling in the entire app state.

#include <array>
#include <optional>

#include "Scene.hpp"
#include "sketch/Sketch.hpp"

// ---- Object / Sketch metadata ----

struct ObjectMetadata {
  std::array<char, 64> name{};
  bool visible = true;
  bool locked = false;
};

struct SketchMetadata {
  std::array<char, 64> name{};
  bool visible = true;
  bool locked = false;
};

struct SketchEntry {
  Sketch sketch;
  SketchMetadata meta;
  SketchPlane plane = SketchPlane::XY;
  float offsetMm = 0.0f;
};
