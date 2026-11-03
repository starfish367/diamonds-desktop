#ifndef DIAMONDS_APP_H
#define DIAMONDS_APP_H

#include <stdint.h>
#include <stdbool.h>
#include <cairo.h>

struct diamonds_client_app;

typedef void (*diamonds_draw_fn)(cairo_t *cr, int width, int height, void *user_data);

struct diamonds_client_app *diamonds_client_app_create(
	const char *title,
	const char *app_id,
	int width,
	int height,
	diamonds_draw_fn draw_fn,
	void *user_data
);

int diamonds_client_app_run(struct diamonds_client_app *app);
void diamonds_client_app_redraw(struct diamonds_client_app *app);
void diamonds_client_app_destroy(struct diamonds_client_app *app);

#endif /* DIAMONDS_APP_H */
