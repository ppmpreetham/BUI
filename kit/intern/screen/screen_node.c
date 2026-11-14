/* The "screen" and "area" node classes, region properties and the C API.
 *
 * The screen lays its areas out from their vertices (area_calc_totrct) and
 * each area its regions (region_rect_recursive), every layout pass, like
 * ED_screen_refresh / ED_area_update_region_sizes. Drawing follows
 * wm_draw_window: the areas and their regions, then the screen's edges
 * (ED_screen_draw_edges) and the operators' overlays on top.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "screen_intern.h"

/* -------------------------------------------------------------------- */
/** \name Screen
 * \{ */

static void screen_init(BKNode *n)
{
  n->ext = calloc(1, sizeof(BKScreen));
}

static void screen_free(BKNode *n)
{
  BKScreen *screen = SCREEN(n);
  if (screen) {
    screen_geom_free(screen);
    free(screen->layout);
    free(screen);
    n->ext = NULL;
  }
}

static bool screen_host_content(const BKNode *n)
{
  return SCREEN(n)->host_content;
}

static void screen_measure(BKNode *n, int *rw, int *rh)
{
  (void)n;
  *rw = 30 * bk_unit();
  *rh = 20 * bk_unit();
}

/* screen_add: the first area takes the whole screen. */
static void screen_first_area(BKNode *n, const rcti *rect)
{
  BKScreen *screen = SCREEN(n);
  ScrVert *sv1 = screen_geom_vertex_add(screen, (short)rect->xmin, (short)rect->ymin);
  ScrVert *sv2 = screen_geom_vertex_add(screen, (short)rect->xmin, (short)(rect->ymax - 1));
  ScrVert *sv3 = screen_geom_vertex_add(screen, (short)(rect->xmax - 1), (short)(rect->ymax - 1));
  ScrVert *sv4 = screen_geom_vertex_add(screen, (short)(rect->xmax - 1), (short)rect->ymin);
  screen_geom_edge_add(screen, sv1, sv2);
  screen_geom_edge_add(screen, sv2, sv3);
  screen_geom_edge_add(screen, sv3, sv4);
  screen_geom_edge_add(screen, sv4, sv1);
  screen_area_attach(n, n->kids[0], sv1, sv2, sv3, sv4);
}

void screen_ensure_verts(BKNode *n)
{
  if (SCREEN(n)->vertbase.first != NULL || n->nkids == 0) {
    return;
  }
  /* Before the first layout the screen has no size yet: its natural one, as Blender makes a
   * screen at a default size and fits it to the window later (screen_geom_vertices_scale). */
  int w = n->w, h = n->h;
  if (w < 2 || h < 2) {
    bk_natural(n, &w, &h);
  }
  rcti rect;
  BLI_rcti_init(&rect, 0, w, 0, h);
  screen_first_area(n, &rect);
}

static void screen_arrange(BKNode *n)
{
  if (n->nkids == 0 || n->w < 2 || n->h < 2) {
    return;
  }
  rcti rect;
  screen_window_rect(n, &rect);
  screen_ensure_verts(n);
  screen_geom_vertices_scale(n, &rect);
  SCREEN_AREAS_ITER (n, area) {
    if (!NODE_VISIBLE(area->node)) {
      bk_arrange(area->node, n->x, n->y, 0, 0);
      continue;
    }
    area_calc_totrct(n, area, &rect);
    const BKRect r = space_to_window(n, &area->totrct);
    bk_arrange(area->node, r.xmin, r.ymin, bk_rw(&r), bk_rh(&r));
  }
  if (screen_host_content(n)) {
    /* The host lays the regions' contents out: tell it when an area or a region moved. */
    uint64_t sig = 1469598103934665603ull;
    for (int i = 0; i < n->nkids; i++) {
      const BKNode *a = n->kids[i];
      const int rects = 1 + a->nkids;
      for (int k = 0; k < rects; k++) {
        const BKNode *c = k == 0 ? a : a->kids[k - 1];
        const int v[4] = {c->x, c->y, c->w, c->h};
        for (int j = 0; j < 4; j++) {
          sig = (sig ^ (uint64_t)(uint32_t)v[j]) * 1099511628211ull;
        }
      }
    }
    if (sig != SCREEN(n)->geom_sig) {
      SCREEN(n)->geom_sig = sig;
      bk_emit(n, BK_EV_CONFIGURE, NULL, n->w, n->h);
    }
  }
}

