// SPDX-License-Identifier: GPL-2.0-or-later

package ui

import (
	"fmt"
	"math"
	"slices"
	"strconv"
	"strings"

	"github.com/ppmpreetham/BUI/bindings/go/bui"
)

// -- containers -----------------------------------------------------------------------

// Column stacks its children from top to bottom and stretches them to its
// width.
func Column(c *Context) Element { return c.elem(c.child(kindColumn, "frame")) }

// Box is a column.
func Box(c *Context) Element { return Column(c) }

// Row places its children from left to right, centered vertically.
func Row(c *Context) Element { return c.elem(c.child(kindRow, "frame")) }

// Scroll is a column that scrolls when its children are taller than it.
func Scroll(c *Context) Element { return c.elem(c.child(kindScroll, "scroll")) }

// Collapsible is one of Blender's panels: a header with label that opens and
// closes *open, over the children fn builds.
func Collapsible(c *Context, label string, open *bool, fn func()) Element {
	s := c.child(kindCollapsible, "panel")
	s.text = label
	s.setStr("text", label)
	// the kit's "collapsed" is the opposite of open
	c.bindNum(s, "collapsed", b2f(!*open), func(v float64) { *open = v == 0 })
	return c.elem(s).Children(fn)
}

// RadioGroup groups radio buttons in a column.
func RadioGroup(c *Context, fn func()) Element {
	return c.elem(c.child(kindRadioGroup, "frame")).Children(fn)
}

// Spacer takes the free space of its row or column.
func Spacer(c *Context) Element { return c.elem(c.child(kindSpacer, "frame")) }

// Divider draws a line across its column, or down its row.
func Divider(c *Context) Element {
	s := c.child(kindDivider, "separator")
	s.setNum("line", 1)
	return c.elem(s)
}

// -- text -------------------------------------------------------------------------------

// Text shows a line of text.
func Text(c *Context, text string) Element {
	s := c.child(kindText, "label")
	s.text = text
	s.setStr("text", text)
	return c.elem(s)
}

// Textf shows formatted text, as fmt.Sprintf.
func Textf(c *Context, format string, args ...any) Element {
	return Text(c, fmt.Sprintf(format, args...))
}

// -- buttons ---------------------------------------------------------------------------------

// Button is a push button; Clicked reports a click.
func Button(c *Context, label string) Element { return button(c, label, false) }

// PrimaryButton is the button of a window's main action, in the theme's
// accent (Blender's default button).
func PrimaryButton(c *Context, label string) Element { return button(c, label, true) }

func button(c *Context, label string, primary bool) Element {
	s := c.child(kindButton, "button")
	s.text = label
	s.setStr("text", label)
	s.setNum("active_default", b2f(primary))
	c.takeClicks(s)
	return c.elem(s)
}

// takeClicks moves the clicks since the last build into this pass.
func (c *Context) takeClicks(s *slot) {
	s.clicked = s.clicks > 0
	if s.clicked {
		s.clicks--
		c.consumed = true
	}
	s.submitted = s.submits > 0
	if s.submitted {
		s.submits--
		c.consumed = true
	}
}

// -- controls bound to a value ------------------------------------------------------------------

// bindNum keeps a number property and the app's value in step: a node value
// other than the one last given is the user's edit, which goes to set;
// otherwise want goes to the node.
func (c *Context) bindNum(s *slot, key string, want float64, set func(float64)) {
	s.changed = false
	if s.bound {
		if cur := s.node.Num(key); cur != s.num {
			s.num = cur
			set(cur)
			s.changed = true
			c.consumed = true
			return
		}
	}
	if !s.bound || want != s.num {
		s.node.SetNum(key, want)
		s.num = s.node.Num(key) // as the node took it (clamped)
		s.bound = true
	}
}

func (c *Context) bindStr(s *slot, want string, set func(string)) {
	s.changed = false
	if s.bound {
		if cur := s.node.Str("value"); cur != s.str {
			s.str = cur
			set(cur)
			s.changed = true
			c.consumed = true
			return
		}
	}
	if !s.bound || want != s.str {
		s.node.SetStr("value", want)
		s.str = want
		s.bound = true
	}
}

// Checkbox toggles *checked, with a label.
func Checkbox(c *Context, checked *bool, label string) Element {
	return boolControl(c, kindCheckbox, "checkbox", checked, label)
}

// Toggle is a button held in while *on.
func Toggle(c *Context, on *bool, label string) Element {
	return boolControl(c, kindToggle, "toggle", on, label)
}

// Switch toggles *on; Blender has no switch, so it is a check box.
func Switch(c *Context, on *bool) Element {
	return boolControl(c, kindCheckbox, "checkbox", on, "")
}

func boolControl(c *Context, k kind, typ string, v *bool, label string) Element {
	s := c.child(k, typ)
	s.text = label
	key := "label" // a check box's text is beside it, a toggle's on it
	if k == kindToggle {
		key = "text"
	}
	s.setStr(key, label)
	c.bindNum(s, "value", b2f(*v), func(x float64) { *v = x != 0 })
	return c.elem(s)
}

