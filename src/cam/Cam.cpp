#include "cam/Cam.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <type_traits>
#include <utility>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include "sketch/Profile.hpp"

namespace {

template <size_t N>
void setName(std::array<char, N>& dest, const std::string& value) {
  std::snprintf(dest.data(), dest.size(), "%s", value.c_str());
}

struct Aabb {
  glm::vec3 min{0.0f};
  glm::vec3 max{0.0f};
  bool valid = false;
};

struct Region2D {
  std::vector<std::vector<glm::vec2>> profiles;
  glm::vec2 min{0.0f};
  glm::vec2 max{0.0f};
  bool valid = false;
};

Aabb meshAabb(const StlMesh& mesh) {
  Aabb box;
  const auto& verts = mesh.vertices();
  if (verts.empty()) return box;
  box.min = verts.front().position;
  box.max = verts.front().position;
  box.valid = true;
  for (const auto& vertex : verts) {
    box.min = glm::min(box.min, vertex.position);
    box.max = glm::max(box.max, vertex.position);
  }
  return box;
}

Region2D regionFromObject(const StlMesh& mesh) {
  Region2D region;
  const Aabb box = meshAabb(mesh);
  if (!box.valid) return region;
  region.min = {box.min.x, box.min.y};
  region.max = {box.max.x, box.max.y};
  region.valid = true;
  region.profiles = {{
      {region.min.x, region.min.y},
      {region.max.x, region.min.y},
      {region.max.x, region.max.y},
      {region.min.x, region.max.y},
      {region.min.x, region.min.y},
  }};
  return region;
}

Region2D regionFromSketch(const SketchEntry& sketch) {
  Region2D region;
  if (sketch.plane != SketchPlane::XY) return region;
  region.profiles = sketch.sketch.closedProfiles();
  if (region.profiles.empty()) return region;

  region.min = region.profiles.front().front();
  region.max = region.min;
  for (const auto& profile : region.profiles) {
    for (const glm::vec2 point : profile) {
      region.min = glm::min(region.min, point);
      region.max = glm::max(region.max, point);
    }
  }
  region.valid = true;
  return region;
}

bool pointInsideStockXY(const CamStockSettings& stock, glm::vec3 point) {
  const glm::vec3 stockMax = stock.originMm + stock.sizeMm;
  return point.x >= stock.originMm.x - 0.01f && point.x <= stockMax.x + 0.01f &&
         point.y >= stock.originMm.y - 0.01f && point.y <= stockMax.y + 0.01f;
}

void appendWarning(std::vector<CamOperationWarning>& warnings,
                   const std::string& message,
                   bool severe) {
  warnings.push_back({message, severe});
}

void pushSegment(std::vector<CamPathSegment>& segments,
                 glm::vec3 start,
                 glm::vec3 end,
                 bool rapid,
                 bool plunge,
                 float* lengthAccumulator) {
  if (glm::length(end - start) <= 1e-5f) return;
  segments.push_back({start, end, rapid, plunge});
  if (lengthAccumulator) {
    *lengthAccumulator += glm::length(end - start);
  }
}

std::vector<float> zPasses(float topZ, float bottomZ, float stepDown) {
  std::vector<float> passes;
  if (!(bottomZ < topZ)) return passes;
  stepDown = std::max(stepDown, 0.05f);
  for (float z = topZ - stepDown; z > bottomZ; z -= stepDown) {
    passes.push_back(z);
  }
  passes.push_back(bottomZ);
  return passes;
}

std::vector<std::pair<float, float>> scanlineIntervals(const Region2D& region, float y) {
  std::vector<float> xs;
  for (const auto& profile : region.profiles) {
    if (profile.size() < 2) continue;
    for (size_t i = 0; i + 1 < profile.size(); ++i) {
      glm::vec2 a = profile[i];
      glm::vec2 b = profile[i + 1];
      if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y)) {
        const float t = (y - a.y) / (b.y - a.y);
        xs.push_back(a.x + (b.x - a.x) * t);
      }
    }
  }
  std::sort(xs.begin(), xs.end());

  std::vector<std::pair<float, float>> intervals;
  for (size_t i = 0; i + 1 < xs.size(); i += 2) {
    if (xs[i + 1] - xs[i] > 0.05f) {
      intervals.emplace_back(xs[i], xs[i + 1]);
    }
  }
  return intervals;
}

