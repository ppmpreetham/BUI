// SPDX-License-Identifier: GPL-2.0-or-later

package bui

/*
#include "glue.h"
*/
import "C"

import (
	"unsafe"
)

// Context shows a tree: it lays it out, takes input and renders, into a
// Raster or a Window.
type Context struct {
	h         *C.BKContext
	id        uintptr
	root      *Node
	onEvent   func(Event)
	onRequest func()
}

// NewContext makes a context showing root.
func NewContext(root *Node) *Context {
	c := &Context{h: C.BK_context_new(root.h), id: newID(), root: root}
	contexts[c.id] = c
	C.bui_go_set_host(c.h, C.uintptr_t(c.id))
	return c
}

// Free frees the context (not its tree).
func (c *Context) Free() {
	if c.h == nil {
		return
	}
	delete(contexts, c.id)
	C.BK_context_free(c.h)
	c.h = nil
}

// Root is the tree the context shows.
func (c *Context) Root() *Node { return wrap(C.BK_context_root(c.h)) }

// SetHeader shows a fixed bar over the tree (nil: none).
func (c *Context) SetHeader(header *Node) {
	var h *C.BKNode
	if header != nil {
		h = header.h
	}
	C.BK_context_set_header(c.h, h)
}

// OnEvent sees every event of the context's nodes, after their own handlers.
func (c *Context) OnEvent(fn func(Event)) { c.onEvent = fn }

// OnRequest runs when the context needs a render (headless hosts; a window
// renders by itself).
func (c *Context) OnRequest(fn func()) { c.onRequest = fn }

// Render draws into r at w x h, only what changed unless full; it returns the
// number of regions drawn (0: nothing changed).
func (c *Context) Render(r *Raster, w, h int, full bool) int {
	return int(C.BK_render(c.h, r.h, C.int(w), C.int(h), cbool(full)))
}

// Configure lays the tree out for a w x h window without drawing; true when
// the size changed.
func (c *Context) Configure(w, h int) bool { return C.BK_configure(c.h, C.int(w), C.int(h)) != 0 }

// NeedsRender reports damage waiting to be drawn.
func (c *Context) NeedsRender() bool { return C.BK_needs_render(c.h) != 0 }

// Animating reports an animation running: keep rendering frames.
func (c *Context) Animating() bool { return C.BK_animating(c.h) != 0 }

// -- input: each returns true when something changed ------------------------------

// Press presses the left button at (x, y) with modifiers.
func (c *Context) Press(x, y, mods int) bool {
	return C.BK_press(c.h, C.int(x), C.int(y), C.int(mods)) != 0
}

// Release releases the left button.
func (c *Context) Release(x, y, mods int) bool {
	return C.BK_release(c.h, C.int(x), C.int(y), C.int(mods)) != 0
}

// Motion moves the pointer.
func (c *Context) Motion(x, y, mods int) bool {
	return C.BK_motion(c.h, C.int(x), C.int(y), C.int(mods)) != 0
}

// Click presses and releases at (x, y).
func (c *Context) Click(x, y, mods int) bool {
	a := c.Motion(x, y, mods)
	b := c.Press(x, y, mods)
	return c.Release(x, y, mods) || a || b
}

// Wheel turns the wheel by delta notches.
func (c *Context) Wheel(x, y, delta, mods int) bool {
	return C.BK_wheel(c.h, C.int(x), C.int(y), C.int(delta), C.int(mods)) != 0
}

// Scroll scrolls precisely: dx > 0 right, dy > 0 up, in notches.
func (c *Context) Scroll(x, y int, dx, dy float32, mods int) bool {
	return C.BK_scroll(c.h, C.int(x), C.int(y), C.float(dx), C.float(dy), C.int(mods)) != 0
}

// Key presses a key: the text it types and its X11 keysym ("Return", "a",
// "Up"); either may be empty.
func (c *Context) Key(text, keysym string, mods int) bool {
	changed := false
	withCstr(text, func(t *C.char) {
		withOptCstr(keysym, func(k *C.char) { changed = C.BK_key(c.h, t, k, C.int(mods)) != 0 })
	})
	return changed
}

// KeyRelease releases a key (raw events only).
func (c *Context) KeyRelease(keysym string, mods int) bool {
	return cstr(keysym, func(k *C.char) bool { return C.BK_key_release(c.h, k, C.int(mods)) != 0 })
}

// Button presses or releases button 1, 2 or 3 (raw events only).
func (c *Context) Button(x, y, button int, release bool, mods int) bool {
	return C.BK_button(c.h, C.int(x), C.int(y), C.int(button), cbool(release), C.int(mods)) != 0
}

// ContextMenu opens the context menu at (x, y).
func (c *Context) ContextMenu(x, y, mods int) bool {
	return C.BK_context_menu(c.h, C.int(x), C.int(y), C.int(mods)) != 0
}

