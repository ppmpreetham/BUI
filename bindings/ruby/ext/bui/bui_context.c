/* BUI for Ruby: BUI::Context (shows a tree: layout, input, rendering, the
 * kit's callbacks) and BUI::Raster (a software frame without a window).
 *
 * Every event the kit emits goes to BUI.dispatch (lib/bui.rb), which makes the
 * BUI::Event and calls the node's handlers and the context's.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "bui_ext.h"

#include <stdlib.h>

VALUE bui_cContext;
VALUE bui_cRaster;

static ID id_dispatch, id_request;

/* The kit's BK_EV_* as Symbols, in the enum's order. */
static const char *const k_event_names[] = {
    NULL,     "change", "click",  "call",   "rename", "activate", "toggle",
    "browse", "press",  "release", "double", "motion", "enter",   "leave",
    "key",    "wheel",  "submit", "view",   "scroll", "configure", "key_release",
};
#define EVENT_KINDS ((int)(sizeof(k_event_names) / sizeof(k_event_names[0])))
/* a new BK_EV_* needs its name here */
_Static_assert(BK_EV_KEYRELEASE == EVENT_KINDS - 1, "an event kind without a name");
static ID g_event_ids[EVENT_KINDS];

/* -------------------------------------------------------------------- */
/** \name Contexts
 * \{ */

typedef struct Context {
  BKContext *ctx;
  uintptr_t id; /* the host data: this wrapper, kept while the context lives */
  int windows;  /* windows showing it: it outlives them */
} Context;

static size_t context_size(const void *p)
{
  return sizeof(Context);
}

static const rb_data_type_t context_type = {
    .wrap_struct_name = "BUI::Context",
    .function = {.dfree = RUBY_TYPED_DEFAULT_FREE, .dsize = context_size},
    .flags = RUBY_TYPED_FREE_IMMEDIATELY | RUBY_TYPED_WB_PROTECTED,
};

static VALUE context_alloc(VALUE klass)
{
  Context *c;
  return TypedData_Make_Struct(klass, Context, &context_type, c);
}

BKContext *bui_context_ptr(VALUE v)
{
  const Context *c = (const Context *)rb_check_typeddata(v, &context_type);
  if (!c->ctx) {
    rb_raise(bui_eFreed, "the context was freed");
  }
  return c->ctx;
}

void bui_context_windows(VALUE v, int delta)
{
  ((Context *)rb_check_typeddata(v, &context_type))->windows += delta;
}

typedef struct Emit {
  uintptr_t ctx;
  BKNode *node;
  int kind;
  void *ref;
  double a, b;
} Emit;

static VALUE emit_body(VALUE p)
{
  const Emit *e = (const Emit *)p;
  VALUE ctx = bui_ref_get(e->ctx);
  VALUE node = bui_node_wrap(e->node);
  VALUE kind = e->kind > 0 && e->kind < EVENT_KINDS ? ID2SYM(g_event_ids[e->kind]) : INT2NUM(e->kind);
  VALUE text = Qnil, extra = Qnil;
  switch (e->kind) {
    case BK_EV_KEY:
    case BK_EV_KEYRELEASE:
      text = bui_str((const char *)e->ref); /* "text\tkeysym" */
      break;
    case BK_EV_RENAME:
      text = e->node ? bui_str(BK_get_str(e->node, "edit")) : Qnil;
      break;
    case BK_EV_CALL:
    case BK_EV_TOGGLE: {
      /* the menu item's or binding's action, the toggled tree item */
      const VALUE v = bui_ref_get((uintptr_t)e->ref);
      extra = v == Qundef ? Qnil : v;
      break;
    }
    default:
      break;
  }
  VALUE args[7] = {ctx == Qundef ? Qnil : ctx, node, kind, DBL2NUM(e->a), DBL2NUM(e->b), text, extra};
  return rb_funcallv(bui_mBUI, id_dispatch, 7, args);
}

static void host_emit(void *data, BKNode *node, int kind, void *ref, double a, double b)
{
  Emit e = {(uintptr_t)data, node, kind, ref, a, b};
  bui_callback(emit_body, &e);
}

static VALUE request_body(VALUE p)
{
  const VALUE ctx = bui_ref_get((uintptr_t)p);
  return ctx == Qundef ? Qnil : rb_funcall(ctx, id_request, 0);
}

