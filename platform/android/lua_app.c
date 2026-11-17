/* A Lua app: assets/main.lua is an ordinary BUI Lua script (it opens its window with bui.open or
 * bui.Window and runs it with bui.run, as on a desktop).
 *
 * The Lua binding (bindings/lua) runs unchanged; this file is what bui-lua's main.c is on a
 * desktop: it owns the Lua state and finds `require`d files in the APK's assets (bui.lua, your own
 * modules). A script error is shown on screen, not just logged.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <android/asset_manager.h>
#include <android/log.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdlib.h>
#include <string.h>

#include "BUI_kit.h"
#include "bui_app.h"

int luaopen__bui(lua_State *L);

/* Load assets/<name> (dots in a module name are folders) as a chunk; 0 when there is none. */
static int load_asset(lua_State *L, const char *path)
{
  AAssetManager *assets = (AAssetManager *)BK_android_assets(); /* NULL before an app is attached */
  if (!assets) {
    return 0;
  }
  AAsset *a = AAssetManager_open(assets, path, AASSET_MODE_BUFFER);
  if (!a) {
    return 0;
  }
  const char *code = (const char *)AAsset_getBuffer(a); /* NULL when it cannot be mapped */
  if (!code) {
    AAsset_close(a);
    return 0;
  }
  const int status = luaL_loadbuffer(L, code, (size_t)AAsset_getLength(a), path);
  AAsset_close(a);
  return status == LUA_OK ? 1 : -1; /* -1: the error message is on the stack */
}

/* package.searchers entry: `require("a.b")` reads assets/a/b.lua */
static int asset_searcher(lua_State *L)
{
  char path[256];
  const char *name = luaL_checkstring(L, 1);
  snprintf(path, sizeof(path), "%s.lua", name);
  for (char *c = path; *c; c++) {
    *c = *c == '.' && strcmp(c, ".lua") != 0 ? '/' : *c;
  }
  const int found = load_asset(L, path);
  if (found > 0) {
    lua_pushstring(L, path);
    return 2;
  }
  if (found < 0) {
    return luaL_error(L, "error loading '%s': %s", path, lua_tostring(L, -1));
  }
  lua_pushfstring(L, "no asset '%s'", path);
  return 1;
}

/* The script failed: say why on screen (the log has it too). */
static void show_error(const char *message)
{
  __android_log_print(ANDROID_LOG_ERROR, "bui", "lua: %s", message);
  BKNode *page = BK_node_new("column");
  BK_set_num(page, "padding", 12);
  BKNode *label = BK_node_new("label");
  BK_set_str(label, "text", message);
  BK_node_add(page, label, -1);
  BKContext *ctx = BK_context_new(page);
  BKWindow *win = BK_window_new(ctx, "BUI", 360, 720);
  BK_window_show(win);
  BK_window_run();
  BK_window_free(win);
  BK_context_free(ctx);
}

/* The error on top of the stack as text: a script may raise a table or nil. */
static const char *error_text(lua_State *L)
{
  const char *m = lua_tostring(L, -1);
  return m ? m : lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, -1));
}

void bui_app_main(void)
{
  lua_State *L = luaL_newstate();
  if (!L) {
    show_error("lua: out of memory");
    return;
  }
  luaL_openlibs(L);
  luaL_requiref(L, "_bui", luaopen__bui, 1); /* what bui.lua requires first */
  lua_pop(L, 1);
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "searchers");
  const int n = (int)luaL_len(L, -1);
  lua_pushcfunction(L, asset_searcher);
  lua_rawseti(L, -2, n + 1);
  lua_pop(L, 2);

  const int loaded = load_asset(L, "main.lua");
  if (loaded <= 0) {
    show_error(loaded < 0 ? error_text(L) : "assets/main.lua is missing");
  }
  else if (lua_pcall(L, 0, 0, 0) != LUA_OK) { /* runs until the window closes */
    show_error(error_text(L));
  }
  lua_close(L);
}