// Timer fires a timer the context scheduled.
func (c *Context) Timer(id int) { C.BK_timer(c.h, C.int(id)) }

// Bind runs fn for a key combination, as "ctrl+s" or "shift+F1". It reports
// false when the context holds no more bindings (the kit keeps 64).
func (c *Context) Bind(combo string, fn func()) bool {
	ref := newAction(fn)
	withCstr(combo, func(s *C.char) { C.BK_bind(c.h, s, ref) })
	id := uintptr(ref)
	if a := actions[id]; a != nil && a.refs == 0 { // the kit did not take it
		delete(actions, id)
		return false
	}
	return true
}

// ClosePopups closes menus and popups.
func (c *Context) ClosePopups() { C.BK_close_popups(c.h) }

// SetPointer moves the pointer without an event.
func (c *Context) SetPointer(x, y int) { C.BK_set_pointer(c.h, C.int(x), C.int(y)) }

// Undo undoes the last value edit (Blender's Ctrl+Z).
func (c *Context) Undo() bool { return C.BK_undo(c.h) != 0 }

// Redo redoes it.
func (c *Context) Redo() bool { return C.BK_redo(c.h) != 0 }

// -- introspection ---------------------------------------------------------------

// HitAt is the interactive node under (x, y), nil over nothing.
func (c *Context) HitAt(x, y int) *Node { return wrap(C.BK_hit_at(c.h, C.int(x), C.int(y))) }

// Hover is the node under the pointer.
func (c *Context) Hover() *Node { return wrap(C.BK_hover(c.h)) }

// Focus is the node with the keyboard focus.
func (c *Context) Focus() *Node { return wrap(C.BK_focus(c.h)) }

// FocusNode gives a text field the keyboard focus (nil ends the edit).
func (c *Context) FocusNode(n *Node) {
	var h *C.BKNode
	if n != nil {
		h = n.h
	}
	C.BK_focus_node(c.h, h)
}

// Cursor is the pointer shape the node under the pointer asks for.
func (c *Context) Cursor() int { return int(C.BK_cursor(c.h)) }

// ModalActive reports a modal interaction (a drag, a text edit).
func (c *Context) ModalActive() bool { return C.BK_modal_active(c.h) != 0 }

// TooltipVisible reports a tooltip showing.
func (c *Context) TooltipVisible() bool { return C.BK_tooltip_visible(c.h) != 0 }

// Popups are the open menus and popups.
func (c *Context) Popups() []*Node {
	out := make([]*Node, int(C.BK_popup_count(c.h)))
	for i := range out {
		out[i] = wrap(C.BK_popup(c.h, C.int(i)))
	}
	return out
}

// Clipboard reads the clipboard through the context's host.
func (c *Context) Clipboard() string {
	p := C.BK_clipboard_get(c.h)
	if p == nil {
		return ""
	}
	defer C.free(unsafe.Pointer(p))
	return C.GoString(p)
}

// SetClipboard writes the clipboard through the context's host.
func (c *Context) SetClipboard(text string) {
	withCstr(text, func(s *C.char) { C.BK_clipboard_set(c.h, s) })
}

// MenuRows are the texts of a menu popup's rows.
func MenuRows(menu *Node) []string {
	out := make([]string, int(C.BK_menu_row_count(menu.h)))
	for i := range out {
		out[i] = C.GoString(C.BK_menu_row_text(menu.h, C.int(i)))
	}
	return out
}

// MenuRowRect is the box of a menu popup's row.
func MenuRowRect(menu *Node, i int) Rect {
	var r [4]C.int
	C.BK_menu_row_rect(menu.h, C.int(i), &r[0])
	return Rect{int(r[0]), int(r[1]), int(r[2]), int(r[3])}
}

// -- raster ------------------------------------------------------------------------

// Raster is a software frame buffer a Context renders into, without a window.
type Raster struct{ h *C.BuiRaster }

// NewRaster makes a w x h raster.
func NewRaster(w, h int) *Raster { return &Raster{C.BUI_raster_create(C.int(w), C.int(h))} }

// Free frees the raster.
func (r *Raster) Free() {
	if r.h != nil {
		C.BUI_raster_free(r.h)
		r.h = nil
	}
}

// Size is the raster's size in pixels.
func (r *Raster) Size() (w, h int) {
	return int(C.BUI_raster_width(r.h)), int(C.BUI_raster_height(r.h))
}

// Pixels are the frame as 0x00RRGGBB, row by row; the slice is the raster's
// own memory, valid until the next render or Free.
func (r *Raster) Pixels() []uint32 {
	w, h := r.Size()
	p := C.BUI_raster_pixels(r.h)
	if p == nil {
		return nil
	}
	return unsafe.Slice((*uint32)(unsafe.Pointer(p)), w*h)
}

func cbool(b bool) C.int {
	if b {
		return 1
	}
	return 0
}
