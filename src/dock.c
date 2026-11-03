#define _GNU_SOURCE
#include "dock.h"
#include "diamonds.h"
#include "vector_icons.h"
#include "icon_loader.h"
#include "sound.h"

#include <stdlib.h>
#include <string.h>
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

static void find_dock_app_window(struct diamonds_server *server,
		const char *kw1, const char *kw2,
		struct diamonds_toplevel **out_toplevel,
		struct diamonds_xwayland_surface **out_xsurface,
		pid_t *out_pid) {
	*out_toplevel = NULL;
	if (out_xsurface) *out_xsurface = NULL;
	*out_pid = 0;
	if (!server) return;

	/* 1. Check toplevels (Wayland) */
	struct diamonds_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (!toplevel->xdg_toplevel) continue;
		const char *app_id = toplevel->xdg_toplevel->app_id;
		const char *title = toplevel->xdg_toplevel->title;
		bool match = false;
		if (kw1 && ((app_id && strcasestr(app_id, kw1)) || (title && strcasestr(title, kw1)))) match = true;
		if (kw2 && ((app_id && strcasestr(app_id, kw2)) || (title && strcasestr(title, kw2)))) match = true;
		if (match) {
			*out_toplevel = toplevel;
			struct wl_resource *res = toplevel->xdg_toplevel->resource ?
				toplevel->xdg_toplevel->resource :
				(toplevel->xdg_toplevel->base ? toplevel->xdg_toplevel->base->resource : NULL);
			if (res) {
				struct wl_client *client = wl_resource_get_client(res);
				if (client) {
					wl_client_get_credentials(client, out_pid, NULL, NULL);
				}
			}
			return;
		}
	}

#ifdef DIAMONDS_ENABLE_XWAYLAND
	/* 2. Check XWayland surfaces (X11) */
	struct diamonds_xwayland_surface *xsurface;
	wl_list_for_each(xsurface, &server->xwayland_surfaces, link) {
		if (!xsurface->xsurface) continue;
		const char *class = xsurface->xsurface->class;
		const char *instance = xsurface->xsurface->instance;
		const char *title = xsurface->xsurface->title;
		bool match = false;
		if (kw1 && ((class && strcasestr(class, kw1)) || (instance && strcasestr(instance, kw1)) || (title && strcasestr(title, kw1)))) match = true;
		if (kw2 && ((class && strcasestr(class, kw2)) || (instance && strcasestr(instance, kw2)) || (title && strcasestr(title, kw2)))) match = true;
		if (match) {
			if (out_xsurface) *out_xsurface = xsurface;
			*out_pid = xsurface->xsurface->pid;
			return;
		}
	}
#endif
}

static void check_dock_app_state(struct diamonds_server *server,
		const char *kw1, const char *kw2,
		bool *is_running, bool *is_focused) {
	*is_running = false;
	*is_focused = false;
	if (!server) return;

	struct diamonds_toplevel *toplevel = NULL;
	struct diamonds_xwayland_surface *xsurface = NULL;
	pid_t pid = 0;
	find_dock_app_window(server, kw1, kw2, &toplevel, &xsurface, &pid);

	if (toplevel) {
		*is_running = true;
		if (server->focused_toplevel == toplevel) {
			*is_focused = true;
		}
	} else if (xsurface) {
		*is_running = true;
	}
}

struct diamonds_dock *diamonds_dock_create(struct diamonds_server *server) {
	struct diamonds_dock *dock = calloc(1, sizeof(*dock));
	if (!dock) {
		return NULL;
	}

	dock->server = server;
	dock->width = 460;
	dock->height = 48;
	dock->margin_bottom = 8;
	dock->corner_radius = 10;
	dock->is_full_width = false;

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
	dock->popup.width = 180;
	dock->popup.height = 72;
	dock->popup.visible = false;
	dock->popup.buffer = diamonds_cairo_buffer_create(dock->popup.width, dock->popup.height);
	if (dock->popup.buffer) {
		dock->popup.scene_buffer = wlr_scene_buffer_create(server->tree_overlay, &dock->popup.buffer->base);
		if (dock->popup.scene_buffer) {
			wlr_scene_node_set_enabled(&dock->popup.scene_buffer->node, false);
		}
	}

	diamonds_dock_render(dock);
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
	if (!dock || !dock->scene_buffer || output_width <= 0 || output_height <= 0) {
		return;
	}

	bool is_docked = false;