static void screen_draw_tree(BKNode *n)
{
  if (!NODE_VISIBLE(n)) {
    return;
  }
  BKRect r = bk_rect(n->x - 3, n->y - 3, n->w + 6, n->h + 6);
  if (!bk_clip_visible(&r)) {
    return;
  }
  BKScreen *screen = SCREEN(n);
  for (int i = 0; i < n->nkids; i++) {
    bk_draw_tree(n->kids[i]);
  }
  bk_clip_push(n->x, n->y, n->w, n->h);
  screen_draw_edges(n);
  if (screen->op == OP_MOVE || n->anim > 0.0f) {
    /* AREA_MOVE_LINE_FADEIN / _FADEOUT: squared in, squared out */
    const float t = n->anim;
    const float factor = screen->op == OP_MOVE ? t * t : 1.0f - (1.0f - t) * (1.0f - t);
    screen_draw_move_highlight(n, screen->fade_axis, factor);
  }
  BKArea *sa1 = screen->jd.sa1 ? AREA(screen->jd.sa1) : NULL;
  BKArea *sa2 = screen->jd.sa2 ? AREA(screen->jd.sa2) : NULL;
  if (screen->op == OP_JOIN && sa1) {
    if (sa1 == sa2 && screen->jd.split_fac > 0.0f) {
      screen_draw_split_preview(n, sa1, screen->jd.split_dir, screen->jd.split_fac);
    }
    else if (sa2 && sa1 != sa2) {
      screen_draw_join_highlight(n, sa1, sa2, screen->jd.dir);
      if (screen->jd.dir == SCREEN_DIR_NONE) {
        screen_draw_dock_preview(n, sa1, sa2, screen->jd.dock_target, screen->jd.factor);
      }
    }
  }
  else if (screen->op == OP_SPLIT && sa1 && screen->jd.split_fac > 0.0f) {
    screen_draw_split_preview(n, sa1, screen->jd.split_dir, screen->jd.split_fac);
  }
  bk_clip_pop();
}

static BKNode *screen_hit(BKNode *n, int x, int y)
{
  BKScreen *screen = SCREEN(n);
  int sx, sy;
  space_from_window(n, x, y, &sx, &sy);
  SCREEN_AREAS_ITER (n, area) {
    if (!NODE_VISIBLE(area->node)) {
      continue;
    }
    const AZone *az = area_actionzone_find_xy(area, sx, sy);
    if (az) {
      /* Favor action zones over the edge zone; the area handles its regions' zones. */
      return az->type == AZONE_AREA ? n : area->node;
    }
  }
  if (screen->state == SCREENNORMAL) {
    rcti rect;
    screen_window_rect(n, &rect);
    if (screen_geom_find_active_scredge(screen, &rect, sx, sy, (int)BORDERPADDING)) {
      return n;
    }
  }
  for (int i = n->nkids - 1; i >= 0; i--) {
    BKNode *f = bk_hit(n->kids[i], x, y);
    if (f) {
      return f;
    }
  }
  return NULL;
}

static int screen_move(BKNode *n, BKEvent *e)
{
  /* the area under the pointer gets the brighter outline (TH_EDITOR_OUTLINE_ACTIVE) */
  BKScreen *screen = SCREEN(n);
  int x, y;
  space_from_window(n, e->x, e->y, &x, &y);
  BKNode *area = screen_find_area_xy(n, x, y);
  if (area != screen->active) {
    screen->active = area;
    bk_invalidate(n);
    return 1;
  }
  return 0;
}

static float screen_anim_target(const BKNode *n)
{
  return SCREEN(n)->op == OP_MOVE ? 1.0f : 0.0f;
}

static bool screen_accept(BKNode *n, BKNode *child)
{
  /* only areas; past the first, only those the screen placed (split, dock, a layout) */
  return IS_AREA(child) && (n->nkids == 0 || AREA(child)->v1 != NULL);
}

static void screen_detach(BKNode *n, BKNode *child)
{
  BKScreen *screen = SCREEN(n);
  if (!IS_AREA(child) || screen->detaching || AREA(child)->v1 == NULL) {
    return;
  }
  /* The host removes an area: a neighbor takes its place (screen_area_close). */
  screen->detaching = child;
  if (n->nkids == 1) {
    screen_geom_free(screen); /* the last one: the screen starts over */
  }
  else if (screen_area_close(n, child, NULL)) {
    screen_remove_unused_scrverts(screen);
  }
  screen->detaching = NULL;
  AREA(child)->v1 = AREA(child)->v2 = AREA(child)->v3 = AREA(child)->v4 = NULL;
  AREA(child)->full = 0;
  bk_emit(n, BK_EV_CHANGE, NULL, 0, 0);
}

