# frozen_string_literal: true

# SPDX-License-Identifier: GPL-2.0-or-later

require "test_helper"

class TestEvents < Minitest::Test
  include Headless

  def test_click_and_change
    root = BUI::Node.new("column").set(padding: 8)
    button = BUI::Node.new("button").set(text: "Go")
    check = BUI::Node.new("checkbox").set(label: "On")
    root << button << check
    events = []
    button.on_click { |e| events << [e.kind, e.node] }
    check.on_change { |e| events << [e.kind, e.node.value] }
    seen = []
    show(root) do |ctx, raster|
      ctx.on_event { |e| seen << e.kind }
      click(ctx, button)
      click(ctx, check)
      assert_equal [[:click, button], [:change, 1.0]], events
      assert_includes seen, :click
      assert_same check, ctx.hit_at(*check.rect.center)
      ctx.render(raster)
      assert_operator raster.rgba.bytes.uniq.size, :>, 2 # a drawn frame
    end
  end

  def test_menus_and_bindings
    root = BUI::Node.new("column")
    mb = BUI::Node.new("menubutton").set(text: "File")
    root << mb
    opened = 0
    mb.menu = [{ text: "Open", shortcut: "Ctrl O", action: -> { opened += 1 } }, nil, { text: "Quit" }]
    show(root, 300, 200) do |ctx, raster|
      click(ctx, mb)
      ctx.render(raster)
      popups = ctx.popups
      assert_equal 1, popups.size
      assert_equal "Open", popups[0].menu_rows.first
      x, y = popups[0].menu_row_rect(0).center
      ctx.motion(x, y)
      ctx.click(x, y)
      assert_equal 1, opened

      saved = 0
      assert ctx.bind("ctrl+s") { saved += 1 }
      ctx.key("", "s", BUI::CTRL)
      assert_equal 1, saved
    end
  end

  def test_an_error_in_a_handler_comes_out_of_the_call
    root = BUI::Node.new("column")
    button = BUI::Node.new("button").set(text: "Boom")
    root << button
    button.on_click { raise IOError, "boom" }
    show(root) do |ctx, _|
      err = assert_raises(IOError) { click(ctx, button) }
      assert_equal "boom", err.message
      button.on_click { break }
      assert_raises(LocalJumpError) { click(ctx, button) }
      assert_raises(LocalJumpError) { catch(:out) { button.on_click { throw :out }; click(ctx, button) } }
      calls = 0
      button.on_click { calls += 1 }
      click(ctx, button) # the kit is fine afterwards
      assert_equal 1, calls
    end
  end

  def test_a_handler_may_free_its_own_row
    root = BUI::Node.new("column")
    rows = Array.new(3) do |i|
      BUI::Node.new("row").tap do |row|
        delete = BUI::Node.new("button").set(text: "Delete #{i}")
        delete.on_click { row.free }
        row << delete
        root << row
      end
    end
    show(root) do |ctx, raster|
      click(ctx, rows[1].child(0))
      assert rows[1].freed?
      assert_equal 2, root.child_count
      ctx.render(raster)
    end
  end

  def test_a_handler_may_free_its_context
    root = BUI::Node.new("column")
    button = BUI::Node.new("button").set(text: "Close")
    root << button
    ctx = BUI::Context.new(root)
    ctx.render(BUI::Raster.new(200, 100))
    button.on_click { ctx.free }
    click(ctx, button)
    assert ctx.freed? # once the click returned
  ensure
    root.free
  end

  def test_other_threads_queue_work
    ran = []
    Thread.new { BUI.on_main { ran << Thread.current } }.join
    assert_empty ran
    BUI.pump
    assert_equal [Thread.main], ran
  end
end
