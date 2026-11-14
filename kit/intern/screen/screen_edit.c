/* Editing the screen layout: split, join, close, swap, dock, maximize.
 *
 * Ported from editors/screen/screen_edit.cc (area_split, area_getorientation,
 * area_getoffsets, screen_areas_align, screen_area_join_aligned,
 * screen_area_trim, screen_area_join_ex, screen_area_close,
 * screen_state_to_nonnormal / ED_screen_state_toggle) and screen_ops.cc
 * (area_docking_apply). Where Blender copies or swaps an area's space data
 * (ED_area_data_copy, ED_area_swapspace), the kit moves the area nodes
 * instead: their contents belong to the host, which fills new areas when
 * told (BK_EV_AREA).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "screen_intern.h"

int screen_headersize(void)
{
  /* ED_area_headersize: accommodate widget and padding */
  return bk_unit() + (int)(UI_SCALE_FAC * HEADER_PADDING_Y);
}

void screen_window_rect(const BKNode *screen_node, rcti *r_rect)
{
  int w = screen_node->w, h = screen_node->h;
  if (w < 2 || h < 2) { /* not laid out yet: the size the vertices were made at */
    w = h = 0;
    for (const ScrVert *v = (const ScrVert *)SCREEN(screen_node)->vertbase.first; v; v = v->next) {
      w = BK_MAX(w, v->x + 1);
      h = BK_MAX(h, v->y + 1);
    }
  }
  BLI_rcti_init(r_rect, 0, w, 0, h);
}

void screen_changed(BKNode *screen_node)
{
  bk_relayout(screen_node);
  bk_invalidate(screen_node);
  bk_emit(screen_node, BK_EV_CHANGE, NULL, 0, 0);
}

static int area_index(const BKNode *screen_node, const BKNode *area_node)
{
  for (int i = 0; i < screen_node->nkids; i++) {
    if (screen_node->kids[i] == area_node) {
      return i;
    }
  }
  return -1;
}

void screen_area_attach(BKNode *screen_node, BKNode *area_node, ScrVert *v1, ScrVert *v2,
                        ScrVert *v3, ScrVert *v4)
{
  BKArea *area = AREA(area_node);
  area->v1 = v1;
  area->v2 = v2;
  area->v3 = v3;
  area->v4 = v4;
  if (area_node->parent != screen_node) {
    BK_node_add(screen_node, area_node, -1);
  }
}

/* screen_addarea + ED_area_data_copy: a new area showing the same editor, which the host fills. */
static BKNode *screen_addarea(BKNode *screen_node, const BKNode *src, ScrVert *v1, ScrVert *v2,
                              ScrVert *v3, ScrVert *v4)
{
  BKNode *newa = bk_node_new_cls(&bk_cls_area);
  bk_set_text(&newa->text, src->text);
  screen_area_attach(screen_node, newa, v1, v2, v3, v4);
  return newa;
}

void screen_area_free(BKNode *screen_node, BKNode *area_node)
{
  BKScreen *screen = SCREEN(screen_node);
  if (screen->active == area_node) {
    screen->active = NULL;
  }
  if (area_node == screen->detaching) {
    return; /* the host is removing it already */
  }
  const int index = area_index(screen_node, area_node);
  if (index >= 0) {
    bk_emit(area_node, BK_EV_AREA, NULL, index, BK_AREA_CLOSE); /* on the area: its host object */
  }
  if (area_node->parent == screen_node) {
    void *ref = area_node->ref;
    screen->detaching = area_node; /* the geometry is done: no closing again in detach */
    BK_node_remove(screen_node, area_node);
    screen->detaching = NULL;
    if (!ref) {
      BK_node_free(area_node); /* made here (a split): nobody else holds it */
    }
  }
}

static void area_new_notify(BKNode *screen_node, BKNode *newa, BKNode *src)
{
  bk_emit(screen_node, BK_EV_AREA, NULL, area_index(screen_node, newa), BK_AREA_NEW);
  (void)src;
}

