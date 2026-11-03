#include "../common/diamonds_app.h"
#include <stdio.h>
#include <pango/pangocairo.h>

struct settings_state {
	int current_tab;         /* 0 = Animations, 1 = Appearance, 2 = Displays, 3 = Sound */
	int animation_mode;      /* 0..4 */
	bool mica_enabled;
	int mica_opacity;        /* 0 = 85%, 1 = 92%, 2 = 96%, 3 = 100% */
	int accent_color;        /* 0 = Sky Cyan, 1 = Emerald, 2 = Amethyst, 3 = Amber, 4 = Crimson */
};

static struct settings_state g_state = {
	.current_tab = 0,
	.animation_mode = 1,     /* Cubic Bezier Win11 Fluent */
	.mica_enabled = true,
	.mica_opacity = 1,       /* 92% default */
	.accent_color = 0,       /* Sky Cyan #38BDF8 */
};

static void draw_settings_ui(cairo_t *cr, int w, int h, void *user_data) {
	(void)user_data;

	/* 1. Background */
	cairo_set_source_rgb(cr, 0.08, 0.08, 0.12);
	cairo_paint(cr);

	/* 2. Left Tab Sidebar (w = 170) */
	cairo_rectangle(cr, 0, 0, 170, h);
	cairo_set_source_rgb(cr, 0.09, 0.09, 0.14);
	cairo_fill(cr);

	cairo_move_to(cr, 170.5, 0);
	cairo_line_to(cr, 170.5, h);
	cairo_set_source_rgb(cr, 0.18, 0.18, 0.25);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	PangoLayout *layout = pango_cairo_create_layout(cr);
	PangoFontDescription *desc = pango_font_description_from_string("Sans Bold 11");
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);

	pango_layout_set_text(layout, "SETTINGS", -1);
	cairo_move_to(cr, 20, 20);
	cairo_set_source_rgb(cr, 0.50, 0.55, 0.65);
	pango_cairo_show_layout(cr, layout);

	const char *tabs[] = { "Animations", "Appearance", "Displays", "Sound" };
	for (int i = 0; i < 4; i++) {
		double ty = 54.0 + (double)i * 36.0;
		if (i == g_state.current_tab) {
			cairo_rectangle(cr, 10, ty - 4, 150, 30);
			cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.20);
			cairo_fill(cr);
		}

		desc = pango_font_description_from_string("Sans 10");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, tabs[i], -1);
		cairo_move_to(cr, 24, ty + 2);
		if (i == g_state.current_tab) {
			cairo_set_source_rgb(cr, 0.22, 0.74, 0.97);
		} else {
			cairo_set_source_rgb(cr, 0.82, 0.85, 0.92);
		}
		pango_cairo_show_layout(cr, layout);
	}

	/* 3. Main Content Panel (w - 170) */
	double content_x = 195.0;
	double content_y = 20.0;

	/* Title */
	desc = pango_font_description_from_string("Sans Bold 14");
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);
	pango_layout_set_text(layout, tabs[g_state.current_tab], -1);
	cairo_move_to(cr, content_x, content_y);
	cairo_set_source_rgb(cr, 0.94, 0.96, 0.98);
	pango_cairo_show_layout(cr, layout);

	if (g_state.current_tab == 0) {
		/* Tab 0: Animations */
		desc = pango_font_description_from_string("Sans 9");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, "Choose window open/close easing physics for Diamonds Compositor:", -1);
		cairo_move_to(cr, content_x, content_y + 30);
		cairo_set_source_rgb(cr, 0.65, 0.70, 0.80);
		pango_cairo_show_layout(cr, layout);

		const char *anim_modes[] = {
			"1. Spring Physics (macOS Smooth, sub-pixel spring curves)",
			"2. Cubic Bezier Ease-Out (Win11 Fluent, default standard)",
			"3. Scale & Fade (Minimalist, low GPU overhead)",
			"4. Slide Down (Dynamic desktop presentation)",
			"5. Instant Minimal (Zero-latency competitive mode)"
		};

		for (int a = 0; a < 5; a++) {
			double ay = content_y + 60.0 + (double)a * 42.0;

			/* Card */
			cairo_rectangle(cr, content_x, ay, w - content_x - 30, 36);
			if (a == g_state.animation_mode) {
				cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.18);
				cairo_fill_preserve(cr);
				cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.85);
				cairo_set_line_width(cr, 1.2);
				cairo_stroke(cr);
			} else {
				cairo_set_source_rgb(cr, 0.12, 0.12, 0.17);
				cairo_fill_preserve(cr);
				cairo_set_source_rgb(cr, 0.20, 0.20, 0.28);
				cairo_set_line_width(cr, 0.8);
				cairo_stroke(cr);
			}

			/* Radio circle */
			cairo_arc(cr, content_x + 18, ay + 18, 6.0, 0, 6.28);
			if (a == g_state.animation_mode) {
				cairo_set_source_rgb(cr, 0.22, 0.74, 0.97);
				cairo_fill(cr);
			} else {
				cairo_set_source_rgb(cr, 0.40, 0.45, 0.55);
				cairo_set_line_width(cr, 1.2);
				cairo_stroke(cr);
			}

			desc = pango_font_description_from_string("Sans 9.5");
			pango_layout_set_font_description(layout, desc);
			pango_font_description_free(desc);
			pango_layout_set_text(layout, anim_modes[a], -1);
			cairo_move_to(cr, content_x + 36, ay + 9);
			cairo_set_source_rgb(cr, 0.92, 0.94, 0.98);
			pango_cairo_show_layout(cr, layout);
		}
	} else if (g_state.current_tab == 1) {
		/* Tab 1: Appearance & Mica */
		double my = content_y + 40.0;

		/* Mica Toggle */
		cairo_rectangle(cr, content_x, my, w - content_x - 30, 65);
		cairo_set_source_rgb(cr, 0.12, 0.12, 0.17);
		cairo_fill_preserve(cr);
		cairo_set_source_rgb(cr, 0.20, 0.20, 0.28);
		cairo_set_line_width(cr, 1.0);
		cairo_stroke(cr);

		desc = pango_font_description_from_string("Sans Bold 10");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, "Mica Material Transparency", -1);
		cairo_move_to(cr, content_x + 16, my + 14);
		cairo_set_source_rgb(cr, 0.94, 0.96, 0.98);
		pango_cairo_show_layout(cr, layout);

		desc = pango_font_description_from_string("Sans 8.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, "Enables subtle alpha-transparency on Dock, Topbar and Start Menu", -1);
		cairo_move_to(cr, content_x + 16, my + 38);
		cairo_set_source_rgb(cr, 0.65, 0.70, 0.80);
		pango_cairo_show_layout(cr, layout);

		/* Switch pill on right */
		double sw_x = w - 85.0;
		double sw_y = my + 20.0;
		cairo_rectangle(cr, sw_x, sw_y, 44, 24);
		cairo_set_source_rgb(cr, 0.22, 0.74, 0.97);
		cairo_fill(cr);
		cairo_arc(cr, sw_x + 32, sw_y + 12, 9, 0, 6.28);
		cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
		cairo_fill(cr);

		/* Accent Color section */
		my += 85.0;
		desc = pango_font_description_from_string("Sans Bold 10");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, "Accent Colors", -1);
		cairo_move_to(cr, content_x, my);
		cairo_set_source_rgb(cr, 0.94, 0.96, 0.98);
		pango_cairo_show_layout(cr, layout);

		const float colors[][3] = {
			{ 0.22f, 0.74f, 0.97f }, /* Sky Cyan */
			{ 0.06f, 0.72f, 0.50f }, /* Emerald */
			{ 0.55f, 0.36f, 0.96f }, /* Royal Amethyst */
			{ 0.96f, 0.62f, 0.04f }, /* Amber */
			{ 0.94f, 0.27f, 0.27f }, /* Crimson */
		};

		for (int c = 0; c < 5; c++) {
			double cx = content_x + (double)c * 48.0 + 16.0;
			double cy = my + 36.0;
			cairo_arc(cr, cx, cy, 16.0, 0, 6.28);
			cairo_set_source_rgb(cr, colors[c][0], colors[c][1], colors[c][2]);
			cairo_fill(cr);

			if (c == g_state.accent_color) {
				cairo_arc(cr, cx, cy, 20.0, 0, 6.28);
				cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
				cairo_set_line_width(cr, 2.0);
				cairo_stroke(cr);
			}
		}
	} else {
		/* Tab 2/3: General */
		desc = pango_font_description_from_string("Sans 10");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, "Hardware Acceleration: Mali450 GLES2 (Lima KMS/DRM)\nAudio Engine: Native PCM (aplay)\nMulti-arch: aarch64 native", -1);
		cairo_move_to(cr, content_x, content_y + 40);
		cairo_set_source_rgb(cr, 0.75, 0.80, 0.90);
		pango_cairo_show_layout(cr, layout);
	}

	g_object_unref(layout);
}

int main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;
	struct diamonds_client_app *app = diamonds_client_app_create(
		"Diamonds Settings", "diamonds-settings", 700, 440, draw_settings_ui, NULL);
	int ret = diamonds_client_app_run(app);
	diamonds_client_app_destroy(app);
	return ret;
}
