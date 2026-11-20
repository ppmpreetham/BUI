// SPDX-License-Identifier: GPL-2.0-or-later

package bui

import (
	"strings"
	"testing"
)

func TestVersion(t *testing.T) {
	if v := Version(); strings.Count(v, ".") != 2 {
		t.Errorf("version %q", v)
	}
}

func TestPropertiesAndTree(t *testing.T) {
	root := NewNode("column")
	defer root.Free()
	s := NewNode("slider").Set("label", "Size").Set("min", 0).Set("max", 10).Set("value", 2.5)
	d := NewNode("dropdown").Set("items", []string{"A", "B"})
	root.Add(s).Add(d)
	if s.Type() != "slider" || s.Str("label") != "Size" || s.Num("value") != 2.5 {
		t.Errorf("slider: %q %q %v", s.Type(), s.Str("label"), s.Num("value"))
	}
	if got := d.Strs("items"); len(got) != 2 || got[1] != "B" {
		t.Errorf("items %q", got)
	}
	if root.ChildCount() != 2 || root.Child(1) != d || d.Parent() != root {
		t.Errorf("tree")
	}
	if s.SetNum("no_such_key", 1) {
		t.Errorf("an unknown key was taken")
	}
	root.Remove(d)
	d.Free()
	if root.ChildCount() != 1 {
		t.Errorf("remove")
	}
}

func TestEventsHeadless(t *testing.T) {
	root := NewNode("column").Set("padding", 8)
	defer root.Free()
	b := NewNode("button").Set("text", "Go")
	c := NewNode("checkbox").Set("text", "On")
	root.Add(b).Add(c)
	clicks, changes, seen := 0, 0, 0
	b.OnClick(func(e Event) {
		if e.Node == b {
			clicks++
		}
	})
	c.OnChange(func(e Event) { changes++ })
	ctx := NewContext(root)
	defer ctx.Free()
	ctx.OnEvent(func(Event) { seen++ })
	r := NewRaster(200, 120)
	defer r.Free()
	if ctx.Render(r, 200, 120, true) == 0 {
		t.Fatalf("nothing rendered")
	}
	x, y := b.Rect().Center()
	ctx.Click(x, y, 0)
	x, y = c.Rect().Center()
	ctx.Click(x, y, 0)
	if clicks != 1 || changes != 1 || c.Num("value") != 1 || seen < 2 {
		t.Errorf("clicks %d, changes %d, value %v, seen %d", clicks, changes, c.Num("value"), seen)
	}
	if ctx.HitAt(x, y) != c {
		t.Errorf("HitAt")
	}
	// some pixel differs from the first: the frame is drawn
	px := r.Pixels()
	diff := false
	for _, p := range px {
		if p != px[0] {
			diff = true
			break
		}
	}
	if !diff {
		t.Errorf("a blank frame")
	}
}

func TestMenuActionsAndBindings(t *testing.T) {
	root := NewNode("column")
	defer root.Free()
	mb := NewNode("menubutton").Set("text", "File")
	root.Add(mb)
	opened := 0
	mb.SetMenu([]MenuItem{{Text: "Open", Shortcut: "Ctrl O", Action: func() { opened++ }}, {}, {Text: "Quit"}})
	ctx := NewContext(root)
	defer ctx.Free()
	r := NewRaster(300, 200)
	defer r.Free()
	ctx.Render(r, 300, 200, true)
	x, y := mb.Rect().Center()
	ctx.Click(x, y, 0)
	ctx.Render(r, 300, 200, false)
	pops := ctx.Popups()
	if len(pops) != 1 {
		t.Fatalf("%d popups after clicking the menu button", len(pops))
	}
	rows := MenuRows(pops[0])
	if len(rows) < 2 || rows[0] != "Open" {
		t.Fatalf("menu rows %q", rows)
	}
	x, y = MenuRowRect(pops[0], 0).Center()
	ctx.Motion(x, y, 0)
	ctx.Click(x, y, 0)
	if opened != 1 {
		t.Errorf("the Open action ran %d times", opened)
	}

	saved := 0
	ctx.Bind("ctrl+s", func() { saved++ })
	ctx.Key("", "s", Ctrl)
	if saved != 1 {
		t.Errorf("ctrl+s ran %d times", saved)
	}
}

func TestCanvasAndTheme(t *testing.T) {
	cv := NewNode("canvas")
	defer cv.Free()
	cv.AddItem(CanvasItem{ID: 1, Type: ItemRect, Coords: []float32{0, 0, 10, 10}, Fill: Color{1, 0, 0, 1}})
	cv.SetItem(CanvasItem{ID: 2, Type: ItemText, Coords: []float32{5, 5}, Text: "hi", Fill: Color{1, 1, 1, 1}})
	if cv.ItemCount() != 2 {
		t.Errorf("%d items", cv.ItemCount())
	}
	cv.DeleteItem(-1)
	if cv.ItemCount() != 0 {
		t.Errorf("delete all")
	}

	if !SetTheme("light") || ThemeMode() != "light" {
		t.Errorf("light theme")
	}
	if c, ok := ThemeColor("text"); !ok || c.A == 0 {
		t.Errorf("theme text color %v %v", c, ok)
	}
	SetTheme("dark")
	if v, ok := EvalNumber("2*3+1"); !ok || v != 7 {
		t.Errorf("EvalNumber %v %v", v, ok)
	}
	if Icon("FILE_FOLDER") == 0 {
		t.Errorf("no FILE_FOLDER icon")
	}
}

func TestFreeReleasesTheTree(t *testing.T) {
	before := len(nodes)
	root := NewNode("column")
	child := NewNode("button").Set("text", "x")
	root.Add(child)
	root.Child(0) // a wrapper looked up again is the same one
	root.Free()
	if !root.Freed() || !child.Freed() {
		t.Errorf("Free left the root (%v) or its child (%v) alive", root.Freed(), child.Freed())
	}
	if len(nodes) != before {
		t.Errorf("%d wrappers left after Free", len(nodes)-before)
	}
	root.Free() // a second Free does nothing
}

func TestBoundaryChecks(t *testing.T) {
	if ImageFromRGBA(make([]byte, 16), 2, 2, 4) != nil {
		t.Errorf("a stride shorter than a row was taken")
	}
	img := ImageFromRGBA(make([]byte, 2*8+4*2), 2, 2, 16) // a tight last row
	if img == nil {
		t.Fatalf("a buffer ending with a tight last row was refused")
	}
	img.Free()
	n := NewNode("slider")
	defer n.Free()
	n.Set("max", uint8(7)).Set("value", uint16(3))
	if n.Num("value") != 3 {
		t.Errorf("Set with small unsigned types")
	}
}

func TestMoveKeepsTheChild(t *testing.T) {
	root := NewNode("column")
	defer root.Free()
	a, b, c := NewNode("label"), NewNode("label"), NewNode("label")
	root.Add(a).Add(b).Add(c)
	if !root.Move(c, 0) || root.Child(0) != c || root.Child(1) != a || root.Child(2) != b {
		t.Errorf("Move to the front")
	}
	if !root.Move(c, -1) || root.Child(2) != c {
		t.Errorf("Move to the end")
	}
	if NewNode("label").Move(a, 0) {
		t.Errorf("Move of another node's child")
	}
}
