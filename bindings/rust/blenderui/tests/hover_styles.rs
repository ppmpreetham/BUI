//! Every way to style by hover still works now that a hover renders only when something depends on it.

use blenderui::prelude::*;

struct Page;

impl Render for Page {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        rsx! {
            <body style="margin: 0">
                <style>"#sheet:hover { height: 70px }"</style>
                <div id="tw" class="h-4 hover:h-12 w-10" />
                <div id="sheet" class="h-4 w-10" />
                <div id="attr" class="h-4 w-10" hover:class="h-14" />
                <div id="grp" class="group h-4 w-10"><div id="kid" class="h-2 group-hover:h-9 w-4" /></div>
                <div id="inl" class="h-4" style="width: 40px" hover:style="height: 50px" />
            </body>
        }
    }
}

fn height(cx: &mut App, w: WindowId, id: &str) -> f32 {
    cx.element_bounds(w, id).unwrap().size.height
}

#[test]
fn hover_styles_all_work() {
    Application::headless().run(|cx| {
        let w = cx.open_window(WindowOptions::default(), |_, cx| cx.new(|_| Page)).unwrap().id;
        cx.draw(w);
        for (id, rest, hovered) in [("tw", 16.0, 48.0), ("sheet", 16.0, 70.0), ("attr", 16.0, 56.0), ("inl", 16.0, 50.0)] {
            let b = cx.element_bounds(w, id).unwrap();
            assert_eq!(height(cx, w, id), rest, "{id} at rest");
            cx.simulate_mouse_move(w, b.origin.x + 2.0, b.origin.y + 2.0);
            cx.draw(w);
            assert_eq!(height(cx, w, id), hovered, "{id} hovered");
            cx.simulate_mouse_move(w, 400.0, 400.0);
            cx.draw(w);
            assert_eq!(height(cx, w, id), rest, "{id} left");
        }
        // group-hover: the child follows its parent
        let g = cx.element_bounds(w, "grp").unwrap();
        assert_eq!(height(cx, w, "kid"), 8.0);
        cx.simulate_mouse_move(w, g.origin.x + 30.0, g.origin.y + 2.0);
        cx.draw(w);
        assert_eq!(height(cx, w, "kid"), 36.0, "the child of a hovered group");
    });
}
