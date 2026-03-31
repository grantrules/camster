#include <array>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>

#include "AppSettings.hpp"
#include "CameraController.hpp"
#include "ColorVertex.hpp"
#include "Gizmo.hpp"
#include "Project.hpp"
#include "Scene.hpp"
#include "StlMesh.hpp"
#include "VulkanRenderer.hpp"
#include "sketch/ExtrudeTool.hpp"
#include "sketch/Profile.hpp"
#include "sketch/Sketch.hpp"
#include "sketch/SketchTool.hpp"
#include "sketch/Constraint.hpp"
#include "ui/FileBrowser.hpp"
#include "ui/Toolbar.hpp"

namespace {
struct AppState {
  VulkanRenderer renderer;
  CameraController camera;
  StlMesh mesh;       // combined mesh sent to renderer
  FileBrowser fileBrowser;
  FileBrowser exportBrowser;  // separate browser instance for export

  // Individual mesh objects for selection / export.
  std::vector<StlMesh> sceneObjects;
  int selectedObject = -1;  // index into sceneObjects, -1 = none

  // Scene / sketch state.
  SceneMode sceneMode = SceneMode::View3D;
  SketchPlane activePlane = SketchPlane::XY;
  Sketch sketches[3];  // one per SketchPlane
  SketchTool sketchTool;
  ExtrudeTool extrudeTool;
  Gizmo gizmo;
  Toolbar toolbar;
  AppSettings appSettings;
  Project project;
  bool showAppSettings = false;
  bool showProjectSettings = false;

  Sketch& activeSketch() { return sketches[static_cast<int>(activePlane)]; }

  std::string status = "Ready";
  bool dragging = false;
  bool wasLeftDown = false;  // for click-edge detection

  // Rubber-band drag selection state.
  bool dragSelecting = false;
  glm::vec2 dragStartScreen{0.0f};  // screen pixel position
  glm::vec2 dragCurScreen{0.0f};

  struct LoadResult {
    bool success = false;
    StlMesh mesh;
    std::string path;
    std::string error;
  };

