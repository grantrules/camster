#pragma once

#include <array>
#include <future>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "AppSettings.hpp"
#include "CameraController.hpp"
#include "Gizmo.hpp"
#include "History.hpp"
#include "Project.hpp"
#include "ProjectTypes.hpp"
#include "Scene.hpp"
#include "StlMesh.hpp"
#include "VulkanRenderer.hpp"
#include "sketch/ExtrudeTool.hpp"
#include "sketch/Sketch.hpp"
#include "sketch/SketchTool.hpp"
#include "ui/FileBrowser.hpp"
#include "ui/PrintWindow.hpp"
#include "ui/Toolbar.hpp"

// Forward declarations
class PrintSettings;

enum class BooleanOp { Add, Subtract };
enum class ExtrudeOp { Add, Subtract, CreateNewObject };
enum class ObjectPickMode { None, ExtrudeTargets, CombineTargets, CombineTools, ChamferEdges };
enum class BrowserSection { Objects, Sketches };

struct ChamferEdgeSelection {
  int objectIndex = -1;
  glm::vec3 a{0.0f};
  glm::vec3 b{0.0f};
};

struct ExtrudeOptionsState {
  bool visible = false;
  bool applyRequested = false;
  ExtrudeOp operation = ExtrudeOp::Add;
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

struct ChamferOptionsState {
  bool visible = false;
  bool pickEdges = false;
  int targetObject = -1;
  std::vector<ChamferEdgeSelection> edges;
  char distanceBuffer[64] = {};
};

struct SolidExtrudeOptionsState {
  bool visible = false;
  int sourceSketch = -1;
  std::vector<bool> profileSelected;
  char depthBuffer[64] = {};
};

struct SketchCreateState {
  bool open = false;
  bool pickFromScene = false;
  SketchPlane plane = SketchPlane::XY;
  float offsetMm = 0.0f;
  bool fromFace = false;
  int sourceObject = -1;
};

// Main application state
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
  ChamferOptionsState chamferOptions;
  SolidExtrudeOptionsState solidExtrudeOptions;

  // Scene / sketch state.
  SceneMode sceneMode = SceneMode::View3D;
  std::vector<SketchEntry> sketches;
  int activeSketchIndex = -1;
  int nextSketchNumber = 1;
  SketchCreateState sketchCreate;
  std::optional<SketchPlane> hoveredPlaneIndicator;  // for gizmo hover feedback
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
  int timelineCursor = -1;  // index into timeline entries; -1 means no selection yet
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
