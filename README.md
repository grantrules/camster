# camster

Cross-platform C/C++ Vulkan STL viewer scaffold with a custom in-app ImGui file browser.

## Features

- Vulkan rendering backend (GLFW surface + Dear ImGui overlay)
- Depth-buffered mesh rendering
- STL loading (ASCII and binary)
- In-app file browser for opening STL files
- GPU-local vertex/index buffers uploaded through staging buffers
- Optional Vulkan validation layers in Debug builds
- Asynchronous STL loading to avoid UI stalls on large files
- On-screen status indicators for validation mode, wireframe capability, and load progress
- Mouse orbit camera
  - Rotate: left-drag
  - Zoom: mouse wheel

## Build

Requirements:

- CMake 3.22+
- A C++20 compiler
- Vulkan SDK tools (`glslc` preferred, `glslangValidator` also supported)
- Git (for CMake FetchContent dependencies)

Build:

```bash
cmake -S . -B build
cmake --build build -j
```

Run:

```bash
./build/camster
```

## Build With Docker

This project includes a Docker-based build toolchain so you do not need local Vulkan/C++ prerequisites.

Build debug:

```bash
./scripts/docker-build-debug.sh
```

This writes a portable bundle to:

```bash
./dist-debug-docker/bin/camster
./dist-debug-docker/bin/shaders/
./dist-debug-docker/bin/assets/
```

Build release:

```bash
./scripts/docker-build-release.sh
```

This writes a portable bundle to:

```bash
./dist-release-docker/bin/camster
./dist-release-docker/bin/shaders/
./dist-release-docker/bin/assets/
```

Clean generated build and dist outputs:

```bash
./scripts/clean.sh
```

Alternative with Docker Compose:

```bash
docker compose run --rm camster-build \
  bash -lc "rm -rf /tmp/camster-build-debug dist-debug-docker && cmake -S . -B /tmp/camster-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build /tmp/camster-build-debug -j\$(nproc) && cmake --install /tmp/camster-build-debug --prefix /workspace/dist-debug-docker --component Runtime"
```

Run from the bundled location:

```bash
cd dist-debug-docker/bin
./camster
```

Windows cross-build with Docker:

```bash
./scripts/docker-build-windows.sh
```

This writes:

```bash
./dist-windows-docker/bin/camster.exe
./dist-windows-docker/bin/shaders/
./dist-windows-docker/bin/assets/
./dist-windows-docker/bin/*.dll
./dist-windows-docker/camster-windows.zip
```

The Windows bundle includes the common MinGW runtime DLLs next to `camster.exe` so it is self-contained apart from the system Vulkan runtime/driver.

Windows cross-build with Docker Compose:

```bash
docker compose run --rm camster-build-windows \
  bash -lc "rm -rf /tmp/camster-build-windows dist-windows-docker && cmake -S . -B /tmp/camster-build-windows -G Ninja -DCMAKE_TOOLCHAIN_FILE=/workspace/cmake/toolchains/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/camster-build-windows -j\$(nproc) && cmake --install /tmp/camster-build-windows --prefix /workspace/dist-windows-docker --component Runtime"
```

## Current UI Scope

Phase 1 is focused on opening files with the in-app file browser.

- Implemented now:
  - `Open` via custom modal file browser (`src/ui/FileBrowser.*`)
  - Render toggles (wireframe/normals)
  - Camera snap presets
- Intentionally not implemented yet:
  - `Save`, `Import`, `Export`
  - Python/plugin-driven UI behavior

## Notes

- This implementation uses depth attachments and staging uploads for more production-like rendering behavior.
- Additional hardening options: validation layers, GPU upload batching, and asynchronous asset streaming.

## Vulkan Learning Resources

If you want to understand both Vulkan in general and why this repo is structured the way it is, these are strong references:

1. Vulkan 1.3 Spec (Khronos)
  - https://registry.khronos.org/vulkan/specs/1.3-extensions/html/
  - Authoritative source for object lifetimes, synchronization rules, and valid usage.

2. Vulkan Guide (Khronos)
  - https://docs.vulkan.org/guide/latest/
  - Practical explanations and modern Vulkan guidance that complements the full spec.

3. Vulkan Tutorial
  - https://vulkan-tutorial.com/
  - Great step-by-step walkthrough for the exact initialization path used in this codebase.

4. GPUOpen Vulkan Barrier Examples
  - https://gpuopen-librariesandsdks.github.io/Vulkan-Samples/samples/performance/pipeline_barriers/
  - Helpful for understanding pipeline stages/access masks and sync hazards.

5. Khronos Vulkan Samples (repo)
  - https://github.com/KhronosGroup/Vulkan-Samples
  - Official sample set with rendering, synchronization, and performance examples.

6. Sascha Willems Vulkan (repo)
  - https://github.com/SaschaWillems/Vulkan
  - Widely used educational sample collection, including descriptors, pipelines, and model rendering.

7. vkguide.dev + vkguide repo
  - https://vkguide.dev/
  - https://github.com/vblanco20-1/vulkan-guide
  - Good for practical engine-style Vulkan architecture and modern patterns.

8. NVIDIA nvpro Vulkan samples (repo)
  - https://github.com/nvpro-samples
  - Advanced techniques and production-style rendering patterns.

9. Dear ImGui Vulkan backend docs/source
  - https://github.com/ocornut/imgui/tree/master/backends
  - Useful for understanding how ImGui is integrated into an existing Vulkan render pass.

### Resource-to-Repo Map

Use this map when reading the resources above and comparing to this project:

- Renderer lifecycle and swapchain flow: `src/VulkanRenderer.cpp`, `src/VulkanRenderer.hpp`
- Mesh upload path (staging -> device local): `src/VulkanRenderer.cpp` (`uploadMeshBuffers`, `copyBuffer`)
- Camera/view-projection integration: `src/CameraController.cpp`, `src/main.cpp`
- Shader inputs and UBO usage: `src/shaders/mesh.vert`, `src/shaders/mesh.frag`
- Frame loop and UI wiring: `src/main.cpp`, `src/ui/FileBrowser.cpp`
