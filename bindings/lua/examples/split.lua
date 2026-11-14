-- Blender's screen from Lua: areas you resize by dragging the edges between them, split and
-- join by dragging their corners, dock by dragging a corner into another area; Ctrl+Space
-- maximizes the area under the pointer. Each area has a header and a sidebar (N toggles it).
-- SPDX-License-Identifier: GPL-2.0-or-later
local bui = require("bui")

-- what an area shows: a header, a sidebar with a panel, and the main view
local function fill(area, title)
  local header = bui.Node("areaheader")
  local name = bui.Node("label")
  name:set("text", title)
  header:add(name)
  area:add(header)

  local side = bui.Node("sidebar")
  side:set("size", 220)
  side:set("hotkey", "n")
  local panel = bui.Node("panel")
  panel:set("text", "Item")
  side:add(panel)
  area:add(side)

  local body = bui.Node("column")
  local note = bui.Node("label")
  note:set("text", title .. ": drag the edges and corners")
  body:add(note)
  area:add(body)
end

-- the starting layout: the viewport, Properties beside it, the Outliner above that
local screen = bui.Node("screen")
local view = bui.Node("area")
view:set("editor", "3D Viewport")
screen:add(view)
local props = view:split("v", 0.7)
props:set("editor", "Properties")
local outliner = props:split("h", 0.6)
outliner:set("editor", "Outliner")
for _, area in ipairs(screen:children()) do
  fill(area, area:get("editor"))
end

-- areas the user makes (splits, docks) show the editor of the one they came from
screen:on(bui.EV_AREA, function(self, index, what)
  if what == bui.AREA_NEW then
    local area = self:child(index)
    fill(area, area:get("editor"))
  end
end)

local ctx = bui.Context(screen)
local win = bui.Window(ctx, "BUI Screen — Lua", 1000, 640)
if win then
  win:show()
  bui.run()
else
  local px, _, w, h = ctx:render(1000, 640, 1)
  print("No window system. Headless render: " .. w .. "x" .. h .. " " .. #px .. " bytes")
end
