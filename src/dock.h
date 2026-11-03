#ifndef DIAMONDS_DOCK_H
#define DIAMONDS_DOCK_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <cairo.h>
#include <wlr/types/wlr_scene.h>
#include "cairo_buffer.h"

struct diamonds_server;
struct diamonds_toplevel;
#ifdef DIAMONDS_ENABLE_XWAYLAND
struct diamonds_xwayland_surface;
#endif

/* Right-click popup context menu */
struct diamonds_dock_popup {
	struct diamonds_dock *dock;
	struct wlr_scene_buffer *scene_buffer;
	struct diamonds_cairo_buffer *buffer;
	bool visible;
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;

	struct diamonds_toplevel *target_toplevel;
#ifdef DIAMONDS_ENABLE_XWAYLAND
	struct diamonds_xwayland_surface *target_xsurface;
#endif
	pid_t target_pid;
	char target_name[64];
};

/* Item on the Diamonds Dock */
struct diamonds_dock_item {
	const char *name;
	const char *app_id;
	const char *exec_cmd;
	bool is_running;
	bool is_focused;
};

struct diamonds_dock {
	struct diamonds_server *server;
	struct wlr_scene_buffer *scene_buffer;
	struct diamonds_cairo_buffer *buffer;

	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	int32_t margin_bottom;
	int32_t corner_radius;
	bool is_full_width;

	struct diamonds_dock_popup popup;
};

struct diamonds_dock *diamonds_dock_create(struct diamonds_server *server);
void diamonds_dock_destroy(struct diamonds_dock *dock);
void diamonds_dock_reposition(struct diamonds_dock *dock, int32_t output_width, int32_t output_height);
void diamonds_dock_render(struct diamonds_dock *dock);
bool diamonds_dock_handle_click(struct diamonds_dock *dock, double lx, double ly, uint32_t button, bool *out_start_menu_toggled);
void diamonds_dock_popup_hide(struct diamonds_dock *dock);
void diamonds_dock_popup_show(struct diamonds_dock *dock, int32_t x, int32_t y,
	struct diamonds_toplevel *toplevel, struct diamonds_xwayland_surface *xsurface,
	pid_t pid, const char *name);

#endif /* DIAMONDS_DOCK_H */
