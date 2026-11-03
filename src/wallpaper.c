#define _GNU_SOURCE
#include "wallpaper.h"
#include "diamonds.h"
#include "icon_loader.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <pixman.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct diamonds_wallpaper *diamonds_wallpaper_create(struct diamonds_server *server) {
	struct diamonds_wallpaper *wp = calloc(1, sizeof(*wp));
	if (!wp) return NULL;

	wp->server = server;
	wp->width = 1920;
	wp->height = 1080;
	wp->image_path[0] = '\0';

	/* Check if default wallpaper image exists on disk */
	static const char *default_wallpapers[] = {
		"/usr/share/backgrounds/armbian/armbian03-Dre0x-Minum-dark-3840x2160.jpg",
		"/usr/share/backgrounds/ubuntu-wallpaper-d.png",
		"/usr/share/backgrounds/warty-final-ubuntu.png",
		"/usr/share/backgrounds/Fuji_san_by_amaral.png",
		NULL
	};

	for (int i = 0; default_wallpapers[i]; i++) {
		if (access(default_wallpapers[i], R_OK) == 0) {
			snprintf(wp->image_path, sizeof(wp->image_path), "%s", default_wallpapers[i]);
			break;
		}
	}

	wp->buffer = diamonds_cairo_buffer_create(wp->width, wp->height);
	if (!wp->buffer) {
		free(wp);
		return NULL;
	}

	wp->scene_buffer = wlr_scene_buffer_create(server->tree_background, &wp->buffer->base);
	if (!wp->scene_buffer) {
		wlr_buffer_drop(&wp->buffer->base);
		free(wp);
		return NULL;
	}

	wlr_scene_node_set_position(&wp->scene_buffer->node, 0, 0);
	diamonds_wallpaper_render(wp);
	return wp;
}

void diamonds_wallpaper_destroy(struct diamonds_wallpaper *wp) {
	if (!wp) return;
	if (wp->scene_buffer) {
		wlr_scene_node_destroy(&wp->scene_buffer->node);
	}
	if (wp->buffer) {
		wlr_buffer_drop(&wp->buffer->base);
	}
	free(wp);
}

void diamonds_wallpaper_reposition(struct diamonds_wallpaper *wp, int32_t width, int32_t height) {
	if (!wp || width <= 0 || height <= 0) return;
	if (wp->width != width || wp->height != height) {
		wp->width = width;
		wp->height = height;
		diamonds_cairo_buffer_resize(&wp->buffer, width, height);
		if (wp->buffer && wp->scene_buffer) {
			wlr_scene_buffer_set_buffer(wp->scene_buffer, &wp->buffer->base);
		}
		diamonds_wallpaper_render(wp);
	}
}

bool diamonds_wallpaper_set_image(struct diamonds_wallpaper *wp, const char *path) {
	if (!wp) return false;
	if (path && access(path, R_OK) == 0) {
		snprintf(wp->image_path, sizeof(wp->image_path), "%s", path);
	} else {
		wp->image_path[0] = '\0';
	}
	diamonds_wallpaper_render(wp);
	return true;
}

