//! Selecting text in the element tree, like a browser: press and drag over
//! text, double-click a word, triple-click a block, `Ctrl+A`, `Ctrl+C`.
//!
//! The model is two endpoints in document order, each a text box (a word of a
//! line box, or a text run in a flex / grid item) by key and a byte offset in
//! its text. Document order is [`BoxNode::seq`], the order layout placed the
//! boxes in. Everything here reads the laid-out frame and measures with the
//! same [`TextCache`] layout used, so the highlight sits on the drawn glyphs.
//! Nothing runs per frame unless a selection exists, and then only to find the
//! endpoints and paint the highlight (see `paint::paint_selection`).

use std::time::{Duration, Instant};

use crate::color::Rgba;
use crate::layout::{Content, Frame, Key, StyleStore};
use crate::paint::each_run_line;
use crate::style::{Computed, DisplayKind, UserSelect};
use crate::text::{self, TextCache};

/// Presses closer than this in time and place chain into double and triple clicks.
const MULTI_CLICK: Duration = Duration::from_millis(500);
const MULTI_CLICK_PX: f32 = 5.0;

/// Where a selection ends: a text box and a byte offset in its text.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Endpoint {
    key: Key,
    off: u32,
}

/// The selected text: from `anchor` (where it began) to `focus`, either way round.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Selection {
    anchor: Endpoint,
    focus: Endpoint,
}

/// A place in the text of this frame: an index into the units, and an offset.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
struct Pos {
    unit: usize,
    off: u32,
}

/// How far a drag moves the focus: a character, a word, a block.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Grain {
    Char,
    Word,
    Block,
}

/// A press that may become a drag: what was chosen first stays selected.
#[derive(Clone, Copy, Debug)]
struct Drag {
    lo: Endpoint,
    hi: Endpoint,
    grain: Grain,
}

/// What the frame offers selection: its frame, styles and text measuring.
pub(crate) struct Cx<'a> {
    pub frame: &'a Frame,
    pub styles: &'a StyleStore,
    pub text: &'a mut TextCache,
}

/// The selected range as paint needs it: the placement order and offsets of the
/// two ends, and the highlight color.
#[derive(Clone, Copy, Debug)]
pub(crate) struct SelPaint {
    pub color: Rgba,
    lo: (u32, u32),
    hi: (u32, u32),
}

impl SelPaint {
    /// The selected bytes `(start, end)` of the text box placed `seq`th, whose text is
    /// `len` long, and whether the selection goes on after it.
    pub(crate) fn range(&self, seq: u32, len: usize) -> Option<(usize, usize, bool)> {
        if seq < self.lo.0 || seq > self.hi.0 {
            return None;
        }
        let start = if seq == self.lo.0 { self.lo.1 as usize } else { 0 };
        let end = if seq == self.hi.0 { (self.hi.1 as usize).min(len) } else { len };
        Some((start.min(end), end, seq != self.hi.0))
    }
}

/// A window's text selection.
#[derive(Default)]
pub(crate) struct Selector {
    sel: Option<Selection>,
    drag: Option<Drag>,
    /// The selectable boxes of the frame, in document order (built when first needed).
    units: Vec<u32>,
    units_ok: bool,
    /// The ends of `sel` as unit positions, in order (found once per frame and selection, not per paint).
    ends: Option<(Pos, Pos)>,
    /// The last press: when, where, and how many in a row.
    click: Option<(Instant, f32, f32, u8)>,
    /// The press in progress selected text: its release is not a click.
    dragged: bool,
}

impl Selector {
    pub(crate) fn has_selection(&self) -> bool {
        self.sel.is_some()
    }

    /// The frame changed: the units are stale.
    pub(crate) fn invalidate(&mut self) {
        self.units_ok = false;
        self.ends = None;
    }

    /// Drop the selection. True when there was one.
    pub(crate) fn clear(&mut self) -> bool {
        self.drag = None;
        self.ends = None;
        self.sel.take().is_some()
    }

    fn ensure(&mut self, f: &Frame, st: &StyleStore) {
        if self.units_ok {
            return;
        }
        self.units.clear();
        for (i, n) in f.nodes.iter().enumerate() {
            let unit = match n.content {
                Content::Word { .. } | Content::LineBreak => true,
                Content::Text(_) => n.taffy.is_some() || n.folded,
                _ => false,
            };
            if unit && n.visible && st[n.style].user_select != UserSelect::None {
                self.units.push(i as u32);
            }
        }
        self.units.sort_unstable_by_key(|&i| f.nodes[i as usize].seq);
        self.units_ok = true;
    }

