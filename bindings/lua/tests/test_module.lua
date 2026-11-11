-- The loadable module (_bui.dll / _bui.so): Lua finds its entry point luaopen__bui, and what it
-- returns is the same API as the bui-lua executable's.
-- Usage: bui-lua test_module.lua <path of the module>
-- SPDX-License-Identifier: GPL-2.0-or-later

local function check(cond, msg)
  if not cond then io.stderr:write("FAIL: " .. msg .. "\n"); os.exit(1) end
end

local path = arg and arg[1]
check(path, "pass the module's path")
local open, err = package.loadlib(path, "luaopen__bui")
check(open, "the module has no luaopen__bui: " .. tostring(err))
local m = open()
check(type(m) == "table", "luaopen__bui should return the module table")
check(type(m.Node) == "function" and type(m.Context) == "function", "the widget API is there")
local major, minor, patch = m.version()
check(("%d.%d.%d"):format(major, minor, patch) == m.KIT_VERSION, "the module reports the kit's version")
local node = m.Node("frame")
check(node ~= nil, "a node can be made")

io.write("OK: test_module.lua\n")
os.exit(0)
