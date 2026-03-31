#include "sketch/Sketch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "sketch/Profile.hpp"

namespace {
constexpr int kCircleSegments = 64;
constexpr float kPi = 3.14159265358979f;

// --- Color palette based on constraint status ---
// Unconstrained (no constraints): blue
constexpr glm::vec4 kColorUnconstrained{0.27f, 0.53f, 1.0f, 1.0f};
// Under-constrained (some constraints, DOF > 0): green
constexpr glm::vec4 kColorUnder{0.27f, 1.0f, 0.53f, 1.0f};
// Fully constrained (DOF == 0): white
constexpr glm::vec4 kColorFully{1.0f, 1.0f, 1.0f, 1.0f};
// Over-constrained (conflicting): red
constexpr glm::vec4 kColorOver{1.0f, 0.2f, 0.2f, 1.0f};
// Construction element: orange, semi-transparent
constexpr glm::vec4 kColorConstruction{1.0f, 0.6f, 0.0f, 0.5f};
// Selected element: orange, opaque
constexpr glm::vec4 kColorSelected{1.0f, 0.6f, 0.0f, 1.0f};
// Constraint annotation lines
constexpr glm::vec4 kColorAnnotation{0.8f, 0.8f, 0.2f, 0.8f};
constexpr float kDimensionOffset = 3.0f;

std::string formatDimensionText(float valueMm, Unit unit, const char* prefix = nullptr) {
  char buffer[64] = {};
  const float displayValue = fromMm(valueMm, unit);
  if (prefix && prefix[0] != '\0') {
    std::snprintf(buffer, sizeof(buffer), "%s%.3f %s", prefix, displayValue,
                  unitSuffix(unit));
  } else {
    std::snprintf(buffer, sizeof(buffer), "%.3f %s", displayValue, unitSuffix(unit));
  }
  return std::string(buffer);
}

glm::vec4 colorForElement(const SketchElement& elem, bool selected) {
  if (selected) return kColorSelected;
  if (elem.construction) return kColorConstruction;
  switch (elem.status) {
    case ConstraintStatus::Unconstrained:    return kColorUnconstrained;
    case ConstraintStatus::UnderConstrained: return kColorUnder;
    case ConstraintStatus::FullyConstrained: return kColorFully;
    case ConstraintStatus::OverConstrained:  return kColorOver;
  }
  return kColorUnconstrained;
}

void appendLine2D(std::vector<ColorVertex>& lines, glm::vec2 a, glm::vec2 b,
                  SketchPlane plane, const glm::vec4& color) {
  lines.push_back({toWorld(a, plane), color});
  lines.push_back({toWorld(b, plane), color});
}

// For construction elements, draw dashed lines (every other segment).
void appendLine2DDashed(std::vector<ColorVertex>& lines, glm::vec2 a, glm::vec2 b,
                        SketchPlane plane, const glm::vec4& color, float dashLen = 2.0f) {
  const glm::vec2 dir = b - a;
  const float len = glm::length(dir);
  if (len < 1e-6f) return;
  int segments = static_cast<int>(len / dashLen);
  for (int i = 0; i < segments; i += 2) {
    float t0 = static_cast<float>(i) * dashLen / len;
    float t1 = std::min(static_cast<float>(i + 1) * dashLen / len, 1.0f);
    appendLine2D(lines, a + dir * t0, a + dir * t1, plane, color);
  }
}

// Append one primitive's lines using the given color.  If `dashed` is true,
// draw dashed segments (used for construction elements).
void appendPrimLines(std::vector<ColorVertex>& lines, const SketchPrimitive& prim,
                     SketchPlane plane, const glm::vec4& color, bool dashed = false) {
  auto drawLine = [&](glm::vec2 a, glm::vec2 b) {
    if (dashed)
      appendLine2DDashed(lines, a, b, plane, color);
    else
      appendLine2D(lines, a, b, plane, color);
  };

  std::visit(
      [&](const auto& p) {
        using T = std::decay_t<decltype(p)>;

        if constexpr (std::is_same_v<T, SketchLine>) {
          drawLine(p.start, p.end);

        } else if constexpr (std::is_same_v<T, SketchRect>) {
          const glm::vec2 a = p.min;
          const glm::vec2 b = {p.max.x, p.min.y};
          const glm::vec2 c = p.max;
          const glm::vec2 d = {p.min.x, p.max.y};
          drawLine(a, b);
          drawLine(b, c);
          drawLine(c, d);
          drawLine(d, a);

        } else if constexpr (std::is_same_v<T, SketchCircle>) {
          for (int i = 0; i < kCircleSegments; ++i) {
            const float a0 = 2.0f * kPi * static_cast<float>(i) / kCircleSegments;
            const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / kCircleSegments;
            const glm::vec2 v0 = p.center + p.radius * glm::vec2(std::cos(a0), std::sin(a0));
            const glm::vec2 v1 = p.center + p.radius * glm::vec2(std::cos(a1), std::sin(a1));
            drawLine(v0, v1);
          }

        } else if constexpr (std::is_same_v<T, SketchArc>) {
          const int segments = std::max(4, static_cast<int>(std::abs(p.sweepAngle) /
                                                            (2.0f * kPi) * kCircleSegments));
          for (int i = 0; i < segments; ++i) {
            const float t0 = p.startAngle + p.sweepAngle * static_cast<float>(i) / segments;
            const float t1 = p.startAngle + p.sweepAngle * static_cast<float>(i + 1) / segments;
            const glm::vec2 v0 = p.center + p.radius * glm::vec2(std::cos(t0), std::sin(t0));
            const glm::vec2 v1 = p.center + p.radius * glm::vec2(std::cos(t1), std::sin(t1));
            drawLine(v0, v1);
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
          while (rel < 0.0f) rel += 2.0f * kPi;
          while (rel > 2.0f * kPi) rel -= 2.0f * kPi;

          if (rel <= p.sweepAngle) {
            return std::abs(dist - p.radius);
          }
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
          glm::vec2 r(p.radius);
          return {p.center - r, p.center + r};
        }
      },
      prim);
}

bool bboxOverlaps(glm::vec2 aMin, glm::vec2 aMax, glm::vec2 bMin, glm::vec2 bMax) {
  return aMin.x <= bMax.x && aMax.x >= bMin.x && aMin.y <= bMax.y && aMax.y >= bMin.y;
}

// --- Constraint application helpers ---
// These modify geometry to immediately satisfy a constraint.

void applyHorizontal(SketchPrimitive& prim) {
  if (auto* l = std::get_if<SketchLine>(&prim)) {
    l->end.y = l->start.y;
  }
}

void applyVertical(SketchPrimitive& prim) {
  if (auto* l = std::get_if<SketchLine>(&prim)) {
    l->end.x = l->start.x;
  }
}

void applyLength(SketchPrimitive& prim, float valueMm) {
  if (auto* l = std::get_if<SketchLine>(&prim)) {
    glm::vec2 dir = l->end - l->start;
    float len = glm::length(dir);
    if (len > 1e-6f) dir /= len;
    else dir = glm::vec2(1.0f, 0.0f);
    l->end = l->start + dir * valueMm;
  }
}

void applyRectangleWidth(SketchPrimitive& prim, float valueMm) {
  if (auto* r = std::get_if<SketchRect>(&prim)) {
    r->max.x = r->min.x + valueMm;
  }
}

void applyRectangleHeight(SketchPrimitive& prim, float valueMm) {
  if (auto* r = std::get_if<SketchRect>(&prim)) {
    r->max.y = r->min.y + valueMm;
  }
}

void applyRadius(SketchPrimitive& prim, float valueMm) {
  if (auto* c = std::get_if<SketchCircle>(&prim)) {
    c->radius = valueMm;
  } else if (auto* a = std::get_if<SketchArc>(&prim)) {
    a->radius = valueMm;
  }
}

void applyDiameter(SketchPrimitive& prim, float valueMm) {
  applyRadius(prim, valueMm * 0.5f);
}

void applyAngle(SketchPrimitive& prim, float degrees) {
  if (auto* l = std::get_if<SketchLine>(&prim)) {
    float len = glm::length(l->end - l->start);
    if (len < 1e-6f) len = 1.0f;
    float rad = degrees * kPi / 180.0f;
    l->end = l->start + glm::vec2(std::cos(rad), std::sin(rad)) * len;
  }
}

void applyFixed(SketchPrimitive& prim, int point, float x, float y) {
  std::visit([&](auto& p) {
    using T = std::decay_t<decltype(p)>;
    if constexpr (std::is_same_v<T, SketchLine>) {
      if (point == 0) p.start = {x, y};
      else p.end = {x, y};
    } else if constexpr (std::is_same_v<T, SketchRect>) {
      if (point == 0) p.min = {x, y};
      else p.max = {x, y};
    } else if constexpr (std::is_same_v<T, SketchCircle>) {
      p.center = {x, y};
    } else if constexpr (std::is_same_v<T, SketchArc>) {
      p.center = {x, y};
    }
  }, prim);
}

glm::vec2 getControlPoint(const SketchPrimitive& prim, int point) {
  return std::visit([&](const auto& p) -> glm::vec2 {
    using T = std::decay_t<decltype(p)>;
    if constexpr (std::is_same_v<T, SketchLine>) {
      return point == 0 ? p.start : p.end;
    } else if constexpr (std::is_same_v<T, SketchRect>) {
      return point == 0 ? p.min : p.max;
    } else if constexpr (std::is_same_v<T, SketchCircle>) {
      return p.center;
    } else if constexpr (std::is_same_v<T, SketchArc>) {
      return p.center;
    }
  }, prim);
}

void setControlPoint(SketchPrimitive& prim, int point, glm::vec2 pos) {
  std::visit([&](auto& p) {
    using T = std::decay_t<decltype(p)>;
    if constexpr (std::is_same_v<T, SketchLine>) {
      if (point == 0) p.start = pos;
      else p.end = pos;
    } else if constexpr (std::is_same_v<T, SketchRect>) {
      if (point == 0) p.min = pos;
      else p.max = pos;
    } else if constexpr (std::is_same_v<T, SketchCircle>) {
      p.center = pos;
    } else if constexpr (std::is_same_v<T, SketchArc>) {
      p.center = pos;
    }
  }, prim);
}

// Midpoint of the primitive's line segment (line → midpoint, others → center).
glm::vec2 primMidpoint(const SketchPrimitive& prim) {
  return std::visit([](const auto& p) -> glm::vec2 {
    using T = std::decay_t<decltype(p)>;
    if constexpr (std::is_same_v<T, SketchLine>) {
      return (p.start + p.end) * 0.5f;
    } else if constexpr (std::is_same_v<T, SketchRect>) {
      return (p.min + p.max) * 0.5f;
    } else if constexpr (std::is_same_v<T, SketchCircle>) {
      return p.center;
    } else if constexpr (std::is_same_v<T, SketchArc>) {
      return p.center;
    }
  }, prim);
}

float primLength(const SketchPrimitive& prim) {
  return std::visit([](const auto& p) -> float {
    using T = std::decay_t<decltype(p)>;
    if constexpr (std::is_same_v<T, SketchLine>) {
      return glm::length(p.end - p.start);
    } else if constexpr (std::is_same_v<T, SketchRect>) {
      glm::vec2 d = p.max - p.min;
      return 2.0f * (std::abs(d.x) + std::abs(d.y));
    } else if constexpr (std::is_same_v<T, SketchCircle>) {
      return p.radius;
    } else if constexpr (std::is_same_v<T, SketchArc>) {
      return p.radius;
    }
  }, prim);
}

}  // namespace

// --- Element management ---

void Sketch::addPrimitive(SketchPrimitive prim) {
  elements_.push_back(
      SketchElement{std::move(prim), false, ConstraintStatus::Unconstrained, std::nullopt});
  selected_.push_back(false);
  updateConstraintStatus();
}

void Sketch::addCompletedPrimitive(CompletedSketchPrimitive prim) {
  undoPush();
  const size_t elemIndex = elements_.size();
  addPrimitive(std::move(prim.geometry));
  for (const auto& dim : prim.dimensions) {
    switch (dim.kind) {
      case SketchDimensionKind::Length:
        addConstraint(LengthConstraint{elemIndex, dim.valueMm});
        break;
      case SketchDimensionKind::RectangleWidth:
        addConstraint(RectangleWidthConstraint{elemIndex, dim.valueMm});
        break;
      case SketchDimensionKind::RectangleHeight:
        addConstraint(RectangleHeightConstraint{elemIndex, dim.valueMm});
        break;
      case SketchDimensionKind::Diameter:
        addConstraint(DiameterConstraint{elemIndex, dim.valueMm});
        break;
    }
  }
}

void Sketch::addElement(SketchElement elem) {
  elements_.push_back(std::move(elem));
  selected_.push_back(false);
  updateConstraintStatus();
}

void Sketch::deleteSelected() {
  undoPush();
  // Collect indices to remove (sorted descending for safe erasure).
  std::vector<size_t> toRemove;
  for (size_t i = 0; i < selected_.size(); ++i) {
    if (selected_[i]) toRemove.push_back(i);
  }
  if (toRemove.empty()) return;

  // Build index remap table: old index → new index (or SIZE_MAX if deleted).
  std::vector<size_t> remap(elements_.size());
  {
    size_t newIdx = 0;
    size_t ri = 0;
    for (size_t i = 0; i < elements_.size(); ++i) {
      if (ri < toRemove.size() && toRemove[ri] == i) {
        remap[i] = SIZE_MAX;
        ++ri;
      } else {
        remap[i] = newIdx++;
      }
    }
  }

  // Remove constraints that reference deleted elements, and remap indices
  // for surviving constraints.
  std::vector<SketchConstraint> newConstraints;
  for (auto& c : constraints_) {
    size_t a = 0, b = 0;
    constraintElements(c, &a, &b);
    if (remap[a] == SIZE_MAX || remap[b] == SIZE_MAX) continue;

    // Remap indices inside the constraint.
    std::visit([&](auto& v) {
      using T = std::decay_t<decltype(v)>;
      if constexpr (std::is_same_v<T, CoincidentConstraint>) {
        v.elemA = remap[v.elemA]; v.elemB = remap[v.elemB];
      } else if constexpr (std::is_same_v<T, ParallelConstraint> ||
                           std::is_same_v<T, PerpendicularConstraint> ||
                           std::is_same_v<T, EqualConstraint> ||
                           std::is_same_v<T, TangentConstraint>) {
        v.elemA = remap[v.elemA]; v.elemB = remap[v.elemB];
      } else if constexpr (std::is_same_v<T, MidpointConstraint>) {
        v.lineElem = remap[v.lineElem]; v.pointElem = remap[v.pointElem];
      } else {
        v.elem = remap[v.elem];
      }
    }, c);
    newConstraints.push_back(std::move(c));
  }
  constraints_ = std::move(newConstraints);

  // Erase elements and selection flags (reverse order).
  for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it) {
    elements_.erase(elements_.begin() + static_cast<ptrdiff_t>(*it));
    selected_.erase(selected_.begin() + static_cast<ptrdiff_t>(*it));
  }

