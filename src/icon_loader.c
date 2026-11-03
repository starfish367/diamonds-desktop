#define _GNU_SOURCE
#include "icon_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#define MAX_CACHE 512

struct icon_cache_entry {
	char key[128];
	int size;
	cairo_surface_t *surface;
};

static struct icon_cache_entry g_cache[MAX_CACHE];
static int g_cache_count = 0;

void diamonds_icon_loader_init(void) {
	/* gdk-pixbuf does not require explicit init in 2.0 */
}

void diamonds_icon_loader_cleanup(void) {
	for (int i = 0; i < g_cache_count; i++) {
		if (g_cache[i].surface) {
			cairo_surface_destroy(g_cache[i].surface);
			g_cache[i].surface = NULL;
		}
	}
	g_cache_count = 0;
}

static cairo_surface_t *pixbuf_to_cairo_surface(GdkPixbuf *pixbuf) {
	if (!pixbuf) return NULL;

	int w = gdk_pixbuf_get_width(pixbuf);
	int h = gdk_pixbuf_get_height(pixbuf);
	int n_channels = gdk_pixbuf_get_n_channels(pixbuf);
	int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
	const guchar *src_pixels = gdk_pixbuf_get_pixels(pixbuf);

	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
		return NULL;
	}

	unsigned char *dst_data = cairo_image_surface_get_data(surf);
	int dst_stride = cairo_image_surface_get_stride(surf);

	for (int y = 0; y < h; y++) {
		const guchar *src = src_pixels + (y * rowstride);
		uint32_t *dst = (uint32_t *)(dst_data + (y * dst_stride));
		for (int x = 0; x < w; x++) {
			uint32_t r = src[0];
			uint32_t g = src[1];
			uint32_t b = src[2];
			uint32_t a = (n_channels == 4) ? src[3] : 255;
			src += n_channels;

			/* Cairo ARGB32 requires pre-multiplied alpha */
			if (a != 255) {
				r = (r * a + 127) / 255;
				g = (g * a + 127) / 255;
				b = (b * a + 127) / 255;
			}
			dst[x] = (a << 24) | (r << 16) | (g << 8) | b;
		}
	}

	cairo_surface_mark_dirty(surf);
	return surf;
}

static const char *search_dirs[] = {
	"/usr/share/icons/hicolor/256x256/apps",
	"/usr/share/icons/hicolor/128x128/apps",
	"/usr/share/icons/hicolor/64x64/apps",
	"/usr/share/icons/hicolor/48x48/apps",
	"/usr/share/icons/hicolor/32x32/apps",
	"/usr/share/icons/hicolor/scalable/apps",
	"/usr/share/icons/hicolor/scalable/categories",
	"/usr/share/icons/hicolor/scalable/devices",
	"/usr/share/icons/hicolor/scalable/status",
	"/usr/share/icons/Yaru/256x256/apps",
	"/usr/share/icons/Yaru/48x48/apps",
	"/usr/share/icons/Yaru/256x256/places",
	"/usr/share/icons/Adwaita/scalable/apps",
	"/usr/share/icons/Adwaita/48x48/apps",
	"/usr/share/icons/Adwaita/32x32/apps",
	"/usr/share/icons/Adwaita/scalable/places",
	"/usr/share/pixmaps",
	NULL
};

static const char *extensions[] = { "", ".png", ".svg", ".xpm" };

static char *find_icon_file(const char *name) {
	if (!name || !*name) return NULL;

	/* If direct path */
	if (name[0] == '/' && access(name, R_OK) == 0) {
		return strdup(name);
	}

	char test_path[1024];

	/* Check user icons first */
	const char *home = getenv("HOME");
	if (home) {
		const char *user_subdirs[] = {
			"/.local/share/icons/hicolor/scalable/apps",
			"/.local/share/icons/hicolor/48x48/apps",
			"/.local/share/icons/hicolor/32x32/apps",
			"/.icons",
			NULL
		};
		for (int u = 0; user_subdirs[u]; u++) {
			for (int e = 0; extensions[e]; e++) {
				snprintf(test_path, sizeof(test_path), "%s%s/%s%s", home, user_subdirs[u], name, extensions[e]);
				if (access(test_path, R_OK) == 0) {
					return strdup(test_path);
				}
			}
		}
	}

	for (int d = 0; search_dirs[d]; d++) {
		for (int e = 0; extensions[e]; e++) {
			snprintf(test_path, sizeof(test_path), "%s/%s%s", search_dirs[d], name, extensions[e]);
			if (access(test_path, R_OK) == 0) {
				return strdup(test_path);
			}
		}
	}
	return NULL;
}

