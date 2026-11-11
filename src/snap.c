#include "snap.h"
#include "diamonds.h"
#include "titlebar.h"
#include "sound.h"

#include <stdlib.h>
#include <pixman.h>

#define TOPBAR_HEIGHT 30
#define DOCK_RESERVE  44

diamonds_snap_zone_t diamonds_detect_snap_zone(double cx, double cy, int32_t output_w, int32_t output_h) {
	if (output_w <= 0 || output_h <= 0) {
		return SNAP_NONE;
	}

	const double corner_thresh = 42.0;
	const double edge_thresh = 20.0;
	const double top_thresh = 34.0;

	/* 1. Corners (Quadrants 25/25/25/25) */
	if (cx < corner_thresh && cy < corner_thresh) {
		return SNAP_TOP_LEFT;
	}
	if (cx > (double)output_w - corner_thresh && cy < corner_thresh) {
		return SNAP_TOP_RIGHT;
	}
	if (cx < corner_thresh && cy > (double)output_h - corner_thresh) {
		return SNAP_BOTTOM_LEFT;
	}
	if (cx > (double)output_w - corner_thresh && cy > (double)output_h - corner_thresh) {
		return SNAP_BOTTOM_RIGHT;
	}

	/* 2. Edges (50/50 and Maximize) */
	if (cy < top_thresh) {
		return SNAP_MAXIMIZE;
	}
	if (cx < edge_thresh) {
		return SNAP_LEFT_HALF;
	}
	if (cx > (double)output_w - edge_thresh) {
		return SNAP_RIGHT_HALF;
	}

	return SNAP_NONE;
}

static void calculate_zone_geometry(
		diamonds_snap_zone_t zone, int32_t output_w, int32_t output_h,
		int32_t *out_x, int32_t *out_y, int32_t *out_w, int32_t *out_h) {
	int32_t usable_y = TOPBAR_HEIGHT;
	int32_t usable_h = output_h - TOPBAR_HEIGHT - DOCK_RESERVE;
	if (usable_h < 100) usable_h = 100;

	int32_t half_w = output_w / 2;
	int32_t half_h = usable_h / 2;

	switch (zone) {
	case SNAP_MAXIMIZE:
		*out_x = 0;
		*out_y = usable_y;
		*out_w = output_w;
		*out_h = usable_h;
		break;
	case SNAP_LEFT_HALF:
		*out_x = 0;
		*out_y = usable_y;
		*out_w = half_w;
		*out_h = usable_h;
		break;
	case SNAP_RIGHT_HALF:
		*out_x = half_w;
		*out_y = usable_y;
		*out_w = output_w - half_w;
		*out_h = usable_h;
		break;
	case SNAP_TOP_LEFT:
		*out_x = 0;
		*out_y = usable_y;
		*out_w = half_w;
		*out_h = half_h;
		break;
	case SNAP_TOP_RIGHT:
		*out_x = half_w;
		*out_y = usable_y;
		*out_w = output_w - half_w;
		*out_h = half_h;
		break;
	case SNAP_BOTTOM_LEFT:
		*out_x = 0;
		*out_y = usable_y + half_h;
		*out_w = half_w;
		*out_h = usable_h - half_h;
		break;
	case SNAP_BOTTOM_RIGHT:
		*out_x = half_w;
		*out_y = usable_y + half_h;
		*out_w = output_w - half_w;
		*out_h = usable_h - half_h;
		break;
	case SNAP_NONE:
	default:
		*out_x = 0;
		*out_y = 0;
		*out_w = 0;
		*out_h = 0;
		break;
	}
}

struct diamonds_snap_preview *diamonds_snap_preview_create(struct diamonds_server *server) {
	struct diamonds_snap_preview *preview = calloc(1, sizeof(*preview));
	if (!preview) {
		return NULL;
	}

	preview->server = server;
	preview->current_zone = SNAP_NONE;
	preview->visible = false;
	preview->target_w = 400;
	preview->target_h = 300;

	preview->buffer = diamonds_cairo_buffer_create(preview->target_w, preview->target_h);
	if (!preview->buffer) {
		free(preview);
		return NULL;
	}

	preview->scene_buffer = wlr_scene_buffer_create(server->tree_overlay, &preview->buffer->base);
	if (!preview->scene_buffer) {
		wlr_buffer_drop(&preview->buffer->base);
		free(preview);
		return NULL;
	}

	wlr_scene_node_set_enabled(&preview->scene_buffer->node, false);
	return preview;
}

