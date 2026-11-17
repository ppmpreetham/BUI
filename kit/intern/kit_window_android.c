/* The native window host on Android: BK_window_* on a NativeActivity (android_native_app_glue),
 * the way kit_window_win32.c and kit_window_x11.c are for their systems. An app (C, Lua, Rust) opens
 * a window and runs the loop exactly as on a desktop; nothing in a binding knows it is on a phone.
 *
 *  - One window: the activity's surface. BK_window_new may come before the surface exists (the
 *    activity starts first); the size the app asks for is used until it arrives.
 *  - Damage driven: the loop sleeps in ALooper until a touch, key, timer, wake or frame request.
 *    Only the damaged rectangles are converted (B,G,R,A raster words to the surface's R,G,B,X
 *    bytes) and locked; while the kit animates, the buffer queue paces the loop to the display.
 *  - Touch: one finger is the pointer (a tap is a click), a drag that starts on a widget that does
 *    not drag scrolls, two fingers scroll, and the pointer leaves with the last finger.
 *  - Keys: key events (KeyEvent.getUnicodeChar through JNI) and the soft keyboard while a field is
 *    being edited (BK_ime_state). No IME composition yet.
 *
 * The app's `android_main` calls BK_android_attach first (platform/android/bui_android.c).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kit_intern.h"

#ifdef BUI_WINDOW_ANDROID

#  include <android/configuration.h>
#  include <android/keycodes.h>
#  include <android/looper.h>
#  include <android/native_activity.h>
#  include <android/native_window.h>
#  include <jni.h>
#  include <math.h>
#  include <stdatomic.h>
#  include <stdlib.h>
#  include <string.h>
#  include <time.h>

#  include "GPU_raster.h"
#  include "android_native_app_glue.h"

#  define MAX_TIMERS 16
#  define TOUCH_SLOP_DP 8.0f
/* fling: slowest and fastest (dp/s) and how fast it slows down (1/s): UIScrollView's normal
 * deceleration, 0.998 per millisecond, is exp(-2.0) per second */
#  define FLING_MIN_DP 40.0f
#  define FLING_MAX_DP 8000.0f
#  define FLING_FRICTION 2.0f
#  define TRAIL 12 /* touches kept for the fling's velocity */
/* a touch this close to the right edge of a custom node is the app's: its scrollbar */
#  define EDGE_DP 20.0f
#  define WINDOW_FORMAT_RGBX 0x2

typedef struct Timer {
  double due_ms; /* CLOCK_MONOTONIC; 0 = unused */
  int id;
  int user; /* BK_window_timer (on_timer) rather than the kit's own (BK_timer) */
} Timer;

typedef struct Sample { /* one touch of a scroll: when and where */
  double t;
  float x, y;
} Sample;

struct BKWindow {
  BKContext *ctx;
  BKHost prev; /* what the context had: BKHost.emit stays the app's */
  BuiRaster *raster;
  BKWindowCallbacks cb;
  int open;
  int w, h;   /* the content area, device pixels (the surface less the system bars) */
  int ox, oy; /* its top left in the surface */
  int sw, sh; /* the surface */
  int req_w, req_h; /* logical size the app asked for, used before the surface exists */
  int dirty, full, want_frame, frames;
  int ime_on;
  float scale, density;
  int next_timer;
  Timer timers[MAX_TIMERS];
  char *clipboard;
  int px, py; /* last pointer position */
  /* touch */
  int pointer_down, scrolling, draggable;
  int pending_tap; /* a finger is down on something that does not drag: a click if it lifts in place */
  int drag_custom; /* custom and canvas nodes take drags (BK_window_wm "touch_drag"); else they scroll */
  float down_x, down_y, scroll_x, scroll_y;
  /* momentum: the last touches of a one-finger scroll, and the fling (velocity in px/s) after it */
  Sample trail[TRAIL];
  int trail_n;
  float vel_x, vel_y;
  double fling_ms;
  int fling;
};

static BKWindow *g_win;
static struct android_app *g_app;
static int g_quit, g_keep_alive;
static void (*g_wake_cb)(void);
static atomic_int g_wake_flag; /* set by any thread (BK_window_wake), consumed by the loop */
static int (*g_stop)(void);

static double now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

/* -- host services ------------------------------------------------------------------------------ */

static void host_request(void *data)
{
  ((BKWindow *)data)->dirty = 1;
}

static void host_set_cursor(void *data, int cursor) {}

static void host_warp(void *data, int x, int y) {}

static char *host_clipboard_get(void *data)
{
  const BKWindow *win = (BKWindow *)data;
  return win->clipboard ? bk_strdup(win->clipboard) : NULL;
}

static void host_clipboard_set(void *data, const char *text)
{
  BKWindow *win = (BKWindow *)data;
  free(win->clipboard);
  win->clipboard = bk_strdup(text ? text : "");
}

