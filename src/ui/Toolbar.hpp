#pragma once

#include "Units.hpp"
#include "sketch/Constraint.hpp"
#include "sketch/ExtrudeTool.hpp"
#include "sketch/SketchTool.hpp"

struct ToolbarAction {
  bool exitSketchRequested = false;
  bool projectToolRequested = false;
  bool extrudeRequested = false;
  bool extrudeConfirmed = false;
  bool deleteRequested = false;
  bool toggleConstruction = false;
  ConstraintTool constraintRequested = ConstraintTool::None;
};

// Horizontal toolbar rendered at the top of the viewport (below the menu bar)
// when the scene is in sketch mode.  Provides tabbed tool-selection buttons,
// an optional dimension-input field, and extrude controls.
class Toolbar {
 public:
  ToolbarAction draw(SketchTool& tool, ExtrudeTool& extrude, bool hasSelection, Unit defaultUnit);
  const char* constraintValue() const { return constraintValueBuffer_; }

 private:
  enum class Tab { Sketch, Solid, Constrain, Dimension };
  Tab activeTab_ = Tab::Sketch;
  char dimBufferA_[128] = {};
  char dimBufferB_[128] = {};
  char extrudeBuffer_[128] = {};
  char constraintValueBuffer_[128] = {};
};
