#!/bin/bash
# Usage: ./bump-version.sh 1.4.2 2

NEW_VER=$1
NEW_REL=${2:-1}
TAG_NAME="v$NEW_VER"

if [ -z "$NEW_VER" ]; then
    echo "Usage: $0 <version>"
    exit 1
fi

if git rev-parse -q --verify "refs/tags/$TAG_NAME" >/dev/null; then
    echo "[!] Error: Tag '$TAG_NAME' already exists in Git. Aborting version bump."
    exit 1;
fi

# 1. Update Makefile
sed -i "s/^VERSION =.*/VERSION = $NEW_VER/" Makefile 2>/dev/null || \
sed -i "s/^VERSION=.*/VERSION=$NEW_VER/" Makefile
sed -i "s/^RELEASE =.*/RELEASE = $NEW_REL/" Makefile

# 2. Update slox.spec Version line
sed -i "s/^Version:.*/Version: $NEW_VER/" slox.spec
sed -i "s/^Release:.*/Release: $NEW_REL%{?dist}/" slox.spec

# 3. Add the Git-sourced RPM changelog entry we built earlier
CHG_DATE=$(date "+%a  %b %d %Y")
USER_EMAIL="Lance Dillon <riffraff169@yahoo.com>"
GIT_CHANGES=$(git log -1 --format="- %s")

printf "* %s %s - %s-1\n%s\n\n" "$CHG_DATE" "$USER_EMAIL" "$NEW_VER-$NEW_REL" "$GIT_CHANGES" > changelog.tmp
sed -i '/^%changelog/r changelog.tmp' slox.spec
rm changelog.tmp

git add Makefile slox.spec
git commit -m "Bump version to $NEW_VER-$NEW_REL"

git tag -a "$TAG_NAME" -m "Version $NEW_VER"

#echo "Version bumped to $NEW_VER in Makefile and slox.spec!"
echo "Successfully bumped to Version: $NEW_VER, Release: $NEW_REL%{?dist}!"

