// SPDX-License-Identifier: GPL-2.0-or-later

package ui

import (
	"github.com/ppmpreetham/BUI/bindings/go/bui"
)

type kind uint8

const (
	kindColumn kind = iota
	kindRow
	kindRadioGroup
	kindScroll
	kindCollapsible
	kindSpacer
	kindDivider
	kindText
	kindButton
	kindCheckbox
	kindToggle
	kindRadio
	kindSelect
	kindSlider
	kindNumber
	kindTextInput
	kindTextArea
	kindProgress
	kindImage
	kindCount
)

// slot is what an element keeps from build to build: its node, its children
// and the input that came for it.
type slot struct {
	kind   kind
	node   *bui.Node // the element's node, in its parent's host
	host   *bui.Node // where its children go (frames); nil for controls
	parent *slot
	pass   int // last pass it was built in

	// children
	index  map[ident]*slot
	kids   []*slot
	ord    [kindCount]int
	begun  int
	spaces []*bui.Node // Justify's spacers
	order  []*bui.Node // the host's children in order, reused each pass

	// input since the last build, and what this pass reports
	clicks, submits             int
	clicked, changed, submitted bool
	onClick, onChange, onSubmit func()

	// the bound value as last given to the node: a node value that differs
	// is the user's edit
	num   float64
	str   string
	bound bool

	options      []string // a Select's, as last given
	defStep      float64  // a number control's kit step and precision
	defPrecision float64
	bitmap       *Bitmap
	text, label  string // for Tester lookups
	nums         map[string]float64
	strs         map[string]string

	look, shown look
	lookSet     bool
	lay         layout
	packed      pack
	hosted      hostLook
}

// look is the style an element's modifiers set in one pass; what is not set
// again goes back to the default.
type look struct {
	disabled, hidden, bold bool
	fontSize               float32
	textColor, background  Color
	tooltip                string
}

// layout is how an element sits in its parent and lays out its children.
type layout struct {
	grow              float32
	width, height     float32
	fillW, fillH      bool
	alignSelf         Align
	hasAlignSelf      bool
	gap               float32
	padding           [4]float32 // top, right, bottom, left
	justify, align    Align
	hasJustify, hasAl bool
}

// pack is a child's packer options, as last set.
type pack struct {
	side, fill, expand, anchor int
	padx0, padx1, pady0, pady1 int
	reqw, reqh                 int
	set                        bool
}

type hostLook struct {
	padding int
	bg      Color
	set     bool
}

// Element is an element built in this pass. Its methods style it and ask
// about its input; it belongs to this build pass only.
type Element struct {
	c    *Context
	s    *slot
	pass int
}

func (c *Context) elem(s *slot) Element { return Element{c: c, s: s, pass: c.pass} }

// Valid reports an element built in this pass.
func (e Element) Valid() bool { return e.s != nil && e.c != nil && e.pass == e.c.pass }

// Key keys a container after its constructor, before its children; prefer
// Context.Key before construction.
func (e Element) Key(key any) Element {
	if !e.Valid() || e.s.parent == nil {
		return e
	}
	p := e.s.parent
	for id, s := range p.index {
		if s == e.s {
			delete(p.index, id)
			break
		}
	}
	id := ident{kind: e.s.kind, n: -1, key: keyOf(key)}
	if old := p.index[id]; old != nil && old != e.s && old.pass != e.c.pass {
		// the keyed element of last time takes this one's place, with the look and
		// layout its modifiers set so far
		for i, k := range p.kids {
			if k == e.s {
				p.kids[i] = old
				break
			}
		}
		old.pass, old.look, old.lay = e.c.pass, e.s.look, e.s.lay
		e.c.freeSlot(e.s)
		p.index[id] = old
		return Element{c: e.c, s: old, pass: e.pass}
	}
	p.index[id] = e.s
	return e
}

// -- input ---------------------------------------------------------------------------

// Clicked reports a click since the last build.
func (e Element) Clicked() bool { return e.s != nil && e.s.clicked }

// Changed reports that the user changed the element's value in this pass.
func (e Element) Changed() bool { return e.s != nil && e.s.changed }

// Submitted reports Return in a text input.
func (e Element) Submitted() bool { return e.s != nil && e.s.submitted }

// OnClick runs fn after the view has built, when the element was clicked.
func (e Element) OnClick(fn func()) Element {
	if e.s != nil && e.s.clicked {
		e.c.actions = append(e.c.actions, fn)
	}
	return e
}

