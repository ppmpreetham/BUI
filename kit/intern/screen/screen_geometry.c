/* Screen vertices and edges: the graph Blender's screen layout lives on.
 *
 * Ported from editors/screen/screen_geometry.cc and the ScrVert/ScrEdge part
 * of blenkernel/intern/screen.cc (BKE_screen_find_edge, _sort_scrvert,
 * _remove_double_*, _remove_unused_*). There are no global areas (top bar,
 * status bar) here: the window rect and the screen rect are the same.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <math.h>
#include <stdlib.h>

#include "screen_intern.h"

static short round_fl_to_short(float v)
{
  return (short)floorf(v + 0.5f);
}

int screen_geom_area_height(const BKArea *area)
{
  return area->v2->y - area->v1->y + 1;
}
int screen_geom_area_width(const BKArea *area)
{
  return area->v4->x - area->v1->x + 1;
}

ScrVert *screen_geom_vertex_add(BKScreen *screen, short x, short y)
{
  ScrVert *sv = (ScrVert *)calloc(1, sizeof(ScrVert));
  sv->x = x;
  sv->y = y;
  BLI_addtail(&screen->vertbase, sv);
  return sv;
}

static void sort_scrvert(ScrVert **v1, ScrVert **v2)
{
  if (*v1 > *v2) {
    ScrVert *tmp = *v1;
    *v1 = *v2;
    *v2 = tmp;
  }
}

ScrEdge *screen_geom_edge_add(BKScreen *screen, ScrVert *v1, ScrVert *v2)
{
  ScrEdge *se = (ScrEdge *)calloc(1, sizeof(ScrEdge));
  sort_scrvert(&v1, &v2);
  se->v1 = v1;
  se->v2 = v2;
  BLI_addtail(&screen->edgebase, se);
  return se;
}

bool screen_geom_edge_is_horizontal(const ScrEdge *se)
{
  return se->v1->y == se->v2->y;
}

ScrEdge *screen_geom_find_active_scredge(const BKScreen *screen, const rcti *bounds_rect,
                                         const int mx, const int my, int safety)
{
  safety = BK_MAX(safety, 2);
  for (ScrEdge *se = (ScrEdge *)screen->edgebase.first; se; se = se->next) {
    if (screen_geom_edge_is_horizontal(se)) {
      if ((se->v1->y > bounds_rect->ymin) && (se->v1->y < (bounds_rect->ymax - 1))) {
        const short min = BK_MIN(se->v1->x, se->v2->x), max = BK_MAX(se->v1->x, se->v2->x);
        if (abs(my - se->v1->y) <= safety && mx >= min && mx <= max) {
          return se;
        }
      }
    }
    else {
      if ((se->v1->x > bounds_rect->xmin) && (se->v1->x < (bounds_rect->xmax - 1))) {
        const short min = BK_MIN(se->v1->y, se->v2->y), max = BK_MAX(se->v1->y, se->v2->y);
        if (abs(mx - se->v1->x) <= safety && my >= min && my <= max) {
          return se;
        }
      }
    }
  }
  return NULL;
}

ScrEdge *screen_find_edge(const BKScreen *screen, ScrVert *v1, ScrVert *v2)
{
  sort_scrvert(&v1, &v2);
  for (ScrEdge *se = (ScrEdge *)screen->edgebase.first; se; se = se->next) {
    if (se->v1 == v1 && se->v2 == v2) {
      return se;
    }
  }
  return NULL;
}

/* A single pass moving all screen vertices to fit into `screen_rect`: true when another pass
 * should run (screen_geom_vertices_scale_pass). */