std::vector<glm::vec3> drillPointsFromSketch(const SketchEntry& sketch) {
  std::vector<glm::vec3> points;
  if (sketch.plane != SketchPlane::XY) return points;

  for (const auto& elem : sketch.sketch.elements()) {
    std::visit(
        [&](const auto& primitive) {
          using T = std::decay_t<decltype(primitive)>;
          if constexpr (std::is_same_v<T, SketchCircle>) {
            points.push_back({primitive.center.x, primitive.center.y, sketch.offsetMm});
          }
        },
        elem.geometry);
  }

  auto samePoint = [](glm::vec3 a, glm::vec3 b) {
    return glm::length(a - b) <= 0.05f;
  };
  std::vector<glm::vec3> deduped;
  for (const glm::vec3 point : points) {
    bool exists = false;
    for (const glm::vec3 existing : deduped) {
      if (samePoint(existing, point)) {
        exists = true;
        break;
      }
    }
    if (!exists) deduped.push_back(point);
  }
  return deduped;
}

float estimateMinutes(const std::vector<CamPathSegment>& segments,
                      const CamToolPreset& tool) {
  constexpr float kRapidMmPerMin = 4000.0f;
  float minutes = 0.0f;
  for (const auto& segment : segments) {
    const float length = glm::length(segment.end - segment.start);
    const float feed = segment.rapid ? kRapidMmPerMin
                     : (segment.plunge ? std::max(tool.plungeRateMmPerMin, 10.0f)
                                       : std::max(tool.feedRateMmPerMin, 10.0f));
    minutes += length / feed;
  }
  return minutes;
}

glm::vec3 wcsOriginWorld(const CamStockSettings& stock) {
  return stock.originMm + stock.wcsOffsetMm;
}

void addCommonWarnings(CamOperation& operation,
                       const CamStockSettings& stock,
                       const CamToolPreset& tool,
                       const Aabb* sourceBox) {
  const glm::vec3 stockMax = stock.originMm + stock.sizeMm;
  if (tool.diameterMm > std::min(stock.sizeMm.x, stock.sizeMm.y)) {
    appendWarning(operation.warnings, "Tool diameter exceeds stock XY envelope", true);
  }
  if (operation.bottomZMm < stock.originMm.z - 0.01f) {
    appendWarning(operation.warnings, "Toolpath cuts below stock bottom", true);
  }
  if (operation.topZMm > stockMax.z + stock.safeRetractMm + 0.01f) {
    appendWarning(operation.warnings, "Operation top is above stock and clearance envelope", false);
  }
  if (stock.safeRetractMm < 1.0f) {
    appendWarning(operation.warnings, "Safe retract is very small", false);
  }
  if (sourceBox && sourceBox->valid) {
    if (sourceBox->min.x < stock.originMm.x - 0.01f ||
        sourceBox->min.y < stock.originMm.y - 0.01f ||
        sourceBox->max.x > stockMax.x + 0.01f ||
        sourceBox->max.y > stockMax.y + 0.01f ||
        sourceBox->max.z > stockMax.z + 0.01f) {
      appendWarning(operation.warnings, "Source geometry extends outside stock", true);
    }
    if (operation.bottomZMm < sourceBox->min.z - 0.01f) {
      appendWarning(operation.warnings, "Cut depth extends below source geometry minimum Z", true);
    }
  }

  for (const auto& segment : operation.segments) {
    if (!pointInsideStockXY(stock, segment.start) || !pointInsideStockXY(stock, segment.end)) {
      appendWarning(operation.warnings, "Toolpath exits stock XY bounds", true);
      break;
    }
  }
}

