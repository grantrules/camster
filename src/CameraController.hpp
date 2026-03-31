#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// Orbit camera that rotates around a target point using spherical coordinates
// (yaw/pitch/distance).  Produces view and projection matrices consumed by the
// renderer each frame.
class CameraController {
 public:
  enum class Orientation {
    Front,
    Back,
    Left,
    Right,
    Top,
    Bottom,
    Isometric,
  };

  void beginRotate(double x, double y);
  void rotateTo(double x, double y);
  void endRotate();
  void zoom(float wheelDelta);
  void snap(Orientation orientation);

  glm::mat4 viewMatrix() const;
  glm::mat4 projectionMatrix(float aspect) const;

 private:
  glm::vec3 target_ = glm::vec3(0.0f);
  float yaw_ = 0.7f;
  float pitch_ = 0.5f;
  float distance_ = 4.0f;

  bool rotating_ = false;
  glm::vec2 lastMouse_ = glm::vec2(0.0f);
};