BKNode *area_split(BKNode *screen_node, BKNode *area_node, const eScreenAxis dir_axis,
                   const float fac)
{
  if (area_node == NULL) {
    return NULL;
  }
  BKScreen *screen = SCREEN(screen_node);
  BKArea *area = AREA(area_node);
  rcti window_rect;
  screen_window_rect(screen_node, &window_rect);

  const short split = screen_geom_find_area_split_point(area, &window_rect, dir_axis, fac);
  if (split == 0) {
    return NULL;
  }

  BKNode *newa;
  if (dir_axis == SCREEN_AXIS_H) {
    /* new vertices */
    ScrVert *sv1 = screen_geom_vertex_add(screen, area->v1->x, split);
    ScrVert *sv2 = screen_geom_vertex_add(screen, area->v4->x, split);
    /* new edges */
    screen_geom_edge_add(screen, area->v1, sv1);
    screen_geom_edge_add(screen, sv1, area->v2);
    screen_geom_edge_add(screen, area->v3, sv2);
    screen_geom_edge_add(screen, sv2, area->v4);
    screen_geom_edge_add(screen, sv1, sv2);
    if (fac > 0.5f) {
      /* new areas: top */
      newa = screen_addarea(screen_node, area_node, sv1, area->v2, area->v3, sv2);
      /* area below */
      area->v2 = sv1;
      area->v3 = sv2;
    }
    else {
      /* new areas: bottom */
      newa = screen_addarea(screen_node, area_node, area->v1, sv1, sv2, area->v4);
      /* area above */
      area->v1 = sv1;
      area->v4 = sv2;
    }
  }
  else {
    ScrVert *sv1 = screen_geom_vertex_add(screen, split, area->v1->y);
    ScrVert *sv2 = screen_geom_vertex_add(screen, split, area->v2->y);
    screen_geom_edge_add(screen, area->v1, sv1);
    screen_geom_edge_add(screen, sv1, area->v4);
    screen_geom_edge_add(screen, area->v2, sv2);
    screen_geom_edge_add(screen, sv2, area->v3);
    screen_geom_edge_add(screen, sv1, sv2);
    if (fac > 0.5f) {
      /* new areas: right */
      newa = screen_addarea(screen_node, area_node, sv1, sv2, area->v3, area->v4);
      /* area left */
      area->v3 = sv2;
      area->v4 = sv1;
    }
    else {
      /* new areas: left */
      newa = screen_addarea(screen_node, area_node, area->v1, area->v2, sv2, sv1);
      /* area right */
      area->v1 = sv1;
      area->v2 = sv2;
    }
  }

  screen_remove_double_scredges(screen);
  screen_remove_unused_scredges(screen_node);
  area_new_notify(screen_node, newa, area_node);
  return newa;
}

eScreenDir area_getorientation(const BKArea *sa_a, const BKArea *sa_b)
{
  if (sa_a == NULL || sa_b == NULL || sa_a == sa_b) {
    return SCREEN_DIR_NONE;
  }
  const short left_a = sa_a->v1->x, right_a = sa_a->v3->x;
  const short top_a = sa_a->v3->y, bottom_a = sa_a->v1->y;
  const short left_b = sa_b->v1->x, right_b = sa_b->v3->x;
  const short top_b = sa_b->v3->y, bottom_b = sa_b->v1->y;

  /* How much these areas share a common edge. */
  const short overlapx = (short)(BK_MIN(right_a, right_b) - BK_MAX(left_a, left_b));
  const short overlapy = (short)(BK_MIN(top_a, top_b) - BK_MAX(bottom_a, bottom_b));

  /* Minimum overlap required. */
  const short minx = (short)BK_MIN(BK_MIN((int)AREAJOINTOLERANCEX, right_a - left_a),
                                   right_b - left_b);
  const short miny = (short)BK_MIN(BK_MIN((int)AREAJOINTOLERANCEY, top_a - bottom_a),
                                   top_b - bottom_b);

  if (top_a == bottom_b && overlapx >= minx) {
    return SCREEN_DIR_N; /* sa_a to bottom of sa_b */
  }
  if (bottom_a == top_b && overlapx >= minx) {
    return SCREEN_DIR_S; /* sa_a on top of sa_b */
  }
  if (left_a == right_b && overlapy >= miny) {
    return SCREEN_DIR_W; /* sa_a to right of sa_b */
  }
  if (right_a == left_b && overlapy >= miny) {
    return SCREEN_DIR_E; /* sa_a to left of sa_b */
  }
  return SCREEN_DIR_NONE;
}

