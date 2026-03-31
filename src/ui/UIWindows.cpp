#include "ui/UIWindows.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <type_traits>
#include <vector>

#include <imgui.h>

#include "Units.hpp"
#include "core/AppLogic.hpp"
#include "sketch/Profile.hpp"

namespace {
void drawObjectSelectionList(const char* title, std::vector<int>& indices,
                             ObjectPickMode modeForSelect, AppState* app) {
  sanitizeObjectIndices(indices, static_cast<int>(app->sceneObjects.size()));

  ImGui::TextUnformatted(title);
  int eraseAt = -1;
  for (int i = 0; i < static_cast<int>(indices.size()); ++i) {
    ImGui::PushID((std::string(title) + std::to_string(i)).c_str());
    if (ImGui::SmallButton("x")) eraseAt = i;
    ImGui::SameLine();
    const int objectIndex = indices[i];
    const auto& meta = app->sceneObjectMeta[objectIndex];
    ImGui::Text("%s%s%s", meta.name.data(), meta.visible ? "" : " [hidden]",
                meta.locked ? " [locked]" : "");
    ImGui::PopID();
  }
  if (eraseAt >= 0) indices.erase(indices.begin() + eraseAt);

  const bool selecting = app->objectPickMode == modeForSelect;
  if (ImGui::Button(selecting ? "Stop" : "Select")) {
    app->objectPickMode = selecting ? ObjectPickMode::None : modeForSelect;
  }
  if (selecting) {
    ImGui::SameLine();
    ImGui::TextDisabled("Click an object in 3D view to add");
  }
}

glm::vec2 projectPointBetweenSketchPlanes(glm::vec2 srcPoint,
                                          SketchPlane srcPlane,
                                          float srcOffsetMm,
                                          SketchPlane dstPlane,
                                          float /*dstOffsetMm*/) {
  const glm::vec3 world = toWorld(srcPoint, srcPlane, srcOffsetMm);
  return toPlane(world, dstPlane);
}

std::vector<SketchElement> projectElementBetweenSketchPlanes(const SketchElement& src,
                                                             SketchPlane srcPlane,
                                                             float srcOffsetMm,
                                                             SketchPlane dstPlane,
                                                             float dstOffsetMm) {
  std::vector<SketchElement> out;
  const auto makeLine = [&](glm::vec2 a, glm::vec2 b, bool construction) {
    SketchElement elem;
    elem.geometry = SketchLine{a, b};
    elem.construction = construction;
    out.push_back(std::move(elem));
  };

  std::visit(
      [&](const auto& prim) {
        using T = std::decay_t<decltype(prim)>;
        if constexpr (std::is_same_v<T, SketchLine>) {
          SketchElement elem;
          elem.geometry = SketchLine{
              projectPointBetweenSketchPlanes(prim.start, srcPlane, srcOffsetMm, dstPlane,
                                              dstOffsetMm),
              projectPointBetweenSketchPlanes(prim.end, srcPlane, srcOffsetMm, dstPlane,
                                              dstOffsetMm)};
          elem.construction = src.construction;
          out.push_back(std::move(elem));
        } else if constexpr (std::is_same_v<T, SketchRect>) {
          const glm::vec2 c0{prim.min.x, prim.min.y};
          const glm::vec2 c1{prim.max.x, prim.min.y};
          const glm::vec2 c2{prim.max.x, prim.max.y};
          const glm::vec2 c3{prim.min.x, prim.max.y};
          const glm::vec2 p0 = projectPointBetweenSketchPlanes(c0, srcPlane, srcOffsetMm,
                                                                dstPlane, dstOffsetMm);
          const glm::vec2 p1 = projectPointBetweenSketchPlanes(c1, srcPlane, srcOffsetMm,
                                                                dstPlane, dstOffsetMm);
          const glm::vec2 p2 = projectPointBetweenSketchPlanes(c2, srcPlane, srcOffsetMm,
                                                                dstPlane, dstOffsetMm);
          const glm::vec2 p3 = projectPointBetweenSketchPlanes(c3, srcPlane, srcOffsetMm,
                                                                dstPlane, dstOffsetMm);
          makeLine(p0, p1, src.construction);
          makeLine(p1, p2, src.construction);
          makeLine(p2, p3, src.construction);
          makeLine(p3, p0, src.construction);
        } else if constexpr (std::is_same_v<T, SketchCircle>) {
          constexpr int kSegments = 48;
          for (int i = 0; i < kSegments; ++i) {
            const float t0 = (2.0f * 3.1415926535f * static_cast<float>(i)) /
                             static_cast<float>(kSegments);
            const float t1 = (2.0f * 3.1415926535f * static_cast<float>(i + 1)) /
                             static_cast<float>(kSegments);
            const glm::vec2 s0 = prim.center + prim.radius * glm::vec2(std::cos(t0), std::sin(t0));
            const glm::vec2 s1 = prim.center + prim.radius * glm::vec2(std::cos(t1), std::sin(t1));
            makeLine(projectPointBetweenSketchPlanes(s0, srcPlane, srcOffsetMm, dstPlane,
                                                     dstOffsetMm),
                     projectPointBetweenSketchPlanes(s1, srcPlane, srcOffsetMm, dstPlane,
                                                     dstOffsetMm),
                     src.construction);
          }
        } else if constexpr (std::is_same_v<T, SketchArc>) {
          constexpr int kSegments = 24;
          const float totalSweep = prim.sweepAngle;
          for (int i = 0; i < kSegments; ++i) {
            const float a0 = prim.startAngle + totalSweep * (static_cast<float>(i) / kSegments);
            const float a1 = prim.startAngle + totalSweep * (static_cast<float>(i + 1) / kSegments);
            const glm::vec2 s0 = prim.center + prim.radius * glm::vec2(std::cos(a0), std::sin(a0));
            const glm::vec2 s1 = prim.center + prim.radius * glm::vec2(std::cos(a1), std::sin(a1));
            makeLine(projectPointBetweenSketchPlanes(s0, srcPlane, srcOffsetMm, dstPlane,
                                                     dstOffsetMm),
                     projectPointBetweenSketchPlanes(s1, srcPlane, srcOffsetMm, dstPlane,
                                                     dstOffsetMm),
                     src.construction);
          }
        }
      },
      src.geometry);

  return out;
}

bool unitCombo(const char* label, Unit* unit) {
  bool changed = false;
  int current = static_cast<int>(*unit);
  if (ImGui::Combo(label, &current,
                   "Millimeters (mm)\0Centimeters (cm)\0Meters (m)\0Inches (in)\0Feet (ft)\0")) {
    if (current != static_cast<int>(*unit)) {
      *unit = static_cast<Unit>(current);
      changed = true;
    }
  }
  return changed;
}
}  // namespace

