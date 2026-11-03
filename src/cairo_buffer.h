#ifndef DIAMONDS_CAIRO_BUFFER_H
#define DIAMONDS_CAIRO_BUFFER_H

#ifndef WLR_USE_UNSTABLE
#define WLR_USE_UNSTABLE
#endif
#include <stdint.h>
#include <stdbool.h>
#include <cairo.h>
#include <wlr/interfaces/wlr_buffer.h>

struct diamonds_cairo_buffer {
	struct wlr_buffer base;
	cairo_surface_t *cairo_surface;
	uint32_t *pixels;
	size_t stride;
	int32_t width;
	int32_t height;
};

struct diamonds_cairo_buffer *diamonds_cairo_buffer_create(int32_t width, int32_t height);
void diamonds_cairo_buffer_resize(struct diamonds_cairo_buffer **buf_ptr, int32_t width, int32_t height);

/* Cairo drawing utility helpers */
void diamonds_draw_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r);

#endif /* DIAMONDS_CAIRO_BUFFER_H */
