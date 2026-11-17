local bui = require("bui")

local root = bui.Node("column")
root:set("padding", 12)
root:set("spacing", 6)

local title = bui.Node("label")
title:set("text", "{{title}}")
root:add(title)

local count = 0
local label = bui.Node("label")
label:set("text", "Clicks: 0")
root:add(label)

local button = bui.Node("button")
button:set("text", "Add one")
button:on(bui.EV_CLICK, function()
  count = count + 1
  label:set("text", "Clicks: " .. count)
end)
root:add(button)

local win, err = bui.open({ root = root, title = "{{title}}", width = 320, height = 200 })
if not win then
  print("No window: " .. tostring(err))
end
