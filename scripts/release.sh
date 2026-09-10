#!/usr/bin/env bash
# release.sh — tag and push a release from the version in package.json.
#
# Flow: bump "version" in package.json + add a CHANGELOG entry, commit, then run
# this. It verifies the tree is clean and building, tags vX.Y, and pushes the tag
# (which triggers .github/workflows/release.yml to build + publish the .pbw).
set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

VERSION="$(node -p "require('./package.json').version")"
TAG="v${VERSION}"

if [[ -n "$(git status --porcelain)" ]]; then
  echo "release: working tree not clean — commit the version bump first." >&2
  exit 1
fi

if ! grep -q "## ${TAG}\b\|## ${VERSION}\b" CHANGELOG.md; then
  echo "release: no CHANGELOG.md entry for ${VERSION}. Add one first." >&2
  exit 1
fi

if git rev-parse "$TAG" >/dev/null 2>&1; then
  echo "release: tag $TAG already exists." >&2
  exit 1
fi

echo "release: clean build to verify $TAG…"
pebble clean >/dev/null 2>&1 || true
pebble build >/dev/null
LABEL="$(node -p "require('./build/appinfo.json').versionLabel")"
if [[ "$LABEL" != "$VERSION" ]]; then
  echo "release: built versionLabel ($LABEL) != package.json ($VERSION). Run 'pebble clean'." >&2
  exit 1
fi
bash scripts/check-appimage-size.sh build/emery/pebble-app.elf

echo "release: tagging $TAG and pushing…"
git tag -a "$TAG" -m "$TAG"
git push origin "$TAG"
echo "release: pushed $TAG. The release workflow will build and attach the .pbw."
