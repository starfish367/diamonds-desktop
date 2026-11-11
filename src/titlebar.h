#ifndef DIAMONDS_TITLEBAR_H
#define DIAMONDS_TITLEBAR_H

#include <stdint.h>
#include <stdbool.h>
#include "diamonds.h"

#define TITLEBAR_HEIGHT 28

typedef enum {
	TITLEBAR_CLICK_NONE,
	TITLEBAR_CLICK_MOVE,
	TITLEBAR_CLICK_MINIMIZE,
	TITLEBAR_CLICK_MAXIMIZE,
	TITLEBAR_CLICK_CLOSE,
} diamonds_titlebar_action_t;

void diamonds_toplevel_create_titlebar(struct diamonds_toplevel *toplevel);
void diamonds_toplevel_update_titlebar(struct diamonds_toplevel *toplevel);
void diamonds_toplevel_destroy_titlebar(struct diamonds_toplevel *toplevel);
diamonds_titlebar_action_t diamonds_titlebar_handle_click(
	struct diamonds_toplevel *toplevel, double rel_x, double rel_y);

#ifdef DIAMONDS_ENABLE_XWAYLAND
void diamonds_xwayland_surface_create_titlebar(struct diamonds_xwayland_surface *xsurface);
void diamonds_xwayland_surface_update_titlebar(struct diamonds_xwayland_surface *xsurface);
void diamonds_xwayland_surface_destroy_titlebar(struct diamonds_xwayland_surface *xsurface);
diamonds_titlebar_action_t diamonds_xwayland_titlebar_handle_click(
	struct diamonds_xwayland_surface *xsurface, double rel_x, double rel_y);
#endif

#endif /* DIAMONDS_TITLEBAR_H */
