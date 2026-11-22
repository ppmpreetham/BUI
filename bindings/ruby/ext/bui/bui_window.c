/* BUI for Ruby: BUI::Window (a native window: Win32, X11, Wayland, Cocoa)
 * and the message loop (BUI.run, BUI.pump, BUI.quit, BUI.wake).
 *
 * The loop runs without the GVL: other Ruby threads run while it waits, and
 * each callback takes the GVL back (bui_callback). lib/bui/window.rb holds the
 * window's options and handlers; this side makes the window and calls them.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "bui_ext.h"

VALUE bui_cWindow;

static ID id_fire_timer, id_close_requested, id_fire_frame, id_fire_focus, id_drain_main,
    id_context;

typedef struct Window {
  BKWindow *win;
  uintptr_t id; /* the callbacks' data: this wrapper, kept until it is freed */
} Window;

static size_t window_size(const void *p)
{
  return sizeof(Window);
}

static const rb_data_type_t window_type = {
    .wrap_struct_name = "BUI::Window",
    .function = {.dfree = RUBY_TYPED_DEFAULT_FREE, .dsize = window_size},
    .flags = RUBY_TYPED_FREE_IMMEDIATELY | RUBY_TYPED_WB_PROTECTED,
};

static VALUE window_alloc(VALUE klass)
{
  Window *w;
  return TypedData_Make_Struct(klass, Window, &window_type, w);
}

static BKWindow *window_ptr(VALUE self)
{
  const Window *w = (const Window *)rb_check_typeddata(self, &window_type);
  if (!w->win) {
    rb_raise(bui_eFreed, "the window was freed");
  }
  return w->win;
}

/* -------------------------------------------------------------------- */
/** \name Callbacks: to the window's private methods in lib/bui/window.rb
 * \{ */

/* The argument travels as C values: Ruby objects are made only once the GVL is held. */
typedef struct WindowCall {
  uintptr_t id;
  ID method;
  int argc;
  int arg_int, arg_bool; /* the argument: an Integer, or true/false */
  VALUE result;
} WindowCall;

static VALUE window_call_body(VALUE p)
{
  WindowCall *c = (WindowCall *)p;
  const VALUE win = bui_ref_get(c->id);
  if (win == Qundef) {
    return Qnil;
  }
  VALUE arg = c->arg_bool ? (c->arg_int ? Qtrue : Qfalse) : INT2NUM(c->arg_int);
  c->result = rb_funcallv(win, c->method, c->argc, &arg);
  return Qnil;
}

static VALUE window_call(void *data, ID method, int argc, int arg, int is_bool)
{
  WindowCall c = {(uintptr_t)data, method, argc, arg, is_bool, Qundef};
  bui_callback(window_call_body, &c);
  return c.result;
}

static void on_timer(void *data, BKWindow *win, int timer)
{
  window_call(data, id_fire_timer, 1, timer, 0);
}

static int on_close(void *data, BKWindow *win)
{
  const VALUE r = window_call(data, id_close_requested, 0, 0, 0);
  return r == Qundef || RTEST(r); /* an error closes it: the loop stops anyway */
}

static void on_frame(void *data, BKWindow *win)
{
  window_call(data, id_fire_frame, 0, 0, 0);
}

