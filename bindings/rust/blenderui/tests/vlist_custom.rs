//! An app's own look on the virtualized view, end to end in an off-screen
//! window: a custom `Measurer` + `ItemPainter` that reads the record's
//! payload, laid out headlessly, and a press that reports the part it hit.
//! Only `blenderui::` imports, as an app would have.

use std::cell::Cell;
use std::rc::Rc;

use blenderui::prelude::*;
use blenderui::vlist::{
    theme_palette, variable_list, ItemPainter, Lines, List, ListState, Measurer, Message, Options, PaintSink, Palette, Record, Rect,
    Rgba, VecSource, SCROLLBAR_WIDTH,
};

const W: f32 = 700.0;
const H: f32 = 400.0;
const ROW: f32 = 50.0;

/// A row of `ROW` px with a 30x20 hit area at the right whose part id is the payload's first byte.
struct Tiles {
    pal: Palette,
}

impl Tiles {
    fn area(r: Rect) -> Rect {
        Rect::new(r.x + r.w - SCROLLBAR_WIDTH - 40.0, r.y + 5.0, 30.0, 20.0)
    }
}

impl Measurer for Tiles {
    fn font_size(&self) -> f32 {
        14.0
    }
    fn line_height(&self) -> f32 {
        20.0
    }
    fn wrap_width(&self, _: &Record<'_>, list_width: f32) -> f32 {
        list_width - SCROLLBAR_WIDTH
    }
    fn chrome(&self, _: &Record<'_>) -> f32 {
        ROW - 20.0
    }
}

impl ItemPainter for Tiles {
    fn palette(&self) -> &Palette {
        &self.pal
    }
    fn item(&self, sink: &mut dyn PaintSink, r: Rect, rec: &Record<'_>, lines: &Lines) {
        sink.fill(Tiles::area(r), Rgba::new(0.9, 0.2, 0.2, 1.0));
        sink.text(8.0, r.y + 15.0, lines.line(rec.text, 0), 14.0, self.pal.text);
    }
    fn skeleton(&self, sink: &mut dyn PaintSink, r: Rect, _: Option<&Record<'_>>) {
        sink.round(Rect::new(8.0, r.y + 8.0, 100.0, 10.0), 5.0, self.pal.dim);
    }
    fn hit(&self, r: Rect, rec: &Record<'_>, _: &Lines, x: f32, y: f32) -> u32 {
        let a = Tiles::area(r);
        if x >= a.x && x < a.x + a.w && y >= a.y && y < a.y + a.h { u32::from(rec.data[0]) } else { 0 }
    }
}

struct View {
    list: Entity<ListState<VecSource, Tiles>>,
    last: Rc<Cell<Option<(usize, u32)>>>,
}

impl Render for View {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        let last = Rc::clone(&self.last);
        body().m_0().child(div().w(px(W)).h(px(H)).child(variable_list(&self.list).on_item_hit(move |i, part, _: &mut List<VecSource, Tiles>| last.set(Some((i, part))))))
    }
}

#[test]
fn a_custom_look_with_a_payload_lays_out_and_reports_the_hit_part() {
    let last = Rc::new(Cell::new(None));
    Application::headless().theme("dark").run(move |cx| {
        let opts = WindowOptions {
            window_bounds: Some(WindowBounds::Windowed(Bounds { origin: point(px(0.0), px(0.0)), size: size(px(W), px(H)) })),
            ..Default::default()
        };
        let mut handle = None;
        let win = cx
            .open_window(opts, |_, cx| {
                let msgs = (0..1000u64).map(|i| Message { id: i + 1, text: format!("tile {i}"), data: vec![(i as u8).wrapping_add(7)], ..Message::default() }).collect();
                let list = List::new(VecSource::from_messages(msgs), Tiles { pal: theme_palette() }, W, H, Options::default()).unwrap();
                let list = cx.new(|cx| ListState::new(list, cx));
                handle = Some(list);
                cx.new(|_| View { list, last: Rc::clone(&last) })
            })
            .unwrap();
        let list = handle.unwrap();
        for _ in 0..3 {
            cx.draw(win.id);
        }
        {
            let s = list.read(cx);
            let p = s.view.placed();
            assert_eq!(p.len(), (H / ROW) as usize, "the viewport is filled with fixed rows");
            assert!(p.iter().all(|p| p.exact && (p.h - ROW).abs() < 0.01), "chrome + one line");
        }
        let (bx, by) = (W - SCROLLBAR_WIDTH - 25.0, ROW + 15.0); // item 1's hit area
        cx.simulate_click(win.id, bx, by);
        cx.draw(win.id);
        assert_eq!(last.get(), Some((1, 8)), "the part is the payload byte of that item");
        cx.simulate_click(win.id, 20.0, 15.0); // item 0, outside its area
        cx.draw(win.id);
        assert_eq!(last.get(), Some((0, 0)), "0 is the item itself");
    });
}
