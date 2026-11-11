-- Windows: a window keeps its context alive, on_close / on_frame take functions, free() is safe twice.
-- Skipped where there is no window system (CI without a display). The window is never shown.
-- SPDX-License-Identifier: GPL-2.0-or-later

local function check(cond, msg)
  if not cond then io.stderr:write("FAIL: " .. msg .. "\n"); os.exit(1) end
end

local bui = require("bui")
if os.getenv("BUI_HEADLESS") == "1" or not bui.window_supported() then
  io.write("SKIP: test_window.lua (no window system)\n")
  os.exit(0)
end

local function make()
  local root = bui.Node("frame")
  local ctx = bui.Context(root)
  local win = bui.Window(ctx, "bui-lua-window-test", 200, 100)
  if not win then return nil end
  return win -- the context goes out of scope here: the window has to keep it alive
end

local win = make()
if not win then
  io.write("SKIP: test_window.lua (no window could be made)\n")
  os.exit(0)
end
for _ = 1, 3 do collectgarbage("collect") end
check(win:is_open(), "the window is open")
win:render_now() -- a freed context would crash here

-- the callbacks take functions, and clear with nothing
win:on_close(function(w) return false end)
win:on_frame(function(w) end)
win:on_close(nil)
win:on_frame(nil)
win:on_frame(function(w) end)
for _ = 1, 3 do collectgarbage("collect") end
check(win:is_open(), "garbage collection does not take the window")

-- freeing twice is harmless, and the callbacks' references go with the window
win:free()
check(not win:is_open(), "free() closes the window")
win:free()

-- a frame callback that frees its own window: the backend is still using it while the callback runs,
-- so it is only closed there, and its memory goes later (a use after free crashed here)
do
  local ctx2 = bui.Context(bui.Node("frame"))
  local w2 = bui.Window(ctx2, "bui-lua-window-test-2", 200, 100)
  local ran = false
  w2:on_frame(function(w)
    ran = true
    w:free()
    w:free() -- twice in the callback is as harmless as outside it
    bui.quit()
  end)
  w2:request_frame(false)
  bui.run()
  check(ran, "the frame callback ran")
  check(not w2:is_open(), "the window the callback freed is closed")
  w2:free() -- no callback is running now: the memory is freed here
  w2 = nil
  for _ = 1, 3 do collectgarbage("collect") end
  -- and a window made afterwards works (the first one's memory is gone, not corrupted)
  local w3 = bui.Window(ctx2, "bui-lua-window-test-3", 100, 60)
  check(w3 and w3:is_open(), "a new window opens")
  w3:render_now()
  w3:free()
end

io.write("OK: test_window.lua\n")
os.exit(0)
