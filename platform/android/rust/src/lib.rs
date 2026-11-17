//! A BUI app in Rust on Android: the same code as on a desktop. `bui_app_main` is what the
//! NativeActivity shell (platform/android/bui_android.c) calls; the window is the kit's Android host
//! (kit/intern/kit_window_android.c), so `Application::run` needs nothing Android-specific.
//!
//! Three tabs: the widgets, a list of ten million rows and a chat of a hundred thousand messages.
//! The lists are `blenderui::vlist`: only the rows in view exist (the row text is computed when it
//! is fetched, nothing is stored per row), a frame costs O(visible rows), and a fling carries the
//! scrollbar through the whole of it.

use std::fmt::Write as _;

use blenderui::prelude::*;
use blenderui::vlist::{
    chat_list, synth, theme_palette, variable_list, ChatList, ChatStyle, CountSource, List, ListState, Options, RowStyle, VecSource,
};

const ROWS: usize = 10_000_000;
const MESSAGES: u64 = 100_000;

type Rows = Entity<ListState<CountSource, RowStyle>>;
type Chat = Entity<ListState<VecSource, ChatStyle>>;

#[derive(Clone, Copy, PartialEq, Eq)]
enum Tab {
    Widgets,
    Rows,
    Chat,
}

const WORDS: [&str; 12] = ["render", "the", "visible", "rows", "only", "measure", "lazily", "and", "wrap", "text", "on", "demand"];

/// Row `i`, a pure function of `i`: one to a dozen words, so rows are of different heights.
fn row_text(i: usize, out: &mut String) {
    let h = (i as u64).wrapping_mul(0x9e37_79b9_7f4a_7c15) >> 24;
    let _ = write!(out, "#{}", i + 1);
    for k in 0..1 + h % 14 {
        out.push(' ');
        out.push_str(WORDS[((h >> 4) + k * 5) as usize % WORDS.len()]);
    }
}

struct Demo {
    tab: Tab,
    taps: u32,
    rows: Rows,
    chat: Chat,
}

impl Demo {
    fn tab_button(&self, tab: Tab, label: &'static str, cx: &mut Context<Self>) -> impl IntoElement {
        // Blender's selected-button blue for the open tab, its button grey for the others
        let on = self.tab == tab;
        div()
            .id(label)
            .flex_1()
            .py_3()
            .rounded_md()
            .text_center()
            .bg(rgb(if on { 0x4772b3 } else { 0x545454 }))
            .child(label)
            .on_click(cx.listener(move |this, _, _, cx| {
                this.tab = tab;
                cx.notify();
            }))
    }

    fn widgets(&self, cx: &mut Context<Self>) -> Div {
        let mut rows = div().flex().flex_col().gap_1();
        for i in 1..=30 {
            rows = rows.child(div().px_3().py_2().child(format!("Row {i}")));
        }
        div()
            .id("widgets")
            .flex_1()
            .min_h_0()
            .overflow_y_scroll()
            .flex()
            .flex_col()
            .gap_2()
            .p_4()
            .child(h1().flex_shrink_0().child("BUI + Rust on Android"))
            .child(button().flex_shrink_0().py_2().child(format!("Tapped {} times", self.taps)).on_click(cx.listener(
                |this, _, _, cx| {
                    this.taps += 1;
                    cx.notify();
                },
            )))
            .child(input().flex_shrink_0().py_2().attr("placeholder", "tap to type"))
            .child(rows.flex_shrink_0())
    }
}

impl Render for Demo {
    fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let tabs = div()
            .flex()
            .flex_shrink_0()
            .gap_1()
            .p_1()
            .child(self.tab_button(Tab::Widgets, "Widgets", cx))
            .child(self.tab_button(Tab::Rows, "10M rows", cx))
            .child(self.tab_button(Tab::Chat, "Chat 100k", cx));
        let content = match self.tab {
            Tab::Widgets => self.widgets(cx),
            Tab::Rows => div().relative().flex_1().min_h_0().child(div().absolute().inset_0().child(variable_list(&self.rows))),
            Tab::Chat => div().relative().flex_1().min_h_0().child(div().absolute().inset_0().child(chat_list(&self.chat))),
        };
        // the window is the root: Blender's theme, no margin, a column of the tab bar and the page
        body().styled().m_0().css("height", "100vh").flex().flex_col().child(tabs).child(content)
    }
}

/// The app: open the window and run until the activity ends.
#[unsafe(no_mangle)]
pub extern "C" fn bui_app_main() {
    Application::new().run(|cx: &mut App| {
        cx.open_window(WindowOptions::default(), |_, cx| {
            let rows = cx.new(|cx| {
                let list = List::new(CountSource::new(ROWS, row_text), RowStyle::new(theme_palette()), 360.0, 640.0, Options::default())
                    .expect("the index holds ten million rows");
                ListState::new(list, cx)
            });
            let chat = cx.new(|cx| {
                let list = ChatList::from_messages(synth::messages(0, MESSAGES), theme_palette(), 360.0, 640.0, Options::default());
                ListState::new(list, cx)
            });
            cx.new(|_| Demo { tab: Tab::Widgets, taps: 0, rows, chat })
        })
        .unwrap();
    });
}