  updateConstraintStatus();
}

void Sketch::clear() {
  undoPush();
  elements_.clear();
  constraints_.clear();
  selected_.clear();
}

bool Sketch::empty() const { return elements_.empty(); }

const std::vector<SketchElement>& Sketch::elements() const { return elements_; }

std::vector<SketchElement>& Sketch::elements() { return elements_; }

std::vector<SketchPrimitive> Sketch::primitives() const {
  std::vector<SketchPrimitive> out;
  out.reserve(elements_.size());
  for (const auto& e : elements_) {
    out.push_back(e.geometry);
  }
  return out;
}

// --- Construction toggle ---

void Sketch::toggleConstruction(size_t idx) {
  undoPush();
  if (idx < elements_.size()) {
    elements_[idx].construction = !elements_[idx].construction;
  }
}

// --- Constraints ---

void Sketch::applyOneConstraint(const SketchConstraint& c) {
  std::visit([&](const auto& v) {
    using T = std::decay_t<decltype(v)>;

    if constexpr (std::is_same_v<T, HorizontalConstraint>) {
      if (v.elem < elements_.size())
        applyHorizontal(elements_[v.elem].geometry);

    } else if constexpr (std::is_same_v<T, VerticalConstraint>) {
      if (v.elem < elements_.size())
        applyVertical(elements_[v.elem].geometry);

    } else if constexpr (std::is_same_v<T, LengthConstraint>) {
      if (v.elem < elements_.size())
        applyLength(elements_[v.elem].geometry, v.valueMm);

    } else if constexpr (std::is_same_v<T, RectangleWidthConstraint>) {
      if (v.elem < elements_.size())
        applyRectangleWidth(elements_[v.elem].geometry, v.valueMm);

    } else if constexpr (std::is_same_v<T, RectangleHeightConstraint>) {
      if (v.elem < elements_.size())
        applyRectangleHeight(elements_[v.elem].geometry, v.valueMm);

    } else if constexpr (std::is_same_v<T, RadiusConstraint>) {
      if (v.elem < elements_.size())
        applyRadius(elements_[v.elem].geometry, v.valueMm);

    } else if constexpr (std::is_same_v<T, DiameterConstraint>) {
      if (v.elem < elements_.size())
        applyDiameter(elements_[v.elem].geometry, v.valueMm);

    } else if constexpr (std::is_same_v<T, AngleConstraint>) {
      if (v.elem < elements_.size())
        applyAngle(elements_[v.elem].geometry, v.degrees);

    } else if constexpr (std::is_same_v<T, FixedConstraint>) {
      if (v.elem < elements_.size())
        applyFixed(elements_[v.elem].geometry, v.point, v.x, v.y);

    } else if constexpr (std::is_same_v<T, CoincidentConstraint>) {
      if (v.elemA < elements_.size() && v.elemB < elements_.size()) {
        glm::vec2 target = getControlPoint(elements_[v.elemA].geometry, v.pointA);
        setControlPoint(elements_[v.elemB].geometry, v.pointB, target);
      }
    } else if constexpr (std::is_same_v<T, ParallelConstraint>) {
      if (v.elemA < elements_.size() && v.elemB < elements_.size()) {
        auto* la = std::get_if<SketchLine>(&elements_[v.elemA].geometry);
        auto* lb = std::get_if<SketchLine>(&elements_[v.elemB].geometry);
        if (la && lb) {
          glm::vec2 dirA = la->end - la->start;
          float lenB = glm::length(lb->end - lb->start);
          float lenA = glm::length(dirA);
          if (lenA > 1e-6f && lenB > 1e-6f) {
            glm::vec2 unit = dirA / lenA;
            lb->end = lb->start + unit * lenB;
          }
        }
      }
    } else if constexpr (std::is_same_v<T, PerpendicularConstraint>) {
      if (v.elemA < elements_.size() && v.elemB < elements_.size()) {
        auto* la = std::get_if<SketchLine>(&elements_[v.elemA].geometry);
        auto* lb = std::get_if<SketchLine>(&elements_[v.elemB].geometry);
        if (la && lb) {
          glm::vec2 dirA = la->end - la->start;
          float lenB = glm::length(lb->end - lb->start);
          float lenA = glm::length(dirA);
          if (lenA > 1e-6f && lenB > 1e-6f) {
            glm::vec2 perpA(-dirA.y / lenA, dirA.x / lenA);
            lb->end = lb->start + perpA * lenB;
          }
        }
      }
    } else if constexpr (std::is_same_v<T, EqualConstraint>) {
      if (v.elemA < elements_.size() && v.elemB < elements_.size()) {
        float lenA = primLength(elements_[v.elemA].geometry);
        auto* lb = std::get_if<SketchLine>(&elements_[v.elemB].geometry);
        if (lb) {
          applyLength(elements_[v.elemB].geometry, lenA);
        } else {
          applyRadius(elements_[v.elemB].geometry, lenA);
        }
      }
    } else if constexpr (std::is_same_v<T, MidpointConstraint>) {
      if (v.lineElem < elements_.size() && v.pointElem < elements_.size()) {
        glm::vec2 mid = primMidpoint(elements_[v.lineElem].geometry);
        setControlPoint(elements_[v.pointElem].geometry, v.point, mid);
      }
    } else if constexpr (std::is_same_v<T, TangentConstraint>) {
      // Tangent is complex; just store the constraint for now.
    }
  }, c);
}

