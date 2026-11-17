/* Integration tests for the D binding: the raw kit ABI, the idiomatic
 * wrappers, ownership and the event path.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
module run;

import bui;
import bui.internal.util : cslice;
import core.memory : GC;
import core.thread : Thread;
import std.file : read;
import std.path : buildPath, dirName;
import std.stdio : writeln;

private void check(bool condition, string message) {
    if (!condition) {
        throw new Error("bui test failed: " ~ message);
    }
}

void main() {
    testVersion();
    testNodes();
    testRenderAndInput();
    testImage();
    testAnimation();
    testTextarea();
    testCanvas();
    testTree();
    testMenus();
    testCustom();
    testHostErrors();
    testTheme();
    testCss();
    testHtml();
    testRsx();
    testReviewFixes();
    testReviewRegressions();
    testCapi();
    testScreen();
    writeln("bindings/d: all tests passed");
}

private void testVersion() {
    check(BK_version() == BUI_KIT_VERSION, "BK_version matches the D constants");
    check(cslice(BK_version_string()) == BUI_KIT_VERSION_STRING,
          "BK_version_string matches the D constants");
}

private void testNodes() {
    auto root = new Node("frame");
    auto label = new Node("label");
    check(label.typeName() == "label", "node type name");

    label.set("text", "hello");
    check(label.str("text") == "hello", "string property round-trip");
    label.set("visible", false);
    check(label.num("visible") == 0, "boolean property round-trip");
    label.set("visible", true);
    check(label.num("visible") == 1, "boolean property round-trip");

    root.add(label);
    check(root.childCount() == 1, "child count");
    check(root.child(0) is label, "the kit returns the existing D wrapper");
    check(label.parent() is root, "parent wrapper");

    auto extra = new Node("label");
    root.add(extra);
    check(root.childCount() == 2, "second child");
    root.remove(extra);
    check(root.childCount() == 1, "remove detaches the child");
    check(extra.parent() is null, "removed child has no parent");
    extra.free();

    auto dropdown = new Node("dropdown");
    dropdown.setStrs("items", ["one", "two"]);
    check(dropdown.strs("items").length == 2, "string list count");
    check(dropdown.strs("items")[1] == "two", "string list value");

    root.free();
}

private void testRenderAndInput() {
    auto root = new Node("column");
    root.set("padding", 6);

    auto title = new Node("label");
    title.set("text", "D binding");
    root.add(title);

    int clicks;
    auto button = new Node("button");
    button.set("text", "Push");
    button.onClick((NodeEvent) {
        ++clicks;
    });
    root.add(button);
    button.pack("fill", 1);

    auto context = new Context(root);
    auto raster = new Raster(240, 140);

    context.render(raster, true);
    check(raster.width == 240 && raster.height == 140, "raster size");
    check(raster.pixels.length == 240 * 140, "raster pixels");

    const rect = button.rect();
    check(rect[2] > 0 && rect[3] > 0, "button was laid out");

    button.set("text", "Push again");
    check(context.needsRender(), "a property change requests a render");

    const x = rect[0] + rect[2] / 2;
    const y = rect[1] + rect[3] / 2;
    context.press(x, y, 0);
    context.release(x, y, 0);
    check(clicks == 1, "click callback");

    auto hit = context.hitAt(x, y);
    bool inside = false;
    for (auto node = hit; node !is null; node = node.parent()) {
        if (node.handle is button.handle) {
            inside = true;
            break;
        }
    }
    check(inside, "hit test");
    check(context.hitAt(-50, -50) is null, "hit test miss");

    check(!raster.alpha(), "raster alpha off");
    raster.setAlpha(true);
    check(raster.alpha(), "raster alpha on");
    raster.clear(0, 0, 4, 4);

    context.clipboard("from D");
    check(context.clipboard() == "from D", "clipboard round-trip");

    raster.free();
    context.free();
    root.free();
}

private void testImage() {
    const(ubyte)[] rgba = [
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        255, 255, 255, 255,
    ];
    auto image = Image.fromRgba(rgba, 2, 2);
    check(image.width == 2 && image.height == 2, "image size");
    check(image.pixels.length == 4, "image pixels");
    check(image.toRgba.length == 16, "RGBA copy");
    check(image.frames == 1, "still image frame count");

    auto root = new Node("column");
    auto node = new Node("image");
    node.setImage(image);
    root.add(node);
    node.pack("fill", 1);

    auto context = new Context(root);
    auto raster = new Raster(80, 60);
    context.render(raster, true);
    check(context.pendingError() is null, "image node rendered");

    raster.free();
    context.free();
    root.free();
    image.free();
}

private void testAnimation() {
    const data = read(buildPath(dirName(__FILE_FULL_PATH__), "..", "..", "..",
                             "tests", "images", "anim.gif"));
    auto animation = new Animation(data);
    const info = animation.info();
    check(info[3] > 1, "animated image frame count");

    int delayMs;
    auto frame = animation.next(delayMs);
    check(frame !is null && frame.handle !is null, "animation frame");
    check(frame.width > 0 && frame.height > 0, "animation frame size");
    frame.free();
    animation.free();
}

private void testTextarea() {
    auto textarea = new Node("textarea");
    textarea.set("value", "hello");
    check(textarea.str("value") == "hello", "textarea initial value");

    insertText(textarea, 0, "abc");
    check(textarea.str("value") == "abchello", "textarea insert");
    check(caret(textarea) == 3, "caret after insert");

    setCaret(textarea, 5);
    check(caret(textarea) == 5, "set caret");

    setSelection(textarea, 1, 4);
    check(selection(textarea) == [1, 4], "selection");

    deleteText(textarea, 1, 4);
    check(textarea.str("value") == "aello", "textarea delete");

    setSpans(textarea, [TextSpan.foreground(0, 2, [1, 0, 0, 1])]);
    see(textarea, 0);

    textarea.free();
}

private void testCanvas() {
    auto root = new Node("frame");
    auto canvas = new Node("canvas");
    root.add(canvas);
    canvas.pack("fill", 1);

    setItem(canvas, CanvasItem.rect(1, 4, 4, 60, 40, [0.2f, 0.4f, 0.8f, 1]));
    addItem(canvas, CanvasItem.oval(2, 10, 10, 50, 30, [0.8f, 0.4f, 0.2f, 1]));
    check(itemCount(canvas) == 2, "canvas item count");

    putItem(canvas, 0, CanvasItem.label(3, 8, 8, "label", [1, 1, 1, 1]));
    raiseItem(canvas, 3, true);
    deleteItem(canvas, 2);
    check(itemCount(canvas) == 1, "canvas delete");
    addItem(canvas, CanvasItem.rect(4, 2, 2, 10, 10, [1, 0, 0, 1]));
    check(itemCount(canvas) == 2, "canvas add after delete");
    truncate(canvas, 1);
    check(itemCount(canvas) == 1, "canvas truncate");

    auto styled = CanvasItem.label(5, 8, 24, "styled", [1, 1, 1, 1]);
    styled.family = "Segoe " ~ "UI"; /* a temporary heap string */
    setItem(canvas, styled);
    styled.family = null;
    GC.collect(); /* the canvas keeps the font family alive */

    auto context = new Context(root);
    auto raster = new Raster(100, 80);
    context.render(raster, true);
    check(context.pendingError() is null, "canvas node rendered");

    raster.free();
    context.free();
    root.free();
}