static void host_request(void *data)
{
  bui_callback(request_body, data);
}

/* BUI::Context.new(root): shows root (a tree of BUI::Nodes). */
static VALUE context_initialize(VALUE self, VALUE root)
{
  Context *c = (Context *)rb_check_typeddata(self, &context_type);
  if (c->ctx) {
    rb_raise(rb_eRuntimeError, "the context is made already");
  }
  BKNode *r = bui_node_ptr(root);
  c->ctx = BK_context_new(r);
  c->id = bui_ref_new(self, BUI_REF_PLAIN);
  bui_ref_retain((void *)c->id);
  BKHost host;
  memset(&host, 0, sizeof(host));
  host.data = (void *)c->id;
  host.emit = host_emit;
  host.request = host_request;
  BK_context_set_host(c->ctx, &host);
  return self;
}

/* Frees the context (not its tree; key bindings go with it). A second free does nothing. */
static VALUE context_free(VALUE self)
{
  Context *c = (Context *)rb_check_typeddata(self, &context_type);
  if (c->windows > 0) {
    rb_raise(bui_eError, "a window shows the context: free the window first");
  }
  if (c->ctx) {
    BK_context_free(c->ctx);
    c->ctx = NULL;
    bui_ref_release((void *)c->id);
  }
  return Qnil;
}

static VALUE context_freed_p(VALUE self)
{
  return ((const Context *)rb_check_typeddata(self, &context_type))->ctx ? Qfalse : Qtrue;
}

static VALUE context_root(VALUE self)
{
  return bui_node_wrap(BK_context_root(bui_context_ptr(self)));
}

/* header = node or nil: a fixed bar over the tree. */
static VALUE context_set_header(VALUE self, VALUE header)
{
  BKContext *ctx = bui_context_ptr(self);
  BK_context_set_header(ctx, bui_node_ptr_opt(header));
  return header;
}

/* render(raster, width = raster.width, height = raster.height, full: false):
 * draws what changed (everything with full: true), resizing the raster to
 * width x height; returns the number of regions drawn (0: nothing changed). */
static VALUE context_render(int argc, VALUE *argv, VALUE self)
{
  VALUE raster, width, height, opts;
  rb_scan_args(argc, argv, "12:", &raster, &width, &height, &opts);
  BKContext *ctx = bui_context_ptr(self);
  BuiRaster *r = bui_raster_ptr(raster);
  const int w = NIL_P(width) ? BUI_raster_width(r) : NUM2INT(width);
  const int h = NIL_P(height) ? BUI_raster_height(r) : NUM2INT(height);
  if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
    rb_raise(rb_eArgError, "a frame of %d x %d (1 to 16384 each)", w, h);
  }
  const int n = BK_render(ctx, r, w, h, RTEST(bui_opt(opts, "full")));
  bui_settle();
  return INT2NUM(n);
}

/* configure(width, height): lays out for a window of that size without drawing. */
static VALUE context_configure(VALUE self, VALUE width, VALUE height)
{
  const int changed = BK_configure(bui_context_ptr(self), NUM2INT(width), NUM2INT(height));
  bui_raise_pending();
  return changed ? Qtrue : Qfalse;
}

static VALUE context_needs_render_p(VALUE self)
{
  return BK_needs_render(bui_context_ptr(self)) ? Qtrue : Qfalse;
}

