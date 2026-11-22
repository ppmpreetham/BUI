# frozen_string_literal: true

module BUI
  # A callback from the kit, given to Node#on handlers and Context#on_event.
  #
  # node  the node it happened to (a BUI::Node), nil for a key binding
  # kind  one of BUI::EVENT_KINDS: :change, :click, :call, :submit, :key, :press...
  # a, b  numbers that depend on the kind: a position for raw pointer events, the
  #       new value for :change, a row index for :rename and :activate, the size
  #       for :configure, the modifiers (BUI::SHIFT | BUI::CTRL | BUI::ALT) for keys
  # text  the typed text and the X11 keysym of :key ("a\ta", "\tReturn"), a new
  #       name for :rename
  # item  the BUI::TreeItem of a :toggle
  Event = Struct.new(:node, :kind, :a, :b, :text, :item) do
    alias_method :x, :a
    alias_method :y, :b

    # The text a key typed ("" for none).
    def typed
      text.to_s.split("\t", 2).first.to_s
    end

    # The key's X11 keysym: "Return", "Up", "a".
    def keysym
      text.to_s.split("\t", 2)[1].to_s
    end
  end
end
