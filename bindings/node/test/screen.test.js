'use strict'
// Blender's screen from the DOM-like API (screen.js over blenderui's screen): the areas come from
// the first layout, each editor's element lands where the kit puts its area, regions take their
// size, and `change` reports the layout.
const test = require('node:test')
const assert = require('node:assert')
const { App, AreaSplit, createScreen, editorElement } = require('..')

test('screen: areas from AreaSplit, regions at the kit\'s rects, layout events (headless)', async (t) => {
  const app = new App({ headless: true })
  t.after(() => app.quit())
  const win = app.window({ width: 900, height: 600 })
  win.body.style = 'margin: 0; width: 900px; height: 600px'
  const layouts = []
  const mains = {}
  let sideLabel = null
  const label = (text) => {
    const d = win.createElement('div')
    d.textContent = text
    return d
  }
  const editor = (name) => ({
    icon: 'QUESTION',
    render: (area) => {
      const main = label(name)
      mains[name] = main
      return editorElement(win, {
        main,
        header: label(name + ' header'),
        sidebar: name === 'View' ? { element: (sideLabel = label('side')), size: 200 } : null,
      })
    },
  })
  const screen = createScreen(win, {
    editors: { View: editor('View'), Outliner: editor('Outliner') },
    layout: AreaSplit.v(0.7, 'View', 'Outliner'),
    onChange: (e) => layouts.push(e.layout),
  })
  win.body.appendChild(screen)
  for (let i = 0; i < 20 && (layouts.length === 0 || !mains.Outliner); i++) await win.screenshot()
  for (let i = 0; i < 3; i++) await win.screenshot() // the kit lays the areas out, the elements follow
  assert.ok(layouts.length > 0, 'the first layout came as a change')
  assert.match(layouts[0], /"bui_screen": 1/)
  const view = await mains.View.getBoundingClientRect()
  const outl = await mains.Outliner.getBoundingClientRect()
  assert.ok(view.x < 10 && outl.x > 600, `${JSON.stringify(view)} ${JSON.stringify(outl)}`)
  assert.ok(view.y > 20, 'below the header')
  // the sidebar's content fills the region: 200 px on the right of the View area
  const side = await sideLabel.getBoundingClientRect()
  assert.ok(Math.abs(side.width - 200) < 2 && side.x > view.x, JSON.stringify(side))
})
