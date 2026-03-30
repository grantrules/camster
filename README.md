# camster

Cross-platform C/C++ Vulkan STL viewer scaffold with a Python-configurable action panel.

## Features

- Vulkan rendering backend (GLFW surface + Dear ImGui overlay)
- Depth-buffered mesh rendering
- STL loading (ASCII and binary)
- Mesh export to binary STL
- Native OS file dialogs for open/save
- GPU-local vertex/index buffers uploaded through staging buffers
- Optional Vulkan validation layers in Debug builds
- Asynchronous STL loading to avoid UI stalls on large files
- Panel settings/status indicators for validation mode, wireframe capability, and load progress
- Mouse orbit camera
  - Rotate: left-drag
  - Zoom: mouse wheel
- Panel buttons configured by Python output (`assets/panel_config.py`)

## Build

Requirements:

- CMake 3.22+
- A C++20 compiler
- Vulkan SDK (with `glslc` available on `PATH`)
- Python 3
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

## Python Panel Configuration

At startup, the app runs `assets/panel_config.py` and expects JSON on stdout with this shape:

```json
{
  "buttons": [
    {"label": "Open", "action": "open"},
    {"label": "Export", "action": "export"},
    {"label": "Snap Front", "action": "snap", "argument": "front"}
  ]
}
```

Supported actions:

- `open`
- `export`
- `snap` with `argument` in: `front`, `back`, `left`, `right`, `top`, `bottom`, `isometric`
- `toggle_wireframe`
- `toggle_normals`

If the script fails or returns invalid JSON, the viewer falls back to built-in defaults.

## Notes

- This implementation uses depth attachments and staging uploads for more production-like rendering behavior.
- Native file dialogs are provided via tinyfiledialogs.
- Additional hardening options: validation layers, GPU upload batching, and asynchronous asset streaming.
