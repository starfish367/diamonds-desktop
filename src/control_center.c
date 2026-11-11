#define _GNU_SOURCE
#include "control_center.h"
#include "diamonds.h"
#include "topbar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <pixman.h>
#include <pango/pangocairo.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CC_WIDTH  340
#define CC_HEIGHT 338
#define FONT_BASE "Segoe UI, Cantarell, DejaVu Sans, Ubuntu, sans-serif"

static struct diamonds_control_center *g_control_center = NULL;

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

/* Vector Icon: Wired Ethernet (RJ45 LAN port / network nodes) */
static void draw_ethernet_symbol(cairo_t *cr, double cx, double cy, bool active) {
	cairo_save(cr);
	cairo_set_line_width(cr, 1.5);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

	if (active) {
		cairo_set_source_rgba(cr, 0.06, 0.08, 0.12, 0.95);
	} else {
		cairo_set_source_rgba(cr, 0.72, 0.78, 0.88, 0.90);
	}

	/* Top monitor/router box */
	diamonds_draw_rounded_rect(cr, cx - 3.5, cy - 7.5, 7.0, 4.5, 1.2);
	cairo_stroke(cr);

	/* Horizontal bus line & vertical stems */
	cairo_move_to(cr, cx, cy - 3.0);
	cairo_line_to(cr, cx, cy + 0.5);
	cairo_move_to(cr, cx - 5.5, cy + 0.5);
	cairo_line_to(cr, cx + 5.5, cy + 0.5);
	cairo_move_to(cr, cx - 5.0, cy + 0.5);
	cairo_line_to(cr, cx - 5.0, cy + 3.0);
	cairo_move_to(cr, cx + 5.0, cy + 0.5);
	cairo_line_to(cr, cx + 5.0, cy + 3.0);
	cairo_stroke(cr);

	/* Bottom two node boxes */
	diamonds_draw_rounded_rect(cr, cx - 7.5, cy + 3.0, 5.0, 4.0, 1.0);
	cairo_stroke(cr);
	diamonds_draw_rounded_rect(cr, cx + 2.5, cy + 3.0, 5.0, 4.0, 1.0);
	cairo_stroke(cr);

	cairo_restore(cr);
}

/* Vector Icon: Wireless Wi-Fi */
static void draw_wifi_symbol(cairo_t *cr, double cx, double cy, bool active) {
	cairo_save(cr);
	cairo_set_line_width(cr, 1.7);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

	if (active) {
		cairo_set_source_rgba(cr, 0.06, 0.08, 0.12, 0.95);
	} else {
		cairo_set_source_rgba(cr, 0.72, 0.78, 0.88, 0.90);
	}

	cairo_new_path(cr);
	cairo_arc(cr, cx, cy + 4.5, 1.5, 0.0, 2.0 * M_PI);
	cairo_fill(cr);

	cairo_new_path(cr);
	cairo_arc(cr, cx, cy + 4.5, 4.6, -M_PI * 0.76, -M_PI * 0.24);
	cairo_stroke(cr);

	cairo_new_path(cr);
	cairo_arc(cr, cx, cy + 4.5, 8.2, -M_PI * 0.76, -M_PI * 0.24);
	cairo_stroke(cr);

	cairo_restore(cr);
}

static void draw_bluetooth_symbol(cairo_t *cr, double cx, double cy, bool active) {
	cairo_save(cr);
	cairo_set_line_width(cr, 1.6);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

	if (active) {
		cairo_set_source_rgba(cr, 0.06, 0.08, 0.12, 0.95);
	} else {
		cairo_set_source_rgba(cr, 0.72, 0.78, 0.88, 0.90);
	}

	cairo_move_to(cr, cx, cy - 7.0);
	cairo_line_to(cr, cx, cy + 7.0);
	cairo_line_to(cr, cx + 4.0, cy + 3.5);
	cairo_line_to(cr, cx - 4.0, cy - 3.5);
	cairo_move_to(cr, cx - 4.0, cy + 3.5);
	cairo_line_to(cr, cx + 4.0, cy - 3.5);
	cairo_line_to(cr, cx, cy - 7.0);
	cairo_stroke(cr);
	cairo_restore(cr);
}

static void draw_night_light_symbol(cairo_t *cr, double cx, double cy, bool active) {
	cairo_save(cr);
	if (active) {
		cairo_set_source_rgba(cr, 0.06, 0.08, 0.12, 0.95);
	} else {
		cairo_set_source_rgba(cr, 0.72, 0.78, 0.88, 0.90);
	}

	cairo_new_path(cr);
	cairo_arc(cr, cx - 1.0, cy, 6.5, -M_PI * 0.5, M_PI * 0.5);
	cairo_arc_negative(cr, cx + 1.5, cy, 4.6, M_PI * 0.5, -M_PI * 0.5);
	cairo_close_path(cr);
	cairo_fill(cr);

	cairo_restore(cr);
}

