#include "sketch/SketchTool.hpp"

#include <cmath>

namespace {
constexpr glm::vec4 kPreviewColor{0.0f, 1.0f, 1.0f, 1.0f};   // cyan
constexpr glm::vec4 kPointColor{1.0f, 1.0f, 0.0f, 1.0f};     // yellow
constexpr int kPreviewCircleSegs = 64;
constexpr float kPointSize = 0.5f;  // half-size of the point indicator cross

void appendCross(std::vector<ColorVertex>& lines, glm::vec2 p, SketchPlane plane,
                 const glm::vec4& color) {
  lines.push_back({toWorld(p + glm::vec2(-kPointSize, 0.0f), plane), color});
  lines.push_back({toWorld(p + glm::vec2(kPointSize, 0.0f), plane), color});
  lines.push_back({toWorld(p + glm::vec2(0.0f, -kPointSize), plane), color});
  lines.push_back({toWorld(p + glm::vec2(0.0f, kPointSize), plane), color});
}

void appendCirclePreview(std::vector<ColorVertex>& lines, glm::vec2 center,
                         float radius, SketchPlane plane, const glm::vec4& color) {
  for (int i = 0; i < kPreviewCircleSegs; ++i) {
    const float a0 = 2.0f * 3.14159265f * static_cast<float>(i) / kPreviewCircleSegs;
    const float a1 = 2.0f * 3.14159265f * static_cast<float>(i + 1) / kPreviewCircleSegs;
    lines.push_back({toWorld(center + radius * glm::vec2(std::cos(a0), std::sin(a0)), plane), color});
    lines.push_back({toWorld(center + radius * glm::vec2(std::cos(a1), std::sin(a1)), plane), color});
  }
}
}  // namespace

void SketchTool::setTool(Tool tool) {
  tool_ = tool;
  step_ = Step::Idle;
  result_.reset();
}

Tool SketchTool::activeTool() const { return tool_; }

void SketchTool::mouseMove(glm::vec2 planePos) { cursor_ = planePos; }

void SketchTool::mouseClick(glm::vec2 planePos) {
  if (tool_ == Tool::None) return;

  switch (tool_) {
    case Tool::Line:
      if (step_ == Step::Idle) {
        firstPoint_ = planePos;
        step_ = Step::Step1;
      } else if (step_ == Step::Step1) {
        result_ = SketchLine{firstPoint_, planePos};
        step_ = Step::Idle;
      }
      break;

    case Tool::Rectangle:
      if (step_ == Step::Idle) {
        firstPoint_ = planePos;
        step_ = Step::Step1;
      } else if (step_ == Step::Step1) {
        const glm::vec2 mn(std::min(firstPoint_.x, planePos.x), std::min(firstPoint_.y, planePos.y));
        const glm::vec2 mx(std::max(firstPoint_.x, planePos.x), std::max(firstPoint_.y, planePos.y));
        result_ = SketchRect{mn, mx};
        step_ = Step::Idle;
      }
      break;

    case Tool::Circle:
      if (step_ == Step::Idle) {
        firstPoint_ = planePos;
        step_ = Step::Step1;
      } else if (step_ == Step::Step1) {
        const float r = glm::length(planePos - firstPoint_);
        result_ = SketchCircle{firstPoint_, r};
        step_ = Step::Idle;
      }
      break;

    case Tool::Arc:
      if (step_ == Step::Idle) {
        firstPoint_ = planePos;  // center
        step_ = Step::Step1;
      } else if (step_ == Step::Step1) {
        secondPoint_ = planePos;  // start point (defines radius + start angle)
        step_ = Step::Step2;
      } else if (step_ == Step::Step2) {
        const float radius = glm::length(secondPoint_ - firstPoint_);
        const float startAngle = std::atan2(secondPoint_.y - firstPoint_.y,
                                            secondPoint_.x - firstPoint_.x);
        const float endAngle = std::atan2(planePos.y - firstPoint_.y,
                                          planePos.x - firstPoint_.x);
        float sweep = endAngle - startAngle;
        if (sweep < 0.0f) sweep += 2.0f * 3.14159265f;
        result_ = SketchArc{firstPoint_, radius, startAngle, sweep};
        step_ = Step::Idle;
      }
      break;

    case Tool::None:
      break;
  }
}

void SketchTool::cancel() {
  step_ = Step::Idle;
  result_.reset();
}

bool SketchTool::wantsDimension() const {
  if (step_ != Step::Step1) return false;
  return tool_ == Tool::Line || tool_ == Tool::Circle;
}