// OnChange runs fn after the view has built, when the user changed the value.
func (e Element) OnChange(fn func()) Element {
	if e.s != nil && e.s.changed {
		e.c.actions = append(e.c.actions, fn)
	}
	return e
}

// OnSubmit runs fn after the view has built, when Return was pressed in a text input.
func (e Element) OnSubmit(fn func()) Element {
	if e.s != nil && e.s.submitted {
		e.c.actions = append(e.c.actions, fn)
	}
	return e
}

// -- look ------------------------------------------------------------------------------

// Disabled grays the element out: it takes no input.
func (e Element) Disabled(d bool) Element { return e.with(func(s *slot) { s.look.disabled = d }) }

// Hidden takes the element and its room away, keeping its state.
func (e Element) Hidden(h bool) Element { return e.with(func(s *slot) { s.look.hidden = h }) }

// Bold makes text bold.
func (e Element) Bold() Element { return e.with(func(s *slot) { s.look.bold = true }) }

// FontSize sets the text size in DIPs (Blender's UI text is 11).
func (e Element) FontSize(v float32) Element { return e.with(func(s *slot) { s.look.fontSize = v }) }

// TextColor colors the text.
func (e Element) TextColor(col Color) Element {
	return e.with(func(s *slot) { s.look.textColor = col })
}

// Background fills a row's or a column's box.
func (e Element) Background(col Color) Element {
	return e.with(func(s *slot) { s.look.background = col })
}

// Tooltip shows s while the pointer rests on the element.
func (e Element) Tooltip(t string) Element { return e.with(func(s *slot) { s.look.tooltip = t }) }

// Label names a control: a slider, number input, select or text input shows
// it, as Blender's fields do ("Volume: 50"); a button keeps it for Tester.
func (e Element) Label(l string) Element {
	return e.with(func(s *slot) {
		s.label = l
		switch s.kind {
		case kindSlider, kindNumber, kindSelect, kindTextInput:
			s.setStr("label", l)
		}
	})
}

// Placeholder shows text while a text input is empty.
func (e Element) Placeholder(p string) Element {
	return e.with(func(s *slot) { s.setStr("placeholder", p) })
}

// Password hides what a text input holds.
func (e Element) Password() Element { return e.with(func(s *slot) { s.setStr("mask", "•") }) }

// -- layout ------------------------------------------------------------------------------

// Gap spaces the children apart.
func (e Element) Gap(v float32) Element { return e.with(func(s *slot) { s.lay.gap = v }) }

// Padding pads the children inside the box: one value, two (vertical,
// horizontal) or four (top, right, bottom, left), as in CSS.
func (e Element) Padding(v ...float32) Element {
	return e.with(func(s *slot) {
		switch len(v) {
		case 1:
			s.lay.padding = [4]float32{v[0], v[0], v[0], v[0]}
		case 2:
			s.lay.padding = [4]float32{v[0], v[1], v[0], v[1]}
		case 3:
			s.lay.padding = [4]float32{v[0], v[1], v[2], v[1]}
		case 4:
			s.lay.padding = [4]float32{v[0], v[1], v[2], v[3]}
		}
	})
}

// Grow gives the element the free space along its parent's direction, as
// CSS's flex-grow (any factor above 0 takes an equal share).
func (e Element) Grow(f float32) Element { return e.with(func(s *slot) { s.lay.grow = f }) }

// Width sets the width in DIPs.
func (e Element) Width(v float32) Element { return e.with(func(s *slot) { s.lay.width = v }) }

// Height sets the height in DIPs.
func (e Element) Height(v float32) Element { return e.with(func(s *slot) { s.lay.height = v }) }

// Size sets both.
func (e Element) Size(w, h float32) Element { return e.Width(w).Height(h) }

// Fill takes the parent's whole content box.
func (e Element) Fill() Element {
	return e.with(func(s *slot) { s.lay.fillW, s.lay.fillH = true, true })
}

// FillWidth takes the parent's width.
func (e Element) FillWidth() Element { return e.with(func(s *slot) { s.lay.fillW = true }) }

// FillHeight takes the parent's height.
func (e Element) FillHeight() Element { return e.with(func(s *slot) { s.lay.fillH = true }) }

// Justify places the children along the direction.
func (e Element) Justify(a Align) Element {
	return e.with(func(s *slot) { s.lay.justify, s.lay.hasJustify = a, true })
}

