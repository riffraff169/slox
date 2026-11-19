#!/bin/bash
# Usage: scripts/bump-version.sh 1.4.2 2

set -e

cd "$(dirname "$0")/.."

NEW_VER=${1:-}
NEW_REL=${2:-1}
TAG_NAME="v$NEW_VER"

if [ -z "$NEW_VER" ]; then
    echo "Usage: $0 <major.minor.patch> [release]"
    exit 1
fi

if ! [[ "$NEW_VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "[!] Error: Version must be in major.minor.patch format."
    exit 1
fi

BRANCH=$(git rev-parse --abbrev-ref HEAD)
if [ "$BRANCH" != "master" ]; then
    echo "[!] Error: Version bumps must run from master (current: $BRANCH)."
    exit 1
fi

if ! git config --get remote.github.url >/dev/null 2>&1; then
    echo "[!] Error: 'github' remote is not configured."
    exit 1
fi

if git rev-parse -q --verify "refs/tags/$TAG_NAME" >/dev/null; then
    echo "[!] Error: Tag '$TAG_NAME' already exists in Git. Aborting version bump."
    exit 1;
fi

# Split version string into major, minor, patch
IFS='.' read -r MAJOR MINOR PATCH <<< "$NEW_VER"
PATCH=${PATCH:-0}

# 1. Update version.h as the single source of truth
cat << EOF > src/version.h
#ifndef SLOX_VERSION_H
#define SLOX_VERSION_H

#define SLOX_VERSION_MAJOR $MAJOR
#define SLOX_VERSION_MINOR $MINOR
#define SLOX_VERSION_PATCH $PATCH

#define SLOX_VERSION_STRING "$NEW_VER"

#endif
EOF

# 2. Update the spec Version/Release lines
sed -i "s/^Version:.*/Version: $NEW_VER/" slox.spec
sed -i "s/^Release:.*/Release: $NEW_REL%{?dist}/" slox.spec
sed -i "s/^Version:.*/Version: $NEW_VER/" slox-all.spec
sed -i "s/^Release:.*/Release: $NEW_REL%{?dist}/" slox-all.spec

# 3. Add the Git-sourced RPM changelog entry to both specs
CHG_DATE=$(date "+%a  %b %d %Y")
USER_EMAIL="Lance Dillon <riffraff169@yahoo.com>"
GIT_CHANGES=$(git log -1 --format="- %s")

printf "* %s %s - %s\n%s\n\n" "$CHG_DATE" "$USER_EMAIL" "$NEW_VER-$NEW_REL" "$GIT_CHANGES" > changelog.tmp
sed -i '/^%changelog/r changelog.tmp' slox.spec
sed -i '/^%changelog/r changelog.tmp' slox-all.spec
rm changelog.tmp

git add src/version.h slox.spec slox-all.spec
git commit -m "Bump version to $NEW_VER-$NEW_REL"
git push origin master
git push github master

git tag -a "$TAG_NAME" -m "Version $NEW_VER"
git push origin "$TAG_NAME"
git push github "$TAG_NAME"

echo "Successfully bumped to Version: $NEW_VER, Release: $NEW_REL%{?dist}!"

