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

namespace {
enum class BooleanOp { Add, Subtract };

enum class ObjectPickMode {
  None,
  ExtrudeTargets,
  CombineTargets,
  CombineTools,
};

enum class BrowserSection {
  Objects,
  Sketches,
};

struct ExtrudeOptionsState {
  bool visible = false;
  bool applyRequested = false;
  BooleanOp operation = BooleanOp::Add;
  std::vector<int> targets;
  char depthBuffer[64] = {};
};

struct CombineOptionsState {
  bool visible = false;
  BooleanOp operation = BooleanOp::Add;
  bool keepTools = false;
  std::vector<int> targets;
  std::vector<int> tools;
};

struct Aabb {
  glm::vec3 min{0.0f};
  glm::vec3 max{0.0f};
  bool valid = false;
};

struct SketchCreateState {
  bool open = false;
  bool pickFromScene = false;
  SketchPlane plane = SketchPlane::XY;
  float offsetMm = 0.0f;
  bool fromFace = false;
  int sourceObject = -1;
};

struct FacePickResult {
  int objectIndex = -1;
  int triangleOffset = -1;
  glm::vec3 hitPoint{0.0f};
  glm::vec3 normal{0.0f, 0.0f, 1.0f};
};

void addUniqueIndex(std::vector<int>& list, int idx) {
  if (idx < 0) return;
  if (std::find(list.begin(), list.end(), idx) == list.end()) {
    list.push_back(idx);
  }
}

void sanitizeObjectIndices(std::vector<int>& list, int objectCount) {
  list.erase(std::remove_if(list.begin(), list.end(), [objectCount](int idx) {
               return idx < 0 || idx >= objectCount;
             }),
             list.end());
  std::sort(list.begin(), list.end());
  list.erase(std::unique(list.begin(), list.end()), list.end());
}

void eraseIndex(std::vector<int>& list, int idx) {
  list.erase(std::remove(list.begin(), list.end(), idx), list.end());
}

Aabb meshAabb(const StlMesh& mesh) {
  Aabb box;
  const auto& verts = mesh.vertices();
  if (verts.empty()) return box;
  box.min = verts[0].position;
  box.max = verts[0].position;
  box.valid = true;
  for (const auto& v : verts) {
    box.min = glm::min(box.min, v.position);
    box.max = glm::max(box.max, v.position);
  }
  return box;
}

bool aabbOverlap(const Aabb& a, const Aabb& b) {
  if (!a.valid || !b.valid) return false;
  return a.min.x <= b.max.x && a.max.x >= b.min.x &&
         a.min.y <= b.max.y && a.max.y >= b.min.y &&
         a.min.z <= b.max.z && a.max.z >= b.min.z;
}

template <size_t N>
void setName(std::array<char, N>& dest, const std::string& value) {
  std::snprintf(dest.data(), dest.size(), "%s", value.c_str());
}

bool hasIndex(const std::vector<int>& list, int idx) {
  return std::find(list.begin(), list.end(), idx) != list.end();
}

void setSingleOrMultiSelection(std::vector<int>& list, int idx, bool multiSelect) {
  if (!multiSelect) {
    list.assign(1, idx);
    return;
  }
  if (hasIndex(list, idx)) {
    eraseIndex(list, idx);
  } else {
    list.push_back(idx);
  }
}

struct AppState {
  // 3D Print
  PrintSettings printSettings;
  PrintWindow   printWindow;
  FileBrowser   slicerBrowser;

  VulkanRenderer renderer;
  CameraController camera;
  StlMesh mesh;       // combined mesh sent to renderer
  FileBrowser fileBrowser;
  FileBrowser exportBrowser;  // separate browser instance for export

