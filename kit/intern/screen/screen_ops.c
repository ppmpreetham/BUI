/* The screen's interactive operators.
 *
 * Ported from editors/screen/screen_ops.cc:
 *  - SCREEN_OT_area_move: area_move_set_limits, area_move_init / _reinit,
 *    area_snap_calc_location, area_move_apply_do, area_move_modal
 *  - SCREEN_OT_actionzone: the corner gestures (Shift: into a new window,
 *    Ctrl: swap) and SCREEN_OT_area_join, which the plain corner drag runs:
 *    area_join_update_data, area_docking_target, area_docking_snap,
 *    area_split_factor, area_join_cursor, area_join_modal
 *  - SCREEN_OT_area_swap, SCREEN_OT_area_close, SCREEN_OT_area_dupli
 *  - SCREEN_OT_region_scale: region_scale_invoke / _modal,
 *    region_scale_validate_size, region_scale_toggle_hidden
 *  - SCREEN_OT_area_options and the header context menu
 *    (screen_area_options_invoke, ED_screens_header_tools_menu_create,
 *    screen_area_menu_items, screen_area_touch_menu_create)
 *  - SCREEN_OT_screen_full_area (Ctrl+Space, Ctrl+Alt+Space), SCREEN_OT_edge_merge
 * Blender's WM cursors map onto the kit's: X/Y move to EW/NS, the split and
 * arrow cursors to NS/EW/move, swap to move, stop to not-allowed, pick-area
 * and edit to crosshair.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "screen_intern.h"

static void event_space(const BKNode *space, const BKEvent *e, int *r_x, int *r_y)
{
  space_from_window(space, e->x, e->y, r_x, r_y);
}

static void screen_rect(const BKNode *screen_node, rcti *r)
{
  screen_window_rect(screen_node, r);
}

static ScrEdge *screen_edge_at(BKNode *screen_node, int x, int y)
{
  rcti rect;
  screen_rect(screen_node, &rect);
  return screen_geom_find_active_scredge(SCREEN(screen_node), &rect, x, y, (int)BORDERPADDING);
}

/* The areas on both sides of the edge under the cursor (screen_area_edge_from_cursor). */
static ScrEdge *screen_area_edge_from_cursor(BKNode *screen_node, int x, int y, BKNode **r_sa1,
                                             BKNode **r_sa2)
{
  rcti rect;
  screen_rect(screen_node, &rect);
  ScrEdge *actedge = screen_geom_find_active_scredge(SCREEN(screen_node), &rect, x, y, 2);
  *r_sa1 = *r_sa2 = NULL;
  if (!actedge) {
    return NULL;
  }
  const int borderwidth = (int)(4 * UI_SCALE_FAC);
  if (screen_geom_edge_is_horizontal(actedge)) {
    *r_sa1 = screen_find_area_xy(screen_node, x, y + borderwidth);
    *r_sa2 = screen_find_area_xy(screen_node, x, y - borderwidth);
  }
  else {
    *r_sa1 = screen_find_area_xy(screen_node, x + borderwidth, y);
    *r_sa2 = screen_find_area_xy(screen_node, x - borderwidth, y);
  }
  return actedge;
}

static void screen_redraw(BKNode *screen_node)
{
  bk_relayout(screen_node);
  bk_invalidate(screen_node);
}

/* -------------------------------------------------------------------- */
/** \name Move area edge (SCREEN_OT_area_move)
 * \{ */

static void area_move_set_limits(BKNode *screen_node, const eScreenAxis dir_axis, int *bigger,
                                 int *smaller)
{
  /* we check all areas and test for free space with MINSIZE */
  *bigger = *smaller = 100000;
  SCREEN_AREAS_ITER (screen_node, area) {
    if (dir_axis == SCREEN_AXIS_H) {
      const int y1 = area->winy - screen_headersize();
      /* if top or down edge selected, test height */
      if (area->v1->editflag && area->v4->editflag) {
        *bigger = BK_MIN(*bigger, y1);
      }
      else if (area->v2->editflag && area->v3->editflag) {
        *smaller = BK_MIN(*smaller, y1);
      }
    }
    else {
      const int x1 = area->winx - (int)(AREAMINX * UI_SCALE_FAC) - 1;
      /* if left or right edge selected, test width */
      if (area->v1->editflag && area->v2->editflag) {
        *bigger = BK_MIN(*bigger, x1);
      }
      else if (area->v3->editflag && area->v4->editflag) {
        *smaller = BK_MIN(*smaller, x1);
      }
    }
  }
}

static void area_move_select(BKNode *screen_node, ScrEdge *actedge, bool extend)
{
  BKScreen *screen = SCREEN(screen_node);
  screen->md.can_extend = screen_geom_edge_can_extend(screen, actedge);
  if (screen->md.can_extend && extend) {
    screen_geom_select_extended_edge(screen, actedge);
    screen->md.extending = true;
  }
  else {
    screen_geom_select_connected_edge(screen, actedge);
    screen->md.extending = false;
  }
  /* now all vertices with 'flag == 1' are the ones that can be moved. Move this to editflag */
  for (ScrVert *v = (ScrVert *)screen->vertbase.first; v; v = v->next) {
    v->editflag = v->flag;
  }
  area_move_set_limits(screen_node, screen->md.dir_axis, &screen->md.bigger, &screen->md.smaller);
}

static bool area_move_init(BKNode *screen_node, int x, int y, bool extend, bool snap)
{
  BKScreen *screen = SCREEN(screen_node);
  ScrEdge *actedge = screen_edge_at(screen_node, x, y);
  if (actedge == NULL) {
    return false;
  }
  screen_area_edge_from_cursor(screen_node, x, y, &screen->md.area1, &screen->md.area2);
  screen->md.dir_axis = screen_geom_edge_is_horizontal(actedge) ? SCREEN_AXIS_H : SCREEN_AXIS_V;
  screen->md.origval = screen->md.dir_axis == SCREEN_AXIS_H ? actedge->v1->y : actedge->v1->x;
  screen->md.x = x;
  screen->md.y = y;
  area_move_select(screen_node, actedge, extend);
  screen->md.snap_type = snap ? SNAP_FRACTION_AND_ADJACENT : SNAP_AREAGRID;
  screen->op = OP_MOVE;
  screen->fade_axis = screen->md.dir_axis;
  bk_anim_start(screen_node); /* AREA_MOVE_LINE_FADEIN */
  return true;
}

/* Changing to/from edge extension reinitializes everything (area_move_reinit). */
static void area_move_reinit(BKNode *screen_node, bool extend, int x, int y)
{
  BKScreen *screen = SCREEN(screen_node);
  for (ScrVert *v = (ScrVert *)screen->vertbase.first; v; v = v->next) {
    v->editflag = 0;
  }
  ScrEdge *actedge = screen_edge_at(screen_node, x, y);
  if (actedge == NULL) {
    screen->md.can_extend = screen->md.extending = false;
    return;
  }
  screen->md.x = x;
  screen->md.y = y;
  screen->md.origval = screen->md.dir_axis == SCREEN_AXIS_H ? actedge->v1->y : actedge->v1->x;
  area_move_select(screen_node, actedge, extend);
}

