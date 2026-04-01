#include "ui/UIWindows.hpp"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <imgui.h>

#include "Units.hpp"
#include "core/AppLogic.hpp"
#include "sketch/Extrude.hpp"
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

int findCreateEntryIndexForSketch(const Timeline& timeline, int sketchIndex) {
  if (sketchIndex < 0) return -1;
  int createCount = 0;
  const auto& entries = timeline.entries();
  for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
    if (std::get_if<CreateSketchAction>(&entries[i].action) != nullptr) {
      if (createCount == sketchIndex) return i;
      ++createCount;
    }
  }
  return -1;
}

bool pointInPolygon2D(glm::vec2 pt, const std::vector<glm::vec2>& poly) {
  size_t n = poly.size();
  if (n > 0 && glm::length(poly.front() - poly.back()) < 0.001f) --n;
  if (n < 3) return false;
  bool inside = false;
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const glm::vec2& a = poly[i];
    const glm::vec2& b = poly[j];
    const bool intersect = ((a.y > pt.y) != (b.y > pt.y)) &&
                           (pt.x < (b.x - a.x) * (pt.y - a.y) / ((b.y - a.y) + 1e-12f) + a.x);
    if (intersect) inside = !inside;
  }
  return inside;
}

glm::vec2 centroid2D(const std::vector<glm::vec2>& poly) {
  size_t n = poly.size();
  if (n > 0 && glm::length(poly.front() - poly.back()) < 0.001f) --n;
  if (n == 0) return {0.0f, 0.0f};
  glm::vec2 c{0.0f, 0.0f};
  for (size_t i = 0; i < n; ++i) c += poly[i];
  return c / static_cast<float>(n);
}

const char* planeShortName(SketchPlane plane) {
  switch (plane) {
    case SketchPlane::XY: return "XY";
    case SketchPlane::XZ: return "XZ";
    case SketchPlane::YZ: return "YZ";
  }
  return "XY";
}

const char* faceSignLabel(int faceSign) {
  return faceSign >= 0 ? "+" : "-";
}

bool lockIconToggle(const char* id, bool* locked, bool enabled = true) {
  if (!locked) return false;

  if (!enabled) ImGui::BeginDisabled();
  const float h = ImGui::GetFrameHeight();
  const float w = h * 0.95f;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  bool changed = false;
  if (ImGui::InvisibleButton(id, ImVec2(w, h))) {
    *locked = !*locked;
    changed = true;
  }

  const bool hovered = ImGui::IsItemHovered();
  const ImU32 col = ImGui::GetColorU32(
      hovered ? ImVec4(0.95f, 0.85f, 0.35f, 1.0f)
              : (*locked ? ImVec4(0.90f, 0.78f, 0.22f, 1.0f)
                         : ImVec4(0.66f, 0.66f, 0.66f, 1.0f)));
  ImDrawList* dl = ImGui::GetWindowDrawList();

  const ImVec2 bodyMin(p.x + w * 0.22f, p.y + h * 0.48f);
  const ImVec2 bodyMax(p.x + w * 0.78f, p.y + h * 0.86f);
  dl->AddRect(bodyMin, bodyMax, col, 2.0f, 0, 1.8f);

  const float shackleTop = p.y + h * 0.20f;
  const float shackleBottom = p.y + h * 0.50f;
  const float leftX = p.x + w * 0.30f;
  const float rightX = p.x + w * 0.70f;
  if (*locked) {
    dl->AddLine(ImVec2(leftX, shackleBottom), ImVec2(leftX, shackleTop), col, 1.8f);
    dl->AddLine(ImVec2(leftX, shackleTop), ImVec2(rightX, shackleTop), col, 1.8f);
    dl->AddLine(ImVec2(rightX, shackleTop), ImVec2(rightX, shackleBottom), col, 1.8f);
  } else {
    dl->AddLine(ImVec2(leftX, shackleBottom), ImVec2(leftX, shackleTop), col, 1.8f);
    dl->AddLine(ImVec2(leftX, shackleTop), ImVec2(rightX - w * 0.08f, shackleTop), col, 1.8f);
    dl->AddLine(ImVec2(rightX, shackleTop + h * 0.09f), ImVec2(rightX, shackleBottom), col,
                1.8f);
  }

  if (hovered) {
    ImGui::SetTooltip(*locked ? "Locked" : "Unlocked");
  }
  if (!enabled) ImGui::EndDisabled();
  return changed;
}

