#define _GNU_SOURCE
#include "calendar.h"
#include "diamonds.h"
#include "topbar.h"
#include "sound.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <pixman.h>
#include <pango/pangocairo.h>

#define FONT_SANS "Segoe UI, Cantarell, DejaVu Sans, Ubuntu, sans-serif"

struct diamonds_calendar *diamonds_calendar_create(struct diamonds_server *server) {
	struct diamonds_calendar *cal = calloc(1, sizeof(*cal));
	if (!cal) return NULL;

	cal->server = server;
	cal->width = 340;
	cal->height = 430;
	cal->visible = false;

	cal->buffer = diamonds_cairo_buffer_create(cal->width, cal->height);
	if (!cal->buffer) {
		free(cal);
		return NULL;
	}

	cal->scene_buffer = wlr_scene_buffer_create(server->tree_overlay, &cal->buffer->base);
	if (!cal->scene_buffer) {
		wlr_buffer_drop(&cal->buffer->base);
		free(cal);
		return NULL;
	}

	wlr_scene_node_set_enabled(&cal->scene_buffer->node, false);
	return cal;
}

void diamonds_calendar_destroy(struct diamonds_calendar *cal) {
	if (!cal) return;
	if (cal->scene_buffer) {
		wlr_scene_node_destroy(&cal->scene_buffer->node);
	}
	if (cal->buffer) {
		wlr_buffer_drop(&cal->buffer->base);
	}
	free(cal);
}

void diamonds_calendar_reposition(struct diamonds_calendar *cal, int32_t output_width, int32_t output_height) {
	(void)output_height;
	if (!cal || !cal->scene_buffer) return;

	cal->x = (output_width - cal->width) / 2;
	cal->y = 36; /* Just below 32px topbar */

	wlr_scene_node_set_position(&cal->scene_buffer->node, cal->x, cal->y);
}

void diamonds_calendar_toggle(struct diamonds_calendar *cal) {
	if (!cal || !cal->scene_buffer) return;
	cal->visible = !cal->visible;
	if (cal->visible) {
		/* Close control center or start menu if open */
		if (cal->server->topbar) {
			cal->server->topbar->control_center_active = false;
			diamonds_topbar_render(cal->server->topbar);
		}
		diamonds_calendar_render(cal);
		wlr_scene_node_set_enabled(&cal->scene_buffer->node, true);
		wlr_scene_node_raise_to_top(&cal->scene_buffer->node);
		diamonds_sound_play(DIAMONDS_SOUND_CLICK);
	} else {
		wlr_scene_node_set_enabled(&cal->scene_buffer->node, false);
	}
}

static int get_days_in_month(int month, int year) {
	static const int days_per_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (month == 1) {
		bool is_leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
		return is_leap ? 29 : 28;
	}
	return days_per_month[month % 12];
}

