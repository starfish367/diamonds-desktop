#define _GNU_SOURCE
#include "diamonds.h"
#include "dock.h"
#include "topbar.h"
#include "startmenu.h"
#include "sound.h"
#include "snap.h"
#include "titlebar.h"
#include "wallpaper.h"
#include "calendar.h"
#include "control_center.h"
#include "icon_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>
#include <wlr/types/wlr_screencopy_v1.h>

#define DIAMONDS_VERSION "0.1.0"

static void handle_output_frame(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_output *output = wl_container_of(listener, output, frame);
	struct diamonds_server *server = output->server;

	struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
		server->scene, output->wlr_output);
	if (scene_output) {
		wlr_scene_output_commit(scene_output, NULL);
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

static void handle_output_request_state(struct wl_listener *listener, void *data) {
	struct diamonds_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);

	struct diamonds_server *server = output->server;
	if (server) {
		int w = output->wlr_output->width;
		int h = output->wlr_output->height;
		if (server->background_rect) {
			wlr_scene_rect_set_size(server->background_rect, w, h);
		}
		if (server->wallpaper) {
			diamonds_wallpaper_reposition(server->wallpaper, w, h);
		}
		if (server->topbar) {
			diamonds_topbar_reposition(server->topbar, w, h);
		}
		if (server->calendar) {
			diamonds_calendar_reposition(server->calendar, w, h);
		}
		if (server->control_center) {
			diamonds_control_center_reposition(server->control_center, w, h);
		}
		if (server->dock) {
			diamonds_dock_reposition(server->dock, w, h);
			if (server->startmenu) {
				diamonds_startmenu_reposition(server->startmenu, server->dock->x, server->dock->y);
			}
		}
	}
}

static void handle_output_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_output *output = wl_container_of(listener, output, destroy);
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);
}

static void server_new_output(struct wl_listener *listener, void *data) {
	struct diamonds_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	if (!wlr_output_init_render(wlr_output, server->allocator, server->renderer)) {
		wlr_log(WLR_ERROR, "Failed to initialize renderer for output");
		return;
	}

	struct diamonds_output *output = calloc(1, sizeof(*output));
	if (!output) {
		return;
	}

	output->server = server;
	output->wlr_output = wlr_output;

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode) {
		wlr_output_state_set_mode(&state, mode);
	}
	wlr_output_state_set_enabled(&state, true);
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	output->frame.notify = handle_output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	output->request_state.notify = handle_output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);

	output->destroy.notify = handle_output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	wl_list_insert(&server->outputs, &output->link);

	struct wlr_output_layout_output *l_output =
		wlr_output_layout_add_auto(server->output_layout, wlr_output);
	output->scene_output = wlr_scene_output_create(server->scene, wlr_output);

	if (server->background_rect) {
		wlr_scene_rect_set_size(server->background_rect, wlr_output->width, wlr_output->height);
	}
	if (server->wallpaper) {
		diamonds_wallpaper_reposition(server->wallpaper, wlr_output->width, wlr_output->height);
	}
	if (server->topbar) {
		diamonds_topbar_reposition(server->topbar, wlr_output->width, wlr_output->height);
	}
	if (server->calendar) {
		diamonds_calendar_reposition(server->calendar, wlr_output->width, wlr_output->height);
	}
	if (server->control_center) {
		diamonds_control_center_reposition(server->control_center, wlr_output->width, wlr_output->height);
	}
	if (server->dock && l_output) {
		diamonds_dock_reposition(server->dock, wlr_output->width, wlr_output->height);
		if (server->startmenu) {
			diamonds_startmenu_reposition(server->startmenu, server->dock->x, server->dock->y);
		}
	}
}

/* Window management */
void diamonds_focus_toplevel(struct diamonds_server *server, struct diamonds_toplevel *toplevel) {
	if (!toplevel || !toplevel->xdg_toplevel) {
		return;
	}

	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;

	if (prev_surface == surface) {
		return;
	}

	if (prev_surface) {
		struct wlr_xdg_surface *prev_xdg = wlr_xdg_surface_try_from_wlr_surface(prev_surface);
		if (prev_xdg && prev_xdg->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
			wlr_xdg_toplevel_set_activated(prev_xdg->toplevel, false);
		}
	}

	wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	wl_list_remove(&toplevel->link);
	wl_list_insert(&server->toplevels, &toplevel->link);
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard) {
		wlr_seat_keyboard_notify_enter(
			seat, surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}

	server->focused_toplevel = toplevel;
	if (server->dock) {
		diamonds_dock_render(server->dock);
	}
}

void diamonds_server_switch_workspace(struct diamonds_server *server, int new_ws) {
	if (!server || new_ws < 0 || new_ws >= DIAMONDS_NUM_WORKSPACES) {
		return;
	}
	if (server->current_workspace == new_ws) {
		return;
	}

	wlr_scene_node_set_enabled(&server->tree_workspaces[server->current_workspace]->node, false);
	server->current_workspace = new_ws;
	wlr_scene_node_set_enabled(&server->tree_workspaces[server->current_workspace]->node, true);

	if (server->topbar) {
		server->topbar->active_workspace = new_ws + 1;
		diamonds_topbar_render(server->topbar);
	}
	diamonds_update_dock_position(server);
}

void diamonds_update_dock_position(struct diamonds_server *server) {
	if (!server || !server->dock) return;
	int32_t width = 1366, height = 768;
	if (!wl_list_empty(&server->outputs)) {
		struct diamonds_output *out = wl_container_of(server->outputs.next, out, link);
		if (out->wlr_output && out->wlr_output->width > 0) {
			width = out->wlr_output->width;
			height = out->wlr_output->height;
		}
	}
	diamonds_dock_reposition(server->dock, width, height);
	if (server->startmenu) {
		diamonds_startmenu_reposition(server->startmenu, server->dock->x, server->dock->y);
	}
}

void diamonds_window_close(struct diamonds_toplevel *toplevel, struct diamonds_xwayland_surface *xsurface) {
	diamonds_sound_play(DIAMONDS_SOUND_CLICK);
	if (toplevel && toplevel->xdg_toplevel) {
		wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
		if (toplevel->server && toplevel->server->wl_display) {
			wl_display_flush_clients(toplevel->server->wl_display);
		}
	}
#ifdef DIAMONDS_ENABLE_XWAYLAND
	else if (xsurface && xsurface->xsurface) {
		wlr_xwayland_surface_close(xsurface->xsurface);
	}
#else
	(void)xsurface;
#endif
}

void diamonds_window_toggle_maximize(struct diamonds_toplevel *toplevel, struct diamonds_xwayland_surface *xsurface) {
	diamonds_sound_play(DIAMONDS_SOUND_CLICK);
	if (toplevel && toplevel->xdg_toplevel) {
		toplevel->is_maximized = !toplevel->is_maximized;
		wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, toplevel->is_maximized);

		if (toplevel->is_maximized) {
			int32_t screen_w = 1280, screen_h = 720;
			if (toplevel->server && !wl_list_empty(&toplevel->server->outputs)) {
				struct diamonds_output *out = wl_container_of(toplevel->server->outputs.next, out, link);
				if (out->wlr_output && out->wlr_output->width > 0) {
					screen_w = out->wlr_output->width;
					screen_h = out->wlr_output->height;
				}
			}
			wlr_scene_node_set_position(&toplevel->scene_tree->node, 0, 32);
			int32_t target_h = screen_h - 32 - 44;
			if (toplevel->titlebar_scene_buffer) {
				target_h -= TITLEBAR_HEIGHT;
			}
			wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, screen_w, target_h);
		} else {
			wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
		}

		if (toplevel->titlebar_scene_buffer) {
			diamonds_toplevel_update_titlebar(toplevel);
		}
		if (toplevel->server) {
			diamonds_update_dock_position(toplevel->server);
			if (toplevel->server->wl_display) {
				wl_display_flush_clients(toplevel->server->wl_display);
			}
		}
	}
#ifdef DIAMONDS_ENABLE_XWAYLAND
	else if (xsurface && xsurface->xsurface) {
		xsurface->is_maximized = !xsurface->is_maximized;
		wlr_xwayland_surface_set_maximized(xsurface->xsurface, xsurface->is_maximized);
		if (xsurface->is_maximized) {
			int32_t screen_w = 1280, screen_h = 720;
			if (xsurface->server && !wl_list_empty(&xsurface->server->outputs)) {
				struct diamonds_output *out = wl_container_of(xsurface->server->outputs.next, out, link);
				if (out->wlr_output && out->wlr_output->width > 0) {
					screen_w = out->wlr_output->width;
					screen_h = out->wlr_output->height;
				}
			}
			int32_t target_h = screen_h - 32 - 44;
			wlr_xwayland_surface_configure(xsurface->xsurface, 0, 32, screen_w, target_h);
			if (xsurface->scene_tree) {
				wlr_scene_node_set_position(&xsurface->scene_tree->node, 0, 32);
			}
		}
		if (xsurface->server) {
			diamonds_update_dock_position(xsurface->server);
		}
	}
