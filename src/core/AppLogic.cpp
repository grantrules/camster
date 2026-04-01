#include "core/AppLogic.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
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

glm::vec3 hsvToRgb(float h, float s, float v) {
  const float hp = h * 6.0f;
  const int i = static_cast<int>(std::floor(hp));
  const float f = hp - static_cast<float>(i);
  const float p = v * (1.0f - s);
  const float q = v * (1.0f - s * f);
  const float t = v * (1.0f - s * (1.0f - f));

  switch (i % 6) {
    case 0: return {v, t, p};
    case 1: return {q, v, p};
    case 2: return {p, v, t};
    case 3: return {p, q, v};
    case 4: return {t, p, v};
    case 5: return {v, p, q};
  }
  return {v, p, q};
}

glm::vec3 randomPastelColor() {
  static std::mt19937 rng(std::random_device{}());
  std::uniform_real_distribution<float> hueDist(0.0f, 1.0f);
  std::uniform_real_distribution<float> satDist(0.28f, 0.48f);
  std::uniform_real_distribution<float> valDist(0.88f, 0.98f);
  return hsvToRgb(hueDist(rng), satDist(rng), valDist(rng));
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

glm::vec3 aabbCenter(const Aabb& box) {
  return (box.min + box.max) * 0.5f;
}

bool aabbOverlap(const Aabb& a, const Aabb& b) {
  if (!a.valid || !b.valid) return false;
  return a.min.x <= b.max.x && a.max.x >= b.min.x &&
         a.min.y <= b.max.y && a.max.y >= b.min.y &&
         a.min.z <= b.max.z && a.max.z >= b.min.z;
}

void normalizeDeterministicAppState(AppState* app) {
  if (!app) return;

  const auto sanitizeIndices = [](std::vector<int>& list, int objectCount) {
    list.erase(std::remove_if(list.begin(), list.end(), [objectCount](int idx) {
                 return idx < 0 || idx >= objectCount;
               }),
               list.end());
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
  };

  if (app->sceneObjectMeta.size() < app->sceneObjects.size()) {
    const size_t prev = app->sceneObjectMeta.size();
    app->sceneObjectMeta.resize(app->sceneObjects.size());
    for (size_t i = prev; i < app->sceneObjectMeta.size(); ++i) {
      ObjectMetadata meta;
      setName(meta.name, "Object " + std::to_string(app->nextObjectNumber++));
      meta.visible = true;
      meta.locked = false;
      app->sceneObjectMeta[i] = meta;
    }
  } else if (app->sceneObjectMeta.size() > app->sceneObjects.size()) {
    app->sceneObjectMeta.resize(app->sceneObjects.size());
  }

  sanitizeIndices(app->browserSelectedObjects,
                  static_cast<int>(app->sceneObjects.size()));
  sanitizeIndices(app->extrudeOptions.targets,
                  static_cast<int>(app->sceneObjects.size()));
  sanitizeIndices(app->combineOptions.targets,
                  static_cast<int>(app->sceneObjects.size()));
  sanitizeIndices(app->combineOptions.tools,
                  static_cast<int>(app->sceneObjects.size()));

  if (app->selectedObject < -1 ||
      app->selectedObject >= static_cast<int>(app->sceneObjects.size())) {
    app->selectedObject = -1;
  }

  if (!app->browserSelectedObjects.empty()) {
    app->selectedObject = app->browserSelectedObjects.front();
  }

  if (app->sceneObjects.empty()) {
    app->selectedObject = -1;
    app->browserSelectedObjects.clear();
  }

  if (app->timelineCursor >= app->timeline.size()) {
    app->timelineCursor = app->timeline.size() - 1;
  }
}

void restoreObjectSnapshot(AppState* app, ObjectEditSnapshot snapshot) {
  if (!app) return;
  app->sceneObjects = std::move(snapshot.sceneObjects);
  app->sceneObjectMeta = std::move(snapshot.sceneObjectMeta);
  app->selectedObject = snapshot.selectedObject;
  app->nextObjectNumber = snapshot.nextObjectNumber;
  app->browserSelectedObjects = std::move(snapshot.browserSelectedObjects);
  normalizeDeterministicAppState(app);
}

bool verifyObjectStateRoundTrip(const AppState& app, std::string& error) {
  ObjectEditSnapshot snap;
  snap.sceneObjects = app.sceneObjects;
  snap.sceneObjectMeta = app.sceneObjectMeta;
  snap.selectedObject = app.selectedObject;
  snap.nextObjectNumber = app.nextObjectNumber;
  snap.browserSelectedObjects = app.browserSelectedObjects;

  const std::vector<StlMesh> objectsCopy = snap.sceneObjects;
  const std::vector<ObjectMetadata> metaCopy = snap.sceneObjectMeta;

  if (objectsCopy.size() != app.sceneObjects.size() ||
      metaCopy.size() != app.sceneObjectMeta.size()) {
    error = "object snapshot round-trip size mismatch";
    return false;
  }

  for (size_t i = 0; i < objectsCopy.size(); ++i) {
    if (objectsCopy[i].vertices().size() != app.sceneObjects[i].vertices().size() ||
        objectsCopy[i].indices().size() != app.sceneObjects[i].indices().size()) {
      error = "object snapshot round-trip geometry mismatch";
      return false;
    }
    if (std::string(metaCopy[i].name.data()) != std::string(app.sceneObjectMeta[i].name.data()) ||
        metaCopy[i].visible != app.sceneObjectMeta[i].visible ||
        metaCopy[i].locked != app.sceneObjectMeta[i].locked) {
      error = "object snapshot round-trip metadata mismatch";
      return false;
    }
  }

  return true;
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

float faceOffsetForBox(const Aabb& box, SketchPlane plane, int faceSign) {
  const bool positive = faceSign >= 0;
  switch (plane) {
    case SketchPlane::XY: return positive ? box.max.z : box.min.z;
    case SketchPlane::XZ: return positive ? box.max.y : box.min.y;
    case SketchPlane::YZ: return positive ? box.max.x : box.min.x;
  }
  return 0.0f;
}

int findObjectIndexByName(const AppState* app, const std::string& name) {
  for (int i = 0; i < static_cast<int>(app->sceneObjectMeta.size()); ++i) {
    if (std::string(app->sceneObjectMeta[i].name.data()) == name) {
      return i;
    }
  }
  return -1;
}

ResolvedPlane resolvePlaneRecursive(const AppState* app, int planeId, int depth) {
  if (!app || depth > 16) return {};
  const int planeIndex = findPlaneIndexById(app, planeId);
  if (planeIndex < 0) return {};

  const PlaneEntry& entry = app->planes[planeIndex];
  ResolvedPlane resolved;
  resolved.plane = entry.reference.plane;
  resolved.valid = true;

  switch (entry.reference.kind) {
    case PlaneReferenceKind::Principal:
      resolved.offsetMm = entry.reference.offsetMm;
      return resolved;

    case PlaneReferenceKind::OffsetFromPlane: {
      const ResolvedPlane parent = resolvePlaneRecursive(app, entry.reference.parentPlaneId,
                                                         depth + 1);
      if (!parent.valid) return {};
      resolved.plane = parent.plane;
      resolved.offsetMm = parent.offsetMm + entry.reference.distanceMm;
      return resolved;
    }

    case PlaneReferenceKind::OffsetFromFace: {
      const int objectIndex = findObjectIndexByName(app, entry.reference.sourceObjectName);
      if (objectIndex < 0 || objectIndex >= static_cast<int>(app->sceneObjects.size())) {
        return {};
      }
      const Aabb box = meshAabb(app->sceneObjects[objectIndex]);
      if (!box.valid) return {};
      resolved.offsetMm = faceOffsetForBox(box, entry.reference.plane,
                                           entry.reference.faceSign) +
                          static_cast<float>(entry.reference.faceSign) *
                              entry.reference.distanceMm;
      return resolved;
    }
  }

  return {};
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

void syncSelectedPlaneFromBrowser(AppState* app) {
  sanitizeObjectIndices(app->browserSelectedPlanes, static_cast<int>(app->planes.size()));
  if (app->browserSelectedPlanes.empty() ||
      app->browserSelectedPlanes.size() != 1 ||
      !hasIndex(app->browserSelectedPlanes, app->renamePlaneIndex)) {
    app->renamePlaneIndex = -1;
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
    syncSketchPlanes(app);
    if (app->hasActiveSketch()) {
      snapCameraToPlane(app, app->activePlane());
    }
  }
  if (app->browserSelectedSketches.size() != 1 ||
      !hasIndex(app->browserSelectedSketches, app->renameSketchIndex)) {
    app->renameSketchIndex = -1;
  }
}

void initializeDefaultPlanes(AppState* app) {
  app->planes.clear();
  app->browserSelectedPlanes.clear();
  app->planeSelectionAnchor = -1;
  app->renamePlaneIndex = -1;
  app->nextPlaneId = 1;
  app->nextPlaneNumber = 1;

  const std::array<std::pair<SketchPlane, const char*>, 3> defaults = {{
      {SketchPlane::XY, "XY"},
      {SketchPlane::XZ, "XZ"},
      {SketchPlane::YZ, "YZ"},
  }};

  for (const auto& [plane, name] : defaults) {
    PlaneEntry entry;
    entry.id = defaultPlaneId(plane);
    setName(entry.meta.name, name);
    entry.meta.visible = false;
    entry.meta.locked = true;
    entry.meta.builtIn = true;
    entry.reference.kind = PlaneReferenceKind::Principal;
    entry.reference.plane = plane;
    entry.reference.offsetMm = 0.0f;
    app->planes.push_back(std::move(entry));
    app->nextPlaneId = std::max(app->nextPlaneId, defaultPlaneId(plane) + 1);
  }
}

int defaultPlaneId(SketchPlane plane) {
  switch (plane) {
    case SketchPlane::XY: return 1;
    case SketchPlane::XZ: return 2;
    case SketchPlane::YZ: return 3;
  }
  return 1;
}

int findPlaneIndexById(const AppState* app, int planeId) {
  if (!app || planeId < 0) return -1;
  for (int i = 0; i < static_cast<int>(app->planes.size()); ++i) {
    if (app->planes[i].id == planeId) return i;
  }
  return -1;
}

ResolvedPlane resolvePlane(const AppState* app, int planeId) {
  return resolvePlaneRecursive(app, planeId, 0);
}

void syncSketchPlanes(AppState* app) {
  for (auto& sketch : app->sketches) {
    const ResolvedPlane resolved = resolvePlane(app, sketch.planeId);
    if (!resolved.valid) continue;
    sketch.plane = resolved.plane;
    sketch.offsetMm = resolved.offsetMm;
  }
}

std::string planeName(const AppState* app, int planeIndex) {
  if (!app || planeIndex < 0 || planeIndex >= static_cast<int>(app->planes.size())) {
    return {};
  }
  return std::string(app->planes[planeIndex].meta.name.data());
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

void createSketch(AppState* app, int planeId) {
  const ResolvedPlane resolved = resolvePlane(app, planeId);
  if (!resolved.valid) return;

  SketchEntry entry;
  setName(entry.meta.name, "Sketch " + std::to_string(app->nextSketchNumber++));
  entry.meta.visible = true;
  entry.meta.locked = false;
  entry.planeId = planeId;
  entry.plane = resolved.plane;
  entry.offsetMm = resolved.offsetMm;

  CreateSketchAction action;
  action.planeId = planeId;
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
  snapCameraToPlane(app, resolved.plane);
}

void createOffsetPlaneFromPlane(AppState* app, int parentPlaneId, float distanceMm) {
  const ResolvedPlane parent = resolvePlane(app, parentPlaneId);
  if (!parent.valid) return;

  PlaneEntry entry;
  entry.id = app->nextPlaneId++;
  setName(entry.meta.name, "Plane " + std::to_string(app->nextPlaneNumber++));
  entry.meta.visible = true;
  entry.meta.locked = false;
  entry.meta.builtIn = false;
  entry.reference.kind = PlaneReferenceKind::OffsetFromPlane;
  entry.reference.plane = parent.plane;
  entry.reference.parentPlaneId = parentPlaneId;
  entry.reference.distanceMm = distanceMm;

  CreatePlaneAction action;
  action.planeId = entry.id;
  action.reference = entry.reference;
  action.name = std::string(entry.meta.name.data());
  app->timeline.push(std::move(action), "Create " + std::string(entry.meta.name.data()));
  app->timelineCursor = app->timeline.size() - 1;

  app->planes.push_back(std::move(entry));
  app->browserSelectedPlanes.assign(1, static_cast<int>(app->planes.size()) - 1);
  app->planeSelectionAnchor = static_cast<int>(app->planes.size()) - 1;
}

void createOffsetPlaneFromFace(AppState* app, int sourceObjectIndex,
                               SketchPlane plane, int faceSign, float distanceMm) {
  if (sourceObjectIndex < 0 || sourceObjectIndex >= static_cast<int>(app->sceneObjectMeta.size())) {
    return;
  }

  PlaneEntry entry;
  entry.id = app->nextPlaneId++;
  setName(entry.meta.name, "Plane " + std::to_string(app->nextPlaneNumber++));
  entry.meta.visible = true;
  entry.meta.locked = false;
  entry.meta.builtIn = false;
  entry.reference.kind = PlaneReferenceKind::OffsetFromFace;
  entry.reference.plane = plane;
  entry.reference.sourceObjectName = objectName(app, sourceObjectIndex);
  entry.reference.faceSign = faceSign >= 0 ? 1 : -1;
  entry.reference.distanceMm = distanceMm;

  CreatePlaneAction action;
  action.planeId = entry.id;
  action.reference = entry.reference;
  action.name = std::string(entry.meta.name.data());
  app->timeline.push(std::move(action), "Create " + std::string(entry.meta.name.data()));
  app->timelineCursor = app->timeline.size() - 1;

  app->planes.push_back(std::move(entry));
  app->browserSelectedPlanes.assign(1, static_cast<int>(app->planes.size()) - 1);
  app->planeSelectionAnchor = static_cast<int>(app->planes.size()) - 1;
}

void createReferencePointFromSelection(AppState* app) {
  if (!app) return;
  ReferencePointEntry entry;
  setName(entry.meta.name, "Point " + std::to_string(app->nextPointNumber++));
  entry.meta.visible = true;
  entry.meta.locked = false;

  if (app->selectedObject >= 0 && app->selectedObject < static_cast<int>(app->sceneObjects.size())) {
    const Aabb box = meshAabb(app->sceneObjects[app->selectedObject]);
    if (box.valid) entry.position = aabbCenter(box);
  } else if (!app->browserSelectedPlanes.empty()) {
    const int planeIndex = app->browserSelectedPlanes.front();
    if (planeIndex >= 0 && planeIndex < static_cast<int>(app->planes.size())) {
      const ResolvedPlane resolved = resolvePlane(app, app->planes[planeIndex].id);
      if (resolved.valid) entry.position = toWorld(glm::vec2(0.0f), resolved.plane, resolved.offsetMm);
    }
  }

  ReferenceGeometryAction action;
  action.geometryType = "Point";
  action.name = entry.meta.name.data();
  app->timeline.push(std::move(action), "Create " + std::string(entry.meta.name.data()));
  app->timelineCursor = app->timeline.size() - 1;

  app->referencePoints.push_back(entry);
  app->browserSelectedPoints.assign(1, static_cast<int>(app->referencePoints.size()) - 1);
  app->pointSelectionAnchor = static_cast<int>(app->referencePoints.size()) - 1;
}

void createReferenceAxisFromSelection(AppState* app) {
  if (!app) return;
  ReferenceAxisEntry entry;
  setName(entry.meta.name, "Axis " + std::to_string(app->nextAxisNumber++));
  entry.meta.visible = true;
  entry.meta.locked = false;
  entry.displayLengthMm = 120.0f;

  if (!app->browserSelectedPlanes.empty()) {
    const int planeIndex = app->browserSelectedPlanes.front();
    if (planeIndex >= 0 && planeIndex < static_cast<int>(app->planes.size())) {
      const ResolvedPlane resolved = resolvePlane(app, app->planes[planeIndex].id);
      if (resolved.valid) {
        entry.origin = toWorld(glm::vec2(0.0f), resolved.plane, resolved.offsetMm);
        entry.direction = planeNormal(resolved.plane);
      }
    }
  } else if (app->selectedObject >= 0 && app->selectedObject < static_cast<int>(app->sceneObjects.size())) {
    const Aabb box = meshAabb(app->sceneObjects[app->selectedObject]);
    if (box.valid) {
      entry.origin = aabbCenter(box);
      const glm::vec3 span = box.max - box.min;
      if (span.x >= span.y && span.x >= span.z) entry.direction = {1.0f, 0.0f, 0.0f};
      else if (span.y >= span.x && span.y >= span.z) entry.direction = {0.0f, 1.0f, 0.0f};
      else entry.direction = {0.0f, 0.0f, 1.0f};
      entry.displayLengthMm = std::max({span.x, span.y, span.z, 40.0f}) * 1.25f;
    }
  }

  ReferenceGeometryAction action;
  action.geometryType = "Axis";
  action.name = entry.meta.name.data();
  app->timeline.push(std::move(action), "Create " + std::string(entry.meta.name.data()));
  app->timelineCursor = app->timeline.size() - 1;

  app->referenceAxes.push_back(entry);
  app->browserSelectedAxes.assign(1, static_cast<int>(app->referenceAxes.size()) - 1);
  app->axisSelectionAnchor = static_cast<int>(app->referenceAxes.size()) - 1;
}

void appendSceneObject(AppState* app, StlMesh mesh) {
  app->sceneObjects.push_back(std::move(mesh));
  ObjectMetadata meta;
  setName(meta.name, "Object " + std::to_string(app->nextObjectNumber++));
  meta.visible = true;
  meta.locked = false;
  const glm::vec3 color = randomPastelColor();
  meta.colorRgb = {color.x, color.y, color.z};
  app->sceneObjectMeta.push_back(meta);
}

void randomizeObjectColor(AppState* app, int objectIndex) {
  if (!app) return;
  if (objectIndex < 0 || objectIndex >= static_cast<int>(app->sceneObjectMeta.size())) return;
  pushObjectUndoSnapshot(app);
  const glm::vec3 color = randomPastelColor();
  app->sceneObjectMeta[objectIndex].colorRgb = {color.x, color.y, color.z};
  rebuildCombinedMesh(app);
}

bool setObjectVisibility(AppState* app, int objectIndex, bool visible) {
  if (!app) return false;
  if (objectIndex < 0 || objectIndex >= static_cast<int>(app->sceneObjectMeta.size())) return false;
  if (app->sceneObjectMeta[objectIndex].visible == visible) return false;
  pushObjectUndoSnapshot(app);
  app->sceneObjectMeta[objectIndex].visible = visible;
  rebuildCombinedMesh(app);
  return true;
}

bool setObjectLocked(AppState* app, int objectIndex, bool locked) {
  if (!app) return false;
  if (objectIndex < 0 || objectIndex >= static_cast<int>(app->sceneObjectMeta.size())) return false;
  if (app->sceneObjectMeta[objectIndex].locked == locked) return false;
  pushObjectUndoSnapshot(app);
  app->sceneObjectMeta[objectIndex].locked = locked;
  return true;
}

bool renameObject(AppState* app, int objectIndex, const std::string& newNameRaw) {
  if (!app) return false;
  if (objectIndex < 0 || objectIndex >= static_cast<int>(app->sceneObjectMeta.size())) return false;

  std::string newName = newNameRaw;
  if (newName.empty()) return false;
  if (newName == app->sceneObjectMeta[objectIndex].name.data()) return false;

  pushObjectUndoSnapshot(app);

  const std::string oldName = app->sceneObjectMeta[objectIndex].name.data();
  setName(app->sceneObjectMeta[objectIndex].name, newName);
  const std::string appliedName = app->sceneObjectMeta[objectIndex].name.data();
  for (auto& plane : app->planes) {
    if (plane.reference.kind == PlaneReferenceKind::OffsetFromFace &&
        plane.reference.sourceObjectName == oldName) {
      plane.reference.sourceObjectName = appliedName;
    }
  }
  return true;
}

void pushObjectUndoSnapshot(AppState* app) {
  if (!app) return;
  ObjectEditSnapshot snap;
  snap.sceneObjects = app->sceneObjects;
  snap.sceneObjectMeta = app->sceneObjectMeta;
  snap.selectedObject = app->selectedObject;
  snap.nextObjectNumber = app->nextObjectNumber;
  snap.browserSelectedObjects = app->browserSelectedObjects;
  app->objectUndoStack.push_back(std::move(snap));
  constexpr size_t kMaxObjectUndoLevels = 64;
  if (app->objectUndoStack.size() > kMaxObjectUndoLevels) {
    app->objectUndoStack.erase(app->objectUndoStack.begin());
  }
  app->objectRedoStack.clear();
}

bool objectCanUndo(const AppState* app) {
  return app && !app->objectUndoStack.empty();
}

bool objectCanRedo(const AppState* app) {
  return app && !app->objectRedoStack.empty();
}

bool objectUndo(AppState* app) {
  if (!objectCanUndo(app)) return false;
  ObjectEditSnapshot current;
  current.sceneObjects = app->sceneObjects;
  current.sceneObjectMeta = app->sceneObjectMeta;
  current.selectedObject = app->selectedObject;
  current.nextObjectNumber = app->nextObjectNumber;
  current.browserSelectedObjects = app->browserSelectedObjects;
  app->objectRedoStack.push_back(std::move(current));

  ObjectEditSnapshot snap = std::move(app->objectUndoStack.back());
  app->objectUndoStack.pop_back();
  restoreObjectSnapshot(app, std::move(snap));
  rebuildCombinedMesh(app);
  return true;
}

bool objectRedo(AppState* app) {
  if (!objectCanRedo(app)) return false;
  ObjectEditSnapshot current;
  current.sceneObjects = app->sceneObjects;
  current.sceneObjectMeta = app->sceneObjectMeta;
  current.selectedObject = app->selectedObject;
  current.nextObjectNumber = app->nextObjectNumber;
  current.browserSelectedObjects = app->browserSelectedObjects;
  app->objectUndoStack.push_back(std::move(current));

  ObjectEditSnapshot snap = std::move(app->objectRedoStack.back());
  app->objectRedoStack.pop_back();
  restoreObjectSnapshot(app, std::move(snap));
  rebuildCombinedMesh(app);
  return true;
}

bool validateDeterministicAppState(const AppState* app, std::string& error) {
  if (!app) {
    error = "null AppState";
    return false;
  }
  if (app->sceneObjectMeta.size() != app->sceneObjects.size()) {
    error = "scene object/meta count mismatch";
    return false;
  }
  if (app->selectedObject < -1 ||
      app->selectedObject >= static_cast<int>(app->sceneObjects.size())) {
    error = "selectedObject out of range";
    return false;
  }
  if (app->timelineCursor < -1 || app->timelineCursor >= app->timeline.size()) {
    error = "timeline cursor out of range";
    return false;
  }
  return true;
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
  app->referenceAxes.clear();
  app->referencePoints.clear();
  app->browserSelectedAxes.clear();
  app->browserSelectedPoints.clear();
  app->axisSelectionAnchor = -1;
  app->pointSelectionAnchor = -1;
  app->nextAxisNumber = 1;
  app->nextPointNumber = 1;
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
  renameObject(app, app->renameObjectIndex, app->renameBuffer.data());
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
  app->renamePlaneIndex = -1;
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

  pushObjectUndoSnapshot(app);

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
  normalizeDeterministicAppState(app);

  std::string deterministicError;
  if (!validateDeterministicAppState(app, deterministicError)) {
    app->status = "State normalized warning: " + deterministicError;
  }
  std::string roundTripError;
  if (!verifyObjectStateRoundTrip(*app, roundTripError)) {
    app->status = "Round-trip warning: " + roundTripError;
  }

  sanitizeObjectIndices(app->browserSelectedObjects, static_cast<int>(app->sceneObjects.size()));
  syncSelectedObjectFromBrowser(app);
  app->mesh = StlMesh();
  for (int i = 0; i < static_cast<int>(app->sceneObjects.size()); ++i) {
    if (i < static_cast<int>(app->sceneObjectMeta.size()) &&
        !app->sceneObjectMeta[i].visible) {
      continue;
    }
    if (i < static_cast<int>(app->sceneObjectMeta.size())) {
      const auto& c = app->sceneObjectMeta[i].colorRgb;
      app->mesh.append(app->sceneObjects[i], glm::vec3(c[0], c[1], c[2]));
    } else {
      app->mesh.append(app->sceneObjects[i]);
    }
  }
  app->renderer.setMesh(app->mesh);
}

bool applyAddExtrude(AppState* app, const StlMesh& extruded,
                     const std::vector<int>& targetsRaw) {
  if (extruded.empty()) return false;
  pushObjectUndoSnapshot(app);

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
  {
    const glm::vec3 color = randomPastelColor();
    mergedMeta.colorRgb = {color.x, color.y, color.z};
  }
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
  pushObjectUndoSnapshot(app);
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
  {
    const glm::vec3 color = randomPastelColor();
    mergedMeta.colorRgb = {color.x, color.y, color.z};
  }
  nextMeta.push_back(mergedMeta);

  app->sceneObjects = std::move(next);
  app->sceneObjectMeta = std::move(nextMeta);
  app->selectedObject = static_cast<int>(app->sceneObjects.size()) - 1;
  app->browserSelectedObjects.assign(1, app->selectedObject);
  rebuildCombinedMesh(app);
  return true;
}

bool applyIntersectCombine(AppState* app, const std::vector<int>& targetsRaw,
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

  pushObjectUndoSnapshot(app);

  std::vector<Aabb> targetBoxes;
  targetBoxes.reserve(targets.size());
  for (int idx : targets) targetBoxes.push_back(meshAabb(app->sceneObjects[idx]));

  std::vector<Aabb> toolBoxes;
  toolBoxes.reserve(tools.size());
  for (int idx : tools) toolBoxes.push_back(meshAabb(app->sceneObjects[idx]));

  std::vector<int> overlapTargets;
  std::vector<int> overlapTools;
  for (size_t ti = 0; ti < targets.size(); ++ti) {
    for (size_t oi = 0; oi < tools.size(); ++oi) {
      if (aabbOverlap(targetBoxes[ti], toolBoxes[oi])) {
        addUniqueIndex(overlapTargets, targets[ti]);
        addUniqueIndex(overlapTools, tools[oi]);
      }
    }
  }

  if (overlapTargets.empty()) return false;

  StlMesh merged;
  for (int idx : overlapTargets) merged.append(app->sceneObjects[idx]);
  for (int idx : overlapTools) merged.append(app->sceneObjects[idx]);

  std::vector<bool> remove(app->sceneObjects.size(), false);
  for (int idx : targets) remove[idx] = true;
  if (!keepTools) {
    for (int idx : overlapTools) remove[idx] = true;
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
  const glm::vec3 color = randomPastelColor();
  mergedMeta.colorRgb = {color.x, color.y, color.z};
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
  pushObjectUndoSnapshot(app);

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
  pushObjectUndoSnapshot(app);
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

bool applyChamferEdges(AppState* app, int objectIndex,
                       const std::vector<ChamferEdgeSelection>& edges,
                       float distanceMm) {
  if (objectIndex < 0 || objectIndex >= static_cast<int>(app->sceneObjects.size())) return false;
  if (distanceMm <= 0.0f || edges.empty()) return false;
  if (objectIndex >= static_cast<int>(app->sceneObjectMeta.size()) ||
      app->sceneObjectMeta[objectIndex].locked) {
    return false;
  }

  pushObjectUndoSnapshot(app);

  const StlMesh& srcMesh = app->sceneObjects[objectIndex];
  auto verts = srcMesh.vertices();
  const auto& inds = srcMesh.indices();
  if (verts.empty() || inds.empty()) return false;

  auto nearPoint = [](const glm::vec3& p, const glm::vec3& q) {
    const glm::vec3 d = p - q;
    return glm::dot(d, d) <= 1e-6f;
  };

  bool changed = false;
  for (const auto& edge : edges) {
    if (edge.objectIndex != objectIndex) continue;
    const float edgeLen = glm::length(edge.b - edge.a);
    if (edgeLen <= 1e-6f) continue;
    const float t = std::clamp(distanceMm / edgeLen, 0.0f, 0.45f);
    if (t <= 0.0f) continue;
    const glm::vec3 mid = 0.5f * (edge.a + edge.b);

    for (auto& v : verts) {
      if (nearPoint(v.position, edge.a) || nearPoint(v.position, edge.b)) {
        v.position = glm::mix(v.position, mid, t);
        changed = true;
      }
    }
  }

  if (!changed) return false;

  for (size_t i = 0; i + 2 < inds.size(); i += 3) {
    StlVertex& va = verts[inds[i + 0]];
    StlVertex& vb = verts[inds[i + 1]];
    StlVertex& vc = verts[inds[i + 2]];
    glm::vec3 n = glm::normalize(glm::cross(vb.position - va.position, vc.position - va.position));
    if (glm::dot(n, n) < 1e-8f) {
      n = {0.0f, 0.0f, 1.0f};
    }
    va.normal = n;
    vb.normal = n;
    vc.normal = n;
  }

  app->sceneObjects[objectIndex] = StlMesh::fromGeometry(std::move(verts), inds);
  app->selectedObject = objectIndex;
  app->browserSelectedObjects.assign(1, objectIndex);
  rebuildCombinedMesh(app);
  return true;
}

bool applyFilletEdges(AppState* app, int objectIndex,
                      const std::vector<ChamferEdgeSelection>& edges,
                      float radiusMm) {
  if (objectIndex < 0 || objectIndex >= static_cast<int>(app->sceneObjects.size())) return false;
  if (radiusMm <= 0.0f || edges.empty()) return false;
  if (objectIndex >= static_cast<int>(app->sceneObjectMeta.size()) ||
      app->sceneObjectMeta[objectIndex].locked) {
    return false;
  }

  const StlMesh& srcMesh = app->sceneObjects[objectIndex];
  auto verts = srcMesh.vertices();
  const auto& inds = srcMesh.indices();
  if (verts.empty() || inds.empty()) return false;

  pushObjectUndoSnapshot(app);

  auto nearestPointOnSegment = [](glm::vec3 p, glm::vec3 a, glm::vec3 b) {
    const glm::vec3 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    if (len2 <= 1e-12f) return a;
    const float t = glm::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f);
    return a + ab * t;
  };

  std::vector<std::vector<uint32_t>> adjacency(verts.size());
  for (size_t i = 0; i + 2 < inds.size(); i += 3) {
    const uint32_t ia = inds[i + 0];
    const uint32_t ib = inds[i + 1];
    const uint32_t ic = inds[i + 2];
    adjacency[ia].push_back(ib);
    adjacency[ia].push_back(ic);
    adjacency[ib].push_back(ia);
    adjacency[ib].push_back(ic);
    adjacency[ic].push_back(ia);
    adjacency[ic].push_back(ib);
  }
  for (auto& neighbors : adjacency) {
    std::sort(neighbors.begin(), neighbors.end());
    neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
  }

  std::vector<glm::vec3> nextPos(verts.size());
  for (size_t i = 0; i < verts.size(); ++i) nextPos[i] = verts[i].position;

  bool changed = false;
  const float influenceRadius = radiusMm * 1.75f;
  for (const auto& edge : edges) {
    if (edge.objectIndex != objectIndex) continue;
    for (size_t i = 0; i < verts.size(); ++i) {
      if (adjacency[i].empty()) continue;
      const glm::vec3 onEdge = nearestPointOnSegment(verts[i].position, edge.a, edge.b);
      const float dist = glm::length(verts[i].position - onEdge);
      if (dist > influenceRadius) continue;

      glm::vec3 neighborAverage(0.0f);
      for (uint32_t neighbor : adjacency[i]) {
        neighborAverage += verts[neighbor].position;
      }
      neighborAverage /= static_cast<float>(adjacency[i].size());

      const float blend = glm::clamp(1.0f - dist / influenceRadius, 0.0f, 1.0f) * 0.65f;
      nextPos[i] = glm::mix(nextPos[i], neighborAverage, blend);
      changed = true;
    }
  }

  if (!changed) return false;

  for (size_t i = 0; i < verts.size(); ++i) {
    verts[i].position = nextPos[i];
  }

  for (size_t i = 0; i + 2 < inds.size(); i += 3) {
    StlVertex& va = verts[inds[i + 0]];
    StlVertex& vb = verts[inds[i + 1]];
    StlVertex& vc = verts[inds[i + 2]];
    glm::vec3 n = glm::normalize(glm::cross(vb.position - va.position, vc.position - va.position));
    if (glm::dot(n, n) < 1e-8f) {
      n = {0.0f, 0.0f, 1.0f};
    }
    va.normal = n;
    vb.normal = n;
    vc.normal = n;
  }

  app->sceneObjects[objectIndex] = StlMesh::fromGeometry(std::move(verts), inds);
  app->selectedObject = objectIndex;
  app->browserSelectedObjects.assign(1, objectIndex);
  rebuildCombinedMesh(app);
  return true;
}

bool applyDraftObject(AppState* app, int objectIndex, float angleDegrees) {
  if (!app) return false;
  if (objectIndex < 0 || objectIndex >= static_cast<int>(app->sceneObjects.size())) return false;
  if (objectIndex >= static_cast<int>(app->sceneObjectMeta.size()) ||
      app->sceneObjectMeta[objectIndex].locked) {
    return false;
  }
  if (!std::isfinite(angleDegrees)) return false;
  if (std::abs(angleDegrees) < 1e-4f) return false;

  const StlMesh& srcMesh = app->sceneObjects[objectIndex];
  auto verts = srcMesh.vertices();
  const auto& inds = srcMesh.indices();
  if (verts.empty() || inds.empty()) return false;

  glm::vec3 bmin = verts[0].position;
  glm::vec3 bmax = verts[0].position;
  for (const auto& v : verts) {
    bmin = glm::min(bmin, v.position);
    bmax = glm::max(bmax, v.position);
  }

  const float height = bmax.z - bmin.z;
  if (height <= 1e-6f) return false;

  glm::vec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
  float maxRadius = 0.0f;
  for (const auto& v : verts) {
    maxRadius = std::max(maxRadius, glm::length(glm::vec2(v.position.x, v.position.y) - center));
  }
  if (maxRadius <= 1e-6f) return false;

  constexpr float kPi = 3.14159265358979f;
  const float angleRad = angleDegrees * kPi / 180.0f;
  const float taperAtTop = std::tan(angleRad) * (height / maxRadius);
  if (!std::isfinite(taperAtTop)) return false;

  pushObjectUndoSnapshot(app);

  for (auto& v : verts) {
    const float t = glm::clamp((v.position.z - bmin.z) / height, 0.0f, 1.0f);
    const float scale = std::max(0.05f, 1.0f - taperAtTop * t);
    glm::vec2 p(v.position.x, v.position.y);
    p = center + (p - center) * scale;
    v.position.x = p.x;
    v.position.y = p.y;
  }

  for (size_t i = 0; i + 2 < inds.size(); i += 3) {
    StlVertex& va = verts[inds[i + 0]];
    StlVertex& vb = verts[inds[i + 1]];
    StlVertex& vc = verts[inds[i + 2]];
    glm::vec3 n = glm::normalize(glm::cross(vb.position - va.position, vc.position - va.position));
    if (glm::dot(n, n) < 1e-8f) {
      n = {0.0f, 0.0f, 1.0f};
    }
    va.normal = n;
    vb.normal = n;
    vc.normal = n;
  }

  app->sceneObjects[objectIndex] = StlMesh::fromGeometry(std::move(verts), inds);
  app->selectedObject = objectIndex;
  app->browserSelectedObjects.assign(1, objectIndex);
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
