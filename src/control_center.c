#define _GNU_SOURCE
#include "control_center.h"
#include "diamonds.h"
#include "snap.h"
#include "topbar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <pixman.h>
#include <pango/pangocairo.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CC_WIDTH  340
#define CC_HEIGHT_DEFAULT 424
#define CC_HEIGHT_EDIT    568
#define FONT_BASE "Segoe UI, Cantarell, DejaVu Sans, Ubuntu, sans-serif"

static void run_cmd_async(const char *cmd) {
	if (!cmd || !*cmd) return;
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		signal(SIGPIPE, SIG_DFL);
		char *args[] = { "/bin/sh", "-c", (char *)cmd, NULL };
		execvp(args[0], args);
		_exit(127);
	}
}

static void apply_volume(int pct) {
	char cmd[256];
	snprintf(cmd, sizeof(cmd),
		"wpctl set-volume @DEFAULT_AUDIO_SINK@ %d%% >/dev/null 2>&1 || "
		"pactl set-sink-volume @DEFAULT_SINK@ %d%% >/dev/null 2>&1 &",
		pct, pct);
	run_cmd_async(cmd);
}

static void apply_brightness(int pct) {
	char cmd[256];
	snprintf(cmd, sizeof(cmd),
		"brightnessctl set %d%% >/dev/null 2>&1 || "
		"(echo $(( %d * 255 / 100 )) | tee /sys/class/backlight/*/brightness) >/dev/null 2>&1 &",
		pct, pct);
	run_cmd_async(cmd);
}

static void draw_wifi_symbol(cairo_t *cr, double cx, double cy, bool active) {
	cairo_save(cr);
	cairo_set_line_width(cr, 1.8);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

	if (active) {
		cairo_set_source_rgba(cr, 0.0, 0.478, 1.0, 1.0); /* #007AFF */
	} else {
		cairo_set_source_rgba(cr, 0.55, 0.58, 0.68, 0.9);
	}

	/* Dot */
	cairo_new_path(cr);
	cairo_arc(cr, cx, cy + 4.5, 1.6, 0.0, 2.0 * M_PI);
	cairo_fill(cr);

	/* Inner wave */
	cairo_new_path(cr);
	cairo_arc(cr, cx, cy + 4.5, 5.0, -M_PI * 0.75, -M_PI * 0.25);
	cairo_stroke(cr);

	/* Outer wave */
	cairo_new_path(cr);
	cairo_arc(cr, cx, cy + 4.5, 9.5, -M_PI * 0.75, -M_PI * 0.25);
	cairo_stroke(cr);

	cairo_restore(cr);
}

static void draw_bluetooth_symbol(cairo_t *cr, double cx, double cy, bool active) {
	cairo_save(cr);
	cairo_set_line_width(cr, 1.8);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

	if (active) {
		cairo_set_source_rgba(cr, 0.0, 0.478, 1.0, 1.0); /* #007AFF */
	} else {
		cairo_set_source_rgba(cr, 0.55, 0.58, 0.68, 0.9);
	}

	cairo_move_to(cr, cx, cy - 9.0);
	cairo_line_to(cr, cx, cy + 9.0);
	cairo_line_to(cr, cx + 4.5, cy + 4.5);
	cairo_line_to(cr, cx - 4.5, cy - 4.5);
	cairo_move_to(cr, cx - 4.5, cy + 4.5);
	cairo_line_to(cr, cx + 4.5, cy - 4.5);
	cairo_line_to(cr, cx, cy - 9.0);
	cairo_stroke(cr);

	cairo_restore(cr);
}

static void draw_airdrop_symbol(cairo_t *cr, double cx, double cy, bool active) {
	cairo_save(cr);
	cairo_set_line_width(cr, 1.6);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

	if (active) {
		cairo_set_source_rgba(cr, 0.0, 0.478, 1.0, 1.0); /* #007AFF */
	} else {
		cairo_set_source_rgba(cr, 0.55, 0.58, 0.68, 0.9);
	}

	/* Concentric broadcast arcs */
	cairo_new_path(cr);
	cairo_arc(cr, cx, cy + 2.0, 3.5, -M_PI * 0.85, -M_PI * 0.15);
	cairo_stroke(cr);

	cairo_new_path(cr);
	cairo_arc(cr, cx, cy + 2.0, 7.0, -M_PI * 0.85, -M_PI * 0.15);
	cairo_stroke(cr);

	cairo_new_path(cr);
	cairo_arc(cr, cx, cy + 2.0, 10.5, -M_PI * 0.85, -M_PI * 0.15);
	cairo_stroke(cr);

	/* Base triangle */
	cairo_move_to(cr, cx - 3.5, cy + 6.0);
	cairo_line_to(cr, cx, cy + 2.5);
	cairo_line_to(cr, cx + 3.5, cy + 6.0);
	cairo_stroke(cr);

	cairo_restore(cr);
}

static void draw_focus_symbol(cairo_t *cr, double cx, double cy, bool active) {
	cairo_save(cr);
	if (active) {
		cairo_set_source_rgba(cr, 0.0, 0.478, 1.0, 1.0); /* #007AFF */
	} else {
		cairo_set_source_rgba(cr, 0.55, 0.58, 0.68, 0.9);
	}

	/* Crescent moon */
	cairo_new_path(cr);
	cairo_arc(cr, cx - 1.0, cy, 7.0, -0.4 * M_PI, 0.4 * M_PI);
	cairo_arc_negative(cr, cx - 4.5, cy, 5.5, 0.35 * M_PI, -0.35 * M_PI);
	cairo_close_path(cr);
	cairo_fill(cr);

	cairo_restore(cr);
}

