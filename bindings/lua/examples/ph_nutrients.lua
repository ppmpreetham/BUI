-- Soil pH and nutrient availability chart for bui-lua.
-- A Truog-style graph custom-drawn on a canvas node: pH scale (4.0-10.0),
-- horizontal availability bands per nutrient, and two overlapping
-- availability curves that peak in the slightly acidic to neutral range.
-- Two tabs share one data model: "Chart" edits through number fields and
-- sliders, "Table" edits the very same data as a Lua table in a tree view
-- (name, pH range, colours, curve parameters). Edits in either tab are
-- reflected in the graph live.
--
-- Usage:  bui-lua bindings/lua/examples/ph_nutrients.lua
--
-- Reference: the classic soil pH / nutrient availability chart
-- (https://jrcpl.us/contrib/2020/gardening-for-engineers/img/ph.jpg).
-- Band ranges are illustrative, not laboratory data.
--
-- SPDX-License-Identifier: GPL-2.0-or-later

local bui = require("bui")

local WIN_W, WIN_H = 1260, 900
local PH_MIN, PH_MAX = 4.0, 10.0

-- Plot geometry (canvas items use absolute coordinates; RIGHT/BOTTOM follow
-- the canvas size and are recomputed in draw() whenever EV_CONFIGURE fires).
local LEFT, TOP = 64, 48
local canvas_w, canvas_h = WIN_W - 16, 636
local RIGHT, BOTTOM = canvas_w - 24, canvas_h - 56

local PAPER = {0.965, 0.955, 0.90, 1.0}
local INK   = {0.10, 0.10, 0.09, 1.0}
local GRID  = {0.50, 0.48, 0.43, 0.35}
local FAINT = {0.50, 0.48, 0.43, 0.18}

local DEFAULT_NUTRIENTS = {
  {name = "Nitrogen",   ph_min = 4.0, ph_max = 10.0, color = {0.30, 0.55, 0.25}},
  {name = "Phosphorus", ph_min = 5.5, ph_max =  8.0, color = {0.25, 0.50, 0.35}},
  {name = "Potassium",  ph_min = 4.5, ph_max =  9.5, color = {0.40, 0.55, 0.20}},
  {name = "Sulfur",     ph_min = 4.0, ph_max =  9.5, color = {0.45, 0.55, 0.30}},
  {name = "Calcium",    ph_min = 5.5, ph_max =  9.5, color = {0.35, 0.50, 0.45}},
  {name = "Magnesium",  ph_min = 5.5, ph_max =  8.5, color = {0.30, 0.45, 0.30}},
  {name = "Iron",       ph_min = 4.0, ph_max =  6.5, color = {0.60, 0.35, 0.15}},
  {name = "Manganese",  ph_min = 4.0, ph_max =  6.5, color = {0.55, 0.30, 0.20}},
  {name = "Boron",      ph_min = 4.5, ph_max =  8.5, color = {0.50, 0.40, 0.20}},
  {name = "Copper",     ph_min = 4.0, ph_max =  7.0, color = {0.60, 0.30, 0.15}},
  {name = "Zinc",       ph_min = 4.0, ph_max =  7.0, color = {0.50, 0.35, 0.25}},
}

local DEFAULT_CURVES = {
  macro = {label = "Macronutrients (N, P, K, S, Ca, Mg)", center = 6.7, spread = 1.60,
           color = {0.13, 0.35, 0.20, 1.0}},
  micro = {label = "Micronutrients (Fe, Mn, B, Cu, Zn)",  center = 5.7, spread = 1.80,
           color = {0.55, 0.25, 0.08, 1.0}},
}

-- ============================================================
-- DATA MODEL (single source of truth for both tabs)
-- ============================================================

local function deepcopy(t)
  if type(t) ~= "table" then return t end
  local c = {}
  for k, v in pairs(t) do c[deepcopy(k)] = deepcopy(v) end
  return c
end

local nutrients = deepcopy(DEFAULT_NUTRIENTS)
local curves = deepcopy(DEFAULT_CURVES)
local show_grid, show_curves = true, true

local undo_stack, redo_stack = {}, {}

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

-- Keeps the model displayable after arbitrary table-tab edits.
local function normalize_model()
  for _, n in ipairs(nutrients) do
    if type(n.ph_min) ~= "number" then n.ph_min = PH_MIN end
    if type(n.ph_max) ~= "number" then n.ph_max = PH_MAX end
    if type(n.name) ~= "string" then n.name = tostring(n.name) end
    n.ph_min, n.ph_max = clamp(n.ph_min, PH_MIN, PH_MAX), clamp(n.ph_max, PH_MIN, PH_MAX)
    if n.ph_min > n.ph_max then n.ph_min, n.ph_max = n.ph_max, n.ph_min end
    n.color = type(n.color) == "table" and n.color or {0.5, 0.5, 0.5}
    for ch = 1, 3 do n.color[ch] = clamp(tonumber(n.color[ch]) or 0.5, 0, 1) end
  end
  for _, key in ipairs({"macro", "micro"}) do
    local c = curves[key]
    if type(c.label) ~= "string" then c.label = key end
    c.center = clamp(tonumber(c.center) or 6.5, 0, 14)
    c.spread = clamp(tonumber(c.spread) or 1.5, 0.1, 5)
    c.color = type(c.color) == "table" and c.color or {0.5, 0.5, 0.5, 1}
    for ch = 1, 4 do c.color[ch] = clamp(tonumber(c.color[ch]) or 0.5, 0, 1) end
  end
end

-- ============================================================
-- UI SHELL
-- ============================================================

local root = bui.Node("frame")
root:set("padding", 8)

local title = bui.Node("label")
title:set("text", "Soil pH and Nutrient Availability")
root:add(title)
title:geom(bui.GEOM_PACK, {side = 0, fill = 1})

-- The kit's tabs widget owns its pages as children (tabs_arrange shows the
-- current child below the bar and hides the rest; tabs_select raises
-- BK_EV_CHANGE) — add pages to the tabs node itself, not a sibling frame.
local tabs = bui.Node("tabs")
tabs:set_strs("items", {"Chart", "Table"})
root:add(tabs)
tabs:geom(bui.GEOM_PACK, {side = 0, expand = 1, fill = 3})