void drawMenuBar(AppState* app) {
  if (!ImGui::BeginMainMenuBar()) return;

  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("New Scene")) {
      app->mesh = StlMesh();
      clearSceneObjects(app);
      app->renderer.setMesh(app->mesh);
      exitSketchMode(app);
      clearSketches(app);
      app->extrudeTool.cancel();
      app->sketchCreate = {};
      app->partialSelectedObject = -1;
      app->project.initFromAppSettings(app->appSettings);
      app->status = "New scene";
    }
    if (ImGui::MenuItem("Open STL...")) {
      if (!app->loadingMesh && !app->fileBrowser.isVisible()) {
        app->fileBrowser.show({{"STL Files", {".stl"}}}, {}, "Open File", "Open");
      }
    }
    if (ImGui::MenuItem("Export STL...", nullptr, false, app->selectedObject >= 0)) {
      if (!app->exportBrowser.isVisible()) {
        app->exportBrowser.show({{"STL Files", {".stl"}}}, {}, "Export STL", "Export");
      }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("3D Print...", nullptr, false, !app->sceneObjects.empty())) {
      app->printWindow.show(static_cast<int>(app->sceneObjects.size()),
                            app->printSettings);
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Sketch")) {
    if (ImGui::MenuItem("New Sketch...")) {
      app->sketchCreate.open = true;
      app->sketchCreate.pickFromScene = true;
      app->sketchCreate.fromFace = false;
      app->sketchCreate.sourceObject = -1;
      app->partialSelectedObject = -1;
      app->status = "Click origin planes or an object face to set the sketch plane";
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Exit Sketch", nullptr, false,
                        app->sceneMode == SceneMode::Sketch && app->hasActiveSketch())) {
      exitSketchMode(app);
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Settings")) {
    if (ImGui::MenuItem("Application Settings...")) {
      app->showAppSettings = true;
    }
    if (ImGui::MenuItem("Project Settings...")) {
      app->showProjectSettings = true;
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Tools")) {
    const bool canCombine = app->sceneMode == SceneMode::View3D &&
                            app->sceneObjects.size() >= 2;
    if (ImGui::MenuItem("Combine...", nullptr, false, canCombine)) {
      app->combineOptions.visible = true;
      app->combineOptions.operation = BooleanOp::Add;
      app->combineOptions.keepTools = false;
      app->combineOptions.targets.clear();
      app->combineOptions.tools.clear();
      if (app->selectedObject >= 0) {
        addUniqueIndex(app->combineOptions.targets, app->selectedObject);
      }
      app->objectPickMode = ObjectPickMode::None;
      app->status = "Combine tool opened";
    }
    ImGui::EndMenu();
  }

  ImGui::Separator();
  if (app->sceneMode == SceneMode::Sketch && app->hasActiveSketch()) {
    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.15f, 1.0f), "MODE: SKETCH");
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", app->activeSketchMeta().name.data());
  } else {
    ImGui::TextDisabled("MODE: 3D VIEW");
  }

  ImGui::EndMainMenuBar();
}

