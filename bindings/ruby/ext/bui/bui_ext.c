/* BUI for Ruby: the extension's entry point, the reference table, callbacks
 * and the module functions (look, theme, icons, numbers, the file dialog).
 * bui_ext.h has the rules every file here follows.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "bui_ext.h"

#include <ruby/thread.h>

VALUE bui_mBUI;
VALUE bui_eError;
VALUE bui_eFreed;
static VALUE cColor;
static VALUE cRect;

/* -------------------------------------------------------------------- */
/** \name The reference table
 *
 * id -> {object, references the kit holds, kind}. An entry with references
 * marks its object, so the GC keeps it; ids are never reused, so a stale id
 * from the kit finds nothing.
 * \{ */

typedef struct BuiRef {
  VALUE obj;
  long count;
  BuiRefKind kind;
} BuiRef;

static st_table *g_refs;
static uintptr_t g_next_id = 1;

static int ref_mark(st_data_t id, st_data_t value, st_data_t arg)
{
  (void)id;
  (void)arg;
  const BuiRef *ref = (const BuiRef *)value;
  if (ref->count > 0) {
    rb_gc_mark_movable(ref->obj);
  }
  return ST_CONTINUE;
}

static int ref_move(st_data_t id, st_data_t value, st_data_t arg)
{
  (void)id;
  (void)arg;
  BuiRef *ref = (BuiRef *)value;
  ref->obj = rb_gc_location(ref->obj); /* unmarked entries' objects may move too */
  return ST_CONTINUE;
}

static void table_mark(void *p)
{
  (void)p;
  st_foreach(g_refs, ref_mark, 0);
}

static void table_compact(void *p)
{
  (void)p;
  st_foreach(g_refs, ref_move, 0);
}

static const rb_data_type_t table_type = {
    .wrap_struct_name = "BUI::References",
    .function = {.dmark = table_mark, .dcompact = table_compact},
    .flags = 0,
};

uintptr_t bui_ref_new(VALUE obj, BuiRefKind kind)
{
  BuiRef *ref = ALLOC(BuiRef);
  ref->obj = obj;
  ref->count = 0;
  ref->kind = kind;
  const uintptr_t id = g_next_id++;
  st_insert(g_refs, (st_data_t)id, (st_data_t)ref);
  return id;
}

VALUE bui_ref_get(uintptr_t id)
{
  st_data_t value;
  return st_lookup(g_refs, (st_data_t)id, &value) ? ((BuiRef *)value)->obj : Qundef;
}

void bui_ref_remove(uintptr_t id)
{
  st_data_t key = (st_data_t)id, value;
  if (st_delete(g_refs, &key, &value)) {
    xfree((BuiRef *)value);
  }
}

void bui_ref_remove_unused(uintptr_t id)
{
  st_data_t value;
  if (st_lookup(g_refs, (st_data_t)id, &value) && ((BuiRef *)value)->count == 0) {
    bui_ref_remove(id);
  }
}

/* Whether this thread gave up the GVL in bui_blocking (and holds no callback):
 * the kit's thread is the only one that sets or reads it. */
static int g_nogvl = 0;

static void *with_gvl(void *(*fn)(void *), void *arg)
{
  if (!g_nogvl) {
    return fn(arg);
  }
  g_nogvl = 0;
  void *r = rb_thread_call_with_gvl(fn, arg);
  g_nogvl = 1;
  return r;
}

typedef struct RefChange {
  uintptr_t id;
  long delta;
} RefChange;

static void *ref_change(void *p)
{
  const RefChange *c = (const RefChange *)p;
  st_data_t value;
  if (!st_lookup(g_refs, (st_data_t)c->id, &value)) {
    return NULL; /* not ours, or gone */
  }
  BuiRef *ref = (BuiRef *)value;
  ref->count += c->delta;
  if (ref->count > 0) {
    return NULL;
  }
  ref->count = 0;
  switch (ref->kind) {
    case BUI_REF_NODE:
      bui_node_forget(ref->obj);
      bui_ref_remove(c->id);
      break;
    case BUI_REF_PLAIN:
      bui_ref_remove(c->id);
      break;
    case BUI_REF_KEEP:
      break;
  }
  return NULL;
}

