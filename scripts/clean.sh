#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cd "$ROOT_DIR"

paths=(
  build
  build-debug
  build-debug-docker
  build-release-docker
  build-windows-docker
  dist-debug-docker
  dist-release-docker
  dist-windows-docker
)

removed_any=0
for path in "${paths[@]}"; do
  if [[ -e "$path" ]]; then
    rm -rf "$path"
    echo "Removed $path"
    removed_any=1
  fi
done

if [[ "$removed_any" -eq 0 ]]; then
  echo "Nothing to clean."
fi