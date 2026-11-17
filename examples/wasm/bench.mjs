// Times full-frame redraws of a built blenderui WebAssembly app under Node.js:
//
//   node examples/wasm/bench.mjs examples/wasm/counter/dist/app.wasm [width height]
//
// Each round resizes the canvas by a pixel, so the kit redraws everything, and times bui_frame.
//
// SPDX-License-Identifier: GPL-2.0-or-later
import fs from 'node:fs'
import { load } from '../../bindings/rust/blenderui-web/js/bui-web.js'

const [file, w = 1280, h = 800] = [process.argv[2], ...process.argv.slice(3).map(Number)]
if (!file) {
  console.error('usage: node bench.mjs app.wasm [width height]')
  process.exit(2)
}
const x = await load(fs.readFileSync(file))
x.bui_start(w, h, 1)
x.bui_frame()
const rounds = 60
const times = []
for (let i = 0; i < rounds + 10; i++) {
  x.bui_resize(w - (i % 2), h, 1)
  const t = performance.now()
  x.bui_frame()
  if (i >= 10) times.push(performance.now() - t)
}
times.sort((a, b) => a - b)
const ms = (v) => v.toFixed(2)
console.log(`${w}x${h}: median ${ms(times[rounds >> 1])} ms, best ${ms(times[0])} ms, worst ${ms(times[rounds - 1])} ms (${rounds} frames)`)