static int area_snap_calc_location(BKNode *screen_node, const int delta)
{
  BKScreen *screen = SCREEN(screen_node);
  const int origval = screen->md.origval, bigger = screen->md.bigger,
            smaller = screen->md.smaller;
  int m_cursor_final = -1;
  const int m_cursor = origval + delta;
  const int m_span = bigger + smaller;
  const int m_min = origval - smaller;

  switch (screen->md.snap_type) {
    case SNAP_AREAGRID: {
      m_cursor_final = m_cursor;
      if (delta != bigger && delta != -smaller) {
        m_cursor_final -= (m_cursor % AREAGRID);
        m_cursor_final = BK_CLAMP(m_cursor_final, origval - smaller, origval + bigger);
      }
      /* Slight snap to vertical minimum and maximum. */
      const int snap_threshold = (int)((float)screen_headersize() * 0.6f);
      const int snaps[2] = {m_min, origval + bigger};
      for (int i = 0; i < 2; i++) {
        if (abs(m_cursor_final - snaps[i]) < snap_threshold) {
          m_cursor_final = snaps[i];
          break;
        }
      }
      break;
    }
    case SNAP_BIGGER_SMALLER_ONLY:
      m_cursor_final = (m_cursor >= bigger) ? bigger : smaller;
      break;
    case SNAP_FRACTION_AND_ADJACENT: {
      const int axis = (screen->md.dir_axis == SCREEN_AXIS_V) ? 0 : 1;
      int snap_dist_best = INT_MAX;
      rcti rect;
      screen_rect(screen_node, &rect);
      const int screen_size = axis == 0 ? BLI_rcti_size_x(&rect) : BLI_rcti_size_y(&rect);
      const int screen_min = axis == 0 ? rect.xmin : rect.ymin;
      /* Number of snap sections between the end-snap points. */
      const int interior_snap_divisor = 24;
      /* Minimum snap helps to cull any that are too close to the endpoint snaps. */
      const int min_snap = (int)((float)screen_size / (float)interior_snap_divisor / 4.0f);
      for (int i = 0; i <= interior_snap_divisor; i++) {
        const int m_cursor_test = i == 0 ? m_min :
                                  i == interior_snap_divisor ?
                                           (m_min + m_span) :
                                           screen_min + (int)floorf((float)screen_size * i /
                                                                        interior_snap_divisor +
                                                                    0.5f);
        if (m_cursor_test < m_min || (m_cursor_test > m_min && m_cursor_test < (m_min + min_snap))) {
          continue; /* too close to the minimum snap position */
        }
        if (m_cursor_test > (m_min + m_span) ||
            (m_cursor_test < (m_min + m_span) && m_cursor_test > (m_min + m_span - min_snap)))
        {
          continue; /* too close to the maximum snap position */
        }
        const int snap_dist_test = abs(m_cursor - m_cursor_test);
        if (snap_dist_best >= snap_dist_test) {
          snap_dist_best = snap_dist_test;
          m_cursor_final = m_cursor_test;
        }
      }
      for (const ScrVert *v1 = (const ScrVert *)screen->vertbase.first; v1; v1 = v1->next) {
        if (!v1->editflag) {
          continue;
        }
        const int v_loc = axis == 0 ? v1->y : v1->x;
        for (const ScrVert *v2 = (const ScrVert *)screen->vertbase.first; v2; v2 = v2->next) {
          if (v2->editflag || v_loc != (axis == 0 ? v2->y : v2->x)) {
            continue;
          }
          const int v_loc2 = axis == 0 ? v2->x : v2->y;
          /* Do not snap to the vertices at the ends. */
          if ((origval - smaller) < v_loc2 && v_loc2 < (origval + bigger)) {
            const int snap_dist_test = abs(m_cursor - v_loc2);
            if (snap_dist_best >= snap_dist_test) {
              snap_dist_best = snap_dist_test;
              m_cursor_final = v_loc2;
            }
          }
        }
      }
      break;
    }
    default:
      break;
  }
  return m_cursor_final;
}

/* Moves the selected screen edge by `delta` (area_move_apply_do). */
static void area_move_apply(BKNode *screen_node, int delta)
{
  BKScreen *screen = SCREEN(screen_node);
  if (screen->md.snap_type != SNAP_BIGGER_SMALLER_ONLY) {
    delta = BK_CLAMP(delta, -screen->md.smaller, screen->md.bigger);
  }
  const short final_loc = (short)(screen->md.snap_type == SNAP_NONE ?
                                      screen->md.origval + delta :
                                      area_snap_calc_location(screen_node, delta));
  const bool x_axis = screen->md.dir_axis == SCREEN_AXIS_V;
  bool doredraw = false;
  for (ScrVert *v = (ScrVert *)screen->vertbase.first; v; v = v->next) {
    if (v->editflag) {
      short *c = x_axis ? &v->x : &v->y;
      if (*c == final_loc) {
        break; /* nothing will change to the other vertices either */
      }
      *c = final_loc;
      doredraw = true;
    }
  }
  if (doredraw) {
    screen_redraw(screen_node);
  }
}

static void area_move_drag(BKNode *screen_node, BKEvent *e, int x, int y)
{
  BKScreen *screen = SCREEN(screen_node);
  /* Shift toggles extending to every aligned edge (EVT_*SHIFTKEY), Ctrl snapping. */
  if (EV_SHIFT(e) != screen->md.extending && screen->md.can_extend) {
    area_move_reinit(screen_node, EV_SHIFT(e), screen->md.x, screen->md.y);
  }
  if (screen->md.snap_type != SNAP_BIGGER_SMALLER_ONLY) {
    screen->md.snap_type = EV_CTRL(e) ? SNAP_FRACTION_AND_ADJACENT : SNAP_AREAGRID;
  }
  const int delta = (screen->md.dir_axis == SCREEN_AXIS_V) ? x - screen->md.x : y - screen->md.y;
  area_move_apply(screen_node, delta);
}

