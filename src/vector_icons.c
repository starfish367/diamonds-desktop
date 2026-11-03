#include "vector_icons.h"
#include "cairo_buffer.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void diamonds_draw_vector_diamond_logo(cairo_t *cr, double cx, double cy, double size) {
	double r = size / 2.0;
	cairo_save(cr);
	cairo_translate(cr, cx, cy);

	/* Outer diamond */
	cairo_new_path(cr);
	cairo_move_to(cr, 0, -r);
	cairo_line_to(cr, r, 0);
	cairo_line_to(cr, 0, r);
	cairo_line_to(cr, -r, 0);
	cairo_close_path(cr);

	cairo_set_source_rgba(cr, 0.2196, 0.7412, 0.9725, 0.95); /* #38BDF8 */
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.50, 0.85, 1.0, 1.0);
	cairo_set_line_width(cr, 1.2);
	cairo_stroke(cr);

	/* Inner diamond core */
	double ir = r * 0.50;
	cairo_new_path(cr);
	cairo_move_to(cr, 0, -ir);
	cairo_line_to(cr, ir, 0);
	cairo_line_to(cr, 0, ir);
	cairo_line_to(cr, -ir, 0);
	cairo_close_path(cr);

	cairo_set_source_rgba(cr, 0.08, 0.35, 0.55, 1.0);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.75, 0.92, 1.0, 0.9);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* Specular reflection point */
	cairo_arc(cr, -ir * 0.3, -ir * 0.3, 1.5, 0.0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.8);
	cairo_fill(cr);

	cairo_restore(cr);
}

void diamonds_draw_vector_terminal(cairo_t *cr, double cx, double cy, double size) {
	double x = cx - size / 2.0;
	double y = cy - size / 2.0;

	/* Main terminal window body */
	diamonds_draw_rounded_rect(cr, x, y, size, size, 4.0);
	cairo_set_source_rgba(cr, 0.10, 0.11, 0.16, 1.0); /* #1A1C29 */
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.25, 0.28, 0.38, 0.9);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* Top window header strip */
	double header_h = size * 0.26;
	diamonds_draw_rounded_rect(cr, x, y, size, header_h, 4.0);
	cairo_set_source_rgba(cr, 0.16, 0.17, 0.24, 1.0);
	cairo_fill(cr);
	/* Flatten bottom corners of header */
	cairo_rectangle(cr, x, y + header_h - 2.0, size, 2.0);
	cairo_set_source_rgba(cr, 0.16, 0.17, 0.24, 1.0);
	cairo_fill(cr);

	/* 3 Micro traffic light control dots */
	double dot_r = size * 0.045;
	double dot_y = y + header_h / 2.0;
	/* Red */
	cairo_arc(cr, x + size * 0.18, dot_y, dot_r, 0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.93, 0.30, 0.30, 1.0);
	cairo_fill(cr);
	/* Yellow */
	cairo_arc(cr, x + size * 0.30, dot_y, dot_r, 0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.94, 0.70, 0.15, 1.0);
	cairo_fill(cr);
	/* Green */
	cairo_arc(cr, x + size * 0.42, dot_y, dot_r, 0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.20, 0.78, 0.45, 1.0);
	cairo_fill(cr);

	/* Terminal prompt: >_ */
	double content_y = y + header_h + (size - header_h) / 2.0;
	cairo_new_path(cr);
	cairo_move_to(cr, x + size * 0.20, content_y - size * 0.16);
	cairo_line_to(cr, x + size * 0.38, content_y);
	cairo_line_to(cr, x + size * 0.20, content_y + size * 0.16);
	cairo_set_source_rgba(cr, 0.2196, 0.7412, 0.9725, 1.0); /* Cyan #38BDF8 */
	cairo_set_line_width(cr, size * 0.08);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_stroke(cr);

	/* Blinking cursor */
	cairo_new_path(cr);
	cairo_move_to(cr, x + size * 0.48, content_y + size * 0.16);
	cairo_line_to(cr, x + size * 0.78, content_y + size * 0.16);
	cairo_set_source_rgba(cr, 0.95, 0.96, 0.98, 1.0);
	cairo_set_line_width(cr, size * 0.08);
	cairo_stroke(cr);
}

