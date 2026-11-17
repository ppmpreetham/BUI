/* Screens, areas and regions (kit/intern/screen): Blender's editors/screen behavior through the
 * public C API and simulated input. Splits, joins, closes, docks and swaps; dragging an edge
 * (snapping, Escape and right-click cancel); dragging a corner to split; regions sized, hidden
 * by dragging, shown by clicking their tab, flipped; Ctrl+Space; the layout saved and restored;
 * and the values the setters refuse.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <stdbool.h>
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

static BKHost g_host;

/* A screen of one area "A" in its own context (g_ctx, until free_screen). */
static BKNode *test_screen(BKNode **r_area)
{
  BKNode *s = BK_node_new("screen");
  BKNode *a = BK_node_new("area");
  BK_set_str(a, "editor", "A");
  BK_node_add(s, a, -1);
  g_ctx = BK_context_new(s);
  g_host.request = request;
  g_host.emit = emit;
  BK_context_set_host(g_ctx, &g_host);
  BK_render(g_ctx, g_r, W, H, 1);
  *r_area = a;
  return s;
}

static void free_screen(BKNode *s)
{
  BK_context_free(g_ctx);
  BK_node_free(s);
}

/* Docks a copy of the layout's left area onto its right one at the default factor and says
 * whether the source ended up on the `where` side of the target. */
static bool dock_lands(BKDock where)
{
  BKNode *a;
  BKNode *s = test_screen(&a);
  BKNode *b = BK_area_split(a, BK_AXIS_V, 0.5f);
  BKNode *c = BK_area_split(b, BK_AXIS_H, 0.5f); /* a is full height, b is not: not aligned */
  int rs[4], rt[4];
  bool ok = a && b && c && BK_area_dock(a, b, where, 0.5f);
  rect_of(a, rs);
  rect_of(b, rt);
  ok = ok && BK_node_child_count(s) == 3 && rt[2] > 0 && rt[3] > 0; /* the target stays */
  switch (where) {
    case BK_DOCK_LEFT:
      ok = ok && rs[0] < rt[0];
      break;
    case BK_DOCK_RIGHT:
      ok = ok && rs[0] > rt[0];
      break;
    case BK_DOCK_TOP:
      ok = ok && rs[1] < rt[1];
      break;
    default:
      ok = ok && rs[1] > rt[1];
      break;
  }
  free_screen(s);
  return ok;
}

/* The "id" of each area in the screen's layout JSON, in its order; returns how many. */
static int layout_ids(BKNode *s, unsigned *ids, int max)
{
  const char *p = BK_get_str(s, "layout");
  int n = 0;
  while (p && n < max && (p = strstr(p, "\"id\": ")) != NULL) {
    p += 6;
    ids[n++] = (unsigned)strtoul(p, NULL, 10);
  }
  return n;
}

/* How many vertices the screen's layout lists. */
static int count_verts(BKNode *s)
{
  const char *p = strstr(BK_get_str(s, "layout"), "\"verts\": [");
  const char *end = p ? strstr(p, "\"areas\"") : NULL;
  int n = 0;
  for (p = p ? p + 10 : NULL; p && p < end; p++) {
    n += *p == '[';
  }
  return n;
}

