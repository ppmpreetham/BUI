# frozen_string_literal: true

# BUI for Ruby: Blender's interface as a library. Widgets, layout and interaction
# rules come from BUI's C kit (a C extension, no FFI); this file and lib/bui/ give
# them a Ruby face.
#
#   require "bui"
#
#   BUI.app do
#     window "Hello", width: 320, height: 120 do
#       count = 0
#       column padding: 8 do
#         shown = label "Clicked 0 times"
#         button("Click me") { shown.text = "Clicked #{count += 1} times" }
#       end
#     end
#   end
#
# The kit belongs to one thread: the one that calls BUI.run (BUI.app runs it).
# Call it from there and from its callbacks; other threads use BUI.on_main or
# Window#update.
#
# SPDX-License-Identifier: GPL-2.0-or-later

require_relative "bui/version"

# The compiled extension: lib/bui/<ruby version>/ in a precompiled gem, lib/bui/ otherwise.
begin
  require_relative "bui/#{RUBY_VERSION[/\d+\.\d+/]}/bui_ext"
rescue LoadError
  require_relative "bui/bui_ext"
end

require_relative "bui/event"
require_relative "bui/node"
require_relative "bui/context"
require_relative "bui/window"
require_relative "bui/builder"

module BUI
  @main_queue = Thread::Queue.new
  @later = []

  class << self
    # Runs the block on the kit's thread, soon, from any thread: the window loop
    # wakes for it. Before BUI.run, it waits for it.
    def on_main(&block)
      raise ArgumentError, "BUI.on_main needs a block" unless block

      @main_queue << block
      wake
      nil
    end

    # Builds windows with the builder (BUI::Builder) and runs until the last one
    # closes; the windows made here are freed after.
    #
    #   BUI.app { window("Notes") { label "Hi" } }
    def app(&block)
      raise Error, "no native windows here (BUI.window_supported? is false)" unless window_supported?

      app = App.new
      app.instance_exec(&block)
      run
    ensure
      app&.windows&.each(&:free)
    end

    # Called by the extension for every event: runs the action of a menu item or
    # key binding, the node's handler, then the context's.
    def dispatch(context, node, kind, a, b, text, extra)
      event = Event.new(node, kind, a, b, text, kind == :toggle ? extra : nil)
      extra.call if kind == :call && extra
      node&.emit(event)
      context&.emit(event)
    end

    # Called by the extension when the loop wakes, before and after it runs: the
    # frees callbacks left for later, then the blocks other threads queued.
    def drain_main
      later = @later
      @later = []
      later.each(&:call)
      loop do
        block = begin
          @main_queue.pop(true)
        rescue ThreadError
          break
        end
        block.call
      end
    end

    # Runs the block once no kit callback is running: at once, or between the
    # loop's messages. Frees of windows and contexts go through here.
    def later(&block)
      if in_callback?
        @later << block
        wake
      else
        block.call
      end
      nil
    end
  end

  # What BUI.app's block runs in: window makes a window, built with the builder.
  class App
    attr_reader :windows

    def initialize
      @windows = []
    end

    def window(title = "BUI", **options, &build)
      Window.new(title, **options, &build).tap { |w| @windows << w }
    end
  end
end
