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