cairo_surface_t *diamonds_load_system_icon(const char *app_key, int size) {
	if (!app_key || !*app_key || size <= 0) return NULL;

	/* Check cache */
	for (int i = 0; i < g_cache_count; i++) {
		if (g_cache[i].size == size && strcasecmp(g_cache[i].key, app_key) == 0) {
			return g_cache[i].surface;
		}
	}

	/* Candidate names list for generic keywords */
	static const char *term_names[]  = { "terminator", "xfce4-terminal", "org.gnome.Terminal", "utilities-terminal", "terminal", "xterm", NULL };
	static const char *files_names[] = { "system-file-manager", "org.gnome.Nautilus", "nautilus", "thunar", "file-manager", "folder", NULL };
	static const char *brow_names[]  = { "thorium", "thorium-browser", "chromium", "google-chrome", "firefox", "web-browser", NULL };
	static const char *media_names[] = { "vlc", "multimedia-video-player", "vlc-xmas", "totem", "mpv", NULL };
	static const char *sett_names[]  = { "system-settings", "preferences-system", "gnome-control-center", "systemsettings", NULL };
	static const char *trash_names[] = { "user-trash", "trash-can", "user-trash-full", "user-trash-empty", NULL };

	const char **candidates = NULL;
	if (strcasestr(app_key, "term") || strcasestr(app_key, "bash")) {
		candidates = term_names;
	} else if (strcasestr(app_key, "file") || strcasestr(app_key, "nautilus")) {
		candidates = files_names;
	} else if (strcasestr(app_key, "browser") || strcasestr(app_key, "thorium") || strcasestr(app_key, "chrome")) {
		candidates = brow_names;
	} else if (strcasestr(app_key, "media") || strcasestr(app_key, "vlc") || strcasestr(app_key, "video")) {
		candidates = media_names;
	} else if (strcasestr(app_key, "sett") || strcasestr(app_key, "control") || strcasestr(app_key, "prefer")) {
		candidates = sett_names;
	} else if (strcasestr(app_key, "trash")) {
		candidates = trash_names;
	}

	char *icon_path = NULL;
	if (candidates) {
		for (int i = 0; candidates[i]; i++) {
			icon_path = find_icon_file(candidates[i]);
			if (icon_path) break;
		}
	}
	if (!icon_path) {
		icon_path = find_icon_file(app_key);
	}

	if (!icon_path) {
		return NULL;
	}

	GError *error = NULL;
	GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(icon_path, size, size, TRUE, &error);
	free(icon_path);

	if (!pb) {
		if (error) g_error_free(error);
		return NULL;
	}

	cairo_surface_t *surf = pixbuf_to_cairo_surface(pb);
	g_object_unref(pb);

	if (surf && g_cache_count < MAX_CACHE) {
		snprintf(g_cache[g_cache_count].key, sizeof(g_cache[g_cache_count].key), "%s", app_key);
		g_cache[g_cache_count].size = size;
		g_cache[g_cache_count].surface = surf;
		g_cache_count++;
	}

	return surf;
}

cairo_surface_t *diamonds_load_desktop_icon(const char *icon_key, const char *app_name, int size) {
	if (size <= 0) return NULL;
	if ((!icon_key || !*icon_key) && (!app_name || !*app_name)) return NULL;

	/* 1. Check cache by icon_key if present */
	if (icon_key && *icon_key) {
		for (int i = 0; i < g_cache_count; i++) {
			if (g_cache[i].size == size && strcasecmp(g_cache[i].key, icon_key) == 0) {
				return g_cache[i].surface;
			}
		}
	}

	/* 2. Check cache by app_name */
	if (app_name && *app_name) {
		for (int i = 0; i < g_cache_count; i++) {
			if (g_cache[i].size == size && strcasecmp(g_cache[i].key, app_name) == 0) {
				return g_cache[i].surface;
			}
		}
	}

	char *icon_path = NULL;

	/* 3. Try to locate icon_key file */
	if (icon_key && *icon_key) {
		icon_path = find_icon_file(icon_key);

		/* Try without extension if icon_key has one */
		if (!icon_path) {
			const char *dot = strrchr(icon_key, '.');
			if (dot && (strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".svg") == 0 || strcasecmp(dot, ".xpm") == 0)) {
				char base_name[128];
				size_t blen = (size_t)(dot - icon_key);
				if (blen < sizeof(base_name)) {
					memcpy(base_name, icon_key, blen);
					base_name[blen] = '\0';
					icon_path = find_icon_file(base_name);
				}
			}
		}

		/* Try lowercase */
		if (!icon_path) {
			char lower[128];
			size_t klen = strlen(icon_key);
			if (klen < sizeof(lower)) {
				for (size_t j = 0; j <= klen; j++) {
					lower[j] = (char)g_ascii_tolower(icon_key[j]);
				}
				icon_path = find_icon_file(lower);
			}
		}
	}

	/* 4. If not found, try app_name */
	if (!icon_path && app_name && *app_name) {
		icon_path = find_icon_file(app_name);
	}

	/* 5. If found, load via GdkPixbuf and convert to Cairo ARGB32 */
	if (icon_path) {
		GError *error = NULL;
		GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(icon_path, size, size, TRUE, &error);
		free(icon_path);

		if (pb) {
			cairo_surface_t *surf = pixbuf_to_cairo_surface(pb);
			g_object_unref(pb);

			if (surf && g_cache_count < MAX_CACHE) {
				const char *save_key = (icon_key && *icon_key) ? icon_key : app_name;
				snprintf(g_cache[g_cache_count].key, sizeof(g_cache[g_cache_count].key), "%s", save_key);
				g_cache[g_cache_count].size = size;
				g_cache[g_cache_count].surface = surf;
				g_cache_count++;
			}
			return surf;
		}
		if (error) g_error_free(error);
	}

	/* 6. Fallback to generic system keyword search */
	if (app_name && *app_name) {
		cairo_surface_t *fallback = diamonds_load_system_icon(app_name, size);
		if (fallback) return fallback;
	}
	if (icon_key && *icon_key) {
		cairo_surface_t *fallback = diamonds_load_system_icon(icon_key, size);
		if (fallback) return fallback;
	}

	return NULL;
}
