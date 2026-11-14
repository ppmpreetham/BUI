/* Screens, areas and regions: Blender's editors/screen for the kit.
 *
 * A "screen" node is Blender's bScreen: its children are "area" nodes laid
 * out on a graph of shared vertices and edges (ScrVert, ScrEdge), so splits,
 * joins, docking and moving aligned edges together work as in Blender. An
 * "area" (ScrArea) lays its children out as regions (ARegion): a child with a
 * region type takes a side of the area, and the one child without is the main
 * region that fills the rest. An area works on its own too (outside a
 * screen), as a panel with resizable sidebars.
 *
 *  screen_geometry.c  vertices and edges (screen_geometry.cc, BKE_screen_*)
 *  screen_edit.c      split, join, close, swap, dock (screen_edit.cc)
 *  area.c             region layout and action zones (area.cc)
 *  screen_ops.c       the interactive operators (screen_ops.cc)
 *  screen_draw.c      edges, highlights and previews (screen_draw.cc)
 *  screen_layout.c    the layout as text, to save and restore
 *  screen_node.c      the "screen" and "area" node classes and the C API
 *
 * Coordinates: inside this module everything is Blender's: pixels relative to
 * the screen's (or a lone area's) bottom-left corner, y up, rectangles with
 * inclusive maxima (rcti, as totrct and winrct). screen_space_* convert at
 * the node boundary.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "../kit_intern.h" /* first: stdbool for the blenlib headers */

#include "BLI_rect.h"
#include "BLI_listbase.h"

/* -------------------------------------------------------------------- */
/** \name Constants (DNA_screen_types.h, screen_intern.hh, UI_interface_c.hh)
 * \{ */

#define AREAGRID 1
#define AREAMINX 29
#define HEADER_PADDING_Y 6
#define HEADERY (20 + HEADER_PADDING_Y)
#define UI_SCALE_FAC bk_scale_fac
#define UI_UNIT_X bk_unit()
#define UI_UNIT_Y bk_unit()
#define U_PIXELSIZE ((int)bk_px())
#define U_BORDER_WIDTH 2 /* U.border_width (versioning_userdef.cc default) */
#define EDITORRADIUS (6.0f * UI_SCALE_FAC)
#define BORDERPADDING (U_BORDER_WIDTH * UI_SCALE_FAC + 3.0f * UI_SCALE_FAC)
#define AREAJOINTOLERANCEX (AREAMINX * UI_SCALE_FAC)
#define AREAJOINTOLERANCEY (HEADERY * UI_SCALE_FAC)
#define EDGE_ALIGN_TOLERANCE (7 * UI_SCALE_FAC)
#define AREA_CLOSE_FADEOUT 0.15f
#define AREA_DOCK_FADEIN 0.15f
#define AREA_MOVE_LINE_FADEIN 0.1f
#define AREA_MOVE_LINE_FADEOUT 0.15f
#define UI_HEADER_OFFSET (8.0f * UI_SCALE_FAC) /* USER_AREA_CORNER_HANDLE off */
#define UI_AZONESPOTW_LEFT UI_HEADER_OFFSET
#define UI_AZONESPOTW_RIGHT (8.0f * UI_SCALE_FAC)
#define UI_AZONESPOTH (0.6f * bk_unit())
#define ICON_DEFAULT_HEIGHT_TOOLBAR 32
#define UI_TOOLBAR_COLUMN (1.25f * ICON_DEFAULT_HEIGHT_TOOLBAR)
#define UI_TOOLBAR_MARGIN (0.5f * ICON_DEFAULT_HEIGHT_TOOLBAR)
#define UI_TOOLBAR_WIDTH (UI_TOOLBAR_MARGIN + UI_TOOLBAR_COLUMN)
#define UI_SIDEBAR_PANEL_WIDTH 280
#define UI_NAVIGATION_REGION_WIDTH 160
#define UI_PANEL_CATEGORY_MIN_WIDTH 26.0f
#define UI_PANEL_CATEGORY_MIN_SNAP_WIDTH 90.0f

