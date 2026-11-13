/* Touch screens and trackpad gestures (BK_touch, BK_magnify, BK_rotate) driven by synthetic
 * contacts, the way a window host feeds them: one finger is the pointer, a second cancels it and
 * makes a gesture (pan, magnify, rotate), raw contacts reach custom nodes that ask for them.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BUI_kit.h"
#include "GPU_raster.h"

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

static int g_fail = 0;
#define CHECK(c) \
  do { \
    if (!(c)) { \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
      g_fail = 1; \
    } \
  } while (0)

enum { W = 400, H = 300 };

static BuiRaster *g_raster;
static int g_clicks;
static int g_warps;

static void host_noop(void *data) {}
static void host_cursor(void *data, int c) {}
static void host_warp(void *data, int x, int y)
{
  g_warps++;
}
static char *host_clip_get(void *data)
{
  return NULL;
}
static void host_clip_set(void *data, const char *t) {}
static void host_schedule(void *data, int ms, int timer) {}
static void host_emit(void *data, BKNode *node, int kind, void *ref, double a, double b)
{
  if (kind == BK_EV_CLICK) {
    g_clicks++;
  }
}

static BKContext *context(BKNode *root)
{
  BKContext *ctx = BK_context_new(root);
  const BKHost host = {NULL,          host_noop,     host_cursor, host_warp,
                       host_clip_get, host_clip_set, host_schedule, host_emit};
  BK_context_set_host(ctx, &host);
  BK_render(ctx, g_raster, W, H, 1);
  return ctx;
}

static void centre(BKNode *n, float *r_x, float *r_y)
{
  int r[4];
  BK_node_rect(n, r);
  *r_x = r[0] + r[2] * 0.5f;
  *r_y = r[1] + r[3] * 0.5f;
}

/* -- a custom node that records what it gets -- */

typedef struct Log {
  int press, release, motion, wheel, magnify, rotate, touch_down, touch_move, touch_up,
      touch_cancel;
  int touch_mods; /* BK_FROM_TOUCH seen on pointer events */
  float wheel_y, magnify_sum, rotate_sum;
  int last_id;
} Log;

static int log_event(void *data, BKNode *node, const BKCustomEvent *e)
{
  Log *l = (Log *)data;
  switch (e->type) {
    case BK_CUSTOM_PRESS:
      l->press++;
      l->touch_mods |= e->mods & BK_FROM_TOUCH;
      break;
    case BK_CUSTOM_RELEASE:
      l->release++;
      break;
    case BK_CUSTOM_MOTION:
      l->motion++;
      break;
    case BK_CUSTOM_WHEEL:
      l->wheel++;
      l->wheel_y += e->wheel_y;
      break;
    case BK_CUSTOM_MAGNIFY:
      l->magnify++;
      l->magnify_sum += e->magnify;
      break;
    case BK_CUSTOM_ROTATE:
      l->rotate++;
      l->rotate_sum += e->rotate;
      break;
    case BK_CUSTOM_TOUCH:
      l->last_id = e->touch_id;
      l->touch_down += e->touch_phase == BK_TOUCH_DOWN;
      l->touch_move += e->touch_phase == BK_TOUCH_MOVE;
      l->touch_up += e->touch_phase == BK_TOUCH_UP;
      l->touch_cancel += e->touch_phase == BK_TOUCH_CANCEL;
      break;
  }
  return 1;
}

static BKNode *custom_node(Log *l)
{
  BKNode *n = BK_node_new("custom");
  BKCustomClass cls;
  memset(&cls, 0, sizeof(cls));
  cls.event = log_event;
  cls.natural_w = W;
  cls.natural_h = 200;
  BK_custom_set(n, &cls, l);
  return n;
}

/* -- one finger: the pointer -- */