void diamonds_calendar_render(struct diamonds_calendar *cal) {
	if (!cal || !cal->buffer || !cal->buffer->cairo_surface) return;

	cairo_t *cr = cairo_create(cal->buffer->cairo_surface);
	if (!cr) return;

	double w = (double)cal->width;
	double h = (double)cal->height;

	/* 1. Clear */
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	/* 2. Mica Dark Card background (#141624, rounded 12px) */
	diamonds_draw_rounded_rect(cr, 0.5, 0.5, w - 1.0, h - 1.0, 12.0);
	cairo_set_source_rgba(cr, 0.078, 0.086, 0.141, 0.96);
	cairo_fill_preserve(cr);

	/* Border: 1px #38BDF8 with 30% alpha */
	cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.35);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	time_t now = time(NULL);
	struct tm *local = localtime(&now);

	/* 3. Header: Month and Year */
	char month_str[64];
	if (local) {
		strftime(month_str, sizeof(month_str), "Tháng %m, %Y", local);
	} else {
		snprintf(month_str, sizeof(month_str), "Lịch");
	}

	PangoLayout *layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_SANS " Bold 11");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, month_str, -1);
		cairo_move_to(cr, 20.0, 16.0);
		cairo_set_source_rgba(cr, 0.94, 0.96, 0.98, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* 4. Day of week labels (T2, T3, T4, T5, T6, T7, CN) */
	static const char *dow_labels[] = { "T2", "T3", "T4", "T5", "T6", "T7", "CN" };
	double grid_start_x = 18.0;
	double grid_start_y = 44.0;
	double col_w = (w - 36.0) / 7.0;

	layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_SANS " Semibold 8.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);

		for (int i = 0; i < 7; i++) {
			pango_layout_set_text(layout, dow_labels[i], -1);
			int lw, lh;
			pango_layout_get_pixel_size(layout, &lw, &lh);
			double tx = grid_start_x + (double)i * col_w + (col_w - lw) / 2.0;
			cairo_move_to(cr, tx, grid_start_y);
			cairo_set_source_rgba(cr, 0.50, 0.55, 0.65, 1.0);
			pango_cairo_show_layout(cr, layout);
		}
		g_object_unref(layout);
	}

	/* 5. Calendar grid numbers */
	int cur_mday = local ? local->tm_mday : 1;
	int cur_mon  = local ? local->tm_mon : 0;
	int cur_year = local ? local->tm_year + 1900 : 2026;

	struct tm t1 = {0};
	t1.tm_year = local ? local->tm_year : 126;
	t1.tm_mon  = cur_mon;
	t1.tm_mday = 1;
	mktime(&t1);
	int start_col = (t1.tm_wday + 6) % 7; /* Monday = 0 */
	int num_days = get_days_in_month(cur_mon, cur_year);

	double row_h = 24.0;
	double days_start_y = grid_start_y + 20.0;

	layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_SANS " 9");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);

		for (int day = 1; day <= num_days; day++) {
			int idx = start_col + day - 1;
			int c = idx % 7;
			int r = idx / 7;

			double cx = grid_start_x + (double)c * col_w + col_w / 2.0;
			double cy = days_start_y + (double)r * row_h + row_h / 2.0;

			if (day == cur_mday) {
				/* Glowing Cyan Badge for Today */
				diamonds_draw_rounded_rect(cr, cx - 11.0, cy - 11.0, 22.0, 22.0, 6.0);
				cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 1.0);
				cairo_fill(cr);
			}

			char day_num[16];
			snprintf(day_num, sizeof(day_num), "%d", day);
			pango_layout_set_text(layout, day_num, -1);
			int lw, lh;
			pango_layout_get_pixel_size(layout, &lw, &lh);

			cairo_move_to(cr, cx - (double)lw / 2.0, cy - (double)lh / 2.0);
			if (day == cur_mday) {
				cairo_set_source_rgba(cr, 0.05, 0.08, 0.15, 1.0);
			} else {
				cairo_set_source_rgba(cr, 0.88, 0.91, 0.96, 1.0);
			}
			pango_cairo_show_layout(cr, layout);
		}
		g_object_unref(layout);
	}

	/* 6. Separator line */
	double sep_y = 230.0;
	cairo_new_path(cr);
	cairo_move_to(cr, 16.0, sep_y);
	cairo_line_to(cr, w - 16.0, sep_y);
	cairo_set_source_rgba(cr, 0.20, 0.22, 0.32, 0.8);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* 7. Notification Center Section */
	layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_SANS " Bold 9.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, "THÔNG BÁO HỆ THỐNG", -1);
		cairo_move_to(cr, 20.0, sep_y + 12.0);
		cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* Empty state: Không có thông báo mới */
	diamonds_draw_rounded_rect(cr, 16.0, sep_y + 36.0, w - 32.0, 56.0, 8.0);
	cairo_set_source_rgba(cr, 0.11, 0.12, 0.18, 0.60);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.20, 0.22, 0.30, 0.40);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_SANS " 9");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, "Không có thông báo mới", -1);
		int text_w = 0, text_h = 0;
		pango_layout_get_pixel_size(layout, &text_w, &text_h);
		cairo_move_to(cr, 16.0 + (w - 32.0 - text_w) / 2.0, sep_y + 36.0 + (56.0 - text_h) / 2.0);
		cairo_set_source_rgba(cr, 0.55, 0.60, 0.70, 0.85);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	cairo_destroy(cr);

	pixman_region32_t damage;
	pixman_region32_init_rect(&damage, 0, 0, (uint32_t)cal->width, (uint32_t)cal->height);
	wlr_scene_buffer_set_buffer_with_damage(cal->scene_buffer, &cal->buffer->base, &damage);
	pixman_region32_fini(&damage);
}

bool diamonds_calendar_handle_click(struct diamonds_calendar *cal, double lx, double ly) {
	if (!cal || !cal->visible) return false;
	if (lx < cal->x || lx > cal->x + cal->width ||
	    ly < cal->y || ly > cal->y + cal->height) {
		/* Clicked outside: dismiss */
		diamonds_calendar_toggle(cal);
		return false;
	}
	/* Click inside */
	diamonds_sound_play(DIAMONDS_SOUND_CLICK);
	return true;
}