static int add_timer(BKWindow *win, int ms, int id, int user)
{
  Timer *slot = NULL;
  for (int i = 0; i < MAX_TIMERS; i++) {
    Timer *t = &win->timers[i];
    if (t->due_ms != 0.0 && t->id == id && t->user == user) {
      slot = t; /* the same timer again: later */
      break;
    }
    if (t->due_ms == 0.0 && !slot) {
      slot = t;
    }
  }
  if (slot) {
    slot->id = id;
    slot->user = user;
    slot->due_ms = now_ms() + (ms > 0 ? ms : 1);
    ALooper_wake(g_app->looper); /* the wait ends in time for it */
  }
  return slot != NULL;
}

static void host_schedule(void *data, int ms, int id)
{
  add_timer((BKWindow *)data, ms, id, 0);
}

static void host_emit(void *data, BKNode *node, int kind, void *ref, double a, double b)
{
  const BKWindow *win = (BKWindow *)data;
  if (win->prev.emit) {
    win->prev.emit(win->prev.data, node, kind, ref, a, b);
  }
}

/* -- surface and drawing ------------------------------------------------------------------------ */

/* Dp to pixels, and the kit's scale from it. A CSS pixel is a dp, as in Chrome on Android (not
 * 1/96 inch): Rust apps' layout and Blender's 20-unit widgets both come out at a phone's size. */
static void apply_density(BKWindow *win)
{
  const float density = (float)AConfiguration_getDensity(g_app->config);
  win->density = (density > 0 ? density : 160.0f) / 160.0f;
  win->scale = win->density;
  BK_set_scale(win->scale);
}

static void surface_changed(BKWindow *win)
{
  ANativeWindow *surface = g_app->window;
  if (!surface) {
    return;
  }
  win->sw = ANativeWindow_getWidth(surface);
  win->sh = ANativeWindow_getHeight(surface);
  ANativeWindow_setBuffersGeometry(surface, win->sw, win->sh, WINDOW_FORMAT_RGBX);
  apply_density(win);
  /* the activity's content rectangle: the surface less the status and navigation bars */
  const ARect c = g_app->contentRect;
  const int ok = c.right > c.left && c.bottom > c.top && c.right <= win->sw && c.bottom <= win->sh;
  win->ox = ok ? c.left : 0;
  win->oy = ok ? c.top : 0;
  win->w = ok ? c.right - c.left : win->sw;
  win->h = ok ? c.bottom - c.top : win->sh;
  BK_configure(win->ctx, win->w, win->h);
  win->dirty = win->full = 1;
}

/* Swap B and R (the raster's 0xAARRGGBB words are B,G,R,A bytes) over one rectangle. */
static void convert(const uint32_t *src, int src_stride, uint32_t *dst, int dst_stride,
                    const int r[4])
{
  for (int y = r[1]; y < r[1] + r[3]; y++) {
    const uint32_t *in = src + (size_t)y * src_stride + r[0];
    uint32_t *out = dst + (size_t)y * dst_stride + r[0];
    for (int x = 0; x < r[2]; x++) {
      const uint32_t p = in[x];
      out[x] = (p & 0xFF00FF00u) | ((p & 0xFFu) << 16) | ((p >> 16) & 0xFFu) | 0xFF000000u;
    }
  }
}

