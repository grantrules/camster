#pragma once

// Shared plain-data types that live outside main.cpp so that History.hpp and
// NutFile.hpp can reference them without pulling in the entire app state.

#include <array>
#include <optional>
#include <string>

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

enum class PlaneReferenceKind { Principal, OffsetFromPlane, OffsetFromFace };

struct PlaneReference {
  PlaneReferenceKind kind = PlaneReferenceKind::Principal;
  SketchPlane plane = SketchPlane::XY;
  float offsetMm = 0.0f;
  int parentPlaneId = -1;
  std::string sourceObjectName;
  float distanceMm = 0.0f;
  int faceSign = 1;
};

struct PlaneMetadata {
  std::array<char, 64> name{};
  bool visible = true;
  bool locked = false;
  bool builtIn = false;
};

struct PlaneEntry {
  int id = -1;
  PlaneMetadata meta;
  PlaneReference reference;
};

struct ResolvedPlane {
  SketchPlane plane = SketchPlane::XY;
  float offsetMm = 0.0f;
  bool valid = false;
};

struct SketchEntry {
  Sketch sketch;
  SketchMetadata meta;
  int planeId = -1;
  SketchPlane plane = SketchPlane::XY;
  float offsetMm = 0.0f;
};