#else
	(void)xsurface;
#endif
}

void diamonds_window_minimize(struct diamonds_toplevel *toplevel, struct diamonds_xwayland_surface *xsurface) {
	diamonds_sound_play(DIAMONDS_SOUND_CLICK);
	if (toplevel && toplevel->scene_tree) {
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
		if (toplevel->server && toplevel->server->focused_toplevel == toplevel) {
			toplevel->server->focused_toplevel = NULL;
		}
		if (toplevel->server) {
			diamonds_update_dock_position(toplevel->server);
		}
	}
#ifdef DIAMONDS_ENABLE_XWAYLAND
	else if (xsurface) {
		if (xsurface->scene_tree) {
			wlr_scene_node_set_enabled(&xsurface->scene_tree->node, false);
		}
		if (xsurface->xsurface) {
			wlr_xwayland_surface_set_minimized(xsurface->xsurface, true);
		}
		if (xsurface->server) {
			diamonds_update_dock_position(xsurface->server);
		}
	}
#else
	(void)xsurface;
#endif
}

static bool toplevel_has_csd(struct diamonds_toplevel *toplevel) {
	if (!toplevel || !toplevel->xdg_toplevel) return false;
	const char *app_id = toplevel->xdg_toplevel->app_id;
	const char *title = toplevel->xdg_toplevel->title;
	if (app_id) {
		if (strcasestr(app_id, "terminator") ||
		    strcasestr(app_id, "nautilus") ||
		    strcasestr(app_id, "org.gnome") ||
		    strcasestr(app_id, "foot") ||
		    strcasestr(app_id, "kitty") ||
		    strcasestr(app_id, "alacritty") ||
		    strcasestr(app_id, "chrome") ||
		    strcasestr(app_id, "thorium") ||
		    strcasestr(app_id, "firefox")) {
			return true;
		}
	}
	if (title) {
		if (strcasestr(title, "terminator") ||
		    strcasestr(title, "nautilus") ||
		    strcasestr(title, "thorium")) {
			return true;
		}
	}
	return false;
}

static void handle_toplevel_map(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_toplevel *toplevel = wl_container_of(listener, toplevel, map);
	if (!toplevel_has_csd(toplevel)) {
		diamonds_toplevel_create_titlebar(toplevel);
	}
	wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
	if (toplevel->scene_tree->node.y < 32) {
		wlr_scene_node_set_position(&toplevel->scene_tree->node, toplevel->scene_tree->node.x, 32);
	}
	diamonds_focus_toplevel(toplevel->server, toplevel);
	diamonds_update_dock_position(toplevel->server);
}

static void handle_toplevel_unmap(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
	wl_list_remove(&toplevel->link);

	if (toplevel->server->focused_toplevel == toplevel) {
		toplevel->server->focused_toplevel = NULL;
		if (!wl_list_empty(&toplevel->server->toplevels)) {
			struct diamonds_toplevel *next = wl_container_of(
				toplevel->server->toplevels.next, next, link);
			diamonds_focus_toplevel(toplevel->server, next);
		}
	}
	diamonds_update_dock_position(toplevel->server);
}

static void handle_toplevel_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_toplevel *toplevel = wl_container_of(listener, toplevel, commit);
	if (toplevel->xdg_toplevel->base->initial_commit) {
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
	}
	diamonds_toplevel_update_titlebar(toplevel);
}

static void handle_toplevel_request_move(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
	struct diamonds_server *server = toplevel->server;
	server->cursor_mode = DIAMONDS_CURSOR_MOVE;
	server->grabbed_toplevel = toplevel;
	server->grabbed_xsurface = NULL;
	server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
	server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
}

static void handle_toplevel_request_resize(struct wl_listener *listener, void *data) {
	struct diamonds_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct diamonds_server *server = toplevel->server;
	server->cursor_mode = DIAMONDS_CURSOR_RESIZE;
	server->grabbed_toplevel = toplevel;
	server->grabbed_xsurface = NULL;
	server->grab_x = server->cursor->x;
	server->grab_y = server->cursor->y;
	server->grab_geobox.x = toplevel->scene_tree->node.x;
	server->grab_geobox.y = toplevel->scene_tree->node.y;
	server->grab_geobox.width = toplevel->xdg_toplevel ? toplevel->xdg_toplevel->current.width : 640;
	server->grab_geobox.height = toplevel->xdg_toplevel ? toplevel->xdg_toplevel->current.height : 480;
	server->resize_edges = event->edges;
}

static void handle_toplevel_request_maximize(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_toplevel *toplevel = wl_container_of(listener, toplevel, request_maximize);
	diamonds_window_toggle_maximize(toplevel, NULL);
}

static void handle_toplevel_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);
	diamonds_toplevel_destroy_titlebar(toplevel);
	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->destroy.link);
	free(toplevel);
}