static void draw(BKWindow *win)
{
  ANativeWindow *surface = g_app->window;
  if (!surface || win->w <= 0 || win->h <= 0) {
    return;
  }
  const int full = win->full || BUI_raster_width(win->raster) != win->w ||
                   BUI_raster_height(win->raster) != win->h;
  win->full = win->dirty = 0;
  if (BK_render(win->ctx, win->raster, win->w, win->h, full) == 0 && !full) {
    return;
  }
  int whole[1][4] = {{0, 0, win->w, win->h}};
  const int(*rects)[4] = whole;
  int n = 1;
  ARect dirty = {win->ox, win->oy, win->ox + win->w, win->oy + win->h};
  if (full) { /* the bars around the content too: a new layout must not leave old pixels there */
    dirty = (ARect){0, 0, win->sw, win->sh};
  }
  else {
    n = BUI_raster_damage(win->raster, &rects);
    if (n <= 0) {
      return;
    }
    int x0 = win->w, y0 = win->h, x1 = 0, y1 = 0;
    for (int i = 0; i < n; i++) {
      x0 = BK_MIN(x0, rects[i][0]);
      y0 = BK_MIN(y0, rects[i][1]);
      x1 = BK_MAX(x1, rects[i][0] + rects[i][2]);
      y1 = BK_MAX(y1, rects[i][1] + rects[i][3]);
    }
    dirty.left = win->ox + x0;
    dirty.top = win->oy + y0;
    dirty.right = win->ox + x1;
    dirty.bottom = win->oy + y1;
  }
  ANativeWindow_Buffer buf;
  if (ANativeWindow_lock(surface, &buf, &dirty) != 0) {
    win->full = win->dirty = 1; /* try again with the next event */
    return;
  }
  if (buf.width != win->sw || buf.height != win->sh || buf.format != WINDOW_FORMAT_RGBX) {
    ANativeWindow_unlockAndPost(surface);
    surface_changed(win);
    return;
  }
  /* The buffer is recycled: outside `dirty` it holds an older frame, which locking brought up to
   * date. Locking may also have widened the rectangle: send all of it. */
  const uint32_t *src = BUI_raster_pixels(win->raster);
  uint32_t *dst = (uint32_t *)buf.bits + (size_t)win->oy * buf.stride + win->ox;
  /* Locking may have widened `dirty` past what was asked (to the whole buffer, say): only the
   * part inside the content has pixels in the raster to copy. */
  const int bx0 = BK_MAX(dirty.left - win->ox, 0), by0 = BK_MAX(dirty.top - win->oy, 0);
  const int bx1 = BK_MIN(dirty.right - win->ox, win->w), by1 = BK_MIN(dirty.bottom - win->oy, win->h);
  const int back[4] = {bx0, by0, BK_MAX(bx1 - bx0, 0), BK_MAX(by1 - by0, 0)};
  if (full) {
    for (int y = 0; y < win->sh; y++) {
      uint32_t *row = (uint32_t *)buf.bits + (size_t)y * buf.stride;
      const int inside = y >= win->oy && y < win->oy + win->h;
      for (int x = 0; x < win->sw; x++) {
        if (!inside || x < win->ox || x >= win->ox + win->w) {
          row[x] = 0xFF000000u;
        }
      }
    }
  }
  if (full || (back[2] > 0 && back[3] > 0)) {
    convert(src, win->w, dst, buf.stride, full ? whole[0] : back);
  }
  ANativeWindow_unlockAndPost(surface);
  win->frames++;
}

static void ime_set(int show);

static void sync_keyboard(BKWindow *win)
{
  BKImeState st;
  if (!BK_ime_state(win->ctx, &st)) {
    return;
  }
  if (st.enabled != win->ime_on) {
    ime_set(st.enabled);
  }
  win->ime_on = st.enabled;
}

static void render_window(BKWindow *win)
{
  if (win->want_frame) {
    win->want_frame = 0;
    if (win->cb.on_frame) {
      win->cb.on_frame(win->cb.data, win); /* the app rebuilds its UI for exactly this frame */
    }
  }
  draw(win);
  sync_keyboard(win);
}

/* -- input -------------------------------------------------------------------------------------- */

static int mods_of(int32_t meta)
{
  return ((meta & AMETA_SHIFT_ON) ? BK_SHIFT : 0) | ((meta & AMETA_CTRL_ON) ? BK_CTRL : 0) |
         ((meta & AMETA_ALT_ON) ? BK_ALT : 0);
}

/* The character a key press types (KeyEvent.getUnicodeChar through JNI: NativeActivity gives the key
 * code and meta state, the layout is the framework's). */
static JNIEnv *jni_enter(int *attached)
{
  JavaVM *vm = g_app->activity->vm;
  JNIEnv *env = NULL;
  *attached = 0;
  if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
      return NULL;
    }
    *attached = 1;
  }
  return env;
}

static void jni_leave(JNIEnv *env, int attached)
{
  if ((*env)->ExceptionCheck(env)) {
    (*env)->ExceptionClear(env);
  }
  if (attached) {
    (*g_app->activity->vm)->DetachCurrentThread(g_app->activity->vm);
  }
}

/* Shows or hides the soft keyboard. ANativeActivity_showSoftInput does nothing on current Android
 * ("view is not served": NativeActivity's view is not a text editor), so the input method manager is
 * asked directly: toggleSoftInput(SHOW_FORCED) opens it without a served view. */
