#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PACKAGE_PATH="${1:-}"

if [ "$(uname -s)" != "Darwin" ]; then
  echo "This smoke test must be run on macOS." >&2
  exit 2
fi

if [ -z "$PACKAGE_PATH" ]; then
  PACKAGE_PATH="$(find "$ROOT_DIR/tmp" \( -name '*.dmg' -o -name 'smb-browser.app' \) \
    -print 2>/dev/null | sort | tail -1)"
fi

if [ -z "$PACKAGE_PATH" ] || [ ! -e "$PACKAGE_PATH" ]; then
  echo "Package not found. Build it first with:" >&2
  echo "  cmake --build tmp/package-macos --target package" >&2
  exit 1
fi

SMOKE_DIR="$ROOT_DIR/tmp/package-smoke-macos"
ROOTFS="$SMOKE_DIR/rootfs"
rm -rf "$SMOKE_DIR"
mkdir -p "$ROOTFS"

APP_PATH=""
MOUNT_POINT=""
if [ -d "$PACKAGE_PATH" ] && [ "$(basename "$PACKAGE_PATH")" = "smb-browser.app" ]; then
  APP_PATH="$PACKAGE_PATH"
else
  MOUNT_POINT="$(mktemp -d "$SMOKE_DIR/dmg.XXXXXX")"
  hdiutil attach "$PACKAGE_PATH" -mountpoint "$MOUNT_POINT" -nobrowse -quiet
  trap 'hdiutil detach "$MOUNT_POINT" -quiet || true' EXIT
  APP_PATH="$(find "$MOUNT_POINT" -maxdepth 2 -name 'smb-browser.app' -type d -print -quit)"
fi

if [ -z "$APP_PATH" ] || [ ! -d "$APP_PATH" ]; then
  echo "smb-browser.app not found." >&2
  exit 1
fi

test -x "$APP_PATH/Contents/MacOS/smb-browser"
test -f "$APP_PATH/Contents/Resources/i18n/smb-browser_ru.qm"

if find "$APP_PATH/Contents" \( -name 'libsmb2*.dylib' -o -name 'smbclient' \) \
  -print -quit | grep -q .; then
  echo "Unexpected legacy SMB runtime found in app bundle." >&2
  find "$APP_PATH/Contents" \( -name 'libsmb2*.dylib' -o -name 'smbclient' \) >&2
  exit 1
fi

if otool -L "$APP_PATH/Contents/MacOS/smb-browser" | grep -Eiq 'libsmb2|smbclient|samba'; then
  echo "Executable links a legacy SMB runtime dependency:" >&2
  otool -L "$APP_PATH/Contents/MacOS/smb-browser" >&2
  exit 1
fi

"$APP_PATH/Contents/MacOS/smb-browser" &
pid=$!
sleep 3
if kill -0 "$pid" 2>/dev/null; then
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
else
  wait "$pid"
fi

echo "macOS package smoke passed for $PACKAGE_PATH"