static int screen_set_num(BKNode *n, const char *key, double v)
{
  if (strcmp(key, "transparent") == 0) {
    SCREEN(n)->host_content = v != 0;
    bk_invalidate(n);
    return 1;
  }
  return 0;
}

static int screen_get_num(BKNode *n, const char *key, double *r_v)
{
  if (strcmp(key, "transparent") == 0) {
    *r_v = SCREEN(n)->host_content;
    return 1;
  }
  return 0;
}

static int screen_set_str(BKNode *n, const char *key, const char *v)
{
  if (strcmp(key, "layout") == 0) {
    return screen_layout_set(n, v);
  }
  if (strcmp(key, "ops") == 0) {
    if (!bk_streq(v, "all") && !bk_streq(v, "resize")) {
      return 0;
    }
    SCREEN(n)->resize_only = bk_streq(v, "resize");
    screen_changed(n);
    return 1;
  }
  return 0;
}

static const char *screen_get_str(BKNode *n, const char *key)
{
  if (strcmp(key, "layout") == 0) {
    return screen_layout_get(n);
  }
  if (strcmp(key, "ops") == 0) {
    return SCREEN(n)->resize_only ? "resize" : "all";
  }
  return NULL;
}

const BKClass bk_cls_screen = {.name = "screen",
                               .flags = CLS_INTERACTIVE,
                               .anim_time = AREA_MOVE_LINE_FADEIN,
                               .init = screen_init,
                               .free_ext = screen_free,
                               .measure = screen_measure,
                               .arrange = screen_arrange,
                               .draw_tree = screen_draw_tree,
                               .hit = screen_hit,
                               .press = screen_op_press,
                               .drag = screen_op_drag,
                               .release = screen_op_release,
                               .cancel = screen_op_cancel,
                               .move = screen_move,
                               .cursor_at = screen_cursor_at,
                               .context_items = screen_context_items,
                               .set_num = screen_set_num,
                               .get_num = screen_get_num,
                               .set_str = screen_set_str,
                               .get_str = screen_get_str,
                               .anim_target = screen_anim_target,
                               .accept = screen_accept,
                               .detach = screen_detach};

/** \} */

/* -------------------------------------------------------------------- */
/** \name Area
 * \{ */

static void area_init(BKNode *n)
{
  BKArea *area = (BKArea *)calloc(1, sizeof(BKArea));
  area->node = n;
  n->ext = area;
}

static void area_free(BKNode *n)
{
  free(n->ext);
  n->ext = NULL;
}

static void area_measure(BKNode *n, int *rw, int *rh)
{
  /* the main region's natural size plus the regions around it */
  int w = 0, h = 0, side_w = 0, side_h = 0;
  for (int i = 0; i < n->nkids; i++) {
    BKNode *c = n->kids[i];
    const BKRegion *r = region_of(c);
    int cw, ch;
    bk_natural(c, &cw, &ch);
    if (!r) {
      w = BK_MAX(w, cw);
      h = BK_MAX(h, ch);
    }
    else if (!region_is_hidden(r) && !r->overlap && r->alignment != RGN_ALIGN_FLOAT) {
      if (r->alignment == RGN_ALIGN_LEFT || r->alignment == RGN_ALIGN_RIGHT) {
        side_w += r->sizex > 1 ? (int)(r->sizex * UI_SCALE_FAC) :
                                 (int)(region_type_info(r->type)->prefsizex * UI_SCALE_FAC);
      }
      else {
        side_h += region_type_info(r->type)->fixed_height ? screen_headersize() : ch;
      }
    }
  }
  *rw = BK_MAX(w + side_w, 4 * bk_unit());
  *rh = BK_MAX(h + side_h, 2 * screen_headersize());
}

static void area_arrange(BKNode *n)
{
  BKNode *screen_node = AREA_SCREEN(n);
  rcti rect;
  if (screen_node) {
    screen_window_rect(screen_node, &rect);
  }
  if (n->w <= 0 || n->h <= 0) {
    for (int i = 0; i < n->nkids; i++) {
      bk_arrange(n->kids[i], n->x, n->y, 0, 0);
    }
    AREA(n)->nazones = 0;
    return;
  }
  area_update_region_sizes(n, screen_node, screen_node ? &rect : NULL);
}

