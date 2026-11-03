#include "topbar.h"
#include "diamonds.h"
#include "calendar.h"
#include "control_center.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pixman.h>
#include <pango/pangocairo.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int topbar_timer_handler(void *data) {
	struct diamonds_topbar *topbar = data;
	time_t now = time(NULL);
	struct tm *local = localtime(&now);
	char buf[64];

	if (local) {
		strftime(buf, sizeof(buf), "%a, %d %b  %H:%M", local);
	} else {
		snprintf(buf, sizeof(buf), "00:00");
	}

	if (strcmp(buf, topbar->last_date_str) != 0) {
		snprintf(topbar->last_date_str, sizeof(topbar->last_date_str), "%s", buf);
		diamonds_topbar_render(topbar);
	}

	wl_event_source_timer_update(topbar->timer_source, 1000);
	return 0;
}

struct diamonds_topbar *diamonds_topbar_create(struct diamonds_server *server) {
	struct diamonds_topbar *topbar = calloc(1, sizeof(*topbar));
	if (!topbar) {
		return NULL;
	}

	topbar->server = server;
	topbar->width = 1920;
	topbar->height = 30;
	topbar->active_workspace = 1;
	topbar->control_center_active = false;
	topbar->last_date_str[0] = '\0';

	topbar->buffer = diamonds_cairo_buffer_create(topbar->width, topbar->height);
	if (!topbar->buffer) {
		free(topbar);
		return NULL;
	}

	topbar->scene_buffer = wlr_scene_buffer_create(server->tree_overlay, &topbar->buffer->base);
	if (!topbar->scene_buffer) {
		wlr_buffer_drop(&topbar->buffer->base);
		free(topbar);
		return NULL;
	}

	topbar->timer_source = wl_event_loop_add_timer(
		server->event_loop, topbar_timer_handler, topbar);
	if (topbar->timer_source) {
		wl_event_source_timer_update(topbar->timer_source, 100);
	}

	diamonds_topbar_render(topbar);
	return topbar;
}

void diamonds_topbar_destroy(struct diamonds_topbar *topbar) {
	if (!topbar) {
		return;
	}
	if (topbar->timer_source) {
		wl_event_source_remove(topbar->timer_source);
	}
	if (topbar->scene_buffer) {
		wlr_scene_node_destroy(&topbar->scene_buffer->node);
	}
	if (topbar->buffer) {
		wlr_buffer_drop(&topbar->buffer->base);
	}
	free(topbar);
}

void diamonds_topbar_reposition(struct diamonds_topbar *topbar, int32_t output_width, int32_t output_height) {
	(void)output_height;
	if (!topbar || !topbar->scene_buffer) {
		return;
	}

	if (topbar->width != output_width) {
		topbar->width = output_width;
		diamonds_cairo_buffer_resize(&topbar->buffer, topbar->width, topbar->height);
		if (topbar->buffer) {
			wlr_scene_buffer_set_buffer(topbar->scene_buffer, &topbar->buffer->base);
		}
	}

	wlr_scene_node_set_position(&topbar->scene_buffer->node, 0, 0);
	diamonds_topbar_render(topbar);
}

