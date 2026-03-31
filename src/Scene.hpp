#pragma once

#include <cmath>
#include <optional>

#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// The two top-level interaction modes.
enum class SceneMode { View3D, Sketch };

// The three principal planes a sketch can live on.
enum class SketchPlane { XY, XZ, YZ };

// ---------------------------------------------------------------------------
// Coordinate helpers
// ---------------------------------------------------------------------------

// Map a 2D sketch coordinate to its 3D world position on the given plane.
inline glm::vec3 toWorld(glm::vec2 p, SketchPlane plane) {
  switch (plane) {
    case SketchPlane::XY: return {p.x, p.y, 0.0f};
    case SketchPlane::XZ: return {p.x, 0.0f, p.y};
    case SketchPlane::YZ: return {0.0f, p.x, p.y};
  }
  return {};
}

// Extract the 2D sketch coordinate from a 3D point on the given plane.
inline glm::vec2 toPlane(glm::vec3 p, SketchPlane plane) {
  switch (plane) {
    case SketchPlane::XY: return {p.x, p.y};
    case SketchPlane::XZ: return {p.x, p.z};
    case SketchPlane::YZ: return {p.y, p.z};
  }
  return {};
}

// ---------------------------------------------------------------------------
// Ray-plane intersection for mouse picking
// ---------------------------------------------------------------------------

// Cast a ray from screen-space pixel (screenX, screenY) through the camera
// and intersect with the given sketch plane.  Returns the 3D hit point or
// nullopt if the ray is (nearly) parallel to the plane.
inline std::optional<glm::vec3> rayPlaneHit(float screenX, float screenY,
                                            float viewportW, float viewportH,
                                            const glm::mat4& view,
                                            const glm::mat4& projection,
                                            SketchPlane plane) {
  // Screen → Vulkan NDC (Y-down in Vulkan, matching our flipped projection).
  const float ndcX = (2.0f * screenX / viewportW) - 1.0f;
  const float ndcY = (2.0f * screenY / viewportH) - 1.0f;

  const glm::mat4 invVP = glm::inverse(projection * view);
  glm::vec4 nearW = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
  glm::vec4 farW  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
  nearW /= nearW.w;
  farW  /= farW.w;

  const glm::vec3 origin(nearW);
  const glm::vec3 dir = glm::normalize(glm::vec3(farW - nearW));

  // Select the plane normal component.
  float denom = 0.0f;
  float originDist = 0.0f;
  switch (plane) {
    case SketchPlane::XY: denom = dir.z; originDist = origin.z; break;
    case SketchPlane::XZ: denom = dir.y; originDist = origin.y; break;
    case SketchPlane::YZ: denom = dir.x; originDist = origin.x; break;
  }

  if (std::abs(denom) < 1e-8f) return std::nullopt;

  const float t = -originDist / denom;
  if (t < 0.0f) return std::nullopt;

  return origin + t * dir;
}
