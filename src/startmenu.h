#ifndef DIAMONDS_STARTMENU_H
#define DIAMONDS_STARTMENU_H

#include <stdint.h>
#include <stdbool.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon.h>
#include "cairo_buffer.h"

struct diamonds_server;

#define MAX_STARTMENU_APPS 512

struct diamonds_desktop_entry {
	char name[96];
	char exec[256];
	char icon[128];
};

struct diamonds_startmenu {
	struct diamonds_server *server;
	struct wlr_scene_buffer *scene_buffer;
	struct diamonds_cairo_buffer *buffer;

	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	bool visible;

	/* All scanned applications from /usr/share/applications and ~/.local/share/applications */
	struct diamonds_desktop_entry apps[MAX_STARTMENU_APPS];
	int app_count;

	/* Pinned applications (frequently used) */
	int pinned_indices[12];
	int pinned_count;

	/* Real-time Search and Filter */
	char search_query[128];
	int search_len;
	int filtered_indices[MAX_STARTMENU_APPS];
	int filtered_count;

	/* Hover state */
	int hover_app_idx;     /* Row in filtered list currently hovered (-1 for none) */
	int hover_pinned_idx;  /* Index in pinned grid currently hovered (-1 for none) */
	int hover_power_btn;   /* 1: Lock, 2: Reboot, 3: Shutdown (0 for none) */

	/* Vertical scrolling for left app list */
	double scroll_offset;
	double max_scroll;

	/* User info */
	char username[64];
};

struct diamonds_startmenu *diamonds_startmenu_create(struct diamonds_server *server);
void diamonds_startmenu_destroy(struct diamonds_startmenu *menu);
void diamonds_startmenu_reposition(struct diamonds_startmenu *menu, int32_t dock_x, int32_t dock_y);
void diamonds_startmenu_render(struct diamonds_startmenu *menu);
void diamonds_startmenu_toggle(struct diamonds_startmenu *menu);
void diamonds_startmenu_set_visible(struct diamonds_startmenu *menu, bool visible);
bool diamonds_startmenu_handle_click(struct diamonds_startmenu *menu, double lx, double ly);
bool diamonds_startmenu_handle_motion(struct diamonds_startmenu *menu, double lx, double ly);
bool diamonds_startmenu_handle_key_event(struct diamonds_startmenu *menu,
		struct xkb_state *xkb_state, uint32_t keycode,
		const xkb_keysym_t *syms, int nsyms);
bool diamonds_startmenu_handle_key(struct diamonds_startmenu *menu, xkb_keysym_t sym, enum wl_keyboard_key_state state);
bool diamonds_startmenu_handle_scroll(struct diamonds_startmenu *menu, double lx, double ly, double delta);

#endif /* DIAMONDS_STARTMENU_H */
