#include "../common/diamonds_app.h"
#include <stdio.h>
#include <pango/pangocairo.h>

static void draw_files_ui(cairo_t *cr, int w, int h, void *user_data) {
	(void)user_data;

	/* 1. Clear background */
	cairo_set_source_rgb(cr, 0.08, 0.08, 0.11);
	cairo_paint(cr);

	/* 2. Left Sidebar (w = 180) */
	cairo_rectangle(cr, 0, 0, 180, h);
	cairo_set_source_rgb(cr, 0.09, 0.09, 0.13);
	cairo_fill(cr);

	/* Separator border */
	cairo_set_source_rgb(cr, 0.18, 0.18, 0.25);
	cairo_set_line_width(cr, 1.0);
	cairo_move_to(cr, 180.5, 0);
	cairo_line_to(cr, 180.5, h);
	cairo_stroke(cr);

	/* Sidebar title */
	PangoLayout *layout = pango_cairo_create_layout(cr);
	PangoFontDescription *desc = pango_font_description_from_string("Sans Bold 10");
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);

	pango_layout_set_text(layout, "PLACES", -1);
	cairo_move_to(cr, 16, 20);
	cairo_set_source_rgb(cr, 0.50, 0.55, 0.65);
	pango_cairo_show_layout(cr, layout);

	/* Sidebar items */
	const char *places[] = { "Home", "Documents", "Downloads", "Pictures", "Music", "Trash" };
	for (int i = 0; i < 6; i++) {
		double iy = 50.0 + (double)i * 32.0;
		if (i == 1) { /* Selected */
			cairo_rectangle(cr, 8, iy - 4, 164, 28);
			cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.18);
			cairo_fill(cr);
		}
		pango_layout_set_text(layout, places[i], -1);
		cairo_move_to(cr, 24, iy);
		if (i == 1) {
			cairo_set_source_rgb(cr, 0.22, 0.74, 0.97);
		} else {
			cairo_set_source_rgb(cr, 0.85, 0.88, 0.95);
		}
		pango_cairo_show_layout(cr, layout);
	}

	/* 3. Breadcrumb Path Bar */
	cairo_rectangle(cr, 181, 0, w - 181, 44);
	cairo_set_source_rgb(cr, 0.10, 0.10, 0.14);
	cairo_fill(cr);

	cairo_move_to(cr, 181, 44.5);
	cairo_line_to(cr, w, 44.5);
	cairo_set_source_rgb(cr, 0.18, 0.18, 0.25);
	cairo_stroke(cr);

	pango_layout_set_text(layout, "Home  >  Documents", -1);
	cairo_move_to(cr, 200, 14);
	cairo_set_source_rgb(cr, 0.92, 0.94, 0.98);
	pango_cairo_show_layout(cr, layout);

	/* 4. Main Icon Grid */
	struct file_item {
		const char *name;
		bool is_dir;
	} files[] = {
		{ "Projects", true },
		{ "Reports", true },
		{ "Archives", true },
		{ "diamonds.c", false },
		{ "notes.txt", false },
		{ "design.png", false },
		{ "budget.xlsx", false },
		{ "readme.md", false },
	};

	int grid_start_x = 210;
	int grid_start_y = 70;
	int cell_w = 110;
	int cell_h = 95;

	for (int i = 0; i < 8; i++) {
		int col = i % 4;
		int row = i / 4;
		int cx = grid_start_x + col * cell_w;
		int cy = grid_start_y + row * cell_h;

		/* Icon */
		if (files[i].is_dir) {
			/* Folder icon */
			cairo_rectangle(cr, cx + 18, cy + 12, 44, 30);
			cairo_set_source_rgb(cr, 0.23, 0.55, 0.88);
			cairo_fill(cr);

			cairo_rectangle(cr, cx + 18, cy + 8, 20, 6);
			cairo_set_source_rgb(cr, 0.18, 0.45, 0.78);
			cairo_fill(cr);
		} else {
			/* File icon */
			cairo_rectangle(cr, cx + 24, cy + 8, 32, 40);
			cairo_set_source_rgb(cr, 0.30, 0.34, 0.44);
			cairo_fill(cr);

			cairo_set_source_rgb(cr, 0.75, 0.80, 0.90);
			cairo_set_line_width(cr, 1.5);
			cairo_move_to(cr, cx + 30, cy + 18);
			cairo_line_to(cr, cx + 50, cy + 18);
			cairo_stroke(cr);
			cairo_move_to(cr, cx + 30, cy + 26);
			cairo_line_to(cr, cx + 50, cy + 26);
			cairo_stroke(cr);
		}

		/* Filename */
		pango_layout_set_text(layout, files[i].name, -1);
		int lw, lh;
		pango_layout_get_pixel_size(layout, &lw, &lh);
		cairo_move_to(cr, cx + (cell_w - lw) / 2 - 15, cy + 54);
		cairo_set_source_rgb(cr, 0.90, 0.92, 0.96);
		pango_cairo_show_layout(cr, layout);
	}

	g_object_unref(layout);
}

int main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;
	struct diamonds_client_app *app = diamonds_client_app_create(
		"Diamonds Files", "diamonds-files", 720, 480, draw_files_ui, NULL);
	int ret = diamonds_client_app_run(app);
	diamonds_client_app_destroy(app);
	return ret;
}