void diamonds_draw_vector_files(cairo_t *cr, double cx, double cy, double size) {
	double x = cx - size / 2.0;
	double y = cy - size / 2.0;

	/* 1. Back folder tab and body (Dark Royal Blue) */
	diamonds_draw_rounded_rect(cr, x + size * 0.08, y + size * 0.12, size * 0.45, size * 0.25, 3.0);
	cairo_set_source_rgba(cr, 0.11, 0.35, 0.75, 1.0);
	cairo_fill(cr);

	diamonds_draw_rounded_rect(cr, x + size * 0.08, y + size * 0.20, size * 0.84, size * 0.68, 3.5);
	cairo_set_source_rgba(cr, 0.11, 0.35, 0.75, 1.0);
	cairo_fill(cr);

	/* 2. Paper insert sticking out */
	diamonds_draw_rounded_rect(cr, x + size * 0.20, y + size * 0.10, size * 0.60, size * 0.40, 2.0);
	cairo_set_source_rgba(cr, 0.95, 0.97, 1.0, 0.95);
	cairo_fill(cr);

	/* 3. Front folder flap (Vibrant Blue #3B82F6) */
	diamonds_draw_rounded_rect(cr, x + size * 0.08, y + size * 0.32, size * 0.84, size * 0.56, 3.5);
	cairo_set_source_rgba(cr, 0.23, 0.51, 0.96, 1.0);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.45, 0.68, 1.0, 0.8);
	cairo_set_line_width(cr, 0.8);
	cairo_stroke(cr);

	/* Front flap fold line / shadow */
	cairo_new_path(cr);
	cairo_move_to(cr, x + size * 0.12, y + size * 0.38);
	cairo_line_to(cr, x + size * 0.88, y + size * 0.38);
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.25);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);
}

void diamonds_draw_vector_browser(cairo_t *cr, double cx, double cy, double size) {
	double r = size * 0.44;

	/* Globe background sphere */
	cairo_arc(cr, cx, cy, r, 0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.05, 0.45, 0.85, 1.0);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.85);
	cairo_set_line_width(cr, size * 0.06);
	cairo_stroke(cr);

	/* Latitude equator */
	cairo_new_path(cr);
	cairo_move_to(cr, cx - r + 1.0, cy);
	cairo_line_to(cr, cx + r - 1.0, cy);
	cairo_set_source_rgba(cr, 0.85, 0.93, 1.0, 0.6);
	cairo_set_line_width(cr, size * 0.05);
	cairo_stroke(cr);

	/* Longitude ellipse */
	cairo_save(cr);
	cairo_translate(cr, cx, cy);
	cairo_scale(cr, 0.52, 1.0);
	cairo_arc(cr, 0, 0, r - 0.5, 0, 2.0 * M_PI);
	cairo_restore(cr);
	cairo_set_source_rgba(cr, 0.85, 0.93, 1.0, 0.6);
	cairo_set_line_width(cr, size * 0.05);
	cairo_stroke(cr);

	/* Compass pointer accent */
	cairo_new_path(cr);
	cairo_move_to(cr, cx + size * 0.22, cy - size * 0.22);
	cairo_line_to(cr, cx + size * 0.04, cy - size * 0.02);
	cairo_line_to(cr, cx - size * 0.22, cy + size * 0.22);
	cairo_line_to(cr, cx - size * 0.02, cy + size * 0.04);
	cairo_close_path(cr);
	cairo_set_source_rgba(cr, 0.95, 0.35, 0.25, 1.0);
	cairo_fill(cr);
}

