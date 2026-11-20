/* Damage: after a layout change (text that grows, a child added or removed, a node hidden,
 * a panel collapsed, a tab switched) the partial frame equals the same frame drawn in full,
 * and only the changed part is redrawn.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BUI_kit.h"
#include "GPU_raster.h"

enum { W = 320, H = 480 };

static int g_fail = 0;
#define CHECK(c) \
  do { \
    if (!(c)) { \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
      g_fail = 1; \
    } \
  } while (0)

static uint32_t g_partial[W * H];

/* Renders the pending damage, then the whole window: the pixels that differ. */
static int missed(BKContext *ctx, BuiRaster *r, int *regions)
{
  *regions = BK_render(ctx, r, W, H, 0);
  memcpy(g_partial, BUI_raster_pixels(r), sizeof(g_partial));
  BK_render(ctx, r, W, H, 1);
  const uint32_t *px = BUI_raster_pixels(r);
  int n = 0;
  for (int i = 0; i < W * H; i++) {
    n += g_partial[i] != px[i];
  }
  return n;
}

static BKNode *add(BKNode *parent, const char *type, const char *text)
{
  BKNode *n = BK_node_new(type);
  if (text) {
    BK_set_str(n, "text", text);
  }
  BK_node_add(parent, n, -1);
  return n;
}

#define STEP(what) \
  do { \
    int regions; \
    const int m = missed(ctx, r, &regions); \
    if (m) { \
      printf("FAIL %s: %d pixels missed\n", what, m); \
      g_fail = 1; \
    } \
    CHECK(regions > 0); \
  } while (0)

int main(void)
{
  BK_set_animations(0);
  BKNode *col = BK_node_new("column");
  BKNode *label = add(col, "label", "Short");
  BKNode *button = add(col, "button", "Button");
  add(col, "number", "Number");
  BKNode *panel = add(col, "panel", "Panel");
  add(panel, "checkbox", "Inside");
  add(panel, "slider", "Slider");
  BKNode *tabs = add(col, "tabs", NULL);
  add(add(tabs, "column", "One"), "button", "First tab");
  add(add(tabs, "column", "Two"), "text", "Second tab");
  add(col, "button", "Last");
  BKContext *ctx = BK_context_new(col);
  BuiRaster *r = BUI_raster_create(W, H);
  BK_render(ctx, r, W, H, 1);
  CHECK(BK_render(ctx, r, W, H, 0) == 0); /* nothing to redraw */

  BK_set_str(label, "text", "A label that is much longer than before");
  STEP("text grows");
  BKNode *extra = add(col, "button", "Added");
  STEP("child added");
  BK_node_remove(col, extra);
  STEP("child removed");
  BK_set_num(button, "visible", 0);
  STEP("hidden");
  BK_set_num(button, "visible", 1);
  STEP("shown");
  BK_set_num(panel, "collapsed", 1);
  STEP("panel collapsed");
  BK_set_num(panel, "collapsed", 0);
  STEP("panel opened");
  BK_set_num(tabs, "current", 1);
  STEP("tab switched");

  BUI_raster_free(r);
  BK_context_free(ctx);
  if (!g_fail) {
    printf("damage_test: ok\n");
  }
  return g_fail;
}
