#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIBSMB2_REPOSITORY="${LIBSMB2_REPOSITORY:-https://github.com/sahlberg/libsmb2.git}"
LIBSMB2_TAG="${LIBSMB2_TAG:-libsmb2-6.2}"
LIBSMB2_SOURCE_DIR="${LIBSMB2_SOURCE_DIR:-$ROOT_DIR/tmp/libsmb2-src}"
LIBSMB2_BUILD_DIR="${LIBSMB2_BUILD_DIR:-$ROOT_DIR/tmp/libsmb2-build}"
LIBSMB2_PREFIX="${LIBSMB2_PREFIX:-$ROOT_DIR/tmp/libsmb2-prefix}"

if ! command -v git >/dev/null 2>&1; then
  echo "git is required" >&2
  exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake is required" >&2
  exit 1
fi

if [ ! -d "$LIBSMB2_SOURCE_DIR/.git" ]; then
  rm -rf "$LIBSMB2_SOURCE_DIR"
  mkdir -p "$(dirname "$LIBSMB2_SOURCE_DIR")"
  git clone --depth 1 --branch "$LIBSMB2_TAG" \
    "$LIBSMB2_REPOSITORY" "$LIBSMB2_SOURCE_DIR"
else
  git -C "$LIBSMB2_SOURCE_DIR" fetch --depth 1 origin "$LIBSMB2_TAG"
  git -C "$LIBSMB2_SOURCE_DIR" checkout FETCH_HEAD
fi

GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
  GENERATOR_ARGS=(-G Ninja)
fi

cmake -S "$LIBSMB2_SOURCE_DIR" -B "$LIBSMB2_BUILD_DIR" \
  "${GENERATOR_ARGS[@]}" \
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}" \
  -DCMAKE_INSTALL_PREFIX="$LIBSMB2_PREFIX" \
  -DENABLE_EXAMPLES=OFF \
  -DENABLE_LIBKRB5=OFF \
  -DENABLE_GSSAPI=OFF

cmake --build "$LIBSMB2_BUILD_DIR" --target install --parallel

cat <<EOF
libsmb2 installed to:
  $LIBSMB2_PREFIX

To force using this prefix instead of the vendored CMake target:
  PKG_CONFIG_PATH="$LIBSMB2_PREFIX/lib/pkgconfig:\$PKG_CONFIG_PATH" \\
  cmake -S . -B tmp/build -G Ninja \\
    -DSMB_BROWSER_WITH_LIBSMB2=ON \\
    -DSMB_BROWSER_USE_SYSTEM_LIBSMB2=ON

Or export:
  export PKG_CONFIG_PATH="$LIBSMB2_PREFIX/lib/pkgconfig:\$PKG_CONFIG_PATH"
EOF
