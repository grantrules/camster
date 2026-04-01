#include <array>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#  include <unistd.h>
#  include <sys/types.h>
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>

#include "AppSettings.hpp"
#include "CameraController.hpp"
#include "ColorVertex.hpp"
#include "Gizmo.hpp"
#include "History.hpp"
#include "Project.hpp"
#include "ProjectTypes.hpp"
#include "PrintSettings.hpp"
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
#include "ui/PrintWindow.hpp"
#include "core/AppLogic.hpp"
#include "ui/UIWindows.hpp"
#include "core/AppState.hpp"

namespace {
void framebufferResizeCallback(GLFWwindow* window, int, int) {
  auto* app = static_cast<AppState*>(glfwGetWindowUserPointer(window));
  if (app) {
    app->renderer.markFramebufferResized();
  }
}

// Launch an external slicer with the given STL file.  The slicer runs
// independently; we do not wait for it to exit.
bool launchSlicer(const std::string& slicerPath, const std::string& stlPath) {
#ifdef _WIN32
  // Windows: spawn via cmd /c to avoid blocking the main process.
  std::string cmd = "\"\"" + slicerPath + "\" \"" + stlPath + "\"\"";
  return std::system(cmd.c_str()) == 0;
#else
  pid_t pid = ::fork();
  if (pid < 0) return false;
  if (pid == 0) {
    // Child: exec the slicer detached from this process.
    const char* args[] = {slicerPath.c_str(), stlPath.c_str(), nullptr};
    ::execvp(slicerPath.c_str(), const_cast<char* const*>(args));
    ::_exit(1);  // exec failed
  }
  return true;  // parent continues immediately
#endif
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

void appendPlaneOutline(std::vector<ColorVertex>& lines, SketchPlane plane,
                        float offsetMm, float halfExtent, const glm::vec4& color) {
  const glm::vec2 p0{-halfExtent, -halfExtent};
  const glm::vec2 p1{halfExtent, -halfExtent};
  const glm::vec2 p2{halfExtent, halfExtent};
  const glm::vec2 p3{-halfExtent, halfExtent};
  lines.push_back({toWorld(p0, plane, offsetMm), color});
  lines.push_back({toWorld(p1, plane, offsetMm), color});
  lines.push_back({toWorld(p1, plane, offsetMm), color});
  lines.push_back({toWorld(p2, plane, offsetMm), color});
  lines.push_back({toWorld(p2, plane, offsetMm), color});
  lines.push_back({toWorld(p3, plane, offsetMm), color});
  lines.push_back({toWorld(p3, plane, offsetMm), color});
  lines.push_back({toWorld(p0, plane, offsetMm), color});
}

int faceSignForPlaneNormal(glm::vec3 normal, SketchPlane plane) {
  switch (plane) {
    case SketchPlane::XY: return normal.z >= 0.0f ? 1 : -1;
    case SketchPlane::XZ: return normal.y >= 0.0f ? 1 : -1;
    case SketchPlane::YZ: return normal.x >= 0.0f ? 1 : -1;
  }
  return 1;
}

struct EdgePickResult {
  bool hit = false;
  ChamferEdgeSelection edge;
  float distance = std::numeric_limits<float>::max();
};

float raySegmentDistance(glm::vec3 rayO, glm::vec3 rayD,
                         glm::vec3 a, glm::vec3 b, float* rayTOut) {
  const glm::vec3 u = rayD;
  const glm::vec3 v = b - a;
  const glm::vec3 w0 = rayO - a;

  const float aDot = glm::dot(u, u);
  const float bDot = glm::dot(u, v);
  const float cDot = glm::dot(v, v);
  const float dDot = glm::dot(u, w0);
  const float eDot = glm::dot(v, w0);
  const float denom = aDot * cDot - bDot * bDot;

  float s = 0.0f;
  float t = 0.0f;
  if (std::abs(denom) > 1e-8f) {
    s = (bDot * eDot - cDot * dDot) / denom;
    t = (aDot * eDot - bDot * dDot) / denom;
  }

  s = std::max(0.0f, s);
  t = std::clamp(t, 0.0f, 1.0f);

  const glm::vec3 pRay = rayO + s * u;
  const glm::vec3 pSeg = a + t * v;
  if (rayTOut) *rayTOut = s;
  return glm::length(pRay - pSeg);
}

EdgePickResult pickChamferEdge(const AppState& app, int objectIndex, glm::vec3 rayO, glm::vec3 rayD) {
  EdgePickResult best;
  if (objectIndex < 0 || objectIndex >= static_cast<int>(app.sceneObjects.size())) return best;

  const auto& mesh = app.sceneObjects[objectIndex];
  const auto& verts = mesh.vertices();
  const auto& inds = mesh.indices();
  if (verts.empty() || inds.empty()) return best;

  glm::vec3 bmin = verts[0].position;
  glm::vec3 bmax = verts[0].position;
  for (const auto& v : verts) {
    bmin = glm::min(bmin, v.position);
    bmax = glm::max(bmax, v.position);
  }
  const float pickThreshold = std::max(0.5f, glm::length(bmax - bmin) * 0.01f);

  struct QuantEdgeKey {
    long long ax, ay, az;
    long long bx, by, bz;
  };
  auto q = [](float v) { return static_cast<long long>(std::llround(v * 1000.0f)); };
  auto makeKey = [&](glm::vec3 a, glm::vec3 b) {
    bool swap = (a.x > b.x) ||
                (a.x == b.x && a.y > b.y) ||
                (a.x == b.x && a.y == b.y && a.z > b.z);
    if (swap) std::swap(a, b);
    return QuantEdgeKey{q(a.x), q(a.y), q(a.z), q(b.x), q(b.y), q(b.z)};
  };
  auto sameKey = [](const QuantEdgeKey& lhs, const QuantEdgeKey& rhs) {
    return lhs.ax == rhs.ax && lhs.ay == rhs.ay && lhs.az == rhs.az &&
           lhs.bx == rhs.bx && lhs.by == rhs.by && lhs.bz == rhs.bz;
  };

  std::vector<QuantEdgeKey> seen;
  seen.reserve(inds.size());

  auto testEdge = [&](glm::vec3 a, glm::vec3 b) {
    const QuantEdgeKey key = makeKey(a, b);
    for (const auto& k : seen) {
      if (sameKey(k, key)) return;
    }
    seen.push_back(key);

    float rayT = 0.0f;
    const float d = raySegmentDistance(rayO, rayD, a, b, &rayT);
    if (rayT > 0.0f && d <= pickThreshold && d < best.distance) {
      best.hit = true;
      best.distance = d;
      best.edge.objectIndex = objectIndex;
      best.edge.a = a;
      best.edge.b = b;
    }
  };

  for (size_t i = 0; i + 2 < inds.size(); i += 3) {
    const glm::vec3 a = verts[inds[i + 0]].position;
    const glm::vec3 b = verts[inds[i + 1]].position;
    const glm::vec3 c = verts[inds[i + 2]].position;
    testEdge(a, b);
    testEdge(b, c);
    testEdge(c, a);
  }
  return best;
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
  clearSketches(&app);
  initializeDefaultPlanes(&app);

  std::string error;
  if (!app.renderer.initialize(window, error)) {
    std::cerr << "Renderer initialization failed: " << error << "\n";
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  app.appSettings.load();  // load saved app settings (no-op if file missing)
  app.project.initFromAppSettings(app.appSettings);
  app.printSettings.load();

  app.selectedObject = -1;
  app.browserSelectedObjects.clear();
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
        clearSceneObjects(&app);
        appendSceneObject(&app, std::move(result.mesh));
        app.selectedObject = 0;
        app.browserSelectedObjects.assign(1, 0);
        rebuildCombinedMesh(&app);
        app.status = "Loaded " + result.path;
      }
    }

    syncSketchPlanes(&app);

    // Keep gizmo and small plane markers roughly constant on screen (~1 inch).
    {
      int fbW = 0, fbH = 0;
      glfwGetFramebufferSize(window, &fbW, &fbH);
      const glm::mat4 view = app.camera.viewMatrix();
      const glm::vec4 originInView = view * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
      const float distToOrigin = std::max(0.001f, std::abs(originInView.z));
      constexpr float fovY = 45.0f * (3.14159265358979323846f / 180.0f);
      const float worldPerPixel = (2.0f * distToOrigin * std::tan(fovY * 0.5f)) /
                                  static_cast<float>(std::max(1, fbH));
      const float desiredPixels = 32.0f;
      const float desiredPlaneSize = worldPerPixel * desiredPixels;
      const float targetScale = std::clamp(desiredPlaneSize / Gizmo::kPlaneSize,
                                           0.01f, 10000000.0f);

      if (!app.gizmoScaleInitialized) {
        app.gizmoScaleSmoothed = targetScale;
        app.gizmoScaleInitialized = true;
      } else {
        const float current = app.gizmoScaleSmoothed;
        const float relDelta = std::abs(targetScale - current) /
                               std::max(1e-6f, targetScale);
        if (relDelta > 0.002f) {
          // Low-pass filter to remove tiny zoom-step jitter.
          app.gizmoScaleSmoothed = current + (targetScale - current) * 0.22f;
        }
      }

      app.gizmo.setScale(app.gizmoScaleSmoothed);
    }

    app.renderer.beginImGuiFrame();

    // --- Menu bar (always visible) ---
    drawMenuBar(&app);
    drawSolidToolbar(&app);

    // --- Toolbar (sketch mode only) ---
    ToolbarAction toolbarAction;
    if (app.sceneMode == SceneMode::Sketch && app.hasActiveSketch()) {
      toolbarAction =
          app.toolbar.draw(app.sketchTool, app.extrudeTool,
                           app.activeSketch().hasSelection(), app.project.defaultUnit);
    }

    if (toolbarAction.exitSketchRequested &&
        app.sceneMode == SceneMode::Sketch && app.hasActiveSketch()) {
      exitSketchMode(&app);
      app.status = "Exited sketch mode";
    }

    // Handle "Extrude" button press: gather profiles from selection.
    if (toolbarAction.extrudeRequested && !app.extrudeTool.active() && app.hasActiveSketch()) {
      if (!app.activeSketchVisible()) {
        app.status = "Active sketch is hidden";
      } else if (app.activeSketchLocked()) {
        app.status = "Active sketch is locked";
      } else {
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
        app.extrudeTool.begin(std::move(profiles), app.activePlane());
        app.camera.snap(CameraController::Orientation::Isometric);
        app.extrudeOptions.visible = true;
        app.extrudeOptions.applyRequested = false;
        app.extrudeOptions.operation = ExtrudeOp::Add;
        app.extrudeOptions.targets.clear();
        if (app.selectedObject >= 0) {
          addUniqueIndex(app.extrudeOptions.targets, app.selectedObject);
        }
        std::snprintf(app.extrudeOptions.depthBuffer,
                      sizeof(app.extrudeOptions.depthBuffer),
                      "%.3f", fromMm(app.extrudeTool.distance(), app.project.defaultUnit));
        app.objectPickMode = ObjectPickMode::None;
        app.status = "Set depth/op and target objects in Extrude Options";
      } else {
        app.status = "Selection does not form a closed profile";
      }
      }
    }