void Sketch::addConstraint(SketchConstraint c) {
  applyOneConstraint(c);
  constraints_.push_back(std::move(c));
  updateConstraintStatus();
}

const std::vector<SketchConstraint>& Sketch::constraints() const { return constraints_; }

std::vector<SketchConstraint>& Sketch::constraints() { return constraints_; }

void Sketch::solveConstraints() {
  // Re-apply all constraints (simple single-pass solver).
  for (const auto& c : constraints_) {
    applyOneConstraint(c);
  }
  updateConstraintStatus();
}

std::string Sketch::applyConstraintToSelection(ConstraintTool tool, float valueMm) {
  auto sel = selectedIndices();
  if (sel.empty()) return "Nothing selected";
  undoPush();

  switch (tool) {
    case ConstraintTool::Horizontal:
      for (size_t i : sel) addConstraint(HorizontalConstraint{i});
      return "Horizontal constraint added";

    case ConstraintTool::Vertical:
      for (size_t i : sel) addConstraint(VerticalConstraint{i});
      return "Vertical constraint added";

    case ConstraintTool::Fixed:
      for (size_t i : sel) {
        auto pts = elements_[i].controlPoints();
        if (!pts.empty())
          addConstraint(FixedConstraint{i, 0, pts[0].x, pts[0].y});
      }
      return "Fixed constraint added";

    case ConstraintTool::Coincident:
      if (sel.size() < 2) return "Select 2 elements for Coincident";
      addConstraint(CoincidentConstraint{sel[0], 1, sel[1], 0});
      return "Coincident constraint added";

    case ConstraintTool::Parallel:
      if (sel.size() < 2) return "Select 2 lines for Parallel";
      addConstraint(ParallelConstraint{sel[0], sel[1]});
      return "Parallel constraint added";

    case ConstraintTool::Perpendicular:
      if (sel.size() < 2) return "Select 2 lines for Perpendicular";
      addConstraint(PerpendicularConstraint{sel[0], sel[1]});
      return "Perpendicular constraint added";

    case ConstraintTool::Equal:
      if (sel.size() < 2) return "Select 2 elements for Equal";
      addConstraint(EqualConstraint{sel[0], sel[1]});
      return "Equal constraint added";

    case ConstraintTool::Tangent:
      if (sel.size() < 2) return "Select 2 elements for Tangent";
      addConstraint(TangentConstraint{sel[0], sel[1]});
      return "Tangent constraint added";

    case ConstraintTool::Midpoint:
      if (sel.size() < 2) return "Select 2 elements for Midpoint";
      addConstraint(MidpointConstraint{sel[0], sel[1], 0});
      return "Midpoint constraint added";

    case ConstraintTool::Length:
      if (valueMm <= 0.0f) return "Enter a length value";
      for (size_t i : sel) addConstraint(LengthConstraint{i, valueMm});
      return "Length constraint added";

    case ConstraintTool::Radius:
      if (valueMm <= 0.0f) return "Enter a radius value";
      for (size_t i : sel) addConstraint(RadiusConstraint{i, valueMm});
      return "Radius constraint added";

    case ConstraintTool::Angle:
      for (size_t i : sel) addConstraint(AngleConstraint{i, valueMm});
      return "Angle constraint added";

    case ConstraintTool::None:
      break;
  }
  return "";
}

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
  for (size_t i = 0; i < elements_.size(); ++i) {
    float d = distanceToPrimitive(pos, elements_[i].geometry);
    if (d < threshold && d < bestDist) {
      bestDist = d;
      bestIdx = i;
    }
  }
  return bestIdx;
}