static VALUE context_animating_p(VALUE self)
{
  return BK_animating(bui_context_ptr(self)) ? Qtrue : Qfalse;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Input: each returns whether something changed
 * \{ */

static int mods_of(VALUE mods)
{
  return NIL_P(mods) ? 0 : NUM2INT(mods);
}

static VALUE changed(int c)
{
  bui_settle();
  return c ? Qtrue : Qfalse;
}

#define POINTER_INPUT(name, call) \
  static VALUE context_##name(int argc, VALUE *argv, VALUE self) \
  { \
    VALUE x, y, mods; \
    rb_scan_args(argc, argv, "21", &x, &y, &mods); \
    BKContext *ctx = bui_context_ptr(self); \
    return changed(call(ctx, NUM2INT(x), NUM2INT(y), mods_of(mods))); \
  }

/* press(x, y, mods = 0), release, motion, context_menu: the left button, the pointer */
POINTER_INPUT(press, BK_press)
POINTER_INPUT(release, BK_release)
POINTER_INPUT(motion, BK_motion)
POINTER_INPUT(context_menu, BK_context_menu)

/* click(x, y, mods = 0): the pointer moves there, presses and releases. */
static VALUE context_click(int argc, VALUE *argv, VALUE self)
{
  VALUE x, y, mods;
  rb_scan_args(argc, argv, "21", &x, &y, &mods);
  BKContext *ctx = bui_context_ptr(self);
  const int px = NUM2INT(x), py = NUM2INT(y), m = mods_of(mods);
  int c = BK_motion(ctx, px, py, m);
  c |= BK_press(ctx, px, py, m);
  c |= BK_release(ctx, px, py, m);
  return changed(c);
}

/* wheel(x, y, notches, mods = 0) */
static VALUE context_wheel(int argc, VALUE *argv, VALUE self)
{
  VALUE x, y, delta, mods;
  rb_scan_args(argc, argv, "31", &x, &y, &delta, &mods);
  BKContext *ctx = bui_context_ptr(self);
  return changed(BK_wheel(ctx, NUM2INT(x), NUM2INT(y), NUM2INT(delta), mods_of(mods)));
}

/* scroll(x, y, dx, dy, mods = 0): precise, in notches; dx > 0 right, dy > 0 up. */
static VALUE context_scroll(int argc, VALUE *argv, VALUE self)
{
  VALUE x, y, dx, dy, mods;
  rb_scan_args(argc, argv, "41", &x, &y, &dx, &dy, &mods);
  BKContext *ctx = bui_context_ptr(self);
  return changed(BK_scroll(
      ctx, NUM2INT(x), NUM2INT(y), (float)NUM2DBL(dx), (float)NUM2DBL(dy), mods_of(mods)));
}

/* key(text, keysym = nil, mods = 0): a key typing text ("" for none), with
 * its X11 keysym ("Return", "Up", "a"). */
static VALUE context_key(int argc, VALUE *argv, VALUE self)
{
  VALUE text, keysym, mods;
  rb_scan_args(argc, argv, "12", &text, &keysym, &mods);
  BKContext *ctx = bui_context_ptr(self);
  const int m = mods_of(mods);
  const char *k = bui_cstr_opt(keysym);
  const char *t = bui_cstr(text);
  return changed(BK_key(ctx, t, k, m));
}

static VALUE context_key_release(int argc, VALUE *argv, VALUE self)
{
  VALUE keysym, mods;
  rb_scan_args(argc, argv, "11", &keysym, &mods);
  BKContext *ctx = bui_context_ptr(self);
  const int m = mods_of(mods);
  return changed(BK_key_release(ctx, bui_cstr(keysym), m));
}

/* button(x, y, button, release, mods = 0): buttons 1 to 3, raw events only. */
static VALUE context_button(int argc, VALUE *argv, VALUE self)
{
  VALUE x, y, button, release, mods;
  rb_scan_args(argc, argv, "41", &x, &y, &button, &release, &mods);
  BKContext *ctx = bui_context_ptr(self);
  const int b = NUM2INT(button);
  if (b < 1 || b > 3) {
    rb_raise(rb_eArgError, "button 1, 2 or 3, not %d", b);
  }
  return changed(BK_button(ctx, NUM2INT(x), NUM2INT(y), b, RTEST(release), mods_of(mods)));
}

/* timer(id): fires a timer the context scheduled (hosts without a window). */
static VALUE context_timer(VALUE self, VALUE id)
{
  BK_timer(bui_context_ptr(self), NUM2INT(id));
  bui_raise_pending();
  return self;
}

/* bind("ctrl+s") { ... }: runs the block for a key combination; false when
 * the context holds no more bindings. */
static VALUE context_bind(VALUE self, VALUE combo)
{
  rb_need_block();
  BKContext *ctx = bui_context_ptr(self);
  bui_cstr(combo); /* checked before anything is made */
  const uintptr_t id = bui_ref_new(rb_block_proc(), BUI_REF_PLAIN);
  BK_bind(ctx, bui_cstr(combo), (void *)id); /* the kit retains what it keeps */
  bui_ref_remove_unused(id);
  return bui_ref_get(id) != Qundef ? Qtrue : Qfalse;
}

static VALUE context_close_popups(VALUE self)
{
  BK_close_popups(bui_context_ptr(self));
  bui_raise_pending();
  return self;
}

/* set_pointer(x, y): moves the pointer without an event. */
static VALUE context_set_pointer(VALUE self, VALUE x, VALUE y)
{
  BK_set_pointer(bui_context_ptr(self), NUM2INT(x), NUM2INT(y));
  return self;
}

static VALUE context_undo(VALUE self)
{
  return changed(BK_undo(bui_context_ptr(self)));
}

static VALUE context_redo(VALUE self)
{
  return changed(BK_redo(bui_context_ptr(self)));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Introspection
 * \{ */

/* hit_at(x, y): the interactive node there, nil over nothing. */
static VALUE context_hit_at(VALUE self, VALUE x, VALUE y)
{
  return bui_node_wrap(BK_hit_at(bui_context_ptr(self), NUM2INT(x), NUM2INT(y)));
}

static VALUE context_hover(VALUE self)
{
  return bui_node_wrap(BK_hover(bui_context_ptr(self)));
}

static VALUE context_focus(VALUE self)
{
  return bui_node_wrap(BK_focus(bui_context_ptr(self)));
}

/* focus = node: a text field starts editing (nil ends the edit). */
static VALUE context_set_focus(VALUE self, VALUE node)
{
  BKContext *ctx = bui_context_ptr(self);
  BK_focus_node(ctx, bui_node_ptr_opt(node));
  bui_raise_pending();
  return node;
}

static VALUE context_cursor(VALUE self)
{
  return INT2NUM(BK_cursor(bui_context_ptr(self)));
}

static VALUE context_modal_p(VALUE self)
{
  return BK_modal_active(bui_context_ptr(self)) ? Qtrue : Qfalse;
}

static VALUE context_tooltip_p(VALUE self)
{
  return BK_tooltip_visible(bui_context_ptr(self)) ? Qtrue : Qfalse;
}

/* The open menus and popups, topmost last. */
static VALUE context_popups(VALUE self)
{
  BKContext *ctx = bui_context_ptr(self);
  const int n = BK_popup_count(ctx);
  VALUE out = rb_ary_new_capa(n);
  for (int i = 0; i < n; i++) {
    rb_ary_push(out, bui_node_wrap(BK_popup(ctx, i)));
  }
  return out;
}

static VALUE context_clipboard(VALUE self)
{
  char *text = BK_clipboard_get(bui_context_ptr(self));
  if (!text) {
    return Qnil;
  }
  VALUE out = bui_str(text);
  free(text);
  return out;
}

static VALUE context_set_clipboard(VALUE self, VALUE text)
{
  BKContext *ctx = bui_context_ptr(self);
  BK_clipboard_set(ctx, bui_cstr(text));
  return text;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Menus opened in place
 * \{ */

typedef struct OpenMenu {
  BKContext *ctx;
  BKNode *owner;
  VALUE title;
  int x, y, pie;
} OpenMenu;

static void open_menu(const BKMenuSpec *items, int n, void *data)
{
  const OpenMenu *m = (const OpenMenu *)data;
  const char *title = bui_cstr_opt(m->title);
  if (m->pie) {
    BK_open_pie(m->ctx, items, n, title, m->x, m->y);
  }
  else {
    BK_open_menu(m->ctx, m->owner, items, n, title, m->x, m->y);
  }
}

static void open_menu_call(int argc, VALUE *argv, VALUE self, int pie)
{
  VALUE items, opts;
  rb_scan_args(argc, argv, "1:", &items, &opts);
  OpenMenu m = {bui_context_ptr(self), NULL, Qnil, -100000, -100000, pie};
  VALUE v;
  m.owner = bui_node_ptr_opt(bui_opt(opts, "owner"));
  m.title = bui_opt(opts, "title");
  bui_cstr_opt(m.title); /* checked before the menu is built */
  if (!NIL_P(v = bui_opt(opts, "x"))) {
    m.x = NUM2INT(v);
  }
  if (!NIL_P(v = bui_opt(opts, "y"))) {
    m.y = NUM2INT(v);
  }
  if (!pie && (m.x == -100000 || m.y == -100000)) {
    rb_raise(rb_eArgError, "a menu opens at x: and y:");
  }
  bui_with_menu(items, open_menu, &m);
  RB_GC_GUARD(opts);
  bui_raise_pending();
}

/* open_menu(items, x:, y:, owner: nil, title: nil): items as Node#menu= takes them. */
static VALUE context_open_menu(int argc, VALUE *argv, VALUE self)
{
  open_menu_call(argc, argv, self, 0);
  return self;
}

/* open_pie(items, x: pointer, y: pointer, title: nil) */
static VALUE context_open_pie(int argc, VALUE *argv, VALUE self)
{
  open_menu_call(argc, argv, self, 1);
  return self;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Rasters
 * \{ */

static void raster_free(void *p)
{
  if (p) {
    BUI_raster_free((BuiRaster *)p);
  }
}

static size_t raster_size(const void *p)
{
  const BuiRaster *r = (const BuiRaster *)p;
  return r ? (size_t)BUI_raster_width(r) * (size_t)BUI_raster_height(r) * 4 : 0;
}

static const rb_data_type_t raster_type = {
    .wrap_struct_name = "BUI::Raster",
    .function = {.dfree = raster_free, .dsize = raster_size},
    .flags = RUBY_TYPED_FREE_IMMEDIATELY | RUBY_TYPED_WB_PROTECTED,
};

static VALUE raster_alloc(VALUE klass)
{
  return TypedData_Wrap_Struct(klass, &raster_type, NULL);
}

BuiRaster *bui_raster_ptr(VALUE v)
{
  BuiRaster *r = (BuiRaster *)rb_check_typeddata(v, &raster_type);
  if (!r) {
    rb_raise(bui_eFreed, "the raster was freed");
  }
  return r;
}

/* BUI::Raster.new(width, height): a frame to render into. The GC frees it; free frees it now. */
static VALUE raster_initialize(VALUE self, VALUE width, VALUE height)
{
  if (RTYPEDDATA_DATA(self)) {
    rb_raise(rb_eRuntimeError, "the raster is made already");
  }
  const int w = NUM2INT(width), h = NUM2INT(height);
  if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
    rb_raise(rb_eArgError, "a raster of %d x %d (1 to 16384 each)", w, h);
  }
  RTYPEDDATA_DATA(self) = BUI_raster_create(w, h);
  return self;
}

static VALUE raster_free_now(VALUE self)
{
  BuiRaster *r = (BuiRaster *)rb_check_typeddata(self, &raster_type);
  if (r) {
    RTYPEDDATA_DATA(self) = NULL;
    BUI_raster_free(r);
  }
  return Qnil;
}

static VALUE raster_width(VALUE self)
{
  return INT2NUM(BUI_raster_width(bui_raster_ptr(self)));
}

static VALUE raster_height(VALUE self)
{
  return INT2NUM(BUI_raster_height(bui_raster_ptr(self)));
}

/* The frame as straight RGBA bytes, row by row (a binary String, a copy). */
static VALUE raster_rgba(VALUE self)
{
  const BuiRaster *r = bui_raster_ptr(self);
  const long n = (long)BUI_raster_width(r) * BUI_raster_height(r);
  const uint32_t *px = BUI_raster_pixels(r);
  VALUE out = rb_str_new(NULL, n * 4);
  unsigned char *dst = (unsigned char *)RSTRING_PTR(out);
  for (long i = 0; i < n; i++) {
    const uint32_t p = px ? px[i] : 0;
    dst[i * 4 + 0] = (unsigned char)(p >> 16);
    dst[i * 4 + 1] = (unsigned char)(p >> 8);
    dst[i * 4 + 2] = (unsigned char)p;
    dst[i * 4 + 3] = 255; /* the frame is opaque (0x00RRGGBB) */
  }
  return out;
}

/* The pixel at (x, y) as 0xRRGGBB. */
static VALUE raster_pixel(VALUE self, VALUE x, VALUE y)
{
  const BuiRaster *r = bui_raster_ptr(self);
  const int px = NUM2INT(x), py = NUM2INT(y);
  const int w = BUI_raster_width(r), h = BUI_raster_height(r);
  if (px < 0 || py < 0 || px >= w || py >= h) {
    rb_raise(rb_eIndexError, "(%d, %d) outside the %d x %d frame", px, py, w, h);
  }
  const uint32_t *pixels = BUI_raster_pixels(r);
  return UINT2NUM(pixels ? pixels[(long)py * w + px] & 0xffffff : 0);
}

/** \} */

void bui_init_context(void)
{
  id_dispatch = rb_intern("dispatch");
  id_request = rb_intern("request_render");
  VALUE kinds = rb_ary_new_capa(EVENT_KINDS - 1);
  for (int i = 1; i < EVENT_KINDS; i++) {
    g_event_ids[i] = rb_intern(k_event_names[i]);
    rb_ary_push(kinds, ID2SYM(g_event_ids[i]));
  }
  /* the event kinds Node#on takes, as Symbols */
  rb_define_const(bui_mBUI, "EVENT_KINDS", rb_ary_freeze(kinds));

  rb_define_const(bui_mBUI, "SHIFT", INT2NUM(BK_SHIFT));
  rb_define_const(bui_mBUI, "CTRL", INT2NUM(BK_CTRL));
  rb_define_const(bui_mBUI, "ALT", INT2NUM(BK_ALT));

  bui_cContext = rb_define_class_under(bui_mBUI, "Context", rb_cObject);
  rb_define_alloc_func(bui_cContext, context_alloc);
  rb_define_method(bui_cContext, "initialize", context_initialize, 1);
  rb_define_private_method(bui_cContext, "free_native", context_free, 0);
  rb_define_method(bui_cContext, "freed?", context_freed_p, 0);
  rb_define_method(bui_cContext, "root", context_root, 0);
  rb_define_method(bui_cContext, "header=", context_set_header, 1);
  rb_define_method(bui_cContext, "render", context_render, -1);
  rb_define_method(bui_cContext, "configure", context_configure, 2);
  rb_define_method(bui_cContext, "needs_render?", context_needs_render_p, 0);
  rb_define_method(bui_cContext, "animating?", context_animating_p, 0);
  rb_define_method(bui_cContext, "press", context_press, -1);
  rb_define_method(bui_cContext, "release", context_release, -1);
  rb_define_method(bui_cContext, "motion", context_motion, -1);
  rb_define_method(bui_cContext, "click", context_click, -1);
  rb_define_method(bui_cContext, "context_menu", context_context_menu, -1);
  rb_define_method(bui_cContext, "wheel", context_wheel, -1);
  rb_define_method(bui_cContext, "scroll", context_scroll, -1);
  rb_define_method(bui_cContext, "key", context_key, -1);
  rb_define_method(bui_cContext, "key_release", context_key_release, -1);
  rb_define_method(bui_cContext, "button", context_button, -1);
  rb_define_method(bui_cContext, "timer", context_timer, 1);
  rb_define_method(bui_cContext, "bind", context_bind, 1);
  rb_define_method(bui_cContext, "close_popups", context_close_popups, 0);
  rb_define_method(bui_cContext, "set_pointer", context_set_pointer, 2);
  rb_define_method(bui_cContext, "undo", context_undo, 0);
  rb_define_method(bui_cContext, "redo", context_redo, 0);
  rb_define_method(bui_cContext, "hit_at", context_hit_at, 2);
  rb_define_method(bui_cContext, "hover", context_hover, 0);
  rb_define_method(bui_cContext, "focus", context_focus, 0);
  rb_define_method(bui_cContext, "focus=", context_set_focus, 1);
  rb_define_method(bui_cContext, "cursor", context_cursor, 0);
  rb_define_method(bui_cContext, "modal_active?", context_modal_p, 0);
  rb_define_method(bui_cContext, "tooltip_visible?", context_tooltip_p, 0);
  rb_define_method(bui_cContext, "popups", context_popups, 0);
  rb_define_method(bui_cContext, "clipboard", context_clipboard, 0);
  rb_define_method(bui_cContext, "clipboard=", context_set_clipboard, 1);
  rb_define_method(bui_cContext, "open_menu", context_open_menu, -1);
  rb_define_method(bui_cContext, "open_pie", context_open_pie, -1);

  bui_cRaster = rb_define_class_under(bui_mBUI, "Raster", rb_cObject);
  rb_define_alloc_func(bui_cRaster, raster_alloc);
  rb_define_method(bui_cRaster, "initialize", raster_initialize, 2);
  rb_define_method(bui_cRaster, "free", raster_free_now, 0);
  rb_define_method(bui_cRaster, "width", raster_width, 0);
  rb_define_method(bui_cRaster, "height", raster_height, 0);
  rb_define_method(bui_cRaster, "rgba", raster_rgba, 0);
  rb_define_method(bui_cRaster, "pixel", raster_pixel, 2);
}