static void area_move_exit(BKNode *screen_node, bool changed)
{
  BKScreen *screen = SCREEN(screen_node);
  screen->op = OP_NONE;
  bk_anim_start(screen_node); /* AREA_MOVE_LINE_FADEOUT */
  screen_remove_double_scredges(screen);
  if (changed) {
    screen_changed(screen_node);
  }
  screen_redraw(screen_node);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Corner gestures: split, join, dock, swap, new window
 * \{ */

static float area_docking_snap(const float pos, const BKEvent *e)
{
  const bool alt = EV_ALT(e), ctrl = EV_CTRL(e);
  const float accel = (alt || ctrl) ? 2.5f : 2.0f;
  float factor = pos * accel;
  if (!alt) {
    if (factor >= 0.4375f && factor < 0.5f) {
      factor = 0.499999f;
    }
    else if (factor >= 0.5f && factor < 0.5625f) {
      factor = 0.500001f;
    }
  }
  if (ctrl) {
    if (factor < 0.1875f) {
      factor = 0.125f;
    }
    else if (factor < 0.3125f) {
      factor = 0.25f;
    }
    else if (factor < 0.4375f) {
      factor = 0.375f;
    }
    else if (factor >= 0.5625f && factor < 0.6875f) {
      factor = 0.625f;
    }
    else if (factor >= 0.6875f && factor < 0.8125f) {
      factor = 0.75f;
    }
    else if (factor > 0.8125f) {
      factor = 0.875f;
    }
  }
  return factor;
}

static AreaDockTarget area_docking_target(BKScreen *screen, const BKEvent *e, int ex, int ey)
{
  BKNode *sa1n = screen->jd.sa1, *sa2n = screen->jd.sa2;
  if (!sa2n || sa1n == sa2n) {
    return DOCK_NONE;
  }
  const BKArea *sa1 = AREA(sa1n), *sa2 = AREA(sa2n);
  /* Local coordinates in sa2. */
  const int x = ex - sa2->totrct.xmin, y = ey - sa2->totrct.ymin;
  const float fac_x = (float)x / (float)sa2->winx;
  const float fac_y = (float)y / (float)sa2->winy;
  const int min_x = (int)(2 * AREAMINX * UI_SCALE_FAC);
  const int min_y = (int)(2 * HEADERY * UI_SCALE_FAC);

  if (screen->jd.dir == SCREEN_DIR_N || screen->jd.dir == SCREEN_DIR_S) {
    /* Up or Down to immediate neighbor. */
    if (ex <= sa1->totrct.xmax && ex >= sa1->totrct.xmin) {
      const int join_y = (int)fminf(sa2->winy * 0.25f, 5 * HEADERY * UI_SCALE_FAC);
      if (sa2->winy < min_y || (screen->jd.dir == SCREEN_DIR_N && y < join_y) ||
          (screen->jd.dir == SCREEN_DIR_S && (sa2->winy - y) < join_y))
      {
        return DOCK_NONE;
      }
    }
  }
  if (screen->jd.dir == SCREEN_DIR_W || screen->jd.dir == SCREEN_DIR_E) {
    /* Left or Right to immediate neighbor. */
    if (ey <= sa1->totrct.ymax && ey >= sa1->totrct.ymin) {
      const int join_x = (int)fminf(sa2->winx * 0.25f, 5 * AREAMINX * UI_SCALE_FAC);
      if (sa2->winx < min_x || (screen->jd.dir == SCREEN_DIR_W && (sa2->winx - x) < join_x) ||
          (screen->jd.dir == SCREEN_DIR_E && x < join_x))
      {
        return DOCK_NONE;
      }
    }
  }
  /* If we've made it here, then there can be no joining possible. */
  screen->jd.dir = SCREEN_DIR_NONE;
  screen->jd.factor = 0.5f;

  const float min_fac_x = 1.5f * AREAMINX * UI_SCALE_FAC / (float)sa2->winx;
  const float min_fac_y = 1.5f * HEADERY * UI_SCALE_FAC / (float)sa2->winy;

  /* if the area is narrow then there are only two docking targets. */
  if (sa2->winx < (min_x * 3)) {
    if (fac_y > 0.4f && fac_y < 0.6f) {
      return DOCK_CENTER;
    }
    if ((float)y > (float)sa2->winy / 2.0f) {
      screen->jd.factor = area_docking_snap(fmaxf(1.0f - fac_y, min_fac_y), e);
      return DOCK_TOP;
    }
    screen->jd.factor = area_docking_snap(fmaxf(fac_y, min_fac_y), e);
    return DOCK_BOTTOM;
  }
  if (sa2->winy < (min_y * 3)) {
    if (fac_x > 0.4f && fac_x < 0.6f) {
      return DOCK_CENTER;
    }
    if ((float)x > (float)sa2->winx / 2.0f) {
      screen->jd.factor = area_docking_snap(fmaxf(1.0f - fac_x, min_fac_x), e);
      return DOCK_RIGHT;
    }
    screen->jd.factor = area_docking_snap(fmaxf(fac_x, min_fac_x), e);
    return DOCK_LEFT;
  }
  /* Are we in the center? But not in same area! */
  if (fac_x > 0.4f && fac_x < 0.6f && fac_y > 0.4f && fac_y < 0.6f) {
    return DOCK_CENTER;
  }
  /* Area is large enough for four docking targets. */
  const float area_ratio = (float)sa2->winx / (float)sa2->winy;
  /* Split the area diagonally from top-right to bottom-left. */
  const bool upper_left = (float)x / (float)(y + 1) < area_ratio;
  /* Split the area diagonally from top-left to bottom-right. */
  const bool lower_left = (float)x / (float)(sa2->winy - y + 1) < area_ratio;
  if (upper_left && !lower_left) {
    screen->jd.factor = area_docking_snap(fmaxf(1.0f - fac_y, min_fac_y), e);
    return DOCK_TOP;
  }
  if (!upper_left && lower_left) {
    screen->jd.factor = area_docking_snap(fmaxf(fac_y, min_fac_y), e);
    return DOCK_BOTTOM;
  }
  if (upper_left && lower_left) {
    screen->jd.factor = area_docking_snap(fmaxf(fac_x, min_fac_x), e);
    return DOCK_LEFT;
  }
  screen->jd.factor = area_docking_snap(fmaxf(1.0f - fac_x, min_fac_x), e);
  return DOCK_RIGHT;
}

static float area_split_factor(BKNode *screen_node, const BKEvent *e, int ex, int ey)
{
  BKScreen *screen = SCREEN(screen_node);
  const BKArea *sa1 = AREA(screen->jd.sa1);
  const bool v = screen->jd.split_dir == SCREEN_AXIS_V;
  float fac = v ? (float)(ex - sa1->totrct.xmin) / (float)(sa1->winx + 1) :
                  (float)(ey - sa1->totrct.ymin) / (float)(sa1->winy + 1);
  if (EV_CTRL(e)) {
    /* Snapping on: the nearest neighboring vertex, or the nearest 12th. */
    int dist = INT_MAX, loc = 0;
    for (const ScrVert *v1 = (const ScrVert *)screen->vertbase.first; v1; v1 = v1->next) {
      const int v_loc = v ? v1->x : v1->y;
      const int v_dist = abs(v_loc - (v ? ex : ey));
      if (v_dist < dist) {
        loc = v_loc;
        dist = v_dist;
      }
    }
    const float near_fac = v ? (float)(loc - sa1->totrct.xmin) / (float)(sa1->winx + 1) :
                               (float)(loc - sa1->totrct.ymin) / (float)(sa1->winy + 1);
    const float frac_fac = roundf(fac * 12.0f) / 12.0f;
    fac = (fabsf(near_fac - fac) < fabsf(frac_fac - fac)) ? near_fac : frac_fac;
  }
  else if (fac >= 0.48f && fac < 0.5f) {
    fac = 0.499999f; /* slight snap to center when no modifiers are held */
  }
  else if (fac >= 0.5f && fac < 0.52f) {
    fac = 0.500001f;
  }
  /* Don't allow a new area to be created that is very small. */
  const float min_size = 2.0f * screen_headersize();
  const float min_fac = min_size / (v ? (float)(sa1->winx + 1) : (float)(sa1->winy + 1));
  return min_fac < 0.5f ? BK_CLAMP(fac, min_fac, 1.0f - min_fac) : 0.5f;
}

static void area_join_update_data(BKNode *screen_node, const BKEvent *e)
{
  BKScreen *screen = SCREEN(screen_node);
  int x, y;
  event_space(screen_node, e, &x, &y);
  screen->jd.current_x = e->x;
  screen->jd.current_y = e->y;
  rcti rect;
  screen_rect(screen_node, &rect);
  screen->jd.outside = !BLI_rcti_isect_pt(&rect, x, y);
  BKNode *area = screen->jd.outside ? NULL : screen_find_area_xy(screen_node, x, y);

  screen->jd.dir = SCREEN_DIR_NONE;
  screen->jd.dock_target = DOCK_NONE;
  if (screen->jd.sa1 == area) {
    const int drag_threshold = (int)(20 * UI_SCALE_FAC);
    screen->jd.sa2 = area;
    if (!(abs(screen->jd.start_x - x) > drag_threshold ||
          abs(screen->jd.start_y - y) > drag_threshold))
    {
      /* We haven't moved enough to start a split. */
      screen->jd.split_fac = 0.0f;
      return;
    }
    screen->jd.split_dir = (abs(x - screen->jd.start_x) > abs(y - screen->jd.start_y)) ?
                               SCREEN_AXIS_V :
                               SCREEN_AXIS_H;
    screen->jd.split_fac = area_split_factor(screen_node, e, x, y);
    return;
  }
  screen->jd.sa2 = area;
  screen->jd.dir = area_getorientation(AREA(screen->jd.sa1), area ? AREA(area) : NULL);
  screen->jd.dock_target = area_docking_target(screen, e, x, y);
}

static int area_join_cursor(const BKScreen *screen)
{
  if (!screen->jd.sa2 && screen->jd.dock_target == DOCK_NONE) {
    return screen->jd.outside ? BK_CURSOR_CROSSHAIR : BK_CURSOR_NOT_ALLOWED;
  }
  if (screen->jd.sa1 && screen->jd.sa1 == screen->jd.sa2) {
    if (screen->jd.split_fac >= 0.0001f) {
      return screen->jd.split_dir == SCREEN_AXIS_V ? BK_CURSOR_EW : BK_CURSOR_NS;
    }
    return BK_CURSOR_CROSSHAIR;
  }
  if (screen->jd.dir != SCREEN_DIR_NONE) {
    return SCREEN_DIR_IS_VERTICAL(screen->jd.dir) ? BK_CURSOR_NS : BK_CURSOR_EW;
  }
  return screen->jd.dock_target != DOCK_NONE ? BK_CURSOR_MOVE : BK_CURSOR_CROSSHAIR;
}

/* The header's left corner, where a click opens the area menu (is_header_azone_location). */
static bool is_header_azone_location(BKNode *area_node, int x, int y)
{
  const BKArea *area = AREA(area_node);
  if (x > area->totrct.xmin + UI_HEADER_OFFSET) {
    return false;
  }
  BKNode *header = area_region_find(area_node, RGN_TYPE_HEADER);
  if (!header || (header->rgn->flag & RGN_FLAG_HIDDEN)) {
    return false;
  }
  const int height = screen_headersize();
  return (header->rgn->alignment == RGN_ALIGN_TOP && y > area->totrct.ymax - height) ||
         (header->rgn->alignment == RGN_ALIGN_BOTTOM && y < area->totrct.ymin + height);
}

static void area_window_request(BKNode *screen_node, BKNode *area_node)
{
  for (int i = 0; i < screen_node->nkids; i++) {
    if (screen_node->kids[i] == area_node) {
      bk_emit(area_node, BK_EV_AREA, NULL, i, BK_AREA_WINDOW);
    }
  }
}

static void area_touch_menu(BKNode *screen_node, BKNode *area_node, int wx, int wy);

/* LEFTMOUSE release in area_join_modal. */
static void area_join_apply(BKNode *screen_node, int x, int y)
{
  BKScreen *screen = SCREEN(screen_node);
  BKNode *sa1 = screen->jd.sa1, *sa2 = screen->jd.sa2;
  if (screen->jd.outside) {
    /* Break out into a new window: the host opens it, the area leaves this screen. */
    area_window_request(screen_node, sa1);
    screen_area_close(screen_node, sa1, NULL);
  }
  else if (sa1 && sa1 == sa2) {
    /* Same area so split. */
    if (screen->jd.split_fac > 0.0001f) {
      BKNode *newa = area_split(screen_node, sa1, screen->jd.split_dir, screen->jd.split_fac);
      if (newa) {
        const float f = screen->jd.split_fac;
        const bool large_v = screen->jd.split_dir == SCREEN_AXIS_V &&
                             ((screen->jd.start_x < x && f > 0.5f) ||
                              (screen->jd.start_x > x && f < 0.5f));
        const bool large_h = screen->jd.split_dir == SCREEN_AXIS_H &&
                             ((screen->jd.start_y < y && f > 0.5f) ||
                              (screen->jd.start_y > y && f < 0.5f));
        if (large_v || large_h) {
          /* The new area goes where the drag came from (from above it is above ...). */
          area_swapspace(screen_node, sa1, newa);
        }
      }
    }
  }
  else if (sa1 && sa2 && screen->jd.dock_target != DOCK_NONE) {
    area_docking_apply(screen_node, sa1, sa2, screen->jd.dock_target, screen->jd.factor);
  }
  else if (sa1 && sa2 && screen->jd.dir != SCREEN_DIR_NONE) {
    screen_area_join(screen_node, sa1, sa2);
  }
  else {
    return;
  }
  screen_remove_double_scredges(screen);
  screen_remove_unused_scredges(screen_node);
  screen_remove_unused_scrverts(screen);
  screen_changed(screen_node);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name The screen's pointer handling
 * \{ */

static int corner_modifier(const BKEvent *e)
{
  /* the actionzone keymap: Shift-drag duplicates into a new window, Ctrl-drag swaps */
  return EV_SHIFT(e) ? 1 : EV_CTRL(e) ? 2 : 0;
}

int screen_op_press(BKNode *screen_node, BKEvent *e)
{
  BKScreen *screen = SCREEN(screen_node);
  int x, y;
  event_space(screen_node, e, &x, &y);
  screen->op = OP_NONE;
  if (!screen->resize_only && screen->state == SCREENNORMAL) {
    SCREEN_AREAS_ITER (screen_node, area) {
      const AZone *az = area_actionzone_find_xy(area, x, y);
      if (az && az->type == AZONE_AREA) {
        memset(&screen->jd, 0, sizeof(screen->jd));
        screen->jd.sa1 = area->node;
        screen->jd.start_x = x;
        screen->jd.start_y = y;
        screen->jd.current_x = e->x;
        screen->jd.current_y = e->y;
        screen->jd.dock_target = DOCK_NONE;
        screen->jd.modifier = corner_modifier(e);
        screen->op = screen->jd.modifier == 1 ? OP_WINDOW :
                     screen->jd.modifier == 2 ? OP_SWAP :
                                                OP_JOIN;
        return 1;
      }
    }
  }
  if (area_move_init(screen_node, x, y, EV_SHIFT(e), EV_CTRL(e))) {
    return 1;
  }
  return 0;
}

int screen_op_drag(BKNode *screen_node, BKEvent *e)
{
  BKScreen *screen = SCREEN(screen_node);
  int x, y;
  event_space(screen_node, e, &x, &y);
  switch (screen->op) {
    case OP_MOVE:
      area_move_drag(screen_node, e, x, y);
      return 1;
    case OP_JOIN:
      area_join_update_data(screen_node, e);
      screen_redraw(screen_node);
      return 1;
    case OP_SWAP:
      screen->jd.sa2 = screen_find_area_xy(screen_node, x, y);
      screen->jd.current_x = e->x;
      screen->jd.current_y = e->y;
      return 1;
    default:
      return 0;
  }
}

int screen_op_release(BKNode *screen_node, BKEvent *e)
{
  BKScreen *screen = SCREEN(screen_node);
  int x, y;
  event_space(screen_node, e, &x, &y);
  const int op = screen->op;
  screen->op = OP_NONE;
  switch (op) {
    case OP_MOVE:
      screen->op = OP_MOVE;
      area_move_exit(screen_node, true);
      break;
    case OP_JOIN:
      if (!e->dragged) {
        if (is_header_azone_location(screen->jd.sa1, x, y)) {
          area_touch_menu(screen_node, screen->jd.sa1, e->x, e->y);
        }
        break;
      }
      area_join_update_data(screen_node, e);
      area_join_apply(screen_node, x, y);
      break;
    case OP_SWAP:
      if (screen->jd.sa2 && screen->jd.sa1 != screen->jd.sa2) {
        area_swapspace(screen_node, screen->jd.sa1, screen->jd.sa2);
        screen_changed(screen_node);
      }
      break;
    case OP_WINDOW:
      /* the gesture: dragged off the corner far enough (area_threshold) */
      if (e->dragged) {
        area_window_request(screen_node, screen->jd.sa1);
      }
      break;
    default:
      break;
  }
  screen_redraw(screen_node);
  return 1;
}

void screen_op_cancel(BKNode *screen_node)
{
  BKScreen *screen = SCREEN(screen_node);
  if (screen->op == OP_MOVE) {
    area_move_apply(screen_node, 0); /* area_move_cancel: back where it started */
    area_move_exit(screen_node, false);
  }
  screen->op = OP_NONE;
  screen_redraw(screen_node);
}

int screen_cursor_at(BKNode *screen_node, int wxp, int wyp)
{
  BKScreen *screen = SCREEN(screen_node);
  switch (screen->op) {
    case OP_MOVE:
      return screen->md.dir_axis == SCREEN_AXIS_V ? BK_CURSOR_EW : BK_CURSOR_NS;
    case OP_JOIN:
      return area_join_cursor(screen);
    case OP_SWAP:
      return screen->jd.sa2 ? BK_CURSOR_MOVE : BK_CURSOR_NOT_ALLOWED;
    case OP_WINDOW:
      return BK_CURSOR_CROSSHAIR;
    default:
      break;
  }
  int x, y;
  space_from_window(screen_node, wxp, wyp, &x, &y);
  SCREEN_AREAS_ITER (screen_node, area) {
    const AZone *az = area_actionzone_find_xy(area, x, y);
    if (az && az->type == AZONE_AREA) {
      return BK_CURSOR_CROSSHAIR; /* WM_CURSOR_EDIT */
    }
  }
  ScrEdge *actedge = screen_edge_at(screen_node, x, y);
  if (actedge) {
    return screen_geom_edge_is_horizontal(actedge) ? BK_CURSOR_NS : BK_CURSOR_EW;
  }
  return BK_CURSOR_DEFAULT;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Menu-started operators (modal: bk_begin_modal)
 * \{ */

/* The modal tool: it forwards to the screen until a click confirms (Escape or right-click
 * cancel, and free it). */
static int modal_move(BKNode *tool, BKEvent *e)
{
  BKNode *screen_node = (BKNode *)tool->ext;
  BKScreen *screen = SCREEN(screen_node);
  int x, y;
  event_space(screen_node, e, &x, &y);
  if (screen->op == OP_SPLIT) {
    /* area_split modal: the split line follows the pointer in the area under it */
    BKNode *area = screen_find_area_xy(screen_node, x, y);
    screen->jd.sa1 = screen->jd.sa2 = area;
    if (area) {
      screen->jd.split_fac = area_split_factor(screen_node, e, x, y);
    }
  }
  else if (screen->op == OP_JOIN) {
    area_join_update_data(screen_node, e);
  }
  screen_redraw(screen_node);
  return 1;
}

static int modal_press(BKNode *tool, BKEvent *e)
{
  BKNode *screen_node = (BKNode *)tool->ext;
  BKScreen *screen = SCREEN(screen_node);
  int x, y;
  event_space(screen_node, e, &x, &y);
  const int op = screen->op;
  screen->op = OP_NONE;
  if (op == OP_SPLIT && screen->jd.sa1 && screen->jd.split_fac > 0.0001f) {
    if (area_split(screen_node, screen->jd.sa1, screen->jd.split_dir, screen->jd.split_fac)) {
      screen_changed(screen_node);
    }
  }
  else if (op == OP_JOIN) {
    area_join_update_data(screen_node, e);
    area_join_apply(screen_node, x, y);
  }
  bk_end_modal(bk_ctx(screen_node));
  return 1;
}

static void modal_free(BKNode *tool)
{
  BKNode *screen_node = (BKNode *)tool->ext;
  tool->ext = NULL;
  if (screen_node) {
    SCREEN(screen_node)->op = OP_NONE;
    SCREEN(screen_node)->jd.modal = false;
    screen_redraw(screen_node);
  }
}

static const BKClass cls_screen_modal = {.name = "screen_modal",
                                         .cursor = BK_CURSOR_CROSSHAIR,
                                         .move = modal_move,
                                         .press = modal_press,
                                         .free_ext = modal_free};

static void screen_modal_begin(BKNode *screen_node, int op)
{
  BKContext *ctx = bk_ctx(screen_node);
  if (!ctx) {
    return;
  }
  BKNode *tool = bk_node_new_cls(&cls_screen_modal);
  tool->ext = screen_node;
  SCREEN(screen_node)->op = op;
  SCREEN(screen_node)->jd.modal = true;
  bk_begin_modal(ctx, tool);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Context menus
 * \{ */

static void menu_split(BKNode *screen_node, eScreenAxis dir)
{
  BKScreen *screen = SCREEN(screen_node);
  memset(&screen->jd, 0, sizeof(screen->jd));
  screen->jd.split_dir = dir;
  screen->jd.dock_target = DOCK_NONE;
  screen_modal_begin(screen_node, OP_SPLIT);
}
static void menu_split_v(void *data)
{
  menu_split((BKNode *)data, SCREEN_AXIS_V);
}
static void menu_split_h(void *data)
{
  menu_split((BKNode *)data, SCREEN_AXIS_H);
}

static void menu_join_into(BKNode *screen_node, BKNode *keep, BKNode *gone)
{
  if (screen_area_join(screen_node, keep, gone)) {
    screen_remove_double_scredges(SCREEN(screen_node));
    screen_remove_unused_scredges(screen_node);
    screen_remove_unused_scrverts(SCREEN(screen_node));
    screen_changed(screen_node);
  }
}
static void menu_join_a(void *data)
{
  BKScreen *screen = SCREEN((BKNode *)data);
  menu_join_into((BKNode *)data, screen->menu_sa2, screen->menu_sa1);
}
static void menu_join_b(void *data)
{
  BKScreen *screen = SCREEN((BKNode *)data);
  menu_join_into((BKNode *)data, screen->menu_sa1, screen->menu_sa2);
}
static void menu_swap(void *data)
{
  BKNode *screen_node = (BKNode *)data;
  area_swapspace(screen_node, SCREEN(screen_node)->menu_sa1, SCREEN(screen_node)->menu_sa2);
  screen_changed(screen_node);
}
static void menu_merge_edge(void *data)
{
  BKNode *screen_node = (BKNode *)data;
  BKScreen *screen = SCREEN(screen_node);
  ScrEdge *edge = screen_edge_at(screen_node, screen->menu_x, screen->menu_y);
  if (edge) {
    screen_geom_edge_aligned_merge(screen_node, edge);
    screen_remove_double_scredges(screen);
    screen_changed(screen_node);
  }
}

/* screen_area_options_invoke: the menu over an edge. */
int screen_context_items(BKNode *screen_node, BKMenuSpec *items, int max)
{
  BKScreen *screen = SCREEN(screen_node);
  BKContext *ctx = bk_ctx(screen_node);
  if (!ctx || screen->resize_only || screen->state != SCREENNORMAL || max < 8) {
    return 0;
  }
  int x, y;
  space_from_window(screen_node, ctx->pointer_x, ctx->pointer_y, &x, &y);
  BKNode *sa1, *sa2;
  ScrEdge *edge = screen_area_edge_from_cursor(screen_node, x, y, &sa1, &sa2);
  if (!edge) {
    return 0;
  }
  screen->menu_x = x;
  screen->menu_y = y;
  screen->menu_sa1 = sa1;
  screen->menu_sa2 = sa2;
  int n = 0;
#define ITEM(label, icon_name, f) \
  (items[n] = (BKMenuSpec){label, bk_icons_lookup(icon_name), NULL, -1, 1, NULL, NULL, 0, f, \
                           screen_node}, \
   n++)
#define SEP() (items[n] = (BKMenuSpec){"-"}, n++)
  ITEM("Vertical Split", "SPLIT_VERTICAL", menu_split_v);
  ITEM("Horizontal Split", "SPLIT_HORIZONTAL", menu_split_h);
  if (sa1 && sa2) {
    SEP();
    /* Join needs two very similar areas. */
    const eScreenDir dir = area_getorientation(AREA(sa1), AREA(sa2));
    if (dir != SCREEN_DIR_NONE) {
      const bool ns = dir == SCREEN_DIR_N || dir == SCREEN_DIR_S;
      ITEM(ns ? "Join Up" : "Join Right", ns ? "AREA_JOIN_UP" : "AREA_JOIN", menu_join_a);
      ITEM(ns ? "Join Down" : "Join Left", ns ? "AREA_JOIN_DOWN" : "AREA_JOIN_LEFT", menu_join_b);
      SEP();
    }
    /* Swap just needs two areas. */
    ITEM("Swap Areas", "AREA_SWAP", menu_swap);
  }
  if (screen_geom_edge_can_extend(screen, edge)) {
    SEP();
    ITEM("Merge Edge", "", menu_merge_edge);
  }
#undef ITEM
#undef SEP
  return n;
}

/* The area menus: the area and the region they were opened for. */
static BKNode *menu_area(void *data)
{
  return (BKNode *)data;
}

static void menu_area_split(BKNode *area_node, eScreenAxis dir)
{
  BKNode *screen_node = AREA_SCREEN(area_node);
  if (screen_node && area_split(screen_node, area_node, dir, 0.49999f)) {
    screen_changed(screen_node);
  }
}
static void menu_area_split_h(void *data)
{
  menu_area_split(menu_area(data), SCREEN_AXIS_H);
}
static void menu_area_split_v(void *data)
{
  menu_area_split(menu_area(data), SCREEN_AXIS_V);
}
static void menu_area_move(void *data)
{
  /* area_join_invoke from a menu: the area follows the pointer until a click */
  BKNode *area_node = menu_area(data);
  BKNode *screen_node = AREA_SCREEN(area_node);
  BKScreen *screen = SCREEN(screen_node);
  memset(&screen->jd, 0, sizeof(screen->jd));
  screen->jd.sa1 = screen->jd.sa2 = area_node;
  screen->jd.start_x = AREA(area_node)->totrct.xmin;
  screen->jd.start_y = AREA(area_node)->totrct.ymax;
  screen->jd.dock_target = DOCK_NONE;
  screen_modal_begin(screen_node, OP_JOIN);
}
static void menu_area_maximize(void *data)
{
  BKNode *area_node = menu_area(data);
  screen_state_toggle(AREA_SCREEN(area_node), area_node, SCREENMAXIMIZED);
}
static void menu_area_focus(void *data)
{
  BKNode *area_node = menu_area(data);
  screen_state_toggle(AREA_SCREEN(area_node), area_node, SCREENFULL);
}
static void menu_area_window(void *data)
{
  BKNode *area_node = menu_area(data);
  area_window_request(AREA_SCREEN(area_node), area_node);
}
static void menu_area_close(void *data)
{
  BKNode *area_node = menu_area(data);
  BKNode *screen_node = AREA_SCREEN(area_node);
  if (screen_area_close(screen_node, area_node, NULL)) {
    screen_remove_unused_scrverts(SCREEN(screen_node));
    screen_changed(screen_node);
  }
}
static void menu_region_flip(void *data)
{
  BKNode *area_node = menu_area(data);
  if (AREA(area_node)->menu_region) {
    region_flip(area_node, AREA(area_node)->menu_region);
  }
}
static void menu_region_hide(void *data)
{
  BKNode *area_node = menu_area(data);
  if (AREA(area_node)->menu_region) {
    region_toggle_hidden(area_node, AREA(area_node)->menu_region);
  }
}

static bool area_can_close(BKNode *area_node)
{
  BKNode *screen_node = AREA_SCREEN(area_node);
  SCREEN_AREAS_ITER (screen_node, other) {
    if (area_getorientation(other, AREA(area_node)) != SCREEN_DIR_NONE) {
      return true;
    }
  }
  return false;
}

/* screen_area_menu_items: what every area menu ends with. */
static int area_menu_items(BKNode *area_node, BKMenuSpec *items, int n, bool touch)
{
  BKNode *screen_node = AREA_SCREEN(area_node);
  if (!screen_node || SCREEN(screen_node)->resize_only) {
    return n;
  }
  const BKArea *area = AREA(area_node);
#define ITEM(label, icon_name, f) \
  (items[n] = (BKMenuSpec){label, bk_icons_lookup(icon_name), NULL, -1, 1, NULL, NULL, 0, f, \
                           area_node}, \
   n++)
#define SEP() (items[n] = (BKMenuSpec){"-"}, n++)
  if (touch) {
    ITEM("Horizontal Split", "SPLIT_HORIZONTAL", menu_area_split_h);
    ITEM("Vertical Split", "SPLIT_VERTICAL", menu_area_split_v);
    SEP();
  }
  if (!area->full) {
    ITEM(touch ? "Move/Join/Dock Area" : "Move/Split Area", "AREA_DOCK", menu_area_move);
    SEP();
  }
  ITEM(area->full ? "Restore Areas" : "Maximize Area", "", menu_area_maximize);
  if (!area->full) {
    ITEM("Focus Mode", "", menu_area_focus);
  }
  ITEM("Duplicate Area into New Window", "", menu_area_window);
  if (area_can_close(area_node) && !area->full) {
    SEP();
    ITEM("Close Area", "X", menu_area_close);
  }
#undef ITEM
#undef SEP
  return n;
}

static void area_touch_menu(BKNode *screen_node, BKNode *area_node, int wx, int wy)
{
  BKMenuSpec items[16];
  const int n = area_menu_items(area_node, items, 0, true);
  BKContext *ctx = bk_ctx(screen_node);
  if (ctx && n > 0) {
    BK_open_menu(ctx, area_node, items, n, "Area Options", wx, wy);
  }
}

/* ED_screens_header_tools_menu_create (header, tool header, footer) and the navigation bar's
 * menu: shown when right-clicking a region's background. */
int area_context_items(BKNode *area_node, BKMenuSpec *items, int max)
{
  BKContext *ctx = bk_ctx(area_node);
  if (!ctx || max < 16) {
    return 0;
  }
  BKArea *area = AREA(area_node);
  area->menu_region = NULL;
  for (int i = 0; i < area_node->nkids; i++) {
    BKNode *c = area_node->kids[i];
    const BKRegion *r = region_of(c);
    if (r && r->visible && bk_contains(c, ctx->pointer_x, ctx->pointer_y)) {
      area->menu_region = c;
    }
  }
  const BKRegion *region = area->menu_region ? area->menu_region->rgn : NULL;
  if (!region || !(region->type == RGN_TYPE_HEADER || region->type == RGN_TYPE_TOOL_HEADER ||
                   region->type == RGN_TYPE_FOOTER || region->type == RGN_TYPE_NAV_BAR))
  {
    return 0;
  }
  int n = 0;
#define ITEM(label, f) \
  (items[n] = (BKMenuSpec){label, 0, NULL, -1, 1, NULL, NULL, 0, f, area_node}, n++)
  ITEM(region->type == RGN_TYPE_FOOTER   ? "Hide Footer" :
       region->type == RGN_TYPE_NAV_BAR  ? "Hide" :
       region->type == RGN_TYPE_HEADER   ? "Hide Header" :
                                           "Hide Tool Settings",
       menu_region_hide);
  ITEM(region->alignment == RGN_ALIGN_LEFT   ? "Flip to Right" :
       region->alignment == RGN_ALIGN_RIGHT  ? "Flip to Left" :
       region->alignment == RGN_ALIGN_BOTTOM ? "Flip to Top" :
                                               "Flip to Bottom",
       menu_region_flip);
#undef ITEM
  if (region->type != RGN_TYPE_NAV_BAR && AREA_SCREEN(area_node)) {
    items[n++] = (BKMenuSpec){"-"};
    n = area_menu_items(area_node, items, n, false);
  }
  return n;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Region scale (SCREEN_OT_region_scale)
 * \{ */

static bool edge_is_x(AZEdge edge)
{
  return edge == AE_LEFT_TO_TOPRIGHT || edge == AE_RIGHT_TO_TOPLEFT;
}

static void region_scale_validate_size(BKArea *area)
{
  BKRegion *region = area->rmd.region->rgn;
  if (!(region->flag & RGN_FLAG_HIDDEN)) {
    short *size = edge_is_x(area->rmd.edge) ? &region->sizex : &region->sizey;
    const short maxsize = (short)(area->rmd.maxsize - (short)(UI_UNIT_Y / UI_SCALE_FAC));
    if (*size > maxsize && maxsize > 0) {
      *size = maxsize;
    }
  }
}

static void region_scale_toggle_hidden(BKArea *area)
{
  area->rmd.region->rgn->flag ^= RGN_FLAG_HIDDEN;
  region_scale_validate_size(area);
}

int area_op_press(BKNode *area_node, BKEvent *e)
{
  BKArea *area = AREA(area_node);
  const BKNode *space = area_space(area_node);
  int x, y;
  event_space(space, e, &x, &y);
  const AZone *az = area_actionzone_find_xy(area, x, y);
  area->rmd.active = false;
  if (!az) {
    return 0;
  }
  if (az->type == AZONE_FULLSCREEN) {
    screen_state_toggle(AREA_SCREEN(area_node), area_node, SCREENFULL);
    return 1;
  }
  if (az->type != AZONE_REGION) {
    return 0;
  }
  BKRegion *region = az->region->rgn;
  area->rmd.region = az->region;
  area->rmd.edge = (AZEdge)az->edge;
  area->rmd.orig_xy[0] = x;
  area->rmd.orig_xy[1] = y;
  area->rmd.maxsize = area_max_regionsize(area, az->region, area->rmd.edge);
  /* if not set we do now, otherwise it uses type */
  if (region->sizex == 0) {
    region->sizex = (short)(region->winx / UI_SCALE_FAC);
  }
  if (region->sizey == 0) {
    region->sizey = (short)(region->winy / UI_SCALE_FAC);
  }
  /* Reset our saved widths if the region is hidden: otherwise you can't drag it out again. */
  if (region->flag & RGN_FLAG_HIDDEN) {
    if (edge_is_x(area->rmd.edge)) {
      region->winx = region->sizex = 0;
    }
    else {
      region->winy = region->sizey = 0;
    }
  }
  area->rmd.origval = edge_is_x(area->rmd.edge) ? region->sizex : region->sizey;
  area->rmd.maxsize = BK_CLAMP(area->rmd.maxsize, 0, 1000);
  area->rmd.active = true;
  bk_invalidate(area_node);
  return 1;
}

int area_op_drag(BKNode *area_node, BKEvent *e)
{
  BKArea *area = AREA(area_node);
  if (!area->rmd.active) {
    return 0;
  }
  BKRegion *region = area->rmd.region->rgn;
  const RegionTypeInfo *info = region_type_info(region->type);
  int x, y;
  event_space(area_space(area_node), e, &x, &y);
  const int snap_size_threshold = bk_unit() * 2;
  const bool x_axis = edge_is_x(area->rmd.edge);
  int delta = x_axis ? x - area->rmd.orig_xy[0] : y - area->rmd.orig_xy[1];
  if (area->rmd.edge == AE_LEFT_TO_TOPRIGHT || area->rmd.edge == AE_BOTTOM_TO_TOPLEFT) {
    delta = -delta;
  }
  delta = (int)(delta / UI_SCALE_FAC); /* region sizes now get multiplied */
  short *size = x_axis ? &region->sizex : &region->sizey;
  const int size_no_snap = area->rmd.origval + delta;
  /* Clamp before snapping, so the snapping doesn't use a size that's invalid anyway. */
  *size = (short)BK_CLAMP(size_no_snap, 0, area->rmd.maxsize);
  if (info->snap_size) {
    const short test = (short)info->snap_size(region, *size, x_axis ? 0 : 1);
    if (abs(*size - test) < snap_size_threshold && test <= area->rmd.maxsize) {
      *size = test;
    }
  }
  /* `UI_UNIT_Y / 4` means you need to drag the footer and execute region almost all the way
   * down for it to become hidden, otherwise its too easy to do this by accident. */
  const int hide_below = x_axis ? UI_UNIT_X : UI_UNIT_Y / 4;
  if (size_no_snap < hide_below) {
    *size = (short)area->rmd.origval;
    if (!(region->flag & RGN_FLAG_HIDDEN)) {
      region_scale_toggle_hidden(area);
    }
  }
  else if (region->flag & RGN_FLAG_HIDDEN) {
    region_scale_toggle_hidden(area);
  }
  /* Hiding/unhiding is handled above, but still fix the size as requested. */
  if ((region->flag & RGN_FLAG_NO_USER_RESIZE) || info->fixed_height) {
    *size = (short)area->rmd.origval;
  }
  bk_relayout(area_node);
  bk_invalidate(area_node);
  return 1;
}

int area_op_release(BKNode *area_node, BKEvent *e)
{
  BKArea *area = AREA(area_node);
  if (!area->rmd.active) {
    return 0;
  }
  area->rmd.active = false;
  BKRegion *region = area->rmd.region->rgn;
  if (!e->dragged) {
    if (region->flag & RGN_FLAG_HIDDEN) {
      region_scale_toggle_hidden(area); /* a click on the tab shows it */
    }
    else if (region->flag & RGN_FLAG_TOO_SMALL) {
      region_scale_validate_size(area);
    }
  }
  BKNode *region_node = area->rmd.region;
  const int size = edge_is_x(area->rmd.edge) ? region->sizex : region->sizey;
  bk_relayout(area_node);
  bk_invalidate(area_node);
  bk_emit(region_node, BK_EV_RESIZE, NULL, size, region_is_hidden(region));
  if (AREA_SCREEN(area_node)) {
    bk_emit(AREA_SCREEN(area_node), BK_EV_CHANGE, NULL, 0, 0);
  }
  return 1;
}

void area_op_cancel(BKNode *area_node)
{
  BKArea *area = AREA(area_node);
  if (area->rmd.active) {
    /* region_scale_modal on Escape: the operator ends where the region is */
    area->rmd.active = false;
    bk_invalidate(area_node);
  }
}

int area_cursor_at(BKNode *area_node, int wxp, int wyp)
{
  BKArea *area = AREA(area_node);
  if (area->rmd.active) {
    return edge_is_x(area->rmd.edge) ? BK_CURSOR_EW : BK_CURSOR_NS;
  }
  int x, y;
  space_from_window(area_space(area_node), wxp, wyp, &x, &y);
  const AZone *az = area_actionzone_find_xy(area, x, y);
  if (az && az->type == AZONE_REGION) {
    if (az->region->rgn->flag & (RGN_FLAG_HIDDEN | RGN_FLAG_TOO_SMALL)) {
      return BK_CURSOR_DEFAULT; /* a tab: click to show */
    }
    return edge_is_x((AZEdge)az->edge) ? BK_CURSOR_EW : BK_CURSOR_NS;
  }
  return BK_CURSOR_DEFAULT;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Keys: maximize, focus, region toggles
 * \{ */

int area_key(BKNode *area_node, BKEvent *e)
{
  BKNode *screen_node = AREA_SCREEN(area_node);
  if (screen_node && !SCREEN(screen_node)->resize_only && bk_keysym_is(e->keysym, "space") &&
      EV_CTRL(e) && !EV_SHIFT(e))
  {
    /* SCREEN_OT_screen_full_area: Ctrl+Space maximizes, Ctrl+Alt+Space is focus mode */
    const BKArea *area = AREA(area_node);
    screen_state_toggle(screen_node, area_node,
                        area->full ? area->full : (EV_ALT(e) ? SCREENFULL : SCREENMAXIMIZED));
    return 1;
  }
  for (int i = 0; i < area_node->nkids; i++) {
    BKNode *c = area_node->kids[i];
    const BKRegion *r = region_of(c);
    if (r && r->key[0] && bk_combo_matches(r->key, e->mods, e->keysym)) {
      region_toggle_hidden(area_node, c); /* SCREEN_OT_region_toggle */
      return 1;
    }
  }
  return 0;
}

/** \} */
