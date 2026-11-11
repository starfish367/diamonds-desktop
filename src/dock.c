#define _GNU_SOURCE
#include "dock.h"
#include "diamonds.h"
#include "vector_icons.h"
#include "icon_loader.h"
#include "sound.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <pixman.h>
#include <linux/input-event-codes.h>
#include <pango/pangocairo.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FONT_BASE "Segoe UI, Cantarell, DejaVu Sans, Ubuntu, sans-serif"

struct pinned_def {
	const char *name;
	const char *kw1;
	const char *kw2;
	const char *icon_key;
	bool is_running;
	bool is_focused;
	struct diamonds_toplevel *toplevel;
#ifdef DIAMONDS_ENABLE_XWAYLAND
	struct diamonds_xwayland_surface *xsurface;
#endif
	pid_t pid;
};

static void draw_window_initials_icon(cairo_t *cr, double ix, double iy, double size, const char *name) {
	/* 1. Window card background */
	diamonds_draw_rounded_rect(cr, ix, iy, size, size, 5.0);
	cairo_set_source_rgba(cr, 0.12, 0.14, 0.22, 0.95);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.50);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* 2. Mini top window bar */
	cairo_new_path(cr);
	cairo_move_to(cr, ix + 2.0, iy + 6.0);
	cairo_line_to(cr, ix + size - 2.0, iy + 6.0);
	cairo_set_source_rgba(cr, 0.25, 0.28, 0.40, 0.7);
	cairo_set_line_width(cr, 0.8);
	cairo_stroke(cr);

	/* 3. 3 dots on top bar */
	cairo_arc(cr, ix + 4.5, iy + 3.5, 1.0, 0.0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.94, 0.35, 0.35, 0.9);
	cairo_fill(cr);
	cairo_arc(cr, ix + 8.0, iy + 3.5, 1.0, 0.0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.96, 0.76, 0.25, 0.9);
	cairo_fill(cr);
	cairo_arc(cr, ix + 11.5, iy + 3.5, 1.0, 0.0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.35, 0.85, 0.45, 0.9);
	cairo_fill(cr);

	/* 4. Extract 2 uppercase initials */
	char initials[3] = "AP";
	if (name && *name) {
		const char *p = name;
		while (*p && !isalnum((unsigned char)*p)) p++;
		if (*p) {
			initials[0] = toupper((unsigned char)*p);
			p++;
			const char *space = strchr(p, ' ');
			if (space) {
				while (*space && !isalnum((unsigned char)*space)) space++;
				if (*space) {
					initials[1] = toupper((unsigned char)*space);
				} else if (*p && isalnum((unsigned char)*p)) {
					initials[1] = toupper((unsigned char)*p);
				}
			} else if (*p && isalnum((unsigned char)*p)) {
				initials[1] = toupper((unsigned char)*p);
			} else {
				initials[1] = '\0';
			}
		}
	}
	initials[2] = '\0';

	/* 5. Render 2 letters centered using Pango */
	PangoLayout *layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 7.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, initials, -1);
		int tw, th;
		pango_layout_get_pixel_size(layout, &tw, &th);
		cairo_move_to(cr, ix + (size - tw) / 2.0, iy + 7.0 + (size - 7.0 - th) / 2.0);
		cairo_set_source_rgba(cr, 0.95, 0.97, 1.0, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}
}

static struct pinned_def g_pinned_defaults[5] = {
	{ .name = "Terminal", .kw1 = "term", .kw2 = "bash", .icon_key = "terminal" },
	{ .name = "Files", .kw1 = "diamonds-files", .kw2 = "nautilus", .icon_key = "files" },
	{ .name = "Thorium", .kw1 = "thorium", .kw2 = "chromium", .icon_key = "browser" },
	{ .name = "VLC", .kw1 = "vlc", .kw2 = NULL, .icon_key = "vlc" },
	{ .name = "Settings", .kw1 = "diamonds-settings", .kw2 = "settings", .icon_key = "settings" },
};

struct diamonds_dock *diamonds_dock_create(struct diamonds_server *server) {
	struct diamonds_dock *dock = calloc(1, sizeof(*dock));
	if (!dock) {
		return NULL;
	}

	dock->server = server;
	dock->width = 1920;
	dock->height = 44;
	dock->margin_bottom = 0;
	dock->corner_radius = 0;
	dock->is_full_width = true;
	dock->dynamic_item_count = 0;

	dock->buffer = diamonds_cairo_buffer_create(dock->width, dock->height);
	if (!dock->buffer) {
		free(dock);
		return NULL;
	}

	dock->scene_buffer = wlr_scene_buffer_create(server->tree_overlay, &dock->buffer->base);
	if (!dock->scene_buffer) {
		wlr_buffer_drop(&dock->buffer->base);
		free(dock);
		return NULL;
	}

