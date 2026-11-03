#ifndef DIAMONDS_CALENDAR_H
#define DIAMONDS_CALENDAR_H

#include <stdint.h>
#include <stdbool.h>
#include <wlr/types/wlr_scene.h>
#include "cairo_buffer.h"

struct diamonds_server;

struct diamonds_calendar {
	struct diamonds_server *server;
	struct wlr_scene_buffer *scene_buffer;
	struct diamonds_cairo_buffer *buffer;
	bool visible;
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
};

struct diamonds_calendar *diamonds_calendar_create(struct diamonds_server *server);
void diamonds_calendar_destroy(struct diamonds_calendar *cal);
void diamonds_calendar_toggle(struct diamonds_calendar *cal);
void diamonds_calendar_reposition(struct diamonds_calendar *cal, int32_t output_width, int32_t output_height);
void diamonds_calendar_render(struct diamonds_calendar *cal);
bool diamonds_calendar_handle_click(struct diamonds_calendar *cal, double lx, double ly);

#endif /* DIAMONDS_CALENDAR_H */
