#include "sketch/Sketch.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr int kCircleSegments = 64;
constexpr float kPi = 3.14159265358979f;

void appendLine2D(std::vector<ColorVertex>& lines, glm::vec2 a, glm::vec2 b,
                  SketchPlane plane, const glm::vec4& color) {
  lines.push_back({toWorld(a, plane), color});
  lines.push_back({toWorld(b, plane), color});
}

// Append one primitive's lines using the given color.
void appendPrimLines(std::vector<ColorVertex>& lines, const SketchPrimitive& prim,
                     SketchPlane plane, const glm::vec4& color) {
  std::visit(
      [&](const auto& p) {
        using T = std::decay_t<decltype(p)>;

        if constexpr (std::is_same_v<T, SketchLine>) {
          appendLine2D(lines, p.start, p.end, plane, color);

        } else if constexpr (std::is_same_v<T, SketchRect>) {
          const glm::vec2 a = p.min;
          const glm::vec2 b = {p.max.x, p.min.y};
          const glm::vec2 c = p.max;
          const glm::vec2 d = {p.min.x, p.max.y};
          appendLine2D(lines, a, b, plane, color);
          appendLine2D(lines, b, c, plane, color);
          appendLine2D(lines, c, d, plane, color);
          appendLine2D(lines, d, a, plane, color);

        } else if constexpr (std::is_same_v<T, SketchCircle>) {
          for (int i = 0; i < kCircleSegments; ++i) {
            const float a0 = 2.0f * kPi * static_cast<float>(i) / kCircleSegments;
            const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / kCircleSegments;
            const glm::vec2 v0 = p.center + p.radius * glm::vec2(std::cos(a0), std::sin(a0));
            const glm::vec2 v1 = p.center + p.radius * glm::vec2(std::cos(a1), std::sin(a1));
            appendLine2D(lines, v0, v1, plane, color);
          }

        } else if constexpr (std::is_same_v<T, SketchArc>) {
          const int segments = std::max(4, static_cast<int>(std::abs(p.sweepAngle) /
                                                            (2.0f * kPi) * kCircleSegments));
          for (int i = 0; i < segments; ++i) {
            const float t0 = p.startAngle + p.sweepAngle * static_cast<float>(i) / segments;
            const float t1 = p.startAngle + p.sweepAngle * static_cast<float>(i + 1) / segments;
            const glm::vec2 v0 = p.center + p.radius * glm::vec2(std::cos(t0), std::sin(t0));
            const glm::vec2 v1 = p.center + p.radius * glm::vec2(std::cos(t1), std::sin(t1));
            appendLine2D(lines, v0, v1, plane, color);
          }
        }
      },
      prim);
}

// Point-to-segment distance.
float pointToSegment(glm::vec2 pt, glm::vec2 a, glm::vec2 b) {
  const glm::vec2 ab = b - a;
  const float len2 = glm::dot(ab, ab);
  if (len2 < 1e-12f) return glm::length(pt - a);
  const float t = glm::clamp(glm::dot(pt - a, ab) / len2, 0.0f, 1.0f);
  return glm::length(pt - (a + t * ab));
}

float distanceToPrimitive(glm::vec2 pt, const SketchPrimitive& prim) {
  return std::visit(
      [&](const auto& p) -> float {
        using T = std::decay_t<decltype(p)>;

        if constexpr (std::is_same_v<T, SketchLine>) {
          return pointToSegment(pt, p.start, p.end);

        } else if constexpr (std::is_same_v<T, SketchRect>) {
          const glm::vec2 a = p.min;
          const glm::vec2 b = {p.max.x, p.min.y};
          const glm::vec2 c = p.max;
          const glm::vec2 d = {p.min.x, p.max.y};
          return std::min({pointToSegment(pt, a, b), pointToSegment(pt, b, c),
                           pointToSegment(pt, c, d), pointToSegment(pt, d, a)});

        } else if constexpr (std::is_same_v<T, SketchCircle>) {
          return std::abs(glm::length(pt - p.center) - p.radius);

        } else if constexpr (std::is_same_v<T, SketchArc>) {
          const float dist = glm::length(pt - p.center);
          float angle = std::atan2(pt.y - p.center.y, pt.x - p.center.x);
          float rel = angle - p.startAngle;
          // Normalize into [0, 2pi).
          while (rel < 0.0f) rel += 2.0f * kPi;
          while (rel > 2.0f * kPi) rel -= 2.0f * kPi;

          if (rel <= p.sweepAngle) {
            return std::abs(dist - p.radius);
          }
          // Outside sweep → distance to endpoints.
          const glm::vec2 s = p.center + p.radius * glm::vec2(std::cos(p.startAngle),
                                                               std::sin(p.startAngle));
          const glm::vec2 e =
              p.center + p.radius * glm::vec2(std::cos(p.startAngle + p.sweepAngle),
                                              std::sin(p.startAngle + p.sweepAngle));
          return std::min(glm::length(pt - s), glm::length(pt - e));
        }
      },
      prim);
}
}  // namespace