void drawNewSketchWindow(AppState* app) {
  if (!app->sketchCreate.open) return;

  ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("New Sketch", &app->sketchCreate.open)) {
    ImGui::End();
    return;
  }

  ImGui::TextWrapped("Pick a base plane from center axes or click a face on an object.");
  ImGui::TextWrapped("Then set an optional offset and create the sketch.");
  ImGui::Separator();

  const char* planeName = "XY";
  if (app->sketchCreate.plane == SketchPlane::XZ) planeName = "XZ";
  if (app->sketchCreate.plane == SketchPlane::YZ) planeName = "YZ";
  ImGui::Text("Selected plane: %s", planeName);
  if (app->sketchCreate.fromFace && app->sketchCreate.sourceObject >= 0) {
    ImGui::Text("Source: Object %d face", app->sketchCreate.sourceObject + 1);
  } else {
    ImGui::TextUnformatted("Source: Center plane");
  }
  ImGui::InputFloat("Offset (mm)", &app->sketchCreate.offsetMm, 1.0f, 10.0f, "%.3f");

  if (!app->sketchCreate.pickFromScene) {
    if (ImGui::Button("Pick Plane From Scene", ImVec2(-1.0f, 0.0f))) {
      app->sketchCreate.pickFromScene = true;
      app->partialSelectedObject = -1;
      app->status = "Click a center plane near origin or click an object face";
    }
  } else {
    ImGui::TextDisabled("Picking active: click in viewport to choose plane");
    if (ImGui::Button("Stop Picking", ImVec2(-1.0f, 0.0f))) {
      app->sketchCreate.pickFromScene = false;
      app->partialSelectedObject = -1;
    }
  }

  ImGui::Spacing();
  if (ImGui::Button("Create Sketch", ImVec2(150.0f, 0.0f))) {
    createSketch(app, app->sketchCreate.plane, app->sketchCreate.offsetMm);
    app->sketchCreate.open = false;
    app->sketchCreate.pickFromScene = false;
    app->partialSelectedObject = -1;
    app->status = "Sketch created";
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(150.0f, 0.0f))) {
    app->sketchCreate = {};
    app->partialSelectedObject = -1;
  }

  ImGui::End();
}

void drawProjectToolWindow(AppState* app) {
  if (!app->showProjectTool || app->sceneMode != SceneMode::Sketch || !app->hasActiveSketch()) {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Project Tool", &app->showProjectTool)) {
    ImGui::End();
    return;
  }

  if (app->activeSketchIndex <= 0) {
    ImGui::TextDisabled("No earlier sketches to project from.");
    ImGui::End();
    return;
  }

  if (app->projectSourceSketchIndex < 0 || app->projectSourceSketchIndex >= app->activeSketchIndex) {
    app->projectSourceSketchIndex = app->activeSketchIndex - 1;
  }

  std::vector<const char*> names;
  names.reserve(app->activeSketchIndex);
  for (int i = 0; i < app->activeSketchIndex; ++i) {
    names.push_back(app->sketches[i].meta.name.data());
  }
  int source = app->projectSourceSketchIndex;
  if (ImGui::Combo("Source Sketch", &source, names.data(), static_cast<int>(names.size()))) {
    app->projectSourceSketchIndex = source;
  }

  const auto& sourceSketch = app->sketches[app->projectSourceSketchIndex].sketch;
  const auto& elems = sourceSketch.elements();
  if (app->projectSelectionMask.size() != elems.size()) {
    app->projectSelectionMask.assign(elems.size(), false);
  }

  if (ImGui::Button("Select All")) {
    std::fill(app->projectSelectionMask.begin(), app->projectSelectionMask.end(), true);
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear")) {
    std::fill(app->projectSelectionMask.begin(), app->projectSelectionMask.end(), false);
  }

  ImGui::BeginChild("##projectElems", ImVec2(0.0f, 180.0f), true);
  for (size_t i = 0; i < elems.size(); ++i) {
    std::string label = "Element " + std::to_string(i + 1);
    bool checked = app->projectSelectionMask[i];
    if (ImGui::Checkbox(label.c_str(), &checked)) {
      app->projectSelectionMask[i] = checked;
    }
  }
  ImGui::EndChild();

  const bool canProject = !app->activeSketchLocked() && !app->projectSelectionMask.empty();
  if (ImGui::Button("Project Selected", ImVec2(-1.0f, 0.0f))) {
    if (!canProject) {
      app->status = "Active sketch is locked or source is empty";
    } else {
      int copied = 0;
      int created = 0;
      const SketchPlane srcPlane = app->sketches[app->projectSourceSketchIndex].plane;
      const float srcOffset = app->sketches[app->projectSourceSketchIndex].offsetMm;
      const SketchPlane dstPlane = app->activePlane();
      const float dstOffset = app->activePlaneOffset();
      for (size_t i = 0; i < elems.size(); ++i) {
        if (!app->projectSelectionMask[i]) continue;
        const auto projected = projectElementBetweenSketchPlanes(
            elems[i], srcPlane, srcOffset, dstPlane, dstOffset);
        for (const auto& elem : projected) {
          app->activeSketch().addElement(elem);
          ++created;
        }
        ++copied;
      }
      if (copied > 0) {
        app->status = "Projected " + std::to_string(copied) +
                      " source element(s) as " + std::to_string(created) +
                      " projected element(s)";
      } else {
        app->status = "No elements selected";
      }
    }
  }

  ImGui::End();
}