static void draw_focus_symbol(cairo_t *cr, double cx, double cy, bool active) {
	cairo_save(cr);
	if (active) {
		cairo_set_source_rgba(cr, 0.06, 0.08, 0.12, 0.95);
	} else {
		cairo_set_source_rgba(cr, 0.72, 0.78, 0.88, 0.90);
	}

	cairo_set_line_width(cr, 1.6);
	cairo_arc(cr, cx, cy, 6.5, 0.0, 2.0 * M_PI);
	cairo_stroke(cr);

	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_move_to(cr, cx - 3.2, cy);
	cairo_line_to(cr, cx + 3.2, cy);
	cairo_stroke(cr);

	cairo_restore(cr);
}

static void draw_perf_symbol(cairo_t *cr, double cx, double cy, bool active) {
	cairo_save(cr);
	if (active) {
		cairo_set_source_rgba(cr, 0.06, 0.08, 0.12, 0.95);
	} else {
		cairo_set_source_rgba(cr, 0.72, 0.78, 0.88, 0.90);
	}

	/* Lightning bolt */
	cairo_move_to(cr, cx + 1.0, cy - 7.0);
	cairo_line_to(cr, cx - 4.5, cy + 0.5);
	cairo_line_to(cr, cx - 0.5, cy + 0.5);
	cairo_line_to(cr, cx - 1.5, cy + 7.0);
	cairo_line_to(cr, cx + 4.5, cy - 0.8);
	cairo_line_to(cr, cx + 0.5, cy - 0.8);
	cairo_close_path(cr);
	cairo_fill(cr);

	cairo_restore(cr);
}

static void draw_sun_symbol(cairo_t *cr, double cx, double cy) {
	cairo_save(cr);
	cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 1.0);
	cairo_arc(cr, cx, cy, 3.2, 0.0, 2.0 * M_PI);
	cairo_fill(cr);

	cairo_set_line_width(cr, 1.3);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	for (int i = 0; i < 8; i++) {
		double ang = (double)i * (M_PI / 4.0);
		cairo_move_to(cr, cx + 4.8 * cos(ang), cy + 4.8 * sin(ang));
		cairo_line_to(cr, cx + 6.8 * cos(ang), cy + 6.8 * sin(ang));
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

static void draw_speaker_symbol(cairo_t *cr, double cx, double cy) {
	cairo_save(cr);
	cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 1.0);

	cairo_move_to(cr, cx - 5.5, cy - 2.5);
	cairo_line_to(cr, cx - 3.0, cy - 2.5);
	cairo_line_to(cr, cx - 0.5, cy - 5.0);
	cairo_line_to(cr, cx - 0.5, cy + 5.0);
	cairo_line_to(cr, cx - 3.0, cy + 2.5);
	cairo_line_to(cr, cx - 5.5, cy + 2.5);
	cairo_close_path(cr);
	cairo_fill(cr);

	cairo_set_line_width(cr, 1.3);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_arc(cr, cx - 0.5, cy, 3.5, -M_PI * 0.3, M_PI * 0.3);
	cairo_stroke(cr);
	cairo_arc(cr, cx - 0.5, cy, 6.0, -M_PI * 0.3, M_PI * 0.3);
	cairo_stroke(cr);

	cairo_restore(cr);
}

