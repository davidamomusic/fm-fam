#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# FM-FAM release build. Needs the JieLi toolchain and the AC79 SDK (see README.md):
#   JIELI_TOOLCHAIN=... AC79_SDK=... fam/release.sh
#
# The package identity is FM-1_8 plus the minor version as two digits (0.19 -> FM-1_819). M-UPGRADE 3.11 accepts
# nothing longer than three digits after the underscore, so the identity cannot carry the whole version. The same
# text goes into the package and into the firmware (tools/build.py, FM1B_ID).
set -eu
cd "$(dirname "$0")/.."
minor=$(sed -n 's/^#define APP_VERSION "0\.\([0-9]\{1,2\}\)".*/\1/p' fam/app.h)
if [ -z "$minor" ]; then
    echo "release.sh: APP_VERSION in fam/app.h must look like 0.NN" >&2
    exit 1
fi
id=$(printf 'FM-1_8%02d' "$minor")
echo "identity $id"
FM1B_ID="$id" python3 tools/build.py fam
echo "package fam/build/fm-fam.fwsc ($id)"