static bool vertices_scale_pass(BKNode *screen_node, const rcti *screen_rect)
{
  BKScreen *screen = SCREEN(screen_node);
  const int screen_size_x = BLI_rcti_size_x(screen_rect);
  const int screen_size_y = BLI_rcti_size_y(screen_rect);
  bool needs_another_pass = false;

  float min[2] = {20000.0f, 20000.0f};
  float max[2] = {0.0f, 0.0f};
  for (ScrVert *sv = (ScrVert *)screen->vertbase.first; sv; sv = sv->next) {
    min[0] = fminf(min[0], sv->x);
    min[1] = fminf(min[1], sv->y);
    max[0] = fmaxf(max[0], sv->x);
    max[1] = fmaxf(max[1], sv->y);
  }
  const int screen_size_x_prev = (int)(max[0] - min[0]) + 1;
  const int screen_size_y_prev = (int)(max[1] - min[1]) + 1;

  if (screen_size_x_prev != screen_size_x || screen_size_y_prev != screen_size_y) {
    const float facx = ((float)screen_size_x - 1) / ((float)screen_size_x_prev - 1);
    const float facy = ((float)screen_size_y - 1) / ((float)screen_size_y_prev - 1);

    /* make sure it fits! */
    for (ScrVert *sv = (ScrVert *)screen->vertbase.first; sv; sv = sv->next) {
      sv->x = (short)(screen_rect->xmin + round_fl_to_short((sv->x - min[0]) * facx));
      sv->x = (short)BK_CLAMP(sv->x, screen_rect->xmin, screen_rect->xmax - 1);
      sv->y = (short)(screen_rect->ymin + round_fl_to_short((sv->y - min[1]) * facy));
      sv->y = (short)BK_CLAMP(sv->y, screen_rect->ymin, screen_rect->ymax - 1);
    }

    /* Make each area at least ED_area_headersize() high, growing or shrinking. */
    const int border_width = (int)ceilf((float)U_BORDER_WIDTH * UI_SCALE_FAC);
    SCREEN_AREAS_ITER (screen_node, area) {
      int amin = screen_headersize() + border_width + border_width - U_PIXELSIZE;
      if (area->v3->y >= (screen_rect->ymax - 1)) {
        amin = screen_headersize() + border_width; /* aligned to the top screen edge */
      }
      else if (area->v4->y <= (screen_rect->ymin + 1)) {
        amin = screen_headersize() + border_width + 1; /* aligned to the bottom screen edge */
      }
      if (screen_geom_area_height(area) < amin) {
        /* lower edge */
        ScrEdge *se = screen_find_edge(screen, area->v4, area->v1);
        if (se && area->v1 != area->v2) {
          const int yval = area->v2->y - amin;
          screen_geom_select_connected_edge(screen, se);
          /* all selected vertices get the right offset */
          for (ScrVert *sv = (ScrVert *)screen->vertbase.first; sv; sv = sv->next) {
            if (sv != area->v2 && sv != area->v3 && sv->flag) {
              sv->y = (short)yval;
              needs_another_pass = true;
            }
          }
        }
      }
    }
  }
  return needs_another_pass;
}

void screen_geom_vertices_scale(BKNode *screen_node, const rcti *screen_rect)
{
  int max_passes_left = 10; /* avoids an endless loop */
  bool needs_another_pass;
  do {
    needs_another_pass = vertices_scale_pass(screen_node, screen_rect);
    max_passes_left--;
  } while (needs_another_pass && max_passes_left > 0);
}

short screen_geom_find_area_split_point(const BKArea *area, const rcti *window_rect,
                                        const eScreenAxis dir_axis, float fac)
{
  const int cur_area_width = screen_geom_area_width(area);
  const int cur_area_height = screen_geom_area_height(area);
  const short area_min_x = (short)(AREAMINX * UI_SCALE_FAC);
  const short area_min_y = (short)screen_headersize();

  /* area big enough? */
  if (dir_axis == SCREEN_AXIS_V) {
    if (cur_area_width <= 2 * area_min_x) {
      return 0;
    }
  }
  else if (cur_area_height <= 2 * area_min_y) {
    return 0;
  }

  fac = BK_CLAMP(fac, 0.0f, 1.0f);

  if (dir_axis == SCREEN_AXIS_H) {
    short y = (short)(area->v1->y + round_fl_to_short(fac * cur_area_height));
    int area_min = area_min_y;
    if (area->v1->y > window_rect->ymin) {
      area_min += U_PIXELSIZE;
    }
    if (area->v2->y < (window_rect->ymax - 1)) {
      area_min += U_PIXELSIZE;
    }
    if (y - area->v1->y < area_min) {
      y = (short)(area->v1->y + area_min);
    }
    else if (area->v2->y - y < area_min) {
      y = (short)(area->v2->y - area_min);
    }
    return y;
  }

  short x = (short)(area->v1->x + round_fl_to_short(fac * cur_area_width));
  int area_min = area_min_x;
  if (area->v1->x > window_rect->xmin) {
    area_min += U_PIXELSIZE;
  }
  if (area->v4->x < (window_rect->xmax - 1)) {
    area_min += U_PIXELSIZE;
  }
  if (x - area->v1->x < area_min) {
    x = (short)(area->v1->x + area_min);
  }
  else if (area->v4->x - x < area_min) {
    x = (short)(area->v4->x - area_min);
  }
  return x;
}