bool buildFacing(const CamOperationBuilderState& builder,
                 const CamToolPreset& tool,
                 const CamStockSettings& stock,
                 CamOperation& operation,
                 std::string& error) {
  const float stockTop = stock.originMm.z + stock.sizeMm.z;
  const float stepoverMm = std::max(tool.diameterMm * (builder.stepoverPercent / 100.0f),
                                    tool.diameterMm * 0.15f);
  const float safeZ = builder.topZMm + std::max(builder.clearanceMm, stock.safeRetractMm);
  const float xMin = stock.originMm.x + tool.diameterMm * 0.5f;
  const float xMax = stock.originMm.x + stock.sizeMm.x - tool.diameterMm * 0.5f;
  const float yMin = stock.originMm.y + tool.diameterMm * 0.5f;
  const float yMax = stock.originMm.y + stock.sizeMm.y - tool.diameterMm * 0.5f;

  if (xMax <= xMin || yMax <= yMin) {
    error = "Facing stock footprint is smaller than the current tool";
    return false;
  }

  operation.type = CamOperationType::Facing;
  operation.name = "Facing";
  operation.topZMm = builder.topZMm;
  operation.bottomZMm = builder.topZMm - std::max(builder.depthMm, 0.05f);
  operation.stepDownMm = std::max(builder.stepDownMm, 0.05f);
  operation.stepoverMm = stepoverMm;
  operation.clearanceMm = builder.clearanceMm;

  float length = 0.0f;
  const auto passes = zPasses(operation.topZMm, operation.bottomZMm, operation.stepDownMm);
  bool reverse = false;
  for (float z : passes) {
    for (float y = yMin; y <= yMax + 0.01f; y += stepoverMm) {
      const glm::vec3 entry = reverse ? glm::vec3(xMax, y, safeZ) : glm::vec3(xMin, y, safeZ);
      const glm::vec3 plunge = {entry.x, entry.y, z};
      const glm::vec3 exit = reverse ? glm::vec3(xMin, y, z) : glm::vec3(xMax, y, z);
      pushSegment(operation.segments, entry, plunge, false, true, &length);
      pushSegment(operation.segments, plunge, exit, false, false, &length);
      pushSegment(operation.segments, exit, {exit.x, exit.y, safeZ}, true, false, &length);
      reverse = !reverse;
    }
  }

  if (builder.topZMm > stockTop + 0.01f) {
    appendWarning(operation.warnings, "Facing start plane is above stock top", false);
  }
  return true;
}