static void test_tap_and_cancel(void)
{
  BKNode *col = BK_node_new("column");
  BKNode *button = BK_node_new("button");
  BK_set_str(button, "label", "Tap");
  BK_node_add(col, button, -1);
  BKContext *ctx = context(col);
  float x, y;
  centre(button, &x, &y);

  g_clicks = 0;
  CHECK(BK_touch(ctx, 7, BK_TOUCH_DOWN, x, y, 0));
  CHECK(BK_touch(ctx, 7, BK_TOUCH_UP, x, y, 0));
  CHECK(g_clicks == 1);
  CHECK(BK_hover(ctx) == NULL); /* a lifted finger leaves no hover behind */

  /* the platform took the contact back: no click */
  g_clicks = 0;
  BK_touch(ctx, 8, BK_TOUCH_DOWN, x, y, 0);
  BK_touch(ctx, 8, BK_TOUCH_CANCEL, x, y, 0);
  CHECK(g_clicks == 0);

  /* a second finger turns the press into a gesture: no click, then or after */
  g_clicks = 0;
  BK_touch(ctx, 1, BK_TOUCH_DOWN, x, y, 0);
  BK_touch(ctx, 2, BK_TOUCH_DOWN, x + 40, y, 0);
  BK_touch(ctx, 2, BK_TOUCH_UP, x + 40, y, 0);
  BK_touch(ctx, 1, BK_TOUCH_MOVE, x + 1, y, 0); /* still latched: not the pointer */
  BK_touch(ctx, 1, BK_TOUCH_UP, x + 1, y, 0);
  CHECK(g_clicks == 0);

  /* after every finger lifted, a tap is a tap again */
  BK_touch(ctx, 3, BK_TOUCH_DOWN, x, y, 0);
  BK_touch(ctx, 3, BK_TOUCH_UP, x, y, 0);
  CHECK(g_clicks == 1);

  /* moves and ups of unknown contacts are ignored */
  CHECK(BK_touch(ctx, 99, BK_TOUCH_MOVE, x, y, 0) == 0);
  CHECK(BK_touch(ctx, 99, BK_TOUCH_UP, x, y, 0) == 0);
  BK_context_free(ctx);
  BK_node_free(col);
}

static void test_drag_cancelled_by_second_finger(void)
{
  BKNode *col = BK_node_new("column");
  BKNode *num = BK_node_new("number");
  BK_set_num(num, "value", 5.0);
  BK_node_add(col, num, -1);
  BKContext *ctx = context(col);
  float x, y;
  centre(num, &x, &y);

  BK_touch(ctx, 1, BK_TOUCH_DOWN, x, y, 0);
  for (int i = 1; i <= 10; i++) {
    BK_touch(ctx, 1, BK_TOUCH_MOVE, x + i * 8, y, 0);
  }
  CHECK(BK_get_num(num, "value") != 5.0); /* one finger drags the value */
  BK_touch(ctx, 2, BK_TOUCH_DOWN, x, y + 30, 0);
  CHECK(BK_get_num(num, "value") == 5.0); /* the second finger cancels it, as Escape */
  BK_touch(ctx, 1, BK_TOUCH_UP, x + 80, y, 0);
  BK_touch(ctx, 2, BK_TOUCH_UP, x, y + 30, 0);
  CHECK(BK_get_num(num, "value") == 5.0);
  CHECK(BK_focus(ctx) == NULL); /* and does not start editing the text */
  BK_context_free(ctx);
  BK_node_free(col);
}

/* a tap opens a menu, which stays open with no finger down; a tap on a row picks it */
static void test_menu(void)
{
  BKNode *col = BK_node_new("column");
  BKNode *dd = BK_node_new("dropdown");
  const char *items[] = {"One", "Two", "Three"};
  BK_set_strs(dd, "items", items, 3);
  BK_node_add(col, dd, -1);
  BKContext *ctx = context(col);
  float x, y;
  centre(dd, &x, &y);
  BK_touch(ctx, 1, BK_TOUCH_DOWN, x, y, 0);
  BK_touch(ctx, 1, BK_TOUCH_UP, x, y, 0);
  BK_render(ctx, g_raster, W, H, 0);
  CHECK(BK_popup_count(ctx) == 1);
  if (BK_popup_count(ctx) == 1) {
    BKNode *menu = BK_popup(ctx, 0);
    const int rows = BK_menu_row_count(menu);
    int r[4];
    BK_menu_row_rect(menu, rows - 1, r);
    const float mx = r[0] + r[2] * 0.5f, my = r[1] + r[3] * 0.5f;
    BK_touch(ctx, 2, BK_TOUCH_DOWN, mx, my, 0);
    BK_touch(ctx, 2, BK_TOUCH_UP, mx, my, 0);
    BK_render(ctx, g_raster, W, H, 0);
    CHECK(BK_popup_count(ctx) == 0);
    CHECK(BK_get_num(dd, "value") != 0.0);
  }
  BK_context_free(ctx);
  BK_node_free(col);
}

