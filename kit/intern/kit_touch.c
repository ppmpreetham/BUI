/* Touch screens (BK_touch): the contacts a host sends, turned into what the widgets know.
 *
 * Blender has no touch screen input; this follows what X11, Wayland compositors and Windows do
 * for windows that are not touch aware (the first finger is the mouse), and gives two fingers
 * the trackpad's events, GHOST_kTrackpadEventScroll / Magnify / Rotate (MOUSEPAN, MOUSEZOOM,
 * MOUSEROTATE in Blender), about the fingers' centre:
 *  - one finger: motion to it, press, drags, release, then the pointer leaves (no hover stays);
 *  - a second finger: the first one's interaction is cancelled as Escape cancels a drag
 *    (kit_core.c's in_BK_key) and released away from every widget, so nothing is clicked;
 *    then the two are a gesture until either lifts, and no finger is the pointer again until
 *    all of them lifted (a lifted pinch must not click what is under the remaining finger);
 *  - a custom node that asked for raw touches (BK_custom_set_touch) gets each contact that began
 *    over it, and nothing else does.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <math.h>
#include <string.h>

#include "kit_intern.h"

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

#define OFF (-100000) /* where the pointer goes when its finger lifts, as hosts send a leave */

void bk_touch_init(BKContext *ctx)
{
  memset(&ctx->touch, 0, sizeof(ctx->touch));
  ctx->touch.pointer = ctx->touch.g0 = ctx->touch.g1 = -1;
}

static int px(float v)
{
  return (int)floorf(v + 0.5f);
}

static int find_point(const BKTouch *t, int id)
{
  for (int i = 0; i < BK_MAX_TOUCH; i++) {
    if (t->pts[i].role != BK_TP_FREE && t->pts[i].id == id) {
      return i;
    }
  }
  return -1;
}

/* How many default-path contacts other than `self` are down; *r_other is one of them. */
static int count_others(const BKTouch *t, int self, int *r_other)
{
  int n = 0;
  for (int i = 0; i < BK_MAX_TOUCH; i++) {
    if (i != self && t->pts[i].role == BK_TP_DEFAULT) {
      *r_other = i;
      n++;
    }
  }
  return n;
}

static bool any_default(const BKTouch *t)
{
  for (int i = 0; i < BK_MAX_TOUCH; i++) {
    if (t->pts[i].role == BK_TP_DEFAULT) {
      return true;
    }
  }
  return false;
}

/* The custom node taking raw touches at x, y: the nearest custom node there, if it asked. */
static BKNode *raw_target(BKContext *ctx, int x, int y)
{
  if (ctx->modal || ctx->npopups) { /* the press path closes popups / feeds the tool */
    return NULL;
  }
  for (BKNode *n = bk_ctx_hit(ctx, x, y); n; n = n->parent) {
    if (BK_IS_CUSTOM(n)) {
      return NODE_ENABLED(n) && bk_custom_takes_touch(n) ? n : NULL;
    }
  }
  return NULL;
}

static int deliver_raw(BKContext *ctx, const BKTouchPoint *p, int phase, int mods)
{
  ctx->in_input++;
  const int r = bk_custom_touch(p->raw, p->id, phase, px(p->x), px(p->y), mods);
  ctx->in_input--;
  bk_views_check(ctx);
  return r;
}

/* The pointer's finger no longer is one: undo what it did, as Escape does to a drag, and end the
 * press away from every widget (buttons are not clicked, number fields do not start editing).
 * Unlike Escape this also cancels a press that has not moved yet: its release would still act
 * (an unmoved box-select press in the node editor deselects everything). */
static void abort_pointer(BKContext *ctx, int mods)
{
  BKTouch *t = &ctx->touch;
  if (t->pointer < 0) {
    return;
  }
  t->pointer = -1;
  if (ctx->grab && ctx->grab->cls->cancel) {
    ctx->grab->cls->cancel(ctx->grab);
    ctx->has_ungrab = false;
  }
  BK_release(ctx, OFF, OFF, mods);
  BK_motion(ctx, OFF, OFF, BK_FROM_TOUCH);
}

