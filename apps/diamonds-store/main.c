#include "../common/diamonds_app.h"
#include <stdio.h>
#include <pango/pangocairo.h>

static void draw_store_ui(cairo_t *cr, int w, int h, void *user_data) {
	(void)user_data;

	/* 1. Background */
	cairo_set_source_rgb(cr, 0.08, 0.08, 0.12);
	cairo_paint(cr);

	/* 2. Top Header */
	cairo_rectangle(cr, 0, 0, w, 60);
	cairo_set_source_rgb(cr, 0.11, 0.11, 0.16);
	cairo_fill(cr);

	PangoLayout *layout = pango_cairo_create_layout(cr);
	PangoFontDescription *desc = pango_font_description_from_string("Sans Bold 14");
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);

	pango_layout_set_text(layout, "Diamonds App Store", -1);
	cairo_move_to(cr, 24, 18);
	cairo_set_source_rgb(cr, 0.94, 0.96, 0.98);
	pango_cairo_show_layout(cr, layout);

	/* 3. Banner Card */
	cairo_rectangle(cr, 24, 80, w - 48, 80);
	cairo_set_source_rgba(cr, 0.18, 0.45, 0.75, 0.35);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.8);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	desc = pango_font_description_from_string("Sans Bold 12");
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);
	pango_layout_set_text(layout, "Discover Native Wayland Applications", -1);
	cairo_move_to(cr, 44, 98);
	cairo_set_source_rgb(cr, 0.95, 0.97, 1.0);
	pango_cairo_show_layout(cr, layout);

	desc = pango_font_description_from_string("Sans 10");
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);
	pango_layout_set_text(layout, "Optimized for x86_64 & ARM64 with ultra-low memory footprint.", -1);
	cairo_move_to(cr, 44, 124);
	cairo_set_source_rgb(cr, 0.75, 0.82, 0.92);
	pango_cairo_show_layout(cr, layout);

	/* 4. App Cards Grid */
	struct store_app {
		const char *name;
		const char *desc;
		const char *status;
	} apps[] = {
		{ "Diamond Files", "Lightweight File Manager with sidebar", "Installed" },
		{ "Diamond Clock", "Analog / Digital Clock & Stopwatch", "Installed" },
		{ "Diamond Mail", "Distraction-free text mail client", "Installed" },
		{ "Alacritty Terminal", "GPU-accelerated terminal emulator", "Get" },
		{ "Geany IDE", "Fast and lightweight code editor", "Get" },
		{ "MPV Player", "Lightweight media player for Wayland", "Get" },
	};

	int card_x = 24;
	int card_y = 180;
	int card_w = (w - 48 - 20) / 2;
	int card_h = 75;

	for (int i = 0; i < 6; i++) {
		int col = i % 2;
		int row = i / 2;
		int cx = card_x + col * (card_w + 20);
		int cy = card_y + row * (card_h + 16);

		/* Card background */
		cairo_rectangle(cr, cx, cy, card_w, card_h);
		cairo_set_source_rgb(cr, 0.12, 0.12, 0.17);
		cairo_fill_preserve(cr);
		cairo_set_source_rgb(cr, 0.20, 0.20, 0.28);
		cairo_set_line_width(cr, 1.0);
		cairo_stroke(cr);

		/* App Title */
		desc = pango_font_description_from_string("Sans Bold 10");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, apps[i].name, -1);
		cairo_move_to(cr, cx + 16, cy + 14);
		cairo_set_source_rgb(cr, 0.94, 0.96, 0.98);
		pango_cairo_show_layout(cr, layout);

		/* App Desc */
		desc = pango_font_description_from_string("Sans 8.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, apps[i].desc, -1);
		cairo_move_to(cr, cx + 16, cy + 38);
		cairo_set_source_rgb(cr, 0.65, 0.70, 0.80);
		pango_cairo_show_layout(cr, layout);

		/* Action Badge */
		int bw = 65, bh = 22;
		int bx = cx + card_w - bw - 14;
		int by = cy + (card_h - bh) / 2;
		cairo_rectangle(cr, bx, by, bw, bh);
		if (apps[i].status[0] == 'I') {
			cairo_set_source_rgba(cr, 0.25, 0.35, 0.45, 0.5);
			cairo_fill(cr);
			cairo_set_source_rgb(cr, 0.75, 0.85, 0.95);
		} else {
			cairo_set_source_rgb(cr, 0.22, 0.74, 0.97);
			cairo_fill(cr);
			cairo_set_source_rgb(cr, 0.05, 0.08, 0.15);
		}
		pango_layout_set_text(layout, apps[i].status, -1);
		int lw, lh;
		pango_layout_get_pixel_size(layout, &lw, &lh);
		cairo_move_to(cr, bx + (bw - lw) / 2, by + (bh - lh) / 2);
		pango_cairo_show_layout(cr, layout);
	}

	g_object_unref(layout);
}

int main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;
	struct diamonds_client_app *app = diamonds_client_app_create(
		"Diamonds Store", "diamonds-store", 680, 480, draw_store_ui, NULL);
	int ret = diamonds_client_app_run(app);
	diamonds_client_app_destroy(app);
	return ret;
}
