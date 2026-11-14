// @blender-ui/react: a React renderer for BUI (react-reconciler 0.34 / React 19.3).
//
//   const { render } = require('@blender-ui/react')
//   render(<div className="p-4 bg-[#4772b3]" onClick={...}>Hello</div>,
//          { title: 'App', width: 400, height: 300 })
//
// Host components are HTML elements: React mutates @blender-ui/native's
// retained tree, which sends only the changes of each commit to the native
// UI thread. Events arrive as BUI events (onClick, onChange, onKeyDown ...).
//
// SPDX-License-Identifier: GPL-2.0-or-later
'use strict'

const React = require('react')
const Reconciler = require('react-reconciler')
const { ConcurrentRoot, DefaultEventPriority, DiscreteEventPriority, NoEventPriority } =
  require('react-reconciler/constants')
const native = require('@blender-ui/native')
const { TextNode, Signal, Memo, isSignal } = native
const { open, blenderWidgets, applyProps, isText, NO_PROPS } = require('@blender-ui/native/host')
const hooks = require('./hooks')

// A signal rendered as a child (`<span>{count}</span>`) becomes this host
// type: a text node bound to the signal, updated without re-rendering.
const SIGNAL_TEXT = '#bui-signal-text'
for (const C of [Signal, Memo]) {
  Object.defineProperties(C.prototype, {
    $$typeof: { value: Symbol.for('react.transitional.element'), configurable: true },
    type: { value: SIGNAL_TEXT, configurable: true },
    key: { value: null, configurable: true },
    ref: { value: null, configurable: true },
    _owner: { value: null, configurable: true },
    // React (dev) marks list children validated here: a fresh object each time
    _store: { get () { return { validated: 1 } }, configurable: true },
    props: { get () { return (this._reactProps ??= { signal: this }) }, configurable: true },
  })
}

// Nodes a render created. React may throw a render away (an interrupted or
// suspended concurrent render); its nodes are then never attached, and are
// freed after the next commit.
function created (container, node) {
  container.created.push(node)
  return node
}

function sweep (container) {
  const body = container.window.body
  for (const node of container.created) {
    let top = node
    while (top.parentNode !== null) top = top.parentNode
    if (top !== body && container.window._nodes.has(top.id)) top.destroy()
  }
  container.created.length = 0
}

let updatePriority = NoEventPriority
const ROOT_CONTEXT = {}

