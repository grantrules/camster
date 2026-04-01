#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

./scripts/docker-build-debug.sh
./scripts/docker-build-release.sh
./scripts/docker-build-windows.sh

if [ ! -f dist-release-docker/bin/SHA256SUMS.txt ]; then
  echo "Missing Linux release checksums" >&2
  exit 2
fi
if [ ! -f dist-windows-docker/SHA256SUMS.txt ]; then
  echo "Missing Windows release checksums" >&2
  exit 2
fi

echo "Release pipeline hardening checks PASSED"
