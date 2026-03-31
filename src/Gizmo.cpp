#include "Gizmo.hpp"

#include <glm/geometric.hpp>
#include <limits>

namespace {
// Axis length and arrowhead proportions.
constexpr float kLen = 1.5f;
constexpr float kHeadLen = 0.15f;
constexpr float kHeadWidth = 0.05f;

// Axis colors (RGB = XYZ).
constexpr glm::vec4 kRed{1.0f, 0.0f, 0.0f, 1.0f};
constexpr glm::vec4 kGreen{0.0f, 1.0f, 0.0f, 1.0f};
constexpr glm::vec4 kBlue{0.0f, 0.0f, 1.0f, 1.0f};

// Plane indicator colors (dimmer than axes).
constexpr glm::vec4 kRedDim{0.6f, 0.0f, 0.0f, 1.0f};
constexpr glm::vec4 kGreenDim{0.0f, 0.6f, 0.0f, 1.0f};
constexpr glm::vec4 kBlueDim{0.0f, 0.0f, 0.6f, 1.0f};

// Bright plane colors for hover highlighting.
constexpr glm::vec4 kRedBright{1.0f, 0.3f, 0.3f, 1.0f};
constexpr glm::vec4 kGreenBright{0.3f, 1.0f, 0.3f, 1.0f};
constexpr glm::vec4 kBlueBright{0.3f, 0.3f, 1.0f, 1.0f};

// Helper: emit a shaft + 4-line arrowhead along `axis` in `color`.
void appendArrow(std::vector<ColorVertex>& lines, glm::vec3 axis,
                 glm::vec3 perp1, glm::vec3 perp2, const glm::vec4& color) {
  const glm::vec3 tip = axis * kLen;
  const glm::vec3 base = axis * (kLen - kHeadLen);

  // Shaft: origin → tip.
  lines.push_back({{0.0f, 0.0f, 0.0f}, color});
  lines.push_back({tip, color});

  // Arrowhead: four lines converging on the tip.
  lines.push_back({base + perp1 * kHeadWidth, color});
  lines.push_back({tip, color});
  lines.push_back({base - perp1 * kHeadWidth, color});
  lines.push_back({tip, color});
  lines.push_back({base + perp2 * kHeadWidth, color});
  lines.push_back({tip, color});
  lines.push_back({base - perp2 * kHeadWidth, color});
  lines.push_back({tip, color});
}

// Helper: emit a square outline at origin in the given plane.
void appendPlaneSquare(std::vector<ColorVertex>& lines, SketchPlane plane,
                       const glm::vec4& color, float size) {
  const float half = size * 0.5f;

  glm::vec3 corners[4];
  if (plane == SketchPlane::XY) {
    // XY plane: z = 0
    corners[0] = {-half, -half, 0.0f};
    corners[1] = {half, -half, 0.0f};
    corners[2] = {half, half, 0.0f};
    corners[3] = {-half, half, 0.0f};
  } else if (plane == SketchPlane::XZ) {
    // XZ plane: y = 0
    corners[0] = {-half, 0.0f, -half};
    corners[1] = {half, 0.0f, -half};
    corners[2] = {half, 0.0f, half};
    corners[3] = {-half, 0.0f, half};
  } else {  // YZ
    // YZ plane: x = 0
    corners[0] = {0.0f, -half, -half};
    corners[1] = {0.0f, half, -half};
    corners[2] = {0.0f, half, half};
    corners[3] = {0.0f, -half, half};
  }

  // Four edges of the square.
  for (int i = 0; i < 4; i++) {
    lines.push_back({corners[i], color});
    lines.push_back({corners[(i + 1) % 4], color});
  }
}

// Test if ray hits an axis-aligned square in the given plane at origin.
// Returns the intersection distance along the ray, or -1 if no hit.
float rayHitsSquare(glm::vec3 rayOrigin, glm::vec3 rayDir, SketchPlane plane,
                    float size) {
  const float half = size * 0.5f;
  const glm::vec3 planeNorm = planeNormal(plane);

  // Ray-plane intersection: rayOrigin + t*rayDir intersects plane.
  const float denom = glm::dot(rayDir, planeNorm);
  if (std::abs(denom) < 1e-8f) return -1.0f;  // Ray parallel to plane.

  const float t = -glm::dot(rayOrigin, planeNorm) / denom;
  if (t < 0.0f) return -1.0f;  // Intersection behind ray origin.

  const glm::vec3 hitPoint = rayOrigin + t * rayDir;

  // Check if hit point is within the square bounds.
  if (plane == SketchPlane::XY) {
    if (std::abs(hitPoint.x) <= half && std::abs(hitPoint.y) <= half)
      return t;
  } else if (plane == SketchPlane::XZ) {
    if (std::abs(hitPoint.x) <= half && std::abs(hitPoint.z) <= half)
      return t;
  } else {  // YZ
    if (std::abs(hitPoint.y) <= half && std::abs(hitPoint.z) <= half)
      return t;
  }

  return -1.0f;
}
}  // namespace

void Gizmo::appendLines(std::vector<ColorVertex>& lines) const {
  // X axis (red).
  appendArrow(lines, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, kRed);
  // Y axis (green).
  appendArrow(lines, {0, 1, 0}, {1, 0, 0}, {0, 0, 1}, kGreen);
  // Z axis (blue).
  appendArrow(lines, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}, kBlue);

  // Plane indicators: small squares positioned at origin, one per plane.
  appendPlaneSquare(lines, SketchPlane::XY, kRedDim, kPlaneSize);
  appendPlaneSquare(lines, SketchPlane::XZ, kGreenDim, kPlaneSize);
  appendPlaneSquare(lines, SketchPlane::YZ, kBlueDim, kPlaneSize);
}

void Gizmo::appendHighlightedPlane(std::vector<ColorVertex>& lines,
                                   SketchPlane plane) const {
  // Render the highlighted plane with a brighter color and slightly larger.
  const float highlightedSize = kPlaneSize * 1.2f;
  const glm::vec4 brightColor = (plane == SketchPlane::XY)   ? kRedBright
                                : (plane == SketchPlane::XZ) ? kGreenBright
                                                             : kBlueBright;
  appendPlaneSquare(lines, plane, brightColor, highlightedSize);
}

std::optional<SketchPlane> Gizmo::rayHitsPlaneIndicator(glm::vec3 rayOrigin,
                                                        glm::vec3 rayDir) const {
  // Normalize ray direction.
  rayDir = glm::normalize(rayDir);

  // Test all three planes, return the closest one.
  float bestDist = std::numeric_limits<float>::max();
  std::optional<SketchPlane> bestPlane;

  for (SketchPlane plane : {SketchPlane::XY, SketchPlane::XZ, SketchPlane::YZ}) {
    float t = rayHitsSquare(rayOrigin, rayDir, plane, kPlaneSize);
    if (t >= 0.0f && t < bestDist) {
      bestDist = t;
      bestPlane = plane;
    }
  }

  return bestPlane;
}
