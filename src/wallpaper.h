#ifndef DIAMONDS_WALLPAPER_H
#define DIAMONDS_WALLPAPER_H

#include <stdint.h>
#include <stdbool.h>
#include <wlr/types/wlr_scene.h>
#include "cairo_buffer.h"

struct diamonds_server;

struct diamonds_wallpaper {
	struct diamonds_server *server;
	struct wlr_scene_buffer *scene_buffer;
	struct diamonds_cairo_buffer *buffer;
	int32_t width;
	int32_t height;
	char image_path[512];
};

struct diamonds_wallpaper *diamonds_wallpaper_create(struct diamonds_server *server);
void diamonds_wallpaper_destroy(struct diamonds_wallpaper *wp);
void diamonds_wallpaper_reposition(struct diamonds_wallpaper *wp, int32_t width, int32_t height);
void diamonds_wallpaper_render(struct diamonds_wallpaper *wp);
bool diamonds_wallpaper_set_image(struct diamonds_wallpaper *wp, const char *path);

#endif /* DIAMONDS_WALLPAPER_H */
