// SPDX-License-Identifier: GPL-2.0-or-later

package ui

import (
	"fmt"
	"math"
	"testing"
)

// edit changes a control's value as the user would (the kit's notifying
// setter), then lets the tester build.
func edit(tt *Tester, label string, v float64) {
	s := tt.find(label)
	s.node.SetNum("set_value", v)
	tt.settle()
}

func TestBoundControls(t *testing.T) {
	var (
		sync    bool
		volume  = 20.0
		plan    = "Free"
		size    = "M"
		changes int
	)
	tt := NewTester(func(c *Context) {
		Column(c).Padding(8).Gap(4).Children(func() {
			if Checkbox(c, &sync, "Sync").Changed() {
				changes++
			}
			Slider(c, &volume, 0, 100).Label("Volume")
			Select(c, &plan, []string{"Free", "Pro", "Team"}).Label("Plan")
			RadioGroup(c, func() {
				for _, v := range []string{"S", "M", "L"} {
					Radio(c, &size, v, v)
				}
			})
		})
	}, 300, 300)
	defer tt.Close()

	if err := tt.Click("Sync"); err != nil {
		t.Fatal(err)
	}
	if !sync || changes != 1 {
		t.Fatalf("sync %v after a click, %d changes", sync, changes)
	}
	if got := tt.find("Sync").node.Str("label"); got != "Sync" {
		t.Errorf("the check box shows %q", got)
	}
	edit(tt, "Volume", 75)
	if volume != 75 {
		t.Errorf("volume %v after the user set 75", volume)
	}
	edit(tt, "Plan", 2)
	if plan != "Team" {
		t.Errorf("plan %q after choosing the third option", plan)
	}
	if err := tt.Click("L"); err != nil {
		t.Fatal(err)
	}
	if size != "L" {
		t.Errorf("size %q after clicking L", size)
	}
	if tt.find("M").node.Num("value") != 0 || tt.find("L").node.Num("value") != 1 {
		t.Errorf("the radios show M %v, L %v", tt.find("M").node.Num("value"), tt.find("L").node.Num("value"))
	}

	// the app changes the values: the controls follow
	sync, volume, plan = false, 10, "Pro"
	tt.Frame()
	if tt.find("Sync").node.Num("value") != 0 || tt.find("Volume").node.Num("value") != 10 ||
		tt.find("Plan").node.Num("value") != 1 {
		t.Errorf("controls did not follow the app's values")
	}
	if changes != 1 {
		t.Errorf("a value the app set reported Changed (%d)", changes)
	}
}

func TestTextInput(t *testing.T) {
	var name string
	submitted := 0
	tt := NewTester(func(c *Context) {
		Column(c).Padding(8).Children(func() {
			if TextInput(c, &name).Label("Name").Submitted() {
				submitted++
			}
			Textf(c, "Hello, %s", name)
		})
	}, 300, 120)
	defer tt.Close()

	if err := tt.Click("Name"); err != nil {
		t.Fatal(err)
	}
	tt.Type("Ada")
	if name != "Ada" || !tt.HasText("Hello, Ada") {
		t.Errorf("name %q after typing, texts %q", name, tt.Texts())
	}
	tt.Key(0, KeyEnter)
	if submitted != 1 {
		t.Errorf("%d submits after Return", submitted)
	}
	name = "Grace"
	tt.Frame()
	if got := tt.find("Name").node.Str("value"); got != "Grace" {
		t.Errorf("the input shows %q after the app set Grace", got)
	}
}

func TestKeysKeepNodes(t *testing.T) {
	items := []string{"a", "b", "c"}
	tt := NewTester(func(c *Context) {
		Column(c).Children(func() {
			for _, it := range items {
				Button(c.Key(it), it)
			}
		})
	}, 200, 200)
	defer tt.Close()

	col := tt.c.root.kids[0]
	b := tt.find("b").node
	items = []string{"c", "b"} // a goes, c moves first
	tt.Frame()
	if tt.find("b").node != b {
		t.Errorf("the keyed b got a new node")
	}
	if tt.find("a") != nil {
		t.Errorf("a is still there")
	}
	if n := col.node.ChildCount(); n != 2 {
		t.Errorf("the column holds %d nodes, want 2", n)
	}
	if col.node.Child(0) != tt.find("c").node || col.node.Child(1) != b {
		t.Errorf("the nodes are not in the view's order")
	}
}

func TestClickedOnceAndActions(t *testing.T) {
	clicked, acted := 0, 0
	tt := NewTester(func(c *Context) {
		if Button(c, "Go").OnClick(func() { acted++ }).Clicked() {
			clicked++
		}
	}, 200, 100)
	defer tt.Close()
	if err := tt.Click("Go"); err != nil {
		t.Fatal(err)
	}
	tt.Frame()
	if clicked != 1 || acted != 1 {
		t.Errorf("Clicked %d, OnClick %d times after one click", clicked, acted)
	}
}