int screen_headersize(void); /* ED_area_headersize */

/** \} */

/* -------------------------------------------------------------------- */
/** \name Screen geometry (DNA_screen_types.h)
 * \{ */

typedef struct ScrVert {
  struct ScrVert *next, *prev, *newv;
  short x, y; /* vec */
  short flag, editflag;
} ScrVert;

typedef struct ScrEdge {
  struct ScrEdge *next, *prev;
  ScrVert *v1, *v2;
  short flag;
} ScrEdge;

typedef enum eScreenDir {
  SCREEN_DIR_NONE = -1,
  SCREEN_DIR_W = 0,
  SCREEN_DIR_N = 1,
  SCREEN_DIR_E = 2,
  SCREEN_DIR_S = 3,
} eScreenDir;

#define SCREEN_DIR_IS_VERTICAL(dir) ((dir) == SCREEN_DIR_N || (dir) == SCREEN_DIR_S)
#define SCREEN_DIR_IS_HORIZONTAL(dir) ((dir) == SCREEN_DIR_W || (dir) == SCREEN_DIR_E)

typedef enum eScreenAxis {
  SCREEN_AXIS_H = 'h',
  SCREEN_AXIS_V = 'v',
} eScreenAxis;

typedef enum AreaDockTarget {
  DOCK_NONE = -1,
  DOCK_LEFT = BK_DOCK_LEFT,
  DOCK_RIGHT = BK_DOCK_RIGHT,
  DOCK_TOP = BK_DOCK_TOP,
  DOCK_BOTTOM = BK_DOCK_BOTTOM,
  DOCK_CENTER = BK_DOCK_CENTER,
} AreaDockTarget;

/* bScreen.state */
enum { SCREENNORMAL = 0, SCREENMAXIMIZED = 1, SCREENFULL = 2 };

/** \} */

/* -------------------------------------------------------------------- */
/** \name Regions (ARegion, ARegionType)
 * \{ */

/* eRegion_Type, in the order regions lay out (Blender's regionbase order of a space). */
typedef enum eRegionType {
  RGN_TYPE_HEADER = 0,
  RGN_TYPE_TOOL_HEADER,
  RGN_TYPE_FOOTER,
  RGN_TYPE_ASSET_SHELF,
  RGN_TYPE_CHANNELS,
  RGN_TYPE_NAV_BAR,
  RGN_TYPE_TOOLS,
  RGN_TYPE_UI,
  RGN_TYPE_EXECUTE,
  RGN_TYPE_HUD,
  RGN_TYPE_WINDOW, /* the main region: no type set */
  RGN_TYPE_NUM
} eRegionType;

enum {
  RGN_ALIGN_NONE = 0,
  RGN_ALIGN_TOP = 1,
  RGN_ALIGN_BOTTOM = 2,
  RGN_ALIGN_LEFT = 3,
  RGN_ALIGN_RIGHT = 4,
  RGN_ALIGN_FLOAT = 7,
};

enum {
  RGN_FLAG_HIDDEN = 1 << 0,
  RGN_FLAG_TOO_SMALL = 1 << 1,
  RGN_FLAG_NO_USER_RESIZE = 1 << 4,
  RGN_FLAG_SIZE_CLAMP_X = 1 << 5,
  RGN_FLAG_SIZE_CLAMP_Y = 1 << 6,
};

typedef struct BKRegion BKRegion;

/* What ARegionType gives each region type here. */
typedef struct RegionTypeInfo {
  const char *name;   /* the "region" property value */
  short alignment;    /* default side */
  short align_mask;   /* sides it may take (1 << RGN_ALIGN_*) */
  short prefsizex, prefsizey;
  bool overlap;       /* ED_region_is_overlap (USER_REGION_OVERLAP on) */
  bool category_tabs; /* BKE_regiontype_uses_category_tabs */
  bool fixed_height;  /* headers and footers: their height is the header size */
  int (*snap_size)(const BKRegion *region, int size, int axis);
} RegionTypeInfo;

