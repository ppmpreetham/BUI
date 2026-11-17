/* Drawing screens: area borders, edge and region highlights, the split, join
 * and dock previews, the drag hint, and the action zones' tabs.
 *
 * Ported from editors/screen/screen_draw.cc (ED_screen_draw_edges and the
 * GPU_SHADER_2D_AREA_BORDERS it draws with, screen_draw_move_highlight,
 * screen_draw_region_scale_highlight, screen_draw_join_highlight,
 * screen_draw_dock_preview, screen_draw_split_preview,
 * screen_draw_area_drag_tip, rounded_corners) and area.cc
 * (region_draw_azone_tab_arrow, draw_azone_arrow,
 * area_draw_azone_fullscreen). Blender draws in y-up window pixels; these
 * convert at each primitive.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <math.h>
#include <string.h>

#include "screen_intern.h"

/* -------------------------------------------------------------------- */
/** \name Primitives in screen space
 * \{ */

static void rctf_from_rcti(rctf *dst, const rcti *src)
{
  /* An inclusive pixel rect covers [xmin, xmax + 1) x [ymin, ymax + 1). */
  dst->xmin = (float)src->xmin;
  dst->xmax = (float)src->xmax + 1.0f;
  dst->ymin = (float)src->ymin;
  dst->ymax = (float)src->ymax + 1.0f;
}

static void rctf_pad(rctf *r, float x, float y)
{
  r->xmin -= x;
  r->xmax += x;
  r->ymin -= y;
  r->ymax += y;
}

static float wx(const BKNode *space, float x)
{
  return (float)space->x + x;
}
static float wy(const BKNode *space, float y)
{
  return (float)(space->y + space->h) - y;
}

/* ui::draw_roundbox_4fv_ex: `inner` and `outline` may be NULL. */
static void roundbox(const BKNode *space, const rctf *r, const float inner[4],
                     const float outline[4], float outline_width, float rad, int corners)
{
  const float w = r->xmax - r->xmin, h = r->ymax - r->ymin;
  if (w <= 0.0f || h <= 0.0f) {
    return;
  }
  BuiWidgetBase wb;
  memset(&wb, 0, sizeof(wb));
  wb.rect[0] = wx(space, r->xmin);
  wb.rect[1] = wy(space, r->ymax);
  wb.rect[2] = w;
  wb.rect[3] = h;
  wb.rad = BK_MIN(rad, 0.5f * BK_MIN(w, h));
  wb.radi = BK_MAX(0.0f, wb.rad - outline_width);
  wb.corners = corners;
  if (inner) {
    memcpy(wb.inner1, inner, sizeof(wb.inner1));
    memcpy(wb.inner2, inner, sizeof(wb.inner2));
  }
  if (outline) {
    memcpy(wb.outline, outline, sizeof(wb.outline));
  }
  wb.pixelsize = outline_width;
  BUI_raster_widget_base(bk_draw.r, &wb);
}

static void fill(const BKNode *space, float xmin, float xmax, float ymin, float ymax,
                 const float c[4])
{
  if (xmax > xmin && ymax > ymin) {
    bk_fill_f(wx(space, xmin), wy(space, ymax), xmax - xmin, ymax - ymin, c);
  }
}

/* Fills the outside of rounded corners of `rect` (screen_draw.cc rounded_corners). */
static void rounded_corners(const BKNode *space, const rctf *rect, const float color[4],
                            int corners)
{
  const float rad = EDITORRADIUS;
  float vec[4][2] = {{0.195f, 0.02f}, {0.55f, 0.169f}, {0.831f, 0.45f}, {0.98f, 0.805f}};
  for (int a = 0; a < 4; a++) {
    vec[a][0] *= rad;
    vec[a][1] *= rad;
  }
  float pts[7][2];
#define PT(i, X, Y) (pts[i][0] = wx(space, X), pts[i][1] = wy(space, Y))
  if (corners & CNR_TL) {
    PT(0, rect->xmin - 1, rect->ymax);
    PT(1, rect->xmin, rect->ymax - rad);
    for (int a = 0; a < 4; a++) {
      PT(2 + a, rect->xmin + vec[a][1], rect->ymax - rad + vec[a][0]);
    }
    PT(6, rect->xmin + rad, rect->ymax);
    bk_polygon((const float(*)[2])pts, 7, color);
  }
  if (corners & CNR_TR) {
    PT(0, rect->xmax + 1, rect->ymax);
    PT(1, rect->xmax - rad, rect->ymax);
    for (int a = 0; a < 4; a++) {
      PT(2 + a, rect->xmax - rad + vec[a][0], rect->ymax - vec[a][1]);
    }
    PT(6, rect->xmax, rect->ymax - rad);
    bk_polygon((const float(*)[2])pts, 7, color);
  }
  if (corners & CNR_BR) {
    PT(0, rect->xmax + 1, rect->ymin);
    PT(1, rect->xmax, rect->ymin + rad);
    for (int a = 0; a < 4; a++) {
      PT(2 + a, rect->xmax - vec[a][1], rect->ymin + rad - vec[a][0]);
    }
    PT(6, rect->xmax - rad, rect->ymin);
    bk_polygon((const float(*)[2])pts, 7, color);
  }
  if (corners & CNR_BL) {
    PT(0, rect->xmin - 1, rect->ymin);
    PT(1, rect->xmin + rad, rect->ymin);
    for (int a = 0; a < 4; a++) {
      PT(2 + a, rect->xmin + rad - vec[a][0], rect->ymin + vec[a][1]);
    }
    PT(6, rect->xmin, rect->ymin + rad);
    bk_polygon((const float(*)[2])pts, 7, color);
  }
#undef PT
}

