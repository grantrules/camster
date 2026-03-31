#include "CameraController.hpp"

#include <algorithm>
#include <cmath>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace {
// Pitch limits prevent the camera from flipping upside-down at the poles.
constexpr float kMinPitch = -1.5f;
constexpr float kMaxPitch = 1.5f;
constexpr float kMinDistance = 0.2f;
constexpr float kMaxDistance = 500.0f;
}  // namespace

void CameraController::beginRotate(double x, double y) {
  rotating_ = true;
  lastMouse_ = glm::vec2(static_cast<float>(x), static_cast<float>(y));
}

void CameraController::rotateTo(double x, double y) {
  if (!rotating_) {
    return;
  }

  const glm::vec2 current(static_cast<float>(x), static_cast<float>(y));
  const glm::vec2 delta = current - lastMouse_;
  lastMouse_ = current;

  yaw_ += delta.x * 0.01f;
  pitch_ += delta.y * 0.01f;
  pitch_ = std::clamp(pitch_, kMinPitch, kMaxPitch);
}

void CameraController::endRotate() { rotating_ = false; }

void CameraController::zoom(float wheelDelta) {
  // Multiplicative zoom feels more natural than additive: small scrolls near
  // the model give fine control, large scrolls far away cover ground quickly.
  distance_ *= (1.0f - wheelDelta * 0.1f);
  distance_ = std::clamp(distance_, kMinDistance, kMaxDistance);
}

void CameraController::snap(Orientation orientation) {
  switch (orientation) {
    case Orientation::Front:
      yaw_ = 0.0f;
      pitch_ = 0.0f;
      break;
    case Orientation::Back:
      yaw_ = 3.1415926f;
      pitch_ = 0.0f;
      break;
    case Orientation::Left:
      yaw_ = -1.5707963f;
      pitch_ = 0.0f;
      break;
    case Orientation::Right:
      yaw_ = 1.5707963f;
      pitch_ = 0.0f;
      break;
    case Orientation::Top:
      pitch_ = -1.3f;
      break;
    case Orientation::Bottom:
      pitch_ = 1.3f;
      break;
    case Orientation::Isometric:
      yaw_ = 0.7853981f;
      pitch_ = -0.6154797f;
      break;
  }
}

glm::mat4 CameraController::viewMatrix() const {
  const glm::vec3 eye(
      target_.x + distance_ * std::cos(pitch_) * std::sin(yaw_),
      target_.y + distance_ * std::sin(pitch_),
      target_.z + distance_ * std::cos(pitch_) * std::cos(yaw_));
  return glm::lookAt(eye, target_, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 CameraController::projectionMatrix(float aspect) const {
  // Vulkan clip space has Y pointing downward (opposite of OpenGL).  Flipping
  // proj[1][1] corrects for this so the scene isn't rendered upside-down.
  glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.01f, 2000.0f);
  proj[1][1] *= -1.0f;
  return proj;
}
