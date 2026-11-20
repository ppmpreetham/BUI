// A slider and a button in a native window, with the kit's nodes.
//
//	go run ./examples/hello
//
// SPDX-License-Identifier: GPL-2.0-or-later
package main

import (
	"fmt"

	"github.com/ppmpreetham/BUI/bindings/go/bui"
)

func main() {
	root := bui.NewNode("column").Set("padding", 8)

	size := bui.NewNode("slider").Set("label", "Size").Set("min", 0).Set("max", 10).Set("value", 2.5)
	size.OnChange(func(e bui.Event) { fmt.Println("size =", e.Node.Num("value")) })
	root.Add(size)

	apply := bui.NewNode("button").Set("text", "Apply")
	apply.OnClick(func(bui.Event) {
		fmt.Println("clicked")
		bui.Quit()
	})
	root.Add(apply)

	if bui.NewWindow(bui.WindowOptions{Title: "BUI from Go", Width: 320, Height: 120, Root: root}) == nil {
		fmt.Println("no native windows here")
		return
	}
	bui.Run()
}