static void draw_sun_symbol(cairo_t *cr, double cx, double cy, double fill_ratio) {
	cairo_save(cr);
	if (fill_ratio > 0.15) {
		cairo_set_source_rgba(cr, 0.10, 0.12, 0.18, 0.95); /* Contrasted on bright white */
	} else {
		cairo_set_source_rgba(cr, 0.92, 0.94, 0.98, 0.95);
	}
	cairo_set_line_width(cr, 1.4);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

	cairo_arc(cr, cx, cy, 3.5, 0.0, 2.0 * M_PI);
	cairo_fill(cr);

	for (int i = 0; i < 8; i++) {
		double a = (double)i * (M_PI / 4.0);
		cairo_move_to(cr, cx + cos(a) * 5.2, cy + sin(a) * 5.2);
		cairo_line_to(cr, cx + cos(a) * 7.8, cy + sin(a) * 7.8);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

static void draw_speaker_symbol(cairo_t *cr, double cx, double cy, double fill_ratio) {
	cairo_save(cr);
	if (fill_ratio > 0.15) {
		cairo_set_source_rgba(cr, 0.98, 0.98, 1.0, 0.98); /* White against blue fill */
	} else {
		cairo_set_source_rgba(cr, 0.75, 0.78, 0.88, 0.95);
	}
	cairo_set_line_width(cr, 1.4);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

	/* Cone */
	cairo_move_to(cr, cx - 6.0, cy - 2.5);
	cairo_line_to(cr, cx - 3.0, cy - 2.5);
	cairo_line_to(cr, cx + 1.0, cy - 6.5);
	cairo_line_to(cr, cx + 1.0, cy + 6.5);
	cairo_line_to(cr, cx - 3.0, cy + 2.5);
	cairo_line_to(cr, cx - 6.0, cy + 2.5);
	cairo_close_path(cr);
	cairo_fill(cr);

	/* Waves */
	cairo_new_path(cr);
	cairo_arc(cr, cx + 2.0, cy, 4.0, -M_PI * 0.35, M_PI * 0.35);
	cairo_stroke(cr);

	cairo_new_path(cr);
	cairo_arc(cr, cx + 2.0, cy, 7.0, -M_PI * 0.35, M_PI * 0.35);
	cairo_stroke(cr);

	cairo_restore(cr);
}

struct diamonds_control_center *diamonds_control_center_create(struct diamonds_server *server) {
	struct diamonds_control_center *cc = calloc(1, sizeof(*cc));
	if (!cc) return NULL;

	cc->server = server;
	cc->width = CC_WIDTH;
	cc->height = CC_HEIGHT_DEFAULT;
	cc->visible = false;

	cc->wifi_enabled = true;
	cc->bluetooth_enabled = true;
	cc->lanshare_enabled = true;
	cc->focus_enabled = false;

	cc->is_playing = true;
	snprintf(cc->track_title, sizeof(cc->track_title), "Diamonds Audio");
	snprintf(cc->track_artist, sizeof(cc->track_artist), "Wayland High-Res");

	cc->volume_percent = 75;
	cc->brightness_percent = 80;

	cc->edit_mode = false;
	cc->mod_battery = true;
	cc->mod_clock = true;
	cc->mod_connectivity = true;
	cc->mod_display = true;
	cc->mod_sound = true;
	cc->mod_keyboard = true;

	cc->active_drag = CC_DRAG_NONE;

	cc->buffer = diamonds_cairo_buffer_create(cc->width, cc->height);
	if (!cc->buffer) {
		free(cc);
		return NULL;
	}

	cc->scene_buffer = wlr_scene_buffer_create(server->tree_overlay, &cc->buffer->base);
	if (!cc->scene_buffer) {
		wlr_buffer_drop(&cc->buffer->base);
		free(cc);
		return NULL;
	}

	wlr_scene_node_set_enabled(&cc->scene_buffer->node, false);
	diamonds_control_center_reposition(cc, 1920, 1080);
	diamonds_control_center_render(cc);
	return cc;
}

void diamonds_control_center_destroy(struct diamonds_control_center *cc) {
	if (!cc) return;
	if (cc->scene_buffer) {
		wlr_scene_node_destroy(&cc->scene_buffer->node);
	}
	if (cc->buffer) {
		wlr_buffer_drop(&cc->buffer->base);
	}
	free(cc);
}

void diamonds_control_center_set_visible(struct diamonds_control_center *cc, bool visible) {
	if (!cc || !cc->scene_buffer) return;
	cc->visible = visible;
	wlr_scene_node_set_enabled(&cc->scene_buffer->node, visible);
	if (visible) {
		wlr_scene_node_raise_to_top(&cc->scene_buffer->node);
		diamonds_control_center_render(cc);
	}
	if (cc->server && cc->server->topbar) {
		cc->server->topbar->control_center_active = visible;
		diamonds_topbar_render(cc->server->topbar);
	}
}

void diamonds_control_center_toggle(struct diamonds_control_center *cc) {
	if (!cc) return;
	diamonds_control_center_set_visible(cc, !cc->visible);
}

void diamonds_control_center_reposition(struct diamonds_control_center *cc, int32_t output_width, int32_t output_height) {
	(void)output_height;
	if (!cc) return;
	cc->x = output_width - cc->width - 12;
	cc->y = 36;
	if (cc->scene_buffer) {
		wlr_scene_node_set_position(&cc->scene_buffer->node, cc->x, cc->y);
	}
}

void diamonds_control_center_render(struct diamonds_control_center *cc) {
	if (!cc || !cc->buffer || !cc->buffer->cairo_surface) return;

	cairo_t *cr = cairo_create(cc->buffer->cairo_surface);
	if (!cr) return;

	double w = (double)cc->width;
	double h = (double)cc->height;

	/* 1. Clear surface */
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	/* 2. Main Mica glass rounded card */
	diamonds_draw_rounded_rect(cr, 0.5, 0.5, w - 1.0, h - 1.0, 18.0);
	cairo_set_source_rgba(cr, 0.082, 0.086, 0.133, 0.95);
	cairo_fill_preserve(cr);

	/* 1px Mica border #32354a */
	cairo_set_source_rgba(cr, 0.22, 0.25, 0.36, 0.70);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* 3. UPPER-LEFT CLUSTER: 2x2 Grid (Wi-Fi, Bluetooth, AirDrop, Focus)
	 * Card rect: x:12, y:14, w:154, h:146, r:14
	 */
	diamonds_draw_rounded_rect(cr, 12.0, 14.0, 154.0, 146.0, 14.0);
	cairo_set_source_rgba(cr, 0.11, 0.12, 0.18, 0.80);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.19, 0.20, 0.29, 0.50);
	cairo_set_line_width(cr, 0.8);
	cairo_stroke(cr);

	/* --- Toggle 1: Wi-Fi (Top-Left: cx=42, cy=44, r=16) --- */
	cairo_arc(cr, 42.0, 44.0, 16.0, 0.0, 2.0 * M_PI);
	if (cc->wifi_enabled) {
		cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0); /* Pure White */
	} else {
		cairo_set_source_rgba(cr, 0.17, 0.18, 0.26, 0.85); /* Translucent dark gray */
	}
	cairo_fill(cr);
	draw_wifi_symbol(cr, 42.0, 44.0, cc->wifi_enabled);

	/* Wi-Fi Label */
	PangoLayout *layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 8.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, "Wi-Fi", -1);
		cairo_move_to(cr, 22.0, 64.0);
		cairo_set_source_rgba(cr, 0.90, 0.92, 0.96, 1.0);
		pango_cairo_show_layout(cr, layout);

		desc = pango_font_description_from_string(FONT_BASE " 7");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, cc->wifi_enabled ? "Home-5G" : "Off", -1);
		cairo_move_to(cr, 22.0, 77.0);
		cairo_set_source_rgba(cr, 0.50, 0.54, 0.66, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* --- Toggle 2: Bluetooth (Top-Right: cx=120, cy=44, r=16) --- */
	cairo_arc(cr, 120.0, 44.0, 16.0, 0.0, 2.0 * M_PI);
	if (cc->bluetooth_enabled) {
		cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
	} else {
		cairo_set_source_rgba(cr, 0.17, 0.18, 0.26, 0.85);
	}
	cairo_fill(cr);
	draw_bluetooth_symbol(cr, 120.0, 44.0, cc->bluetooth_enabled);

	/* Bluetooth Label */
	layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 8.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, "Bluetooth", -1);
		cairo_move_to(cr, 96.0, 64.0);
		cairo_set_source_rgba(cr, 0.90, 0.92, 0.96, 1.0);
		pango_cairo_show_layout(cr, layout);

		desc = pango_font_description_from_string(FONT_BASE " 7");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, cc->bluetooth_enabled ? "On" : "Off", -1);
		cairo_move_to(cr, 96.0, 77.0);
		cairo_set_source_rgba(cr, 0.50, 0.54, 0.66, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* --- Toggle 3: AirDrop/LanShare (Bottom-Left: cx=42, cy=108, r=16) --- */
	cairo_arc(cr, 42.0, 108.0, 16.0, 0.0, 2.0 * M_PI);
	if (cc->lanshare_enabled) {
		cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
	} else {
		cairo_set_source_rgba(cr, 0.17, 0.18, 0.26, 0.85);
	}
	cairo_fill(cr);
	draw_airdrop_symbol(cr, 42.0, 108.0, cc->lanshare_enabled);

	/* AirDrop Label */
	layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 8.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, "AirDrop", -1);
		cairo_move_to(cr, 22.0, 128.0);
		cairo_set_source_rgba(cr, 0.90, 0.92, 0.96, 1.0);
		pango_cairo_show_layout(cr, layout);

		desc = pango_font_description_from_string(FONT_BASE " 7");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, cc->lanshare_enabled ? "Everyone" : "Off", -1);
		cairo_move_to(cr, 22.0, 141.0);
		cairo_set_source_rgba(cr, 0.50, 0.54, 0.66, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* --- Toggle 4: Focus (Bottom-Right: cx=120, cy=108, r=16) --- */
	cairo_arc(cr, 120.0, 108.0, 16.0, 0.0, 2.0 * M_PI);
	if (cc->focus_enabled) {
		cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
	} else {
		cairo_set_source_rgba(cr, 0.17, 0.18, 0.26, 0.85);
	}
	cairo_fill(cr);
	draw_focus_symbol(cr, 120.0, 108.0, cc->focus_enabled);

	/* Focus Label */
	layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 8.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, "Focus", -1);
		cairo_move_to(cr, 96.0, 128.0);
		cairo_set_source_rgba(cr, 0.90, 0.92, 0.96, 1.0);
		pango_cairo_show_layout(cr, layout);

		desc = pango_font_description_from_string(FONT_BASE " 7");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, cc->focus_enabled ? "On" : "Off", -1);
		cairo_move_to(cr, 96.0, 141.0);
		cairo_set_source_rgba(cr, 0.50, 0.54, 0.66, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* 4. UPPER-RIGHT CLUSTER: Now Playing Card
	 * Card rect: x:174, y:14, w:154, h:146, r:14
	 */
	diamonds_draw_rounded_rect(cr, 174.0, 14.0, 154.0, 146.0, 14.0);
	cairo_set_source_rgba(cr, 0.11, 0.12, 0.18, 0.80);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.19, 0.20, 0.29, 0.50);
	cairo_set_line_width(cr, 0.8);
	cairo_stroke(cr);

	/* Album art box: x:186, y:24, w:38, h:38, r:8 */
	diamonds_draw_rounded_rect(cr, 186.0, 24.0, 38.0, 38.0, 8.0);
	cairo_pattern_t *pat = cairo_pattern_create_linear(186.0, 24.0, 224.0, 62.0);
	cairo_pattern_add_color_stop_rgb(pat, 0.0, 0.388, 0.40, 0.945); /* Indigo */
	cairo_pattern_add_color_stop_rgb(pat, 1.0, 0.658, 0.33, 0.968); /* Purple */
	cairo_set_source(cr, pat);
	cairo_fill(cr);
	cairo_pattern_destroy(pat);

	/* Music note symbol inside album art */
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
	cairo_arc(cr, 200.0, 47.0, 2.5, 0.0, 2.0 * M_PI);
	cairo_fill(cr);
	cairo_arc(cr, 208.0, 44.0, 2.5, 0.0, 2.0 * M_PI);
	cairo_fill(cr);
	cairo_set_line_width(cr, 1.4);
	cairo_move_to(cr, 202.5, 47.0);
	cairo_line_to(cr, 202.5, 33.0);
	cairo_line_to(cr, 210.5, 30.0);
	cairo_line_to(cr, 210.5, 44.0);
	cairo_stroke(cr);

	/* Track Title and Artist */
	layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 8.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, cc->track_title, -1);
		pango_layout_set_width(layout, 82 * PANGO_SCALE);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
		cairo_move_to(cr, 230.0, 26.0);
		cairo_set_source_rgba(cr, 0.94, 0.96, 0.98, 1.0);
		pango_cairo_show_layout(cr, layout);

		desc = pango_font_description_from_string(FONT_BASE " 7.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, cc->track_artist, -1);
		pango_layout_set_width(layout, 82 * PANGO_SCALE);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
		cairo_move_to(cr, 230.0, 42.0);
		cairo_set_source_rgba(cr, 0.50, 0.54, 0.66, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* Waveform / progress line */
	diamonds_draw_rounded_rect(cr, 186.0, 72.0, 130.0, 3.0, 1.5);
	cairo_set_source_rgba(cr, 0.20, 0.22, 0.32, 0.7);
	cairo_fill(cr);
	diamonds_draw_rounded_rect(cr, 186.0, 72.0, 68.0, 3.0, 1.5);
	cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.9);
	cairo_fill(cr);

	/* 3 Media Playback Buttons: Prev, Play/Pause, Next (y: 114) */
	/* 1. Prev (cx=202, cy=114, r=13) */
	cairo_arc(cr, 202.0, 114.0, 13.0, 0.0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.16, 0.17, 0.24, 0.85);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 0.88, 0.90, 0.96, 1.0);
	/* Bar + left triangle */
	cairo_set_line_width(cr, 1.4);
	cairo_move_to(cr, 198.0, 109.0);
	cairo_line_to(cr, 198.0, 119.0);
	cairo_stroke(cr);
	cairo_move_to(cr, 206.0, 109.0);
	cairo_line_to(cr, 199.0, 114.0);
	cairo_line_to(cr, 206.0, 119.0);
	cairo_close_path(cr);
	cairo_fill(cr);

	/* 2. Play/Pause (cx=251, cy=114, r=16) */
	cairo_arc(cr, 251.0, 114.0, 16.0, 0.0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.22, 0.24, 0.35, 0.95);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);

	if (cc->is_playing) {
		/* Two pause bars */
		cairo_rectangle(cr, 247.0, 108.0, 2.5, 12.0);
		cairo_rectangle(cr, 252.5, 108.0, 2.5, 12.0);
		cairo_fill(cr);
	} else {
		/* Play triangle */
		cairo_move_to(cr, 248.0, 107.0);
		cairo_line_to(cr, 256.0, 114.0);
		cairo_line_to(cr, 248.0, 121.0);
		cairo_close_path(cr);
		cairo_fill(cr);
	}

	/* 3. Next (cx=300, cy=114, r=13) */
	cairo_arc(cr, 300.0, 114.0, 13.0, 0.0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.16, 0.17, 0.24, 0.85);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 0.88, 0.90, 0.96, 1.0);
	/* Right triangle + bar */
	cairo_move_to(cr, 296.0, 109.0);
	cairo_line_to(cr, 303.0, 114.0);
	cairo_line_to(cr, 296.0, 119.0);
	cairo_close_path(cr);
	cairo_fill(cr);
	cairo_move_to(cr, 304.0, 109.0);
	cairo_line_to(cr, 304.0, 119.0);
	cairo_stroke(cr);

	/* 5. MIDDLE SECTION: Two Wide Pill Sliders (Display & Sound) */
	/* --- Slider 1: Display (x:12, y:168, w:316, h:64) --- */
	diamonds_draw_rounded_rect(cr, 12.0, 168.0, 316.0, 64.0, 14.0);
	cairo_set_source_rgba(cr, 0.11, 0.12, 0.18, 0.80);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.19, 0.20, 0.29, 0.50);
	cairo_set_line_width(cr, 0.8);
	cairo_stroke(cr);

	layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 8.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, "Display", -1);
		cairo_move_to(cr, 22.0, 175.0);
		cairo_set_source_rgba(cr, 0.88, 0.90, 0.96, 1.0);
		pango_cairo_show_layout(cr, layout);

		char b_str[16];
		snprintf(b_str, sizeof(b_str), "%d%%", cc->brightness_percent);
		pango_layout_set_text(layout, b_str, -1);
		cairo_move_to(cr, 292.0, 175.0);
		cairo_set_source_rgba(cr, 0.50, 0.54, 0.66, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* Display Pill Track: x:22, y:194, w:296, h:28, r:14 */
	diamonds_draw_rounded_rect(cr, 22.0, 194.0, 296.0, 28.0, 14.0);
	cairo_set_source_rgba(cr, 0.16, 0.17, 0.25, 0.90);
	cairo_fill(cr);

	/* Fill bar */
	double b_fill = 296.0 * ((double)cc->brightness_percent / 100.0);
	if (b_fill > 0.0) {
		cairo_save(cr);
		diamonds_draw_rounded_rect(cr, 22.0, 194.0, 296.0, 28.0, 14.0);
		cairo_clip(cr);
		cairo_rectangle(cr, 22.0, 194.0, b_fill, 28.0);
		cairo_set_source_rgba(cr, 0.96, 0.97, 0.99, 0.96); /* macOS pure bright white */
		cairo_fill(cr);
		cairo_restore(cr);
	}
	draw_sun_symbol(cr, 36.0, 208.0, (double)cc->brightness_percent / 100.0);

	/* --- Slider 2: Sound (x:12, y:238, w:316, h:64) --- */
	diamonds_draw_rounded_rect(cr, 12.0, 238.0, 316.0, 64.0, 14.0);
	cairo_set_source_rgba(cr, 0.11, 0.12, 0.18, 0.80);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.19, 0.20, 0.29, 0.50);
	cairo_set_line_width(cr, 0.8);
	cairo_stroke(cr);

	layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 8.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, "Sound", -1);
		cairo_move_to(cr, 22.0, 245.0);
		cairo_set_source_rgba(cr, 0.88, 0.90, 0.96, 1.0);
		pango_cairo_show_layout(cr, layout);

		char v_str[16];
		snprintf(v_str, sizeof(v_str), "%d%%", cc->volume_percent);
		pango_layout_set_text(layout, v_str, -1);
		cairo_move_to(cr, 292.0, 245.0);
		cairo_set_source_rgba(cr, 0.50, 0.54, 0.66, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* Sound Pill Track: x:22, y:264, w:296, h:28, r:14 */
	diamonds_draw_rounded_rect(cr, 22.0, 264.0, 296.0, 28.0, 14.0);
	cairo_set_source_rgba(cr, 0.16, 0.17, 0.25, 0.90);
	cairo_fill(cr);

	/* Fill bar */
	double v_fill = 296.0 * ((double)cc->volume_percent / 100.0);
	if (v_fill > 0.0) {
		cairo_save(cr);
		diamonds_draw_rounded_rect(cr, 22.0, 264.0, 296.0, 28.0, 14.0);
		cairo_clip(cr);
		cairo_rectangle(cr, 22.0, 264.0, v_fill, 28.0);
		cairo_set_source_rgba(cr, 0.0, 0.478, 1.0, 0.96); /* #007AFF macOS Blue */
		cairo_fill(cr);
		cairo_restore(cr);
	}
	draw_speaker_symbol(cr, 36.0, 278.0, (double)cc->volume_percent / 100.0);

	/* 6. BOTTOM ROW: 4 Small Circular Utility Buttons (x:12, y:308, w:316, h:56) */
	diamonds_draw_rounded_rect(cr, 12.0, 308.0, 316.0, 56.0, 14.0);
	cairo_set_source_rgba(cr, 0.11, 0.12, 0.18, 0.80);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.19, 0.20, 0.29, 0.50);
	cairo_set_line_width(cr, 0.8);
	cairo_stroke(cr);

	/* Utility 1: Screenshot (cx=51, cy=336, r=16) */
	cairo_arc(cr, 51.0, 336.0, 16.0, 0.0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.17, 0.18, 0.26, 0.9);
	cairo_fill(cr);
	/* Camera vector */
	cairo_set_source_rgba(cr, 0.88, 0.90, 0.98, 1.0);
	diamonds_draw_rounded_rect(cr, 43.0, 331.0, 16.0, 11.0, 2.0);
	cairo_fill(cr);
	cairo_rectangle(cr, 47.0, 329.0, 8.0, 3.0);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 0.17, 0.18, 0.26, 1.0);
	cairo_arc(cr, 51.0, 336.5, 3.0, 0.0, 2.0 * M_PI);
	cairo_fill(cr);

	/* Utility 2: Calculator (cx=124, cy=336, r=16) */
	cairo_arc(cr, 124.0, 336.0, 16.0, 0.0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.17, 0.18, 0.26, 0.9);
	cairo_fill(cr);
	/* Calculator symbols (+ - = x) */
	cairo_set_source_rgba(cr, 0.88, 0.90, 0.98, 1.0);
	cairo_set_line_width(cr, 1.4);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	/* + */
	cairo_move_to(cr, 120.0, 332.0); cairo_line_to(cr, 120.0, 336.0);
	cairo_move_to(cr, 118.0, 334.0); cairo_line_to(cr, 122.0, 334.0);
	/* - */
	cairo_move_to(cr, 126.0, 334.0); cairo_line_to(cr, 130.0, 334.0);
	/* = */
	cairo_move_to(cr, 126.0, 338.0); cairo_line_to(cr, 130.0, 338.0);
	cairo_move_to(cr, 126.0, 340.5); cairo_line_to(cr, 130.0, 340.5);
	cairo_stroke(cr);

	/* Utility 3: Split Window Left & Right (cx=197, cy=336, r=16) */
	cairo_arc(cr, 197.0, 336.0, 16.0, 0.0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.17, 0.18, 0.26, 0.9);
	cairo_fill(cr);
	/* Split icon */
	diamonds_draw_rounded_rect(cr, 189.0, 329.0, 16.0, 14.0, 2.5);
	cairo_set_source_rgba(cr, 0.88, 0.90, 0.98, 1.0);
	cairo_set_line_width(cr, 1.2);
	cairo_stroke(cr);
	cairo_move_to(cr, 197.0, 329.0);
	cairo_line_to(cr, 197.0, 343.0);
	cairo_stroke(cr);

	/* Utility 4: Timer (cx=270, cy=336, r=16) */
	cairo_arc(cr, 270.0, 336.0, 16.0, 0.0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.17, 0.18, 0.26, 0.9);
	cairo_fill(cr);
	/* Timer clock */
	cairo_set_source_rgba(cr, 0.88, 0.90, 0.98, 1.0);
	cairo_set_line_width(cr, 1.3);
	cairo_arc(cr, 270.0, 337.0, 6.0, 0.0, 2.0 * M_PI);
	cairo_stroke(cr);
	cairo_move_to(cr, 270.0, 337.0); cairo_line_to(cr, 270.0, 333.5);
	cairo_move_to(cr, 270.0, 337.0); cairo_line_to(cr, 273.0, 337.0);
	cairo_move_to(cr, 269.0, 330.0); cairo_line_to(cr, 271.0, 330.0);
	cairo_stroke(cr);

	/* 7. "EDIT CONTROLS" BUTTON (x:12, y:372, w:316, h:32, r:10) */
	diamonds_draw_rounded_rect(cr, 12.0, 372.0, 316.0, 32.0, 10.0);
	cairo_set_source_rgba(cr, 0.14, 0.15, 0.23, 0.90);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.24, 0.26, 0.38, 0.60);
	cairo_set_line_width(cr, 0.8);
	cairo_stroke(cr);

	layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Medium 8.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, cc->edit_mode ? "✕  Close Controls Customization" : "⚙  Edit Controls...", -1);
		pango_layout_set_width(layout, 316 * PANGO_SCALE);
		pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
		cairo_move_to(cr, 12.0, 379.0);
		cairo_set_source_rgba(cr, 0.80, 0.84, 0.94, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* 8. IF EDIT MODE ACTIVE: ALL CONTROLS CUSTOMIZATION DRAWER */
	if (cc->edit_mode) {
		diamonds_draw_rounded_rect(cr, 12.0, 412.0, 316.0, 142.0, 14.0);
		cairo_set_source_rgba(cr, 0.10, 0.11, 0.17, 0.95);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0.22, 0.25, 0.38, 0.60);
		cairo_set_line_width(cr, 0.8);
		cairo_stroke(cr);

		layout = pango_cairo_create_layout(cr);
		if (layout) {
			PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 8.5");
			pango_layout_set_font_description(layout, desc);
			pango_font_description_free(desc);
			pango_layout_set_text(layout, "All Controls (Toggle Modules):", -1);
			cairo_move_to(cr, 22.0, 420.0);
			cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 1.0);
			pango_cairo_show_layout(cr, layout);

			/* 6 modules in 2 columns: Col 0 x:22, Col 1 x:172 */
			struct {
				const char *label;
				bool active;
				double x;
				double y;
			} mods[] = {
				{ "🔋 Battery",      cc->mod_battery,      22.0,  440.0 },
				{ "🕒 Clock & Date",  cc->mod_clock,        172.0, 440.0 },
				{ "📶 Connectivity",  cc->mod_connectivity, 22.0,  474.0 },
				{ "🔆 Display True",  cc->mod_display,      172.0, 474.0 },
				{ "🔊 Sound Spatial", cc->mod_sound,        22.0,  508.0 },
				{ "⌨ Keyboard",      cc->mod_keyboard,     172.0, 508.0 },
			};

			for (size_t m = 0; m < sizeof(mods) / sizeof(mods[0]); m++) {
				diamonds_draw_rounded_rect(cr, mods[m].x, mods[m].y, 126.0, 26.0, 6.0);
				if (mods[m].active) {
					cairo_set_source_rgba(cr, 0.14, 0.22, 0.35, 0.85);
				} else {
					cairo_set_source_rgba(cr, 0.14, 0.15, 0.20, 0.60);
				}
				cairo_fill_preserve(cr);
				cairo_set_source_rgba(cr, 0.25, 0.28, 0.40, 0.40);
				cairo_set_line_width(cr, 0.6);
				cairo_stroke(cr);

				pango_layout_set_text(layout, mods[m].label, -1);
				cairo_move_to(cr, mods[m].x + 8.0, mods[m].y + 5.0);
				if (mods[m].active) {
					cairo_set_source_rgba(cr, 0.94, 0.96, 0.99, 1.0);
				} else {
					cairo_set_source_rgba(cr, 0.50, 0.54, 0.64, 1.0);
				}
				pango_cairo_show_layout(cr, layout);

				/* Toggle indicator dot */
				cairo_arc(cr, mods[m].x + 114.0, mods[m].y + 13.0, 3.5, 0.0, 2.0 * M_PI);
				if (mods[m].active) {
					cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 1.0);
				} else {
					cairo_set_source_rgba(cr, 0.38, 0.40, 0.50, 0.8);
				}
				cairo_fill(cr);
			}
			g_object_unref(layout);
		}
	}

	cairo_destroy(cr);

	/* Damage buffer */
	pixman_region32_t damage;
	pixman_region32_init_rect(&damage, 0, 0, (uint32_t)cc->width, (uint32_t)cc->height);
	wlr_scene_buffer_set_buffer_with_damage(cc->scene_buffer, &cc->buffer->base, &damage);
	pixman_region32_fini(&damage);
}