void area_getoffsets(const BKArea *sa_a, const BKArea *sa_b, const eScreenDir dir,
                     int *r_offset1, int *r_offset2)
{
  if (sa_a == NULL || sa_b == NULL) {
    *r_offset1 = *r_offset2 = INT_MAX;
  }
  else if (dir == SCREEN_DIR_W) { /* West: sa on right and sa_b to the left. */
    *r_offset1 = sa_b->v3->y - sa_a->v2->y;
    *r_offset2 = sa_b->v4->y - sa_a->v1->y;
  }
  else if (dir == SCREEN_DIR_N) { /* North: sa below and sa_b above. */
    *r_offset1 = sa_a->v2->x - sa_b->v1->x;
    *r_offset2 = sa_a->v3->x - sa_b->v4->x;
  }
  else if (dir == SCREEN_DIR_E) { /* East: sa on left and sa_b to the right. */
    *r_offset1 = sa_b->v2->y - sa_a->v3->y;
    *r_offset2 = sa_b->v1->y - sa_a->v4->y;
  }
  else if (dir == SCREEN_DIR_S) { /* South: sa above and sa_b below. */
    *r_offset1 = sa_a->v1->x - sa_b->v2->x;
    *r_offset2 = sa_a->v4->x - sa_b->v3->x;
  }
  else {
    *r_offset1 = *r_offset2 = INT_MAX;
  }
}

/* Screen verts on `from` along an axis move to `to` (screen_verts_halign / _valign). */
static void screen_verts_align(BKScreen *screen, const bool horizontal, const short from,
                               const short to)
{
  for (ScrVert *v = (ScrVert *)screen->vertbase.first; v; v = v->next) {
    short *c = horizontal ? &v->x : &v->y;
    if (*c == from) {
      *c = to;
    }
  }
}

/* Test if two adjoining areas can be aligned by having their screen edges adjusted. */
static bool screen_areas_can_align(BKNode *screen_node, BKArea *sa1, BKArea *sa2, eScreenDir dir)
{
  if (dir == SCREEN_DIR_NONE) {
    return false;
  }
  int offset1, offset2;
  area_getoffsets(sa1, sa2, dir, &offset1, &offset2);
  const int tolerance = (int)(SCREEN_DIR_IS_HORIZONTAL(dir) ? AREAJOINTOLERANCEY :
                                                              AREAJOINTOLERANCEX);
  if ((abs(offset1) >= tolerance) || (abs(offset2) >= tolerance)) {
    return false; /* misalignment is too great */
  }
  /* Areas that are _smaller_ than minimum sizes, sharing an edge to be moved. See #100772. */
  const bool vertical = SCREEN_DIR_IS_VERTICAL(dir);
  const short lo = vertical ? BK_MIN(sa1->v1->x, sa2->v1->x) : BK_MIN(sa1->v1->y, sa2->v1->y);
  const short hi = vertical ? BK_MAX(sa1->v3->x, sa2->v3->x) : BK_MAX(sa1->v3->y, sa2->v3->y);
  SCREEN_AREAS_ITER (screen_node, area) {
    if (area == sa1 || area == sa2) {
      continue;
    }
    const short a1 = vertical ? area->v1->x : area->v1->y;
    const short a3 = vertical ? area->v3->x : area->v3->y;
    if (a3 - a1 < tolerance && (a1 == lo || a3 == hi)) {
      return false; /* a narrow area interferes with this operation */
    }
  }
  return true;
}

