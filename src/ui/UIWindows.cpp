#include "ui/UIWindows.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <imgui.h>

#include "Units.hpp"
#include "core/AppLogic.hpp"
#include "dfm/Dfm.hpp"
#include "drawing/Drawing.hpp"
#include "interop/Step.hpp"
#include "sketch/Extrude.hpp"
#include "sketch/Profile.hpp"

namespace {
template <size_t N>
void setName(std::array<char, N>& dest, const std::string& value) {
  std::snprintf(dest.data(), dest.size(), "%s", value.c_str());
}

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

void syncCamBuilderSources(AppState* app) {
  if (app->camBuilder.sourceObject < 0 ||
      app->camBuilder.sourceObject >= static_cast<int>(app->sceneObjects.size())) {
    app->camBuilder.sourceObject = app->selectedObject;
  }
  if (app->camBuilder.sourceSketch < 0 ||
      app->camBuilder.sourceSketch >= static_cast<int>(app->sketches.size())) {
    app->camBuilder.sourceSketch = app->browserSelectedSketches.empty()
                                       ? -1
                                       : app->browserSelectedSketches.front();
  }
  if (app->camBuilder.toolIndex < 0 ||
      app->camBuilder.toolIndex >= static_cast<int>(app->camTools.size())) {
    app->camBuilder.toolIndex = app->camTools.empty() ? -1 : 0;
  }
}

void setCamTopFromStock(AppState* app) {
  app->camBuilder.topZMm = app->camStock.originMm.z + app->camStock.sizeMm.z;
}

void setCamTopFromSource(AppState* app) {
  if (app->camBuilder.sourceSketch >= 0 &&
      app->camBuilder.sourceSketch < static_cast<int>(app->sketches.size())) {
    app->camBuilder.topZMm = app->sketches[app->camBuilder.sourceSketch].offsetMm;
    return;
  }
  if (app->camBuilder.sourceObject >= 0 &&
      app->camBuilder.sourceObject < static_cast<int>(app->sceneObjects.size())) {
    const auto& mesh = app->sceneObjects[app->camBuilder.sourceObject];
    const auto& verts = mesh.vertices();
    if (!verts.empty()) {
      float maxZ = verts.front().position.z;
      for (const auto& vertex : verts) maxZ = std::max(maxZ, vertex.position.z);
      app->camBuilder.topZMm = maxZ;
    }
  }
}

void duplicateCamTool(AppState* app, int toolIndex) {
  if (toolIndex < 0 || toolIndex >= static_cast<int>(app->camTools.size())) return;
  CamToolPreset tool = app->camTools[toolIndex];
  std::string name = tool.name.data();
  setName(tool.name, name + " Copy");
  app->camTools.push_back(tool);
  app->camBuilder.toolIndex = static_cast<int>(app->camTools.size()) - 1;
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
      app->timeline.clear();
      app->timelineCursor = -1;
      app->extrudeTool.cancel();
      app->sketchCreate = {};
      app->planeCreate = {};
      app->partialSelectedObject = -1;
      app->project.initFromAppSettings(app->appSettings);
      resetCamSession(app->camBuilder, app->camOperations, -1, -1, app->camStock);
      app->camSelectedOperation = -1;
      app->drawingSheet = {};
      app->dfmReport = {};
      app->dfmHasReport = false;
      app->status = "New scene";
    }
    if (ImGui::MenuItem("Open STL...")) {
      if (!app->loadingMesh && !app->fileBrowser.isVisible()) {
        app->importIntent = ImportIntent::Stl;
        app->fileBrowser.show({{"STL Files", {".stl"}}}, {}, "Open File", "Open");
      }
    }
    if (ImGui::MenuItem("Open STEP...")) {
      if (!app->loadingMesh && !app->fileBrowser.isVisible()) {
        app->importIntent = ImportIntent::Step;
        app->fileBrowser.show({{"STEP Files", {".step", ".stp"}}}, {}, "Open STEP", "Open");
      }
    }
    if (ImGui::MenuItem("Export STL...", nullptr, false, app->selectedObject >= 0)) {
      if (!app->exportBrowser.isVisible()) {
        app->exportIntent = ExportIntent::Stl;
        app->exportBrowser.show({{"STL Files", {".stl"}}}, {}, "Export STL", "Export");
      }
    }
    if (ImGui::MenuItem("Export STEP...", nullptr, false, app->selectedObject >= 0)) {
      if (!app->exportBrowser.isVisible()) {
        app->exportIntent = ExportIntent::Step;
        app->exportBrowser.show({{"STEP Files", {".step", ".stp"}}}, {}, "Export STEP", "Export");
      }
    }
    if (ImGui::MenuItem("Export Drawing PDF...", nullptr, false,
                        !app->drawingSheet.views.empty())) {
      if (!app->exportBrowser.isVisible()) {
        app->exportIntent = ExportIntent::Pdf;
        app->exportBrowser.show({{"PDF Files", {".pdf"}}}, {}, "Export PDF", "Export");
      }
    }
    if (ImGui::MenuItem("Export Drawing DXF...", nullptr, false,
                        !app->drawingSheet.views.empty())) {
      if (!app->exportBrowser.isVisible()) {
        app->exportIntent = ExportIntent::Dxf;
        app->exportBrowser.show({{"DXF Files", {".dxf"}}}, {}, "Export DXF", "Export");
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
    const bool canDraft = app->sceneMode == SceneMode::View3D && app->selectedObject >= 0;
    if (ImGui::MenuItem("Draft...", nullptr, false, canDraft)) {
      app->draftOptions.visible = true;
      app->draftOptions.targetObject = app->selectedObject;
      std::snprintf(app->draftOptions.angleBuffer,
                    sizeof(app->draftOptions.angleBuffer), "2.000");
      app->status = "Draft tool opened";
    }
    const bool canReference = app->sceneMode == SceneMode::View3D;
    if (ImGui::MenuItem("Create Reference Point", nullptr, false, canReference)) {
      createReferencePointFromSelection(app);
      app->status = "Reference point created";
    }
    if (ImGui::MenuItem("Create Reference Axis", nullptr, false, canReference)) {
      createReferenceAxisFromSelection(app);
      app->status = "Reference axis created";
    }
    if (ImGui::MenuItem("Drawings...", nullptr, false,
                        app->sceneMode == SceneMode::View3D && !app->sceneObjects.empty())) {
      app->showDrawingWindow = true;
      app->drawingSourceObject = app->selectedObject >= 0 ? app->selectedObject : 0;
      app->status = "Drawing workspace opened";
    }
    if (ImGui::MenuItem("DFM Checks...", nullptr, false,
                        app->sceneMode == SceneMode::View3D && !app->sceneObjects.empty())) {
      app->showDfmWindow = true;
      app->dfmSourceObject = app->selectedObject >= 0 ? app->selectedObject : 0;
      app->status = "DFM checks opened";
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("CAM")) {
    if (ImGui::MenuItem("Manufacture...", nullptr, false, app->sceneMode == SceneMode::View3D)) {
      app->showCamWindow = true;
      syncCamBuilderSources(app);
    }
    if (ImGui::MenuItem("Export Toolpath...", nullptr, false, !app->camOperations.empty())) {
      if (!app->exportBrowser.isVisible()) {
        app->exportIntent = ExportIntent::Gcode;
        app->exportBrowser.show({{"NC Files", {".nc", ".gcode"}}}, {},
                                "Export Toolpath", "Export");
      }
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
  if (ImGui::Button("Axis")) {
    createReferenceAxisFromSelection(app);
    app->status = "Reference axis created";
  }

  ImGui::SameLine();
  if (ImGui::Button("Point")) {
    createReferencePointFromSelection(app);
    app->status = "Reference point created";
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
  if (!hasSketch) ImGui::BeginDisabled();
  if (ImGui::Button("Revolve")) {
    app->revolveOptions.visible = true;
    app->revolveOptions.sourceSketch = app->browserSelectedSketches.empty()
                                           ? (static_cast<int>(app->sketches.size()) - 1)
                                           : app->browserSelectedSketches.front();
    app->revolveOptions.axisMode = 0;
    std::snprintf(app->revolveOptions.angleBuffer,
                  sizeof(app->revolveOptions.angleBuffer), "360.000");
    app->status = "Revolve tool opened";
  }
  if (!hasSketch) ImGui::EndDisabled();

  ImGui::SameLine();
  if (!hasSketch) ImGui::BeginDisabled();
  if (ImGui::Button("Sweep")) {
    app->sweepOptions.visible = true;
    app->sweepOptions.sourceSketch = app->browserSelectedSketches.empty()
                                         ? (static_cast<int>(app->sketches.size()) - 1)
                                         : app->browserSelectedSketches.front();
    app->sweepOptions.axisMode = 2;
    std::snprintf(app->sweepOptions.distanceBuffer,
                  sizeof(app->sweepOptions.distanceBuffer), "10.000");
    app->status = "Sweep tool opened";
  }
  if (!hasSketch) ImGui::EndDisabled();

  ImGui::SameLine();
  if (app->sketches.size() < 2) ImGui::BeginDisabled();
  if (ImGui::Button("Loft")) {
    app->loftOptions.visible = true;
    app->loftOptions.sourceSketchA = app->browserSelectedSketches.empty() ? 0 : app->browserSelectedSketches.front();
    app->loftOptions.sourceSketchB = std::min(app->loftOptions.sourceSketchA + 1,
                                              static_cast<int>(app->sketches.size()) - 1);
    app->status = "Loft tool opened";
  }
  if (app->sketches.size() < 2) ImGui::EndDisabled();

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
  if (!hasSelectedObject) ImGui::BeginDisabled();
  if (ImGui::Button("Fillet")) {
    app->filletOptions.visible = true;
    app->filletOptions.targetObject = app->selectedObject;
    app->filletOptions.edges.clear();
    std::snprintf(app->filletOptions.radiusBuffer,
                  sizeof(app->filletOptions.radiusBuffer), "1.000");
    app->objectPickMode = ObjectPickMode::FilletEdges;
    app->filletOptions.pickEdges = true;
    app->status = "Fillet: click edges on the selected object";
  }
  if (!hasSelectedObject) ImGui::EndDisabled();

  ImGui::SameLine();
  if (!hasSelectedObject) ImGui::BeginDisabled();
  if (ImGui::Button("Draft")) {
    app->draftOptions.visible = true;
    app->draftOptions.targetObject = app->selectedObject;
    std::snprintf(app->draftOptions.angleBuffer,
                  sizeof(app->draftOptions.angleBuffer), "2.000");
    app->status = "Draft: positive tapers inward along +Z";
  }
  if (!hasSelectedObject) ImGui::EndDisabled();

  ImGui::SameLine();
  if (!hasSelectedObject) ImGui::BeginDisabled();
  if (ImGui::Button("Shell")) {
    app->shellOptions.visible = true;
    app->shellOptions.targetObject = app->selectedObject;
    std::snprintf(app->shellOptions.thicknessBuffer,
                  sizeof(app->shellOptions.thicknessBuffer), "1.000");
    app->status = "Shell tool opened";
  }
  if (!hasSelectedObject) ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::Button("CAM")) {
    app->showCamWindow = true;
    syncCamBuilderSources(app);
    app->status = "CAM workspace opened";
  }

  ImGui::SameLine();
  if (ImGui::Button("Drawings") && !app->sceneObjects.empty()) {
    app->showDrawingWindow = true;
    app->drawingSourceObject = app->selectedObject >= 0 ? app->selectedObject : 0;
    app->status = "Drawing workspace opened";
  }

  ImGui::SameLine();
  if (ImGui::Button("DFM") && !app->sceneObjects.empty()) {
    app->showDfmWindow = true;
    app->dfmSourceObject = app->selectedObject >= 0 ? app->selectedObject : 0;
    app->status = "DFM checks opened";
  }

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

  if (app->sceneMode == SceneMode::Sketch && app->hasActiveSketch()) {
    const auto diag = app->activeSketch().constraintDiagnostics();
    ImGui::Separator();
    ImGui::TextWrapped("Sketch Diagnostics");
    ImGui::TextWrapped("Elements: U:%d  u:%d  F:%d  O:%d",
                       diag.unconstrainedCount,
                       diag.underConstrainedCount,
                       diag.fullyConstrainedCount,
                       diag.overConstrainedCount);
    ImGui::TextWrapped("Constraints: %d (duplicates: %d)",
                       diag.totalConstraints,
                       diag.duplicateConstraintCount);
    for (size_t i = 0; i < diag.issues.size() && i < 3; ++i) {
      ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.2f, 1.0f), "- %s", diag.issues[i].c_str());
    }
  }

  ImGui::Separator();

  if (!app->lastFeatureFailure.code.empty()) {
    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.25f, 1.0f),
                       "Last Feature Failure [%s]", app->lastFeatureFailure.code.c_str());
    ImGui::TextWrapped("%s", app->lastFeatureFailure.message.c_str());
  }