static void area_draw_tree(BKNode *n)
{
  if (!NODE_VISIBLE(n) || n->w <= 0 || n->h <= 0) {
    return;
  }
  BKRect r = bk_rect(n->x - 3, n->y - 3, n->w + 6, n->h + 6);
  if (!bk_clip_visible(&r)) {
    return;
  }
  BKArea *area = AREA(n);
  const BKNode *space = area_space(n);
  bk_clip_push(n->x, n->y, n->w, n->h);
  area_draw_regions_back(n);
  /* the main region first, regions over it (overlapping ones float above) */
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < n->nkids; i++) {
      if ((region_of(n->kids[i]) != NULL) == (pass == 1)) {
        bk_draw_tree(n->kids[i]);
      }
    }
  }
  area_draw_azones(space, area);
  if (area->rmd.active) {
    screen_draw_region_scale_highlight(space, area->rmd.region->rgn);
  }
  bk_clip_pop();
}

static BKNode *area_hit(BKNode *n, int x, int y)
{
  BKArea *area = AREA(n);
  int sx, sy;
  space_from_window(area_space(n), x, y, &sx, &sy);
  if (area_actionzone_find_xy(area, sx, sy)) {
    return n;
  }
  /* regions over the main region first */
  for (int pass = 1; pass >= 0; pass--) {
    for (int i = n->nkids - 1; i >= 0; i--) {
      BKNode *c = n->kids[i];
      if ((region_of(c) != NULL) != (pass == 1)) {
        continue;
      }
      BKNode *f = bk_hit(c, x, y);
      if (f) {
        return f;
      }
    }
  }
  /* a header's empty space takes the right button (the header menu), unless the host draws what
   * is in the regions: then what is under the pointer is the host's */
  const BKNode *screen_node = AREA_SCREEN(n);
  if (screen_node && screen_host_content(screen_node)) {
    return NULL;
  }
  for (int i = 0; i < n->nkids; i++) {
    const BKRegion *r = region_of(n->kids[i]);
    if (r && r->visible && bk_contains(n->kids[i], x, y) &&
        (r->type == RGN_TYPE_HEADER || r->type == RGN_TYPE_TOOL_HEADER ||
         r->type == RGN_TYPE_FOOTER || r->type == RGN_TYPE_NAV_BAR))
    {
      return n;
    }
  }
  return NULL;
}

static int area_set_str(BKNode *n, const char *key, const char *v)
{
  if (strcmp(key, "editor") == 0) {
    bk_set_text(&n->text, v);
    return 1;
  }
  if (strcmp(key, "full") == 0) {
    BKNode *screen_node = AREA_SCREEN(n);
    const short state = bk_streq(v, "maximized") ? SCREENMAXIMIZED :
                        bk_streq(v, "focus")     ? SCREENFULL :
                        (v == NULL || v[0] == '\0') ? SCREENNORMAL :
                                                       -1;
    if (state < 0 || !screen_node) {
      return 0;
    }
    if (AREA(n)->full != state) {
      screen_state_toggle(screen_node, n, AREA(n)->full ? AREA(n)->full : state);
      if (state != SCREENNORMAL && AREA(n)->full != state) {
        screen_state_toggle(screen_node, n, state);
      }
    }
    return 1;
  }
  return 0;
}

static const char *area_get_str(BKNode *n, const char *key)
{
  if (strcmp(key, "editor") == 0) {
    return n->text ? n->text : "";
  }
  if (strcmp(key, "full") == 0) {
    return AREA(n)->full == SCREENMAXIMIZED ? "maximized" :
           AREA(n)->full == SCREENFULL      ? "focus" :
                                              "";
  }
  return NULL;
}

static bool area_accept(BKNode *n, BKNode *child)
{
  /* one main region: other children say which region they are first */
  if (region_wanted(child)) {
    return true;
  }
  for (int i = 0; i < n->nkids; i++) {
    if (!region_of(n->kids[i])) {
      return false;
    }
  }
  return true;
}

const BKClass bk_cls_area = {.name = "area",
                             .flags = CLS_INTERACTIVE,
                             .init = area_init,
                             .free_ext = area_free,
                             .measure = area_measure,
                             .arrange = area_arrange,
                             .draw_tree = area_draw_tree,
                             .hit = area_hit,
                             .press = area_op_press,
                             .drag = area_op_drag,
                             .release = area_op_release,
                             .cancel = area_op_cancel,
                             .key = area_key,
                             .cursor_at = area_cursor_at,
                             .context_items = area_context_items,
                             .set_str = area_set_str,
                             .get_str = area_get_str,
                             .accept = area_accept};

