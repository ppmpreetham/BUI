/* BUI Lua binding: shared internal types and helpers.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "BUI_kit.h"
#include "bui_c_api.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32 /* no setenv in the Windows C runtime */
#  define setenv(name, value, overwrite) _putenv_s((name), (value))
#endif

/* ------------------------------------------------------------------ */
/* Userdata wrapper structs                                           */
/* ------------------------------------------------------------------ */

typedef struct {
  BKNode *node;
} luabui_Node;

typedef struct {
  BKContext *ctx;
} luabui_Context;

typedef struct {
  BKWindow *win;
  lua_State *L;
  int on_close_ref;  /* LUA_NOREF if not set */
  int on_timer_ref;  /* LUA_NOREF if not set */
  int on_frame_ref;  /* LUA_NOREF if not set */
  int on_focus_ref;  /* LUA_NOREF if not set */
  int self_ref;      /* this userdata, while a callback needs to be given it: LUA_NOREF otherwise */
  int ctx_ref;       /* the Context the window shows: it must outlive the window */
  int depth;         /* callbacks of this window running right now: the backend is using the window */
  BKWindow *zombie;  /* a window freed from inside a callback: closed, its memory freed once they are done */
} luabui_Window;

typedef struct {
  BKTreeItem *item;
} luabui_TreeItem;

typedef struct {
  BKImBuf *ib;
} luabui_ImBuf;

typedef struct {
  BKVideo *video;
  int listener_ref;  /* the state listener function: LUA_NOREF if not set */
  int self_ref;      /* this userdata, while the listener runs: LUA_NOREF otherwise */
} luabui_Video;

typedef struct {
  BKAnim *anim;
} luabui_Anim;

/* ------------------------------------------------------------------ */
/* Registry key names (metatables + node map)                         */
/* ------------------------------------------------------------------ */

#define LUABUI_NODE_MT      "bui.Node"
#define LUABUI_CONTEXT_MT   "bui.Context"
#define LUABUI_WINDOW_MT    "bui.Window"
#define LUABUI_TREEITEM_MT  "bui.TreeItem"
#define LUABUI_IMBUF_MT     "bui.ImBuf"
#define LUABUI_VIDEO_MT     "bui.Video"
#define LUABUI_ANIM_MT      "bui.Anim"
#define LUABUI_NODE_MAP      "bui._node_map"
#define LUABUI_TREEITEM_MAP  "bui._treeitem_map"
#define LUABUI_CTX_MAP       "bui._ctx_map"
#define LUABUI_TIMER_QUEUE   "bui._timer_queue"

/* ------------------------------------------------------------------ */
/* Macros                                                             */
/* ------------------------------------------------------------------ */

#define CHECK_NODE(L, idx)    ((luabui_Node *)luaL_checkudata(L, idx, LUABUI_NODE_MT))
#define CHECK_CTX(L, idx)     ((luabui_Context *)luaL_checkudata(L, idx, LUABUI_CONTEXT_MT))
#define CHECK_WIN(L, idx)     ((luabui_Window *)luaL_checkudata(L, idx, LUABUI_WINDOW_MT))
#define CHECK_TREEITEM(L, idx)((luabui_TreeItem *)luaL_checkudata(L, idx, LUABUI_TREEITEM_MT))
#define CHECK_IMBUF(L, idx)   ((luabui_ImBuf *)luaL_checkudata(L, idx, LUABUI_IMBUF_MT))
#define CHECK_VIDEO(L, idx)   ((luabui_Video *)luaL_checkudata(L, idx, LUABUI_VIDEO_MT))
#define CHECK_ANIM(L, idx)    ((luabui_Anim *)luaL_checkudata(L, idx, LUABUI_ANIM_MT))

/* ------------------------------------------------------------------ */
/* Registration functions                                             */
/* ------------------------------------------------------------------ */

void luabui_register_node(lua_State *L);
void luabui_register_context(lua_State *L);
void luabui_register_window(lua_State *L);
void luabui_register_treeitem(lua_State *L);
void luabui_register_imbuf(lua_State *L);
void luabui_register_video(lua_State *L);
void luabui_register_anim_ud(lua_State *L);

/* Push the Lua wrapper of a BKNode* (made on first use), or nil for NULL. Returns 1 if on the stack. */
int luabui_push_node(lua_State *L, BKNode *node);

