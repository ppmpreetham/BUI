/* BUI Lua binding: BKWindow userdata.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "bui_internal.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Window methods                                                     */
/* ------------------------------------------------------------------ */

static int l_win_show(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  if (wud->win) BK_window_show(wud->win);
  return 0;
}

static int l_win_close(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  if (wud->win) BK_window_close(wud->win);
  return 0;
}

static int l_win_is_open(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  lua_pushboolean(L, wud->win ? BK_window_is_open(wud->win) : 0);
  return 1;
}

static int l_win_set_title(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  if (wud->win) BK_window_set_title(wud->win, luaL_checkstring(L, 2));
  return 0;
}

static int l_win_set_size(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  if (wud->win) {
    int w = (int)luaL_checkinteger(L, 2);
    int h = (int)luaL_checkinteger(L, 3);
    BK_window_set_size(wud->win, w, h);
  }
  return 0;
}

static int l_win_get_size(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  if (!wud->win) return 0;
  int w, h;
  BK_window_get_size(wud->win, &w, &h);
  lua_pushinteger(L, w);
  lua_pushinteger(L, h);
  return 2;
}

static int l_win_render_now(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  if (wud->win) BK_window_render_now(wud->win);
  return 0;
}

static int l_win_timer(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  if (wud->win) {
    int ms = (int)luaL_checkinteger(L, 2);
    lua_pushinteger(L, BK_window_timer(wud->win, ms));
    return 1;
  }
  return 0;
}

static int l_win_cancel_timer(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  if (wud->win) BK_window_cancel_timer(wud->win, (int)luaL_checkinteger(L, 2));
  return 0;
}

static int l_win_set_gpu(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  if (wud->win) BK_window_set_gpu(wud->win, (int)lua_toboolean(L, 2));
  return 0;
}

static int l_win_handle(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  if (wud->win) {
    lua_pushlightuserdata(L, BK_window_handle(wud->win));
    return 1;
  }
  return 0;
}

static int l_win_wm(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  if (!wud->win) return 0;
  const char *key = luaL_checkstring(L, 2);
  double a = luaL_optnumber(L, 3, 0);
  double b = luaL_optnumber(L, 4, 0);
  BK_window_wm(wud->win, key, a, b);
  return 0;
}

static int l_win_wm_get(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  if (!wud->win) return 0;
  double a, b;
  if (BK_window_wm_get(wud->win, luaL_checkstring(L, 2), &a, &b)) {
    lua_pushnumber(L, a);
    lua_pushnumber(L, b);
    return 2;
  }
  return 0;
}

static int l_win_pixel(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  if (!wud->win) return 0;
  int x = (int)luaL_checkinteger(L, 2);
  int y = (int)luaL_checkinteger(L, 3);
  BuiRaster *r = BK_window_raster(wud->win);
  if (!r) return 0;
  int rw = BUI_raster_width(r), rh = BUI_raster_height(r);
  if (x < 0 || y < 0 || x >= rw || y >= rh) {
    return luaL_error(L, "pixel (%d, %d) out of bounds (%dx%d)", x, y, rw, rh);
  }
  const uint32_t p = BUI_raster_pixels(r)[(size_t)y * rw + x];
  lua_pushinteger(L, (p >> 16) & 255);
  lua_pushinteger(L, (p >> 8) & 255);
  lua_pushinteger(L, p & 255);
  return 3;
}

static int l_win_frames(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  lua_pushinteger(L, wud->win ? BK_window_frames(wud->win) : 0);
  return 1;
}

static int l_win_gpu(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  lua_pushboolean(L, wud->win ? BK_window_gpu(wud->win) : 0);
  return 1;
}

static int l_win_request_frame(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  if (wud->win) BK_window_request_frame(wud->win, (int)lua_toboolean(L, 2));
  return 0;
}

/* ------------------------------------------------------------------ */
/* Callbacks                                                           */
/* ------------------------------------------------------------------ */

/* Call the Lua function in registry slot `ref` with the window itself (the userdata the script holds,
 * not a copy of it: a copy would be collected on its own and take the window with it). Returns its
 * result as a boolean, or `fallback` when there is no function or it raises an error (the message
 * goes to stderr: an error must not unwind through the window loop). */
static int call_window_fn(luabui_Window *wud, int ref, int fallback) {
  lua_State *L = wud->L;
  if (ref == LUA_NOREF || wud->self_ref == LUA_NOREF) return fallback;
  lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    return fallback;
  }
  lua_rawgeti(L, LUA_REGISTRYINDEX, wud->self_ref);
  if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
    const char *msg = lua_tostring(L, -1);
    fprintf(stderr, "bui: window callback: %s\n", msg ? msg : "error");
    lua_pop(L, 1);
    return fallback;
  }
  const int result = lua_toboolean(L, -1);
  lua_pop(L, 1);
  return result;
}

