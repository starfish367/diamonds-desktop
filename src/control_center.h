#ifndef DIAMONDS_CONTROL_CENTER_H
#define DIAMONDS_CONTROL_CENTER_H

#include <stdint.h>
#include <stdbool.h>
#include <wlr/types/wlr_scene.h>
#include "cairo_buffer.h"

struct diamonds_server;

struct diamonds_control_center {
	struct diamonds_server *server;
	struct wlr_scene_buffer *scene_buffer;
	struct diamonds_cairo_buffer *buffer;

	bool visible;
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;

	/* Top 4 circular toggles */
	bool wifi_enabled;
	bool bluetooth_enabled;
	bool lanshare_enabled;
	bool focus_enabled;

	/* Top right Now Playing */
	bool is_playing;
	char track_title[64];
	char track_artist[64];

	/* Middle 2 wide pill sliders */
	int volume_percent;     /* 0 - 100 */
	int brightness_percent; /* 0 - 100 */

	/* Edit mode & All Controls toggles */
	bool edit_mode;
	bool mod_battery;
	bool mod_clock;
	bool mod_connectivity;
	bool mod_display;
	bool mod_sound;
	bool mod_keyboard;

	/* Dragging tracking */
	enum {
		CC_DRAG_NONE = 0,
		CC_DRAG_VOLUME,
		CC_DRAG_BRIGHTNESS,
	} active_drag;
};

struct diamonds_control_center *diamonds_control_center_create(struct diamonds_server *server);
void diamonds_control_center_destroy(struct diamonds_control_center *cc);
void diamonds_control_center_toggle(struct diamonds_control_center *cc);
void diamonds_control_center_set_visible(struct diamonds_control_center *cc, bool visible);
void diamonds_control_center_reposition(struct diamonds_control_center *cc, int32_t output_width, int32_t output_height);
void diamonds_control_center_render(struct diamonds_control_center *cc);
bool diamonds_control_center_handle_click(struct diamonds_control_center *cc, double lx, double ly);
bool diamonds_control_center_handle_motion(struct diamonds_control_center *cc, double lx, double ly);
void diamonds_control_center_handle_button_release(struct diamonds_control_center *cc);

#endif /* DIAMONDS_CONTROL_CENTER_H */
