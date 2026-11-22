/* BUI for Ruby, the C side: the extension `bui/bui_ext` over the kit's public C API
 * (BUI_kit.h). Everything Ruby-shaped (keyword options, the builder, event
 * objects) lives in lib/; this side converts values, keeps Ruby objects alive
 * while the kit holds them, and runs Ruby code from the kit's callbacks.
 *
 * Three rules hold everywhere here:
 *
 * - The kit holds Ruby objects only as small integer ids (`void *ref`, a
 *   context's host data, a window's callback data), never as addresses: the GC
 *   may move objects. The reference table (bui_ext.c) maps each id to its
 *   object and marks the object while the kit keeps a reference to it.
 * - Ruby code runs from the kit's callbacks only through bui_callback, which
 *   takes the GVL when the window loop gave it up and catches every exception.
 *   The first one is raised again when the kit call that led to it returns
 *   (bui_raise_pending), never across the kit's C frames.
 * - The kit belongs to one thread: the one that runs BUI.run. Other threads
 *   reach it through BUI.on_main.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <ruby.h>
#include <ruby/util.h> /* ruby_strdup */
#include <stdint.h>

#include "BUI_kit.h"

extern VALUE bui_mBUI;
extern VALUE bui_cNode;
extern VALUE bui_cContext;
extern VALUE bui_cRaster;
extern VALUE bui_cWindow;
extern VALUE bui_cImage;
extern VALUE bui_cTreeItem;
extern VALUE bui_eError;
extern VALUE bui_eFreed;

/* -- the reference table (bui_ext.c) ------------------------------------------ */

/* What becomes of an entry when the kit's last reference goes. */
typedef enum BuiRefKind {
  BUI_REF_PLAIN = 0, /* a proc, a context, a window: the entry goes */
  BUI_REF_NODE,      /* a node's wrapper: the node was freed, the wrapper lets go of it */
  BUI_REF_KEEP,      /* a tree item's wrapper: the entry stays until the GC frees the wrapper */
} BuiRefKind;

/* A new entry for `obj`, with no references yet: its id is what the kit holds. */
uintptr_t bui_ref_new(VALUE obj, BuiRefKind kind);
/* The object of an id, Qundef for an id not in the table. */
VALUE bui_ref_get(uintptr_t id);
/* Removes an entry (its object is being freed). */
void bui_ref_remove(uintptr_t id);
/* Removes an entry the kit kept no reference to (a key binding it refused). */
void bui_ref_remove_unused(uintptr_t id);
/* The binding's own reference, for ids the kit holds without retaining them (a
 * context's host data, a window's callback data). The kit's retain and release
 * hooks are these too. */
void bui_ref_retain(void *id);
void bui_ref_release(void *id);

/* -- running Ruby from the kit's callbacks (bui_ext.c) ------------------------- */

/* Runs fn(data) with the GVL and every exception caught; the first is kept for
 * bui_raise_pending, and a window loop running stops. */
void bui_callback(VALUE (*fn)(VALUE data), void *data);
/* The same for code the loop runs between messages (BUI.on_main's queue): it
 * does not count as inside a callback (BUI.in_callback?). */
void bui_callback_between(VALUE (*fn)(VALUE data), void *data);
/* Raises the exception a callback kept, if any. */
void bui_raise_pending(void);
/* After a headless kit call (input, a render): raises a callback's exception,
 * then, outside callbacks, runs what they left for later (BUI.drain_main: frees). */
void bui_settle(void);
/* Runs fn(arg) without the GVL (the window loop, a file dialog): other Ruby
 * threads run meanwhile, and an interrupt (Ctrl+C, Thread#raise) ends a
 * window loop. Raises a callback's exception afterwards. */
void bui_blocking(void *(*fn)(void *), void *arg);

/* -- arenas ----------------------------------------------------------------------
 * C copies of what a kit call reads from Ruby objects (menu items, canvas
 * items): converting one value may allocate, a GC may then move another
 * object's string, so nothing points into Ruby memory. bui_with_arena frees
 * everything after fn, even when fn raises. */
typedef struct BuiArena {
  void **ptrs;
  long n, cap;
} BuiArena;
void *bui_arena_alloc(BuiArena *a, size_t size); /* zeroed */
char *bui_arena_str(BuiArena *a, VALUE v);       /* a copy of bui_cstr(v); NULL for nil */
VALUE bui_with_arena(VALUE (*fn)(BuiArena *a, void *data), void *data);

/* -- conversions --------------------------------------------------------------- */

/* A String or Symbol as a C string; raises for anything else or an embedded NUL. */
const char *bui_cstr(VALUE v);
/* The same, NULL for nil. */
const char *bui_cstr_opt(VALUE v);
/* A UTF-8 String, nil for NULL. */
VALUE bui_str(const char *s);
/* An option of a keyword Hash by name; nil when absent (or no Hash). */
VALUE bui_opt(VALUE opts, const char *key);
/* A BUI::Color, or an Array of 3 or 4 numbers (alpha 1), as RGBA floats. */
void bui_rgba(VALUE v, float r_rgba[4]);
VALUE bui_color_new(const float rgba[4]);
VALUE bui_rect_new(const int r[4]);

/* -- the wrapped handles ------------------------------------------------------- */

/* The kit handle of a wrapper; raises BUI::FreedError once it was freed and
 * TypeError for another class. */
BKNode *bui_node_ptr(VALUE v);
BKNode *bui_node_ptr_opt(VALUE v); /* NULL for nil */
BKContext *bui_context_ptr(VALUE v);
/* A window starts (+1) or stops (-1) showing the context, which refuses to be freed before. */
void bui_context_windows(VALUE ctx, int delta);
BuiRaster *bui_raster_ptr(VALUE v);
const BKImBuf *bui_image_ptr(VALUE v);
BKTreeItem *bui_tree_item_ptr(VALUE v);
/* The wrapper of a kit node, made on first sight (a popup, a panel's body); nil for NULL. */
VALUE bui_node_wrap(BKNode *n);
/* The release hook's part for node wrappers: the node was freed. */
void bui_node_forget(VALUE wrapper);
/* The wrapper of a tree item (made from Ruby), nil for NULL or one Ruby did not make. */
VALUE bui_tree_item_wrap(BKTreeItem *item);

/* Builds the kit's menu items from Ruby's (an Array of Hashes, nil or "-" for a
 * separator) and calls fn with them; the memory is freed after fn, even when
 * converting or fn raises. */
void bui_with_menu(VALUE items, void (*fn)(const BKMenuSpec *items, int n, void *data), void *data);

void bui_init_node(void);
void bui_init_context(void);
void bui_init_window(void);
void bui_init_image(void);