void Sketch::selectInRect(glm::vec2 rectMin, glm::vec2 rectMax) {
  clearSelection();
  addToSelectInRect(rectMin, rectMax);
}

void Sketch::addToSelectInRect(glm::vec2 rectMin, glm::vec2 rectMax) {
  const glm::vec2 rMin = glm::min(rectMin, rectMax);
  const glm::vec2 rMax = glm::max(rectMin, rectMax);
  for (size_t i = 0; i < elements_.size(); ++i) {
    auto [pMin, pMax] = primBBox(elements_[i].geometry);
    if (bboxOverlaps(rMin, rMax, pMin, pMax)) {
      selected_[i] = true;
    }
  }
}

// --- Snap ---

std::optional<glm::vec2> Sketch::snapToPoint(glm::vec2 pos, float threshold) const {
  float bestDist = threshold;
  std::optional<glm::vec2> bestPt;
  for (const auto& elem : elements_) {
    for (const glm::vec2& cp : elem.controlPoints()) {
      float d = glm::length(pos - cp);
      if (d < bestDist) {
        bestDist = d;
        bestPt = cp;
      }
    }
  }
  return bestPt;
}

std::vector<std::vector<glm::vec2>> Sketch::closedProfiles(float tolerance) const {
  std::vector<std::vector<glm::vec2>> polylines;
  polylines.reserve(elements_.size());
  for (const auto& elem : elements_) {
    if (elem.construction) continue;
    polylines.push_back(profile::tessellate2D(elem.geometry));
  }
  return profile::chainProfiles(polylines, tolerance);
}

