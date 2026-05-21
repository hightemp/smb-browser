#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REMOTE="${RELEASE_REMOTE:-origin}"
TAG_PREFIX="${RELEASE_TAG_PREFIX:-v}"
BRANCH="${RELEASE_BRANCH:-$(git -C "$ROOT_DIR" branch --show-current)}"
DRY_RUN="${RELEASE_DRY_RUN:-0}"
VERSION_FILE="$ROOT_DIR/VERSION"

version="$(tr -d '[:space:]' <"$VERSION_FILE")"
if ! [[ "$version" =~ ^[0-9]+(\.[0-9]+){2,3}$ ]]; then
  echo "Invalid VERSION '$version'. Expected numeric MAJOR.MINOR.PATCH." >&2
  exit 1
fi

if [ -z "$BRANCH" ]; then
  echo "Release must be run from a branch, not detached HEAD." >&2
  exit 1
fi

version_files=(
  "VERSION"
  "CMakeLists.txt"
  "packaging/linux/io.github.smb_browser.SmbBrowser.metainfo.xml"
)

dirty_unrelated="$(
  git -C "$ROOT_DIR" status --porcelain --untracked-files=normal |
    awk '
      {
        path = substr($0, 4)
        if (path != "VERSION" &&
            path != "CMakeLists.txt" &&
            path != "packaging/linux/io.github.smb_browser.SmbBrowser.metainfo.xml") {
          print
        }
      }
    '
)"
if [ -n "$dirty_unrelated" ]; then
  echo "Refusing to release with unrelated dirty files:" >&2
  echo "$dirty_unrelated" >&2
  echo "Commit or stash them first." >&2
  exit 1
fi

"$ROOT_DIR/scripts/sync-version.sh" "$version"

git -C "$ROOT_DIR" add "${version_files[@]}"
if ! git -C "$ROOT_DIR" diff --cached --quiet -- "${version_files[@]}"; then
  git -C "$ROOT_DIR" commit -m "Release ${TAG_PREFIX}${version}"
else
  echo "No version file changes to commit."
fi

tag="${TAG_PREFIX}${version}"
git -C "$ROOT_DIR" tag -fa "$tag" -m "Release $tag"

if [ "$DRY_RUN" = "1" ]; then
  echo "Dry run enabled; skipping push."
  echo "Would run: git push -f $REMOTE HEAD:$BRANCH"
  echo "Would run: git push -f $REMOTE refs/tags/$tag"
  exit 0
fi

git -C "$ROOT_DIR" push -f "$REMOTE" "HEAD:$BRANCH"
git -C "$ROOT_DIR" push -f "$REMOTE" "refs/tags/$tag"

echo "Released $tag and pushed to $REMOTE/$BRANCH."
