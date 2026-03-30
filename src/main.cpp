#include <array>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef __linux__
#include <unistd.h>
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>

extern "C" {
#include <tinyfiledialogs.h>
}

#include "CameraController.hpp"
#include "PanelConfig.hpp"
#include "StlMesh.hpp"
#include "VulkanRenderer.hpp"

namespace {
struct AppState {
  VulkanRenderer renderer;
  CameraController camera;
  StlMesh mesh;
  PanelConfig panel;

  std::string status = "Ready";
  bool dragging = false;

  struct LoadResult {
    bool success = false;
    StlMesh mesh;
    std::string path;
    std::string error;
  };

  std::future<LoadResult> pendingLoad;
  bool loadingMesh = false;
};

CameraController::Orientation parseOrientation(const std::string& value) {
  if (value == "front") return CameraController::Orientation::Front;
  if (value == "back") return CameraController::Orientation::Back;
  if (value == "left") return CameraController::Orientation::Left;
  if (value == "right") return CameraController::Orientation::Right;
  if (value == "top") return CameraController::Orientation::Top;
  if (value == "bottom") return CameraController::Orientation::Bottom;
  return CameraController::Orientation::Isometric;
}

std::filesystem::path panelScriptPath() {
  std::filesystem::path exeDir;
#ifdef __linux__
  std::array<char, 4096> exePath{};
  const ssize_t len = readlink("/proc/self/exe", exePath.data(), exePath.size() - 1);
  if (len > 0) {
    exePath[static_cast<size_t>(len)] = '\0';
    exeDir = std::filesystem::path(exePath.data()).parent_path();
  }
#endif

  const std::vector<std::filesystem::path> candidates = {
      "assets/panel_config.py",
      "../assets/panel_config.py",
      std::filesystem::current_path() / "assets" / "panel_config.py",
      exeDir / "assets" / "panel_config.py",
      exeDir.parent_path() / "assets" / "panel_config.py",
  };

  for (const auto& path : candidates) {
    if (std::filesystem::exists(path)) {
      return path;
    }
  }

  return "assets/panel_config.py";
}

void framebufferResizeCallback(GLFWwindow* window, int, int) {
  auto* app = static_cast<AppState*>(glfwGetWindowUserPointer(window));
  if (app) {
    app->renderer.markFramebufferResized();
  }
}

std::optional<std::string> openStlDialog() {
  const char* patterns[] = {"*.stl"};
  const char* result = tinyfd_openFileDialog("Open STL", "", 1, patterns, "STL files", 0);
  if (!result || std::string(result).empty()) {
    return std::nullopt;
  }
  return std::string(result);
}

std::optional<std::string> saveStlDialog() {
  const char* patterns[] = {"*.stl"};
  const char* result = tinyfd_saveFileDialog("Export STL", "exported.stl", 1, patterns, "STL files");
  if (!result || std::string(result).empty()) {
    return std::nullopt;
  }
  return std::string(result);
}

void handleButtonAction(AppState* app, const PanelButton& button) {
  if (button.action == "open") {
    const auto path = openStlDialog();
    if (!path) {
      app->status = "Open canceled.";
      return;
    }

    if (app->loadingMesh) {
      app->status = "Already loading a mesh.";
      return;
    }

    const std::string selectedPath = *path;
    app->loadingMesh = true;
    app->status = "Loading " + selectedPath + "...";
    app->pendingLoad = std::async(std::launch::async, [selectedPath]() {
      AppState::LoadResult result;
      result.path = selectedPath;

      StlMesh loaded;
      std::string loadError;
      if (!loaded.loadFromFile(selectedPath, loadError)) {
        result.success = false;
        result.error = loadError;
        return result;
      }

      result.success = true;
      result.mesh = std::move(loaded);
      return result;
    });
    return;
  }

  if (button.action == "export") {
    const auto path = saveStlDialog();
    if (!path) {
      app->status = "Export canceled.";
      return;
    }

    std::string exportError;
    if (!app->mesh.saveAsBinary(*path, exportError)) {
      app->status = "Export failed: " + exportError;
      return;
    }

    app->status = "Exported " + *path;
    return;
  }

  if (button.action == "snap") {
    app->camera.snap(parseOrientation(button.argument));
    return;
  }

  if (button.action == "toggle_wireframe") {
    if (!app->renderer.wireframeSupported()) {
      app->status = "Wireframe not supported on this GPU.";
      return;
    }

    app->renderer.toggleWireframe();
    app->status = app->renderer.wireframeEnabled() ? "Wireframe: ON" : "Wireframe: OFF";
    return;
  }

  if (button.action == "toggle_normals") {
    app->renderer.toggleNormalVisualization();
    app->status =
        app->renderer.normalVisualizationEnabled() ? "Normals view: ON" : "Normals view: OFF";
  }
}

void drawPanel(AppState* app) {
  ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(240.0f, 0.0f), ImGuiCond_FirstUseEver);

