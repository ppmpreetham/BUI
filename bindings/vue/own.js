// What @blender-ui/vue adds to Vue: a window to draw into (`render`, `createRoot`, `createApp`),
// Blender's widgets as components, and BUI's signals. index.js and index.cjs put it next to
// everything `vue` exports; this file has no `export *`, so a `require()` of it (index.cjs) gets
// these names, not Vue's (`render` and `createApp` are in both).
//
//   import { render, ref } from '@blender-ui/vue'
//   import App from './App.vue'
//   render(App, { props: { name: 'BUI' }, title: 'App', width: 400, height: 300 })
//
// It is a custom renderer (Vue's `createRenderer`) over @blender-ui/native's DOM-like tree:
// Vue's own runtime (reactivity, the virtual DOM diff, components, slots, `provide`/`inject`)
// runs unchanged, and its node operations create detached elements that join a window's retained
// tree when inserted. Only the changes of each tick cross to the native UI thread. What needs a
// browser (`<Transition>`, `v-html`, DOM refs' layout APIs) does not work; the README lists it.
//
// SPDX-License-Identifier: GPL-2.0-or-later
import { Fragment, cloneVNode, createRenderer, h, ref } from 'vue'
import native from '@blender-ui/native'
import host from '@blender-ui/native/host'
import screen from '@blender-ui/native/screen'
import { patchProp, preset } from './props.js'

const { document } = native // a window-less document: its nodes are detached until inserted

const renderer = createRenderer({
  patchProp,
  insert: (child, parent, anchor) => { parent.insertBefore(child, anchor ?? null) },
  // a node made by `document` and dropped is freed natively at the end of the tick
  remove: (child) => { child.parentNode?.removeChild(child) },
  createElement: (tag, namespace, is, props) => preset(document.createElement(tag), props),
  createText: (text) => document.createTextNode(text),
  createComment: (text) => document.createComment(text),
  setText: (node, text) => { node.nodeValue = text },
  setElementText: (el, text) => { el.textContent = text },
  parentNode: (node) => node.parentNode,
  nextSibling: (node) => node.nextSibling,
  querySelector: () => null, // `<Teleport to="#id">`: there is no page to search; teleport to an element
  setScopeId: (el, id) => { el.setAttribute(id, '') }, // `<style scoped>` selects on it
})

/** Vue's `createApp`, for BUI: `createApp(App).use(plugin)`, then `render(app, options)`. */
export const createApp = renderer.createApp

const styles = host.styleRegistry() // scoped <style> blocks of components, added to each window once

/** Called by compiled single-file components that have a <style> block (compile.js). */
export function addStyle (key, css) {
  styles.add(key, css)
}

const isApp = (x) => typeof x?.mount === 'function' && typeof x.use === 'function'

/** A root mounting a Vue app (or a component) into `window`'s body; unmounted when the window closes. */
export function createRoot (window) {
  let app = null
  const root = {
    /** Mount `component` (or an app made with `createApp`), replacing what this root showed; returns
     *  the root component's instance. `options.props` are the root component's props. */
    render (component, options = {}) {
      root.unmount()
      styles.attach(window)
      app = isApp(component) ? component : createApp(component, options.props)
      return app.mount(window.body)
    },
    unmount () {
      app?.unmount()
      app = null
    },
  }
  window.on('close', () => root.unmount())
  return root
}

/**
 * Open a window (and an App, unless `options.app` is given) and mount `component` in it: a
 * component, or an app made with `createApp` (to `.use()` Pinia and the like first).
 * @param {import('vue').Component | import('vue').App} component
 * @param {{ props?: Record<string, unknown>, title?: string, width?: number, height?: number, resizable?: boolean,
 *           gpu?: boolean, slug?: boolean, headless?: boolean, app?: import('@blender-ui/native').App }} [options]
 */
export function render (component, options = {}) {
  const { app, window } = host.open(options)
  const root = createRoot(window)
  const instance = root.render(component, options)
  return { app, window, root, component: instance }
}