static bool has_id(const unsigned *ids, int n, unsigned id)
{
  for (int i = 0; i < n; i++) {
    if (ids[i] == id) {
      return true;
    }
  }
  return false;
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
  CHECK(BK_set_str(body2, "region", "sidebar")); /* a loose node may become any region */
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
  CHECK(!BK_set_str(dup, "region", "toolbar")); /* a second one is refused */

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

  /* -- reordering an area inside its screen keeps it: no close, no lost geometry -- */
  {
    BKNode *a;
    BKNode *s = test_screen(&a);
    BKNode *b = BK_area_split(a, BK_AXIS_V, 0.5f);
    BKNode *c = BK_area_split(b, BK_AXIS_H, 0.5f);
    const int closes = g_close;
    CHECK(BK_node_add(s, a, 2));
    CHECK(BK_node_child_count(s) == 3 && BK_node_parent(a) == s);
    CHECK(BK_node_child(s, 0) == b && BK_node_child(s, 1) == c && BK_node_child(s, 2) == a);
    CHECK(BK_node_add(s, a, 0)); /* and back to the front */
    CHECK(BK_node_child(s, 0) == a && BK_node_child(s, 1) == b && BK_node_child(s, 2) == c);
    CHECK(BK_node_add(s, a, -1)); /* -1: the end */
    CHECK(BK_node_child(s, 2) == a);
    CHECK(g_close == closes);
    int rr[4];
    rect_of(a, rr);
    CHECK(rr[2] > 0 && rr[3] > 0);
    free_screen(s);
  }

  /* -- area ids: in the layout, unique, and stable through drags, splits, joins and reorders -- */
  {
    BKNode *a;
    BKNode *s = test_screen(&a);
    unsigned id0[8], id1[8], id2[8];
    CHECK(layout_ids(s, id0, 8) == 1 && id0[0] != 0);
    BKNode *b = BK_area_split(a, BK_AXIS_V, 0.5f);
    CHECK(b != NULL);
    CHECK(layout_ids(s, id1, 8) == 2);
    CHECK(id1[0] != id1[1]);
    CHECK(has_id(id1, 2, id0[0])); /* the split kept the id on the kept half */
    unsigned b_id = id1[0] == id0[0] ? id1[1] : id1[0];
    /* an edge drag changes the rectangles, not the ids */
    int ra[4];
    rect_of(a, ra);
    drag(ra[0] + ra[2] + 1, H / 2, ra[0] + ra[2] - 80, H / 2, 0);
    CHECK(layout_ids(s, id2, 8) == 2 && has_id(id2, 2, id0[0]) && has_id(id2, 2, b_id));
    /* a reorder keeps them */
    CHECK(BK_node_add(s, a, 1));
    CHECK(layout_ids(s, id2, 8) == 2 && has_id(id2, 2, id0[0]) && has_id(id2, 2, b_id));
    /* a join keeps the surviving area's id; a later split never reuses the one that went */
    CHECK(BK_area_join(a, b));
    CHECK(layout_ids(s, id2, 8) == 1 && id2[0] == id0[0]);
    BKNode *c = BK_area_split(a, BK_AXIS_H, 0.5f);
    CHECK(c != NULL);
    CHECK(layout_ids(s, id2, 8) == 2 && !has_id(id2, 2, b_id));
    /* restoring a layout leaves the areas their own ids */
    const char *l = BK_get_str(s, "layout");
    char *saved = malloc(strlen(l) + 1);
    strcpy(saved, l);
    unsigned before[8];
    layout_ids(s, before, 8);
    CHECK(BK_set_str(s, "layout", saved));
    CHECK(layout_ids(s, id2, 8) == 2 && has_id(id2, 2, before[0]) && has_id(id2, 2, before[1]));
    free(saved);
    free_screen(s);
  }

  /* -- a removed area leaves no divider behind: the neighbor's outline is all that is left -- */
  {
    BKNode *a;
    BKNode *s = test_screen(&a);
    BKNode *b = BK_area_split(a, BK_AXIS_V, 0.5f);
    CHECK(b != NULL && count_verts(s) == 6);
    BK_node_remove(s, b);
    CHECK(BK_node_child_count(s) == 1 && count_verts(s) == 4);
    free_screen(s);
  }

  /* -- a region the host makes late, while its area is in focus mode, restores as saved -- */
  {
    BKNode *a;
    BKNode *s = test_screen(&a);
    BKNode *side_a = BK_node_new("sidebar");
    CHECK(BK_node_add(a, side_a, -1));
    CHECK(BK_set_num(side_a, "hidden", 1));
    CHECK(BK_area_split(a, BK_AXIS_V, 0.5f) != NULL);
    const char *l = BK_get_str(s, "layout");
    char *saved = malloc(strlen(l) + 1);
    strcpy(saved, l);
    free_screen(s);
    BKNode *a2;
    BKNode *s2 = test_screen(&a2);
    CHECK(BK_set_str(s2, "layout", saved));
    BKNode *a2x = BK_node_child(s2, 0);
    CHECK(BK_set_str(a2x, "full", "focus"));
    BKNode *side_b = BK_node_new("sidebar");
    CHECK(BK_node_add(a2x, side_b, -1)); /* the host answers late, in focus mode */
    int rr[4];
    rect_of(a2x, rr);
    CHECK(BK_set_str(a2x, "full", ""));
    CHECK(BK_get_num(side_b, "hidden") == 1); /* leaving focus mode keeps it as saved */
    free(saved);
    free_screen(s2);
  }

  /* -- a late sidebar saved visible is hidden while its area is in focus mode, shown after -- */
  {
    BKNode *a;
    BKNode *s = test_screen(&a);
    BKNode *side_a = BK_node_new("sidebar");
    CHECK(BK_node_add(a, side_a, -1));
    CHECK(BK_area_split(a, BK_AXIS_V, 0.5f) != NULL);
    const char *l = BK_get_str(s, "layout");
    char *saved = malloc(strlen(l) + 1);
    strcpy(saved, l);
    free_screen(s);
    BKNode *a2;
    BKNode *s2 = test_screen(&a2);
    CHECK(BK_set_str(s2, "layout", saved));
    BKNode *first = BK_node_child(s2, 0);
    CHECK(BK_set_str(first, "full", "focus"));
    BKNode *side = BK_node_new("sidebar");
    CHECK(BK_node_add(first, side, -1));
    int rr[4];
    rect_of(first, rr);
    CHECK(BK_get_num(side, "hidden") == 1); /* focus mode hides it */
    CHECK(BK_set_str(first, "full", ""));
    CHECK(BK_get_num(side, "hidden") == 0); /* shown again as saved */
    free(saved);
    free_screen(s2);
  }

  /* -- a pixel scroll that nets out to nothing is still the scroller's, away from its ends -- */
  {
    BKNode *col = BK_node_new("column");
    for (int i = 0; i < 40; i++) {
      BKNode *row = BK_node_new("label");
      BK_set_str(row, "text", "row");
      BK_node_add(col, row, -1);
    }
    BKContext *sc = BK_context_new(col);
    BK_context_set_host(sc, &g_host);
    BK_render(sc, g_r, 200, 100, 1);
    CHECK(BK_scroll_px(sc, 10, 50, 0.0f, -30.0f, 0)); /* away from both ends */
    BK_render(sc, g_r, 200, 100, 0);
    const float steps[4] = {0.5f, -0.5f, 0.5f, -0.5f};
    for (int i = 0; i < 4; i++) {
      CHECK(BK_scroll_px(sc, 10, 50, 0.0f, steps[i], 0)); /* not handed on to an ancestor */
    }
    BK_context_free(sc);
    BK_node_free(col);
  }

  /* -- docking at exactly one half picks the requested side -- */
  CHECK(dock_lands(BK_DOCK_LEFT));
  CHECK(dock_lands(BK_DOCK_RIGHT));
  CHECK(dock_lands(BK_DOCK_TOP));
  CHECK(dock_lands(BK_DOCK_BOTTOM));

  /* -- a maximized screen restored from a normal layout shows all its areas again -- */
  {
    BKNode *a;
    BKNode *s = test_screen(&a);
    BKNode *b = BK_area_split(a, BK_AXIS_V, 0.5f);
    CHECK(b != NULL);
    const char *l = BK_get_str(s, "layout");
    char *normal = malloc(strlen(l) + 1);
    strcpy(normal, l);
    CHECK(BK_set_str(a, "full", "maximized"));
    int rr[4];
    rect_of(b, rr);
    CHECK(rr[2] == 0 || rr[3] == 0); /* hidden behind the maximized one */
    CHECK(BK_set_str(s, "layout", normal));
    rect_of(b, rr);
    CHECK(rr[2] > 0 && rr[3] > 0);
    /* coordinates a ScrVert cannot hold are refused up front */
    CHECK(!BK_set_str(s, "layout",
                      "{\"bui_screen\": 1, \"verts\": [[0,0],[0,10],[65536,10],[65536,0]], "
                      "\"areas\": [{\"v\": [0,1,2,3], \"editor\": \"A\"}]}"));
    CHECK(!BK_set_str(s, "layout",
                      "{\"bui_screen\": 1, \"verts\": [[0,0],[0,10],[20.5,10],[20.5,0]], "
                      "\"areas\": [{\"v\": [0,1,2,3], \"editor\": \"A\"}]}"));
    CHECK(BK_node_child_count(s) == 2);
    free(normal);
    free_screen(s);
  }

  /* -- saved region state: booleans for hidden, and regions the host creates later -- */
  {
    BKNode *a;
    BKNode *s = test_screen(&a);
    BKNode *side_a = BK_node_new("sidebar");
    CHECK(BK_node_add(a, side_a, -1));
    CHECK(BK_set_num(side_a, "size", 250));
    BKNode *b = BK_area_split(a, BK_AXIS_V, 0.5f);
    CHECK(b != NULL);
    rect_of(a, r);
    const char *l = BK_get_str(s, "layout");
    CHECK(strstr(l, "\"sidebar\"") && strstr(l, "\"size\": 250"));
    char *saved = malloc(strlen(l) + 1);
    strcpy(saved, l);
    /* "hidden": true restores as hidden */
    char *hidden = malloc(strlen(l) + 8);
    char *at = strstr(saved, "\"hidden\": 0");
    CHECK(at != NULL);
    snprintf(hidden, strlen(l) + 8, "%.*s\"hidden\": true%s", (int)(at - saved), saved,
             at + strlen("\"hidden\": 0"));
    CHECK(BK_set_str(s, "layout", hidden));
    CHECK(BK_get_num(side_a, "hidden") == 1);
    CHECK(BK_set_str(s, "layout", saved));
    CHECK(BK_get_num(side_a, "hidden") == 0);
    free(hidden);
    free_screen(s);

    /* a second screen restores the saved one: its new area is filled by the host after the
     * notification, and still gets the saved sidebar size */
    BKNode *a2;
    BKNode *s2 = test_screen(&a2);
    CHECK(BK_set_str(s2, "layout", saved));
    CHECK(BK_node_child_count(s2) == 2);
    BKNode *b2 = BK_node_child(s2, 1);
    BKNode *side_b = BK_node_new("sidebar");
    CHECK(BK_node_add(a2, side_b, -1)); /* the host answers late (the first area) */
    { int rr[4]; rect_of(b2, rr); }
    CHECK(BK_get_num(side_b, "size") == 250);
    free_screen(s2);
    free(saved);
  }

  /* -- a plain scroll of a quarter pixel adds up -- */
  {
    BKNode *col = BK_node_new("column");
    BKNode *rows[40];
    for (int i = 0; i < 40; i++) {
      rows[i] = BK_node_new("label");
      BK_set_str(rows[i], "text", "row");
      BK_node_add(col, rows[i], -1);
    }
    BKContext *sc = BK_context_new(col);
    BK_context_set_host(sc, &g_host);
    BK_render(sc, g_r, 200, 100, 1);
    int y0[4], y1[4];
    BK_node_rect(rows[0], y0);
    for (int i = 0; i < 3; i++) {
      BK_scroll_px(sc, 10, 50, 0.0f, -0.25f, 0);
    }
    BK_render(sc, g_r, 200, 100, 0);
    BK_node_rect(rows[0], y1);
    CHECK(y1[1] == y0[1]); /* three quarters: not a whole pixel yet */
    BK_scroll_px(sc, 10, 50, 0.0f, -0.25f, 0);
    BK_render(sc, g_r, 200, 100, 0);
    BK_node_rect(rows[0], y1);
    CHECK(y1[1] == y0[1] - 1); /* the fourth completes it */
    BK_context_free(sc);
    BK_node_free(col);
  }
  BUI_raster_free(g_r);
  if (!g_fail) {
    printf("screen_test: ok\n");
  }
  return g_fail;
}