bool visibilityIconToggle(const char* id, bool* visible) {
  if (!visible) return false;

  const float h = ImGui::GetFrameHeight();
  const float w = h * 0.95f;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  bool changed = false;
  if (ImGui::InvisibleButton(id, ImVec2(w, h))) {
    *visible = !*visible;
    changed = true;
  }

  const bool hovered = ImGui::IsItemHovered();
  const ImU32 col = ImGui::GetColorU32(
      hovered ? ImVec4(0.72f, 0.86f, 0.98f, 1.0f)
              : (*visible ? ImVec4(0.60f, 0.80f, 0.96f, 1.0f)
                          : ImVec4(0.42f, 0.42f, 0.42f, 1.0f)));
  ImDrawList* dl = ImGui::GetWindowDrawList();

  const ImVec2 center(p.x + w * 0.5f, p.y + h * 0.56f);
  const float rx = w * 0.32f;
  const float ry = h * 0.22f;
  dl->AddEllipse(center, ImVec2(rx, ry), col, 0.0f, 0, 1.6f);
  const float pupil = (*visible ? h * 0.10f : h * 0.07f);
  dl->AddCircleFilled(center, pupil, col);

  if (!*visible) {
    dl->AddLine(ImVec2(p.x + w * 0.20f, p.y + h * 0.24f),
                ImVec2(p.x + w * 0.80f, p.y + h * 0.88f), col, 1.8f);
  }

  if (hovered) {
    ImGui::SetTooltip(*visible ? "Visible" : "Hidden");
  }
  return changed;
}

ImVec2 uiViewportPos() {
  if (const ImGuiViewport* vp = ImGui::GetMainViewport()) {
    return vp->WorkPos;
  }
  return ImVec2(0.0f, 0.0f);
}

ImVec2 uiViewportSize() {
  if (const ImGuiViewport* vp = ImGui::GetMainViewport()) {
    return vp->WorkSize;
  }
  return ImGui::GetIO().DisplaySize;
}

float uiTopBarHeight() {
  // Menu bar + one tool row (solid/sketch toolbar).
  return ImGui::GetFrameHeight() * 2.0f + 8.0f;
}

