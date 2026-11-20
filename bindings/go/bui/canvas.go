// SPDX-License-Identifier: GPL-2.0-or-later

package bui

/*
#include "glue.h"
*/
import "C"

import (
	"unsafe"
)

// Canvas item types (a "canvas" node's display list, Tk's Canvas).
const (
	ItemRect      = C.BK_ITEM_RECT
	ItemOval      = C.BK_ITEM_OVAL
	ItemLine      = C.BK_ITEM_LINE
	ItemPolygon   = C.BK_ITEM_POLYGON
	ItemText      = C.BK_ITEM_TEXT
	ItemArc       = C.BK_ITEM_ARC
	ItemImage     = C.BK_ITEM_IMAGE
	ItemRoundRect = C.BK_ITEM_ROUNDRECT
)

// CanvasItem is one shape of a canvas. Coords are x0 y0 x1 y1 ... in canvas
// space; a Fill or Outline with A 0 is not drawn.
type CanvasItem struct {
	ID, Type      int
	Coords        []float32
	Fill, Outline Color
	Width         float32 // line width
	Radius        float32 // round rect corners
	Size          float32 // text size, 0 for the UI's
	Start, Extent float32 // arcs, degrees
	Text          string
	Anchor        int    // Anchor* bits, 0 centred
	Src           string // ItemImage: an image file, fitted into the box by Fit
	Fit           int
	Hidden        bool
}

// withItem fills the kit's struct for one call; its memory is freed after.
func withItem(it *CanvasItem, fn func(*C.BKCanvasItem)) {
	var ci C.BKCanvasItem
	ci.id, ci._type = C.int(it.ID), C.int(it.Type)
	if len(it.Coords) > 0 {
		p := (*C.float)(C.malloc(C.size_t(len(it.Coords)) * 4))
		defer C.free(unsafe.Pointer(p))
		for i, v := range it.Coords {
			unsafe.Slice(p, len(it.Coords))[i] = C.float(v)
		}
		ci.coords, ci.ncoords = p, C.int(len(it.Coords))
	}
	ci.fill = [4]C.float{C.float(it.Fill.R), C.float(it.Fill.G), C.float(it.Fill.B), C.float(it.Fill.A)}
	ci.outline = [4]C.float{C.float(it.Outline.R), C.float(it.Outline.G), C.float(it.Outline.B), C.float(it.Outline.A)}
	ci.width, ci.radius, ci.size = C.float(it.Width), C.float(it.Radius), C.float(it.Size)
	ci.start, ci.extent = C.float(it.Start), C.float(it.Extent)
	ci.anchor, ci.fit, ci.hidden = C.int(it.Anchor), C.int(it.Fit), cbool(it.Hidden)
	withOptCstr(it.Text, func(t *C.char) {
		withOptCstr(it.Src, func(s *C.char) {
			ci.text, ci.src = t, s
			fn(&ci)
		})
	})
}

// SetItem inserts or replaces a canvas item by its ID.
func (n *Node) SetItem(it CanvasItem) {
	withItem(&it, func(ci *C.BKCanvasItem) { C.BK_canvas_set_item(n.h, ci) })
}

// AddItem appends a canvas item.
func (n *Node) AddItem(it CanvasItem) {
	withItem(&it, func(ci *C.BKCanvasItem) { C.BK_canvas_add_item(n.h, ci) })
}

// PutItem overwrites the item at index (index == count appends), for
// immediate-mode hosts that rebuild the list each frame; TruncateItems drops
// the rest. Only items that changed redraw.
func (n *Node) PutItem(index int, it CanvasItem) {
	withItem(&it, func(ci *C.BKCanvasItem) { C.BK_canvas_put_item(n.h, C.int(index), ci) })
}

// TruncateItems keeps the first count items.
func (n *Node) TruncateItems(count int) { C.BK_canvas_truncate(n.h, C.int(count)) }