/* -- two fingers: gestures -- */

static void test_pan_scrolls_region(void)
{
  BKNode *col = BK_node_new("column");
  BKNode *first = NULL;
  for (int i = 0; i < 60; i++) {
    BKNode *b = BK_node_new("button");
    BK_set_str(b, "label", "Row");
    BK_node_add(col, b, -1);
    first = first ? first : b;
  }
  BKContext *ctx = context(col);
  int r0[4], r1[4];
  BK_node_rect(first, r0);
  g_clicks = 0;
  /* two fingers move up together: the content follows them */
  BK_touch(ctx, 1, BK_TOUCH_DOWN, 180, 200, 0);
  BK_touch(ctx, 2, BK_TOUCH_DOWN, 220, 200, 0);
  for (int i = 1; i <= 10; i++) {
    BK_touch(ctx, 1, BK_TOUCH_MOVE, 180, 200 - i * 12, 0);
    BK_touch(ctx, 2, BK_TOUCH_MOVE, 220, 200 - i * 12, 0);
  }
  BK_touch(ctx, 1, BK_TOUCH_UP, 180, 80, 0);
  BK_touch(ctx, 2, BK_TOUCH_UP, 220, 80, 0);
  BK_render(ctx, g_raster, W, H, 1);
  BK_node_rect(first, r1);
  CHECK(r1[1] < r0[1]);
  CHECK(r0[1] - r1[1] > 60); /* about the 120 px the fingers moved */
  CHECK(g_clicks == 0);
  BK_context_free(ctx);
  BK_node_free(col);
}

static void test_node_editor(void)
{
  BKNode *editor = BK_node_new("nodeeditor");
  BKContext *ctx = context(editor);
  const double zoom0 = BK_get_num(editor, "zoom");

  /* spread: zoom in about the fingers */
  BK_touch(ctx, 1, BK_TOUCH_DOWN, 150, 150, 0);
  BK_touch(ctx, 2, BK_TOUCH_DOWN, 250, 150, 0);
  BK_touch(ctx, 1, BK_TOUCH_MOVE, 125, 150, 0);
  BK_touch(ctx, 2, BK_TOUCH_MOVE, 275, 150, 0);
  CHECK(BK_get_num(editor, "zoom") > zoom0 * 1.3);
  const double zoom1 = BK_get_num(editor, "zoom");
  const double panx1 = BK_get_num(editor, "panx");
  /* together to the right: the view pans with them, 40 px (not a wheel zoom) */
  BK_touch(ctx, 1, BK_TOUCH_MOVE, 165, 150, 0);
  BK_touch(ctx, 2, BK_TOUCH_MOVE, 315, 150, 0);
  CHECK(fabs(BK_get_num(editor, "panx") - (panx1 + 40.0)) < 1e-3);
  CHECK(fabs(BK_get_num(editor, "zoom") - zoom1) < 1e-6);
  BK_touch(ctx, 1, BK_TOUCH_UP, 165, 150, 0);
  BK_touch(ctx, 2, BK_TOUCH_UP, 315, 150, 0);

  /* a trackpad pinch (BK_magnify) zooms the same way */
  const double zoom2 = BK_get_num(editor, "zoom");
  CHECK(BK_magnify(ctx, 200, 150, -0.2f, 0));
  CHECK(BK_get_num(editor, "zoom") < zoom2);
  CHECK(BK_rotate(ctx, 200, 150, 10.0f, 0) == 0); /* the editor does not rotate */
  CHECK(BK_magnify(ctx, 200, 150, 0.0f, 0) == 0);
  BK_context_free(ctx);
  BK_node_free(editor);
}