const RegionTypeInfo *region_type_info(int type);
int region_type_from_name(const char *name); /* -1: unknown */

/* ARegion: the region state of an area's child node (BKNode.rgn). */
struct BKRegion {
  short type; /* eRegionType */
  short alignment;
  short flag, flagfullscreen;
  short sizex, sizey; /* unscaled; 0: the type's preferred size */
  short maxsize;      /* unscaled; 0: none */
  bool overlap, overlap_off; /* overlap_off: the "overlap" property turned it off */
  bool visible;
  rcti winrct;
  int winx, winy;
  int offset_x, offset_y; /* floating regions: moved by the user (runtime->offset_x/y) */
  char key[24];           /* toggle hotkey ("n", "t", "ctrl+]"), "" none */
};

/** \} */

/* -------------------------------------------------------------------- */
/** \name Areas (ScrArea) and action zones (AZone)
 * \{ */

enum { AZONE_AREA = 1, AZONE_REGION, AZONE_FULLSCREEN, AZONE_REGION_QUAD };

typedef enum AZEdge {
  AE_RIGHT_TO_TOPLEFT,
  AE_LEFT_TO_TOPRIGHT,
  AE_TOP_TO_BOTTOMRIGHT,
  AE_BOTTOM_TO_TOPLEFT,
} AZEdge;

typedef struct AZone {
  short type;
  short edge;     /* AZEdge, AZONE_REGION */
  BKNode *region; /* AZONE_REGION */
  rcti rect;
  float alpha;
} AZone;

#define AREA_MAX_AZONES 20

typedef struct BKArea {
  BKNode *node;
  ScrVert *v1, *v2, *v3, *v4; /* bottom-left, top-left, top-right, bottom-right */
  rcti totrct;
  int winx, winy;
  AZone azones[AREA_MAX_AZONES];
  int nazones;
  float quadview_ratio[2];
  short full; /* the screen shows only this area: SCREENMAXIMIZED / SCREENFULL */
  /* region scale in progress (region_scale_*) */
  struct {
    BKNode *region;
    AZEdge edge;
    int origval, maxsize, orig_xy[2];
    bool active;
  } rmd;
  BKNode *menu_region; /* the region a context menu was opened over */
} BKArea;

#define AREA(n) ((BKArea *)(n)->ext)

/** \} */

/* -------------------------------------------------------------------- */
/** \name Screens (bScreen) and their operators in progress
 * \{ */

enum { SNAP_NONE = 0, SNAP_AREAGRID, SNAP_FRACTION_AND_ADJACENT, SNAP_BIGGER_SMALLER_ONLY };

enum { OP_NONE = 0, OP_MOVE, OP_JOIN, OP_SWAP, OP_WINDOW, OP_SPLIT };

typedef struct BKScreen {
  ListBase vertbase, edgebase; /* ScrVert, ScrEdge */
  short state;                 /* SCREENNORMAL ... */
  BKNode *detaching;           /* the area the host is removing (node_detach) */
  bool resize_only;            /* "ops": "resize": only edges move (QSplitter, PanedWindow) */
  bool host_content;           /* "transparent": the host draws the region contents */
  uint64_t geom_sig;           /* host content: the areas' and regions' rects last reported */
  BKNode *active;              /* the area the pointer is in (its outline is brighter) */
  char *layout;                /* BK_get_str(screen, "layout") */
  eScreenAxis fade_axis;       /* the edge highlight fading (node->anim, AREA_MOVE_LINE_FADE*) */
  /* the operator in progress */
  int op;
  struct {                     /* sAreaMoveData */
    int bigger, smaller, origval;
    eScreenAxis dir_axis;
    int snap_type;
    bool can_extend, extending;
    int x, y; /* where it started */
    BKNode *area1, *area2;
  } md;
  struct {                     /* sActionzoneData + sAreaJoinData */
    BKNode *sa1, *sa2;
    int x, y;
    int modifier;              /* 0 move/split/join/dock, 1 Shift: window, 2 Ctrl: swap */
    eScreenDir dir;
    eScreenAxis split_dir;
    AreaDockTarget dock_target;
    float factor, split_fac;
    int current_x, current_y;  /* window pixels (the drag hint) */
    int start_x, start_y;
    bool outside;              /* outside the screen: a new window */
    bool modal;                /* started from a menu: runs until a click (bk_begin_modal) */
  } jd;
  /* the edge menu's context (screen_area_options_invoke) */
  int menu_x, menu_y;
  BKNode *menu_sa1, *menu_sa2;
} BKScreen;

