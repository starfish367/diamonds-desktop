#include "cairo_buffer.h"

#include <stdlib.h>
#include <math.h>
#include <drm_fourcc.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void cairo_buffer_destroy(struct wlr_buffer *wlr_buf) {
	struct diamonds_cairo_buffer *buf = wl_container_of(wlr_buf, buf, base);
	if (buf->cairo_surface) {
		cairo_surface_destroy(buf->cairo_surface);
	}
	free(buf->pixels);
	free(buf);
}

static bool cairo_buffer_begin_data_ptr_access(
	struct wlr_buffer *wlr_buf, uint32_t flags,
	void **data, uint32_t *format, size_t *stride) {
	(void)flags;
	struct diamonds_cairo_buffer *buf = wl_container_of(wlr_buf, buf, base);
	*data = buf->pixels;
	*stride = buf->stride;
	*format = DRM_FORMAT_ARGB8888;
	return true;
}

static void cairo_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buf) {
	(void)wlr_buf;
}

static const struct wlr_buffer_impl cairo_buffer_impl = {
	.destroy = cairo_buffer_destroy,
	.begin_data_ptr_access = cairo_buffer_begin_data_ptr_access,
	.end_data_ptr_access = cairo_buffer_end_data_ptr_access,
};

struct diamonds_cairo_buffer *diamonds_cairo_buffer_create(int32_t width, int32_t height) {
	if (width <= 0 || height <= 0) {
		return NULL;
	}

	struct diamonds_cairo_buffer *buf = calloc(1, sizeof(*buf));
	if (!buf) {
		return NULL;
	}

	buf->width = width;
	buf->height = height;
	buf->stride = (size_t)cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
	buf->pixels = calloc((size_t)height, buf->stride);
	if (!buf->pixels) {
		free(buf);
		return NULL;
	}

	buf->cairo_surface = cairo_image_surface_create_for_data(
		(unsigned char *)buf->pixels,
		CAIRO_FORMAT_ARGB32,
		width,
		height,
		(int)buf->stride
	);

	wlr_buffer_init(&buf->base, &cairo_buffer_impl, width, height);
	return buf;
}

void diamonds_cairo_buffer_resize(struct diamonds_cairo_buffer **buf_ptr, int32_t width, int32_t height) {
	if (!buf_ptr) return;
	struct diamonds_cairo_buffer *old_buf = *buf_ptr;
	if (old_buf && old_buf->width == width && old_buf->height == height) {
		return;
	}

	struct diamonds_cairo_buffer *new_buf = diamonds_cairo_buffer_create(width, height);
	if (new_buf) {
		if (old_buf) {
			wlr_buffer_drop(&old_buf->base);
		}
		*buf_ptr = new_buf;
	}
}

void diamonds_draw_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
	if (r <= 0.0) {
		cairo_rectangle(cr, x, y, w, h);
		return;
	}
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0, 0.0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0.0, M_PI / 2.0);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2.0, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
	cairo_close_path(cr);
}
