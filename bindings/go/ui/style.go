// SPDX-License-Identifier: GPL-2.0-or-later

package ui

import (
	"strconv"
	"strings"

	"github.com/ppmpreetham/BUI/bindings/go/bui"
)

// Color is an sRGB color with alpha, each 0..255.
type Color struct{ R, G, B, A uint8 }

// Transparent is no color: a TextColor or Background left at it is the theme's.
var Transparent = Color{}

// RGB is an opaque color.
func RGB(r, g, b uint8) Color { return Color{r, g, b, 255} }

// RGBA is a color with alpha from 0 to 1.
func RGBA(r, g, b uint8, alpha float32) Color { return Color{r, g, b, channel(alpha)} }

// channel is a 0..1 value as 0..255, clamped and rounded.
func channel(v float32) uint8 { return uint8(min(max(v, 0), 1)*255 + 0.5) }

// Hex parses "#rgb", "#rgba", "#rrggbb" or "#rrggbbaa"; Transparent when it
// cannot.
func Hex(s string) Color {
	s = strings.TrimPrefix(s, "#")
	if len(s) == 3 || len(s) == 4 {
		var b strings.Builder
		for _, ch := range s {
			b.WriteRune(ch)
			b.WriteRune(ch)
		}
		s = b.String()
	}
	if len(s) == 6 {
		s += "ff"
	}
	v, err := strconv.ParseUint(s, 16, 32)
	if len(s) != 8 || err != nil {
		return Transparent
	}
	return Color{uint8(v >> 24), uint8(v >> 16), uint8(v >> 8), uint8(v)}
}

func (c Color) argb() uint32 {
	return uint32(c.A)<<24 | uint32(c.R)<<16 | uint32(c.G)<<8 | uint32(c.B)
}

func (c Color) color() bui.Color {
	return bui.Color{R: float32(c.R) / 255, G: float32(c.G) / 255, B: float32(c.B) / 255, A: float32(c.A) / 255}
}

func fromKit(c bui.Color) Color {
	return Color{channel(c.R), channel(c.G), channel(c.B), channel(c.A)}
}

// Align places children along (Justify) or across (AlignItems, AlignSelf) a
// row's or a column's direction.
type Align uint8

// Alignments. Stretch is AlignItems' and AlignSelf's only; the Space ones
// Justify's.
const (
	Start Align = iota
	Center
	End
	Stretch
	SpaceBetween
	SpaceAround
	SpaceEvenly
)

// Theme is the colors of the active Blender theme, by MyGo's names.
type Theme struct {
	Dark       bool
	Background Color // the window: Blender's region background
	Surface    Color // buttons and fields
	Border     Color
	Text       Color
	TextMuted  Color // secondary text
	Accent     Color // selected and default buttons
	AccentText Color // text on the accent
}

func currentTheme() Theme {
	get := func(key string) Color {
		c, _ := bui.ThemeColor(key)
		return fromKit(c)
	}
	t := Theme{
		Dark:       bui.ThemeMode() == "dark",
		Background: get("region_bg"),
		Surface:    get("regular.inner"),
		Border:     get("regular.outline"),
		Text:       get("text"),
		Accent:     get("regular.inner_sel"),
		AccentText: get("regular.text_sel"),
	}
	// the kit draws text opaque: muted is 60% of the way from the background
	mix := func(a, b uint8) uint8 { return uint8((int(a)*3 + int(b)*2) / 5) }
	t.TextMuted = Color{mix(t.Text.R, t.Background.R), mix(t.Text.G, t.Background.G), mix(t.Text.B, t.Background.B), 255}
	return t
}

// Modifiers are the keys held with another: Shift, Ctrl, Alt and Cmd (Ctrl
// outside macOS).
type Modifiers int

// Modifier keys.
const (
	Shift Modifiers = bui.Shift
	Ctrl  Modifiers = bui.Ctrl
	Alt   Modifiers = bui.Alt
	Cmd   Modifiers = bui.Ctrl
)

// Key is a key, by its X11 keysym name ("Return", "a", "F1").
type Key string

// Keys. Letters and digits are their own names: Key("s").
const (
	KeyUp        Key = "Up"
	KeyDown      Key = "Down"
	KeyLeft      Key = "Left"
	KeyRight     Key = "Right"
	KeyEnter     Key = "Return"
	KeyEscape    Key = "Escape"
	KeyTab       Key = "Tab"
	KeySpace     Key = "space"
	KeyBackspace Key = "BackSpace"
	KeyDelete    Key = "Delete"
	KeyHome      Key = "Home"
	KeyEnd       Key = "End"
	KeyPageUp    Key = "Prior"
	KeyPageDown  Key = "Next"
	KeyA         Key = "a"
	KeyC         Key = "c"
	KeyN         Key = "n"
	KeyO         Key = "o"
	KeyS         Key = "s"
	KeyV         Key = "v"
	KeyX         Key = "x"
	KeyZ         Key = "z"
	KeyF1        Key = "F1"
)

// comboName is the kit's name of a key combination: "ctrl+shift+s".
func comboName(mods Modifiers, key Key) string {
	var b strings.Builder
	if mods&Ctrl != 0 {
		b.WriteString("ctrl+")
	}
	if mods&Shift != 0 {
		b.WriteString("shift+")
	}
	if mods&Alt != 0 {
		b.WriteString("alt+")
	}
	b.WriteString(string(key))
	return b.String()
}