static void server_new_toplevel_decoration(struct wl_listener *listener, void *data) {
	(void)listener;
	struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
	wlr_xdg_toplevel_decoration_v1_set_mode(
		decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void server_new_xdg_surface(struct wl_listener *listener, void *data) {
	struct diamonds_server *server = wl_container_of(listener, server, new_xdg_surface);
	struct wlr_xdg_surface *xdg_surface = data;

	if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		return;
	}

	struct diamonds_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	if (!toplevel) {
		return;
	}

	toplevel->server = server;
	toplevel->xdg_toplevel = xdg_surface->toplevel;
	toplevel->workspace = server->current_workspace;
	toplevel->scene_tree = wlr_scene_tree_create(server->tree_workspaces[server->current_workspace]);
	struct wlr_scene_tree *xdg_tree = wlr_scene_xdg_surface_create(toplevel->scene_tree, xdg_surface);
	if (!toplevel_has_csd(toplevel)) {
		wlr_scene_node_set_position(&xdg_tree->node, 0, TITLEBAR_HEIGHT);
	} else {
		wlr_scene_node_set_position(&xdg_tree->node, 0, 0);
	}
	toplevel->scene_tree->node.data = toplevel;
	xdg_surface->data = toplevel->scene_tree;

	toplevel->map.notify = handle_toplevel_map;
	wl_signal_add(&xdg_surface->surface->events.map, &toplevel->map);

	toplevel->unmap.notify = handle_toplevel_unmap;
	wl_signal_add(&xdg_surface->surface->events.unmap, &toplevel->unmap);

	toplevel->commit.notify = handle_toplevel_commit;
	wl_signal_add(&xdg_surface->surface->events.commit, &toplevel->commit);

	toplevel->request_move.notify = handle_toplevel_request_move;
	wl_signal_add(&xdg_surface->toplevel->events.request_move, &toplevel->request_move);

	toplevel->request_resize.notify = handle_toplevel_request_resize;
	wl_signal_add(&xdg_surface->toplevel->events.request_resize, &toplevel->request_resize);

	toplevel->request_maximize.notify = handle_toplevel_request_maximize;
	wl_signal_add(&xdg_surface->toplevel->events.request_maximize, &toplevel->request_maximize);

	toplevel->destroy.notify = handle_toplevel_destroy;
	wl_signal_add(&xdg_surface->events.destroy, &toplevel->destroy);
}

#ifdef DIAMONDS_ENABLE_XWAYLAND
static void handle_xwayland_surface_request_configure(struct wl_listener *listener, void *data) {
	struct diamonds_xwayland_surface *xsurface = wl_container_of(listener, xsurface, request_configure);
	struct wlr_xwayland_surface_configure_event *event = data;
	int16_t y = event->y;
	if (y < 32) y = 32;
	wlr_xwayland_surface_configure(xsurface->xsurface,
		event->x, y, event->width, event->height);
	if (xsurface->scene_tree) {
		wlr_scene_node_set_position(&xsurface->scene_tree->node, event->x, y);
	}
	diamonds_update_dock_position(xsurface->server);
}

static void handle_xwayland_surface_map(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_xwayland_surface *xsurface = wl_container_of(listener, xsurface, map);
	wl_list_insert(&xsurface->server->xwayland_surfaces, &xsurface->link);
	wlr_xwayland_surface_activate(xsurface->xsurface, true);
	if (xsurface->server->seat) {
		wlr_xwayland_set_seat(xsurface->server->xwayland, xsurface->server->seat);
	}
	int y = xsurface->xsurface->y;
	if (y < 32) y = 32;
	xsurface->xsurface->y = y;
	if (xsurface->scene_tree) {
		wlr_scene_node_set_position(&xsurface->scene_tree->node,
			xsurface->xsurface->x, y);
	}
	diamonds_update_dock_position(xsurface->server);
}

static void handle_xwayland_surface_unmap(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_xwayland_surface *xsurface = wl_container_of(listener, xsurface, unmap);
	wl_list_remove(&xsurface->link);
	diamonds_update_dock_position(xsurface->server);
}

static void handle_xwayland_surface_request_move(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_xwayland_surface *xsurface = wl_container_of(listener, xsurface, request_move);
	struct diamonds_server *server = xsurface->server;
	server->cursor_mode = DIAMONDS_CURSOR_MOVE;
	server->grabbed_xsurface = xsurface;
	server->grabbed_toplevel = NULL;
	server->grab_x = server->cursor->x - xsurface->xsurface->x;
	server->grab_y = server->cursor->y - xsurface->xsurface->y;
}

static void handle_xwayland_surface_request_resize(struct wl_listener *listener, void *data) {
	struct diamonds_xwayland_surface *xsurface = wl_container_of(listener, xsurface, request_resize);
	struct wlr_xwayland_resize_event *event = data;
	struct diamonds_server *server = xsurface->server;
	server->cursor_mode = DIAMONDS_CURSOR_RESIZE;
	server->grabbed_xsurface = xsurface;
	server->grabbed_toplevel = NULL;
	server->grab_x = server->cursor->x;
	server->grab_y = server->cursor->y;
	server->grab_geobox.x = xsurface->xsurface->x;
	server->grab_geobox.y = xsurface->xsurface->y;
	server->grab_geobox.width = xsurface->xsurface->width;
	server->grab_geobox.height = xsurface->xsurface->height;
	server->resize_edges = event->edges;
}

static void handle_xwayland_surface_request_maximize(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_xwayland_surface *xsurface = wl_container_of(listener, xsurface, request_maximize);
	diamonds_window_toggle_maximize(NULL, xsurface);
}

static void handle_xwayland_surface_request_fullscreen(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_xwayland_surface *xsurface = wl_container_of(listener, xsurface, request_fullscreen);
	wlr_xwayland_surface_set_fullscreen(xsurface->xsurface, !xsurface->xsurface->fullscreen);
}

static void handle_xwayland_surface_associate(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_xwayland_surface *xsurface = wl_container_of(listener, xsurface, associate);
	struct wlr_xwayland_surface *wlr_xsurface = xsurface->xsurface;
	if (!wlr_xsurface || !wlr_xsurface->surface) {
		return;
	}

	xsurface->workspace = xsurface->server->current_workspace;
	xsurface->scene_tree = wlr_scene_subsurface_tree_create(
		xsurface->server->tree_workspaces[xsurface->server->current_workspace], wlr_xsurface->surface);
	if (xsurface->scene_tree) {
		int y = wlr_xsurface->y;
		if (y < 32) y = 32;
		wlr_scene_node_set_position(&xsurface->scene_tree->node, wlr_xsurface->x, y);
		xsurface->scene_tree->node.data = xsurface;
	}

	xsurface->map.notify = handle_xwayland_surface_map;
	wl_signal_add(&wlr_xsurface->surface->events.map, &xsurface->map);

	xsurface->unmap.notify = handle_xwayland_surface_unmap;
	wl_signal_add(&wlr_xsurface->surface->events.unmap, &xsurface->unmap);
}

static void handle_xwayland_surface_dissociate(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_xwayland_surface *xsurface = wl_container_of(listener, xsurface, dissociate);
	if (xsurface->xsurface && xsurface->xsurface->surface) {
		wl_list_remove(&xsurface->map.link);
		wl_list_remove(&xsurface->unmap.link);
	}
	if (xsurface->scene_tree) {
		wlr_scene_node_destroy(&xsurface->scene_tree->node);
		xsurface->scene_tree = NULL;
	}
}

static void handle_xwayland_surface_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_xwayland_surface *xsurface = wl_container_of(listener, xsurface, destroy);
	if (xsurface->xsurface && xsurface->xsurface->surface) {
		wl_list_remove(&xsurface->map.link);
		wl_list_remove(&xsurface->unmap.link);
	}
	wl_list_remove(&xsurface->associate.link);
	wl_list_remove(&xsurface->dissociate.link);
	wl_list_remove(&xsurface->request_configure.link);
	wl_list_remove(&xsurface->request_move.link);
	wl_list_remove(&xsurface->request_resize.link);
	wl_list_remove(&xsurface->request_maximize.link);
	wl_list_remove(&xsurface->request_fullscreen.link);
	wl_list_remove(&xsurface->destroy.link);
	if (xsurface->scene_tree) {
		wlr_scene_node_destroy(&xsurface->scene_tree->node);
	}
	free(xsurface);
}

static void server_new_xwayland_surface(struct wl_listener *listener, void *data) {
	struct diamonds_server *server = wl_container_of(listener, server, new_xwayland_surface);
	struct wlr_xwayland_surface *wlr_xsurface = data;

	struct diamonds_xwayland_surface *xsurface = calloc(1, sizeof(*xsurface));
	if (!xsurface) {
		return;
	}

	xsurface->server = server;
	xsurface->xsurface = wlr_xsurface;

	xsurface->associate.notify = handle_xwayland_surface_associate;
	wl_signal_add(&wlr_xsurface->events.associate, &xsurface->associate);

	xsurface->dissociate.notify = handle_xwayland_surface_dissociate;
	wl_signal_add(&wlr_xsurface->events.dissociate, &xsurface->dissociate);

	xsurface->request_configure.notify = handle_xwayland_surface_request_configure;
	wl_signal_add(&wlr_xsurface->events.request_configure, &xsurface->request_configure);

	xsurface->request_move.notify = handle_xwayland_surface_request_move;
	wl_signal_add(&wlr_xsurface->events.request_move, &xsurface->request_move);

	xsurface->request_resize.notify = handle_xwayland_surface_request_resize;
	wl_signal_add(&wlr_xsurface->events.request_resize, &xsurface->request_resize);

	xsurface->request_maximize.notify = handle_xwayland_surface_request_maximize;
	wl_signal_add(&wlr_xsurface->events.request_maximize, &xsurface->request_maximize);

	xsurface->request_fullscreen.notify = handle_xwayland_surface_request_fullscreen;
	wl_signal_add(&wlr_xsurface->events.request_fullscreen, &xsurface->request_fullscreen);

	xsurface->destroy.notify = handle_xwayland_surface_destroy;
	wl_signal_add(&wlr_xsurface->events.destroy, &xsurface->destroy);

	if (wlr_xsurface->surface) {
		handle_xwayland_surface_associate(&xsurface->associate, NULL);
	}
}
#endif

/* Keyboard input */
static void handle_keyboard_modifiers(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->wlr_keyboard->modifiers);
}

static bool handle_keybinding(struct diamonds_server *server, uint32_t modifiers, xkb_keysym_t sym) {
	/* Alt+Esc to terminate compositor cleanly */
	if ((modifiers & WLR_MODIFIER_ALT) && sym == XKB_KEY_Escape) {
		wl_display_terminate(server->wl_display);
		return true;
	}
	return false;
}

static void handle_keyboard_key(struct wl_listener *listener, void *data) {
	struct diamonds_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct diamonds_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = server->seat;

	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (int i = 0; i < nsyms; i++) {
			handled = handle_keybinding(server, modifiers, syms[i]);
			if (handled) break;
		}
	}

	if (!handled && server->startmenu && server->startmenu->visible) {
		for (int i = 0; i < nsyms; i++) {
			handled = diamonds_startmenu_handle_key(server->startmenu, syms[i], event->state);
			if (handled) break;
		}
	}

	if (!handled) {
		wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
	}
}

static void handle_keyboard_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

static void server_new_keyboard(struct diamonds_server *server, struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);
	struct diamonds_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	if (!keyboard) return;

	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(
		context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

	keyboard->modifiers.notify = handle_keyboard_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);

	keyboard->key.notify = handle_keyboard_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);

	keyboard->destroy.notify = handle_keyboard_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, wlr_keyboard);
	wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_pointer(struct diamonds_server *server, struct wlr_input_device *device) {
	wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_input(struct wl_listener *listener, void *data) {
	struct diamonds_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	default:
		break;
	}

	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

/* 8-Edge Window Resizing & Border Detection */
static struct diamonds_toplevel *find_toplevel_at(
		struct diamonds_server *server, double lx, double ly,
		uint32_t *out_edges) {
	if (out_edges) *out_edges = WLR_EDGE_NONE;
	const int32_t BORDER = 6;

	struct diamonds_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (!toplevel->scene_tree || !toplevel->scene_tree->node.enabled) continue;
		int32_t wx = toplevel->scene_tree->node.x;
		int32_t wy = toplevel->scene_tree->node.y;
		int32_t ww = toplevel->xdg_toplevel ? toplevel->xdg_toplevel->current.width : 640;
		int32_t wh = toplevel->xdg_toplevel ? toplevel->xdg_toplevel->current.height : 480;
		if (ww <= 0) ww = 640;
		if (wh <= 0) wh = 480;
		if (toplevel->titlebar_scene_buffer) {
			wh += TITLEBAR_HEIGHT;
		}

		if (lx >= wx - BORDER && lx <= wx + ww + BORDER &&
		    ly >= wy - BORDER && ly <= wy + wh + BORDER) {
			uint32_t edges = WLR_EDGE_NONE;
			if (lx <= wx + BORDER) edges |= WLR_EDGE_LEFT;
			if (lx >= wx + ww - BORDER) edges |= WLR_EDGE_RIGHT;
			if (ly <= wy + BORDER) edges |= WLR_EDGE_TOP;
			if (ly >= wy + wh - BORDER) edges |= WLR_EDGE_BOTTOM;

			if (out_edges) *out_edges = edges;
			return toplevel;
		}
	}
	return NULL;
}