static void ime_set(int show)
{
  int attached;
  JNIEnv *env = jni_enter(&attached);
  if (!env) {
    return;
  }
  jobject act = g_app->activity->clazz;
  jclass ac = (*env)->GetObjectClass(env, act);
  jmethodID gss = (*env)->GetMethodID(env, ac, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
  jstring name = (*env)->NewStringUTF(env, "input_method");
  jobject imm = gss ? (*env)->CallObjectMethod(env, act, gss, name) : NULL;
  if (imm) {
    jclass ic = (*env)->GetObjectClass(env, imm);
    if (show) {
      jmethodID toggle = (*env)->GetMethodID(env, ic, "toggleSoftInput", "(II)V");
      if (toggle) {
        (*env)->CallVoidMethod(env, imm, toggle, 2 /* SHOW_FORCED */, 0);
      }
    }
    else {
      jmethodID hide = (*env)->GetMethodID(env, ic, "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z");
      jmethodID gw = (*env)->GetMethodID(env, ac, "getWindow", "()Landroid/view/Window;");
      jobject window = gw ? (*env)->CallObjectMethod(env, act, gw) : NULL;
      jobject deco = NULL, token = NULL;
      if (window) {
        jclass wc = (*env)->GetObjectClass(env, window);
        jmethodID gd = (*env)->GetMethodID(env, wc, "getDecorView", "()Landroid/view/View;");
        deco = gd ? (*env)->CallObjectMethod(env, window, gd) : NULL;
      }
      if (deco) {
        jclass vc = (*env)->GetObjectClass(env, deco);
        jmethodID gt = (*env)->GetMethodID(env, vc, "getWindowToken", "()Landroid/os/IBinder;");
        token = gt ? (*env)->CallObjectMethod(env, deco, gt) : NULL;
      }
      if (hide && token) {
        (*env)->CallBooleanMethod(env, imm, hide, token, 0);
      }
    }
  }
  jni_leave(env, attached);
}

static int unicode_of(int32_t code, int32_t meta)
{
  int attached;
  JNIEnv *env = jni_enter(&attached);
  if (!env) {
    return 0;
  }
  int ch = 0;
  jclass cls = (*env)->FindClass(env, "android/view/KeyEvent");
  if (cls) {
    jmethodID ctor = (*env)->GetMethodID(env, cls, "<init>", "(II)V");
    jmethodID get = (*env)->GetMethodID(env, cls, "getUnicodeChar", "(I)I");
    if (ctor && get) {
      jobject ev = (*env)->NewObject(env, cls, ctor, 0 /* ACTION_DOWN */, code);
      if (ev) {
        ch = (*env)->CallIntMethod(env, ev, get, meta);
        (*env)->DeleteLocalRef(env, ev);
      }
    }
    (*env)->DeleteLocalRef(env, cls);
  }
  jni_leave(env, attached);
  return ch;
}

static const char *keysym_of(int32_t code)
{
  switch (code) {
    case AKEYCODE_DEL: return "BackSpace";
    case AKEYCODE_FORWARD_DEL: return "Delete";
    case AKEYCODE_ENTER:
    case AKEYCODE_NUMPAD_ENTER: return "Return";
    case AKEYCODE_TAB: return "Tab";
    case AKEYCODE_ESCAPE: return "Escape";
    case AKEYCODE_DPAD_LEFT: return "Left";
    case AKEYCODE_DPAD_RIGHT: return "Right";
    case AKEYCODE_DPAD_UP: return "Up";
    case AKEYCODE_DPAD_DOWN: return "Down";
    case AKEYCODE_MOVE_HOME: return "Home";
    case AKEYCODE_MOVE_END: return "End";
    case AKEYCODE_PAGE_UP: return "Prior";
    case AKEYCODE_PAGE_DOWN: return "Next";
    case AKEYCODE_INSERT: return "Insert";
  }
  return NULL;
}

static void utf8_encode(int cp, char out[5])
{
  if (cp < 0x80) {
    out[0] = (char)cp;
    out[1] = 0;
  }
  else if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    out[2] = 0;
  }
  else if (cp < 0x10000) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    out[3] = 0;
  }
  else {
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    out[4] = 0;
  }
}

static int32_t on_key(BKWindow *win, AInputEvent *e)
{
  if (AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_UP) {
    return 0;
  }
  const int32_t code = AKeyEvent_getKeyCode(e), meta = AKeyEvent_getMetaState(e);
  const int mods = mods_of(meta);
  if (code == AKEYCODE_BACK) { /* closes a popup or leaves a text field; else the app goes back */
    return BK_key(win->ctx, "", "Escape", 0) ? 1 : 0;
  }
  const char *ks = keysym_of(code);
  if (ks) {
    return BK_key(win->ctx, "", ks, mods);
  }
  const int cp = unicode_of(code, meta & ~(AMETA_CTRL_ON | AMETA_META_ON));
  if (cp > 0 && cp < 0x10FFFF && cp != 0x7F && (cp >= 32 || cp == '\t')) {
    char text[5];
    utf8_encode(cp, text);
    const char *sym = cp == ' ' ? "space" : cp == '-' ? "minus" : text;
    return BK_key(win->ctx, text, sym, mods);
  }
  return 0;
}

/* Widgets that use a drag themselves; a drag that starts anywhere else scrolls the page. A custom
 * or canvas node (a Rust page is one) scrolls unless the app asks for its drags. */
static int drags(const BKWindow *win, const BKNode *n)
{
  static const char *const types[] = {"slider", "number", "scrollbar", "text", "textarea", "panel",
                                      "nodeeditor", "curve", "ramp", "picker", "normal", "swatch",
                                      "histogram", "eyedropper"};
  const char *t = n ? BK_node_type(n) : "";
  if (win->drag_custom && (strcmp(t, "custom") == 0 || strcmp(t, "canvas") == 0)) {
    return 1;
  }
  for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
    if (strcmp(t, types[i]) == 0) {
      return 1;
    }
  }
  return 0;
}