bool buildPocket(const CamOperationBuilderState& builder,
                 const CamToolPreset& tool,
                 const CamStockSettings& stock,
                 const std::vector<StlMesh>& sceneObjects,
                 const std::vector<SketchEntry>& sketches,
                 CamOperation& operation,
                 std::string& error) {
  Region2D region;
  Aabb sourceBox;
  if (builder.sourceSketch >= 0 && builder.sourceSketch < static_cast<int>(sketches.size())) {
    region = regionFromSketch(sketches[builder.sourceSketch]);
    if (!region.valid) {
      error = "Pocket source sketch must be on an XY-aligned plane with closed profiles";
      return false;
    }
    sourceBox = {{region.min.x, region.min.y, sketches[builder.sourceSketch].offsetMm},
                 {region.max.x, region.max.y, sketches[builder.sourceSketch].offsetMm}, true};
  } else if (builder.sourceObject >= 0 && builder.sourceObject < static_cast<int>(sceneObjects.size())) {
    region = regionFromObject(sceneObjects[builder.sourceObject]);
    sourceBox = meshAabb(sceneObjects[builder.sourceObject]);
  } else {
    error = "Pocket requires a source object or an XY sketch";
    return false;
  }

  const float stepoverMm = std::max(tool.diameterMm * (builder.stepoverPercent / 100.0f),
                                    tool.diameterMm * 0.15f);
  const float safeZ = builder.topZMm + std::max(builder.clearanceMm, stock.safeRetractMm);

  operation.type = CamOperationType::Pocket;
  operation.name = "Pocket";
  operation.sourceObject = builder.sourceObject;
  operation.sourceSketch = builder.sourceSketch;
  operation.topZMm = builder.topZMm;
  operation.bottomZMm = builder.topZMm - std::max(builder.depthMm, 0.05f);
  operation.stepDownMm = std::max(builder.stepDownMm, 0.05f);
  operation.stepoverMm = stepoverMm;
  operation.clearanceMm = builder.clearanceMm;

  float length = 0.0f;
  const auto passes = zPasses(operation.topZMm, operation.bottomZMm, operation.stepDownMm);
  bool reverse = false;
  for (float z : passes) {
    for (float y = region.min.y + tool.diameterMm * 0.5f;
         y <= region.max.y - tool.diameterMm * 0.5f + 0.01f;
         y += stepoverMm) {
      auto intervals = scanlineIntervals(region, y);
      for (auto [x0, x1] : intervals) {
        x0 += tool.diameterMm * 0.5f;
        x1 -= tool.diameterMm * 0.5f;
        if (x1 <= x0) continue;
        const glm::vec3 safeEntry = reverse ? glm::vec3(x1, y, safeZ) : glm::vec3(x0, y, safeZ);
        const glm::vec3 plunge = {safeEntry.x, safeEntry.y, z};
        const glm::vec3 cutEnd = reverse ? glm::vec3(x0, y, z) : glm::vec3(x1, y, z);
        pushSegment(operation.segments, safeEntry, plunge, false, true, &length);
        pushSegment(operation.segments, plunge, cutEnd, false, false, &length);
        pushSegment(operation.segments, cutEnd, {cutEnd.x, cutEnd.y, safeZ}, true, false, &length);
        reverse = !reverse;
      }
    }
  }

  if (operation.segments.empty()) {
    error = "Pocket region is too small for the current tool";
    return false;
  }

  addCommonWarnings(operation, stock, tool, sourceBox.valid ? &sourceBox : nullptr);
  return true;
}