private void testTree() {
    auto tree = new Node("tree");
    auto rootItem = new TreeItem("Root");
    auto child = new TreeItem("Child");
    rootItem.add(child);

    check(rootItem.childCount() == 1, "tree item child count");
    check(rootItem.child(0) is child, "tree item child wrapper");
    check(child.parent() is rootItem, "tree item parent");

    setRoots(tree, [rootItem]);
    check(rowCount(tree) == 2, "visible tree rows");
    check(rowItem(tree, 0) is rootItem, "first row");
    check(rowItem(tree, 1) is child, "second row");

    select(tree, child);
    check(selected(tree) is child, "tree selection");
    select(tree, null);
    check(selected(tree) is null, "tree selection cleared");

    rootItem.free();
    check(rowCount(tree) == 2, "a tree root cannot be freed while the tree holds it");

    child.setValues(["a", "b"]);
    check(child.label == "Child", "tree item label");

    rootItem.expanded = false;
    check(!rootItem.expanded, "tree item collapse");
    check(rowCount(tree) == 1, "collapsed tree rows");
    rootItem.expanded = true;

    setRoots(tree, []);
    check(rowCount(tree) == 0, "empty tree");

    tree.free();
}

private void testMenus() {
    auto root = new Node("frame");
    auto context = new Context(root);
    auto raster = new Raster(320, 200);

    int clicks;
    context.openMenu(root, [new MenuItem("One", {
        ++clicks;
    })]);
    check(context.popupCount() == 1, "menu popup opened");

    auto menu = context.popup(0);
    check(menu !is null, "menu popup handle");
    check(context.menuRowCount(menu) == 1, "menu row count");
    check(context.menuRowText(menu, 0) == "One", "menu row text");

    context.render(raster, true);
    const row = context.menuRowRect(menu, 0);
    check(row[2] > 0 && row[3] > 0, "menu row laid out");
    context.press(row[0] + row[2] / 2, row[1] + row[3] / 2, 0);
    context.release(row[0] + row[2] / 2, row[1] + row[3] / 2, 0);
    check(clicks == 1, "menu callback");
    check(context.popupCount() == 0, "menu closed after picking");

    raster.free();
    context.free();
    root.free();
}

private void testCustom() {
    auto root = new Node("column");
    auto node = new Node("custom");
    root.add(node);
    node.pack("fill", 1);

    int events, frames, draws;
    auto custom = setCustom(node, CustomHandlers(
        (Node, CustomEvent) {
            ++events;
            return 1;
        },
        (Node) {
            ++frames;
        },
        (Node, Raster, const(int)*) {
            ++draws;
        },
        (Node) {}, 80, 40));

    auto context = new Context(root);
    auto raster = new Raster(160, 100);
    context.render(raster, true);
    check(frames >= 1, "custom frame callback");
    check(draws >= 1, "custom draw callback");
    check(custom.raster !is null, "custom draw raster");
    check(custom.pendingError() is null, "no custom callback exception");

    const rect = node.rect();
    context.press(rect[0] + rect[2] / 2, rect[1] + rect[3] / 2, 0);
    context.release(rect[0] + rect[2] / 2, rect[1] + rect[3] / 2, 0);
    check(events >= 1, "custom event callback");

    raster.free();
    context.free();
    root.free();
}

private void testHostErrors() {
    auto root = new Node("frame");
    auto label = new Node("label");
    root.add(label);

    auto context = new Context(root);
    context.setHost(Host(request: {
        throw new Exception("request failed");
    }));

    label.set("text", "changed");
    check(context.pendingError() !is null, "host exception stored");

    bool threw;
    try {
        context.check();
    }
    catch (Exception e) {
        threw = e.msg == "request failed";
    }
    check(threw, "host exception rethrown");
    check(context.pendingError() is null, "host exception cleared");

    context.free();
    root.free();
}

private void testTheme() {
    check(setTheme("dark"), "dark theme");
    check(themeName() == "dark", "theme name");

    float[4] color;
    check(themeColor("region_bg", color), "theme color");
    check(color[3] > 0, "theme color alpha");

    check(setTheme("light"), "light theme");
    check(themeColor("regular.inner", color), "light theme color");
    check(setTheme("dark"), "restore dark theme");

    const shadow = textShadow();
    setTextShadow(!shadow);
    check(textShadow() == !shadow, "text shadow");
    setTextShadow(shadow);

    const points = fontSizePoints();
    setFontSize(12);
    check(fontSizePoints() == 12.0f, "font size");
    setFontSize(points);

    const generation = themeGeneration();
    check(loadTheme(`{"name":"D test","themes":[{"name":"D Test Dark","mode":"dark",
          "colors":{"button.background":"#302d41"}}]}`) == 1, "load theme");
    check(themeCount() > 0, "theme count");
    check(themeAt(themeCount() - 1) == "D Test Dark", "theme at");
    check(themeIgnored() >= 0, "theme ignored");
    check(setTheme("D Test Dark"), "custom theme");
    check(themeMode() == "dark", "theme mode");
    check(themeGeneration() != generation, "theme generation");

    float[4] custom;
    check(themeColor("button.background", custom), "custom theme color");
    check(setThemeColor("button.background", [0.2f, 0.4f, 0.6f, 1]), "set theme color");
    check(themeColor("button.background", custom) && custom[0] > 0.19f && custom[0] < 0.21f,
          "theme color update");
    check(setThemeNum("button.roundness", 0.25), "set theme number");
    double roundness;
    check(themeNum("button.roundness", roundness) && roundness == 0.25, "theme number");

    check(loadTheme("{") == -1 && themeError().length > 0, "theme load error");
    check(setTheme("dark"), "restore dark theme");

    setScale(1);
    check(scale() == 1.0f, "scale");
    check(unit() > 0, "widget unit");
    check(iconCount() > 0, "icon count");
    check(iconName(1).length > 0, "icon name");
}

