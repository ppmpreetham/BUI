// SPDX-License-Identifier: GPL-2.0-or-later

package ui

import (
	"fmt"
	"image"
	"image/png"
	"os"
	"strings"

	"github.com/ppmpreetham/BUI/bindings/go/bui"
)

// Tester runs a view without a window, for tests: it clicks and types as a
// user would, on a software raster.
//
//	tt := ui.NewTester(s.view, 360, 260)
//	defer tt.Close()
//	if err := tt.Click("Increment"); err != nil {
//		t.Fatal(err)
//	}
type Tester struct {
	c      *Context
	ctx    *bui.Context
	root   *bui.Node
	raster *bui.Raster
	w, h   int
}

// NewTester builds view into a width x height raster.
func NewTester(view func(c *Context), width, height int) *Tester {
	root := bui.NewNode("frame")
	ctx := bui.NewContext(root)
	t := &Tester{c: newContext(view, ctx, nil), ctx: ctx, root: root,
		raster: bui.NewRaster(width, height), w: width, h: height}
	t.c.build()
	t.render()
	return t
}

// Close frees what the tester made.
func (t *Tester) Close() {
	t.raster.Free()
	t.ctx.Free()
	t.root.Free()
}

// Frame builds the view again and renders.
func (t *Tester) Frame() {
	t.c.build()
	t.render()
}

// settle builds after input, as a window does before its next frame.
func (t *Tester) settle() {
	for i := 0; t.c.dirty && i < 8; i++ {
		t.c.build()
	}
	t.render()
}

func (t *Tester) render() { t.ctx.Render(t.raster, t.w, t.h, false) }

// Context is the view's context.
func (t *Tester) Context() *Context { return t.c }

// Raster is what the view last rendered.
func (t *Tester) Raster() *bui.Raster { return t.raster }

// Click clicks the center of the element showing text, or labeled, s.
func (t *Tester) Click(s string) error {
	sl := t.find(s)
	if sl == nil {
		return fmt.Errorf("ui: no element shows or is labeled %q (texts %q)", s, t.Texts())
	}
	x, y := sl.node.Rect().Center()
	t.ClickAt(float32(x), float32(y))
	return nil
}

// ClickAt clicks a point of the window.
func (t *Tester) ClickAt(x, y float32) {
	t.ctx.Click(int(x), int(y), 0)
	t.settle()
}

// Key presses a key with modifiers.
func (t *Tester) Key(mods Modifiers, key Key) {
	t.ctx.Key("", string(key), int(mods))
	t.settle()
}

// Type types text into the focused input.
func (t *Tester) Type(text string) {
	for _, r := range text {
		t.ctx.Key(string(r), "", 0)
	}
	t.settle()
}

// HasText reports an element whose text contains s.
func (t *Tester) HasText(s string) bool {
	for _, x := range t.Texts() {
		if strings.Contains(x, s) {
			return true
		}
	}
	return false
}

// Texts are the texts the shown elements show, in build order.
func (t *Tester) Texts() []string {
	var out []string
	walk(t.c.root, func(s *slot) bool {
		if s.text != "" && !s.look.hidden {
			out = append(out, s.text)
		}
		return shows(s)
	})
	return out
}

func (t *Tester) find(text string) *slot {
	var found *slot
	walk(t.c.root, func(s *slot) bool {
		if found == nil && !s.look.hidden && (s.text == text || s.label == text) {
			found = s
		}
		return found == nil && shows(s)
	})
	return found
}

// shows reports whether what s holds shows: not when it is hidden, nor in a
// closed panel.
func shows(s *slot) bool {
	return !s.look.hidden && !(s.kind == kindCollapsible && s.node.Num("collapsed") != 0)
}

func walk(s *slot, fn func(*slot) bool) {
	for _, k := range s.kids {
		if fn(k) {
			walk(k, fn)
		}
	}
}

// SavePNG writes what the view last rendered to a PNG file.
func (t *Tester) SavePNG(path string) error {
	w, h := t.raster.Size()
	img := image.NewRGBA(image.Rect(0, 0, w, h))
	for i, p := range t.raster.Pixels() { // 0x00RRGGBB
		img.Pix[i*4], img.Pix[i*4+1], img.Pix[i*4+2], img.Pix[i*4+3] = uint8(p>>16), uint8(p>>8), uint8(p), 255
	}
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	if err := png.Encode(f, img); err != nil {
		f.Close()
		return err
	}
	return f.Close()
}
