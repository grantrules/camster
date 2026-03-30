#!/usr/bin/env bash
set -euo pipefail

if ! docker info >/dev/null 2>&1; then
  if [[ "${CAMSTER_DOCKER_GROUP_RETRY:-0}" != "1" ]] && command -v sg >/dev/null 2>&1; then
    exec sg docker -c "CAMSTER_DOCKER_GROUP_RETRY=1 $0"
  fi
  echo "Docker daemon is not accessible for this shell. Try: sg docker -c '$0'" >&2
  exit 1
fi

docker build -f Dockerfile.windows -t camster-build-windows:latest .

docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$(pwd)":/workspace \
  -w /workspace \
  camster-build-windows:latest \
  bash -lc "rm -rf /tmp/camster-build-windows dist-windows-docker && cmake -S . -B /tmp/camster-build-windows -G Ninja -DCMAKE_TOOLCHAIN_FILE=/workspace/cmake/toolchains/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/camster-build-windows -j\$(nproc) && cmake --install /tmp/camster-build-windows --prefix /workspace/dist-windows-docker --component Runtime && for dll in libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll libssp-0.dll libatomic-1.dll; do path=\$(x86_64-w64-mingw32-g++-posix -print-file-name=\$dll); if [ -f \"\$path\" ]; then cp \"\$path\" /workspace/dist-windows-docker/bin/; fi; done && cd /workspace/dist-windows-docker && cmake -E tar cfv camster-windows.zip --format=zip bin"

echo "Portable windows bundle: ./dist-windows-docker/bin/camster.exe"
echo "Portable windows zip: ./dist-windows-docker/camster-windows.zip"
