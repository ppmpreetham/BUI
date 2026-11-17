/* The demo app: a scrolling page of the kit's widgets, to try touch, text and the keyboard.
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <stdio.h>

#include "BUI_kit.h"
#include "bui_app.h"

static BKNode *add(BKNode *parent, const char *type, const char *key, const char *text)
{
  BKNode *n = BK_node_new(type);
  if (key && text) {
    BK_set_str(n, key, text);
  }
  BK_node_add(parent, n, -1);
  return n;
}

static BKNode *bui_demo_build(void)
{
  BKNode *root = BK_node_new("scroll");
  BKNode *page = add(root, "column", NULL, NULL);
  BK_set_num(page, "padding", 12);
  BK_set_num(page, "spacing", 6);

  add(page, "label", "text", "BUI on Android");
  add(page, "button", "text", "A button");
  BKNode *check = add(page, "checkbox", "label", "A checkbox");
  BK_set_num(check, "value", 1);
  BKNode *slider = add(page, "slider", "label", "Slider");
  BK_set_num(slider, "value", 0.5);
  BKNode *num = add(page, "number", "label", "Number");
  BK_set_num(num, "value", 3);
  BKNode *field = add(page, "text", "label", "Text field");
  BK_set_str(field, "value", "tap to type");
  BKNode *area = add(page, "textarea", "value", "A text area:\ntype here with the soft keyboard");
  BK_set_num(area, "rows", 5);
  BKNode *bar = add(page, "progress", "text", "Progress");
  BK_set_num(bar, "factor", 0.4);

  BKNode *panel = add(page, "panel", "text", "A panel");
  BKNode *body = BK_node_child(panel, 0); /* the panel makes its own body */
  add(body, "label", "text", "Panels open and close with a tap");
  add(body, "checkbox", "label", "Another checkbox");
  for (int i = 0; i < 40; i++) {
    char label[24];
    snprintf(label, sizeof(label), "Row %d", i + 1);
    add(page, "button", "text", label);
  }
  return root;
}

void bui_app_main(void)
{
  BKContext *ctx = BK_context_new(bui_demo_build());
  BKWindow *win = BK_window_new(ctx, "BUI", 360, 720);
  BK_window_show(win);
  BK_window_run();
  BK_window_free(win);
  BK_context_free(ctx);
}