bool buildContour(const CamOperationBuilderState& builder,
                  const CamToolPreset& tool,
                  const CamStockSettings& stock,
                  const std::vector<StlMesh>& sceneObjects,
                  const std::vector<SketchEntry>& sketches,
                  CamOperation& operation,
                  std::string& error) {
  Region2D region;
  Aabb sourceBox;
  if (builder.sourceSketch >= 0 && builder.sourceSketch < static_cast<int>(sketches.size())) {
    region = regionFromSketch(sketches[builder.sourceSketch]);
    if (!region.valid) {
      error = "Contour source sketch must be on an XY-aligned plane with closed profiles";
      return false;
    }
    sourceBox = {{region.min.x, region.min.y, sketches[builder.sourceSketch].offsetMm},
                 {region.max.x, region.max.y, sketches[builder.sourceSketch].offsetMm}, true};
  } else if (builder.sourceObject >= 0 && builder.sourceObject < static_cast<int>(sceneObjects.size())) {
    region = regionFromObject(sceneObjects[builder.sourceObject]);
    sourceBox = meshAabb(sceneObjects[builder.sourceObject]);
  } else {
    error = "Contour requires a source object or an XY sketch";
    return false;
  }

  operation.type = CamOperationType::Contour;
  operation.name = "Contour";
  operation.sourceObject = builder.sourceObject;
  operation.sourceSketch = builder.sourceSketch;
  operation.topZMm = builder.topZMm;
  operation.bottomZMm = builder.topZMm - std::max(builder.depthMm, 0.05f);
  operation.stepDownMm = std::max(builder.stepDownMm, 0.05f);
  operation.stepoverMm = 0.0f;
  operation.clearanceMm = builder.clearanceMm;

  const float safeZ = builder.topZMm + std::max(builder.clearanceMm, stock.safeRetractMm);
  float length = 0.0f;
  const auto passes = zPasses(operation.topZMm, operation.bottomZMm, operation.stepDownMm);
  for (float z : passes) {
    for (const auto& profile : region.profiles) {
      if (profile.size() < 2) continue;
      const glm::vec2 start2 = profile.front();
      pushSegment(operation.segments, {start2.x, start2.y, safeZ},
                  {start2.x, start2.y, z}, false, true, &length);
      for (size_t i = 1; i < profile.size(); ++i) {
        const glm::vec2 prev = profile[i - 1];
        const glm::vec2 next = profile[i];
        pushSegment(operation.segments, {prev.x, prev.y, z}, {next.x, next.y, z},
                    false, false, &length);
      }
      if (glm::length(profile.front() - profile.back()) > 0.05f) {
        const glm::vec2 last = profile.back();
        pushSegment(operation.segments, {last.x, last.y, z}, {start2.x, start2.y, z},
                    false, false, &length);
      }
      const glm::vec2 end2 = profile.back();
      pushSegment(operation.segments, {end2.x, end2.y, z}, {end2.x, end2.y, safeZ},
                  true, false, &length);
    }
  }

  if (operation.segments.empty()) {
    error = "Contour source does not contain a usable profile";
    return false;
  }

  if (tool.type == CamToolType::Drill) {
    appendWarning(operation.warnings, "Drill tool selected for contour operation", false);
  }
  addCommonWarnings(operation, stock, tool, sourceBox.valid ? &sourceBox : nullptr);
  return true;
}

bool buildDrilling(const CamOperationBuilderState& builder,
                   const CamToolPreset& tool,
                   const CamStockSettings& stock,
                   const std::vector<SketchEntry>& sketches,
                   const std::vector<ReferencePointEntry>& referencePoints,
                   CamOperation& operation,
                   std::string& error) {
  std::vector<glm::vec3> points;
  if (builder.sourceSketch >= 0 && builder.sourceSketch < static_cast<int>(sketches.size())) {
    points = drillPointsFromSketch(sketches[builder.sourceSketch]);
    if (points.empty()) {
      error = "Drilling sketch needs circle centers to drill";
      return false;
    }
  } else {
    for (const auto& point : referencePoints) {
      if (point.meta.visible) points.push_back(point.position);
    }
    if (points.empty()) {
      error = "Drilling requires a sketch with circles or visible reference points";
      return false;
    }
  }

  operation.type = CamOperationType::Drilling;
  operation.name = "Drilling";
  operation.sourceSketch = builder.sourceSketch;
  operation.topZMm = builder.topZMm;
  operation.bottomZMm = builder.topZMm - std::max(builder.depthMm, 0.05f);
  operation.stepDownMm = std::max(builder.stepDownMm, 0.05f);
  operation.clearanceMm = builder.clearanceMm;
  const float safeZ = builder.topZMm + std::max(builder.clearanceMm, stock.safeRetractMm);

  float length = 0.0f;
  const float peck = std::max(builder.stepDownMm, 0.05f);
  for (const glm::vec3 point : points) {
    pushSegment(operation.segments, {point.x, point.y, safeZ},
                {point.x, point.y, builder.topZMm}, false, true, &length);
    for (float z = builder.topZMm - peck; z > operation.bottomZMm; z -= peck) {
      pushSegment(operation.segments, {point.x, point.y, std::max(z + peck, operation.topZMm)},
                  {point.x, point.y, z}, false, true, &length);
      pushSegment(operation.segments, {point.x, point.y, z}, {point.x, point.y, safeZ},
                  true, false, &length);
    }
    pushSegment(operation.segments, {point.x, point.y, safeZ},
                {point.x, point.y, operation.bottomZMm}, false, true, &length);
    pushSegment(operation.segments, {point.x, point.y, operation.bottomZMm},
                {point.x, point.y, safeZ}, true, false, &length);
  }

  if (tool.type != CamToolType::Drill) {
    appendWarning(operation.warnings, "Non-drill tool selected for drilling operation", false);
  }
  addCommonWarnings(operation, stock, tool, nullptr);
  return true;
}

}  // namespace