void diamonds_draw_vector_media(cairo_t *cr, double cx, double cy, double size) {
	double x = cx - size / 2.0;
	double y = cy - size / 2.0;

	/* Rounded tile background (Rich VLC Orange #F97316) */
	diamonds_draw_rounded_rect(cr, x, y, size, size, 4.0);
	cairo_set_source_rgba(cr, 0.95, 0.45, 0.10, 1.0);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 1.0, 0.65, 0.35, 0.8);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* Top white band */
	cairo_rectangle(cr, x + 2.0, y + size * 0.32, size - 4.0, size * 0.12);
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.85);
	cairo_fill(cr);

	/* Bottom white band */
	cairo_rectangle(cr, x + 2.0, y + size * 0.60, size - 4.0, size * 0.12);
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.85);
	cairo_fill(cr);

	/* Center Play Triangle */
	cairo_new_path(cr);
	cairo_move_to(cr, cx - size * 0.14, cy - size * 0.22);
	cairo_line_to(cr, cx + size * 0.22, cy);
	cairo_line_to(cr, cx - size * 0.14, cy + size * 0.22);
	cairo_close_path(cr);
	cairo_set_source_rgba(cr, 0.12, 0.12, 0.16, 0.95);
	cairo_fill(cr);
}

void diamonds_draw_vector_store(cairo_t *cr, double cx, double cy, double size) {
	double x = cx - size / 2.0;
	double y = cy - size / 2.0;

	/* Shopping Bag body (Teal #0D9488) */
	diamonds_draw_rounded_rect(cr, x + size * 0.12, y + size * 0.28, size * 0.76, size * 0.64, 4.0);
	cairo_set_source_rgba(cr, 0.08, 0.62, 0.52, 1.0);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.25, 0.78, 0.68, 0.9);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* Dual arched handles */
	cairo_new_path(cr);
	cairo_arc(cr, cx, y + size * 0.28, size * 0.20, M_PI, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.88, 0.94, 0.92, 1.0);
	cairo_set_line_width(cr, size * 0.08);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_stroke(cr);

	/* Center diamond badge */
	diamonds_draw_vector_diamond_logo(cr, cx, cy + size * 0.12, size * 0.32);
}

void diamonds_draw_vector_clock(cairo_t *cr, double cx, double cy, double size) {
	double r = size * 0.44;

	/* Dial background */
	cairo_arc(cr, cx, cy, r, 0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.12, 0.15, 0.22, 1.0);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.45, 0.52, 0.64, 0.9);
	cairo_set_line_width(cr, size * 0.06);
	cairo_stroke(cr);

	/* 4 Hour tick marks */
	for (int i = 0; i < 4; i++) {
		double angle = (double)i * (M_PI / 2.0);
		double tx1 = cx + (r - size * 0.10) * cos(angle);
		double ty1 = cy + (r - size * 0.10) * sin(angle);
		double tx2 = cx + (r - size * 0.04) * cos(angle);
		double ty2 = cy + (r - size * 0.04) * sin(angle);
		cairo_move_to(cr, tx1, ty1);
		cairo_line_to(cr, tx2, ty2);
		cairo_set_source_rgba(cr, 0.70, 0.75, 0.85, 0.8);
		cairo_set_line_width(cr, size * 0.05);
		cairo_stroke(cr);
	}

	/* Hour hand (points to ~10:00) */
	cairo_move_to(cr, cx, cy);
	cairo_line_to(cr, cx - r * 0.45, cy - r * 0.35);
	cairo_set_source_rgba(cr, 0.95, 0.96, 0.98, 1.0);
	cairo_set_line_width(cr, size * 0.07);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_stroke(cr);

	/* Minute hand (points to ~2:00) */
	cairo_move_to(cr, cx, cy);
	cairo_line_to(cr, cx + r * 0.50, cy - r * 0.50);
	cairo_set_source_rgba(cr, 0.2196, 0.7412, 0.9725, 1.0);
	cairo_set_line_width(cr, size * 0.06);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_stroke(cr);

	/* Center pivot pin */
	cairo_arc(cr, cx, cy, size * 0.06, 0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.2196, 0.7412, 0.9725, 1.0);
	cairo_fill(cr);
}