/* A touch in the strip at the right edge of a custom or canvas node: the app draws its scrollbar
 * there (a list's thumb), so the touch is its press and drag, not a scroll. */
static int on_edge(const BKWindow *win, const BKNode *n, float x)
{
  const char *t = n ? BK_node_type(n) : "";
  if (strcmp(t, "custom") != 0 && strcmp(t, "canvas") != 0) {
    return 0;
  }
  int r[4];
  BK_node_rect(n, r);
  return x >= (float)(r[0] + r[2]) - EDGE_DP * win->density;
}

static void pointer_up(BKWindow *win, float x, float y)
{
  if (win->pointer_down) {
    BK_motion(win->ctx, (int)x, (int)y, 0);
    BK_release(win->ctx, (int)x, (int)y, 0);
    win->pointer_down = 0;
  }
  BK_motion(win->ctx, -1, -1, 0); /* no hover once the finger is gone */
}

/* A press that turned into a scroll ends outside the screen: no click, no drag. */
static void cancel_press(BKWindow *win)
{
  if (win->pointer_down) {
    BK_motion(win->ctx, -100, -100, 0);
    BK_release(win->ctx, -100, -100, 0);
    win->pointer_down = 0;
  }
}

/* Scrolls the content under (x, y) by (dx, dy) pixels. */
static void scroll_px(BKWindow *win, float x, float y, float dx, float dy)
{
  BK_motion(win->ctx, (int)x, (int)y, 0); /* a page that scrolls under the pointer needs to know it */
  BK_scroll_px(win->ctx, (int)x, (int)y, dx, dy, 0); /* the content follows the finger exactly */
  BK_motion(win->ctx, -1, -1, 0); /* a scrolling touch is no hover: nothing under it lights up */
}

static void scroll_by(BKWindow *win, float x, float y, float *last_x, float *last_y)
{
  scroll_px(win, x, y, *last_x - x, y - *last_y);
  *last_x = x;
  *last_y = y;
}

/* A one-finger scroll to (x, y): the touch is recorded for the fling's velocity. */
static void scroll_finger(BKWindow *win, float x, float y)
{
  const double t = now_ms();
  Sample *s = &win->trail[win->trail_n++ % TRAIL];
  s->t = t;
  s->x = x;
  s->y = y;
  scroll_by(win, x, y, &win->scroll_x, &win->scroll_y);
}

/* The finger lifted while scrolling: carry on at its velocity, over the last 100 ms of touches
 * (a finger that rested before lifting does not fling), slowing down the way UIScrollView does. */
static void fling_start(BKWindow *win)
{
  const float max = FLING_MAX_DP * win->density, min = FLING_MIN_DP * win->density;
  const double t = now_ms();
  const int n = win->trail_n < TRAIL ? win->trail_n : TRAIL;
  win->vel_x = win->vel_y = 0.0f;
  if (n < 2) {
    return;
  }
  const Sample last = win->trail[(win->trail_n - 1) % TRAIL];
  if (t - last.t > 80.0) { /* the finger rested: no fling */
    return;
  }
  Sample first = last;
  for (int i = 2; i <= n; i++) { /* the oldest touch within 100 ms of the last */
    const Sample s = win->trail[(win->trail_n - i) % TRAIL];
    if (last.t - s.t > 100.0) {
      break;
    }
    first = s;
  }
  const float dt = (float)(last.t - first.t) * 0.001f;
  if (dt <= 0.0f) {
    return;
  }
  float vx = (last.x - first.x) / dt, vy = (last.y - first.y) / dt;
  vx = vx > max ? max : (vx < -max ? -max : vx);
  vy = vy > max ? max : (vy < -max ? -max : vy);
  if (vx * vx + vy * vy >= min * min) {
    win->vel_x = vx;
    win->vel_y = vy;
    win->fling = 1;
    win->fling_ms = t;
  }
}

static void fling_step(BKWindow *win)
{
  const double t = now_ms();
  float dt = (float)(t - win->fling_ms) * 0.001f;
  dt = dt > 0.05f ? 0.05f : dt; /* a stalled frame is not a leap */
  win->fling_ms = t;
  scroll_px(win, (float)win->px, (float)win->py, -win->vel_x * dt, win->vel_y * dt);
  const float keep = expf(-FLING_FRICTION * dt);
  win->vel_x *= keep;
  win->vel_y *= keep;
  const float min = FLING_MIN_DP * win->density * 0.5f;
  if (win->vel_x * win->vel_x + win->vel_y * win->vel_y < min * min) {
    win->fling = 0;
  }
  win->dirty = 1;
}

