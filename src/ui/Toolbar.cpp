#include "ui/Toolbar.hpp"

#include <cstdio>
#include <cstring>

#include <imgui.h>

namespace {
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

    // Keep the input in sync with the live distance (e.g. from mouse drag).
    // Only overwrite the buffer when the user is NOT actively typing.
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
    // --- Normal sketch toolbar ---
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

    // Extrude button (enabled only when primitives are selected).
    if (!hasSelection) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Extrude")) {
      // Signal to main loop to begin extrusion with the current selection.
      tool.setTool(Tool::None);
      action.extrudeRequested = true;
    }
    if (!hasSelection) {
      ImGui::EndDisabled();
    }

    // Dimension input (visible when the drawing tool can accept a typed value).
    if (tool.wantsDimension()) {
      ImGui::SameLine();
      ImGui::Separator();
      ImGui::SameLine();
      ImGui::Text("%s:", tool.dimensionPrompt());
      ImGui::SameLine();

      ImGui::PushItemWidth(120);
      if (ImGui::InputText("##dim", dimBuffer_, sizeof(dimBuffer_),
                           ImGuiInputTextFlags_EnterReturnsTrue)) {
        auto parsed = parseDimension(std::string(dimBuffer_), defaultUnit);
        if (parsed) {
          tool.applyDimension(parsed->valueMm);
          dimBuffer_[0] = '\0';
        }
      }
      ImGui::PopItemWidth();

      ImGui::SameLine();
      ImGui::TextDisabled("(%s)", unitSuffix(defaultUnit));
    }
  }

  ImGui::End();
  return action;
}