std::vector<glm::vec2> Sketch::danglingEndpoints(float tolerance) const {
  std::vector<glm::vec2> points;

  for (size_t i = 0; i < elements_.size(); ++i) {
    if (elements_[i].construction) continue;

    std::vector<glm::vec2> candidates;
    std::visit([&](const auto& p) {
      using T = std::decay_t<decltype(p)>;
      if constexpr (std::is_same_v<T, SketchLine>) {
        candidates.push_back(p.start);
        candidates.push_back(p.end);
      } else if constexpr (std::is_same_v<T, SketchArc>) {
        candidates.push_back(
            p.center + p.radius * glm::vec2(std::cos(p.startAngle), std::sin(p.startAngle)));
        candidates.push_back(p.center + p.radius *
                                          glm::vec2(std::cos(p.startAngle + p.sweepAngle),
                                                    std::sin(p.startAngle + p.sweepAngle)));
      }
    }, elements_[i].geometry);

    for (const glm::vec2& candidate : candidates) {
      bool connected = false;
      for (size_t j = 0; j < elements_.size(); ++j) {
        if (i == j || elements_[j].construction) continue;
        if (distanceToPrimitive(candidate, elements_[j].geometry) <= tolerance) {
          connected = true;
          break;
        }
      }
      if (!connected) {
        bool duplicate = false;
        for (const glm::vec2& existing : points) {
          if (glm::length(existing - candidate) <= tolerance) {
            duplicate = true;
            break;
          }
        }
        if (!duplicate) points.push_back(candidate);
      }
    }
  }

  return points;
}

