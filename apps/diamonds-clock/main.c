#include "../common/diamonds_app.h"
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <pango/pangocairo.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void draw_clock_ui(cairo_t *cr, int w, int h, void *user_data) {
	(void)user_data;

	/* 1. Background */
	cairo_set_source_rgb(cr, 0.08, 0.08, 0.12);
	cairo_paint(cr);

	time_t now = time(NULL);
	struct tm *local = localtime(&now);
	int hours = local ? local->tm_hour : 12;
	int mins = local ? local->tm_min : 0;
	int secs = local ? local->tm_sec : 0;

	/* 2. Left side: Analog Clock */
	double cx = 160.0;
	double cy = (double)h / 2.0;
	double radius = 100.0;

	/* Clock outer rim */
	cairo_arc(cr, cx, cy, radius, 0.0, 2.0 * M_PI);
	cairo_set_source_rgb(cr, 0.12, 0.12, 0.18);
	cairo_fill_preserve(cr);
	cairo_set_source_rgb(cr, 0.25, 0.25, 0.38);
	cairo_set_line_width(cr, 2.0);
	cairo_stroke(cr);

	/* Hour tick marks */
	for (int i = 0; i < 12; i++) {
		double angle = (double)i * (M_PI / 6.0);
		double x1 = cx + (radius - 12.0) * sin(angle);
		double y1 = cy - (radius - 12.0) * cos(angle);
		double x2 = cx + (radius - 4.0) * sin(angle);
		double y2 = cy - (radius - 4.0) * cos(angle);

		cairo_move_to(cr, x1, y1);
		cairo_line_to(cr, x2, y2);
		cairo_set_source_rgb(cr, 0.65, 0.70, 0.85);
		cairo_set_line_width(cr, (i % 3 == 0) ? 2.5 : 1.2);
		cairo_stroke(cr);
	}

	/* Hour Hand */
	double hour_angle = ((double)(hours % 12) + (double)mins / 60.0) * (M_PI / 6.0);
	cairo_move_to(cr, cx, cy);
	cairo_line_to(cr, cx + (radius * 0.5) * sin(hour_angle), cy - (radius * 0.5) * cos(hour_angle));
	cairo_set_source_rgb(cr, 0.94, 0.96, 0.98);
	cairo_set_line_width(cr, 3.5);
	cairo_stroke(cr);

	/* Minute Hand */
	double min_angle = ((double)mins + (double)secs / 60.0) * (M_PI / 30.0);
	cairo_move_to(cr, cx, cy);
	cairo_line_to(cr, cx + (radius * 0.75) * sin(min_angle), cy - (radius * 0.75) * cos(min_angle));
	cairo_set_source_rgb(cr, 0.22, 0.74, 0.97);
	cairo_set_line_width(cr, 2.5);
	cairo_stroke(cr);

	/* Second Hand */
	double sec_angle = (double)secs * (M_PI / 30.0);
	cairo_move_to(cr, cx, cy);
	cairo_line_to(cr, cx + (radius * 0.85) * sin(sec_angle), cy - (radius * 0.85) * cos(sec_angle));
	cairo_set_source_rgb(cr, 0.95, 0.35, 0.35);
	cairo_set_line_width(cr, 1.2);
	cairo_stroke(cr);

	/* Center cap */
	cairo_arc(cr, cx, cy, 4.0, 0.0, 2.0 * M_PI);
	cairo_set_source_rgb(cr, 0.95, 0.35, 0.35);
	cairo_fill(cr);

	/* 3. Right side: Digital Display & Stopwatch */
	PangoLayout *layout = pango_cairo_create_layout(cr);
	PangoFontDescription *desc = pango_font_description_from_string("Sans Bold 24");
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);

	char dig_buf[32];
	snprintf(dig_buf, sizeof(dig_buf), "%02d:%02d:%02d", hours, mins, secs);
	pango_layout_set_text(layout, dig_buf, -1);
	cairo_move_to(cr, 330, 70);
	cairo_set_source_rgb(cr, 0.94, 0.96, 0.98);
	pango_cairo_show_layout(cr, layout);

	/* Date text */
	desc = pango_font_description_from_string("Sans 11");
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);

	char date_buf[64];
	if (local) strftime(date_buf, sizeof(date_buf), "%A, %d %B %Y", local);
	else snprintf(date_buf, sizeof(date_buf), "Today");
	pango_layout_set_text(layout, date_buf, -1);
	cairo_move_to(cr, 330, 115);
	cairo_set_source_rgb(cr, 0.65, 0.70, 0.82);
	pango_cairo_show_layout(cr, layout);

	/* Stopwatch section card */
	cairo_rectangle(cr, 320, 160, 240, 100);
	cairo_set_source_rgb(cr, 0.12, 0.12, 0.17);
	cairo_fill_preserve(cr);
	cairo_set_source_rgb(cr, 0.22, 0.22, 0.30);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	desc = pango_font_description_from_string("Sans Bold 10");
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);
	pango_layout_set_text(layout, "STOPWATCH", -1);
	cairo_move_to(cr, 336, 172);
	cairo_set_source_rgb(cr, 0.55, 0.60, 0.75);
	pango_cairo_show_layout(cr, layout);

	desc = pango_font_description_from_string("Sans Bold 18");
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);
	pango_layout_set_text(layout, "00:00:00.0", -1);
	cairo_move_to(cr, 336, 192);
	cairo_set_source_rgb(cr, 0.22, 0.74, 0.97);
	pango_cairo_show_layout(cr, layout);

	/* Stopwatch buttons */
	const char *btn_labels[] = { "Start", "Reset" };
	for (int b = 0; b < 2; b++) {
		int bx = 336 + b * 75;
		int by = 224;
		cairo_rectangle(cr, bx, by, 65, 24);
		cairo_set_source_rgb(cr, b == 0 ? 0.18 : 0.25, b == 0 ? 0.45 : 0.25, b == 0 ? 0.35 : 0.32);
		cairo_fill(cr);

		desc = pango_font_description_from_string("Sans Bold 9");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, btn_labels[b], -1);
		int bw, bh;
		pango_layout_get_pixel_size(layout, &bw, &bh);
		cairo_move_to(cr, bx + (65 - bw) / 2, by + (24 - bh) / 2);
		cairo_set_source_rgb(cr, 0.94, 0.96, 0.98);
		pango_cairo_show_layout(cr, layout);
	}

	g_object_unref(layout);
}

int main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;
	struct diamonds_client_app *app = diamonds_client_app_create(
		"Diamonds Clock", "diamonds-clock", 600, 320, draw_clock_ui, NULL);
	int ret = diamonds_client_app_run(app);
	diamonds_client_app_destroy(app);
	return ret;
}
