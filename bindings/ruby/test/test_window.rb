# frozen_string_literal: true

# Native windows: only where a display is (BUI_TEST_WINDOWS=1 asks for it).
#
# SPDX-License-Identifier: GPL-2.0-or-later

require "test_helper"

class TestWindow < Minitest::Test
  def setup
    skip "set BUI_TEST_WINDOWS=1 with a display to test native windows" unless ENV["BUI_TEST_WINDOWS"]
  end

  def test_run_until_closed
    closes = 0
    other_thread = nil
    win = BUI::Window.new("Test", width: 200, height: 100) do
      label "Closing soon"
    end
    win.on_close { closes += 1 }
    Thread.new { sleep 0.05; win.update { other_thread = :ran } }
    win.timer(200) { win.close }
    BUI.run
    assert_equal :ran, other_thread
    refute win.open?
    win.free
    assert win.freed?
  end

  def test_an_error_stops_the_loop
    win = BUI::Window.new("Test", width: 200, height: 100)
    win.timer(10) { raise ArgumentError, "from a timer" }
    err = assert_raises(ArgumentError) { BUI.run }
    assert_equal "from a timer", err.message
  ensure
    win&.free
  end

  def test_an_interrupt_ends_the_loop
    win = BUI::Window.new("Test", width: 200, height: 100)
    # what Ctrl+C does: an exception raised into the thread that waits in the loop
    Thread.new { sleep 0.1; Thread.main.raise(Interrupt) }
    assert_raises(Interrupt) { BUI.run }
  ensure
    win&.free
  end

  def test_free_inside_a_callback
    win = BUI::Window.new("Test", width: 200, height: 100)
    win.timer(10) { win.free }
    BUI.run
    assert win.freed?
  end
end
