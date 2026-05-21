#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION_FILE="$ROOT_DIR/VERSION"
VERSION="${1:-}"

if [ -z "$VERSION" ]; then
  if [ ! -f "$VERSION_FILE" ]; then
    echo "VERSION file not found: $VERSION_FILE" >&2
    exit 1
  fi
  VERSION="$(tr -d '[:space:]' <"$VERSION_FILE")"
fi

if ! [[ "$VERSION" =~ ^[0-9]+(\.[0-9]+){2,3}$ ]]; then
  echo "Invalid VERSION '$VERSION'. Expected numeric MAJOR.MINOR.PATCH." >&2
  exit 1
fi

printf '%s\n' "$VERSION" >"$VERSION_FILE"

VERSION="$VERSION" perl -0pi -e '
  my $version = $ENV{"VERSION"};
  s/(project\(\s*SmbBrowser\s+VERSION\s+)[0-9]+(?:\.[0-9]+){0,3}/$1$version/s
    or die "Unable to update CMake project version\n";
' "$ROOT_DIR/CMakeLists.txt"

release_date="$(date -u +%F)"
VERSION="$VERSION" RELEASE_DATE="$release_date" perl -0pi -e '
  my $version = $ENV{"VERSION"};
  my $date = $ENV{"RELEASE_DATE"};
  s{<release version="[^"]+" date="[^"]+"\s*/>}{<release version="$version" date="$date"/>}
    or die "Unable to update AppStream release metadata\n";
' "$ROOT_DIR/packaging/linux/io.github.smb_browser.SmbBrowser.metainfo.xml"

echo "Synced version $VERSION"
