#include "Gizmo.hpp"

namespace {
// Axis length and arrowhead proportions.
constexpr float kLen = 1.5f;
constexpr float kHeadLen = 0.15f;
constexpr float kHeadWidth = 0.05f;

// Axis colors (RGB = XYZ).
constexpr glm::vec4 kRed{1.0f, 0.0f, 0.0f, 1.0f};
constexpr glm::vec4 kGreen{0.0f, 1.0f, 0.0f, 1.0f};
constexpr glm::vec4 kBlue{0.0f, 0.0f, 1.0f, 1.0f};

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
}  // namespace

void Gizmo::appendLines(std::vector<ColorVertex>& lines) const {
  // X axis (red).
  appendArrow(lines, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, kRed);
  // Y axis (green).
  appendArrow(lines, {0, 1, 0}, {1, 0, 0}, {0, 0, 1}, kGreen);
  // Z axis (blue).
  appendArrow(lines, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}, kBlue);
}