void diamonds_snap_preview_destroy(struct diamonds_snap_preview *preview) {
	if (!preview) return;
	if (preview->scene_buffer) {
		wlr_scene_node_destroy(&preview->scene_buffer->node);
	}
	if (preview->buffer) {
		wlr_buffer_drop(&preview->buffer->base);
	}
	free(preview);
}

void diamonds_snap_preview_update(
		struct diamonds_snap_preview *preview,
		diamonds_snap_zone_t zone,
		int32_t output_w,
		int32_t output_h) {
	if (!preview) return;

	if (zone == SNAP_NONE) {
		diamonds_snap_preview_hide(preview);
		return;
	}

	int32_t tx, ty, tw, th;
	calculate_zone_geometry(zone, output_w, output_h, &tx, &ty, &tw, &th);

	if (tw <= 0 || th <= 0) {
		diamonds_snap_preview_hide(preview);
		return;
	}

	preview->current_zone = zone;
	preview->target_x = tx;
	preview->target_y = ty;
	preview->target_w = tw;
	preview->target_h = th;

	diamonds_cairo_buffer_resize(&preview->buffer, tw, th);
	if (preview->buffer && preview->buffer->cairo_surface) {
		wlr_scene_buffer_set_buffer(preview->scene_buffer, &preview->buffer->base);

		cairo_t *cr = cairo_create(preview->buffer->cairo_surface);
		if (cr) {
			cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
			cairo_paint(cr);
			cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

			/* Windows 11 style frosted Snap Preview card with Cyan accent */
			diamonds_draw_rounded_rect(cr, 4.0, 4.0, (double)tw - 8.0, (double)th - 8.0, 10.0);
			cairo_set_source_rgba(cr, 0.2196, 0.7412, 0.9725, 0.16); /* #38BDF8 alpha 0.16 */
			cairo_fill_preserve(cr);

			cairo_set_source_rgba(cr, 0.2196, 0.7412, 0.9725, 0.85); /* #38BDF8 alpha 0.85 */
			cairo_set_line_width(cr, 2.0);
			cairo_stroke(cr);

			cairo_destroy(cr);
		}

		pixman_region32_t damage;
		pixman_region32_init_rect(&damage, 0, 0, (uint32_t)tw, (uint32_t)th);
		wlr_scene_buffer_set_buffer_with_damage(preview->scene_buffer, &preview->buffer->base, &damage);
		pixman_region32_fini(&damage);
	}

	wlr_scene_node_set_position(&preview->scene_buffer->node, tx, ty);
	wlr_scene_node_set_enabled(&preview->scene_buffer->node, true);
	preview->visible = true;
}

void diamonds_snap_preview_hide(struct diamonds_snap_preview *preview) {
	if (!preview) return;
	preview->current_zone = SNAP_NONE;
	preview->visible = false;
	if (preview->scene_buffer) {
		wlr_scene_node_set_enabled(&preview->scene_buffer->node, false);
	}
}

void diamonds_apply_snap(
		struct diamonds_toplevel *toplevel,
		diamonds_snap_zone_t zone,
		int32_t output_w,
		int32_t output_h) {
	if (!toplevel || !toplevel->xdg_toplevel || zone == SNAP_NONE) {
		return;
	}

	if (zone == SNAP_MAXIMIZE) {
		if (!toplevel->is_maximized) {
			diamonds_window_toggle_maximize(toplevel, NULL);
		} else {
			/* Already flagged maximized, ensure position & size match full workarea */
			toplevel->is_maximized = false;
			diamonds_window_toggle_maximize(toplevel, NULL);
		}
		diamonds_sound_play(DIAMONDS_SOUND_SNAP);
		return;
	}

	int32_t tx, ty, tw, th;
	calculate_zone_geometry(zone, output_w, output_h, &tx, &ty, &tw, &th);
	if (tw <= 0 || th <= 0) return;

	toplevel->is_maximized = false;
	wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
	wlr_xdg_toplevel_set_tiled(toplevel->xdg_toplevel,
		WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);

	/* Position toplevel */
	wlr_scene_node_set_position(&toplevel->scene_tree->node, tx, ty);

	/* Set client dimensions (only subtract titlebar height if SSD titlebar is active) */
	int32_t client_h = th;
	if (toplevel->titlebar_scene_buffer) {
		client_h -= TITLEBAR_HEIGHT;
	}
	if (client_h < 50) client_h = 50;
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, tw, client_h);

	if (toplevel->titlebar_scene_buffer) {
		diamonds_toplevel_update_titlebar(toplevel);
	}
	if (toplevel->server && toplevel->server->wl_display) {
		wl_display_flush_clients(toplevel->server->wl_display);
	}

	/* Play satisfying snap sound */
	diamonds_sound_play(DIAMONDS_SOUND_SNAP);
}
