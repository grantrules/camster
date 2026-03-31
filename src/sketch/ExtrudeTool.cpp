#include "sketch/ExtrudeTool.hpp"

#include <cmath>

#include <glm/geometric.hpp>

#include "sketch/Extrude.hpp"

namespace {
constexpr glm::vec4 kPreviewColor{0.4f, 0.9f, 0.4f, 1.0f};   // green
constexpr glm::vec4 kEdgeColor{0.2f, 0.7f, 0.2f, 1.0f};      // darker green
constexpr int kCircleSegs = 64;
constexpr float kPi = 3.14159265358979f;

glm::vec3 normalForPlane(SketchPlane plane) {
  switch (plane) {
    case SketchPlane::XY: return {0, 0, 1};
    case SketchPlane::XZ: return {0, 1, 0};
    case SketchPlane::YZ: return {1, 0, 0};
  }
  return {0, 0, 1};
}
}  // namespace

void ExtrudeTool::begin(std::vector<std::vector<glm::vec2>> profiles, SketchPlane plane) {
  profiles_ = std::move(profiles);
  plane_ = plane;
  normal_ = normalForPlane(plane);
  active_ = true;
  dragging_ = false;
  distance_ = 0.0f;

  // Compute centroid of all profile points.
  glm::vec2 sum{0.0f};
  int count = 0;
  for (const auto& prof : profiles_) {
    for (const auto& pt : prof) {
      sum += pt;
      ++count;
    }
  }
  if (count > 0) sum /= static_cast<float>(count);
  centroid3D_ = toWorld(sum, plane_);
}

float ExtrudeTool::axisParam(glm::vec3 rayOrigin, glm::vec3 rayDir) const {
  // Closest approach between the extrude axis and the mouse ray.
  // Axis: P(t) = centroid3D_ + t * normal_
  // Ray:  Q(s) = rayOrigin + s * rayDir
  const glm::vec3 w = centroid3D_ - rayOrigin;
  const float a = glm::dot(normal_, normal_);   // = 1
  const float b = glm::dot(normal_, rayDir);
  const float c = glm::dot(rayDir, rayDir);     // = 1 if normalized
  const float d = glm::dot(normal_, w);
  const float e = glm::dot(rayDir, w);

  const float denom = a * c - b * b;
  if (std::abs(denom) < 1e-8f) return 0.0f;  // parallel

  return (b * e - c * d) / denom;
}

void ExtrudeTool::mouseDown(glm::vec3 rayOrigin, glm::vec3 rayDir) {
  if (!active_) return;
  dragging_ = true;
  dragStartDistance_ = distance_;
  dragRefParam_ = axisParam(rayOrigin, rayDir);
}

void ExtrudeTool::mouseMove(glm::vec3 rayOrigin, glm::vec3 rayDir) {
  if (!active_ || !dragging_) return;
  const float param = axisParam(rayOrigin, rayDir);
  distance_ = dragStartDistance_ + (param - dragRefParam_);
}

void ExtrudeTool::mouseUp() { dragging_ = false; }

void ExtrudeTool::setDistance(float mm) { distance_ = mm; }

float ExtrudeTool::distance() const { return distance_; }

bool ExtrudeTool::active() const { return active_; }

SketchPlane ExtrudeTool::plane() const { return plane_; }

const std::vector<std::vector<glm::vec2>>& ExtrudeTool::profiles() const { return profiles_; }

void ExtrudeTool::cancel() {
  active_ = false;
  dragging_ = false;
  distance_ = 0.0f;
  profiles_.clear();
}

StlMesh ExtrudeTool::confirm() {
  auto mesh = extrudeMesh(profiles_, plane_, distance_);
  active_ = false;
  dragging_ = false;
  distance_ = 0.0f;
  profiles_.clear();
  return mesh;
}

void ExtrudeTool::appendPreview(std::vector<ColorVertex>& lines) const {
  if (!active_) return;

  const glm::vec3 offset = normal_ * distance_;

  for (const auto& prof : profiles_) {
    const size_t n = prof.size();
    if (n < 2) continue;

    // Front outline.
    for (size_t i = 0; i + 1 < n; ++i) {
      lines.push_back({toWorld(prof[i], plane_), kPreviewColor});
      lines.push_back({toWorld(prof[i + 1], plane_), kPreviewColor});
    }

    // Back outline (offset along normal).
    for (size_t i = 0; i + 1 < n; ++i) {
      lines.push_back({toWorld(prof[i], plane_) + offset, kPreviewColor});
      lines.push_back({toWorld(prof[i + 1], plane_) + offset, kPreviewColor});
    }

    // Connecting edges (every Nth vertex to avoid clutter).
    const size_t step = std::max(size_t(1), n / 16);
    for (size_t i = 0; i < n; i += step) {
      const glm::vec3 base = toWorld(prof[i], plane_);
      lines.push_back({base, kEdgeColor});
      lines.push_back({base + offset, kEdgeColor});
    }
  }
}