/* Adjust all screen edges to allow joining two areas. */
static bool screen_areas_align(BKNode *screen_node, BKArea *sa1, BKArea *sa2, const eScreenDir dir)
{
  if (!screen_areas_can_align(screen_node, sa1, sa2, dir)) {
    return false;
  }
  BKScreen *screen = SCREEN(screen_node);
  if (SCREEN_DIR_IS_HORIZONTAL(dir)) {
    /* horizontal join, use average for new top and bottom. */
    const short top = (short)((sa1->v2->y + sa2->v2->y) / 2);
    const short bottom = (short)((sa1->v4->y + sa2->v4->y) / 2);
    screen_verts_align(screen, false, sa1->v2->y, top);
    screen_verts_align(screen, false, sa1->v4->y, bottom);
    screen_verts_align(screen, false, sa2->v2->y, top);
    screen_verts_align(screen, false, sa2->v4->y, bottom);
  }
  else {
    /* Vertical join, use averages for new left and right. */
    const short left = (short)((sa1->v1->x + sa2->v1->x) / 2);
    const short right = (short)((sa1->v3->x + sa2->v3->x) / 2);
    screen_verts_align(screen, true, sa1->v1->x, left);
    screen_verts_align(screen, true, sa1->v3->x, right);
    screen_verts_align(screen, true, sa2->v1->x, left);
    screen_verts_align(screen, true, sa2->v3->x, right);
  }
  return true;
}

/* Simple join of two areas without any splitting. Will return false if not possible. */
static bool screen_area_join_aligned(BKNode *screen_node, BKNode *n1, BKNode *n2)
{
  BKScreen *screen = SCREEN(screen_node);
  BKArea *sa1 = AREA(n1), *sa2 = AREA(n2);
  const eScreenDir dir = area_getorientation(sa1, sa2);
  if (!screen_areas_align(screen_node, sa1, sa2, dir)) {
    return false;
  }
  if (dir == SCREEN_DIR_W) { /* sa1 to right of sa2 = West. */
    sa1->v1 = sa2->v1;
    sa1->v2 = sa2->v2;
    screen_geom_edge_add(screen, sa1->v2, sa1->v3);
    screen_geom_edge_add(screen, sa1->v1, sa1->v4);
  }
  else if (dir == SCREEN_DIR_N) { /* sa1 to bottom of sa2 = North. */
    sa1->v2 = sa2->v2;
    sa1->v3 = sa2->v3;
    screen_geom_edge_add(screen, sa1->v1, sa1->v2);
    screen_geom_edge_add(screen, sa1->v3, sa1->v4);
  }
  else if (dir == SCREEN_DIR_E) { /* sa1 to left of sa2 = East. */
    sa1->v3 = sa2->v3;
    sa1->v4 = sa2->v4;
    screen_geom_edge_add(screen, sa1->v2, sa1->v3);
    screen_geom_edge_add(screen, sa1->v1, sa1->v4);
  }
  else if (dir == SCREEN_DIR_S) { /* sa1 on top of sa2 = South. */
    sa1->v1 = sa2->v1;
    sa1->v4 = sa2->v4;
    screen_geom_edge_add(screen, sa1->v1, sa1->v2);
    screen_geom_edge_add(screen, sa1->v3, sa1->v4);
  }
  screen_area_free(screen_node, n2);
  return true;
}