	/* Initialize Right-Click Popup Context Menu */
	dock->popup.dock = dock;
	dock->popup.width = 160;
	dock->popup.height = 76;
	dock->popup.visible = false;
	dock->popup.buffer = diamonds_cairo_buffer_create(dock->popup.width, dock->popup.height);
	if (dock->popup.buffer) {
		dock->popup.scene_buffer = wlr_scene_buffer_create(server->tree_overlay, &dock->popup.buffer->base);
		if (dock->popup.scene_buffer) {
			wlr_scene_node_set_enabled(&dock->popup.scene_buffer->node, false);
		}
	}

	diamonds_dock_reposition(dock, 1920, 1080);
	return dock;
}

void diamonds_dock_destroy(struct diamonds_dock *dock) {
	if (!dock) {
		return;
	}
	if (dock->popup.scene_buffer) {
		wlr_scene_node_destroy(&dock->popup.scene_buffer->node);
	}
	if (dock->popup.buffer) {
		wlr_buffer_drop(&dock->popup.buffer->base);
	}
	if (dock->scene_buffer) {
		wlr_scene_node_destroy(&dock->scene_buffer->node);
	}
	if (dock->buffer) {
		wlr_buffer_drop(&dock->buffer->base);
	}
	free(dock);
}

void diamonds_dock_reposition(struct diamonds_dock *dock, int32_t output_width, int32_t output_height) {
	if (!dock || !dock->scene_buffer) {
		return;
	}

	int32_t new_w = output_width;
	int32_t new_h = 44;
	int32_t new_x = 0;
	int32_t new_y = output_height - 44;
	int32_t new_r = 0;
	int32_t new_mb = 0;
	dock->is_full_width = true;

	if (dock->width != new_w || dock->height != new_h) {
		dock->width = new_w;
		dock->height = new_h;
		diamonds_cairo_buffer_resize(&dock->buffer, dock->width, dock->height);
		if (dock->buffer) {
			wlr_scene_buffer_set_buffer(dock->scene_buffer, &dock->buffer->base);
		}
	}

	dock->x = new_x;
	dock->y = new_y;
	dock->corner_radius = new_r;
	dock->margin_bottom = new_mb;

	wlr_scene_node_set_position(&dock->scene_buffer->node, dock->x, dock->y);
	diamonds_dock_render(dock);
}

