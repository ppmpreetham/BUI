/* Areas: region layout, action zones, hiding and flipping regions.
 *
 * Ported from editors/screen/area.cc: area_calc_totrct,
 * region_rect_recursive, region_overlap_fix, ED_area_max_regionsize,
 * area_azone_init, region_azone_edge, region_azone_tab_plus,
 * region_azone_edge_poll, region_azones_add, region_toggle_hidden,
 * ED_area_update_region_sizes; and the region types' defaults from the
 * spaces' ARegionType setup (space_view3d.cc and friends) and area_utils.cc
 * (ED_region_generic_tools_region_snap_size, _panel_region_snap_size).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "screen_intern.h"

/* -------------------------------------------------------------------- */
/** \name Region types
 * \{ */

static int tools_snap_size(const BKRegion *region, int size, int axis)
{
  (void)region;
  if (axis != 0) {
    return size;
  }
  const float column = UI_TOOLBAR_COLUMN, margin = UI_TOOLBAR_MARGIN;
  const float snap_units[] = {column + margin, (2.0f * column) + margin, (2.7f * column) + margin};
  int best_diff = 1 << 30, best_size = size;
  /* Only snap if less than last snap unit. */
  if (size <= snap_units[2]) {
    for (int i = 0; i < 3; i++) {
      const int test_size = (int)snap_units[i];
      const int test_diff = abs(test_size - size);
      if (test_diff < best_diff) {
        best_size = test_size;
        best_diff = test_diff;
      }
    }
  }
  return best_size;
}

static int panel_snap_size(const BKRegion *region, int size, int axis)
{
  (void)region;
  /* ED_region_generic_panel_region_snap_size: only the category tabs when narrow */
  return axis == 0 && size < UI_PANEL_CATEGORY_MIN_SNAP_WIDTH ? (int)UI_PANEL_CATEGORY_MIN_WIDTH :
                                                                 size;
}

#define SIDES_TB ((1 << RGN_ALIGN_TOP) | (1 << RGN_ALIGN_BOTTOM))
#define SIDES_LR ((1 << RGN_ALIGN_LEFT) | (1 << RGN_ALIGN_RIGHT))

static const RegionTypeInfo g_region_types[RGN_TYPE_NUM] = {
    [RGN_TYPE_HEADER] = {"header", RGN_ALIGN_TOP, SIDES_TB, 0, HEADERY, false, false, true, NULL},
    [RGN_TYPE_TOOL_HEADER] = {"tool_header", RGN_ALIGN_TOP, SIDES_TB, 0, HEADERY, true, false, true,
                              NULL},
    [RGN_TYPE_FOOTER] = {"footer", RGN_ALIGN_BOTTOM, SIDES_TB, 0, HEADERY, true, false, true, NULL},
    /* asset::shelf::region_prefsizey: a row of previews and the shelf's padding */
    [RGN_TYPE_ASSET_SHELF] = {"asset_shelf", RGN_ALIGN_BOTTOM, SIDES_TB, 0, 120, true, false,
                              false, NULL},
    [RGN_TYPE_CHANNELS] = {"channels", RGN_ALIGN_LEFT, SIDES_LR, 200, 0, false, false, false,
                           NULL},
    [RGN_TYPE_NAV_BAR] = {"nav_bar", RGN_ALIGN_LEFT, SIDES_LR, UI_NAVIGATION_REGION_WIDTH, 0,
                          false, false, false, NULL},
    [RGN_TYPE_TOOLS] = {"toolbar", RGN_ALIGN_LEFT, SIDES_LR, (short)UI_TOOLBAR_WIDTH, 0, true,
                        false, false, tools_snap_size},
    [RGN_TYPE_UI] = {"sidebar", RGN_ALIGN_RIGHT, SIDES_LR, UI_SIDEBAR_PANEL_WIDTH, 0, true, true,
                     false, panel_snap_size},
    [RGN_TYPE_EXECUTE] = {"execute", RGN_ALIGN_BOTTOM, SIDES_TB, 0, HEADERY, false, false, true,
                          NULL},
    [RGN_TYPE_HUD] = {"hud", RGN_ALIGN_FLOAT, 1 << RGN_ALIGN_FLOAT, 0, 0, true, false, false,
                      NULL},
    [RGN_TYPE_WINDOW] = {"window", RGN_ALIGN_NONE, 1 << RGN_ALIGN_NONE, 0, 0, false, false, false,
                         NULL},
};

