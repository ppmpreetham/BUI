-- Soil pH and nutrient availability chart for bui-lua.
-- A Truog-style graph custom-drawn on a canvas node: pH scale (4.0-10.0),
-- horizontal availability bands per nutrient, and two overlapping
-- availability curves that peak in the slightly acidic to neutral range.
-- Every plotted value is editable with ordinary widgets below the chart.
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

local nutrients = {}
for i, n in ipairs(DEFAULT_NUTRIENTS) do
  nutrients[i] = {name = n.name, ph_min = n.ph_min, ph_max = n.ph_max, color = n.color}
end
local curves = {
  macro = {label = DEFAULT_CURVES.macro.label, center = DEFAULT_CURVES.macro.center,
           spread = DEFAULT_CURVES.macro.spread, color = DEFAULT_CURVES.macro.color},
  micro = {label = DEFAULT_CURVES.micro.label, center = DEFAULT_CURVES.micro.center,
           spread = DEFAULT_CURVES.micro.spread, color = DEFAULT_CURVES.micro.color},
}
local show_grid, show_curves = true, true

-- ============================================================
-- UI SHELL
-- ============================================================

local root = bui.Node("frame")
root:set("padding", 8)

local title = bui.Node("label")
title:set("text", "Soil pH and Nutrient Availability")
root:add(title)
title:geom(bui.GEOM_PACK, {side = 0, fill = 1})

local cv = bui.Node("canvas")
root:add(cv)
cv:geom(bui.GEOM_PACK, {side = 0, expand = 1, fill = 3})

local sep = bui.Node("separator")
root:add(sep)
sep:geom(bui.GEOM_PACK, {side = 0, fill = 1})

-- Controls row: two nutrient columns + a curves/options column.
local ctrl_row = bui.Node("row")
root:add(ctrl_row)
ctrl_row:geom(bui.GEOM_PACK, {side = 0, fill = 1})

local function make_column(parent)
  local col = bui.Node("column")
  col:set("padding", 4)
  parent:add(col)
  col:geom(bui.GEOM_PACK, {side = 1})
  return col
end

local status_node = bui.Node("label")
status_node:set("text", "Edit the fields below; the graph redraws live.")
root:add(status_node)
status_node:geom(bui.GEOM_PACK, {side = 0, fill = 1})

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

local function status(msg)
  status_node:set("text", msg)
end

-- ============================================================
-- DRAWING
-- ============================================================

local function draw()
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
  local row_h = (BOTTOM - TOP - 8) / #nutrients
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

-- Redraw the plot whenever the canvas geometry changes (window resize).
-- The kit only reports BK_EV_CONFIGURE to nodes carrying the Tk-style
-- "events" flag (bk_arrange checks NF_EVENTS), so turn it on first.
cv:set("events", 1)
cv:on(bui.EV_CONFIGURE, function(_, w, h)
  w, h = math.floor(w), math.floor(h)
  if w <= 0 or h <= 0 or (w == canvas_w and h == canvas_h) then
    return
  end
  canvas_w, canvas_h = w, h
  draw()
end)

-- ============================================================
-- EDIT WIDGETS
-- ============================================================

local min_fields, max_fields = {}, {}

local function clamp_ph(v, lo, hi)
  if v < PH_MIN then v = PH_MIN end
  if v > PH_MAX then v = PH_MAX end
  if v < lo then v = lo end
  if v > hi then v = hi end
  return v
end

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
    local v = clamp_ph(fmin:get("value") or n.ph_min, PH_MIN, n.ph_max)
    fmin:set("value", v)
    n.ph_min = v
    draw()
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
    local v = clamp_ph(fmax:get("value") or n.ph_max, n.ph_min, PH_MAX)
    fmax:set("value", v)
    n.ph_max = v
    draw()
    status(("%s: %.1f - %.1f"):format(n.name, n.ph_min, n.ph_max))
  end)
  row:add(fmax)
  max_fields[i] = fmax
end

local col_a = make_column(ctrl_row)
local head_a = bui.Node("label")
head_a:set("text", "Nutrient pH ranges")
col_a:add(head_a)
for i = 1, 6 do nutrient_row(col_a, i) end

local col_b = make_column(ctrl_row)
col_b:add(bui.Node("separator"))
for i = 7, #nutrients do nutrient_row(col_b, i) end

local col_c = make_column(ctrl_row)
local head_c = bui.Node("label")
head_c:set("text", "Availability curves")
col_c:add(head_c)

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
    c[field] = sl:get("value") or c[field]
    draw()
    status(("%s %s = %.2f"):format(c.label, field, c[field]))
  end)
  row:add(sl)
  sl:geom(bui.GEOM_PACK, {fill = 1})
  return sl
end

local sliders = {
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

local reset_btn = bui.Node("button")
reset_btn:set("text", "Reset defaults")
reset_btn:set("icon", bui.icon_id("LOOP_BACK"))
reset_btn:on(bui.EV_CLICK, function()
  for i, n in ipairs(nutrients) do
    local d = DEFAULT_NUTRIENTS[i]
    n.ph_min, n.ph_max = d.ph_min, d.ph_max
    min_fields[i]:set("value", d.ph_min)
    max_fields[i]:set("value", d.ph_max)
  end
  curves.macro.center, curves.macro.spread = DEFAULT_CURVES.macro.center, DEFAULT_CURVES.macro.spread
  curves.micro.center, curves.micro.spread = DEFAULT_CURVES.micro.center, DEFAULT_CURVES.micro.spread
  sliders[1]:set("value", curves.macro.center)
  sliders[2]:set("value", curves.macro.spread)
  sliders[3]:set("value", curves.micro.center)
  sliders[4]:set("value", curves.micro.spread)
  draw()
  status("Reset to default chart.")
end)
col_c:add(reset_btn)
reset_btn:geom(bui.GEOM_PACK, {fill = 1, pady0 = 6})

-- ============================================================
-- MAIN
-- ============================================================

print("Soil pH and Nutrient Availability")
print("Edit nutrient ranges (number fields) and curves (sliders);\nthe canvas graph redraws live.\n")

draw()

local ctx = bui.Context(root)
local win = bui.Window(ctx, "Soil pH and Nutrient Availability", WIN_W, WIN_H)

if win then
  win:show()
  bui.run()
else
  local px, _, w, h = ctx:render(WIN_W, WIN_H, 1)
  print("No window system. Headless render: " .. w .. "x" .. h .. " " .. #px .. " bytes")
end
