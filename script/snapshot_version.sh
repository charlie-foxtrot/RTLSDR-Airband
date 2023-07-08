#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail

# if it has a tag on current coommit, use that.
tag=$(git tag --points-at HEAD | { grep --max-count=1 -E "[0-9\.]+" || true; })

if [ -n "${tag}" ]; then
    echo "${tag}"
    exit 0
fi

# otherwise it reads the current version from CMakeLists.txt to build package snapshot version
# e.g. 1.2.3+git20210105.ab123cd
current_version=$(grep "(RTL_AIRBAND_VERSION_" CMakeLists.txt | tr '\n' '.' | tr -Cd '0-9.' | sed 's/\.$//g')
commit_date=$(TZ=UTC0 git show --quiet --date='format-local:%Y%m%d' --format="%cd")
commit_hash=$(git show --quiet --format='%h')

package_snapshot_version="${current_version}+git${commit_date}.${commit_hash}"

echo "${package_snapshot_version}"