func TestLayout(t *testing.T) {
	tt := NewTester(func(c *Context) {
		Column(c).Fill().Center().Children(func() {
			Button(c, "Middle")
		})
		Row(c).Children(func() {}) // empty: nothing to lay out
	}, 400, 300)
	defer tt.Close()
	r := tt.find("Middle").node.Rect()
	cx, cy := r.Center()
	if cx < 190 || cx > 210 || cy < 140 || cy > 160 {
		t.Errorf("a centered button sits at %v", r)
	}

	tt2 := NewTester(func(c *Context) {
		Row(c).Gap(10).Width(300).Children(func() {
			Button(c, "Left")
			Spacer(c)
			Button(c, "Right").Width(60)
		})
	}, 400, 100)
	defer tt2.Close()
	l, rt := tt2.find("Left").node.Rect(), tt2.find("Right").node.Rect()
	if rt.W != 60 || rt.X+rt.W != 300 || l.X != 0 {
		t.Errorf("row: left %v, right %v", l, rt)
	}
}

func TestHiddenAndCollapsible(t *testing.T) {
	open, show := true, true
	tt := NewTester(func(c *Context) {
		Collapsible(c, "Details", &open, func() {
			Text(c, "Inside")
		})
		Text(c, "Maybe").Hidden(!show)
	}, 300, 200)
	defer tt.Close()
	if !tt.HasText("Inside") || !tt.HasText("Maybe") {
		t.Fatalf("texts %q", tt.Texts())
	}
	show, open = false, false
	tt.Frame()
	if tt.HasText("Maybe") {
		t.Errorf("a hidden text still shows")
	}
	if tt.find("Details").node.Num("collapsed") != 1 {
		t.Errorf("the panel did not close")
	}
	if n := tt.find("Details").node.ChildCount(); n != 1 {
		t.Errorf("the panel has %d children: its content went to the header", n)
	}
}

func TestHex(t *testing.T) {
	if Hex("#336699") != RGB(0x33, 0x66, 0x99) || Hex("#fff8") != (Color{255, 255, 255, 0x88}) {
		t.Errorf("Hex")
	}
	if comboName(Ctrl|Shift, KeyS) != "ctrl+shift+s" {
		t.Errorf("comboName %q", comboName(Ctrl|Shift, KeyS))
	}
}

func TestOddKeys(t *testing.T) {
	type holder struct{ V any }
	keys := []any{[]int{1}, holder{[]int{2}}, float32(math.NaN()), "[1]"}
	tt := NewTester(func(c *Context) {
		Column(c).Children(func() {
			for i, k := range keys {
				Button(c.Key(k), fmt.Sprint("b", i))
			}
		})
	}, 200, 200)
	defer tt.Close()
	if n := tt.c.root.kids[0].node.ChildCount(); n != 4 {
		t.Fatalf("%d buttons for 4 distinct keys", n)
	}
	keys = keys[:1]
	tt.Frame()
	if n := tt.c.root.kids[0].node.ChildCount(); n != 1 {
		t.Errorf("%d buttons left, want 1", n)
	}
}

func TestStepSliderStaysInRange(t *testing.T) {
	v := 0.0
	tt := NewTester(func(c *Context) { StepSlider(c, &v, 0, 10, 6).Label("Q") }, 200, 60)
	defer tt.Close()
	edit(tt, "Q", 10)
	if v != 6 {
		t.Errorf("value %v after 10 with steps of 6 up to 10, want 6", v)
	}
}

func TestLateKeyKeepsSiblings(t *testing.T) {
	tt := NewTester(func(c *Context) {
		r := Row(c).Gap(8)
		Button(c, "Sibling")
		r.Key("row")
	}, 200, 100)
	defer tt.Close()
	tt.Frame()
	tt.Frame()
	if tt.find("Sibling") == nil || len(tt.c.root.kids) != 2 {
		t.Errorf("a late Key replaced a sibling: texts %q", tt.Texts())
	}
	if tt.c.root.kids[0].lay.gap != 8 {
		t.Errorf("the keyed row lost its gap")
	}
}

func TestShortcutsOnlyWhileAsked(t *testing.T) {
	ask, hits := true, 0
	tt := NewTester(func(c *Context) {
		if ask && c.Shortcut(0, KeyUp) {
			hits++
		}
	}, 100, 100)
	defer tt.Close()
	ask = false
	tt.Frame()
	tt.Key(0, KeyUp) // nobody asks now
	ask = true
	tt.Frame()
	if hits != 0 {
		t.Errorf("a press while no view asked fired later")
	}
	for i := 0; i < 6; i++ { // more presses than one build's passes
		tt.ctx.Key("", string(KeyUp), 0)
	}
	tt.settle()
	if hits != 6 {
		t.Errorf("%d of 6 presses taken", hits)
	}
}

func TestClosedPanelHidesItsTexts(t *testing.T) {
	open := false
	tt := NewTester(func(c *Context) {
		Collapsible(c, "More", &open, func() { Text(c, "Secret") })
	}, 200, 100)
	defer tt.Close()
	if tt.HasText("Secret") || tt.Click("Secret") == nil {
		t.Errorf("a closed panel's text is found")
	}
}