static void verts_flag_clear(BKScreen *screen)
{
  for (ScrVert *sv = (ScrVert *)screen->vertbase.first; sv; sv = sv->next) {
    sv->flag = 0;
  }
}

void screen_geom_select_connected_edge(BKScreen *screen, ScrEdge *edge)
{
  /* 'dir_axis' is the direction of EDGE */
  const eScreenAxis dir_axis = (edge->v1->x == edge->v2->x) ? SCREEN_AXIS_V : SCREEN_AXIS_H;
  verts_flag_clear(screen);
  edge->v1->flag = 1;
  edge->v2->flag = 1;

  /* select connected, only in the right direction */
  bool oneselected = true;
  while (oneselected) {
    oneselected = false;
    for (ScrEdge *se = (ScrEdge *)screen->edgebase.first; se; se = se->next) {
      if (se->v1->flag + se->v2->flag == 1) {
        if ((dir_axis == SCREEN_AXIS_H && se->v1->y == se->v2->y) ||
            (dir_axis == SCREEN_AXIS_V && se->v1->x == se->v2->x))
        {
          se->v1->flag = se->v2->flag = 1;
          oneselected = true;
        }
      }
    }
  }
}

bool screen_geom_edge_can_extend(BKScreen *screen, ScrEdge *edge)
{
  bool can_extend = false;
  const eScreenAxis dir_axis = (edge->v1->x == edge->v2->x) ? SCREEN_AXIS_V : SCREEN_AXIS_H;
  screen_geom_select_connected_edge(screen, edge);

  for (ScrEdge *se = (ScrEdge *)screen->edgebase.first; se; se = se->next) {
    if (se->v1->flag + se->v2->flag != 0) {
      continue;
    }
    for (ScrVert *v = (ScrVert *)screen->vertbase.first; v; v = v->next) {
      const bool aligned =
          (dir_axis == SCREEN_AXIS_H) ?
              (v->x == se->v1->x && (abs(v->y - se->v1->y) < EDGE_ALIGN_TOLERANCE ||
                                     abs(v->y - se->v2->y) < EDGE_ALIGN_TOLERANCE)) :
              (v->y == se->v1->y && (abs(v->x - se->v1->x) < EDGE_ALIGN_TOLERANCE ||
                                     abs(v->x - se->v2->x) < EDGE_ALIGN_TOLERANCE));
      if (v->flag && aligned) {
        se->v1->flag = se->v2->flag = 1;
        can_extend = true;
        break;
      }
    }
  }
  verts_flag_clear(screen);
  return can_extend;
}

void screen_geom_select_extended_edge(BKScreen *screen, ScrEdge *edge)
{
  const eScreenAxis dir_axis = (edge->v1->x == edge->v2->x) ? SCREEN_AXIS_V : SCREEN_AXIS_H;
  verts_flag_clear(screen);
  for (ScrVert *v = (ScrVert *)screen->vertbase.first; v; v = v->next) {
    if ((dir_axis == SCREEN_AXIS_H && abs(v->y - edge->v1->y) < EDGE_ALIGN_TOLERANCE) ||
        (dir_axis == SCREEN_AXIS_V && abs(v->x - edge->v1->x) < EDGE_ALIGN_TOLERANCE))
    {
      v->flag = 1;
    }
  }
}

/* Replace the vertices with `newv` set in edges and areas, then free them (shared by
 * BKE_screen_remove_double_scrverts and screen_geom_edge_aligned_merge). */
static void verts_replace_newv(BKNode *screen_node)
{
  BKScreen *screen = SCREEN(screen_node);
  for (ScrEdge *se = (ScrEdge *)screen->edgebase.first; se; se = se->next) {
    if (se->v1->newv) {
      se->v1 = se->v1->newv;
    }
    if (se->v2->newv) {
      se->v2 = se->v2->newv;
    }
    sort_scrvert(&se->v1, &se->v2);
  }
  SCREEN_AREAS_ITER (screen_node, area) {
    ScrVert **vs[4] = {&area->v1, &area->v2, &area->v3, &area->v4};
    for (int i = 0; i < 4; i++) {
      if ((*vs[i])->newv) {
        *vs[i] = (*vs[i])->newv;
      }
    }
  }
  for (ScrVert *v = (ScrVert *)screen->vertbase.first, *next; v; v = next) {
    next = v->next;
    if (v->newv) {
      BLI_remlink(&screen->vertbase, v);
      free(v);
    }
  }
}