const char* camOperationTypeLabel(CamOperationType type) {
  switch (type) {
    case CamOperationType::Facing: return "Facing";
    case CamOperationType::Pocket: return "Pocket";
    case CamOperationType::Contour: return "Contour";
    case CamOperationType::Drilling: return "Drilling";
  }
  return "Facing";
}

const char* camToolTypeLabel(CamToolType type) {
  switch (type) {
    case CamToolType::FlatEndMill: return "Flat End Mill";
    case CamToolType::BallEndMill: return "Ball End Mill";
    case CamToolType::Drill: return "Drill";
  }
  return "Flat End Mill";
}

const char* camPostProcessorLabel(CamPostProcessor post) {
  switch (post) {
    case CamPostProcessor::Grbl: return "GRBL";
  }
  return "GRBL";
}

void initializeCamDefaults(std::vector<CamToolPreset>& tools, CamStockSettings& stock,
                           CamOperationBuilderState& builder) {
  if (tools.empty()) {
    CamToolPreset facing;
    setName(facing.name, "6mm Flat - Aluminum");
    facing.type = CamToolType::FlatEndMill;
    facing.diameterMm = 6.0f;
    facing.feedRateMmPerMin = 900.0f;
    facing.plungeRateMmPerMin = 240.0f;
    facing.spindleRpm = 12000.0f;
    facing.maxStepDownMm = 1.2f;
    facing.stepover = 0.55f;
    tools.push_back(facing);

    CamToolPreset detail;
    setName(detail.name, "3mm Flat - Finishing");
    detail.type = CamToolType::FlatEndMill;
    detail.diameterMm = 3.0f;
    detail.feedRateMmPerMin = 650.0f;
    detail.plungeRateMmPerMin = 180.0f;
    detail.spindleRpm = 14000.0f;
    detail.maxStepDownMm = 0.8f;
    detail.stepover = 0.4f;
    tools.push_back(detail);

    CamToolPreset drill;
    setName(drill.name, "5mm Drill");
    drill.type = CamToolType::Drill;
    drill.diameterMm = 5.0f;
    drill.feedRateMmPerMin = 300.0f;
    drill.plungeRateMmPerMin = 180.0f;
    drill.spindleRpm = 5000.0f;
    drill.maxStepDownMm = 3.0f;
    drill.stepover = 1.0f;
    tools.push_back(drill);
  }

  stock.originMm = {-60.0f, -60.0f, 0.0f};
  stock.sizeMm = {120.0f, 120.0f, 40.0f};
  stock.wcsOffsetMm = {0.0f, 0.0f, stock.sizeMm.z};
  stock.safeRetractMm = 8.0f;

  builder.type = CamOperationType::Facing;
  builder.sourceObject = -1;
  builder.sourceSketch = -1;
  builder.toolIndex = 0;
  builder.topZMm = stock.originMm.z + stock.sizeMm.z;
  builder.depthMm = 1.0f;
  builder.stepDownMm = 1.0f;
  builder.stepoverPercent = 55.0f;
  builder.clearanceMm = 5.0f;
}

void resetCamSession(CamOperationBuilderState& builder, std::vector<CamOperation>& operations,
                     int selectedObject, int selectedSketch, const CamStockSettings& stock) {
  operations.clear();
  builder.sourceObject = selectedObject;
  builder.sourceSketch = selectedSketch;
  builder.topZMm = stock.originMm.z + stock.sizeMm.z;
}

