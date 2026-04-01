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
  bash -lc "set -euo pipefail && rm -rf /tmp/camster-build-release dist-release-docker && cmake -S . -B /tmp/camster-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON && cmake --build /tmp/camster-build-release -j\$(nproc) && ctest --test-dir /tmp/camster-build-release --output-on-failure && cmake --install /tmp/camster-build-release --prefix /workspace/dist-release-docker --component Runtime && test -x /workspace/dist-release-docker/bin/camster && sha256sum /workspace/dist-release-docker/bin/camster > /workspace/dist-release-docker/bin/SHA256SUMS.txt"

echo "Portable release bundle: ./dist-release-docker/bin/camster"
echo "Release checksums: ./dist-release-docker/bin/SHA256SUMS.txt"