  std::future<LoadResult> pendingLoad;
  bool loadingMesh = false;
};

void framebufferResizeCallback(GLFWwindow* window, int, int) {
  auto* app = static_cast<AppState*>(glfwGetWindowUserPointer(window));
  if (app) {
    app->renderer.markFramebufferResized();
  }
}

void rebuildCombinedMesh(AppState* app) {
  app->mesh = StlMesh();
  for (const auto& obj : app->sceneObjects) {
    app->mesh.append(obj);
  }
  app->renderer.setMesh(app->mesh);
}

std::optional<ImVec2> projectToScreen(glm::vec3 world, const glm::mat4& view,
                                      const glm::mat4& projection,
                                      const ImVec2& viewportSize) {
  const glm::vec4 clip = projection * view * glm::vec4(world, 1.0f);
  if (clip.w <= 1e-6f) return std::nullopt;

  const glm::vec3 ndc = glm::vec3(clip) / clip.w;
  if (ndc.z < 0.0f || ndc.z > 1.0f) return std::nullopt;

  return ImVec2((ndc.x * 0.5f + 0.5f) * viewportSize.x,
                (ndc.y * 0.5f + 0.5f) * viewportSize.y);
}

// Möller–Trumbore ray-triangle intersection.  Returns distance or -1.
float rayTriangle(glm::vec3 origin, glm::vec3 dir, glm::vec3 v0, glm::vec3 v1, glm::vec3 v2) {
  const glm::vec3 e1 = v1 - v0;
  const glm::vec3 e2 = v2 - v0;
  const glm::vec3 h = glm::cross(dir, e2);
  const float a = glm::dot(e1, h);
  if (std::abs(a) < 1e-8f) return -1.0f;
  const float f = 1.0f / a;
  const glm::vec3 s = origin - v0;
  const float u = f * glm::dot(s, h);
  if (u < 0.0f || u > 1.0f) return -1.0f;
  const glm::vec3 q = glm::cross(s, e1);
  const float v = f * glm::dot(dir, q);
  if (v < 0.0f || u + v > 1.0f) return -1.0f;
  const float t = f * glm::dot(e2, q);
  return t > 1e-6f ? t : -1.0f;
}

// Pick scene object by ray.  Returns index or -1.
int pickObject(const AppState& app, glm::vec3 rayO, glm::vec3 rayD) {
  float bestT = std::numeric_limits<float>::max();
  int bestIdx = -1;
  for (int oi = 0; oi < static_cast<int>(app.sceneObjects.size()); ++oi) {
    const auto& verts = app.sceneObjects[oi].vertices();
    const auto& inds = app.sceneObjects[oi].indices();
    for (size_t i = 0; i + 2 < inds.size(); i += 3) {
      float t = rayTriangle(rayO, rayD, verts[inds[i]].position, verts[inds[i + 1]].position,
                            verts[inds[i + 2]].position);
      if (t > 0.0f && t < bestT) {
        bestT = t;
        bestIdx = oi;
      }
    }
  }
  return bestIdx;
}

void appendGrid(std::vector<ColorVertex>& lines, SketchPlane plane, float extent, float spacing) {
  const glm::vec4 gridColor(0.25f, 0.25f, 0.25f, 1.0f);
  const int count = static_cast<int>(extent / spacing);

  for (int i = -count; i <= count; ++i) {
    const float offset = static_cast<float>(i) * spacing;
    glm::vec2 a1(offset, -extent);
    glm::vec2 a2(offset, extent);
    glm::vec2 b1(-extent, offset);
    glm::vec2 b2(extent, offset);

    lines.push_back({toWorld(a1, plane), gridColor});
    lines.push_back({toWorld(a2, plane), gridColor});
    lines.push_back({toWorld(b1, plane), gridColor});
    lines.push_back({toWorld(b2, plane), gridColor});
  }
}

void enterSketchMode(AppState* app, SketchPlane plane) {
  app->sceneMode = SceneMode::Sketch;
  app->activePlane = plane;
  app->sketchTool.setTool(Tool::None);

  // Snap camera to face the chosen plane.
  switch (plane) {
    case SketchPlane::XY: app->camera.snap(CameraController::Orientation::Front); break;
    case SketchPlane::XZ: app->camera.snap(CameraController::Orientation::Top);   break;
    case SketchPlane::YZ: app->camera.snap(CameraController::Orientation::Right); break;
  }
}

void exitSketchMode(AppState* app) {
  app->sceneMode = SceneMode::View3D;
  app->sketchTool.cancel();
  app->sketchTool.setTool(Tool::None);
  app->extrudeTool.cancel();
  app->activeSketch().clearSelection();
}

void drawMenuBar(AppState* app) {
  if (!ImGui::BeginMainMenuBar()) return;

  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("New Scene")) {
      app->mesh = StlMesh();
      app->sceneObjects.clear();
      app->selectedObject = -1;
      app->renderer.setMesh(app->mesh);
      exitSketchMode(app);
      for (auto& s : app->sketches) { s.clear(); }
      app->extrudeTool.cancel();
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
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Sketch")) {
    if (ImGui::MenuItem("XY Plane")) enterSketchMode(app, SketchPlane::XY);
    if (ImGui::MenuItem("XZ Plane")) enterSketchMode(app, SketchPlane::XZ);
    if (ImGui::MenuItem("YZ Plane")) enterSketchMode(app, SketchPlane::YZ);
    ImGui::Separator();
    if (ImGui::MenuItem("Exit Sketch", nullptr, false, app->sceneMode == SceneMode::Sketch)) {
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

  ImGui::EndMainMenuBar();
}

void drawPanel(AppState* app) {
  ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(240.0f, 0.0f), ImGuiCond_FirstUseEver);
  ImGui::Begin("Actions");

  // --- File operations ---
  if (ImGui::Button("Open", ImVec2(-1.0f, 0.0f))) {
    if (!app->loadingMesh && !app->fileBrowser.isVisible()) {
      app->fileBrowser.show({{"STL Files", {".stl"}}}, {}, "Open File", "Open");
    }
  }

  ImGui::Separator();

  // --- Render toggles ---
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

  // --- Snap presets ---
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

// Helper to draw a unit combo box.  Returns true if the value changed.
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

}  // namespace

int main() {
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW\n";
    return 1;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  GLFWwindow* window = glfwCreateWindow(1280, 800, "camster - Vulkan STL Viewer", nullptr, nullptr);
  if (!window) {
    std::cerr << "Failed to create window\n";
    glfwTerminate();
    return 1;
  }

  AppState app;
  glfwSetWindowUserPointer(window, &app);
  glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

  std::string error;
  if (!app.renderer.initialize(window, error)) {
    std::cerr << "Renderer initialization failed: " << error << "\n";
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  app.appSettings.load();  // load saved app settings (no-op if file missing)
  app.project.initFromAppSettings(app.appSettings);

  app.sceneObjects.push_back(StlMesh::makeUnitCube());
  rebuildCombinedMesh(&app);

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    // Mesh loading runs on a background thread so the UI stays responsive
    // during large file reads.  We poll the future each frame with a zero
    // timeout and only consume the result once it's ready.
    if (app.loadingMesh && app.pendingLoad.valid() &&
        app.pendingLoad.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      AppState::LoadResult result = app.pendingLoad.get();
      app.loadingMesh = false;

      if (!result.success) {
        app.status = "Open failed: " + result.error;
      } else {
        app.sceneObjects.clear();
        app.sceneObjects.push_back(std::move(result.mesh));
        app.selectedObject = -1;
        rebuildCombinedMesh(&app);
        app.status = "Loaded " + result.path;
      }
    }

    app.renderer.beginImGuiFrame();

    // --- Menu bar (always visible) ---
    drawMenuBar(&app);

    // --- Toolbar (sketch mode only) ---
    ToolbarAction toolbarAction;
    if (app.sceneMode == SceneMode::Sketch) {
      toolbarAction =
          app.toolbar.draw(app.sketchTool, app.extrudeTool,
                           app.activeSketch().hasSelection(), app.project.defaultUnit);
    }

    // Handle "Extrude" button press: gather profiles from selection.
    if (toolbarAction.extrudeRequested && !app.extrudeTool.active()) {
      const auto& sketch = app.activeSketch();
      const auto sel = sketch.selectedIndices();

      std::vector<std::vector<glm::vec2>> polylines;
      for (size_t idx : sel) {
        const auto& elems = sketch.elements();
        if (!elems[idx].construction)
          polylines.push_back(profile::tessellate2D(elems[idx].geometry));
      }

      auto profiles = profile::chainProfiles(polylines);
      if (!profiles.empty()) {
        app.extrudeTool.begin(std::move(profiles), app.activePlane);
        app.camera.snap(CameraController::Orientation::Isometric);
        app.status = "Drag or type distance, then Confirm";
      } else {
        app.status = "Selection does not form a closed profile";
      }
    }

    // Handle delete request.
    if (toolbarAction.deleteRequested) {
      app.activeSketch().deleteSelected();
      app.status = "Deleted selected elements";
    }

    // Handle construction toggle.
    if (toolbarAction.toggleConstruction) {
      for (size_t idx : app.activeSketch().selectedIndices()) {
        app.activeSketch().toggleConstruction(idx);
      }
      app.status = "Toggled construction";
    }

    // Handle constraint requests from toolbar.
    if (toolbarAction.constraintRequested != ConstraintTool::None) {
      auto& sketch = app.activeSketch();
      float valueMm = 0.0f;
      // Dimensional constraints need a parsed value.
      if (toolbarAction.constraintRequested == ConstraintTool::Length ||
          toolbarAction.constraintRequested == ConstraintTool::Radius ||
          toolbarAction.constraintRequested == ConstraintTool::Angle) {
        auto parsed = parseDimension(std::string(app.toolbar.constraintValue()),
                                     app.project.defaultUnit);
        if (parsed) {
          valueMm = parsed->valueMm;
        } else {
          app.status = (toolbarAction.constraintRequested == ConstraintTool::Angle)
                           ? "Enter angle in degrees"
                           : "Enter a value in the Dimension tab";
          valueMm = -1.0f;  // signal: skip
        }
      }
      if (valueMm >= 0.0f) {
        app.status = sketch.applyConstraintToSelection(
            toolbarAction.constraintRequested, valueMm);
      }
    }

    // Handle extrude confirm.
    if (toolbarAction.extrudeConfirmed && app.extrudeTool.active()) {
      StlMesh extruded = app.extrudeTool.confirm();
      if (!extruded.empty()) {
        app.sceneObjects.push_back(std::move(extruded));
        app.selectedObject = static_cast<int>(app.sceneObjects.size()) - 1;
        rebuildCombinedMesh(&app);
        app.status = "Extruded (click to select, File > Export STL to save)";
      }
    }

    // WantCaptureMouse is true when ImGui has focus (e.g. hovering a window,
    // dragging a slider).  We skip scene input in that case so mouse events
    // don't simultaneously rotate the camera and interact with UI.
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse) {
      double x = 0.0;
      double y = 0.0;
      glfwGetCursorPos(window, &x, &y);
      const bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
      const bool ctrlHeld = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                             glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);

      if (app.extrudeTool.active()) {
        // --- Extrude mode: mouse controls extrusion distance ---
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        const glm::mat4 view = app.camera.viewMatrix();
        const glm::mat4 proj = app.camera.projectionMatrix(app.renderer.framebufferAspect());

        glm::vec3 rayO, rayD;
        screenToRay(static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(fbW), static_cast<float>(fbH),
                    view, proj, rayO, rayD);

        if (leftDown && !app.wasLeftDown) {
          app.extrudeTool.mouseDown(rayO, rayD);
        } else if (leftDown) {
          app.extrudeTool.mouseMove(rayO, rayD);
        } else if (!leftDown && app.wasLeftDown) {
          app.extrudeTool.mouseUp();
        }

      } else if (app.sceneMode == SceneMode::Sketch &&
                 app.sketchTool.activeTool() != Tool::None) {
        // --- Drawing mode: feed mouse to SketchTool ---
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        const glm::mat4 view = app.camera.viewMatrix();
        const glm::mat4 proj = app.camera.projectionMatrix(app.renderer.framebufferAspect());

        auto hit = rayPlaneHit(static_cast<float>(x), static_cast<float>(y),
                               static_cast<float>(fbW), static_cast<float>(fbH),
                               view, proj, app.activePlane);
        if (hit) {
          glm::vec2 planePos = toPlane(*hit, app.activePlane);

          // Snap to existing control points.
          auto snap = app.activeSketch().snapToPoint(planePos, kSnapThreshold);
          if (snap) planePos = *snap;

          app.sketchTool.mouseMove(planePos);

          if (leftDown && !app.wasLeftDown) {
            app.sketchTool.mouseClick(planePos);
          }
        }

      } else if (app.sceneMode == SceneMode::Sketch &&
                 app.sketchTool.activeTool() == Tool::None) {
        // --- Selection mode: click, ctrl-click, or drag-select ---
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        const glm::mat4 view = app.camera.viewMatrix();
        const glm::mat4 proj = app.camera.projectionMatrix(app.renderer.framebufferAspect());

        if (leftDown && !app.wasLeftDown) {
          // Mouse down: record start position for potential drag.
          app.dragStartScreen = {static_cast<float>(x), static_cast<float>(y)};
          app.dragCurScreen = app.dragStartScreen;
          app.dragSelecting = false;
        } else if (leftDown && app.wasLeftDown) {
          // Mouse held: check if we've moved enough to start a drag-select.
          app.dragCurScreen = {static_cast<float>(x), static_cast<float>(y)};
          const glm::vec2 delta = app.dragCurScreen - app.dragStartScreen;
          if (glm::dot(delta, delta) > kDragThresholdSq) {
            app.dragSelecting = true;
          }
        } else if (!leftDown && app.wasLeftDown) {
          if (app.dragSelecting) {
            // Drag-select complete: convert screen rect to plane coords.
            auto hitA = rayPlaneHit(app.dragStartScreen.x, app.dragStartScreen.y,
                                    static_cast<float>(fbW), static_cast<float>(fbH),
                                    view, proj, app.activePlane);
            auto hitB = rayPlaneHit(app.dragCurScreen.x, app.dragCurScreen.y,
                                    static_cast<float>(fbW), static_cast<float>(fbH),
                                    view, proj, app.activePlane);
            if (hitA && hitB) {
              glm::vec2 pA = toPlane(*hitA, app.activePlane);
              glm::vec2 pB = toPlane(*hitB, app.activePlane);
              if (ctrlHeld) {
                app.activeSketch().addToSelectInRect(pA, pB);
              } else {
                app.activeSketch().selectInRect(pA, pB);
              }
            }
            app.dragSelecting = false;
          } else {
            // Single click: point-select with hit test.
            auto hit = rayPlaneHit(static_cast<float>(x), static_cast<float>(y),
                                   static_cast<float>(fbW), static_cast<float>(fbH),
                                   view, proj, app.activePlane);
            if (hit) {
              glm::vec2 planePos = toPlane(*hit, app.activePlane);
              auto idx = app.activeSketch().hitTest(planePos, kHitTestThreshold);
              if (idx) {
                if (ctrlHeld) {
                  app.activeSketch().toggleSelect(*idx);
                } else {
                  app.activeSketch().select(*idx);
                }
              } else if (!ctrlHeld) {
                app.activeSketch().clearSelection();
              }
            }
          }
        }

      } else {
        // --- 3D orbit camera + object picking ---
        if (leftDown && !app.wasLeftDown) {
          app.dragging = true;
          app.camera.beginRotate(x, y);
          app.dragStartScreen = {static_cast<float>(x), static_cast<float>(y)};
        } else if (leftDown && app.dragging) {
          app.camera.rotateTo(x, y);
        } else if (!leftDown && app.dragging) {
          app.dragging = false;
          app.camera.endRotate();

          // If mouse barely moved, treat as a click → pick object.
          const glm::vec2 cur(static_cast<float>(x), static_cast<float>(y));
          if (glm::dot(cur - app.dragStartScreen, cur - app.dragStartScreen) < kDragThresholdSq) {
            int fbW = 0, fbH = 0;
            glfwGetFramebufferSize(window, &fbW, &fbH);
            const glm::mat4 view = app.camera.viewMatrix();
            const glm::mat4 proj =
                app.camera.projectionMatrix(app.renderer.framebufferAspect());

            glm::vec3 rayO, rayD;
            screenToRay(static_cast<float>(x), static_cast<float>(y),
                        static_cast<float>(fbW), static_cast<float>(fbH),
                        view, proj, rayO, rayD);

            int hit = pickObject(app, rayO, rayD);
            app.selectedObject = hit;
            if (hit >= 0) {
              app.status = "Object selected (File > Export STL to save)";
            } else {
              app.status = "Ready";
            }
          }
        }
      }

      app.wasLeftDown = leftDown;

      const float wheel = io.MouseWheel;
      if (wheel != 0.0f) {
        app.camera.zoom(wheel);
      }
    }