private void testCss() {
    auto sheet = Stylesheet.parse(`
        div { color: #ff0000; font-size: 20px; }
        .card { background: #202020; padding: 8px; }
        #main { border: 2px solid #00ff00; }
        div.card:hover { color: #0000ff; }
    `);
    check(sheet.ruleCount() == 4, "css rule count");

    auto element = div("x").class_("card").id("main");
    Style base = Style.init;
    base.fontSize = 11.0f;
    auto style = sheet.compute(element, base, "color: #00ff00");
    check(style.color.g == 1 && style.color.r == 0, "inline style wins");
    check(style.fontSize == 20.0f, "type selector");
    check(style.hasBackground && style.background.r > 0, "class background");
    check(style.borderWidth == 2.0f, "id border");
    check(style.padding[0] == 8.0f && style.padding[2] == 8.0f, "padding shorthand");

    element.setState("hover", true);
    auto hovered = sheet.compute(element, base);
    check(hovered.color.b == 1 && hovered.color.r == 0, "hover pseudo-class");
}

private void testHtml() {
    auto page = body_(
        h1("Hello"),
        p("World ", strong("bold"), " text"),
        div(
            input().attr("type", "text").attr("value", "abc"),
            button("OK")
        ).class_("card"),
        table(
            caption("Data"),
            tbody(
                tr(td("A"), td("B")),
                tr(td("1").attr("colspan", "2"))
            )
        ),
        select_(option("One"), option("Two").attr("selected"))
    );

    auto document = new Document(page, `
        .card { background: #202020; padding: 8px; border: 2px solid #ff8800; }
        button:hover { color: #00ff00; }
    `);

    auto heading = document.find("h1");
    check(heading !is null && heading.node.typeName() == "label", "html h1 label");
    check(heading.node.str("text") == "Hello", "html h1 text");
    check(heading.style().fontSize == 22.0f, "html h1 user-agent size");

    auto card = document.find(".card");
    check(card !is null && card.node.typeName() == "frame", "html card frame");
    check(card.style().hasBackground, "html card background");
    check(card.node.childCount() == 1, "html card flow child");

    auto text = document.find("input[type=text]");
    check(text !is null && text.node.str("value") == "abc", "html text input");

    auto pressable = document.find("button");
    check(pressable.node.typeName() == "button", "html button widget");
    pressable.setState("hover", true);
    document.restyle();
    check(pressable.style().color.g == 1, "html hover restyle");

    auto caption_ = document.find("caption");
    check(caption_ !is null && caption_.node.str("text") == "Data", "html caption");
    check(document.find("table").node.childCount() == 2, "html caption above grid");

    auto dropdown = document.find("select");
    check(dropdown.node.typeName() == "dropdown", "html select dropdown");
    check(dropdown.node.strs("items").length == 2, "html select options");
    check(dropdown.node.num("value") == 1, "html selected option");
}

private bool throwsBui(void delegate() action) {
    try {
        action();
    }
    catch (BuiException) {
        return true;
    }
    return false;
}

private void testReviewFixes() {
    // `[attr*=...]` with a needle longer than the value does not underflow.
    check(div().attr("title", "ab").query("[title*=abcd]").length == 0,
          "substring selector, long needle");
    check(div().attr("title", "xabcx").query("[title*=abc]").length == 1,
          "substring selector match");

    // grid-template-columns counts its tracks.
    auto sheet = Stylesheet.parse(`
        .two { grid-template-columns: 1fr 1fr; }
        .three { grid-template-columns: repeat(3, 1fr); }
    `);
    Style base = Style.init;
    check(sheet.compute(div().class_("two"), base).gridColumns == 2, "grid two tracks");
    check(sheet.compute(div().class_("three"), base).gridColumns == 3, "grid repeat tracks");

    // Negative strides are rejected.
    const(ubyte)[] rgba = new ubyte[16];
    check(throwsBui({ Image.fromRgba(rgba, 2, 2, -1); }), "negative stride");

    // Tree items cannot form cycles or have two parents.
    auto top = new TreeItem("top");
    auto mid = new TreeItem("mid");
    auto leaf = new TreeItem("leaf");
    top.add(mid);
    mid.add(leaf);
    check(throwsBui({ top.add(top); }), "tree self add");
    check(throwsBui({ leaf.add(top); }), "tree cycle add");
    check(throwsBui({ top.add(mid); }), "tree double parent");

    // Realiser fixes.
    auto page = body_(
        input().attr("type", "number").attr("value", "-3"),
        input().attr("type", "radio").attr("name", "g").attr("checked", "").id("r1"),
        input().attr("type", "radio").attr("name", "g").id("r2"),
        button(span("Deep")),
        table(tr(td("shown")), tr(td("gone")).attr("hidden", "").id("hidden"))
    );
    auto document = new Document(page);
    check(document.find("input[type=number]").node.num("value") == -3,
          "number input is not clamped at 0");
    check(document.find("button").node.str("text") == "Deep", "button descendant text");
    check(document.find("#hidden td").node is null, "hidden row not realised");

    auto first = document.find("#r1").node;
    auto second = document.find("#r2").node;
    second.set("value", 1);
    second.dispatch(BK_EV_TOGGLE, null, 0, 0);
    check(first.num("value") == 0 && second.num("value") == 1, "radio group exclusive");
}

private __gshared int collectFlags;

/// A probe object whose destructor records that the GC collected it.
private final class CollectFlag {
    int ticks;
    ~this() {
        ++collectFlags;
    }
}

private NodeHandler captureFlag(CollectFlag flag) {
    return (NodeEvent e) {
        ++flag.ticks;
    };
}

private NodeHandler captureNode(Node node) {
    return (NodeEvent e) {
        node.set("text", "tick");
    };
}

private __gshared int probeTicks;

/// A handler whose only state is a module-level counter, so nothing on the
/// registering frame keeps the delegate alive: its store does.
private NodeHandler captureTick() {
    return (NodeEvent e) {
        ++probeTicks;
    };
}