/** \} */

/* -------------------------------------------------------------------- */
/** \name Region properties
 * \{ */

/* The region state of a node that is one: an explicit "region", or the kit's region classes. */
static BKRegion *region_props(BKNode *n)
{
  if (n->rgn) {
    return n->rgn->type == RGN_TYPE_WINDOW ? NULL : n->rgn;
  }
  if (n->cls == &bk_cls_areaheader || n->cls == &bk_cls_sidebar) {
    return region_ensure(n);
  }
  return NULL;
}

static void region_props_changed(BKNode *n)
{
  if (IS_AREA(n->parent)) {
    bk_relayout(n->parent);
    bk_invalidate(n->parent);
  }
}

static bool region_axis_x(const BKRegion *r)
{
  return r->alignment == RGN_ALIGN_LEFT || r->alignment == RGN_ALIGN_RIGHT;
}

int bk_region_set_num(BKNode *n, const char *key, double v)
{
  BKRegion *r = region_props(n);
  if (!r) {
    return 0;
  }
  if (strcmp(key, "size") == 0) {
    if (v < 0 || v > 10000 || r->alignment == RGN_ALIGN_FLOAT) {
      return -1;
    }
    if (region_axis_x(r)) {
      r->sizex = (short)v;
    }
    else if (!region_type_info(r->type)->fixed_height) {
      r->sizey = (short)v;
    }
  }
  else if (strcmp(key, "hidden") == 0) {
    r->flag = (short)(v != 0 ? (r->flag | RGN_FLAG_HIDDEN) : (r->flag & ~RGN_FLAG_HIDDEN));
  }
  else if (strcmp(key, "overlap") == 0) {
    if (v != 0 && !region_type_info(r->type)->overlap) {
      return -1; /* this type of region never overlaps (ED_region_is_overlap) */
    }
    r->overlap_off = v == 0;
  }
  else if (strcmp(key, "resizable") == 0) {
    r->flag = (short)(v != 0 ? (r->flag & ~RGN_FLAG_NO_USER_RESIZE) :
                               (r->flag | RGN_FLAG_NO_USER_RESIZE));
  }
  else if (strcmp(key, "maxsize") == 0) {
    if (v < 0 || v > 10000) {
      return -1;
    }
    r->maxsize = (short)v;
  }
  else {
    return 0;
  }
  region_props_changed(n);
  return 1;
}

int bk_region_get_num(BKNode *n, const char *key, double *r_v)
{
  const BKRegion *r = region_props(n);
  if (!r) {
    return 0;
  }
  if (strcmp(key, "size") == 0) {
    *r_v = region_axis_x(r) ? r->sizex : r->sizey;
  }
  else if (strcmp(key, "hidden") == 0) {
    *r_v = region_is_hidden(r);
  }
  else if (strcmp(key, "overlap") == 0) {
    *r_v = region_type_info(r->type)->overlap && !r->overlap_off;
  }
  else if (strcmp(key, "resizable") == 0) {
    *r_v = !(r->flag & RGN_FLAG_NO_USER_RESIZE);
  }
  else if (strcmp(key, "maxsize") == 0) {
    *r_v = r->maxsize;
  }
  else {
    return 0;
  }
  return 1;
}

static int side_from_name(const char *v)
{
  return bk_streq(v, "top")    ? RGN_ALIGN_TOP :
         bk_streq(v, "bottom") ? RGN_ALIGN_BOTTOM :
         bk_streq(v, "left")   ? RGN_ALIGN_LEFT :
         bk_streq(v, "right")  ? RGN_ALIGN_RIGHT :
         bk_streq(v, "float")  ? RGN_ALIGN_FLOAT :
                                 -1;
}