/* Slice off and return a new area. "Reverse" gives right/bottom, rather than left/top. */
static BKNode *screen_area_trim(BKNode *screen_node, BKNode **area, int size, eScreenDir dir,
                                bool reverse)
{
  const bool vertical = SCREEN_DIR_IS_VERTICAL(dir);
  if (abs(size) < (vertical ? AREAJOINTOLERANCEX : AREAJOINTOLERANCEY)) {
    return NULL;
  }
  const BKArea *a = AREA(*area);
  /* Measurement with ScrVerts because winx and winy might not be correct at this time. */
  float fac = (float)abs(size) / (float)(vertical ? (a->v3->x - a->v1->x) : (a->v3->y - a->v1->y));
  fac = (reverse == vertical) ? 1.0f - fac : fac;
  BKNode *newsa = area_split(screen_node, *area, vertical ? SCREEN_AXIS_V : SCREEN_AXIS_H, fac);
  /* area_split always returns smallest of the two areas, so might have to swap. */
  if (((fac > 0.5f) == vertical) != reverse) {
    BKNode *temp = *area;
    *area = newsa;
    newsa = temp;
  }
  return newsa;
}

/* Join any two neighboring areas. Might create new areas, kept if over min_remainder. */
static bool screen_area_join_ex(BKNode *screen_node, BKNode *sa1, BKNode *sa2,
                                bool close_all_remainders)
{
  const eScreenDir dir = area_getorientation(AREA(sa1), AREA(sa2));
  if (dir == SCREEN_DIR_NONE) {
    return false;
  }
  int offset1, offset2;
  area_getoffsets(AREA(sa1), AREA(sa2), dir, &offset1, &offset2);

  /* Split Left/Top into new area if overhanging. */
  BKNode *side1 = screen_area_trim(screen_node, (offset1 > 0) ? &sa2 : &sa1, offset1, dir, false);
  /* Split Right/Bottom into new area if overhanging. */
  BKNode *side2 = screen_area_trim(screen_node, (offset2 > 0) ? &sa1 : &sa2, offset2, dir, true);

  /* The two areas now line up, so join them. */
  screen_area_join_aligned(screen_node, sa1, sa2);

  if (close_all_remainders || offset1 < 0 || offset2 > 0) {
    /* Close both if trimming `sa1`, but not by joining with the area just split. */
    if (side1) {
      screen_area_close(screen_node, side1, sa1);
    }
    if (side2) {
      screen_area_close(screen_node, side2, sa1);
    }
  }
  if (SCREEN(screen_node)->active == sa2) {
    SCREEN(screen_node)->active = NULL;
  }
  return true;
}

int screen_area_join(BKNode *screen_node, BKNode *sa1, BKNode *sa2)
{
  return screen_area_join_ex(screen_node, sa1, sa2, false);
}

bool screen_area_close(BKNode *screen_node, BKNode *area_node, BKNode *not_area)
{
  if (area_node == NULL) {
    return false;
  }
  const BKArea *area = AREA(area_node);
  BKNode *sa2 = NULL;
  float best_alignment = 0.0f;
  for (int i = 0; i < screen_node->nkids; i++) {
    BKNode *n = screen_node->kids[i];
    if (n == area_node || n == not_area) {
      continue;
    }
    const BKArea *neighbor = AREA(n);
    const eScreenDir dir = area_getorientation(area, neighbor);
    /* Must at least partially share an edge. */
    if (dir != SCREEN_DIR_NONE) {
      const bool vertical = SCREEN_DIR_IS_VERTICAL(dir);
      const int area_length = vertical ? (area->v3->x - area->v1->x) : (area->v3->y - area->v1->y);
      const int ar_length = vertical ? (neighbor->v3->x - neighbor->v1->x) :
                                       (neighbor->v3->y - neighbor->v1->y);
      /* The ratio of the lengths of the shared edges. */
      const float alignment = (float)BK_MIN(area_length, ar_length) /
                              (float)BK_MAX(area_length, ar_length);
      if (alignment > best_alignment) {
        best_alignment = alignment;
        sa2 = n;
      }
    }
  }
  /* Join from neighbor into this area to close it. */
  return sa2 && screen_area_join_ex(screen_node, sa2, area_node, true);
}

