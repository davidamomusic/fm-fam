#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The browser player, fam/docs/play/ (GitHub Pages): FM-FAM built for the browser (tools/build.py fam --web: its own
# page, fam/web/emu/index.html, and its own worklet part, app_worklet.js) and Render Audio's files beside it.
# From the repository's root:  sh fam/tools/play.sh   (emcc on the PATH: Emscripten, or fam/tools/emcc's stand-in)
set -e
python3 tools/build.py fam --web
rm -rf fam/web/words                       # (the screen reader's words: the player has no screen reader)
dst=fam/docs/play
rm -rf "$dst"
mkdir -p "$dst"
cp -R fam/build/emu/index.html fam/build/emu/app.wasm fam/build/emu/worklet.js fam/build/emu/app.js \
      fam/build/emu/app_worklet.js fam/build/emu/app_own_worklet.js fam/build/emu/parts "$dst"/
cp fam/web/emu/render.js fam/web/emu/render_worker.js "$dst"/
echo "player   $dst ($(du -sk "$dst" | cut -f1) kB)"