/* ------------------------------------------------------------------ */
/* Node map helpers (BKNode* <-> Lua userdata)                        */
/* ------------------------------------------------------------------ */

/* Store node -> userdata in the weak-keyed node map. */
void luabui_map_node(lua_State *L, BKNode *node, int ud_idx);

/* Look up userdata for a BKNode*. Pushes userdata on stack and returns 1
 * if found; returns 0 and pushes nothing if not found. Caller must pop
 * with lua_pop(L, 2) when found (returns 1). */
int luabui_lookup_node(lua_State *L, BKNode *node);

/* Remove a node from the map. */
void luabui_unmap_node(lua_State *L, BKNode *node);
/* The wrappers of a subtree the kit freed (or is about to) no longer point at it. */
void luabui_invalidate_subtree(lua_State *L, BKNode *n);
/* 1 while `n` is an area whose close handler is running, or an ancestor of one (its screen). The
 * screen is still working with those nodes: freeing one from the handler would pull it from under
 * the screen, so node:free() refuses (see l_node_free). */
int luabui_in_close_notice(const BKNode *n);

/* ------------------------------------------------------------------ */
/* Tree item map helpers (BKTreeItem* <-> Lua userdata)               */
/* ------------------------------------------------------------------ */

void luabui_map_treeitem(lua_State *L, BKTreeItem *item, int ud_idx);
int  luabui_lookup_treeitem(lua_State *L, BKTreeItem *item);
void luabui_unmap_treeitem(lua_State *L, BKTreeItem *item);

/* Nullify all tree item userdata whose C pointer has been freed. */
void luabui_nullify_treeitem(lua_State *L, BKTreeItem *item);

/* ------------------------------------------------------------------ */
/* Context map helpers (BKContext* <-> Lua userdata)                   */
/* ------------------------------------------------------------------ */

void luabui_map_ctx(lua_State *L, BKContext *ctx, int ud_idx);
int  luabui_lookup_ctx(lua_State *L, BKContext *ctx);
void luabui_unmap_ctx(lua_State *L, BKContext *ctx);

/* ------------------------------------------------------------------ */
/* Host callbacks (implemented in bui_context.c)                      */
/* ------------------------------------------------------------------ */

extern int luabui_host_requested;

void luabui_init_host_callbacks(lua_State *L, BKContext *ctx);

/* A callback's error (on top of the stack) to stderr, popped: errors never unwind through
 * the kit. */
void luabui_report_error(lua_State *L);

/* bui.gsap and bui.motion (bui_anim.c) into the module table on top of the stack. */
void luabui_register_anim(lua_State *L);

/* Install the engine ref hooks against this state (called from luaopen). */
void luabui_set_ref_state(lua_State *L);

/* The module's Lua state (set at load): callbacks that run on the UI thread call into it. */
lua_State *luabui_module_state(void);

/* ------------------------------------------------------------------ */
/* Known numeric property names (for node:get() dispatch)             */
/* ------------------------------------------------------------------ */

int luabui_is_numeric_key(const char *key);

/* Build a BKMenuSpec array from a Lua items table at stack index `idx`,
 * including recursive submenu processing.  The array is allocated as a
 * Lua userdata (traced by the GC).  Returns the number of items. */
int luabui_build_menuspec(lua_State *L, int idx, BKMenuSpec **out);



/* Lua module entry point (defined in bui_lua.c) */
/* The module's entry points are exported by name: Lua's own LUAMOD_API is a dllimport in a module that
 * does not define LUA_LIB, and a function cannot be defined as one. */
#if defined(_WIN32)
#  define BUI_LUA_ENTRY __declspec(dllexport)
#elif defined(__GNUC__)
#  define BUI_LUA_ENTRY __attribute__((visibility("default")))
#else
#  define BUI_LUA_ENTRY
#endif

BUI_LUA_ENTRY int luaopen_bui(lua_State *L);
BUI_LUA_ENTRY int luaopen__bui(lua_State *L); /* the loadable module's name: _bui */

/* Factory functions (defined in bui_image.c) */
int l_image_decode(lua_State *L);
int l_image_load(lua_State *L);
int l_image_from_rgba(lua_State *L);
int l_image_from_argb(lua_State *L);
int l_video_new(lua_State *L);
int l_anim_open(lua_State *L);