void screen_geom_edge_aligned_merge(BKNode *screen_node, ScrEdge *edge)
{
  BKScreen *screen = SCREEN(screen_node);
  screen_geom_select_extended_edge(screen, edge);
  const eScreenAxis dir_axis = (edge->v1->x == edge->v2->x) ? SCREEN_AXIS_V : SCREEN_AXIS_H;
  /* Align the vertices if close. */
  for (ScrVert *v = (ScrVert *)screen->vertbase.first; v; v = v->next) {
    if (dir_axis == SCREEN_AXIS_V && abs(v->x - edge->v2->x) < EDGE_ALIGN_TOLERANCE) {
      v->x = edge->v2->x;
    }
    else if (abs(v->y - edge->v2->y) < EDGE_ALIGN_TOLERANCE) {
      v->y = edge->v2->y;
    }
  }
  for (ScrVert *v = (ScrVert *)screen->vertbase.first; v; v = v->next) {
    if (v->flag == 1 && v->newv == NULL) {
      for (ScrVert *v1 = v->next; v1; v1 = v1->next) {
        if (v1->newv == NULL && abs(v1->x - v->x) < EDGE_ALIGN_TOLERANCE &&
            abs(v1->y - v->y) < EDGE_ALIGN_TOLERANCE)
        {
          v1->newv = v;
        }
      }
    }
  }
  verts_replace_newv(screen_node);
  verts_flag_clear(screen);
}

void screen_remove_double_scrverts(BKNode *screen_node)
{
  BKScreen *screen = SCREEN(screen_node);
  for (ScrVert *verg = (ScrVert *)screen->vertbase.first; verg; verg = verg->next) {
    if (verg->newv == NULL) {
      for (ScrVert *v1 = verg->next; v1; v1 = v1->next) {
        if (v1->newv == NULL && v1->x == verg->x && v1->y == verg->y) {
          v1->newv = verg;
        }
      }
    }
  }
  verts_replace_newv(screen_node);
}

void screen_remove_double_scredges(BKScreen *screen)
{
  for (ScrEdge *verg = (ScrEdge *)screen->edgebase.first; verg; verg = verg->next) {
    for (ScrEdge *se = verg->next, *sn; se; se = sn) {
      sn = se->next;
      if (verg->v1 == se->v1 && verg->v2 == se->v2) {
        BLI_remlink(&screen->edgebase, se);
        free(se);
      }
    }
  }
}

void screen_remove_unused_scredges(BKNode *screen_node)
{
  BKScreen *screen = SCREEN(screen_node);
  /* sets flags when edge is used in area */
  SCREEN_AREAS_ITER (screen_node, area) {
    ScrVert *loop[5] = {area->v1, area->v2, area->v3, area->v4, area->v1};
    for (int i = 0; i < 4; i++) {
      ScrEdge *se = screen_find_edge(screen, loop[i], loop[i + 1]);
      if (se) {
        se->flag = 1;
      }
    }
  }
  for (ScrEdge *se = (ScrEdge *)screen->edgebase.first, *next; se; se = next) {
    next = se->next;
    if (se->flag == 0) {
      BLI_remlink(&screen->edgebase, se);
      free(se);
    }
    else {
      se->flag = 0;
    }
  }
}

void screen_remove_unused_scrverts(BKScreen *screen)
{
  /* we assume edges are ok */
  for (ScrEdge *se = (ScrEdge *)screen->edgebase.first; se; se = se->next) {
    se->v1->flag = 1;
    se->v2->flag = 1;
  }
  for (ScrVert *sv = (ScrVert *)screen->vertbase.first, *next; sv; sv = next) {
    next = sv->next;
    if (sv->flag == 0) {
      BLI_remlink(&screen->vertbase, sv);
      free(sv);
    }
    else {
      sv->flag = 0;
    }
  }
}

void screen_geom_free(BKScreen *screen)
{
  BLI_freelistN(&screen->vertbase);
  BLI_freelistN(&screen->edgebase);
}

BKNode *screen_find_area_xy(BKNode *screen_node, const int x, const int y)
{
  for (int i = 0; i < screen_node->nkids; i++) {
    const BKArea *area = AREA(screen_node->kids[i]);
    /* Test the area's outer screen verts, not the inner totrct. */
    if (NODE_VISIBLE(area->node) && x >= area->v1->x && x <= area->v4->x && y >= area->v1->y &&
        y <= area->v2->y)
    {
      return area->node;
    }
  }
  return NULL;
}
