#ifndef DIAMONDS_CONTROL_CENTER_H
#define DIAMONDS_CONTROL_CENTER_H

#include <stdint.h>
#include <stdbool.h>
#include <cairo.h>
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

	/* Quick Setting Toggles: Wired Ethernet, Wireless Wi-Fi, Bluetooth, Night Light, DND, Power */
	bool ethernet_enabled;
	bool ethernet_connected;
	char ethernet_iface[32];
	char ethernet_ip[48];

	bool wifi_enabled;
	bool wifi_connected;
	char wifi_ssid[64];
	char wifi_ip[48];

	bool bluetooth_enabled;
	bool nightlight_enabled;
	bool focus_enabled;
	bool perf_mode_high;

	/* Sliders: Brightness & Volume */
	int volume_percent;     /* 0 - 100 */
	int brightness_percent; /* 0 - 100 */

	/* Drag tracking */
	enum {
		CC_DRAG_NONE = 0,
		CC_DRAG_VOLUME,
		CC_DRAG_BRIGHTNESS,
	} active_drag;
};

/* Core Control Center API */
struct diamonds_control_center *diamonds_control_center_create(struct diamonds_server *server);
void diamonds_control_center_destroy(struct diamonds_control_center *cc);
void diamonds_control_center_toggle(struct diamonds_control_center *cc);
void diamonds_control_center_toggle_at(struct diamonds_control_center *cc, int32_t screen_width, int32_t topbar_height);
void diamonds_control_center_set_visible(struct diamonds_control_center *cc, bool visible);
void diamonds_control_center_reposition(struct diamonds_control_center *cc, int32_t output_width, int32_t output_height);
void diamonds_control_center_render(struct diamonds_control_center *cc);
void diamonds_control_center_render_to(struct diamonds_control_center *cc, cairo_t *cr);
bool diamonds_control_center_handle_click(struct diamonds_control_center *cc, double lx, double ly);
bool diamonds_control_center_handle_motion(struct diamonds_control_center *cc, double lx, double ly);
void diamonds_control_center_handle_button_release(struct diamonds_control_center *cc);

/* Direct Global Compositor Connection Wrappers */
void control_center_toggle(int32_t screen_width, int32_t topbar_height);
bool control_center_handle_click(double cur_x, double cur_y);
void control_center_render(cairo_t *cr);

#endif /* DIAMONDS_CONTROL_CENTER_H */