/// Free R in R -> A, B while A's handler is the only reference to its flag.
/// The caller keeps B, so one store shared by both survivors would keep A's
/// handler (and the flag) alive; a store per survivor root lets both go.
/// A thread gives the probe a stack the main collector no longer scans, so
/// nothing but the handler can reach the flag once `free()` returns.
private Node siblingRetentionProbe() {
    Node survivor;
    auto thread = new Thread({
        auto root = new Node("frame");
        auto first = new Node("frame");
        auto sibling = new Node("frame");
        root.add(first);
        root.add(sibling);
        first.on(BK_EV_CLICK, captureFlag(new CollectFlag()));
        root.free();
        survivor = sibling;
    });
    thread.start();
    thread.join();
    return survivor;
}

/// Build R -> B where B's handler captures R and R's own handler captures a
/// flag. B stays attached and kit-rooted; storing B's handler on B would keep
/// R, its store and the flag alive, so R would never free and release B. A
/// thread gives the probe a stack the main collector no longer scans.
private void ancestorRetentionProbe() {
    auto thread = new Thread({
        auto root = new Node("frame");
        auto child = new Node("frame");
        root.add(child);
        root.on(BK_EV_CLICK, captureFlag(new CollectFlag()));
        child.on(BK_EV_CLICK, captureNode(root));
    });
    thread.start();
    thread.join();
}

/// Free M in R -> M -> S while R stays alive and holds its own handler. S
/// keeps its own handler; if the owner kept a copy of the survivor's entry,
/// the flag would live as long as R. A thread gives the probe a stack the main
/// collector no longer scans.
private Node intermediateFreeProbe() {
    Node root;
    auto thread = new Thread({
        auto outer = new Node("frame");
        auto middle = new Node("frame");
        auto survivor = new Node("frame");
        outer.add(middle);
        middle.add(survivor);
        outer.on(BK_EV_CLICK, captureFlag(new CollectFlag()));
        survivor.on(BK_EV_CLICK, captureFlag(new CollectFlag()));
        middle.free();
        root = outer;
    });
    thread.start();
    thread.join();
    return root;
}

