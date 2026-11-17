// Runs a blenderui app (compiled to WebAssembly with blenderui-web) in a <canvas>.
//
//   import { mount } from './bui-web.js'
//   const app = await mount(document.querySelector('canvas'), 'counter.wasm')
//   app.stop()
//
// The canvas's CSS size is its logical size: its backing store is that times devicePixelRatio, so the
// page keeps its layout on a retina screen. With `fit` (the default) the page's CSS sizes the canvas;
// without it `mount` sets the CSS size from `width` and `height`.
//
// The module draws off-screen: this calls into it on every animation frame, puts the frame it returns
// on the canvas, and passes it the page's pointer, wheel and key events. `load` is the same without
// a canvas (Node.js, tests).
//
// SPDX-License-Identifier: GPL-2.0-or-later

const ENOSYS = 52
const EBADF = 8

/** The part of WASI (preview 1) a blenderui module uses: a clock, random bytes, stdout and stderr as the
 *  console, and "not supported" for files, which a page has none of. */
function wasiImports (module, memory) {
  const view = () => new DataView(memory.buffer)
  const text = new TextDecoder()
  const lines = { 1: '', 2: '' }
  const known = {
    random_get (ptr, len) {
      const bytes = new Uint8Array(memory.buffer, ptr, len)
      for (let i = 0; i < len; i += 65536) crypto.getRandomValues(bytes.subarray(i, Math.min(len, i + 65536)))
      return 0
    },
    environ_sizes_get (count, size) {
      view().setUint32(count, 0, true)
      view().setUint32(size, 0, true)
      return 0
    },
    environ_get: () => 0,
    clock_time_get (id, _precision, out) {
      // 0: wall clock, anything else: monotonic
      const ms = id === 0 ? Date.now() : performance.now()
      view().setBigUint64(out, BigInt(Math.round(ms * 1e6)), true)
      return 0
    },
    fd_write (fd, iovs, count, written) {
      const v = view()
      let total = 0
      let out = ''
      for (let i = 0; i < count; i++) {
        const ptr = v.getUint32(iovs + i * 8, true)
        const len = v.getUint32(iovs + i * 8 + 4, true)
        out += text.decode(new Uint8Array(memory.buffer, ptr, len), { stream: true })
        total += len
      }
      v.setUint32(written, total, true)
      if (fd === 1 || fd === 2) {
        // whole lines to the console
        lines[fd] += out
        const parts = lines[fd].split('\n')
        lines[fd] = parts.pop()
        for (const line of parts) (fd === 1 ? console.log : console.error)(line)
      }
      return 0
    },
    fd_prestat_get: () => EBADF, // no preopened directories
    proc_exit (code) {
      throw new Error(`the module exited with code ${code}`)
    },
    sched_yield: () => 0,
  }
  const imports = {}
  for (const { module: ns, name, kind } of WebAssembly.Module.imports(module)) {
    if (ns !== 'wasi_snapshot_preview1' || kind !== 'function') continue
    imports[name] = known[name] ?? (() => ENOSYS)
  }
  return { wasi_snapshot_preview1: imports }
}

/** Instantiates a module (a URL, a Response or bytes). Returns its exports. */
export async function load (source) {
  const bytes = typeof source === 'string' || source instanceof URL ? await fetch(source) : source
  const module = bytes instanceof Response
    ? await WebAssembly.compileStreaming(bytes)
    : await WebAssembly.compile(bytes)
  const memory = { buffer: null }
  const imports = wasiImports(module, memory)
  const instance = await WebAssembly.instantiate(module, imports)
  Object.defineProperty(memory, 'buffer', { get: () => instance.exports.memory.buffer })
  return instance.exports
}

// KeyboardEvent.key -> the X11 key names the kit takes
const KEYSYMS = {
  Enter: 'Return', Backspace: 'BackSpace', Delete: 'Delete', Tab: 'Tab', Escape: 'Escape',
  ArrowLeft: 'Left', ArrowRight: 'Right', ArrowUp: 'Up', ArrowDown: 'Down',
  Home: 'Home', End: 'End', PageUp: 'Prior', PageDown: 'Next', Insert: 'Insert', ' ': 'space',
}

/** Writes a string into the module's memory for `bui_key`; the module frees it. */
function putString (exports, s) {
  const bytes = new TextEncoder().encode(s)
  const ptr = exports.bui_alloc(bytes.length)
  new Uint8Array(exports.memory.buffer, ptr, bytes.length).set(bytes)
  return [ptr, bytes.length]
}

/**
 * Starts the app in `canvas`. `source` is the .wasm (a URL or bytes). Options: `fit` (default true) makes
 * the canvas follow its CSS size; otherwise it keeps `width` x `height` CSS pixels. `onerror` gets what
 * the module throws (a trap), after which the app is stopped (default: console.error). Returns
 * `{ exports, resize, stop }`.
 */
