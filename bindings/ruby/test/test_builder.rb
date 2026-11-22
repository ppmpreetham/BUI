# frozen_string_literal: true

# SPDX-License-Identifier: GPL-2.0-or-later

require "test_helper"

class TestBuilder < Minitest::Test
  include Headless

  def test_counter
    count = 0
    shown = plus = nil
    root = build do
      column padding: 8 do
        shown = label "0"
        row do
          button("-") { shown.text = (count -= 1).to_s }
          plus = button("+") { shown.text = (count += 1).to_s }
        end
      end
    end
    show(root) do |ctx, _|
      2.times { click(ctx, plus) }
      assert_equal "2", shown.text
      assert_equal 2, count
    end
  end

  def test_value_handlers
    got = {}
    root = build do
      checkbox("Sync") { |on| got[:sync] = on }
      slider("Volume", value: 20, min: 0, max: 100) { |v| got[:volume] = v }
      dropdown(%w[Free Pro Team]) { |i, item| got[:plan] = [i, item] }
      text_field("", placeholder: "Name") { |text| got[:name] = text }
    end
    sync, volume, plan, name = root.children
    show(root) do |ctx, _|
      assert_equal 20, volume.value # not clamped to the default range
      click(ctx, sync)
      volume.set_num("set_value", 75) # the user's edit: it notifies
      plan.set_num("set_value", 2)
      ctx.focus = name
      ctx.key("A", "A")
      ctx.key("", "Return")
      assert_equal({ sync: true, volume: 75.0, plan: [2, "Team"], name: "A" }, got)
    end
  end

  def test_a_block_with_an_argument_keeps_self
    root = BUI::Builder.new(BUI::Node.new("column")).build do |ui|
      ui.label helper_text
    end
    assert_equal "from the test", root.child(0).text
  ensure
    root&.free
  end

  def test_a_failed_widget_leaves_nothing
    before = BUI.references
    root = BUI::Node.new("column")
    assert_raises(ArgumentError) { BUI::Builder.new(root).build { button "x", no_such_key: 1 } }
    assert_equal 0, root.child_count
    root.free
    assert_equal before, BUI.references
  end

  def test_panel_and_frame
    root = build do
      panel "Details", open: false do
        label "Inside"
      end
      frame do
        button("Left").pack(side: :left)
      end
    end
    panel, frame = root.children
    assert_equal 1, panel.num("collapsed")
    assert_equal BUI::GEOM_PACK, frame.child(0).geometry_manager
    root.free
  end

  private

  def helper_text = "from the test"
end