/* Two nodes, A's output linked into B's input; the view at zoom 1, pan (50, 50). */
static BKNode *graph(BKNode **r_b)
{
  BKNode *editor = BK_node_new("nodeeditor");
  BKNode *a = BK_node_new("graphnode"), *b = BK_node_new("graphnode");
  const char *a_items[] = {"o|Value|float"};
  const char *b_items[] = {"o|Result|float", "i|A|float|0.5"};
  BK_set_strs(a, "items", a_items, 1);
  BK_set_strs(b, "items", b_items, 2);
  BK_set_num(b, "nx", 300.0);
  BK_node_add(editor, a, -1);
  BK_node_add(editor, b, -1);
  const char *links[] = {"0 0 1 0"};
  BK_set_strs(editor, "items", links, 1);
  *r_b = b;
  return editor;
}

static void place_view(BKContext *ctx, BKNode *editor)
{
  BK_set_num(editor, "zoom", 1.0);
  BK_set_num(editor, "panx", 50.0);
  BK_set_num(editor, "pany", 50.0);
  BK_render(ctx, g_raster, W, H, 1);
}

/* A second finger keeps what the first one's press began from changing the graph. */
static void test_node_editor_cancel(void)
{
  BKNode *b;
  BKNode *editor = graph(&b);
  BKContext *ctx = context(editor);
  place_view(ctx, editor);
  int r[4];
  BK_node_rect(editor, r);
  /* B's input socket: its left edge, below the header and the output row */
  const float sx = r[0] + 50 + 300, sy = r[1] + 50 + (20 + 5 + 22 + 10);

  /* pressing a linked input picks its link up; a pinch must put it back */
  BK_touch(ctx, 1, BK_TOUCH_DOWN, sx, sy, 0);
  CHECK(BK_strs_count(editor, "items") == 0); /* picked up */
  BK_touch(ctx, 2, BK_TOUCH_DOWN, sx + 60, sy + 60, 0);
  CHECK(BK_strs_count(editor, "items") == 1);
  BK_touch(ctx, 1, BK_TOUCH_UP, sx, sy, 0);
  BK_touch(ctx, 2, BK_TOUCH_UP, sx + 60, sy + 60, 0);
  CHECK(BK_strs_count(editor, "items") == 1);
  /* the same once the link was dragged away */
  BK_touch(ctx, 1, BK_TOUCH_DOWN, sx, sy, 0);
  for (int i = 1; i <= 5; i++) {
    BK_touch(ctx, 1, BK_TOUCH_MOVE, sx - i * 10, sy + i * 6, 0);
  }
  BK_touch(ctx, 2, BK_TOUCH_DOWN, sx + 60, sy + 60, 0);
  CHECK(BK_strs_count(editor, "items") == 1);
  BK_touch(ctx, 1, BK_TOUCH_UP, sx, sy, 0);
  BK_touch(ctx, 2, BK_TOUCH_UP, sx + 60, sy + 60, 0);
  CHECK(BK_strs_count(editor, "items") == 1);

  /* a pinch that begins on empty canvas keeps the selection (an unmoved box press deselects) */
  BK_set_num(b, "selected", 1.0);
  BK_touch(ctx, 1, BK_TOUCH_DOWN, r[0] + 60.0f, r[1] + H - 40.0f, 0);
  BK_touch(ctx, 2, BK_TOUCH_DOWN, r[0] + 160.0f, r[1] + H - 40.0f, 0);
  BK_touch(ctx, 1, BK_TOUCH_UP, r[0] + 60.0f, r[1] + H - 40.0f, 0);
  BK_touch(ctx, 2, BK_TOUCH_UP, r[0] + 160.0f, r[1] + H - 40.0f, 0);
  CHECK(BK_get_num(b, "selected") == 1.0);
  BK_context_free(ctx);
  BK_node_free(editor);
}

/* A finger at the window edge does not wrap like the hidden mouse pointer does: the host's warp
 * moves the mouse, not the finger, so the drag would run away. */
