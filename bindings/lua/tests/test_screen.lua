-- Screens, areas and regions from Lua: splits, joins, docks and closes; areas the screen makes
-- (AREA_NEW) and closes (their wrappers go stale, not dangling); an edge dragged; a region sized,
-- hidden by its hotkey; the layout saved and restored; values the setters refuse.
-- SPDX-License-Identifier: GPL-2.0-or-later

local function check(cond, msg)
  if not cond then
    io.stderr:write("FAIL: " .. msg .. "\n")
    os.exit(1)
  end
end

local bui = require("bui")

local screen = bui.Node("screen")
local first = bui.Node("area")
first:set("editor", "3D Viewport")
screen:add(first)
local made, closed = 0, 0
screen:on(bui.EV_AREA, function(self, index, what)
  if what == bui.AREA_NEW then made = made + 1 end
end)
local ctx = bui.Context(screen)
ctx:render(800, 600, 1)

-- split: the new area copies the editor, the screen tells (AREA_NEW)
local right = first:split("v", 0.75)
check(right ~= nil and made == 1, "split makes an area")
check(right:get("editor") == "3D Viewport", "the new area shows the same editor")
right:set("editor", "Properties")
right:on(bui.EV_AREA, function(self, index, what)
  if what == bui.AREA_CLOSE then closed = closed + 1 end
end)
ctx:render(800, 600, 0)
local x0, y0, w0, h0 = first:rect()
local x1 = right:rect()
check(x1 > x0 + w0, "the new area is on the right")

-- drag the edge between them 100 px left
local edge = x0 + w0 + 1
ctx:motion(edge, 300, 0)
ctx:press(edge, 300, 0)
ctx:motion(edge - 50, 300, 0)
ctx:motion(edge - 100, 300, 0)
ctx:release(edge - 100, 300, 0)
ctx:render(800, 600, 0)
local _, _, w1 = first:rect()
check(math.abs(w1 - (w0 - 100)) <= 2, "the edge moved: " .. w0 .. " -> " .. w1)

-- the layout: saved, changed, restored
local saved = screen:get("layout")
check(type(saved) == "string" and saved:find('"bui_screen": 1', 1, true), "layout text")
check(first:join(right), "join")
check(closed == 1 and screen:child_count() == 1, "the joined area closed")
check(right:type() == "(freed)", "a closed area's wrapper is stale, not dangling")
screen:set("layout", saved)
check(screen:child_count() == 2 and made == 2, "the restored layout makes the area again")
check(not screen:set("layout", "nonsense"), "a malformed layout is refused")

-- dock, swap, close
local a, b = screen:child(0), screen:child(1)
check(a:swap(b), "swap")
check(b:dock(a, "left", 0.3), "dock")
check(not pcall(b.dock, b, a, "middle"), "an unknown dock place is refused")
local c = screen:child(0):split("h", 0.5)
check(c ~= nil and c:close(), "close")

-- regions: a sidebar 200 px wide, hidden by its hotkey
local area = bui.Node("area")
local header = bui.Node("areaheader")
local side = bui.Node("sidebar")
local body = bui.Node("column")
check(side:set("size", 200) and side:set("hotkey", "n"), "region properties")
check(not side:set("side", "top"), "a sidebar can't be on top")
area:add(header)
area:add(side)
area:add(body)
local tools = bui.Node("column")
check(tools:set("region", "toolbar"), "a toolbar region")
area:add(tools)
check(not bui.Node("column"):set("region", "nope"), "an unknown region")
local actx = bui.Context(area)
actx:render(600, 400, 1)
local sx, _, sw = side:rect()
check(sw == 200 and sx + sw == 600, "the sidebar is 200 px on the right")
actx:motion(300, 200, 0)
actx:key("n", "n", 0)
actx:render(600, 400, 0)
check(side:get("hidden") == 1, "the hotkey hides it")

io.write("test_screen.lua: ok\n")