static void gesture_metrics(const BKTouch *t, float *r_cx, float *r_cy, float *r_dist,
                            float *r_ang)
{
  const BKTouchPoint *a = &t->pts[t->g0], *b = &t->pts[t->g1];
  *r_cx = 0.5f * (a->x + b->x);
  *r_cy = 0.5f * (a->y + b->y);
  *r_dist = hypotf(b->x - a->x, b->y - a->y);
  *r_ang = atan2f(b->y - a->y, b->x - a->x);
}

static void gesture_begin(BKContext *ctx, int i0, int i1)
{
  BKTouch *t = &ctx->touch;
  t->g0 = i0;
  t->g1 = i1;
  t->latched = true;
  gesture_metrics(t, &t->cx, &t->cy, &t->dist, &t->ang);
  t->start_x = px(t->cx);
  t->start_y = px(t->cy);
  t->target = bk_ctx_hit(ctx, t->start_x, t->start_y);
  t->rest_x = t->rest_y = 0.0f;
  t->held = 0;
}

static void gesture_end(BKTouch *t)
{
  t->g0 = t->g1 = -1;
  t->target = NULL;
  t->held = 0;
}

/* A pan nothing took: the region or scroll view under the gesture's start scrolls one wheel
 * notch per 2 widget units the fingers moved (in_BK_wheel's step), the content following them. */
static int pan_steps(BKContext *ctx, float dx, float dy, int mods)
{
  BKTouch *t = &ctx->touch;
  const float step = 2.0f * bk_unit();
  int r = 0;
  t->rest_x += dx;
  t->rest_y += dy;
  for (int i = 0; i < 8 && fabsf(t->rest_y) >= step; i++) { /* down: the content goes down */
    const int d = t->rest_y > 0.0f ? 1 : -1;
    t->rest_y -= d * step;
    r |= BK_wheel(ctx, t->start_x, t->start_y, d, mods & ~BK_SHIFT);
  }
  /* Shift + wheel, as BK_scroll: with BK_FROM_TOUCH it reaches bindings only (in_BK_wheel) */
  for (int i = 0; i < 8 && fabsf(t->rest_x) >= step; i++) {
    const int d = t->rest_x > 0.0f ? 1 : -1;
    t->rest_x -= d * step;
    r |= BK_wheel(ctx, t->start_x, t->start_y, d, mods | BK_SHIFT);
  }
  return r;
}

static int gesture_update(BKContext *ctx, int mods)
{
  BKTouch *t = &ctx->touch;
  float cx, cy, dist, ang;
  gesture_metrics(t, &cx, &cy, &dist, &ang);
  const float dx = cx - t->cx, dy = cy - t->cy;
  const float magnify = t->dist > 1.0f ? dist / t->dist - 1.0f : 0.0f;
  float turn = ang - t->ang;
  while (turn > (float)M_PI) {
    turn -= 2.0f * (float)M_PI;
  }
  while (turn < -(float)M_PI) {
    turn += 2.0f * (float)M_PI;
  }
  /* y points down on screen: a counter-clockwise turn lowers the angle */
  const float degrees = -turn * (180.0f / (float)M_PI);
  t->cx = cx;
  t->cy = cy;
  t->dist = dist;
  t->ang = ang;
  const int x = px(cx), y = px(cy);
  int r = 0;
  if (dx != 0.0f || dy != 0.0f) {
    r |= bk_gesture(ctx, t->target, x, y, BK_GESTURE_PAN, dx, dy, mods) ||
         pan_steps(ctx, dx, dy, mods);
  }
  if (magnify != 0.0f) {
    r |= bk_gesture(ctx, t->target, x, y, BK_GESTURE_MAGNIFY, magnify, 0.0f, mods);
  }
  if (degrees != 0.0f) {
    r |= bk_gesture(ctx, t->target, x, y, BK_GESTURE_ROTATE, degrees, 0.0f, mods);
  }
  return r;
}

