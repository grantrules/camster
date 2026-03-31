#include <array>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>

#include "CameraController.hpp"
#include "StlMesh.hpp"
#include "VulkanRenderer.hpp"
#include "ui/FileBrowser.hpp"

namespace {
struct AppState {
  VulkanRenderer renderer;
  CameraController camera;
  StlMesh mesh;
  FileBrowser fileBrowser;

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

void framebufferResizeCallback(GLFWwindow* window, int, int) {
  auto* app = static_cast<AppState*>(glfwGetWindowUserPointer(window));
  if (app) {
    app->renderer.markFramebufferResized();
  }
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
        app.mesh = std::move(result.mesh);
        app.renderer.setMesh(app.mesh);
        app.status = "Loaded " + result.path;
      }
    }

    app.renderer.beginImGuiFrame();

  // WantCaptureMouse is true when ImGui has focus (e.g. hovering a window,
  // dragging a slider).  We skip scene input in that case so mouse events
  // don't simultaneously rotate the camera and interact with UI.
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
