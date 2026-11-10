-- Lua Table Editor for bui-lua
-- Navigate, edit, and export any Lua table using BUI tree view.
--
-- Usage:  bui-lua bindings/lua/examples/lua_table_editor.lua
--
-- SPDX-License-Identifier: GPL-2.0-or-later

local bui = require("bui")

-- ============================================================
-- HELPERS
-- ============================================================

local function sorted_pairs(t)
  local keys = {}
  for k in pairs(t) do keys[#keys + 1] = k end
  table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
  local i = 0
  return function()
    i = i + 1; local k = keys[i]
    if k == nil then return nil end
    return k, t[k]
  end
end

local function count_items(t)
  local n = 0
  for _ in pairs(t) do n = n + 1 end
  return n
end

local function count_all(t)
  local n = 0
  for _, v in pairs(t) do
    n = n + 1
    if type(v) == "table" then n = n + count_all(v) end
  end
  return n
end

local function table_deepcopy(t, seen)
  if type(t) ~= "table" then return t end
  seen = seen or {}
  if seen[t] then return seen[t] end
  local c = {}
  seen[t] = c
  for k, v in pairs(t) do c[table_deepcopy(k, seen)] = table_deepcopy(v, seen) end
  return c
end

local function lua_type_name(v)
  local t = type(v)
  if t ~= "table" then return t end
  local mt = getmetatable(v)
  if mt then
    local name = mt.__name or mt.__type or getmetatable(mt)
    if name then return "(" .. tostring(name) .. ")" end
  end
  local n = count_items(v)
  if n == 0 then return "table" end
  for i = 1, n do if v[i] == nil then return "table" end end
  return "array"
end

local function fmt_val(v)
  local t = type(v)
  if t == "string" then return string.format("%q", v)
  elseif t == "number" then
    if v == math.floor(v) and math.abs(v) < 1e15 then return tostring(v)
    else return string.format("%.14g", v) end
  elseif t == "boolean" then return v and "true" or "false"
  elseif t == "nil" then return "nil"
  elseif t == "table" then
    local mt = getmetatable(v)
    local tag = ""
    if mt then
      local name = mt.__name or mt.__type
      if name then tag = " [" .. tostring(name) .. "]" end
    end
    return "{" .. count_items(v) .. " items" .. tag .. "}"
  else return "(" .. t .. ")" end
end

local TYPE_NAMES = {"string", "number", "boolean", "table", "array", "nil"}

local function type_index(tn)
  for i, t in ipairs(TYPE_NAMES) do
    if t == tn then return i - 1 end
  end
  return nil
end

local function is_scalar_type(tn)
  return tn == "string" or tn == "number" or tn == "boolean"
end

-- ============================================================
-- DEMO TABLES
-- ============================================================

local DEMOS = {}

DEMOS["Person Record"] = {
  name = "Alice Smith",
  age = 30,
  email = "alice@example.com",
  active = true,
  score = 95.5,
  tags = {"developer", "designer", "rust"},
  address = {
    street = "123 Main St",
    city = "Portland",
    state = "OR",
    zip = 97201,
    coordinates = {lat = 45.5152, lon = -122.6784}
  },
  metadata = nil
}

DEMOS["Project Config"] = {
  name = "bui-lua",
  version = "0.1.0",
  debug = false,
  features = {
    images = true,
    video = false,
    gpu = true,
    limits = {
      max_windows = 10,
      max_fps = 60,
      cache_size_mb = 256
    }
  },
  paths = {
    data = "/usr/local/share/bui",
    cache = "/tmp/bui-cache",
    logs = "/var/log/bui"
  },
  users = {
    {id = 1, name = "admin", role = "owner"},
    {id = 2, name = "guest", role = "viewer"}
  }
}

DEMOS["Game State"] = {
  player = {
    name = "Hero",
    level = 12,
    health = 85,
    max_health = 100,
    mana = 42,
    max_mana = 80,
    inventory = {"sword", "shield", "potion", "ring", "map"},
    stats = {
      strength = 15,
      dexterity = 12,
      intelligence = 8,
      vitality = 14
    },
    position = {x = 120.5, y = 340.2, z = 0}
  },
  quests = {
    active = {"find_the_artifact", "defeat_the_dragon"},
    completed = {"tutorial", "first_blood", "side_quest_01"},
    failed = {}
  },
  settings = {
    volume = 0.8,
    fullscreen = true,
    resolution = {width = 1920, height = 1080},
    controls = {
      move_forward = "w",
      move_back = "s",
      jump = "space",
      interact = "e"
    }
  }
}

-- ============================================================
-- DATA MODEL
-- ============================================================

local data           = nil
local id_counter     = 1000
local item_meta      = {}
local undo_stack     = {}
local redo_stack     = {}
local key_numeric    = false

local function next_id()
  id_counter = id_counter + 1
  return id_counter
end

local function push_undo()
  table.insert(undo_stack, table_deepcopy(data))
  redo_stack = {}
end

-- ============================================================
-- UI STATE
-- ============================================================

local tree_node       = nil
local editor_key      = nil
local editor_type     = nil
local editor_val_str  = nil
local editor_val_num  = nil
local editor_bool     = nil
local val_label       = nil
local apply_btn       = nil
local status_node     = nil
local current_demo    = ""
local edit_id         = nil
local type_dirty      = false
local undo_btn        = nil
local redo_btn        = nil
local search_node     = nil
local key_num_toggle  = nil

-- ============================================================
-- TREE BUILDING
-- ============================================================

local function make_item(k, v, parent_data)
  local key_str = tostring(k)
  local vt = lua_type_name(v)
  local item_icon = (vt == "table" or vt == "array") and bui.icon_id("OUTLINER_COLLECTION") or bui.icon_id("FILE")
  local item = bui.TreeItem(key_str, item_icon)
  item:set_values({key_str, vt, fmt_val(v)})
  local id = next_id()
  item:set_ref(id)
  item_meta[id] = {
    type   = vt,
    key    = k,
    value  = v,
    parent = parent_data,
    id     = id,
    item   = item,
  }
  return item
end

local function build_children(item, tbl)
  for k, v in sorted_pairs(tbl) do
    local child = make_item(k, v, tbl)
    if type(v) == "table" then
      build_children(child, v)
    end
    item:add(child)
  end
end

local function key_matches(k, q)
  return tostring(k):lower():find(q, 1, true) ~= nil
end

local function scalar_matches(v, q)
  return fmt_val(v):lower():find(q, 1, true) ~= nil
end

local function subtree_matches(t, q)
  for k, v in pairs(t) do
    if key_matches(k, q) then return true end
    if type(v) == "table" then
      if subtree_matches(v, q) then return true end
    elseif scalar_matches(v, q) then
      return true
    end
  end
  return false
end

-- Single builder for filtered and unfiltered trees. q == nil builds
-- everything; otherwise a node is kept when its own key or scalar value
-- matches (whole subtree kept) or when any descendant matches (only
-- matching branches kept). meta.parent always points at the live table.
local function collect_items(parent_item, tbl, parent_data, q)
  local items = {}
  for k, v in sorted_pairs(tbl) do
    local own = true
    if q then
      own = key_matches(k, q) or (type(v) ~= "table" and scalar_matches(v, q))
      if not own and not (type(v) == "table" and subtree_matches(v, q)) then
        goto continue
      end
    end
    local item = make_item(k, v, parent_data)
    if type(v) == "table" then
      if q == nil or own then
        build_children(item, v)
      else
        collect_items(item, v, v, q)
      end
    end
    if parent_item then parent_item:add(item) else items[#items + 1] = item end
    ::continue::
  end
  return items
end

local function refresh_tree()
  if not tree_node or not data then return 0 end
  item_meta = {}
  id_counter = 1000
  local query = search_node and search_node:get("text") or ""
  local q = query ~= "" and query:lower() or nil
  local items = collect_items(nil, data, data, q)
  tree_node:set_roots(items)
  tree_node:relayout()
  return #items
end

-- ============================================================
-- SERIALIZATION
-- ============================================================

local function serialize(v, indent, seen)
  indent = indent or ""
  local t = type(v)
  if t == "string" then return string.format("%q", v)
  elseif t == "number" then
    if v == math.floor(v) and math.abs(v) < 1e15 then return tostring(v)
    else return string.format("%.14g", v) end
  elseif t == "boolean" then return v and "true" or "false"
  elseif t == "nil"   then return "nil"
  elseif t ~= "table" then return tostring(v) end

  seen = seen or {}
  if seen[v] then return "{...}" end
  seen[v] = true

  local n = count_items(v)
  if n == 0 then return "{}" end
  local is_arr = true
  for i = 1, n do if v[i] == nil then is_arr = false; break end end

  local sub = indent .. "  "
  local parts = {}
  for k, val in sorted_pairs(v) do
    local line
    if is_arr then
      line = serialize(val, sub, seen)
    elseif type(k) == "string" and k:match("^[%a_][%w_]*$") then
      line = k .. " = " .. serialize(val, sub, seen)
    else
      local ks = type(k) == "string" and string.format("[%q]", k) or "[" .. tostring(k) .. "]"
      line = ks .. " = " .. serialize(val, sub, seen)
    end
    parts[#parts + 1] = line
  end
  seen[v] = nil
  local body
  if #parts <= 3 and #table.concat(parts, ", ") < 60 then
    body = "{ " .. table.concat(parts, ", ") .. " }"
  else
    body = "{\n" .. sub .. table.concat(parts, ",\n" .. sub) .. "\n" .. indent .. "}"
  end
  return body
end

-- ============================================================
-- EDITOR ACTIONS
-- ============================================================

local function update_status(text)
  if status_node then status_node:set("text", text) end
end

local function refresh_undo_buttons()
  if undo_btn then undo_btn:set("enabled", #undo_stack > 0 and 1 or 0) end
  if redo_btn then redo_btn:set("enabled", #redo_stack > 0 and 1 or 0) end
end

local function selected_type_name()
  return TYPE_NAMES[(editor_type:get("value") or 0) + 1] or "string"
end

local function apply_is_allowed()
  if not edit_id then return false end
  local meta = item_meta[edit_id]
  if not meta then return false end
  if not type_dirty then return is_scalar_type(meta.type) end
  return selected_type_name() ~= meta.type
end

local function update_value_widgets(tn)
  if editor_val_str then editor_val_str:set("enabled", tn == "string" and 1 or 0) end
  if editor_val_num then editor_val_num:set("enabled", tn == "number" and 1 or 0) end
  if editor_bool then editor_bool:set("enabled", tn == "boolean" and 1 or 0) end
  if val_label then val_label:set("text", is_scalar_type(tn) and "Value:" or "(read-only)") end
  if apply_btn then apply_btn:set("enabled", apply_is_allowed() and 1 or 0) end
end

local function clear_editor()
  edit_id = nil
  type_dirty = false
  if editor_key then editor_key:set("value", "") end
  if editor_type then editor_type:set("value", 0) end
  if editor_val_str then
    editor_val_str:set("value", "")
    editor_val_str:set("enabled", 0)
  end
  if editor_val_num then
    editor_val_num:set("value", 0)
    editor_val_num:set("enabled", 0)
  end
  if editor_bool then
    editor_bool:set("value", 0)
    editor_bool:set("enabled", 0)
  end
  if val_label then val_label:set("text", "Value:") end
  if apply_btn then apply_btn:set("enabled", 0) end
end

local function refresh_editor()
  if not tree_node then return end
  local sel = tree_node:selected()
  if not sel then clear_editor(); return end
  local meta = item_meta[sel:ref()]
  if not meta then clear_editor(); return end
  edit_id = meta.id
  type_dirty = false

  if editor_key then editor_key:set("value", tostring(meta.key)) end
  local ti = type_index(meta.type)
  if editor_type then editor_type:set("value", ti or 0) end
  if meta.type == "string" and editor_val_str then editor_val_str:set("value", meta.value or "") end
  if meta.type == "number" and editor_val_num then editor_val_num:set("value", meta.value or 0) end
  if meta.type == "boolean" and editor_bool then editor_bool:set("value", meta.value and 1 or 0) end
  update_value_widgets(meta.type)
end

local function apply_edit()
  if not edit_id then return end
  if not apply_is_allowed() then
    local meta = item_meta[edit_id]
    if meta then update_status("Read-only: pick a new type to replace this entry") end
    return
  end
  local meta = item_meta[edit_id]
  local parent = meta.parent
  local old_key = meta.key

  local new_key_str = editor_key:get("text")
  if not new_key_str or new_key_str == "" then return end

  local new_type = selected_type_name()
  local type_changed = type_dirty and new_type ~= meta.type
  local scalar_meta = is_scalar_type(meta.type)

  local new_val
  if new_type == "string" then
    new_val = editor_val_str:get("text") or ""
  elseif new_type == "number" then
    new_val = editor_val_num:get("value") or 0
  elseif new_type == "boolean" then
    new_val = editor_bool:get("value") > 0
  elseif new_type == "table" or new_type == "array" then
    new_val = {}
  else
    new_val = nil
  end

  local new_key
  if key_numeric then
    new_key = tonumber(new_key_str) or new_key_str
  else
    if new_key_str:match("^[+-]?%d+$") and not new_key_str:match("^0%d") then
      new_key = tonumber(new_key_str) or new_key_str
    else
      new_key = new_key_str
    end
  end

  local key_changed = (new_key ~= old_key)
  if key_changed and parent[new_key] ~= nil then
    update_status("Duplicate key: " .. tostring(new_key))
    return
  end

  push_undo()

  if scalar_meta and not type_changed and not key_changed then
    parent[old_key] = new_val
    meta.value = new_val
    local sel = tree_node:selected()
    if sel then
      sel:set_values({tostring(new_key), new_type, fmt_val(new_val)})
    end
  else
    if key_changed then
      parent[old_key] = nil
    end
    parent[new_key] = new_val
    edit_id = nil
    refresh_tree()
  end

  update_status("Applied. Entries: " .. count_all(data))
  refresh_undo_buttons()
end

local function delete_selected()
  if not edit_id then return end
  local meta = item_meta[edit_id]
  if not meta then return end
  push_undo()
  meta.parent[meta.key] = nil
  refresh_tree()
  clear_editor()
  update_status("Deleted. Entries: " .. count_all(data))
  refresh_undo_buttons()
end

local function add_row()
  local target, is_arr = data, false
  if edit_id then
    local meta = item_meta[edit_id]
    if meta and meta.type == "array" then
      target, is_arr = meta.value, true
    elseif meta and meta.type == "table" then
      target = meta.value
    end
  end
  push_undo()
  if is_arr then
    target[#target + 1] = ""
  else
    local i = 1
    while target["new" .. i] ~= nil do i = i + 1 end
    target["new" .. i] = ""
  end
  refresh_tree()
  clear_editor()
  update_status("Added entry. Entries: " .. count_all(data))
  refresh_undo_buttons()
end

local function undo_action()
  if #undo_stack == 0 then return end
  table.insert(redo_stack, table_deepcopy(data))
  data = table.remove(undo_stack)
  refresh_tree()
  clear_editor()
  refresh_undo_buttons()
  update_status("Undo. Entries: " .. count_all(data))
end

local function redo_action()
  if #redo_stack == 0 then return end
  table.insert(undo_stack, table_deepcopy(data))
  data = table.remove(redo_stack)
  refresh_tree()
  clear_editor()
  refresh_undo_buttons()
  update_status("Redo. Entries: " .. count_all(data))
end

local function export_table()
  print("\n=== Exported: " .. current_demo .. " ===")
  print(serialize(data))
  print("=== End ===\n")
end

local function export_to_file_via_browser()
  local fb = bui.Node("filebrowser")
  fb:set("mode", 1)
  local default_name = (current_demo or "export"):gsub("%s+", "_") .. ".lua"
  fb:set("file", default_name)
  local fbx = bui.Context(fb)
  if not fbx then return end
  local fbw = bui.Window(fbx, "Export: " .. current_demo, 800, 500)
  if not fbw then
    fbx:free()
    return
  end
  fbw:show()
  while fb:get("state") == 0 and fbw:is_open() do
    bui.pump()
  end
  local state = fb:get("state")
  local result_path = nil
  if state > 0 then
    local r = fb:get_strs("result")
    result_path = r and r[1]
  end
  if fbw:is_open() then fbw:close() end
  fbx:free()
  if result_path then
    local fh, err = io.open(result_path, "w")
    if fh then
      fh:write(serialize(data))
      fh:close()
      update_status("Exported to " .. result_path)
    else
      update_status("Export error: " .. tostring(err))
    end
  end
end

local function on_search()
  if not tree_node or not search_node then return end
  local query = search_node:get("text") or ""
  local n = refresh_tree()
  if query == "" then
    update_status("Loaded: " .. current_demo .. " | Entries: " .. count_all(data))
  else
    update_status("Search: " .. n .. " top-level matches | Entries: " .. count_all(data))
  end
end

local function switch_demo(name, t)
  current_demo = name
  data = table_deepcopy(t)
  undo_stack = {}
  redo_stack = {}
  refresh_tree()
  clear_editor()
  if undo_btn then undo_btn:set("enabled", 0) end
  if redo_btn then redo_btn:set("enabled", 0) end
  if search_node then search_node:set("value", "") end
  update_status("Loaded: " .. name .. " | Entries: " .. count_all(data))
end

-- ============================================================
-- BUILD THE UI
-- ============================================================

local function build_ui()
  local root = bui.Node("frame")
  root:set("padding", 6)

  -- === Toolbar ===
  local tb = bui.Node("row")
  tb:set("padding", 4)

  local title_lbl = bui.Node("label")
  title_lbl:set("text", "Lua Table Editor")
  tb:add(title_lbl)
  title_lbl:geom(bui.GEOM_PACK, {side = 0})

  local demo_dd = bui.Node("dropdown")
  local demo_names = {}
  for name in pairs(DEMOS) do demo_names[#demo_names + 1] = name end
  table.sort(demo_names)
  demo_dd:set_strs("items", demo_names)
  demo_dd:on(bui.EV_CHANGE, function()
    local idx = demo_dd:get("value")
    local name = demo_names[idx + 1]
    if name then switch_demo(name, DEMOS[name]) end
  end)
  tb:add(demo_dd)
  demo_dd:geom(bui.GEOM_PACK, {fill = 1, side = 1})

  local search = bui.Node("text")
  search:set("placeholder", "Search...")
  search:on(bui.EV_CHANGE, on_search)
  tb:add(search)
  search:geom(bui.GEOM_PACK, {fill = 1, side = 1})
  search_node = search

  local sep1 = bui.Node("separator")
  sep1:geom(bui.GEOM_PACK, {side = 1})
  tb:add(sep1)

  local add_btn = bui.Node("button")
  add_btn:set("text", "+ Add")
  add_btn:set("icon", bui.icon_id("ADD"))
  add_btn:on(bui.EV_CLICK, add_row)
  tb:add(add_btn)
  add_btn:geom(bui.GEOM_PACK, {side = 1})

  local del_btn = bui.Node("button")
  del_btn:set("text", "Delete")
  del_btn:set("icon", bui.icon_id("X"))
  del_btn:on(bui.EV_CLICK, delete_selected)
  tb:add(del_btn)
  del_btn:geom(bui.GEOM_PACK, {side = 1})

  local exp_btn = bui.Node("button")
  exp_btn:set("text", "Export")
  exp_btn:set("icon", bui.icon_id("EXPORT"))
  exp_btn:on(bui.EV_CLICK, export_table)
  tb:add(exp_btn)
  exp_btn:geom(bui.GEOM_PACK, {side = 1})

  local expf_btn = bui.Node("button")
  expf_btn:set("text", "Save...")
  expf_btn:set("icon", bui.icon_id("FILE_TICK"))
  expf_btn:on(bui.EV_CLICK, export_to_file_via_browser)
  tb:add(expf_btn)
  expf_btn:geom(bui.GEOM_PACK, {side = 1})

  local sep2 = bui.Node("separator")
  sep2:geom(bui.GEOM_PACK, {side = 1})
  tb:add(sep2)

  local ub = bui.Node("button")
  ub:set("text", "Undo")
  ub:set("icon", bui.icon_id("LOOP_BACK"))
  ub:set("enabled", 0)
  ub:on(bui.EV_CLICK, undo_action)
  tb:add(ub)
  ub:geom(bui.GEOM_PACK, {side = 1})
  undo_btn = ub

  local rb = bui.Node("button")
  rb:set("text", "Redo")
  rb:set("icon", bui.icon_id("LOOP_FORWARDS"))
  rb:set("enabled", 0)
  rb:on(bui.EV_CLICK, redo_action)
  tb:add(rb)
  rb:geom(bui.GEOM_PACK, {side = 1})
  redo_btn = rb

  root:add(tb)
  tb:geom(bui.GEOM_PACK, {side = 0, fill = 1})

  -- === Main: tree + editor side panel ===
  local main_row = bui.Node("row")
  root:add(main_row)
  main_row:geom(bui.GEOM_PACK, {side = 0, expand = 1, fill = 3})

  -- Tree view
  local tree = bui.Node("tree")
  tree:set_strs("headers", {"Key", "Type", "Value"})
  tree:set_strs("columns", {"0", "2", "1", "3"})
  tree:on(bui.EV_ACTIVATE, refresh_editor)
  tree:on(bui.EV_CHANGE, refresh_editor)
  main_row:add(tree)
  tree:geom(bui.GEOM_PACK, {fill = 1})
  tree_node = tree

  -- Editor side panel (collapsible)
  local ep = bui.Node("panel")
  ep:set("text", "Edit Entry")
  ep:set("collapsed", 0)
  ep:geom(bui.GEOM_PACK, {side = 1})

  local body = ep:child(0)
  if not body then
    body = bui.Node("column")
    body:set("padding", 4)
    ep:add(body)
  end
  body:set("padding", 4)

  -- Key
  local kl = bui.Node("label")
  kl:set("text", "Key:")
  body:add(kl)
  editor_key = bui.Node("text")
  editor_key:set("placeholder", "key name")
  body:add(editor_key)

  -- Numeric key toggle
  local num_key_row = bui.Node("row")
  num_key_row:set("padding", 2)
  local nkl = bui.Node("label")
  nkl:set("text", "Numeric key:")
  num_key_row:add(nkl)
  key_num_toggle = bui.Node("toggle")
  key_num_toggle:set("value", 0)
  key_num_toggle:on(bui.EV_CHANGE, function()
    key_numeric = key_num_toggle:get("value") > 0
  end)
  num_key_row:add(key_num_toggle)
  body:add(num_key_row)

  -- Type
  local tl = bui.Node("label")
  tl:set("text", "Type:")
  body:add(tl)
  editor_type = bui.Node("dropdown")
  editor_type:set_strs("items", TYPE_NAMES)
  editor_type:on(bui.EV_CHANGE, function()
    type_dirty = true
    update_value_widgets(selected_type_name())
  end)
  body:add(editor_type)

  -- Value — type-aware widgets
  val_label = bui.Node("label")
  val_label:set("text", "Value:")
  body:add(val_label)

  editor_val_str = bui.Node("text")
  editor_val_str:set("placeholder", "value")
  editor_val_str:on(bui.EV_SUBMIT, apply_edit)
  body:add(editor_val_str)

  editor_val_num = bui.Node("number")
  editor_val_num:set("min", -1e15)
  editor_val_num:set("max", 1e15)
  editor_val_num:set("enabled", 0)
  body:add(editor_val_num)

  -- Boolean toggle
  local bl = bui.Node("label")
  bl:set("text", "Value:")
  body:add(bl)
  editor_bool = bui.Node("toggle")
  editor_bool:set("text", "Enabled")
  editor_bool:set("value", 0)
  editor_bool:set("enabled", 0)
  body:add(editor_bool)

  -- Apply
  apply_btn = bui.Node("button")
  apply_btn:set("text", "Apply")
  apply_btn:set("enabled", 0)
  apply_btn:on(bui.EV_CLICK, apply_edit)
  body:add(apply_btn)
  apply_btn:geom(bui.GEOM_PACK, {fill = 1, pady0 = 4})

  local clear_btn = bui.Node("button")
  clear_btn:set("text", "Clear Editor")
  clear_btn:on(bui.EV_CLICK, clear_editor)
  body:add(clear_btn)
  clear_btn:geom(bui.GEOM_PACK, {fill = 1, pady0 = 2})

  main_row:add(ep)

  -- === Status bar ===
  status_node = bui.Node("label")
  status_node:set("text", "Select a demo to begin")
  root:add(status_node)
  status_node:geom(bui.GEOM_PACK, {side = 0, fill = 1})

  -- === Initial load ===
  local first_name = demo_names[1]
  switch_demo(first_name, DEMOS[first_name])
  if demo_dd then demo_dd:set("value", 0) end

  return root, tree
end

-- ============================================================
-- MAIN
-- ============================================================

print("Lua Table Editor v0.3")
print("3 demos: Person Record, Project Config, Game State")
print("Select a demo from the dropdown, then edit entries.")
print("Export to stdout | Save... to file | Ctrl+Z / Ctrl+Shift+Z for undo/redo\n")

local ui_root = build_ui()
local ctx = bui.Context(ui_root)

ctx:bind("ctrl+z", undo_action)
ctx:bind("ctrl+shift+z", redo_action)

local win = bui.Window(ctx, "Lua Table Editor", 800, 550)

if win then
  win:show()
  bui.run()
  print("Editor closed.")
else
  print("No window system available. Printing demo tables:\n")
  for name, t in sorted_pairs(DEMOS) do
    print("=== " .. name .. " ===")
    print(serialize(t))
    print()
  end
end
