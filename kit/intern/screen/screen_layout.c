/* A screen's arrangement as text: what Blender keeps in a .blend (bScreen's
 * vertbase and areabase, each area's regions), as JSON so hosts can store it
 * anywhere.
 *
 *   {"bui_screen": 1, "size": [w, h], "verts": [[x, y], ...],
 *    "areas": [{"v": [1, 0, 3, 2], "editor": "3D Viewport",
 *               "regions": [{"region": "sidebar", "side": "right", "size": 280,
 *                            "hidden": false}, ...]}, ...]}
 *
 * Vertices are in the screen's pixels (y up) at the saved size; restoring
 * scales them to the screen's current size (screen_geom_vertices_scale).
 * Edges are rebuilt from the areas' outlines. Restoring makes the screen
 * have the saved areas: children in order take the saved areas, missing
 * ones are created (BK_AREA_NEW), extra ones are closed (BK_AREA_CLOSE).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kit_json.h"
#include "screen_intern.h"

/* -------------------------------------------------------------------- */
/** \name Writing
 * \{ */

typedef struct Buf {
  char *s;
  size_t len, cap;
} Buf;

static void buf_add(Buf *b, const char *s, size_t n)
{
  if (b->len + n + 1 > b->cap) {
    b->cap = BK_MAX(b->cap * 2, b->len + n + 256);
    b->s = (char *)realloc(b->s, b->cap);
  }
  memcpy(b->s + b->len, s, n);
  b->len += n;
  b->s[b->len] = '\0';
}

static void buf_printf(Buf *b, const char *fmt, int a, int c)
{
  char tmp[64];
  const int n = snprintf(tmp, sizeof(tmp), fmt, a, c);
  buf_add(b, tmp, (size_t)n);
}

static void buf_str(Buf *b, const char *s)
{
  buf_add(b, "\"", 1);
  for (const char *p = s ? s : ""; *p; p++) {
    if (*p == '"' || *p == '\\') {
      buf_add(b, "\\", 1);
      buf_add(b, p, 1);
    }
    else if ((unsigned char)*p < 0x20) {
      char esc[8];
      snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)*p);
      buf_add(b, esc, 6);
    }
    else {
      buf_add(b, p, 1);
    }
  }
  buf_add(b, "\"", 1);
}

