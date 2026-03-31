#include "ui/Toolbar.hpp"

#include <cstdio>
#include <cstring>

#include <imgui.h>

namespace {
std::optional<float> parseToolbarDimension(const char* buffer, Unit defaultUnit) {
  if (!buffer || buffer[0] == '\0') return std::nullopt;
  auto parsed = parseDimension(std::string(buffer), defaultUnit);
  if (!parsed) return std::nullopt;
  return parsed->valueMm;
}

bool toolButton(const char* label, bool active) {
  if (active) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 1.0f, 1.0f));
  }
  bool clicked = ImGui::Button(label);
  if (active) {
    ImGui::PopStyleColor();
  }
  return clicked;
}

bool tabButton(const char* label, bool active) {
  if (active) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
  }
  bool clicked = ImGui::SmallButton(label);
  ImGui::PopStyleColor(2);
  return clicked;
}
}  // namespace

ToolbarAction Toolbar::draw(SketchTool& tool, ExtrudeTool& extrude, bool hasSelection,
                            Unit defaultUnit) {
  ToolbarAction action;

  const float menuBarHeight = ImGui::GetFrameHeight();
  ImGui::SetNextWindowPos(ImVec2(0, menuBarHeight));
  ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, 0));

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                           ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;

  ImGui::Begin("##toolbar", nullptr, flags);

  if (extrude.active()) {
    // --- Extrude mode toolbar ---
    if (ImGui::Button("Cancel")) {
      extrude.cancel();
    }
    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    ImGui::Text("Extrude:");
    ImGui::SameLine();

    if (!ImGui::IsItemActive()) {
      const float displayVal = fromMm(extrude.distance(), defaultUnit);
      std::snprintf(extrudeBuffer_, sizeof(extrudeBuffer_), "%.3f", displayVal);
    }

    ImGui::PushItemWidth(120);
    if (ImGui::InputText("##extrudeDist", extrudeBuffer_, sizeof(extrudeBuffer_),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
      auto parsed = parseDimension(std::string(extrudeBuffer_), defaultUnit);
      if (parsed) {
        extrude.setDistance(parsed->valueMm);
      }
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", unitSuffix(defaultUnit));

    ImGui::SameLine();
    if (ImGui::Button("Confirm")) {
      action.extrudeConfirmed = true;
    }
  } else {
    if (ImGui::Button("Exit Sketch")) {
      action.exitSketchRequested = true;
    }
    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    // --- Tab bar ---
    if (tabButton("Sketch", activeTab_ == Tab::Sketch)) activeTab_ = Tab::Sketch;
    ImGui::SameLine();
    if (tabButton("Solid", activeTab_ == Tab::Solid)) activeTab_ = Tab::Solid;
    ImGui::SameLine();
    if (tabButton("Constrain", activeTab_ == Tab::Constrain)) activeTab_ = Tab::Constrain;
    ImGui::SameLine();
    if (tabButton("Dimension", activeTab_ == Tab::Dimension)) activeTab_ = Tab::Dimension;

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    if (activeTab_ == Tab::Sketch) {
      // --- Sketch tools ---
      const Tool active = tool.activeTool();

      if (toolButton("Select", active == Tool::None)) tool.setTool(Tool::None);
      ImGui::SameLine();
      if (toolButton("Line", active == Tool::Line)) tool.setTool(Tool::Line);
      ImGui::SameLine();
      if (toolButton("Rect", active == Tool::Rectangle)) tool.setTool(Tool::Rectangle);
      ImGui::SameLine();
      if (toolButton("Circle", active == Tool::Circle)) tool.setTool(Tool::Circle);
      ImGui::SameLine();
      if (toolButton("Arc", active == Tool::Arc)) tool.setTool(Tool::Arc);

      ImGui::SameLine();
      ImGui::Text("|");
      ImGui::SameLine();

      // Construction toggle (needs selection).
      if (!hasSelection) ImGui::BeginDisabled();
      if (ImGui::Button("Construction")) {
        action.toggleConstruction = true;
      }
      if (!hasSelection) ImGui::EndDisabled();

      ImGui::SameLine();

      // Delete (needs selection).
      if (!hasSelection) ImGui::BeginDisabled();
      if (ImGui::Button("Delete")) {
        action.deleteRequested = true;
      }
      if (!hasSelection) ImGui::EndDisabled();

      ImGui::SameLine();
      ImGui::Text("|");
      ImGui::SameLine();

      // Dimension input for the in-progress sketch tool.
      const int dimCount = tool.dimensionInputCount();
      if (dimCount > 0) {
        tool.setDimensionValue(0, parseToolbarDimension(dimBufferA_, defaultUnit));
        tool.setDimensionValue(1, parseToolbarDimension(dimBufferB_, defaultUnit));

        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();

        bool finishRequested = false;
        for (int i = 0; i < dimCount; ++i) {
          char* buffer = (i == 0) ? dimBufferA_ : dimBufferB_;
          const char* inputId = (i == 0) ? "##dimA" : "##dimB";

          ImGui::Text("%s:", tool.dimensionPrompt(i));
          ImGui::SameLine();
          ImGui::PushItemWidth(120);
          if (ImGui::InputText(inputId, buffer, 128, ImGuiInputTextFlags_EnterReturnsTrue)) {
            finishRequested = true;
          }
          ImGui::PopItemWidth();

          if (i + 1 < dimCount) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", unitSuffix(defaultUnit));
            ImGui::SameLine();
            ImGui::Separator();
            ImGui::SameLine();
          }
        }

        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", unitSuffix(defaultUnit));

        tool.setDimensionValue(0, parseToolbarDimension(dimBufferA_, defaultUnit));
        tool.setDimensionValue(1, parseToolbarDimension(dimBufferB_, defaultUnit));
        if (finishRequested && tool.finishFromDimensions()) {
          dimBufferA_[0] = '\0';
          dimBufferB_[0] = '\0';
        }
      }

    } else if (activeTab_ == Tab::Solid) {
      if (!hasSelection) ImGui::BeginDisabled();
      if (ImGui::Button("Extrude")) {
        tool.setTool(Tool::None);
        action.extrudeRequested = true;
      }
      if (!hasSelection) ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::TextDisabled("Use selected closed profiles to create or modify solids");

    } else if (activeTab_ == Tab::Constrain) {
      // --- Constraint tools (require selection) ---
      if (!hasSelection) ImGui::BeginDisabled();

      if (ImGui::Button("Horizontal")) action.constraintRequested = ConstraintTool::Horizontal;
      ImGui::SameLine();
      if (ImGui::Button("Vertical")) action.constraintRequested = ConstraintTool::Vertical;
      ImGui::SameLine();
      if (ImGui::Button("Fixed")) action.constraintRequested = ConstraintTool::Fixed;
      ImGui::SameLine();
      if (ImGui::Button("Coincident")) action.constraintRequested = ConstraintTool::Coincident;
      ImGui::SameLine();
      if (ImGui::Button("Parallel")) action.constraintRequested = ConstraintTool::Parallel;
      ImGui::SameLine();
      if (ImGui::Button("Perpendicular")) action.constraintRequested = ConstraintTool::Perpendicular;
      ImGui::SameLine();
      if (ImGui::Button("Equal")) action.constraintRequested = ConstraintTool::Equal;
      ImGui::SameLine();
      if (ImGui::Button("Tangent")) action.constraintRequested = ConstraintTool::Tangent;
      ImGui::SameLine();
      if (ImGui::Button("Midpoint")) action.constraintRequested = ConstraintTool::Midpoint;

      if (!hasSelection) ImGui::EndDisabled();

    } else if (activeTab_ == Tab::Dimension) {
      // --- Dimension constraint tools (require selection + value input) ---
      if (!hasSelection) ImGui::BeginDisabled();

      if (ImGui::Button("Length")) action.constraintRequested = ConstraintTool::Length;
      ImGui::SameLine();
      if (ImGui::Button("Radius")) action.constraintRequested = ConstraintTool::Radius;
      ImGui::SameLine();
      if (ImGui::Button("Angle")) action.constraintRequested = ConstraintTool::Angle;

      if (!hasSelection) ImGui::EndDisabled();

      ImGui::SameLine();
      ImGui::Text("|");
      ImGui::SameLine();

      ImGui::Text("Value:");
      ImGui::SameLine();
      ImGui::PushItemWidth(120);
      ImGui::InputText("##cval", constraintValueBuffer_, sizeof(constraintValueBuffer_));
      ImGui::PopItemWidth();
      ImGui::SameLine();
      ImGui::TextDisabled("(%s)", unitSuffix(defaultUnit));
    }
  }

  ImGui::End();
  return action;
}