void area_swapspace(BKNode *screen_node, BKNode *n1, BKNode *n2)
{
  /* ED_area_swapspace swaps the contents; the kit swaps where the two areas are. */
  BKArea *a = AREA(n1), *b = AREA(n2);
  ScrVert *t;
  t = a->v1, a->v1 = b->v1, b->v1 = t;
  t = a->v2, a->v2 = b->v2, b->v2 = t;
  t = a->v3, a->v3 = b->v3, b->v3 = t;
  t = a->v4, a->v4 = b->v4, b->v4 = t;
  const rcti r = a->totrct;
  a->totrct = b->totrct;
  b->totrct = r;
  bk_relayout(screen_node);
}

bool area_docking_apply(BKNode *screen_node, BKNode *sa1, BKNode *sa2,
                        const AreaDockTarget dock_target, const float factor)
{
  int offset1, offset2;
  area_getoffsets(AREA(sa1), AREA(sa2), area_getorientation(AREA(sa1), AREA(sa2)), &offset1,
                  &offset2);
  /* Check before making changes. */
  const bool aligned_neighbors = (offset1 == 0 && offset2 == 0);
  const bool same_area = (sa1 == sa2);

  if (dock_target != DOCK_CENTER) {
    const eScreenAxis dir = (dock_target == DOCK_LEFT || dock_target == DOCK_RIGHT) ?
                                SCREEN_AXIS_V :
                                SCREEN_AXIS_H;
    float fac = factor;
    if (dock_target == DOCK_RIGHT || dock_target == DOCK_TOP) {
      fac = 1.0f - fac;
    }
    BKNode *newa = area_split(screen_node, sa2, dir, fac);
    if (newa == NULL) {
      return false;
    }
    if (factor <= 0.5f) {
      sa2 = newa;
    }
  }
  if (same_area) {
    return true;
  }
  if (!aligned_neighbors || !screen_area_join(screen_node, sa1, sa2)) {
    /* Move the source into the target's place and close what is left where it was. */
    area_swapspace(screen_node, sa2, sa1);
    screen_area_close(screen_node, sa2, NULL);
  }
  return true;
}

/* The regions focus mode hides (screen_state_to_nonnormal). */
static bool region_hidden_in_focus(int type)
{
  return type == RGN_TYPE_UI || type == RGN_TYPE_HEADER || type == RGN_TYPE_TOOL_HEADER ||
         type == RGN_TYPE_FOOTER || type == RGN_TYPE_TOOLS || type == RGN_TYPE_NAV_BAR ||
         type == RGN_TYPE_EXECUTE || type == RGN_TYPE_ASSET_SHELF;
}

void screen_state_toggle(BKNode *screen_node, BKNode *area_node, const short state)
{
  BKScreen *screen = SCREEN(screen_node);
  BKArea *area = AREA(area_node);
  if (area->full) {
    /* restoring back to SCREENNORMAL */
    if (area->full == SCREENFULL) {
      for (int i = 0; i < area_node->nkids; i++) {
        BKRegion *region = region_of(area_node->kids[i]);
        if (region) {
          region->flag = region->flagfullscreen;
        }
      }
    }
    area->full = 0;
    screen->state = SCREENNORMAL;
  }
  else {
    SCREEN_AREAS_ITER (screen_node, other) {
      if (other->full) {
        screen_state_toggle(screen_node, other->node, other->full); /* one at a time */
      }
    }
    if (state == SCREENFULL) {
      /* temporarily hide the side panels/header */
      for (int i = 0; i < area_node->nkids; i++) {
        BKRegion *region = region_of(area_node->kids[i]);
        if (region) {
          region->flagfullscreen = region->flag;
          if (region_hidden_in_focus(region->type)) {
            region->flag |= RGN_FLAG_HIDDEN;
          }
        }
      }
    }
    area->full = state;
    screen->state = state;
  }
  for (int i = 0; i < screen_node->nkids; i++) {
    BKNode *n = screen_node->kids[i];
    const bool shown = screen->state == SCREENNORMAL || AREA(n)->full;
    n->flags = shown ? (n->flags | NF_VISIBLE) : (n->flags & ~NF_VISIBLE);
  }
  screen_changed(screen_node);
}