  // Individual mesh objects for selection / export.
  std::vector<StlMesh> sceneObjects;
  std::vector<ObjectMetadata> sceneObjectMeta;
  int selectedObject = -1;  // index into sceneObjects, -1 = none
  int nextObjectNumber = 1;
  std::vector<int> browserSelectedObjects;
  std::vector<int> browserSelectedSketches;
  int objectSelectionAnchor = -1;
  int sketchSelectionAnchor = -1;
  int renameObjectIndex = -1;
  int renameSketchIndex = -1;
  std::array<char, 64> renameBuffer{};
  bool focusRenameInput = false;
  BrowserSection browserFocusSection = BrowserSection::Objects;
  std::vector<int> pendingDeleteObjects;
  bool openDeleteObjectsPopup = false;
  ObjectPickMode objectPickMode = ObjectPickMode::None;
  ExtrudeOptionsState extrudeOptions;
  CombineOptionsState combineOptions;

  // Scene / sketch state.
  SceneMode sceneMode = SceneMode::View3D;
  std::vector<SketchEntry> sketches;
  int activeSketchIndex = -1;
  int nextSketchNumber = 1;
  SketchCreateState sketchCreate;
  int partialSelectedObject = -1;
  int projectSourceSketchIndex = -1;
  bool showProjectTool = false;
  std::vector<bool> projectSelectionMask;
  SketchTool sketchTool;
  ExtrudeTool extrudeTool;
  Gizmo gizmo;
  Toolbar toolbar;
  AppSettings appSettings;
  Project project;
  Timeline timeline;
  bool showAppSettings = false;
  bool showProjectSettings = false;

