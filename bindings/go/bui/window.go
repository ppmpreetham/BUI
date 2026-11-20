// SPDX-License-Identifier: GPL-2.0-or-later

package bui

/*
#include "glue.h"
*/
import "C"

import (
	"errors"
	"unsafe"
)

// Content is what a window shows when it is made from a view rather than a
// tree: package ui's View.
type Content interface {
	// AttachContent connects the content to its window, on the main thread,
	// before the window shows.
	AttachContent(w *Window)
}

// WindowOptions describe a native window. Give it a Root tree, a Context, or a
// Content (package ui); with none it shows an empty column.
type WindowOptions struct {
	Title         string
	Width, Height int // logical pixels; 0 is 640 x 480
	Root          *Node
	Context       *Context
	Content       Content
	Hidden        bool // made without showing; Show shows it
	GPU           bool // render on the GPU (Direct3D 11 / OpenGL), falling back to software
	MinWidth      int
	MinHeight     int
}

// Window is a native window (Win32, X11, Wayland, Cocoa) showing a Context.
type Window struct {
	h       *C.BKWindow
	id      uintptr
	ctx     *Context
	ownsCtx bool
	timers  map[int]func()
	onFrame func()
	onClose func() bool
	onFocus func(active bool)
	// a callback of the window is running: Free waits for it to return
	dispatching int
	freeLater   bool
}

// ErrNoWindows is returned where the platform has no window host (or, on
// macOS, off the main thread).
var ErrNoWindows = errors.New("bui: no native windows on this platform")

// WindowSupported reports whether native windows work here.
func WindowSupported() bool { return C.BK_window_supported() != 0 }

// NewWindow makes a window and shows it unless Hidden. It returns nil when
// there are no native windows (WindowSupported).
func NewWindow(o WindowOptions) *Window {
	if !WindowSupported() {
		return nil
	}
	ctx, owns := o.Context, false
	var made *Node // a root made here, freed with the window
	if ctx == nil {
		root := o.Root
		if root == nil {
			typ := "column"
			if o.Content != nil {
				typ = "frame"
			}
			root = NewNode(typ)
			made = root
		}
		ctx, owns = NewContext(root), true
	}
	if o.Width <= 0 || o.Height <= 0 {
		o.Width, o.Height = 640, 480
	}
	h := cstr(o.Title, func(t *C.char) *C.BKWindow {
		return C.BK_window_new(ctx.h, t, C.int(o.Width), C.int(o.Height))
	})
	if h == nil {
		if owns {
			ctx.Free()
		}
		made.Free()
		return nil
	}
	w := &Window{h: h, id: newID(), ctx: ctx, ownsCtx: owns}
	windows[w.id] = w
	C.bui_go_window_callbacks(h, C.uintptr_t(w.id))
	if o.GPU {
		C.BK_window_set_gpu(h, 1)
	}
	if o.MinWidth > 0 || o.MinHeight > 0 {
		w.WM("minsize", float64(o.MinWidth), float64(o.MinHeight))
	}
	if o.Content != nil {
		o.Content.AttachContent(w)
	}
	if !o.Hidden {
		w.Show()
	}
	return w
}

// Context is the context the window shows.
func (w *Window) Context() *Context { return w.ctx }

// Show shows the window.
func (w *Window) Show() { C.BK_window_show(w.h) }

// Close closes the window (OnClose is not asked).
func (w *Window) Close() { C.BK_window_close(w.h) }

// IsOpen reports the window still open.
func (w *Window) IsOpen() bool { return w.h != nil && C.BK_window_is_open(w.h) != 0 }

// Free frees a closed window, and its context and tree when NewWindow made
// them. Called from one of the window's own callbacks, it frees the window
// once that callback has returned.
func (w *Window) Free() {
	if w.h == nil {
		return
	}
	if w.dispatching > 0 {
		w.freeLater = true
		return
	}
	delete(windows, w.id)
	C.BK_window_free(w.h)
	w.h = nil
	if w.ownsCtx {
		root := w.ctx.Root()
		w.ctx.Free()
		root.Free()
	}
}

// SetTitle sets the title.
func (w *Window) SetTitle(title string) {
	withCstr(title, func(t *C.char) { C.BK_window_set_title(w.h, t) })
}

// SetSize sets the size in logical pixels.
func (w *Window) SetSize(width, height int) { C.BK_window_set_size(w.h, C.int(width), C.int(height)) }

// Size is the size in logical pixels, as SetSize takes it (the kit reports
// device pixels).
func (w *Window) Size() (width, height int) {
	var a, b C.int
	C.BK_window_get_size(w.h, &a, &b)
	scale, _, ok := w.WMGet("scale")
	if !ok || scale <= 0 {
		scale = 1
	}
	return int(float64(a)/scale + 0.5), int(float64(b)/scale + 0.5)
}

// UseGPU moves the window to the GPU renderer (Direct3D 11, OpenGL); it
// stays in software where the GPU cannot start. A window does not go back.
func (w *Window) UseGPU() { C.BK_window_set_gpu(w.h, 1) }

// GPU reports frames drawn on the GPU.
func (w *Window) GPU() bool { return C.BK_window_gpu(w.h) != 0 }

