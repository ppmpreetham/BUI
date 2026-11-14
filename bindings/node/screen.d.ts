// Blender's screen for JavaScript (screen.js): areas the user splits, joins, docks and resizes,
// and the regions inside them.
//
// SPDX-License-Identifier: GPL-2.0-or-later

/** A screen's first arrangement: an editor's name, or two split by an edge. */
export type AreaSplitLayout = string | { axis: 'h' | 'v', factor: number, first: AreaSplitLayout, second: AreaSplitLayout }
export const AreaSplit: {
  /** `first` on the left taking `factor` of the width, `second` on the right. */
  v (factor: number, first: AreaSplitLayout, second: AreaSplitLayout): AreaSplitLayout
  /** `first` on top taking `factor` of the height, `second` below. */
  h (factor: number, first: AreaSplitLayout, second: AreaSplitLayout): AreaSplitLayout
}

/** An area an editor's `render` fills: its place in the screen's area order and its editor. */
export interface AreaInfo { index: number, editor: string }
export interface ScreenChangeEvent { layout: string }
export interface AreaWindowEvent { editor: string }
export interface RegionResizeEvent { size: number, hidden: boolean }

/** A region's options. `size`: unscaled pixels across the area; `side`: the sides its kind allows. */
export interface RegionOptions {
  size?: number
  hidden?: boolean
  side?: 'left' | 'right' | 'top' | 'bottom'
  overlap?: boolean
  resizable?: boolean
  maxsize?: number
  /** Toggles it while the pointer is over the area: `'n'`, `'ctrl+]'`. */
  hotkey?: string
  onResize?: (e: RegionResizeEvent) => void
}

/** The region slots of an editor (one of each). */
export type RegionSlot = 'header' | 'toolHeader' | 'footer' | 'assetShelf' | 'channels' | 'navBar' | 'toolbar' |
  'sidebar' | 'execute' | 'hud'

export const REGIONS: readonly string[]
export const SLOT: Record<RegionSlot, string>
export const REGION_PROPS: readonly string[]
/** The text form of a layout (`<bui-screen layout="...">`). */
export function layoutText (layout: AreaSplitLayout | null | undefined): string | null
export function regionAttrs (props: RegionOptions): Record<string, string>
/** What a `change` value of a screen means (null: another element's). */
export function screenChange (value: unknown): { layout: string, areas: string[] } | { window: string } | null
export function editorFor<E> (editors: Record<string, E>, name: string): E

/** A `<bui-area>`: `main` and the regions, each an element or `{ element, ...RegionOptions }`. */
export function editorElement (document: { createElement (tag: string): unknown },
  regions: { main?: unknown } & Partial<Record<RegionSlot, unknown>>): unknown

/** A `<bui-screen>` for the DOM-like API: each editor's `render` returns an `editorElement`. */
export function createScreen (document: { createElement (tag: string): unknown }, options: {
  editors: Record<string, { icon?: string, render (area: AreaInfo): unknown }>
  layout?: AreaSplitLayout
  saved?: string
  ops?: 'all' | 'resize'
  onChange?: (e: ScreenChangeEvent) => void
  onWindow?: (e: AreaWindowEvent) => void
}): unknown
