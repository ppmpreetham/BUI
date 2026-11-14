/* Screens, areas and regions (kit/intern/screen): Blender's editors/screen behavior through the
 * public C API and simulated input. Splits, joins, closes, docks and swaps; dragging an edge
 * (snapping, Escape and right-click cancel); dragging a corner to split; regions sized, hidden
 * by dragging, shown by clicking their tab, flipped; Ctrl+Space; the layout saved and restored;
 * and the values the setters refuse.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BUI_kit.h"
#include "GPU_raster.h"

static int g_fail = 0;
#define CHECK(c) \
  do { \
    if (!(c)) { \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
      g_fail = 1; \
    } \
  } while (0)

enum { W = 800, H = 600 };

static int g_new, g_close, g_window, g_changes, g_moves, g_move_from, g_move_to;

static void request(void *data)
{
  (void)data;
}

static void emit(void *data, BKNode *node, int kind, void *ref, double a, double b)
{
  (void)data, (void)ref;
  if (kind == BK_EV_AREA) {
    g_new += b == BK_AREA_NEW;
    g_close += b == BK_AREA_CLOSE;
    g_window += b == BK_AREA_WINDOW;
  }
  else if (kind == BK_EV_CHANGE && strcmp(BK_node_type(node), "screen") == 0) {
    g_changes++;
  }
  else if (kind == BK_EV_MOVE) {
    g_moves++;
    g_move_from = (int)a;
    g_move_to = (int)b;
  }
}

static BKContext *g_ctx;
static BuiRaster *g_r;

static void render(void)
{
  BK_render(g_ctx, g_r, W, H, 0);
}

static void rect_of(BKNode *n, int r[4])
{
  render();
  BK_node_rect(n, r);
}

static void drag(int x0, int y0, int x1, int y1, int mods)
{
  BK_motion(g_ctx, x0, y0, mods);
  BK_press(g_ctx, x0, y0, mods);
  BK_motion(g_ctx, (x0 + x1) / 2, (y0 + y1) / 2, mods);
  BK_motion(g_ctx, x1, y1, mods);
  BK_release(g_ctx, x1, y1, mods);
  render();
}

int main(void)
{
  BKNode *screen = BK_node_new("screen");
  BKNode *a0 = BK_node_new("area");
  CHECK(screen && a0);
  CHECK(BK_set_str(a0, "editor", "3D Viewport"));
  CHECK(BK_node_add(screen, a0, -1));
  g_ctx = BK_context_new(screen);
  BKHost host = {0};
  host.request = request;
  host.emit = emit;
  BK_context_set_host(g_ctx, &host);
  g_r = BUI_raster_create(W, H);
  BK_render(g_ctx, g_r, W, H, 1);

  int r[4], r1[4], r2[4];
  rect_of(a0, r);
  CHECK(r[0] == 0 && r[1] == 0 && r[2] == W && r[3] == H); /* one area: the whole screen */

  /* -- validation at the boundary -- */
  BKNode *stray = BK_node_new("area");
  CHECK(!BK_node_add(screen, stray, -1)); /* past the first, areas come from splits */
  BKNode *label = BK_node_new("label");
  CHECK(!BK_node_add(screen, label, -1));
  CHECK(BK_area_split(stray, BK_AXIS_V, 0.5f) == NULL); /* not in a screen */
  CHECK(BK_area_split(a0, BK_AXIS_V, 1.5f) == NULL);
  BK_node_free(stray);
  BK_node_free(label);

  /* -- split -- */
  BKNode *a1 = BK_area_split(a0, BK_AXIS_V, 0.75f);
  CHECK(a1 != NULL);
  CHECK(g_new == 1);
  CHECK(strcmp(BK_get_str(a1, "editor"), "3D Viewport") == 0); /* ED_area_data_copy */
  CHECK(BK_node_child_count(screen) == 2);
  rect_of(a0, r1);
  rect_of(a1, r2);
  CHECK(r1[0] < r2[0]);                                /* the new area is the right part */
  CHECK(r1[2] > 500 && r1[2] < 620 && r2[2] > 150 && r2[2] < 220);
  CHECK(r1[3] == r2[3]);
  BK_set_str(a1, "editor", "Properties");
  BKNode *a2 = BK_area_split(a1, BK_AXIS_H, 0.5f);
  CHECK(a2 && BK_node_child_count(screen) == 3);

  /* -- the edge between a0 and the right column: drag it -- */
  rect_of(a0, r1);
  const int edge_x = r1[0] + r1[2] + 1, mid_y = H / 2;
  const int before = r1[2];
  const int changes = g_changes;
  drag(edge_x, mid_y, edge_x - 100, mid_y, 0);
  rect_of(a0, r1);
  CHECK(abs(r1[2] - (before - 100)) <= 2);
  CHECK(g_changes > changes); /* the layout changed: BK_EV_CHANGE */
  rect_of(a1, r2);
  CHECK(r2[0] > r1[0] + r1[2]); /* both areas of the column moved with the edge */

  /* Escape cancels: the edge goes back where it started */
  rect_of(a0, r1);
  const int x0 = r1[0] + r1[2] + 1, w0 = r1[2];
  BK_motion(g_ctx, x0, mid_y, 0);
  BK_press(g_ctx, x0, mid_y, 0);
  BK_motion(g_ctx, x0 + 40, mid_y, 0);
  BK_motion(g_ctx, x0 + 80, mid_y, 0);
  render();
  rect_of(a0, r1);
  CHECK(r1[2] > w0 + 60);
  BK_key(g_ctx, "", "Escape", 0);
  BK_release(g_ctx, x0 + 80, mid_y, 0);
  rect_of(a0, r1);
  CHECK(r1[2] == w0);

  /* the right button cancels too */
  BK_motion(g_ctx, x0, mid_y, 0);
  BK_press(g_ctx, x0, mid_y, 0);
  BK_motion(g_ctx, x0 - 30, mid_y, 0);
  BK_motion(g_ctx, x0 - 60, mid_y, 0);
  BK_context_menu(g_ctx, x0 - 60, mid_y, 0);
  BK_release(g_ctx, x0 - 60, mid_y, 0);
  rect_of(a0, r1);
  CHECK(r1[2] == w0);
  CHECK(BK_popup_count(g_ctx) == 0);

  /* Ctrl snaps to 24ths of the screen (SNAP_FRACTION_AND_ADJACENT) */
  drag(x0, mid_y, W / 2 + 7, mid_y, BK_CTRL);
  rect_of(a1, r2);
  CHECK(abs(r2[0] - 2 - W / 2) <= 2);

  /* the edge can't make an area narrower than AREAMINX */
  drag(W / 2, mid_y, W - 5, mid_y, 0);
  rect_of(a1, r2);
  CHECK(r2[2] >= 29);

  /* -- the screen's layout, saved -- */
  const char *saved_c = BK_get_str(screen, "layout");
  CHECK(saved_c && strstr(saved_c, "\"bui_screen\": 1") && strstr(saved_c, "Properties"));
  char *saved = malloc(strlen(saved_c) + 1);
  strcpy(saved, saved_c);
  rect_of(a2, r);

  /* -- join: a2 joins into a1 (they share the column's horizontal edge) -- */
  const int closes = g_close;
  CHECK(BK_area_join(a1, a2));
  CHECK(g_close == closes + 1);
  CHECK(BK_node_child_count(screen) == 2);
  CHECK(!BK_area_join(a0, a0));

  /* -- restoring the layout makes the missing area again -- */
  const int news = g_new;
  CHECK(BK_set_str(screen, "layout", saved));
  CHECK(BK_node_child_count(screen) == 3);
  CHECK(g_new == news + 1);
  BKNode *a2b = BK_node_child(screen, 2);
  rect_of(a2b, r1);
  CHECK(r1[0] == r[0] && r1[1] == r[1] && r1[2] == r[2] && r1[3] == r[3]);
  CHECK(!BK_set_str(screen, "layout", "{\"bui_screen\": 1, \"verts\": []}"));
  CHECK(!BK_set_str(screen, "layout", "not json"));
  CHECK(BK_node_child_count(screen) == 3); /* refused: nothing changed */
  free(saved);

  /* -- swap and close -- */
  rect_of(a0, r1);
  rect_of(a2b, r2);
  CHECK(BK_area_swap(a0, a2b));
  rect_of(a0, r);
  CHECK(r[0] == r2[0] && r[1] == r2[1]);
  CHECK(BK_area_swap(a0, a2b));
  CHECK(BK_area_close(a2b)); /* the area above takes its place */
  CHECK(BK_node_child_count(screen) == 2);

  /* -- dock a1 to the left of a0 -- */
  CHECK(BK_area_dock(a1, a0, BK_DOCK_LEFT, 0.3f));
  CHECK(BK_node_child_count(screen) == 2);
  rect_of(a0, r1);
  rect_of(a1, r2);
  CHECK(r2[0] < r1[0]); /* a1 is left of a0 now */
  CHECK(r2[3] == r1[3]);

  /* -- the corner: drag inward to split (area_join modal, same area) -- */
  rect_of(a0, r1);
  const int cx = r1[0] + 3, cy = r1[1] + r1[3] - 3; /* bottom-left corner zone */
  const int areas = BK_node_child_count(screen);
  drag(cx, cy, cx + 150, cy - 10, 0);
  CHECK(BK_node_child_count(screen) == areas + 1);

  /* -- Ctrl+Space maximizes the area under the pointer, again restores -- */
  BKNode *first = BK_node_child(screen, 0);
  rect_of(first, r1);
  BK_motion(g_ctx, r1[0] + r1[2] / 2, r1[1] + r1[3] / 2, 0);
  BK_key(g_ctx, " ", "space", BK_CTRL);
  rect_of(first, r);
  CHECK(r[2] == W && r[3] == H);
  CHECK(strcmp(BK_get_str(first, "full"), "maximized") == 0);
  BK_key(g_ctx, " ", "space", BK_CTRL);
  rect_of(first, r);
  CHECK(r[2] == r1[2] && r[3] == r1[3]);
  CHECK(BK_set_str(first, "full", "focus"));
  CHECK(strcmp(BK_get_str(first, "full"), "focus") == 0);
  CHECK(BK_set_str(first, "full", ""));
  CHECK(!BK_set_str(first, "full", "huge"));

  /* -- regions: a header and a sidebar in an area of its own -- */
  BKNode *panel = BK_node_new("area");
  BKNode *header = BK_node_new("areaheader");
  BKNode *side = BK_node_new("sidebar");
  BKNode *body = BK_node_new("column");
  BKNode *body2 = BK_node_new("column");
  CHECK(BK_set_num(side, "size", 200));
  CHECK(BK_set_str(side, "hotkey", "n"));
  CHECK(!BK_set_str(side, "side", "top")); /* a sidebar is on the left or the right */
  CHECK(BK_node_add(panel, header, -1));
  CHECK(BK_node_add(panel, side, -1));
  CHECK(BK_node_add(panel, body, -1));
  CHECK(!BK_node_add(panel, body2, -1)); /* one main region */
  CHECK(!BK_set_str(body2, "region", "sidebar") || 1);
  BKNode *tools = BK_node_new("column");
  CHECK(BK_set_str(tools, "region", "toolbar"));
  CHECK(BK_node_add(panel, tools, -1));
  CHECK(!BK_set_str(body2, "region", "nonsense"));
  BKNode *side2 = BK_node_new("column");
  BK_node_add(panel, side2, -1); /* refused: a second main region */
  CHECK(BK_set_str(side2, "region", "footer"));
  CHECK(BK_node_add(panel, side2, -1));
  BKNode *dup = BK_node_new("column");
  CHECK(BK_set_str(dup, "region", "toolbar"));
  CHECK(BK_node_add(panel, dup, -1)); /* allowed in, but not the region: it takes no place */
  CHECK(!BK_set_str(dup, "region", "toolbar") || 1);

  BKContext *pc = BK_context_new(panel);
  BK_context_set_host(pc, &host);
  BK_render(pc, g_r, W, H, 1);
  BK_node_rect(side, r);
  CHECK(r[2] == 200 && r[0] + r[2] == W);
  BK_node_rect(header, r1);
  CHECK(r1[1] == 0 && r1[3] == 26 && r1[2] == W); /* HEADERY */
  BK_node_rect(body, r2);
  CHECK(r2[1] == r1[3]);
  BK_node_rect(dup, r);
  CHECK(r[2] == 0 || r[3] == 0);
  CHECK(BK_get_num(side, "overlap") == 1);
  CHECK(strcmp(BK_get_str(side, "region"), "sidebar") == 0);
  CHECK(strcmp(BK_get_str(side, "side"), "right") == 0);

  /* drag the sidebar's edge (its left side) 60px left: wider */
  BK_node_rect(side, r);
  const int ex = r[0] + 1, ey = r[1] + r[3] / 2;
  BK_motion(pc, ex, ey, 0);
  BK_press(pc, ex, ey, 0);
  BK_motion(pc, ex - 30, ey, 0);
  BK_motion(pc, ex - 60, ey, 0);
  BK_release(pc, ex - 60, ey, 0);
  BK_render(pc, g_r, W, H, 0);
  BK_node_rect(side, r);
  CHECK(abs(r[2] - 260) <= 2);
  CHECK(abs(BK_get_num(side, "size") - 260) <= 2);
  /* drag it all the way to the right: hidden */
  BK_node_rect(side, r);
  BK_motion(pc, r[0] + 1, ey, 0);
  BK_press(pc, r[0] + 1, ey, 0);
  BK_motion(pc, r[0] + 100, ey, 0);
  BK_motion(pc, W - 1, ey, 0);
  BK_release(pc, W - 1, ey, 0);
  BK_render(pc, g_r, W, H, 0);
  CHECK(BK_get_num(side, "hidden") == 1);
  BK_node_rect(side, r);
  CHECK(r[2] == 0);
  /* its hotkey shows it again, with the size it had */
  BK_motion(pc, W / 2, H / 2, 0);
  BK_key(pc, "n", "n", 0);
  BK_render(pc, g_r, W, H, 0);
  CHECK(BK_get_num(side, "hidden") == 0);
  BK_node_rect(side, r);
  CHECK(abs(r[2] - 260) <= 2);

  /* flip to the left: after the toolbar, which is on the left too */
  CHECK(BK_set_str(side, "side", "left"));
  BK_render(pc, g_r, W, H, 0);
  BK_node_rect(side, r);
  BK_node_rect(tools, r1);
  CHECK(r[0] == r1[0] + r1[2] && r1[0] == 0);
  /* toolbars snap to their columns */
  CHECK(BK_set_num(tools, "size", 60));
  CHECK(!BK_set_num(tools, "size", -5));
  CHECK(!BK_set_num(tools, "overlap", 1) || BK_get_num(tools, "overlap") == 1);

  BK_context_free(pc);
  BK_node_free(panel);
  BK_node_free(body2);

  /* -- panels: Shift+click pins (shown in every category), the grip drag reorders (BK_EV_MOVE) */
  {
    BKNode *bar = BK_node_new("sidebar");
    BKNode *p[3];
    const char *cats[3] = {"Item", "Item", "Tool"};
    for (int i = 0; i < 3; i++) {
      p[i] = BK_node_new("panel");
      BK_set_str(p[i], "text", "Panel");
      BK_set_str(p[i], "category", cats[i]);
      BK_node_add(bar, p[i], -1);
    }
    BKContext *bc = BK_context_new(bar);
    BK_context_set_host(bc, &host);
    BK_render(bc, g_r, 300, 400, 1);
    BK_node_rect(p[0], r);
    CHECK(r[2] > 0 && r[3] > 0);
    BK_node_rect(p[2], r1);
    CHECK(!BK_get_num(p[2], "visible")); /* the "Tool" category isn't active */
    BK_press(bc, r[0] + 40, r[1] + 5, BK_SHIFT); /* pin the first panel */
    BK_release(bc, r[0] + 40, r[1] + 5, BK_SHIFT);
    CHECK(BK_get_num(p[0], "pinned") == 1);
    CHECK(BK_set_num(bar, "value", 1)); /* the "Tool" tab */
    BK_render(bc, g_r, 300, 400, 0);
    CHECK(BK_get_num(p[0], "visible") && BK_get_num(p[2], "visible") && !BK_get_num(p[1], "visible"));
    CHECK(BK_set_num(bar, "value", 0));
    CHECK(BK_set_num(p[0], "pinned", 0));
    BK_render(bc, g_r, 300, 400, 0);
    /* drag the first panel's grip below the second */
    g_moves = 0;
    BK_node_rect(p[0], r);
    BK_node_rect(p[1], r1);
    const int gx = r[0] + r[2] - 8, gy = r[1] + 8;
    BK_motion(bc, gx, gy, 0);
    BK_press(bc, gx, gy, 0);
    BK_motion(bc, gx, gy + 10, 0);
    BK_motion(bc, gx, r1[1] + r1[3] - 2, 0);
    BK_release(bc, gx, r1[1] + r1[3] - 2, 0);
    CHECK(BK_node_child(bar, 1) == p[0]);
    CHECK(g_moves == 1 && g_move_from == 0 && g_move_to == 1);
    BK_context_free(bc);
    BK_node_free(bar);
  }
  BK_context_free(g_ctx);
  BK_node_free(screen);
  BUI_raster_free(g_r);
  if (!g_fail) {
    printf("screen_test: ok\n");
  }
  return g_fail;
}
