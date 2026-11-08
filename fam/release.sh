#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# FM-FAM release build. Needs the JieLi toolchain and the AC79 SDK (see README.md):
#   JIELI_TOOLCHAIN=... AC79_SDK=... fam/release.sh
#
# The package identity is FM-1_111, the same for every release; the version is APP_VERSION in fam/app.h (shown on
# the HOME help pages). M-UPGRADE 3.11 accepts nothing longer than three digits after the underscore. The same
# text goes into the package and into the firmware (tools/build.py, FM1B_ID).
set -eu
cd "$(dirname "$0")/.."
id="FM-1_111"
echo "identity $id"
FM1B_ID="$id" python3 tools/build.py fam
echo "package fam/build/fm-fam.fwsc ($id)"
# the screen mirror page installs a release from the copy kept here (a browser cannot download the release's own file); the
# name is the one of the file attached to the release. A copy already there is left as it is: it is the release's.
ver=$(sed -n 's/^#define APP_VERSION "\(.*\)"/\1/p' fam/app.h)
mkdir -p fam/releases
if [ -e "fam/releases/fm-fam-$ver.fwsc" ]; then
    echo "fam/releases/fm-fam-$ver.fwsc is there already (left as it is)"
else
    cp fam/build/fm-fam.fwsc "fam/releases/fm-fam-$ver.fwsc"
    echo "copy fam/releases/fm-fam-$ver.fwsc (commit it, and attach the same file to the release)"
fi