  ImGui::TextDisabled("Perf (ms): combine %.2f | chamfer %.2f | fillet %.2f | shell %.2f",
                      app->opPerf.combineMs, app->opPerf.chamferMs,
                      app->opPerf.filletMs, app->opPerf.shellMs);
  if (app->opPerf.combineMs > 50.0f || app->opPerf.chamferMs > 40.0f ||
      app->opPerf.filletMs > 60.0f || app->opPerf.shellMs > 80.0f) {
    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f),
                       "Perf regression threshold exceeded");
  }

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
          setObjectVisibility(app, i, meta.visible);
        }

        ImGui::TableSetColumnIndex(1);
        if (lockIconToggle("##objlock", &meta.locked)) {
          setObjectLocked(app, i, meta.locked);
        }

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
              setObjectVisibility(app, i, !meta.visible);
            }
            if (ImGui::MenuItem(meta.locked ? "Unlock" : "Lock")) {
              setObjectLocked(app, i, !meta.locked);
            }
            if (ImGui::MenuItem("Randomize Pastel Color")) {
              randomizeObjectColor(app, i);
              app->status = "Object color randomized";
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
        if (isPlaneReferenceBroken(app, i)) {
          label += " [BROKEN]";
        }
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

  if (ImGui::CollapsingHeader("Axes", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::BeginTable("##axesTable", 2,
                          ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthFixed, 28.0f);
      ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();
      for (int i = 0; i < static_cast<int>(app->referenceAxes.size()); ++i) {
        auto& axis = app->referenceAxes[i];
        const bool selected = hasIndex(app->browserSelectedAxes, i);
        ImGui::TableNextRow();
        ImGui::PushID(3000 + i);
        ImGui::TableSetColumnIndex(0);
        visibilityIconToggle("##axvis", &axis.meta.visible);
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Selectable(axis.meta.name.data(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
          setSingleOrMultiSelection(app->browserSelectedAxes, i, multiSelect);
          app->axisSelectionAnchor = i;
          app->browserFocusSection = BrowserSection::Axes;
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    if (app->referenceAxes.empty()) ImGui::TextDisabled("(no axes)");
  }

  if (ImGui::CollapsingHeader("Points", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::BeginTable("##pointsTable", 2,
                          ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthFixed, 28.0f);
      ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();
      for (int i = 0; i < static_cast<int>(app->referencePoints.size()); ++i) {
        auto& point = app->referencePoints[i];
        const bool selected = hasIndex(app->browserSelectedPoints, i);
        ImGui::TableNextRow();
        ImGui::PushID(4000 + i);
        ImGui::TableSetColumnIndex(0);
        visibilityIconToggle("##ptvis", &point.meta.visible);
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Selectable(point.meta.name.data(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
          setSingleOrMultiSelection(app->browserSelectedPoints, i, multiSelect);
          app->pointSelectionAnchor = i;
          app->browserFocusSection = BrowserSection::Points;
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    if (app->referencePoints.empty()) ImGui::TextDisabled("(no points)");
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

void drawRevolveWindow(AppState* app) {
  if (!app->revolveOptions.visible) return;
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
  bool open = app->revolveOptions.visible;
  if (ImGui::Begin("Revolve Tool", &open)) {
    std::vector<const char*> names;
    for (const auto& sk : app->sketches) names.push_back(sk.meta.name.data());
    int idx = std::clamp(app->revolveOptions.sourceSketch, 0,
                         std::max(0, static_cast<int>(app->sketches.size()) - 1));
    ImGui::Combo("Source Sketch", &idx, names.data(), static_cast<int>(names.size()));
    app->revolveOptions.sourceSketch = idx;
    ImGui::Combo("Axis", &app->revolveOptions.axisMode, "Sketch X\0Sketch Y\0");
    ImGui::Checkbox("Replace Selected Object", &app->revolveOptions.replaceSelectedObject);
    ImGui::InputText("Angle", app->revolveOptions.angleBuffer, sizeof(app->revolveOptions.angleBuffer));
    if (ImGui::Button("Apply Revolve")) {
      char* endPtr = nullptr;
      const float angle = std::strtof(app->revolveOptions.angleBuffer, &endPtr);
      const auto& sk = app->sketches[idx];
      StlMesh mesh = revolveMesh(sk.sketch.closedProfiles(), sk.plane, sk.offsetMm,
                                 app->revolveOptions.axisMode, angle);
      const StlMesh verifyMesh = revolveMesh(sk.sketch.closedProfiles(), sk.plane, sk.offsetMm,
                                             app->revolveOptions.axisMode, angle);
      if (endPtr == app->revolveOptions.angleBuffer || mesh.empty()) {
        app->lastFeatureFailure = {"INVALID_INPUT", "Revolve requires valid angle and closed profiles", -1, idx};
        app->status = "Revolve failed";
      } else if (meshDeterminismHash(mesh) != meshDeterminismHash(verifyMesh)) {
        app->lastFeatureFailure = {"NON_DETERMINISTIC", "Revolve generated non-deterministic mesh output", -1, idx};
        app->status = "Revolve failed: non-deterministic output";
      } else if (app->revolveOptions.replaceSelectedObject && app->selectedObject >= 0) {
        if (!replaceSceneObjectMesh(app, app->selectedObject, mesh, "(Revolve)")) {
          app->status = "Revolve failed to replace selected object";
        } else {
          app->lastFeatureFailure = {};
          app->status = "Revolve updated selected object";
          app->revolveOptions.visible = false;
        }
      } else if (!applyAddExtrude(app, mesh, {})) {
        app->lastFeatureFailure = {"BOOLEAN_FAILED", "Revolve result could not be added to scene", -1, idx};
        app->status = "Revolve failed";
      } else {
        app->lastFeatureFailure = {};
        SolidFeatureAction action;
        action.featureName = "Revolve";
        action.sourceNames = {std::string(sk.meta.name.data())};
        action.resultObjectName = objectName(app, static_cast<int>(app->sceneObjects.size()) - 1);
        app->timeline.push(std::move(action), "Revolve");
        app->timelineCursor = app->timeline.size() - 1;
        app->status = "Revolve complete";
        app->revolveOptions.visible = false;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) app->revolveOptions.visible = false;
  }
  ImGui::End();
  app->revolveOptions.visible = open;
}

void drawSweepWindow(AppState* app) {
  if (!app->sweepOptions.visible) return;
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
  bool open = app->sweepOptions.visible;
  if (ImGui::Begin("Sweep Tool", &open)) {
    std::vector<const char*> names;
    for (const auto& sk : app->sketches) names.push_back(sk.meta.name.data());
    int idx = std::clamp(app->sweepOptions.sourceSketch, 0,
                         std::max(0, static_cast<int>(app->sketches.size()) - 1));
    ImGui::Combo("Source Sketch", &idx, names.data(), static_cast<int>(names.size()));
    app->sweepOptions.sourceSketch = idx;
    ImGui::Combo("Direction", &app->sweepOptions.axisMode, "World X\0World Y\0World Z\0");
    ImGui::Checkbox("Replace Selected Object", &app->sweepOptions.replaceSelectedObject);
    ImGui::InputText("Distance", app->sweepOptions.distanceBuffer, sizeof(app->sweepOptions.distanceBuffer));
    if (ImGui::Button("Apply Sweep")) {
      auto parsed = parseDimension(std::string(app->sweepOptions.distanceBuffer), app->project.defaultUnit);
      const auto& sk = app->sketches[idx];
      StlMesh mesh = parsed ? sweepMesh(sk.sketch.closedProfiles(), sk.plane, sk.offsetMm,
                                        app->sweepOptions.axisMode, parsed->valueMm) : StlMesh();
      const StlMesh verifyMesh = parsed ? sweepMesh(sk.sketch.closedProfiles(), sk.plane, sk.offsetMm,
                                                    app->sweepOptions.axisMode, parsed->valueMm)
                                        : StlMesh();
      if (!parsed || mesh.empty()) {
        app->lastFeatureFailure = {"INVALID_INPUT", "Sweep requires valid distance and closed profiles", -1, idx};
        app->status = "Sweep failed";
      } else if (meshDeterminismHash(mesh) != meshDeterminismHash(verifyMesh)) {
        app->lastFeatureFailure = {"NON_DETERMINISTIC", "Sweep generated non-deterministic mesh output", -1, idx};
        app->status = "Sweep failed: non-deterministic output";
      } else if (app->sweepOptions.replaceSelectedObject && app->selectedObject >= 0) {
        if (!replaceSceneObjectMesh(app, app->selectedObject, mesh, "(Sweep)")) {
          app->status = "Sweep failed to replace selected object";
        } else {
          app->lastFeatureFailure = {};
          app->status = "Sweep updated selected object";
          app->sweepOptions.visible = false;
        }
      } else if (!applyAddExtrude(app, mesh, {})) {
        app->lastFeatureFailure = {"BOOLEAN_FAILED", "Sweep result could not be added to scene", -1, idx};
        app->status = "Sweep failed";
      } else {
        app->lastFeatureFailure = {};
        SolidFeatureAction action;
        action.featureName = "Sweep";
        action.sourceNames = {std::string(sk.meta.name.data())};
        action.resultObjectName = objectName(app, static_cast<int>(app->sceneObjects.size()) - 1);
        app->timeline.push(std::move(action), "Sweep");
        app->timelineCursor = app->timeline.size() - 1;
        app->status = "Sweep complete";
        app->sweepOptions.visible = false;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) app->sweepOptions.visible = false;
  }
  ImGui::End();
  app->sweepOptions.visible = open;
}

void drawLoftWindow(AppState* app) {
  if (!app->loftOptions.visible) return;
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
  bool open = app->loftOptions.visible;
  if (ImGui::Begin("Loft Tool", &open)) {
    std::vector<const char*> names;
    for (const auto& sk : app->sketches) names.push_back(sk.meta.name.data());
    int a = std::clamp(app->loftOptions.sourceSketchA, 0,
                       std::max(0, static_cast<int>(app->sketches.size()) - 1));
    int b = std::clamp(app->loftOptions.sourceSketchB, 0,
                       std::max(0, static_cast<int>(app->sketches.size()) - 1));
    ImGui::Combo("Source Sketch A", &a, names.data(), static_cast<int>(names.size()));
    ImGui::Combo("Source Sketch B", &b, names.data(), static_cast<int>(names.size()));
    ImGui::Checkbox("Replace Selected Object", &app->loftOptions.replaceSelectedObject);
    app->loftOptions.sourceSketchA = a;
    app->loftOptions.sourceSketchB = b;
    if (ImGui::Button("Apply Loft")) {
      const auto& skA = app->sketches[a];
      const auto& skB = app->sketches[b];
      StlMesh mesh = loftMesh(skA.sketch.closedProfiles(), skA.plane, skA.offsetMm,
                              skB.sketch.closedProfiles(), skB.plane, skB.offsetMm);
      const StlMesh verifyMesh = loftMesh(skA.sketch.closedProfiles(), skA.plane, skA.offsetMm,
                                          skB.sketch.closedProfiles(), skB.plane, skB.offsetMm);
      if (a == b || mesh.empty()) {
        app->lastFeatureFailure = {"INVALID_INPUT", "Loft needs two distinct sketches with closed profiles", -1, a};
        app->status = "Loft failed";
      } else if (meshDeterminismHash(mesh) != meshDeterminismHash(verifyMesh)) {
        app->lastFeatureFailure = {"NON_DETERMINISTIC", "Loft generated non-deterministic mesh output", -1, a};
        app->status = "Loft failed: non-deterministic output";
      } else if (app->loftOptions.replaceSelectedObject && app->selectedObject >= 0) {
        if (!replaceSceneObjectMesh(app, app->selectedObject, mesh, "(Loft)")) {
          app->status = "Loft failed to replace selected object";
        } else {
          app->lastFeatureFailure = {};
          app->status = "Loft updated selected object";
          app->loftOptions.visible = false;
        }
      } else if (!applyAddExtrude(app, mesh, {})) {
        app->lastFeatureFailure = {"BOOLEAN_FAILED", "Loft result could not be added to scene", -1, a};
        app->status = "Loft failed";
      } else {
        app->lastFeatureFailure = {};
        SolidFeatureAction action;
        action.featureName = "Loft";
        action.sourceNames = {std::string(skA.meta.name.data()), std::string(skB.meta.name.data())};
        action.resultObjectName = objectName(app, static_cast<int>(app->sceneObjects.size()) - 1);
        app->timeline.push(std::move(action), "Loft");
        app->timelineCursor = app->timeline.size() - 1;
        app->status = "Loft complete";
        app->loftOptions.visible = false;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) app->loftOptions.visible = false;
  }
  ImGui::End();
  app->loftOptions.visible = open;
}

void drawShellWindow(AppState* app) {
  if (!app->shellOptions.visible) return;
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
  bool open = app->shellOptions.visible;
  if (ImGui::Begin("Shell Tool", &open)) {
    ImGui::Text("Target: %s", objectName(app, app->shellOptions.targetObject).c_str());
    ImGui::InputText("Thickness", app->shellOptions.thicknessBuffer,
                     sizeof(app->shellOptions.thicknessBuffer));
    if (ImGui::Button("Apply Shell")) {
      const auto perfStart = std::chrono::steady_clock::now();
      auto parsed = parseDimension(std::string(app->shellOptions.thicknessBuffer), app->project.defaultUnit);
      StlMesh mesh = parsed && app->shellOptions.targetObject >= 0 &&
                             app->shellOptions.targetObject < static_cast<int>(app->sceneObjects.size())
                         ? shellMesh(app->sceneObjects[app->shellOptions.targetObject], parsed->valueMm)
                         : StlMesh();
      const StlMesh verifyMesh = parsed && app->shellOptions.targetObject >= 0 &&
                                     app->shellOptions.targetObject < static_cast<int>(app->sceneObjects.size())
                                 ? shellMesh(app->sceneObjects[app->shellOptions.targetObject], parsed->valueMm)
                                 : StlMesh();
      if (!parsed || mesh.empty()) {
        app->lastFeatureFailure = {"INVALID_INPUT", "Shell requires valid thickness and target object", app->shellOptions.targetObject, -1};
        app->status = "Shell failed";
      } else if (meshDeterminismHash(mesh) != meshDeterminismHash(verifyMesh)) {
        app->lastFeatureFailure = {"NON_DETERMINISTIC", "Shell generated non-deterministic mesh output", app->shellOptions.targetObject, -1};
        app->status = "Shell failed: non-deterministic output";
      } else if (!replaceSceneObjectMesh(app, app->shellOptions.targetObject, mesh, "(Shell)")) {
        app->lastFeatureFailure = {"TOPOLOGY_INVALID", "Shell result failed topology validation", app->shellOptions.targetObject, -1};
        app->status = "Shell failed";
      } else {
        const auto perfEnd = std::chrono::steady_clock::now();
        app->opPerf.shellMs =
            std::chrono::duration<float, std::milli>(perfEnd - perfStart).count();
        app->lastFeatureFailure = {};
        SolidFeatureAction action;
        action.featureName = "Shell";
        action.sourceNames = {objectName(app, app->shellOptions.targetObject)};
        action.resultObjectName = objectName(app, app->shellOptions.targetObject);
        app->timeline.push(std::move(action), "Shell");
        app->timelineCursor = app->timeline.size() - 1;
        app->status = "Shell complete (updated selected object)";
        app->shellOptions.visible = false;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) app->shellOptions.visible = false;
  }
  ImGui::End();
  app->shellOptions.visible = open;
}

void drawCombineWindow(AppState* app) {
  if (!app->combineOptions.visible) return;

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_FirstUseEver);
  bool open = app->combineOptions.visible;
  if (ImGui::Begin("Combine Tool", &open)) {
    int op = static_cast<int>(app->combineOptions.operation);
    if (ImGui::Combo("Operation", &op, "Add\0Subtract\0Intersect\0")) {
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
    ImGui::TextDisabled("Boolean ops use AABB overlap/coplanar fallback");
    ImGui::TextDisabled("(requires touching/overlapping target+tool bounds)");

    ImGui::Separator();
    if (ImGui::Button("Apply")) {
      const bool subtract = (app->combineOptions.operation == BooleanOp::Subtract);
      const bool intersect = (app->combineOptions.operation == BooleanOp::Intersect);
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
                           std::string(subtract ? "Subtract Combine"
                                                : (intersect ? "Intersect Combine"
                                                             : "Add Combine")));
        app->timelineCursor = app->timeline.size() - 1;
      };

      if (subtract) {
        if (!applySubtractCombine(app, app->combineOptions.targets,
                                  app->combineOptions.tools,
                                  app->combineOptions.keepTools)) {
          app->lastFeatureFailure = {"NO_EFFECT", "Subtract had no overlapping/coplanar targets to remove", -1, -1};
          app->status = "Combine subtract removed no target objects";
        } else {
          app->lastFeatureFailure = {};
          recordCombine();
          app->status = "Combine subtract complete (object-level)";
        }
      } else if (intersect) {
        if (!applyIntersectCombine(app, app->combineOptions.targets,
                                   app->combineOptions.tools,
                                   app->combineOptions.keepTools)) {
          app->lastFeatureFailure = {"NO_OVERLAP", "Intersect found no touching/overlapping target-tool pairs", -1, -1};
          app->status = "Combine intersect found no overlapping objects";
        } else {
          app->lastFeatureFailure = {};
          recordCombine();
          app->status = "Combine intersect complete (AABB fallback)";
        }
      } else if (!applyAddCombine(app, app->combineOptions.targets,
                                  app->combineOptions.tools,
                                  app->combineOptions.keepTools)) {
        app->lastFeatureFailure = {"NO_CONTACT", "Add combine requires touching/overlapping target and tool bounds", -1, -1};
        app->status = "Combine add needs at least one touching/overlapping target and tool";
      } else {
        app->lastFeatureFailure = {};
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

void drawFilletWindow(AppState* app) {
  if (!app->filletOptions.visible) return;

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_FirstUseEver);
  bool open = app->filletOptions.visible;
  if (ImGui::Begin("Fillet Tool", &open)) {
    const int obj = app->filletOptions.targetObject;
    if (obj < 0 || obj >= static_cast<int>(app->sceneObjects.size())) {
      ImGui::TextDisabled("No valid target object selected.");
    } else {
      ImGui::Text("Target: %s", app->sceneObjectMeta[obj].name.data());
      ImGui::Text("Selected edges: %d", static_cast<int>(app->filletOptions.edges.size()));
    }

    ImGui::Text("Fillet Radius:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputText("##filletRadius", app->filletOptions.radiusBuffer,
                     sizeof(app->filletOptions.radiusBuffer));
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", unitSuffix(app->project.defaultUnit));

    if (ImGui::Button(app->filletOptions.pickEdges ? "Stop Edge Pick" : "Pick Edges")) {
      app->filletOptions.pickEdges = !app->filletOptions.pickEdges;
      app->objectPickMode = app->filletOptions.pickEdges ? ObjectPickMode::FilletEdges
                                                         : ObjectPickMode::None;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Edges")) {
      app->filletOptions.edges.clear();
    }

    ImGui::Separator();
    ImGui::BeginChild("##filletEdges", ImVec2(0.0f, 140.0f), true);
    int eraseIdx = -1;
    for (int i = 0; i < static_cast<int>(app->filletOptions.edges.size()); ++i) {
      const auto& e = app->filletOptions.edges[i];
      ImGui::PushID(i);
      if (ImGui::SmallButton("x")) eraseIdx = i;
      ImGui::SameLine();
      ImGui::Text("Edge %d  (%.2f, %.2f, %.2f) -> (%.2f, %.2f, %.2f)", i + 1,
                  e.a.x, e.a.y, e.a.z, e.b.x, e.b.y, e.b.z);
      ImGui::PopID();
    }
    if (eraseIdx >= 0) {
      app->filletOptions.edges.erase(app->filletOptions.edges.begin() + eraseIdx);
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::Button("Apply Fillet")) {
      auto parsed = parseDimension(std::string(app->filletOptions.radiusBuffer),
                                   app->project.defaultUnit);
      if (!parsed) {
        app->status = "Fillet: enter a valid radius";
      } else if (!applyFilletEdges(app, app->filletOptions.targetObject,
                                   app->filletOptions.edges, parsed->valueMm)) {
        app->status = "Fillet failed (select edges on an unlocked object)";
      } else {
        app->status = "Fillet applied";
        app->filletOptions.visible = false;
        app->filletOptions.pickEdges = false;
        app->objectPickMode = ObjectPickMode::None;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
      app->filletOptions.visible = false;
      app->filletOptions.pickEdges = false;
      if (app->objectPickMode == ObjectPickMode::FilletEdges) {
        app->objectPickMode = ObjectPickMode::None;
      }
    }
  }
  ImGui::End();

  app->filletOptions.visible = open;
  if (!app->filletOptions.visible && app->objectPickMode == ObjectPickMode::FilletEdges) {
    app->objectPickMode = ObjectPickMode::None;
    app->filletOptions.pickEdges = false;
  }
}

void drawDraftWindow(AppState* app) {
  if (!app->draftOptions.visible) return;

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
  bool open = app->draftOptions.visible;
  if (ImGui::Begin("Draft Tool", &open)) {
    int obj = app->draftOptions.targetObject;
    if (obj < 0 || obj >= static_cast<int>(app->sceneObjects.size())) {
      obj = app->selectedObject;
    }
    if (obj >= 0 && obj < static_cast<int>(app->sceneObjects.size())) {
      app->draftOptions.targetObject = obj;
      ImGui::Text("Target: %s", app->sceneObjectMeta[obj].name.data());
    } else {
      ImGui::TextDisabled("No target object selected");
    }

    ImGui::Text("Draft Angle (deg):");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputText("##draftAngle", app->draftOptions.angleBuffer,
                     sizeof(app->draftOptions.angleBuffer));
    ImGui::TextDisabled("Positive = inward taper toward +Z");

    if (ImGui::Button("Apply Draft")) {
      char* endPtr = nullptr;
      const float degrees = std::strtof(app->draftOptions.angleBuffer, &endPtr);
      if (endPtr == app->draftOptions.angleBuffer || !std::isfinite(degrees)) {
        app->status = "Draft: enter a valid angle in degrees";
      } else if (!applyDraftObject(app, app->draftOptions.targetObject, degrees)) {
        app->status = "Draft failed (check target object and lock state)";
      } else {
        app->status = "Draft applied";
        app->draftOptions.visible = false;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
      app->draftOptions.visible = false;
    }
  }
  ImGui::End();
  app->draftOptions.visible = open;
}

void drawCamWindow(AppState* app) {
  if (!app->showCamWindow) return;

  syncCamBuilderSources(app);

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(860.0f, 680.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Manufacture", &app->showCamWindow)) {
    ImGui::End();
    return;
  }

  if (ImGui::BeginTable("##camLayout", 2,
                        ImGuiTableFlags_SizingStretchProp |
                            ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupColumn("Setup", ImGuiTableColumnFlags_WidthStretch, 0.52f);
    ImGui::TableSetupColumn("Operations", ImGuiTableColumnFlags_WidthStretch, 0.48f);
    ImGui::TableNextColumn();

    ImGui::TextUnformatted("Stock / WCS");
    ImGui::DragFloat3("Stock Origin", &app->camStock.originMm.x, 1.0f);
    ImGui::DragFloat3("Stock Size", &app->camStock.sizeMm.x, 1.0f, 1.0f, 10000.0f);
    ImGui::DragFloat3("WCS Offset", &app->camStock.wcsOffsetMm.x, 1.0f);
    ImGui::DragFloat("Safe Retract", &app->camStock.safeRetractMm, 0.25f, 1.0f, 500.0f);
    if (ImGui::Button("Use Stock Top for WCS Z")) {
      app->camStock.wcsOffsetMm.z = app->camStock.sizeMm.z;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset CAM Session")) {
      resetCamSession(app->camBuilder, app->camOperations, app->selectedObject,
                      app->browserSelectedSketches.empty() ? -1 : app->browserSelectedSketches.front(),
                      app->camStock);
      app->camSelectedOperation = -1;
      app->status = "CAM session reset";
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Tool Library");
    if (ImGui::BeginListBox("##camTools", ImVec2(-1.0f, 130.0f))) {
      for (int i = 0; i < static_cast<int>(app->camTools.size()); ++i) {
        const bool selected = i == app->camBuilder.toolIndex;
        const std::string label = std::to_string(i + 1) + ". " + app->camTools[i].name.data();
        if (ImGui::Selectable(label.c_str(), selected)) {
          app->camBuilder.toolIndex = i;
        }
      }
      ImGui::EndListBox();
    }

    if (ImGui::Button("Add Tool")) {
      CamToolPreset tool;
      setName(tool.name, "New Tool");
      app->camTools.push_back(tool);
      app->camBuilder.toolIndex = static_cast<int>(app->camTools.size()) - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate") && app->camBuilder.toolIndex >= 0) {
      duplicateCamTool(app, app->camBuilder.toolIndex);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && app->camBuilder.toolIndex >= 0 &&
        app->camTools.size() > 1) {
      app->camTools.erase(app->camTools.begin() + app->camBuilder.toolIndex);
      app->camBuilder.toolIndex = std::clamp(app->camBuilder.toolIndex - 1, 0,
                                             static_cast<int>(app->camTools.size()) - 1);
    }

    if (app->camBuilder.toolIndex >= 0 &&
        app->camBuilder.toolIndex < static_cast<int>(app->camTools.size())) {
      auto& tool = app->camTools[app->camBuilder.toolIndex];
      ImGui::InputText("Tool Name", tool.name.data(), tool.name.size());
      int toolType = static_cast<int>(tool.type);
      if (ImGui::Combo("Tool Type", &toolType, "Flat End Mill\0Ball End Mill\0Drill\0")) {
        tool.type = static_cast<CamToolType>(toolType);
      }
      ImGui::DragFloat("Diameter", &tool.diameterMm, 0.1f, 0.1f, 100.0f);
      ImGui::DragFloat("Feed", &tool.feedRateMmPerMin, 10.0f, 10.0f, 30000.0f);
      ImGui::DragFloat("Plunge", &tool.plungeRateMmPerMin, 10.0f, 10.0f, 10000.0f);
      ImGui::DragFloat("Spindle", &tool.spindleRpm, 100.0f, 100.0f, 60000.0f);
      ImGui::DragFloat("Max Stepdown", &tool.maxStepDownMm, 0.05f, 0.05f, 50.0f);
      ImGui::DragFloat("Default Stepover", &tool.stepover, 0.01f, 0.05f, 1.0f);
    }

    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Operation Builder");
    int opType = static_cast<int>(app->camBuilder.type);
    if (ImGui::Combo("Operation", &opType, "Facing\0Pocket\0Contour\0Drilling\0")) {
      app->camBuilder.type = static_cast<CamOperationType>(opType);
    }

    std::vector<const char*> objectNames;
    objectNames.push_back("None");
    for (const auto& meta : app->sceneObjectMeta) objectNames.push_back(meta.name.data());
    int objectIndex = app->camBuilder.sourceObject + 1;
    if (ImGui::Combo("Source Object", &objectIndex, objectNames.data(),
                     static_cast<int>(objectNames.size()))) {
      app->camBuilder.sourceObject = objectIndex - 1;
    }

    std::vector<const char*> sketchNames;
    sketchNames.push_back("None");
    for (const auto& sketch : app->sketches) sketchNames.push_back(sketch.meta.name.data());
    int sketchIndex = app->camBuilder.sourceSketch + 1;
    if (ImGui::Combo("Source Sketch", &sketchIndex, sketchNames.data(),
                     static_cast<int>(sketchNames.size()))) {
      app->camBuilder.sourceSketch = sketchIndex - 1;
    }

    if (ImGui::Button("Top From Stock")) {
      setCamTopFromStock(app);
    }
    ImGui::SameLine();
    if (ImGui::Button("Top From Source")) {
      setCamTopFromSource(app);
    }

    ImGui::DragFloat("Top Z", &app->camBuilder.topZMm, 0.25f);
    ImGui::DragFloat("Depth", &app->camBuilder.depthMm, 0.25f, 0.05f, 500.0f);
    ImGui::DragFloat("Step Down", &app->camBuilder.stepDownMm, 0.1f, 0.05f, 100.0f);
    ImGui::DragFloat("Stepover %", &app->camBuilder.stepoverPercent, 1.0f, 5.0f, 95.0f);
    ImGui::DragFloat("Clearance", &app->camBuilder.clearanceMm, 0.25f, 1.0f, 500.0f);

    if (ImGui::Button("Queue Operation", ImVec2(-1.0f, 0.0f))) {
      CamOperation operation;
      std::string error;
      if (!generateCamOperation(app->camBuilder, app->camTools, app->camStock,
                                app->sceneObjects, app->sketches, app->referencePoints,
                                operation, error)) {
        app->status = "CAM: " + error;
      } else {
        const int opNumber = static_cast<int>(app->camOperations.size()) + 1;
        operation.name = std::string(camOperationTypeLabel(operation.type)) +
                         " Op " + std::to_string(opNumber);
        app->camOperations.push_back(std::move(operation));
        app->camSelectedOperation = static_cast<int>(app->camOperations.size()) - 1;
        app->timeline.push(ReferenceGeometryAction{"CAM", app->camOperations.back().name},
                           app->camOperations.back().name);
        app->timelineCursor = app->timeline.size() - 1;
        app->status = "CAM operation queued";
      }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Queued Operations");
    float totalMinutes = 0.0f;
    for (const auto& operation : app->camOperations) totalMinutes += operation.estimatedMinutes;
    ImGui::Text("Ops: %d", static_cast<int>(app->camOperations.size()));
    ImGui::Text("Estimated Time: %.2f min", totalMinutes);
    ImGui::Text("Post: %s", camPostProcessorLabel(app->camPostProcessor));

    if (ImGui::BeginListBox("##camOps", ImVec2(-1.0f, 180.0f))) {
      for (int i = 0; i < static_cast<int>(app->camOperations.size()); ++i) {
        const auto& operation = app->camOperations[i];
        std::string label = operation.name;
        bool severe = false;
        for (const auto& warning : operation.warnings) severe = severe || warning.severe;
        if (severe) label += " [warn]";
        if (ImGui::Selectable(label.c_str(), i == app->camSelectedOperation)) {
          app->camSelectedOperation = i;
        }
      }
      ImGui::EndListBox();
    }

    if (ImGui::Button("Remove Selected") && app->camSelectedOperation >= 0 &&
        app->camSelectedOperation < static_cast<int>(app->camOperations.size())) {
      app->camOperations.erase(app->camOperations.begin() + app->camSelectedOperation);
      app->camSelectedOperation = std::min(app->camSelectedOperation,
                                           static_cast<int>(app->camOperations.size()) - 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear All") && !app->camOperations.empty()) {
      app->camOperations.clear();
      app->camSelectedOperation = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Export G-code") && !app->camOperations.empty() &&
        !app->exportBrowser.isVisible()) {
      app->exportIntent = ExportIntent::Gcode;
      app->exportBrowser.show({{"NC Files", {".nc", ".gcode"}}}, {},
                              "Export Toolpath", "Export");
    }

    if (app->camSelectedOperation >= 0 &&
        app->camSelectedOperation < static_cast<int>(app->camOperations.size())) {
      const auto& operation = app->camOperations[app->camSelectedOperation];
      ImGui::Separator();
      ImGui::Text("Selected: %s", operation.name.c_str());
      ImGui::Text("Length: %.1f mm", operation.estimatedLengthMm);
      ImGui::Text("Time: %.2f min", operation.estimatedMinutes);
      ImGui::Text("Segments: %d", static_cast<int>(operation.segments.size()));
      ImGui::Text("Warnings: %d", static_cast<int>(operation.warnings.size()));
      ImGui::BeginChild("##camWarnings", ImVec2(0.0f, 110.0f), true);
      if (operation.warnings.empty()) {
        ImGui::TextDisabled("No baseline collision/gouge warnings");
      } else {
        for (const auto& warning : operation.warnings) {
          ImGui::TextColored(warning.severe ? ImVec4(1.0f, 0.4f, 0.2f, 1.0f)
                                            : ImVec4(0.95f, 0.8f, 0.25f, 1.0f),
                             "%s", warning.message.c_str());
        }
      }
      ImGui::EndChild();
    }

    ImGui::EndTable();
  }

  ImGui::Separator();
  ImGui::TextDisabled("Preview legend: green=cut, blue=plunge, yellow=rapid, orange=warning, grey=stock, RGB=WCS axes");

  ImGui::End();
}

void drawDrawingWindow(AppState* app) {
  if (!app->showDrawingWindow) return;

  if (app->drawingSourceObject < 0 ||
      app->drawingSourceObject >= static_cast<int>(app->sceneObjects.size())) {
    app->drawingSourceObject = app->selectedObject >= 0 ? app->selectedObject : 0;
  }

  bool open = app->showDrawingWindow;
  ImGui::SetNextWindowSize(ImVec2(560.0f, 520.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Drawings", &open)) {
    ImGui::End();
    app->showDrawingWindow = open;
    return;
  }

  std::vector<const char*> objectNamesUi;
  for (const auto& meta : app->sceneObjectMeta) objectNamesUi.push_back(meta.name.data());
  if (!objectNamesUi.empty()) {
    int idx = std::clamp(app->drawingSourceObject, 0,
                         static_cast<int>(objectNamesUi.size()) - 1);
    ImGui::Combo("Source Object", &idx, objectNamesUi.data(),
                 static_cast<int>(objectNamesUi.size()));
    app->drawingSourceObject = idx;
  }

  ImGui::SliderFloat("Section Ratio", &app->drawingSectionRatio, 0.1f, 0.9f, "%.2f");
  if (ImGui::Button("Generate Drawing Sheet") &&
      app->drawingSourceObject >= 0 &&
      app->drawingSourceObject < static_cast<int>(app->sceneObjects.size())) {
    std::string err;
    if (buildDrawingSheet(app->sceneObjects[app->drawingSourceObject],
                          objectName(app, app->drawingSourceObject),
                          app->drawingSectionRatio,
                          app->drawingSheet, err)) {
      app->drawingSheet.sourceObject = app->drawingSourceObject;
      app->status = "Drawing sheet generated";
    } else {
      app->status = "Drawing failed: " + err;
    }
  }

  if (!app->drawingSheet.views.empty()) {
    ImGui::Separator();
    ImGui::Text("Title: %s", app->drawingSheet.title.c_str());
    ImGui::Text("Views: %d", static_cast<int>(app->drawingSheet.views.size()));

    if (ImGui::BeginTable("##drawingViews", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("View");
      ImGui::TableSetupColumn("Width (mm)");
      ImGui::TableSetupColumn("Height (mm)");
      ImGui::TableHeadersRow();
      for (const auto& view : app->drawingSheet.views) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(view.label.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.3f", view.widthMm);
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%.3f", view.heightMm);
      }
      ImGui::EndTable();
    }

    if (ImGui::BeginTable("##drawingDims", 2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("Dimension");
      ImGui::TableSetupColumn("Value (mm)");
      ImGui::TableHeadersRow();
      for (const auto& dim : app->drawingSheet.dimensions) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(dim.label.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.3f", dim.valueMm);
      }
      ImGui::EndTable();
    }

    if (ImGui::Button("Export PDF") && !app->exportBrowser.isVisible()) {
      app->exportIntent = ExportIntent::Pdf;
      app->exportBrowser.show({{"PDF Files", {".pdf"}}}, {}, "Export PDF", "Export");
    }
    ImGui::SameLine();
    if (ImGui::Button("Export DXF") && !app->exportBrowser.isVisible()) {
      app->exportIntent = ExportIntent::Dxf;
      app->exportBrowser.show({{"DXF Files", {".dxf"}}}, {}, "Export DXF", "Export");
    }
  } else {
    ImGui::TextDisabled("Generate a drawing sheet to enable PDF/DXF export.");
  }

  ImGui::End();
  app->showDrawingWindow = open;
}

void drawDfmWindow(AppState* app) {
  if (!app->showDfmWindow) return;

  if (app->dfmSourceObject < 0 ||
      app->dfmSourceObject >= static_cast<int>(app->sceneObjects.size())) {
    app->dfmSourceObject = app->selectedObject >= 0 ? app->selectedObject : 0;
  }

  bool open = app->showDfmWindow;
  ImGui::SetNextWindowSize(ImVec2(520.0f, 460.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("DFM Checks", &open)) {
    ImGui::End();
    app->showDfmWindow = open;
    return;
  }

  std::vector<const char*> objectNamesUi;
  for (const auto& meta : app->sceneObjectMeta) objectNamesUi.push_back(meta.name.data());
  if (!objectNamesUi.empty()) {
    int idx = std::clamp(app->dfmSourceObject, 0,
                         static_cast<int>(objectNamesUi.size()) - 1);
    ImGui::Combo("Source Object", &idx, objectNamesUi.data(),
                 static_cast<int>(objectNamesUi.size()));
    app->dfmSourceObject = idx;
  }

  if (ImGui::Button("Run DFM Heuristics") &&
      app->dfmSourceObject >= 0 &&
      app->dfmSourceObject < static_cast<int>(app->sceneObjects.size())) {
    std::string err;
    if (runDfmChecks(app->sceneObjects[app->dfmSourceObject], app->dfmReport, err)) {
      app->dfmHasReport = true;
      app->status = "DFM checks completed";
    } else {
      app->dfmHasReport = false;
      app->status = "DFM checks failed: " + err;
    }
  }

  if (app->dfmHasReport) {
    ImGui::Separator();
    ImGui::Text("Estimated Min Wall: %.3f mm", app->dfmReport.estimatedMinWallMm);
    ImGui::Text("Estimated Min Radius: %.3f mm", app->dfmReport.estimatedMinRadiusMm);
    ImGui::Text("Drillable Features: %d", app->dfmReport.drillableFeatureCount);

    ImGui::Separator();
    if (app->dfmReport.issues.empty()) {
      ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1.0f), "No DFM issues detected by baseline heuristics");
    } else {
      for (const auto& issue : app->dfmReport.issues) {
        const ImVec4 color = issue.severe ? ImVec4(1.0f, 0.35f, 0.2f, 1.0f)
                                          : ImVec4(0.95f, 0.78f, 0.2f, 1.0f);
        ImGui::TextColored(color, "[%s] %s", issue.code.c_str(), issue.message.c_str());
      }
    }
  } else {
    ImGui::TextDisabled("Run checks to evaluate wall thickness, minimum radius, and drillability.");
  }

  ImGui::End();
  app->showDfmWindow = open;
}
