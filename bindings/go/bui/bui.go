// Package bui is BUI for Go: Blender's widgets, layout and interaction rules
// from BUI's C kit, with a Go face.
//
// The kit is retained: a tree of nodes ("column", "button", "slider", ...)
// shown by a Context, drawn into a Raster or a native Window. Nodes are made by
// type name and set by property name:
//
//	root := bui.NewNode("column").Set("padding", 8)
//	size := bui.NewNode("slider").Set("label", "Size").Set("max", 10).Set("value", 2.5)
//	size.OnChange(func(e bui.Event) { fmt.Println(e.Node.Num("value")) })
//	root.Add(size)
//	bui.NewWindow(bui.WindowOptions{Title: "BUI from Go", Width: 320, Height: 120, Root: root})
//	bui.Run()
//
// Package ui builds the same widgets the way MyGo's ui package does, from a
// view function of the app's state.
//
// The kit belongs to one thread, the main one, which the package locks its
// main goroutine to: call it from main and from its callbacks, and from other
// goroutines through RunOnMain or Window.Update.
//
// Build the C kit once before the first build, and after updating BUI:
//
//	go generate github.com/ppmpreetham/BUI/bindings/go/bui
//
// SPDX-License-Identifier: GPL-2.0-or-later
package bui

//go:generate go run ../internal/kitgen

/*
#cgo CFLAGS: -I${SRCDIR}/../.bui/include
#cgo LDFLAGS: -L${SRCDIR}/../.bui/lib -lbui_kit -lbui_gpu -lbui_core
#cgo windows LDFLAGS: -lstdc++ -luser32 -lgdi32 -limm32 -lole32 -loleaut32 -luuid -ldwmapi -lshell32 -lshlwapi -lwinmm -lmfplat -lmfreadwrite -lmfuuid -lpropsys -lversion -lavrt -lksuser
#cgo linux LDFLAGS: -lstdc++ -ldl -lm -lpthread
#cgo darwin LDFLAGS: -lc++ -lobjc -framework Cocoa -framework CoreGraphics -framework AVFoundation -framework CoreMedia -framework CoreVideo -framework AudioToolbox
#include "glue.h"
*/
import "C"

import (
	"runtime"
	"sync"
	"unsafe"
)

func init() {
	// The kit's windows and every callback live on the main thread: keep the
	// main goroutine on it.
	runtime.LockOSThread()
	C.bui_go_set_ref_hooks()
}

// Version is the version of the linked kit, as "MAJOR.MINOR.PATCH".
func Version() string { return C.GoString(C.BK_version_string()) }

// keys interns property names: the kit takes them as C strings, and the same
// few names come back on every call.
var keys = map[string]*C.char{}

func ckey(key string) *C.char {
	if k, ok := keys[key]; ok {
		return k
	}
	k := C.CString(key)
	keys[key] = k
	return k
}

// cstr runs fn with s as a C string and returns what fn returns.
func cstr[T any](s string, fn func(*C.char) T) T {
	c := C.CString(s)
	defer C.free(unsafe.Pointer(c))
	return fn(c)
}

// withCstr runs fn with s as a C string.
func withCstr(s string, fn func(*C.char)) {
	c := C.CString(s)
	defer C.free(unsafe.Pointer(c))
	fn(c)
}

// withOptCstr runs fn with s as a C string, or NULL when s is empty (an
// optional title, a keysym).
func withOptCstr(s string, fn func(*C.char)) {
	if s == "" {
		fn(nil)
		return
	}
	withCstr(s, fn)
}

// -- ids ---------------------------------------------------------------------
//
// The kit holds integers, never Go pointers: a context's host data, a window's
// callback data and the refs of menu items and key bindings are ids into these
// tables.

var (
	nextID   uintptr = 1
	contexts         = map[uintptr]*Context{}
	windows          = map[uintptr]*Window{}
	actions          = map[uintptr]*action{}
	// nodeRefs: the borrowed ref of each wrapped node, released by the kit
	// when it frees the node (whoever frees it), which drops the wrapper.
	nodeRefs = map[uintptr]*C.BKNode{}
)

func newID() uintptr {
	id := nextID
	nextID++
	return id
}

// action is a Go function the kit calls back by its ref (BK_EV_CALL). The kit
// retains and releases its refs; an action is dropped when no one holds it.
type action struct {
	fn   func()
	refs int
}

// newAction returns the ref for fn. The kit's first retain keeps it.
func newAction(fn func()) unsafe.Pointer {
	id := newID()
	actions[id] = &action{fn: fn}
	return C.bui_go_token(C.uintptr_t(id))
}

//export goRefRetain
func goRefRetain(ref C.uintptr_t) {
	if a := actions[uintptr(ref)]; a != nil {
		a.refs++
	}
}

//export goRefRelease
func goRefRelease(ref C.uintptr_t) {
	if h, ok := nodeRefs[uintptr(ref)]; ok {
		delete(nodeRefs, uintptr(ref))
		if n := nodes[h]; n != nil {
			n.h = nil
			delete(nodes, h)
		}
		return
	}
	if a := actions[uintptr(ref)]; a != nil {
		if a.refs--; a.refs <= 0 {
			delete(actions, uintptr(ref))
		}
	}
}

//export goHostEmit
func goHostEmit(id C.uintptr_t, n *C.BKNode, kind C.int, ref unsafe.Pointer, a, b C.double) {
	ctx := contexts[uintptr(id)]
	k := EventKind(kind)
	if k == EventCall && ref != nil {
		if act := actions[uintptr(ref)]; act != nil {
			act.fn()
		}
		if ctx != nil && ctx.onEvent != nil {
			ctx.onEvent(Event{Node: wrap(n), Kind: k, A: float64(a), B: float64(b)})
		}
		return
	}
	e := Event{Node: wrap(n), Kind: k, A: float64(a), B: float64(b)}
	switch k {
	case EventKey, EventKeyRelease:
		if ref != nil {
			e.Text = C.GoString((*C.char)(ref))
		}
	case EventRename:
		if n != nil {
			e.Text = C.GoString(C.BK_get_str(n, ckey("edit")))
		}
	}
	if e.Node != nil {
		if h := e.Node.handlers[k]; h != nil {
			h(e)
		}
	}
	if ctx != nil && ctx.onEvent != nil {
		ctx.onEvent(e)
	}
}

//export goHostRequest
func goHostRequest(id C.uintptr_t) {
	if ctx := contexts[uintptr(id)]; ctx != nil && ctx.onRequest != nil {
		ctx.onRequest()
	}
}

//export goHostSchedule
func goHostSchedule(id C.uintptr_t, ms, timer C.int) {
	// Headless contexts: tooltips and other timers fire at the next Timer call
	// of the host that drives them; nothing to do here.
	_ = id
	_ = ms
	_ = timer
}

// -- running on the main thread ------------------------------------------------

var (
	mainMu    sync.Mutex
	mainQueue []func()
	woken     bool
)

// RunOnMain runs fn on the main thread, soon, from any goroutine: the window
// loop wakes for it. Before the loop runs, fn waits for it.
func RunOnMain(fn func()) {
	mainMu.Lock()
	mainQueue = append(mainQueue, fn)
	mainMu.Unlock()
	C.BK_window_wake()
}

//export goWake
func goWake() { drainMain() }

func drainMain() {
	for {
		mainMu.Lock()
		q := mainQueue
		mainQueue = nil
		mainMu.Unlock()
		if len(q) == 0 {
			return
		}
		for _, fn := range q {
			fn()
		}
	}
}

func wakeInit() {
	if !woken {
		woken = true
		C.bui_go_wake_init()
	}
}