private void testReviewRegressions() {
    // `onKey` binds the key event only; the release must not repeat it.
    int keys;
    auto keyPage = body_(div("key").id("key").onKey((string key) {
        ++keys;
    }));
    auto keyDocument = new Document(keyPage);
    auto keyed = keyDocument.find("#key").node;
    keyed.dispatch(BK_EV_KEY, cast(void*) "a".ptr, 0, 0);
    keyed.dispatch(BK_EV_KEYRELEASE, cast(void*) "a".ptr, 0, 0);
    check(keys == 1, "onKey fires once per key event");

    // A rowspan reserves its column in the following row.
    auto tablePage = body_(table(
        tr(td("a").attr("rowspan", "2").id("span-a"), td("b")),
        tr(td("c").id("span-c"))
    ));
    auto tableDocument = new Document(tablePage);
    check(tableDocument.find("#span-c").outerNode().geom("column") == 1,
          "rowspan reserves a column");

    // A column flex container blockifies its inline children.
    auto flexPage = body_(div(span("a"), span("b")).id("flex")
        .style("display: flex; flex-direction: column"));
    auto flexDocument = new Document(flexPage);
    check(flexDocument.find("#flex").outerNode().childCount() == 2,
          "column flex keeps its items separate");

    // A hidden row group hides its rows.
    auto hiddenPage = body_(table(tbody(tr(td("gone")))
        .attr("hidden", "").id("hidden-body")));
    auto hiddenDocument = new Document(hiddenPage);
    check(hiddenDocument.find("#hidden-body").node is null,
          "hidden tbody is not realised");
    check(hiddenDocument.find("td").node is null,
          "hidden tbody row is not realised");

    // Tree operations reject a node that is not a tree.
    auto label = new Node("label");
    check(throwsBui({ setRoots(label, []); }), "tree type guard");
    label.free();

    // A handler registered before the node is attached moves to the owning
    // wrapper, so handlers added later do not hide it.
    int before, after;
    string order;
    auto handlerRoot = new Node("frame");
    auto handlerChild = new Node("label");
    handlerChild.on((NodeEvent e) {
        if (e.kind == BK_EV_CLICK) {
            ++before;
            order ~= "before";
        }
    });
    handlerRoot.add(handlerChild);
    handlerChild.on((NodeEvent e) {
        if (e.kind == BK_EV_CLICK) {
            ++after;
            order ~= "after";
        }
    });
    handlerChild.dispatch(BK_EV_CLICK, null, 0, 0);
    check(before == 1 && after == 1 && order == "beforeafter",
          "handlers before and after attach both run in order");
    handlerRoot.free();
    handlerChild.dispatch(BK_EV_CLICK, null, 0, 0);
    check(before == 2 && after == 2,
          "an owned child keeps its handlers after the parent is freed");
    handlerChild.free();

    // A handler built outside this frame is reachable only through the node's
    // store, so the store is the only thing keeping the delegate alive and the
    // observation channel lives outside the delegate.
    probeTicks = 0;
    Node probed;
    auto probeThread = new Thread({
        auto n = new Node("frame");
        n.on(BK_EV_CLICK, captureTick());
        probed = n;
    });
    probeThread.start();
    probeThread.join();
    GC.collect();
    probed.dispatch(BK_EV_CLICK, null, 0, 0);
    check(probeTicks == 1, "a store keeps a handler reachable across a collection");
    probed.free();

    // A surviving owned root keeps only its own handler store; sharing it
    // with a sibling would keep an unrelated sibling's handlers, and anything
    // they capture, alive for as long as the survivor lives.
    collectFlags = 0;
    auto surviving = siblingRetentionProbe();
    GC.collect();
    check(collectFlags > 0,
          "a surviving sibling does not pin another survivor's handlers");
    surviving.free();

    // An attached child is a GC root while its native tree lives, but its
    // handlers live on the owning ancestor, so they cannot root that ancestor
    // back and stop it from freeing the tree.
    collectFlags = 0;
    ancestorRetentionProbe();
    GC.collect();
    check(collectFlags > 0,
          "an attached child's handler does not pin its owning ancestor");

    // Freeing an intermediate node hands its survivor's entries to that
    // survivor only; the still-live owner must not keep a copy either.
    collectFlags = 0;
    auto keptRoot = intermediateFreeProbe();
    GC.collect();
    check(collectFlags > 0,
          "freeing an intermediate node does not pin its survivor's handlers");
    keptRoot.free();

    // Moving a handled subtree to another parent keeps its handlers.
    int moved;
    auto firstRoot = new Node("frame");
    auto secondRoot = new Node("frame");
    auto movedNode = new Node("label");
    movedNode.on((NodeEvent e) {
        if (e.kind == BK_EV_CLICK) {
            ++moved;
        }
    });
    firstRoot.add(movedNode);
    movedNode.on((NodeEvent e) {
        if (e.kind == BK_EV_CLICK) {
            ++moved;
        }
    });
    secondRoot.add(movedNode);
    movedNode.dispatch(BK_EV_CLICK, null, 0, 0);
    check(moved == 2 && movedNode.parent() is secondRoot,
          "handlers move with a reparented node");
    firstRoot.free();
    secondRoot.free();
    movedNode.free();

    // Moving an intermediate ancestor keeps handlers that live further up.
    int guarded;
    auto guardRoot = new Node("frame");
    auto guardMiddle = new Node("frame");
    auto guardLeaf = new Node("label");
    guardRoot.add(guardMiddle);
    guardLeaf.on((NodeEvent e) {
        if (e.kind == BK_EV_CLICK) {
            ++guarded;
        }
    });
    guardMiddle.add(guardLeaf);
    auto guardSecond = new Node("frame");
    guardSecond.add(guardMiddle);
    guardLeaf.dispatch(BK_EV_CLICK, null, 0, 0);
    check(guarded == 1, "handlers survive moving an intermediate ancestor");
    guardSecond.free();
    guardMiddle.free();
    guardLeaf.free();
    guardRoot.free();

    // A C-owned child gets one cached borrowed wrapper, and handlers added
    // through it see the kit's events.
    auto panel = new Node("panel");
    auto body = panel.child(0);
    check(body !is null, "panel body child");
    check(panel.child(0) is body, "borrowed wrapper is cached");
    body.set("events", true);
    int enters;
    body.on(BK_EV_ENTER, (NodeEvent) {
        ++enters;
    });
    auto content = new Node("label");
    content.set("text", "body");
    body.add(content);
    auto panelContext = new Context(panel);
    auto panelRaster = new Raster(200, 160);
    panelContext.render(panelRaster, true);
    const bodyRect = body.rect();
    check(bodyRect[2] > 0 && bodyRect[3] > 0, "panel body laid out");
    panelContext.motion(bodyRect[0] + bodyRect[2] / 2,
                        bodyRect[1] + bodyRect[3] / 2, 0);
    check(enters == 1, "borrowed wrapper handler runs");
    int refreshed;
    body.free();
    body.on(BK_EV_ENTER, (NodeEvent) {
        ++refreshed;
    });
    body.dispatch(BK_EV_ENTER, null, 0, 0);
    check(enters == 1 && refreshed == 1,
          "a freed borrowed wrapper does not revive its old handlers");
    // The handler lives on the owning wrapper's store, so collecting the
    // borrowed wrapper does not drop it.
    panelContext.motion(0, 0, 0);
    body = null;
    GC.collect();
    panelContext.motion(bodyRect[0] + bodyRect[2] / 2,
                        bodyRect[1] + bodyRect[3] / 2, 0);
    check(enters == 1 && refreshed == 2, "handlers outlive the borrowed wrapper");
    panelRaster.free();
    panelContext.free();
    panel.free();
    content.free();

    // The texture accessor returns the texture ID, not a success flag.
    static assert(is(typeof(Video.init.texture(0)) == int),
                  "Video.texture returns a texture id");

    // A document that takes over an existing element tree rebinds the state
    // callback instead of keeping the old document's hook.
    auto rebindPage = body_(div("x").id("box"));
    auto firstDocument = new Document(rebindPage, "#box { width: 50vw; }");
    auto secondDocument = new Document(rebindPage, "#box { width: 50vw; }");
    secondDocument.setViewport(200, 100);
    check(secondDocument.find("#box").outerNode().geom("reqwidth") == 100,
          "second document viewport applied");
    secondDocument.find("#box").node.dispatch(BK_EV_ENTER, null, 0, 0);
    check(secondDocument.find("#box").outerNode().geom("reqwidth") == 100,
          "state callback follows the current document");
    firstDocument.dispose();
    check(secondDocument.find("#box").node !is null,
          "disposing an older document leaves the newer mount");
    secondDocument.restyle();
    secondDocument.dispose();

    // A remount that fails (display: none) hands the element hooks back to
    // the document that still owns the realised tree.
    auto hookPage = body_(div("x").id("hook"));
    auto hookOwner = new Document(hookPage);
    auto hookElement = hookOwner.find("#hook");
    check(hookElement.node !is null, "ownership test element is realised");
    bool rejected;
    try {
        (new Document(hookPage, "body { display: none; }")).dispose();
    }
    catch (BuiException) {
        rejected = true;
    }
    check(rejected, "a display:none root is rejected");
    hookOwner.dispose();
    check(hookElement.node is null,
          "a failed remount restores the previous document's hooks");

    // Freeing a root closes the popups it owns before the C node disappears.
    auto popupRoot = new Node("frame");
    auto popupContext = new Context(popupRoot);
    popupContext.openMenu(popupRoot, [new MenuItem("One", {})]);
    check(popupContext.popupCount() == 1, "root popup opened");
    // The handler's body does not matter here: it parks an entry in the root's
    // store keyed by the popup's native node, which the kit destroys on close,
    // so neither the close nor the root's free may read that key as a node.
    popupContext.popup(0).on(BK_EV_CLICK, captureTick());
    popupContext.closePopups();
    check(popupContext.popupCount() == 0, "closePopups drops the popup");
    popupRoot.free();
    check(popupContext.popupCount() == 0, "freeing the root closes its popups");
    popupContext.free();

    // A disposed document releases its kit root and the element tree can be
    // mounted again.
    auto disposablePage = body_(div("x").id("box"));
    auto disposable = new Document(disposablePage);
    auto disposableRoot = disposable.node();
    check(disposableRoot !is null, "document root");
    disposable.dispose();
    check(disposable.node() is null, "document dispose drops the kit root");
    check(disposableRoot.handle is null, "disposed root node is freed");
    auto remounted = new Document(disposablePage, "#box { color: #00ff00; }");
    check(remounted.find("#box").node !is null, "element tree remounts after dispose");
    disposable.dispose();
    check(remounted.find("#box").node !is null,
          "disposing the old document again leaves the remounted nodes");
    remounted.restyle();
    remounted.dispose();

    // Disposing clears node wrappers for hidden elements too. The next
    // document skips them, so they must not keep pointing at freed nodes.
    auto hidingPage = body_(div("x").id("hidden-box"));
    auto hidingDocument = new Document(hidingPage);
    auto hiddenElement = hidingDocument.find("#hidden-box");
    check(hiddenElement !is null, "element for the hide test");
    hiddenElement.attr("hidden", "");
    hidingDocument.dispose();
    auto hidingAgain = new Document(hidingPage);
    check(hidingAgain.find("#hidden-box").node is null, "hidden element stays hidden");
    hidingAgain.restyle();
    hidingAgain.dispose();

    // `clear` detaches children so their wrappers can be released.
    auto detachRoot = new TreeItem("detach");
    auto detachChild = new TreeItem("child");
    detachRoot.add(detachChild);
    check(detachChild.parent() is detachRoot, "tree parent before clear");
    detachRoot.clear();
    check(detachChild.parent() is null, "tree clear detaches children");
    detachChild.free();
    check(detachChild.handle is null, "detached child frees");
    detachRoot.free();
    check(detachRoot.handle is null, "detached root frees");

    // Making a child a root detaches it from its old parent in the kit.
    auto oldParent = new TreeItem("parent");
    auto promoted = new TreeItem("child");
    oldParent.add(promoted);
    auto tree = new Node("tree");
    setRoots(tree, [promoted]);
    check(promoted.parent() is null, "a tree root has no parent");
    check(oldParent.childCount() == 0, "setRoots detaches from the old parent");
    setRoots(tree, []);
    tree.free();
    promoted.free();
    oldParent.free();

    // Promoting a selected item detaches it from the source parent; the
    // selection keeps the wrapper alive until it is cleared or the tree goes.
    auto sourceTree = new Node("tree");
    auto destTree = new Node("tree");
    auto sourceParent = new TreeItem("source");
    auto selectedChild = new TreeItem("selected");
    sourceParent.add(selectedChild);
    setRoots(sourceTree, [sourceParent]);
    select(sourceTree, selectedChild);
    setRoots(destTree, [selectedChild]);
    check(selectedChild.parent() is null, "promoted selection is detached");
    setRoots(destTree, []);
    selectedChild.free();
    check(selectedChild.handle !is null, "selection retains the promoted item");
    check(selected(sourceTree) is selectedChild, "source tree keeps its selection");
    select(sourceTree, null);
    selectedChild.free();
    check(selectedChild.handle is null, "clearing the selection releases the item");
    setRoots(sourceTree, []);
    sourceTree.free();
    destTree.free();
    sourceParent.free();

    // Promoting many siblings detaches them in one compaction pass.
    auto batchParent = new TreeItem("batch");
    TreeItem[] batchKids;
    foreach (i; 0 .. 64) {
        auto kid = new TreeItem("kid");
        batchParent.add(kid);
        batchKids ~= kid;
    }
    auto batchTree = new Node("tree");
    setRoots(batchTree, batchKids);
    check(batchParent.childCount() == 0, "setRoots detaches a batch of siblings");
    check(rowCount(batchTree) == 64, "batch roots are rows");
    setRoots(batchTree, []);
    batchTree.free();
    batchParent.free();
    foreach (kid; batchKids) {
        kid.free();
    }

    // Removing a tree's roots clears ownership for the whole subtree, so a
    // later promotion cannot follow a pointer to the freed tree.
    auto staleTree = new Node("tree");
    auto staleParent = new TreeItem("parent");
    auto staleChild = new TreeItem("child");
    auto staleLeaf = new TreeItem("leaf");
    staleParent.add(staleChild);
    staleChild.add(staleLeaf);
    setRoots(staleTree, [staleParent]);
    setRoots(staleTree, []);
    staleParent.free();
    staleTree.free();
    auto freshTree = new Node("tree");
    setRoots(freshTree, [staleLeaf]);
    check(rowCount(freshTree) == 1, "promotion after a freed tree still works");
    setRoots(freshTree, []);
    freshTree.free();
    staleLeaf.free();
    staleChild.free();

    // Promoting a row out of an idle tree redraws the tree that lost it.
    auto sourceShell = new Node("frame");
    auto invalidated = new Node("tree");
    invalidated.pack("fill", 1);
    auto invalidatedParent = new TreeItem("parent");
    auto invalidatedChild = new TreeItem("child");
    invalidatedParent.add(invalidatedChild);
    setRoots(invalidated, [invalidatedParent]);
    sourceShell.add(invalidated);
    auto sourceContext = new Context(sourceShell);
    auto sourceRaster = new Raster(240, 140);
    sourceContext.render(sourceRaster, true);
    check(!sourceContext.needsRender(), "source tree is idle after render");
    auto destinationShell = new Node("frame");
    auto destination = new Node("tree");
    destinationShell.add(destination);
    auto destinationContext = new Context(destinationShell);
    auto destinationRaster = new Raster(240, 140);
    destinationContext.render(destinationRaster, true);
    setRoots(destination, [invalidatedChild]);
    check(sourceContext.needsRender(), "promotion invalidates the source tree");
    setRoots(destination, []);
    setRoots(invalidated, []);
    sourceRaster.free();
    destinationRaster.free();
    sourceContext.free();
    destinationContext.free();
    sourceShell.free();
    destinationShell.free();
    invalidatedParent.free();
    invalidatedChild.free();

    // A rename keeps the item that started the edit, even when a promotion
    // shifts the source rows while the edit is open.
    auto renameShell = new Node("frame");
    auto renameTree = new Node("tree");
    renameTree.pack("fill", 1);
    auto renameBefore = new TreeItem("before");
    auto renameTarget = new TreeItem("target");
    auto renameAfter = new TreeItem("after");
    setRoots(renameTree, [renameBefore, renameTarget, renameAfter]);
    renameShell.add(renameTree);
    auto renameContext = new Context(renameShell);
    auto renameRaster = new Raster(240, 140);
    renameContext.render(renameRaster, true);
    const targetRect = rowRect(renameTree, 1);
    const targetX = targetRect[0] + targetRect[2] / 2;
    const targetY = targetRect[1] + targetRect[3] / 2;
    renameContext.press(targetX, targetY, 0);
    renameContext.press(targetX, targetY, 0);
    renameContext.release(targetX, targetY, 0);
    auto renameDestShell = new Node("frame");
    auto renameDest = new Node("tree");
    renameDestShell.add(renameDest);
    auto renameDestContext = new Context(renameDestShell);
    auto renameDestRaster = new Raster(240, 140);
    renameDestContext.render(renameDestRaster, true);
    auto renameHost = new TreeItem("host");
    setRoots(renameDest, [renameHost]);
    setRoots(renameDest, [renameHost, renameBefore]);
    renameContext.key("renamed");
    renameContext.key("", "Return");
    check(renameTarget.label == "renamed", "rename follows the promoted item");
    check(renameAfter.label == "after" && renameBefore.label == "before",
          "rename does not retarget a shifted row");
    setRoots(renameDest, []);
    setRoots(renameTree, []);
    renameRaster.free();
    renameDestRaster.free();
    renameContext.free();
    renameDestContext.free();
    renameShell.free();
    renameDestShell.free();
    renameBefore.free();
    renameTarget.free();
    renameAfter.free();
    renameHost.free();

    // A rename keeps its retained target when promoting a preceding nested
    // row shrinks the source below the index the edit started at.
    auto shiftShell = new Node("frame");
    auto shiftTree = new Node("tree");
    shiftTree.pack("fill", 1);
    int renameEvents;
    shiftTree.on(BK_EV_RENAME, (NodeEvent) {
        ++renameEvents;
    });
    auto shiftParent = new TreeItem("parent");
    auto shiftMiddle = new TreeItem("middle");
    auto shiftTarget = new TreeItem("target");
    shiftParent.add(shiftMiddle);
    shiftParent.add(shiftTarget);
    setRoots(shiftTree, [shiftParent]);
    shiftShell.add(shiftTree);
    auto shiftContext = new Context(shiftShell);
    auto shiftRaster = new Raster(240, 140);
    shiftContext.render(shiftRaster, true);
    check(rowCount(shiftTree) == 3, "nested tree rows before rename");
    const shiftRect = rowRect(shiftTree, 2);
    const shiftX = shiftRect[0] + shiftRect[2] / 2;
    const shiftY = shiftRect[1] + shiftRect[3] / 2;
    shiftContext.press(shiftX, shiftY, 0);
    shiftContext.press(shiftX, shiftY, 0);
    shiftContext.release(shiftX, shiftY, 0);
    auto shiftDestShell = new Node("frame");
    auto shiftDest = new Node("tree");
    shiftDestShell.add(shiftDest);
    auto shiftDestContext = new Context(shiftDestShell);
    auto shiftDestRaster = new Raster(240, 140);
    shiftDestContext.render(shiftDestRaster, true);
    setRoots(shiftDest, [shiftMiddle]);
    check(rowCount(shiftTree) == 2, "promotion shrinks the rename source");
    shiftContext.key("shifted");
    shiftContext.key("", "Return");
    check(shiftTarget.label == "shifted",
          "rename keeps the retained target past its old row index");
    check(shiftMiddle.label == "middle", "rename does not retarget the new row");
    check(renameEvents == 1, "rename reports its source row");

    // A target promoted out of the source during the edit still gets the new
    // label, but the source has no row to report, so it emits no event.
    const awayRect = rowRect(shiftTree, 1);
    const awayX = awayRect[0] + awayRect[2] / 2;
    const awayY = awayRect[1] + awayRect[3] / 2;
    shiftContext.press(awayX, awayY, 0);
    shiftContext.press(awayX, awayY, 0);
    shiftContext.release(awayX, awayY, 0);
    setRoots(shiftDest, [shiftTarget]);
    check(rowCount(shiftTree) == 1, "promotion removes the rename target");
    shiftContext.key("away");
    shiftContext.key("", "Return");
    check(shiftTarget.label == "away",
          "rename still updates a target promoted out of the tree");
    check(renameEvents == 1, "a promoted rename target reports no source row");
    check(rowCount(shiftTree) == 1, "the source keeps its remaining row");

    setRoots(shiftDest, []);
    setRoots(shiftTree, []);
    shiftRaster.free();
    shiftDestRaster.free();
    shiftContext.free();
    shiftDestContext.free();
    shiftShell.free();
    shiftDestShell.free();
    shiftMiddle.free();
    shiftTarget.free();
    shiftParent.free();

    // Every wrapper surface rejects an empty node the same way.
    Node empty;
    check(throwsBui({ insertText(empty, 0, "x"); }), "textarea handle guard");
    check(throwsBui({ addItem(empty, CanvasItem.rect(1, 0, 0, 1, 1)); }),
          "canvas handle guard");
    check(throwsBui({ requestFrame(empty); }), "custom frame guard");
    check(throwsBui({ setCursor(empty, 0); }), "custom cursor guard");
    check(throwsBui({ hasFocus(empty); }), "custom focus guard");

    // Rectangles and ovals share their construction path.
    auto rectItem = CanvasItem.rect(7, 1, 2, 3, 4,
                                    [0.1f, 0.2f, 0.3f, 0.4f],
                                    [0.5f, 0.6f, 0.7f, 0.8f]);
    check(rectItem.id == 7 && rectItem.type == BK_ITEM_RECT, "canvas rect type");
    check(rectItem.coords.length == 4 && rectItem.coords[0] == 1 &&
          rectItem.coords[3] == 4, "canvas rect coords");
    check(rectItem.fill[1] == 0.2f && rectItem.outline[3] == 0.8f,
          "canvas rect colors");
    auto ovalItem = CanvasItem.oval(8, 5, 6, 7, 8);
    check(ovalItem.id == 8 && ovalItem.type == BK_ITEM_OVAL, "canvas oval type");
    check(ovalItem.coords.length == 4 && ovalItem.coords[0] == 5 &&
          ovalItem.coords[3] == 8, "canvas oval coords");
    check(ovalItem.fill[3] == 0 && ovalItem.outline[3] == 0, "canvas oval defaults");

    // The color parser shares the CSS token helpers.
    Color parsed;
    check(tryParseColor("  ReD ", parsed) && parsed.r == 1 && parsed.g == 0 &&
          parsed.a == 1, "color name trim and case");
    check(tryParseColor("#0f08", parsed) && parsed.r == 0 && parsed.g == 1 &&
          parsed.b == 0 && parsed.a > 0.53 && parsed.a < 0.54,
          "color short hex with alpha");
    check(tryParseColor("rgb(100% 0% 50% / 50%)", parsed) &&
          parsed.r == 1 && parsed.b > 0.49 && parsed.b < 0.51 &&
          parsed.a > 0.49 && parsed.a < 0.51, "color rgb percentages");
    check(tryParseColor(" hsl(120, 100%, 25%) ", parsed) && parsed.g > 0.49,
          "color hsl whitespace");
    check(!tryParseColor("currentcolor", parsed), "color keyword rejected");
}

