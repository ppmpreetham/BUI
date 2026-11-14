//! A variable-height virtualized view for any app, from 100 items to 10M+:
//! feeds, logs, file lists, cards, chats. One design, drawn the way
//! Blender's text view draws (only what is visible, straight to the paint
//! sink), with a Fenwick-over-blocks height index. You bring the items: a
//! [`Measurer`] (how tall) and an [`ItemPainter`] (how it looks and what a
//! press hits) over a [`Record`] with a free `data` payload. The chat
//! ([`ChatStyle`]) is one shipped look; everything a look needs is
//! re-exported here, so an app depends on `blenderui` alone.
//!
//! Everything that is not blenderui lives in the `blenderui-vlist` crate,
//! once: the height index, anchor-based scrolling, measuring and wrapping,
//! the item sources (in memory, file-backed) and the chat look. This module
//! is the adapter:
//!
//! * [`ListState`]: the entity around a [`List`] (the core's items + style +
//!   view), which lays a frame out while rendering and answers input;
//! * [`VariableList`] ([`variable_list`]): the element, one painted leaf that
//!   draws the state ([`crate::sink`] turns the core's paint calls into the
//!   engine's display list), plus pointer, wheel and keyboard input;
//! * [`chat_list`]: a [`VariableList`] over a [`ChatList`] with whole-message
//!   selection wired. Your own look goes through [`variable_list`] instead
//!   (`on_item_click`, `on_item_hit`), see `examples/vlist_cards.rs`.
//!
//! ```ignore
//! let chat = cx.new(|cx| ListState::new(ChatList::from_messages(msgs, palette, 600.0, 400.0, opts), cx));
//! // in render
//! chat_list(&chat).size_full()
//! // from anywhere
//! chat.update(cx, |s, _| s.push(message));
//! ```

mod element;
mod state;

pub use blenderui_vlist::{
    Align, Budget, ChatList, ChatStyle, CountSource, Estimator, Fx, Hit, ItemPainter, ItemSource, Lines, List, ListView, Measurer, Message,
    Nav, Options, PaintSink, Page, Palette, Placed, Point, Pos, Record, Rect, Rgba, RowList, RowStyle, TextWidth, VecSource, FLAG_DIVIDER,
    FLAG_HEADER, FLAG_OWN, FLAG_PLACEHOLDER, FLAG_USER, SCROLLBAR_WIDTH,
};
pub use blenderui_vlist::synth;
pub use element::{chat_list, variable_list, VariableList};
pub use state::{ListState, ViewInfo};

/// The colors of the engine's current theme, for a list style's palette.
#[must_use]
pub fn theme_palette() -> blenderui_vlist::Palette {
    crate::sink::current_palette()
}
