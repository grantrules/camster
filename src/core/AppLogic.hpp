#pragma once

#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

#include "ColorVertex.hpp"
#include "core/AppState.hpp"

struct FacePickResult {
  int objectIndex = -1;
  int triangleOffset = -1;
  glm::vec3 hitPoint{0.0f};
  glm::vec3 normal{0.0f, 0.0f, 1.0f};
};

const char* browserRowLabel(bool visible, bool locked);

void addUniqueIndex(std::vector<int>& list, int idx);
void sanitizeObjectIndices(std::vector<int>& list, int objectCount);
void eraseIndex(std::vector<int>& list, int idx);
bool hasIndex(const std::vector<int>& list, int idx);
void setSingleOrMultiSelection(std::vector<int>& list, int idx, bool multiSelect);
void setSelectionRange(std::vector<int>& selection, int start, int end);
void stepSelection(std::vector<int>& selection, int count, int delta);

void syncSelectedObjectFromBrowser(AppState* app);
void syncSelectedPlaneFromBrowser(AppState* app);
void syncSelectedSketchFromBrowser(AppState* app);

void initializeDefaultPlanes(AppState* app);
int defaultPlaneId(SketchPlane plane);
int findPlaneIndexById(const AppState* app, int planeId);
ResolvedPlane resolvePlane(const AppState* app, int planeId);
void syncSketchPlanes(AppState* app);
std::string planeName(const AppState* app, int planeIndex);

void clearSketches(AppState* app);
void snapCameraToPlane(AppState* app, SketchPlane plane);

std::string objectName(const AppState* app, int idx);
std::vector<std::string> objectNames(const AppState* app, const std::vector<int>& indices);

void createSketch(AppState* app, int planeId);
void createOffsetPlaneFromPlane(AppState* app, int parentPlaneId, float distanceMm);
void createOffsetPlaneFromFace(AppState* app, int sourceObjectIndex,
                               SketchPlane plane, int faceSign, float distanceMm);
void appendSceneObject(AppState* app, StlMesh mesh);
void clearSceneObjects(AppState* app);

void beginObjectRename(AppState* app, int idx);
void beginSketchRename(AppState* app, int idx);
void commitObjectRename(AppState* app);
void commitSketchRename(AppState* app);
void cancelBrowserRename(AppState* app);

void deleteSceneObjects(AppState* app, const std::vector<int>& rawIndices);

void rebuildCombinedMesh(AppState* app);

bool applyAddExtrude(AppState* app, const StlMesh& extruded,
                     const std::vector<int>& targetsRaw);
bool applyAddCombine(AppState* app, const std::vector<int>& targetsRaw,
                     const std::vector<int>& toolsRaw, bool keepTools);
bool applySubtractExtrude(AppState* app, const StlMesh& extruded,
                          const std::vector<int>& targetsRaw);
bool applySubtractCombine(AppState* app, const std::vector<int>& targetsRaw,
                          const std::vector<int>& toolsRaw, bool keepTools);
bool applyChamferEdges(AppState* app, int objectIndex,
                       const std::vector<ChamferEdgeSelection>& edges,
                       float distanceMm);

int pickObject(const AppState& app, glm::vec3 rayO, glm::vec3 rayD);
std::optional<FacePickResult> pickObjectFace(const AppState& app, glm::vec3 rayO, glm::vec3 rayD);
SketchPlane sketchPlaneFromNormal(glm::vec3 n);

void appendGrid(std::vector<ColorVertex>& lines, SketchPlane plane, float extent, float spacing);

void exitSketchMode(AppState* app);