static void draw_gear_symbol(cairo_t *cr, double cx, double cy) {
	cairo_save(cr);
	cairo_set_source_rgba(cr, 0.85, 0.90, 0.96, 0.95);
	cairo_set_line_width(cr, 1.5);
	cairo_arc(cr, cx, cy, 3.2, 0.0, 2.0 * M_PI);
	cairo_stroke(cr);
	for (int i = 0; i < 6; i++) {
		double a = i * (M_PI / 3.0);
		cairo_move_to(cr, cx + 3.5 * cos(a), cy + 3.5 * sin(a));
		cairo_line_to(cr, cx + 5.6 * cos(a), cy + 5.6 * sin(a));
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

static void update_network_info(struct diamonds_control_center *cc) {
	if (!cc) return;

	/* 1. Detect Wired Ethernet (eth*, enp*, eno*, ens*) */
	cc->ethernet_connected = false;
	snprintf(cc->ethernet_iface, sizeof(cc->ethernet_iface), "eth0");
	cc->ethernet_ip[0] = '\0';

	DIR *d = opendir("/sys/class/net");
	if (d) {
		struct dirent *ent;
		while ((ent = readdir(d)) != NULL) {
			const char *name = ent->d_name;
			if (strncmp(name, "eth", 3) == 0 ||
			    strncmp(name, "enp", 3) == 0 ||
			    strncmp(name, "eno", 3) == 0 ||
			    strncmp(name, "ens", 3) == 0) {
				snprintf(cc->ethernet_iface, sizeof(cc->ethernet_iface), "%.31s", name);
				char oper_path[512];
				snprintf(oper_path, sizeof(oper_path), "/sys/class/net/%s/operstate", name);
				FILE *fp = fopen(oper_path, "r");
				if (fp) {
					char st[32] = {0};
					if (fgets(st, sizeof(st), fp)) {
						if (strncmp(st, "up", 2) == 0) {
							cc->ethernet_connected = true;
							cc->ethernet_enabled = true;
						}
					}
					fclose(fp);
				}
				char carrier_path[512];
				snprintf(carrier_path, sizeof(carrier_path), "/sys/class/net/%s/carrier", name);
				fp = fopen(carrier_path, "r");
				if (fp) {
					char ca[16] = {0};
					if (fgets(ca, sizeof(ca), fp) && ca[0] == '1') {
						cc->ethernet_connected = true;
						cc->ethernet_enabled = true;
					}
					fclose(fp);
				}
				break;
			}
		}
		closedir(d);
	}

	/* Read IPv4 addresses for Ethernet & Wi-Fi via ip -4 -br addr */
	FILE *ip_fp = popen("ip -4 -br addr 2>/dev/null", "r");
	if (ip_fp) {
		char line[256];
		while (fgets(line, sizeof(line), ip_fp)) {
			char iface[64] = {0}, state[64] = {0}, cidr[64] = {0};
			if (sscanf(line, "%63s %63s %63s", iface, state, cidr) >= 2) {
				char *slash = strchr(cidr, '/');
				if (slash) *slash = '\0';
				if (strncmp(iface, "eth", 3) == 0 || strncmp(iface, "en", 2) == 0) {
					snprintf(cc->ethernet_iface, sizeof(cc->ethernet_iface), "%s", iface);
					if (strcasecmp(state, "UP") == 0 || cidr[0]) {
						cc->ethernet_connected = true;
						cc->ethernet_enabled = true;
						if (cidr[0]) {
							snprintf(cc->ethernet_ip, sizeof(cc->ethernet_ip), "%s", cidr);
						}
					}
				} else if (strncmp(iface, "wl", 2) == 0) {
					if (strcasecmp(state, "UP") == 0 || cidr[0]) {
						cc->wifi_connected = true;
						if (cidr[0]) {
							snprintf(cc->wifi_ip, sizeof(cc->wifi_ip), "%s", cidr);
						}
					}
				}
			}
		}
		pclose(ip_fp);
	}

	/* 2. Detect Wireless Wi-Fi SSID */
	if (!cc->wifi_enabled) {
		cc->wifi_connected = false;
		snprintf(cc->wifi_ssid, sizeof(cc->wifi_ssid), "Đã tắt");
		return;
	}

	FILE *fp = popen("nmcli -t -f active,ssid dev wifi 2>/dev/null | grep '^yes:' | head -n1 | cut -d: -f2-", "r");
	if (fp) {
		char buf[64] = {0};
		if (fgets(buf, sizeof(buf), fp)) {
			buf[strcspn(buf, "\r\n")] = '\0';
			if (strlen(buf) > 0) {
				snprintf(cc->wifi_ssid, sizeof(cc->wifi_ssid), "%s", buf);
				cc->wifi_connected = true;
				pclose(fp);
				return;
			}
		}
		pclose(fp);
	}

	/* Fallback: nmcli dev status for wifi connection name */
	fp = popen("nmcli -t -f TYPE,STATE,CONNECTION dev status 2>/dev/null | grep '^wifi:connected:' | head -n1 | cut -d: -f3-", "r");
	if (fp) {
		char buf[64] = {0};
		if (fgets(buf, sizeof(buf), fp)) {
			buf[strcspn(buf, "\r\n")] = '\0';
			if (strlen(buf) > 0) {
				snprintf(cc->wifi_ssid, sizeof(cc->wifi_ssid), "%s", buf);
				cc->wifi_connected = true;
				pclose(fp);
				return;
			}
		}
		pclose(fp);
	}

	if (cc->wifi_connected && cc->wifi_ip[0]) {
		snprintf(cc->wifi_ssid, sizeof(cc->wifi_ssid), "%s", cc->wifi_ip);
	} else {
		snprintf(cc->wifi_ssid, sizeof(cc->wifi_ssid), "Chưa kết nối");
	}
}

struct diamonds_control_center *diamonds_control_center_create(struct diamonds_server *server) {
	struct diamonds_control_center *cc = calloc(1, sizeof(*cc));
	if (!cc) return NULL;

	cc->server = server;
	cc->width = CC_WIDTH;
	cc->height = CC_HEIGHT;
	cc->visible = false;

	cc->ethernet_enabled = true;
	cc->ethernet_connected = true;
	cc->wifi_enabled = true;
	cc->wifi_connected = false;
	cc->bluetooth_enabled = true;
	cc->nightlight_enabled = false;
	cc->focus_enabled = false;
	cc->perf_mode_high = false;
	update_network_info(cc);

	cc->volume_percent = 75;
	cc->brightness_percent = 85;
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
	diamonds_control_center_reposition(cc, 1366, 768);
	diamonds_control_center_render(cc);

	g_control_center = cc;
	return cc;
}

void diamonds_control_center_destroy(struct diamonds_control_center *cc) {
	if (!cc) return;
	if (g_control_center == cc) {
		g_control_center = NULL;
	}
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
		update_network_info(cc);
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

void diamonds_control_center_toggle_at(struct diamonds_control_center *cc, int32_t screen_width, int32_t topbar_height) {
	if (!cc) return;
	cc->x = screen_width - cc->width - 10;
	cc->y = topbar_height + 6;
	if (cc->scene_buffer) {
		wlr_scene_node_set_position(&cc->scene_buffer->node, cc->x, cc->y);
	}
	diamonds_control_center_toggle(cc);
}

void diamonds_control_center_reposition(struct diamonds_control_center *cc, int32_t output_width, int32_t output_height) {
	if (!cc || !cc->scene_buffer) return;
	(void)output_height;
	cc->x = output_width - cc->width - 10;
	cc->y = 36;
	wlr_scene_node_set_position(&cc->scene_buffer->node, cc->x, cc->y);
}

static void draw_slider_row(cairo_t *cr, PangoLayout *layout,
		double y, const char *label, int pct, bool is_brightness) {
	if (is_brightness) {
		draw_sun_symbol(cr, 28.0, y + 7.0);
	} else {
		draw_speaker_symbol(cr, 28.0, y + 7.0);
	}

	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 8");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);

		pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
		pango_layout_set_width(layout, 150 * PANGO_SCALE);
		pango_layout_set_text(layout, label, -1);
		cairo_move_to(cr, 42.0, y);
		cairo_set_source_rgba(cr, 0.93, 0.96, 1.0, 0.98);
		pango_cairo_show_layout(cr, layout);

		char p_str[16];
		snprintf(p_str, sizeof(p_str), "%d%%", pct);
		pango_layout_set_alignment(layout, PANGO_ALIGN_RIGHT);
		pango_layout_set_width(layout, 60 * PANGO_SCALE);
		pango_layout_set_text(layout, p_str, -1);
		cairo_move_to(cr, 254.0, y);
		cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.98);
		pango_cairo_show_layout(cr, layout);

		/* Reset alignment to LEFT */
		pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
	}

	/* Track: x=24, y+17, w=292, h=12, r=6 */
	double tx = 24.0;
	double ty = y + 17.0;
	double tw = 292.0;
	double th = 12.0;

	diamonds_draw_rounded_rect(cr, tx, ty, tw, th, 6.0);
	cairo_set_source_rgba(cr, 0.05, 0.06, 0.10, 0.85);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.10);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	double fill_w = tw * ((double)pct / 100.0);
	if (fill_w < 8.0 && pct > 0) fill_w = 8.0;
	if (fill_w > 0.0) {
		cairo_save(cr);
		diamonds_draw_rounded_rect(cr, tx, ty, tw, th, 6.0);
		cairo_clip(cr);

		cairo_pattern_t *liq = cairo_pattern_create_linear(tx, ty, tx + fill_w, ty);
		cairo_pattern_add_color_stop_rgba(liq, 0.0, 0.14, 0.62, 0.92, 0.98);
		cairo_pattern_add_color_stop_rgba(liq, 1.0, 0.28, 0.80, 0.99, 0.98);
		cairo_rectangle(cr, tx, ty, fill_w, th);
		cairo_set_source(cr, liq);
		cairo_fill(cr);
		cairo_pattern_destroy(liq);
		cairo_restore(cr);

		/* Thumb knob */
		double kx = tx + fill_w;
		if (kx < tx + 6.0) kx = tx + 6.0;
		if (kx > tx + tw - 6.0) kx = tx + tw - 6.0;
		double ky = ty + th / 2.0;

		cairo_arc(cr, kx, ky, 7.5, 0.0, 2.0 * M_PI);
		cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.28);
		cairo_fill(cr);

		cairo_arc(cr, kx, ky, 5.5, 0.0, 2.0 * M_PI);
		cairo_set_source_rgba(cr, 0.98, 0.99, 1.0, 1.0);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0.12, 0.58, 0.88, 1.0);
		cairo_set_line_width(cr, 1.5);
		cairo_stroke(cr);
	}
}

