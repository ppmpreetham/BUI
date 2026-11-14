/* Multi-line text editing on a "textarea" node. Offsets are UTF-8 bytes.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
module bui.textarea;

import bui.internal.util : cstr;
import bui.kit;
import bui.node : Node, requireHandle;

/// A styled range: foreground and/or background color.
struct TextSpan {
    int start;
    int end;
    float[4] fg;
    float[4] bg;
    bool hasFg;
    bool hasBg;

    /// A foreground-only span.
    static TextSpan foreground(int start, int end, const float[4] rgba) {
        TextSpan span;
        span.start = start;
        span.end = end;
        span.fg[] = rgba[];
        span.hasFg = true;
        return span;
    }

    /// A background-only span.
    static TextSpan background(int start, int end, const float[4] rgba) {
        TextSpan span;
        span.start = start;
        span.end = end;
        span.bg[] = rgba[];
        span.hasBg = true;
        return span;
    }
}

/// Insert `text` at byte offset `offset`.
void insertText(Node textarea, int offset, string text) {
    BK_textarea_insert(requireHandle(textarea, "Textarea.insertText", "no textarea node"),
                       offset, cstr(text));
}

/// Delete bytes `start` up to (not including) `end`.
void deleteText(Node textarea, int start, int end) {
    BK_textarea_delete(requireHandle(textarea, "Textarea.deleteText", "no textarea node"),
                       start, end);
}

/// The caret byte offset.
int caret(Node textarea) {
    return BK_textarea_caret(requireHandle(textarea, "Textarea.caret", "no textarea node"));
}

/// Move the caret to byte offset `offset`.
void setCaret(Node textarea, int offset) {
    BK_textarea_set_caret(requireHandle(textarea, "Textarea.setCaret", "no textarea node"),
                          offset);
}

/// Selection start and end byte offsets.
int[2] selection(Node textarea) {
    int start, end;
    BK_textarea_selection(requireHandle(textarea, "Textarea.selection", "no textarea node"),
                          &start, &end);
    return [start, end];
}

/// Select bytes `start` up to `end`.
void setSelection(Node textarea, int start, int end) {
    BK_textarea_set_selection(requireHandle(textarea, "Textarea.setSelection",
                                            "no textarea node"), start, end);
}

/// Replace the styled ranges.
void setSpans(Node textarea, TextSpan[] spans) {
    auto raw = new BKTextSpan[spans.length];
    foreach (i, span; spans) {
        raw[i].start = span.start;
        raw[i].end = span.end;
        raw[i].fg[] = span.fg[];
        raw[i].bg[] = span.bg[];
        raw[i].has_fg = span.hasFg ? 1 : 0;
        raw[i].has_bg = span.hasBg ? 1 : 0;
    }
    BK_textarea_set_spans(requireHandle(textarea, "Textarea.setSpans", "no textarea node"),
                          raw.ptr, cast(int) raw.length);
}

/// Byte offset under window pixel `x`, `y`.
int indexAt(Node textarea, int x, int y) {
    return BK_textarea_index_at(requireHandle(textarea, "Textarea.indexAt", "no textarea node"),
                                x, y);
}

/// Scroll so byte offset `offset` is visible.
void see(Node textarea, int offset) {
    BK_textarea_see(requireHandle(textarea, "Textarea.see", "no textarea node"), offset);
}
