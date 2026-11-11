#ifndef DIAMONDS_H
#define DIAMONDS_H

#ifndef WLR_USE_UNSTABLE
#define WLR_USE_UNSTABLE
#endif
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/util/log.h>
#include <wlr/util/edges.h>

#ifdef DIAMONDS_ENABLE_XWAYLAND
#include <wlr/xwayland.h>
#endif

#define DIAMONDS_NUM_WORKSPACES 4

struct diamonds_dock;
struct diamonds_topbar;
struct diamonds_startmenu;
struct diamonds_snap_preview;
struct diamonds_wallpaper;
struct diamonds_calendar;
struct diamonds_control_center;
struct diamonds_toplevel;
struct diamonds_xwayland_surface;

enum diamonds_cursor_mode {
	DIAMONDS_CURSOR_PASSTHROUGH,
	DIAMONDS_CURSOR_MOVE,
	DIAMONDS_CURSOR_RESIZE,
};

struct diamonds_server {
	struct wl_display *wl_display;
	struct wl_event_loop *event_loop;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;

	/* Scene graph layers */
	struct wlr_scene_tree *tree_background;
	struct wlr_scene_tree *tree_windows;
	struct wlr_scene_tree *tree_workspaces[DIAMONDS_NUM_WORKSPACES];
	int current_workspace;
	struct wlr_scene_tree *tree_overlay;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_surface;
	struct wl_list toplevels;

#ifdef DIAMONDS_ENABLE_XWAYLAND
	struct wlr_xwayland *xwayland;
	struct wl_listener new_xwayland_surface;
	struct wl_list xwayland_surfaces;
#endif

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	char current_cursor[64];
	struct wlr_scene_rect *background_rect;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;

	struct diamonds_dock *dock;
	struct diamonds_topbar *topbar;
	struct diamonds_startmenu *startmenu;
	struct diamonds_snap_preview *snap_preview;
	struct diamonds_wallpaper *wallpaper;
	struct diamonds_calendar *calendar;
	struct diamonds_control_center *control_center;
	struct diamonds_toplevel *focused_toplevel;

	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
	struct wl_listener new_toplevel_decoration;

	enum diamonds_cursor_mode cursor_mode;
	struct diamonds_toplevel *grabbed_toplevel;
	struct diamonds_xwayland_surface *grabbed_xsurface;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;
};

struct diamonds_output {
	struct wl_list link;
	struct diamonds_server *server;
	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

struct diamonds_toplevel {
	struct wl_list link;
	struct diamonds_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	struct wlr_scene_tree *xdg_tree;
	int workspace;
	bool prefers_ssd;

	/* SSD Titlebar */
	struct wlr_scene_buffer *titlebar_scene_buffer;
	struct diamonds_cairo_buffer *titlebar_buffer;
	int32_t titlebar_width;
	bool is_maximized;
	bool is_fullscreen;
	int32_t saved_x, saved_y, saved_w, saved_h;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
};

#ifdef DIAMONDS_ENABLE_XWAYLAND
struct diamonds_xwayland_surface {
	struct wl_list link;
	struct diamonds_server *server;
	struct wlr_xwayland_surface *xsurface;
	struct wlr_scene_tree *scene_tree;
	struct wlr_scene_tree *surface_tree;
	int workspace;
	bool is_maximized;
	int32_t saved_x, saved_y, saved_w, saved_h;

	/* SSD Titlebar */
	struct wlr_scene_buffer *titlebar_scene_buffer;
	struct diamonds_cairo_buffer *titlebar_buffer;
	int32_t titlebar_width;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener request_configure;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_title;
	struct wl_listener destroy;
};
#endif

struct diamonds_keyboard {
	struct wl_list link;
	struct diamonds_server *server;
	struct wlr_keyboard *wlr_keyboard;
	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

void diamonds_focus_toplevel(struct diamonds_server *server, struct diamonds_toplevel *toplevel);
void diamonds_update_dock_position(struct diamonds_server *server);
void diamonds_window_close(struct diamonds_toplevel *toplevel, struct diamonds_xwayland_surface *xsurface);
void diamonds_window_toggle_maximize(struct diamonds_toplevel *toplevel, struct diamonds_xwayland_surface *xsurface);
void diamonds_window_minimize(struct diamonds_toplevel *toplevel, struct diamonds_xwayland_surface *xsurface);
void diamonds_toplevel_set_fullscreen(struct diamonds_toplevel *toplevel, bool fullscreen);
void diamonds_capture_screen(struct diamonds_server *server, const char *filepath);
void launch_app(const char *cmd);
void launch_terminal(void);
void launch_files(void);
void launch_app_menu(struct diamonds_server *server);
void diamonds_server_switch_workspace(struct diamonds_server *server, int new_ws);

#endif /* DIAMONDS_H */