/* The kit calls these from its own frames, maybe inside the window loop
 * without the GVL (a popup closing frees its nodes): the table changes only
 * with the GVL, as the GC reads it. */
void bui_ref_retain(void *id)
{
  RefChange c = {(uintptr_t)id, 1};
  with_gvl(ref_change, &c);
}

void bui_ref_release(void *id)
{
  RefChange c = {(uintptr_t)id, -1};
  with_gvl(ref_change, &c);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Callbacks and errors
 * \{ */

/* The first exception a callback raised, until the kit call that ran it returns. */
static VALUE g_pending = Qnil;
/* Window loops running (BUI.run inside a callback nests). */
static int g_loops = 0;
/* An interrupt asked the window loop to stop (set from another thread). */
static volatile int g_interrupted = 0;

static int is_exception(VALUE err)
{
  return !RB_SPECIAL_CONST_P(err) && RB_BUILTIN_TYPE(err) == T_OBJECT &&
         RTEST(rb_obj_is_kind_of(err, rb_eException));
}

/* Callbacks running: the kit has frames on the stack that use its contexts and
 * windows, so lib/ frees those later (BUI.in_callback?). The loop's wake does
 * not count: it runs between messages. */
static int g_depth = 0;

typedef struct Callback {
  VALUE (*fn)(VALUE);
  void *data;
  int counts;
} Callback;

static void *callback_run(void *p)
{
  const Callback *cb = (const Callback *)p;
  int state = 0;
  g_depth += cb->counts;
  rb_protect(cb->fn, (VALUE)cb->data, &state);
  g_depth -= cb->counts;
  if (state) {
    VALUE err = rb_errinfo();
    rb_set_errinfo(Qnil);
    if (NIL_P(g_pending)) {
      /* break, next or throw out of a callback is no exception: name it */
      g_pending = is_exception(err) ?
                      err :
                      rb_exc_new_cstr(rb_eLocalJumpError,
                                      "break or throw out of a BUI callback (use return or next)");
    }
    if (g_loops > 0) {
      BK_window_quit();
    }
  }
  return NULL;
}

void bui_callback(VALUE (*fn)(VALUE), void *data)
{
  Callback cb = {fn, data, 1};
  with_gvl(callback_run, &cb);
}

void bui_callback_between(VALUE (*fn)(VALUE), void *data)
{
  Callback cb = {fn, data, 0};
  with_gvl(callback_run, &cb);
}

/* BUI.references: the entries in the reference table (tests check that it
 * shrinks back: nothing kept after it was freed). */
static VALUE m_references(VALUE self)
{
  return SIZET2NUM(g_refs->num_entries);
}

/* BUI.in_callback?: a kit callback is running (an event handler, a window's). */
static VALUE m_in_callback_p(VALUE self)
{
  return g_depth > 0 ? Qtrue : Qfalse;
}

void bui_raise_pending(void)
{
  if (!NIL_P(g_pending)) {
    VALUE err = g_pending;
    g_pending = Qnil;
    rb_exc_raise(err);
  }
}

void bui_settle(void)
{
  bui_raise_pending();
  if (g_depth == 0) {
    rb_funcall(bui_mBUI, rb_intern("drain_main"), 0);
  }
}

/* An interrupt for this thread (Ctrl+C, Thread#raise, Thread#kill) while the
 * loop waits: stop it. Runs on another thread: only the flag and the wake,
 * which is thread-safe. */
static void loop_unblock(void *arg)
{
  (void)arg;
  g_interrupted = 1;
  BK_window_wake();
}

static int loop_stop(void)
{
  return g_interrupted;
}

typedef struct Blocking {
  void *(*fn)(void *);
  void *arg;
} Blocking;

static void *blocking_run(void *p)
{
  const Blocking *b = (const Blocking *)p;
  return b->fn(b->arg);
}

void bui_blocking(void *(*fn)(void *), void *arg)
{
  Blocking b = {fn, arg};
  const int nogvl = g_nogvl;
  g_interrupted = 0;
  g_loops++;
  g_nogvl = 1;
  /* gvl2: an interrupt is not raised from inside, before the state is back */
  rb_thread_call_without_gvl2(blocking_run, &b, loop_unblock, NULL);
  g_nogvl = nogvl;
  g_loops--;
  bui_raise_pending();
  rb_thread_check_ints(); /* Ctrl+C: Interrupt, now */
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Arenas
 * \{ */

void *bui_arena_alloc(BuiArena *a, size_t size)
{
  if (a->n == a->cap) {
    const long cap = a->cap ? a->cap * 2 : 16;
    REALLOC_N(a->ptrs, void *, cap);
    a->cap = cap;
  }
  void *p = ruby_xcalloc(1, size ? size : 1);
  a->ptrs[a->n++] = p;
  return p;
}

char *bui_arena_str(BuiArena *a, VALUE v)
{
  if (NIL_P(v)) {
    return NULL;
  }
  const char *s = bui_cstr(v);
  const size_t len = strlen(s);
  char *copy = (char *)bui_arena_alloc(a, len + 1);
  memcpy(copy, s, len);
  return copy;
}

typedef struct ArenaCall {
  BuiArena arena;
  VALUE (*fn)(BuiArena *a, void *data);
  void *data;
} ArenaCall;

static VALUE arena_body(VALUE p)
{
  ArenaCall *c = (ArenaCall *)p;
  return c->fn(&c->arena, c->data);
}

static VALUE arena_free(VALUE p)
{
  ArenaCall *c = (ArenaCall *)p;
  for (long i = 0; i < c->arena.n; i++) {
    ruby_xfree(c->arena.ptrs[i]);
  }
  ruby_xfree(c->arena.ptrs);
  return Qnil;
}

VALUE bui_with_arena(VALUE (*fn)(BuiArena *a, void *data), void *data)
{
  ArenaCall c = {{NULL, 0, 0}, fn, data};
  return rb_ensure(arena_body, (VALUE)&c, arena_free, (VALUE)&c);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Conversions
 * \{ */

const char *bui_cstr(VALUE v)
{
  if (RB_SYMBOL_P(v)) {
    v = rb_sym2str(v);
  }
  return StringValueCStr(v);
}

const char *bui_cstr_opt(VALUE v)
{
  return NIL_P(v) ? NULL : bui_cstr(v);
}

VALUE bui_str(const char *s)
{
  return s ? rb_utf8_str_new_cstr(s) : Qnil;
}

VALUE bui_opt(VALUE opts, const char *key)
{
  if (!RB_TYPE_P(opts, T_HASH)) {
    return Qnil;
  }
  return rb_hash_lookup(opts, ID2SYM(rb_intern(key)));
}

void bui_rgba(VALUE v, float r_rgba[4])
{
  if (RTEST(rb_obj_is_kind_of(v, cColor))) {
    for (int i = 0; i < 4; i++) {
      r_rgba[i] = (float)NUM2DBL(rb_struct_aref(v, INT2FIX(i)));
    }
    return;
  }
  Check_Type(v, T_ARRAY);
  const long n = RARRAY_LEN(v);
  if (n != 3 && n != 4) {
    rb_raise(rb_eArgError, "a color is 3 or 4 numbers (0..1), not %ld", n);
  }
  for (int i = 0; i < 4; i++) {
    r_rgba[i] = i < n ? (float)NUM2DBL(rb_ary_entry(v, i)) : 1.0f;
  }
}

VALUE bui_color_new(const float rgba[4])
{
  return rb_struct_new(cColor, DBL2NUM(rgba[0]), DBL2NUM(rgba[1]), DBL2NUM(rgba[2]),
                       DBL2NUM(rgba[3]));
}

VALUE bui_rect_new(const int r[4])
{
  return rb_struct_new(cRect, INT2NUM(r[0]), INT2NUM(r[1]), INT2NUM(r[2]), INT2NUM(r[3]));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Module functions: the global look
 * \{ */

static VALUE m_kit_version(VALUE self)
{
  return bui_str(BK_version_string());
}

static VALUE m_scale(VALUE self)
{
  return DBL2NUM(BK_scale());
}

static VALUE m_set_scale(VALUE self, VALUE scale)
{
  const double s = NUM2DBL(scale);
  if (!(s > 0.0)) {
    rb_raise(rb_eArgError, "the scale is above 0, not %g", s);
  }
  BK_set_scale((float)s);
  return scale;
}

static VALUE m_unit(VALUE self)
{
  return INT2NUM(BK_unit());
}

static VALUE m_set_font(VALUE self, VALUE family)
{
  BK_set_font(bui_cstr(family));
  return family;
}

static VALUE m_font_size(VALUE self)
{
  return DBL2NUM(BK_font_size_points());
}

static VALUE m_set_font_size(VALUE self, VALUE points)
{
  const double p = NUM2DBL(points);
  if (!(p > 0.0)) {
    rb_raise(rb_eArgError, "the font size is above 0, not %g", p);
  }
  BK_set_font_size((float)p);
  return points;
}

static VALUE m_text_shadow(VALUE self)
{
  return BK_text_shadow() ? Qtrue : Qfalse;
}

static VALUE m_set_text_shadow(VALUE self, VALUE on)
{
  BK_set_text_shadow(RTEST(on));
  return on;
}

static VALUE m_set_animations(VALUE self, VALUE on)
{
  BK_set_animations(RTEST(on));
  return on;
}

static VALUE m_text_width(VALUE self, VALUE text)
{
  StringValue(text);
  if (RSTRING_LEN(text) > INT_MAX) {
    rb_raise(rb_eArgError, "the text is too long");
  }
  return DBL2NUM(BK_text_width(RSTRING_PTR(text), (int)RSTRING_LEN(text)));
}

static VALUE m_eval_number(VALUE self, VALUE expr)
{
  double v;
  return BK_eval_number(bui_cstr(expr), &v) ? DBL2NUM(v) : Qnil;
}

static VALUE m_icon(VALUE self, VALUE name)
{
  return INT2NUM(BK_icon_id(bui_cstr(name)));
}

static VALUE m_icon_names(VALUE self)
{
  const int n = BK_icon_list_count();
  VALUE out = rb_ary_new_capa(n);
  for (int i = 0; i < n; i++) {
    rb_ary_push(out, bui_str(BK_icon_list_name(i)));
  }
  return out;
}

static VALUE m_link_fields(int argc, VALUE *argv, VALUE self)
{
  if (argc == 0) {
    return Qnil;
  }
  VALUE buf;
  BKNode **fields = ALLOCV_N(BKNode *, buf, argc);
  for (int i = 0; i < argc; i++) {
    fields[i] = bui_node_ptr(argv[i]);
  }
  BK_link_fields(fields, argc);
  ALLOCV_END(buf);
  return Qnil;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Module functions: themes
 * \{ */

static VALUE m_theme(VALUE self)
{
  return bui_str(BK_theme_name());
}

static VALUE m_set_theme(VALUE self, VALUE name)
{
  if (!BK_set_theme(bui_cstr(name))) {
    rb_raise(rb_eArgError, "no theme named %" PRIsVALUE " (BUI.themes lists them)", name);
  }
  return name;
}

static VALUE m_theme_mode(VALUE self)
{
  return bui_str(BK_theme_mode());
}

static VALUE m_theme_generation(VALUE self)
{
  return INT2NUM(BK_theme_generation());
}

static VALUE m_themes(VALUE self)
{
  const int n = BK_theme_count();
  VALUE out = rb_ary_new_capa(n);
  for (int i = 0; i < n; i++) {
    rb_ary_push(out, bui_str(BK_theme_at(i)));
  }
  return out;
}

static VALUE m_theme_color(VALUE self, VALUE key)
{
  float c[4];
  return BK_theme_color(bui_cstr(key), c) ? bui_color_new(c) : Qnil;
}

static VALUE m_set_theme_color(VALUE self, VALUE key, VALUE color)
{
  float c[4];
  bui_rgba(color, c);
  return BK_theme_set_color(bui_cstr(key), c) ? Qtrue : Qfalse;
}

static VALUE m_theme_num(VALUE self, VALUE key)
{
  double v;
  return BK_theme_num(bui_cstr(key), &v) ? DBL2NUM(v) : Qnil;
}

static VALUE m_set_theme_num(VALUE self, VALUE key, VALUE value)
{
  return BK_theme_set_num(bui_cstr(key), NUM2DBL(value)) ? Qtrue : Qfalse;
}

static VALUE theme_loaded(int n)
{
  if (n < 0) {
    rb_raise(bui_eError, "%s", BK_theme_error());
  }
  return INT2NUM(n);
}

static VALUE m_load_theme_json(VALUE self, VALUE json)
{
  return theme_loaded(BK_theme_load_json(bui_cstr(json)));
}

static VALUE m_load_theme_file(VALUE self, VALUE path)
{
  FilePathValue(path);
  return theme_loaded(BK_theme_load_file(bui_cstr(path)));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name The file dialog
 * \{ */

/* The dialog runs without the GVL, while other threads may move or change Ruby
 * strings: it gets copies. strs[0..2] are the title, folder and file; the
 * filters follow. */
typedef struct FileDialog {
  char **strs;
  int nstrs, mode, multiple, result;
  char **paths;
} FileDialog;

static void *file_dialog_run(void *p)
{
  FileDialog *d = (FileDialog *)p;
  d->result = BK_file_dialog(d->strs[0],
                             d->mode,
                             d->strs[1],
                             d->strs[2],
                             (const char *const *)d->strs + 3,
                             d->nstrs - 3,
                             d->multiple,
                             &d->paths);
  return NULL;
}

static VALUE file_dialog_body(VALUE p)
{
  bui_blocking(file_dialog_run, (void *)p);
  return Qnil;
}

static VALUE file_dialog_free(VALUE p)
{
  FileDialog *d = (FileDialog *)p;
  for (int i = 0; i < d->nstrs; i++) {
    xfree(d->strs[i]);
  }
  xfree(d->strs);
  return Qnil;
}

static char *copy_opt(VALUE v)
{
  return NIL_P(v) ? NULL : ruby_strdup(bui_cstr(v));
}

/* BUI.file_dialog(title: "Open", mode: :open | :save | :directory, dir: nil,
 * file: nil, filters: ["Images\t*.png *.jpg"], multiple: false): the chosen
 * paths, [] when cancelled. */
static VALUE m_file_dialog(int argc, VALUE *argv, VALUE self)
{
  VALUE opts;
  rb_scan_args(argc, argv, ":", &opts);
  FileDialog d = {0};
  VALUE title = bui_opt(opts, "title"), mode = bui_opt(opts, "mode");
  VALUE dir = bui_opt(opts, "dir"), file = bui_opt(opts, "file");
  VALUE filters = bui_opt(opts, "filters");
  d.multiple = RTEST(bui_opt(opts, "multiple"));
  if (NIL_P(mode) || mode == ID2SYM(rb_intern("open"))) {
    d.mode = 0;
  }
  else if (mode == ID2SYM(rb_intern("save"))) {
    d.mode = 1;
  }
  else if (mode == ID2SYM(rb_intern("directory"))) {
    d.mode = 2;
  }
  else {
    rb_raise(rb_eArgError, "mode is :open, :save or :directory, not %" PRIsVALUE, mode);
  }
  /* every value is checked before anything is copied: nothing leaks on a raise */
  bui_cstr_opt(title);
  bui_cstr_opt(dir);
  bui_cstr_opt(file);
  if (!NIL_P(filters)) {
    Check_Type(filters, T_ARRAY);
    for (long i = 0; i < RARRAY_LEN(filters); i++) {
      bui_cstr(rb_ary_entry(filters, i));
    }
  }
  const long nfilters = NIL_P(filters) ? 0 : RARRAY_LEN(filters);
  if (nfilters > 1024) {
    rb_raise(rb_eArgError, "%ld filters (at most 1024)", nfilters);
  }
  d.nstrs = 3 + (int)nfilters;
  d.strs = ZALLOC_N(char *, d.nstrs);
  d.strs[0] = NIL_P(title) ? ruby_strdup("Open") : copy_opt(title);
  d.strs[1] = copy_opt(dir);
  d.strs[2] = copy_opt(file);
  for (long i = 0; i < nfilters; i++) {
    d.strs[3 + i] = copy_opt(rb_ary_entry(filters, i));
  }
  rb_ensure(file_dialog_body, (VALUE)&d, file_dialog_free, (VALUE)&d);
  if (d.result < 0) {
    rb_raise(bui_eError, "no native windows on this platform");
  }
  VALUE out = rb_ary_new_capa(d.result);
  for (int i = 0; d.paths && i < d.result; i++) {
    rb_ary_push(out, bui_str(d.paths[i]));
  }
  BK_file_dialog_free(d.paths);
  return out;
}

/** \} */

RUBY_FUNC_EXPORTED void Init_bui_ext(void)
{
  /* not Ractor-safe (the default): the kit has one thread and global state */
  bui_mBUI = rb_define_module("BUI");
  bui_eError = rb_define_class_under(bui_mBUI, "Error", rb_eStandardError);
  bui_eFreed = rb_define_class_under(bui_mBUI, "FreedError", bui_eError);
  cColor = rb_struct_define_under(bui_mBUI, "Color", "r", "g", "b", "a", NULL);
  cRect = rb_struct_define_under(bui_mBUI, "Rect", "x", "y", "w", "h", NULL);

  g_refs = st_init_numtable();
  /* the GC marks (and compacts) a data object's content only when its pointer is set */
  rb_gc_register_mark_object(TypedData_Wrap_Struct(0, &table_type, g_refs));
  rb_gc_register_address(&g_pending);
  BK_set_ref_hooks(bui_ref_retain, bui_ref_release);
  BK_window_set_stop_check(loop_stop);

  rb_define_const(bui_mBUI, "KIT_VERSION", bui_str(BK_version_string()));
  rb_define_module_function(bui_mBUI, "kit_version", m_kit_version, 0);
  rb_define_module_function(bui_mBUI, "scale", m_scale, 0);
  rb_define_module_function(bui_mBUI, "scale=", m_set_scale, 1);
  rb_define_module_function(bui_mBUI, "unit", m_unit, 0);
  rb_define_module_function(bui_mBUI, "font=", m_set_font, 1);
  rb_define_module_function(bui_mBUI, "font_size", m_font_size, 0);
  rb_define_module_function(bui_mBUI, "font_size=", m_set_font_size, 1);
  rb_define_module_function(bui_mBUI, "text_shadow?", m_text_shadow, 0);
  rb_define_module_function(bui_mBUI, "text_shadow=", m_set_text_shadow, 1);
  rb_define_module_function(bui_mBUI, "animations=", m_set_animations, 1);
  rb_define_module_function(bui_mBUI, "text_width", m_text_width, 1);
  rb_define_module_function(bui_mBUI, "eval_number", m_eval_number, 1);
  rb_define_module_function(bui_mBUI, "icon", m_icon, 1);
  rb_define_module_function(bui_mBUI, "icon_names", m_icon_names, 0);
  rb_define_module_function(bui_mBUI, "link_fields", m_link_fields, -1);
  rb_define_module_function(bui_mBUI, "theme", m_theme, 0);
  rb_define_module_function(bui_mBUI, "theme=", m_set_theme, 1);
  rb_define_module_function(bui_mBUI, "theme_mode", m_theme_mode, 0);
  rb_define_module_function(bui_mBUI, "theme_generation", m_theme_generation, 0);
  rb_define_module_function(bui_mBUI, "themes", m_themes, 0);
  rb_define_module_function(bui_mBUI, "theme_color", m_theme_color, 1);
  rb_define_module_function(bui_mBUI, "set_theme_color", m_set_theme_color, 2);
  rb_define_module_function(bui_mBUI, "theme_num", m_theme_num, 1);
  rb_define_module_function(bui_mBUI, "set_theme_num", m_set_theme_num, 2);
  rb_define_module_function(bui_mBUI, "load_theme_json", m_load_theme_json, 1);
  rb_define_module_function(bui_mBUI, "load_theme_file", m_load_theme_file, 1);
  rb_define_module_function(bui_mBUI, "file_dialog", m_file_dialog, -1);
  rb_define_module_function(bui_mBUI, "in_callback?", m_in_callback_p, 0);
  rb_define_module_function(bui_mBUI, "references", m_references, 0);

  bui_init_node();
  bui_init_context();
  bui_init_window();
  bui_init_image();
}
