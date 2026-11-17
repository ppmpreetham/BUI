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
/** The ones of them that are flags (a bare `<Region overlap>` means true). */
const REGION_FLAGS = ['hidden', 'overlap', 'resizable']

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

/** A name in the layout grammar: quotes and backslashes escaped, nothing else (the kit's
 *  writer and blenderui's `AreaSplit::parse` agree on that; `JSON.stringify`'s backslash-n would not parse). */
const quoted = (s) => `"${s.replace(/[\\"]/g, (c) => '\\' + c)}"`

/** The text form `<bui-screen layout="...">` takes (blenderui's `AreaSplit::parse`). */
function layoutText (layout) {
  if (layout == null) return null
  if (typeof layout === 'string') return quoted(layout)
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

/** The rectangle of an area of a layout document: `[x0, y0, x1, y1]` of its four vertices. */
function areaRect (verts, a) {
  const xs = a.v.map((i) => verts[i][0])
  const ys = a.v.map((i) => verts[i][1])
  return [Math.min(...xs), Math.min(...ys), Math.max(...xs), Math.max(...ys)]
}

const overlap = (a, b) => Math.max(0, Math.min(a[2], b[2]) - Math.max(a[0], b[0])) *
  Math.max(0, Math.min(a[3], b[3]) - Math.max(a[1], b[1]))
const sameRect = (a, b) => a.every((v, i) => v === b[i])

/**
 * Identities for the areas of a new layout, so a framework keeps an area's component (and its
 * state) while its neighbors are split, joined or closed. An area that has an `id` in the layout
 * document keeps it; otherwise each area takes the identity of the `previous` area of the same
 * editor that has its rectangle, or else overlaps it most (a survivor of a join has grown), and
 * new areas get fresh ones. `previous` is the `areas` of the last change (empty at first).
 */
function areaIdentities (doc, previous) {
  const rects = doc.areas.map((a) => areaRect(doc.verts, a))
  const taken = new Set()
  const ids = doc.areas.map((a) => (a.id === undefined ? null : a.id))
  // explicit ids are reserved: neither a fallback match nor a fresh id may reuse one
  const explicit = new Set(ids.filter((id) => id !== null))
  let fresh = Math.max(previous.reduce((m, a) => Math.max(m, a.id), 0), ...explicit)
  const claim = (i, score) => {
    let best = -1
    let bestScore = 0
    previous.forEach((p, j) => {
      if (taken.has(j) || explicit.has(p.id) || p.editor !== doc.areas[i].editor) return
      const s = score(p)
      if (s > bestScore) { best = j; bestScore = s }
    })
    if (best >= 0) { taken.add(best); ids[i] = previous[best].id }
  }
  for (let i = 0; i < ids.length; i++) if (ids[i] === null) claim(i, (p) => (sameRect(p.rect, rects[i]) ? Infinity : 0))
  for (let i = 0; i < ids.length; i++) if (ids[i] === null) claim(i, (p) => overlap(p.rect, rects[i]))
  return doc.areas.map((a, i) => ({ id: ids[i] ?? ++fresh, editor: a.editor, rect: rects[i] }))
}

/**
 * What a `change` event of a screen's elements means: `{ layout, areas }` (the layout changed;
 * `areas` are `{ id, editor, rect }` of the areas in the kit's order, `id` staying with an area as
 * the others change, see `areaIdentities`), `{ window: editor }` (an area was dragged out of the
 * window), or null (another element's event). `previous` is the `areas` of the last change.
 */
function screenChange (value, previous = []) {
  if (typeof value !== 'string') return null
  if (value.startsWith('window\t')) return { window: value.slice(7) }
  if (value[0] !== '{') return null
  const doc = JSON.parse(value)
  return { layout: value, areas: areaIdentities(doc, previous) }
}

/** `screenChange` of an event, when `screen` itself sent it (a nested screen's events bubble). */
function ownScreenChange (e, screen, previous) {
  return e.target === screen ? screenChange(screen.value, previous) : null
}

/** The render function of an editor, by name (the first editor for an unknown name). */
function editorFor (editors, name) {
  return Object.hasOwn(editors, name) ? editors[name] : Object.values(editors)[0]
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
  /** The editor elements the screen holds, by area identity. */
  const held = new Map()
  let areas = []
  const discard = (h) => {
    h.element.parentNode?.removeChild(h.element)
    if (!h.element._auto) h.element.destroy() // window-made nodes have no automatic cleanup
  }
  screen.addEventListener('change', (e) => {
    const c = ownScreenChange(e, screen, areas)
    if (c === null) return
    if (c.window !== undefined) {
      onWindow?.({ editor: c.window })
      return
    }
    areas = c.areas
    // Reconcile: an area that lives on (same identity and editor) keeps its element, so a drag of
    // an edge does not rebuild an editor (a text field's draft); the others are made or released.
    const next = new Map()
    areas.forEach(({ id, editor }, index) => {
      let h = held.get(id)
      if (h !== undefined && h.editor !== editor) { discard(h); h = undefined }
      next.set(id, h ?? { editor, element: editorFor(editors, editor).render({ index, editor }) })
      held.delete(id)
    })
    for (const h of held.values()) discard(h)
    held.clear()
    for (const [id, h] of next) held.set(id, h)
    let cursor = screen.firstChild
    for (const h of next.values()) {
      if (h.element === cursor) cursor = cursor.nextSibling
      else screen.insertBefore(h.element, cursor)
    }
    onChange?.({ layout: c.layout })
  })
  return screen
}

module.exports = { AreaSplit, REGIONS, SLOT, REGION_PROPS, REGION_FLAGS, layoutText, regionAttrs, screenChange, ownScreenChange, editorFor,
  editorElement, createScreen }