int bk_region_set_str(BKNode *n, const char *key, const char *v)
{
  if (strcmp(key, "region") == 0) {
    const int type = region_type_from_name(v);
    if (type < 0) {
      return -1;
    }
    BKNode *area = IS_AREA(n->parent) ? n->parent : NULL;
    if (area && type != RGN_TYPE_WINDOW) {
      BKNode *other = area_region_find(area, type);
      if (other && other != n) {
        return -1; /* one region of each type */
      }
    }
    BKRegion *r = region_ensure(n);
    if (r->type != type) {
      r->type = (short)type;
      r->alignment = region_type_info(type)->alignment;
    }
    region_props_changed(n);
    return 1;
  }
  BKRegion *r = region_props(n);
  if (!r) {
    return 0;
  }
  if (strcmp(key, "side") == 0) {
    const int side = side_from_name(v);
    if (side < 0 || !(region_type_info(r->type)->align_mask & (1 << side))) {
      return -1;
    }
    r->alignment = (short)side;
  }
  else if (strcmp(key, "hotkey") == 0) {
    char keysym[32];
    bk_combo_parse(v ? v : "", keysym, sizeof(keysym));
    if (v && v[0] && !keysym[0]) {
      return -1;
    }
    snprintf(r->key, sizeof(r->key), "%s", v ? v : "");
  }
  else {
    return 0;
  }
  region_props_changed(n);
  return 1;
}

const char *bk_region_get_str(BKNode *n, const char *key)
{
  if (strcmp(key, "region") == 0) {
    const BKRegion *r = region_props(n);
    return r ? region_type_info(r->type)->name : (n->rgn ? "window" : NULL);
  }
  const BKRegion *r = region_props(n);
  if (!r) {
    return NULL;
  }
  if (strcmp(key, "side") == 0) {
    switch (r->alignment) {
      case RGN_ALIGN_TOP:
        return "top";
      case RGN_ALIGN_BOTTOM:
        return "bottom";
      case RGN_ALIGN_LEFT:
        return "left";
      case RGN_ALIGN_RIGHT:
        return "right";
      default:
        return "float";
    }
  }
  if (strcmp(key, "hotkey") == 0) {
    return r->key;
  }
  return NULL;
}

bool bk_region_overlaps(const BKNode *n)
{
  const BKRegion *r = region_of(n);
  return r && r->overlap;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name C API
 * \{ */

static BKNode *api_screen(BKNode *area)
{
  BKNode *screen_node = IS_AREA(area) ? AREA_SCREEN(area) : NULL;
  if (screen_node) {
    screen_ensure_verts(screen_node); /* operations before the first layout */
  }
  return (screen_node && AREA(area)->v1) ? screen_node : NULL;
}

static void api_done(BKNode *screen_node)
{
  BKScreen *screen = SCREEN(screen_node);
  screen_remove_double_scredges(screen);
  screen_remove_unused_scredges(screen_node);
  screen_remove_unused_scrverts(screen);
  screen_changed(screen_node);
}

BKNode *BK_area_split(BKNode *area, BKScreenAxis axis, float factor)
{
  BKNode *screen_node = api_screen(area);
  if (!screen_node || (axis != BK_AXIS_H && axis != BK_AXIS_V) || !(factor > 0.0f) ||
      !(factor < 1.0f))
  {
    return NULL;
  }
  BKNode *newa = area_split(screen_node, area, (eScreenAxis)axis, factor);
  if (newa) {
    api_done(screen_node);
  }
  return newa;
}

int BK_area_join(BKNode *keep, BKNode *gone)
{
  BKNode *screen_node = api_screen(keep);
  if (!screen_node || api_screen(gone) != screen_node || keep == gone ||
      area_getorientation(AREA(keep), AREA(gone)) == SCREEN_DIR_NONE)
  {
    return 0;
  }
  screen_area_join(screen_node, keep, gone);
  api_done(screen_node);
  return 1;
}

int BK_area_dock(BKNode *area, BKNode *target, BKDock where, float factor)
{
  BKNode *screen_node = api_screen(area);
  if (!screen_node || api_screen(target) != screen_node || area == target ||
      where < BK_DOCK_LEFT || where > BK_DOCK_CENTER || !(factor > 0.0f) || !(factor < 1.0f))
  {
    return 0;
  }
  if (!area_docking_apply(screen_node, area, target, (AreaDockTarget)where, factor)) {
    return 0;
  }
  api_done(screen_node);
  return 1;
}

int BK_area_swap(BKNode *a, BKNode *b)
{
  BKNode *screen_node = api_screen(a);
  if (!screen_node || api_screen(b) != screen_node || a == b) {
    return 0;
  }
  area_swapspace(screen_node, a, b);
  screen_changed(screen_node);
  return 1;
}

int BK_area_close(BKNode *area)
{
  BKNode *screen_node = api_screen(area);
  if (!screen_node || !screen_area_close(screen_node, area, NULL)) {
    return 0;
  }
  api_done(screen_node);
  return 1;
}

/** \} */
