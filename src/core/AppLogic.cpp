#include "core/AppLogic.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include "sketch/Profile.hpp"

namespace {
struct Aabb {
  glm::vec3 min{0.0f};
  glm::vec3 max{0.0f};
  bool valid = false;
};

template <size_t N>
void setName(std::array<char, N>& dest, const std::string& value) {
  std::snprintf(dest.data(), dest.size(), "%s", value.c_str());
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
}  // namespace

const char* browserRowLabel(bool visible, bool locked) {
  if (!visible && locked) return "[H][L]";
  if (!visible) return "[H]";
  if (locked) return "[L]";
  return "";
}

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

void setSelectionRange(std::vector<int>& selection, int start, int end) {
  selection.clear();
  if (start > end) std::swap(start, end);
  for (int i = start; i <= end; ++i) selection.push_back(i);
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
  app->timelineCursor = app->timeline.size() - 1;

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
    app->timelineCursor = app->timeline.size() - 1;
    app->activeSketch().clearSelection();
  }
  app->sceneMode = SceneMode::View3D;
  app->sketchTool.cancel();
  app->sketchTool.setTool(Tool::None);
  app->extrudeTool.cancel();
}
