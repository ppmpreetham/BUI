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

// SetScale sets Blender's resolution scale (1: 20-pixel widgets).
func SetScale(scale float32) { C.BK_set_scale(C.float(scale)) }

// Scale is the resolution scale.
func Scale() float32 { return float32(C.BK_scale()) }

// Unit is Blender's widget unit in pixels at the current scale.
func Unit() int { return int(C.BK_unit()) }

// SetFont sets the UI font family.
func SetFont(family string) {
	withCstr(family, func(s *C.char) { C.BK_set_font(s) })
}

// SetFontSize sets the UI text size in points (Blender's default is 11).
func SetFontSize(points float32) { C.BK_set_font_size(C.float(points)) }

// FontSize is the UI text size in points.
func FontSize() float32 { return float32(C.BK_font_size_points()) }

// SetTextShadow turns Blender's text shadows on or off.
func SetTextShadow(on bool) { C.BK_set_text_shadow(cbool(on)) }

// SetAnimations turns panel and image animations on or off (reduced motion).
func SetAnimations(on bool) { C.BK_set_animations(cbool(on)) }

// SetTheme picks a theme: "dark", "light" or one loaded from JSON; false for
// an unknown name.
func SetTheme(name string) bool {
	return cstr(name, func(s *C.char) bool { return C.BK_set_theme(s) != 0 })
}

// ThemeName is the active theme's name.
func ThemeName() string { return C.GoString(C.BK_theme_name()) }

// ThemeMode is the built-in theme the active one is based on: "dark" or "light".
func ThemeMode() string { return C.GoString(C.BK_theme_mode()) }

// ThemeGeneration changes whenever a theme value does.
func ThemeGeneration() int { return int(C.BK_theme_generation()) }

// ThemeColor reads a color of the active theme: "region_bg", "text",
// "regular.inner", "regular.inner_sel", a theme's own keys...
func ThemeColor(key string) (Color, bool) {
	var c [4]C.float
	ok := cstr(key, func(s *C.char) bool { return C.BK_theme_color(s, &c[0]) != 0 })
	return Color{float32(c[0]), float32(c[1]), float32(c[2]), float32(c[3])}, ok
}

// SetThemeColor sets one color of the active theme until the next SetTheme.
func SetThemeColor(key string, c Color) bool {
	rgba := [4]C.float{C.float(c.R), C.float(c.G), C.float(c.B), C.float(c.A)}
	return cstr(key, func(s *C.char) bool { return C.BK_theme_set_color(s, &rgba[0]) != 0 })
}

// ThemeNum reads a number of the active theme.
func ThemeNum(key string) (float64, bool) {
	var v C.double
	ok := cstr(key, func(s *C.char) bool { return C.BK_theme_num(s, &v) != 0 })
	return float64(v), ok
}

// SetThemeNum sets a number of the active theme (roundness, border.width, ...).
func SetThemeNum(key string, v float64) bool {
	return cstr(key, func(s *C.char) bool { return C.BK_theme_set_num(s, C.double(v)) != 0 })
}

// LoadThemeJSON registers the themes of a theme file's text (docs/theming.md)
// and returns how many.
func LoadThemeJSON(json string) (int, error) {
	return themeResult(cstr(json, func(s *C.char) C.int { return C.BK_theme_load_json(s) }))
}

// LoadThemeFile registers the themes of a theme file.
func LoadThemeFile(path string) (int, error) {
	return themeResult(cstr(path, func(s *C.char) C.int { return C.BK_theme_load_file(s) }))
}

func themeResult(n C.int) (int, error) {
	if n < 0 {
		return 0, errors.New(C.GoString(C.BK_theme_error()))
	}
	return int(n), nil
}

// Themes are the registered themes' names.
func Themes() []string {
	out := make([]string, int(C.BK_theme_count()))
	for i := range out {
		out[i] = C.GoString(C.BK_theme_at(C.int(i)))
	}
	return out
}

// Icon is the id of one of Blender's icons by name ("FILE_FOLDER"), 0 when unknown.
func Icon(name string) int {
	return cstr(name, func(s *C.char) int { return int(C.BK_icon_id(s)) })
}

// EvalNumber evaluates Blender-style number input ("2*pi", "1/3 + 2^2").
func EvalNumber(expr string) (float64, bool) {
	var v C.double
	ok := cstr(expr, func(s *C.char) bool { return C.BK_eval_number(s, &v) != 0 })
	return float64(v), ok
}

// TextWidth is the width of s in the UI font, in pixels.
func TextWidth(s string) float32 {
	if s == "" {
		return 0
	}
	p := unsafe.StringData(s)
	return float32(C.BK_text_width((*C.char)(unsafe.Pointer(p)), C.int(len(s))))
}

// File dialog modes.
const (
	FileOpen      = 0
	FileSave      = 1
	FileDirectory = 2
)

// FileDialog runs Blender's File Browser in its own window until it is
// answered: the chosen paths, none when cancelled. filters are "Label\t*.png
// *.jpg" entries.
func FileDialog(title string, mode int, dir, file string, filters []string, multiple bool) ([]string, error) {
	t, d, f := C.CString(title), C.CString(dir), C.CString(file)
	defer C.free(unsafe.Pointer(t))
	defer C.free(unsafe.Pointer(d))
	defer C.free(unsafe.Pointer(f))
	list := cstrings(filters)
	defer list.free()
	var paths **C.char
	n := int(C.BK_file_dialog(t, C.int(mode), d, f, list.ptr(), C.int(len(filters)), cbool(multiple), &paths))
	if n < 0 {
		return nil, ErrNoWindows
	}
	if paths == nil {
		return nil, nil
	}
	defer C.BK_file_dialog_free(paths)
	out := make([]string, n)
	for i, p := range unsafe.Slice(paths, n) {
		out[i] = C.GoString(p)
	}
	return out, nil
}