    fn node(&self, unit: usize) -> usize {
        self.units[unit] as usize
    }

    fn pos_of(&self, f: &Frame, e: Endpoint) -> Option<Pos> {
        let unit = self.units.iter().position(|&i| f.nodes[i as usize].key == e.key)?;
        Some(Pos { unit, off: e.off })
    }

    fn endpoint(&self, f: &Frame, p: Pos) -> Endpoint {
        Endpoint { key: f.nodes[self.node(p.unit)].key, off: p.off }
    }

    /// The ends of the selection in order, or None (and the selection dropped)
    /// when a text box it ended in is gone.
    fn range(&mut self, f: &Frame) -> Option<(Pos, Pos)> {
        if self.ends.is_some() {
            return self.ends;
        }
        let sel = self.sel?;
        let ends = self.pos_of(f, sel.anchor).zip(self.pos_of(f, sel.focus));
        let Some((a, b)) = ends else {
            self.sel = None;
            return None;
        };
        self.ends = Some(if a <= b { (a, b) } else { (b, a) });
        self.ends
    }

    /// What paint highlights, or None when nothing is selected.
    pub(crate) fn paint(&mut self, f: &Frame, st: &StyleStore) -> Option<SelPaint> {
        self.sel?;
        self.ensure(f, st);
        let (lo, hi) = self.range(f)?;
        let [r, g, b, a] = crate::theme_color("selection").unwrap_or([0.28, 0.45, 0.77, 1.0]);
        let seq = |p: Pos| (f.nodes[self.node(p.unit)].seq, p.off);
        Some(SelPaint { color: Rgba { r, g, b, a }, lo: seq(lo), hi: seq(hi) })
    }

    // -- text ---------------------------------------------------------------------------------

    /// The selected text: block boundaries are newlines, whitespace is as rendered.
    pub(crate) fn text(&mut self, f: &Frame, st: &StyleStore) -> Option<String> {
        self.sel?;
        self.ensure(f, st);
        let (lo, hi) = self.range(f)?;
        let mut out = String::new();
        let mut prev: Option<usize> = None;
        let mut newline = false;
        for u in lo.unit..=hi.unit {
            let i = self.node(u);
            if matches!(f.nodes[i].content, Content::LineBreak) {
                newline = prev.is_some();
                continue;
            }
            let t = f.text(i);
            let s = if u == lo.unit { floor(t, lo.off as usize) } else { 0 };
            let e = if u == hi.unit { floor(t, hi.off as usize) } else { t.len() };
            let piece = &t[s..e.max(s)];
            if piece.is_empty() {
                continue;
            }
            if let Some(p) = prev {
                if newline || block_of(f, st, p) != block_of(f, st, i) {
                    out.push('\n');
                } else if !piece.starts_with(char::is_whitespace) && space_after(f, p) {
                    out.push(' ');
                }
            }
            out.push_str(piece);
            prev = Some(i);
            newline = false;
        }
        Some(out)
    }

    // -- input --------------------------------------------------------------------------------

    /// Select everything selectable.
    pub(crate) fn select_all(&mut self, f: &Frame, st: &StyleStore) -> bool {
        self.ensure(f, st);
        let Some(last) = self.units.len().checked_sub(1) else { return false };
        let end = f.text(self.node(last)).len() as u32;
        let (a, b) = (self.endpoint(f, Pos { unit: 0, off: 0 }), self.endpoint(f, Pos { unit: last, off: end }));
        self.set(Some(Selection { anchor: a, focus: b }).filter(|s| s.anchor != s.focus))
    }

    fn set(&mut self, s: Option<Selection>) -> bool {
        let changed = self.sel != s;
        self.sel = s;
        self.ends = None;
        changed
    }

    /// The left button went down at (x, y); `hit` is the topmost box there,
    /// `shift` extends the selection. True when the highlight changed.
    pub(crate) fn press(&mut self, cx: &mut Cx, x: f32, y: f32, hit: Option<usize>, shift: bool) -> bool {
        self.ensure(cx.frame, cx.styles);
        let clicks = self.count_click(x, y);
        self.dragged = false;
        self.drag = None;
        let caret = self.pick(cx, x, y)
            .filter(|&(p, inside)| inside && hit.and_then(|h| text_box(cx.frame, h)) == Some(self.node(p.unit)));
        let Some((caret, _)) = caret else { return self.clear() };
        let grain = match clicks {
            1 => Grain::Char,
            2 => Grain::Word,
            _ => Grain::Block,
        };
        let (lo, hi) = self.grain_at(cx, caret, grain);
        let (lo, hi) = (self.endpoint(cx.frame, lo), self.endpoint(cx.frame, hi));
        if let Some(sel) = self.sel.filter(|_| shift) {
            // Shift+click: the selection keeps its anchor and reaches the click
            let anchor = sel.anchor;
            self.drag = Some(Drag { lo: anchor, hi: anchor, grain });
            return self.drag_to(cx, x, y);
        }
        self.drag = Some(Drag { lo, hi, grain });
        self.set(Some(Selection { anchor: lo, focus: hi }).filter(|s| s.anchor != s.focus))
    }

