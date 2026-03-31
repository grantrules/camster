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
  resetDimensions();
  result_.reset();
}

Tool SketchTool::activeTool() const { return tool_; }

void SketchTool::mouseMove(glm::vec2 planePos) { cursor_ = planePos; }

glm::vec2 SketchTool::lineEndPoint() const {
  glm::vec2 dir = cursor_ - firstPoint_;
  float len = glm::length(dir);
  if (len > 1e-6f) {
    dir /= len;
  } else {
    dir = glm::vec2(1.0f, 0.0f);
  }
  if (dimensions_[0]) {
    return firstPoint_ + dir * *dimensions_[0];
  }
  return cursor_;
}

glm::vec2 SketchTool::rectangleCorner() const {
  const glm::vec2 delta = cursor_ - firstPoint_;
  const float signX = delta.x < 0.0f ? -1.0f : 1.0f;
  const float signY = delta.y < 0.0f ? -1.0f : 1.0f;
  const float width = dimensions_[0] ? *dimensions_[0] : std::abs(delta.x);
  const float height = dimensions_[1] ? *dimensions_[1] : std::abs(delta.y);
  return firstPoint_ + glm::vec2(signX * width, signY * height);
}

float SketchTool::circleRadius() const {
  if (dimensions_[0]) {
    return *dimensions_[0] * 0.5f;
  }
  return glm::length(cursor_ - firstPoint_);
}

void SketchTool::mouseClick(glm::vec2 planePos) {
  if (tool_ == Tool::None) return;

  switch (tool_) {
    case Tool::Line:
      if (step_ == Step::Idle) {
        firstPoint_ = planePos;
        cursor_ = planePos;
        resetDimensions();
        step_ = Step::Step1;
      } else if (step_ == Step::Step1) {
        cursor_ = planePos;
        CompletedSketchPrimitive completed;
        completed.geometry = SketchLine{firstPoint_, lineEndPoint()};
        if (dimensions_[0]) {
          completed.dimensions.push_back({SketchDimensionKind::Length, *dimensions_[0]});
        }
        result_ = std::move(completed);
        step_ = Step::Idle;
        resetDimensions();
      }
      break;

    case Tool::Rectangle:
      if (step_ == Step::Idle) {
        firstPoint_ = planePos;
        cursor_ = planePos;
        resetDimensions();
        step_ = Step::Step1;
      } else if (step_ == Step::Step1) {
        cursor_ = planePos;
        const glm::vec2 corner = rectangleCorner();
        const glm::vec2 mn(std::min(firstPoint_.x, corner.x), std::min(firstPoint_.y, corner.y));
        const glm::vec2 mx(std::max(firstPoint_.x, corner.x), std::max(firstPoint_.y, corner.y));
        CompletedSketchPrimitive completed;
        completed.geometry = SketchRect{mn, mx};
        if (dimensions_[0]) {
          completed.dimensions.push_back({SketchDimensionKind::RectangleWidth, *dimensions_[0]});
        }
        if (dimensions_[1]) {
          completed.dimensions.push_back({SketchDimensionKind::RectangleHeight, *dimensions_[1]});
        }
        result_ = std::move(completed);
        step_ = Step::Idle;
        resetDimensions();
      }
      break;

    case Tool::Circle:
      if (step_ == Step::Idle) {
        firstPoint_ = planePos;
        cursor_ = planePos;
        resetDimensions();
        step_ = Step::Step1;
      } else if (step_ == Step::Step1) {
        cursor_ = planePos;
        CompletedSketchPrimitive completed;
        completed.geometry = SketchCircle{firstPoint_, circleRadius()};
        if (dimensions_[0]) {
          completed.dimensions.push_back({SketchDimensionKind::Diameter, *dimensions_[0]});
        }
        result_ = std::move(completed);
        step_ = Step::Idle;
        resetDimensions();
      }
      break;

    case Tool::Arc:
      if (step_ == Step::Idle) {
        firstPoint_ = planePos;  // center
        cursor_ = planePos;
        resetDimensions();
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
        result_ = CompletedSketchPrimitive{SketchArc{firstPoint_, radius, startAngle, sweep}, {}};
        step_ = Step::Idle;
        resetDimensions();
      }
      break;

    case Tool::None:
      break;
  }
}

