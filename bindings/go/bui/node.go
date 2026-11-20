// SPDX-License-Identifier: GPL-2.0-or-later

package bui

/*
#include "glue.h"
*/
import "C"

import (
	"fmt"
	"unsafe"
)

// EventKind is what happened to a node (the kit's BK_EV_*).
type EventKind int

// Event kinds, delivered to a node's handlers and to Context.OnEvent.
const (
	EventChange     EventKind = C.BK_EV_CHANGE   // the value changed
	EventClick      EventKind = C.BK_EV_CLICK    // a button was pressed
	EventCall       EventKind = C.BK_EV_CALL     // a menu item or a key binding ran
	EventRename     EventKind = C.BK_EV_RENAME   // a list or tree row was renamed: A = index, Text = name
	EventActivate   EventKind = C.BK_EV_ACTIVATE // a grid view item was opened: A = index
	EventToggle     EventKind = C.BK_EV_TOGGLE   // a tree toggle column: A = column, B = value
	EventBrowse     EventKind = C.BK_EV_BROWSE   // a path field's browse button
	EventPress      EventKind = C.BK_EV_PRESS    // raw events of nodes with "events": A, B = position
	EventRelease    EventKind = C.BK_EV_RELEASE
	EventDouble     EventKind = C.BK_EV_DOUBLE
	EventMotion     EventKind = C.BK_EV_MOTION
	EventEnter      EventKind = C.BK_EV_ENTER
	EventLeave      EventKind = C.BK_EV_LEAVE
	EventKey        EventKind = C.BK_EV_KEY        // Text = "text\tkeysym", A = modifiers
	EventWheel      EventKind = C.BK_EV_WHEEL      // A = delta
	EventSubmit     EventKind = C.BK_EV_SUBMIT     // a text field was confirmed with Return
	EventView       EventKind = C.BK_EV_VIEW       // a scrolled view moved ("viewevents")
	EventScroll     EventKind = C.BK_EV_SCROLL     // a scrollbar request
	EventConfigure  EventKind = C.BK_EV_CONFIGURE  // the geometry changed: A, B = width, height
	EventKeyRelease EventKind = C.BK_EV_KEYRELEASE // Text = "\tkeysym", A = modifiers
	eventKindCount            = int(EventKeyRelease) + 1
)

// Event is a callback from the kit.
type Event struct {
	Node *Node
	Kind EventKind
	A, B float64
	Text string
}

// Modifier and button bits of input calls and key events.
const (
	Shift   = C.BK_SHIFT
	Ctrl    = C.BK_CTRL
	Alt     = C.BK_ALT
	Button1 = C.BK_BUTTON1
	Button2 = C.BK_BUTTON2
	Button3 = C.BK_BUTTON3
)

// Rect is a node's box in window pixels.
type Rect struct{ X, Y, W, H int }

// Center is the middle of the box.
func (r Rect) Center() (x, y int) { return r.X + r.W/2, r.Y + r.H/2 }

// Color is RGBA, each 0..1.
type Color struct{ R, G, B, A float32 }

// Node is a node of the kit's tree. Nodes made with NewNode are yours: free a
// root with Free; children go with their parent.
type Node struct {
	h        *C.BKNode
	handlers [eventKindCount]func(Event)
}

// nodes maps the kit's nodes to their Go values, for events and lookups.
var nodes = map[*C.BKNode]*Node{}

// wrap returns the Go value of a kit node, making one for nodes the kit built
// (a panel's body, a popup). The node gets a borrowed ref, which the kit
// releases as it frees the node, whoever frees it: that drops the value, so a
// later node at the same address never finds a stale one.
func wrap(h *C.BKNode) *Node {
	if h == nil {
		return nil
	}
	if n := nodes[h]; n != nil {
		return n
	}
	n := &Node{h: h}
	nodes[h] = n
	id := newID()
	nodeRefs[id] = h
	C.BK_node_set_borrowed_ref(h, C.bui_go_token(C.uintptr_t(id)))
	return n
}

// NewNode makes a node of a type: "column", "row", "grid", "box", "frame",
// "panel", "tabs", "scroll", "label", "separator", "progress", "button",
// "toggle", "checkbox", "radio", "text", "search", "number", "slider",
// "dropdown", "enum", "swatch", "menubutton", "menubar", "list", "tree",
// "gridview", "canvas", "textarea", "image", "video", "ramp", "curve",
// "histogram", "custom", "filebrowser", and the others BK_node_new knows. It
// panics for an unknown type.
func NewNode(typ string) *Node {
	h := cstr(typ, func(s *C.char) *C.BKNode { return C.BK_node_new(s) })
	if h == nil {
		panic(fmt.Sprintf("bui: unknown node type %q", typ))
	}
	return wrap(h)
}

// Free frees the node and everything under it; it leaves its parent first.
// The Go values of the freed nodes let go of them as the kit releases their
// refs, so a second Free does nothing.
func (n *Node) Free() {
	if n == nil || n.h == nil {
		return
	}
	C.BK_node_free(n.h)
}

