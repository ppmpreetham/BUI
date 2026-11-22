# frozen_string_literal: true

# The GC and the kit together: objects the kit holds survive collection and
# compaction, and what nothing holds goes.
#
# SPDX-License-Identifier: GPL-2.0-or-later

require "test_helper"

class TestMemory < Minitest::Test
  include Headless

  def test_handlers_survive_compaction
    root = BUI::Node.new("column")
    clicks = 0
    buttons = Array.new(20) { |i| BUI::Node.new("button").set(text: "B#{i}").on_click { clicks += 1 } }
    buttons.each { |b| root << b }
    target = buttons[7].rect # wrappers are found again through the kit after a GC
    buttons = nil # rubocop:disable Lint/UselessAssignment
    GC.start
    GC.compact if GC.respond_to?(:compact)
    show(root) do |ctx, _|
      ctx.click(*root.child(7).rect.center)
      assert_equal 1, clicks
      assert_equal "B7", root.child(7).text
      refute_nil target
    end
  end

  def test_menu_actions_go_when_replaced
    root = BUI::Node.new("column")
    mb = BUI::Node.new("menubutton")
    root << mb
    before = BUI.references
    mb.menu = Array.new(5) { |i| { text: "Item #{i}", action: -> {} } }
    assert_equal before + 5, BUI.references
    mb.menu = []
    assert_equal before, BUI.references
    root.free
  end

  def test_tree_items_belong_to_the_gc
    before = BUI.references
    tree = BUI::Node.new("tree")
    roots = Array.new(3) do |i|
      BUI::TreeItem.new("Root #{i}").tap { |r| 4.times { |j| r << BUI::TreeItem.new("Leaf #{i}.#{j}") } }
    end
    tree.tree_roots = roots
    roots = nil # rubocop:disable Lint/UselessAssignment
    GC.start
    GC.compact if GC.respond_to?(:compact)
    # still there: the tree holds the roots, the roots their leaves (and the tree's own wrapper)
    assert_equal before + 16, BUI.references
    show(tree, 200, 200) { |ctx, raster| ctx.render(raster) }
    # the tree is gone: its items are only Ruby's now, and Ruby has none (a root
    # lets go of its leaves as it is freed, so they go one collection later)
    3.times { GC.start(full_mark: true, immediate_sweep: true) }
    assert_equal before, BUI.references
  end

  def test_an_item_cannot_have_two_parents
    a, b, c = BUI::TreeItem.new("a"), BUI::TreeItem.new("b"), BUI::TreeItem.new("c")
    a << c
    assert_raises(ArgumentError) { b << c }
    assert_raises(ArgumentError) { c << a }
    assert_equal [c], a.children
    assert_same a, c.parent
  end

  def test_under_gc_stress
    GC.stress = true
    root = BUI::Node.new("column")
    3.times { |i| root << BUI::Node.new("button").set(text: "#{i}").on_click {} }
    root << BUI::Node.new("menubutton").tap { |m| m.menu = [{ text: "x", action: -> {} }] }
    assert_raises(ArgumentError) { root.child(0).menu = [] } # only menubuttons have menus
    ctx = BUI::Context.new(root)
    ctx.render(BUI::Raster.new(64, 64), full: true)
    ctx.click(*root.child(1).rect.center)
  ensure
    GC.stress = false
    ctx&.free
    root&.free
  end
end
