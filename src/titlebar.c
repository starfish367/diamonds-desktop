#include "titlebar.h"
#include "cairo_buffer.h"
#include "sound.h"
#include "icon_loader.h"
#include "vector_icons.h"

#include <stdlib.h>
#include <string.h>
#include <pixman.h>
#include <strings.h>
#include <math.h>
#include <pango/pangocairo.h>

#define TITLEBAR_ICON_SIZE 16

static const char *last_char_name(const char *id) {
	const char *dot = strrchr(id, '.');
	return (dot && dot[1]) ? dot + 1 : id;
}

/* Draw the window icon at (x, y). Order: built-in Diamonds vector icons ->
 * desktop/theme icon (works for GTK, Qt, XWayland) -> initial-letter badge. */
static void draw_titlebar_app_icon(cairo_t *cr, double x, double y,
		const char *app_id, const char *alt_name, const char *title) {
	const double s = TITLEBAR_ICON_SIZE;
	const double cx = x + s / 2.0, cy = y + s / 2.0;

	if (app_id && strncasecmp(app_id, "diamonds-", 9) == 0) {
		const char *k = app_id + 9;
		if (strncasecmp(k, "files", 5) == 0)    { diamonds_draw_vector_files(cr, cx, cy, s); return; }
		if (strncasecmp(k, "settings", 8) == 0) { diamonds_draw_vector_settings(cr, cx, cy, s); return; }
		if (strncasecmp(k, "store", 5) == 0)    { diamonds_draw_vector_store(cr, cx, cy, s); return; }
		if (strncasecmp(k, "clock", 5) == 0)    { diamonds_draw_vector_clock(cr, cx, cy, s); return; }
		if (strncasecmp(k, "mail", 4) == 0)     { diamonds_draw_vector_mail(cr, cx, cy, s); return; }
		if (strncasecmp(k, "terminal", 8) == 0) { diamonds_draw_vector_terminal(cr, cx, cy, s); return; }
		if (strncasecmp(k, "music", 5) == 0)    { diamonds_draw_vector_media(cr, cx, cy, s); return; }
	}

	cairo_surface_t *icon = diamonds_load_app_icon(app_id, alt_name, TITLEBAR_ICON_SIZE);
	if (icon) {
		int iw = cairo_image_surface_get_width(icon);
		int ih = cairo_image_surface_get_height(icon);
		cairo_save(cr);
		/* Center non-square icons inside the 16x16 slot */
		cairo_set_source_surface(cr, icon, x + (s - iw) / 2.0, y + (s - ih) / 2.0);
		cairo_paint(cr);
		cairo_restore(cr);
		return;
	}

	/* Fallback: rounded badge with first letter */
	diamonds_draw_rounded_rect(cr, x, y, s, s, 3.0);
	cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.90);
	cairo_fill(cr);

	const char *name = (title && *title) ? title : ((app_id && *app_id) ? last_char_name(app_id) : "?");
	char letter[2] = { name[0] ? (char)g_ascii_toupper(name[0]) : '?', '\0' };
	PangoLayout *layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string("Sans Bold 8");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, letter, -1);
		int lw = 0, lh = 0;
		pango_layout_get_pixel_size(layout, &lw, &lh);
		cairo_move_to(cr, x + (s - lw) / 2.0, y + (s - lh) / 2.0);
		cairo_set_source_rgba(cr, 0.06, 0.07, 0.10, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}
}

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

	/* 3. App Icon + Title */
	draw_titlebar_app_icon(cr, 8, (h - TITLEBAR_ICON_SIZE) / 2.0,
		toplevel->xdg_toplevel->app_id, NULL, toplevel->xdg_toplevel->title);

	const char *title = toplevel->xdg_toplevel->title;
	if (!title || !title[0]) {
		title = toplevel->xdg_toplevel->app_id;
	}
	if (!title || !title[0]) {
		title = "Diamonds Window";
	}

	PangoLayout *layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string("Segoe UI, Cantarell, DejaVu Sans, Ubuntu, sans-serif Bold 9");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, title, -1);
		pango_layout_set_width(layout, (width - 120) * PANGO_SCALE);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

		cairo_move_to(cr, 28, 5);
		cairo_set_source_rgba(cr, 0.88, 0.91, 0.96, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* 4. Windows 11 SSD Control Buttons: Minimize (-), Maximize (□), Close (✕) */
	/* Button 1: Minimize (-) */
	double btn_min_x = w - 96.0;
	cairo_move_to(cr, btn_min_x + 8.0, 15.0);
	cairo_line_to(cr, btn_min_x + 18.0, 15.0);
	cairo_set_source_rgba(cr, 0.80, 0.84, 0.92, 1.0);
	cairo_set_line_width(cr, 1.4);
	cairo_stroke(cr);

	/* Button 2: Maximize / Restore (□) */
	double btn_max_x = w - 64.0;
	if (toplevel->is_maximized) {
		cairo_rectangle(cr, btn_max_x + 10.0, 7.0, 8.0, 8.0);
		cairo_stroke(cr);
		cairo_rectangle(cr, btn_max_x + 7.0, 10.0, 8.0, 8.0);
		cairo_stroke(cr);
	} else {
		cairo_rectangle(cr, btn_max_x + 8.0, 8.0, 10.0, 10.0);
		cairo_set_source_rgba(cr, 0.80, 0.84, 0.92, 1.0);
		cairo_set_line_width(cr, 1.2);
		cairo_stroke(cr);
	}

	/* Button 3: Close (✕) */
	double btn_close_x = w - 32.0;
	cairo_move_to(cr, btn_close_x + 8.0, 8.0);
	cairo_line_to(cr, btn_close_x + 18.0, 18.0);
	cairo_stroke(cr);
	cairo_move_to(cr, btn_close_x + 18.0, 8.0);
	cairo_line_to(cr, btn_close_x + 8.0, 18.0);
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
	if (rel_x >= w - 34.0 && rel_x <= w) {
		diamonds_window_close(toplevel, NULL);
		return TITLEBAR_CLICK_CLOSE;
	}

	/* Maximize button */
	if (rel_x >= w - 66.0 && rel_x < w - 34.0) {
		diamonds_window_toggle_maximize(toplevel, NULL);
		return TITLEBAR_CLICK_MAXIMIZE;
	}

	/* Minimize button */
	if (rel_x >= w - 98.0 && rel_x < w - 66.0) {
		diamonds_window_minimize(toplevel, NULL);
		return TITLEBAR_CLICK_MINIMIZE;
	}

	/* Drag / Move */
	return TITLEBAR_CLICK_MOVE;
}