// AlignItems places the children across the direction.
func (e Element) AlignItems(a Align) Element {
	return e.with(func(s *slot) { s.lay.align, s.lay.hasAl = a, true })
}

// AlignSelf places this element across its parent's direction.
func (e Element) AlignSelf(a Align) Element {
	return e.with(func(s *slot) { s.lay.alignSelf, s.lay.hasAlignSelf = a, true })
}

// Center centers the children both ways.
func (e Element) Center() Element { return e.Justify(Center).AlignItems(Center) }

func (e Element) with(fn func(*slot)) Element {
	if e.s != nil {
		fn(e.s)
	}
	return e
}

// -- cached property setters ---------------------------------------------------------------

func (s *slot) setNum(key string, v float64) {
	if s.nums == nil {
		s.nums = map[string]float64{}
	} else if old, ok := s.nums[key]; ok && old == v {
		return
	}
	s.nums[key] = v
	s.node.SetNum(key, v)
}

func (s *slot) setStr(key, v string) {
	if s.strs == nil {
		s.strs = map[string]string{}
	} else if old, ok := s.strs[key]; ok && old == v {
		return
	}
	s.strs[key] = v
	s.node.SetStr(key, v)
}

// -- finishing a pass: what goes, where the rest sits ---------------------------------------

// finish frees the children not built this pass, puts the others in order and
// lays them out, depth first, once the view has returned and every modifier
// has run.
func (c *Context) finish(s *slot) {
	if s.host == nil {
		c.applyLook(s)
		return
	}
	if s.begun != c.pass {
		s.kids = s.kids[:0] // its Children was not called: it holds nothing now
	}
	for _, k := range s.kids {
		c.finish(k)
	}
	for id, k := range s.index {
		if k.pass != c.pass {
			delete(s.index, id)
			c.freeSlot(k)
		}
	}
	c.arrange(s)
	c.applyLook(s)
}

func (c *Context) freeSlot(s *slot) {
	if s.parent != nil && s.parent.host != nil {
		s.parent.host.Remove(s.node)
	}
	s.node.Free()
	s.node = nil
}

// applyLook sets the look properties that differ from what the node shows.
func (c *Context) applyLook(s *slot) {
	l, o := s.look, s.shown
	if l == o && s.lookSet {
		return
	}
	first := !s.lookSet
	if first || l.disabled != o.disabled {
		s.node.Set("enabled", !l.disabled)
	}
	if first || l.hidden != o.hidden {
		s.node.Set("visible", !l.hidden)
	}
	if first || l.bold != o.bold {
		s.node.Set("bold", l.bold)
	}
	if first || l.fontSize != o.fontSize {
		scale := float64(1)
		if l.fontSize > 0 {
			scale = float64(l.fontSize) / 11
		}
		s.node.SetNum("font_scale", scale)
	}
	if first || l.textColor != o.textColor {
		s.node.SetNum("text_color", float64(l.textColor.argb()))
	}
	if first || l.tooltip != o.tooltip {
		s.node.SetStr("description", l.tooltip)
	}
	s.shown, s.lookSet = l, true
}