    /// The pointer moved with the button down. True when the highlight changed.
    pub(crate) fn drag_to(&mut self, cx: &mut Cx, x: f32, y: f32) -> bool {
        let Some(drag) = self.drag else { return false };
        self.ensure(cx.frame, cx.styles);
        let Some((caret, _)) = self.pick(cx, x, y) else { return false };
        let (Some(lo), Some(hi)) = (self.pos_of(cx.frame, drag.lo), self.pos_of(cx.frame, drag.hi)) else {
            return false;
        };
        let (glo, ghi) = self.grain_at(cx, caret, drag.grain);
        // the first choice stays selected, the focus is the far side of where the pointer is
        let (anchor, focus) = if glo < lo { (hi, glo) } else { (lo, ghi) };
        let s = Selection { anchor: self.endpoint(cx.frame, anchor), focus: self.endpoint(cx.frame, focus) };
        let s = (s.anchor != s.focus).then_some(s);
        self.dragged |= s.is_some();
        self.set(s)
    }

    /// The button went up. True when the press selected text: it is not a click.
    pub(crate) fn release(&mut self) -> bool {
        self.drag = None;
        std::mem::take(&mut self.dragged)
    }

    /// Presses in a row near each other: 1, 2, 3 (a fourth stays a block).
    fn count_click(&mut self, x: f32, y: f32) -> u8 {
        let now = Instant::now();
        let n = match self.click {
            Some((t, cx, cy, n)) if now - t < MULTI_CLICK && (x - cx).abs() <= MULTI_CLICK_PX
                && (y - cy).abs() <= MULTI_CLICK_PX => (n + 1).min(3),
            _ => 1,
        };
        self.click = Some((now, x, y, n));
        n
    }

    // -- finding text ---------------------------------------------------------------------------

    /// The place in the text nearest (x, y) and whether (x, y) is on a text box.
    /// On a line: the character boundary nearest x. Above every line: the start
    /// of the nearest one; below: its end.
    fn pick(&self, cx: &mut Cx, x: f32, y: f32) -> Option<(Pos, bool)> {
        let f = cx.frame;
        let node = |u: usize| &f.nodes[self.units[u] as usize];
        let styles = cx.styles;
        let usable = |u: usize| !matches!(node(u).content, Content::LineBreak) && styles[node(u).style].pointer_events;
        let dy = |u: usize| span_distance(y, node(u).bounds.origin.y, node(u).bounds.origin.y + node(u).bounds.size.height);
        let units = 0..self.units.len();
        let near = units.clone().filter(|&u| usable(u)).map(dy).min_by(f32::total_cmp)?;
        // the boxes on the nearest line, in document order
        let tied: Vec<usize> = units.filter(|&u| usable(u) && dy(u) <= near + 0.5).collect();
        let len = |u: usize| f.text(self.units[u] as usize).len() as u32;
        if near == 0.0 {
            // on a line: the box nearest across it
            let across = |u: usize| span_distance(x, node(u).bounds.origin.x, node(u).bounds.origin.x + node(u).bounds.size.width);
            let u = tied.iter().copied().min_by(|&a, &b| across(a).total_cmp(&across(b)))?;
            let n = node(u);
            let inside = across(u) == 0.0 && n.clip.is_none_or(|c| c.contains(x, y));
            let off = if across(u) == 0.0 { self.offset_in(cx, u, x, y) } else if x < n.bounds.origin.x { 0 } else { len(u) };
            return Some((Pos { unit: u, off }, inside));
        }
        let above = y < node(tied[0]).bounds.origin.y;
        let u = if above { tied[0] } else { tied[tied.len() - 1] };
        Some((Pos { unit: u, off: if above { 0 } else { len(u) } }, false))
    }