const RegionTypeInfo *region_type_info(int type)
{
  return &g_region_types[BK_CLAMP(type, 0, RGN_TYPE_NUM - 1)];
}

int region_type_from_name(const char *name)
{
  for (int i = 0; i < RGN_TYPE_NUM; i++) {
    if (bk_streq(g_region_types[i].name, name)) {
      return i;
    }
  }
  return -1;
}

static bool region_is_header_any(int type)
{
  return type == RGN_TYPE_HEADER || type == RGN_TYPE_TOOL_HEADER || type == RGN_TYPE_FOOTER;
}

/* The region type a node has without saying (the kit's own region classes). */
static int region_implicit_type(const BKNode *n)
{
  if (n->cls == &bk_cls_areaheader) {
    return RGN_TYPE_HEADER;
  }
  if (n->cls == &bk_cls_sidebar) {
    return RGN_TYPE_UI;
  }
  return RGN_TYPE_WINDOW;
}

bool region_wanted(const BKNode *n)
{
  return n->rgn ? n->rgn->type != RGN_TYPE_WINDOW : region_implicit_type(n) != RGN_TYPE_WINDOW;
}

BKRegion *region_ensure(BKNode *n)
{
  if (!n->rgn) {
    n->rgn = (BKRegion *)calloc(1, sizeof(BKRegion));
    n->rgn->type = (short)region_implicit_type(n);
    n->rgn->alignment = region_type_info(n->rgn->type)->alignment;
  }
  return n->rgn;
}

BKRegion *region_of(const BKNode *n)
{
  if (!IS_AREA(n->parent)) {
    return NULL;
  }
  BKRegion *r = n->rgn ? n->rgn : (region_implicit_type(n) != RGN_TYPE_WINDOW ?
                                       region_ensure((BKNode *)n) :
                                       NULL);
  return (r && r->type != RGN_TYPE_WINDOW) ? r : NULL;
}