void snapCurrentWindowToGuides(float topInset) {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  if (!vp) return;
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
  if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;

  ImVec2 pos = ImGui::GetWindowPos();
  const ImVec2 size = ImGui::GetWindowSize();
  const float threshold = 18.0f;

  const float left = vp->WorkPos.x + 12.0f;
  const float right = vp->WorkPos.x + vp->WorkSize.x - size.x - 12.0f;
  const float top = vp->WorkPos.y + topInset;
  const float bottom = vp->WorkPos.y + vp->WorkSize.y - size.y - 12.0f;

  bool snapped = false;
  if (std::abs(pos.x - left) <= threshold) {
    pos.x = left;
    snapped = true;
  } else if (std::abs(pos.x - right) <= threshold) {
    pos.x = right;
    snapped = true;
  }

  if (std::abs(pos.y - top) <= threshold) {
    pos.y = top;
    snapped = true;
  } else if (std::abs(pos.y - bottom) <= threshold) {
    pos.y = bottom;
    snapped = true;
  }

  if (snapped) {
    ImGui::SetWindowPos(pos, ImGuiCond_Always);
  }
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
      initializeDefaultPlanes(app);
      app->extrudeTool.cancel();
      app->sketchCreate = {};
      app->planeCreate = {};
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
      int selectedPlaneId = defaultPlaneId(SketchPlane::XY);
      if (!app->browserSelectedPlanes.empty()) {
        const int planeIndex = app->browserSelectedPlanes.front();
        if (planeIndex >= 0 && planeIndex < static_cast<int>(app->planes.size())) {
          selectedPlaneId = app->planes[planeIndex].id;
        }
      }
      app->sketchCreate.open = true;
      app->sketchCreate.selectedPlaneId = selectedPlaneId;
      app->partialSelectedObject = -1;
      app->status = "Pick an existing plane from the scene, browser, or list";
    }
    if (ImGui::MenuItem("New Plane...", nullptr, false, app->sceneMode == SceneMode::View3D)) {
      app->planeCreate.open = true;
      app->planeCreate.source = PlaneReferenceSource::Plane;
      app->planeCreate.sourcePlaneId = defaultPlaneId(SketchPlane::XY);
      app->planeCreate.sourceObject = -1;
      app->planeCreate.sourceFacePlane = SketchPlane::XY;
      app->planeCreate.sourceFaceSign = 1;
      std::snprintf(app->planeCreate.distanceBuffer, sizeof(app->planeCreate.distanceBuffer),
                    "10.000");
      if (!app->browserSelectedPlanes.empty()) {
        const int planeIndex = app->browserSelectedPlanes.front();
        if (planeIndex >= 0 && planeIndex < static_cast<int>(app->planes.size())) {
          app->planeCreate.sourcePlaneId = app->planes[planeIndex].id;
        }
      }
      app->status = "Create a new plane from an existing plane or selected face";
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

void drawSolidToolbar(AppState* app) {
  if (app->sceneMode != SceneMode::View3D) return;

  const float menuBarHeight = ImGui::GetFrameHeight();
  ImGui::SetNextWindowPos(ImVec2(0.0f, menuBarHeight), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, 0.0f), ImGuiCond_Always);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                           ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
  ImGui::Begin("##solidToolbar", nullptr, flags);

  if (ImGui::Button("Plane")) {
    app->planeCreate.open = true;
    app->planeCreate.source = PlaneReferenceSource::Plane;
    app->planeCreate.sourcePlaneId = defaultPlaneId(SketchPlane::XY);
    if (!app->browserSelectedPlanes.empty()) {
      const int planeIndex = app->browserSelectedPlanes.front();
      if (planeIndex >= 0 && planeIndex < static_cast<int>(app->planes.size())) {
        app->planeCreate.sourcePlaneId = app->planes[planeIndex].id;
      }
    }
    app->planeCreate.sourceObject = -1;
    app->planeCreate.sourceFacePlane = SketchPlane::XY;
    app->planeCreate.sourceFaceSign = 1;
    std::snprintf(app->planeCreate.distanceBuffer, sizeof(app->planeCreate.distanceBuffer),
                  "10.000");
    app->status = "Plane tool opened";
  }

  ImGui::SameLine();
  const bool hasSketch = !app->sketches.empty();
  if (!hasSketch) ImGui::BeginDisabled();
  if (ImGui::Button("Extrude")) {
    app->solidExtrudeOptions.visible = true;
    app->solidExtrudeOptions.sourceSketch =
        app->browserSelectedSketches.empty()
            ? (static_cast<int>(app->sketches.size()) - 1)
            : app->browserSelectedSketches.front();
    app->solidExtrudeOptions.profileSelected.clear();
    std::snprintf(app->solidExtrudeOptions.depthBuffer,
                  sizeof(app->solidExtrudeOptions.depthBuffer), "10.000");
    app->status = "3D Extrude: pick closed profiles; uncheck inner loops to create holes";
  }
  if (!hasSketch) ImGui::EndDisabled();

  ImGui::SameLine();
  const bool canCombine = app->sceneObjects.size() >= 2;
  if (!canCombine) ImGui::BeginDisabled();
  if (ImGui::Button("Combine")) {
    app->combineOptions.visible = true;
    app->combineOptions.operation = BooleanOp::Add;
    app->combineOptions.keepTools = false;
    app->combineOptions.targets.clear();
    app->combineOptions.tools.clear();
    if (app->selectedObject >= 0) {
      addUniqueIndex(app->combineOptions.targets, app->selectedObject);
    }
    app->objectPickMode = ObjectPickMode::None;
  }
  if (!canCombine) ImGui::EndDisabled();

  ImGui::SameLine();
  const bool hasSelectedObject = app->selectedObject >= 0 &&
                                 app->selectedObject < static_cast<int>(app->sceneObjects.size());
  if (!hasSelectedObject) ImGui::BeginDisabled();
  if (ImGui::Button("Chamfer")) {
    app->chamferOptions.visible = true;
    app->chamferOptions.targetObject = app->selectedObject;
    app->chamferOptions.edges.clear();
    std::snprintf(app->chamferOptions.distanceBuffer,
                  sizeof(app->chamferOptions.distanceBuffer), "1.000");
    app->objectPickMode = ObjectPickMode::ChamferEdges;
    app->chamferOptions.pickEdges = true;
    app->status = "Chamfer: click edges on the selected object";
  }
  if (!hasSelectedObject) ImGui::EndDisabled();

  ImGui::SameLine();
  ImGui::TextDisabled("3D Solid Tools");

  ImGui::End();
}