#define SCREEN(n) ((BKScreen *)(n)->ext)

extern const BKClass bk_cls_screen, bk_cls_area;
#define IS_SCREEN(n) ((n) && (n)->cls == &bk_cls_screen)
#define IS_AREA(n) ((n) && (n)->cls == &bk_cls_area)
#define AREA_SCREEN(n) (IS_SCREEN((n)->parent) ? (n)->parent : NULL)

/* Areas of the screen in order (the screen's children). */
#define SCREEN_AREAS_ITER(scr, var) \
  for (int var##_i = 0; var##_i < (scr)->nkids; var##_i++) \
    for (BKArea *var = AREA((scr)->kids[var##_i]); var; var = NULL)

/** \} */

/* -------------------------------------------------------------------- */
/** \name screen_geometry.c
 * \{ */

int screen_geom_area_height(const BKArea *area);
int screen_geom_area_width(const BKArea *area);
ScrVert *screen_geom_vertex_add(BKScreen *screen, short x, short y);
ScrEdge *screen_geom_edge_add(BKScreen *screen, ScrVert *v1, ScrVert *v2);
bool screen_geom_edge_is_horizontal(const ScrEdge *se);
ScrEdge *screen_geom_find_active_scredge(const BKScreen *screen, const rcti *bounds, int mx, int my,
                                         int safety);
void screen_geom_vertices_scale(BKNode *screen_node, const rcti *screen_rect);
short screen_geom_find_area_split_point(const BKArea *area, const rcti *window_rect,
                                        eScreenAxis dir_axis, float fac);
void screen_geom_select_connected_edge(BKScreen *screen, ScrEdge *edge);
bool screen_geom_edge_can_extend(BKScreen *screen, ScrEdge *edge);
void screen_geom_select_extended_edge(BKScreen *screen, ScrEdge *edge);
void screen_geom_edge_aligned_merge(BKNode *screen_node, ScrEdge *edge);
ScrEdge *screen_find_edge(const BKScreen *screen, ScrVert *v1, ScrVert *v2);
void screen_remove_double_scrverts(BKNode *screen_node);
void screen_remove_double_scredges(BKScreen *screen);
void screen_remove_unused_scredges(BKNode *screen_node);
void screen_remove_unused_scrverts(BKScreen *screen);
void screen_geom_free(BKScreen *screen);
BKNode *screen_find_area_xy(BKNode *screen_node, int x, int y); /* BKE_screen_find_area_xy */

/** \} */

/* -------------------------------------------------------------------- */
/** \name screen_edit.c
 * \{ */

void screen_window_rect(const BKNode *screen_node, rcti *r_rect); /* WM_window_rect_calc */
void screen_ensure_verts(BKNode *screen_node); /* the first area's corners, before any layout */
void screen_area_attach(BKNode *screen_node, BKNode *area_node, ScrVert *v1, ScrVert *v2,
                        ScrVert *v3, ScrVert *v4);
BKNode *area_split(BKNode *screen_node, BKNode *area_node, eScreenAxis dir_axis, float fac);
eScreenDir area_getorientation(const BKArea *sa_a, const BKArea *sa_b);
void area_getoffsets(const BKArea *sa_a, const BKArea *sa_b, eScreenDir dir, int *r_offset1,
                     int *r_offset2);
int screen_area_join(BKNode *screen_node, BKNode *sa1, BKNode *sa2);
bool screen_area_close(BKNode *screen_node, BKNode *area, BKNode *not_area);
void area_swapspace(BKNode *screen_node, BKNode *sa1, BKNode *sa2);
bool area_docking_apply(BKNode *screen_node, BKNode *sa1, BKNode *sa2, AreaDockTarget target,
                        float factor);
void screen_state_toggle(BKNode *screen_node, BKNode *area_node, short state);
/* The area left the screen: tell the host, free it (unless the host owns it). */
void screen_area_free(BKNode *screen_node, BKNode *area_node);
/* The layout changed: tell the host (BK_EV_CHANGE on the screen) and lay out again. */
void screen_changed(BKNode *screen_node);

/** \} */

/* -------------------------------------------------------------------- */
/** \name area.c
 * \{ */

BKRegion *region_ensure(BKNode *n);
BKRegion *region_of(const BKNode *n);          /* NULL: not a region child of an area */
bool region_wanted(const BKNode *n);           /* it is a region (or will be, in an area) */
BKNode *area_region_find(BKNode *area_node, int type);
void area_calc_totrct(BKNode *screen_node, BKArea *area, const rcti *window_rect);
void area_update_region_sizes(BKNode *area_node, BKNode *screen_node, const rcti *window_rect);
int area_max_regionsize(const BKArea *area, BKNode *scale_region, AZEdge edge);
void region_toggle_hidden(BKNode *area_node, BKNode *region_node);
void region_flip(BKNode *area_node, BKNode *region_node);
AZone *area_actionzone_find_xy(BKArea *area, int x, int y);
bool region_is_hidden(const BKRegion *region);
/* Area coordinates <-> node (window) coordinates. `space` is the screen, or the lone area. */
void space_from_window(const BKNode *space, int wx, int wy, int *r_x, int *r_y);
BKRect space_to_window(const BKNode *space, const rcti *r);
const BKNode *area_space(const BKNode *area_node);

/** \} */

/* -------------------------------------------------------------------- */
/** \name screen_draw.c
 * \{ */

void screen_draw_edges(BKNode *screen_node);
void screen_draw_move_highlight(BKNode *screen_node, eScreenAxis dir_axis, float anim_factor);
void screen_draw_region_scale_highlight(const BKNode *space, const BKRegion *region);
void screen_draw_join_highlight(BKNode *screen_node, BKArea *sa1, BKArea *sa2, eScreenDir dir);
void screen_draw_dock_preview(BKNode *screen_node, BKArea *source, BKArea *target,
                              AreaDockTarget dock_target, float factor);
void screen_draw_split_preview(BKNode *screen_node, BKArea *area, eScreenAxis dir_axis,
                               float factor);
void screen_draw_area_drag_tip(BKNode *screen_node, int x, int y, const char *hint,
                               const BKArea *source);
void area_draw_azones(const BKNode *space, BKArea *area);
void area_draw_regions_back(BKNode *area_node);

/** \} */

/* -------------------------------------------------------------------- */
/** \name screen_ops.c
 * \{ */

int screen_op_press(BKNode *screen_node, BKEvent *e);
int screen_op_drag(BKNode *screen_node, BKEvent *e);
int screen_op_release(BKNode *screen_node, BKEvent *e);
void screen_op_cancel(BKNode *screen_node);
int screen_cursor_at(BKNode *screen_node, int x, int y);
int screen_context_items(BKNode *screen_node, BKMenuSpec *items, int max);
int area_op_press(BKNode *area_node, BKEvent *e);
int area_op_drag(BKNode *area_node, BKEvent *e);
int area_op_release(BKNode *area_node, BKEvent *e);
void area_op_cancel(BKNode *area_node);
int area_cursor_at(BKNode *area_node, int x, int y);
int area_context_items(BKNode *area_node, BKMenuSpec *items, int max);
int area_key(BKNode *area_node, BKEvent *e);

/** \} */

/* -------------------------------------------------------------------- */
/** \name screen_layout.c
 * \{ */

const char *screen_layout_get(BKNode *screen_node);
int screen_layout_set(BKNode *screen_node, const char *text);

/** \} */
