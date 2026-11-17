'use strict'
// The pure parts of screen.js (no native window): area identities across layouts, the layout
// grammar's escaping, editor lookup, and createScreen's reconciling on a minimal fake document.
const test = require('node:test')
const assert = require('node:assert')
const { AreaSplit, layoutText, screenChange, ownScreenChange, editorFor, createScreen } = require('../screen')

/** A layout document as the kit writes it: `rects` are [x0, y0, x1, y1]. */
function doc (...areas) {
  const verts = []
  const vi = (x, y) => {
    let i = verts.findIndex((v) => v[0] === x && v[1] === y)
    if (i < 0) i = verts.push([x, y]) - 1
    return i
  }
  return JSON.stringify({
    bui_screen: 1, size: [100, 100], verts,
    areas: areas.map(([editor, [x0, y0, x1, y1]]) => ({ v: [vi(x0, y0), vi(x0, y1), vi(x1, y1), vi(x1, y0)], editor, regions: [] })),
  })
}

test('layoutText escapes only quotes and backslashes', () => {
  assert.strictEqual(layoutText('A\nB'), '"A\nB"')
  assert.strictEqual(layoutText('a"b\\c'), '"a\\"b\\\\c"')
  assert.strictEqual(layoutText(AreaSplit.v(0.5, 'x', 'y')), 'v0.5("x","y")')
})

test('editorFor ignores inherited properties', () => {
  const editors = { View: { render () {} } }
  assert.strictEqual(editorFor(editors, 'constructor'), editors.View)
  assert.strictEqual(editorFor(editors, 'View'), editors.View)
})

test('area identities survive a join, even between areas of one editor', () => {
  const first = screenChange(doc(['A', [0, 0, 50, 100]], ['A', [50, 0, 100, 100]])).areas
  assert.deepStrictEqual(first.map((a) => a.id), [1, 2])
  // an edge drag: same areas, new rectangles
  const dragged = screenChange(doc(['A', [0, 0, 30, 100]], ['A', [30, 0, 100, 100]]), first).areas
  assert.deepStrictEqual(dragged.map((a) => a.id), [1, 2])
  // the first joins into the second (the survivor grew): it keeps its identity, 2
  const joined = screenChange(doc(['A', [0, 0, 100, 100]]), dragged).areas
  assert.deepStrictEqual(joined.map((a) => a.id), [2])
  // a split makes a new one, the old keep theirs
  const split = screenChange(doc(['A', [0, 0, 100, 50]], ['B', [0, 50, 100, 100]]), joined).areas
  assert.deepStrictEqual(split.map((a) => a.id), [2, 3])
  // the kit's own id wins
  const kit = JSON.parse(doc(['A', [0, 0, 100, 100]]))
  kit.areas[0].id = 9
  assert.strictEqual(screenChange(JSON.stringify(kit), split).areas[0].id, 9)
})

test('the kit area ids are used as they are', () => {
  const withIds = (ids, text) => {
    const d = JSON.parse(text)
    d.areas.forEach((a, i) => { a.id = ids[i] })
    return JSON.stringify(d)
  }
  const first = screenChange(withIds([7, 9], doc(['A', [0, 0, 50, 100]], ['A', [50, 0, 100, 100]]))).areas
  assert.deepStrictEqual(first.map((a) => a.id), [7, 9])
  // the first area went (a join): the survivor keeps the kit's id whatever its rectangle does
  const joined = screenChange(withIds([9], doc(['A', [0, 0, 100, 100]])), first).areas
  assert.deepStrictEqual(joined.map((a) => a.id), [9])
})

test('screenChange: window and foreign values', () => {
  assert.deepStrictEqual(screenChange('window\tView'), { window: 'View' })
  assert.strictEqual(screenChange('x'), null)
  assert.strictEqual(screenChange(3), null)
  const screen = { value: doc(['A', [0, 0, 100, 100]]) }
  assert.strictEqual(ownScreenChange({ target: {} }, screen), null) // a nested screen's event
  assert.strictEqual(ownScreenChange({ target: screen }, screen).areas.length, 1)
})

/** The least of a DOM createScreen needs. */
function fakeDocument () {
  class El {
    constructor (tag) { this.tag = tag; this.kids = []; this.parentNode = null; this.listeners = []; this.destroyed = false; this._auto = false }
    addEventListener (t, f) { this.listeners.push(f) }
    setAttribute () {}
    get firstChild () { return this.kids[0] ?? null }
    get nextSibling () { const k = this.parentNode.kids; return k[k.indexOf(this) + 1] ?? null }
    insertBefore (c, before) {
      c.parentNode?.removeChild(c)
      const i = before === null ? this.kids.length : this.kids.indexOf(before)
      this.kids.splice(i, 0, c)
      c.parentNode = this
    }
    removeChild (c) { this.kids.splice(this.kids.indexOf(c), 1); c.parentNode = null }
    destroy () { this.destroyed = true }
  }
  return { createElement: (tag) => new El(tag) }
}

test('createScreen keeps editors across geometry changes and frees discarded ones', () => {
  const document = fakeDocument()
  const made = []
  const editors = { A: { render: () => { const e = document.createElement('bui-area'); made.push(e); return e } } }
  const screen = createScreen(document, { editors })
  const fire = (value) => { screen.value = value; screen.listeners.forEach((f) => f({ target: screen })) }
  fire(doc(['A', [0, 0, 50, 100]], ['A', [50, 0, 100, 100]]))
  assert.strictEqual(made.length, 2)
  fire(doc(['A', [0, 0, 30, 100]], ['A', [30, 0, 100, 100]])) // geometry only
  assert.strictEqual(made.length, 2)
  assert.deepStrictEqual(screen.kids, made)
  fire(doc(['A', [0, 0, 100, 100]])) // the first joined into the second
  assert.deepStrictEqual(screen.kids, [made[1]])
  assert.ok(made[0].destroyed && !made[1].destroyed, 'the closed area is released, the survivor kept')
})

test('an area without an id never takes one a neighbor has', () => {
  const d = JSON.parse(doc(['A', [0, 0, 50, 100]], ['A', [50, 0, 100, 100]]))
  d.areas[0].id = 1
  const areas = screenChange(JSON.stringify(d), []).areas
  assert.strictEqual(new Set(areas.map((a) => a.id)).size, 2, 'two distinct ids')
  assert.strictEqual(areas[0].id, 1)
  // a previous area whose id is explicit in the new document is not matched to another area
  const next = JSON.parse(doc(['A', [0, 0, 50, 100]], ['A', [50, 0, 100, 100]]))
  next.areas[1].id = areas[0].id
  const again = screenChange(JSON.stringify(next), areas).areas
  assert.strictEqual(new Set(again.map((a) => a.id)).size, 2)
  assert.strictEqual(again[1].id, areas[0].id)
})