// --- Rendering ---

void Sketch::appendLines(std::vector<ColorVertex>& lines, SketchPlane plane) const {
  for (size_t i = 0; i < elements_.size(); ++i) {
    const bool sel = (i < selected_.size() && selected_[i]);
    const glm::vec4 color = colorForElement(elements_[i], sel);
    appendPrimLines(lines, elements_[i].geometry, plane, color, elements_[i].construction);
  }
}

void Sketch::appendConstraintAnnotations(std::vector<ColorVertex>& lines,
                                         SketchPlane plane) const {
  for (const auto& c : constraints_) {
    std::visit([&](const auto& v) {
      using T = std::decay_t<decltype(v)>;

      if constexpr (std::is_same_v<T, LengthConstraint>) {
        if (v.elem >= elements_.size()) return;
        auto* l = std::get_if<SketchLine>(&elements_[v.elem].geometry);
        if (!l) return;
        // Draw a small offset dimension line.
        glm::vec2 dir = l->end - l->start;
        float len = glm::length(dir);
        if (len < 1e-6f) return;
        glm::vec2 perp(-dir.y / len, dir.x / len);
        glm::vec2 a = l->start + perp * kDimensionOffset;
        glm::vec2 b = l->end + perp * kDimensionOffset;
        appendLine2D(lines, a, b, plane, kColorAnnotation);
        // Leader lines.
        appendLine2D(lines, l->start, a, plane, kColorAnnotation);
        appendLine2D(lines, l->end, b, plane, kColorAnnotation);

      } else if constexpr (std::is_same_v<T, RectangleWidthConstraint>) {
        if (v.elem >= elements_.size()) return;
        auto* r = std::get_if<SketchRect>(&elements_[v.elem].geometry);
        if (!r) return;
        glm::vec2 a = r->min + glm::vec2(0.0f, -kDimensionOffset);
        glm::vec2 b = glm::vec2(r->max.x, r->min.y - kDimensionOffset);
        appendLine2D(lines, a, b, plane, kColorAnnotation);
        appendLine2D(lines, r->min, a, plane, kColorAnnotation);
        appendLine2D(lines, glm::vec2(r->max.x, r->min.y), b, plane, kColorAnnotation);

      } else if constexpr (std::is_same_v<T, RectangleHeightConstraint>) {
        if (v.elem >= elements_.size()) return;
        auto* r = std::get_if<SketchRect>(&elements_[v.elem].geometry);
        if (!r) return;
        glm::vec2 a(r->max.x + kDimensionOffset, r->min.y);
        glm::vec2 b = r->max + glm::vec2(kDimensionOffset, 0.0f);
        appendLine2D(lines, a, b, plane, kColorAnnotation);
        appendLine2D(lines, glm::vec2(r->max.x, r->min.y), a, plane, kColorAnnotation);
        appendLine2D(lines, r->max, b, plane, kColorAnnotation);

      } else if constexpr (std::is_same_v<T, RadiusConstraint>) {
        if (v.elem >= elements_.size()) return;
        glm::vec2 center;
        float radius = 0.0f;
        if (auto* ci = std::get_if<SketchCircle>(&elements_[v.elem].geometry)) {
          center = ci->center;
          radius = ci->radius;
        } else if (auto* ar = std::get_if<SketchArc>(&elements_[v.elem].geometry)) {
          center = ar->center;
          radius = ar->radius;
        } else {
          return;
        }
        // Radius leader from center to edge.
        glm::vec2 edge = center + glm::vec2(radius, 0.0f);
        appendLine2D(lines, center, edge, plane, kColorAnnotation);

      } else if constexpr (std::is_same_v<T, DiameterConstraint>) {
        if (v.elem >= elements_.size()) return;
        auto* ci = std::get_if<SketchCircle>(&elements_[v.elem].geometry);
        if (!ci) return;
        glm::vec2 left = ci->center + glm::vec2(-ci->radius, 0.0f);
        glm::vec2 right = ci->center + glm::vec2(ci->radius, 0.0f);
        appendLine2D(lines, left, right, plane, kColorAnnotation);
      }
      // Other constraint types: no annotation lines for now.
    }, c);
  }
}