static void on_focus(void *data, BKWindow *win, int active)
{
  window_call(data, id_fire_focus, 1, active, 1);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Windows
 * \{ */

/* open_native(context, title, width, height): makes the native window showing
 * context, hidden. Raises BUI::Error where there are no native windows. */
static VALUE window_open_native(VALUE self, VALUE context, VALUE title, VALUE width, VALUE height)
{
  Window *w = (Window *)rb_check_typeddata(self, &window_type);
  if (w->win) {
    rb_raise(rb_eRuntimeError, "the window is made already");
  }
  BKContext *ctx = bui_context_ptr(context);
  const int ww = NUM2INT(width), wh = NUM2INT(height);
  if (ww <= 0 || wh <= 0 || ww > 16384 || wh > 16384) {
    rb_raise(rb_eArgError, "a window of %d x %d (1 to 16384 each)", ww, wh);
  }
  if (!BK_window_supported()) {
    rb_raise(bui_eError, "no native windows here (BUI.window_supported? is false)");
  }
  BKWindow *win = BK_window_new(ctx, bui_cstr(title), ww, wh);
  if (!win) {
    bui_raise_pending();
    rb_raise(bui_eError, "the window could not be made");
  }
  w->win = win;
  w->id = bui_ref_new(self, BUI_REF_PLAIN);
  bui_ref_retain((void *)w->id);
  rb_ivar_set(self, id_context, context);
  bui_context_windows(context, 1);
  BKWindowCallbacks cb;
  memset(&cb, 0, sizeof(cb));
  cb.data = (void *)w->id;
  cb.on_timer = on_timer;
  cb.on_close = on_close;
  cb.on_frame = on_frame;
  cb.on_focus = on_focus;
  BK_window_set_callbacks(win, &cb);
  bui_raise_pending();
  return self;
}

/* free_native: frees the native window (lib/ frees it outside its own callbacks). */
static VALUE window_free_native(VALUE self)
{
  Window *w = (Window *)rb_check_typeddata(self, &window_type);
  if (!w->win) {
    return Qnil;
  }
  BK_window_free(w->win);
  w->win = NULL;
  bui_context_windows(rb_ivar_get(self, id_context), -1);
  bui_ref_release((void *)w->id);
  bui_raise_pending();
  return Qnil;
}

static VALUE window_freed_p(VALUE self)
{
  return ((const Window *)rb_check_typeddata(self, &window_type))->win ? Qfalse : Qtrue;
}

static VALUE window_show(VALUE self)
{
  BK_window_show(window_ptr(self));
  bui_raise_pending();
  return self;
}

/* close: closes the window (the close handler is not asked). */
static VALUE window_close(VALUE self)
{
  BK_window_close(window_ptr(self));
  bui_raise_pending();
  return self;
}

static VALUE window_open_p(VALUE self)
{
  const Window *w = (const Window *)rb_check_typeddata(self, &window_type);
  return w->win && BK_window_is_open(w->win) ? Qtrue : Qfalse;
}

static VALUE window_set_title(VALUE self, VALUE title)
{
  BKWindow *win = window_ptr(self);
  BK_window_set_title(win, bui_cstr(title));
  return title;
}

/* resize(width, height), in logical pixels. */
static VALUE window_resize(VALUE self, VALUE width, VALUE height)
{
  BKWindow *win = window_ptr(self);
  const int w = NUM2INT(width), h = NUM2INT(height);
  if (w <= 0 || h <= 0) {
    rb_raise(rb_eArgError, "a size of %d x %d", w, h);
  }
  BK_window_set_size(win, w, h);
  bui_raise_pending();
  return self;
}

/* size: [width, height] in logical pixels, as resize takes them (the kit
 * reports device pixels). */
static VALUE window_size_get(VALUE self)
{
  BKWindow *win = window_ptr(self);
  int w, h;
  double scale = 1.0, unused;
  BK_window_get_size(win, &w, &h);
  if (!BK_window_wm_get(win, "scale", &scale, &unused) || !(scale > 0.0)) {
    scale = 1.0;
  }
  return rb_assoc_new(INT2NUM((int)(w / scale + 0.5)), INT2NUM((int)(h / scale + 0.5)));
}

/* use_gpu: renders on the GPU (Direct3D 11, OpenGL) where it starts, falling back to software. */
static VALUE window_use_gpu(VALUE self)
{
  BK_window_set_gpu(window_ptr(self), 1);
  return self;
}

static VALUE window_gpu_p(VALUE self)
{
  return BK_window_gpu(window_ptr(self)) ? Qtrue : Qfalse;
}

static VALUE window_frames(VALUE self)
{
  return INT2NUM(BK_window_frames(window_ptr(self)));
}

static VALUE window_render_now(VALUE self)
{
  BK_window_render_now(window_ptr(self));
  bui_raise_pending();
  return self;
}

/* The native handle as an Integer: an HWND, an X11 Window or an NSWindow *. */
static VALUE window_handle(VALUE self)
{
  return ULL2NUM((unsigned long long)(uintptr_t)BK_window_handle(window_ptr(self)));
}

/* wm(key, a = 0, b = 0): a window-manager request (Tk's wm): "pos", "minsize",
 * "maxsize", "resizable", "topmost", "alpha", "state", "lift", "focus"... */
static VALUE window_wm(int argc, VALUE *argv, VALUE self)
{
  VALUE key, a, b;
  rb_scan_args(argc, argv, "12", &key, &a, &b);
  BKWindow *win = window_ptr(self);
  const double va = NIL_P(a) ? 0.0 : NUM2DBL(a), vb = NIL_P(b) ? 0.0 : NUM2DBL(b);
  BK_window_wm(win, bui_cstr(key), va, vb);
  bui_raise_pending();
  return self;
}

/* wm_get(key): [a, b] for "pos", "state", "screen", "pointer", "scale"; nil when unknown. */
static VALUE window_wm_get(VALUE self, VALUE key)
{
  BKWindow *win = window_ptr(self);
  double a = 0.0, b = 0.0;
  if (!BK_window_wm_get(win, bui_cstr(key), &a, &b)) {
    return Qnil;
  }
  return rb_assoc_new(DBL2NUM(a), DBL2NUM(b));
}

/* start_timer(ms): a one-shot timer's id (fire_timer gets it), nil when the window holds no more. */
static VALUE window_start_timer(VALUE self, VALUE ms)
{
  const int m = NUM2INT(ms);
  if (m < 0) {
    rb_raise(rb_eArgError, "a timer of %d ms", m);
  }
  const int id = BK_window_timer(window_ptr(self), m);
  return id < 0 ? Qnil : INT2NUM(id);
}

static VALUE window_cancel_timer(VALUE self, VALUE id)
{
  BK_window_cancel_timer(window_ptr(self), NUM2INT(id));
  return self;
}

/* request_frame(vsync = false): the frame handler runs right before the next frame. */
static VALUE window_request_frame(int argc, VALUE *argv, VALUE self)
{
  VALUE vsync;
  rb_scan_args(argc, argv, "01", &vsync);
  BK_window_request_frame(window_ptr(self), RTEST(vsync));
  return self;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name The loop
 * \{ */

static VALUE drain_body(VALUE p)
{
  return rb_funcall(bui_mBUI, id_drain_main, 0);
}

/* BK_window_wake from another thread: on the loop's thread, without the GVL. */
static void wake(void)
{
  bui_callback_between(drain_body, NULL);
}

static void loop_start(void)
{
  static int ready = 0;
  if (!ready) {
    ready = BK_window_wake_init(wake);
  }
  rb_funcall(bui_mBUI, id_drain_main, 0); /* what other threads queued before */
}

static void *run_loop(void *p)
{
  BK_window_run();
  return NULL;
}

/* BUI.run: handles input and draws until every window is closed or BUI.quit. */
static VALUE m_run(VALUE self)
{
  loop_start();
  bui_blocking(run_loop, NULL);
  rb_funcall(bui_mBUI, id_drain_main, 0); /* what callbacks left for later (frees) */
  return Qnil;
}

static void *pump_loop(void *p)
{
  *(int *)p = BK_window_pump();
  return NULL;
}

/* BUI.pump: handles what is pending without waiting; returns the open windows. */
static VALUE m_pump(VALUE self)
{
  int open = 0;
  loop_start();
  bui_blocking(pump_loop, &open);
  rb_funcall(bui_mBUI, id_drain_main, 0);
  return INT2NUM(open);
}

static VALUE m_quit(VALUE self)
{
  BK_window_quit();
  return Qnil;
}

/* BUI.wake: from any thread, makes the loop run BUI.drain_main soon. */
static VALUE m_wake(VALUE self)
{
  BK_window_wake();
  return Qnil;
}

static VALUE m_window_supported_p(VALUE self)
{
  return BK_window_supported() ? Qtrue : Qfalse;
}

/* BUI.keep_alive = true: BUI.run keeps running with no window open, until BUI.quit. */
static VALUE m_set_keep_alive(VALUE self, VALUE on)
{
  BK_window_set_keep_alive(RTEST(on));
  return on;
}

/** \} */

void bui_init_window(void)
{
  id_fire_timer = rb_intern("fire_timer");
  id_close_requested = rb_intern("close_requested");
  id_fire_frame = rb_intern("fire_frame");
  id_fire_focus = rb_intern("fire_focus");
  id_drain_main = rb_intern("drain_main");
  id_context = rb_intern("@context");

  bui_cWindow = rb_define_class_under(bui_mBUI, "Window", rb_cObject);
  rb_define_alloc_func(bui_cWindow, window_alloc);
  rb_define_private_method(bui_cWindow, "open_native", window_open_native, 4);
  rb_define_private_method(bui_cWindow, "free_native", window_free_native, 0);
  rb_define_method(bui_cWindow, "freed?", window_freed_p, 0);
  rb_define_method(bui_cWindow, "show", window_show, 0);
  rb_define_method(bui_cWindow, "close", window_close, 0);
  rb_define_method(bui_cWindow, "open?", window_open_p, 0);
  rb_define_method(bui_cWindow, "title=", window_set_title, 1);
  rb_define_method(bui_cWindow, "resize", window_resize, 2);
  rb_define_method(bui_cWindow, "size", window_size_get, 0);
  rb_define_method(bui_cWindow, "use_gpu", window_use_gpu, 0);
  rb_define_method(bui_cWindow, "gpu?", window_gpu_p, 0);
  rb_define_method(bui_cWindow, "frames", window_frames, 0);
  rb_define_method(bui_cWindow, "render_now", window_render_now, 0);
  rb_define_method(bui_cWindow, "handle", window_handle, 0);
  rb_define_method(bui_cWindow, "wm", window_wm, -1);
  rb_define_method(bui_cWindow, "wm_get", window_wm_get, 1);
  rb_define_private_method(bui_cWindow, "start_timer", window_start_timer, 1);
  rb_define_private_method(bui_cWindow, "stop_timer", window_cancel_timer, 1);
  rb_define_method(bui_cWindow, "request_frame", window_request_frame, -1);

  rb_define_module_function(bui_mBUI, "run", m_run, 0);
  rb_define_module_function(bui_mBUI, "pump", m_pump, 0);
  rb_define_module_function(bui_mBUI, "quit", m_quit, 0);
  rb_define_module_function(bui_mBUI, "wake", m_wake, 0);
  rb_define_module_function(bui_mBUI, "window_supported?", m_window_supported_p, 0);
  rb_define_module_function(bui_mBUI, "keep_alive=", m_set_keep_alive, 1);
}
