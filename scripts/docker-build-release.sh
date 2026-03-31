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
  bash -lc "rm -rf /tmp/camster-build-release dist-release-docker && cmake -S . -B /tmp/camster-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/camster-build-release -j\$(nproc) && cmake --install /tmp/camster-build-release --prefix /workspace/dist-release-docker --component Runtime"

echo "Portable release bundle: ./dist-release-docker/bin/camster"