  ImGui::Begin("Actions");
  for (const auto& button : app->panel.buttons) {
    if (ImGui::Button(button.label.c_str(), ImVec2(-1.0f, 0.0f))) {
      handleButtonAction(app, button);
    }
  }

  ImGui::Separator();
  ImGui::TextWrapped("LMB drag: rotate");
  ImGui::TextWrapped("Mouse wheel: zoom");
  ImGui::TextWrapped("Triangles: %d", static_cast<int>(app->mesh.indices().size() / 3));

  ImGui::Separator();
  ImGui::TextUnformatted("Settings");
  ImGui::TextWrapped("Validation: %s", app->renderer.validationEnabled() ? "ON" : "OFF");
  ImGui::TextWrapped("Wireframe Support: %s", app->renderer.wireframeSupported() ? "YES" : "NO");
  ImGui::TextWrapped("Wireframe: %s", app->renderer.wireframeEnabled() ? "ON" : "OFF");
  ImGui::TextWrapped("Normals View: %s",
                     app->renderer.normalVisualizationEnabled() ? "ON" : "OFF");

  if (app->loadingMesh) {
    const double t = ImGui::GetTime();
    const int phase = static_cast<int>(t * 3.0) % 4;
    const char* suffix = "";
    if (phase == 1) suffix = ".";
    if (phase == 2) suffix = "..";
    if (phase == 3) suffix = "...";
    ImGui::Text("Loading mesh%s", suffix);
  }

  ImGui::Separator();
  ImGui::TextWrapped("Status: %s", app->status.c_str());
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

  app.mesh = StlMesh::makeUnitCube();
  app.renderer.setMesh(app.mesh);

  std::string panelWarning;
  app.panel = PanelConfig::loadFromPythonScript(panelScriptPath().string(), panelWarning);
  if (!panelWarning.empty()) {
    app.status = panelWarning;
  }

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    if (app.loadingMesh && app.pendingLoad.valid() &&
        app.pendingLoad.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      AppState::LoadResult result = app.pendingLoad.get();
      app.loadingMesh = false;

      if (!result.success) {
        app.status = "Open failed: " + result.error;
      } else {
        app.mesh = std::move(result.mesh);
        app.renderer.setMesh(app.mesh);
        app.status = "Loaded " + result.path;
      }
    }

    app.renderer.beginImGuiFrame();

    const ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse) {
      double x = 0.0;
      double y = 0.0;
      glfwGetCursorPos(window, &x, &y);
      const bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

      if (leftDown && !app.dragging) {
        app.dragging = true;
        app.camera.beginRotate(x, y);
      } else if (leftDown && app.dragging) {
        app.camera.rotateTo(x, y);
      } else if (!leftDown && app.dragging) {
        app.dragging = false;
        app.camera.endRotate();
      }

      const float wheel = io.MouseWheel;
      if (wheel != 0.0f) {
        app.camera.zoom(wheel);
      }
    }

    drawPanel(&app);

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