#ifdef DIAMONDS_ENABLE_XWAYLAND
void diamonds_xwayland_surface_create_titlebar(struct diamonds_xwayland_surface *xsurface) {
	if (!xsurface || xsurface->titlebar_scene_buffer || !xsurface->scene_tree) {
		return;
	}
	if (xsurface->xsurface && xsurface->xsurface->override_redirect) {
		return;
	}

	int32_t width = xsurface->xsurface ? xsurface->xsurface->width : 640;
	if (width < 200) width = 200;
	xsurface->titlebar_width = width;
	xsurface->titlebar_buffer = diamonds_cairo_buffer_create(width, TITLEBAR_HEIGHT);
	if (!xsurface->titlebar_buffer) {
		return;
	}

	xsurface->titlebar_scene_buffer = wlr_scene_buffer_create(
		xsurface->scene_tree, &xsurface->titlebar_buffer->base);
	if (!xsurface->titlebar_scene_buffer) {
		wlr_buffer_drop(&xsurface->titlebar_buffer->base);
		xsurface->titlebar_buffer = NULL;
		return;
	}

	wlr_scene_node_set_position(&xsurface->titlebar_scene_buffer->node, 0, 0);
	diamonds_xwayland_surface_update_titlebar(xsurface);
}

void diamonds_xwayland_surface_update_titlebar(struct diamonds_xwayland_surface *xsurface) {
	if (!xsurface || !xsurface->titlebar_scene_buffer) {
		return;
	}

	int32_t width = xsurface->xsurface ? xsurface->xsurface->width : 640;
	if (width < 200) width = 200;

	xsurface->titlebar_width = width;
	diamonds_cairo_buffer_resize(&xsurface->titlebar_buffer, width, TITLEBAR_HEIGHT);
	if (!xsurface->titlebar_buffer || !xsurface->titlebar_buffer->cairo_surface) {
		return;
	}

	wlr_scene_buffer_set_buffer(xsurface->titlebar_scene_buffer, &xsurface->titlebar_buffer->base);

	cairo_t *cr = cairo_create(xsurface->titlebar_buffer->cairo_surface);
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

	/* 3. App Icon + Title */
	draw_titlebar_app_icon(cr, 8, (h - TITLEBAR_ICON_SIZE) / 2.0,
		xsurface->xsurface ? xsurface->xsurface->class : NULL,
		xsurface->xsurface ? xsurface->xsurface->instance : NULL,
		xsurface->xsurface ? xsurface->xsurface->title : NULL);

	const char *title = xsurface->xsurface ? xsurface->xsurface->title : NULL;
	if (!title || !title[0]) {
		title = xsurface->xsurface ? xsurface->xsurface->class : NULL;
	}
	if (!title || !title[0]) {
		title = "Diamonds Window (X11)";
	}

	PangoLayout *layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string("Segoe UI, Cantarell, DejaVu Sans, Ubuntu, sans-serif Bold 9");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, title, -1);
		pango_layout_set_width(layout, (width - 120) * PANGO_SCALE);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

		cairo_move_to(cr, 28, 5);
		cairo_set_source_rgba(cr, 0.88, 0.91, 0.96, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* 4. Windows 11 SSD Control Buttons: Minimize (-), Maximize (□), Close (✕) */
	/* Button 1: Minimize (-) */
	double btn_min_x = w - 96.0;
	cairo_move_to(cr, btn_min_x + 8.0, 15.0);
	cairo_line_to(cr, btn_min_x + 18.0, 15.0);
	cairo_set_source_rgba(cr, 0.80, 0.84, 0.92, 1.0);
	cairo_set_line_width(cr, 1.4);
	cairo_stroke(cr);

	/* Button 2: Maximize / Restore (□) */
	double btn_max_x = w - 64.0;
	if (xsurface->is_maximized) {
		cairo_rectangle(cr, btn_max_x + 10.0, 7.0, 8.0, 8.0);
		cairo_stroke(cr);
		cairo_rectangle(cr, btn_max_x + 7.0, 10.0, 8.0, 8.0);
		cairo_stroke(cr);
	} else {
		cairo_rectangle(cr, btn_max_x + 8.0, 8.0, 10.0, 10.0);
		cairo_set_source_rgba(cr, 0.80, 0.84, 0.92, 1.0);
		cairo_set_line_width(cr, 1.2);
		cairo_stroke(cr);
	}

	/* Button 3: Close (✕) */
	double btn_close_x = w - 32.0;
	cairo_move_to(cr, btn_close_x + 8.0, 8.0);
	cairo_line_to(cr, btn_close_x + 18.0, 18.0);
	cairo_stroke(cr);
	cairo_move_to(cr, btn_close_x + 18.0, 8.0);
	cairo_line_to(cr, btn_close_x + 8.0, 18.0);
	cairo_stroke(cr);

	cairo_destroy(cr);

	pixman_region32_t damage;
	pixman_region32_init_rect(&damage, 0, 0, (uint32_t)width, TITLEBAR_HEIGHT);
	wlr_scene_buffer_set_buffer_with_damage(xsurface->titlebar_scene_buffer, &xsurface->titlebar_buffer->base, &damage);
	pixman_region32_fini(&damage);
}