	/* Check toplevels: if any window is maximized or touches the bottom area */
	struct diamonds_toplevel *toplevel;
	wl_list_for_each(toplevel, &dock->server->toplevels, link) {
		if (!toplevel->scene_tree || !toplevel->scene_tree->node.enabled) continue;
		if (toplevel->is_maximized) {
			is_docked = true;
			break;
		}
		int wy = toplevel->scene_tree->node.y;
		int wh = toplevel->xdg_toplevel ? toplevel->xdg_toplevel->current.height : 400;
		if (toplevel->titlebar_scene_buffer) {
			wh += 32; /* TITLEBAR_HEIGHT */
		}
		if (wy + wh >= output_height - 56) {
			is_docked = true;
			break;
		}
	}

#ifdef DIAMONDS_ENABLE_XWAYLAND
	if (!is_docked) {
		struct diamonds_xwayland_surface *xsurface;
		wl_list_for_each(xsurface, &dock->server->xwayland_surfaces, link) {
			if (!xsurface->scene_tree || !xsurface->scene_tree->node.enabled) continue;
			if (xsurface->is_maximized || (xsurface->xsurface && (xsurface->xsurface->maximized_vert || xsurface->xsurface->maximized_horz))) {
				is_docked = true;
				break;
			}
			int wy = xsurface->xsurface ? xsurface->xsurface->y : 0;
			int wh = xsurface->xsurface ? xsurface->xsurface->height : 400;
			if (wy + wh >= output_height - 56) {
				is_docked = true;
				break;
			}
		}
	}
#endif

	int32_t new_w, new_h, new_x, new_y, new_r, new_mb;
	if (is_docked) {
		/* Full-width flat Windows-style Taskbar at screen bottom */
		new_w = output_width;
		new_h = 44;
		new_x = 0;
		new_y = output_height - 44;
		new_r = 0;
		new_mb = 0;
		dock->is_full_width = true;
	} else {
		/* Floating Dock */
		new_w = 460;
		new_h = 48;
		new_mb = 8;
		new_r = 10;
		new_x = (output_width - new_w) / 2;
		new_y = output_height - new_h - new_mb;
		dock->is_full_width = false;
	}

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
	if (!dock || !dock->buffer || !dock->buffer->cairo_surface) {
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

	/* 3. Center Content (Start + 5 Icons + Trash) */
	/* Total content width:
	 * Start (24) + sep_gap (14) + sep (1) + gap (14) + 5 icons (5 * 24 + 4 * 22 = 208)
	 * + gap (14) + sep (1) + gap (14) + Trash (24) = ~314px
	 */
	double content_w = 314.0;
	double content_start_x = (w - content_w) / 2.0;
	if (content_start_x < 8.0) content_start_x = 8.0;

	/* Start Button (Diamond logo rotated 90 deg) */
	double start_btn_cx = content_start_x + 12.0;
	diamonds_draw_vector_diamond_logo(cr, start_btn_cx, h / 2.0, 20.0);

	/* Separator after Start */
	double sep1_x = content_start_x + 28.5;
	cairo_new_path(cr);
	cairo_move_to(cr, sep1_x, 10.0);
	cairo_line_to(cr, sep1_x, h - 10.0);
	cairo_set_source_rgba(cr, 0.22, 0.22, 0.30, 0.85);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* 4. App Icons & Indicators */
	bool term_running = false, term_focused = false;
	bool files_running = false, files_focused = false;
	bool browser_running = false, browser_focused = false;
	bool vlc_running = false, vlc_focused = false;
	bool settings_running = false, settings_focused = false;

	check_dock_app_state(dock->server, "term", "bash", &term_running, &term_focused);
	check_dock_app_state(dock->server, "nautilus", "file", &files_running, &files_focused);
	check_dock_app_state(dock->server, "thorium", "chromium", &browser_running, &browser_focused);
	check_dock_app_state(dock->server, "vlc", NULL, &vlc_running, &vlc_focused);
	check_dock_app_state(dock->server, "settings", "control", &settings_running, &settings_focused);

	struct {
		bool is_running;
		bool is_focused;
	} item_states[5] = {
		{ term_running, term_focused },
		{ files_running, files_focused },
		{ browser_running, browser_focused },
		{ vlc_running, vlc_focused },
		{ settings_running, settings_focused },
	};

	double icons_start_x = sep1_x + 14.0;
	double item_spacing = 46.0;
	double icon_size = 24.0;
	const char *app_keys[5] = { "terminal", "files", "browser", "vlc", "settings" };

	for (size_t i = 0; i < 5; i++) {
		double ix = icons_start_x + (double)i * item_spacing;
		double cx = ix + icon_size / 2.0;
		double cy = (h - icon_size) / 2.0 + icon_size / 2.0;

		cairo_surface_t *sys_icon = diamonds_load_system_icon(app_keys[i], (int)icon_size);
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

		/* Indicators at the bottom:
		 * Win11 style: 16x3px #38BDF8 bar when focused
		 * macOS style: 4px circular dot #94A3B8 when running in background
		 */
		double indicator_cx = cx;
		if (item_states[i].is_focused) {
			double bar_w = 16.0;
			double bar_h = 3.0;
			double bar_x = indicator_cx - bar_w / 2.0;
			double bar_y = h - 5.5;
			diamonds_draw_rounded_rect(cr, bar_x, bar_y, bar_w, bar_h, 1.5);
			cairo_set_source_rgba(cr, 0.2196, 0.7412, 0.9725, 1.0); /* #38BDF8 */
			cairo_fill(cr);
		} else if (item_states[i].is_running) {
			double dot_y = h - 4.5;
			cairo_new_path(cr);
			cairo_arc(cr, indicator_cx, dot_y, 2.0, 0.0, 2.0 * M_PI);
			cairo_set_source_rgba(cr, 0.580, 0.639, 0.722, 1.0); /* #94A3B8 */
			cairo_fill(cr);
		}
	}

	/* 5. Separator & Trash Can Icon */
	double sep2_x = icons_start_x + 5.0 * item_spacing - 6.0;
	cairo_new_path(cr);
	cairo_move_to(cr, sep2_x, 10.0);
	cairo_line_to(cr, sep2_x, h - 10.0);
	cairo_set_source_rgba(cr, 0.22, 0.22, 0.30, 0.85);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	double trash_x = sep2_x + 18.0;
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
	/* Red close icon (✕) */
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
	/* Amber lightning zap icon (⚡) */
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
	if (!dock) {
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
			/* Clicked outside popup: dismiss it */
			diamonds_dock_popup_hide(dock);
		}
	}

