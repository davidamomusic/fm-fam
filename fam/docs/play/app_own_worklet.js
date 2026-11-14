// SPDX-License-Identifier: MPL-2.0
// FM-FAM's own part of the player's worklet: Render Audio (render.js) asks for a copy of the running app, its whole
// memory, which render_worker.js plays from (the sound, the patterns and the tempo exactly as they are now).
globalThis.fm1App = {
  message(fm1, m) {
    if (m.op === "snapshot") {
      const copy = new Uint8Array(fm1.mem.buffer).slice();
      fm1.port.postMessage({ type: "app", op: "snapshot", memory: copy.buffer }, [copy.buffer]);
    }
  },
};