#ifdef DIAMONDS_ENABLE_XWAYLAND
static struct diamonds_xwayland_surface *find_xwayland_surface_at(
		struct diamonds_server *server, double lx, double ly,
		uint32_t *out_edges) {
	if (out_edges) *out_edges = WLR_EDGE_NONE;
	const int32_t BORDER = 6;

	struct diamonds_xwayland_surface *xsurface;
	wl_list_for_each(xsurface, &server->xwayland_surfaces, link) {
		if (!xsurface->scene_tree || !xsurface->scene_tree->node.enabled) continue;
		if (!xsurface->xsurface || !xsurface->xsurface->surface || !xsurface->xsurface->surface->mapped) continue;
		int32_t wx = xsurface->xsurface->x;
		int32_t wy = xsurface->xsurface->y;
		int32_t ww = xsurface->xsurface->width;
		int32_t wh = xsurface->xsurface->height;
		if (ww <= 0) ww = 640;
		if (wh <= 0) wh = 480;

		if (lx >= wx - BORDER && lx <= wx + ww + BORDER &&
		    ly >= wy - BORDER && ly <= wy + wh + BORDER) {
			uint32_t edges = WLR_EDGE_NONE;
			if (lx <= wx + BORDER) edges |= WLR_EDGE_LEFT;
			if (lx >= wx + ww - BORDER) edges |= WLR_EDGE_RIGHT;
			if (ly <= wy + BORDER) edges |= WLR_EDGE_TOP;
			if (ly >= wy + wh - BORDER) edges |= WLR_EDGE_BOTTOM;

			if (out_edges) *out_edges = edges;
			return xsurface;
		}
	}
	return NULL;
}
#endif

static const char *cursor_name_from_edges(uint32_t edges) {
	if ((edges & WLR_EDGE_TOP) && (edges & WLR_EDGE_LEFT)) return "nw-resize";
	if ((edges & WLR_EDGE_TOP) && (edges & WLR_EDGE_RIGHT)) return "ne-resize";
	if ((edges & WLR_EDGE_BOTTOM) && (edges & WLR_EDGE_LEFT)) return "sw-resize";
	if ((edges & WLR_EDGE_BOTTOM) && (edges & WLR_EDGE_RIGHT)) return "se-resize";
	if (edges & WLR_EDGE_TOP) return "n-resize";
	if (edges & WLR_EDGE_BOTTOM) return "s-resize";
	if (edges & WLR_EDGE_LEFT) return "w-resize";
	if (edges & WLR_EDGE_RIGHT) return "e-resize";
	return "default";
}

/* Cursor handling & Interactive Snap Move & 8-Edge Resize */
static void process_cursor_motion(struct diamonds_server *server, uint32_t time_msec) {
	if (server->control_center && server->control_center->visible) {
		if (diamonds_control_center_handle_motion(server->control_center, server->cursor->x, server->cursor->y)) {
			wlr_seat_pointer_notify_frame(server->seat);
			return;
		}
	}

	/* 1. Interactive Move */
	if (server->cursor_mode == DIAMONDS_CURSOR_MOVE) {
		int32_t new_x = (int32_t)(server->cursor->x - server->grab_x);
		int32_t new_y = (int32_t)(server->cursor->y - server->grab_y);
		/* Chặn cửa sổ tràn lên vùng Top Bar (y >= 32px) */
		if (new_y < 32) new_y = 32;

		if (server->grabbed_toplevel && server->grabbed_toplevel->scene_tree) {
			wlr_scene_node_set_position(&server->grabbed_toplevel->scene_tree->node, new_x, new_y);
			struct wlr_output *out = wlr_output_layout_output_at(
				server->output_layout, server->cursor->x, server->cursor->y);
			if (out && server->snap_preview) {
				diamonds_snap_zone_t zone = diamonds_detect_snap_zone(
					server->cursor->x, server->cursor->y, out->width, out->height);
				diamonds_snap_preview_update(server->snap_preview, zone, out->width, out->height);
			}
			return;
		}
#ifdef DIAMONDS_ENABLE_XWAYLAND
		if (server->grabbed_xsurface && server->grabbed_xsurface->xsurface) {
			server->grabbed_xsurface->xsurface->x = new_x;
			server->grabbed_xsurface->xsurface->y = new_y;
			if (server->grabbed_xsurface->scene_tree) {
				wlr_scene_node_set_position(&server->grabbed_xsurface->scene_tree->node, new_x, new_y);
			}
			wlr_xwayland_surface_configure(server->grabbed_xsurface->xsurface,
				new_x, new_y,
				server->grabbed_xsurface->xsurface->width,
				server->grabbed_xsurface->xsurface->height);
			return;
		}
#endif
		return;
	}

	/* 2. Interactive Resize (8 edges/corners) */
	if (server->cursor_mode == DIAMONDS_CURSOR_RESIZE) {
		double dx = server->cursor->x - server->grab_x;
		double dy = server->cursor->y - server->grab_y;

		int32_t new_x = server->grab_geobox.x;
		int32_t new_y = server->grab_geobox.y;
		int32_t new_w = server->grab_geobox.width;
		int32_t new_h = server->grab_geobox.height;

		if (server->resize_edges & WLR_EDGE_RIGHT) {
			new_w += (int32_t)dx;
		}
		if (server->resize_edges & WLR_EDGE_LEFT) {
			new_x += (int32_t)dx;
			new_w -= (int32_t)dx;
		}
		if (server->resize_edges & WLR_EDGE_BOTTOM) {
			new_h += (int32_t)dy;
		}
		if (server->resize_edges & WLR_EDGE_TOP) {
			new_y += (int32_t)dy;
			new_h -= (int32_t)dy;
		}

		if (new_w < 150) {
			if (server->resize_edges & WLR_EDGE_LEFT) {
				new_x = server->grab_geobox.x + server->grab_geobox.width - 150;
			}
			new_w = 150;
		}
		if (new_h < 100) {
			if (server->resize_edges & WLR_EDGE_TOP) {
				new_y = server->grab_geobox.y + server->grab_geobox.height - 100;
			}
			new_h = 100;
		}

		/* Chặn cửa sổ tràn lên vùng Top Bar (y >= 32px) */
		if (new_y < 32) {
			if (server->resize_edges & WLR_EDGE_TOP) {
				new_h -= (32 - new_y);
				if (new_h < 100) new_h = 100;
			}
			new_y = 32;
		}

		if (server->grabbed_toplevel && server->grabbed_toplevel->xdg_toplevel) {
			wlr_scene_node_set_position(&server->grabbed_toplevel->scene_tree->node, new_x, new_y);
			int32_t client_h = new_h;
			if (server->grabbed_toplevel->titlebar_scene_buffer) {
				client_h = new_h - TITLEBAR_HEIGHT;
				if (client_h < 60) client_h = 60;
			}
			wlr_xdg_toplevel_set_size(server->grabbed_toplevel->xdg_toplevel, new_w, client_h);
			if (server->grabbed_toplevel->titlebar_scene_buffer) {
				diamonds_toplevel_update_titlebar(server->grabbed_toplevel);
			}
			return;
		}
#ifdef DIAMONDS_ENABLE_XWAYLAND
		if (server->grabbed_xsurface && server->grabbed_xsurface->xsurface) {
			server->grabbed_xsurface->xsurface->x = new_x;
			server->grabbed_xsurface->xsurface->y = new_y;
			server->grabbed_xsurface->xsurface->width = new_w;
			server->grabbed_xsurface->xsurface->height = new_h;
			if (server->grabbed_xsurface->scene_tree) {
				wlr_scene_node_set_position(&server->grabbed_xsurface->scene_tree->node, new_x, new_y);
			}
			wlr_xwayland_surface_configure(server->grabbed_xsurface->xsurface,
				new_x, new_y, new_w, new_h);
			return;
		}
#endif
		return;
	}

	/* 3. Passthrough mode: Check resize borders on mapped windows */
	uint32_t edges = WLR_EDGE_NONE;
	struct diamonds_toplevel *hover_toplevel = find_toplevel_at(server, server->cursor->x, server->cursor->y, &edges);
	if (hover_toplevel && edges != WLR_EDGE_NONE) {
		const char *cname = cursor_name_from_edges(edges);
		if (strcmp(server->current_cursor, cname) != 0) {
			wlr_cursor_unset_image(server->cursor);
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, cname);
			snprintf(server->current_cursor, sizeof(server->current_cursor), "%s", cname);
		}
		wlr_seat_pointer_notify_clear_focus(server->seat);
		return;
	}