local status_node = bui.Node("label")
status_node:set("text", "Chart tab: edit the fields below; the graph redraws live.")
root:add(status_node)
status_node:geom(bui.GEOM_PACK, {side = 0, fill = 1})

local function status(msg)
  status_node:set("text", msg)
end

-- Forward declarations for the shared edit pipeline.
local cv
local draw, rebuild_chart_rows, build_data_tree
local undo_btn, redo_btn

-- After any structural change: redraw the plot, rebuild the chart rows and
-- the data tree from the model.
local function after_model_change()
  normalize_model()
  draw()
  rebuild_chart_rows()
  build_data_tree()
end

local tree_dirty = false

-- Model-only edit from the chart tab: cheap path (tree rebuilt lazily on tab
-- switch, chart rows unaffected).
local function after_chart_edit()
  draw()
  tree_dirty = true
end

-- ============================================================
-- CANVAS HELPERS
-- ============================================================

local drawn, next_id = {}, 1

local function item(spec)
  spec.id = next_id
  next_id = next_id + 1
  cv:canvas_set(spec)
  drawn[#drawn + 1] = spec.id
end

local function line(x1, y1, x2, y2, color, width)
  item({type = bui.ITEM_LINE, coords = {x1, y1, x2, y2}, fill = color or INK,
        width = width or 1})
end

-- RECT/ROUNDRECT/OVAL take Tk-style corner coords {x1, y1, x2, y2}.
local function rect(x, y, w, h, fill, outline, radius)
  item({type = radius and bui.ITEM_ROUNDRECT or bui.ITEM_RECT,
        coords = {x, y, x + w, y + h}, fill = fill, outline = outline or INK,
        width = 1, radius = radius})
end

local function text(x, y, str, size, color)
  item({type = bui.ITEM_TEXT, coords = {x, y}, text = str, fill = color or INK,
        size = size or 12})
end

local function polyline(points, color, width)
  for i = 1, #points - 1 do
    local a, b = points[i], points[i + 1]
    line(a[1], a[2], b[1], b[2], color, width)
  end
end

local function ph_x(ph)
  return LEFT + ((ph - PH_MIN) / (PH_MAX - PH_MIN)) * (RIGHT - LEFT)
end

local function alpha(c, a)
  return {c[1], c[2], c[3], a}
end

local function gaussian(x, center, sigma)
  local d = (x - center) / sigma
  return math.exp(-0.5 * d * d)
end

-- ============================================================
-- DRAWING
-- ============================================================

draw = function()
  for _, id in ipairs(drawn) do cv:canvas_delete(id) end
  drawn, next_id = {}, 1

  RIGHT = math.max(LEFT + 240, canvas_w - 24)
  BOTTOM = math.max(TOP + 160, canvas_h - 56)

  rect(LEFT, TOP, RIGHT - LEFT, BOTTOM - TOP, PAPER, INK)

  if show_grid then
    local i = 0
    while i <= (PH_MAX - PH_MIN) * 2 do
      local ph = PH_MIN + i * 0.5
      local major = (ph * 2) % 2 == 0
      line(ph_x(ph), TOP, ph_x(ph), BOTTOM, major and GRID or FAINT, major and 1 or 0.6)
      i = i + 1
    end
    for r = 1, 9 do
      local y = TOP + r * (BOTTOM - TOP) / 10
      line(LEFT, y, RIGHT, y, FAINT, 0.6)
    end
  end

  -- Nutrient bands (horizontal, each spanning its available pH range)
  local row_h = (BOTTOM - TOP - 8) / math.max(#nutrients, 1)
  for i, n in ipairs(nutrients) do
    local cy = TOP + 4 + (i - 0.5) * row_h
    local x0, x1 = ph_x(n.ph_min), ph_x(n.ph_max)
    local bh = row_h - 12
    rect(x0, cy - bh / 2, math.max(x1 - x0, 4), bh, alpha(n.color, 0.40),
         alpha(n.color, 0.9), bh / 2)
    text((x0 + x1) / 2 - #n.name * 3.1, cy - 6, n.name, 11, INK)
  end

  -- Two overlapping availability curves
  if show_curves then
    for _, c in ipairs({curves.macro, curves.micro}) do
      local pts = {}
      for s = 0, 120 do
        local ph = PH_MIN + (s / 120) * (PH_MAX - PH_MIN)
        local avail = gaussian(ph, c.center, c.spread)
        pts[#pts + 1] = {ph_x(ph), BOTTOM - 8 - avail * (BOTTOM - TOP - 20)}
      end
      polyline(pts, c.color, 2.5)
    end
    local lx, ly = RIGHT - 262, TOP + 10
    rect(lx, ly, 252, 44, {1, 1, 1, 0.85}, GRID)
    line(lx + 8, ly + 14, lx + 30, ly + 14, curves.macro.color, 2.5)
    text(lx + 36, ly + 8, curves.macro.label, 10, INK)
    line(lx + 8, ly + 32, lx + 30, ly + 32, curves.micro.color, 2.5)
    text(lx + 36, ly + 26, curves.micro.label, 10, INK)
  end

  -- Axis labels
  text(ph_x(5.0) - 24, TOP - 20, "ACIDIC", 11, INK)
  text(ph_x(7.0) - 30, TOP - 20, "NEUTRAL", 11, INK)
  text(ph_x(9.0) - 34, TOP - 20, "ALKALINE", 11, INK)
  local i = 0
  while i <= (PH_MAX - PH_MIN) * 2 do
    local ph = PH_MIN + i * 0.5
    local major = (ph * 2) % 2 == 0
    local x = ph_x(ph)
    line(x, BOTTOM, x, BOTTOM + (major and 6 or 3), INK, 1)
    if major then
      text(x - 11, BOTTOM + 12, string.format("%.1f", ph), 11, INK)
    end
    i = i + 1
  end
  text((LEFT + RIGHT) / 2 - 8, BOTTOM + 32, "pH", 12, INK)
  text(LEFT - 56, TOP + 2, "more", 10, INK)
  text(LEFT - 56, TOP + 14, "available", 10, INK)
  text(LEFT - 56, BOTTOM - 18, "less", 10, INK)
  text(LEFT - 56, BOTTOM - 6, "available", 10, INK)
end

-- ============================================================
-- CHART PAGE (canvas + control columns)
-- ============================================================

local chart_page = bui.Node("frame")
chart_page:set("padding", 4)

cv = bui.Node("canvas")
chart_page:add(cv)
cv:geom(bui.GEOM_PACK, {side = 0, expand = 1, fill = 3})

-- Redraw the plot whenever the canvas geometry changes (window resize, or
-- being re-packed when the Chart tab comes back). The kit only reports
-- BK_EV_CONFIGURE to nodes carrying the Tk-style "events" flag (bk_arrange
-- checks NF_EVENTS), so turn it on first.
cv:set("events", 1)
cv:on(bui.EV_CONFIGURE, function(_, w, h)
  w, h = math.floor(w), math.floor(h)
  if w <= 0 or h <= 0 or (w == canvas_w and h == canvas_h) then
    return
  end
  canvas_w, canvas_h = w, h
  draw()
end)

local sep = bui.Node("separator")
chart_page:add(sep)
sep:geom(bui.GEOM_PACK, {side = 0, fill = 1})

local ctrl_row = bui.Node("row")
chart_page:add(ctrl_row)
ctrl_row:geom(bui.GEOM_PACK, {side = 0, fill = 1})

local function make_column(parent)
  local col = bui.Node("column")
  col:set("padding", 4)
  parent:add(col)
  col:geom(bui.GEOM_PACK, {side = 1})
  return col
end

local min_fields, max_fields = {}, {}

local function nutrient_row(parent, i)
  local n = nutrients[i]
  local row = bui.Node("row")
  row:set("padding", 2)
  parent:add(row)

  local name_lbl = bui.Node("label")
  name_lbl:set("text", n.name)
  row:add(name_lbl)

  local fmin = bui.Node("number")
  fmin:set("min", PH_MIN)
  fmin:set("max", PH_MAX)
  fmin:set("step", 0.1)
  fmin:set("precision", 1)
  fmin:set("value", n.ph_min)
  fmin:on(bui.EV_CHANGE, function()
    local v = clamp(fmin:get("value") or n.ph_min, PH_MIN, n.ph_max)
    fmin:set("value", v)
    n.ph_min = v
    after_chart_edit()
    status(("%s: %.1f - %.1f"):format(n.name, n.ph_min, n.ph_max))
  end)
  row:add(fmin)
  min_fields[i] = fmin

  local dash = bui.Node("label")
  dash:set("text", " - ")
  row:add(dash)

  local fmax = bui.Node("number")
  fmax:set("min", PH_MIN)
  fmax:set("max", PH_MAX)
  fmax:set("step", 0.1)
  fmax:set("precision", 1)
  fmax:set("value", n.ph_max)
  fmax:on(bui.EV_CHANGE, function()
    local v = clamp(fmax:get("value") or n.ph_max, n.ph_min, PH_MAX)
    fmax:set("value", v)
    n.ph_max = v
    after_chart_edit()
    status(("%s: %.1f - %.1f"):format(n.name, n.ph_min, n.ph_max))
  end)
  row:add(fmax)
  max_fields[i] = fmax
end

local col_a = make_column(ctrl_row)
local head_a = bui.Node("label")
head_a:set("text", "Nutrient pH ranges")
col_a:add(head_a)

local col_b = make_column(ctrl_row)
col_b:add(bui.Node("separator"))

-- Rebuild the two nutrient columns from the model (after add/delete/undo).
rebuild_chart_rows = function()
  while col_a:child_count() > 1 do col_a:child(1):free() end
  while col_b:child_count() > 1 do col_b:child(1):free() end
  min_fields, max_fields = {}, {}
  local half = math.ceil(#nutrients / 2)
  for i = 1, half do nutrient_row(col_a, i) end
  for i = half + 1, #nutrients do nutrient_row(col_b, i) end
end
rebuild_chart_rows()

local col_c = make_column(ctrl_row)
local head_c = bui.Node("label")
head_c:set("text", "Availability curves")
col_c:add(head_c)

local sliders

local function curve_slider(c, field, lo, hi, label)
  local row = bui.Node("row")
  row:set("padding", 2)
  col_c:add(row)
  local lbl = bui.Node("label")
  lbl:set("text", label)
  row:add(lbl)
  local sl = bui.Node("slider")
  sl:set("min", lo)
  sl:set("max", hi)
  sl:set("value", c[field])
  sl:on(bui.EV_CHANGE, function()
    c[field] = clamp(sl:get("value") or c[field], lo, hi)
    after_chart_edit()
    status(("%s %s = %.2f"):format(c.label, field, c[field]))
  end)
  row:add(sl)
  sl:geom(bui.GEOM_PACK, {fill = 1})
  return sl
end

local function sync_curve_sliders()
  sliders[1]:set("value", curves.macro.center)
  sliders[2]:set("value", curves.macro.spread)
  sliders[3]:set("value", curves.micro.center)
  sliders[4]:set("value", curves.micro.spread)
end

sliders = {
  curve_slider(curves.macro, "center", 5.0, 8.5, "macro center"),
  curve_slider(curves.macro, "spread", 0.6, 3.0, "macro spread"),
  curve_slider(curves.micro, "center", 4.0, 7.5, "micro center"),
  curve_slider(curves.micro, "spread", 0.6, 3.0, "micro spread"),
}

local tg_grid = bui.Node("toggle")
tg_grid:set("text", "Grid")
tg_grid:set("value", 1)
tg_grid:on(bui.EV_CHANGE, function()
  show_grid = tg_grid:get("value") > 0
  draw()
end)
col_c:add(tg_grid)

local tg_curves = bui.Node("toggle")
tg_curves:set("text", "Curves")
tg_curves:set("value", 1)
tg_curves:on(bui.EV_CHANGE, function()
  show_curves = tg_curves:get("value") > 0
  draw()
end)
col_c:add(tg_curves)

-- Shared by the chart Reset button and the table Reset button.
local function restore_defaults()
  nutrients = deepcopy(DEFAULT_NUTRIENTS)
  curves = deepcopy(DEFAULT_CURVES)
  undo_stack, redo_stack = {}, {}
  after_model_change()
  sync_curve_sliders()
  if undo_btn then undo_btn:set("enabled", 0) end
  if redo_btn then redo_btn:set("enabled", 0) end
  status("Reset to default chart.")
end

local reset_btn = bui.Node("button")
reset_btn:set("text", "Reset defaults")
reset_btn:set("icon", bui.icon_id("LOOP_BACK"))
reset_btn:on(bui.EV_CLICK, restore_defaults)
col_c:add(reset_btn)
reset_btn:geom(bui.GEOM_PACK, {fill = 1, pady0 = 6})

-- ============================================================
-- TABLE PAGE (edit the same model as a Lua table)
-- ============================================================

local table_page = bui.Node("frame")
table_page:set("padding", 4)

local trow = bui.Node("row")
table_page:add(trow)
trow:geom(bui.GEOM_PACK, {side = 0, expand = 1, fill = 3})

local tree_node = bui.Node("tree")
tree_node:set_strs("headers", {"Key", "Type", "Value"})
tree_node:set_strs("columns", {"0", "2", "1", "3"})
trow:add(tree_node)
tree_node:geom(bui.GEOM_PACK, {fill = 1, expand = 1})

local edit_col = make_column(trow)
edit_col:set("padding", 6)

local ed_head = bui.Node("label")
ed_head:set("text", "Edit entry")
edit_col:add(ed_head)

local ed_key_lbl = bui.Node("label")
ed_key_lbl:set("text", "Key: -")
edit_col:add(ed_key_lbl)
local ed_type_lbl = bui.Node("label")
ed_type_lbl:set("text", "Type: -")
edit_col:add(ed_type_lbl)
local ed_val_lbl = bui.Node("label")
ed_val_lbl:set("text", "Value:")
edit_col:add(ed_val_lbl)

local ed_str = bui.Node("text")
ed_str:set("placeholder", "text value")
edit_col:add(ed_str)
local ed_num = bui.Node("number")
ed_num:set("min", -20)
ed_num:set("max", 20)
ed_num:set("step", 0.01)
ed_num:set("precision", 2)
edit_col:add(ed_num)

local ed_apply = bui.Node("button")
ed_apply:set("text", "Apply")
ed_apply:set("enabled", 0)
edit_col:add(ed_apply)
ed_apply:geom(bui.GEOM_PACK, {fill = 1, pady0 = 4})

local add_btn = bui.Node("button")
add_btn:set("text", "+ Nutrient")
add_btn:set("icon", bui.icon_id("ADD"))
edit_col:add(add_btn)

local del_btn = bui.Node("button")
del_btn:set("text", "Delete Nutrient")
del_btn:set("icon", bui.icon_id("X"))
del_btn:set("enabled", 0)
edit_col:add(del_btn)

local sep_t = bui.Node("separator")
edit_col:add(sep_t)

undo_btn = bui.Node("button")
undo_btn:set("text", "Undo")
undo_btn:set("icon", bui.icon_id("LOOP_BACK"))
undo_btn:set("enabled", 0)
edit_col:add(undo_btn)

redo_btn = bui.Node("button")
redo_btn:set("text", "Redo")
redo_btn:set("icon", bui.icon_id("LOOP_FORWARDS"))
redo_btn:set("enabled", 0)
edit_col:add(redo_btn)

local treset_btn = bui.Node("button")
treset_btn:set("text", "Reset defaults")
treset_btn:set("icon", bui.icon_id("LOOP_BACK"))
edit_col:add(treset_btn)
treset_btn:geom(bui.GEOM_PACK, {fill = 1, pady0 = 6})
treset_btn:on(bui.EV_CLICK, restore_defaults)

-- Tree building: every item carries the live container/key it edits.
local item_meta, tree_id_count, sel_id

local function add_tree_item(parent_item, key_str, kind, container, key, disp, nutrient_i)
  local icon = kind == "group" and bui.icon_id("OUTLINER_COLLECTION") or bui.icon_id("FILE")
  local it = bui.TreeItem(key_str, icon)
  it:set_values({key_str, kind == "group" and "table" or kind, disp})
  tree_id_count = tree_id_count + 1
  it:set_ref(tree_id_count)
  item_meta[tree_id_count] = {kind = kind, container = container, key = key,
                              label = key_str, nutrient_i = nutrient_i}
  if parent_item then parent_item:add(it) end
  return it
end

local function fmt_num(v)
  return string.format("%g", v)
end

build_data_tree = function()
  if not tree_node then return end
  item_meta, tree_id_count, sel_id = {}, 0, nil
  local roots = {}

  local g = add_tree_item(nil, "nutrients", "group", nil, nil, #nutrients .. " items")
  roots[#roots + 1] = g
  for i, n in ipairs(nutrients) do
    local ni = add_tree_item(g, "[" .. i .. "]", "group", nil, nil, n.name, i)
    add_tree_item(ni, "name", "string", n, "name", string.format("%q", n.name), i)
    add_tree_item(ni, "ph_min", "number", n, "ph_min", fmt_num(n.ph_min), i)
    add_tree_item(ni, "ph_max", "number", n, "ph_max", fmt_num(n.ph_max), i)
    local cg = add_tree_item(ni, "color", "group", nil, nil, "3 items", i)
    for ch = 1, 3 do
      add_tree_item(cg, "[" .. ch .. "]", "number", n.color, ch, fmt_num(n.color[ch]), i)
    end
  end

  local cg2 = add_tree_item(nil, "curves", "group", nil, nil, "2 items")
  roots[#roots + 1] = cg2
  for _, cname in ipairs({"macro", "micro"}) do
    local c = curves[cname]
    local ci = add_tree_item(cg2, cname, "group", nil, nil, "4 items")
    add_tree_item(ci, "label", "string", c, "label", string.format("%q", c.label))
    add_tree_item(ci, "center", "number", c, "center", fmt_num(c.center))
    add_tree_item(ci, "spread", "number", c, "spread", fmt_num(c.spread))
    local col = add_tree_item(ci, "color", "group", nil, nil, "4 items")
    for ch = 1, 4 do
      add_tree_item(col, "[" .. ch .. "]", "number", c.color, ch, fmt_num(c.color[ch]))
    end
  end

  tree_node:set_roots(roots)
  tree_node:relayout()
  ed_key_lbl:set("text", "Key: -")
  ed_type_lbl:set("text", "Type: -")
  ed_str:set("enabled", 0)
  ed_num:set("enabled", 0)
  ed_apply:set("enabled", 0)
  del_btn:set("enabled", 0)
end

local function clear_editor()
  sel_id = nil
  ed_key_lbl:set("text", "Key: -")
  ed_type_lbl:set("text", "Type: -")
  ed_str:set("value", "")
  ed_str:set("enabled", 0)
  ed_num:set("value", 0)
  ed_num:set("enabled", 0)
  ed_apply:set("enabled", 0)
  del_btn:set("enabled", 0)
end

local function tree_select()
  local sel = tree_node:selected()
  if not sel then clear_editor(); return end
  local m = item_meta[sel:ref()]
  if not m or m.kind == "group" then
    clear_editor()
    if m then
      ed_key_lbl:set("text", "Key: " .. m.label)
      ed_type_lbl:set("text", "Type: table (select a leaf to edit)")
      del_btn:set("enabled", m.nutrient_i and 1 or 0)
    end
    return
  end
  sel_id = sel:ref()
  ed_key_lbl:set("text", "Key: " .. m.label)
  ed_type_lbl:set("text", "Type: " .. m.kind)
  local cur = m.container[m.key]
  if m.kind == "string" then
    ed_str:set("value", tostring(cur))
    ed_str:set("enabled", 1)
    ed_num:set("enabled", 0)
  else
    ed_num:set("value", tonumber(cur) or 0)
    ed_num:set("enabled", 1)
    ed_str:set("enabled", 0)
  end
  ed_apply:set("enabled", 1)
  del_btn:set("enabled", m.nutrient_i and 1 or 0)
end

tree_node:on(bui.EV_ACTIVATE, tree_select)
tree_node:on(bui.EV_CHANGE, tree_select)

local function refresh_undo_buttons()
  undo_btn:set("enabled", #undo_stack > 0 and 1 or 0)
  redo_btn:set("enabled", #redo_stack > 0 and 1 or 0)
end

local function push_undo()
  table.insert(undo_stack, {nutrients = deepcopy(nutrients), curves = deepcopy(curves)})
  redo_stack = {}
  refresh_undo_buttons()
end

local function apply_tree_edit()
  if not sel_id then return end
  local m = item_meta[sel_id]
  if not m or m.kind == "group" or not m.container then return end
  local v
  if m.kind == "string" then
    v = ed_str:get("text")
    if not v then return end
  else
    v = tonumber(ed_num:get("value"))
    if not v then return end
  end
  push_undo()
  m.container[m.key] = v
  after_model_change()
  status(("Applied %s = %s"):format(m.label, tostring(v)))
end
ed_apply:on(bui.EV_CLICK, apply_tree_edit)
ed_str:on(bui.EV_SUBMIT, apply_tree_edit)

add_btn:on(bui.EV_CLICK, function()
  push_undo()
  nutrients[#nutrients + 1] = {
    name = "New Nutrient", ph_min = 5.0, ph_max = 8.0, color = {0.40, 0.50, 0.40}}
  after_model_change()
  status("Added nutrient (" .. #nutrients .. " total)")
end)

del_btn:on(bui.EV_CLICK, function()
  local m = sel_id and item_meta[sel_id]
  local idx = m and m.nutrient_i
  if not idx or not nutrients[idx] then
    status("Select a nutrient row (or a field inside one) to delete")
    return
  end
  push_undo()
  local name = nutrients[idx].name
  table.remove(nutrients, idx)
  clear_editor()
  after_model_change()
  status("Deleted " .. name .. " (" .. #nutrients .. " left)")
end)

local function undo_action()
  if #undo_stack == 0 then return end
  table.insert(redo_stack, {nutrients = deepcopy(nutrients), curves = deepcopy(curves)})
  local s = table.remove(undo_stack)
  nutrients, curves = s.nutrients, s.curves
  clear_editor()
  after_model_change()
  refresh_undo_buttons()
  sync_curve_sliders()
  status("Undo")
end

local function redo_action()
  if #redo_stack == 0 then return end
  table.insert(undo_stack, {nutrients = deepcopy(nutrients), curves = deepcopy(curves)})
  local s = table.remove(redo_stack)
  nutrients, curves = s.nutrients, s.curves
  clear_editor()
  after_model_change()
  refresh_undo_buttons()
  sync_curve_sliders()
  status("Redo")
end

undo_btn:on(bui.EV_CLICK, undo_action)
redo_btn:on(bui.EV_CLICK, redo_action)

-- ============================================================
-- TAB SWITCHING
-- ============================================================

-- Pages are children of the tabs node: tabs_arrange packs the current child
-- below the bar (hiding the rest) and tabs_select raises EV_CHANGE. Pages
-- still need pack geometry to count as managed (drawn) children.
for _, p in ipairs({chart_page, table_page}) do
  tabs:add(p)
  p:geom(bui.GEOM_PACK, {side = 0, expand = 1, fill = 3})
end
tabs:set("value", 0)

tabs:on(bui.EV_CHANGE, function(_, which)
  if (which or tabs:get("value") or 0) + 1 == 1 then
    draw() -- hidden canvases keep stale items; refresh on the way back
    status("Chart tab: edit the fields below; the graph redraws live.")
  else
    if tree_dirty then build_data_tree(); tree_dirty = false end
    status("Table tab: expand nutrients/curves, select a leaf, Apply.")
  end
end)

-- ============================================================
-- MAIN
-- ============================================================

print("Soil pH and Nutrient Availability")
print("Chart tab: number fields + sliders | Table tab: edit the same data\nas a Lua table (tree view, Apply, add/delete, undo/redo).\n")

draw()
build_data_tree()

local ctx = bui.Context(root)
local win = bui.Window(ctx, "Soil pH and Nutrient Availability", WIN_W, WIN_H)

if win then
  win:show()
  bui.run()
else
  local px, _, w, h = ctx:render(WIN_W, WIN_H, 1)
  print("No window system. Headless render: " .. w .. "x" .. h .. " " .. #px .. " bytes")
end