static void theme_color(const uchar c[4], float alpha_fac, float r_out[4])
{
  bk_rgba(c, r_out);
  r_out[3] *= alpha_fac;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Edges
 * \{ */

void screen_draw_edges(BKNode *screen_node)
{
  const BKScreen *screen = SCREEN(screen_node);
  if (screen->state != SCREENNORMAL || screen_node->nkids < 2) {
    return; /* no edges on a screen with only one editor */
  }
  float border[4], outline1[4], outline2[4];
  bk_rgba(bk_theme.editor_border, border);
  bk_rgba(bk_theme.editor_outline, outline1);
  bk_rgba(bk_theme.editor_outline_active, outline2);
  const float edge_thickness = (float)U_BORDER_WIDTH * UI_SCALE_FAC;

  /* GPU_SHADER_2D_AREA_BORDERS: the border color between the area's rounded rect and its rect
   * grown by the edge thickness. */
  SCREEN_AREAS_ITER (screen_node, area) {
    rctf in, out;
    rctf_from_rcti(&in, &area->totrct);
    out = in;
    rctf_pad(&out, edge_thickness, edge_thickness);
    fill(screen_node, out.xmin, out.xmax, in.ymax, out.ymax, border);
    fill(screen_node, out.xmin, out.xmax, out.ymin, in.ymin, border);
    fill(screen_node, out.xmin, in.xmin, in.ymin, in.ymax, border);
    fill(screen_node, in.xmax, out.xmax, in.ymin, in.ymax, border);
    rounded_corners(screen_node, &in, border, CNR_ALL);
  }
  /* Outset by 1/2 pixel, regardless of UI scale or pixel size. #141550. */
  SCREEN_AREAS_ITER (screen_node, area) {
    rctf bounds;
    rctf_from_rcti(&bounds, &area->totrct);
    rctf_pad(&bounds, 0.5f, 0.5f);
    roundbox(screen_node, &bounds, NULL, (area->node == screen->active) ? outline2 : outline1,
             (float)U_PIXELSIZE, EDITORRADIUS, CNR_ALL);
  }
}

void screen_draw_move_highlight(BKNode *screen_node, const eScreenAxis dir_axis,
                                const float anim_factor)
{
  const BKScreen *screen = SCREEN(screen_node);
  rctf rect = {32767.0f, -32768.0f, 32767.0f, -32768.0f};
  bool any = false;
  for (const ScrEdge *edge = (const ScrEdge *)screen->edgebase.first; edge; edge = edge->next) {
    if (edge->v1->editflag && edge->v2->editflag) {
      any = true;
      if (dir_axis == SCREEN_AXIS_H) {
        rect.xmin = fminf(rect.xmin, fminf(edge->v1->x, edge->v2->x));
        rect.xmax = fmaxf(rect.xmax, fmaxf(edge->v1->x, edge->v2->x));
        rect.ymin = rect.ymax = (float)edge->v1->y;
      }
      else {
        rect.ymin = fminf(rect.ymin, fminf(edge->v1->y, edge->v2->y));
        rect.ymax = fmaxf(rect.ymax, fmaxf(edge->v1->y, edge->v2->y));
        rect.xmin = rect.xmax = (float)edge->v1->x;
      }
    }
  }
  if (!any) {
    return;
  }
  const float offset = U_BORDER_WIDTH * UI_SCALE_FAC;
  const float width = fminf(2.0f * offset, 5.0f * UI_SCALE_FAC);
  if (dir_axis == SCREEN_AXIS_H) {
    rctf_pad(&rect, -offset, width);
  }
  else {
    rctf_pad(&rect, width, -offset);
  }
  /* the edge is the pixel at vert x/y: center the bar on it */
  rect.xmin += dir_axis == SCREEN_AXIS_V ? 0.5f : 0.0f;
  rect.xmax += dir_axis == SCREEN_AXIS_V ? 0.5f : 0.0f;
  rect.ymin += dir_axis == SCREEN_AXIS_H ? 0.5f : 0.0f;
  rect.ymax += dir_axis == SCREEN_AXIS_H ? 0.5f : 0.0f;
  const float inner[4] = {1.0f, 1.0f, 1.0f, 0.4f * anim_factor};
  float outline[4];
  theme_color(bk_theme.editor_border, anim_factor, outline);
  roundbox(screen_node, &rect, inner, outline, width - U_PIXELSIZE, 2.5f * UI_SCALE_FAC, CNR_ALL);
}

void screen_draw_region_scale_highlight(const BKNode *space, const BKRegion *region)
{
  rctf rect;
  rctf_from_rcti(&rect, &region->winrct);
  const float px = (float)U_PIXELSIZE;
  switch (region->alignment) {
    case RGN_ALIGN_RIGHT:
      rect.xmax = rect.xmin - px;
      rect.xmin = rect.xmax - (4.0f * px);
      rect.ymax -= EDITORRADIUS;
      rect.ymin += EDITORRADIUS;
      break;
    case RGN_ALIGN_LEFT:
      rect.xmin = rect.xmax + px;
      rect.xmax = rect.xmin + (4.0f * px);
      rect.ymax -= EDITORRADIUS;
      rect.ymin += EDITORRADIUS;
      break;
    case RGN_ALIGN_TOP:
      rect.ymax = rect.ymin - px;
      rect.ymin = rect.ymax - (4.0f * px);
      rect.xmax -= EDITORRADIUS;
      rect.xmin += EDITORRADIUS;
      break;
    case RGN_ALIGN_BOTTOM:
      rect.ymin = rect.ymax + px;
      rect.ymax = rect.ymin + (4.0f * px);
      rect.xmax -= EDITORRADIUS;
      rect.xmin += EDITORRADIUS;
      break;
    default:
      return;
  }
  const float inner[4] = {1.0f, 1.0f, 1.0f, 0.4f};
  const float outline[4] = {0.0f, 0.0f, 0.0f, 0.3f};
  roundbox(space, &rect, inner, outline, 1.0f * px, 2.5f * UI_SCALE_FAC, CNR_ALL);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Split, join and dock previews
 * \{ */

void screen_draw_area_drag_tip(BKNode *screen_node, int x, int y, const char *hint,
                               const BKArea *source)
{
  const char *area_name = source->node->text ? source->node->text : "";
  const float margin = 4.0f * UI_SCALE_FAC;
  const float line_gap = 5.0f * UI_SCALE_FAC;
  const float lheight = bk_line_height();
  const float width = margin + fmaxf(bk_text_width(hint, -1, false),
                                     bk_text_width(area_name, -1, false)) + margin;
  const float height = margin + lheight + line_gap + lheight + margin;
  /* Position of this hint relative to the mouse position (window pixels, y down). */
  const int left = BK_MIN(x + (int)(5.0f * UI_SCALE_FAC),
                          screen_node->x + screen_node->w - (int)width);
  const int top = BK_MAX(y + (int)(7.0f * UI_SCALE_FAC), screen_node->y);
  const BKRect back = bk_rect(left, top, (int)width, (int)height);
  bk_widget_tooltip_back(&back);
  const BKRect line1 = bk_rect(left + (int)margin, top + (int)margin, (int)(width - 2 * margin),
                               (int)lheight);
  const BKRect line2 = bk_rect(left + (int)margin, top + (int)(margin + lheight + line_gap),
                               (int)(width - 2 * margin), (int)lheight);
  bk_text_in_pad(&line1, hint, bk_theme.tooltip.text, BK_ALIGN_LEFT, false, 0);
  bk_text_in_pad(&line2, area_name, bk_theme.tooltip.text, BK_ALIGN_LEFT, false, 0);
}

static void screen_draw_area_closed(const BKNode *space, float xmin, float xmax, float ymin,
                                    float ymax, float anim_factor)
{
  /* Darken the area. */
  const rctf rect = {xmin, xmax, ymin, ymax};
  const float darken[4] = {0.0f, 0.0f, 0.0f, 0.7f * anim_factor};
  roundbox(space, &rect, darken, NULL, (float)U_PIXELSIZE, EDITORRADIUS, CNR_ALL);
}

void screen_draw_join_highlight(BKNode *screen_node, BKArea *sa1, BKArea *sa2, eScreenDir dir)
{
  rctf r1, r2;
  rctf_from_rcti(&r1, &sa1->totrct);
  if (dir == SCREEN_DIR_NONE || !sa2) {
    /* Darken source if docking. */
    screen_draw_area_closed(screen_node, r1.xmin, r1.xmax, r1.ymin, r1.ymax, 1.0f);
    return;
  }
  rctf_from_rcti(&r2, &sa2->totrct);
  const float anim_factor = 1.0f;
  /* Rect of the combined areas. */
  const bool vertical = SCREEN_DIR_IS_VERTICAL(dir);
  rctf combined;
  combined.xmin = vertical ? fmaxf(r1.xmin, r2.xmin) : fminf(r1.xmin, r2.xmin);
  combined.xmax = vertical ? fminf(r1.xmax, r2.xmax) : fmaxf(r1.xmax, r2.xmax);
  combined.ymin = vertical ? fminf(r1.ymin, r2.ymin) : fmaxf(r1.ymin, r2.ymin);
  combined.ymax = vertical ? fmaxf(r1.ymax, r2.ymax) : fminf(r1.ymax, r2.ymax);

  int offset1, offset2;
  area_getoffsets(sa1, sa2, dir, &offset1, &offset2);
  if (offset1 < 0 || offset2 > 0) {
    /* Show partial areas that will be closed. */
    const rctf *rs[2] = {&r1, &r2};
    for (int i = 0; i < 2; i++) {
      const rctf *r = rs[i];
      if (vertical) {
        if (r->xmin < combined.xmin) {
          screen_draw_area_closed(screen_node, r->xmin, combined.xmin, r->ymin, r->ymax,
                                  anim_factor);
        }
        if (r->xmax > combined.xmax) {
          screen_draw_area_closed(screen_node, combined.xmax, r->xmax, r->ymin, r->ymax,
                                  anim_factor);
        }
      }
      else {
        if (r->ymin < combined.ymin) {
          screen_draw_area_closed(screen_node, r->xmin, r->xmax, r->ymin, combined.ymin,
                                  anim_factor);
        }
        if (r->ymax > combined.ymax) {
          screen_draw_area_closed(screen_node, r->xmin, r->xmax, combined.ymax, r->ymax,
                                  anim_factor);
        }
      }
    }
  }
  /* Outline the combined area. */
  const float outline[4] = {1.0f, 1.0f, 1.0f, 0.4f * anim_factor};
  const float inner[4] = {1.0f, 1.0f, 1.0f, 0.10f * anim_factor};
  roundbox(screen_node, &combined, inner, outline, (float)U_PIXELSIZE, EDITORRADIUS, CNR_ALL);
  const BKScreen *screen = SCREEN(screen_node);
  screen_draw_area_drag_tip(screen_node, screen->jd.current_x, screen->jd.current_y, "Join Areas",
                            sa1);
}

void screen_draw_dock_preview(BKNode *screen_node, BKArea *source, BKArea *target,
                              AreaDockTarget dock_target, float factor)
{
  if (dock_target == DOCK_NONE) {
    return;
  }
  const float anim_factor = 1.0f;
  const float outline[4] = {1.0f, 1.0f, 1.0f, 0.4f * anim_factor};
  const float inner[4] = {1.0f, 1.0f, 1.0f, 0.1f * anim_factor};
  float border[4];
  theme_color(bk_theme.editor_border, anim_factor, border);
  const float half_line_width = (float)U_BORDER_WIDTH * UI_SCALE_FAC;

  rctf dest;
  rctf_from_rcti(&dest, &target->totrct);
  float split = 0.0f;
  int corners = 0;
  if (dock_target == DOCK_RIGHT) {
    split = fminf(dest.xmin + target->winx * (1.0f - factor), dest.xmax - AREAMINX * UI_SCALE_FAC);
    dest.xmin = split + half_line_width;
    corners = CNR_TL | CNR_BL;
  }
  else if (dock_target == DOCK_LEFT) {
    split = fmaxf(dest.xmax - target->winx * (1.0f - factor), dest.xmin + AREAMINX * UI_SCALE_FAC);
    dest.xmax = split - half_line_width;
    corners = CNR_TR | CNR_BR;
  }
  else if (dock_target == DOCK_TOP) {
    split = fminf(dest.ymin + target->winy * (1.0f - factor), dest.ymax - HEADERY * UI_SCALE_FAC);
    dest.ymin = split + half_line_width;
    corners = CNR_BR | CNR_BL;
  }
  else if (dock_target == DOCK_BOTTOM) {
    split = fmaxf(dest.ymax - target->winy * (1.0f - factor), dest.ymin + HEADERY * UI_SCALE_FAC);
    dest.ymax = split - half_line_width;
    corners = CNR_TR | CNR_TL;
  }
  rounded_corners(screen_node, &dest, border, corners);
  roundbox(screen_node, &dest, inner, NULL, (float)U_PIXELSIZE, EDITORRADIUS, CNR_ALL);
  roundbox(screen_node, &dest, NULL, outline, (float)U_PIXELSIZE, EDITORRADIUS, CNR_ALL);

  if (dock_target != DOCK_CENTER) {
    /* Darken the split position itself. */
    if (dock_target == DOCK_RIGHT || dock_target == DOCK_LEFT) {
      dest.xmin = split - half_line_width;
      dest.xmax = split + half_line_width;
    }
    else {
      dest.ymin = split - half_line_width;
      dest.ymax = split + half_line_width;
    }
    fill(screen_node, dest.xmin, dest.xmax, dest.ymin, dest.ymax, border);
  }
  const BKScreen *screen = SCREEN(screen_node);
  screen_draw_area_drag_tip(screen_node, screen->jd.current_x, screen->jd.current_y,
                            dock_target == DOCK_CENTER ? "Replace this area" : "Move area here",
                            source);
}

void screen_draw_split_preview(BKNode *screen_node, BKArea *area, const eScreenAxis dir_axis,
                               const float factor)
{
  const float outline[4] = {1.0f, 1.0f, 1.0f, 0.4f};
  const float inner[4] = {1.0f, 1.0f, 1.0f, 0.10f};
  float border[4];
  bk_rgba(bk_theme.editor_border, border);
  rctf rect;
  rctf_from_rcti(&rect, &area->totrct);
  if (factor < 0.0001f || factor > 0.9999f) {
    roundbox(screen_node, &rect, inner, outline, (float)U_PIXELSIZE, EDITORRADIUS, CNR_ALL);
    return;
  }
  const rctf whole = rect;
  float x = (1 - factor) * rect.xmin + factor * rect.xmax;
  float y = (1 - factor) * rect.ymin + factor * rect.ymax;
  x = BK_CLAMP(x, rect.xmin, rect.xmax);
  y = BK_CLAMP(y, rect.ymin, rect.ymax);
  const float half_line_width = (float)U_BORDER_WIDTH * UI_SCALE_FAC;

  /* Outlined rectangle to left/below the split position. */
  rect.xmax = (dir_axis == SCREEN_AXIS_V) ? x - half_line_width : rect.xmax;
  rect.ymax = (dir_axis == SCREEN_AXIS_H) ? y - half_line_width : rect.ymax;
  rounded_corners(screen_node, &rect, border,
                  (dir_axis == SCREEN_AXIS_H) ? CNR_TR | CNR_TL : CNR_BR | CNR_TR);
  roundbox(screen_node, &rect, inner, outline, (float)U_PIXELSIZE, EDITORRADIUS, CNR_ALL);

  /* Outlined rectangle to right/above the split position. */
  rect = whole;
  if (dir_axis == SCREEN_AXIS_H) {
    rect.ymin = y + half_line_width;
  }
  else {
    rect.xmin = x + half_line_width;
  }
  rounded_corners(screen_node, &rect, border,
                  (dir_axis == SCREEN_AXIS_H) ? CNR_BR | CNR_BL : CNR_BL | CNR_TL);
  roundbox(screen_node, &rect, inner, outline, (float)U_PIXELSIZE, EDITORRADIUS, CNR_ALL);

  /* Darken the split position itself. */
  rect = whole;
  if (dir_axis == SCREEN_AXIS_H) {
    rect.ymin = y - half_line_width;
    rect.ymax = y + half_line_width;
  }
  else {
    rect.xmin = x - half_line_width;
    rect.xmax = x + half_line_width;
  }
  fill(screen_node, rect.xmin, rect.xmax, rect.ymin, rect.ymax, border);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Action zones and region backgrounds
 * \{ */

/* Edge widgets to show hidden regions (draw_azone_arrow). */
static void draw_azone_arrow(const BKNode *space, const rcti *r, AZEdge edge)
{
  const float size = 0.2f * bk_unit();
  const float l = 1.0f, s = 0.25f, hl = l / 2.0f;
  const float points[6][2] = {
      {0, -hl}, {l, hl}, {l - s, hl + s}, {0, s + s - hl}, {s - l, hl + s}, {-l, hl}};
  const float center[2] = {(r->xmin + r->xmax) / 2.0f, (r->ymin + r->ymax) / 2.0f};
  int axis, sign;
  switch (edge) {
    case AE_BOTTOM_TO_TOPLEFT:
      axis = 0, sign = 1;
      break;
    case AE_TOP_TO_BOTTOMRIGHT:
      axis = 0, sign = -1;
      break;
    case AE_LEFT_TO_TOPRIGHT:
      axis = 1, sign = 1;
      break;
    default:
      axis = 1, sign = -1;
      break;
  }
  float pts[6][2];
  for (int i = 0; i < 6; i++) {
    const float px = axis == 0 ? center[0] + points[i][0] * size :
                                 center[0] + points[i][1] * sign * size;
    const float py = axis == 0 ? center[1] + points[i][1] * sign * size :
                                 center[1] + points[i][0] * size;
    pts[i][0] = wx(space, px);
    pts[i][1] = wy(space, py);
  }
  const float color[4] = {0.8f, 0.8f, 0.8f, 0.4f};
  bk_polygon((const float(*)[2])pts, 6, color);
}

void area_draw_azones(const BKNode *space, BKArea *area)
{
  for (int i = 0; i < area->nazones; i++) {
    const AZone *az = &area->azones[i];
    if (az->type == AZONE_REGION) {
      const BKRegion *region = az->region->rgn;
      /* only display tab or icons when the region is hidden */
      if (!(region->flag & (RGN_FLAG_HIDDEN | RGN_FLAG_TOO_SMALL))) {
        continue;
      }
      int corners = CNR_TR | CNR_BR;
      switch (az->edge) {
        case AE_TOP_TO_BOTTOMRIGHT:
          corners = CNR_TL | CNR_TR;
          break;
        case AE_BOTTOM_TO_TOPLEFT:
          corners = CNR_BR | CNR_BL;
          break;
        case AE_LEFT_TO_TOPRIGHT:
          corners = CNR_TL | CNR_BL;
          break;
        default:
          break;
      }
      const float color[4] = {0.05f, 0.05f, 0.05f, 0.4f};
      /* Hit size is a bit larger than visible background. */
      const float px = (float)U_PIXELSIZE;
      const rctf rect = {az->rect.xmin + px, az->rect.xmax - px + 1.0f, az->rect.ymin + px,
                         az->rect.ymax - px + 1.0f};
      roundbox(space, &rect, color, NULL, 0.0f, 4.0f, corners);
      draw_azone_arrow(space, &az->rect, (AZEdge)az->edge);
    }
    else if (az->type == AZONE_FULLSCREEN) {
      /* area_draw_azone_fullscreen: the corner icon that leaves focus mode */
      const float u = (float)bk_unit();
      bk_icon(bk_icons_lookup("FULLSCREEN_EXIT"), wx(space, az->rect.xmax - u * 0.5f),
              wy(space, az->rect.ymax - u * 0.5f), NULL, 0.75f, 0.0f);
    }
  }
}

void area_draw_regions_back(BKNode *area_node)
{
  const BKNode *screen_node = AREA_SCREEN(area_node);
  if (screen_node && SCREEN(screen_node)->host_content) {
    return;
  }
  for (int i = 0; i < area_node->nkids; i++) {
    BKNode *c = area_node->kids[i];
    const BKRegion *region = region_of(c);
    if (c->w <= 0 || c->h <= 0 || c->cls == &bk_cls_areaheader || c->cls == &bk_cls_sidebar) {
      continue; /* hidden, or a region class that draws its own background */
    }
    if (region && region->overlap) {
      continue; /* drawn over the main region, without a background */
    }
    const bool header = region && (region->type == RGN_TYPE_HEADER ||
                                   region->type == RGN_TYPE_TOOL_HEADER ||
                                   region->type == RGN_TYPE_FOOTER);
    bk_fill(c->x, c->y, c->w, c->h, header ? bk_theme.area_header : bk_theme.region_bg);
  }
}

/** \} */