    // Handle delete request.
    if (toolbarAction.deleteRequested) {
      if (app.activeSketchLocked()) {
        app.status = "Active sketch is locked";
      } else {
        app.activeSketch().deleteSelected();
        app.status = "Deleted selected elements";
      }
    }

    // Handle construction toggle.
    if (toolbarAction.toggleConstruction) {
      if (app.activeSketchLocked()) {
        app.status = "Active sketch is locked";
      } else {
        for (size_t idx : app.activeSketch().selectedIndices()) {
          app.activeSketch().toggleConstruction(idx);
        }
        app.status = "Toggled construction";
      }
    }

    // Handle constraint requests from toolbar.
    if (toolbarAction.constraintRequested != ConstraintTool::None) {
      if (app.activeSketchLocked()) {
        app.status = "Active sketch is locked";
      } else {
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
    }

    // Handle extrude confirm.
    if (toolbarAction.extrudeConfirmed && app.extrudeTool.active()) {
      app.extrudeOptions.visible = true;
      app.extrudeOptions.applyRequested = true;
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

      if (app.sceneMode == SceneMode::View3D || app.sketchCreate.open || app.planeCreate.open) {
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        const glm::mat4 view = app.camera.viewMatrix(); 
        const glm::mat4 proj = app.camera.projectionMatrix(app.renderer.framebufferAspect());
        glm::vec3 rayO, rayD;
        screenToRay(static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(fbW), static_cast<float>(fbH),
                    view, proj, rayO, rayD);
        app.hoveredPlaneId.reset();
        if (auto planeHit = app.gizmo.rayHitsPlaneIndicator(rayO, rayD)) {
          app.hoveredPlaneId = defaultPlaneId(*planeHit);
        }
      } else {
        app.hoveredPlaneId.reset();
      }

      if (app.planeCreate.open && app.planeCreate.source == PlaneReferenceSource::Face) {
        if (leftDown && !app.wasLeftDown) {
          app.dragStartScreen = {static_cast<float>(x), static_cast<float>(y)};
        } else if (!leftDown && app.wasLeftDown) {
          const glm::vec2 cur(static_cast<float>(x), static_cast<float>(y));
          if (glm::dot(cur - app.dragStartScreen, cur - app.dragStartScreen) < kDragThresholdSq) {
            int fbW = 0, fbH = 0;
            glfwGetFramebufferSize(window, &fbW, &fbH);
            const glm::mat4 view = app.camera.viewMatrix();
            const glm::mat4 proj = app.camera.projectionMatrix(app.renderer.framebufferAspect());
            glm::vec3 rayO, rayD;
            screenToRay(static_cast<float>(x), static_cast<float>(y),
                        static_cast<float>(fbW), static_cast<float>(fbH),
                        view, proj, rayO, rayD);

            if (auto face = pickObjectFace(app, rayO, rayD)) {
              const SketchPlane facePlane = sketchPlaneFromNormal(face->normal);
              app.planeCreate.sourceObject = face->objectIndex;
              app.planeCreate.sourceFacePlane = facePlane;
              app.planeCreate.sourceFaceSign = faceSignForPlaneNormal(face->normal, facePlane);
              app.partialSelectedObject = face->objectIndex;
              app.status = "Face selected for new plane";
            } else {
              app.status = "No face hit. Click a visible object face";
            }
          }
        }
      } else if (app.extrudeTool.active()) {
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

      } else if (app.sceneMode == SceneMode::Sketch && app.hasActiveSketch() &&
                 app.sketchTool.activeTool() != Tool::None &&
                 app.activeSketchVisible() && !app.activeSketchLocked()) {
        // --- Drawing mode: feed mouse to SketchTool ---
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        const glm::mat4 view = app.camera.viewMatrix();
        const glm::mat4 proj = app.camera.projectionMatrix(app.renderer.framebufferAspect());

        auto hit = rayPlaneHit(static_cast<float>(x), static_cast<float>(y),
                               static_cast<float>(fbW), static_cast<float>(fbH),
                               view, proj, app.activePlane(), app.activePlaneOffset());
        if (hit) {
          glm::vec2 planePos = toPlane(*hit, app.activePlane());

          // Snap to existing control points.
          auto snap = app.activeSketch().snapToPoint(planePos, kSnapThreshold);
          if (snap) planePos = *snap;

          app.sketchTool.mouseMove(planePos);

          if (leftDown && !app.wasLeftDown) {
            app.sketchTool.mouseClick(planePos);
          }
        }

      } else if (app.sceneMode == SceneMode::Sketch && app.hasActiveSketch() &&
                 app.sketchTool.activeTool() == Tool::None &&
                 app.activeSketchVisible() && !app.activeSketchLocked()) {
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
                                    view, proj, app.activePlane(), app.activePlaneOffset());
            auto hitB = rayPlaneHit(app.dragCurScreen.x, app.dragCurScreen.y,
                                    static_cast<float>(fbW), static_cast<float>(fbH),
                                    view, proj, app.activePlane(), app.activePlaneOffset());
            if (hitA && hitB) {
              glm::vec2 pA = toPlane(*hitA, app.activePlane());
              glm::vec2 pB = toPlane(*hitB, app.activePlane());
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
                                   view, proj, app.activePlane(), app.activePlaneOffset());
            if (hit) {
              glm::vec2 planePos = toPlane(*hit, app.activePlane());
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

      } else if (app.objectPickMode != ObjectPickMode::None) {
        // --- Object-pick mode for Extrude/Combine lists ---
        if (leftDown && !app.wasLeftDown) {
          app.dragStartScreen = {static_cast<float>(x), static_cast<float>(y)};
        } else if (!leftDown && app.wasLeftDown) {
          const glm::vec2 cur(static_cast<float>(x), static_cast<float>(y));
          if (glm::dot(cur - app.dragStartScreen, cur - app.dragStartScreen) <
              kDragThresholdSq) {
            int fbW = 0, fbH = 0;
            glfwGetFramebufferSize(window, &fbW, &fbH);
            const glm::mat4 view = app.camera.viewMatrix();
            const glm::mat4 proj =
                app.camera.projectionMatrix(app.renderer.framebufferAspect());

            glm::vec3 rayO, rayD;
            screenToRay(static_cast<float>(x), static_cast<float>(y),
                        static_cast<float>(fbW), static_cast<float>(fbH),
                        view, proj, rayO, rayD);

            const int hit = pickObject(app, rayO, rayD);
            if (app.objectPickMode == ObjectPickMode::ChamferEdges) {
              const int obj = app.chamferOptions.targetObject >= 0
                                  ? app.chamferOptions.targetObject
                                  : app.selectedObject;
              const EdgePickResult edgePick = pickChamferEdge(app, obj, rayO, rayD);
              if (edgePick.hit) {
                auto sameEdge = [](const ChamferEdgeSelection& lhs,
                                   const ChamferEdgeSelection& rhs) {
                  const auto near = [](glm::vec3 p, glm::vec3 q) {
                    const glm::vec3 d = p - q;
                    return glm::dot(d, d) <= 1e-6f;
                  };
                  return lhs.objectIndex == rhs.objectIndex &&
                         ((near(lhs.a, rhs.a) && near(lhs.b, rhs.b)) ||
                          (near(lhs.a, rhs.b) && near(lhs.b, rhs.a)));
                };

                bool removed = false;
                for (auto it = app.chamferOptions.edges.begin();
                     it != app.chamferOptions.edges.end(); ++it) {
                  if (sameEdge(*it, edgePick.edge)) {
                    app.chamferOptions.edges.erase(it);
                    removed = true;
                    app.status = "Chamfer edge removed";
                    break;
                  }
                }
                if (!removed) {
                  app.chamferOptions.edges.push_back(edgePick.edge);
                  app.status = "Chamfer edge selected";
                }
              }
            } else if (hit >= 0) {
              if (app.objectPickMode == ObjectPickMode::ExtrudeTargets) {
                addUniqueIndex(app.extrudeOptions.targets, hit);
                app.status = "Added object to extrude target list";
              } else if (app.objectPickMode == ObjectPickMode::CombineTargets) {
                addUniqueIndex(app.combineOptions.targets, hit);
                eraseIndex(app.combineOptions.tools, hit);
                app.status = "Added object to combine target list";
              } else if (app.objectPickMode == ObjectPickMode::CombineTools) {
                addUniqueIndex(app.combineOptions.tools, hit);
                eraseIndex(app.combineOptions.targets, hit);
                app.status = "Added object to combine tool list";
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

            bool planeSelected = false;
            if (auto planeHit = app.gizmo.rayHitsPlaneIndicator(rayO, rayD)) {
              const int planeId = defaultPlaneId(*planeHit);
              const int planeIndex = findPlaneIndexById(&app, planeId);
              if (planeIndex >= 0) {
                app.browserSelectedPlanes.assign(1, planeIndex);
                app.planeSelectionAnchor = planeIndex;
                syncSelectedPlaneFromBrowser(&app);
                app.sketchCreate.selectedPlaneId = planeId;
                app.planeCreate.sourcePlaneId = planeId;
                app.status = "Plane selected";
                planeSelected = true;
              }
            }

            if (!planeSelected) {
              int hit = pickObject(app, rayO, rayD);
              app.selectedObject = hit;
              if (hit >= 0) {
                app.browserSelectedObjects.assign(1, hit);
                syncSelectedObjectFromBrowser(&app);
                app.status = "Object selected (File > Export STL to save)";
              } else {
                app.browserSelectedObjects.clear();
                syncSelectedObjectFromBrowser(&app);
                app.status = "Ready";
              }
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
    if (!io.WantCaptureKeyboard && glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      if (app.objectPickMode != ObjectPickMode::None) {
        app.objectPickMode = ObjectPickMode::None;
        app.status = "Selection mode cancelled";
      } else if (app.extrudeTool.active()) {
        app.extrudeTool.cancel();
        app.extrudeOptions.visible = false;
        app.status = "Extrude cancelled";
      } else if (app.sketchTool.activeTool() != Tool::None) {
        app.sketchTool.cancel();
      } else if (app.sceneMode == SceneMode::Sketch && app.hasActiveSketch()) {
        exitSketchMode(&app);
      }
    }

    // Delete key: remove selected sketch elements.
    if (!io.WantCaptureKeyboard && app.sceneMode == SceneMode::Sketch && app.hasActiveSketch() &&
        (glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS ||
         glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS)) {
      if (app.activeSketchLocked()) {
        app.status = "Active sketch is locked";
      } else if (app.activeSketch().hasSelection()) {
        app.activeSketch().deleteSelected();
        app.status = "Deleted selected elements";
      }
    }

    // Ctrl+Z / Ctrl+Shift+Z: sketch undo / redo.
    if (!io.WantCaptureKeyboard && app.sceneMode == SceneMode::Sketch && app.hasActiveSketch() &&
        !app.activeSketchLocked() &&
        (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
         glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) &&
        glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS) {
      const bool shift = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                          glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
      if (shift && app.activeSketch().canRedo()) {
        app.activeSketch().redo();
        app.status = "Redo";
      } else if (!shift && app.activeSketch().canUndo()) {
        app.activeSketch().undo();
        app.status = "Undo";
      }
    }

    // Right-click context menu (sketch mode).
    if (app.sceneMode == SceneMode::Sketch && app.hasActiveSketch() && !app.extrudeTool.active() &&
        app.activeSketchVisible()) {
      if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS &&
          !io.WantCaptureMouse) {
        ImGui::OpenPopup("##sketchCtx");
      }
      if (ImGui::BeginPopup("##sketchCtx")) {
        const bool sketchUnlocked = !app.activeSketchLocked();
        if (ImGui::MenuItem("Select", nullptr, false, true)) {
          app.sketchTool.setTool(Tool::None);
        }
        if (ImGui::MenuItem("Line", nullptr, false, sketchUnlocked)) app.sketchTool.setTool(Tool::Line);
        if (ImGui::MenuItem("Rectangle", nullptr, false, sketchUnlocked)) app.sketchTool.setTool(Tool::Rectangle);
        if (ImGui::MenuItem("Circle", nullptr, false, sketchUnlocked)) app.sketchTool.setTool(Tool::Circle);
        if (ImGui::MenuItem("Arc", nullptr, false, sketchUnlocked)) app.sketchTool.setTool(Tool::Arc);
        ImGui::Separator();
        bool hasSel = app.activeSketch().hasSelection() && sketchUnlocked;
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
    if (app.hasActiveSketch()) {
      if (auto prim = app.sketchTool.takeResult()) {
      app.activeSketch().addCompletedPrimitive(std::move(*prim));
      }
    }

    // --- Build line data every frame ---
    std::vector<ColorVertex> allLines;
    std::vector<SketchDimensionLabel> dimensionLabels;
    std::vector<std::vector<glm::vec2>> filledProfiles;
    std::vector<glm::vec2> danglingPoints;
    app.gizmo.appendLines(allLines);

    const float planeHalfExtent = app.gizmo.planeSize();
    for (int i = 0; i < static_cast<int>(app.planes.size()); ++i) {
      const auto& planeEntry = app.planes[i];
      const ResolvedPlane resolved = resolvePlane(&app, planeEntry.id);
      if (!resolved.valid) continue;
      if (!planeEntry.meta.visible) continue;
      const glm::vec4 planeColor = planeEntry.meta.builtIn
                                       ? glm::vec4(0.45f, 0.45f, 0.45f, 0.8f)
                                       : glm::vec4(0.55f, 0.65f, 0.85f, 0.75f);
      appendPlaneOutline(allLines, resolved.plane, resolved.offsetMm, planeHalfExtent,
                         planeColor);
    }

    // Highlight hovered default plane indicator near the gizmo.
    if (app.hoveredPlaneId) {
      const ResolvedPlane hovered = resolvePlane(&app, *app.hoveredPlaneId);
      if (hovered.valid) {
        app.gizmo.appendHighlightedPlane(allLines, hovered.plane);
      }
    }

    std::vector<int> highlightedPlaneIds;
    auto addPlaneHighlight = [&](int planeId) {
      if (planeId < 0) return;
      if (std::find(highlightedPlaneIds.begin(), highlightedPlaneIds.end(), planeId) ==
          highlightedPlaneIds.end()) {
        highlightedPlaneIds.push_back(planeId);
      }
    };
    if (app.sketchCreate.open) addPlaneHighlight(app.sketchCreate.selectedPlaneId);
    if (app.planeCreate.open && app.planeCreate.source == PlaneReferenceSource::Plane) {
      addPlaneHighlight(app.planeCreate.sourcePlaneId);
    }
    for (int planeIndex : app.browserSelectedPlanes) {
      if (planeIndex >= 0 && planeIndex < static_cast<int>(app.planes.size())) {
        addPlaneHighlight(app.planes[planeIndex].id);
      }
    }
    for (int planeId : highlightedPlaneIds) {
      const ResolvedPlane resolved = resolvePlane(&app, planeId);
      if (!resolved.valid) continue;
      appendPlaneOutline(allLines, resolved.plane, resolved.offsetMm,
                         planeHalfExtent * 1.15f,
                         glm::vec4(1.0f, 0.82f, 0.25f, 1.0f));
    }

    const int maxVisibleSketchIndex =
        (app.sceneMode == SceneMode::Sketch && app.hasActiveSketch())
            ? app.activeSketchIndex
            : static_cast<int>(app.sketches.size()) - 1;
    for (int i = 0; i <= maxVisibleSketchIndex; ++i) {
      const auto& entry = app.sketches[i];
      if (!entry.meta.visible) continue;
      std::vector<ColorVertex> sketchLines;
      entry.sketch.appendLines(sketchLines, entry.plane);
      entry.sketch.appendConstraintAnnotations(sketchLines, entry.plane);
      for (auto& line : sketchLines) {
        line.position += planeNormal(entry.plane) * entry.offsetMm;
        allLines.push_back(line);
      }
    }

    if (app.sceneMode == SceneMode::Sketch && app.hasActiveSketch() && app.activeSketchVisible()) {
      appendGrid(allLines, app.activePlane(), app.project.gridExtent, app.project.gridSpacing);
      app.activeSketch().appendConstraintLabels(dimensionLabels, app.activePlane(),
                                                app.project.defaultUnit);
      for (auto& label : dimensionLabels) {
        label.worldPos += planeNormal(app.activePlane()) * app.activePlaneOffset();
      }
      filledProfiles = app.activeSketch().closedProfiles();
      danglingPoints = app.activeSketch().danglingEndpoints();
      app.sketchTool.appendPreview(allLines, app.activePlane());

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
                               view, proj, app.activePlane(), app.activePlaneOffset());
        if (hit) {
          glm::vec2 pp = toPlane(*hit, app.activePlane());
          auto snap = app.activeSketch().snapToPoint(pp, kSnapThreshold);
          if (snap) {
            const float sz = kSnapIndicatorSize;
            const glm::vec4 snapColor{0.0f, 1.0f, 1.0f, 1.0f};
            glm::vec2 s = *snap;
            allLines.push_back({toWorld(s + glm::vec2(-sz, 0.0f), app.activePlane(), app.activePlaneOffset()), snapColor});
            allLines.push_back({toWorld(s + glm::vec2(sz, 0.0f), app.activePlane(), app.activePlaneOffset()), snapColor});
            allLines.push_back({toWorld(s + glm::vec2(0.0f, -sz), app.activePlane(), app.activePlaneOffset()), snapColor});
            allLines.push_back({toWorld(s + glm::vec2(0.0f, sz), app.activePlane(), app.activePlaneOffset()), snapColor});
          }
        }
      }
    }

    // Extrude preview lines are sketch-mode only.
    if (app.sceneMode == SceneMode::Sketch && app.hasActiveSketch()) {
      app.extrudeTool.appendPreview(allLines);
    }

    // Highlight selected 3D object with cyan wireframe overlay.
    std::vector<int> highlightObjects = app.browserSelectedObjects;
    sanitizeObjectIndices(highlightObjects, static_cast<int>(app.sceneObjects.size()));
    if (highlightObjects.empty() && app.selectedObject >= 0) {
      highlightObjects.push_back(app.selectedObject);
    }
    if (app.partialSelectedObject >= 0 && !hasIndex(highlightObjects, app.partialSelectedObject)) {
      highlightObjects.push_back(app.partialSelectedObject);
    }
    for (int selectedIdx : highlightObjects) {
      if (selectedIdx >= static_cast<int>(app.sceneObjectMeta.size()) ||
          !app.sceneObjectMeta[selectedIdx].visible) {
        continue;
      }
      const auto& obj = app.sceneObjects[selectedIdx];
      const auto& verts = obj.vertices();
      const auto& inds = obj.indices();
      const bool partial = selectedIdx == app.partialSelectedObject;
      const glm::vec4 hlColor = partial ? glm::vec4(0.4f, 0.6f, 0.7f, 0.8f)
                    : glm::vec4(0.0f, 0.8f, 1.0f, 1.0f);
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
    drawObjectBrowserWindow(&app);
    drawTimelineWindow(&app);
    drawSolidExtrudeWindow(&app);
    drawNewPlaneWindow(&app);
    drawNewSketchWindow(&app);
    drawProjectToolWindow(&app);
    drawAppSettingsWindow(&app);
    drawProjectSettingsWindow(&app);
    drawExtrudeOptionsWindow(&app);
    drawCombineWindow(&app);
    drawChamferWindow(&app);

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

    // --- 3D Print window ---
    if (app.printWindow.isVisible()) {
      if (app.printWindow.draw(app.sceneObjects, app.printSettings)) {
        // User clicked Print: merge selected objects into a temp STL and
        // launch the slicer.
        const auto& sel = app.printWindow.selectedObjects();
        StlMesh exportMesh;
        for (int i = 0; i < static_cast<int>(app.sceneObjects.size()); ++i) {
          if (i < static_cast<int>(sel.size()) && sel[i])
            exportMesh.append(app.sceneObjects[i]);
        }
        if (!exportMesh.empty()) {
          const auto tmpPath =
              (std::filesystem::temp_directory_path() / "camster_print.stl").string();
          std::string err;
          const bool exported = exportMesh.saveAsBinary(tmpPath, err);
          if (!exported) {
            app.status = "3D Print: export failed: " + err;
          } else {
            app.printSettings.addRecent(app.printWindow.currentSlicer());
            app.printSettings.save();
            if (!launchSlicer(app.printWindow.currentSlicer(), tmpPath)) {
              app.status = "3D Print: failed to launch slicer";
            } else {
              app.status = "Opened in slicer: " + app.printWindow.currentSlicer();
            }
          }
        }
      }
    }

    // --- Slicer file browser (opened by PrintWindow "Browse" button) ---
    if (app.printWindow.slicerBrowseRequested()) {
      app.printWindow.consumeBrowseRequest();
      app.slicerBrowser.show({}, {}, "Select Slicer Executable", "Select");
    }
    if (app.slicerBrowser.isVisible()) {
      if (app.slicerBrowser.draw() && app.slicerBrowser.confirmed()) {
        const std::string path = app.slicerBrowser.selectedPath().string();
        app.printWindow.setSlicerPath(path);
        app.printSettings.addRecent(path);
        app.printSettings.save();
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

    if (app.sceneMode == SceneMode::Sketch && app.hasActiveSketch() && app.activeSketchVisible()) {
      ImDrawList* dl = ImGui::GetForegroundDrawList();
      const ImVec2 viewportSize = ImGui::GetIO().DisplaySize;
      const glm::mat4 view = app.camera.viewMatrix();
      const glm::mat4 proj = app.camera.projectionMatrix(app.renderer.framebufferAspect());

      for (const auto& profile : filledProfiles) {
        const auto tris = profile::triangulate2D(profile);
        for (const auto& tri : tris) {
          auto a = projectToScreen(toWorld(profile[tri[0]], app.activePlane(), app.activePlaneOffset()), view, proj,
                                   viewportSize);
          auto b = projectToScreen(toWorld(profile[tri[1]], app.activePlane(), app.activePlaneOffset()), view, proj,
                                   viewportSize);
          auto c = projectToScreen(toWorld(profile[tri[2]], app.activePlane(), app.activePlaneOffset()), view, proj,
                                   viewportSize);
          if (!a || !b || !c) continue;
          dl->AddTriangleFilled(*a, *b, *c, IM_COL32(90, 170, 255, 28));
        }
      }

      for (const glm::vec2& pt : danglingPoints) {
        auto screenPos = projectToScreen(toWorld(pt, app.activePlane(), app.activePlaneOffset()), view, proj, viewportSize);
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