void diamonds_topbar_render(struct diamonds_topbar *topbar) {
	if (!topbar || !topbar->buffer || !topbar->buffer->cairo_surface) {
		return;
	}

	cairo_t *cr = cairo_create(topbar->buffer->cairo_surface);
	if (!cr) {
		return;
	}

	double w = topbar->width;
	double h = topbar->height;

	/* 1. Clear background */
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	/* 2. Top bar background (#121218 with 0.94 alpha) */
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_set_source_rgba(cr, 0.0706, 0.0706, 0.0941, 0.94);
	cairo_fill(cr);

	/* Subtle bottom border line: #282836 */
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 0.1568, 0.1568, 0.2118, 1.0);
	cairo_move_to(cr, 0, h - 0.5);
	cairo_line_to(cr, w, h - 0.5);
	cairo_stroke(cr);

	/* 3. Virtual Desktops (1..4) on the left */
	double ws_start_x = 12.0;
	double ws_w = 22.0;
	double ws_h = 20.0;
	double ws_spacing = 26.0;

	for (int i = 1; i <= 4; i++) {
		double bx = ws_start_x + (double)(i - 1) * ws_spacing;
		double by = (h - ws_h) / 2.0;

		diamonds_draw_rounded_rect(cr, bx, by, ws_w, ws_h, 4.0);

		if (i == topbar->active_workspace) {
			cairo_set_source_rgba(cr, 0.2196, 0.7412, 0.9725, 1.0); /* #38BDF8 */
			cairo_fill(cr);
		} else {
			cairo_set_source_rgba(cr, 0.137, 0.137, 0.180, 0.8);
			cairo_fill_preserve(cr);
			cairo_set_source_rgba(cr, 0.25, 0.25, 0.35, 0.6);
			cairo_stroke(cr);
		}

		/* Number label */
		char num_str[2] = { (char)('0' + i), '\0' };
		PangoLayout *layout = pango_cairo_create_layout(cr);
		if (layout) {
			PangoFontDescription *desc = pango_font_description_from_string("Segoe UI, Cantarell, DejaVu Sans, Ubuntu, sans-serif Bold 9");
			pango_layout_set_font_description(layout, desc);
			pango_font_description_free(desc);
			pango_layout_set_text(layout, num_str, -1);

			int lw, lh;
			pango_layout_get_pixel_size(layout, &lw, &lh);
			cairo_move_to(cr, bx + (ws_w - lw) / 2.0, by + (ws_h - lh) / 2.0);

			if (i == topbar->active_workspace) {
				cairo_set_source_rgba(cr, 0.05, 0.08, 0.15, 1.0);
			} else {
				cairo_set_source_rgba(cr, 0.75, 0.78, 0.85, 1.0);
			}
			pango_cairo_show_layout(cr, layout);
			g_object_unref(layout);
		}
	}

	/* 4. Center: Date & 24H Clock */
	const char *date_str = topbar->last_date_str[0] ? topbar->last_date_str : "Diamonds OS";
	PangoLayout *date_layout = pango_cairo_create_layout(cr);
	if (date_layout) {
		PangoFontDescription *desc = pango_font_description_from_string("Segoe UI, Cantarell, DejaVu Sans, Ubuntu, sans-serif Semibold 10");
		pango_layout_set_font_description(date_layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(date_layout, date_str, -1);

		int dlw, dlh;
		pango_layout_get_pixel_size(date_layout, &dlw, &dlh);
		cairo_move_to(cr, (w - dlw) / 2.0, (h - dlh) / 2.0);
		cairo_set_source_rgba(cr, 0.94, 0.96, 0.98, 1.0);
		pango_cairo_show_layout(cr, date_layout);
		g_object_unref(date_layout);
	}

	/* 5. Control Center Toggle Pill (Far Right) */
	double cc_w = 68.0;
	double cc_h = 22.0;
	double cc_x = w - cc_w - 10.0;
	double cc_y = (h - cc_h) / 2.0;

	diamonds_draw_rounded_rect(cr, cc_x, cc_y, cc_w, cc_h, 11.0);
	if (topbar->control_center_active) {
		cairo_set_source_rgba(cr, 0.2196, 0.7412, 0.9725, 0.35);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0.2196, 0.7412, 0.9725, 0.9);
		cairo_set_line_width(cr, 1.0);
		cairo_stroke(cr);
	} else {
		cairo_set_source_rgba(cr, 0.14, 0.14, 0.19, 0.85);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0.25, 0.25, 0.35, 0.7);
		cairo_set_line_width(cr, 1.0);
		cairo_stroke(cr);
	}

	/* Sliders / Control icons inside pill */
	cairo_set_source_rgba(cr, 0.88, 0.91, 0.95, 1.0);
	cairo_set_line_width(cr, 1.5);
	/* Bar 1 with knob */
	cairo_move_to(cr, cc_x + 14.0, cc_y + 7.0);
	cairo_line_to(cr, cc_x + 30.0, cc_y + 7.0);
	cairo_stroke(cr);
	cairo_arc(cr, cc_x + 24.0, cc_y + 7.0, 2.0, 0.0, 2.0 * M_PI);
	cairo_fill(cr);

	/* Bar 2 with knob */
	cairo_move_to(cr, cc_x + 14.0, cc_y + 14.0);
	cairo_line_to(cr, cc_x + 30.0, cc_y + 14.0);
	cairo_stroke(cr);
	cairo_arc(cr, cc_x + 18.0, cc_y + 14.0, 2.0, 0.0, 2.0 * M_PI);
	cairo_fill(cr);

	/* Battery outline */
	diamonds_draw_rounded_rect(cr, cc_x + 38.0, cc_y + 6.5, 18.0, 9.0, 2.0);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);
	cairo_rectangle(cr, cc_x + 40.0, cc_y + 8.5, 10.0, 5.0);
	cairo_fill(cr);

	cairo_destroy(cr);

	/* Damage scene node */
	pixman_region32_t damage;
	pixman_region32_init_rect(&damage, 0, 0, (uint32_t)topbar->width, (uint32_t)topbar->height);
	wlr_scene_buffer_set_buffer_with_damage(topbar->scene_buffer, &topbar->buffer->base, &damage);
	pixman_region32_fini(&damage);
}

bool diamonds_topbar_handle_click(struct diamonds_topbar *topbar, double lx, double ly) {
	if (!topbar || ly < 0 || ly > topbar->height) {
		return false;
	}

	/* Check workspace clicks (1..4) */
	double ws_start_x = 12.0;
	double ws_w = 22.0;
	double ws_spacing = 26.0;

	for (int i = 1; i <= 4; i++) {
		double bx = ws_start_x + (double)(i - 1) * ws_spacing;
		if (lx >= bx && lx <= bx + ws_w) {
			topbar->active_workspace = i;
			diamonds_topbar_render(topbar);
			if (topbar->server) {
				diamonds_server_switch_workspace(topbar->server, i - 1);
			}
			return true;
		}
	}

	/* Check Date / Calendar popup click in center */
	double center_x = (double)topbar->width / 2.0;
	if (lx >= center_x - 110.0 && lx <= center_x + 110.0) {
		if (topbar->server && topbar->server->calendar) {
			diamonds_calendar_toggle(topbar->server->calendar);
		}
		return true;
	}

	/* Check Control Center pill click */
	double cc_w = 68.0;
	double cc_x = topbar->width - cc_w - 10.0;
	if (lx >= cc_x && lx <= cc_x + cc_w) {
		if (topbar->server && topbar->server->control_center) {
			diamonds_control_center_toggle(topbar->server->control_center);
			topbar->control_center_active = topbar->server->control_center->visible;
		} else {
			topbar->control_center_active = !topbar->control_center_active;
		}
		diamonds_topbar_render(topbar);
		return true;
	}

	return false;
}