void diamonds_dock_render(struct diamonds_dock *dock) {
	if (!dock || !dock->buffer || !dock->buffer->cairo_surface || !dock->server) {
		return;
	}

	cairo_t *cr = cairo_create(dock->buffer->cairo_surface);
	if (!cr) {
		return;
	}

	double w = dock->width;
	double h = dock->height;
	double r = dock->corner_radius;

	/* 1. Clear background surface */
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	/* 2. Render Taskbar Background */
	if (r == 0) {
		/* Windows-style full-width flat bottom taskbar */
		cairo_rectangle(cr, 0, 0, w, h);
		cairo_set_source_rgba(cr, 0.086, 0.086, 0.118, 0.96); /* #16161E */
		cairo_fill(cr);

		/* Top border line */
		cairo_new_path(cr);
		cairo_move_to(cr, 0, 0.5);
		cairo_line_to(cr, w, 0.5);
		cairo_set_source_rgba(cr, 0.22, 0.22, 0.30, 1.0);
		cairo_set_line_width(cr, 1.0);
		cairo_stroke(cr);
	} else {
		/* Floating Dock */
		diamonds_draw_rounded_rect(cr, 0.5, 0.5, w - 1.0, h - 1.0, r);
		cairo_set_source_rgba(cr, 0.0941, 0.0941, 0.1255, 0.92);
		cairo_fill_preserve(cr);

		/* Border: 1px #323242 */
		cairo_set_source_rgba(cr, 0.1961, 0.1961, 0.2588, 1.0);
		cairo_set_line_width(cr, 1.0);
		cairo_stroke(cr);
	}

	/* 3. Collect Pinned and Dynamic windows from server toplevels & xsurfaces */
	struct pinned_def pinned[5];
	memcpy(pinned, g_pinned_defaults, sizeof(pinned));
	for (int k = 0; k < 5; k++) {
		pinned[k].is_running = false;
		pinned[k].is_focused = false;
		pinned[k].toplevel = NULL;
#ifdef DIAMONDS_ENABLE_XWAYLAND
		pinned[k].xsurface = NULL;
#endif
		pinned[k].pid = 0;
	}

	dock->dynamic_item_count = 0;

	/* 3a. Scan Wayland toplevels */
	struct diamonds_toplevel *toplevel;
	wl_list_for_each(toplevel, &dock->server->toplevels, link) {
		if (!toplevel->xdg_toplevel) continue;
		const char *app_id = toplevel->xdg_toplevel->app_id;
		const char *title = toplevel->xdg_toplevel->title;

		int match_idx = -1;
		for (int k = 0; k < 5; k++) {
			bool match = false;
			if (pinned[k].kw1 && ((app_id && strcasestr(app_id, pinned[k].kw1)) || (title && strcasestr(title, pinned[k].kw1)))) match = true;
			if (pinned[k].kw2 && ((app_id && strcasestr(app_id, pinned[k].kw2)) || (title && strcasestr(title, pinned[k].kw2)))) match = true;
			if (match && pinned[k].toplevel == NULL) {
				match_idx = k;
				break;
			}
		}

		pid_t pid = 0;
		struct wl_resource *res = toplevel->xdg_toplevel->resource ?
			toplevel->xdg_toplevel->resource :
			(toplevel->xdg_toplevel->base ? toplevel->xdg_toplevel->base->resource : NULL);
		if (res) {
			struct wl_client *client = wl_resource_get_client(res);
			if (client) wl_client_get_credentials(client, &pid, NULL, NULL);
		}

		bool is_focused = (dock->server->focused_toplevel == toplevel);

		if (match_idx >= 0) {
			pinned[match_idx].is_running = true;
			if (is_focused) pinned[match_idx].is_focused = true;
			pinned[match_idx].toplevel = toplevel;
			pinned[match_idx].pid = pid;
		} else {
			if (dock->dynamic_item_count < MAX_DOCK_DYNAMIC_ITEMS) {
				struct diamonds_dock_dynamic_item *dyn = &dock->dynamic_items[dock->dynamic_item_count++];
				dyn->toplevel = toplevel;
#ifdef DIAMONDS_ENABLE_XWAYLAND
				dyn->xsurface = NULL;
#endif
				snprintf(dyn->app_id, sizeof(dyn->app_id), "%s", app_id ? app_id : "");
				snprintf(dyn->title, sizeof(dyn->title), "%s", title ? title : (app_id ? app_id : "Application"));
				dyn->pid = pid;
				dyn->is_focused = is_focused;
				dyn->is_running = true;
			}
		}
	}

#ifdef DIAMONDS_ENABLE_XWAYLAND
	/* 3b. Scan XWayland surfaces */
	struct diamonds_xwayland_surface *xsurface;
	wl_list_for_each(xsurface, &dock->server->xwayland_surfaces, link) {
		if (!xsurface->xsurface || !xsurface->xsurface->surface) continue;
		if (xsurface->xsurface->override_redirect) continue;
		const char *class = xsurface->xsurface->class;
		const char *instance = xsurface->xsurface->instance;
		const char *title = xsurface->xsurface->title;

		int match_idx = -1;
		for (int k = 0; k < 5; k++) {
			bool match = false;
			if (pinned[k].kw1 && ((class && strcasestr(class, pinned[k].kw1)) || (instance && strcasestr(instance, pinned[k].kw1)) || (title && strcasestr(title, pinned[k].kw1)))) match = true;
			if (pinned[k].kw2 && ((class && strcasestr(class, pinned[k].kw2)) || (instance && strcasestr(instance, pinned[k].kw2)) || (title && strcasestr(title, pinned[k].kw2)))) match = true;
			if (match && pinned[k].toplevel == NULL && pinned[k].xsurface == NULL) {
				match_idx = k;
				break;
			}
		}

		pid_t pid = xsurface->xsurface->pid;
		bool is_focused = (dock->server->focused_toplevel == NULL && xsurface->scene_tree && xsurface->scene_tree->node.enabled);

		if (match_idx >= 0) {
			pinned[match_idx].is_running = true;
			if (is_focused) pinned[match_idx].is_focused = true;
			pinned[match_idx].xsurface = xsurface;
			pinned[match_idx].pid = pid;
		} else {
			if (dock->dynamic_item_count < MAX_DOCK_DYNAMIC_ITEMS) {
				struct diamonds_dock_dynamic_item *dyn = &dock->dynamic_items[dock->dynamic_item_count++];
				dyn->toplevel = NULL;
				dyn->xsurface = xsurface;
				snprintf(dyn->app_id, sizeof(dyn->app_id), "%s", class ? class : (instance ? instance : ""));
				snprintf(dyn->title, sizeof(dyn->title), "%s", title ? title : (class ? class : "Application"));
				dyn->pid = pid;
				dyn->is_focused = is_focused;
				dyn->is_running = true;
			}
		}
	}
#endif

	/* 4. Calculate Total Content Width & Center */
	double item_spacing = 46.0;
	double icon_size = 24.0;
	int num_pinned = 5;
	int num_dynamic = dock->dynamic_item_count;

	/* Start (24) + sep1 (1) + gaps (28) = 53
	 * Pinned items = 5 * 46
	 * If dynamic > 0: sep_dyn (1) + gaps (28) + num_dynamic * 46 = 29 + num_dynamic * 46
	 * Trash = sep2 (1) + gaps (28) + Trash (24) = 53
	 */
	double content_w = 53.0 + (double)num_pinned * item_spacing + 53.0;
	if (num_dynamic > 0) {
		content_w += 29.0 + (double)num_dynamic * item_spacing;
	}

	double content_start_x = (w - content_w) / 2.0;
	if (content_start_x < 8.0) content_start_x = 8.0;
	dock->content_start_x = content_start_x;

	/* Start Button (Diamond logo rotated 90 deg) */
	double start_btn_cx = content_start_x + 12.0;
	diamonds_draw_vector_diamond_logo(cr, start_btn_cx, h / 2.0, 20.0);

	/* Separator 1 after Start */
	double sep1_x = content_start_x + 28.5;
	cairo_new_path(cr);
	cairo_move_to(cr, sep1_x, 10.0);
	cairo_line_to(cr, sep1_x, h - 10.0);
	cairo_set_source_rgba(cr, 0.22, 0.22, 0.30, 0.85);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* 5. Render Pinned Apps */
	double icons_start_x = sep1_x + 14.0;
	dock->icons_start_x = icons_start_x;

	for (size_t i = 0; i < 5; i++) {
		double ix = icons_start_x + (double)i * item_spacing;
		dock->pinned_item_x[i] = ix;
		double cx = ix + icon_size / 2.0;
		double cy = (h - icon_size) / 2.0 + icon_size / 2.0;

		cairo_surface_t *sys_icon = diamonds_load_system_icon(pinned[i].icon_key, (int)icon_size);
		if (sys_icon) {
			cairo_save(cr);
			cairo_set_source_surface(cr, sys_icon, ix, (h - icon_size) / 2.0);
			cairo_paint(cr);
			cairo_restore(cr);
		} else {
			switch (i) {
			case 0:
				diamonds_draw_vector_terminal(cr, cx, cy, icon_size);
				break;
			case 1:
				diamonds_draw_vector_files(cr, cx, cy, icon_size);
				break;
			case 2:
				diamonds_draw_vector_browser(cr, cx, cy, icon_size);
				break;
			case 3:
				diamonds_draw_vector_media(cr, cx, cy, icon_size);
				break;
			case 4:
			default:
				diamonds_draw_vector_settings(cr, cx, cy, icon_size);
				break;
			}
		}

		/* Indicator: Win11 bar if focused, macOS dot if background */
		if (pinned[i].is_focused) {
			double bar_w = 16.0;
			double bar_h = 3.0;
			double bar_x = cx - bar_w / 2.0;
			double bar_y = h - 5.5;
			diamonds_draw_rounded_rect(cr, bar_x, bar_y, bar_w, bar_h, 1.5);
			cairo_set_source_rgba(cr, 0.2196, 0.7412, 0.9725, 1.0); /* #38BDF8 */
			cairo_fill(cr);
		} else if (pinned[i].is_running) {
			double dot_y = h - 4.5;
			cairo_new_path(cr);
			cairo_arc(cr, cx, dot_y, 2.0, 0.0, 2.0 * M_PI);
			cairo_set_source_rgba(cr, 0.580, 0.639, 0.722, 1.0); /* #94A3B8 */
			cairo_fill(cr);
		}
	}

	double next_x = icons_start_x + 5.0 * item_spacing - 6.0;

	/* 6. Render Dynamic Unpinned Running Apps (if any) */
	if (num_dynamic > 0) {
		/* Vertical separator between Pinned and Dynamic */
		double sep_dyn_x = next_x;
		cairo_new_path(cr);
		cairo_move_to(cr, sep_dyn_x, 10.0);
		cairo_line_to(cr, sep_dyn_x, h - 10.0);
		cairo_set_source_rgba(cr, 0.22, 0.22, 0.30, 0.85);
		cairo_set_line_width(cr, 1.0);
		cairo_stroke(cr);

		double dynamic_start_x = sep_dyn_x + 14.0;
		for (int d = 0; d < num_dynamic; d++) {
			double ix = dynamic_start_x + (double)d * item_spacing;
			dock->dynamic_items[d].x = ix;
			double cx = ix + icon_size / 2.0;

			cairo_surface_t *dyn_icon = diamonds_load_app_icon(
				dock->dynamic_items[d].app_id,
				NULL,
				(int)icon_size);

			if (dyn_icon) {
				cairo_save(cr);
				cairo_set_source_surface(cr, dyn_icon, ix, (h - icon_size) / 2.0);
				cairo_paint(cr);
				cairo_restore(cr);
			} else {
				draw_window_initials_icon(cr, ix, (h - icon_size) / 2.0, icon_size,
					dock->dynamic_items[d].title[0] ? dock->dynamic_items[d].title : dock->dynamic_items[d].app_id);
			}

			/* Indicator */
			if (dock->dynamic_items[d].is_focused) {
				double bar_w = 16.0;
				double bar_h = 3.0;
				double bar_x = cx - bar_w / 2.0;
				double bar_y = h - 5.5;
				diamonds_draw_rounded_rect(cr, bar_x, bar_y, bar_w, bar_h, 1.5);
				cairo_set_source_rgba(cr, 0.2196, 0.7412, 0.9725, 1.0); /* #38BDF8 */
				cairo_fill(cr);
			} else {
				double dot_y = h - 4.5;
				cairo_new_path(cr);
				cairo_arc(cr, cx, dot_y, 2.0, 0.0, 2.0 * M_PI);
				cairo_set_source_rgba(cr, 0.90, 0.92, 0.96, 0.85); /* Light dot */
				cairo_fill(cr);
			}
		}
		next_x = dynamic_start_x + (double)num_dynamic * item_spacing - 6.0;
	}

	/* 7. Separator & Trash Can Icon */
	double sep2_x = next_x;
	cairo_new_path(cr);
	cairo_move_to(cr, sep2_x, 10.0);
	cairo_line_to(cr, sep2_x, h - 10.0);
	cairo_set_source_rgba(cr, 0.22, 0.22, 0.30, 0.85);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	double trash_x = sep2_x + 14.0;
	dock->trash_x = trash_x;

	cairo_surface_t *trash_icon = diamonds_load_system_icon("trash", (int)icon_size);
	if (trash_icon) {
		cairo_save(cr);
		cairo_set_source_surface(cr, trash_icon, trash_x, (h - icon_size) / 2.0);
		cairo_paint(cr);
		cairo_restore(cr);
	} else {
		diamonds_draw_vector_trash(cr, trash_x + icon_size / 2.0, h / 2.0, icon_size);
	}

	cairo_destroy(cr);

	/* Damage tracking: only damage the dock region in the scene graph */
	pixman_region32_t damage;
	pixman_region32_init_rect(&damage, 0, 0, (uint32_t)dock->width, (uint32_t)dock->height);
	wlr_scene_buffer_set_buffer_with_damage(dock->scene_buffer, &dock->buffer->base, &damage);
	pixman_region32_fini(&damage);
}