// Blender's widgets as components: `<NumberSlider v-model="n" :min="0" :max="10" />`,
// `<Button @click="apply">Apply</Button>`. Always Blender widgets; their lowercase HTML
// counterparts look like a browser's (Blender's inside `styled`).
export const { Button, Checkbox, Radio, NumberField, NumberSlider, TextField, TextArea, ColorSwatch, Dropdown,
  ProgressBar } = host.blenderWidgets((tag) => {
  // no declared props: every attribute, `v-model` and listener included, goes to the element once
  const Widget = (props, { slots }) => h(tag, props, slots.default?.())
  Widget.inheritAttrs = false
  return Widget
})

// Blender's screen (@blender-ui/native/screen: the elements blenderui lays out):
//   <Screen :editors="{ '3D Viewport': { icon: 'VIEW3D', render: (area) => h(Viewport) } }"
//           :layout="AreaSplit.v(0.75, '3D Viewport', 'Properties')" @change="(e) => save(e.layout)" />
// An editor's render returns an <Editor>: its default slot is the main view, its named slots
// (`header`, `toolHeader`, `toolbar`, `sidebar` ...) the regions, each content or a <Region>.

/** A region of an <Editor> (its slot names the kind): `size`, `hidden`, `side`, `overlap`,
 *  `resizable`, `maxsize`, `hotkey`, `@resize="({ size, hidden }) => ..."`. */
export const Region = {
  name: 'Region',
  props: ['region', ...screen.REGION_PROPS],
  emits: ['resize'],
  setup (props, { slots, emit }) {
    const onChange = (e) => {
      if (e.target.localName === 'bui-region') emit('resize', { size: e.target.valueAsNumber, hidden: e.target.checked })
    }
    return () => h('bui-region', { region: props.region, ...screen.regionAttrs(props), onChange }, slots.default?.())
  },
}

/** What an area shows: the default slot (the main view) and the regions' named slots. */
export const Editor = {
  name: 'Editor',
  setup (_, { slots }) {
    return () => {
      const out = []
      for (const [slot, kind] of Object.entries(screen.SLOT)) {
        const content = slots[slot]?.()
        if (!content) continue
        const one = content.length === 1 && content[0].type === Region
        out.push(one ? cloneVNode(content[0], { region: kind, key: kind })
          : h(Region, { region: kind, key: kind }, () => content))
      }
      out.push(h('bui-main', { key: 'main' }, slots.default?.()))
      return h('bui-area', null, out)
    }
  },
}

/** Areas the user splits, joins, docks and resizes as in Blender. `editors`: name ->
 *  `{ icon, render(area) }` (`area`: `{ index, editor }`) returning an <Editor> vnode; `layout`:
 *  the first arrangement (`AreaSplit.v/h` or a name); `saved`: a layout from `change`; events
 *  `change({ layout })` and `window({ editor })`; `ops="resize"`. */
export const Screen = {
  name: 'Screen',
  props: ['editors', 'layout', 'saved', 'ops'],
  emits: ['change', 'window'],
  setup (props, { emit }) {
    const areas = ref([])
    const onChange = (e) => {
      if (e.target.localName !== 'bui-screen') return
      const c = screen.screenChange(e.target.value)
      if (c === null) return
      if (c.window !== undefined) {
        emit('window', { editor: c.window })
        return
      }
      areas.value = c.areas
      emit('change', { layout: c.layout })
    }
    return () => h('bui-screen', { layout: screen.layoutText(props.layout), saved: props.saved, ops: props.ops, onChange },
      areas.value.map((editor, index) => h(Fragment, { key: `${index}:${editor}` },
        [screen.editorFor(props.editors, editor).render({ index, editor })])))
  },
}

export const AreaSplit = screen.AreaSplit

// BUI's own signals, for state shared outside components (Vue's refs are Vue's): a signal as the
// value of a prop or an attribute binds it, and the node follows it without a re-render. (`effect`
// stays Vue's.)
export const { signal, batch, untrack, createSignal, isSignal, Signal, Memo } = native
/** A derived, cached BUI signal (`computed` stays Vue's). */
export const memo = native.memo
