#ifndef DIAMONDS_SNAP_H
#define DIAMONDS_SNAP_H

#include <stdint.h>
#include <stdbool.h>
#include <wlr/types/wlr_scene.h>
#include "cairo_buffer.h"

struct diamonds_server;
struct diamonds_toplevel;

typedef enum {
	SNAP_NONE = 0,
	SNAP_MAXIMIZE,
	SNAP_LEFT_HALF,
	SNAP_RIGHT_HALF,
	SNAP_TOP_LEFT,
	SNAP_TOP_RIGHT,
	SNAP_BOTTOM_LEFT,
	SNAP_BOTTOM_RIGHT,
} diamonds_snap_zone_t;

struct diamonds_snap_preview {
	struct diamonds_server *server;
	struct wlr_scene_buffer *scene_buffer;
	struct diamonds_cairo_buffer *buffer;
	diamonds_snap_zone_t current_zone;
	int32_t target_x;
	int32_t target_y;
	int32_t target_w;
	int32_t target_h;
	bool visible;
};

struct diamonds_snap_preview *diamonds_snap_preview_create(struct diamonds_server *server);
void diamonds_snap_preview_destroy(struct diamonds_snap_preview *preview);
void diamonds_snap_preview_update(
	struct diamonds_snap_preview *preview,
	diamonds_snap_zone_t zone,
	int32_t output_w,
	int32_t output_h
);
void diamonds_snap_preview_hide(struct diamonds_snap_preview *preview);
diamonds_snap_zone_t diamonds_detect_snap_zone(double cx, double cy, int32_t output_w, int32_t output_h);
void diamonds_apply_snap(
	struct diamonds_toplevel *toplevel,
	diamonds_snap_zone_t zone,
	int32_t output_w,
	int32_t output_h
);

#endif /* DIAMONDS_SNAP_H */
