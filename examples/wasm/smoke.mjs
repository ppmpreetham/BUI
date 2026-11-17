// Runs a built blenderui WebAssembly app under Node.js (no browser) and checks what it draws:
//
//   python platform/wasm/build.py examples/wasm/counter
//   node examples/wasm/smoke.mjs examples/wasm/counter/dist/app.wasm
//
// It starts the app in a 420 x 300 canvas, asks for frames, clicks the button and checks that the first
// frame exists, a second one only comes when something changed, a click changes the picture, and a
// resize gives a frame of the new size.
//
// SPDX-License-Identifier: GPL-2.0-or-later
import assert from 'node:assert/strict'
import fs from 'node:fs'
import { load } from '../../bindings/rust/blenderui-web/js/bui-web.js'

const file = process.argv[2]
if (!file) {
  console.error('usage: node smoke.mjs app.wasm')
  process.exit(2)
}
const x = await load(fs.readFileSync(file))

assert.equal(x.bui_start(420, 300, 1), 1, 'the app opens a window')
const frame = () => {
  if (!x.bui_frame()) return null
  const [w, h] = [x.bui_width(), x.bui_height()]
  return { w, h, pixels: new Uint8Array(x.memory.buffer.slice(x.bui_pixels(), x.bui_pixels() + w * h * 4)) }
}

const first = frame()
assert.ok(first, 'a first frame')
assert.deepEqual([first.w, first.h], [420, 300])
assert.ok(first.pixels.some((v, i) => i % 4 === 3 && v === 255), 'opaque pixels')
assert.ok(new Set(first.pixels).size > 8, 'more than a flat color was drawn')
assert.equal(frame(), null, 'nothing changed: no new frame')

x.bui_pointer_move(60, 150)
x.bui_pointer_down(60, 150)
x.bui_pointer_up(60, 150)
const clicked = frame()
assert.ok(clicked, 'a click gives a frame')
assert.notDeepEqual(clicked.pixels, first.pixels, 'the click changed the picture')

x.bui_resize(300, 200, 1)
const resized = frame()
assert.ok(resized, 'a resize gives a frame')
assert.deepEqual([resized.w, resized.h], [300, 200])

// a retina canvas draws twice the pixels
x.bui_resize(300, 200, 2)
const retina = frame()
assert.deepEqual([retina.w, retina.h], [600, 400])

// ... and the same layout: a page that moves to a retina screen (the pixel ratio changes, the CSS size
// does not) draws what a page that started there draws. A fresh module for each, with no click made.
const bytes = fs.readFileSync(file)
const firstFrame = (m) => {
  assert.ok(m.bui_frame(), 'a frame')
  const [w, h] = [m.bui_width(), m.bui_height()]
  return { w, h, pixels: new Uint8Array(m.memory.buffer.slice(m.bui_pixels(), m.bui_pixels() + w * h * 4)) }
}
const moved = await load(bytes)
moved.bui_start(420, 300, 1)
firstFrame(moved)
moved.bui_resize(420, 300, 2)
const after = firstFrame(moved)
const started = await load(bytes)
started.bui_start(420, 300, 2)
const direct = firstFrame(started)
assert.deepEqual([after.w, after.h], [direct.w, direct.h])
assert.deepEqual(after.pixels, direct.pixels, 'a pixel ratio change lays the page out as one that started there')

// a click at a logical position lands on the button after the change (not at the device position, 380)
moved.bui_pointer_move(60, 190)
moved.bui_pointer_down(60, 190)
moved.bui_pointer_up(60, 190)
assert.notDeepEqual(firstFrame(moved).pixels, after.pixels, 'the button is where the CSS size says after a pixel ratio change')

x.bui_stop()
assert.equal(x.bui_frame(), 0, 'stopped: no more frames')
console.log(`ok: ${(fs.statSync(file).size / 1048576).toFixed(1)} MB module, ${first.w}x${first.h} first frame`)
