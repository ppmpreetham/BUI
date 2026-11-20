# Go: the `bui` and `ui` packages

The canonical Go guide lives in
[`bindings/go/README.md`](../bindings/go/README.md). It covers requirements,
building the C kit with `go generate`, the MyGo-style `ui` package (views,
input, keys, layout, the look, `Tester`), the kit's `bui` package (nodes,
contexts, windows, menus, images, canvases, trees, themes), threads and
platforms.

The module lives in the repository. Use it from your own module with a
`replace`, and build the kit once:

```
require github.com/ppmpreetham/BUI/bindings/go v0.0.0
replace github.com/ppmpreetham/BUI/bindings/go => ../BUI/bindings/go
```

```sh
go generate github.com/ppmpreetham/BUI/bindings/go/bui
```

cgo needs gcc or clang: on Windows, MinGW-w64 gcc (MSVC and `clang-cl` don't
work with cgo).