private void testRsx() {
    int count = 3;
    string label = "dynamic";

    auto page = mixin(rsx!`
        <body class="app">
            <style>
                .app { background: #202020; padding: 6px; }
                h1 { color: #ff0000; font-size: 22px; }
            </style>
            <h1>Hello &amp; welcome</h1>
            <p id="line">Count: {count} and {label}</p>
            <div id="main" class="panel card">
                <span>Span</span>
                <strong>Bold</strong>
            </div>
            <button onclick={() { ++count; }}>Go</button>
            <input type="text" value="abc">
            <input type="checkbox" checked>
            <img src="pic.png" width="32" height="32" alt="pic">
            <my-widget data-x="1">Custom</my-widget>
        </body>
    `);

    auto document = new Document(page);

    auto heading = document.find("h1");
    check(heading.node.str("text") == "Hello & welcome", "rsx entities");
    check(heading.style().fontSize == 22.0f, "rsx style element");

    auto line = document.find("#line");
    check(line.node.str("text") == "Count: 3 and dynamic", "rsx interpolation");
    line.setText("Count: 4");
    check(line.node.str("text") == "Count: 4", "rsx setText");

    auto main_ = document.find("#main");
    check(main_ !is null && main_.attrValue("class") == "panel card", "rsx class");
    check(main_.children().length == 2, "rsx children");

    auto button = document.find("button");
    check(button.node.typeName() == "button" && button.node.str("text") == "Go",
          "rsx button");

    auto text = document.find("input[type=text]");
    check(text.node.typeName() == "text" && text.node.str("value") == "abc",
          "rsx text input");

    auto checkbox = document.find("input[type=checkbox]");
    check(checkbox.node.typeName() == "checkbox" && checkbox.node.num("value") == 1,
          "rsx checkbox");

    auto image = document.find("img");
    check(image.node.typeName() == "image", "rsx image");
    check(document.find("my-widget").attrValue("data-x") == "1", "rsx custom tag");

    string cls = "one two";
    bool on = true;
    bool off = false;
    auto dynamic = mixin(rsx!`
        <div class={cls}>
            <span hidden={on}>x</span>
            <span disabled={off}>y</span>
        </div>
    `);
    check(dynamic.attrValue("class") == "one two", "rsx dynamic class");
    check(dynamic.children()[0].hasAttr("hidden"), "rsx dynamic flag on");
    check(!dynamic.children()[1].hasAttr("disabled"), "rsx dynamic flag off");

    auto raw = mixin(rsx!`
        <div id="raw">{r"C:\tmp\"}</div>
    `);
    auto rawDocument = new Document(raw);
    check(rawDocument.find("#raw").node.str("text") == r"C:\tmp\",
          "rsx raw string expression");

    // Delimited q strings can contain double quotes; the scanner must stop at
    // the matching close delimiter, not the first quote.
    auto delimited = mixin(rsx!`
        <div id="delimited">{q"[a \" }x]"}</div>
    `);
    auto delimitedDocument = new Document(delimited);
    check(delimitedDocument.find("#delimited").node.str("text") == q"[a \" }x]",
          "rsx delimited q string expression");

    // Nested delimiters are content until the matching outer close.
    auto nested = mixin(rsx!`
        <div id="nested">{q"(a (b) } c)"}</div>
    `);
    auto nestedDocument = new Document(nested);
    check(nestedDocument.find("#nested").node.str("text") == q"(a (b) } c)",
          "rsx nested delimited q string expression");
}