void diamonds_control_center_render_to(struct diamonds_control_center *cc, cairo_t *cr) {
	if (!cc || !cr) return;

	double w = (double)cc->width;
	double h = (double)cc->height;

	/* 1. Clear background */
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	/* 2. High-contrast Deep Mica Card (#141724 -> #0f121c, alpha 0.96, radius 16px) */
	diamonds_draw_rounded_rect(cr, 1.0, 1.0, w - 2.0, h - 2.0, 16.0);
	cairo_pattern_t *bg_pat = cairo_pattern_create_linear(0.0, 0.0, 0.0, h);
	cairo_pattern_add_color_stop_rgba(bg_pat, 0.0, 0.090, 0.105, 0.160, 0.96);
	cairo_pattern_add_color_stop_rgba(bg_pat, 1.0, 0.062, 0.074, 0.115, 0.98);
	cairo_set_source(cr, bg_pat);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(bg_pat);

	/* 3. Crisp Mica Border */
	cairo_pattern_t *border_pat = cairo_pattern_create_linear(0.0, 0.0, 0.0, h);
	cairo_pattern_add_color_stop_rgba(border_pat, 0.0, 0.35, 0.42, 0.58, 0.65);
	cairo_pattern_add_color_stop_rgba(border_pat, 1.0, 0.20, 0.24, 0.34, 0.55);
	cairo_set_source(cr, border_pat);
	cairo_set_line_width(cr, 1.2);
	cairo_stroke(cr);
	cairo_pattern_destroy(border_pat);

	PangoLayout *layout = pango_cairo_create_layout(cr);

	/* 4. Header Row (y = 12..34) */
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 9.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
		pango_layout_set_width(layout, 200 * PANGO_SCALE);
		pango_layout_set_text(layout, "Trung tâm điều khiển", -1);
		cairo_move_to(cr, 16.0, 13.0);
		cairo_set_source_rgba(cr, 0.95, 0.97, 1.0, 1.0);
		pango_cairo_show_layout(cr, layout);

		/* Connection status badge on top-right */
		char net_badge[64];
		if (cc->ethernet_connected && cc->wifi_connected) {
			snprintf(net_badge, sizeof(net_badge), "LAN + Wi-Fi");
		} else if (cc->ethernet_connected) {
			snprintf(net_badge, sizeof(net_badge), "Mạng dây (%s)", cc->ethernet_iface);
		} else if (cc->wifi_connected) {
			snprintf(net_badge, sizeof(net_badge), "Wi-Fi • Online");
		} else {
			snprintf(net_badge, sizeof(net_badge), "Ngoại tuyến");
		}

		diamonds_draw_rounded_rect(cr, w - 110.0, 11.0, 96.0, 20.0, 10.0);
		if (cc->ethernet_connected || cc->wifi_connected) {
			cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.16);
			cairo_fill_preserve(cr);
			cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.45);
		} else {
			cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.06);
			cairo_fill_preserve(cr);
			cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.15);
		}
		cairo_set_line_width(cr, 1.0);
		cairo_stroke(cr);

		desc = pango_font_description_from_string(FONT_BASE " Bold 7");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
		pango_layout_set_width(layout, 96 * PANGO_SCALE);
		pango_layout_set_text(layout, net_badge, -1);
		cairo_move_to(cr, w - 110.0, 15.0);
		if (cc->ethernet_connected || cc->wifi_connected) {
			cairo_set_source_rgba(cr, 0.35, 0.82, 0.99, 1.0);
		} else {
			cairo_set_source_rgba(cr, 0.65, 0.70, 0.78, 0.9);
		}
		pango_cairo_show_layout(cr, layout);
		pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
	}

	/* 5. 6 Quick Setting Tiles (3 rows x 2 cols, each 151 x 44) */
	char eth_sub[128];
	if (cc->ethernet_connected) {
		if (cc->ethernet_ip[0]) {
			snprintf(eth_sub, sizeof(eth_sub), "%s • %s", cc->ethernet_iface, cc->ethernet_ip);
		} else {
			snprintf(eth_sub, sizeof(eth_sub), "Đã kết nối (%s)", cc->ethernet_iface);
		}
	} else if (!cc->ethernet_enabled) {
		snprintf(eth_sub, sizeof(eth_sub), "Đã tắt (%s)", cc->ethernet_iface);
	} else {
		snprintf(eth_sub, sizeof(eth_sub), "Không cắm cáp");
	}

	char wifi_sub[64];
	if (cc->wifi_enabled) {
		snprintf(wifi_sub, sizeof(wifi_sub), "%s", cc->wifi_ssid);
	} else {
		snprintf(wifi_sub, sizeof(wifi_sub), "Đã tắt");
	}

	struct {
		const char *name;
		const char *subtitle;
		bool active;
		bool has_chevron;
		double x;
		double y;
	} tiles[6] = {
		{ "Mạng dây (LAN)", eth_sub,                                    cc->ethernet_connected, true,  14.0,  38.0 },
		{ "Wi-Fi",          wifi_sub,                                   cc->wifi_enabled,       true,  175.0, 38.0 },
		{ "Bluetooth",      cc->bluetooth_enabled ? "Đang bật" : "Đã tắt", cc->bluetooth_enabled, true, 14.0,  89.0 },
		{ "Ánh sáng đêm",   cc->nightlight_enabled ? "4000K Ấm" : "Tắt",   cc->nightlight_enabled, false, 175.0, 89.0 },
		{ "Không làm phiền", cc->focus_enabled ? "Im lặng" : "Tắt",        cc->focus_enabled,      false, 14.0,  140.0 },
		{ "Hiệu năng",      cc->perf_mode_high ? "Tối đa" : "Cân bằng",    cc->perf_mode_high,     false, 175.0, 140.0 },
	};

	for (int i = 0; i < 6; i++) {
		double px = tiles[i].x;
		double py = tiles[i].y;
		double pw = 151.0;
		double ph = 44.0;
		bool on = tiles[i].active;

		diamonds_draw_rounded_rect(cr, px, py, pw, ph, 11.0);
		if (on) {
			cairo_set_source_rgba(cr, 0.14, 0.22, 0.34, 0.90);
			cairo_fill_preserve(cr);
			cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.65);
			cairo_set_line_width(cr, 1.2);
			cairo_stroke(cr);
		} else {
			cairo_set_source_rgba(cr, 0.12, 0.14, 0.21, 0.90);
			cairo_fill_preserve(cr);
			cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
			cairo_set_line_width(cr, 1.0);
			cairo_stroke(cr);
		}

		/* Left circular icon badge */
		double icx = px + 21.0;
		double icy = py + ph / 2.0;
		cairo_arc(cr, icx, icy, 13.5, 0.0, 2.0 * M_PI);
		if (on) {
			cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 1.0);
		} else {
			cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
		}
		cairo_fill(cr);

		if (i == 0) draw_ethernet_symbol(cr, icx, icy, on);
		else if (i == 1) draw_wifi_symbol(cr, icx, icy, on);
		else if (i == 2) draw_bluetooth_symbol(cr, icx, icy, on);
		else if (i == 3) draw_night_light_symbol(cr, icx, icy, on);
		else if (i == 4) draw_focus_symbol(cr, icx, icy, on);
		else if (i == 5) draw_perf_symbol(cr, icx, icy, on);

		/* Text Title & Subtitle */
		if (layout) {
			int text_w = tiles[i].has_chevron ? 90 : 102;
			PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 8");
			pango_layout_set_font_description(layout, desc);
			pango_font_description_free(desc);
			pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
			pango_layout_set_width(layout, text_w * PANGO_SCALE);
			pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
			pango_layout_set_text(layout, tiles[i].name, -1);

			cairo_move_to(cr, px + 40.0, py + 7.0);
			cairo_set_source_rgba(cr, 0.95, 0.97, 1.0, 1.0);
			pango_cairo_show_layout(cr, layout);

			desc = pango_font_description_from_string(FONT_BASE " 7");
			pango_layout_set_font_description(layout, desc);
			pango_font_description_free(desc);
			pango_layout_set_width(layout, text_w * PANGO_SCALE);
			pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
			pango_layout_set_text(layout, tiles[i].subtitle, -1);

			cairo_move_to(cr, px + 40.0, py + 23.0);
			if (on) {
				cairo_set_source_rgba(cr, 0.38, 0.83, 0.99, 1.0);
			} else {
				cairo_set_source_rgba(cr, 0.60, 0.66, 0.76, 0.90);
			}
			pango_cairo_show_layout(cr, layout);
		}

		/* Right chevron '>' for Network / Bluetooth settings sub-action */
		if (tiles[i].has_chevron) {
			/* Subtle vertical divider */
			cairo_move_to(cr, px + pw - 22.0, py + 8.0);
			cairo_line_to(cr, px + pw - 22.0, py + ph - 8.0);
			cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.10);
			cairo_set_line_width(cr, 1.0);
			cairo_stroke(cr);

			double cx_arr = px + pw - 11.0;
			double cy_arr = py + ph / 2.0;
			cairo_move_to(cr, cx_arr - 2.0, cy_arr - 4.0);
			cairo_line_to(cr, cx_arr + 2.0, cy_arr);
			cairo_line_to(cr, cx_arr - 2.0, cy_arr + 4.0);
			cairo_set_source_rgba(cr, 0.78, 0.84, 0.92, 0.85);
			cairo_set_line_width(cr, 1.4);
			cairo_stroke(cr);
		}
	}

	/* 6. Sliders Card (x=14, y=194, w=312, h=92) */
	diamonds_draw_rounded_rect(cr, 14.0, 194.0, 312.0, 92.0, 12.0);
	cairo_set_source_rgba(cr, 0.11, 0.13, 0.19, 0.92);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* Brightness Slider at y=202 */
	draw_slider_row(cr, layout, 202.0, "Độ sáng màn hình", cc->brightness_percent, true);
	/* Volume Slider at y=244 */
	draw_slider_row(cr, layout, 244.0, "Âm lượng hệ thống", cc->volume_percent, false);

	/* 7. Bottom Footer Bar (y = 296..326) */
	/* Left button: Mạng & Internet */
	diamonds_draw_rounded_rect(cr, 14.0, 296.0, 114.0, 28.0, 8.0);
	cairo_set_source_rgba(cr, 0.14, 0.17, 0.25, 0.95);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 7.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
		pango_layout_set_width(layout, 114 * PANGO_SCALE);
		pango_layout_set_text(layout, "Cài đặt Mạng", -1);
		cairo_move_to(cr, 14.0, 303.0);
		cairo_set_source_rgba(cr, 0.35, 0.82, 0.99, 1.0);
		pango_cairo_show_layout(cr, layout);
	}

	/* Middle button: Theo dõi hệ thống */
	diamonds_draw_rounded_rect(cr, 134.0, 296.0, 92.0, 28.0, 8.0);
	cairo_set_source_rgba(cr, 0.14, 0.17, 0.25, 0.95);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	if (layout) {
		pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
		pango_layout_set_width(layout, 92 * PANGO_SCALE);
		pango_layout_set_text(layout, "Hệ thống", -1);
		cairo_move_to(cr, 134.0, 303.0);
		cairo_set_source_rgba(cr, 0.88, 0.92, 0.98, 0.95);
		pango_cairo_show_layout(cr, layout);
	}

	/* Right button: Mở Cài đặt (Settings) */
	diamonds_draw_rounded_rect(cr, 232.0, 296.0, 94.0, 28.0, 8.0);
	cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.22);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.55);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	draw_gear_symbol(cr, 246.0, 310.0);
	if (layout) {
		pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
		pango_layout_set_width(layout, 68 * PANGO_SCALE);
		pango_layout_set_text(layout, "Cài đặt", -1);
		cairo_move_to(cr, 257.0, 303.0);
		cairo_set_source_rgba(cr, 0.95, 0.98, 1.0, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}
}