void drawPanel(AppState* app) {
  ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(240.0f, 0.0f), ImGuiCond_FirstUseEver);
  ImGui::Begin("Actions");

  if (ImGui::Button("Open", ImVec2(-1.0f, 0.0f))) {
    if (!app->loadingMesh && !app->fileBrowser.isVisible()) {
      app->fileBrowser.show({{"STL Files", {".stl"}}}, {}, "Open File", "Open");
    }
  }

  ImGui::Separator();

  if (ImGui::Button("Wireframe", ImVec2(-1.0f, 0.0f))) {
    if (!app->renderer.wireframeSupported()) {
      app->status = "Wireframe not supported on this GPU.";
    } else {
      app->renderer.toggleWireframe();
      app->status = app->renderer.wireframeEnabled() ? "Wireframe: ON" : "Wireframe: OFF";
    }
  }
  if (ImGui::Button("Normals", ImVec2(-1.0f, 0.0f))) {
    app->renderer.toggleNormalVisualization();
    app->status =
        app->renderer.normalVisualizationEnabled() ? "Normals view: ON" : "Normals view: OFF";
  }

  ImGui::Separator();

  const auto snap = [&](const char* label, CameraController::Orientation o) {
    if (ImGui::Button(label, ImVec2(-1.0f, 0.0f))) app->camera.snap(o);
  };
  snap("Snap Front", CameraController::Orientation::Front);
  snap("Snap Right", CameraController::Orientation::Right);
  snap("Snap Top",   CameraController::Orientation::Top);
  snap("Snap ISO",   CameraController::Orientation::Isometric);

  ImGui::Separator();

  ImGui::TextWrapped("LMB drag: rotate");
  ImGui::TextWrapped("Mouse wheel: zoom");
  ImGui::TextWrapped("Triangles: %d", static_cast<int>(app->mesh.indices().size() / 3));

  ImGui::Separator();

  ImGui::TextWrapped("Validation: %s", app->renderer.validationEnabled() ? "ON" : "OFF");
  ImGui::TextWrapped("Wireframe Support: %s", app->renderer.wireframeSupported() ? "YES" : "NO");
  ImGui::TextWrapped("Wireframe: %s", app->renderer.wireframeEnabled() ? "ON" : "OFF");
  ImGui::TextWrapped("Normals View: %s",
                     app->renderer.normalVisualizationEnabled() ? "ON" : "OFF");

  if (app->loadingMesh) {
    const double t = ImGui::GetTime();
    const int phase = static_cast<int>(t * 3.0) % 4;
    const char* suffix = phase == 1 ? "." : phase == 2 ? ".." : phase == 3 ? "..." : "";
    ImGui::Text("Loading mesh%s", suffix);
  }

  ImGui::Separator();
  ImGui::TextWrapped("Status: %s", app->status.c_str());
  ImGui::End();
}

void drawAppSettingsWindow(AppState* app) {
  if (!app->showAppSettings) return;

  ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Application Settings", &app->showAppSettings)) {
    ImGui::TextWrapped("These defaults apply to new projects.");
    ImGui::Separator();

    unitCombo("Default Unit", &app->appSettings.defaultUnit);
    unitCombo("Export Unit", &app->appSettings.exportUnit);
    ImGui::DragFloat("Grid Spacing (mm)", &app->appSettings.gridSpacing, 1.0f, 1.0f, 1000.0f);
    ImGui::DragFloat("Grid Extent (mm)", &app->appSettings.gridExtent, 10.0f, 10.0f, 10000.0f);

    ImGui::Separator();
    if (ImGui::Button("Save")) {
      if (app->appSettings.save()) {
        app->status = "Application settings saved";
      } else {
        app->status = "Failed to save settings";
      }
    }
  }
  ImGui::End();
}

void drawProjectSettingsWindow(AppState* app) {
  if (!app->showProjectSettings) return;

  ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Project Settings", &app->showProjectSettings)) {
    ImGui::TextWrapped("Settings for the current project.");
    ImGui::Separator();

    unitCombo("Default Unit", &app->project.defaultUnit);
    unitCombo("Export Unit", &app->project.exportUnit);
    ImGui::DragFloat("Grid Spacing (mm)", &app->project.gridSpacing, 1.0f, 1.0f, 1000.0f);
    ImGui::DragFloat("Grid Extent (mm)", &app->project.gridExtent, 10.0f, 10.0f, 10000.0f);

    ImGui::Separator();
    if (ImGui::Button("Reset from App Defaults")) {
      app->project.initFromAppSettings(app->appSettings);
      app->status = "Project settings reset from app defaults";
    }
  }
  ImGui::End();
}