    /// The byte offset in unit `u` nearest the point (x, y) on it.
    fn offset_in(&self, cx: &mut Cx, u: usize, x: f32, y: f32) -> u32 {
        let i = self.units[u] as usize;
        let n = &cx.frame.nodes[i];
        let c = &cx.styles[n.style];
        match &n.content {
            Content::Text(t) => {
                let mut best = (f32::MAX, 0usize);
                each_run_line(cx.text, c, t, n.bounds, true, |tc, line| {
                    let d = span_distance(y, line.y, line.y + line.lh);
                    if d < best.0 {
                        best = (d, line.at + offset_at(tc, c, line.text, x - line.x));
                    }
                });
                best.1 as u32
            }
            _ => offset_at(cx.text, c, cx.frame.text(i), x - n.bounds.origin.x) as u32,
        }
    }

    /// The range of `grain` around `p`: the unit's character, word or block; the
    /// whole element where `user-select: all` is set.
    fn grain_at(&self, cx: &Cx, p: Pos, grain: Grain) -> (Pos, Pos) {
        let f = cx.frame;
        let i = self.node(p.unit);
        if let Some(root) = all_root(f, cx.styles, i) {
            let inside = |u: usize| within(f, self.node(u), root);
            let (mut a, mut b) = (p.unit, p.unit);
            while a > 0 && inside(a - 1) {
                a -= 1;
            }
            while b + 1 < self.units.len() && inside(b + 1) {
                b += 1;
            }
            return (Pos { unit: a, off: 0 }, Pos { unit: b, off: f.text(self.node(b)).len() as u32 });
        }
        match grain {
            Grain::Char => (p, p),
            Grain::Word => {
                let (a, b) = word_at(f.text(i), p.off as usize);
                (Pos { unit: p.unit, off: a as u32 }, Pos { unit: p.unit, off: b as u32 })
            }
            Grain::Block => {
                let block = block_of(f, cx.styles, i);
                let same = |u: usize| block_of(f, cx.styles, self.node(u)) == block;
                let (mut a, mut b) = (p.unit, p.unit);
                while a > 0 && same(a - 1) {
                    a -= 1;
                }
                while b + 1 < self.units.len() && same(b + 1) {
                    b += 1;
                }
                let end = f.text(self.node(b)).len() as u32;
                (Pos { unit: a, off: 0 }, Pos { unit: b, off: end })
            }
        }
    }
}

/// The text box that a pointer over box `i` is over: a word or text run, or
/// the text folded into an element's own box.
fn text_box(f: &Frame, i: usize) -> Option<usize> {
    let n = &f.nodes[i];
    match n.content {
        Content::Word { .. } => Some(i),
        Content::Text(_) if n.taffy.is_some() => Some(i),
        _ => n.fold,
    }
}

/// Whether (x, y) is over drawn text of box `i` that can be selected (the
/// I-beam cursor): a word, or a glyph run of a text box (not the blank part
/// of its lines).
pub(crate) fn is_text(cx: &mut Cx, i: usize, x: f32, y: f32) -> bool {
    let Some(t) = text_box(cx.frame, i) else { return false };
    let n = &cx.frame.nodes[t];
    let c = &cx.styles[n.style];
    if c.user_select == UserSelect::None || !n.bounds.contains(x, y) {
        return false;
    }
    match &n.content {
        Content::Word { .. } => !cx.frame.text(t).is_empty(),
        Content::Text(text) => {
            let mut over = false;
            each_run_line(cx.text, c, text, n.bounds, false, |_, line| {
                over |= x >= line.x && x <= line.x + line.w && y >= line.y && y <= line.y + line.lh;
            });
            over
        }
        _ => false,
    }
}

/// The distance from `v` to the span `lo..hi` (0 inside it).
fn span_distance(v: f32, lo: f32, hi: f32) -> f32 {
    if v < lo { lo - v } else if v > hi { v - hi } else { 0.0 }
}

/// `at` moved back to a character boundary of `s` (and into it).
fn floor(s: &str, mut at: usize) -> usize {
    at = at.min(s.len());
    while !s.is_char_boundary(at) {
        at -= 1;
    }
    at
}

/// Whether whitespace follows word box `i` in its text (it ends a word that a
/// space separates from the next, which the box does not hold).
fn space_after(f: &Frame, i: usize) -> bool {
    let Content::Word { src, end, .. } = f.nodes[i].content else { return false };
    match &f.nodes[src as usize].content {
        Content::Text(t) => t[end as usize..].starts_with(char::is_whitespace),
        _ => false,
    }
}

