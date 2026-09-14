set -e

if [ $# -ne 1 ]; then
  echo "Usage: $0 <major|minor|patch>"
  exit 1
fi

BUMP_TYPE=$1

TAG_PREFIX=${TAG_PREFIX:-v}

git fetch origin --tags --quiet || echo "Warning: git fetch failed. Tags may be outdated."

if ! LATEST_TAG=$(git describe --tags --abbrev=0 --match "${TAG_PREFIX}*" 2>/dev/null); then
  echo "No tags found, starting from 1.0.0"
  LATEST_TAG="${TAG_PREFIX}-1.0.0"
fi

CURRENT_VERSION=$(echo "$LATEST_TAG" | sed -e "s,^${TAG_PREFIX},," -e 's/^-//' -e 's/\+.*//')
if [ -z "$CURRENT_VERSION" ]; then
  CURRENT_VERSION="1.0.0"
fi

IFS='.' read -r MAJOR MINOR PATCH <<< "$CURRENT_VERSION"

case $BUMP_TYPE in
  major)
    MAJOR=$((MAJOR + 1))
    MINOR=0
    PATCH=0
    ;;
  minor)
    MINOR=$((MINOR + 1))
    PATCH=0
    ;;
  patch)
    PATCH=$((PATCH + 1))
    ;;
  *)
    echo "Invalid bump type: $BUMP_TYPE"
    exit 1
    ;;
esac

NEW_VERSION="$MAJOR.$MINOR.$PATCH"
echo "$NEW_VERSION" > VERSION

TODAY=$(date +%Y-%m-%d)
CHANGELOG_TEMP=$(mktemp)

cat > "$CHANGELOG_TEMP" << EOF
# Changelog

## [$NEW_VERSION] - $TODAY

*

EOF

if [ -f CHANGELOG.md ]; then
  tail -n +3 CHANGELOG.md >> "$CHANGELOG_TEMP"
fi

mv "$CHANGELOG_TEMP" CHANGELOG.md

echo "Bumped version to $NEW_VERSION"