private void testCapi() {
    int major, minor, patch;
    bui_get_version(&major, &minor, &patch);
    check(major == BUI_KIT_VERSION_MAJOR && minor == BUI_KIT_VERSION_MINOR &&
          patch == BUI_KIT_VERSION_PATCH, "stable C API version");
    check(cslice(bui_version_string()) == "BUI " ~ BUI_KIT_VERSION_STRING,
          "stable C API version string");
}

/* Screens: a split, an edge dragged, a region sized and refused values, the layout saved. */
void testScreen() {
    auto screen = new Node("screen");
    auto first = new Node("area");
    first.set("editor", "3D Viewport");
    screen.add(first);
    int made;
    screen.onArea((NodeEvent e) {
        if (cast(int) e.b == BK_AREA_NEW) {
            made++;
        }
    });
    auto context = new Context(screen);
    auto raster = new Raster(800, 600);
    context.render(raster, true);
    auto right = first.split('v', 0.75f);
    check(right !is null && made == 1, "split makes an area");
    context.render(raster, false);
    auto r0 = first.rect, r1 = right.rect;
    check(r1[0] > r0[0] + r0[2], "the new area is on the right");
    const edge = r0[0] + r0[2] + 1;
    context.motion(edge, 300, 0);
    context.press(edge, 300, 0);
    context.motion(edge - 50, 300, 0);
    context.motion(edge - 100, 300, 0);
    context.release(edge - 100, 300, 0);
    context.render(raster, false);
    check(first.rect[2] < r0[2] - 90, "the edge moved");
    check(cslice(BK_get_str(screen.handle, "layout")).length > 0, "the layout as text");

    // A D-owned node in an area the screen made keeps its handlers when that area closes.
    auto pane = new Node("column");
    right.add(pane);
    int clicks;
    pane.onClick((NodeEvent) { ++clicks; });
    check(right.close(), "close an area");
    pane.dispatch(BK_EV_CLICK, null, 0, 0);
    check(clicks == 1, "a D-owned node keeps its handlers when its area closes");

    auto area = new Node("area");
    auto side = new Node("sidebar");
    check(BK_set_num(side.handle, "size", 200) == 1, "a sidebar's size");
    check(BK_set_str(side.handle, "side", "top") == 0, "a sidebar is never on top");
    area.add(side);
    area.add(new Node("column"));
    check(BK_node_add(area.handle, (new Node("column")).handle, -1) == 0, "one main region");
    auto areaContext = new Context(area);
    areaContext.render(new Raster(600, 400), true);
    check(side.rect[2] == 200, "the sidebar is 200 px");
    writeln("screen: ok");
}