void drawObjectBrowserWindow(AppState* app) {
  ImGui::SetNextWindowPos(ImVec2(12.0f, 320.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_FirstUseEver);
  ImGui::Begin("Object Browser");

  const bool multiSelect = ImGui::GetIO().KeyCtrl;
  const bool browserFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  const auto tintRow = [](bool visible, bool locked) {
    if (!visible && locked) {
      return IM_COL32(90, 70, 40, 50);
    }
    if (!visible) {
      return IM_COL32(70, 70, 70, 45);
    }
    if (locked) {
      return IM_COL32(110, 90, 40, 40);
    }
    return IM_COL32(0, 0, 0, 0);
  };

  if (ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::BeginTable("##objectsTable", 3,
                          ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthFixed, 24.0f);
      ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 24.0f);
      ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();

      for (int i = 0; i < static_cast<int>(app->sceneObjects.size()); ++i) {
        auto& meta = app->sceneObjectMeta[i];
        const bool selected = hasIndex(app->browserSelectedObjects, i);
        const bool renameInline = app->renameObjectIndex == i &&
                                  app->browserSelectedObjects.size() == 1 &&
                                  selected;

        ImGui::TableNextRow();
        const ImU32 rowTint = tintRow(meta.visible, meta.locked);
        if (rowTint != 0) {
          ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowTint);
        }
        ImGui::PushID(i);

        ImGui::TableSetColumnIndex(0);
        if (ImGui::Checkbox("##objvis", &meta.visible)) {
          rebuildCombinedMesh(app);
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::Checkbox("##objlock", &meta.locked);

        ImGui::TableSetColumnIndex(2);
        if (renameInline) {
          if (app->focusRenameInput) {
            ImGui::SetKeyboardFocusHere();
            app->focusRenameInput = false;
          }
          ImGui::SetNextItemWidth(-1.0f);
          const bool accepted = ImGui::InputText(
              "##objname", app->renameBuffer.data(), app->renameBuffer.size(),
              ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
          const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
          if (cancel) {
            cancelBrowserRename(app);
          } else if (accepted) {
            commitObjectRename(app);
          } else if (ImGui::IsItemDeactivated()) {
            commitObjectRename(app);
          }
        } else {
          std::string label = meta.name.data();
          const char* state = browserRowLabel(meta.visible, meta.locked);
          if (state[0] != '\0') {
            label += " ";
            label += state;
          }
          if (ImGui::Selectable(label.c_str(), selected,
                                ImGuiSelectableFlags_SpanAllColumns)) {
            const bool shiftClick = ImGui::GetIO().KeyShift;
            if (shiftClick && app->objectSelectionAnchor >= 0) {
              setSelectionRange(app->browserSelectedObjects, app->objectSelectionAnchor, i);
            } else {
              setSingleOrMultiSelection(app->browserSelectedObjects, i, multiSelect);
              app->objectSelectionAnchor = i;
            }
            syncSelectedObjectFromBrowser(app);
            app->renameObjectIndex = -1;
            app->browserFocusSection = BrowserSection::Objects;
          }
          if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            app->browserSelectedObjects.assign(1, i);
            app->objectSelectionAnchor = i;
            syncSelectedObjectFromBrowser(app);
            beginObjectRename(app, i);
            app->browserFocusSection = BrowserSection::Objects;
          }
        }

        ImGui::PopID();
      }

      ImGui::EndTable();
    }
    if (app->sceneObjects.empty()) {
      ImGui::TextDisabled("(no objects)");
    }
  }

  if (ImGui::CollapsingHeader("Sketches", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::BeginTable("##sketchesTable", 3,
                          ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthFixed, 24.0f);
      ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 24.0f);
      ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();

      for (int i = 0; i < static_cast<int>(app->sketches.size()); ++i) {
        if (app->sceneMode == SceneMode::Sketch && app->hasActiveSketch() && i > app->activeSketchIndex) {
          continue;
        }
        auto& meta = app->sketches[i].meta;
        const bool selected = hasIndex(app->browserSelectedSketches, i);
        const bool renameInline = app->renameSketchIndex == i &&
                                  app->browserSelectedSketches.size() == 1 &&
                                  selected;

        ImGui::TableNextRow();
        const ImU32 rowTint = tintRow(meta.visible, meta.locked);
        if (rowTint != 0) {
          ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowTint);
        }
        ImGui::PushID(1000 + i);

        ImGui::TableSetColumnIndex(0);
        const bool changedVisibility = ImGui::Checkbox("##skvis", &meta.visible);

        ImGui::TableSetColumnIndex(1);
        const bool changedLock = ImGui::Checkbox("##sklock", &meta.locked);

        ImGui::TableSetColumnIndex(2);
        if (renameInline) {
          if (app->focusRenameInput) {
            ImGui::SetKeyboardFocusHere();
            app->focusRenameInput = false;
          }
          ImGui::SetNextItemWidth(-1.0f);
          const bool accepted = ImGui::InputText(
              "##skname", app->renameBuffer.data(), app->renameBuffer.size(),
              ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
          const bool cancel = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
          if (cancel) {
            cancelBrowserRename(app);
          } else if (accepted) {
            commitSketchRename(app);
          } else if (ImGui::IsItemDeactivated()) {
            commitSketchRename(app);
          }
        } else {
          std::string label = meta.name.data();
          const char* state = browserRowLabel(meta.visible, meta.locked);
          if (state[0] != '\0') {
            label += " ";
            label += state;
          }
          if (ImGui::Selectable(label.c_str(), selected,
                                ImGuiSelectableFlags_SpanAllColumns)) {
            const bool shiftClick = ImGui::GetIO().KeyShift;
            if (shiftClick && app->sketchSelectionAnchor >= 0) {
              setSelectionRange(app->browserSelectedSketches, app->sketchSelectionAnchor, i);
            } else {
              setSingleOrMultiSelection(app->browserSelectedSketches, i, multiSelect);
              app->sketchSelectionAnchor = i;
            }
            syncSelectedSketchFromBrowser(app);
            app->renameSketchIndex = -1;
            app->browserFocusSection = BrowserSection::Sketches;
          }
          if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            app->browserSelectedSketches.assign(1, i);
            app->sketchSelectionAnchor = i;
            syncSelectedSketchFromBrowser(app);
            beginSketchRename(app, i);
            app->browserFocusSection = BrowserSection::Sketches;
          }
        }

        if ((changedVisibility || changedLock) &&
            app->hasActiveSketch() && app->activeSketchIndex == i &&
            (!meta.visible || meta.locked)) {
          app->sketchTool.cancel();
          app->sketchTool.setTool(Tool::None);
          app->extrudeTool.cancel();
          app->activeSketch().clearSelection();
        }

        ImGui::PopID();
      }

      ImGui::EndTable();
    }
    if (app->sketches.empty()) {
      ImGui::TextDisabled("(no sketches)");
    } else if (app->sceneMode == SceneMode::Sketch && app->hasActiveSketch() &&
               app->activeSketchIndex + 1 < static_cast<int>(app->sketches.size())) {
      ImGui::TextDisabled("Future sketches are hidden while editing this sketch");
    }
  }

  if (browserFocused && !ImGui::IsAnyItemActive()) {
    if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
      if (app->browserFocusSection == BrowserSection::Objects &&
          app->browserSelectedObjects.size() == 1) {
        beginObjectRename(app, app->browserSelectedObjects.front());
      } else if (app->browserFocusSection == BrowserSection::Sketches &&
                 app->browserSelectedSketches.size() == 1) {
        beginSketchRename(app, app->browserSelectedSketches.front());
      }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
      if (app->browserFocusSection == BrowserSection::Objects) {
        const int count = static_cast<int>(app->sceneObjects.size());
        if (ImGui::GetIO().KeyShift && count > 0) {
          int anchor = app->objectSelectionAnchor >= 0 ? app->objectSelectionAnchor :
                       (app->browserSelectedObjects.empty() ? 0 : app->browserSelectedObjects.front());
          int current = app->browserSelectedObjects.empty() ? anchor : app->browserSelectedObjects.front();
          current = std::clamp(current - 1, 0, count - 1);
          app->objectSelectionAnchor = anchor;
          setSelectionRange(app->browserSelectedObjects, anchor, current);
        } else {
          stepSelection(app->browserSelectedObjects, count, -1);
          if (!app->browserSelectedObjects.empty()) {
            app->objectSelectionAnchor = app->browserSelectedObjects.front();
          }
        }
        syncSelectedObjectFromBrowser(app);
      } else {
        const int count = (app->sceneMode == SceneMode::Sketch && app->hasActiveSketch())
                              ? (app->activeSketchIndex + 1)
                              : static_cast<int>(app->sketches.size());
        if (ImGui::GetIO().KeyShift && count > 0) {
          int anchor = app->sketchSelectionAnchor >= 0 ? app->sketchSelectionAnchor :
                       (app->browserSelectedSketches.empty() ? 0 : app->browserSelectedSketches.front());
          int current = app->browserSelectedSketches.empty() ? anchor : app->browserSelectedSketches.front();
          current = std::clamp(current - 1, 0, count - 1);
          app->sketchSelectionAnchor = anchor;
          setSelectionRange(app->browserSelectedSketches, anchor, current);
        } else {
          stepSelection(app->browserSelectedSketches, count, -1);
          if (!app->browserSelectedSketches.empty()) {
            app->sketchSelectionAnchor = app->browserSelectedSketches.front();
          }
        }
        syncSelectedSketchFromBrowser(app);
      }
      cancelBrowserRename(app);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
      if (app->browserFocusSection == BrowserSection::Objects) {
        const int count = static_cast<int>(app->sceneObjects.size());
        if (ImGui::GetIO().KeyShift && count > 0) {
          int anchor = app->objectSelectionAnchor >= 0 ? app->objectSelectionAnchor :
                       (app->browserSelectedObjects.empty() ? 0 : app->browserSelectedObjects.front());
          int current = app->browserSelectedObjects.empty() ? anchor : app->browserSelectedObjects.front();
          current = std::clamp(current + 1, 0, count - 1);
          app->objectSelectionAnchor = anchor;
          setSelectionRange(app->browserSelectedObjects, anchor, current);
        } else {
          stepSelection(app->browserSelectedObjects, count, 1);
          if (!app->browserSelectedObjects.empty()) {
            app->objectSelectionAnchor = app->browserSelectedObjects.front();
          }
        }
        syncSelectedObjectFromBrowser(app);
      } else {
        const int count = (app->sceneMode == SceneMode::Sketch && app->hasActiveSketch())
                              ? (app->activeSketchIndex + 1)
                              : static_cast<int>(app->sketches.size());
        if (ImGui::GetIO().KeyShift && count > 0) {
          int anchor = app->sketchSelectionAnchor >= 0 ? app->sketchSelectionAnchor :
                       (app->browserSelectedSketches.empty() ? 0 : app->browserSelectedSketches.front());
          int current = app->browserSelectedSketches.empty() ? anchor : app->browserSelectedSketches.front();
          current = std::clamp(current + 1, 0, count - 1);
          app->sketchSelectionAnchor = anchor;
          setSelectionRange(app->browserSelectedSketches, anchor, current);
        } else {
          stepSelection(app->browserSelectedSketches, count, 1);
          if (!app->browserSelectedSketches.empty()) {
            app->sketchSelectionAnchor = app->browserSelectedSketches.front();
          }
        }
        syncSelectedSketchFromBrowser(app);
      }
      cancelBrowserRename(app);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete) &&
        app->browserFocusSection == BrowserSection::Objects &&
        !app->browserSelectedObjects.empty()) {
      app->pendingDeleteObjects = app->browserSelectedObjects;
      app->openDeleteObjectsPopup = true;
    }
  }

  if (app->openDeleteObjectsPopup) {
    ImGui::OpenPopup("Delete Objects?");
    app->openDeleteObjectsPopup = false;
  }
  if (ImGui::BeginPopupModal("Delete Objects?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    std::vector<int> deletable = app->pendingDeleteObjects;
    sanitizeObjectIndices(deletable, static_cast<int>(app->sceneObjects.size()));
    deletable.erase(std::remove_if(deletable.begin(), deletable.end(), [app](int idx) {
                    return idx >= static_cast<int>(app->sceneObjectMeta.size()) ||
                           app->sceneObjectMeta[idx].locked;
                  }),
                  deletable.end());

    if (deletable.empty()) {
      ImGui::TextUnformatted("No unlocked selected objects can be deleted.");
    } else {
      ImGui::Text("Delete %d selected object(s)?", static_cast<int>(deletable.size()));
      for (int idx : deletable) {
        ImGui::BulletText("%s", app->sceneObjectMeta[idx].name.data());
      }
    }

    ImGui::Spacing();
    if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f))) {
      if (!deletable.empty()) {
        deleteSceneObjects(app, deletable);
        app->status = "Deleted selected objects";
      }
      app->pendingDeleteObjects.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
      app->pendingDeleteObjects.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::Separator();
  ImGui::TextDisabled("Ctrl+click: multi-select  |  Shift+click: range  |  F2: rename  |  Del: delete  |  Arrows: move");
  ImGui::End();
}