  bool hasActiveSketch() const {
    return activeSketchIndex >= 0 && activeSketchIndex < static_cast<int>(sketches.size());
  }
  Sketch& activeSketch() { return sketches[activeSketchIndex].sketch; }
  const Sketch& activeSketch() const { return sketches[activeSketchIndex].sketch; }
  SketchMetadata& activeSketchMeta() { return sketches[activeSketchIndex].meta; }
  const SketchMetadata& activeSketchMeta() const { return sketches[activeSketchIndex].meta; }
  SketchPlane activePlane() const {
    return hasActiveSketch() ? sketches[activeSketchIndex].plane : SketchPlane::XY;
  }
  float activePlaneOffset() const {
    return hasActiveSketch() ? sketches[activeSketchIndex].offsetMm : 0.0f;
  }
  bool activeSketchLocked() const { return hasActiveSketch() && activeSketchMeta().locked; }
  bool activeSketchVisible() const { return hasActiveSketch() && activeSketchMeta().visible; }

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

const char* browserRowLabel(bool visible, bool locked) {
  if (!visible && locked) return "[H][L]";
  if (!visible) return "[H]";
  if (locked) return "[L]";
  return "";
}

void rebuildCombinedMesh(AppState* app);
void snapCameraToPlane(AppState* app, SketchPlane plane);

void syncSelectedObjectFromBrowser(AppState* app) {
  sanitizeObjectIndices(app->browserSelectedObjects, static_cast<int>(app->sceneObjects.size()));
  if (app->browserSelectedObjects.empty()) {
    app->selectedObject = -1;
    app->renameObjectIndex = -1;
  } else {
    app->selectedObject = app->browserSelectedObjects.front();
    if (app->browserSelectedObjects.size() != 1 ||
        !hasIndex(app->browserSelectedObjects, app->renameObjectIndex)) {
      app->renameObjectIndex = -1;
    }
  }
}

void syncSelectedSketchFromBrowser(AppState* app) {
  sanitizeObjectIndices(app->browserSelectedSketches, static_cast<int>(app->sketches.size()));
  if (app->browserSelectedSketches.empty()) {
    app->renameSketchIndex = -1;
    return;
  }
  if (app->browserSelectedSketches.size() == 1) {
    app->activeSketchIndex = app->browserSelectedSketches.front();
    app->sceneMode = SceneMode::Sketch;
    if (app->hasActiveSketch()) {
      snapCameraToPlane(app, app->activePlane());
    }
  }
  if (app->browserSelectedSketches.size() != 1 ||
      !hasIndex(app->browserSelectedSketches, app->renameSketchIndex)) {
    app->renameSketchIndex = -1;
  }
}

void clearSketches(AppState* app) {
  app->sketches.clear();
  app->activeSketchIndex = -1;
  app->nextSketchNumber = 1;
  app->browserSelectedSketches.clear();
  app->sketchSelectionAnchor = -1;
  app->renameSketchIndex = -1;
  app->sketchTool.cancel();
  app->sketchTool.setTool(Tool::None);
  app->extrudeTool.cancel();
  app->showProjectTool = false;
  app->projectSelectionMask.clear();
  app->projectSourceSketchIndex = -1;
}

void snapCameraToPlane(AppState* app, SketchPlane plane) {
  switch (plane) {
    case SketchPlane::XY: app->camera.snap(CameraController::Orientation::Front); break;
    case SketchPlane::XZ: app->camera.snap(CameraController::Orientation::Top); break;
    case SketchPlane::YZ: app->camera.snap(CameraController::Orientation::Right); break;
  }
}

std::string objectName(const AppState* app, int idx) {
  if (idx >= 0 && idx < static_cast<int>(app->sceneObjectMeta.size()))
    return std::string(app->sceneObjectMeta[idx].name.data());
  return {};
}

std::vector<std::string> objectNames(const AppState* app, const std::vector<int>& indices) {
  std::vector<std::string> names;
  names.reserve(indices.size());
  for (int i : indices) names.push_back(objectName(app, i));
  return names;
}

void createSketch(AppState* app, SketchPlane plane, float offsetMm) {
  SketchEntry entry;
  setName(entry.meta.name, "Sketch " + std::to_string(app->nextSketchNumber++));
  entry.meta.visible = true;
  entry.meta.locked = false;
  entry.plane = plane;
  entry.offsetMm = offsetMm;

  CreateSketchAction action;
  action.plane = plane;
  action.offsetMm = offsetMm;
  action.name = std::string(entry.meta.name.data());
  app->timeline.push(std::move(action), "Create " + std::string(entry.meta.name.data()));

  app->sketches.push_back(std::move(entry));
  app->activeSketchIndex = static_cast<int>(app->sketches.size()) - 1;
  app->browserSelectedSketches.assign(1, app->activeSketchIndex);
  app->sketchSelectionAnchor = app->activeSketchIndex;
  app->sceneMode = SceneMode::Sketch;
  app->showProjectTool = true;
  app->partialSelectedObject = -1;
  snapCameraToPlane(app, plane);
}

void appendSceneObject(AppState* app, StlMesh mesh) {
  app->sceneObjects.push_back(std::move(mesh));
  ObjectMetadata meta;
  setName(meta.name, "Object " + std::to_string(app->nextObjectNumber++));
  meta.visible = true;
  meta.locked = false;
  app->sceneObjectMeta.push_back(meta);
}

void clearSceneObjects(AppState* app) {
  app->sceneObjects.clear();
  app->sceneObjectMeta.clear();
  app->browserSelectedObjects.clear();
  app->pendingDeleteObjects.clear();
  app->objectSelectionAnchor = -1;
  app->selectedObject = -1;
  app->nextObjectNumber = 1;
  app->renameObjectIndex = -1;
  app->renameSketchIndex = -1;
}

void setSelectionRange(std::vector<int>& selection, int start, int end) {
  selection.clear();
  if (start > end) std::swap(start, end);
  for (int i = start; i <= end; ++i) selection.push_back(i);
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

void beginObjectRename(AppState* app, int idx) {
  if (idx < 0 || idx >= static_cast<int>(app->sceneObjectMeta.size())) return;
  app->renameObjectIndex = idx;
  app->renameSketchIndex = -1;
  setName(app->renameBuffer, app->sceneObjectMeta[idx].name.data());
  app->focusRenameInput = true;
}

void beginSketchRename(AppState* app, int idx) {
  if (idx < 0 || idx >= static_cast<int>(app->sketches.size())) return;
  app->renameSketchIndex = idx;
  app->renameObjectIndex = -1;
  setName(app->renameBuffer, app->sketches[idx].meta.name.data());
  app->focusRenameInput = true;
}

void commitObjectRename(AppState* app) {
  if (app->renameObjectIndex < 0 ||
      app->renameObjectIndex >= static_cast<int>(app->sceneObjectMeta.size())) {
    app->renameObjectIndex = -1;
    return;
  }
  setName(app->sceneObjectMeta[app->renameObjectIndex].name, app->renameBuffer.data());
  app->renameObjectIndex = -1;
}

void commitSketchRename(AppState* app) {
  if (app->renameSketchIndex < 0 ||
      app->renameSketchIndex >= static_cast<int>(app->sketches.size())) {
    app->renameSketchIndex = -1;
    return;
  }
  setName(app->sketches[app->renameSketchIndex].meta.name, app->renameBuffer.data());
  app->renameSketchIndex = -1;
}

void cancelBrowserRename(AppState* app) {
  app->renameObjectIndex = -1;
  app->renameSketchIndex = -1;
}

void stepSelection(std::vector<int>& selection, int count, int delta) {
  if (count <= 0) {
    selection.clear();
    return;
  }

  int current = selection.empty() ? 0 : selection.front();
  current = std::clamp(current + delta, 0, count - 1);
  selection.assign(1, current);
}

void deleteSceneObjects(AppState* app, const std::vector<int>& rawIndices) {
  std::vector<int> indices = rawIndices;
  sanitizeObjectIndices(indices, static_cast<int>(app->sceneObjects.size()));
  indices.erase(std::remove_if(indices.begin(), indices.end(), [app](int idx) {
                  return idx >= static_cast<int>(app->sceneObjectMeta.size()) ||
                         app->sceneObjectMeta[idx].locked;
                }),
                indices.end());
  if (indices.empty()) return;

  std::vector<bool> remove(app->sceneObjects.size(), false);
  for (int idx : indices) remove[idx] = true;

  std::vector<StlMesh> nextObjects;
  std::vector<ObjectMetadata> nextMeta;
  nextObjects.reserve(app->sceneObjects.size());
  nextMeta.reserve(app->sceneObjectMeta.size());
  for (int i = 0; i < static_cast<int>(app->sceneObjects.size()); ++i) {
    if (remove[i]) continue;
    nextObjects.push_back(std::move(app->sceneObjects[i]));
    nextMeta.push_back(app->sceneObjectMeta[i]);
  }

  app->sceneObjects = std::move(nextObjects);
  app->sceneObjectMeta = std::move(nextMeta);
  app->pendingDeleteObjects.clear();
  app->renameObjectIndex = -1;
  app->objectSelectionAnchor = -1;
  sanitizeObjectIndices(app->extrudeOptions.targets, static_cast<int>(app->sceneObjects.size()));
  sanitizeObjectIndices(app->combineOptions.targets, static_cast<int>(app->sceneObjects.size()));
  sanitizeObjectIndices(app->combineOptions.tools, static_cast<int>(app->sceneObjects.size()));
  app->browserSelectedObjects.clear();
  if (!app->sceneObjects.empty()) {
    app->browserSelectedObjects.assign(1, std::min(indices.front(), static_cast<int>(app->sceneObjects.size()) - 1));
  }
  syncSelectedObjectFromBrowser(app);
  rebuildCombinedMesh(app);
}

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

void rebuildCombinedMesh(AppState* app) {
  sanitizeObjectIndices(app->browserSelectedObjects, static_cast<int>(app->sceneObjects.size()));
  syncSelectedObjectFromBrowser(app);
  app->mesh = StlMesh();
  for (int i = 0; i < static_cast<int>(app->sceneObjects.size()); ++i) {
    if (i < static_cast<int>(app->sceneObjectMeta.size()) &&
        !app->sceneObjectMeta[i].visible) {
      continue;
    }
    app->mesh.append(app->sceneObjects[i]);
  }
  app->renderer.setMesh(app->mesh);
}

bool applyAddExtrude(AppState* app, const StlMesh& extruded,
                     const std::vector<int>& targetsRaw) {
  if (extruded.empty()) return false;

  std::vector<int> targets = targetsRaw;
  sanitizeObjectIndices(targets, static_cast<int>(app->sceneObjects.size()));
  targets.erase(std::remove_if(targets.begin(), targets.end(), [app](int idx) {
                  return idx >= static_cast<int>(app->sceneObjectMeta.size()) ||
                         app->sceneObjectMeta[idx].locked;
                }),
                targets.end());

  if (targets.empty()) {
    appendSceneObject(app, extruded);
    app->selectedObject = static_cast<int>(app->sceneObjects.size()) - 1;
    app->browserSelectedObjects.assign(1, app->selectedObject);
    rebuildCombinedMesh(app);
    return true;
  }

  StlMesh merged;
  for (int idx : targets) {
    merged.append(app->sceneObjects[idx]);
  }
  merged.append(extruded);

  std::vector<bool> remove(app->sceneObjects.size(), false);
  for (int idx : targets) remove[idx] = true;

  std::vector<StlMesh> next;
  std::vector<ObjectMetadata> nextMeta;
  next.reserve(app->sceneObjects.size() + 1);
  nextMeta.reserve(app->sceneObjectMeta.size() + 1);
  for (int i = 0; i < static_cast<int>(app->sceneObjects.size()); ++i) {
    if (!remove[i]) {
      next.push_back(std::move(app->sceneObjects[i]));
      nextMeta.push_back(app->sceneObjectMeta[i]);
    }
  }
  next.push_back(std::move(merged));
  ObjectMetadata mergedMeta;
  setName(mergedMeta.name, "Object " + std::to_string(app->nextObjectNumber++));
  nextMeta.push_back(mergedMeta);

  app->sceneObjects = std::move(next);
  app->sceneObjectMeta = std::move(nextMeta);
  app->selectedObject = static_cast<int>(app->sceneObjects.size()) - 1;
  app->browserSelectedObjects.assign(1, app->selectedObject);
  rebuildCombinedMesh(app);
  return true;
}

bool applyAddCombine(AppState* app, const std::vector<int>& targetsRaw,
                     const std::vector<int>& toolsRaw, bool keepTools) {
  std::vector<int> targets = targetsRaw;
  std::vector<int> tools = toolsRaw;
  sanitizeObjectIndices(targets, static_cast<int>(app->sceneObjects.size()));
  sanitizeObjectIndices(tools, static_cast<int>(app->sceneObjects.size()));
  targets.erase(std::remove_if(targets.begin(), targets.end(), [app](int idx) {
                  return idx >= static_cast<int>(app->sceneObjectMeta.size()) ||
                         app->sceneObjectMeta[idx].locked;
                }),
                targets.end());
  tools.erase(std::remove_if(tools.begin(), tools.end(), [app, keepTools](int idx) {
                return idx >= static_cast<int>(app->sceneObjectMeta.size()) ||
                       (app->sceneObjectMeta[idx].locked && !keepTools);
              }),
              tools.end());
  for (int idx : targets) {
    eraseIndex(tools, idx);
  }
  if (targets.empty() || tools.empty()) return false;

  StlMesh merged;
  for (int idx : targets) merged.append(app->sceneObjects[idx]);
  for (int idx : tools) merged.append(app->sceneObjects[idx]);

  std::vector<bool> remove(app->sceneObjects.size(), false);
  for (int idx : targets) remove[idx] = true;
  if (!keepTools) {
    for (int idx : tools) remove[idx] = true;
  }

  std::vector<StlMesh> next;
  std::vector<ObjectMetadata> nextMeta;
  next.reserve(app->sceneObjects.size() + 1);
  nextMeta.reserve(app->sceneObjectMeta.size() + 1);
  for (int i = 0; i < static_cast<int>(app->sceneObjects.size()); ++i) {
    if (!remove[i]) {
      next.push_back(std::move(app->sceneObjects[i]));
      nextMeta.push_back(app->sceneObjectMeta[i]);
    }
  }
  next.push_back(std::move(merged));
  ObjectMetadata mergedMeta;
  setName(mergedMeta.name, "Object " + std::to_string(app->nextObjectNumber++));
  nextMeta.push_back(mergedMeta);

  app->sceneObjects = std::move(next);
  app->sceneObjectMeta = std::move(nextMeta);
  app->selectedObject = static_cast<int>(app->sceneObjects.size()) - 1;
  app->browserSelectedObjects.assign(1, app->selectedObject);
  rebuildCombinedMesh(app);
  return true;
}

bool applySubtractExtrude(AppState* app, const StlMesh& extruded,
                          const std::vector<int>& targetsRaw) {
  if (extruded.empty()) return false;

  std::vector<int> targets = targetsRaw;
  sanitizeObjectIndices(targets, static_cast<int>(app->sceneObjects.size()));
  targets.erase(std::remove_if(targets.begin(), targets.end(), [app](int idx) {
                  return idx >= static_cast<int>(app->sceneObjectMeta.size()) ||
                         app->sceneObjectMeta[idx].locked;
                }),
                targets.end());
  if (targets.empty()) return false;

  const Aabb toolBox = meshAabb(extruded);
  std::vector<bool> remove(app->sceneObjects.size(), false);
  for (int idx : targets) {
    if (aabbOverlap(meshAabb(app->sceneObjects[idx]), toolBox)) {
      remove[idx] = true;
    }
  }

  bool removedAny = false;
  std::vector<StlMesh> next;
  std::vector<ObjectMetadata> nextMeta;
  next.reserve(app->sceneObjects.size());
  nextMeta.reserve(app->sceneObjectMeta.size());
  for (int i = 0; i < static_cast<int>(app->sceneObjects.size()); ++i) {
    if (remove[i]) {
      removedAny = true;
      continue;
    }
    next.push_back(std::move(app->sceneObjects[i]));
    nextMeta.push_back(app->sceneObjectMeta[i]);
  }

  if (!removedAny) return false;
  app->sceneObjects = std::move(next);
  app->sceneObjectMeta = std::move(nextMeta);
  app->selectedObject = app->sceneObjects.empty() ? -1 : 0;
  app->browserSelectedObjects = app->sceneObjects.empty() ? std::vector<int>{} : std::vector<int>{0};
  rebuildCombinedMesh(app);
  return true;
}

bool applySubtractCombine(AppState* app, const std::vector<int>& targetsRaw,
                          const std::vector<int>& toolsRaw, bool keepTools) {
  std::vector<int> targets = targetsRaw;
  std::vector<int> tools = toolsRaw;
  sanitizeObjectIndices(targets, static_cast<int>(app->sceneObjects.size()));
  sanitizeObjectIndices(tools, static_cast<int>(app->sceneObjects.size()));
  targets.erase(std::remove_if(targets.begin(), targets.end(), [app](int idx) {
                  return idx >= static_cast<int>(app->sceneObjectMeta.size()) ||
                         app->sceneObjectMeta[idx].locked;
                }),
                targets.end());
  tools.erase(std::remove_if(tools.begin(), tools.end(), [app, keepTools](int idx) {
                return idx >= static_cast<int>(app->sceneObjectMeta.size()) ||
                       (app->sceneObjectMeta[idx].locked && !keepTools);
              }),
              tools.end());
  for (int idx : targets) {
    eraseIndex(tools, idx);
  }
  if (targets.empty() || tools.empty()) return false;

  std::vector<Aabb> toolBoxes;
  toolBoxes.reserve(tools.size());
  for (int idx : tools) toolBoxes.push_back(meshAabb(app->sceneObjects[idx]));

  std::vector<bool> remove(app->sceneObjects.size(), false);
  for (int idx : targets) {
    const Aabb targetBox = meshAabb(app->sceneObjects[idx]);
    for (const Aabb& tb : toolBoxes) {
      if (aabbOverlap(targetBox, tb)) {
        remove[idx] = true;
        break;
      }
    }
  }
  if (!keepTools) {
    for (int idx : tools) remove[idx] = true;
  }

  bool removedAny = false;
  std::vector<StlMesh> next;
  std::vector<ObjectMetadata> nextMeta;
  next.reserve(app->sceneObjects.size());
  nextMeta.reserve(app->sceneObjectMeta.size());
  for (int i = 0; i < static_cast<int>(app->sceneObjects.size()); ++i) {
    if (remove[i]) {
      removedAny = true;
      continue;
    }
    next.push_back(std::move(app->sceneObjects[i]));
    nextMeta.push_back(app->sceneObjectMeta[i]);
  }

  if (!removedAny) return false;
  app->sceneObjects = std::move(next);
  app->sceneObjectMeta = std::move(nextMeta);
  app->selectedObject = app->sceneObjects.empty() ? -1 : 0;
  app->browserSelectedObjects = app->sceneObjects.empty() ? std::vector<int>{} : std::vector<int>{0};
  rebuildCombinedMesh(app);
  return true;
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
    if (oi < static_cast<int>(app.sceneObjectMeta.size()) && !app.sceneObjectMeta[oi].visible) {
      continue;
    }
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

std::optional<FacePickResult> pickObjectFace(const AppState& app, glm::vec3 rayO, glm::vec3 rayD) {
  float bestT = std::numeric_limits<float>::max();
  std::optional<FacePickResult> best;
  for (int oi = 0; oi < static_cast<int>(app.sceneObjects.size()); ++oi) {
    if (oi < static_cast<int>(app.sceneObjectMeta.size()) && !app.sceneObjectMeta[oi].visible) {
      continue;
    }
    const auto& verts = app.sceneObjects[oi].vertices();
    const auto& inds = app.sceneObjects[oi].indices();
    for (size_t i = 0; i + 2 < inds.size(); i += 3) {
      const glm::vec3& v0 = verts[inds[i]].position;
      const glm::vec3& v1 = verts[inds[i + 1]].position;
      const glm::vec3& v2 = verts[inds[i + 2]].position;
      float t = rayTriangle(rayO, rayD, v0, v1, v2);
      if (t > 0.0f && t < bestT) {
        bestT = t;
        FacePickResult hit;
        hit.objectIndex = oi;
        hit.triangleOffset = static_cast<int>(i);
        hit.hitPoint = rayO + rayD * t;
        hit.normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
        if (glm::dot(hit.normal, hit.normal) < 1e-8f) {
          hit.normal = {0.0f, 0.0f, 1.0f};
        }
        best = hit;
      }
    }
  }
  return best;
}

SketchPlane sketchPlaneFromNormal(glm::vec3 n) {
  n = glm::abs(glm::normalize(n));
  if (n.x >= n.y && n.x >= n.z) return SketchPlane::YZ;
  if (n.y >= n.x && n.y >= n.z) return SketchPlane::XZ;
  return SketchPlane::XY;
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

void exitSketchMode(AppState* app) {
  if (app->hasActiveSketch()) {
    const auto& sketch = app->activeSketch();
    const auto& entry = app->sketches[app->activeSketchIndex];
    EditSketchAction action;
    action.sketchIndex = app->activeSketchIndex;
    action.sketchName = std::string(entry.meta.name.data());
    action.elements = sketch.elements();
    action.constraints = sketch.constraints();
    app->timeline.push(std::move(action),
                       "Edit " + std::string(entry.meta.name.data()));
    app->activeSketch().clearSelection();
  }
  app->sceneMode = SceneMode::View3D;
  app->sketchTool.cancel();
  app->sketchTool.setTool(Tool::None);
  app->extrudeTool.cancel();
}

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
  if (!app->extrudeTool.active()) {
    app->extrudeOptions.visible = false;
    app->objectPickMode = ObjectPickMode::None;
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_FirstUseEver);
  bool open = app->extrudeOptions.visible;
  if (ImGui::Begin("Extrude Options", &open)) {
    int op = static_cast<int>(app->extrudeOptions.operation);
    if (ImGui::Combo("Operation", &op, "Add\0Subtract\0")) {
      app->extrudeOptions.operation = static_cast<BooleanOp>(op);
    }

    ImGui::Text("Depth:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputText("##extrudeDepth", app->extrudeOptions.depthBuffer,
                     sizeof(app->extrudeOptions.depthBuffer));
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", unitSuffix(app->project.defaultUnit));

    ImGui::Separator();
    drawObjectSelectionList("Target Objects", app->extrudeOptions.targets,
                            ObjectPickMode::ExtrudeTargets, app);

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
        const bool subtract = (app->extrudeOptions.operation == BooleanOp::Subtract);
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
          ea.targetObjectNames = objectNames(app, app->extrudeOptions.targets);
          ea.resultObjectName = objectName(
              app, static_cast<int>(app->sceneObjects.size()) - 1);
          app->timeline.push(std::move(ea),
                             std::string(subtract ? "Subtract Extrude" : "Add Extrude"));
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
          if (!applyAddExtrude(app, extruded, app->extrudeOptions.targets)) {
            app->status = "Extrude failed";
          } else {
            recordExtrude();
            app->status = "Extrude add complete";
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

  appendSceneObject(&app, StlMesh::makeUnitCube());
  app.selectedObject = 0;
  app.browserSelectedObjects.assign(1, 0);
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

    app.renderer.beginImGuiFrame();

    // --- Menu bar (always visible) ---
    drawMenuBar(&app);

    // --- Toolbar (sketch mode only) ---
    ToolbarAction toolbarAction;
    if (app.sceneMode == SceneMode::Sketch && app.hasActiveSketch()) {
      toolbarAction =
          app.toolbar.draw(app.sketchTool, app.extrudeTool,
                           app.activeSketch().hasSelection(), app.project.defaultUnit);
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
        app.extrudeOptions.operation = BooleanOp::Add;
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

      if (app.sketchCreate.open && app.sketchCreate.pickFromScene) {
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
              app.sketchCreate.fromFace = true;
              app.sketchCreate.sourceObject = face->objectIndex;
              app.sketchCreate.plane = sketchPlaneFromNormal(face->normal);
              app.sketchCreate.offsetMm = planeAxisValue(face->hitPoint, app.sketchCreate.plane);
              app.partialSelectedObject = face->objectIndex;
              app.sketchCreate.pickFromScene = false;
              app.status = "Face selected for sketch plane";
            } else {
              std::optional<glm::vec3> bestHit;
              SketchPlane bestPlane = SketchPlane::XY;
              float bestDist = std::numeric_limits<float>::max();
              for (SketchPlane candidate : {SketchPlane::XY, SketchPlane::XZ, SketchPlane::YZ}) {
                auto hit = rayPlaneHit(static_cast<float>(x), static_cast<float>(y),
                                       static_cast<float>(fbW), static_cast<float>(fbH),
                                       view, proj, candidate, 0.0f);
                if (!hit) continue;
                const float d = glm::length(*hit);
                if (d < bestDist) {
                  bestDist = d;
                  bestHit = hit;
                  bestPlane = candidate;
                }
              }
              if (bestHit && bestDist <= 25.0f) {
                app.sketchCreate.fromFace = false;
                app.sketchCreate.sourceObject = -1;
                app.sketchCreate.plane = bestPlane;
                app.sketchCreate.offsetMm = 0.0f;
                app.partialSelectedObject = -1;
                app.sketchCreate.pickFromScene = false;
                app.status = "Center plane selected for new sketch";
              } else {
                app.status = "No face or center plane hit. Try clicking a visible face or near origin";
              }
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
            if (hit >= 0) {
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

    if (app.sceneMode == SceneMode::Sketch && app.hasActiveSketch() && app.activeSketchVisible()) {
      appendGrid(allLines, app.activePlane(), app.project.gridExtent, app.project.gridSpacing);
      for (int i = 0; i <= app.activeSketchIndex; ++i) {
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

    // Extrude preview lines (visible from any view).
    app.extrudeTool.appendPreview(allLines);

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
    drawNewSketchWindow(&app);
    drawProjectToolWindow(&app);
    drawAppSettingsWindow(&app);
    drawProjectSettingsWindow(&app);
    drawExtrudeOptionsWindow(&app);
    drawCombineWindow(&app);

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