void Sketch::addPrimitive(SketchPrimitive prim) {
  primitives_.push_back(std::move(prim));
  selected_.push_back(false);
}

void Sketch::clear() {
  primitives_.clear();
  selected_.clear();
}

bool Sketch::empty() const { return primitives_.empty(); }

const std::vector<SketchPrimitive>& Sketch::primitives() const { return primitives_; }

// --- Selection ---

void Sketch::select(size_t idx) {
  clearSelection();
  if (idx < selected_.size()) selected_[idx] = true;
}

void Sketch::toggleSelect(size_t idx) {
  if (idx < selected_.size()) selected_[idx] = !selected_[idx];
}

void Sketch::clearSelection() {
  std::fill(selected_.begin(), selected_.end(), false);
}

bool Sketch::isSelected(size_t idx) const {
  return idx < selected_.size() && selected_[idx];
}

bool Sketch::hasSelection() const {
  return std::any_of(selected_.begin(), selected_.end(), [](bool s) { return s; });
}

std::vector<size_t> Sketch::selectedIndices() const {
  std::vector<size_t> out;
  for (size_t i = 0; i < selected_.size(); ++i) {
    if (selected_[i]) out.push_back(i);
  }
  return out;
}

std::optional<size_t> Sketch::hitTest(glm::vec2 pos, float threshold) const {
  float bestDist = std::numeric_limits<float>::max();
  std::optional<size_t> bestIdx;
  for (size_t i = 0; i < primitives_.size(); ++i) {
    float d = distanceToPrimitive(pos, primitives_[i]);
    if (d < threshold && d < bestDist) {
      bestDist = d;
      bestIdx = i;
    }
  }
  return bestIdx;
}

namespace {
std::pair<glm::vec2, glm::vec2> primBBox(const SketchPrimitive& prim) {
  return std::visit(
      [](const auto& p) -> std::pair<glm::vec2, glm::vec2> {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, SketchLine>) {
          return {glm::min(p.start, p.end), glm::max(p.start, p.end)};
        } else if constexpr (std::is_same_v<T, SketchRect>) {
          return {p.min, p.max};
        } else if constexpr (std::is_same_v<T, SketchCircle>) {
          glm::vec2 r(p.radius);
          return {p.center - r, p.center + r};
        } else if constexpr (std::is_same_v<T, SketchArc>) {
          // Conservative: full circle bbox.
          glm::vec2 r(p.radius);
          return {p.center - r, p.center + r};
        }
      },
      prim);
}

bool bboxOverlaps(glm::vec2 aMin, glm::vec2 aMax, glm::vec2 bMin, glm::vec2 bMax) {
  return aMin.x <= bMax.x && aMax.x >= bMin.x && aMin.y <= bMax.y && aMax.y >= bMin.y;
}
}  // namespace

void Sketch::selectInRect(glm::vec2 rectMin, glm::vec2 rectMax) {
  clearSelection();
  addToSelectInRect(rectMin, rectMax);
}

void Sketch::addToSelectInRect(glm::vec2 rectMin, glm::vec2 rectMax) {
  const glm::vec2 rMin = glm::min(rectMin, rectMax);
  const glm::vec2 rMax = glm::max(rectMin, rectMax);
  for (size_t i = 0; i < primitives_.size(); ++i) {
    auto [pMin, pMax] = primBBox(primitives_[i]);
    if (bboxOverlaps(rMin, rMax, pMin, pMax)) {
      selected_[i] = true;
    }
  }
}

void Sketch::appendLines(std::vector<ColorVertex>& lines, SketchPlane plane,
                         const glm::vec4& color,
                         const glm::vec4& selectedColor) const {
  for (size_t i = 0; i < primitives_.size(); ++i) {
    const glm::vec4& c = (i < selected_.size() && selected_[i]) ? selectedColor : color;
    appendPrimLines(lines, primitives_[i], plane, c);
  }
}
