// Headless BUI from Go: render a frame and drive a button without a window.
//
//	go run ./examples/headless
//
// SPDX-License-Identifier: GPL-2.0-or-later
package main

import (
	"fmt"

	"github.com/ppmpreetham/BUI/bindings/go/bui"
)

func main() {
	root := bui.NewNode("column").Set("padding", 12)
	defer root.Free()
	root.Add(bui.NewNode("label").Set("text", "BUI from Go"))

	clicks := 0
	button := bui.NewNode("button").Set("text", "Click me")
	button.OnClick(func(bui.Event) { clicks++ })
	root.Add(button)

	ctx := bui.NewContext(root)
	defer ctx.Free()
	raster := bui.NewRaster(320, 200)
	defer raster.Free()

	regions := ctx.Render(raster, 320, 200, true)
	w, h := raster.Size()
	fmt.Printf("rendered %d region(s) into %dx%d (%d pixels)\n", regions, w, h, len(raster.Pixels()))

	x, y := button.Rect().Center()
	ctx.Click(x, y, 0)
	fmt.Printf("clicks: %d; needs render: %v\n", clicks, ctx.NeedsRender())
}