// DeleteItem deletes an item by ID (id < 0: all).
func (n *Node) DeleteItem(id int) { C.BK_canvas_delete(n.h, C.int(id)) }

// ItemCount is the number of items.
func (n *Node) ItemCount() int { return int(C.BK_canvas_count(n.h)) }

// -- text area ------------------------------------------------------------------
// The text is the "value" string; offsets are bytes.

// TextInsert inserts text at a byte offset.
func (n *Node) TextInsert(offset int, text string) {
	withCstr(text, func(s *C.char) { C.BK_textarea_insert(n.h, C.int(offset), s) })
}

// TextDelete deletes the bytes start..end.
func (n *Node) TextDelete(start, end int) { C.BK_textarea_delete(n.h, C.int(start), C.int(end)) }

// Caret is the caret's byte offset.
func (n *Node) Caret() int { return int(C.BK_textarea_caret(n.h)) }

// SetCaret moves the caret.
func (n *Node) SetCaret(offset int) { C.BK_textarea_set_caret(n.h, C.int(offset)) }

// Selection is the selected byte range; ok false when nothing is selected.
func (n *Node) Selection() (start, end int, ok bool) {
	var a, b C.int
	ok = C.BK_textarea_selection(n.h, &a, &b) != 0
	return int(a), int(b), ok
}

// SetSelection selects a byte range.
func (n *Node) SetSelection(start, end int) {
	C.BK_textarea_set_selection(n.h, C.int(start), C.int(end))
}

// See scrolls a byte offset into view.
func (n *Node) See(offset int) { C.BK_textarea_see(n.h, C.int(offset)) }

// -- tree ----------------------------------------------------------------------

// TreeItem is a row of a "tree" node.
type TreeItem struct{ h *C.BKTreeItem }

// NewTreeItem makes an item with a label and an icon (Icon(...), 0 for none).
func NewTreeItem(label string, icon int) *TreeItem {
	return cstr(label, func(s *C.char) *TreeItem { return &TreeItem{C.BK_tree_item_new(s, C.int(icon))} })
}

// Add adds a child item and returns it.
func (t *TreeItem) Add(child *TreeItem) *TreeItem {
	C.BK_tree_item_add(t.h, child.h)
	return child
}

// Label is the item's label.
func (t *TreeItem) Label() string { return C.GoString(C.BK_tree_item_label(t.h)) }

// SetLabel sets it.
func (t *TreeItem) SetLabel(label string) {
	withCstr(label, func(s *C.char) { C.BK_tree_item_set_label(t.h, s) })
}

// Set sets a number of the item ("open", "selected", ...).
func (t *TreeItem) Set(key string, v float64) { C.BK_tree_item_set(t.h, ckey(key), C.double(v)) }

// Get reads it.
func (t *TreeItem) Get(key string) float64 { return float64(C.BK_tree_item_get(t.h, ckey(key))) }

// SetValues sets the item's column texts.
func (t *TreeItem) SetValues(values []string) {
	list := cstrings(values)
	defer list.free()
	C.BK_tree_item_set_values(t.h, list.ptr(), C.int(len(values)))
}

// SetTreeRoots shows items as a tree node's top level.
func (n *Node) SetTreeRoots(roots []*TreeItem) {
	if len(roots) == 0 {
		C.BK_tree_set_roots(n.h, nil, 0)
		return
	}
	p := (**C.BKTreeItem)(C.malloc(C.size_t(len(roots)) * C.size_t(unsafe.Sizeof((*C.BKTreeItem)(nil)))))
	defer C.free(unsafe.Pointer(p))
	for i, r := range roots {
		unsafe.Slice(p, len(roots))[i] = r.h
	}
	C.BK_tree_set_roots(n.h, p, C.int(len(roots)))
}

// TreeSelected is the selected item of a tree node, nil for none.
func (n *Node) TreeSelected() *TreeItem {
	if h := C.BK_tree_selected(n.h); h != nil {
		return &TreeItem{h}
	}
	return nil
}