void SketchTool::cancel() {
  step_ = Step::Idle;
  resetDimensions();
  result_.reset();
}

int SketchTool::dimensionInputCount() const {
  if (step_ != Step::Step1) return 0;
  if (tool_ == Tool::Line || tool_ == Tool::Circle) return 1;
  if (tool_ == Tool::Rectangle) return 2;
  return 0;
}

const char* SketchTool::dimensionPrompt(int index) const {
  if (tool_ == Tool::Line && index == 0) return "Length";
  if (tool_ == Tool::Rectangle && index == 0) return "Width";
  if (tool_ == Tool::Rectangle && index == 1) return "Height";
  if (tool_ == Tool::Circle && index == 0) return "Diameter";
  return "";
}

void SketchTool::setDimensionValue(int index, std::optional<float> valueMm) {
  if (index < 0 || index >= static_cast<int>(dimensions_.size())) return;
  dimensions_[index] = valueMm;
}

bool SketchTool::finishFromDimensions() {
  if (step_ != Step::Step1) return false;

  if (tool_ == Tool::Line) {
    if (!dimensions_[0]) return false;
    CompletedSketchPrimitive completed;
    completed.geometry = SketchLine{firstPoint_, lineEndPoint()};
    completed.dimensions.push_back({SketchDimensionKind::Length, *dimensions_[0]});
    result_ = std::move(completed);
  } else if (tool_ == Tool::Rectangle) {
    if (!dimensions_[0] || !dimensions_[1]) return false;
    const glm::vec2 corner = rectangleCorner();
    const glm::vec2 mn(std::min(firstPoint_.x, corner.x), std::min(firstPoint_.y, corner.y));
    const glm::vec2 mx(std::max(firstPoint_.x, corner.x), std::max(firstPoint_.y, corner.y));
    CompletedSketchPrimitive completed;
    completed.geometry = SketchRect{mn, mx};
    completed.dimensions.push_back({SketchDimensionKind::RectangleWidth, *dimensions_[0]});
    completed.dimensions.push_back({SketchDimensionKind::RectangleHeight, *dimensions_[1]});
    result_ = std::move(completed);
  } else if (tool_ == Tool::Circle) {
    if (!dimensions_[0]) return false;
    CompletedSketchPrimitive completed;
    completed.geometry = SketchCircle{firstPoint_, circleRadius()};
    completed.dimensions.push_back({SketchDimensionKind::Diameter, *dimensions_[0]});
    result_ = std::move(completed);
  } else {
    return false;
  }

  step_ = Step::Idle;
  resetDimensions();
  return true;
}

std::optional<CompletedSketchPrimitive> SketchTool::takeResult() {
  auto r = std::move(result_);
  result_.reset();
  return r;
}

void SketchTool::resetDimensions() {
  dimensions_[0].reset();
  dimensions_[1].reset();
}

void SketchTool::appendPreview(std::vector<ColorVertex>& lines, SketchPlane plane) const {
  if (tool_ == Tool::None || step_ == Step::Idle) return;

  switch (tool_) {
    case Tool::Line:
      if (step_ == Step::Step1) {
        appendCross(lines, firstPoint_, plane, kPointColor);
        lines.push_back({toWorld(firstPoint_, plane), kPreviewColor});
        lines.push_back({toWorld(lineEndPoint(), plane), kPreviewColor});
      }
      break;

    case Tool::Rectangle:
      if (step_ == Step::Step1) {
        appendCross(lines, firstPoint_, plane, kPointColor);
        const glm::vec2& a = firstPoint_;
        const glm::vec2 c = rectangleCorner();
        const glm::vec2 b(c.x, firstPoint_.y);
        const glm::vec2 d(firstPoint_.x, c.y);
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
        appendCirclePreview(lines, firstPoint_, circleRadius(), plane, kPreviewColor);
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