static void test_drag_at_edge(void)
{
  BKNode *col = BK_node_new("column");
  BKNode *num = BK_node_new("number");
  BK_set_num(num, "value", 0.0);
  BK_node_add(col, num, -1);
  BKContext *ctx = context(col);
  float x, y;
  centre(num, &x, &y);
  g_warps = 0;
  BK_touch(ctx, 1, BK_TOUCH_DOWN, x, y, 0);
  for (int i = 1; i <= 20; i++) {
    BK_touch(ctx, 1, BK_TOUCH_MOVE, x + (W - 4 - x) * i / 20.0f, y, 0);
  }
  const double at_edge = BK_get_num(num, "value");
  CHECK(at_edge > 0.0);
  for (int i = 0; i < 5; i++) { /* the finger rests there */
    BK_touch(ctx, 1, BK_TOUCH_MOVE, W - 4, y, 0);
  }
  CHECK(BK_get_num(num, "value") == at_edge);
  BK_touch(ctx, 1, BK_TOUCH_UP, W - 4, y, 0);
  CHECK(BK_get_num(num, "value") == at_edge);
  CHECK(g_warps == 0); /* the mouse pointer was never moved, not even back on release */
  BK_context_free(ctx);
  BK_node_free(col);
}

/* -- custom nodes -- */

static void test_custom_default(void)
{
  Log l;
  memset(&l, 0, sizeof(l));
  BKNode *col = BK_node_new("column");
  BKNode *c = custom_node(&l);
  BK_node_add(col, c, -1);
  BKContext *ctx = context(col);
  float x, y;
  centre(c, &x, &y);

  BK_touch(ctx, 1, BK_TOUCH_DOWN, x, y, 0);
  BK_touch(ctx, 1, BK_TOUCH_MOVE, x + 5, y, 0);
  BK_touch(ctx, 1, BK_TOUCH_UP, x + 5, y, 0);
  CHECK(l.press == 1 && l.release == 1 && l.motion >= 1);
  CHECK(l.touch_mods == BK_FROM_TOUCH);
  CHECK(l.touch_down == 0);

  /* two fingers: the first press is released, then pan, magnify and rotate arrive */
  memset(&l, 0, sizeof(l));
  BK_touch(ctx, 1, BK_TOUCH_DOWN, x - 40, y, 0);
  BK_touch(ctx, 2, BK_TOUCH_DOWN, x + 40, y, 0);
  CHECK(l.press == 1 && l.release == 1);
  BK_touch(ctx, 1, BK_TOUCH_MOVE, x - 40, y + 20, 0); /* down: wheel up */
  BK_touch(ctx, 2, BK_TOUCH_MOVE, x + 40, y + 20, 0);
  CHECK(l.wheel >= 1 && l.wheel_y > 0.0f);
  BK_touch(ctx, 1, BK_TOUCH_MOVE, x - 80, y + 20, 0); /* spread */
  BK_touch(ctx, 2, BK_TOUCH_MOVE, x + 80, y + 20, 0);
  CHECK(l.magnify >= 1 && l.magnify_sum > 0.5f);
  /* turn counter-clockwise on screen: the right finger goes up, the left one down */
  const float a = 20.0f * (float)M_PI / 180.0f;
  BK_touch(ctx, 1, BK_TOUCH_MOVE, x - 80 * cosf(a), y + 20 + 80 * sinf(a), 0);
  BK_touch(ctx, 2, BK_TOUCH_MOVE, x + 80 * cosf(a), y + 20 - 80 * sinf(a), 0);
  CHECK(l.rotate >= 1 && fabsf(l.rotate_sum - 20.0f) < 0.5f);
  BK_touch(ctx, 1, BK_TOUCH_UP, 0, 0, 0);
  BK_touch(ctx, 2, BK_TOUCH_UP, 0, 0, 0);
  CHECK(l.press == 1); /* no second press while the gesture ran */

  /* trackpad gestures reach it too */
  memset(&l, 0, sizeof(l));
  CHECK(BK_magnify(ctx, (int)x, (int)y, 0.25f, 0) && l.magnify == 1 && l.magnify_sum == 0.25f);
  CHECK(BK_rotate(ctx, (int)x, (int)y, -15.0f, 0) && l.rotate == 1 && l.rotate_sum == -15.0f);
  BK_context_free(ctx);
  BK_node_free(col);
}