void diamonds_wallpaper_render(struct diamonds_wallpaper *wp) {
	if (!wp || !wp->buffer || !wp->buffer->cairo_surface) return;

	cairo_t *cr = cairo_create(wp->buffer->cairo_surface);
	if (!cr) return;

	double w = (double)wp->width;
	double h = (double)wp->height;

	/* 1. Try loading background image from disk */
	bool image_drawn = false;
	if (wp->image_path[0] != '\0' && access(wp->image_path, R_OK) == 0) {
		GError *err = NULL;
		GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(wp->image_path, wp->width, wp->height, FALSE, &err);
		if (pb) {
			int pw = gdk_pixbuf_get_width(pb);
			int ph = gdk_pixbuf_get_height(pb);
			int n_channels = gdk_pixbuf_get_n_channels(pb);
			int rowstride = gdk_pixbuf_get_rowstride(pb);
			const guchar *src_pixels = gdk_pixbuf_get_pixels(pb);

			cairo_surface_t *img_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
			if (img_surf && cairo_surface_status(img_surf) == CAIRO_STATUS_SUCCESS) {
				unsigned char *dst_data = cairo_image_surface_get_data(img_surf);
				int dst_stride = cairo_image_surface_get_stride(img_surf);

				for (int y = 0; y < ph; y++) {
					const guchar *src = src_pixels + (y * rowstride);
					uint32_t *dst = (uint32_t *)(dst_data + (y * dst_stride));
					for (int x = 0; x < pw; x++) {
						uint32_t r = src[0];
						uint32_t g = src[1];
						uint32_t b = src[2];
						uint32_t a = (n_channels == 4) ? src[3] : 255;
						src += n_channels;
						if (a != 255) {
							r = (r * a + 127) / 255;
							g = (g * a + 127) / 255;
							b = (b * a + 127) / 255;
						}
						dst[x] = (a << 24) | (r << 16) | (g << 8) | b;
					}
				}
				cairo_surface_mark_dirty(img_surf);
				cairo_set_source_surface(cr, img_surf, 0, 0);
				cairo_paint(cr);
				cairo_surface_destroy(img_surf);
				image_drawn = true;
			}
			g_object_unref(pb);
		} else if (err) {
			g_error_free(err);
		}
	}

	/* 2. Mathematical Geometric Mica Wallpaper fallback */
	if (!image_drawn) {
		/* Deep space dark gradient background */
		cairo_pattern_t *pat = cairo_pattern_create_linear(0, 0, w, h);
		cairo_pattern_add_color_stop_rgb(pat, 0.0, 0.043, 0.051, 0.082); /* Deep Midnight */
		cairo_pattern_add_color_stop_rgb(pat, 0.35, 0.066, 0.078, 0.125); /* Graphite Blue */
		cairo_pattern_add_color_stop_rgb(pat, 0.70, 0.078, 0.098, 0.156); /* Slate Mica */
		cairo_pattern_add_color_stop_rgb(pat, 1.0, 0.035, 0.043, 0.070);
		cairo_set_source(cr, pat);
		cairo_paint(cr);
		cairo_pattern_destroy(pat);

		/* Central soft glowing radial aura */
		double cx = w / 2.0;
		double cy = h / 2.0;
		double radius = fmax(w, h) * 0.55;
		cairo_pattern_t *radial = cairo_pattern_create_radial(cx, cy, 10.0, cx, cy, radius);
		cairo_pattern_add_color_stop_rgba(radial, 0.0, 0.22, 0.74, 0.97, 0.09); /* Cyan glow */
		cairo_pattern_add_color_stop_rgba(radial, 0.45, 0.39, 0.40, 0.95, 0.05); /* Indigo glow */
		cairo_pattern_add_color_stop_rgba(radial, 1.0, 0.0, 0.0, 0.0, 0.0);
		cairo_set_source(cr, radial);
		cairo_paint(cr);
		cairo_pattern_destroy(radial);

		/* Mathematical Diamond facets (Vector polygons) */
		/* Facet 1: Top Left Diamond wing */
		cairo_new_path(cr);
		cairo_move_to(cr, cx, cy - 220.0);
		cairo_line_to(cr, cx - 260.0, cy);
		cairo_line_to(cr, cx, cy + 180.0);
		cairo_close_path(cr);
		cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.04);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.12);
		cairo_set_line_width(cr, 1.2);
		cairo_stroke(cr);

		/* Facet 2: Top Right Diamond wing */
		cairo_new_path(cr);
		cairo_move_to(cr, cx, cy - 220.0);
		cairo_line_to(cr, cx + 260.0, cy);
		cairo_line_to(cr, cx, cy + 180.0);
		cairo_close_path(cr);
		cairo_set_source_rgba(cr, 0.39, 0.40, 0.95, 0.05);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0.39, 0.40, 0.95, 0.14);
		cairo_set_line_width(cr, 1.2);
		cairo_stroke(cr);

		/* Central diamond prism */
		cairo_new_path(cr);
		cairo_move_to(cr, cx, cy - 120.0);
		cairo_line_to(cr, cx + 130.0, cy);
		cairo_line_to(cr, cx, cy + 120.0);
		cairo_line_to(cr, cx - 130.0, cy);
		cairo_close_path(cr);
		cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.06);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.25);
		cairo_set_line_width(cr, 1.5);
		cairo_stroke(cr);

		/* Inner concentric diamond */
		cairo_new_path(cr);
		cairo_move_to(cr, cx, cy - 60.0);
		cairo_line_to(cr, cx + 65.0, cy);
		cairo_line_to(cr, cx, cy + 60.0);
		cairo_line_to(cr, cx - 65.0, cy);
		cairo_close_path(cr);
		cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.08);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0.90, 0.95, 1.0, 0.35);
		cairo_set_line_width(cr, 1.0);
		cairo_stroke(cr);
	}

	cairo_destroy(cr);

	pixman_region32_t damage;
	pixman_region32_init_rect(&damage, 0, 0, (uint32_t)wp->width, (uint32_t)wp->height);
	wlr_scene_buffer_set_buffer_with_damage(wp->scene_buffer, &wp->buffer->base, &damage);
	pixman_region32_fini(&damage);
}
