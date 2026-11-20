// SPDX-License-Identifier: GPL-2.0-or-later

package bui

/*
#include "glue.h"
*/
import "C"

import (
	"unsafe"
)

// MenuItem is a row of a menu. A zero Text (or "-") is a separator.
type MenuItem struct {
	Text      string
	Icon      int    // Icon("...")
	Shortcut  string // shown at the right, as "Ctrl S"
	Checkable bool   // shows a check box, Checked or not
	Checked   bool
	Disabled  bool
	Action    func()
	Sub       []MenuItem
}

// menuSpecs builds the kit's spec array in C memory; free releases it after
// the kit has copied it.
type menuSpecs struct {
	p    *C.BKMenuSpec
	strs []*C.char
	subs []*C.BKMenuSpec
}

func buildMenu(items []MenuItem) *menuSpecs {
	m := &menuSpecs{}
	m.p = m.build(items)
	return m
}

func (m *menuSpecs) build(items []MenuItem) *C.BKMenuSpec {
	if len(items) == 0 {
		return nil
	}
	p := (*C.BKMenuSpec)(C.calloc(C.size_t(len(items)), C.size_t(unsafe.Sizeof(C.BKMenuSpec{}))))
	m.subs = append(m.subs, p)
	for i, it := range unsafe.Slice(p, len(items)) {
		item := items[i]
		if item.Text != "" {
			it.text = m.str(item.Text)
		}
		it.icon = C.int(item.Icon)
		if item.Shortcut != "" {
			it.shortcut = m.str(item.Shortcut)
		}
		it.checked = -1
		if item.Checkable {
			it.checked = cbool(item.Checked)
		}
		it.enabled = cbool(!item.Disabled)
		if item.Action != nil {
			it.ref = newAction(item.Action)
		}
		it.sub = m.build(item.Sub)
		it.nsub = C.int(len(item.Sub))
		unsafe.Slice(p, len(items))[i] = it
	}
	return p
}

func (m *menuSpecs) str(s string) *C.char {
	c := C.CString(s)
	m.strs = append(m.strs, c)
	return c
}

func (m *menuSpecs) free() {
	for _, s := range m.strs {
		C.free(unsafe.Pointer(s))
	}
	for _, p := range m.subs {
		C.free(unsafe.Pointer(p))
	}
}

// SetMenu sets the items of a "menubutton" (or a "menubar" entry).
func (n *Node) SetMenu(items []MenuItem) {
	m := buildMenu(items)
	defer m.free()
	C.BK_menubutton_set_items(n.h, m.p, C.int(len(items)))
}

// OpenMenu opens a menu owned by a node at (x, y) in window pixels.
func (c *Context) OpenMenu(owner *Node, title string, items []MenuItem, x, y int) {
	m := buildMenu(items)
	defer m.free()
	var o *C.BKNode
	if owner != nil {
		o = owner.h
	}
	withOptCstr(title, func(t *C.char) {
		C.BK_open_menu(c.h, o, m.p, C.int(len(items)), t, C.int(x), C.int(y))
	})
}

// OpenPie opens a pie menu centred at (x, y); x or y -100000 centres it on the pointer.
func (c *Context) OpenPie(title string, items []MenuItem, x, y int) {
	m := buildMenu(items)
	defer m.free()
	withOptCstr(title, func(t *C.char) {
		C.BK_open_pie(c.h, m.p, C.int(len(items)), t, C.int(x), C.int(y))
	})
}