#ifdef DIAMONDS_ENABLE_XWAYLAND
	struct diamonds_xwayland_surface *hover_xsurface = find_xwayland_surface_at(server, server->cursor->x, server->cursor->y, &edges);
	if (hover_xsurface && edges != WLR_EDGE_NONE) {
		const char *cname = cursor_name_from_edges(edges);
		if (strcmp(server->current_cursor, cname) != 0) {
			wlr_cursor_unset_image(server->cursor);
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, cname);
			snprintf(server->current_cursor, sizeof(server->current_cursor), "%s", cname);
		}
		wlr_seat_pointer_notify_clear_focus(server->seat);
		return;
	}
#endif

	/* 4. Scene graph lookup */
	double sx, sy;
	struct wlr_scene_node *node = wlr_scene_node_at(
		&server->scene->tree.node, server->cursor->x, server->cursor->y, &sx, &sy);
	if (!node) {
		if (strcmp(server->current_cursor, "default") != 0) {
			wlr_cursor_unset_image(server->cursor);
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
			snprintf(server->current_cursor, sizeof(server->current_cursor), "default");
		}
		wlr_seat_pointer_notify_clear_focus(server->seat);
		return;
	}

	if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *scene_buf = wlr_scene_buffer_from_node(node);
		struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buf);
		if (scene_surface) {
			server->current_cursor[0] = '\0';
			wlr_seat_pointer_notify_enter(server->seat, scene_surface->surface, sx, sy);
			wlr_seat_pointer_notify_motion(server->seat, time_msec, sx, sy);
			return;
		}
	}

	if (strcmp(server->current_cursor, "default") != 0) {
		wlr_cursor_unset_image(server->cursor);
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
		snprintf(server->current_cursor, sizeof(server->current_cursor), "default");
	}
	wlr_seat_pointer_notify_clear_focus(server->seat);
}

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
	struct diamonds_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	struct diamonds_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

static void handle_cursor_button(struct wl_listener *listener, void *data) {
	struct diamonds_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;

	if (event->state == WLR_BUTTON_RELEASED) {
		if (server->control_center) {
			diamonds_control_center_handle_button_release(server->control_center);
		}
		if (server->cursor_mode == DIAMONDS_CURSOR_MOVE) {
			if (server->grabbed_toplevel) {
				struct wlr_output *out = wlr_output_layout_output_at(
					server->output_layout, server->cursor->x, server->cursor->y);
				if (out && server->snap_preview && server->snap_preview->visible &&
				    server->snap_preview->current_zone != SNAP_NONE) {
					diamonds_apply_snap(
						server->grabbed_toplevel,
						server->snap_preview->current_zone,
						out->width, out->height);
				}
				if (server->snap_preview) {
					diamonds_snap_preview_hide(server->snap_preview);
				}
			}
			server->cursor_mode = DIAMONDS_CURSOR_PASSTHROUGH;
			server->grabbed_toplevel = NULL;
			server->grabbed_xsurface = NULL;
			diamonds_update_dock_position(server);
			wlr_seat_pointer_notify_frame(server->seat);
			return;
		} else if (server->cursor_mode == DIAMONDS_CURSOR_RESIZE) {
			server->cursor_mode = DIAMONDS_CURSOR_PASSTHROUGH;
			server->grabbed_toplevel = NULL;
			server->grabbed_xsurface = NULL;
			diamonds_update_dock_position(server);
			wlr_seat_pointer_notify_frame(server->seat);
			return;
		}
	} else if (event->state == WLR_BUTTON_PRESSED) {
		double lx = server->cursor->x;
		double ly = server->cursor->y;

		/* 1. Check Top Bar click (Left click only) */
		if (event->button == BTN_LEFT && server->topbar && diamonds_topbar_handle_click(server->topbar, lx, ly)) {
			diamonds_sound_play(DIAMONDS_SOUND_CLICK);
			wlr_seat_pointer_notify_frame(server->seat);
			return;
		}

		/* 1b. Check Control Center click */
		if (event->button == BTN_LEFT && server->control_center && server->control_center->visible) {
			if (diamonds_control_center_handle_click(server->control_center, lx, ly)) {
				diamonds_sound_play(DIAMONDS_SOUND_CLICK);
				wlr_seat_pointer_notify_frame(server->seat);
				return;
			}
		}

		/* 1c. Check Calendar click */
		if (event->button == BTN_LEFT && server->calendar && server->calendar->visible) {
			if (diamonds_calendar_handle_click(server->calendar, lx, ly)) {
				diamonds_sound_play(DIAMONDS_SOUND_CLICK);
				wlr_seat_pointer_notify_frame(server->seat);
				return;
			}
		}

		/* 2. Check Start Menu click */
		if (event->button == BTN_LEFT && server->startmenu && server->startmenu->visible) {
			if (diamonds_startmenu_handle_click(server->startmenu, lx, ly)) {
				diamonds_sound_play(DIAMONDS_SOUND_CLICK);
				wlr_seat_pointer_notify_frame(server->seat);
				return;
			}
		}

		/* 3. Check Dock click (Handles BOTH BTN_LEFT and BTN_RIGHT) */
		if (server->dock) {
			bool start_toggled = false;
			if (diamonds_dock_handle_click(server->dock, lx, ly, event->button, &start_toggled)) {
				if (start_toggled) {
					diamonds_sound_play(DIAMONDS_SOUND_CLICK);
					launch_app_menu(server);
				}
				wlr_seat_pointer_notify_frame(server->seat);
				return;
			}
		}

		/* 4. Alt+Left Drag on Window to Move */
		struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
		uint32_t mods = kb ? wlr_keyboard_get_modifiers(kb) : 0;
		if (event->button == BTN_LEFT && (mods & WLR_MODIFIER_ALT)) {
			struct diamonds_toplevel *alt_top = find_toplevel_at(server, lx, ly, NULL);
			if (alt_top) {
				diamonds_focus_toplevel(server, alt_top);
				server->cursor_mode = DIAMONDS_CURSOR_MOVE;
				server->grabbed_toplevel = alt_top;
				server->grabbed_xsurface = NULL;
				server->grab_x = lx - alt_top->scene_tree->node.x;
				server->grab_y = ly - alt_top->scene_tree->node.y;
				wlr_seat_pointer_notify_frame(server->seat);
				return;
			}
#ifdef DIAMONDS_ENABLE_XWAYLAND
			struct diamonds_xwayland_surface *alt_x = find_xwayland_surface_at(server, lx, ly, NULL);
			if (alt_x) {
				server->cursor_mode = DIAMONDS_CURSOR_MOVE;
				server->grabbed_xsurface = alt_x;
				server->grabbed_toplevel = NULL;
				server->grab_x = lx - alt_x->xsurface->x;
				server->grab_y = ly - alt_x->xsurface->y;
				wlr_xwayland_surface_activate(alt_x->xsurface, true);
				wlr_seat_pointer_notify_frame(server->seat);
				return;
			}
#endif
		}

		/* 5. Check 6px window resize border click (Left click) */
		if (event->button == BTN_LEFT) {
			uint32_t edges = WLR_EDGE_NONE;
			struct diamonds_toplevel *target_top = find_toplevel_at(server, lx, ly, &edges);
			if (target_top && edges != WLR_EDGE_NONE) {
				diamonds_focus_toplevel(server, target_top);
				server->cursor_mode = DIAMONDS_CURSOR_RESIZE;
				server->grabbed_toplevel = target_top;
				server->grabbed_xsurface = NULL;
				server->grab_x = lx;
				server->grab_y = ly;
				server->grab_geobox.x = target_top->scene_tree->node.x;
				server->grab_geobox.y = target_top->scene_tree->node.y;
				server->grab_geobox.width = target_top->xdg_toplevel ? target_top->xdg_toplevel->current.width : 640;
				server->grab_geobox.height = target_top->xdg_toplevel ? target_top->xdg_toplevel->current.height : 480;
				if (target_top->titlebar_scene_buffer) {
					server->grab_geobox.height += TITLEBAR_HEIGHT;
				}
				server->resize_edges = edges;
				wlr_seat_pointer_notify_frame(server->seat);
				return;
			}

#ifdef DIAMONDS_ENABLE_XWAYLAND
			struct diamonds_xwayland_surface *target_x = find_xwayland_surface_at(server, lx, ly, &edges);
			if (target_x && edges != WLR_EDGE_NONE) {
				server->cursor_mode = DIAMONDS_CURSOR_RESIZE;
				server->grabbed_xsurface = target_x;
				server->grabbed_toplevel = NULL;
				server->grab_x = lx;
				server->grab_y = ly;
				server->grab_geobox.x = target_x->xsurface->x;
				server->grab_geobox.y = target_x->xsurface->y;
				server->grab_geobox.width = target_x->xsurface->width;
				server->grab_geobox.height = target_x->xsurface->height;
				server->resize_edges = edges;
				wlr_xwayland_surface_activate(target_x->xsurface, true);
				wlr_seat_pointer_notify_frame(server->seat);
				return;
			}
#endif
		}

		/* 6. Check Windows & SSD Titlebar click */
		double sx, sy;
		struct wlr_scene_node *node = wlr_scene_node_at(
			&server->scene->tree.node, lx, ly, &sx, &sy);
		if (node) {
			/* Check if clicked on a toplevel's SSD titlebar */
			struct diamonds_toplevel *toplevel;
			wl_list_for_each(toplevel, &server->toplevels, link) {
				if (toplevel->titlebar_scene_buffer &&
				    node == &toplevel->titlebar_scene_buffer->node) {
					diamonds_focus_toplevel(server, toplevel);
					if (event->button == BTN_LEFT) {
						diamonds_titlebar_action_t action =
							diamonds_titlebar_handle_click(toplevel, sx, sy);
						if (action == TITLEBAR_CLICK_MOVE) {
							server->cursor_mode = DIAMONDS_CURSOR_MOVE;
							server->grabbed_toplevel = toplevel;
							server->grabbed_xsurface = NULL;
							server->grab_x = lx - toplevel->scene_tree->node.x;
							server->grab_y = ly - toplevel->scene_tree->node.y;
						}
					}
					diamonds_update_dock_position(server);
					wlr_seat_pointer_notify_frame(server->seat);
					return;
				}
			}

			/* Check if clicked on client surface */
			if (node->type == WLR_SCENE_NODE_BUFFER) {
				struct wlr_scene_buffer *scene_buf = wlr_scene_buffer_from_node(node);
				struct wlr_scene_surface *scene_surface =
					wlr_scene_surface_try_from_buffer(scene_buf);
				if (scene_surface) {
					struct wlr_xdg_surface *xdg_surface =
						wlr_xdg_surface_try_from_wlr_surface(scene_surface->surface);
					if (xdg_surface && xdg_surface->data) {
						struct wlr_scene_tree *tree = xdg_surface->data;
						if (tree->node.data) {
							diamonds_focus_toplevel(server, tree->node.data);
						}
					}
#ifdef DIAMONDS_ENABLE_XWAYLAND
					struct diamonds_xwayland_surface *xsurface;
					wl_list_for_each(xsurface, &server->xwayland_surfaces, link) {
						if (xsurface->xsurface && xsurface->xsurface->surface == scene_surface->surface) {
							wlr_xwayland_surface_activate(xsurface->xsurface, true);
							if (xsurface->scene_tree) {
								wlr_scene_node_raise_to_top(&xsurface->scene_tree->node);
							}
							break;
						}
					}
#endif
				}
			}
		}
	}

	wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
}

