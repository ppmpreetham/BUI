# Touch screens and trackpad gestures

BUI's widgets work with fingers. On a touch screen, one finger is the pointer: tap a button,
drag a number field or a slider, open a menu and tap a row. Two fingers scroll, zoom and turn
what is under them. A trackpad pinch zooms the same way. The native window hosts do this for
you; if you host the kit yourself, three calls feed it.

## What a finger does

| Fingers | What happens |
|---|---|
| **One** | It is the pointer: it moves there, presses, drags and releases, as the left mouse button does. When it lifts, the pointer leaves, so no hover highlight or tooltip stays behind. Pointer events made this way carry `BK_FROM_TOUCH` in their mods. |
| **A second one** | Whatever the first finger was doing is cancelled, as <kbd>Esc</kbd> cancels a drag in Blender: a dragged value goes back, a pressed button is not clicked, a number field does not start editing. The two fingers are now a gesture. |
| **Two, moving together** | Pan. The scroll view or region under them scrolls, and the content follows the fingers. The node editor pans its view. |
| **Two, spreading or pinching** | Magnify about their centre (`BK_magnify`). The node editor zooms. |
| **Two, turning** | Rotate about their centre (`BK_rotate`). |
| **After a gesture** | No finger becomes the pointer again until every finger has lifted, so lifting one finger of a pinch never clicks what is under the other. |
| **More than two** | Followed, not used, up to ten contacts. |

Blender has no touch-screen input, so there is no source to follow for touch itself. One finger
does what X11, Wayland compositors and Windows do for windows that don't know about touch. Two
fingers send the events Blender gets from a trackpad: GHOST's `Scroll`, `Magnify` and `Rotate`
trackpad events, which Blender reads as `MOUSEPAN`, `MOUSEZOOM` and `MOUSEROTATE`.

## Window hosts

| Host | Touch screen | Trackpad pinch / rotate |
|---|---|---|
| **Windows** | `WM_TOUCH` (the window is registered with `RegisterTouchWindow`). The mouse messages Windows makes from touches are ignored, and press-and-hold no longer turns into a right click. | Precision touchpads send pinches as Ctrl + wheel, as before. |
| **X11** | XInput 2.2 touch events (`XI_TouchBegin` / `Update` / `End`). libXi is loaded at run time like libX11; without it, or with an older server, touches stay emulated mouse input as before. | Not sent by X11 touchpad drivers. |
| **Wayland** | `wl_touch`, including `cancel` when the compositor takes the touches for a system gesture. | `zwp_pointer_gestures_v1` pinch: scale and rotation, as GHOST_SystemWayland reads them. |
| **macOS** | No touch screens. | `magnifyWithEvent:` and `rotateWithEvent:`, as GHOST_SystemCocoa. |

## Hosting the kit yourself

```c
/* every contact of a touch screen, in window pixels; id is the platform's */
BK_touch(ctx, id, BK_TOUCH_DOWN, x, y, mods);
BK_touch(ctx, id, BK_TOUCH_MOVE, x, y, mods);
BK_touch(ctx, id, BK_TOUCH_UP, x, y, mods);
BK_touch(ctx, id, BK_TOUCH_CANCEL, x, y, mods); /* the platform took it back */

/* trackpad gestures, at the pointer */
BK_magnify(ctx, x, y, 0.1f, mods);  /* 10% larger since the last event */
BK_rotate(ctx, x, y, 5.0f, mods);   /* degrees, > 0 counter-clockwise */
```

Each call returns non-zero when something changed. A `BK_TOUCH_DOWN` for an id that is still
down ends the old contact first (its up was lost). Moves and ups for unknown ids are ignored.
Don't also send the mouse events your platform makes from the same touches.

The same calls are on every binding: `ctx.touch(id, phase, x, y, mods)`,
`ctx.magnify(x, y, delta, mods)` and `ctx.rotate(x, y, degrees, mods)` in Python (phase 0 down,
1 move, 2 up, 3 cancel); `ctx:touch(...)`, `ctx:magnify(...)` and `ctx:rotate(...)` with
`bui.TOUCH_DOWN` ... `bui.TOUCH_CANCEL` in Lua; `context.touch(...)`, `magnify` and `rotate`
with `BK_TOUCH_*` in D (and `node.setTouch(true)` for custom nodes); `BK_touch`, `BK_magnify`
and `BK_rotate` in `blenderui-sys`.

