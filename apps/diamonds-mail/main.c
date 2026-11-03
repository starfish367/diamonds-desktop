#include "../common/diamonds_app.h"
#include <stdio.h>
#include <pango/pangocairo.h>

static void draw_mail_ui(cairo_t *cr, int w, int h, void *user_data) {
	(void)user_data;

	/* 1. Background */
	cairo_set_source_rgb(cr, 0.08, 0.08, 0.12);
	cairo_paint(cr);

	/* 2. Left Folder List (w = 150) */
	cairo_rectangle(cr, 0, 0, 150, h);
	cairo_set_source_rgb(cr, 0.09, 0.09, 0.13);
	cairo_fill(cr);

	cairo_move_to(cr, 150.5, 0);
	cairo_line_to(cr, 150.5, h);
	cairo_set_source_rgb(cr, 0.18, 0.18, 0.25);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	PangoLayout *layout = pango_cairo_create_layout(cr);
	PangoFontDescription *desc = pango_font_description_from_string("Sans Bold 10");
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);

	pango_layout_set_text(layout, "MAILBOXES", -1);
	cairo_move_to(cr, 16, 18);
	cairo_set_source_rgb(cr, 0.50, 0.55, 0.65);
	pango_cairo_show_layout(cr, layout);

	const char *folders[] = { "Inbox (3)", "Sent", "Drafts", "Archive", "Trash" };
	for (int i = 0; i < 5; i++) {
		double iy = 48.0 + (double)i * 30.0;
		if (i == 0) {
			cairo_rectangle(cr, 8, iy - 3, 134, 24);
			cairo_set_source_rgba(cr, 0.85, 0.45, 0.22, 0.2);
			cairo_fill(cr);
		}
		pango_layout_set_text(layout, folders[i], -1);
		cairo_move_to(cr, 20, iy);
		if (i == 0) {
			cairo_set_source_rgb(cr, 0.95, 0.55, 0.32);
		} else {
			cairo_set_source_rgb(cr, 0.85, 0.88, 0.94);
		}
		pango_cairo_show_layout(cr, layout);
	}

	/* 3. Middle Mail List (w = 230) */
	cairo_rectangle(cr, 151, 0, 230, h);
	cairo_set_source_rgb(cr, 0.10, 0.10, 0.14);
	cairo_fill(cr);

	cairo_move_to(cr, 381.5, 0);
	cairo_line_to(cr, 381.5, h);
	cairo_set_source_rgb(cr, 0.18, 0.18, 0.25);
	cairo_stroke(cr);

	struct mail_entry {
		const char *sender;
		const char *subject;
		const char *time;
	} mails[] = {
		{ "Wayland Project", "Release 1.24 Announcement", "10:45" },
		{ "GitHub Notifications", "New commit on diamonds-desktop", "09:30" },
		{ "Armbian Developer", "Kernel update ready for testing", "Yesterday" },
	};

	for (int i = 0; i < 3; i++) {
		double my = (double)i * 68.0;
		if (i == 0) {
			cairo_rectangle(cr, 152, my, 229, 68);
			cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.15);
			cairo_fill(cr);
		}

		cairo_move_to(cr, 151, my + 68.5);
		cairo_line_to(cr, 381, my + 68.5);
		cairo_set_source_rgb(cr, 0.16, 0.16, 0.22);
		cairo_stroke(cr);

		desc = pango_font_description_from_string("Sans Bold 9.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, mails[i].sender, -1);
		cairo_move_to(cr, 164, my + 10);
		cairo_set_source_rgb(cr, 0.94, 0.96, 0.98);
		pango_cairo_show_layout(cr, layout);

		desc = pango_font_description_from_string("Sans 8.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, mails[i].subject, -1);
		cairo_move_to(cr, 164, my + 28);
		cairo_set_source_rgb(cr, 0.75, 0.80, 0.90);
		pango_cairo_show_layout(cr, layout);

		pango_layout_set_text(layout, mails[i].time, -1);
		cairo_move_to(cr, 164, my + 46);
		cairo_set_source_rgb(cr, 0.50, 0.55, 0.65);
		pango_cairo_show_layout(cr, layout);
	}

	/* 4. Right Preview Pane (w - 382) */
	cairo_rectangle(cr, 382, 0, w - 382, h);
	cairo_set_source_rgb(cr, 0.08, 0.08, 0.12);
	cairo_fill(cr);

	/* Email Subject */
	desc = pango_font_description_from_string("Sans Bold 13");
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);
	pango_layout_set_text(layout, "Release 1.24 Announcement", -1);
	cairo_move_to(cr, 404, 20);
	cairo_set_source_rgb(cr, 0.96, 0.97, 1.0);
	pango_cairo_show_layout(cr, layout);

	/* Email From & Date */
	desc = pango_font_description_from_string("Sans 9");
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);
	pango_layout_set_text(layout, "From: Wayland Project <core@wayland.freedesktop.org>\nDate: 31 October 2026 at 10:45", -1);
	cairo_move_to(cr, 404, 48);
	cairo_set_source_rgb(cr, 0.65, 0.70, 0.82);
	pango_cairo_show_layout(cr, layout);

	cairo_move_to(cr, 404, 88.5);
	cairo_line_to(cr, w - 24, 88.5);
	cairo_set_source_rgb(cr, 0.18, 0.18, 0.25);
	cairo_stroke(cr);

	/* Message Body */
	desc = pango_font_description_from_string("Sans 9.5");
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);
	const char *body =
		"Hello Wayland Community,\n\n"
		"We are thrilled to announce the new update!\n\n"
		"Key highlights in this release:\n"
		"  - Improved protocol stability and frame synchronization.\n"
		"  - Enhanced multi-architecture builds for aarch64 & x86_64.\n"
		"  - Reduced resource consumption for low-power embedded devices.\n\n"
		"Best regards,\n"
		"The Wayland Team";
	pango_layout_set_text(layout, body, -1);
	cairo_move_to(cr, 404, 105);
	cairo_set_source_rgb(cr, 0.88, 0.90, 0.96);
	pango_cairo_show_layout(cr, layout);

	g_object_unref(layout);
}

int main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;
	struct diamonds_client_app *app = diamonds_client_app_create(
		"Diamonds Mail", "diamonds-mail", 760, 480, draw_mail_ui, NULL);
	int ret = diamonds_client_app_run(app);
	diamonds_client_app_destroy(app);
	return ret;
}