void diamonds_draw_vector_mail(cairo_t *cr, double cx, double cy, double size) {
	double x = cx - size / 2.0;
	double y = cy - size / 2.0;

	/* Envelope body (Warm Coral #EA580C) */
	diamonds_draw_rounded_rect(cr, x + size * 0.08, y + size * 0.18, size * 0.84, size * 0.64, 4.0);
	cairo_set_source_rgba(cr, 0.92, 0.38, 0.12, 1.0);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.98, 0.55, 0.32, 0.9);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* Letter insert */
	diamonds_draw_rounded_rect(cr, x + size * 0.16, y + size * 0.10, size * 0.68, size * 0.32, 2.0);
	cairo_set_source_rgba(cr, 0.96, 0.97, 1.0, 0.95);
	cairo_fill(cr);

	/* Triangular flap */
	cairo_new_path(cr);
	cairo_move_to(cr, x + size * 0.08, y + size * 0.22);
	cairo_line_to(cr, cx, cy + size * 0.12);
	cairo_line_to(cr, x + size * 0.92, y + size * 0.22);
	cairo_set_source_rgba(cr, 1.0, 0.85, 0.75, 1.0);
	cairo_set_line_width(cr, size * 0.06);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_stroke(cr);
}

void diamonds_draw_vector_settings(cairo_t *cr, double cx, double cy, double size) {
	double outer_r = size * 0.42;
	double inner_r = size * 0.32;
	double hole_r  = size * 0.16;
	int teeth = 6;

	cairo_save(cr);
	cairo_translate(cr, cx, cy);

	/* Draw gear teeth */
	cairo_new_path(cr);
	for (int i = 0; i < teeth; i++) {
		double a0 = (double)i * (2.0 * M_PI / teeth);
		double a1 = a0 + (0.4 * M_PI / teeth);
		double a2 = a0 + (0.7 * M_PI / teeth);
		double a3 = a0 + (1.3 * M_PI / teeth);
		double a4 = a0 + (1.6 * M_PI / teeth);

		if (i == 0) {
			cairo_move_to(cr, outer_r * cos(a1), outer_r * sin(a1));
		} else {
			cairo_line_to(cr, outer_r * cos(a1), outer_r * sin(a1));
		}
		cairo_line_to(cr, outer_r * cos(a2), outer_r * sin(a2));
		cairo_line_to(cr, inner_r * cos(a3), inner_r * sin(a3));
		cairo_line_to(cr, inner_r * cos(a4), inner_r * sin(a4));
	}
	cairo_close_path(cr);

	cairo_set_source_rgba(cr, 0.42, 0.46, 0.58, 1.0);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.68, 0.74, 0.86, 0.9);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* Inner hole cutout */
	cairo_arc(cr, 0, 0, hole_r, 0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.10, 0.11, 0.16, 1.0);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.2196, 0.7412, 0.9725, 0.8);
	cairo_set_line_width(cr, 1.2);
	cairo_stroke(cr);

	cairo_restore(cr);
}

void diamonds_draw_vector_trash(cairo_t *cr, double cx, double cy, double size) {
	double x = cx - size / 2.0;
	double y = cy - size / 2.0;

	/* Lid handle */
	diamonds_draw_rounded_rect(cr, cx - size * 0.14, y + size * 0.08, size * 0.28, size * 0.10, 1.5);
	cairo_set_source_rgba(cr, 0.75, 0.80, 0.90, 1.0);
	cairo_fill(cr);

	/* Lid brim */
	diamonds_draw_rounded_rect(cr, x + size * 0.08, y + size * 0.18, size * 0.84, size * 0.10, 1.5);
	cairo_set_source_rgba(cr, 0.75, 0.80, 0.90, 1.0);
	cairo_fill(cr);

	/* Can body (tapering downwards) */
	cairo_new_path(cr);
	cairo_move_to(cr, x + size * 0.16, y + size * 0.30);
	cairo_line_to(cr, x + size * 0.84, y + size * 0.30);
	cairo_line_to(cr, x + size * 0.76, y + size * 0.92);
	cairo_line_to(cr, x + size * 0.24, y + size * 0.92);
	cairo_close_path(cr);

	cairo_set_source_rgba(cr, 0.25, 0.28, 0.36, 1.0);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.60, 0.66, 0.78, 0.9);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* 3 Vertical flutes */
	cairo_set_source_rgba(cr, 0.75, 0.80, 0.90, 0.7);
	cairo_set_line_width(cr, size * 0.05);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

	cairo_move_to(cr, cx - size * 0.14, y + size * 0.40);
	cairo_line_to(cr, cx - size * 0.11, y + size * 0.82);
	cairo_stroke(cr);

	cairo_move_to(cr, cx, y + size * 0.40);
	cairo_line_to(cr, cx, y + size * 0.82);
	cairo_stroke(cr);

	cairo_move_to(cr, cx + size * 0.14, y + size * 0.40);
	cairo_line_to(cr, cx + size * 0.11, y + size * 0.82);
	cairo_stroke(cr);
}