void Sketch::appendConstraintLabels(std::vector<SketchDimensionLabel>& labels, SketchPlane plane,
                                    Unit unit) const {
  for (const auto& c : constraints_) {
    std::visit([&](const auto& v) {
      using T = std::decay_t<decltype(v)>;

      if constexpr (std::is_same_v<T, LengthConstraint>) {
        if (v.elem >= elements_.size()) return;
        auto* l = std::get_if<SketchLine>(&elements_[v.elem].geometry);
        if (!l) return;
        glm::vec2 dir = l->end - l->start;
        float len = glm::length(dir);
        if (len < 1e-6f) return;
        glm::vec2 perp(-dir.y / len, dir.x / len);
        glm::vec2 mid = (l->start + l->end) * 0.5f + perp * (kDimensionOffset + 0.75f);
        labels.push_back({toWorld(mid, plane), formatDimensionText(v.valueMm, unit)});

      } else if constexpr (std::is_same_v<T, RectangleWidthConstraint>) {
        if (v.elem >= elements_.size()) return;
        auto* r = std::get_if<SketchRect>(&elements_[v.elem].geometry);
        if (!r) return;
        glm::vec2 mid((r->min.x + r->max.x) * 0.5f, r->min.y - (kDimensionOffset + 0.75f));
        labels.push_back({toWorld(mid, plane), formatDimensionText(v.valueMm, unit)});

      } else if constexpr (std::is_same_v<T, RectangleHeightConstraint>) {
        if (v.elem >= elements_.size()) return;
        auto* r = std::get_if<SketchRect>(&elements_[v.elem].geometry);
        if (!r) return;
        glm::vec2 mid(r->max.x + (kDimensionOffset + 0.75f), (r->min.y + r->max.y) * 0.5f);
        labels.push_back({toWorld(mid, plane), formatDimensionText(v.valueMm, unit)});

      } else if constexpr (std::is_same_v<T, RadiusConstraint>) {
        if (v.elem >= elements_.size()) return;
        glm::vec2 center;
        float radius = 0.0f;
        if (auto* ci = std::get_if<SketchCircle>(&elements_[v.elem].geometry)) {
          center = ci->center;
          radius = ci->radius;
        } else if (auto* ar = std::get_if<SketchArc>(&elements_[v.elem].geometry)) {
          center = ar->center;
          radius = ar->radius;
        } else {
          return;
        }
        glm::vec2 pos = center + glm::vec2(radius * 0.5f, 0.75f);
        labels.push_back({toWorld(pos, plane), formatDimensionText(v.valueMm, unit, "R ")});

      } else if constexpr (std::is_same_v<T, DiameterConstraint>) {
        if (v.elem >= elements_.size()) return;
        auto* ci = std::get_if<SketchCircle>(&elements_[v.elem].geometry);
        if (!ci) return;
        glm::vec2 pos = ci->center + glm::vec2(0.0f, 0.75f);
        labels.push_back({toWorld(pos, plane), formatDimensionText(v.valueMm, unit, "D ")});
      }
    }, c);
  }
}