static void handle_cursor_axis(struct wl_listener *listener, void *data) {
	struct diamonds_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;

	if (server->startmenu && server->startmenu->visible) {
		if (diamonds_startmenu_handle_scroll(server->startmenu, server->cursor->x, server->cursor->y, event->delta)) {
			wlr_seat_pointer_notify_frame(server->seat);
			return;
		}
	}

	wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation,
		event->delta, event->delta_discrete, event->source);
}

static void handle_cursor_frame(struct wl_listener *listener, void *data) {
	(void)data;
	struct diamonds_server *server = wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

static void handle_request_cursor(struct wl_listener *listener, void *data) {
	struct diamonds_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;
	if (focused_client == event->seat_client) {
		if (event->surface) {
			wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
		} else {
			wlr_cursor_unset_image(server->cursor);
		}
		server->current_cursor[0] = '\0';
	}
}

static void handle_request_set_selection(struct wl_listener *listener, void *data) {
	struct diamonds_server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static int handle_signal_term(int signal, void *data) {
	(void)signal;
	struct diamonds_server *server = data;
	wlr_log(WLR_INFO, "Caught termination signal, stopping diamonds-desktop...");
	wl_display_terminate(server->wl_display);
	return 0;
}

void launch_app(const char *cmd) {
	if (!cmd || !*cmd) {
		return;
	}

	pid_t pid = fork();
	if (pid < 0) {
		wlr_log(WLR_ERROR, "Failed to fork process for %s", cmd);
		return;
	}

	if (pid == 0) {
		signal(SIGPIPE, SIG_DFL);
		setsid();

		char *cmd_dup = strdup(cmd);
		if (!cmd_dup) {
			_exit(1);
		}

		char *argv[64];
		int argc = 0;
		char *p = cmd_dup;
		while (*p && argc < 63) {
			while (*p == ' ' || *p == '\t') p++;
			if (!*p) break;
			argv[argc++] = p;
			while (*p && *p != ' ' && *p != '\t') p++;
			if (*p) {
				*p = '\0';
				p++;
			}
		}
		argv[argc] = NULL;

		const char *home = getenv("HOME");
		if (!home) home = "/";
		char expanded[64][1024];
		for (int i = 0; i < argc; i++) {
			if (strcmp(argv[i], "~") == 0) {
				argv[i] = (char *)home;
			} else if (strncmp(argv[i], "~/", 2) == 0) {
				snprintf(expanded[i], sizeof(expanded[i]), "%s/%s", home, argv[i] + 2);
				argv[i] = expanded[i];
			}
		}

		if (argc > 0) {
			execvp(argv[0], argv);
		}

		execl("/bin/sh", "/bin/sh", "-c", cmd, (void *)NULL);
		_exit(127);
	}
}

static bool command_exists(const char *name) {
	if (!name || !*name) return false;
	const char *path = getenv("PATH");
	if (!path) path = "/usr/local/bin:/usr/bin:/bin";
	char path_copy[2048];
	snprintf(path_copy, sizeof(path_copy), "%s", path);
	char *dir = strtok(path_copy, ":");
	while (dir) {
		char full[1024];
		snprintf(full, sizeof(full), "%s/%s", dir, name);
		if (access(full, X_OK) == 0) {
			return true;
		}
		dir = strtok(NULL, ":");
	}
	return false;
}

void launch_terminal(void) {
	static const char *terminals[] = {
		"terminator",
		"xfce4-terminal",
		"foot",
		"xterm",
		NULL
	};
	for (int i = 0; terminals[i]; i++) {
		if (command_exists(terminals[i])) {
			launch_app(terminals[i]);
			return;
		}
	}
	launch_app("x-terminal-emulator");
}

void launch_files(void) {
	if (command_exists("xdg-open")) {
		launch_app("xdg-open ~");
	} else if (command_exists("thunar")) {
		launch_app("thunar");
	} else if (command_exists("pcmanfm")) {
		launch_app("pcmanfm");
	} else if (command_exists("nautilus")) {
		launch_app("nautilus");
	}
}

void launch_app_menu(struct diamonds_server *server) {
	if (command_exists("wofi")) {
		launch_app("wofi --show drun");
		return;
	}
	if (command_exists("fuzzel")) {
		launch_app("fuzzel");
		return;
	}
	if (server && server->startmenu) {
		diamonds_startmenu_toggle(server->startmenu);
	}
}

void diamonds_capture_screen(struct diamonds_server *server, const char *filepath) {
	int w = 1366, h = 768;
	if (!wl_list_empty(&server->outputs)) {
		struct diamonds_output *out = wl_container_of(server->outputs.next, out, link);
		if (out->wlr_output && out->wlr_output->width > 0) {
			w = out->wlr_output->width;
			h = out->wlr_output->height;
		}
	}

	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *cr = cairo_create(surf);

	/* 1. Deep Mica Dark Wallpaper or Custom Background */
	if (server->wallpaper && server->wallpaper->buffer && server->wallpaper->buffer->cairo_surface) {
		cairo_set_source_surface(cr, server->wallpaper->buffer->cairo_surface, 0, 0);
		cairo_paint(cr);
	} else {
		cairo_pattern_t *pat = cairo_pattern_create_linear(0, 0, (double)w, (double)h);
		cairo_pattern_add_color_stop_rgb(pat, 0.0, 0.07, 0.08, 0.12);
		cairo_pattern_add_color_stop_rgb(pat, 0.5, 0.09, 0.11, 0.16);
		cairo_pattern_add_color_stop_rgb(pat, 1.0, 0.06, 0.07, 0.10);
		cairo_set_source(cr, pat);
		cairo_paint(cr);
		cairo_pattern_destroy(pat);

		/* Subtle geometric diamond motif in background */
		cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.04);
		cairo_arc(cr, (double)w / 2.0, (double)h / 2.0, 220.0, 0, 6.28);
		cairo_fill(cr);
	}

	/* 2. Top Bar */
	if (server->topbar && server->topbar->buffer && server->topbar->buffer->cairo_surface) {
		cairo_set_source_surface(cr, server->topbar->buffer->cairo_surface, 0, 0);
		cairo_paint(cr);
	}

	/* 3. Windows (Toplevels) */
	struct diamonds_toplevel *toplevel;
	wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
		if (!toplevel->xdg_toplevel || !toplevel->xdg_toplevel->base->surface->mapped) {
			continue;
		}
		if (!toplevel->scene_tree || !toplevel->scene_tree->node.enabled) {
			continue;
		}

		int wx = toplevel->scene_tree->node.x;
		int wy = toplevel->scene_tree->node.y;
		int client_y = wy;
		if (toplevel->titlebar_scene_buffer) {
			client_y += TITLEBAR_HEIGHT;
		}

		/* Window client surface */
		struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
		if (surface && surface->buffer) {
			struct wlr_buffer *w_buf = surface->buffer->source;
			void *data = NULL;
			uint32_t fmt = 0;
			size_t stride = 0;
			if (w_buf && wlr_buffer_begin_data_ptr_access(w_buf, WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &fmt, &stride)) {
				cairo_surface_t *c_surf = cairo_image_surface_create_for_data(
					(unsigned char *)data, CAIRO_FORMAT_ARGB32,
					surface->current.width, surface->current.height, (int)stride);
				cairo_set_source_surface(cr, c_surf, wx, client_y);
				cairo_paint(cr);
				cairo_surface_destroy(c_surf);
				wlr_buffer_end_data_ptr_access(w_buf);
			}
		}

		/* SSD Titlebar */
		if (toplevel->titlebar_buffer && toplevel->titlebar_buffer->cairo_surface && toplevel->titlebar_scene_buffer) {
			cairo_set_source_surface(cr, toplevel->titlebar_buffer->cairo_surface, wx, wy);
			cairo_paint(cr);
		}
	}

#ifdef DIAMONDS_ENABLE_XWAYLAND
	struct diamonds_xwayland_surface *xsurface;
	wl_list_for_each_reverse(xsurface, &server->xwayland_surfaces, link) {
		if (!xsurface->xsurface || !xsurface->xsurface->surface || !xsurface->xsurface->surface->mapped) {
			continue;
		}
		if (!xsurface->scene_tree || !xsurface->scene_tree->node.enabled) {
			continue;
		}
		int wx = xsurface->xsurface->x;
		int wy = xsurface->xsurface->y;
		struct wlr_surface *surface = xsurface->xsurface->surface;
		if (surface && surface->buffer) {
			struct wlr_buffer *w_buf = surface->buffer->source;
			void *data = NULL;
			uint32_t fmt = 0;
			size_t stride = 0;
			if (w_buf && wlr_buffer_begin_data_ptr_access(w_buf, WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &fmt, &stride)) {
				cairo_surface_t *c_surf = cairo_image_surface_create_for_data(
					(unsigned char *)data, CAIRO_FORMAT_ARGB32,
					surface->current.width, surface->current.height, (int)stride);
				cairo_set_source_surface(cr, c_surf, wx, wy);
				cairo_paint(cr);
				cairo_surface_destroy(c_surf);
				wlr_buffer_end_data_ptr_access(w_buf);
			}
		}
	}
#endif

	/* 4. Dock */
	if (server->dock && server->dock->buffer && server->dock->buffer->cairo_surface) {
		cairo_set_source_surface(cr, server->dock->buffer->cairo_surface, server->dock->x, server->dock->y);
		cairo_paint(cr);
	}

	/* 4b. Dock Popup Context Menu (if visible) */
	if (server->dock && server->dock->popup.visible &&
	    server->dock->popup.buffer && server->dock->popup.buffer->cairo_surface) {
		cairo_set_source_surface(cr, server->dock->popup.buffer->cairo_surface,
			server->dock->popup.x, server->dock->popup.y);
		cairo_paint(cr);
	}

	/* 5. Start Menu (if visible) */
	if (server->startmenu && server->startmenu->visible && server->startmenu->buffer && server->startmenu->buffer->cairo_surface) {
		cairo_set_source_surface(cr, server->startmenu->buffer->cairo_surface, server->startmenu->x, server->startmenu->y);
		cairo_paint(cr);
	}

	/* 6. Snap Preview (if visible) */
	if (server->snap_preview && server->snap_preview->visible && server->snap_preview->buffer && server->snap_preview->buffer->cairo_surface) {
		cairo_set_source_surface(cr, server->snap_preview->buffer->cairo_surface, server->snap_preview->target_x, server->snap_preview->target_y);
		cairo_paint(cr);
	}

	/* 7. Calendar (if visible) */
	if (server->calendar && server->calendar->visible && server->calendar->buffer && server->calendar->buffer->cairo_surface) {
		cairo_set_source_surface(cr, server->calendar->buffer->cairo_surface, server->calendar->x, server->calendar->y);
		cairo_paint(cr);
	}

	/* 8. Control Center (if visible) */
	if (server->control_center && server->control_center->visible && server->control_center->buffer && server->control_center->buffer->cairo_surface) {
		cairo_set_source_surface(cr, server->control_center->buffer->cairo_surface, server->control_center->x, server->control_center->y);
		cairo_paint(cr);
	}

	cairo_destroy(cr);
	cairo_surface_write_to_png(surf, filepath);
	cairo_surface_destroy(surf);
}