const char* SketchTool::dimensionPrompt() const {
  if (tool_ == Tool::Line) return "Length";
  if (tool_ == Tool::Circle) return "Radius";
  return "";
}

void SketchTool::applyDimension(float valueMm) {
  if (tool_ == Tool::Line && step_ == Step::Step1) {
    glm::vec2 dir = cursor_ - firstPoint_;
    const float len = glm::length(dir);
    if (len > 1e-6f) {
      dir /= len;
    } else {
      dir = glm::vec2(1.0f, 0.0f);
    }
    result_ = SketchLine{firstPoint_, firstPoint_ + dir * valueMm};
    step_ = Step::Idle;
  } else if (tool_ == Tool::Circle && step_ == Step::Step1) {
    result_ = SketchCircle{firstPoint_, valueMm};
    step_ = Step::Idle;
  }
}

std::optional<SketchPrimitive> SketchTool::takeResult() {
  auto r = std::move(result_);
  result_.reset();
  return r;
}

void SketchTool::appendPreview(std::vector<ColorVertex>& lines, SketchPlane plane) const {
  if (tool_ == Tool::None || step_ == Step::Idle) return;

  switch (tool_) {
    case Tool::Line:
      if (step_ == Step::Step1) {
        appendCross(lines, firstPoint_, plane, kPointColor);
        lines.push_back({toWorld(firstPoint_, plane), kPreviewColor});
        lines.push_back({toWorld(cursor_, plane), kPreviewColor});
      }
      break;

    case Tool::Rectangle:
      if (step_ == Step::Step1) {
        appendCross(lines, firstPoint_, plane, kPointColor);
        const glm::vec2& a = firstPoint_;
        const glm::vec2 b(cursor_.x, firstPoint_.y);
        const glm::vec2& c = cursor_;
        const glm::vec2 d(firstPoint_.x, cursor_.y);
        lines.push_back({toWorld(a, plane), kPreviewColor});
        lines.push_back({toWorld(b, plane), kPreviewColor});
        lines.push_back({toWorld(b, plane), kPreviewColor});
        lines.push_back({toWorld(c, plane), kPreviewColor});
        lines.push_back({toWorld(c, plane), kPreviewColor});
        lines.push_back({toWorld(d, plane), kPreviewColor});
        lines.push_back({toWorld(d, plane), kPreviewColor});
        lines.push_back({toWorld(a, plane), kPreviewColor});
      }
      break;

    case Tool::Circle:
      if (step_ == Step::Step1) {
        appendCross(lines, firstPoint_, plane, kPointColor);
        const float r = glm::length(cursor_ - firstPoint_);
        appendCirclePreview(lines, firstPoint_, r, plane, kPreviewColor);
      }
      break;

    case Tool::Arc:
      if (step_ == Step::Step1) {
        appendCross(lines, firstPoint_, plane, kPointColor);
        // Show a full circle at radius = distance to cursor.
        const float r = glm::length(cursor_ - firstPoint_);
        appendCirclePreview(lines, firstPoint_, r, plane, kPreviewColor);
      } else if (step_ == Step::Step2) {
        appendCross(lines, firstPoint_, plane, kPointColor);
        const float radius = glm::length(secondPoint_ - firstPoint_);
        const float startAngle = std::atan2(secondPoint_.y - firstPoint_.y,
                                            secondPoint_.x - firstPoint_.x);
        const float endAngle = std::atan2(cursor_.y - firstPoint_.y,
                                          cursor_.x - firstPoint_.x);
        float sweep = endAngle - startAngle;
        if (sweep < 0.0f) sweep += 2.0f * 3.14159265f;
        const int segs = std::max(4, static_cast<int>(std::abs(sweep) /
                                                      (2.0f * 3.14159265f) * kPreviewCircleSegs));
        for (int i = 0; i < segs; ++i) {
          const float t0 = startAngle + sweep * static_cast<float>(i) / segs;
          const float t1 = startAngle + sweep * static_cast<float>(i + 1) / segs;
          lines.push_back(
              {toWorld(firstPoint_ + radius * glm::vec2(std::cos(t0), std::sin(t0)), plane),
               kPreviewColor});
          lines.push_back(
              {toWorld(firstPoint_ + radius * glm::vec2(std::cos(t1), std::sin(t1)), plane),
               kPreviewColor});
        }
      }
      break;

    case Tool::None:
      break;
  }
}
