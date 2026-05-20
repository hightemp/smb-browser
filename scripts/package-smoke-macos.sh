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

require_path() {
  local path="$1"
  local description="$2"
  if [ ! -e "$path" ]; then
    echo "$description not found in app bundle: $path" >&2
    exit 1
  fi
}

require_find_any() {
  local description="$1"
  local match
  shift
  match="$(find "$APP_PATH/Contents" "$@" -print -quit 2>/dev/null || true)"
  if [ -z "$match" ]; then
    echo "$description not found in app bundle." >&2
    exit 1
  fi
}

require_path "$APP_PATH/Contents/Frameworks/QtCore.framework" "Qt5 Core framework"
require_path "$APP_PATH/Contents/Frameworks/QtGui.framework" "Qt5 Gui framework"
require_path "$APP_PATH/Contents/Frameworks/QtWidgets.framework" "Qt5 Widgets framework"
require_path "$APP_PATH/Contents/Frameworks/QtSql.framework" "Qt5 SQL framework"
require_path "$APP_PATH/Contents/Frameworks/QtSvg.framework" "Qt5 SVG framework"
require_find_any "Qt Cocoa platform plugin" -name 'libqcocoa.dylib'
require_find_any "Qt SQLite driver plugin" -name 'libqsqlite.dylib'
require_find_any "QtKeychain runtime" \( -name 'Qt5Keychain.framework' -o -name 'libqt5keychain*.dylib' \)
require_find_any "libsodium runtime" -name 'libsodium*.dylib'

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

"$APP_PATH/Contents/MacOS/smb-browser" --smoke-close-ms=1000 &
pid=$!
deadline=$((SECONDS + 10))
while kill -0 "$pid" 2>/dev/null && [ "$SECONDS" -lt "$deadline" ]; do
  sleep 1
done
if kill -0 "$pid" 2>/dev/null; then
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  echo "Application did not close cleanly during smoke test." >&2
  exit 1
fi
wait "$pid"

if [ -n "${SMB_BROWSER_SMOKE_SERVER:-}" ] && [ -n "${SMB_BROWSER_SMOKE_SHARE:-}" ]; then
  "$APP_PATH/Contents/MacOS/smb-browser" --smoke-smb-list &
  pid=$!
  deadline=$((SECONDS + ${SMB_BROWSER_SMOKE_TIMEOUT_SECONDS:-30}))
  while kill -0 "$pid" 2>/dev/null && [ "$SECONDS" -lt "$deadline" ]; do
    sleep 1
  done
  if kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    echo "Application did not finish SMB list smoke." >&2
    exit 1
  fi
  wait "$pid"
fi

echo "macOS package smoke passed for $PACKAGE_PATH"