static int touch_down(BKContext *ctx, int id, float x, float y, int mods)
{
  BKTouch *t = &ctx->touch;
  int i = 0;
  while (i < BK_MAX_TOUCH && t->pts[i].role != BK_TP_FREE) {
    i++;
  }
  if (i == BK_MAX_TOUCH) {
    return 0;
  }
  BKTouchPoint *p = &t->pts[i];
  p->id = id;
  p->x = x;
  p->y = y;
  p->raw = raw_target(ctx, px(x), px(y));
  if (p->raw) {
    p->role = BK_TP_RAW;
    return deliver_raw(ctx, p, BK_TOUCH_DOWN, mods);
  }
  p->role = BK_TP_DEFAULT;
  const int tm = mods | BK_FROM_TOUCH;
  int other = -1;
  const int others = count_others(t, i, &other);
  if (others == 0 && !t->latched) {
    t->pointer = i;
    BK_motion(ctx, px(x), px(y), tm);
    BK_press(ctx, px(x), px(y), tm | BK_BUTTON1);
    return 1;
  }
  if (others == 1 && t->g0 < 0 && !ctx->modal) {
    abort_pointer(ctx, tm);
    gesture_begin(ctx, other, i);
    return 1;
  }
  return 0; /* a third finger, or one while a tool runs: followed, not used */
}

/* MOVE, UP or CANCEL of contact i. */
static int touch_update(BKContext *ctx, int i, int phase, int mods)
{
  BKTouch *t = &ctx->touch;
  BKTouchPoint *p = &t->pts[i];
  const int tm = mods | BK_FROM_TOUCH;
  const int x = px(p->x), y = px(p->y);
  if (p->role == BK_TP_RAW) {
    return deliver_raw(ctx, p, phase, mods);
  }
  if (p->role != BK_TP_DEFAULT) {
    return 0;
  }
  if (i == t->pointer) {
    if (phase == BK_TOUCH_MOVE) {
      return BK_motion(ctx, x, y, tm | BK_BUTTON1);
    }
    if (phase == BK_TOUCH_CANCEL) {
      abort_pointer(ctx, tm);
      return 1;
    }
    t->pointer = -1;
    BK_release(ctx, x, y, tm);
    BK_motion(ctx, OFF, OFF, BK_FROM_TOUCH);
    return 1;
  }
  if (i == t->g0 || i == t->g1) {
    /* One update per pair of moves: hosts report the fingers one by one, so updating after each
     * would make a pure pan wobble the spread and angle between them. A finger that moves again
     * while the other stays put, or lifts, sends what it held. */
    const int bit = i == t->g0 ? 1 : 2;
    if (phase == BK_TOUCH_MOVE && !t->held) {
      t->held = bit;
      return 0;
    }
    /* an up sends the move it held; a cancel drops it, as it drops the rest of the contact */
    const int r = t->held && phase != BK_TOUCH_CANCEL ? gesture_update(ctx, tm) : 0;
    t->held = phase == BK_TOUCH_MOVE && t->held == bit ? bit : 0;
    if (phase == BK_TOUCH_MOVE) {
      return r;
    }
    gesture_end(t);
    return 1;
  }
  return 0;
}

int BK_touch(BKContext *ctx, int id, int phase, float x, float y, int mods)
{
  BKTouch *t = &ctx->touch;
  mods &= ~(BK_BUTTON1 | BK_BUTTON2 | BK_BUTTON3 | BK_FROM_TOUCH);
  int i = find_point(t, id);
  if (phase == BK_TOUCH_DOWN) {
    int r = 0;
    if (i >= 0) { /* its up was lost: what it did is not kept */
      r |= touch_update(ctx, i, BK_TOUCH_CANCEL, mods);
      t->pts[i].role = BK_TP_FREE;
    }
    return touch_down(ctx, id, x, y, mods) | r;
  }
  if (i < 0 || phase < BK_TOUCH_MOVE || phase > BK_TOUCH_CANCEL) {
    return 0;
  }
  BKTouchPoint *p = &t->pts[i];
  p->x = x;
  p->y = y;
  const int r = touch_update(ctx, i, phase, mods);
  if (phase != BK_TOUCH_MOVE) {
    p->role = BK_TP_FREE;
    p->raw = NULL;
    if (!any_default(t)) {
      t->latched = false;
    }
  }
  return r;
}
