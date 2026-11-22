# frozen_string_literal: true

# A to-do list: rows added and removed while the window runs, a menu, a key
# binding, and a clock that a background thread keeps up to date.
#
#   ruby -Ilib examples/todos.rb
#
# SPDX-License-Identifier: GPL-2.0-or-later

require "bui"

BUI.app do
  list = clock = nil

  add = lambda do |title|
    row = BUI::Node.new("row").set(spacing: 4)
    list << row
    BUI::Builder.new(row).build do
      checkbox(title)
      button("Delete", weight: 0) { row.free } # a handler may free its own row
    end
  end
  clear = -> { list.children.each(&:free) }

  win = window("To-dos", width: 360, height: 320, min_size: [280, 200]) do
    row spacing: 4 do
      edit = [
        { text: "Add three", action: -> { 3.times { |i| add.("Item #{i + 1}") } } },
        nil,
        { text: "Clear", shortcut: "Ctrl Delete", action: clear }
      ]
      menu_button "Edit", edit, weight: 0
      draft = text_field("", placeholder: "New to-do")
      draft.on_submit do
        add.(draft.value) unless draft.value.strip.empty?
        draft.value = ""
      end
    end
    list = scroll
    clock = label ""
  end
  win.context.bind("ctrl+Delete", &clear)

  # The kit has one thread: another one changes the window through update.
  Thread.new do
    loop do
      now = Time.now.strftime("%H:%M:%S")
      win.update { clock.text = "It is #{now}" unless clock.freed? }
      sleep 1
    end
  end
end