// Freed reports a node the kit has freed.
func (n *Node) Freed() bool { return n == nil || n.h == nil }

// Type is the node's type name.
func (n *Node) Type() string { return C.GoString(C.BK_node_type(n.h)) }

// Add appends child and returns n, for chaining.
func (n *Node) Add(child *Node) *Node {
	C.BK_node_add(n.h, child.h, -1)
	return n
}

// Insert puts child at index among n's children.
func (n *Node) Insert(child *Node, index int) {
	C.BK_node_add(n.h, child.h, C.int(index))
}

// Remove takes child out of n without freeing it.
func (n *Node) Remove(child *Node) { C.BK_node_remove(n.h, child.h) }

// Move moves one of n's children to index (-1: last) without taking it out,
// so it keeps the keyboard focus and a text edit; false when child is not n's.
func (n *Node) Move(child *Node, index int) bool {
	return C.BK_node_move(n.h, child.h, C.int(index)) != 0
}

// ChildCount is the number of children.
func (n *Node) ChildCount() int { return int(C.BK_node_child_count(n.h)) }

// Child is the i-th child.
func (n *Node) Child(i int) *Node { return wrap(C.BK_node_child(n.h, C.int(i))) }

// Parent is the node's parent, nil for a root.
func (n *Node) Parent() *Node { return wrap(C.BK_node_parent(n.h)) }

// Rect is the node's box from the last layout, in window pixels.
func (n *Node) Rect() Rect {
	var r [4]C.int
	C.BK_node_rect(n.h, &r[0])
	return Rect{int(r[0]), int(r[1]), int(r[2]), int(r[3])}
}

// Natural is the size the node asks for.
func (n *Node) Natural() (w, h int) {
	var s [2]C.int
	C.BK_node_natural(n.h, &s[0])
	return int(s[0]), int(s[1])
}

// Invalidate redraws the node.
func (n *Node) Invalidate() { C.BK_node_invalidate(n.h) }

// Relayout lays the node out again (its size or children changed).
func (n *Node) Relayout() { C.BK_node_relayout(n.h) }

// -- properties ----------------------------------------------------------------

// Set sets a property by name and returns n, for chaining. value is a number
// (any integer or float type, or a bool as 0/1), a string, a []string or a
// Color. It panics for other types; a key the node does not know is ignored
// (SetNum and the other setters report it).
func (n *Node) Set(key string, value any) *Node {
	switch v := value.(type) {
	case string:
		n.SetStr(key, v)
	case []string:
		n.SetStrs(key, v)
	case Color:
		n.SetColor(key, v)
	case bool:
		if v {
			n.SetNum(key, 1)
		} else {
			n.SetNum(key, 0)
		}
	case float64:
		n.SetNum(key, v)
	case float32:
		n.SetNum(key, float64(v))
	case int:
		n.SetNum(key, float64(v))
	case int8:
		n.SetNum(key, float64(v))
	case int16:
		n.SetNum(key, float64(v))
	case int32:
		n.SetNum(key, float64(v))
	case int64:
		n.SetNum(key, float64(v))
	case uint:
		n.SetNum(key, float64(v))
	case uint8:
		n.SetNum(key, float64(v))
	case uint16:
		n.SetNum(key, float64(v))
	case uint32:
		n.SetNum(key, float64(v))
	case uint64:
		n.SetNum(key, float64(v))
	case uintptr:
		n.SetNum(key, float64(v))
	default:
		panic(fmt.Sprintf("bui: Set(%q) with a %T", key, value))
	}
	return n
}

// SetNum sets a number property; false when the node has no such key.
func (n *Node) SetNum(key string, v float64) bool {
	return C.BK_set_num(n.h, ckey(key), C.double(v)) != 0
}

// SetStr sets a string property; false when the node has no such key.
func (n *Node) SetStr(key, v string) bool {
	return cstr(v, func(s *C.char) bool { return C.BK_set_str(n.h, ckey(key), s) != 0 })
}

// SetColor sets a color property; false when the node has no such key.
func (n *Node) SetColor(key string, c Color) bool {
	rgba := [4]C.float{C.float(c.R), C.float(c.G), C.float(c.B), C.float(c.A)}
	return C.BK_set_color(n.h, ckey(key), &rgba[0]) != 0
}

// SetStrs sets a list property ("items" of a dropdown, "headers" of a tree).
func (n *Node) SetStrs(key string, values []string) bool {
	list := cstrings(values)
	defer list.free()
	return C.BK_set_strs(n.h, ckey(key), list.ptr(), C.int(len(values))) != 0
}

// Num reads a number property (0 when unknown).
func (n *Node) Num(key string) float64 { return float64(C.BK_get_num(n.h, ckey(key))) }

// Str reads a string property ("" when unknown).
func (n *Node) Str(key string) string { return C.GoString(C.BK_get_str(n.h, ckey(key))) }

