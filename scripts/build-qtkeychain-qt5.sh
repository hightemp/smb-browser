#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QTKEYCHAIN_VERSION="${QTKEYCHAIN_VERSION:-0.15.0}"
QTKEYCHAIN_REPOSITORY="${QTKEYCHAIN_REPOSITORY:-https://github.com/frankosterfeld/qtkeychain.git}"
QTKEYCHAIN_SOURCE_DIR="${QTKEYCHAIN_SOURCE_DIR:-$ROOT_DIR/tmp/qtkeychain-src}"
QTKEYCHAIN_BUILD_DIR="${QTKEYCHAIN_BUILD_DIR:-$ROOT_DIR/tmp/qtkeychain-build}"
QTKEYCHAIN_PREFIX="${QTKEYCHAIN_PREFIX:-$ROOT_DIR/tmp/qtkeychain-qt5-prefix}"
QTKEYCHAIN_GENERATOR="${QTKEYCHAIN_GENERATOR:-Ninja}"

sed_in_place() {
  if sed --version >/dev/null 2>&1; then
    sed -i "$@"
  else
    local expression="$1"
    local file="$2"
    sed -i '' "$expression" "$file"
  fi
}

if command -v brew >/dev/null 2>&1; then
  qt5_prefix="$(brew --prefix qt@5 2>/dev/null || true)"
  if [ -n "$qt5_prefix" ]; then
    export PATH="$qt5_prefix/bin:$PATH"
    export CMAKE_PREFIX_PATH="$qt5_prefix${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
  fi
fi

if [ -n "${MINGW_PREFIX:-}" ]; then
  export PATH="$MINGW_PREFIX/bin:$PATH"
  export CMAKE_PREFIX_PATH="$MINGW_PREFIX${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
fi

if [ ! -d "$QTKEYCHAIN_SOURCE_DIR/.git" ]; then
  rm -rf "$QTKEYCHAIN_SOURCE_DIR"
  git clone --depth 1 --branch "$QTKEYCHAIN_VERSION" \
    "$QTKEYCHAIN_REPOSITORY" "$QTKEYCHAIN_SOURCE_DIR"
else
  git -C "$QTKEYCHAIN_SOURCE_DIR" fetch --depth 1 origin \
    "refs/tags/$QTKEYCHAIN_VERSION:refs/tags/$QTKEYCHAIN_VERSION"
  git -C "$QTKEYCHAIN_SOURCE_DIR" checkout --detach "$QTKEYCHAIN_VERSION"
fi

if [ -f "$QTKEYCHAIN_SOURCE_DIR/qtkeychain/CMakeLists.txt" ]; then
  sed_in_place 's/add_definitions( \/utf-8 -DUNICODE )/add_definitions( -DUNICODE )/' \
    "$QTKEYCHAIN_SOURCE_DIR/qtkeychain/CMakeLists.txt"
fi

if [ -f "$QTKEYCHAIN_SOURCE_DIR/qtkeychain/keychain_win.cpp" ] &&
   ! grep -q '#include <cmath>' "$QTKEYCHAIN_SOURCE_DIR/qtkeychain/keychain_win.cpp"; then
  tmp_file="$QTKEYCHAIN_SOURCE_DIR/qtkeychain/keychain_win.cpp.tmp"
  awk '
    { print }
    $0 == "#include <memory>" && !inserted {
      print "#include <cmath>"
      inserted = 1
    }
  ' "$QTKEYCHAIN_SOURCE_DIR/qtkeychain/keychain_win.cpp" >"$tmp_file"
  mv "$tmp_file" "$QTKEYCHAIN_SOURCE_DIR/qtkeychain/keychain_win.cpp"
fi

rm -rf "$QTKEYCHAIN_BUILD_DIR" "$QTKEYCHAIN_PREFIX"
cmake -S "$QTKEYCHAIN_SOURCE_DIR" -B "$QTKEYCHAIN_BUILD_DIR" \
  -G "$QTKEYCHAIN_GENERATOR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$QTKEYCHAIN_PREFIX" \
  -DBUILD_WITH_QT6=OFF \
  -DBUILD_TEST_APPLICATION=OFF \
  -DBUILD_TESTING=OFF \
  -DBUILD_TRANSLATIONS=OFF
cmake --build "$QTKEYCHAIN_BUILD_DIR" --config Release
cmake --install "$QTKEYCHAIN_BUILD_DIR" --config Release

host_prefix="$QTKEYCHAIN_PREFIX"
host_bin="$QTKEYCHAIN_PREFIX/bin"
if command -v cygpath >/dev/null 2>&1; then
  host_prefix="$(cygpath -m "$QTKEYCHAIN_PREFIX")"
  host_bin="$(cygpath -m "$QTKEYCHAIN_PREFIX/bin")"
fi

if [ -n "${GITHUB_ENV:-}" ]; then
  {
    echo "QTKEYCHAIN_PREFIX=$host_prefix"
  } >>"$GITHUB_ENV"
fi

if [ -n "${GITHUB_PATH:-}" ]; then
  echo "$host_bin" >>"$GITHUB_PATH"
fi

echo "QtKeychain $QTKEYCHAIN_VERSION installed to $host_prefix"
