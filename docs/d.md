# D: the `bui` DUB package

The canonical D guide lives in
[`bindings/d/README.md`](../bindings/d/README.md). It covers install, a first
window, headless rendering, HTML/CSS/RSX, events, ownership, menus, images,
trees, the raw C API and building/testing; the package's integration suite
covers the APIs it describes.

The package is a normal DUB dependency:

```json
{
    "name": "my-app",
    "targetType": "executable",
    "dependencies": {
        "bui": { "path": "../BUI/bindings/d" }
    }
}
```

Its first build runs CMake and builds the C kit into `bindings/d/.bui/`; later
builds reuse it. Set `BUI_SOURCE_DIR` to point at another BUI checkout, and
`BUI_CMAKE_GENERATOR` to choose a generator such as `"Ninja"`. See the
[README](../bindings/d/README.md#building-the-c-kit) for the build hooks and
the [website's D section](https://bui.preetham.top/docs/d/overview/) for the
same material with a guided tour.