// Frames is the number of frames presented.
func (w *Window) Frames() int { return int(C.BK_window_frames(w.h)) }

// RenderNow renders and presents at once.
func (w *Window) RenderNow() { C.BK_window_render_now(w.h) }

// Handle is the native handle: HWND, X11 Window or NSWindow*.
func (w *Window) Handle() unsafe.Pointer { return C.BK_window_handle(w.h) }

// WM sends a window-manager request (Tk's wm): "pos" (x, y), "minsize",
// "maxsize", "resizable" (w, h), "topmost", "alpha", "override" (a), "state"
// (0 normal, 1 iconic, 2 withdrawn, 3 zoomed), "lift", "lower", "focus",
// "transparent".
func (w *Window) WM(key string, a, b float64) {
	C.BK_window_wm(w.h, ckey(key), C.double(a), C.double(b))
}

// WMGet queries the window manager: "pos", "state", "screen", "pointer",
// "scale".
func (w *Window) WMGet(key string) (a, b float64, ok bool) {
	var x, y C.double
	ok = C.BK_window_wm_get(w.h, ckey(key), &x, &y) != 0
	return float64(x), float64(y), ok
}

// Timer runs fn once after ms milliseconds; it returns the timer's id, or -1
// when the window can hold no more timers.
func (w *Window) Timer(ms int, fn func()) int {
	id := int(C.BK_window_timer(w.h, C.int(ms)))
	if id < 0 {
		return -1
	}
	if w.timers == nil {
		w.timers = map[int]func(){}
	}
	w.timers[id] = fn
	return id
}

// CancelTimer cancels a timer.
func (w *Window) CancelTimer(id int) {
	C.BK_window_cancel_timer(w.h, C.int(id))
	delete(w.timers, id)
}

// RequestFrame asks for a frame: OnFrame runs right before it renders. With
// vsync, consecutive frames follow the display.
func (w *Window) RequestFrame(vsync bool) { C.BK_window_request_frame(w.h, cbool(vsync)) }

// OnFrame runs before each frame RequestFrame asked for: the place to update
// the tree for exactly the frame that shows it.
func (w *Window) OnFrame(fn func()) { w.onFrame = fn }

// OnClose asks whether the window may close; false keeps it open.
func (w *Window) OnClose(fn func() bool) { w.onClose = fn }

// OnFocus sees the window gain and lose the keyboard focus.
func (w *Window) OnFocus(fn func(active bool)) { w.onFocus = fn }

// Invalidate asks for a frame, from the main thread.
func (w *Window) Invalidate() { w.RequestFrame(false) }

// Update runs fn on the main thread, from any goroutine, then draws a frame:
// the way to change what a window shows from other goroutines.
func (w *Window) Update(fn func()) {
	RunOnMain(func() {
		fn()
		if w.IsOpen() {
			w.Invalidate()
		}
	})
}

// dispatch runs one of the window's callbacks; a Free asked for inside it
// happens once it has returned, when the kit no longer uses the window.
func (w *Window) dispatch(fn func()) {
	w.dispatching++
	fn()
	w.dispatching--
	if w.dispatching == 0 && w.freeLater {
		w.freeLater = false
		w.Free()
	}
}

//export goWindowTimer
func goWindowTimer(id C.uintptr_t, timer C.int) {
	if w := windows[uintptr(id)]; w != nil {
		if fn := w.timers[int(timer)]; fn != nil {
			delete(w.timers, int(timer))
			w.dispatch(fn)
		}
	}
}

//export goWindowClose
func goWindowClose(id C.uintptr_t) C.int {
	keep := false
	if w := windows[uintptr(id)]; w != nil && w.onClose != nil {
		w.dispatch(func() { keep = !w.onClose() })
	}
	return cbool(!keep)
}

//export goWindowFrame
func goWindowFrame(id C.uintptr_t) {
	if w := windows[uintptr(id)]; w != nil && w.onFrame != nil {
		w.dispatch(w.onFrame)
	}
}

//export goWindowFocus
func goWindowFocus(id C.uintptr_t, active C.int) {
	if w := windows[uintptr(id)]; w != nil && w.onFocus != nil {
		w.dispatch(func() { w.onFocus(active != 0) })
	}
}

// -- the loop ------------------------------------------------------------------------

// Run handles input and draws until every window is closed or Quit is called.
func Run() {
	wakeInit()
	drainMain()
	C.BK_window_run()
}

// Pump handles pending messages without waiting; it returns the open windows.
func Pump() int {
	wakeInit()
	drainMain()
	return int(C.BK_window_pump())
}

// Quit ends Run.
func Quit() { C.BK_window_quit() }

// Application is the app's lifetime, MyGo style: App.WhenReady, then App.Run.
type Application struct{ ready []func() }

// App is the application.
var App = &Application{}

// WhenReady runs fn on the main thread once the app runs: make the first
// windows there.
func (a *Application) WhenReady(fn func()) { a.ready = append(a.ready, fn) }

// Run runs the ready functions and the loop until the last window closes.
func (a *Application) Run() error {
	if !WindowSupported() {
		return ErrNoWindows
	}
	ready := a.ready
	a.ready = nil
	for _, fn := range ready {
		fn()
	}
	Run()
	return nil
}

// Quit ends Run.
func (a *Application) Quit() { Quit() }
