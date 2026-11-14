// Blender's screen for JavaScript: areas the user splits, joins, docks and resizes, and the
// regions inside them. The layout is the kit's and the elements are blenderui's (Rust's
// `blenderui::screen`): a `<bui-screen>` whose `<bui-area>` children, in the kit's area
// order, hold `<bui-region region="sidebar" ...>` elements and one `<bui-main>`. This module
// is what every framework shares: the protocol (attribute names, the layout's text form, the
// change events), and `createScreen` for the DOM-like API. React, Vue and Svelte build the
// same elements their own way.
//
//   const screen = createScreen(document, {
//     editors: { '3D Viewport': { icon: 'VIEW3D', render: (area) => editorElement(document, {...}) } },
//     layout: AreaSplit.v(0.75, '3D Viewport', 'Properties'),
//     onChange: (e) => save(e.layout),
//   })
//
// SPDX-License-Identifier: GPL-2.0-or-later
'use strict'

/** The regions an editor may have (one of each), in Blender's names; the props name them in
 *  camelCase (`toolHeader`). */
const REGIONS = ['header', 'tool_header', 'footer', 'asset_shelf', 'channels', 'nav_bar', 'toolbar',
  'sidebar', 'execute', 'hud']
const SLOT = Object.fromEntries(REGIONS.map((r) => [r.replace(/_(.)/g, (_, c) => c.toUpperCase()), r]))
/** A region's props, as `<bui-region>` attributes. */
const REGION_PROPS = ['size', 'hidden', 'side', 'overlap', 'resizable', 'maxsize', 'hotkey']

/** A screen's first arrangement: an editor's name, or two split by an edge. */
const AreaSplit = {
  /** `first` on the left taking `factor` of the width, `second` on the right. */
  v: (factor, first, second) => split('v', factor, first, second),
  /** `first` on top taking `factor` of the height, `second` below. */
  h: (factor, first, second) => split('h', factor, first, second),
}

function split (axis, factor, first, second) {
  if (!(factor > 0 && factor < 1)) throw new RangeError('AreaSplit: factor must be between 0 and 1')
  return { axis, factor, first, second }
}

/** The text form `<bui-screen layout="...">` takes (blenderui's `AreaSplit::parse`). */
function layoutText (layout) {
  if (layout == null) return null
  if (typeof layout === 'string') return JSON.stringify(layout)
  return `${layout.axis}${layout.factor}(${layoutText(layout.first)},${layoutText(layout.second)})`
}

/** A `<bui-region>`'s attributes from its props (`hidden: true` -> "1"). */
function regionAttrs (props) {
  const out = {}
  for (const k of REGION_PROPS) {
    const v = props[k]
    if (v == null) continue
    out[k] = typeof v === 'boolean' ? (v ? '1' : '0') : String(v)
  }
  return out
}

/**
 * What a `change` event of a screen's elements means: `{ layout, areas }` (the layout changed;
 * `areas` are the editors of the areas in the kit's order), `{ window: editor }` (an area was
 * dragged out of the window), or null (another element's event).
 */
function screenChange (value) {
  if (typeof value !== 'string') return null
  if (value.startsWith('window\t')) return { window: value.slice(7) }
  if (value[0] !== '{') return null
  const doc = JSON.parse(value)
  return { layout: value, areas: doc.areas.map((a) => a.editor) }
}

/** The render function of an editor, by name (the first editor for an unknown name). */
function editorFor (editors, name) {
  return editors[name] ?? Object.values(editors)[0]
}

// ---- the DOM-like API ----------------------------------------------------------------

/**
 * A `<bui-area>` element: `main` (an element) and the regions, each an element or
 * `{ element, size, hidden, side, overlap, resizable, maxsize, hotkey, onResize }`.
 */
function editorElement (document, { main, ...regions }) {
  const area = document.createElement('bui-area')
  for (const [slot, value] of Object.entries(regions)) {
    const kind = SLOT[slot]
    if (kind === undefined) throw new TypeError(`editorElement: no region '${slot}'`)
    if (value == null) continue
    const r = value.element ? value : { element: value }
    const el = document.createElement('bui-region')
    el.setAttribute('region', kind)
    for (const [k, v] of Object.entries(regionAttrs(r))) el.setAttribute(k, v)
    if (typeof r.onResize === 'function') {
      el.addEventListener('change', (e) => {
        if (e.target === el) r.onResize({ size: el.valueAsNumber, hidden: el.checked })
      })
    }
    el.appendChild(r.element)
    area.appendChild(el)
  }
  const m = document.createElement('bui-main')
  if (main) m.appendChild(main)
  area.appendChild(m)
  return area
}

/**
 * A `<bui-screen>` element that fills its areas from `editors` (name -> `{ icon, render(area) }`,
 * `render` returning an element from `editorElement`), starting from `layout` (or `saved`, a
 * layout `onChange` gave). `onChange({ layout })` follows every change the user makes;
 * `onWindow({ editor })` an area dragged out of the window. `ops: 'resize'`: only edges move.
 */
function createScreen (document, { editors, layout, saved, onChange, onWindow, ops } = {}) {
  const screen = document.createElement('bui-screen')
  const text = layoutText(layout)
  if (text) screen.setAttribute('layout', text)
  if (saved) screen.setAttribute('saved', saved)
  if (ops) screen.setAttribute('ops', ops)
  screen.addEventListener('change', (e) => {
    if (e.target !== screen) return
    const c = screenChange(screen.value)
    if (c === null) return
    if (c.window !== undefined) {
      onWindow?.({ editor: c.window })
      return
    }
    while (screen.firstChild) screen.removeChild(screen.firstChild)
    c.areas.forEach((editor, index) => {
      screen.appendChild(editorFor(editors, editor).render({ index, editor }))
    })
    onChange?.({ layout: c.layout })
  })
  return screen
}

module.exports = { AreaSplit, REGIONS, SLOT, REGION_PROPS, layoutText, regionAttrs, screenChange, editorFor,
  editorElement, createScreen }
