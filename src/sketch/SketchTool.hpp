#pragma once

#include <array>
#include <optional>
#include <vector>

#include <glm/vec2.hpp>

#include "ColorVertex.hpp"
#include "Scene.hpp"
#include "sketch/Primitive.hpp"

enum class Tool { None, Line, Polyline, Rectangle, Circle, Arc };

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

  int dimensionInputCount() const;
  const char* dimensionPrompt(int index) const;
  void setDimensionValue(int index, std::optional<float> valueMm);
  bool finishFromDimensions();

  // Retrieve (and consume) the completed primitive, if any.
  std::optional<CompletedSketchPrimitive> takeResult();

  // Append preview line segments for the tool's in-progress geometry.
  void appendPreview(std::vector<ColorVertex>& lines, SketchPlane plane) const;

 private:
  enum class Step { Idle, Step1, Step2 };

  glm::vec2 lineEndPoint() const;
  glm::vec2 rectangleCorner() const;
  float circleRadius() const;
  void resetDimensions();

  Tool tool_ = Tool::None;
  Step step_ = Step::Idle;
  glm::vec2 firstPoint_{0.0f};
  glm::vec2 secondPoint_{0.0f};
  glm::vec2 cursor_{0.0f};
  std::array<std::optional<float>, 2> dimensions_;
  std::optional<CompletedSketchPrimitive> result_;
};
