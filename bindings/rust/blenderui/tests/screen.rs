//! Blender screens from Rust (`blenderui::screen`): the layout from AreaSplit, region elements
//! at the kit's rects, a corner drag that splits an area (filled by its editor's factory),
//! region resizes reported, and the layout saved and restored.

use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Mutex;

use blenderui::prelude::*;
use blenderui::screen::{AreaRef, AreaSplit, Editor, Editors, Region, Screen, ScreenEvent};

static MADE: AtomicUsize = AtomicUsize::new(0);
static CHANGES: AtomicUsize = AtomicUsize::new(0);
static RESIZED: AtomicUsize = AtomicUsize::new(0);
static SAVED: Mutex<String> = Mutex::new(String::new());

fn editors() -> Editors {
    let n = std::cell::Cell::new(0u32); // element ids: one per area this render
    Editors::new()
        .editor("3D Viewport", "VIEW3D", move |_area: &AreaRef| {
            MADE.fetch_add(1, Ordering::Relaxed);
            let i = n.get();
            n.set(i + 1);
            let side = Region::new(div().id(format!("side{i}")).child("Item"))
                .size(200)
                .on_resize(|e, _, _| {
                    assert!(e.size > 0 || e.hidden);
                    RESIZED.fetch_add(1, Ordering::Relaxed);
                });
            Editor::new(div().id(format!("view{i}")).child("viewport"))
                .header(div().id(format!("head{i}")).child("View"))
                .sidebar(side)
        })
        .editor("Outliner", "OUTLINER", |_| Editor::new(div().id("outliner").child("outliner")))
        .editor("Properties", "PROPERTIES", |_| Editor::new(div().id("props").child("props")))
}

struct Screens {
    saved: Option<String>,
}

impl Render for Screens {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        body().m_0().w(px(900.0)).h(px(600.0)).child(
            Screen()
                .editors(editors())
                .layout(AreaSplit::v(0.7, "3D Viewport", AreaSplit::h(0.4, "Outliner", "Properties")))
                .saved(self.saved.as_deref())
                .on_change(|e: &ScreenEvent, _, _| {
                    CHANGES.fetch_add(1, Ordering::Relaxed);
                    *SAVED.lock().unwrap() = e.layout().to_owned();
                }),
        )
    }
}

fn open(cx: &mut App, v: Screens) -> WindowId {
    let opts = WindowOptions {
        window_bounds: Some(WindowBounds::Windowed(Bounds {
            origin: point(px(0.0), px(0.0)),
            size: size(px(900.0), px(600.0)),
        })),
        ..Default::default()
    };
    let id = cx.open_window(opts, |_, cx| cx.new(|_| v)).unwrap().id;
    for _ in 0..3 {
        cx.draw(id); // the kit lays the screen out, the elements follow
    }
    id
}

#[test]
fn screen_layout_regions_split_and_restore() {
    Application::headless().run(|cx| {
        let id = open(cx, Screens { saved: None });
        let view = cx.element_bounds(id, "view0").expect("the viewport's main region");
        let outl = cx.element_bounds(id, "outliner").expect("the outliner");
        let props = cx.element_bounds(id, "props").expect("the properties");
        let head = cx.element_bounds(id, "head0").expect("the header");
        let side = cx.element_bounds(id, "side0").expect("the sidebar");
        // the viewport takes 70 % on the left; the outliner is above the properties
        assert!((head.size.width - 630.0).abs() < 8.0, "{head:?}");
        assert!(outl.origin.x > 620.0 && props.origin.x > 620.0, "{outl:?} {props:?}");
        assert!(outl.origin.y < props.origin.y, "{outl:?} {props:?}");
        assert!((props.origin.y - 0.4 * 600.0).abs() < 8.0, "{props:?}"); // the outliner takes 40 %
        // the header on top, the main region below it; the sidebar 200 wide on the right
        assert!(head.origin.y < view.origin.y, "{head:?} {view:?}");
        assert!((side.size.width - 200.0).abs() < 2.0, "{side:?}");
        assert!((side.origin.x + side.size.width - (head.origin.x + head.size.width)).abs() < 2.0);

        // a corner drag inside the viewport splits it: its factory fills the new area
        let made = MADE.load(Ordering::Relaxed);
        let (cx0, cy0) = (head.origin.x + 2.0, 600.0 - 4.0); // the bottom-left corner zone
        cx.simulate_drag(id, (cx0, cy0), (cx0 + 200.0, cy0 - 10.0));
        for _ in 0..3 {
            cx.draw(id);
        }
        assert!(cx.element_bounds(id, "view1").is_some(), "a second viewport");
        assert!(MADE.load(Ordering::Relaxed) > made);
        assert!(CHANGES.load(Ordering::Relaxed) > 0, "on_change got the layout");
        let saved = SAVED.lock().unwrap().clone();
        assert!(saved.contains("\"bui_screen\": 1"), "{saved}");

        // drag a sidebar's edge: on_resize, and its element follows
        let side = cx.element_bounds(id, "side0").unwrap();
        let (sx, sy) = (side.origin.x + 1.0, side.origin.y + side.size.height / 2.0);
        cx.simulate_drag(id, (sx, sy), (sx + 40.0, sy));
        for _ in 0..3 {
            cx.draw(id);
        }
        assert!(RESIZED.load(Ordering::Relaxed) > 0);
        let narrower = cx.element_bounds(id, "side0").unwrap();
        assert!((narrower.size.width - (side.size.width - 40.0)).abs() < 4.0, "{side:?} {narrower:?}");

        // a new window with the saved layout shows the same areas
        let id2 = open(cx, Screens { saved: Some(saved) });
        assert!(cx.element_bounds(id2, "view1").is_some(), "the split came back");
    });
}

/// `<Screen>` in `rsx!`: props by name, `onChange` with the event alone.
#[allow(dead_code)]
fn rsx_screen(saved: Option<String>) -> impl IntoElement {
    rsx! {
        <Screen editors={editors()} layout={AreaSplit::v(0.75, "3D Viewport", "Properties")}
                saved={saved.as_deref()} onChange={|e: &ScreenEvent| { let _ = e.layout(); }} />
    }
}
