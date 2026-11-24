#!/usr/bin/env bash
# Build the local rpmbuild images used by .gitea/workflows/build-rpm.yml.
# Run this on the Gitea runner host. Pass release numbers to rebuild a subset.

set -euo pipefail

cd "$(dirname "$0")/.."

RELEASES=("$@")
if [ ${#RELEASES[@]} -eq 0 ]; then
    RELEASES=(42 44 45)
fi

for release in "${RELEASES[@]}"; do
    docker build \
        --build-arg FEDORA_VERSION="$release" \
        -t "rpmbuild-env:fc${release}" \
        -f containers/rpmbuild-env/Dockerfile \
        .
done
