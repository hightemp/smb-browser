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
CPACK_GENERATOR="${CPACK_GENERATOR:-DragNDrop}"

if command -v brew >/dev/null 2>&1; then
  qt5_prefix="$(brew --prefix qt@5 2>/dev/null || true)"
  if [ -n "$qt5_prefix" ]; then
    export PATH="$qt5_prefix/bin:$PATH"
    export CMAKE_PREFIX_PATH="$qt5_prefix${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
  fi

  qtkeychain_prefix="$(brew --prefix qtkeychain 2>/dev/null || true)"
  if [ -n "$qtkeychain_prefix" ]; then
    export CMAKE_PREFIX_PATH="$qtkeychain_prefix${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
  fi
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G "$GENERATOR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DSMB_BROWSER_WITH_LIBSMB2=OFF \
  -DSMB_BROWSER_WITH_NATIVE_SMB=ON \
  -DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=OFF \
  -DCPACK_GENERATOR="$CPACK_GENERATOR"

cmake --build "$BUILD_DIR" --config "$BUILD_TYPE"

if [ "$SKIP_TESTS" != "1" ]; then
  ctest --test-dir "$BUILD_DIR" --output-on-failure -C "$BUILD_TYPE"
fi

APP_PATH="$BUILD_DIR/src/app/smb-browser.app"
if command -v macdeployqt >/dev/null 2>&1 && [ -d "$APP_PATH" ]; then
  macdeployqt "$APP_PATH" -verbose=1
else
  echo "macdeployqt not found or app bundle missing; Qt frameworks cannot be staged." >&2
  exit 1
fi

rm -rf "$BUILD_DIR/packages"
cpack -G "$CPACK_GENERATOR" --config "$BUILD_DIR/CPackConfig.cmake"

if [ "$SKIP_SMOKE" != "1" ]; then
  package_path="$(find "$BUILD_DIR/packages" \( -name '*.dmg' -o -name 'smb-browser.app' \) \
    -print 2>/dev/null | sort | tail -1)"
  if [ -z "$package_path" ]; then
    echo "Package not found under $BUILD_DIR/packages" >&2
    exit 1
  fi
  "$ROOT_DIR/scripts/package-smoke-macos.sh" "$package_path"
fi