const host = {
  rendererPackageName: '@blender-ui/react',
  rendererVersion: require('./package.json').version,
  supportsMutation: true,
  supportsPersistence: false,
  supportsHydration: false,
  supportsMicrotasks: true,
  isPrimaryRenderer: true,
  warnsIfNotActing: false,
  noTimeout: -1,
  scheduleTimeout: setTimeout,
  cancelTimeout: clearTimeout,
  scheduleMicrotask: queueMicrotask,

  getRootHostContext: () => ROOT_CONTEXT, // must not be null (React checks)
  getChildHostContext: (parent) => parent,
  getPublicInstance: (instance) => instance,
  prepareForCommit: () => null,
  resetAfterCommit (container) {
    if (container.created.length > 0) sweep(container)
    container.window.flush() // one native call per commit
  },
  preparePortalMount () {},

  createInstance (type, props, container) {
    if (type === SIGNAL_TEXT) return created(container, container.window.createTextNode(props.signal))
    const el = container.window.createElement(type)
    applyProps(el, NO_PROPS, props)
    return created(container, el)
  },
  createTextInstance: (text, container) => created(container, container.window.createTextNode(text)),
  appendInitialChild: (parent, child) => parent.appendChild(child),
  finalizeInitialChildren: () => false,
  shouldSetTextContent: (type, props) => isText(props.children),

  appendChild: (parent, child) => parent.appendChild(child),
  appendChildToContainer: (container, child) => container.window.body.appendChild(child),
  insertBefore: (parent, child, before) => parent.insertBefore(child, before),
  insertInContainerBefore: (container, child, before) => container.window.body.insertBefore(child, before),
  removeChild (parent, child) {
    parent.removeChild(child)
    child.destroy()
  },
  removeChildFromContainer (container, child) {
    container.window.body.removeChild(child)
    child.destroy()
  },
  clearContainer (container) {
    for (const c of [...container.window.body.childNodes]) container.window.body.removeChild(c)
  },
  // the subtree: text set through textContent has no fiber of its own (freeing twice is a no-op)
  detachDeletedInstance: (node) => node.destroy(),

  commitUpdate (el, type, prev, next) {
    if (type === SIGNAL_TEXT) {
      if (prev.signal !== next.signal) el.data = next.signal
    } else {
      applyProps(el, prev, next)
    }
  },
  commitTextUpdate: (text, prev, next) => { text.data = next },
  commitMount () {},
  resetTextContent: (el) => { el.textContent = '' },
  hideInstance (el) {
    if (el instanceof TextNode) el.data = '' // a signal's text: unbound while hidden
    else el.setAttribute('hidden', '')
  },
  unhideInstance (el, props) {
    if (el instanceof TextNode) el.data = props.signal
    else if (!props.hidden) el.removeAttribute('hidden')
  },
  hideTextInstance: (text) => { text._hidden = text.data; text.data = '' },
  unhideTextInstance: (text, value) => { text.data = value },

  setCurrentUpdatePriority: (p) => { updatePriority = p },
  getCurrentUpdatePriority: () => updatePriority,
  resolveUpdatePriority: () => updatePriority || DefaultEventPriority,
  resolveEventType: () => null,
  resolveEventTimeStamp: () => -1.1,
  shouldAttemptEagerTransition: () => false,
  trackSchedulerEvent () {},
  requestPostPaintCallback () {},
  maySuspendCommit: () => false,
  preloadInstance: () => true,
  startSuspendingCommit () {},
  suspendInstance () {},
  waitForCommitToBeReady: () => null,
  NotPendingTransition: null,
  HostTransitionContext: React.createContext(null),
  resetFormInstance () {},
  getInstanceFromNode: () => null,
  getInstanceFromScope: () => null,
  beforeActiveInstanceBlur () {},
  afterActiveInstanceBlur () {},
  prepareScopeUpdate () {},
  bindToConsole: (method, args) => Function.prototype.bind.apply(console[method], [console, ...args]),

  // Commits that wait on resources: none here.
  maySuspendCommitOnUpdate: () => false,
  maySuspendCommitInSyncRender: () => false,
  getSuspendedCommitReason: () => null,

  // View transitions (<ViewTransition>): not animated here. As React DOM does
  // without the browser API: run the commit's phases now and report none.
  startViewTransition (suspendedState, container, types, mutation, layout, afterMutation, spawnedWork) {
    mutation()
    layout()
    spawnedWork()
    return null
  },
  startGestureTransition: () => null,
  stopViewTransition () {},
  suspendOnActiveViewTransition () {},
  addViewTransitionFinishedListener (transition, callback) { callback() },
  createViewTransitionInstance: (name) => ({ name, group: null, imagePair: null, old: null, new: null }),
  applyViewTransitionName () {},
  restoreViewTransitionName () {},
  cancelViewTransitionName () {},
  cancelRootViewTransitionName () {},
  restoreRootViewTransitionName () {},
  cloneRootViewTransitionContainer: (container) => container,
  removeRootViewTransitionClone () {},
  measureInstance: () => null,
  measureClonedInstance: () => null,
  wasInstanceInViewport: () => true,
  hasInstanceChanged: () => false,
  hasInstanceAffectedParent: () => false,
  getCurrentGestureOffset: () => 0,

  // <Fragment ref>: the fragment's fiber, nothing more (no DOM-like API yet).
  createFragmentInstance: (fiber) => ({ _fragmentFiber: fiber }),
  updateFragmentInstanceFiber (fiber, instance) { instance._fragmentFiber = fiber },
  commitNewChildToFragmentInstance () {},
  deleteChildFromFragmentInstance () {},
}

const reconciler = Reconciler(host)

// Events: run handlers as discrete updates (React DOM does the same), so a
// click's state change commits right away.
const nativeDispatch = native.Element.prototype._dispatch
native.Element.prototype._dispatch = function (raw) {
  const prev = updatePriority
  updatePriority = DiscreteEventPriority
  try {
    nativeDispatch.call(this, raw)
  } finally {
    updatePriority = prev
  }
}

function report (kind) {
  return (err) => console.error(`@blender-ui/react: ${kind}`, err)
}

// The root component gets render tracking even when created with React's
// own createElement (components below need this package's JSX runtime or
// createElement).
function trackRoot (element) {
  if (!React.isValidElement(element)) return element
  const type = hooks.track(element.type)
  if (type === element.type) return element
  return React.createElement(type, element.key != null ? { ...element.props, key: element.key } : element.props)
}

/** A React root rendering into `window`'s body. */
function createRoot (window) {
  const container = { window, created: [] }
  const root = reconciler.createContainer(container, ConcurrentRoot, null, false, null, 'bui',
    report('uncaught error'), report('caught error'), report('recoverable error'), null)
  window.on('close', () => reconciler.updateContainer(null, root, null, null))
  return {
    render (element) { reconciler.updateContainer(trackRoot(element), root, null, null) },
    unmount () { reconciler.updateContainer(null, root, null, null) },
  }
}