void drawNewPlaneWindow(AppState* app) {
  if (!app->planeCreate.open) return;

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_FirstUseEver);
  bool open = app->planeCreate.open;
  if (!ImGui::Begin("New Plane", &open)) {
    ImGui::End();
    app->planeCreate.open = open;
    return;
  }

  ImGui::TextWrapped("Create a reference plane from an existing plane or an object face.");
  ImGui::Separator();

  int sourceMode = static_cast<int>(app->planeCreate.source);
  if (ImGui::Combo("Reference", &sourceMode, "Plane\0Face\0")) {
    app->planeCreate.source = static_cast<PlaneReferenceSource>(sourceMode);
    app->partialSelectedObject = -1;
  }

  if (app->planeCreate.source == PlaneReferenceSource::Plane) {
    if (app->browserSelectedPlanes.size() == 1) {
      const int planeIndex = app->browserSelectedPlanes.front();
      if (planeIndex >= 0 && planeIndex < static_cast<int>(app->planes.size())) {
        app->planeCreate.sourcePlaneId = app->planes[planeIndex].id;
      }
    }

    const int planeIndex = findPlaneIndexById(app, app->planeCreate.sourcePlaneId);
    if (planeIndex >= 0) {
      ImGui::Text("Selected plane: %s", app->planes[planeIndex].meta.name.data());
    } else {
      ImGui::TextDisabled("Selected plane: none");
    }
    ImGui::TextDisabled("Select a plane by clicking a gizmo plane square or in Object Browser > Planes.");
  } else {
    if (app->planeCreate.sourceObject >= 0) {
      const std::string faceLabel = std::string(faceSignLabel(app->planeCreate.sourceFaceSign)) +
                                    planeShortName(app->planeCreate.sourceFacePlane) +
                                    " face of " +
                                    objectName(app, app->planeCreate.sourceObject);
      ImGui::Text("Selected face: %s", faceLabel.c_str());
    } else {
      ImGui::TextDisabled("Selected face: none");
    }
    ImGui::TextDisabled("Click a face in 3D view to set the source face.");
  }

  ImGui::Text("Offset Distance:");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(140.0f);
  ImGui::InputText("##planeDistance", app->planeCreate.distanceBuffer,
                   sizeof(app->planeCreate.distanceBuffer));
  ImGui::SameLine();
  ImGui::TextDisabled("(%s)", unitSuffix(app->project.defaultUnit));

  ImGui::Spacing();
  if (ImGui::Button("Create Plane", ImVec2(160.0f, 0.0f))) {
    auto parsed = parseDimension(std::string(app->planeCreate.distanceBuffer),
                                 app->project.defaultUnit);
    if (!parsed) {
      app->status = "Plane: enter a valid distance";
    } else if (app->planeCreate.source == PlaneReferenceSource::Plane) {
      createOffsetPlaneFromPlane(app, app->planeCreate.sourcePlaneId, parsed->valueMm);
      app->planeCreate.open = false;
      app->status = "Plane created";
    } else if (app->planeCreate.sourceObject < 0) {
      app->status = "Plane: pick a source face first";
    } else {
      createOffsetPlaneFromFace(app, app->planeCreate.sourceObject,
                                app->planeCreate.sourceFacePlane,
                                app->planeCreate.sourceFaceSign,
                                parsed->valueMm);
      app->planeCreate.open = false;
      app->partialSelectedObject = -1;
      app->status = "Plane created";
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(160.0f, 0.0f))) {
    app->planeCreate = {};
    app->partialSelectedObject = -1;
    open = false;
  }

  ImGui::End();
  app->planeCreate.open = open;
  if (!app->planeCreate.open) {
    app->partialSelectedObject = -1;
  }
}

void drawNewSketchWindow(AppState* app) {
  if (!app->sketchCreate.open) return;

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
  bool open = app->sketchCreate.open;
  if (!ImGui::Begin("New Sketch", &open)) {
    ImGui::End();
    app->sketchCreate.open = open;
    return;
  }

  ImGui::TextWrapped("Select an existing plane for the new sketch.");
  ImGui::Separator();

  if (app->browserSelectedPlanes.size() == 1) {
    const int planeIndex = app->browserSelectedPlanes.front();
    if (planeIndex >= 0 && planeIndex < static_cast<int>(app->planes.size())) {
      app->sketchCreate.selectedPlaneId = app->planes[planeIndex].id;
    }
  }

  std::vector<const char*> names;
  std::vector<int> ids;
  names.reserve(app->planes.size());
  ids.reserve(app->planes.size());
  int current = 0;
  for (int i = 0; i < static_cast<int>(app->planes.size()); ++i) {
    names.push_back(app->planes[i].meta.name.data());
    ids.push_back(app->planes[i].id);
    if (app->planes[i].id == app->sketchCreate.selectedPlaneId) {
      current = i;
    }
  }
  if (!names.empty() &&
      ImGui::Combo("Plane", &current, names.data(), static_cast<int>(names.size()))) {
    app->sketchCreate.selectedPlaneId = ids[current];
  }
  ImGui::TextDisabled("You can also pick a plane by clicking a gizmo plane square or in Object Browser > Planes.");

  ImGui::Spacing();
  if (ImGui::Button("Create Sketch", ImVec2(150.0f, 0.0f))) {
    createSketch(app, app->sketchCreate.selectedPlaneId);
    open = false;
    app->partialSelectedObject = -1;
    app->status = "Sketch created";
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(150.0f, 0.0f))) {
    app->sketchCreate = {};
    app->partialSelectedObject = -1;
    open = false;
  }

  ImGui::End();
  app->sketchCreate.open = open;
  if (!app->sketchCreate.open) {
    app->partialSelectedObject = -1;
  }
}