static int handle_control_ipc(int fd, uint32_t mask, void *data) {
	(void)mask;
	struct diamonds_server *server = data;
	int client_fd = accept(fd, NULL, NULL);
	if (client_fd < 0) return 0;

	char buf[256] = {0};
	ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = '\0';
		if (strncmp(buf, "see", 3) == 0) {
			diamonds_capture_screen(server, "/tmp/diamonds_screen.png");
			ssize_t w = write(client_fd, "OK\n", 3);
			(void)w;
		} else if (strncmp(buf, "click", 5) == 0) {
			double cx = 0, cy = 0;
			if (sscanf(buf + 5, "%lf %lf", &cx, &cy) == 2) {
				server->cursor->x = cx;
				server->cursor->y = cy;
				wlr_cursor_warp(server->cursor, NULL, cx, cy);
				struct timespec now;
				clock_gettime(CLOCK_MONOTONIC, &now);
				uint32_t t = (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000);
				process_cursor_motion(server, t);

				struct wlr_pointer_button_event press_ev = {
					.button = BTN_LEFT,
					.state = WLR_BUTTON_PRESSED,
					.time_msec = t,
				};
				handle_cursor_button(&server->cursor_button, &press_ev);

				struct wlr_pointer_button_event rel_ev = {
					.button = BTN_LEFT,
					.state = WLR_BUTTON_RELEASED,
					.time_msec = t + 50,
				};
				handle_cursor_button(&server->cursor_button, &rel_ev);
				ssize_t w = write(client_fd, "OK\n", 3);
				(void)w;
			}
		} else if (strncmp(buf, "rclick", 6) == 0) {
			double cx = 0, cy = 0;
			if (sscanf(buf + 6, "%lf %lf", &cx, &cy) == 2) {
				server->cursor->x = cx;
				server->cursor->y = cy;
				wlr_cursor_warp(server->cursor, NULL, cx, cy);
				struct timespec now;
				clock_gettime(CLOCK_MONOTONIC, &now);
				uint32_t t = (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000);
				process_cursor_motion(server, t);

				struct wlr_pointer_button_event press_ev = {
					.button = BTN_RIGHT,
					.state = WLR_BUTTON_PRESSED,
					.time_msec = t,
				};
				handle_cursor_button(&server->cursor_button, &press_ev);

				struct wlr_pointer_button_event rel_ev = {
					.button = BTN_RIGHT,
					.state = WLR_BUTTON_RELEASED,
					.time_msec = t + 50,
				};
				handle_cursor_button(&server->cursor_button, &rel_ev);
				ssize_t w = write(client_fd, "OK\n", 3);
				(void)w;
			}
		} else if (strncmp(buf, "toggle_startmenu", 16) == 0) {
			if (server->startmenu) {
				diamonds_startmenu_toggle(server->startmenu);
			}
			ssize_t w = write(client_fd, "OK\n", 3);
			(void)w;
		} else if (strncmp(buf, "toggle_calendar", 15) == 0) {
			if (server->calendar) {
				diamonds_calendar_toggle(server->calendar);
			}
			ssize_t w = write(client_fd, "OK\n", 3);
			(void)w;
		} else if (strncmp(buf, "toggle_control_center", 21) == 0) {
			if (server->control_center) {
				diamonds_control_center_toggle(server->control_center);
				if (server->topbar) {
					server->topbar->control_center_active = server->control_center->visible;
					diamonds_topbar_render(server->topbar);
				}
			}
			ssize_t w = write(client_fd, "OK\n", 3);
			(void)w;
		} else if (strncmp(buf, "type ", 5) == 0) {
			const char *str = buf + 5;
			while (*str && *str != '\n' && *str != '\r') {
				if (server->startmenu && server->startmenu->visible) {
					diamonds_startmenu_handle_key(server->startmenu, (xkb_keysym_t)(unsigned char)*str, WL_KEYBOARD_KEY_STATE_PRESSED);
				}
				str++;
			}
			ssize_t w = write(client_fd, "OK\n", 3);
			(void)w;
		} else if (strncmp(buf, "workspace", 9) == 0) {
			int ws = 1;
			if (sscanf(buf + 9, "%d", &ws) == 1 && ws >= 1 && ws <= DIAMONDS_NUM_WORKSPACES) {
				diamonds_server_switch_workspace(server, ws - 1);
			}
			ssize_t w = write(client_fd, "OK\n", 3);
			(void)w;
		}
	}
	close(client_fd);
	return 0;
}

