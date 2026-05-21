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

add_cmake_prefix() {
  local prefix="$1"
  export CMAKE_PREFIX_PATH="$prefix${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
}

if [ -n "${QTKEYCHAIN_PREFIX:-}" ]; then
  add_cmake_prefix "$QTKEYCHAIN_PREFIX"
  if [ -d "$QTKEYCHAIN_PREFIX/bin" ]; then
    export PATH="$QTKEYCHAIN_PREFIX/bin:$PATH"
  fi
fi

if command -v brew >/dev/null 2>&1; then
  qt5_prefix="$(brew --prefix qt@5 2>/dev/null || true)"
  if [ -n "$qt5_prefix" ]; then
    export PATH="$qt5_prefix/bin:$PATH"
    add_cmake_prefix "$qt5_prefix"
  fi

  if [ -z "${QTKEYCHAIN_PREFIX:-}" ]; then
    qtkeychain_prefix="$(brew --prefix qtkeychain 2>/dev/null || true)"
    if [ -n "$qtkeychain_prefix" ]; then
      add_cmake_prefix "$qtkeychain_prefix"
    fi
  fi
fi

cmake_args=(
  -S "$ROOT_DIR"
  -B "$BUILD_DIR"
  -G "$GENERATOR"
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
  -DSMB_BROWSER_WITH_LIBSMB2=OFF
  -DSMB_BROWSER_WITH_NATIVE_SMB=ON
  -DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=OFF
  -DCPACK_GENERATOR="$CPACK_GENERATOR"
)

if [ "$SKIP_TESTS" = "1" ]; then
  cmake_args+=(-DBUILD_TESTING=OFF)
fi

stage_qtkeychain_runtime() {
  local frameworks_dir="$APP_PATH/Contents/Frameworks"
  local staged=0
  local roots=()

  mkdir -p "$frameworks_dir"
  if [ -n "${QTKEYCHAIN_PREFIX:-}" ]; then
    roots+=("$QTKEYCHAIN_PREFIX")
  elif command -v brew >/dev/null 2>&1; then
    qtkeychain_prefix="$(brew --prefix qtkeychain 2>/dev/null || true)"
    if [ -n "$qtkeychain_prefix" ]; then
      roots+=("$qtkeychain_prefix")
    fi
  fi

  for root in "${roots[@]}"; do
    if [ -d "$root/lib/Qt5Keychain.framework" ]; then
      rm -rf "$frameworks_dir/Qt5Keychain.framework"
      cp -R "$root/lib/Qt5Keychain.framework" "$frameworks_dir/"
      staged=1
    fi

    for lib in "$root"/lib/libqt5keychain*.dylib; do
      if [ -e "$lib" ]; then
        cp -P "$lib" "$frameworks_dir/"
        staged=1
      fi
    done
  done

  if [ "$staged" != "1" ]; then
    echo "QtKeychain runtime not found; set QTKEYCHAIN_PREFIX or install a Qt5 build." >&2
    exit 1
  fi

  for lib in "$frameworks_dir"/libqt5keychain*.dylib; do
    if [ -e "$lib" ] && [ ! -L "$lib" ]; then
      chmod u+w "$lib" || true
      install_name_tool -id "@executable_path/../Frameworks/$(basename "$lib")" \
        "$lib" 2>/dev/null || true
    fi
  done

  if otool -L "$APP_PATH/Contents/MacOS/smb-browser" | grep -q 'libqt5keychain'; then
    otool -L "$APP_PATH/Contents/MacOS/smb-browser" |
      awk '/libqt5keychain/ {print $1}' |
      while read -r ref; do
        lib_name="$(basename "$ref")"
        if [ -e "$frameworks_dir/$lib_name" ]; then
          install_name_tool -change "$ref" \
            "@executable_path/../Frameworks/$lib_name" \
            "$APP_PATH/Contents/MacOS/smb-browser"
        fi
      done
  fi
}

cmake "${cmake_args[@]}"

cmake --build "$BUILD_DIR" --config "$BUILD_TYPE"

if [ "$SKIP_TESTS" != "1" ]; then
  ctest --test-dir "$BUILD_DIR" --output-on-failure -C "$BUILD_TYPE"
fi

APP_PATH="$BUILD_DIR/src/app/smb-browser.app"
if command -v macdeployqt >/dev/null 2>&1 && [ -d "$APP_PATH" ]; then
  macdeployqt "$APP_PATH" -verbose=1
  stage_qtkeychain_runtime
else
  echo "macdeployqt not found or app bundle missing; Qt frameworks cannot be staged." >&2
  exit 1
fi

rm -rf "$BUILD_DIR/packages"
if ! cpack -G "$CPACK_GENERATOR" --config "$BUILD_DIR/CPackConfig.cmake"; then
  package_path="$(find "$BUILD_DIR/packages" \( -name '*.dmg' -o -name 'smb-browser.app' \) \
    -print 2>/dev/null | sort | tail -1)"
  if [ -z "$package_path" ]; then
    echo "CPack failed and no package was created under $BUILD_DIR/packages" >&2
    exit 1
  fi
  echo "CPack returned a nonzero status after creating $package_path; continuing." >&2
fi

if [ "$SKIP_SMOKE" != "1" ]; then
  package_path="$(find "$BUILD_DIR/packages" \( -name '*.dmg' -o -name 'smb-browser.app' \) \
    -print 2>/dev/null | sort | tail -1)"
  if [ -z "$package_path" ]; then
    echo "Package not found under $BUILD_DIR/packages" >&2
    exit 1
  fi
  "$ROOT_DIR/scripts/package-smoke-macos.sh" "$package_path"
fi