void drawExtrudeOptionsWindow(AppState* app) {
  if (!app->extrudeOptions.visible) return;
  if (app->sceneMode != SceneMode::Sketch || !app->hasActiveSketch()) {
    app->extrudeTool.cancel();
    app->extrudeOptions.visible = false;
    if (app->objectPickMode == ObjectPickMode::ExtrudeTargets) {
      app->objectPickMode = ObjectPickMode::None;
    }
    return;
  }
  if (!app->extrudeTool.active()) {
    app->extrudeOptions.visible = false;
    app->objectPickMode = ObjectPickMode::None;
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_FirstUseEver);
  bool open = app->extrudeOptions.visible;
  if (ImGui::Begin("Extrude Options", &open)) {
    int op = static_cast<int>(app->extrudeOptions.operation);
    if (ImGui::Combo("Operation", &op, "Add\0Subtract\0Create New Object\0")) {
      app->extrudeOptions.operation = static_cast<ExtrudeOp>(op);
      if (app->extrudeOptions.operation == ExtrudeOp::CreateNewObject) {
        app->extrudeOptions.targets.clear();
        if (app->objectPickMode == ObjectPickMode::ExtrudeTargets) {
          app->objectPickMode = ObjectPickMode::None;
        }
      }
    }

    ImGui::Text("Depth:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputText("##extrudeDepth", app->extrudeOptions.depthBuffer,
                     sizeof(app->extrudeOptions.depthBuffer));
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", unitSuffix(app->project.defaultUnit));

    ImGui::Separator();
    if (app->extrudeOptions.operation == ExtrudeOp::CreateNewObject) {
      ImGui::TextDisabled("Target selection is disabled in Create New Object mode.");
    } else {
      drawObjectSelectionList("Target Objects", app->extrudeOptions.targets,
                              ObjectPickMode::ExtrudeTargets, app);
    }

    ImGui::Separator();
    bool applyNow = app->extrudeOptions.applyRequested;
    app->extrudeOptions.applyRequested = false;
    if (ImGui::Button("Apply")) applyNow = true;
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      app->extrudeTool.cancel();
      app->extrudeOptions.visible = false;
      app->objectPickMode = ObjectPickMode::None;
    }

    if (applyNow) {
      auto parsed = parseDimension(std::string(app->extrudeOptions.depthBuffer),
                                   app->project.defaultUnit);
      if (!parsed) {
        app->status = "Extrude: enter a valid depth";
      } else {
        app->extrudeTool.setDistance(parsed->valueMm);
        const bool subtract = (app->extrudeOptions.operation == ExtrudeOp::Subtract);
        const bool createNewObject =
          (app->extrudeOptions.operation == ExtrudeOp::CreateNewObject);
        const auto profiles = app->extrudeTool.profiles();
        const auto extrudePlane = app->extrudeTool.plane();
        const float depthMm = parsed->valueMm;
        StlMesh extruded = app->extrudeTool.confirm();

        auto recordExtrude = [&]() {
          ExtrudeAction ea;
          ea.sketchIndex = app->activeSketchIndex;
          if (app->hasActiveSketch())
            ea.sketchName = std::string(app->sketches[app->activeSketchIndex].meta.name.data());
          ea.profiles = profiles;
          ea.plane = extrudePlane;
          ea.sketchOffsetMm = app->hasActiveSketch()
                                  ? app->sketches[app->activeSketchIndex].offsetMm
                                  : 0.0f;
          ea.depthMm = depthMm;
          ea.subtract = subtract;
            const auto targetNames = createNewObject
                           ? std::vector<int>{}
                           : app->extrudeOptions.targets;
            ea.targetObjectNames = objectNames(app, targetNames);
          ea.resultObjectName = objectName(
              app, static_cast<int>(app->sceneObjects.size()) - 1);
          app->timeline.push(std::move(ea),
                     std::string(createNewObject
                             ? "Extrude New Object"
                             : (subtract ? "Subtract Extrude" : "Add Extrude")));
        };

        if (subtract) {
          if (!applySubtractExtrude(app, extruded, app->extrudeOptions.targets)) {
            app->status = "Extrude subtract removed no target objects";
          } else {
            recordExtrude();
            app->status = "Extrude subtract complete (object-level)";
          }
          app->extrudeOptions.visible = false;
          app->objectPickMode = ObjectPickMode::None;
        } else {
          const auto targets = createNewObject ? std::vector<int>{} : app->extrudeOptions.targets;
          if (!applyAddExtrude(app, extruded, targets)) {
            app->status = "Extrude failed";
          } else {
            recordExtrude();
            app->status = createNewObject ? "Extrude new object complete" : "Extrude add complete";
            app->extrudeOptions.visible = false;
            app->objectPickMode = ObjectPickMode::None;
          }
        }
      }
    }
  }
  ImGui::End();

  app->extrudeOptions.visible = open;
  if (!app->extrudeOptions.visible && app->objectPickMode == ObjectPickMode::ExtrudeTargets) {
    app->objectPickMode = ObjectPickMode::None;
  }
}