static int32_t on_touch(BKWindow *win, AInputEvent *e)
{
  const int32_t action = AMotionEvent_getAction(e) & AMOTION_EVENT_ACTION_MASK;
  const size_t count = AMotionEvent_getPointerCount(e);
  const float x = AMotionEvent_getX(e, 0) - (float)win->ox;
  const float y = AMotionEvent_getY(e, 0) - (float)win->oy;
  const float slop = TOUCH_SLOP_DP * win->density;
  switch (action) {
    case AMOTION_EVENT_ACTION_DOWN:
      win->down_x = win->scroll_x = x;
      win->down_y = win->scroll_y = y;
      win->scrolling = win->fling = 0; /* a touch stops a fling */
      win->vel_x = win->vel_y = 0.0f;
      win->trail_n = 1; /* the velocity of a scroll counts from the touch down */
      win->trail[0] = (Sample){now_ms(), x, y};
      BK_motion(win->ctx, (int)x, (int)y, 0);
      win->draggable = drags(win, BK_hover(win->ctx)) || on_edge(win, BK_hover(win->ctx), x);
      if (win->draggable) { /* a slider, a text field ...: it gets the press now */
        BK_press(win->ctx, (int)x, (int)y, 0);
        win->pointer_down = 1;
      }
      else { /* a tap or a scroll: the kit hears of it once we know which */
        win->pending_tap = 1;
      }
      break;
    case AMOTION_EVENT_ACTION_POINTER_DOWN:
      if (count >= 2 && !win->scrolling) { /* a second finger: scroll instead of drag */
        win->pending_tap = 0;
        cancel_press(win);
        win->scrolling = 1;
        win->scroll_x = (AMotionEvent_getX(e, 0) + AMotionEvent_getX(e, 1)) * 0.5f - (float)win->ox;
        win->scroll_y = (AMotionEvent_getY(e, 0) + AMotionEvent_getY(e, 1)) * 0.5f - (float)win->oy;
      }
      break;
    case AMOTION_EVENT_ACTION_MOVE:
      if (win->scrolling && count >= 2) {
        scroll_by(win, (AMotionEvent_getX(e, 0) + AMotionEvent_getX(e, 1)) * 0.5f - (float)win->ox,
                  (AMotionEvent_getY(e, 0) + AMotionEvent_getY(e, 1)) * 0.5f - (float)win->oy,
                  &win->scroll_x, &win->scroll_y);
      }
      else if (win->scrolling) { /* a one-finger scroll */
        scroll_finger(win, x, y);
      }
      else if (win->pending_tap) {
        const float dx = x - win->down_x, dy = y - win->down_y;
        if (dx * dx + dy * dy > slop * slop) { /* not a click: scroll, from where the finger went down */
          win->pending_tap = 0;
          win->scrolling = 1;
          scroll_finger(win, x, y);
        }
      }
      else if (win->pointer_down) {
        BK_motion(win->ctx, (int)x, (int)y, BK_BUTTON1);
      }
      break;
    case AMOTION_EVENT_ACTION_POINTER_UP:
      if (count <= 2) {
        win->scrolling = 0;
      }
      break;
    case AMOTION_EVENT_ACTION_UP:
      if (win->scrolling) { /* a one-finger scroll (two fingers cleared it when one lifted) */
        fling_start(win);
      }
      win->scrolling = 0;
      if (win->pending_tap) { /* lifted in place: the click */
        win->pending_tap = 0;
        BK_motion(win->ctx, (int)x, (int)y, 0);
        BK_press(win->ctx, (int)x, (int)y, 0);
        win->pointer_down = 1;
      }
      pointer_up(win, x, y);
      break;
    case AMOTION_EVENT_ACTION_CANCEL:
      win->scrolling = win->pending_tap = win->fling = 0;
      pointer_up(win, -100.0f, -100.0f);
      break;
    default:
      return 0;
  }
  win->px = (int)x;
  win->py = (int)y;
  win->dirty = 1;
  return 1;
}

static int32_t on_input(struct android_app *app, AInputEvent *e)
{
  BKWindow *win = g_win;
  if (!win || !win->open) {
    return 0;
  }
  const int32_t type = AInputEvent_getType(e), source = AInputEvent_getSource(e);
  if (type == AINPUT_EVENT_TYPE_MOTION && (source & AINPUT_SOURCE_CLASS_POINTER)) {
    return on_touch(win, e);
  }
  return type == AINPUT_EVENT_TYPE_KEY ? on_key(win, e) : 0;
}