	/* 2. Check if click falls within dock bar */
	if (lx < dock->x || lx > dock->x + dock->width ||
	    ly < dock->y || ly > dock->y + dock->height) {
		return false;
	}

	double rel_x = lx - dock->x;
	double content_w = 314.0;
	double content_start_x = (dock->width - content_w) / 2.0;
	if (content_start_x < 8.0) content_start_x = 8.0;

	/* Start button click [content_start_x - 6 .. content_start_x + 28] */
	if (rel_x >= content_start_x - 6.0 && rel_x <= content_start_x + 28.0) {
		if (button == BTN_LEFT && out_start_menu_toggled) {
			*out_start_menu_toggled = true;
		}
		return true;
	}

	/* App icon coordinates */
	double sep1_x = content_start_x + 28.5;
	double icons_start_x = sep1_x + 14.0;
	double item_spacing = 46.0;
	double icon_size = 24.0;

	const char *app_names[] = { "Terminal", "Files", "Thorium", "VLC", "Settings" };
	const char *kw1_list[]  = { "term", "nautilus", "thorium", "vlc", "settings" };
	const char *kw2_list[]  = { "bash", "file", "chromium", NULL, "control" };

	for (size_t i = 0; i < 5; i++) {
		double ix = icons_start_x + (double)i * item_spacing;
		if (rel_x >= ix - 4.0 && rel_x <= ix + icon_size + 4.0) {
			struct diamonds_toplevel *toplevel = NULL;
			struct diamonds_xwayland_surface *xsurface = NULL;
			pid_t pid = 0;
			find_dock_app_window(dock->server, kw1_list[i], kw2_list[i], &toplevel, &xsurface, &pid);

			if (button == BTN_RIGHT) {
				/* RIGHT CLICK: Show Context Menu */
				diamonds_sound_play(DIAMONDS_SOUND_CLICK);
				int32_t pop_x = (int32_t)(dock->x + ix + icon_size / 2.0 - dock->popup.width / 2.0);
				int32_t pop_y = (int32_t)(dock->y - dock->popup.height - 6);
				if (pop_x < 4) pop_x = 4;
				diamonds_dock_popup_show(dock, pop_x, pop_y, toplevel, xsurface, pid, app_names[i]);
				return true;
			} else if (button == BTN_LEFT) {
				/* LEFT CLICK: Focus / Minimize / Restore or Launch */
				diamonds_sound_play(DIAMONDS_SOUND_CLICK);

				if (toplevel) {
					bool visible = toplevel->scene_tree->node.enabled;
					if (visible) {
						if (dock->server->focused_toplevel == toplevel) {
							/* Currently focused -> minimize / hide */
							wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
							dock->server->focused_toplevel = NULL;
						} else {
							/* Not focused -> bring to top and focus */
							wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
							diamonds_focus_toplevel(dock->server, toplevel);
						}
					} else {
						/* Was minimized -> restore, raise to top and focus */
						wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
						wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
						diamonds_focus_toplevel(dock->server, toplevel);
					}
					diamonds_update_dock_position(dock->server);
					return true;
				}
#ifdef DIAMONDS_ENABLE_XWAYLAND
				else if (xsurface) {
					bool visible = xsurface->scene_tree ? xsurface->scene_tree->node.enabled : false;
					if (visible) {
						/* Minimize */
						if (xsurface->scene_tree) {
							wlr_scene_node_set_enabled(&xsurface->scene_tree->node, false);
						}
						wlr_xwayland_surface_set_minimized(xsurface->xsurface, true);
					} else {
						/* Restore */
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
				else {
					/* No window running: Spawn application */
					switch (i) {
					case 0:
						launch_terminal();
						break;
					case 1:
						launch_files();
						break;
					case 2:
						launch_app("thorium");
						break;
					case 3:
						launch_app("vlc");
						break;
					case 4:
						launch_app("systemsettings5 || systemsettings || gnome-control-center");
						break;
					}
					return true;
				}
			}
		}
	}

	/* Trash icon click */
	double sep2_x = icons_start_x + 5.0 * item_spacing - 6.0;
	double trash_x = sep2_x + 18.0;
	if (rel_x >= trash_x - 4.0 && rel_x <= trash_x + icon_size + 4.0) {
		diamonds_sound_play(DIAMONDS_SOUND_CLICK);
		launch_app("xdg-open trash:// || nautilus trash://");
		return true;
	}

	return true;
}
