/* SPDX-License-Identifier: GPL-2.0-or-later */

/* Where the portable text engine (gpu_raster.c) finds fonts: the platform's
 * font folders, installed families by their own names (read from each
 * font's `name` table, so "Malgun Gothic" finds malgun.ttf and "Yu Gothic"
 * a face inside YuGothM.ttc), and the fonts that cover a script the
 * requested ones lack (per-glyph fallback). */

#pragma once

#define GPU_FONT_PATH 260
/* Room for every font folder of any platform (Linux: BUI_FONT_DIR, 14 system
 * folders and 2 of the user's). */
#define GPU_FONT_DIRS_MAX 20

/* The font folders, most specific first (BUI_FONT_DIR, then the platform's);
 * returns how many were written. */
int gpu_font_dirs(char dirs[][GPU_FONT_PATH], int max);

/* The installed face of `family` (case-insensitive, any of the font's own
 * family names) nearest `style` (BUI_FONT_BOLD | BUI_FONT_ITALIC): its file,
 * its index in a collection (.ttc), and the style bits it really has.
 * The index is read once (see gpu_font_index_warm); a lookup before it is
 * ready waits for it. 0 when no installed font has that family. */
int gpu_font_named(const char *family, int style, char r_path[GPU_FONT_PATH], int *r_index, int *r_have);

/* Starts reading the font folders (or the index saved from them, kept until a
 * folder changes) on a thread of its own; the text engine calls it when it
 * opens its first font, so a later lookup by name finds it ready. Once. */
void gpu_font_index_warm(void);

/* Fallback groups: the scripts a font is looked for by (each has its own
 * files, most wanted first); GPU_FALLBACK_ANY covers what the others don't. */
enum {
  GPU_FALLBACK_HANGUL,
  GPU_FALLBACK_KANA,
  GPU_FALLBACK_HAN,
  GPU_FALLBACK_THAI,
  GPU_FALLBACK_INDIC,
  GPU_FALLBACK_SYMBOLS,
  GPU_FALLBACK_ANY,
  GPU_FALLBACK_GROUPS,
};

/* The group of code point `cp`. */
int gpu_font_fallback_group(int cp);

/* The font files of `group`, '|'-separated (looked for in the font folders):
 * its bold faces when `bold`, else its regular ones. */
const char *gpu_font_fallback_files(int group, int bold);
