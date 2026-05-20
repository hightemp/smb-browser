#!/usr/bin/env bash
set -euo pipefail

if [ "$(uname -s)" != "Darwin" ]; then
  echo "This packaging profile must be run on macOS." >&2
  exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/tmp/package-macos}"
GENERATOR="${GENERATOR:-Ninja}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
SKIP_TESTS="${SKIP_TESTS:-0}"
SKIP_SMOKE="${SKIP_SMOKE:-0}"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G "$GENERATOR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DSMB_BROWSER_WITH_LIBSMB2=OFF \
  -DSMB_BROWSER_WITH_NATIVE_SMB=ON \
  -DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=OFF

cmake --build "$BUILD_DIR" --config "$BUILD_TYPE"

if [ "$SKIP_TESTS" != "1" ]; then
  ctest --test-dir "$BUILD_DIR" --output-on-failure -C "$BUILD_TYPE"
fi

APP_PATH="$BUILD_DIR/src/app/smb-browser.app"
if command -v macdeployqt >/dev/null 2>&1 && [ -d "$APP_PATH" ]; then
  macdeployqt "$APP_PATH" -verbose=1
else
  echo "macdeployqt not found or app bundle missing; ensure Qt frameworks are staged before publishing." >&2
fi

cmake --build "$BUILD_DIR" --target package --config "$BUILD_TYPE"

if [ "$SKIP_SMOKE" != "1" ]; then
  "$ROOT_DIR/scripts/package-smoke-macos.sh"
fi