## Gestures for your own elements and windows

Kit widgets react to gestures by themselves (scroll views scroll, the node editor pans and
zooms). Your own content can take them too.

**Rust (`blenderui`):** every element can listen.

```rust
div().id("map").on_gesture(|e, window, cx| {
    match e.kind {
        GestureKind::Pan => { /* e.dx, e.dy: the fingers' movement, in logical pixels */ }
        GestureKind::Magnify => { /* e.magnify: 0.1 = 10% larger, about e.position */ }
        GestureKind::Rotate => { /* e.degrees, positive counter-clockwise */ }
    }
    cx.stop_propagation(); // a stopped pan does not also scroll the page
})
```

`GestureEvent { kind, position, dx, dy, magnify, degrees, modifiers, touch }` bubbles like the
other events. `touch` is `false` for a trackpad pinch or turn. A pan that no listener stops then
scrolls the scroll container under it and reaches `on_scroll_wheel` listeners, with
`ScrollWheelEvent::touch` set and `delta` in lines (`delta * WHEEL_LINE_PX` is the pixels the
fingers moved). Pages written for the wheel scroll with two fingers without any change; the
virtual list and the data table use that to keep their rows under the fingers.
`rsx!` takes `onGesture={...}`. Tests can drive it with `cx.simulate_touch(window, id,
TouchPhase::Down, x, y)`, `simulate_magnify` and `simulate_rotate`.

**C and the other bindings:** give a node the `"gestures"` property
(`BK_set_num(node, "gestures", 1)`). Gestures over it or its children then come to your
`BKHost.emit` as `BK_EV_GESTURE`, and the kit doesn't scroll anything for them:

| Field | Value |
|---|---|
| `ref` | `"mods kind x y"`: `mods` with `BK_FROM_TOUCH` for a touch screen; `kind` `BK_GESTURE_PAN`, `MAGNIFY` or `ROTATE`; `x`, `y` the gesture's centre, relative to the node |
| `a`, `b` | Pan: the pixels moved. Magnify: `a` = scale change. Rotate: `a` = degrees |

Nodes with only `"events"` (Tk bindings, Qt event handlers) are unchanged: two fingers still
scroll under them. The two fingers' moves are paired into one update, so a pan sends no magnify
or rotate of its own and a pinch sends no pan.

## Custom nodes

By default a `custom` node gets touch the way it gets the mouse: `BK_CUSTOM_PRESS`, `MOTION` and
`RELEASE` from the first finger (with `BK_FROM_TOUCH` in `mods`), then, for two fingers,
`BK_CUSTOM_WHEEL` (the pan, in wheel notches like `BK_scroll`'s), `BK_CUSTOM_MAGNIFY`
(`e->magnify`) and `BK_CUSTOM_ROTATE` (`e->rotate`). Trackpad gestures arrive as the same
`MAGNIFY` and `ROTATE` events. The Dear ImGui backend reports touch as
`ImGuiMouseSource_TouchScreen`.

A node that does its own multi-touch asks for every contact instead:

```c
BK_custom_set_touch(node, 1);

static int my_event(void *data, BKNode *node, const BKCustomEvent *e)
{
  if (e->type == BK_CUSTOM_TOUCH) {
    /* e->touch_id, e->touch_phase (BK_TOUCH_DOWN / MOVE / UP / CANCEL), e->x, e->y */
    return 1;
  }
  return 0;
}
```

Every contact that begins over the node then arrives as `BK_CUSTOM_TOUCH`, with no pointer
emulation and no gestures. The first finger gives the node keyboard focus, as a click does.
Fingers that begin elsewhere still work the rest of the UI. While a menu or popup is open,
touches take the normal path, so a tap outside closes the menu.

## Limits

- A finger's drag never wraps at the window edge and never moves the system mouse pointer (the
  hidden-pointer drag of number fields is for the mouse); a number field dragged by touch stops
  changing at the edge.
- One finger never scrolls; scrolling takes two (as in Blender, where a single drag edits).