bool diamonds_control_center_handle_click(struct diamonds_control_center *cc, double lx, double ly) {
	if (!cc || !cc->visible) return false;

	if (lx < cc->x || lx > cc->x + cc->width || ly < cc->y || ly > cc->y + cc->height) {
		diamonds_control_center_set_visible(cc, false);
		return false;
	}

	double rel_x = lx - cc->x;
	double rel_y = ly - cc->y;

	/* 1. Wi-Fi toggle */
	if (rel_x >= 14.0 && rel_x <= 84.0 && rel_y >= 16.0 && rel_y <= 84.0) {
		cc->wifi_enabled = !cc->wifi_enabled;
		if (cc->wifi_enabled) {
			run_cmd_async("nmcli radio wifi on >/dev/null 2>&1 &");
		} else {
			run_cmd_async("nmcli radio wifi off >/dev/null 2>&1 &");
		}
		diamonds_control_center_render(cc);
		return true;
	}

	/* 2. Bluetooth toggle */
	if (rel_x >= 92.0 && rel_x <= 162.0 && rel_y >= 16.0 && rel_y <= 84.0) {
		cc->bluetooth_enabled = !cc->bluetooth_enabled;
		if (cc->bluetooth_enabled) {
			run_cmd_async("rfkill unblock bluetooth >/dev/null 2>&1 || bluetoothctl power on >/dev/null 2>&1 &");
		} else {
			run_cmd_async("rfkill block bluetooth >/dev/null 2>&1 || bluetoothctl power off >/dev/null 2>&1 &");
		}
		diamonds_control_center_render(cc);
		return true;
	}

	/* 3. AirDrop / LanShare toggle */
	if (rel_x >= 14.0 && rel_x <= 84.0 && rel_y >= 88.0 && rel_y <= 156.0) {
		cc->lanshare_enabled = !cc->lanshare_enabled;
		run_cmd_async("notify-send 'AirDrop / LanShare' 'Local network sharing updated' &");
		diamonds_control_center_render(cc);
		return true;
	}

	/* 4. Focus toggle */
	if (rel_x >= 92.0 && rel_x <= 162.0 && rel_y >= 88.0 && rel_y <= 156.0) {
		cc->focus_enabled = !cc->focus_enabled;
		run_cmd_async("makoctl mode -s dnd || dunstctl set-paused toggle || true &");
		diamonds_control_center_render(cc);
		return true;
	}

	/* 5. Now Playing Playback Buttons */
	/* Prev */
	if (rel_x >= 186.0 && rel_x <= 218.0 && rel_y >= 98.0 && rel_y <= 130.0) {
		run_cmd_async("playerctl previous >/dev/null 2>&1 &");
		return true;
	}
	/* Play / Pause */
	if (rel_x >= 232.0 && rel_x <= 270.0 && rel_y >= 96.0 && rel_y <= 132.0) {
		cc->is_playing = !cc->is_playing;
		run_cmd_async("playerctl play-pause >/dev/null 2>&1 &");
		diamonds_control_center_render(cc);
		return true;
	}
	/* Next */
	if (rel_x >= 284.0 && rel_x <= 316.0 && rel_y >= 98.0 && rel_y <= 130.0) {
		run_cmd_async("playerctl next >/dev/null 2>&1 &");
		return true;
	}

	/* 6. Display (Brightness) Slider Track: x:22 to 318, y:194 to 222 */
	if (rel_x >= 22.0 && rel_x <= 318.0 && rel_y >= 194.0 && rel_y <= 222.0) {
		int pct = (int)(((rel_x - 22.0) / 296.0) * 100.0);
		if (pct < 5) pct = 5;
		if (pct > 100) pct = 100;
		cc->brightness_percent = pct;
		cc->active_drag = CC_DRAG_BRIGHTNESS;
		apply_brightness(pct);
		diamonds_control_center_render(cc);
		return true;
	}

	/* 7. Sound (Volume) Slider Track: x:22 to 318, y:264 to 292 */
	if (rel_x >= 22.0 && rel_x <= 318.0 && rel_y >= 264.0 && rel_y <= 292.0) {
		int pct = (int)(((rel_x - 22.0) / 296.0) * 100.0);
		if (pct < 0) pct = 0;
		if (pct > 100) pct = 100;
		cc->volume_percent = pct;
		cc->active_drag = CC_DRAG_VOLUME;
		apply_volume(pct);
		diamonds_control_center_render(cc);
		return true;
	}

	/* 8. 4 Utility Buttons (y: 312 to 364) */
	/* 1. Screenshot */
	if (rel_x >= 32.0 && rel_x <= 70.0 && rel_y >= 318.0 && rel_y <= 356.0) {
		run_cmd_async("grim /tmp/diamonds_screenshot_$(date +%s).png & notify-send 'Screenshot Captured' 'Saved to /tmp' &");
		return true;
	}
	/* 2. Calculator */
	if (rel_x >= 106.0 && rel_x <= 144.0 && rel_y >= 318.0 && rel_y <= 356.0) {
		run_cmd_async("gnome-calculator || galculator || kcalc || xcalc &");
		return true;
	}
	/* 3. Split Window Left & Right */
	if (rel_x >= 178.0 && rel_x <= 216.0 && rel_y >= 318.0 && rel_y <= 356.0) {
		if (cc->server && cc->server->focused_toplevel) {
			struct wlr_output *out = wlr_output_layout_output_at(
				cc->server->output_layout, cc->x, cc->y);
			int32_t ow = out ? out->width : 1920;
			int32_t oh = out ? out->height : 1080;
			diamonds_apply_snap(cc->server->focused_toplevel, SNAP_LEFT_HALF, ow, oh);
		}
		return true;
	}
	/* 4. Timer */
	if (rel_x >= 252.0 && rel_x <= 290.0 && rel_y >= 318.0 && rel_y <= 356.0) {
		run_cmd_async("diamonds-clock || gnome-clocks &");
		return true;
	}

	/* 9. "Edit Controls" Button: x:12 to 328, y:372 to 404 */
	if (rel_x >= 12.0 && rel_x <= 328.0 && rel_y >= 372.0 && rel_y <= 404.0) {
		cc->edit_mode = !cc->edit_mode;
		int new_h = cc->edit_mode ? CC_HEIGHT_EDIT : CC_HEIGHT_DEFAULT;
		if (new_h != cc->height) {
			cc->height = new_h;
			diamonds_cairo_buffer_resize(&cc->buffer, cc->width, cc->height);
			wlr_scene_buffer_set_buffer(cc->scene_buffer, &cc->buffer->base);
		}
		diamonds_control_center_render(cc);
		return true;
	}

	/* 10. Module Toggles in All Controls */
	if (cc->edit_mode && rel_y >= 435.0) {
		if (rel_x >= 22.0 && rel_x <= 148.0 && rel_y >= 440.0 && rel_y <= 466.0) {
			cc->mod_battery = !cc->mod_battery;
		} else if (rel_x >= 172.0 && rel_x <= 298.0 && rel_y >= 440.0 && rel_y <= 466.0) {
			cc->mod_clock = !cc->mod_clock;
		} else if (rel_x >= 22.0 && rel_x <= 148.0 && rel_y >= 474.0 && rel_y <= 500.0) {
			cc->mod_connectivity = !cc->mod_connectivity;
		} else if (rel_x >= 172.0 && rel_x <= 298.0 && rel_y >= 474.0 && rel_y <= 500.0) {
			cc->mod_display = !cc->mod_display;
		} else if (rel_x >= 22.0 && rel_x <= 148.0 && rel_y >= 508.0 && rel_y <= 534.0) {
			cc->mod_sound = !cc->mod_sound;
		} else if (rel_x >= 172.0 && rel_x <= 298.0 && rel_y >= 508.0 && rel_y <= 534.0) {
			cc->mod_keyboard = !cc->mod_keyboard;
		}
		diamonds_control_center_render(cc);
		return true;
	}

	return true;
}

bool diamonds_control_center_handle_motion(struct diamonds_control_center *cc, double lx, double ly) {
	if (!cc || !cc->visible || cc->active_drag == CC_DRAG_NONE) return false;
	(void)ly;

	double rel_x = lx - cc->x;

	if (cc->active_drag == CC_DRAG_BRIGHTNESS) {
		int pct = (int)(((rel_x - 22.0) / 296.0) * 100.0);
		if (pct < 5) pct = 5;
		if (pct > 100) pct = 100;
		if (pct != cc->brightness_percent) {
			cc->brightness_percent = pct;
			apply_brightness(pct);
			diamonds_control_center_render(cc);
		}
		return true;
	}

	if (cc->active_drag == CC_DRAG_VOLUME) {
		int pct = (int)(((rel_x - 22.0) / 296.0) * 100.0);
		if (pct < 0) pct = 0;
		if (pct > 100) pct = 100;
		if (pct != cc->volume_percent) {
			cc->volume_percent = pct;
			apply_volume(pct);
			diamonds_control_center_render(cc);
		}
		return true;
	}

	return false;
}

void diamonds_control_center_handle_button_release(struct diamonds_control_center *cc) {
	if (!cc) return;
	cc->active_drag = CC_DRAG_NONE;
}