bool generateCamOperation(const CamOperationBuilderState& builder,
                          const std::vector<CamToolPreset>& tools,
                          const CamStockSettings& stock,
                          const std::vector<StlMesh>& sceneObjects,
                          const std::vector<SketchEntry>& sketches,
                          const std::vector<ReferencePointEntry>& referencePoints,
                          CamOperation& operation,
                          std::string& error) {
  if (tools.empty()) {
    error = "No CAM tools are defined";
    return false;
  }
  if (builder.toolIndex < 0 || builder.toolIndex >= static_cast<int>(tools.size())) {
    error = "Selected CAM tool is out of range";
    return false;
  }
  if (builder.depthMm <= 0.0f) {
    error = "Depth must be positive";
    return false;
  }

  const CamToolPreset& tool = tools[builder.toolIndex];
  operation = {};
  operation.toolIndex = builder.toolIndex;

  bool built = false;
  switch (builder.type) {
    case CamOperationType::Facing:
      built = buildFacing(builder, tool, stock, operation, error);
      break;
    case CamOperationType::Pocket:
      built = buildPocket(builder, tool, stock, sceneObjects, sketches, operation, error);
      break;
    case CamOperationType::Contour:
      built = buildContour(builder, tool, stock, sceneObjects, sketches, operation, error);
      break;
    case CamOperationType::Drilling:
      built = buildDrilling(builder, tool, stock, sketches, referencePoints, operation, error);
      break;
  }
  if (!built) return false;

  operation.estimatedLengthMm = 0.0f;
  for (const auto& segment : operation.segments) {
    operation.estimatedLengthMm += glm::length(segment.end - segment.start);
  }
  operation.estimatedMinutes = estimateMinutes(operation.segments, tool);
  return true;
}

void appendCamPreviewLines(const CamStockSettings& stock,
                           const std::vector<CamOperation>& operations,
                           std::vector<ColorVertex>& lines) {
  const glm::vec3 stockMin = stock.originMm;
  const glm::vec3 stockMax = stock.originMm + stock.sizeMm;
  const glm::vec4 stockColor{0.55f, 0.55f, 0.65f, 0.85f};
  const glm::vec3 wcs = wcsOriginWorld(stock);

  const std::array<glm::vec3, 8> corners = {{
      {stockMin.x, stockMin.y, stockMin.z},
      {stockMax.x, stockMin.y, stockMin.z},
      {stockMax.x, stockMax.y, stockMin.z},
      {stockMin.x, stockMax.y, stockMin.z},
      {stockMin.x, stockMin.y, stockMax.z},
      {stockMax.x, stockMin.y, stockMax.z},
      {stockMax.x, stockMax.y, stockMax.z},
      {stockMin.x, stockMax.y, stockMax.z},
  }};
  const std::array<std::pair<int, int>, 12> edges = {{
      {0, 1}, {1, 2}, {2, 3}, {3, 0},
      {4, 5}, {5, 6}, {6, 7}, {7, 4},
      {0, 4}, {1, 5}, {2, 6}, {3, 7},
  }};
  for (const auto& edge : edges) {
    lines.push_back({corners[edge.first], stockColor});
    lines.push_back({corners[edge.second], stockColor});
  }

  const float axisLength = std::max({stock.sizeMm.x, stock.sizeMm.y, stock.sizeMm.z, 25.0f}) * 0.15f;
  lines.push_back({wcs, {1.0f, 0.3f, 0.3f, 1.0f}});
  lines.push_back({wcs + glm::vec3(axisLength, 0.0f, 0.0f), {1.0f, 0.3f, 0.3f, 1.0f}});
  lines.push_back({wcs, {0.3f, 1.0f, 0.3f, 1.0f}});
  lines.push_back({wcs + glm::vec3(0.0f, axisLength, 0.0f), {0.3f, 1.0f, 0.3f, 1.0f}});
  lines.push_back({wcs, {0.3f, 0.7f, 1.0f, 1.0f}});
  lines.push_back({wcs + glm::vec3(0.0f, 0.0f, axisLength), {0.3f, 0.7f, 1.0f, 1.0f}});

  for (const auto& operation : operations) {
    bool severe = false;
    for (const auto& warning : operation.warnings) {
      if (warning.severe) {
        severe = true;
        break;
      }
    }

    for (const auto& segment : operation.segments) {
      glm::vec4 color;
      if (severe && !segment.rapid) {
        color = {1.0f, 0.35f, 0.15f, 1.0f};
      } else if (segment.rapid) {
        color = {1.0f, 0.85f, 0.2f, 0.95f};
      } else if (segment.plunge) {
        color = {0.3f, 0.75f, 1.0f, 0.95f};
      } else {
        color = {0.25f, 1.0f, 0.45f, 0.95f};
      }
      lines.push_back({segment.start, color});
      lines.push_back({segment.end, color});
    }
  }
}

