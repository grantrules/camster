#pragma once

#include "Units.hpp"
#include "sketch/ExtrudeTool.hpp"
#include "sketch/SketchTool.hpp"

struct ToolbarAction {
  bool extrudeRequested = false;  // user clicked "Extrude" to start
  bool extrudeConfirmed = false;  // user clicked "Confirm" to finish
};

// Horizontal toolbar rendered at the top of the viewport (below the menu bar)
// when the scene is in sketch mode.  Provides tool-selection buttons, an
// optional dimension-input field, and extrude controls.
class Toolbar {
 public:
  ToolbarAction draw(SketchTool& tool, ExtrudeTool& extrude, bool hasSelection, Unit defaultUnit);

 private:
  char dimBuffer_[128] = {};
  char extrudeBuffer_[128] = {};
};