void drawCombineWindow(AppState* app) {
  if (!app->combineOptions.visible) return;

  ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_FirstUseEver);
  bool open = app->combineOptions.visible;
  if (ImGui::Begin("Combine Tool", &open)) {
    int op = static_cast<int>(app->combineOptions.operation);
    if (ImGui::Combo("Operation", &op, "Add\0Subtract\0")) {
      app->combineOptions.operation = static_cast<BooleanOp>(op);
    }
    ImGui::Checkbox("Keep Tools", &app->combineOptions.keepTools);

    ImGui::Separator();
    drawObjectSelectionList("Target Objects", app->combineOptions.targets,
                            ObjectPickMode::CombineTargets, app);
    ImGui::Separator();
    drawObjectSelectionList("Tool Objects", app->combineOptions.tools,
                            ObjectPickMode::CombineTools, app);

    ImGui::Separator();
    ImGui::TextDisabled("Subtract currently uses object-overlap fallback");
    ImGui::TextDisabled("(removes overlapping targets by AABB)");

    ImGui::Separator();
    if (ImGui::Button("Apply")) {
      const bool subtract = (app->combineOptions.operation == BooleanOp::Subtract);
      auto targetNamesBefore = objectNames(app, app->combineOptions.targets);
      auto toolNamesBefore = objectNames(app, app->combineOptions.tools);

      auto recordCombine = [&]() {
        CombineAction ca;
        ca.subtract = subtract;
        ca.keepTools = app->combineOptions.keepTools;
        ca.targetNames = targetNamesBefore;
        ca.toolNames = toolNamesBefore;
        ca.resultObjectName = objectName(
            app, static_cast<int>(app->sceneObjects.size()) - 1);
        app->timeline.push(std::move(ca),
                           std::string(subtract ? "Subtract Combine" : "Add Combine"));
      };

      if (subtract) {
        if (!applySubtractCombine(app, app->combineOptions.targets,
                                  app->combineOptions.tools,
                                  app->combineOptions.keepTools)) {
          app->status = "Combine subtract removed no target objects";
        } else {
          recordCombine();
          app->status = "Combine subtract complete (object-level)";
        }
      } else if (!applyAddCombine(app, app->combineOptions.targets,
                                  app->combineOptions.tools,
                                  app->combineOptions.keepTools)) {
        app->status = "Combine add requires at least one target and one tool";
      } else {
        recordCombine();
        app->status = "Combine add complete";
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
      app->combineOptions.visible = false;
      if (app->objectPickMode == ObjectPickMode::CombineTargets ||
          app->objectPickMode == ObjectPickMode::CombineTools) {
        app->objectPickMode = ObjectPickMode::None;
      }
    }
  }
  ImGui::End();

  app->combineOptions.visible = open;
  if (!app->combineOptions.visible &&
      (app->objectPickMode == ObjectPickMode::CombineTargets ||
       app->objectPickMode == ObjectPickMode::CombineTools)) {
    app->objectPickMode = ObjectPickMode::None;
  }
}