static void on_cmd(struct android_app *app, int32_t cmd)
{
  BKWindow *win = g_win;
  if (!win) {
    return;
  }
  switch (cmd) {
    case APP_CMD_INIT_WINDOW:
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
    case APP_CMD_CONTENT_RECT_CHANGED:
      if (app->window) {
        surface_changed(win);
      }
      break;
    case APP_CMD_TERM_WINDOW:
      win->full = 1; /* the next surface starts empty */
      break;
    case APP_CMD_GAINED_FOCUS:
    case APP_CMD_LOST_FOCUS:
      if (win->cb.on_focus) {
        win->cb.on_focus(win->cb.data, win, cmd == APP_CMD_GAINED_FOCUS);
      }
      /* fallthrough */
    case APP_CMD_RESUME:
      win->full = win->dirty = 1;
      break;
  }
}

void BK_android_attach(void *android_app)
{
  g_app = (struct android_app *)android_app;
  g_app->onAppCmd = on_cmd;
  g_app->onInputEvent = on_input;
}

void *BK_android_assets(void)
{
  return g_app ? (void *)g_app->activity->assetManager : NULL;
}

/* -- the loop ----------------------------------------------------------------------------------- */

static int open_windows(void)
{
  return g_win && g_win->open;
}

/* Milliseconds the loop may sleep: forever when idle. */
static int wait_ms(const BKWindow *win)
{
  /* With no surface nothing can be drawn, so a pending frame must not keep the loop spinning: it
   * waits for the surface (APP_CMD_INIT_WINDOW wakes the looper). */
  if (atomic_load(&g_wake_flag) ||
      (win && (win->fling || (g_app->window && (win->dirty || win->want_frame || BK_animating(win->ctx)))))) {
    return 0;
  }
  double next = 0.0;
  if (win) {
    for (int i = 0; i < MAX_TIMERS; i++) {
      if (win->timers[i].due_ms != 0.0 && (next == 0.0 || win->timers[i].due_ms < next)) {
        next = win->timers[i].due_ms;
      }
    }
  }
  if (next == 0.0) {
    return -1;
  }
  const double d = next - now_ms();
  return d <= 0.0 ? 0 : (int)d + 1;
}

static void fire_timers(BKWindow *win)
{
  const double t = now_ms();
  for (int i = 0; i < MAX_TIMERS && win->open; i++) {
    Timer *tm = &win->timers[i];
    if (tm->due_ms != 0.0 && tm->due_ms <= t) {
      const int id = tm->id, user = tm->user;
      tm->due_ms = 0.0;
      if (user) {
        if (win->cb.on_timer) {
          win->cb.on_timer(win->cb.data, win, id);
        }
      }
      else {
        BK_timer(win->ctx, id);
      }
    }
  }
}

/* One pass: events, wake, timers, a frame. `block`: sleep until there is something to do. */
static void step(int block)
{
  BKWindow *win = g_win && g_win->open ? g_win : NULL;
  struct android_poll_source *src;
  int events;
  int timeout = block ? wait_ms(win) : 0;
  while (ALooper_pollOnce(timeout, NULL, &events, (void **)&src) >= 0) {
    if (src) {
      src->process(g_app, src);
    }
    timeout = 0;
  }
  if (g_app->destroyRequested) {
    if (win) {
      win->open = 0;
      if (win->cb.on_close) {
        win->cb.on_close(win->cb.data, win);
      }
    }
    g_quit = 1;
    return;
  }
  if (atomic_exchange(&g_wake_flag, 0)) {
    if (g_wake_cb) {
      g_wake_cb();
    }
  }
  if (!win || !win->open) {
    return;
  }
  fire_timers(win);
  if (win->fling) {
    fling_step(win);
  }
  if (win->open && g_app->window &&
      (win->dirty || win->want_frame || BK_needs_render(win->ctx) || BK_animating(win->ctx))) {
    render_window(win);
  }
}

void BK_window_run(void)
{
  while (!g_quit && (open_windows() || g_keep_alive) && !(g_stop && g_stop())) {
    step(1);
  }
}

int BK_window_pump(void)
{
  step(0);
  return open_windows();
}

void BK_window_quit(void)
{
  g_quit = 1;
  if (g_app) {
    ALooper_wake(g_app->looper);
  }
}

void BK_window_set_stop_check(int (*stop)(void))
{
  g_stop = stop;
}

int bk_window_wake_ready(void)
{
  return g_app != NULL; /* the looper wakes from any thread */
}

int BK_window_wake_init(void (*cb)(void))
{
  g_wake_cb = cb;
  return bk_window_wake_ready();
}

void BK_window_wake(void)
{
  atomic_store(&g_wake_flag, 1); /* any thread: the loop calls the wake callback soon */
  if (g_app) {
    ALooper_wake(g_app->looper);
  }
}

void BK_window_set_keep_alive(int on)
{
  g_keep_alive = on;
}

/* -- BK_window_* -------------------------------------------------------------------------------- */

int BK_window_supported(void)
{
  return g_app != NULL; /* an activity is running: BK_android_attach was called */
}