void diamonds_dock_popup_render(struct diamonds_dock *dock) {
	if (!dock || !dock->popup.buffer || !dock->popup.buffer->cairo_surface) {
		return;
	}

	cairo_t *cr = cairo_create(dock->popup.buffer->cairo_surface);
	if (!cr) return;

	double w = dock->popup.width;
	double h = dock->popup.height;

	/* 1. Clear background */
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	/* 2. Mica Card background (#161622 alpha 0.98, rounded 8px) */
	diamonds_draw_rounded_rect(cr, 0.5, 0.5, w - 1.0, h - 1.0, 8.0);
	cairo_set_source_rgba(cr, 0.086, 0.086, 0.133, 0.98);
	cairo_fill_preserve(cr);

	/* Border: 1px #38BDF8 with 35% opacity */
	cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.35);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* 3. Item 1: Đóng ứng dụng (Close) */
	cairo_new_path(cr);
	cairo_move_to(cr, 16.0, 13.0);
	cairo_line_to(cr, 24.0, 21.0);
	cairo_move_to(cr, 24.0, 13.0);
	cairo_line_to(cr, 16.0, 21.0);
	cairo_set_source_rgba(cr, 0.94, 0.30, 0.30, 1.0); /* Red #EF4444 */
	cairo_set_line_width(cr, 1.8);
	cairo_stroke(cr);

	PangoLayout *layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " 9.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, "Đóng ứng dụng", -1);
		cairo_move_to(cr, 34.0, 9.0);
		cairo_set_source_rgba(cr, 0.94, 0.96, 0.98, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* Subtle separator line between items */
	cairo_new_path(cr);
	cairo_move_to(cr, 8.0, 36.5);
	cairo_line_to(cr, w - 8.0, 36.5);
	cairo_set_source_rgba(cr, 0.22, 0.22, 0.32, 0.8);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* 4. Item 2: Buộc dừng (Kill PID) */
	cairo_new_path(cr);
	cairo_move_to(cr, 21.0, 42.0);
	cairo_line_to(cr, 16.0, 52.0);
	cairo_line_to(cr, 20.0, 52.0);
	cairo_line_to(cr, 19.0, 62.0);
	cairo_line_to(cr, 24.0, 50.0);
	cairo_line_to(cr, 20.0, 50.0);
	cairo_close_path(cr);
	cairo_set_source_rgba(cr, 0.96, 0.62, 0.10, 1.0); /* Amber #F59E0B */
	cairo_fill(cr);

	layout = pango_cairo_create_layout(cr);
	if (layout) {
		char kill_str[64];
		if (dock->popup.target_pid > 0) {
			snprintf(kill_str, sizeof(kill_str), "Buộc dừng (%d)", (int)dock->popup.target_pid);
		} else {
			snprintf(kill_str, sizeof(kill_str), "Buộc dừng (Kill)");
		}
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " 9.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, kill_str, -1);
		cairo_move_to(cr, 34.0, 45.0);
		cairo_set_source_rgba(cr, 0.94, 0.96, 0.98, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	cairo_destroy(cr);

	pixman_region32_t damage;
	pixman_region32_init_rect(&damage, 0, 0, (uint32_t)dock->popup.width, (uint32_t)dock->popup.height);
	wlr_scene_buffer_set_buffer_with_damage(dock->popup.scene_buffer, &dock->popup.buffer->base, &damage);
	pixman_region32_fini(&damage);
}

void diamonds_dock_popup_show(struct diamonds_dock *dock, int32_t x, int32_t y,
		struct diamonds_toplevel *toplevel, struct diamonds_xwayland_surface *xsurface,
		pid_t pid, const char *name) {
	if (!dock || !dock->popup.scene_buffer) return;

	dock->popup.target_toplevel = toplevel;
#ifdef DIAMONDS_ENABLE_XWAYLAND
	dock->popup.target_xsurface = xsurface;
#else
	(void)xsurface;
#endif
	dock->popup.target_pid = pid;
	if (name) {
		snprintf(dock->popup.target_name, sizeof(dock->popup.target_name), "%s", name);
	} else {
		dock->popup.target_name[0] = '\0';
	}

	dock->popup.x = x;
	dock->popup.y = y;
	diamonds_dock_popup_render(dock);

	wlr_scene_node_set_position(&dock->popup.scene_buffer->node, dock->popup.x, dock->popup.y);
	wlr_scene_node_set_enabled(&dock->popup.scene_buffer->node, true);
	wlr_scene_node_raise_to_top(&dock->popup.scene_buffer->node);
	dock->popup.visible = true;
}

void diamonds_dock_popup_hide(struct diamonds_dock *dock) {
	if (!dock || !dock->popup.scene_buffer) return;
	if (dock->popup.visible) {
		wlr_scene_node_set_enabled(&dock->popup.scene_buffer->node, false);
		dock->popup.visible = false;
		dock->popup.target_toplevel = NULL;
#ifdef DIAMONDS_ENABLE_XWAYLAND
		dock->popup.target_xsurface = NULL;
#endif
		dock->popup.target_pid = 0;
	}
}

bool diamonds_dock_handle_click(struct diamonds_dock *dock, double lx, double ly, uint32_t button, bool *out_start_menu_toggled) {
	if (out_start_menu_toggled) {
		*out_start_menu_toggled = false;
	}
	if (!dock || !dock->server) {
		return false;
	}

	/* 1. Handle Click on Popup Context Menu (if visible) */
	if (dock->popup.visible) {
		if (lx >= dock->popup.x && lx <= dock->popup.x + dock->popup.width &&
		    ly >= dock->popup.y && ly <= dock->popup.y + dock->popup.height) {
			if (button == BTN_LEFT) {
				double py = ly - dock->popup.y;
				if (py < 36.0) {
					/* "Đóng ứng dụng" */
					diamonds_sound_play(DIAMONDS_SOUND_CLICK);
					if (dock->popup.target_toplevel && dock->popup.target_toplevel->xdg_toplevel) {
						wlr_xdg_toplevel_send_close(dock->popup.target_toplevel->xdg_toplevel);
						if (dock->server && dock->server->wl_display) {
							wl_display_flush_clients(dock->server->wl_display);
						}
					}
#ifdef DIAMONDS_ENABLE_XWAYLAND
					if (dock->popup.target_xsurface && dock->popup.target_xsurface->xsurface) {
						wlr_xwayland_surface_close(dock->popup.target_xsurface->xsurface);
					}
#endif
				} else {
					/* "Buộc dừng (Kill PID)" */
					diamonds_sound_play(DIAMONDS_SOUND_CLICK);
					if (dock->popup.target_pid > 1) {
						kill(dock->popup.target_pid, SIGKILL);
					} else if (dock->popup.target_toplevel && dock->popup.target_toplevel->xdg_toplevel) {
						struct wl_resource *res = dock->popup.target_toplevel->xdg_toplevel->resource ?
							dock->popup.target_toplevel->xdg_toplevel->resource :
							(dock->popup.target_toplevel->xdg_toplevel->base ? dock->popup.target_toplevel->xdg_toplevel->base->resource : NULL);
						if (res) {
							struct wl_client *client = wl_resource_get_client(res);
							if (client) {
								wl_client_destroy(client);
							}
						}
					}
				}
				diamonds_dock_popup_hide(dock);
				diamonds_update_dock_position(dock->server);
				return true;
			}
		} else {
			diamonds_dock_popup_hide(dock);
		}
	}

	/* 2. Check if click falls within dock bar */
	if (lx < dock->x || lx > dock->x + dock->width ||
	    ly < dock->y || ly > dock->y + dock->height) {
		return false;
	}

	double rel_x = lx - dock->x;
	double icon_size = 24.0;

	/* Start button click */
	if (rel_x >= dock->content_start_x - 6.0 && rel_x <= dock->content_start_x + 28.0) {
		if (button == BTN_LEFT && out_start_menu_toggled) {
			*out_start_menu_toggled = true;
		}
		return true;
	}

	/* 3. Check 5 Pinned App Icons */
	const char *app_names[] = { "Terminal", "Files", "Thorium", "VLC", "Settings" };
	const char *kw1_list[]  = { "term", "diamonds-files", "thorium", "vlc", "diamonds-settings" };
	const char *kw2_list[]  = { "bash", "nautilus", "chromium", NULL, "settings" };

	for (size_t i = 0; i < 5; i++) {
		double ix = dock->pinned_item_x[i];
		if (rel_x >= ix - 4.0 && rel_x <= ix + icon_size + 4.0) {
			/* Find running window matching this pinned slot */
			struct diamonds_toplevel *matched_top = NULL;
			struct diamonds_xwayland_surface *matched_xsurf = NULL;
			pid_t pid = 0;

			/* Check Wayland */
			struct diamonds_toplevel *top;
			wl_list_for_each(top, &dock->server->toplevels, link) {
				if (!top->xdg_toplevel) continue;
				const char *app_id = top->xdg_toplevel->app_id;
				const char *title = top->xdg_toplevel->title;
				bool m = false;
				if (kw1_list[i] && ((app_id && strcasestr(app_id, kw1_list[i])) || (title && strcasestr(title, kw1_list[i])))) m = true;
				if (kw2_list[i] && ((app_id && strcasestr(app_id, kw2_list[i])) || (title && strcasestr(title, kw2_list[i])))) m = true;
				if (m) {
					matched_top = top;
					struct wl_resource *res = top->xdg_toplevel->resource ?
						top->xdg_toplevel->resource :
						(top->xdg_toplevel->base ? top->xdg_toplevel->base->resource : NULL);
					if (res) {
						struct wl_client *client = wl_resource_get_client(res);
						if (client) wl_client_get_credentials(client, &pid, NULL, NULL);
					}
					break;
				}
			}

#ifdef DIAMONDS_ENABLE_XWAYLAND
			if (!matched_top) {
				struct diamonds_xwayland_surface *xsurf;
				wl_list_for_each(xsurf, &dock->server->xwayland_surfaces, link) {
					if (!xsurf->xsurface || !xsurf->xsurface->surface || xsurf->xsurface->override_redirect) continue;
					const char *class = xsurf->xsurface->class;
					const char *instance = xsurf->xsurface->instance;
					const char *title = xsurf->xsurface->title;
					bool m = false;
					if (kw1_list[i] && ((class && strcasestr(class, kw1_list[i])) || (instance && strcasestr(instance, kw1_list[i])) || (title && strcasestr(title, kw1_list[i])))) m = true;
					if (kw2_list[i] && ((class && strcasestr(class, kw2_list[i])) || (instance && strcasestr(instance, kw2_list[i])) || (title && strcasestr(title, kw2_list[i])))) m = true;
					if (m) {
						matched_xsurf = xsurf;
						pid = xsurf->xsurface->pid;
						break;
					}
				}
			}
#endif

			if (button == BTN_RIGHT) {
				diamonds_sound_play(DIAMONDS_SOUND_CLICK);
				int32_t pop_x = (int32_t)(dock->x + ix + icon_size / 2.0 - dock->popup.width / 2.0);
				int32_t pop_y = (int32_t)(dock->y - dock->popup.height - 6);
				if (pop_x < 4) pop_x = 4;
				diamonds_dock_popup_show(dock, pop_x, pop_y, matched_top, matched_xsurf, pid, app_names[i]);
				return true;
			} else if (button == BTN_LEFT) {
				diamonds_sound_play(DIAMONDS_SOUND_CLICK);
				if (matched_top) {
					bool visible = matched_top->scene_tree && matched_top->scene_tree->node.enabled;
					if (visible) {
						if (dock->server->focused_toplevel == matched_top) {
							diamonds_window_minimize(matched_top, NULL);
						} else {
							wlr_scene_node_raise_to_top(&matched_top->scene_tree->node);
							diamonds_focus_toplevel(dock->server, matched_top);
						}
					} else {
						if (matched_top->scene_tree) {
							wlr_scene_node_set_enabled(&matched_top->scene_tree->node, true);
							wlr_scene_node_raise_to_top(&matched_top->scene_tree->node);
						}
						diamonds_focus_toplevel(dock->server, matched_top);
					}
					diamonds_update_dock_position(dock->server);
					return true;
				}
#ifdef DIAMONDS_ENABLE_XWAYLAND
				else if (matched_xsurf) {
					bool visible = matched_xsurf->scene_tree ? matched_xsurf->scene_tree->node.enabled : false;
					if (visible) {
						diamonds_window_minimize(NULL, matched_xsurf);
					} else {
						if (matched_xsurf->scene_tree) {
							wlr_scene_node_set_enabled(&matched_xsurf->scene_tree->node, true);
							wlr_scene_node_raise_to_top(&matched_xsurf->scene_tree->node);
						}
						wlr_xwayland_surface_set_minimized(matched_xsurf->xsurface, false);
						wlr_xwayland_surface_activate(matched_xsurf->xsurface, true);
					}
					diamonds_update_dock_position(dock->server);
					return true;
				}
#endif
				else {
					/* No window running: Spawn application */
					switch (i) {
					case 0:
						launch_terminal();
						break;
					case 1:
						launch_app("./build/diamonds-files || diamonds-files");
						break;
					case 2:
						launch_app("thorium");
						break;
					case 3:
						launch_app("vlc || ./build/diamonds-music || diamonds-music");
						break;
					case 4:
						launch_app("./build/diamonds-settings || diamonds-settings");
						break;
					}
					return true;
				}
			}
		}
	}

	/* 4. Check Dynamic App Icons */
	if (dock->dynamic_item_count > 0) {
		for (int d = 0; d < dock->dynamic_item_count; d++) {
			struct diamonds_dock_dynamic_item *item = &dock->dynamic_items[d];
			double ix = item->x;
			if (rel_x >= ix - 4.0 && rel_x <= ix + icon_size + 4.0) {
				diamonds_sound_play(DIAMONDS_SOUND_CLICK);

				if (button == BTN_RIGHT) {
					int32_t pop_x = (int32_t)(dock->x + ix + icon_size / 2.0 - dock->popup.width / 2.0);
					int32_t pop_y = (int32_t)(dock->y - dock->popup.height - 6);
					if (pop_x < 4) pop_x = 4;
					diamonds_dock_popup_show(dock, pop_x, pop_y,
						item->toplevel,
#ifdef DIAMONDS_ENABLE_XWAYLAND
						item->xsurface,
#else
						NULL,
#endif
						item->pid,
						item->title[0] ? item->title : item->app_id);
					return true;
				} else if (button == BTN_LEFT) {
					if (item->toplevel) {
						struct diamonds_toplevel *toplevel = item->toplevel;
						bool visible = toplevel->scene_tree && toplevel->scene_tree->node.enabled;
						if (visible) {
							if (dock->server->focused_toplevel == toplevel) {
								diamonds_window_minimize(toplevel, NULL);
							} else {
								wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
								diamonds_focus_toplevel(dock->server, toplevel);
							}
						} else {
							if (toplevel->scene_tree) {
								wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
								wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
							}
							diamonds_focus_toplevel(dock->server, toplevel);
						}
						diamonds_update_dock_position(dock->server);
						return true;
					}
#ifdef DIAMONDS_ENABLE_XWAYLAND
					else if (item->xsurface) {
						struct diamonds_xwayland_surface *xsurface = item->xsurface;
						bool visible = xsurface->scene_tree ? xsurface->scene_tree->node.enabled : false;
						if (visible) {
							diamonds_window_minimize(NULL, xsurface);
						} else {
							if (xsurface->scene_tree) {
								wlr_scene_node_set_enabled(&xsurface->scene_tree->node, true);
								wlr_scene_node_raise_to_top(&xsurface->scene_tree->node);
							}
							wlr_xwayland_surface_set_minimized(xsurface->xsurface, false);
							wlr_xwayland_surface_activate(xsurface->xsurface, true);
						}
						diamonds_update_dock_position(dock->server);
						return true;
					}
#endif
				}
			}
		}
	}

	/* 5. Trash icon click */
	if (rel_x >= dock->trash_x - 4.0 && rel_x <= dock->trash_x + icon_size + 4.0) {
		diamonds_sound_play(DIAMONDS_SOUND_CLICK);
		launch_app("xdg-open trash:// || nautilus trash://");
		return true;
	}

	return true;
}