static const char *side_name(int alignment)
{
  switch (alignment) {
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

const char *screen_layout_get(BKNode *screen_node)
{
  BKScreen *screen = SCREEN(screen_node);
  Buf b = {0};
  buf_printf(&b, "{\"bui_screen\": 1, \"size\": [%d, %d], \"verts\": [", screen_node->w,
             screen_node->h);
  int count = 0;
  for (ScrVert *v = (ScrVert *)screen->vertbase.first; v; v = v->next) {
    v->newv = NULL;
    buf_printf(&b, count ? ", [%d, %d]" : "[%d, %d]", v->x, v->y);
    count++;
  }
  buf_add(&b, "], \"areas\": [", 13);
  SCREEN_AREAS_ITER (screen_node, area) {
    if (area_i) {
      buf_add(&b, ", ", 2);
    }
    const ScrVert *vs[4] = {area->v1, area->v2, area->v3, area->v4};
    int idx[4] = {0};
    for (int k = 0; k < 4; k++) {
      idx[k] = BLI_findindex(&screen->vertbase, vs[k]);
    }
    buf_printf(&b, "{\"v\": [%d, %d, ", idx[0], idx[1]);
    buf_printf(&b, "%d, %d], \"editor\": ", idx[2], idx[3]);
    buf_str(&b, area->node->text);
    buf_printf(&b, ", \"full\": %d, \"regions\": [", area->full, 0);
    int nr = 0;
    for (int i = 0; i < area->node->nkids; i++) {
      const BKRegion *r = region_of(area->node->kids[i]);
      if (!r) {
        continue;
      }
      const char *open = nr++ ? ", {\"region\": " : "{\"region\": ";
      buf_add(&b, open, strlen(open));
      buf_str(&b, region_type_info(r->type)->name);
      buf_add(&b, ", \"side\": ", 10);
      buf_str(&b, side_name(r->alignment));
      const int size = (r->alignment == RGN_ALIGN_TOP || r->alignment == RGN_ALIGN_BOTTOM) ?
                           r->sizey :
                           r->sizex;
      buf_printf(&b, ", \"size\": %d, \"hidden\": %d}", size, region_is_hidden(r));
    }
    buf_add(&b, "]}", 2);
  }
  buf_add(&b, "]}", 2);
  free(screen->layout);
  screen->layout = b.s;
  return b.s;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Reading
 * \{ */

static int json_int(const BKJson *v, int fallback)
{
  return (v && v->type == BK_JSON_NUMBER) ? (int)v->number : fallback;
}

static const BKJson *json_at(const BKJson *arr, int i)
{
  const BKJson *e = (arr && arr->type == BK_JSON_ARRAY) ? arr->first : NULL;
  for (; e && i > 0; i--) {
    e = e->next;
  }
  return e;
}

static int json_len(const BKJson *arr)
{
  int n = 0;
  for (const BKJson *e = (arr && arr->type == BK_JSON_ARRAY) ? arr->first : NULL; e; e = e->next)
  {
    n++;
  }
  return n;
}

/* Every area's corners name saved vertices and make a rectangle; validated before anything
 * changes (errors at the boundary). */
static bool layout_valid(const BKJson *verts, const BKJson *areas)
{
  const int nverts = json_len(verts), nareas = json_len(areas);
  if (nverts < 4 || nareas < 1) {
    return false;
  }
  for (int i = 0; i < nverts; i++) {
    const BKJson *v = json_at(verts, i);
    if (json_len(v) != 2 || !json_at(v, 0) || json_at(v, 0)->type != BK_JSON_NUMBER ||
        json_at(v, 1)->type != BK_JSON_NUMBER)
    {
      return false;
    }
  }
  for (int i = 0; i < nareas; i++) {
    const BKJson *a = json_at(areas, i);
    const BKJson *vs = bk_json_get(a, "v");
    if (json_len(vs) != 4) {
      return false;
    }
    int x[4], y[4];
    for (int k = 0; k < 4; k++) {
      const int idx = json_int(json_at(vs, k), -1);
      if (idx < 0 || idx >= nverts) {
        return false;
      }
      x[k] = json_int(json_at(json_at(verts, idx), 0), 0);
      y[k] = json_int(json_at(json_at(verts, idx), 1), 0);
    }
    /* v1 bottom-left, v2 top-left, v3 top-right, v4 bottom-right */
    if (x[0] != x[1] || x[2] != x[3] || y[0] != y[3] || y[1] != y[2] || x[0] >= x[3] ||
        y[0] >= y[1])
    {
      return false;
    }
  }
  return true;
}

static void region_from_json(BKNode *area_node, const BKJson *r)
{
  const BKJson *type = bk_json_get(r, "region");
  if (!type || type->type != BK_JSON_STRING) {
    return;
  }
  const int t = region_type_from_name(type->string);
  BKNode *node = t >= 0 ? area_region_find(area_node, t) : NULL;
  if (!node || !node->rgn) {
    return; /* the host didn't give this area that region (yet): nothing to restore */
  }
  const BKJson *side = bk_json_get(r, "side");
  if (side && side->type == BK_JSON_STRING) {
    bk_region_set_str(node, "side", side->string);
  }
  bk_region_set_num(node, "size", json_int(bk_json_get(r, "size"), 0));
  bk_region_set_num(node, "hidden", json_int(bk_json_get(r, "hidden"), 0));
}

int screen_layout_set(BKNode *screen_node, const char *text)
{
  char err[128];
  BKJson *root = text ? bk_json_parse(text, strlen(text), err, sizeof(err)) : NULL;
  const BKJson *verts = bk_json_get(root, "verts");
  const BKJson *areas = bk_json_get(root, "areas");
  if (!root || json_int(bk_json_get(root, "bui_screen"), 0) != 1 || !layout_valid(verts, areas)) {
    bk_json_free(root);
    return 0;
  }
  BKScreen *screen = SCREEN(screen_node);
  const int nareas = json_len(areas);

  /* Areas: children in order take the saved ones; the rest are made or closed. */
  for (int i = screen_node->nkids - 1; i >= nareas; i--) {
    screen_area_free(screen_node, screen_node->kids[i]);
  }
  screen_geom_free(screen);
  const int nverts = json_len(verts);
  ScrVert **vs = (ScrVert **)calloc((size_t)nverts, sizeof(ScrVert *));
  for (int i = 0; i < nverts; i++) {
    const BKJson *v = json_at(verts, i);
    vs[i] = screen_geom_vertex_add(screen, (short)json_int(json_at(v, 0), 0),
                                   (short)json_int(json_at(v, 1), 0));
  }
  for (int i = 0; i < nareas; i++) {
    const BKJson *a = json_at(areas, i);
    const BKJson *av = bk_json_get(a, "v");
    BKNode *node;
    bool made = false;
    if (i < screen_node->nkids) {
      node = screen_node->kids[i];
    }
    else {
      node = bk_node_new_cls(&bk_cls_area);
      made = true;
    }
    const BKJson *editor = bk_json_get(a, "editor");
    if (editor && editor->type == BK_JSON_STRING) {
      bk_set_text(&node->text, editor->string);
    }
    BKArea *area = AREA(node);
    ScrVert *c[4];
    for (int k = 0; k < 4; k++) {
      c[k] = vs[json_int(json_at(av, k), 0)];
    }
    area->full = 0;
    screen_area_attach(screen_node, node, c[0], c[1], c[2], c[3]);
    ScrVert *loop[5] = {c[0], c[1], c[2], c[3], c[0]};
    for (int k = 0; k < 4; k++) {
      screen_geom_edge_add(screen, loop[k], loop[k + 1]);
    }
    if (made) {
      bk_emit(screen_node, BK_EV_AREA, NULL, i, BK_AREA_NEW); /* the host fills it */
    }
    const BKJson *regions = bk_json_get(a, "regions");
    for (int k = 0; k < json_len(regions); k++) {
      region_from_json(node, json_at(regions, k));
    }
  }
  free(vs);
  screen_remove_double_scredges(screen);
  screen->state = SCREENNORMAL;
  for (int i = 0; i < nareas; i++) {
    const int full = json_int(bk_json_get(json_at(areas, i), "full"), 0);
    if (full == SCREENMAXIMIZED || full == SCREENFULL) {
      screen_state_toggle(screen_node, screen_node->kids[i], (short)full);
      break;
    }
  }
  /* the saved size scales to the current one at the next layout (screen_geom_vertices_scale) */
  bk_json_free(root);
  screen_changed(screen_node);
  return 1;
}

/** \} */