// Color reads a color property.
func (n *Node) Color(key string) (Color, bool) {
	var c [4]C.float
	ok := C.BK_get_color(n.h, ckey(key), &c[0]) != 0
	return Color{float32(c[0]), float32(c[1]), float32(c[2]), float32(c[3])}, ok
}

// Strs reads a list property.
func (n *Node) Strs(key string) []string {
	k := ckey(key)
	out := make([]string, int(C.BK_strs_count(n.h, k)))
	for i := range out {
		out[i] = C.GoString(C.BK_strs_get(n.h, k, C.int(i)))
	}
	return out
}

// LinkFields links number fields edited together with Alt (a vector).
func LinkFields(fields ...*Node) {
	if len(fields) == 0 {
		return
	}
	hs := (**C.BKNode)(C.malloc(C.size_t(len(fields)) * C.size_t(unsafe.Sizeof((*C.BKNode)(nil)))))
	defer C.free(unsafe.Pointer(hs))
	s := unsafe.Slice(hs, len(fields))
	for i, f := range fields {
		s[i] = f.h
	}
	C.BK_link_fields(hs, C.int(len(fields)))
}

// -- events --------------------------------------------------------------------

// On sets the node's handler for a kind of event, replacing the last one; nil
// removes it.
func (n *Node) On(kind EventKind, fn func(Event)) *Node {
	n.handlers[kind] = fn
	return n
}

// OnClick handles button presses.
func (n *Node) OnClick(fn func(Event)) *Node { return n.On(EventClick, fn) }

// OnChange handles value changes.
func (n *Node) OnChange(fn func(Event)) *Node { return n.On(EventChange, fn) }

// OnSubmit handles Return in a text field (which also needs "events" set).
func (n *Node) OnSubmit(fn func(Event)) *Node { return n.On(EventSubmit, fn) }

// -- geometry managers (children of a "frame") ------------------------------------

// Geometry managers, Tk's: pack, grid and place.
const (
	GeomNone  = C.BK_GEOM_NONE
	GeomPack  = C.BK_GEOM_PACK
	GeomGrid  = C.BK_GEOM_GRID
	GeomPlace = C.BK_GEOM_PLACE
)

// Anchor bits for "anchor" and "sticky".
const (
	AnchorN = C.BK_ANCHOR_N
	AnchorS = C.BK_ANCHOR_S
	AnchorW = C.BK_ANCHOR_W
	AnchorE = C.BK_ANCHOR_E
)

// Pack manages the node with pack and sets one of its options: side (0 top,
// 1 bottom, 2 left, 3 right), fill (1 x, 2 y, 3 both), expand, anchor, padx0,
// padx1, pady0, pady1, ipadx, ipady, reqwidth, reqheight.
func (n *Node) Pack(key string, v float64) *Node { return n.geom(GeomPack, key, v) }

// Grid manages the node with grid: row, column, rowspan, columnspan, sticky,
// and the pads.
func (n *Node) Grid(key string, v float64) *Node { return n.geom(GeomGrid, key, v) }

// Place manages the node with place: x, y, relx, rely, width, height,
// relwidth, relheight, anchor.
func (n *Node) Place(key string, v float64) *Node { return n.geom(GeomPlace, key, v) }

func (n *Node) geom(manager int, key string, v float64) *Node {
	if int(C.BK_geom_manager(n.h)) != manager {
		C.BK_geom_set_manager(n.h, C.int(manager))
	}
	C.BK_geom_set(n.h, ckey(key), C.double(v))
	return n
}

// GeomManager is the node's manager (Geom*).
func (n *Node) GeomManager() int { return int(C.BK_geom_manager(n.h)) }

// Geom reads a geometry option.
func (n *Node) Geom(key string) float64 { return float64(C.BK_geom_get(n.h, ckey(key))) }

// GridWeight sets how a frame's grid shares space: a column (or a row, with
// columnAxis false) takes weight of the extra space and at least minSize.
func (n *Node) GridWeight(columnAxis bool, index int, weight float64, minSize int) {
	axis := 0
	if columnAxis {
		axis = 1
	}
	C.BK_frame_grid_weight(n.h, C.int(axis), C.int(index), C.double(weight), C.int(minSize))
}

// -- C string lists ------------------------------------------------------------

type cstringList struct {
	p **C.char
	n int
}

func cstrings(values []string) cstringList {
	if len(values) == 0 {
		return cstringList{}
	}
	p := (**C.char)(C.malloc(C.size_t(len(values)) * C.size_t(unsafe.Sizeof((*C.char)(nil)))))
	s := unsafe.Slice(p, len(values))
	for i, v := range values {
		s[i] = C.CString(v)
	}
	return cstringList{p, len(values)}
}

func (l cstringList) ptr() **C.char { return l.p }

func (l cstringList) free() {
	if l.p == nil {
		return
	}
	for _, s := range unsafe.Slice(l.p, l.n) {
		C.free(unsafe.Pointer(s))
	}
	C.free(unsafe.Pointer(l.p))
}