static int win_on_close_cb(void *data, BKWindow *win) {
  (void)win;
  luabui_Window *wud = (luabui_Window *)data;
  return call_window_fn(wud, wud->on_close_ref, 1) ? 1 : 0; /* true closes, false keeps it open */
}

static void win_on_frame_cb(void *data, BKWindow *win) {
  (void)win;
  luabui_Window *wud = (luabui_Window *)data;
  call_window_fn(wud, wud->on_frame_ref, 0);
}

/* BK_window_set_callbacks replaces them all: install the ones that have a Lua function. */
static void install_callbacks(luabui_Window *wud) {
  BKWindowCallbacks cb;
  memset(&cb, 0, sizeof(cb));
  if (wud->on_close_ref != LUA_NOREF || wud->on_frame_ref != LUA_NOREF) cb.data = wud;
  if (wud->on_close_ref != LUA_NOREF) cb.on_close = win_on_close_cb;
  if (wud->on_frame_ref != LUA_NOREF) cb.on_frame = win_on_frame_cb;
  BK_window_set_callbacks(wud->win, &cb);
}

/* window:on_close(fn) / window:on_frame(fn): set, or with no function clear, a callback. */
static void set_window_fn(lua_State *L, luabui_Window *wud, int *slot) {
  if (*slot != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, *slot);
    *slot = LUA_NOREF;
  }
  if (lua_isfunction(L, 2)) {
    lua_pushvalue(L, 2);
    *slot = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  const int wanted = wud->on_close_ref != LUA_NOREF || wud->on_frame_ref != LUA_NOREF;
  if (wanted && wud->self_ref == LUA_NOREF) {
    lua_pushvalue(L, 1);
    wud->self_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  } else if (!wanted && wud->self_ref != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, wud->self_ref);
    wud->self_ref = LUA_NOREF;
  }
  install_callbacks(wud);
}

static int l_win_set_on_close(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  if (wud->win) set_window_fn(L, wud, &wud->on_close_ref);
  return 0;
}

static int l_win_set_on_frame(lua_State *L) {
  luabui_Window *wud = CHECK_WIN(L, 1);
  if (wud->win) set_window_fn(L, wud, &wud->on_frame_ref);
  return 0;
}

/* The native window goes first, then what it used: the callbacks' functions, this userdata's own
 * reference, and the context. */
static void win_release(lua_State *L, luabui_Window *wud) {
  if (!wud->win) return;
  BK_window_free(wud->win);
  wud->win = NULL;
  int *refs[] = {&wud->on_close_ref, &wud->on_timer_ref, &wud->on_frame_ref, &wud->self_ref, &wud->ctx_ref};
  for (size_t i = 0; i < sizeof(refs) / sizeof(refs[0]); i++) {
    if (*refs[i] != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, *refs[i]);
    *refs[i] = LUA_NOREF;
  }
}

static int l_win_free(lua_State *L) {
  win_release(L, CHECK_WIN(L, 1));
  return 0;
}

static int l_win_gc(lua_State *L) {
  win_release(L, (luabui_Window *)luaL_checkudata(L, 1, LUABUI_WINDOW_MT));
  return 0;
}

static int l_win_tostring(lua_State *L) {
  luabui_Window *wud = (luabui_Window *)luaL_checkudata(L, 1, LUABUI_WINDOW_MT);
  lua_pushfstring(L, "bui.Window (%p)", (void *)(wud->win ? wud->win : NULL));
  return 1;
}

/* ------------------------------------------------------------------ */
/* Registration                                                       */
/* ------------------------------------------------------------------ */

static const luaL_Reg win_methods[] = {
  {"show",         l_win_show},
  {"close",        l_win_close},
  {"is_open",      l_win_is_open},
  {"set_title",    l_win_set_title},
  {"set_size",     l_win_set_size},
  {"get_size",     l_win_get_size},
  {"render_now",   l_win_render_now},
  {"timer",        l_win_timer},
  {"cancel_timer", l_win_cancel_timer},
  {"set_gpu",      l_win_set_gpu},
  {"gpu",          l_win_gpu},
  {"request_frame", l_win_request_frame},
  {"handle",       l_win_handle},
  {"pixel",        l_win_pixel},
  {"frames",       l_win_frames},
  {"wm",           l_win_wm},
  {"wm_get",       l_win_wm_get},
  {"on_close",     l_win_set_on_close},
  {"on_frame",     l_win_set_on_frame},
  {"free",         l_win_free},
  {"__gc",         l_win_gc},
  {"__tostring",   l_win_tostring},
  {NULL, NULL}
};

void luabui_register_window(lua_State *L) {
  luaL_newmetatable(L, LUABUI_WINDOW_MT);
  luaL_setfuncs(L, win_methods, 0);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  lua_pop(L, 1);
}
