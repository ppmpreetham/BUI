//! The virtualized view as a general list, not a chat: a feed of 200 000
//! task cards with variable heights (a colored bar by kind, a title, a
//! wrapped body, a count badge read from the item's payload bytes). The
//! look is this file's own `Measurer` + `ItemPainter`; the engine (height
//! index, anchored scrolling, wrap cache, scrollbar, input) is BUI's.
//! Click a card to select it, click its badge to acknowledge it.
//!
//! ```text
//! cargo run --release --example vlist_cards
//! ```

#[path = "site/common.rs"]
mod common;

use blenderui::prelude::*;
use blenderui::vlist::{
    theme_palette, variable_list, ItemPainter, ItemSource, Lines, List, ListState, Measurer, Message, Options, PaintSink, Palette, Record, Rect, Rgba,
    VecSource, SCROLLBAR_WIDTH,
};

/// The badge's part id for `ItemPainter::hit` (0 is the card itself).
const BADGE: u32 = 1;

/// Kind colors: `Record::sender` picks one.
const KINDS: [Rgba; 4] = [Rgba::new(0.30, 0.55, 0.85, 1.0), Rgba::new(0.35, 0.70, 0.45, 1.0), Rgba::new(0.85, 0.70, 0.30, 1.0), Rgba::new(0.85, 0.45, 0.30, 1.0)];

/// A card's payload: byte 0 is the badge count, the rest is the title (UTF-8).
fn payload(count: u8, title: &str) -> Vec<u8> {
    let mut d = vec![count];
    d.extend_from_slice(title.as_bytes());
    d
}

struct CardStyle {
    pal: Palette,
    font_size: f32,
    line_height: f32,
    pad: f32,
    title_h: f32,
    selected: Option<u64>,
    /// Ids of the cards whose badge was clicked.
    acked: Vec<u64>,
}

impl CardStyle {
    fn new(pal: Palette) -> Self {
        Self { pal, font_size: 13.0, line_height: 18.0, pad: 10.0, title_h: 22.0, selected: None, acked: Vec::new() }
    }

    /// The badge of the card painted in `r`: one rect for painting and for hit testing.
    fn badge(&self, r: Rect) -> Rect {
        Rect::new(r.x + r.w - SCROLLBAR_WIDTH - self.pad - 34.0, r.y + self.pad, 34.0, 18.0)
    }
}

impl Measurer for CardStyle {
    fn font_size(&self) -> f32 {
        self.font_size
    }

    fn line_height(&self) -> f32 {
        self.line_height
    }

    fn wrap_width(&self, _: &Record<'_>, list_width: f32) -> f32 {
        (list_width - SCROLLBAR_WIDTH - 6.0 - 2.0 * self.pad).max(40.0)
    }

    fn chrome(&self, _: &Record<'_>) -> f32 {
        2.0 * self.pad + self.title_h + 1.0
    }
    // `estimator` is left to the default: pad, line height and wrap width come from the methods above.
}

impl ItemPainter for CardStyle {
    fn palette(&self) -> &Palette {
        &self.pal
    }

    fn item(&self, sink: &mut dyn PaintSink, r: Rect, rec: &Record<'_>, lines: &Lines) {
        let pal = self.pal;
        let (count, title) = rec.data.split_first().map_or((0, ""), |(c, t)| (*c, std::str::from_utf8(t).unwrap_or("")));
        if self.selected == Some(rec.id) {
            sink.fill(r, pal.sel.opacity(0.35));
        }
        sink.fill(Rect::new(r.x, r.y + 2.0, 4.0, r.h - 4.0), KINDS[usize::from(rec.sender) % KINDS.len()]);
        let x = 6.0 + self.pad;
        sink.text(x, r.y + self.pad + self.title_h * 0.5, title, self.font_size + 1.0, pal.text);
        let mut y = r.y + self.pad + self.title_h;
        for i in 0..lines.count() {
            sink.text(x, y + self.line_height * 0.5, lines.line(rec.text, i), self.font_size, pal.dim);
            y += self.line_height;
        }
        let b = self.badge(r);
        let acked = self.acked.contains(&rec.id);
        sink.round(b, 9.0, if acked { pal.sel } else { pal.dim.opacity(0.25) });
        let mut n = [0u8; 3];
        let label = if count > 99 { "99+" } else { format_count(count, &mut n) };
        let w = sink.text_width(label, self.font_size - 1.0);
        sink.text(b.x + (b.w - w) * 0.5, b.y + b.h * 0.5, label, self.font_size - 1.0, pal.text);
        sink.fill(Rect::new(0.0, r.y + r.h - 1.0, r.w - SCROLLBAR_WIDTH, 1.0), pal.line);
    }