void drawProjectToolWindow(AppState* app) {
  if (!app->showProjectTool || app->sceneMode != SceneMode::Sketch || !app->hasActiveSketch()) {
    return;
  }

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
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
  const ImVec2 vpPos = uiViewportPos();
  const ImVec2 vpSize = uiViewportSize();
  const float top = vpPos.y + uiTopBarHeight();
  ImGui::SetNextWindowPos(ImVec2(vpPos.x + 12.0f, top), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(280.0f, 300.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(240.0f, 220.0f),
                                      ImVec2(420.0f, vpSize.y * 0.7f));
  ImGui::Begin("Actions");
  snapCurrentWindowToGuides(uiTopBarHeight());

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
  if (ImGui::Button("Reset Window Layout", ImVec2(-1.0f, 0.0f))) {
    app->status = "Docked layout is active";
  }

  ImGui::Separator();
  ImGui::TextWrapped("Status: %s", app->status.c_str());
  ImGui::End();
}

void drawAppSettingsWindow(AppState* app) {
  if (!app->showAppSettings) return;

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
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

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
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
  const ImVec2 vpPos = uiViewportPos();
  const ImVec2 vpSize = uiViewportSize();
  const float top = vpPos.y + uiTopBarHeight() + 312.0f;
  const float usableHeight = std::max(240.0f, vpSize.y - (top - vpPos.y) - 12.0f);
  ImGui::SetNextWindowPos(ImVec2(vpPos.x + 12.0f, top), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(360.0f, usableHeight), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(300.0f, 220.0f),
                                      ImVec2(vpSize.x * 0.5f, vpSize.y - 40.0f));
  ImGui::Begin("Object Browser");
  snapCurrentWindowToGuides(uiTopBarHeight());

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
      ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthFixed, 28.0f);
      ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 28.0f);
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
        if (visibilityIconToggle("##objvis", &meta.visible)) {
          rebuildCombinedMesh(app);
        }

        ImGui::TableSetColumnIndex(1);
        lockIconToggle("##objlock", &meta.locked);

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
          if (ImGui::BeginPopupContextItem("##objctx")) {
            app->browserSelectedObjects.assign(1, i);
            app->objectSelectionAnchor = i;
            syncSelectedObjectFromBrowser(app);
            app->browserFocusSection = BrowserSection::Objects;

            if (ImGui::MenuItem("Rename")) {
              beginObjectRename(app, i);
            }
            if (ImGui::MenuItem(meta.visible ? "Hide" : "Show")) {
              meta.visible = !meta.visible;
              rebuildCombinedMesh(app);
            }
            if (ImGui::MenuItem(meta.locked ? "Unlock" : "Lock")) {
              meta.locked = !meta.locked;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", nullptr, false, !meta.locked)) {
              app->pendingDeleteObjects.assign(1, i);
              app->openDeleteObjectsPopup = true;
            }
            ImGui::EndPopup();
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

  if (ImGui::CollapsingHeader("Planes", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::BeginTable("##planesTable", 3,
                          ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthFixed, 28.0f);
      ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 28.0f);
      ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();

      for (int i = 0; i < static_cast<int>(app->planes.size()); ++i) {
        auto& meta = app->planes[i].meta;
        const bool selected = hasIndex(app->browserSelectedPlanes, i);

        ImGui::TableNextRow();
        const ImU32 rowTint = tintRow(meta.visible, meta.locked);
        if (rowTint != 0) {
          ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, rowTint);
        }
        ImGui::PushID(2000 + i);

        ImGui::TableSetColumnIndex(0);
        visibilityIconToggle("##plvis", &meta.visible);

        ImGui::TableSetColumnIndex(1);
        lockIconToggle("##pllock", &meta.locked, !meta.builtIn);

        ImGui::TableSetColumnIndex(2);
        std::string label = meta.name.data();
        const char* state = browserRowLabel(meta.visible, meta.locked);
        if (state[0] != '\0') {
          label += " ";
          label += state;
        }
        if (ImGui::Selectable(label.c_str(), selected,
                              ImGuiSelectableFlags_SpanAllColumns)) {
          const bool shiftClick = ImGui::GetIO().KeyShift;
          if (shiftClick && app->planeSelectionAnchor >= 0) {
            setSelectionRange(app->browserSelectedPlanes, app->planeSelectionAnchor, i);
          } else {
            setSingleOrMultiSelection(app->browserSelectedPlanes, i, multiSelect);
            app->planeSelectionAnchor = i;
          }
          syncSelectedPlaneFromBrowser(app);
          app->browserFocusSection = BrowserSection::Planes;
        }
        if (ImGui::BeginPopupContextItem("##plctx")) {
          app->browserSelectedPlanes.assign(1, i);
          app->planeSelectionAnchor = i;
          syncSelectedPlaneFromBrowser(app);
          app->browserFocusSection = BrowserSection::Planes;

          if (ImGui::MenuItem(meta.visible ? "Hide" : "Show")) {
            meta.visible = !meta.visible;
          }
          if (ImGui::MenuItem(meta.locked ? "Unlock" : "Lock", nullptr, false,
                              !meta.builtIn)) {
            meta.locked = !meta.locked;
          }
          ImGui::Separator();
          if (ImGui::MenuItem("New Sketch on Plane")) {
            app->sketchCreate.open = true;
            app->sketchCreate.selectedPlaneId = app->planes[i].id;
            app->status = "Create sketch on selected plane";
          }
          if (ImGui::MenuItem("Offset Plane from This")) {
            app->planeCreate.open = true;
            app->planeCreate.source = PlaneReferenceSource::Plane;
            app->planeCreate.sourcePlaneId = app->planes[i].id;
            app->status = "Create offset plane from selected plane";
          }
          ImGui::EndPopup();
        }

        ImGui::PopID();
      }

      ImGui::EndTable();
    }
    if (app->planes.empty()) {
      ImGui::TextDisabled("(no planes)");
    }
  }

  if (ImGui::CollapsingHeader("Sketches", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::BeginTable("##sketchesTable", 3,
                          ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthFixed, 28.0f);
      ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 28.0f);
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
        const bool changedVisibility = visibilityIconToggle("##skvis", &meta.visible);

        ImGui::TableSetColumnIndex(1);
        const bool changedLock = lockIconToggle("##sklock", &meta.locked);

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
            const int createEntryIndex = findCreateEntryIndexForSketch(app->timeline, i);
            if (createEntryIndex >= 0) {
              app->timelineCursor = createEntryIndex;
            }
            app->sceneMode = SceneMode::Sketch;
            if (app->hasActiveSketch()) {
              snapCameraToPlane(app, app->activePlane());
            }
            app->status = "Jumped to sketch creation point in timeline";
            app->browserFocusSection = BrowserSection::Sketches;
          }
          if (ImGui::BeginPopupContextItem("##skctx")) {
            app->browserSelectedSketches.assign(1, i);
            app->sketchSelectionAnchor = i;
            syncSelectedSketchFromBrowser(app);
            app->browserFocusSection = BrowserSection::Sketches;

            if (ImGui::MenuItem("Rename")) {
              beginSketchRename(app, i);
            }
            if (ImGui::MenuItem(meta.visible ? "Hide" : "Show")) {
              meta.visible = !meta.visible;
            }
            if (ImGui::MenuItem(meta.locked ? "Unlock" : "Lock")) {
              meta.locked = !meta.locked;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Edit Sketch")) {
              app->browserSelectedSketches.assign(1, i);
              app->sketchSelectionAnchor = i;
              syncSelectedSketchFromBrowser(app);
              const int createEntryIndex = findCreateEntryIndexForSketch(app->timeline, i);
              if (createEntryIndex >= 0) {
                app->timelineCursor = createEntryIndex;
              }
              app->sceneMode = SceneMode::Sketch;
              if (app->hasActiveSketch()) {
                snapCameraToPlane(app, app->activePlane());
              }
            }
            ImGui::EndPopup();
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
      } else if (app->browserFocusSection == BrowserSection::Planes) {
        const int count = static_cast<int>(app->planes.size());
        if (ImGui::GetIO().KeyShift && count > 0) {
          int anchor = app->planeSelectionAnchor >= 0 ? app->planeSelectionAnchor :
                       (app->browserSelectedPlanes.empty() ? 0 : app->browserSelectedPlanes.front());
          int current = app->browserSelectedPlanes.empty() ? anchor : app->browserSelectedPlanes.front();
          current = std::clamp(current - 1, 0, count - 1);
          app->planeSelectionAnchor = anchor;
          setSelectionRange(app->browserSelectedPlanes, anchor, current);
        } else {
          stepSelection(app->browserSelectedPlanes, count, -1);
          if (!app->browserSelectedPlanes.empty()) {
            app->planeSelectionAnchor = app->browserSelectedPlanes.front();
          }
        }
        syncSelectedPlaneFromBrowser(app);
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
      } else if (app->browserFocusSection == BrowserSection::Planes) {
        const int count = static_cast<int>(app->planes.size());
        if (ImGui::GetIO().KeyShift && count > 0) {
          int anchor = app->planeSelectionAnchor >= 0 ? app->planeSelectionAnchor :
                       (app->browserSelectedPlanes.empty() ? 0 : app->browserSelectedPlanes.front());
          int current = app->browserSelectedPlanes.empty() ? anchor : app->browserSelectedPlanes.front();
          current = std::clamp(current + 1, 0, count - 1);
          app->planeSelectionAnchor = anchor;
          setSelectionRange(app->browserSelectedPlanes, anchor, current);
        } else {
          stepSelection(app->browserSelectedPlanes, count, 1);
          if (!app->browserSelectedPlanes.empty()) {
            app->planeSelectionAnchor = app->browserSelectedPlanes.front();
          }
        }
        syncSelectedPlaneFromBrowser(app);
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

void drawTimelineWindow(AppState* app) {
  const ImVec2 vpPos = uiViewportPos();
  const ImVec2 vpSize = uiViewportSize();
  const float top = vpPos.y + uiTopBarHeight();
  const float width = 460.0f;
  ImGui::SetNextWindowPos(ImVec2(vpPos.x + vpSize.x - width - 12.0f, top),
                          ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(width, std::max(240.0f, vpSize.y * 0.42f)),
                           ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f, 200.0f),
                                      ImVec2(vpSize.x - 80.0f, vpSize.y - 40.0f));
  if (!ImGui::Begin("History Timeline")) {
    ImGui::End();
    return;
  }
  snapCurrentWindowToGuides(uiTopBarHeight());

  const auto& entries = app->timeline.entries();
  if (entries.empty()) {
    ImGui::TextDisabled("(timeline is empty)");
    app->timelineCursor = -1;
    ImGui::End();
    return;
  }

  if (app->timelineCursor < 0 || app->timelineCursor >= static_cast<int>(entries.size())) {
    app->timelineCursor = static_cast<int>(entries.size()) - 1;
  }

  ImGui::Text("Current timeline position: %d / %d", app->timelineCursor + 1,
              static_cast<int>(entries.size()));
  ImGui::Separator();
  ImGui::BeginChild("##timelineEntries", ImVec2(0.0f, 0.0f), true);

  for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
    const auto& e = entries[i];
    const bool isCurrent = i == app->timelineCursor;
    std::string row = (isCurrent ? "-> " : "   ") + std::to_string(i + 1) + ". " + e.displayName;
    if (e.hasConflict) {
      row += " [conflict]";
    }
    if (ImGui::Selectable(row.c_str(), isCurrent)) {
      app->timelineCursor = i;
      app->status = "Timeline position set to step " + std::to_string(i + 1);
    }
  }

  ImGui::EndChild();
  ImGui::End();
}