/// The box whose lines box `i` is on: the nearest ancestor that is not inline
/// (a text run in a flex / grid item is its own).
fn block_of(f: &Frame, st: &StyleStore, i: usize) -> usize {
    let n = &f.nodes[i];
    if matches!(n.content, Content::Text(_)) && n.taffy.is_some() {
        return i;
    }
    let mut cur = n.parent.unwrap_or(i);
    while f.nodes[cur].taffy.is_none() || st[f.nodes[cur].style].display.is_inline_level()
        || st[f.nodes[cur].style].display == DisplayKind::Contents {
        match f.nodes[cur].parent {
            Some(p) => cur = p,
            None => break,
        }
    }
    cur
}

/// The outermost element at or above box `i` with `user-select: all`, when
/// `i` is in one.
fn all_root(f: &Frame, st: &StyleStore, i: usize) -> Option<usize> {
    if st[f.nodes[i].style].user_select != UserSelect::All {
        return None;
    }
    let mut cur = f.nodes[i].owner;
    if cur == i {
        cur = f.nodes[i].parent?;
    }
    while let Some(p) = f.nodes[cur].parent {
        if st[f.nodes[p].style].user_select != UserSelect::All {
            break;
        }
        cur = p;
    }
    Some(cur)
}

/// Is box `i` inside box `root` (or `root` itself)?
fn within(f: &Frame, mut i: usize, root: usize) -> bool {
    loop {
        if i == root {
            return true;
        }
        match f.nodes[i].parent {
            Some(p) => i = p,
            None => return false,
        }
    }
}

#[derive(PartialEq)]
enum Class {
    Word,
    Space,
    Other,
}

fn class(c: char) -> Class {
    if c.is_alphanumeric() || c == '_' {
        Class::Word
    } else if c.is_whitespace() {
        Class::Space
    } else {
        Class::Other
    }
}

/// The word around byte `off` of `s`: the run of letters and digits, of
/// spaces or of punctuation that holds the character there (the one before
/// at the end of the text), as Blender's `BLI_str_cursor_step_bounds_utf8`
/// takes delimiters apart.
fn word_at(s: &str, off: usize) -> (usize, usize) {
    let off = floor(s, off);
    let Some((at, ch)) = s[off..].chars().next().map(|c| (off, c)).or_else(|| s.char_indices().next_back()) else {
        return (0, 0);
    };
    let k = class(ch);
    let start = s[..at].char_indices().rev().take_while(|&(_, c)| class(c) == k).last().map_or(at, |(b, _)| b);
    let end = s[at..].char_indices().find(|&(_, c)| class(c) != k).map_or(s.len(), |(b, _)| at + b);
    (start, end)
}

/// The width of the first `upto` bytes of `s` as paint draws them.
pub(crate) fn advance(tc: &mut TextCache, c: &Computed, s: &str, upto: usize) -> f32 {
    let s = &s[..floor(s, upto)];
    let flags = text::flags(c);
    let size = if c.small_caps { c.font_size * 0.8 } else { c.font_size };
    if c.letter_spacing != 0.0 {
        // drawn a character at a time, each followed by the spacing
        let mut buf = [0u8; 4];
        return s.chars().map(|ch| tc.width(ch.encode_utf8(&mut buf), size, flags) + c.letter_spacing).sum();
    }
    if c.small_caps {
        return tc.width(&s.to_uppercase(), size, flags);
    }
    tc.width(s, size, flags)
}

/// The character boundary of `line` nearest `px` from its start.
fn offset_at(tc: &mut TextCache, c: &Computed, line: &str, px: f32) -> usize {
    if px <= 0.0 || line.is_empty() {
        return 0;
    }
    let bounds: Vec<usize> = line.char_indices().map(|(b, _)| b).chain(std::iter::once(line.len())).collect();
    // the first boundary at or past px (widths only grow)
    let (mut lo, mut hi) = (0, bounds.len() - 1);
    while lo < hi {
        let mid = (lo + hi) / 2;
        if advance(tc, c, line, bounds[mid]) < px {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if lo > 0 {
        let (before, after) = (advance(tc, c, line, bounds[lo - 1]), advance(tc, c, line, bounds[lo]));
        if px - before < after - px {
            lo -= 1;
        }
    }
    bounds[lo]
}

#[cfg(test)]
mod tests {
    use super::{class, word_at, Class};

    #[test]
    fn words_split_on_delimiters() {
        assert_eq!(word_at("hello, world", 1), (0, 5));
        assert_eq!(word_at("hello, world", 5), (5, 6));
        assert_eq!(word_at("hello, world", 12), (7, 12));
        assert_eq!(word_at("a  b", 2), (1, 3));
        // multi-byte: boundaries are characters
        assert_eq!(word_at("h\u{e9}llo w", 2), (0, 6));
        assert_eq!(word_at("", 0), (0, 0));
        assert!(class('_') == Class::Word);
    }
}