export async function mount (canvas, source, { fit = true, width = 640, height = 480, onerror = console.error } = {}) {
  const exports = await load(source)
  const ctx = canvas.getContext('2d')
  canvas.tabIndex = canvas.tabIndex >= 0 ? canvas.tabIndex : 0 // it can take the keyboard
  canvas.style.touchAction = 'none'
  if (!fit) {
    // the logical size, whatever the pixel ratio (the backing store below is that times the ratio)
    canvas.style.width = `${width}px`
    canvas.style.height = `${height}px`
  }
  const css = () => (fit ? [Math.max(1, canvas.clientWidth || width), Math.max(1, canvas.clientHeight || height)] : [width, height])
  const scale = () => window.devicePixelRatio || 1

  const [w0, h0] = css()
  canvas.width = Math.round(w0 * scale())
  canvas.height = Math.round(h0 * scale())
  if (fit && (canvas.clientWidth !== w0 || canvas.clientHeight !== h0)) {
    // an unstyled canvas is as big in CSS pixels as its backing store: at a pixel ratio above 1 it would grow
    // on every frame (the store follows the CSS size, which follows the store). Pin the logical size.
    canvas.style.width = `${w0}px`
    canvas.style.height = `${h0}px`
  }
  if (!exports.bui_start(w0, h0, scale())) throw new Error('the app did not open a window')

  let running = true
  let frameId = 0
  let off = []
  let observer = null
  let ratio = null
  const stop = () => {
    if (!running) return
    running = false
    cancelAnimationFrame(frameId)
    observer?.disconnect()
    ratio?.()
    for (const f of off) f()
    try {
      exports.bui_stop?.() // the module frees what it holds
    } catch { /* it already trapped: nothing is left to free */ }
  }
  // a trap in the module stops the app (no more frames, no listeners) and is reported once
  const guard = (fn) => (...args) => {
    if (!running) return
    try {
      fn(...args)
    } catch (e) {
      stop()
      onerror(e)
    }
  }
  const draw = guard(() => {
    if (exports.bui_frame()) {
      const [w, h] = [exports.bui_width(), exports.bui_height()]
      if (canvas.width !== w || canvas.height !== h) {
        canvas.width = w
        canvas.height = h
      }
      ctx.putImageData(new ImageData(new Uint8ClampedArray(exports.memory.buffer, exports.bui_pixels(), w * h * 4), w, h), 0, 0)
    }
    frameId = requestAnimationFrame(draw)
  })
  frameId = requestAnimationFrame(draw)

  const at = (e) => [e.offsetX, e.offsetY]
  const on = (type, fn, options) => {
    canvas.addEventListener(type, fn, options)
    return () => canvas.removeEventListener(type, fn, options)
  }
  // only the primary pointer's primary button: the module has one pointer and one button
  const primary = (e) => e.isPrimary && e.button === 0
  // whether the module has a press that has not been released: it is released however the page learns of
  // it (a pointerup, the primary button leaving `buttons` while another is held, or a cancellation)
  let pressed = false
  const release = (x, y) => {
    if (!pressed) return
    pressed = false
    exports.bui_pointer_up(x, y)
  }
  off = [
    on('pointermove', guard((e) => {
      if (!e.isPrimary) return
      exports.bui_pointer_move(...at(e))
      if (pressed && !(e.buttons & 1)) release(...at(e)) // left released while another button is held
    })),
    on('pointerdown', guard((e) => {
      if (!primary(e)) return
      canvas.focus()
      canvas.setPointerCapture(e.pointerId)
      exports.bui_pointer_move(...at(e))
      pressed = true
      exports.bui_pointer_down(...at(e))
    })),
    on('pointerup', guard((e) => {
      // the primary button's own release, or the last release of a chord whose primary button went earlier
      if (e.isPrimary && (e.button === 0 || !(e.buttons & 1))) release(...at(e))
    })),
    // cancelled (a dialog, an unplugged device) or capture lost: end the press away from the canvas, so it
    // clicks nothing
    on('pointercancel', guard((e) => {
      if (!e.isPrimary || !pressed) return
      exports.bui_pointer_move(-1, -1)
      release(-1, -1)
    })),
    on('lostpointercapture', guard((e) => {
      if (!e.isPrimary || !pressed) return
      exports.bui_pointer_move(-1, -1)
      release(-1, -1)
    })),
    on('wheel', guard((e) => {
      e.preventDefault()
      // the module's positive lines are wheel-up, the DOM's positive deltaY is wheel-down
      const lines = -Math.sign(e.deltaY) * Math.max(1, Math.round(Math.abs(e.deltaY) / 40))
      exports.bui_wheel(...at(e), lines)
    }), { passive: false }),
    on('keydown', guard((e) => {
      const text = e.key.length === 1 && !e.ctrlKey && !e.metaKey ? e.key : ''
      const keysym = KEYSYMS[e.key] ?? (e.key.length === 1 ? e.key : null)
      if (keysym === null) return
      const [tp, tl] = putString(exports, text)
      const [kp, kl] = putString(exports, keysym)
      exports.bui_key(tp, tl, kp, kl, e.ctrlKey || e.metaKey ? 1 : 0, e.shiftKey ? 1 : 0)
      e.preventDefault()
    })),
  ]
  const resize = guard(() => {
    const [w, h] = css()
    exports.bui_resize(w, h, scale())
  })
  if (fit && typeof ResizeObserver !== 'undefined') {
    observer = new ResizeObserver(resize)
    observer.observe(canvas)
  }
  // moving the window to another screen changes devicePixelRatio without resizing the canvas
  if (typeof matchMedia !== 'undefined') {
    const watch = () => {
      const query = matchMedia(`(resolution: ${scale()}dppx)`)
      const changed = () => {
        ratio()
        resize()
        if (running) watch()
      }
      query.addEventListener('change', changed, { once: true })
      ratio = () => query.removeEventListener('change', changed)
    }
    watch()
  }

  return { exports, resize, stop }
}