void diamonds_draw_vector_avatar(cairo_t *cr, double cx, double cy, double radius) {
	/* Background circular badge */
	cairo_arc(cr, cx, cy, radius, 0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.14, 0.18, 0.26, 1.0);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.2196, 0.7412, 0.9725, 0.9); /* Cyan rim */
	cairo_set_line_width(cr, 1.5);
	cairo_stroke(cr);

	/* Head circle */
	double head_r = radius * 0.36;
	double head_y = cy - radius * 0.22;
	cairo_arc(cr, cx, head_y, head_r, 0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 0.82, 0.88, 0.96, 1.0);
	cairo_fill(cr);

	/* Shoulders parabola / arc */
	cairo_new_path(cr);
	cairo_arc(cr, cx, cy + radius * 0.65, radius * 0.58, M_PI * 1.15, M_PI * 1.85);
	cairo_set_source_rgba(cr, 0.82, 0.88, 0.96, 1.0);
	cairo_fill(cr);
}

void diamonds_draw_vector_document(cairo_t *cr, double x, double y, double w, double h, const char *kind) {
	double dog_ear = 6.0;

	/* Document body with folded corner */
	cairo_new_path(cr);
	cairo_move_to(cr, x, y);
	cairo_line_to(cr, x + w - dog_ear, y);
	cairo_line_to(cr, x + w, y + dog_ear);
	cairo_line_to(cr, x + w, y + h);
	cairo_line_to(cr, x, y + h);
	cairo_close_path(cr);

	cairo_set_source_rgba(cr, 0.16, 0.18, 0.26, 0.95);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.35, 0.40, 0.55, 0.8);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* Dog-ear triangle fold */
	cairo_new_path(cr);
	cairo_move_to(cr, x + w - dog_ear, y);
	cairo_line_to(cr, x + w - dog_ear, y + dog_ear);
	cairo_line_to(cr, x + w, y + dog_ear);
	cairo_close_path(cr);
	cairo_set_source_rgba(cr, 0.45, 0.52, 0.68, 1.0);
	cairo_fill(cr);

	/* Kind-specific badge */
	if (kind && strcmp(kind, "pdf") == 0) {
		/* Red PDF tag */
		diamonds_draw_rounded_rect(cr, x + 2.0, y + h - 10.0, w - 4.0, 7.0, 1.5);
		cairo_set_source_rgba(cr, 0.90, 0.25, 0.25, 1.0);
		cairo_fill(cr);
	} else if (kind && strcmp(kind, "c") == 0) {
		/* Cyan source code symbol {} */
		diamonds_draw_rounded_rect(cr, x + 2.0, y + h - 10.0, w - 4.0, 7.0, 1.5);
		cairo_set_source_rgba(cr, 0.10, 0.65, 0.85, 1.0);
		cairo_fill(cr);
	} else {
		/* Document text lines */
		cairo_set_source_rgba(cr, 0.65, 0.72, 0.85, 0.8);
		cairo_set_line_width(cr, 1.0);
		cairo_move_to(cr, x + 3.0, y + 8.0);
		cairo_line_to(cr, x + w - 4.0, y + 8.0);
		cairo_stroke(cr);
		cairo_move_to(cr, x + 3.0, y + 12.0);
		cairo_line_to(cr, x + w - 4.0, y + 12.0);
		cairo_stroke(cr);
	}
}