void diamonds_control_center_render(struct diamonds_control_center *cc) {
	if (!cc || !cc->buffer || !cc->buffer->cairo_surface) return;

	cairo_t *cr = cairo_create(cc->buffer->cairo_surface);
	if (!cr) return;

	diamonds_control_center_render_to(cc, cr);
	cairo_destroy(cr);

	pixman_region32_t damage;
	pixman_region32_init_rect(&damage, 0, 0, (uint32_t)cc->width, (uint32_t)cc->height);
	wlr_scene_buffer_set_buffer_with_damage(cc->scene_buffer, &cc->buffer->base, &damage);
	pixman_region32_fini(&damage);
}

static void open_settings_page(struct diamonds_control_center *cc, const char *page) {
	diamonds_control_center_set_visible(cc, false);
	char cmd[256];
	if (page && *page) {
		snprintf(cmd, sizeof(cmd), "(./build/diamonds-settings %s || ./diamonds-settings %s || diamonds-settings %s) &", page, page, page);
	} else {
		snprintf(cmd, sizeof(cmd), "(./build/diamonds-settings || ./diamonds-settings || diamonds-settings) &");
	}
	run_cmd_async(cmd);
}

bool diamonds_control_center_handle_click(struct diamonds_control_center *cc, double lx, double ly) {
	if (!cc || !cc->visible) return false;

	if (lx < cc->x || lx > cc->x + cc->width || ly < cc->y || ly > cc->y + cc->height) {
		diamonds_control_center_set_visible(cc, false);
		return true;
	}

	double rx = lx - cc->x;
	double ry = ly - cc->y;

	/* 1. Row 1 Tiles: y in [38, 82] — Ethernet & Wi-Fi */
	if (ry >= 38.0 && ry <= 82.0) {
		/* Tile 0: Mạng dây (Ethernet) [14, 165] */
		if (rx >= 14.0 && rx <= 165.0) {
			if (rx >= 143.0) {
				/* Right chevron clicked -> Open Network Settings */
				open_settings_page(cc, "network");
				return true;
			}
			cc->ethernet_enabled = !cc->ethernet_enabled;
			char cmd[256];
			if (cc->ethernet_enabled) {
				snprintf(cmd, sizeof(cmd), "nmcli dev connect %s >/dev/null 2>&1 &", cc->ethernet_iface);
				cc->ethernet_connected = true;
			} else {
				snprintf(cmd, sizeof(cmd), "nmcli dev disconnect %s >/dev/null 2>&1 &", cc->ethernet_iface);
				cc->ethernet_connected = false;
			}
			run_cmd_async(cmd);
			diamonds_control_center_render(cc);
			return true;
		}
		/* Tile 1: Wi-Fi [175, 326] */
		if (rx >= 175.0 && rx <= 326.0) {
			if (rx >= 304.0) {
				/* Right chevron clicked -> Open Network Settings */
				open_settings_page(cc, "network");
				return true;
			}
			cc->wifi_enabled = !cc->wifi_enabled;
			if (cc->wifi_enabled) {
				run_cmd_async("nmcli radio wifi on >/dev/null 2>&1 &");
			} else {
				run_cmd_async("nmcli radio wifi off >/dev/null 2>&1 &");
			}
			update_network_info(cc);
			diamonds_control_center_render(cc);
			return true;
		}
	}

	/* 2. Row 2 Tiles: y in [89, 133] — Bluetooth & Night Light */
	if (ry >= 89.0 && ry <= 133.0) {
		/* Tile 2: Bluetooth [14, 165] */
		if (rx >= 14.0 && rx <= 165.0) {
			if (rx >= 143.0) {
				open_settings_page(cc, "bluetooth");
				return true;
			}
			cc->bluetooth_enabled = !cc->bluetooth_enabled;
			if (cc->bluetooth_enabled) {
				run_cmd_async("rfkill unblock bluetooth >/dev/null 2>&1 || bluetoothctl power on >/dev/null 2>&1 &");
			} else {
				run_cmd_async("rfkill block bluetooth >/dev/null 2>&1 || bluetoothctl power off >/dev/null 2>&1 &");
			}
			diamonds_control_center_render(cc);
			return true;
		}
		/* Tile 3: Night Light [175, 326] */
		if (rx >= 175.0 && rx <= 326.0) {
			cc->nightlight_enabled = !cc->nightlight_enabled;
			if (cc->nightlight_enabled) {
				run_cmd_async("gammastep -O 4000 -m wayland >/dev/null 2>&1 || wlsunset -t 4000 >/dev/null 2>&1 &");
			} else {
				run_cmd_async("pkill -x gammastep || pkill -x wlsunset || true &");
			}
			diamonds_control_center_render(cc);
			return true;
		}
	}

	/* 3. Row 3 Tiles: y in [140, 184] — DND & Performance */
	if (ry >= 140.0 && ry <= 184.0) {
		/* Tile 4: DND [14, 165] */
		if (rx >= 14.0 && rx <= 165.0) {
			cc->focus_enabled = !cc->focus_enabled;
			run_cmd_async("makoctl mode -s dnd || dunstctl set-paused toggle || true &");
			diamonds_control_center_render(cc);
			return true;
		}
		/* Tile 5: Performance Mode [175, 326] */
		if (rx >= 175.0 && rx <= 326.0) {
			cc->perf_mode_high = !cc->perf_mode_high;
			if (cc->perf_mode_high) {
				run_cmd_async("powerprofilesctl set performance >/dev/null 2>&1 || true &");
			} else {
				run_cmd_async("powerprofilesctl set balanced >/dev/null 2>&1 || true &");
			}
			diamonds_control_center_render(cc);
			return true;
		}
	}

	/* 4. Slider 1: Brightness [200, 238] */
	if (rx >= 20.0 && rx <= 320.0 && ry >= 200.0 && ry <= 238.0) {
		int pct = (int)(((rx - 24.0) / 292.0) * 100.0);
		if (pct < 5) pct = 5;
		if (pct > 100) pct = 100;
		cc->brightness_percent = pct;
		cc->active_drag = CC_DRAG_BRIGHTNESS;
		apply_brightness(pct);
		diamonds_control_center_render(cc);
		return true;
	}

	/* 5. Slider 2: Volume [242, 282] */
	if (rx >= 20.0 && rx <= 320.0 && ry >= 242.0 && ry <= 282.0) {
		int pct = (int)(((rx - 24.0) / 292.0) * 100.0);
		if (pct < 0) pct = 0;
		if (pct > 100) pct = 100;
		cc->volume_percent = pct;
		cc->active_drag = CC_DRAG_VOLUME;
		apply_volume(pct);
		diamonds_control_center_render(cc);
		return true;
	}

	/* 6. Bottom Footer Buttons: y in [294, 326] */
	if (ry >= 294.0 && ry <= 328.0) {
		if (rx >= 14.0 && rx <= 128.0) {
			open_settings_page(cc, "network");
			return true;
		}
		if (rx >= 134.0 && rx <= 226.0) {
			diamonds_control_center_set_visible(cc, false);
			run_cmd_async("(./build/diamonds-monitor || ./diamonds-monitor || diamonds-monitor) &");
			return true;
		}
		if (rx >= 232.0 && rx <= 326.0) {
			open_settings_page(cc, NULL);
			return true;
		}
	}

	return true;
}

bool diamonds_control_center_handle_motion(struct diamonds_control_center *cc, double lx, double ly) {
	if (!cc || !cc->visible || cc->active_drag == CC_DRAG_NONE) return false;
	(void)ly;

	double rx = lx - cc->x;

	if (cc->active_drag == CC_DRAG_BRIGHTNESS) {
		int pct = (int)(((rx - 24.0) / 292.0) * 100.0);
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
		int pct = (int)(((rx - 24.0) / 292.0) * 100.0);
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

/* Global wrapper functions matching requirement */
void control_center_toggle(int32_t screen_width, int32_t topbar_height) {
	if (g_control_center) {
		diamonds_control_center_toggle_at(g_control_center, screen_width, topbar_height);
	}
}

bool control_center_handle_click(double cur_x, double cur_y) {
	if (g_control_center && g_control_center->visible) {
		return diamonds_control_center_handle_click(g_control_center, cur_x, cur_y);
	}
	return false;
}

void control_center_render(cairo_t *cr) {
	if (g_control_center && g_control_center->visible) {
		diamonds_control_center_render_to(g_control_center, cr);
	}
}