static void test_custom_raw(void)
{
  Log l;
  memset(&l, 0, sizeof(l));
  BKNode *col = BK_node_new("column");
  BKNode *c = custom_node(&l);
  BK_custom_set_touch(c, 1);
  BKNode *button = BK_node_new("button");
  BK_set_str(button, "label", "Below");
  BK_node_add(col, c, -1);
  BK_node_add(col, button, -1);
  BKContext *ctx = context(col);
  float x, y, bx, by;
  centre(c, &x, &y);
  centre(button, &bx, &by);

  /* three fingers on the node: each one arrives as it is, no pointer, no gesture */
  for (int id = 10; id < 13; id++) {
    BK_touch(ctx, id, BK_TOUCH_DOWN, x + (id - 10) * 30, y, 0);
  }
  CHECK(l.touch_down == 3 && l.press == 0 && l.last_id == 12);
  CHECK(BK_custom_has_focus(c)); /* the first finger focused it, as a click does */
  BK_touch(ctx, 11, BK_TOUCH_MOVE, x + 35, y + 10, 0);
  CHECK(l.touch_move == 1 && l.last_id == 11 && l.magnify == 0 && l.wheel == 0);

  /* a finger that lands outside is the pointer as usual, even meanwhile */
  g_clicks = 0;
  BK_touch(ctx, 20, BK_TOUCH_DOWN, bx, by, 0);
  BK_touch(ctx, 20, BK_TOUCH_UP, bx, by, 0);
  CHECK(g_clicks == 1);

  BK_touch(ctx, 10, BK_TOUCH_UP, x, y, 0);
  BK_touch(ctx, 12, BK_TOUCH_CANCEL, x, y, 0);
  CHECK(l.touch_up == 1 && l.touch_cancel == 1);

  /* the node goes away while a finger is on it: the rest of that contact is dropped */
  BK_node_remove(col, c);
  BK_node_free(c);
  CHECK(BK_touch(ctx, 11, BK_TOUCH_MOVE, x, y, 0) == 0);
  CHECK(BK_touch(ctx, 11, BK_TOUCH_UP, x, y, 0) == 0);
  BK_context_free(ctx);
  BK_node_free(col);
}

static void test_capacity(void)
{
  BKNode *col = BK_node_new("column");
  BKNode *button = BK_node_new("button");
  BK_node_add(col, button, -1);
  BKContext *ctx = context(col);
  for (int id = 0; id < 20; id++) { /* more fingers than followed: no harm */
    BK_touch(ctx, id, BK_TOUCH_DOWN, 10.0f + id, 10.0f, 0);
  }
  for (int id = 0; id < 20; id++) {
    BK_touch(ctx, id, BK_TOUCH_MOVE, 30.0f + id, 40.0f, 0);
    BK_touch(ctx, id, BK_TOUCH_UP, 30.0f + id, 40.0f, 0);
  }
  /* a down for a contact still down (its up was lost) ends the old one first */
  float x, y;
  centre(button, &x, &y);
  g_clicks = 0;
  BK_touch(ctx, 5, BK_TOUCH_DOWN, x, y, 0);
  BK_touch(ctx, 5, BK_TOUCH_DOWN, x, y, 0);
  BK_touch(ctx, 5, BK_TOUCH_UP, x, y, 0);
  CHECK(g_clicks == 1);
  BK_context_free(ctx);
  BK_node_free(col);
}

int main(void)
{
  g_raster = BUI_raster_create(W, H);
  test_tap_and_cancel();
  test_drag_cancelled_by_second_finger();
  test_menu();
  test_pan_scrolls_region();
  test_node_editor();
  test_node_editor_cancel();
  test_drag_at_edge();
  test_custom_default();
  test_custom_raw();
  test_capacity();
  BUI_raster_free(g_raster);
  printf(g_fail ? "touch_test: FAILED\n" : "touch_test: ok\n");
  return g_fail;
}