void drawSolidExtrudeWindow(AppState* app) {
  if (!app->solidExtrudeOptions.visible) return;
  if (app->sceneMode != SceneMode::View3D) return;

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_FirstUseEver);
  bool open = app->solidExtrudeOptions.visible;
  if (ImGui::Begin("3D Extrude", &open)) {
    if (app->sketches.empty()) {
      ImGui::TextDisabled("No sketches available.");
    } else {
      std::vector<const char*> names;
      names.reserve(app->sketches.size());
      for (const auto& sk : app->sketches) names.push_back(sk.meta.name.data());

      int sketchIdx = app->solidExtrudeOptions.sourceSketch;
      if (sketchIdx < 0 || sketchIdx >= static_cast<int>(app->sketches.size())) {
        sketchIdx = static_cast<int>(app->sketches.size()) - 1;
      }
      if (ImGui::Combo("Source Sketch", &sketchIdx, names.data(), static_cast<int>(names.size()))) {
        app->solidExtrudeOptions.profileSelected.clear();
      }
      app->solidExtrudeOptions.sourceSketch = sketchIdx;

      const auto& entry = app->sketches[sketchIdx];
      const auto profiles = entry.sketch.closedProfiles();
      if (app->solidExtrudeOptions.profileSelected.size() != profiles.size()) {
        app->solidExtrudeOptions.profileSelected.assign(profiles.size(), true);
      }

      ImGui::Text("Closed Profiles: %d", static_cast<int>(profiles.size()));
      ImGui::TextDisabled("Tip: uncheck inner loops to keep holes.");
      ImGui::BeginChild("##extrudeProfiles", ImVec2(0.0f, 170.0f), true);
      for (size_t i = 0; i < profiles.size(); ++i) {
        std::string label = "Profile " + std::to_string(i + 1);
        bool checked = app->solidExtrudeOptions.profileSelected[i];
        if (ImGui::Checkbox(label.c_str(), &checked)) {
          app->solidExtrudeOptions.profileSelected[i] = checked;
        }
      }
      ImGui::EndChild();

      ImGui::Text("Depth:");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(140.0f);
      ImGui::InputText("##solidExtrudeDepth", app->solidExtrudeOptions.depthBuffer,
                       sizeof(app->solidExtrudeOptions.depthBuffer));
      ImGui::SameLine();
      ImGui::TextDisabled("(%s)", unitSuffix(app->project.defaultUnit));

      if (ImGui::Button("Apply Extrude")) {
        auto parsed = parseDimension(std::string(app->solidExtrudeOptions.depthBuffer),
                                     app->project.defaultUnit);
        if (!parsed) {
          app->status = "Extrude: enter a valid depth";
        } else {
          std::vector<std::vector<glm::vec2>> selected;
          std::vector<std::vector<glm::vec2>> holes;
          selected.reserve(profiles.size());
          holes.reserve(profiles.size());

          for (size_t i = 0; i < profiles.size(); ++i) {
            if (app->solidExtrudeOptions.profileSelected[i]) {
              selected.push_back(profiles[i]);
            }
          }
          for (size_t i = 0; i < profiles.size(); ++i) {
            if (app->solidExtrudeOptions.profileSelected[i]) continue;
            const glm::vec2 c = centroid2D(profiles[i]);
            bool inSelected = false;
            for (const auto& s : selected) {
              if (pointInPolygon2D(c, s)) {
                inSelected = true;
                break;
              }
            }
            if (inSelected) holes.push_back(profiles[i]);
          }

          if (selected.empty()) {
            app->status = "Extrude: select at least one closed profile";
          } else {
            StlMesh extruded = extrudeMesh(selected, entry.plane, parsed->valueMm, holes);
            if (extruded.empty() || !applyAddExtrude(app, extruded, {})) {
              app->status = "Extrude failed";
            } else {
              ExtrudeAction ea;
              ea.sketchIndex = sketchIdx;
              ea.sketchName = std::string(entry.meta.name.data());
              ea.profiles = selected;
              ea.plane = entry.plane;
              ea.sketchOffsetMm = entry.offsetMm;
              ea.depthMm = parsed->valueMm;
              ea.subtract = false;
              ea.resultObjectName = objectName(app, static_cast<int>(app->sceneObjects.size()) - 1);
              app->timeline.push(std::move(ea), "3D Extrude");
              app->timelineCursor = app->timeline.size() - 1;
              app->status = holes.empty() ? "Extrude complete" : "Extrude complete (with holes)";
              app->solidExtrudeOptions.visible = false;
            }
          }
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Close")) {
        app->solidExtrudeOptions.visible = false;
      }
    }
  }
  ImGui::End();
  app->solidExtrudeOptions.visible = open;
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
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
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
            app->timelineCursor = app->timeline.size() - 1;
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

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
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
        app->timelineCursor = app->timeline.size() - 1;
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

void drawChamferWindow(AppState* app) {
  if (!app->chamferOptions.visible) return;

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_FirstUseEver);
  bool open = app->chamferOptions.visible;
  if (ImGui::Begin("Chamfer Tool", &open)) {
    const int obj = app->chamferOptions.targetObject;
    if (obj < 0 || obj >= static_cast<int>(app->sceneObjects.size())) {
      ImGui::TextDisabled("No valid target object selected.");
    } else {
      ImGui::Text("Target: %s", app->sceneObjectMeta[obj].name.data());
      ImGui::Text("Selected edges: %d", static_cast<int>(app->chamferOptions.edges.size()));
    }

    ImGui::Text("Chamfer Distance:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputText("##chamferDistance", app->chamferOptions.distanceBuffer,
                     sizeof(app->chamferOptions.distanceBuffer));
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", unitSuffix(app->project.defaultUnit));

    if (ImGui::Button(app->chamferOptions.pickEdges ? "Stop Edge Pick" : "Pick Edges")) {
      app->chamferOptions.pickEdges = !app->chamferOptions.pickEdges;
      app->objectPickMode = app->chamferOptions.pickEdges ? ObjectPickMode::ChamferEdges
                                                          : ObjectPickMode::None;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Edges")) {
      app->chamferOptions.edges.clear();
    }

    ImGui::Separator();
    ImGui::BeginChild("##chamferEdges", ImVec2(0.0f, 140.0f), true);
    int eraseIdx = -1;
    for (int i = 0; i < static_cast<int>(app->chamferOptions.edges.size()); ++i) {
      const auto& e = app->chamferOptions.edges[i];
      ImGui::PushID(i);
      if (ImGui::SmallButton("x")) eraseIdx = i;
      ImGui::SameLine();
      ImGui::Text("Edge %d  (%.2f, %.2f, %.2f) -> (%.2f, %.2f, %.2f)", i + 1,
                  e.a.x, e.a.y, e.a.z, e.b.x, e.b.y, e.b.z);
      ImGui::PopID();
    }
    if (eraseIdx >= 0) {
      app->chamferOptions.edges.erase(app->chamferOptions.edges.begin() + eraseIdx);
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::Button("Apply Chamfer")) {
      auto parsed = parseDimension(std::string(app->chamferOptions.distanceBuffer),
                                   app->project.defaultUnit);
      if (!parsed) {
        app->status = "Chamfer: enter a valid distance";
      } else if (!applyChamferEdges(app, app->chamferOptions.targetObject,
                                    app->chamferOptions.edges, parsed->valueMm)) {
        app->status = "Chamfer failed (select edges on an unlocked object)";
      } else {
        app->status = "Chamfer applied";
        app->chamferOptions.visible = false;
        app->chamferOptions.pickEdges = false;
        app->objectPickMode = ObjectPickMode::None;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
      app->chamferOptions.visible = false;
      app->chamferOptions.pickEdges = false;
      if (app->objectPickMode == ObjectPickMode::ChamferEdges) {
        app->objectPickMode = ObjectPickMode::None;
      }
    }
  }
  ImGui::End();

  app->chamferOptions.visible = open;
  if (!app->chamferOptions.visible && app->objectPickMode == ObjectPickMode::ChamferEdges) {
    app->objectPickMode = ObjectPickMode::None;
    app->chamferOptions.pickEdges = false;
  }
}
