#include <stdio.h>

#include "BUI_kit.h"

static void on_event(void *data, BKNode *node, int kind, void *ref, double a, double b)
{
  (void)data;
  (void)ref;
  (void)a;
  (void)b;
  if (kind == BK_EV_CLICK) {
    printf("clicked %s\n", BK_get_str(node, "text"));
  }
  else if (kind == BK_EV_CHANGE) {
    printf("%s = %g\n", BK_get_str(node, "label"), BK_get_num(node, "value"));
  }
}

int main(void)
{
  BKNode *root = BK_node_new("frame");
  BK_set_num(root, "padding", 8);

  BKNode *size = BK_node_new("slider");
  BK_set_str(size, "label", "Size");
  BK_set_num(size, "min", 0);
  BK_set_num(size, "max", 10);
  BK_set_num(size, "value", 2.5);
  BK_node_add(root, size, -1);
  BK_geom_set_manager(size, BK_GEOM_PACK);
  BK_geom_set(size, "fill", 1);

  BKNode *apply = BK_node_new("button");
  BK_set_str(apply, "text", "Apply");
  BK_node_add(root, apply, -1);
  BK_geom_set_manager(apply, BK_GEOM_PACK);
  BK_geom_set(apply, "fill", 1);
  BK_geom_set(apply, "pady0", 6);

  BKContext *ctx = BK_context_new(root);
  BKHost host = {0};
  host.emit = on_event;
  BK_context_set_host(ctx, &host);

  if (!BK_window_supported()) {
    fprintf(stderr, "No window system here.\n");
    return 1;
  }
  BKWindow *win = BK_window_new(ctx, "{{title}}", 320, 120);
  BK_window_show(win);
  BK_window_run();
  BK_window_free(win);
  return 0;
}