void diamonds_xwayland_surface_destroy_titlebar(struct diamonds_xwayland_surface *xsurface) {
	if (!xsurface) return;
	if (xsurface->titlebar_scene_buffer) {
		wlr_scene_node_destroy(&xsurface->titlebar_scene_buffer->node);
		xsurface->titlebar_scene_buffer = NULL;
	}
	if (xsurface->titlebar_buffer) {
		wlr_buffer_drop(&xsurface->titlebar_buffer->base);
		xsurface->titlebar_buffer = NULL;
	}
}

diamonds_titlebar_action_t diamonds_xwayland_titlebar_handle_click(
		struct diamonds_xwayland_surface *xsurface, double rel_x, double rel_y) {
	if (!xsurface || rel_y < 0 || rel_y > TITLEBAR_HEIGHT) {
		return TITLEBAR_CLICK_NONE;
	}

	double w = (double)xsurface->titlebar_width;

	/* Close button */
	if (rel_x >= w - 34.0 && rel_x <= w) {
		diamonds_window_close(NULL, xsurface);
		return TITLEBAR_CLICK_CLOSE;
	}

	/* Maximize button */
	if (rel_x >= w - 66.0 && rel_x < w - 34.0) {
		diamonds_window_toggle_maximize(NULL, xsurface);
		return TITLEBAR_CLICK_MAXIMIZE;
	}

	/* Minimize button */
	if (rel_x >= w - 98.0 && rel_x < w - 66.0) {
		diamonds_window_minimize(NULL, xsurface);
		return TITLEBAR_CLICK_MINIMIZE;
	}

	/* Drag / Move */
	return TITLEBAR_CLICK_MOVE;
}
#endif
