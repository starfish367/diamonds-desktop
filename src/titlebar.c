#include "titlebar.h"
#include "cairo_buffer.h"
#include "sound.h"

#include <stdlib.h>
#include <string.h>
#include <pixman.h>
#include <pango/pangocairo.h>

void diamonds_toplevel_create_titlebar(struct diamonds_toplevel *toplevel) {
	if (!toplevel || toplevel->titlebar_scene_buffer) {
		return;
	}

	toplevel->titlebar_width = 640;
	toplevel->titlebar_buffer = diamonds_cairo_buffer_create(toplevel->titlebar_width, TITLEBAR_HEIGHT);
	if (!toplevel->titlebar_buffer) {
		return;
	}

	toplevel->titlebar_scene_buffer = wlr_scene_buffer_create(
		toplevel->scene_tree, &toplevel->titlebar_buffer->base);
	if (!toplevel->titlebar_scene_buffer) {
		wlr_buffer_drop(&toplevel->titlebar_buffer->base);
		toplevel->titlebar_buffer = NULL;
		return;
	}

	wlr_scene_node_set_position(&toplevel->titlebar_scene_buffer->node, 0, 0);
	diamonds_toplevel_update_titlebar(toplevel);
}

void diamonds_toplevel_update_titlebar(struct diamonds_toplevel *toplevel) {
	if (!toplevel || !toplevel->titlebar_scene_buffer) {
		return;
	}

	int32_t width = 640;
	struct wlr_box box = {0};
	wlr_xdg_surface_get_geometry(toplevel->xdg_toplevel->base, &box);
	if (box.width > 0) {
		width = box.width;
	} else if (toplevel->xdg_toplevel->base->surface && toplevel->xdg_toplevel->base->surface->current.width > 0) {
		width = toplevel->xdg_toplevel->base->surface->current.width;
	}
	if (width < 200) width = 200;

	toplevel->titlebar_width = width;
	diamonds_cairo_buffer_resize(&toplevel->titlebar_buffer, width, TITLEBAR_HEIGHT);
	if (!toplevel->titlebar_buffer || !toplevel->titlebar_buffer->cairo_surface) {
		return;
	}

	wlr_scene_buffer_set_buffer(toplevel->titlebar_scene_buffer, &toplevel->titlebar_buffer->base);

	cairo_t *cr = cairo_create(toplevel->titlebar_buffer->cairo_surface);
	if (!cr) return;

	double w = (double)width;
	double h = (double)TITLEBAR_HEIGHT;

	/* 1. Clear background */
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	/* 2. Titlebar background: #161622 (Mica graphite) */
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_set_source_rgba(cr, 0.086, 0.086, 0.133, 0.98);
	cairo_fill(cr);

	/* Bottom border line */
	cairo_move_to(cr, 0, h - 0.5);
	cairo_line_to(cr, w, h - 0.5);
	cairo_set_source_rgba(cr, 0.17, 0.17, 0.24, 1.0);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* 3. Small App Icon + Title */
	cairo_rectangle(cr, 12, 10, 12, 12);
	cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.9);
	cairo_fill(cr);

	const char *title = toplevel->xdg_toplevel->title;
	if (!title || !title[0]) {
		title = toplevel->xdg_toplevel->app_id;
	}
	if (!title || !title[0]) {
		title = "Diamonds Window";
	}

	PangoLayout *layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string("Segoe UI, Cantarell, DejaVu Sans, Ubuntu, sans-serif Bold 9.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, title, -1);

		cairo_move_to(cr, 32, 8);
		cairo_set_source_rgba(cr, 0.88, 0.91, 0.96, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* 4. Windows 11 SSD Control Buttons */
	/* Button 1: Minimize (-) at w - 100 */
	double btn_min_x = w - 102.0;
	cairo_move_to(cr, btn_min_x + 10.0, 17.5);
	cairo_line_to(cr, btn_min_x + 22.0, 17.5);
	cairo_set_source_rgba(cr, 0.80, 0.84, 0.92, 1.0);
	cairo_set_line_width(cr, 1.4);
	cairo_stroke(cr);

	/* Button 2: Maximize / Restore (□) at w - 68 */
	double btn_max_x = w - 70.0;
	if (toplevel->is_maximized) {
		/* Restore double box */
		cairo_rectangle(cr, btn_max_x + 12.0, 9.0, 9.0, 9.0);
		cairo_stroke(cr);
		cairo_rectangle(cr, btn_max_x + 9.0, 12.0, 9.0, 9.0);
		cairo_stroke(cr);
	} else {
		/* Single box */
		cairo_rectangle(cr, btn_max_x + 10.0, 10.0, 11.0, 11.0);
		cairo_set_source_rgba(cr, 0.80, 0.84, 0.92, 1.0);
		cairo_set_line_width(cr, 1.2);
		cairo_stroke(cr);
	}

	/* Button 3: Close (✕) at w - 36 */
	double btn_close_x = w - 38.0;
	cairo_move_to(cr, btn_close_x + 11.0, 11.0);
	cairo_line_to(cr, btn_close_x + 21.0, 21.0);
	cairo_stroke(cr);
	cairo_move_to(cr, btn_close_x + 21.0, 11.0);
	cairo_line_to(cr, btn_close_x + 11.0, 21.0);
	cairo_stroke(cr);

	cairo_destroy(cr);

	pixman_region32_t damage;
	pixman_region32_init_rect(&damage, 0, 0, (uint32_t)width, TITLEBAR_HEIGHT);
	wlr_scene_buffer_set_buffer_with_damage(toplevel->titlebar_scene_buffer, &toplevel->titlebar_buffer->base, &damage);
	pixman_region32_fini(&damage);
}

void diamonds_toplevel_destroy_titlebar(struct diamonds_toplevel *toplevel) {
	if (!toplevel) return;
	if (toplevel->titlebar_scene_buffer) {
		wlr_scene_node_destroy(&toplevel->titlebar_scene_buffer->node);
		toplevel->titlebar_scene_buffer = NULL;
	}
	if (toplevel->titlebar_buffer) {
		wlr_buffer_drop(&toplevel->titlebar_buffer->base);
		toplevel->titlebar_buffer = NULL;
	}
}

diamonds_titlebar_action_t diamonds_titlebar_handle_click(
		struct diamonds_toplevel *toplevel, double rel_x, double rel_y) {
	if (!toplevel || rel_y < 0 || rel_y > TITLEBAR_HEIGHT) {
		return TITLEBAR_CLICK_NONE;
	}

	double w = (double)toplevel->titlebar_width;

	/* Close button */
	if (rel_x >= w - 38.0 && rel_x <= w) {
		diamonds_window_close(toplevel, NULL);
		return TITLEBAR_CLICK_CLOSE;
	}

	/* Maximize button */
	if (rel_x >= w - 70.0 && rel_x < w - 38.0) {
		diamonds_window_toggle_maximize(toplevel, NULL);
		return TITLEBAR_CLICK_MAXIMIZE;
	}

	/* Minimize button */
	if (rel_x >= w - 102.0 && rel_x < w - 70.0) {
		diamonds_window_minimize(toplevel, NULL);
		return TITLEBAR_CLICK_MINIMIZE;
	}

	/* Drag / Move */
	return TITLEBAR_CLICK_MOVE;
}