// Radio is a radio button setting *selected to value when chosen.
func Radio[T comparable](c *Context, selected *T, value T, label string) Element {
	s := c.child(kindRadio, "radio")
	s.text = label
	s.setStr("label", label)
	c.bindNum(s, "value", b2f(*selected == value), func(x float64) {
		if x != 0 {
			*selected = value
		}
	})
	return c.elem(s)
}

// Select chooses one of options into *selected, from Blender's drop-down menu.
func Select(c *Context, selected *string, options []string) Element {
	s := c.child(kindSelect, "dropdown")
	picked := false
	if !slices.Equal(s.options, options) {
		// a choice the user made among the old options that no build has read yet
		if cur := s.node.Num("value"); s.bound && cur != s.num {
			if i := int(cur); i >= 0 && i < len(s.options) {
				*selected, picked = s.options[i], true
			}
		}
		s.options = slices.Clone(options)
		s.node.SetStrs("items", options)
		s.bound = false
	}
	at := -1
	for i, o := range options {
		if o == *selected {
			at = i
			break
		}
	}
	s.text = *selected
	c.bindNum(s, "value", float64(at), func(x float64) {
		if i := int(x); i >= 0 && i < len(options) {
			*selected = options[i]
			s.text = options[i]
		}
	})
	if picked {
		s.changed, c.consumed = true, true
	}
	return c.elem(s)
}

// Slider sets *value between lo and hi by dragging, as Blender's number
// sliders do (Ctrl snaps, Shift is precise, a click types a value).
func Slider(c *Context, value *float64, lo, hi float64) Element {
	return numControl(c, kindSlider, "slider", value, lo, hi, 0)
}

// StepSlider is a slider whose values are lo plus multiples of step.
func StepSlider(c *Context, value *float64, lo, hi, step float64) Element {
	return numControl(c, kindSlider, "slider", value, lo, hi, step)
}

// NumberInput edits *value between lo and hi, by step with its arrows.
func NumberInput(c *Context, value *float64, lo, hi, step float64) Element {
	return numControl(c, kindNumber, "number", value, lo, hi, step)
}

func numControl(c *Context, k kind, typ string, v *float64, lo, hi, step float64) Element {
	s := c.child(k, typ)
	if !s.bound { // the kit's own step and precision, for when none is given
		s.defStep, s.defPrecision = s.node.Num("step"), s.node.Num("precision")
	}
	s.setNum("min", lo)
	s.setNum("max", hi)
	if step > 0 {
		s.setNum("step", step)
		s.setNum("precision", float64(decimals(step)))
	} else {
		s.setNum("step", s.defStep)
		s.setNum("precision", s.defPrecision)
	}
	c.bindNum(s, "value", *v, func(x float64) {
		if step > 0 { // the nearest step inside lo..hi
			last := math.Floor((hi - lo) / step)
			x = lo + step*min(max(math.Round((x-lo)/step), 0), last)
		}
		*v = x
	})
	return c.elem(s)
}

// decimals is how many decimals step shows: 0 for 1, 2 for 0.25.
func decimals(step float64) int {
	s := strconv.FormatFloat(step, 'f', -1, 64)
	if i := strings.IndexByte(s, '.'); i >= 0 {
		return len(s) - i - 1
	}
	return 0
}

// TextInput edits *value on one line; Submitted reports Return.
func TextInput(c *Context, value *string) Element {
	s := c.child(kindTextInput, "text")
	s.setNum("live", 1)
	s.setNum("events", 1)
	c.takeClicks(s)
	c.bindStr(s, *value, func(x string) { *value = x })
	s.text = *value
	return c.elem(s)
}

// TextArea edits *value on several lines.
func TextArea(c *Context, value *string) Element {
	s := c.child(kindTextArea, "textarea")
	c.bindStr(s, *value, func(x string) { *value = x })
	s.text = *value
	return c.elem(s)
}

// Progress shows a value from 0 to 1.
func Progress(c *Context, value float64) Element {
	s := c.child(kindProgress, "progress")
	s.setNum("factor", min(max(value, 0), 1))
	return c.elem(s)
}

// -- images ------------------------------------------------------------------------------------

// Bitmap is a decoded image. Make bitmaps once, not in the view.
type Bitmap struct{ b *bui.ImBuf }

// DecodeBitmap decodes PNG, JPEG, WebP, GIF, BMP, TGA, QOI, HDR or SVG data.
func DecodeBitmap(data []byte) (*Bitmap, error) {
	b, err := bui.DecodeImage(data, 0, 0)
	if err != nil {
		return nil, err
	}
	return &Bitmap{b}, nil
}

// Release frees the bitmap's pixels. Every Image showing it must be gone first
// (not built again, or built with another bitmap).
func (b *Bitmap) Release() {
	if b != nil {
		b.b.Free()
	}
}

// Size is the bitmap's size in pixels.
func (b *Bitmap) Size() (w, h int) {
	w, h, _ = b.b.Size()
	return w, h
}

// Image shows a bitmap, fitted into the element's size.
func Image(c *Context, b *Bitmap) Element {
	s := c.child(kindImage, "image")
	if s.bitmap != b || !s.bound {
		s.bitmap, s.bound = b, true
		var ib *bui.ImBuf
		if b != nil {
			ib = b.b
		}
		s.node.SetImBuf(ib)
	}
	return c.elem(s)
}