BKNode *area_region_find(BKNode *area_node, int type)
{
  for (int i = 0; i < area_node->nkids; i++) {
    const BKRegion *r = region_of(area_node->kids[i]);
    if (r ? r->type == type : type == RGN_TYPE_WINDOW) {
      return area_node->kids[i];
    }
  }
  return NULL;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Coordinates
 * \{ */

const BKNode *area_space(const BKNode *area_node)
{
  return IS_SCREEN(area_node->parent) ? area_node->parent : area_node;
}

void space_from_window(const BKNode *space, int wx, int wy, int *r_x, int *r_y)
{
  *r_x = wx - space->x;
  *r_y = space->h - 1 - (wy - space->y);
}

BKRect space_to_window(const BKNode *space, const rcti *r)
{
  BKRect out = {space->x + r->xmin, space->y + (space->h - 1 - r->ymax), space->x + r->xmax + 1,
                space->y + (space->h - 1 - r->ymin) + 1};
  return out;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Region layout (region_rect_recursive)
 * \{ */

bool region_is_hidden(const BKRegion *region)
{
  return (region->flag & RGN_FLAG_HIDDEN) != 0;
}

/* dir is direction to check, not the splitting edge direction! */
static int rct_fits(const rcti *rect, const eScreenAxis dir_axis, int size)
{
  if (dir_axis == SCREEN_AXIS_H) {
    return BLI_rcti_size_x(rect) + 1 - size;
  }
  return BLI_rcti_size_y(rect) + 1 - size; /* Vertical. */
}

/* The area's regions in layout order (regionbase), the main region last. */
static int area_regions_ordered(BKNode *area_node, BKNode **out, int max)
{
  int count = 0;
  for (int type = 0; type < RGN_TYPE_NUM && count < max; type++) {
    for (int i = 0; i < area_node->nkids && count < max; i++) {
      BKNode *c = area_node->kids[i];
      const BKRegion *r = region_of(c);
      if (r ? r->type == type : type == RGN_TYPE_WINDOW) {
        out[count++] = c;
        break; /* one region of each type */
      }
    }
  }
  return count;
}

/* region should be overlapping: checks if some overlapping region was defined before - on the
 * same place (region_overlap_fix). */
static void region_overlap_fix(BKArea *area, BKNode **regions, int index)
{
  BKRegion *region = regions[index]->rgn;
  int align1 = 0, iter = index - 1;
  const int align = region->alignment;
  for (; iter >= 0; iter--) {
    const BKRegion *ri = region_of(regions[iter]);
    if (!ri || region_is_hidden(ri) || !ri->overlap || ri->alignment == RGN_ALIGN_FLOAT) {
      continue;
    }
    align1 = ri->alignment;
    if (BLI_rcti_isect(&ri->winrct, &region->winrct, NULL)) {
      if (align1 != align) {
        region->flag |= RGN_FLAG_TOO_SMALL; /* left overlapping right or vice-versa */
        return;
      }
      break; /* our previous region on the same side */
    }
  }
  /* translate or close */
  if (iter >= 0) {
    const BKRegion *ri = regions[iter]->rgn;
    if (align1 == RGN_ALIGN_LEFT) {
      if (region->winrct.xmax + ri->winx > area->winx - UI_UNIT_X) {
        region->flag |= RGN_FLAG_TOO_SMALL;
        return;
      }
      BLI_rcti_translate(&region->winrct, ri->winx, 0);
    }
    else if (align1 == RGN_ALIGN_RIGHT) {
      if (region->winrct.xmin - ri->winx < UI_UNIT_X) {
        region->flag |= RGN_FLAG_TOO_SMALL;
        return;
      }
      BLI_rcti_translate(&region->winrct, -ri->winx, 0);
    }
  }
  /* Final check it does not overlap any previous 'other side' region. */
  for (iter = index - 1; iter >= 0; iter--) {
    const BKRegion *ri = region_of(regions[iter]);
    if (!ri || region_is_hidden(ri) || !ri->overlap || ri->alignment == RGN_ALIGN_FLOAT) {
      continue;
    }
    if (ri->alignment != align && BLI_rcti_isect(&ri->winrct, &region->winrct, NULL)) {
      region->flag |= RGN_FLAG_TOO_SMALL;
      return;
    }
  }
}

/* One region's rect off `remainder` (region_rect_recursive's body). The main region (`region`
 * NULL) takes what is left. */
static void region_rect_calc(BKArea *area, BKNode **regions, int index, int count, rcti *remainder,
                             rcti *overlap_remainder, rcti *r_main)
{
  BKNode *node = regions[index];
  BKRegion *region = region_of(node);
  if (!region) {
    /* RGN_ALIGN_NONE: typically the last region */
    *r_main = *remainder;
    if (rct_fits(remainder, SCREEN_AXIS_V, 1) < 0 || rct_fits(remainder, SCREEN_AXIS_H, 1) < 0) {
      BLI_rcti_init(r_main, remainder->xmin, remainder->xmin, remainder->ymin, remainder->ymin);
    }
    BLI_rcti_init(remainder, 0, 0, 0, 0);
    return;
  }
  const RegionTypeInfo *info = region_type_info(region->type);
  int alignment = region->alignment;
  const bool is_hidden = region_is_hidden(region);
  (void)count;

  region->overlap = info->overlap && !region->overlap_off;
  BLI_rcti_init(&region->winrct, 0, 0, 0, 0);
  region->flag &= ~(RGN_FLAG_TOO_SMALL | RGN_FLAG_SIZE_CLAMP_X | RGN_FLAG_SIZE_CLAMP_Y);

  int natural_w = 0, natural_h = 0;
  if (alignment == RGN_ALIGN_FLOAT) {
    bk_natural(node, &natural_w, &natural_h); /* the HUD sizes itself to its panel */
  }
  /* `prefsizex/y`, taking into account DPI. */
  int prefsizex = alignment == RGN_ALIGN_FLOAT ?
                      natural_w :
                      (int)(UI_SCALE_FAC *
                            ((region->sizex > 1) ? region->sizex + 0.5f : info->prefsizex));
  int prefsizey;
  if (info->fixed_height) {
    prefsizey = screen_headersize();
  }
  else if (alignment == RGN_ALIGN_FLOAT) {
    prefsizey = natural_h;
  }
  else {
    prefsizey = (int)(UI_SCALE_FAC *
                      (region->sizey > 1 ? region->sizey + 0.5f : info->prefsizey));
  }

  if (is_hidden) {
    /* hidden is user flag */
  }
  else if (alignment == RGN_ALIGN_FLOAT) {
    /* Aligns to the lower left of the area (only the HUD floats). */
    const int size_min[2] = {UI_UNIT_X, UI_UNIT_Y};
    rcti margin = *overlap_remainder;
    BLI_rcti_resize(&margin, BK_MAX(0, BLI_rcti_size_x(overlap_remainder) - UI_UNIT_X / 2),
                    BK_MAX(0, BLI_rcti_size_y(overlap_remainder) - UI_UNIT_Y / 2));
    region->winrct.xmin = margin.xmin + region->offset_x;
    region->winrct.ymin = margin.ymin + region->offset_y;
    region->winrct.xmax = region->winrct.xmin + prefsizex - 1;
    region->winrct.ymax = region->winrct.ymin + prefsizey - 1;
    BLI_rcti_isect(&region->winrct, &margin, &region->winrct);
    if (BLI_rcti_size_x(&region->winrct) != prefsizex - 1) {
      region->flag |= RGN_FLAG_SIZE_CLAMP_X;
    }
    if (BLI_rcti_size_y(&region->winrct) != prefsizey - 1) {
      region->flag |= RGN_FLAG_SIZE_CLAMP_Y;
    }
    rcti test = {region->winrct.xmin, region->winrct.xmin + size_min[0], region->winrct.ymin,
                 region->winrct.ymin + size_min[1]};
    BLI_rcti_isect(&test, &margin, &test);
    if (BLI_rcti_size_x(&test) < size_min[0] || BLI_rcti_size_y(&test) < size_min[1]) {
      region->flag |= RGN_FLAG_TOO_SMALL;
    }
  }
  else if (rct_fits(remainder, SCREEN_AXIS_V, 1) < 0 || rct_fits(remainder, SCREEN_AXIS_H, 1) < 0)
  {
    region->flag |= RGN_FLAG_TOO_SMALL; /* remainder is too small for any usage */
  }
  else if (alignment == RGN_ALIGN_TOP || alignment == RGN_ALIGN_BOTTOM) {
    rcti *winrct = region->overlap ? overlap_remainder : remainder;
    if (prefsizey == 0 || rct_fits(winrct, SCREEN_AXIS_V, prefsizey) < (U_PIXELSIZE * -2)) {
      region->flag |= RGN_FLAG_TOO_SMALL;
    }
    else {
      const int fac = rct_fits(winrct, SCREEN_AXIS_V, prefsizey);
      if (fac < 0) {
        prefsizey += fac;
      }
      region->winrct = *winrct;
      if (alignment == RGN_ALIGN_TOP) {
        region->winrct.ymin = region->winrct.ymax - prefsizey + 1;
        winrct->ymax = region->winrct.ymin - 1;
      }
      else {
        region->winrct.ymax = region->winrct.ymin + prefsizey - 1;
        winrct->ymin = region->winrct.ymax + 1;
      }
      BLI_rcti_sanitize(winrct);
    }
  }
  else if (alignment == RGN_ALIGN_LEFT || alignment == RGN_ALIGN_RIGHT) {
    rcti *winrct = region->overlap ? overlap_remainder : remainder;
    const int width = BLI_rcti_size_x(winrct) + 1;
    if (prefsizex == 0) {
      region->flag |= RGN_FLAG_TOO_SMALL;
    }
    else if (width < prefsizex) {
      const int min = (int)(UI_SCALE_FAC * (info->category_tabs ? UI_PANEL_CATEGORY_MIN_SNAP_WIDTH :
                                                                  UI_TOOLBAR_WIDTH));
      if (width > min) {
        region->winrct = *winrct; /* adjust width to fit */
        BLI_rcti_sanitize(winrct);
      }
      else if (info->category_tabs) {
        /* Too narrow for content so show only the category tabs. */
        const int cat_min = (int)(UI_PANEL_CATEGORY_MIN_WIDTH * UI_SCALE_FAC);
        region->winrct = *winrct;
        if (alignment == RGN_ALIGN_RIGHT) {
          region->winrct.xmin = region->winrct.xmax - cat_min + 1;
        }
        else {
          region->winrct.xmax = region->winrct.xmin + cat_min - 1;
        }
        BLI_rcti_sanitize(winrct);
      }
      else {
        region->flag |= RGN_FLAG_TOO_SMALL;
      }
    }
    else {
      const int fac = rct_fits(winrct, SCREEN_AXIS_H, prefsizex);
      if (fac < 0) {
        prefsizex += fac;
      }
      region->winrct = *winrct;
      if (alignment == RGN_ALIGN_RIGHT) {
        region->winrct.xmin = region->winrct.xmax - prefsizex + 1;
        winrct->xmax = region->winrct.xmin - 1;
      }
      else {
        region->winrct.xmax = region->winrct.xmin + prefsizex - 1;
        winrct->xmin = region->winrct.xmax + 1;
      }
      BLI_rcti_sanitize(winrct);
    }
  }

  region->winx = BLI_rcti_size_x(&region->winrct) + 1;
  region->winy = BLI_rcti_size_y(&region->winrct) + 1;
  if (region->winy <= U_BORDER_WIDTH && !is_hidden) {
    region->flag |= RGN_FLAG_TOO_SMALL; /* don't draw when just a couple pixels tall */
  }
  /* If region opened normally, store this for hide/reveal usage. */
  if (alignment != RGN_ALIGN_FLOAT && !(region->flag & RGN_FLAG_TOO_SMALL) && !is_hidden) {
    if (region->winx > 1 && (alignment == RGN_ALIGN_LEFT || alignment == RGN_ALIGN_RIGHT)) {
      region->sizex = (short)((region->winx + 0.5f) / UI_SCALE_FAC);
    }
    if (region->winy > 1 && !info->fixed_height &&
        (alignment == RGN_ALIGN_TOP || alignment == RGN_ALIGN_BOTTOM))
    {
      region->sizey = (short)((region->winy + 0.5f) / UI_SCALE_FAC);
    }
  }
  /* exception for multiple overlapping regions on same spot */
  if (region->overlap && alignment != RGN_ALIGN_FLOAT) {
    region_overlap_fix(area, regions, index);
  }
  /* Set `region->winrct` for action-zones: an edge line where the region would be. */
  if (is_hidden || (region->flag & RGN_FLAG_TOO_SMALL)) {
    region->winrct = region->overlap ? *overlap_remainder : *remainder;
    switch (alignment) {
      case RGN_ALIGN_TOP:
        region->winrct.ymin = region->winrct.ymax;
        break;
      case RGN_ALIGN_BOTTOM:
        region->winrct.ymax = region->winrct.ymin;
        break;
      case RGN_ALIGN_RIGHT:
        region->winrct.xmin = region->winrct.xmax;
        break;
      case RGN_ALIGN_LEFT:
        region->winrct.xmax = region->winrct.xmin;
        break;
      default:
        region->winrct.xmax = region->winrct.xmin;
        break;
    }
    BLI_rcti_sanitize(&region->winrct);
  }
  /* After non-overlapping region, all following overlapping regions fit within the remaining
   * space again. */
  if (!region->overlap) {
    *overlap_remainder = *remainder;
  }
  region->visible = !(region->flag & (RGN_FLAG_HIDDEN | RGN_FLAG_TOO_SMALL));
}

void area_calc_totrct(BKNode *screen_node, BKArea *area, const rcti *window_rect)
{
  if (!screen_node) { /* a lone area: the whole node, no border */
    BLI_rcti_init(&area->totrct, 0, area->node->w - 1, 0, area->node->h - 1);
  }
  else {
    const BKScreen *screen = SCREEN(screen_node);
    /* Padding around each area, except at window edges. */
    const short px = (short)BK_MAX((float)U_BORDER_WIDTH * UI_SCALE_FAC, UI_SCALE_FAC);
    /* Padding at window edges. Cannot be less than border width. */
    const short px_edge = (short)BK_MIN(UI_SCALE_FAC * 2.0f, (float)U_BORDER_WIDTH * UI_SCALE_FAC);
    area->totrct.xmin = area->v1->x;
    area->totrct.xmax = area->v4->x;
    area->totrct.ymin = area->v1->y;
    area->totrct.ymax = area->v2->y;
    /* Scale down totrct by the border size on all sides not at window edges. */
    if (screen->state != SCREENFULL && screen_node->nkids > 1 && screen->state == SCREENNORMAL) {
      area->totrct.xmin += (area->totrct.xmin > window_rect->xmin) ? px : px_edge;
      area->totrct.xmax -= (area->totrct.xmax < (window_rect->xmax - 1)) ? px : px_edge;
      area->totrct.ymin += (area->totrct.ymin > window_rect->ymin) ? px : px_edge;
      area->totrct.ymax -= (area->totrct.ymax < (window_rect->ymax - 1)) ? px : px_edge;
    }
    else if (area->full) { /* the temporary screen of one area */
      BLI_rcti_init(&area->totrct, window_rect->xmin, window_rect->xmax - 1, window_rect->ymin,
                    window_rect->ymax - 1);
    }
  }
  area->winx = BLI_rcti_size_x(&area->totrct) + 1;
  area->winy = BLI_rcti_size_y(&area->totrct) + 1;
}

/* -------------------------------------------------------------------- */
/** \name Action zones
 * \{ */

static AZone *azone_add(BKArea *area, short type)
{
  if (area->nazones == AREA_MAX_AZONES) {
    return NULL;
  }
  AZone *az = &area->azones[area->nazones++];
  memset(az, 0, sizeof(*az));
  az->type = type;
  return az;
}

static void area_azone_init(BKNode *screen_node, BKArea *area)
{
  area->nazones = 0;
  if (!screen_node || SCREEN(screen_node)->state != SCREENNORMAL ||
      SCREEN(screen_node)->resize_only)
  {
    return;
  }
  /* A taller zone on the left side, the height of the header, to make them easier to hit. The
   * others on the right are shorter to not interfere with scroll bars. */
  const rcti *t = &area->totrct;
  const int px = U_PIXELSIZE, hs = screen_headersize();
  const float coords[4][4] = {
      {t->xmin - px, t->ymin - px, t->xmin + UI_AZONESPOTW_LEFT, (float)(t->ymin + hs)},
      {t->xmax - UI_AZONESPOTW_RIGHT, t->ymin - px, t->xmax + px, t->ymin + UI_AZONESPOTH},
      {t->xmin - px, (float)(t->ymax - hs), t->xmin + UI_AZONESPOTW_LEFT, t->ymax + px},
      {t->xmax - UI_AZONESPOTW_RIGHT, t->ymax - UI_AZONESPOTH, t->xmax + px, t->ymax + px},
  };
  for (int i = 0; i < 4; i++) {
    AZone *az = azone_add(area, AZONE_AREA);
    if (az) {
      BLI_rcti_init(&az->rect, (int)coords[i][0], (int)coords[i][2], (int)coords[i][1],
                    (int)coords[i][3]);
    }
  }
}

static void fullscreen_azone_init(BKArea *area, const BKRegion *region)
{
  AZone *az = azone_add(area, AZONE_FULLSCREEN);
  if (!az) {
    return;
  }
  const int fade = (int)(6.5f * bk_unit()); /* AZONEFADEOUT */
  BLI_rcti_init(&az->rect, region->winrct.xmax - fade, region->winrct.xmax,
                region->winrct.ymax - fade, region->winrct.ymax);
}

static void region_azone_edge(AZone *az, const BKRegion *region)
{
  /* Narrow regions like headers need a smaller hit-space that does not interfere with content. */
  const bool is_narrow = region_is_header_any(region->type);
  const int pad_out = (int)((is_narrow ? 2.0f : 3.0f) * UI_SCALE_FAC);
  const int pad_in = (int)((is_narrow ? 1.0f : 4.0f) * UI_SCALE_FAC);
  const rcti *w = &region->winrct;
  switch (az->edge) {
    case AE_TOP_TO_BOTTOMRIGHT:
      BLI_rcti_init(&az->rect, w->xmin, w->xmax, w->ymax - pad_in, w->ymax + pad_out);
      break;
    case AE_BOTTOM_TO_TOPLEFT:
      BLI_rcti_init(&az->rect, w->xmin, w->xmax, w->ymin - pad_in, w->ymin + pad_out);
      break;
    case AE_LEFT_TO_TOPRIGHT:
      BLI_rcti_init(&az->rect, w->xmin - pad_out, w->xmin + pad_in, w->ymin, w->ymax);
      break;
    case AE_RIGHT_TO_TOPLEFT:
      BLI_rcti_init(&az->rect, w->xmax - pad_in, w->xmax + pad_out, w->ymin, w->ymax);
      break;
  }
}

/* region already made zero sized, in shape of edge */
static void region_azone_tab_plus(const BKArea *area, AZone *az, const BKRegion *region)
{
  const float edge_offset = 1.0f;
  const float tab_size_x = 1.0f * bk_unit(), tab_size_y = 0.5f * bk_unit();
  const rcti *w = &region->winrct;
  const int px = U_PIXELSIZE;
  switch (az->edge) {
    case AE_TOP_TO_BOTTOMRIGHT: {
      const int add = (w->ymax == area->totrct.ymin) ? 1 : 0;
      BLI_rcti_init(&az->rect, (int)(w->xmax - (edge_offset + 1.0f) * tab_size_x),
                    (int)(w->xmax - edge_offset * tab_size_x), w->ymax - px,
                    (int)(w->ymax - add + tab_size_y));
      break;
    }
    case AE_BOTTOM_TO_TOPLEFT:
      BLI_rcti_init(&az->rect, (int)(w->xmax - (edge_offset + 1.0f) * tab_size_x),
                    (int)(w->xmax - edge_offset * tab_size_x), (int)(w->ymin - tab_size_y),
                    w->ymin + px);
      break;
    case AE_LEFT_TO_TOPRIGHT:
      BLI_rcti_init(&az->rect, (int)(w->xmin - tab_size_y), w->xmin + px,
                    (int)(w->ymax - (edge_offset + 1.0f) * tab_size_x),
                    (int)(w->ymax - edge_offset * tab_size_x));
      break;
    case AE_RIGHT_TO_TOPLEFT:
      BLI_rcti_init(&az->rect, w->xmax - px, (int)(w->xmax + tab_size_y),
                    (int)(w->ymax - (edge_offset + 1.0f) * tab_size_x),
                    (int)(w->ymax - edge_offset * tab_size_x));
      break;
  }
}

static bool region_azone_edge_poll(const BKArea *area, const BKRegion *region, bool is_fullscreen)
{
  if (area->winy < (int)((float)screen_headersize() * 1.5f)) {
    return false;
  }
  const bool is_hidden = (region->flag & (RGN_FLAG_HIDDEN | RGN_FLAG_TOO_SMALL)) != 0;
  if (is_hidden && is_fullscreen) {
    return false;
  }
  if (!is_hidden && (region->type == RGN_TYPE_HEADER || region->type == RGN_TYPE_TOOL_HEADER)) {
    return false;
  }
  return true;
}

static void region_azones_add(const BKNode *screen_node, BKArea *area, BKNode *node,
                              BKRegion *region)
{
  const bool is_fullscreen = screen_node && SCREEN(screen_node)->state == SCREENFULL;
  /* Only display tab or icons when the header region is hidden (not the tool header). */
  if (region->type == RGN_TYPE_TOOL_HEADER || region->alignment == RGN_ALIGN_FLOAT) {
    return;
  }
  AZEdge edge;
  switch (region->alignment) {
    case RGN_ALIGN_TOP:
      edge = AE_BOTTOM_TO_TOPLEFT;
      break;
    case RGN_ALIGN_BOTTOM:
      edge = AE_TOP_TO_BOTTOMRIGHT;
      break;
    case RGN_ALIGN_RIGHT:
      edge = AE_LEFT_TO_TOPRIGHT;
      break;
    case RGN_ALIGN_LEFT:
      edge = AE_RIGHT_TO_TOPLEFT;
      break;
    default:
      return;
  }
  if (!region_azone_edge_poll(area, region, is_fullscreen)) {
    return;
  }
  AZone *az = azone_add(area, AZONE_REGION);
  if (!az) {
    return;
  }
  az->region = node;
  az->edge = (short)edge;
  if (region->flag & (RGN_FLAG_HIDDEN | RGN_FLAG_TOO_SMALL)) {
    region_azone_tab_plus(area, az, region);
  }
  else {
    region_azone_edge(az, region);
  }
}

AZone *area_actionzone_find_xy(BKArea *area, int x, int y)
{
  for (int i = 0; i < area->nazones; i++) {
    AZone *az = &area->azones[i];
    if (BLI_rcti_isect_pt(&az->rect, x, y)) {
      if (az->type == AZONE_FULLSCREEN) {
        /* fullscreen_click_rcti_init: only the corner icon takes the click */
        rcti click;
        BLI_rcti_init(&click, az->rect.xmax - bk_unit(), az->rect.xmax,
                      az->rect.ymax - bk_unit(), az->rect.ymax);
        if (!BLI_rcti_isect_pt(&click, x, y)) {
          continue;
        }
      }
      return az;
    }
  }
  return NULL;
}

/** \} */

void area_update_region_sizes(BKNode *area_node, BKNode *screen_node, const rcti *window_rect)
{
  BKArea *area = AREA(area_node);
  area_calc_totrct(screen_node, area, window_rect);

  BKNode *regions[RGN_TYPE_NUM];
  const int count = area_regions_ordered(area_node, regions, RGN_TYPE_NUM);
  rcti rect = area->totrct, overlap_rect = rect, main = rect;
  for (int i = 0; i < count; i++) {
    region_rect_calc(area, regions, i, count, &rect, &overlap_rect, &main);
  }
  area_azone_init(screen_node, area);

  /* Place the nodes: regions at their rects (a hidden one has no size), the main region in the
   * rest; children that are neither (a second region of a type) are not shown. */
  const BKNode *space = screen_node ? screen_node : area_node;
  for (int i = 0; i < area_node->nkids; i++) {
    BKNode *c = area_node->kids[i];
    bool placed = false;
    for (int k = 0; k < count && !placed; k++) {
      placed = regions[k] == c;
    }
    BKRegion *region = region_of(c);
    if (!placed) {
      bk_arrange(c, area_node->x, area_node->y, 0, 0);
      continue;
    }
    if (region) {
      const BKRect r = space_to_window(space, &region->winrct);
      if (region->visible) {
        bk_arrange(c, r.xmin, r.ymin, bk_rw(&r), bk_rh(&r));
      }
      else {
        bk_arrange(c, r.xmin, r.ymin, 0, 0);
      }
      region_azones_add(screen_node, area, c, region);
    }
    else {
      const BKRect r = space_to_window(space, &main);
      bk_arrange(c, r.xmin, r.ymin, BK_MAX(0, bk_rw(&r)), BK_MAX(0, bk_rh(&r)));
    }
  }
  if (screen_node && SCREEN(screen_node)->state == SCREENFULL && count > 0) {
    /* the exit icon in the main region's top-right corner */
    BKRegion corner = {0};
    corner.winrct = main;
    fullscreen_azone_init(area, &corner);
  }
}

int area_max_regionsize(const BKArea *area, BKNode *scale_node, const AZEdge edge)
{
  const BKRegion *scale_region = scale_node->rgn;
  int dist = (edge == AE_RIGHT_TO_TOPLEFT || edge == AE_LEFT_TO_TOPRIGHT) ?
                 BLI_rcti_size_x(&area->totrct) :
                 BLI_rcti_size_y(&area->totrct);
  /* Subtract the width of regions on opposite side: prevents dragging regions into other
   * opposite regions. */
  for (int i = 0; i < area->node->nkids; i++) {
    const BKRegion *region = region_of(area->node->kids[i]);
    if (!region || area->node->kids[i] == scale_node || !region->visible) {
      continue;
    }
    if ((scale_region->alignment == RGN_ALIGN_LEFT && region->alignment == RGN_ALIGN_RIGHT) ||
        (scale_region->alignment == RGN_ALIGN_RIGHT && region->alignment == RGN_ALIGN_LEFT))
    {
      dist -= region->winx;
    }
    else if ((scale_region->alignment == RGN_ALIGN_TOP &&
              (region->alignment == RGN_ALIGN_BOTTOM || region_is_header_any(region->type))) ||
             (scale_region->alignment == RGN_ALIGN_BOTTOM &&
              (region->alignment == RGN_ALIGN_TOP || region_is_header_any(region->type))))
    {
      dist -= region->winy;
    }
  }
  dist = (int)(dist / UI_SCALE_FAC);
  if (scale_region->maxsize > 0) {
    dist = BK_MIN(dist, scale_region->maxsize);
  }
  return dist;
}

static void region_changed(BKNode *area_node, BKNode *region_node)
{
  const BKRegion *region = region_node->rgn;
  bk_relayout(area_node);
  bk_invalidate(area_node);
  const int size = (region->alignment == RGN_ALIGN_LEFT || region->alignment == RGN_ALIGN_RIGHT) ?
                       region->sizex :
                       region->sizey;
  bk_emit(region_node, BK_EV_RESIZE, NULL, size, region_is_hidden(region));
  BKNode *screen_node = AREA_SCREEN(area_node);
  if (screen_node) {
    bk_emit(screen_node, BK_EV_CHANGE, NULL, 0, 0);
  }
}

void region_toggle_hidden(BKNode *area_node, BKNode *region_node)
{
  BKRegion *region = region_node->rgn;
  region->flag ^= RGN_FLAG_HIDDEN;
  region_changed(area_node, region_node);
}

void region_flip(BKNode *area_node, BKNode *region_node)
{
  BKRegion *region = region_node->rgn;
  switch (region->alignment) {
    case RGN_ALIGN_TOP:
      region->alignment = RGN_ALIGN_BOTTOM;
      break;
    case RGN_ALIGN_BOTTOM:
      region->alignment = RGN_ALIGN_TOP;
      break;
    case RGN_ALIGN_LEFT:
      region->alignment = RGN_ALIGN_RIGHT;
      break;
    case RGN_ALIGN_RIGHT:
      region->alignment = RGN_ALIGN_LEFT;
      break;
  }
  region_changed(area_node, region_node);
}