bool exportCamGcode(const std::string& path,
                    CamPostProcessor post,
                    const CamStockSettings& stock,
                    const std::vector<CamToolPreset>& tools,
                    const std::vector<CamOperation>& operations,
                    std::string& error) {
  if (post != CamPostProcessor::Grbl) {
    error = "Unsupported CAM post processor";
    return false;
  }
  if (operations.empty()) {
    error = "No CAM operations queued";
    return false;
  }

  std::ofstream out(path);
  if (!out) {
    error = "Failed to open G-code output path";
    return false;
  }

  out << "(camster GRBL post)\n";
  out << "G21\n";
  out << "G17\n";
  out << "G90\n";
  out << "G94\n";
  out << "G54\n";

  const glm::vec3 wcs = wcsOriginWorld(stock);
  int lastTool = -1;
  bool spindleOn = false;
  for (size_t i = 0; i < operations.size(); ++i) {
    const CamOperation& operation = operations[i];
    if (operation.toolIndex < 0 || operation.toolIndex >= static_cast<int>(tools.size())) {
      error = "Operation references an invalid tool";
      return false;
    }
    const CamToolPreset& tool = tools[operation.toolIndex];
    out << "\n(" << operation.name << " " << (i + 1) << ")\n";
    if (operation.toolIndex != lastTool) {
      out << "(T" << (operation.toolIndex + 1) << " - " << tool.name.data() << ")\n";
      lastTool = operation.toolIndex;
    }
    out << "S" << static_cast<int>(std::round(tool.spindleRpm)) << " M3\n";
    spindleOn = true;

    if (!operation.segments.empty()) {
      const glm::vec3 first = operation.segments.front().start - wcs;
      out << "G0 X" << first.x << " Y" << first.y << " Z" << first.z << "\n";
    }

    bool firstLinear = true;
    bool firstPlunge = true;
    for (const auto& segment : operation.segments) {
      const glm::vec3 end = segment.end - wcs;
      if (segment.rapid) {
        out << "G0 X" << end.x << " Y" << end.y << " Z" << end.z << "\n";
      } else if (segment.plunge) {
        if (firstPlunge) {
          out << "G1 F" << tool.plungeRateMmPerMin << "\n";
          firstPlunge = false;
        }
        out << "G1 X" << end.x << " Y" << end.y << " Z" << end.z << "\n";
      } else {
        if (firstLinear) {
          out << "G1 F" << tool.feedRateMmPerMin << "\n";
          firstLinear = false;
        }
        out << "G1 X" << end.x << " Y" << end.y << " Z" << end.z << "\n";
      }
    }
    out << "G0 Z" << (operation.topZMm + std::max(operation.clearanceMm, stock.safeRetractMm) - wcs.z)
        << "\n";
  }

  if (spindleOn) {
    out << "M5\n";
  }
  out << "G0 X0 Y0\n";
  out << "M30\n";
  return true;
}