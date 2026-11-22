# frozen_string_literal: true

# SPDX-License-Identifier: GPL-2.0-or-later

require "test_helper"

class TestNode < Minitest::Test
  include Headless

  def test_properties
    s = BUI::Node.new("slider").set(label: "Size", min: 0, max: 10, value: 2.5)
    assert_equal "slider", s.type
    assert_equal "Size", s.str("label")
    assert_equal 2.5, s.value
    d = BUI::Node.new("dropdown").set(items: %w[A B])
    assert_equal %w[A B], d.strs("items")
    t = BUI::Node.new("text").set(value: "hi")
    assert_equal "hi", t.value
    t.text = "ünïcode"
    assert_equal "ünïcode", t.text
    assert_equal Encoding::UTF_8, t.text.encoding
    [s, d, t].each(&:free)
  end

  def test_bad_values_are_refused
    assert_raises(ArgumentError) { BUI::Node.new("no_such_type") }
    n = BUI::Node.new("button")
    assert_raises(ArgumentError) { n[:no_such_key] = 1 }
    assert_raises(TypeError) { n[:text] = Object.new }
    assert_raises(ArgumentError) { n[:text] = "a\0b" }
    assert_raises(ArgumentError) { n.on(:no_such_event) {} }
    assert_raises(TypeError) { n << "a string" }
  ensure
    n&.free
  end

  def test_tree
    root = BUI::Node.new("column")
    a, b, c = Array.new(3) { BUI::Node.new("label") }
    root << a << c
    root.add(b, 1)
    assert_equal [a, b, c], root.children
    assert_same root, b.parent
    assert_same c, root.child(2)
    assert_nil root.child(3)
    assert_same b, root.remove(b)
    assert_nil b.parent
    assert_raises(ArgumentError) { root.remove(b) }
    b.free

    # a node under itself would make the tree a cycle
    a << (inner = BUI::Node.new("row"))
    assert_raises(ArgumentError) { inner << root }
    assert_raises(ArgumentError) { root << root }

    root.free
    assert [root, a, c, inner].all?(&:freed?)
    assert_raises(BUI::FreedError) { a.text }
    root.free # a second free does nothing
  end

  def test_nothing_is_kept_after_free
    before = BUI.references
    root = BUI::Node.new("column")
    10.times { root << BUI::Node.new("button").on_click {} }
    root << BUI::Node.new("menubutton").tap { |m| m.menu = [{ text: "Open", action: -> {} }] }
    root.free
    assert_equal before, BUI.references
  end

  def test_layout_managers
    frame = BUI::Node.new("frame")
    left = BUI::Node.new("button").pack(side: :left, fill: :y, padx: [2, 4])
    frame << left
    assert_equal BUI::GEOM_PACK, left.geometry_manager
    assert_equal 2, left.geometry_get("side")
    assert_equal 2, left.geometry_get("fill")
    right = BUI::Node.new("label").grid(row: 1, column: 2, sticky: "nsew")
    frame << right
    assert_equal BUI::ANCHOR_N | BUI::ANCHOR_S | BUI::ANCHOR_E | BUI::ANCHOR_W, right.geometry_get("sticky")
    assert_raises(ArgumentError) { right.pack(side: :middle) }
  ensure
    frame&.free
  end

  def test_text_area
    t = BUI::Node.new("textarea").set(value: "hello")
    t.text_insert(5, " world")
    assert_equal "hello world", t.value
    t.text_delete(0, 6)
    assert_equal "world", t.value
    assert_raises(IndexError) { t.text_insert(99, "x") }
    t.select(0, 2)
    assert_equal 0...2, t.selection
  ensure
    t&.free
  end

  def test_canvas
    cv = BUI::Node.new("canvas")
    cv.canvas_add(id: 1, type: :rect, coords: [0, 0, 10, 10], fill: [1, 0, 0])
    cv.canvas_set(id: 2, type: :text, coords: [5, 5], text: "hi", fill: BUI::Color.hex("#fff"),
                  family: "Inter", bold: true)
    cv.canvas_add(id: 3, type: :image, coords: [0, 0, 2, 1], rgba: "\xff".b * 8, image_size: [2, 1])
    assert_equal 3, cv.canvas_count
    assert_raises(ArgumentError) { cv.canvas_add(type: :hexagon) }
    assert_raises(ArgumentError) { cv.canvas_add(type: :image, rgba: "x", image_size: [2, 2]) }
    cv.canvas_delete(:all)
    assert_equal 0, cv.canvas_count
  ensure
    cv&.free
  end

  def test_colors_and_theme
    assert_equal BUI::Color.new(1.0, 1.0, 1.0, 1.0), BUI::Color.hex("#fff")
    assert_raises(ArgumentError) { BUI::Color.hex("#12") }
    # BUI.themes lists the loaded ones; "dark" and "light" are built in
    assert_equal 1, BUI.load_theme_json('{"themes": [{"name": "Ruby Red", "mode": "dark", ' \
                                        '"colors": {"button.background": "#9b111e"}}]}')
    assert_includes BUI.themes, "Ruby Red"
    assert_raises(BUI::Error) { BUI.load_theme_json("{") }
    BUI.theme = "light"
    assert_equal "light", BUI.theme_mode
    assert_kind_of BUI::Color, BUI.theme_color("text")
    assert_raises(ArgumentError) { BUI.theme = "no such theme" }
    BUI.theme = "dark"
    assert_equal 7, BUI.eval_number("2*3+1")
    assert_nil BUI.eval_number("2*")
    assert_operator BUI.icon("FILE_FOLDER"), :>, 0
    assert_operator BUI.text_width("Hello"), :>, 0
  end
end
