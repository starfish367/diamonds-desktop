#define _GNU_SOURCE
#include "diamonds_app.h"
#include "xdg-shell-client-protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <errno.h>
#include <wayland-client.h>

struct diamonds_client_app {
	const char *title;
	const char *app_id;
	int width;
	int height;
	diamonds_draw_fn draw_fn;
	void *user_data;

	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;

	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;

	struct wl_buffer *wl_buf;
	cairo_surface_t *cairo_surf;
	void *shm_data;
	int shm_fd;
	size_t shm_size;

	bool configured;
	bool running;
};

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

static int create_shm_file(size_t size) {
	int fd = -1;
#if defined(__NR_memfd_create)
	fd = (int)syscall(__NR_memfd_create, "diamonds-shm", MFD_CLOEXEC);
#endif
	if (fd < 0) {
		char path[] = "/dev/shm/diamonds-XXXXXX";
		fd = mkstemp(path);
		if (fd >= 0) {
			unlink(path);
		}
	}
	if (fd >= 0) {
		if (ftruncate(fd, (off_t)size) != 0) {
			close(fd);
			return -1;
		}
	}
	return fd;
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
	(void)data;
	xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	.ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
	struct diamonds_client_app *app = data;
	xdg_surface_ack_configure(xdg_surface, serial);
	app->configured = true;
	diamonds_client_app_redraw(app);
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
		int32_t width, int32_t height, struct wl_array *states) {
	(void)data;
	(void)toplevel;
	(void)width;
	(void)height;
	(void)states;
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
	(void)toplevel;
	struct diamonds_client_app *app = data;
	app->running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_configure,
	.close = xdg_toplevel_close,
};

static void registry_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	(void)version;
	struct diamonds_client_app *app = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		app->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		app->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
	}
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

struct diamonds_client_app *diamonds_client_app_create(
		const char *title, const char *app_id, int width, int height,
		diamonds_draw_fn draw_fn, void *user_data) {
	struct diamonds_client_app *app = calloc(1, sizeof(*app));
	if (!app) return NULL;

	app->title = title;
	app->app_id = app_id;
	app->width = width;
	app->height = height;
	app->draw_fn = draw_fn;
	app->user_data = user_data;
	app->running = true;

	app->display = wl_display_connect(NULL);
	if (!app->display) {
		/* Return allocated app in headless mode */
		return app;
	}

	app->registry = wl_display_get_registry(app->display);
	wl_registry_add_listener(app->registry, &registry_listener, app);
	wl_display_roundtrip(app->display);

	if (!app->compositor || !app->shm || !app->wm_base) {
		return app;
	}

	/* Allocate shm buffer */
	int stride = width * 4;
	app->shm_size = (size_t)(stride * height);
	app->shm_fd = create_shm_file(app->shm_size);
	if (app->shm_fd < 0) return app;

	app->shm_data = mmap(NULL, app->shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, app->shm_fd, 0);
	if (app->shm_data == MAP_FAILED) return app;

	struct wl_shm_pool *pool = wl_shm_create_pool(app->shm, app->shm_fd, (int32_t)app->shm_size);
	app->wl_buf = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);

	app->cairo_surf = cairo_image_surface_create_for_data(
		(unsigned char *)app->shm_data, CAIRO_FORMAT_ARGB32, width, height, stride);

	/* Create surface and toplevel */
	app->surface = wl_compositor_create_surface(app->compositor);
	app->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base, app->surface);
	xdg_surface_add_listener(app->xdg_surface, &xdg_surface_listener, app);

	app->xdg_toplevel = xdg_surface_get_toplevel(app->xdg_surface);
	xdg_toplevel_add_listener(app->xdg_toplevel, &xdg_toplevel_listener, app);

	xdg_toplevel_set_title(app->xdg_toplevel, title);
	xdg_toplevel_set_app_id(app->xdg_toplevel, app_id);

	wl_surface_commit(app->surface);
	wl_display_roundtrip(app->display);

	return app;
}

void diamonds_client_app_redraw(struct diamonds_client_app *app) {
	if (!app || !app->cairo_surf || !app->surface) return;

	cairo_t *cr = cairo_create(app->cairo_surf);
	if (cr) {
		if (app->draw_fn) {
			app->draw_fn(cr, app->width, app->height, app->user_data);
		}
		cairo_destroy(cr);
	}

	wl_surface_attach(app->surface, app->wl_buf, 0, 0);
	wl_surface_damage_buffer(app->surface, 0, 0, app->width, app->height);
	wl_surface_commit(app->surface);
}

int diamonds_client_app_run(struct diamonds_client_app *app) {
	if (!app) return 1;
	if (!app->display) {
		printf("[%s] Running in headless demo mode (no Wayland display found).\n", app->title);
		return 0;
	}

	while (app->running && wl_display_dispatch(app->display) != -1) {
		/* Event loop */
	}

	return 0;
}

void diamonds_client_app_destroy(struct diamonds_client_app *app) {
	if (!app) return;

	if (app->cairo_surf) cairo_surface_destroy(app->cairo_surf);
	if (app->shm_data && app->shm_data != MAP_FAILED) {
		munmap(app->shm_data, app->shm_size);
	}
	if (app->shm_fd >= 0) close(app->shm_fd);

	if (app->wl_buf) wl_buffer_destroy(app->wl_buf);
	if (app->xdg_toplevel) xdg_toplevel_destroy(app->xdg_toplevel);
	if (app->xdg_surface) xdg_surface_destroy(app->xdg_surface);
	if (app->surface) wl_surface_destroy(app->surface);

	if (app->wm_base) xdg_wm_base_destroy(app->wm_base);
	if (app->shm) wl_shm_destroy(app->shm);
	if (app->compositor) wl_compositor_destroy(app->compositor);
	if (app->registry) wl_registry_destroy(app->registry);
	if (app->display) wl_display_disconnect(app->display);

	free(app);
}
