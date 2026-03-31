#pragma once

#include <optional>
#include <vector>

#include <glm/vec2.hpp>

#include "ColorVertex.hpp"
#include "Scene.hpp"
#include "sketch/Primitive.hpp"

enum class Tool { None, Line, Rectangle, Circle, Arc };

// Manages the interactive state of the active drawing tool.  Each tool is a
// small state machine driven by mouse clicks and optional dimension input.
//
// Flow:
//   setTool(Line) → mouseClick(p1) → [mouseMove preview] → mouseClick(p2) → done
//   setTool(Circle) → mouseClick(center) → [mouseMove radius] → mouseClick → done
//   setTool(Arc) → click(center) → click(startPt) → click(endPt) → done
//
// After a tool completes, call takeResult() to get the finished primitive.
class SketchTool {
 public:
  void setTool(Tool tool);
  Tool activeTool() const;

  void mouseMove(glm::vec2 planePos);
  void mouseClick(glm::vec2 planePos);
  void cancel();

  // Returns true when the tool can accept a typed dimension value.
  bool wantsDimension() const;
  const char* dimensionPrompt() const;
  void applyDimension(float valueMm);

  // Retrieve (and consume) the completed primitive, if any.
  std::optional<SketchPrimitive> takeResult();

  // Append preview line segments for the tool's in-progress geometry.
  void appendPreview(std::vector<ColorVertex>& lines, SketchPlane plane) const;

 private:
  enum class Step { Idle, Step1, Step2 };

  Tool tool_ = Tool::None;
  Step step_ = Step::Idle;
  glm::vec2 firstPoint_{0.0f};
  glm::vec2 secondPoint_{0.0f};
  glm::vec2 cursor_{0.0f};
  std::optional<SketchPrimitive> result_;
};