/**
 * Open a window and render `element` into it.
 * @param {React.ReactNode} element
 * @param {{ title?: string, width?: number, height?: number, resizable?: boolean,
 *           gpu?: boolean, slug?: boolean, headless?: boolean, app?: import('@blender-ui/native').App }} [options]
 * @returns {{ app: import('@blender-ui/native').App, window: import('@blender-ui/native').Window, root: ReturnType<typeof createRoot> }}
 */
function render (element, options = {}) {
  const { app, window } = open(options)
  const root = createRoot(window)
  root.render(element)
  return { app, window, root }
}

/** Run `fn` and flush React's work synchronously (tests). */
function act (fn) {
  const flushSync = reconciler.flushSyncFromReconciler ?? reconciler.flushSync
  return typeof flushSync === 'function' ? flushSync(fn) : fn()
}

// React's hooks by React's names: those that hold or react to state are the
// signal versions (hooks.js), the others are React's own.
const reactHooks = {}
for (const k of Object.keys(React)) if (/^use([A-Z]|$)/.test(k)) reactHooks[k] = React[k]

// Blender's widgets as components: `<NumberSlider min={0} max={10} />`,
// `<Button onClick={..}>Apply</Button>`. Always Blender widgets; their
// lowercase HTML counterparts look like a browser's (Blender's inside `styled`).
const blenderComponents = blenderWidgets((name) => (props) => React.createElement(name, props))

// Blender's screen (@blender-ui/native/screen: the elements blenderui lays out):
//   <Screen editors={{ '3D Viewport': { icon: 'VIEW3D', render: (area) =>
//       <Editor header={<div>View</div>} sidebar={<Region size={280} hotkey="n">...</Region>}>...</Editor> } }}
//     layout={AreaSplit.v(0.75, '3D Viewport', 'Properties')} saved={saved} onChange={(e) => save(e.layout)} />
const screen = require('@blender-ui/native/screen')

/** A region of an `<Editor>` (its slot names the kind): `size`, `hidden`, `side`, `overlap`,
 *  `resizable`, `maxsize`, `hotkey`, `onResize({ size, hidden })`. */
function Region ({ region, children, onResize, ...props }) {
  const onChange = onResize && ((e) => {
    if (e.target.localName === 'bui-region') onResize({ size: e.target.valueAsNumber, hidden: e.target.checked })
  })
  return React.createElement('bui-region', { region, ...screen.regionAttrs(props), onChange }, children)
}

/** What an area shows: `children` (the main view) and regions by slot (`header`, `toolHeader`,
 *  `footer`, `assetShelf`, `channels`, `navBar`, `toolbar`, `sidebar`, `execute`, `hud`): each an
 *  element or a `<Region>`. */
function Editor ({ children, ...regions }) {
  const out = []
  for (const [slot, value] of Object.entries(regions)) {
    const kind = screen.SLOT[slot]
    if (kind === undefined) throw new TypeError(`<Editor>: no region '${slot}'`)
    if (value == null) continue
    out.push(React.isValidElement(value) && value.type === Region
      ? React.cloneElement(value, { region: kind, key: kind })
      : React.createElement(Region, { region: kind, key: kind }, value))
  }
  out.push(React.createElement('bui-main', { key: 'main' }, children))
  return React.createElement('bui-area', null, out)
}

/** Areas the user splits, joins, docks and resizes as in Blender. `editors`: name ->
 *  `{ icon, render(area) }` (`area`: `{ index, editor }`) returning an `<Editor>`; `layout`: the
 *  first arrangement (`AreaSplit.v/h`, or an editor's name); `saved`: a layout `onChange` gave;
 *  `onChange({ layout })`, `onWindow({ editor })`, `ops="resize"`. */
function Screen ({ editors, layout, saved, onChange, onWindow, ops, children, ...rest }) {
  const [areas, setAreas] = React.useState(() => [])
  const handle = (e) => {
    if (e.target.localName !== 'bui-screen') return
    const c = screen.screenChange(e.target.value)
    if (c === null) return
    if (c.window !== undefined) {
      onWindow?.({ editor: c.window })
      return
    }
    setAreas(c.areas)
    onChange?.({ layout: c.layout })
  }
  return React.createElement('bui-screen', { ...rest, layout: screen.layoutText(layout), saved, ops, onChange: handle },
    areas.map((editor, index) => React.createElement(React.Fragment, { key: `${index}:${editor}` },
      screen.editorFor(editors, editor).render({ index, editor }))))
}

module.exports = {
  ...blenderComponents,
  Screen,
  Editor,
  Region,
  AreaSplit: screen.AreaSplit,
  ...reactHooks,
  ...hooks,
  render,
  createRoot,
  act,
  reconciler,
  signal: native.signal,
  computed: native.memo, // `memo` stays React's (components)
  effect: native.effect,
  batch: native.batch,
  untrack: native.untrack,
  createSignal: native.createSignal,
  ref: native.ref,
  isSignal,
  Signal,
  Memo,
}