BKWindow *BK_window_new(BKContext *ctx, const char *title, int width, int height)
{
  if (!g_app || g_win) { /* one surface, one window */
    return NULL;
  }
  BKWindow *win = (BKWindow *)calloc(1, sizeof(BKWindow));
  win->ctx = ctx;
  win->prev = ctx->host;
  win->raster = BUI_raster_create(1, 1);
  win->next_timer = 1;
  apply_density(win);
  win->req_w = width;
  win->req_h = height;
  win->w = (int)(width * win->scale);
  win->h = (int)(height * win->scale);
  win->sw = win->w;
  win->sh = win->h;
  win->open = 1;
  g_win = win;
  BKHost host = {win,           host_request,       host_set_cursor, host_warp,
                 host_clipboard_get, host_clipboard_set, host_schedule, host_emit};
  BK_context_set_host(ctx, &host);
  /* The activity starts before its surface exists: wait for it, so the app builds its UI at the
   * real size and scale, as it would in a desktop window. */
  struct android_poll_source *src;
  int events;
  const double give_up = now_ms() + 1000.0; /* the content rectangle follows the surface */
  while ((!g_app->window || g_app->contentRect.right <= 0) && !g_app->destroyRequested &&
         now_ms() < give_up) {
    if (ALooper_pollOnce(50, NULL, &events, (void **)&src) >= 0 && src) {
      src->process(g_app, src);
    }
  }
  if (g_app->window) {
    surface_changed(win);
  }
  else {
    BK_configure(ctx, win->w, win->h);
  }
  (void)title;
  return win;
}

void BK_window_free(BKWindow *win)
{
  if (!win) {
    return;
  }
  if (win->ctx) {
    BK_context_set_host(win->ctx, &win->prev);
  }
  if (g_win == win) {
    g_win = NULL;
  }
  BUI_raster_free(win->raster);
  free(win->clipboard);
  free(win);
}

void BK_window_set_callbacks(BKWindow *win, const BKWindowCallbacks *cb)
{
  win->cb = *cb;
}

void BK_window_show(BKWindow *win)
{
  win->dirty = win->full = 1;
}

void BK_window_close(BKWindow *win)
{
  if (win->open) {
    win->open = 0;
    if (win->cb.on_close) {
      win->cb.on_close(win->cb.data, win);
    }
    ANativeActivity_finish(g_app->activity);
  }
}

int BK_window_is_open(BKWindow *win)
{
  return win->open;
}

void BK_window_set_title(BKWindow *win, const char *title) {}

void BK_window_set_size(BKWindow *win, int width, int height) {} /* the screen decides */

void BK_window_get_size(BKWindow *win, int *r_width, int *r_height)
{
  *r_width = (int)((float)win->w / win->scale);
  *r_height = (int)((float)win->h / win->scale);
}

BuiRaster *BK_window_raster(BKWindow *win)
{
  return win->raster;
}

void BK_window_set_gpu(BKWindow *win, int on) {} /* the software raster only */

int BK_window_gpu(BKWindow *win)
{
  return 0;
}

int BK_window_frames(BKWindow *win)
{
  return win->frames;
}

void BK_window_render_now(BKWindow *win)
{
  if (win->open && g_app->window) {
    win->dirty = 1;
    render_window(win);
  }
}

int BK_window_timer(BKWindow *win, int ms)
{
  const int id = win->next_timer++;
  return add_timer(win, ms, id, 1) ? id : 0;
}

void BK_window_cancel_timer(BKWindow *win, int id)
{
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (win->timers[i].due_ms != 0.0 && win->timers[i].id == id && win->timers[i].user) {
      win->timers[i].due_ms = 0.0;
    }
  }
}

void BK_window_request_frame(BKWindow *win, int vsync)
{
  win->want_frame = 1; /* frames are paced by the buffer queue either way */
  if (g_app) {
    ALooper_wake(g_app->looper);
  }
}

void *BK_window_handle(BKWindow *win)
{
  return g_app ? (void *)g_app->window : NULL; /* ANativeWindow * */
}

void BK_window_wm(BKWindow *win, const char *key, double a, double b)
{
  if (strcmp(key, "touch_drag") == 0) { /* the only request an Android window has */
    win->drag_custom = a != 0.0;
  }
}

int BK_window_wm_get(BKWindow *win, const char *key, double *r_a, double *r_b)
{
  *r_a = *r_b = 0.0;
  if (strcmp(key, "scale") == 0) {
    *r_a = win->density;
    return 1;
  }
  if (strcmp(key, "screen") == 0) {
    *r_a = (double)win->sw / win->density;
    *r_b = (double)win->sh / win->density;
    return 1;
  }
  if (strcmp(key, "pointer") == 0) {
    *r_a = win->px;
    *r_b = win->py;
    return 1;
  }
  return 0;
}

#endif /* BUI_WINDOW_ANDROID */