static void print_usage(const char *prog) {
	printf("Usage: %s [options]\n\n"
	       "Options:\n"
	       "  -h, --help       Show help options\n"
	       "  -v, --version    Show version\n"
	       "  -s <command>     Run startup command\n", prog);
}

int main(int argc, char *argv[]) {
	const char *startup_cmd = NULL;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
			printf("diamonds-desktop version %s (wlroots compositor & shell)\n", DIAMONDS_VERSION);
			return 0;
		}
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			print_usage(argv[0]);
			return 0;
		}
		if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			startup_cmd = argv[++i];
		}
	}

	wlr_log_init(WLR_DEBUG, NULL);
	diamonds_sound_init();
	signal(SIGPIPE, SIG_IGN);

	const char *init_old_path = getenv("PATH");
	char init_new_path[2048];
	if (init_old_path) {
		snprintf(init_new_path, sizeof(init_new_path), "/home/a/diamonds-desktop/build:/usr/local/bin:/usr/bin:/bin:%s", init_old_path);
	} else {
		snprintf(init_new_path, sizeof(init_new_path), "/home/a/diamonds-desktop/build:/usr/local/bin:/usr/bin:/bin");
	}
	setenv("PATH", init_new_path, 1);

	struct diamonds_server server = {0};
	server.cursor_mode = DIAMONDS_CURSOR_PASSTHROUGH;
	wl_list_init(&server.outputs);
	wl_list_init(&server.toplevels);
	wl_list_init(&server.keyboards);

#ifdef DIAMONDS_ENABLE_XWAYLAND
	wl_list_init(&server.xwayland_surfaces);
#endif

	server.wl_display = wl_display_create();
	if (!server.wl_display) {
		wlr_log(WLR_ERROR, "Failed to create Wayland display");
		return 1;
	}

	server.event_loop = wl_display_get_event_loop(server.wl_display);
	server.backend = wlr_backend_autocreate(server.wl_display, NULL);
	if (!server.backend) {
		wlr_log(WLR_ERROR, "Failed to autocreate wlr_backend");
		wl_display_destroy(server.wl_display);
		return 1;
	}

	server.renderer = wlr_renderer_autocreate(server.backend);
	if (!server.renderer) {
		wlr_log(WLR_ERROR, "Failed to autocreate wlr_renderer");
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}
	wlr_renderer_init_wl_display(server.renderer, server.wl_display);

	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	if (!server.allocator) {
		wlr_log(WLR_ERROR, "Failed to autocreate wlr_allocator");
		return 1;
	}

	/* Compositor globals */
	struct wlr_compositor *compositor = wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);

	/* Scene graph and layers */
	server.scene = wlr_scene_create();
	server.tree_background = wlr_scene_tree_create(&server.scene->tree);
	server.tree_windows = wlr_scene_tree_create(&server.scene->tree);
	server.tree_overlay = wlr_scene_tree_create(&server.scene->tree);

	/* Virtual Workspaces 1..4 */
	for (int i = 0; i < DIAMONDS_NUM_WORKSPACES; i++) {
		server.tree_workspaces[i] = wlr_scene_tree_create(server.tree_windows);
		wlr_scene_node_set_enabled(&server.tree_workspaces[i]->node, i == 0);
	}
	server.current_workspace = 0;

	/* Clean solid blank desktop background: #12141c */
	server.background_rect = wlr_scene_rect_create(
		server.tree_background, 1920, 1080, (float[]){0.07f, 0.08f, 0.11f, 1.0f});

	server.output_layout = wlr_output_layout_create();
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

	server.new_output.notify = server_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	/* XDG Shell & Server-Side Decoration Enforcement */
	server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
	server.new_xdg_surface.notify = server_new_xdg_surface;
	wl_signal_add(&server.xdg_shell->events.new_surface, &server.new_xdg_surface);

	server.xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(server.wl_display);
	server.new_toplevel_decoration.notify = server_new_toplevel_decoration;
	wl_signal_add(&server.xdg_decoration_mgr->events.new_toplevel_decoration, &server.new_toplevel_decoration);

#ifdef DIAMONDS_ENABLE_XWAYLAND
	server.xwayland = wlr_xwayland_create(server.wl_display, compositor, true);
	if (server.xwayland) {
		server.new_xwayland_surface.notify = server_new_xwayland_surface;
		wl_signal_add(&server.xwayland->events.new_surface, &server.new_xwayland_surface);
		setenv("DISPLAY", server.xwayland->display_name, true);
		wlr_log(WLR_INFO, "XWayland running on DISPLAY=%s", server.xwayland->display_name);
	}
#endif

	/* Cursor and seat setup */
	server.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
	server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	if (server.cursor_mgr) {
		wlr_xcursor_manager_load(server.cursor_mgr, 1.0f);
	}
	snprintf(server.current_cursor, sizeof(server.current_cursor), "default");
	wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "default");

	server.cursor_motion.notify = handle_cursor_motion;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);

	server.cursor_motion_absolute.notify = handle_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);

	server.cursor_button.notify = handle_cursor_button;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);

	server.cursor_axis.notify = handle_cursor_axis;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);

	server.cursor_frame.notify = handle_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

	server.seat = wlr_seat_create(server.wl_display, "seat0");
	server.request_cursor.notify = handle_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);

	server.request_set_selection.notify = handle_request_set_selection;
	wl_signal_add(&server.seat->events.request_set_selection, &server.request_set_selection);

#ifdef DIAMONDS_ENABLE_XWAYLAND
	if (server.xwayland) {
		wlr_xwayland_set_seat(server.xwayland, server.seat);
	}
#endif

	server.new_input.notify = server_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);

	/* Initialize Wallpaper, Top Bar, Calendar, Control Center, Dock, Start Menu, and Snap Assist Preview */
	server.wallpaper = diamonds_wallpaper_create(&server);
	server.topbar = diamonds_topbar_create(&server);
	server.calendar = diamonds_calendar_create(&server);
	server.control_center = diamonds_control_center_create(&server);
	server.dock = diamonds_dock_create(&server);
	server.startmenu = diamonds_startmenu_create(&server);
	server.snap_preview = diamonds_snap_preview_create(&server);
	if (server.dock && server.startmenu) {
		diamonds_startmenu_reposition(server.startmenu, server.dock->x, server.dock->y);
	}

	/* Screencopy protocol for Wayland screenshot tools like grim */
	wlr_screencopy_manager_v1_create(server.wl_display);

	/* IPC control socket at /tmp/diamonds.sock */
	int ipc_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (ipc_fd >= 0) {
		struct sockaddr_un addr = {0};
		addr.sun_family = AF_UNIX;
		strncpy(addr.sun_path, "/tmp/diamonds.sock", sizeof(addr.sun_path) - 1);
		unlink("/tmp/diamonds.sock");
		if (bind(ipc_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0 && listen(ipc_fd, 5) == 0) {
			fcntl(ipc_fd, F_SETFL, O_NONBLOCK);
			wl_event_loop_add_fd(server.event_loop, ipc_fd, WL_EVENT_READABLE, handle_control_ipc, &server);
		}
	}

	/* Clean termination on signals */
	wl_event_loop_add_signal(server.event_loop, SIGINT, handle_signal_term, &server);
	wl_event_loop_add_signal(server.event_loop, SIGTERM, handle_signal_term, &server);

	/* Add Wayland socket */
	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (!socket) {
		wlr_log(WLR_ERROR, "Failed to create Wayland socket");
		return 1;
	}

	/* Start backend */
	if (!wlr_backend_start(server.backend)) {
		wlr_log(WLR_ERROR, "Failed to start wlr_backend");
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, true);
	wlr_log(WLR_INFO, "diamonds-desktop started on WAYLAND_DISPLAY=%s", socket);

	/* Run optional startup client */
	if (startup_cmd) {
		if (fork() == 0) {
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
			_exit(1);
		}
	}

	/* Main event loop */
	wl_display_run(server.wl_display);

	/* Cleanup */
	diamonds_snap_preview_destroy(server.snap_preview);
	diamonds_control_center_destroy(server.control_center);
	diamonds_calendar_destroy(server.calendar);
	diamonds_startmenu_destroy(server.startmenu);
	diamonds_dock_destroy(server.dock);
	diamonds_topbar_destroy(server.topbar);
	diamonds_wallpaper_destroy(server.wallpaper);
	diamonds_sound_cleanup();

#ifdef DIAMONDS_ENABLE_XWAYLAND
	if (server.xwayland) {
		wlr_xwayland_destroy(server.xwayland);
	}
#endif

	wl_display_destroy_clients(server.wl_display);
	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_output_layout_destroy(server.output_layout);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.wl_display);

	unlink("/tmp/diamonds.sock");
	return 0;
}
