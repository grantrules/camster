#!/usr/bin/env bash
set -euo pipefail

if ! docker info >/dev/null 2>&1; then
  echo "Docker daemon is not accessible for this shell." >&2
  exit 1
fi

docker build -t camster-build:latest .

docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$(pwd)":/workspace \
  -w /workspace \
  camster-build:latest \
  bash -lc "rm -rf /tmp/camster-build-debug dist-debug-docker && cmake -S . -B /tmp/camster-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build /tmp/camster-build-debug -j\$(nproc) && cmake --install /tmp/camster-build-debug --prefix /workspace/dist-debug-docker --component Runtime"

echo "Portable debug bundle: ./dist-debug-docker/bin/camster"