    fn skeleton(&self, sink: &mut dyn PaintSink, r: Rect, _: Option<&Record<'_>>) {
        let dim = self.pal.dim.opacity(0.18);
        sink.round(Rect::new(6.0 + self.pad, r.y + self.pad + 4.0, 140.0, 12.0), 6.0, dim);
        sink.round(Rect::new(6.0 + self.pad, r.y + self.pad + self.title_h + 3.0, (r.w - 80.0).max(20.0) * 0.7, 10.0), 5.0, dim);
    }

    fn hit(&self, r: Rect, _: &Record<'_>, _: &Lines, x: f32, y: f32) -> u32 {
        let b = self.badge(r);
        if x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h { BADGE } else { 0 }
    }
}

/// 0..=99 as text without allocating.
fn format_count(n: u8, buf: &mut [u8; 3]) -> &str {
    let len = if n >= 10 {
        buf[0] = b'0' + n / 10;
        buf[1] = b'0' + n % 10;
        2
    } else {
        buf[0] = b'0' + n;
        1
    };
    std::str::from_utf8(&buf[..len]).unwrap_or("")
}

const BODY: [&str; 12] = ["deploy", "the", "staging", "build", "after", "review", "and", "check", "logs", "for", "errors", "today"];

/// Card `i`, a pure function of `i`: one to a few lines of body.
fn card(i: u64) -> Message {
    let h = i.wrapping_mul(0x9e37_79b9_7f4a_7c15) >> 20;
    let words = 3 + (h % 40) as usize;
    let body = (0..words).map(|k| BODY[((h >> 3) as usize + k * 7) % BODY.len()]).collect::<Vec<_>>().join(" ");
    Message { id: i + 1, sender: (h % 4) as u16, text: body, data: payload((h % 120) as u8, &format!("Task #{}", i + 1)), ..Message::default() }
}

type Cards = Entity<ListState<VecSource, CardStyle>>;

struct Page {
    cards: Cards,
}

impl Render for Page {
    fn render(&mut self, _: &mut Window, _: &mut Context<Self>) -> impl IntoElement {
        html().styled().size_full().child(
            body().m_0().size_full().child(
                div().size_full().child(
                    variable_list(&self.cards)
                        .on_item_click(|i, list: &mut List<VecSource, CardStyle>| {
                            let id = list.source.id_at(i);
                            list.style.selected = if list.style.selected == id { None } else { id };
                        })
                        .on_item_hit(|i, part, list| {
                            if part != BADGE {
                                return;
                            }
                            if let Some(id) = list.source.id_at(i) {
                                match list.style.acked.iter().position(|a| *a == id) {
                                    Some(at) => drop(list.style.acked.swap_remove(at)),
                                    None => list.style.acked.push(id),
                                }
                            }
                        }),
                ),
            ),
        )
    }
}

const CARDS: u64 = 200_000;

fn main() {
    let shot = common::screenshot_arg();
    let app = if shot.is_some() { Application::headless() } else { Application::new() };
    app.run(move |cx| {
        let win = cx
            .open_window(common::options(720.0, 460.0, "Cards"), |_, cx| {
                let source = VecSource::from_messages((0..CARDS).map(card).collect());
                let list = List::new(source, CardStyle::new(theme_palette()), 720.0, 460.0, Options::default()).expect("a Vec fits the index");
                let cards = cx.new(|cx| ListState::new(list, cx));
                cx.new(|_| Page { cards })
            })
            .unwrap();
        if let Some(path) = shot {
            for _ in 0..3 {
                cx.draw(win.id);
            }
            // the first card's badge, the way a click does it
            cx.simulate_click(win.id, 720.0 - SCROLLBAR_WIDTH - 10.0 - 17.0, 20.0);
            cx.draw(win.id);
            let (w, h, px) = cx.draw(win.id);
            common::save(&path, w, h, &px);
        }
    });
}
