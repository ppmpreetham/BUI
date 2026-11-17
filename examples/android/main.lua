-- A Lua app for Android: build.py --lua examples/android puts this in the APK as assets/main.lua.
-- It is an ordinary BUI Lua script: the same code runs on a desktop with bui-lua.
local bui = require("bui")

local root = bui.Node("scroll")
local page = bui.Node("column")
page:set("padding", 12)
page:set("spacing", 6)
root:add(page)

local title = bui.Node("label")
title:set("text", "BUI + Lua on Android")
page:add(title)

local clicks = 0
local button = bui.Node("button")
button:set("text", "Tap me")
button:on(bui.EV_CLICK, function()
  clicks = clicks + 1
  button:set("text", "Tapped " .. clicks .. " times")
end)
page:add(button)

local check = bui.Node("checkbox")
check:set("label", "A checkbox")
page:add(check)

local slider = bui.Node("slider")
slider:set("label", "Slider")
slider:set("value", 0.5)
page:add(slider)

local field = bui.Node("text")
field:set("label", "Text field")
field:set("value", "tap to type")
page:add(field)

bui.open({ root = root, title = "BUI + Lua" })