// --- DOF tracking ---

// --- Undo / Redo ---

void Sketch::undoPush() {
  undoStack_.push_back({elements_, constraints_});
  if (undoStack_.size() > kMaxUndoLevels) {
    undoStack_.erase(undoStack_.begin());
  }
  redoStack_.clear();
}

bool Sketch::canUndo() const { return !undoStack_.empty(); }
bool Sketch::canRedo() const { return !redoStack_.empty(); }

void Sketch::undo() {
  if (undoStack_.empty()) return;
  redoStack_.push_back({elements_, constraints_});
  auto snap = std::move(undoStack_.back());
  undoStack_.pop_back();
  elements_ = std::move(snap.elements);
  constraints_ = std::move(snap.constraints);
  selected_.assign(elements_.size(), false);
  solveConstraints();
}

void Sketch::redo() {
  if (redoStack_.empty()) return;
  undoStack_.push_back({elements_, constraints_});
  auto snap = std::move(redoStack_.back());
  redoStack_.pop_back();
  elements_ = std::move(snap.elements);
  constraints_ = std::move(snap.constraints);
  selected_.assign(elements_.size(), false);
  solveConstraints();
}

void Sketch::updateConstraintStatus() {
  // Count DOF reductions per element.
  std::vector<int> reductions(elements_.size(), 0);
  std::vector<bool> hasConstraint(elements_.size(), false);

  for (const auto& c : constraints_) {
    size_t a = 0, b = 0;
    constraintElements(c, &a, &b);
    int dof = constraintDofReduction(c);

    if (a < elements_.size()) {
      reductions[a] += dof;
      hasConstraint[a] = true;
    }
    if (b < elements_.size() && b != a) {
      reductions[b] += dof;
      hasConstraint[b] = true;
    }
  }

  for (size_t i = 0; i < elements_.size(); ++i) {
    if (!hasConstraint[i]) {
      elements_[i].status = ConstraintStatus::Unconstrained;
    } else {
      int remaining = elements_[i].baseDof() - reductions[i];
      if (remaining > 0) {
        elements_[i].status = ConstraintStatus::UnderConstrained;
      } else if (remaining == 0) {
        elements_[i].status = ConstraintStatus::FullyConstrained;
      } else {
        elements_[i].status = ConstraintStatus::OverConstrained;
      }
    }
  }
}