// arrange puts a container's children in order and packs them.
func (c *Context) arrange(s *slot) {
	row := s.kind == kindRow
	lay := s.lay
	if s.kind != kindRow && s.kind != kindColumn && !lay.hasAl {
		lay.align = Stretch
	}

	// the order: Justify's spacers around and between the children
	grows := false // a growing child takes the free space: no spacers then
	for _, k := range s.kids {
		grows = grows || !k.look.hidden && (k.lay.grow > 0 || k.kind == kindSpacer ||
			(row && k.lay.fillW) || (!row && k.lay.fillH))
	}
	// spacers share the free space equally: one at an end and two between
	// children make SpaceAround's half gaps at the ends
	before, between, after := 0, 0, 0
	if !grows && lay.hasJustify && len(s.kids) > 0 {
		switch lay.justify {
		case Center:
			before, after = 1, 1
		case End:
			before = 1
		case SpaceBetween:
			between = 1
		case SpaceAround:
			before, between, after = 1, 2, 1
		case SpaceEvenly:
			before, between, after = 1, 1, 1
		}
	}
	order := s.order[:0]
	used := 0
	space := func(n int) {
		for ; n > 0; n-- {
			if used == len(s.spaces) {
				s.spaces = append(s.spaces, bui.NewNode("frame"))
			}
			sp := s.spaces[used]
			used++
			side := 0
			if row {
				side = 2
			}
			sp.Pack("side", float64(side)).Pack("expand", 1).Pack("fill", 3)
			order = append(order, sp)
		}
	}
	space(before)
	for i, k := range s.kids {
		if i > 0 {
			space(between)
		}
		order = append(order, k.node)
	}
	space(after)
	for _, sp := range s.spaces[used:] { // spacers no longer needed go
		sp.Free()
	}
	s.spaces = s.spaces[:used]
	for i, n := range order {
		if s.host.ChildCount() > i && s.host.Child(i) == n {
			continue
		}
		if n.Parent() == s.host {
			s.host.Move(n, i) // in place: it keeps the focus and a text edit
		} else {
			s.host.Insert(n, i)
		}
	}
	s.order = order

	// the box itself
	px := func(v float32) int { return int(v*c.scale + 0.5) }
	pad := lay.padding
	m := pad[0]
	for _, v := range pad[1:] {
		m = min(m, v)
	}
	hl := hostLook{padding: px(m), bg: s.look.background, set: true}
	if hl != s.hosted {
		if hl.padding != s.hosted.padding || !s.hosted.set {
			s.host.SetNum("padding", float64(hl.padding))
		}
		if hl.bg != s.hosted.bg || !s.hosted.set {
			s.host.SetColor("bg", hl.bg.color())
		}
		s.hosted = hl
	}
	extra := [4]int{px(pad[0] - m), px(pad[1] - m), px(pad[2] - m), px(pad[3] - m)}

	// each child in its parcel
	gap := px(lay.gap)
	visible := 0
	for _, k := range s.kids {
		if !k.look.hidden {
			visible++
		}
	}
	at := 0
	for _, k := range s.kids {
		p := pack{set: true, reqw: px(k.lay.width), reqh: px(k.lay.height)}
		a := Stretch
		if row {
			a = Center
		}
		if lay.hasAl {
			a = lay.align
		}
		if k.lay.hasAlignSelf {
			a = k.lay.alignSelf
		}
		// as in CSS, a size of its own across the direction wins over stretch
		if a == Stretch && ((row && k.lay.height > 0) || (!row && k.lay.width > 0)) {
			a = Start
		}
		if row {
			p.side = 2
			switch a {
			case Stretch:
				p.fill |= 2
			case Start:
				p.anchor = bui.AnchorN
			case End:
				p.anchor = bui.AnchorS
			}
			if k.lay.grow > 0 || k.lay.fillW || k.kind == kindSpacer {
				p.expand, p.fill = 1, p.fill|1
			}
			if k.lay.fillH {
				p.fill |= 2
			}
			if at > 0 {
				p.padx0 = gap
			}
			p.pady0, p.pady1 = extra[0], extra[2]
			if at == 0 {
				p.padx0 += extra[3]
			}
			if at == visible-1 {
				p.padx1 += extra[1]
			}
		} else {
			switch a {
			case Stretch:
				p.fill |= 1
			case Start:
				p.anchor = bui.AnchorW
			case End:
				p.anchor = bui.AnchorE
			}
			if k.lay.grow > 0 || k.lay.fillH || k.kind == kindSpacer {
				p.expand, p.fill = 1, p.fill|2
			}
			if k.lay.fillW {
				p.fill |= 1
			}
			if at > 0 {
				p.pady0 = gap
			}
			p.padx0, p.padx1 = extra[3], extra[1]
			if at == 0 {
				p.pady0 += extra[0]
			}
			if at == visible-1 {
				p.pady1 += extra[2]
			}
		}
		if k.kind == kindDivider {
			k.setNum("vertical", b2f(row))
		}
		if !k.look.hidden {
			at++
		}
		if p != k.packed {
			n, o := k.node, k.packed
			for _, f := range [...]struct {
				key  string
				v, w int
			}{
				{"side", p.side, o.side}, {"fill", p.fill, o.fill}, {"expand", p.expand, o.expand},
				{"anchor", p.anchor, o.anchor}, {"padx0", p.padx0, o.padx0}, {"padx1", p.padx1, o.padx1},
				{"pady0", p.pady0, o.pady0}, {"pady1", p.pady1, o.pady1},
				{"reqwidth", p.reqw, o.reqw}, {"reqheight", p.reqh, o.reqh},
			} {
				if !o.set || f.v != f.w {
					n.Pack(f.key, float64(f.v))
				}
			}
			k.packed = p
		}
	}
}

func b2f(b bool) float64 {
	if b {
		return 1
	}
	return 0
}