    // Escape key: cancel extrude, cancel active tool, or exit sketch mode.
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      if (app.extrudeTool.active()) {
        app.extrudeTool.cancel();
        app.status = "Extrude cancelled";
      } else if (app.sketchTool.activeTool() != Tool::None) {
        app.sketchTool.cancel();
      } else if (app.sceneMode == SceneMode::Sketch) {
        exitSketchMode(&app);
      }
    }

    // Delete key: remove selected sketch elements.
    if (app.sceneMode == SceneMode::Sketch &&
        (glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS ||
         glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS)) {
      if (app.activeSketch().hasSelection()) {
        app.activeSketch().deleteSelected();
        app.status = "Deleted selected elements";
      }
    }

    // Right-click context menu (sketch mode).
    if (app.sceneMode == SceneMode::Sketch && !app.extrudeTool.active()) {
      if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS &&
          !io.WantCaptureMouse) {
        ImGui::OpenPopup("##sketchCtx");
      }
      if (ImGui::BeginPopup("##sketchCtx")) {
        if (ImGui::MenuItem("Select", nullptr, false, true)) {
          app.sketchTool.setTool(Tool::None);
        }
        if (ImGui::MenuItem("Line")) app.sketchTool.setTool(Tool::Line);
        if (ImGui::MenuItem("Rectangle")) app.sketchTool.setTool(Tool::Rectangle);
        if (ImGui::MenuItem("Circle")) app.sketchTool.setTool(Tool::Circle);
        if (ImGui::MenuItem("Arc")) app.sketchTool.setTool(Tool::Arc);
        ImGui::Separator();
        bool hasSel = app.activeSketch().hasSelection();
        if (ImGui::MenuItem("Toggle Construction", nullptr, false, hasSel)) {
          for (size_t idx : app.activeSketch().selectedIndices()) {
            app.activeSketch().toggleConstruction(idx);
          }
        }
        if (ImGui::MenuItem("Delete", "Del", false, hasSel)) {
          app.activeSketch().deleteSelected();
          app.status = "Deleted selected elements";
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Horizontal", nullptr, false, hasSel)) {
          app.activeSketch().applyConstraintToSelection(ConstraintTool::Horizontal, 0.0f);
        }
        if (ImGui::MenuItem("Vertical", nullptr, false, hasSel)) {
          app.activeSketch().applyConstraintToSelection(ConstraintTool::Vertical, 0.0f);
        }
        if (ImGui::MenuItem("Fixed", nullptr, false, hasSel)) {
          app.activeSketch().applyConstraintToSelection(ConstraintTool::Fixed, 0.0f);
        }
        ImGui::EndPopup();
      }
    }

    // Consume completed sketch primitives.
    if (auto prim = app.sketchTool.takeResult()) {
      app.activeSketch().addCompletedPrimitive(std::move(*prim));
    }

    // --- Build line data every frame ---
    std::vector<ColorVertex> allLines;
    std::vector<SketchDimensionLabel> dimensionLabels;
    std::vector<std::vector<glm::vec2>> filledProfiles;
    std::vector<glm::vec2> danglingPoints;
    app.gizmo.appendLines(allLines);

    if (app.sceneMode == SceneMode::Sketch) {
      appendGrid(allLines, app.activePlane, app.project.gridExtent, app.project.gridSpacing);
      app.activeSketch().appendLines(allLines, app.activePlane);
      app.activeSketch().appendConstraintAnnotations(allLines, app.activePlane);
      app.activeSketch().appendConstraintLabels(dimensionLabels, app.activePlane,
                                               app.project.defaultUnit);
      filledProfiles = app.activeSketch().closedProfiles();
      danglingPoints = app.activeSketch().danglingEndpoints();
      app.sketchTool.appendPreview(allLines, app.activePlane);

      // Snap indicator: show a small preview cross at the snap point when drawing.
      if (app.sketchTool.activeTool() != Tool::None) {
        double mx = 0.0, my = 0.0;
        glfwGetCursorPos(window, &mx, &my);
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        const glm::mat4 view = app.camera.viewMatrix();
        const glm::mat4 proj = app.camera.projectionMatrix(app.renderer.framebufferAspect());
        auto hit = rayPlaneHit(static_cast<float>(mx), static_cast<float>(my),
                               static_cast<float>(fbW), static_cast<float>(fbH),
                               view, proj, app.activePlane);
        if (hit) {
          glm::vec2 pp = toPlane(*hit, app.activePlane);
          auto snap = app.activeSketch().snapToPoint(pp, kSnapThreshold);
          if (snap) {
            const float sz = kSnapIndicatorSize;
            const glm::vec4 snapColor{0.0f, 1.0f, 1.0f, 1.0f};
            glm::vec2 s = *snap;
            allLines.push_back({toWorld(s + glm::vec2(-sz, 0.0f), app.activePlane), snapColor});
            allLines.push_back({toWorld(s + glm::vec2(sz, 0.0f), app.activePlane), snapColor});
            allLines.push_back({toWorld(s + glm::vec2(0.0f, -sz), app.activePlane), snapColor});
            allLines.push_back({toWorld(s + glm::vec2(0.0f, sz), app.activePlane), snapColor});
          }
        }
      }
    }

    // Extrude preview lines (visible from any view).
    app.extrudeTool.appendPreview(allLines);

    // Highlight selected 3D object with cyan wireframe overlay.
    if (app.selectedObject >= 0 &&
        app.selectedObject < static_cast<int>(app.sceneObjects.size())) {
      const auto& obj = app.sceneObjects[app.selectedObject];
      const auto& verts = obj.vertices();
      const auto& inds = obj.indices();
      const glm::vec4 hlColor(0.0f, 0.8f, 1.0f, 1.0f);
      for (size_t i = 0; i + 2 < inds.size(); i += 3) {
        const glm::vec3& a = verts[inds[i]].position;
        const glm::vec3& b = verts[inds[i + 1]].position;
        const glm::vec3& c = verts[inds[i + 2]].position;
        allLines.push_back({a, hlColor});
        allLines.push_back({b, hlColor});
        allLines.push_back({b, hlColor});
        allLines.push_back({c, hlColor});
        allLines.push_back({c, hlColor});
        allLines.push_back({a, hlColor});
      }
    }

    app.renderer.setLines(allLines);

    drawPanel(&app);
    drawAppSettingsWindow(&app);
    drawProjectSettingsWindow(&app);

    // --- File browser (modal — drawn every frame while visible) ---
    if (app.fileBrowser.isVisible()) {
      if (app.fileBrowser.draw() && app.fileBrowser.confirmed()) {
        if (!app.loadingMesh) {
          const std::string path = app.fileBrowser.selectedPath().string();
          app.loadingMesh = true;
          app.status = "Loading " + path + "...";
          app.pendingLoad = std::async(std::launch::async, [path]() {
            AppState::LoadResult result;
            result.path = path;
            StlMesh loaded;
            std::string err;
            if (!loaded.loadFromFile(path, err)) {
              result.success = false;
              result.error = err;
              return result;
            }
            result.success = true;
            result.mesh = std::move(loaded);
            return result;
          });
        }
      }
    }

    // --- Export file browser ---
    if (app.exportBrowser.isVisible()) {
      if (app.exportBrowser.draw() && app.exportBrowser.confirmed()) {
        if (app.selectedObject >= 0 &&
            app.selectedObject < static_cast<int>(app.sceneObjects.size())) {
          std::string path = app.exportBrowser.selectedPath().string();
          // Ensure .stl extension.
          if (path.size() < 4 ||
              path.substr(path.size() - 4) != ".stl") {
            path += ".stl";
          }
          std::string err;
          // Scale from internal mm to the project's export unit.
          const float scale = fromMm(1.0f, app.project.exportUnit);
          if (app.sceneObjects[app.selectedObject].saveAsBinaryScaled(path, scale, err)) {
            app.status = "Exported " + path + " (" + unitSuffix(app.project.exportUnit) + ")";
          } else {
            app.status = "Export failed: " + err;
          }
        }
      }
    }

    // --- Rubber-band selection overlay ---
    if (app.dragSelecting) {
      ImDrawList* dl = ImGui::GetForegroundDrawList();
      ImVec2 p1(app.dragStartScreen.x, app.dragStartScreen.y);
      ImVec2 p2(app.dragCurScreen.x, app.dragCurScreen.y);
      dl->AddRectFilled(p1, p2, IM_COL32(0, 120, 255, 40));
      dl->AddRect(p1, p2, IM_COL32(0, 120, 255, 200), 0.0f, 0, 1.5f);
    }

    if (app.sceneMode == SceneMode::Sketch) {
      ImDrawList* dl = ImGui::GetForegroundDrawList();
      const ImVec2 viewportSize = ImGui::GetIO().DisplaySize;
      const glm::mat4 view = app.camera.viewMatrix();
      const glm::mat4 proj = app.camera.projectionMatrix(app.renderer.framebufferAspect());

      for (const auto& profile : filledProfiles) {
        const auto tris = profile::triangulate2D(profile);
        for (const auto& tri : tris) {
          auto a = projectToScreen(toWorld(profile[tri[0]], app.activePlane), view, proj,
                                   viewportSize);
          auto b = projectToScreen(toWorld(profile[tri[1]], app.activePlane), view, proj,
                                   viewportSize);
          auto c = projectToScreen(toWorld(profile[tri[2]], app.activePlane), view, proj,
                                   viewportSize);
          if (!a || !b || !c) continue;
          dl->AddTriangleFilled(*a, *b, *c, IM_COL32(90, 170, 255, 28));
        }
      }

      for (const glm::vec2& pt : danglingPoints) {
        auto screenPos = projectToScreen(toWorld(pt, app.activePlane), view, proj, viewportSize);
        if (!screenPos) continue;
        dl->AddCircleFilled(*screenPos, 4.0f, IM_COL32(230, 60, 60, 255), 16);
      }

      for (const auto& label : dimensionLabels) {
        auto screenPos = projectToScreen(label.worldPos, view, proj, viewportSize);
        if (!screenPos) continue;
        dl->AddText(*screenPos, IM_COL32(235, 235, 170, 255), label.text.c_str());
      }
    }

    ImGui::Render();

    std::string frameError;
    if (!app.renderer.drawFrame(app.camera.viewMatrix(),
                                app.camera.projectionMatrix(app.renderer.framebufferAspect()),
                                ImGui::GetDrawData(), frameError)) {
      std::cerr << "Draw failed: " << frameError << "\n";
      break;
    }
  }

  if (app.loadingMesh && app.pendingLoad.valid()) {
    app.pendingLoad.wait();
    app.loadingMesh = false;
  }

  app.renderer.waitIdle();
  app.renderer.cleanup();

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
