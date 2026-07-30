#!/bin/bash
# Bumps the project version according to Semantic Versioning 2.0.0.
#
#   scripts/bump-version.sh build  ["changelog message"]   # 1.0.0+12 -> 1.0.0+13
#   scripts/bump-version.sh patch  ["..."]                 # 1.0.0+12 -> 1.0.1+13
#   scripts/bump-version.sh minor  ["..."]                 # 1.0.0+12 -> 1.1.0+13
#   scripts/bump-version.sh major  ["..."]                 # 1.0.0+12 -> 2.0.0+13
#
# The build number is strictly monotonic and is incremented by every bump,
# including a plain "build" bump for changes that do not affect the public
# behaviour (documentation, packaging, refactoring).
#
# Updates CMakeLists.txt (the single source of truth) and prepends an entry to
# debian/changelog. Everything else - the C++ code, --version, the About dialog,
# the .deb version and file name - derives from CMakeLists.txt.
set -euo pipefail

PROJECT_DIR="${BUMP_PROJECT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
CMAKE_FILE="$PROJECT_DIR/CMakeLists.txt"
CHANGELOG="$PROJECT_DIR/debian/changelog"
MAINTAINER="Serhii Artiushenko <artushenko@protonmail.com>"

usage() {
    sed -n '2,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-1}"
}

[ $# -ge 1 ] || usage 1
case "$1" in
    build|patch|minor|major) PART="$1" ;;
    -h|--help)               usage 0 ;;
    *) echo "error: unknown version part '$1'" >&2; usage 1 ;;
esac
MESSAGE="${2:-}"

[ -f "$CMAKE_FILE" ] || { echo "error: $CMAKE_FILE not found" >&2; exit 1; }

# ---- read the current version --------------------------------------------
CURRENT_VERSION=$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\+\([0-9]\+\.[0-9]\+\.[0-9]\+\).*/\1/p' \
                  "$CMAKE_FILE" | head -1)
CURRENT_BUILD=$(sed -n 's/^set(CORVO_BUILD[[:space:]]\+\([0-9]\+\)).*/\1/p' "$CMAKE_FILE" | head -1)

if [ -z "$CURRENT_VERSION" ] || [ -z "$CURRENT_BUILD" ]; then
    echo "error: cannot parse the current version out of $CMAKE_FILE" >&2
    exit 1
fi

IFS=. read -r MAJOR MINOR PATCH <<< "$CURRENT_VERSION"

case "$PART" in
    major) MAJOR=$((MAJOR + 1)); MINOR=0; PATCH=0 ;;
    minor) MINOR=$((MINOR + 1)); PATCH=0 ;;
    patch) PATCH=$((PATCH + 1)) ;;
    build) ;;
esac
NEW_BUILD=$((CURRENT_BUILD + 1))
NEW_VERSION="$MAJOR.$MINOR.$PATCH"
NEW_FULL="$NEW_VERSION+$NEW_BUILD"

# ---- write it back --------------------------------------------------------
# Only the project() line and the build counter are touched; both appear once.
sed -i "0,/^[[:space:]]*VERSION[[:space:]]\+[0-9]\+\.[0-9]\+\.[0-9]\+/s//    VERSION $NEW_VERSION/" \
    "$CMAKE_FILE"
sed -i "s/^set(CORVO_BUILD[[:space:]]\+[0-9]\+)/set(CORVO_BUILD $NEW_BUILD)/" "$CMAKE_FILE"

# ---- changelog -----------------------------------------------------------
# debian/source/format is "3.0 (native)", so the version carries no revision.
if [ -f "$CHANGELOG" ]; then
    [ -n "$MESSAGE" ] || MESSAGE="$PART bump to $NEW_FULL."
    {
        printf 'corvo (%s) unstable; urgency=medium\n\n' "$NEW_FULL"
        printf '  * %s\n\n' "$MESSAGE"
        printf ' -- %s  %s\n\n' "$MAINTAINER" "$(date -R)"
        cat "$CHANGELOG"
    } > "$CHANGELOG.new"
    mv "$CHANGELOG.new" "$CHANGELOG"
fi

echo "$CURRENT_VERSION+$CURRENT_BUILD -> $NEW_FULL"
echo
echo "Next: rebuild so the new version reaches the binary and the package:"
echo "  cmake -S \"$PROJECT_DIR\" -B \"$PROJECT_DIR/build\" -G Ninja && cmake --build \"$PROJECT_DIR/build\""
echo "  (cd \"$PROJECT_DIR/build\" && cpack -G DEB)"
