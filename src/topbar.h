#ifndef DIAMONDS_TOPBAR_H
#define DIAMONDS_TOPBAR_H

#include <stdint.h>
#include <stdbool.h>
#include <wlr/types/wlr_scene.h>
#include "cairo_buffer.h"

struct diamonds_server;

struct diamonds_topbar {
	struct diamonds_server *server;
	struct wlr_scene_buffer *scene_buffer;
	struct diamonds_cairo_buffer *buffer;
	struct wl_event_source *timer_source;

	int32_t width;
	int32_t height;
	int active_workspace; // 1..4
	bool control_center_active;

	char last_date_str[64];
};

struct diamonds_topbar *diamonds_topbar_create(struct diamonds_server *server);
void diamonds_topbar_destroy(struct diamonds_topbar *topbar);
void diamonds_topbar_reposition(struct diamonds_topbar *topbar, int32_t output_width, int32_t output_height);
void diamonds_topbar_render(struct diamonds_topbar *topbar);
bool diamonds_topbar_handle_click(struct diamonds_topbar *topbar, double lx, double ly);

#endif /* DIAMONDS_TOPBAR_H */
