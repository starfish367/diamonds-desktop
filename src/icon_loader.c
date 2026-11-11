#define _GNU_SOURCE
#include "icon_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <strings.h>
#include <dirent.h>
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

static void app_cache_clear(void);

void diamonds_icon_loader_cleanup(void) {
	app_cache_clear();
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

static const char *extensions[] = { "", ".png", ".svg", ".xpm", NULL };

/* Freedesktop icon theme fallback search (hicolor/breeze/Adwaita/Yaru/Papirus,
 * all common sizes, both "<size>/apps" and breeze-style "apps/<size>" layouts).
 * Needed for Qt/KDE apps whose icons are often only shipped in small hicolor
 * sizes or in the breeze theme. */
static char *find_icon_in_themes(const char *name) {
	static const char *themes[] = {
		"hicolor", "breeze", "breeze-dark", "Papirus", "Papirus-Dark",
		"Adwaita", "Yaru", "Humanity", "default", NULL
	};
	static const char *sizes[] = {
		"scalable", "256x256", "128x128", "96x96", "64x64", "48x48",
		"512x512", "32x32", "24x24", "22x22", "16x16", NULL
	};
	static const char *breeze_sizes[] = { "64", "48", "32", "22", "16", NULL };
	static const char *theme_exts[] = { ".png", ".svg", ".xpm", NULL };

	char bases[6][512];
	int nbases = 0;
	const char *home = getenv("HOME");
	if (home) {
		snprintf(bases[nbases++], sizeof(bases[0]), "%s/.local/share/icons", home);
		snprintf(bases[nbases++], sizeof(bases[0]), "%s/.icons", home);
		snprintf(bases[nbases++], sizeof(bases[0]), "%s/.local/share/flatpak/exports/share/icons", home);
	}
	snprintf(bases[nbases++], sizeof(bases[0]), "%s", "/usr/local/share/icons");
	snprintf(bases[nbases++], sizeof(bases[0]), "%s", "/usr/share/icons");
	snprintf(bases[nbases++], sizeof(bases[0]), "%s", "/var/lib/flatpak/exports/share/icons");

	char path[1024];
	for (int b = 0; b < nbases; b++) {
		for (int t = 0; themes[t]; t++) {
			snprintf(path, sizeof(path), "%s/%s", bases[b], themes[t]);
			if (access(path, R_OK) != 0) continue;
			for (int s = 0; sizes[s]; s++) {
				for (int e = 0; theme_exts[e]; e++) {
					snprintf(path, sizeof(path), "%s/%s/%s/apps/%s%s",
						bases[b], themes[t], sizes[s], name, theme_exts[e]);
					if (access(path, R_OK) == 0) return strdup(path);
				}
			}
			for (int s = 0; breeze_sizes[s]; s++) {
				for (int e = 0; theme_exts[e]; e++) {
					snprintf(path, sizeof(path), "%s/%s/apps/%s/%s%s",
						bases[b], themes[t], breeze_sizes[s], name, theme_exts[e]);
					if (access(path, R_OK) == 0) return strdup(path);
				}
			}
		}
	}
	return NULL;
}

static char *find_icon_file(const char *name) {
	if (!name || !*name) return NULL;

	/* If direct path */
	if (name[0] == '/') {
		return access(name, R_OK) == 0 ? strdup(name) : NULL;
	}
	/* Reject relative paths / traversal in icon names */
	if (strchr(name, '/')) return NULL;

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
	return find_icon_in_themes(name);
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

/* ------------------------------------------------------------------------- */
/* App icon resolution: app_id / WM_CLASS -> .desktop -> Icon= -> file        */
/* ------------------------------------------------------------------------- */

#define MAX_APP_CACHE 128

struct app_icon_cache_entry {
	char app_id[128];
	int size;
	cairo_surface_t *surface; /* NULL = negative (miss) cache */
	bool owned;               /* true if this entry must destroy the surface */
};

static struct app_icon_cache_entry g_app_cache[MAX_APP_CACHE];
static int g_app_cache_count = 0;

static void app_cache_clear(void) {
	for (int i = 0; i < g_app_cache_count; i++) {
		if (g_app_cache[i].owned && g_app_cache[i].surface) {
			cairo_surface_destroy(g_app_cache[i].surface);
		}
		g_app_cache[i].surface = NULL;
		g_app_cache[i].owned = false;
	}
	g_app_cache_count = 0;
}

static char *str_trim(char *s) {
	while (*s == ' ' || *s == '\t') s++;
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
		s[--n] = '\0';
	}
	return s;
}

struct desktop_info {
	char icon[256];
	char wmclass[128];
	char exec_base[128];
};

/* Parse only the [Desktop Entry] group. Returns true if Icon= was found. */
static bool parse_desktop_file(const char *path, struct desktop_info *info) {
	memset(info, 0, sizeof(*info));
	FILE *f = fopen(path, "r");
	if (!f) return false;

	char line[1024];
	bool in_entry = false;
	while (fgets(line, sizeof(line), f)) {
		char *l = str_trim(line);
		if (l[0] == '[') {
			if (in_entry) break; /* next group -> done */
			in_entry = (strcmp(l, "[Desktop Entry]") == 0);
			continue;
		}
		if (!in_entry) continue;

		if (!info->icon[0] && strncmp(l, "Icon=", 5) == 0) {
			snprintf(info->icon, sizeof(info->icon), "%s", str_trim(l + 5));
		} else if (!info->wmclass[0] && strncmp(l, "StartupWMClass=", 15) == 0) {
			snprintf(info->wmclass, sizeof(info->wmclass), "%s", str_trim(l + 15));
		} else if (!info->exec_base[0] && strncmp(l, "Exec=", 5) == 0) {
			char *cmd = str_trim(l + 5);
			/* Skip "env VAR=val ..." prefix */
			if (strncmp(cmd, "env ", 4) == 0) {
				cmd += 4;
				while (*cmd) {
					while (*cmd == ' ') cmd++;
					char *sp = strchr(cmd, ' ');
					char *eq = strchr(cmd, '=');
					if (eq && (!sp || eq < sp)) {
						cmd = sp ? sp : cmd + strlen(cmd);
					} else {
						break;
					}
				}
			}
			char tok[256];
			size_t k = 0;
			if (*cmd == '"') cmd++;
			while (*cmd && *cmd != ' ' && *cmd != '"' && k + 1 < sizeof(tok)) {
				tok[k++] = *cmd++;
			}
			tok[k] = '\0';
			const char *base = strrchr(tok, '/');
			snprintf(info->exec_base, sizeof(info->exec_base), "%s", base ? base + 1 : tok);
		}
	}
	fclose(f);
	return info->icon[0] != '\0';
}

static int collect_app_dirs(char dirs[][512], int max) {
	int n = 0;
	const char *home = getenv("HOME");
	const char *data_home = getenv("XDG_DATA_HOME");
	if (data_home && *data_home) {
		snprintf(dirs[n++], 512, "%s/applications", data_home);
	} else if (home) {
		snprintf(dirs[n++], 512, "%s/.local/share/applications", home);
	}
	if (home && n < max) {
		snprintf(dirs[n++], 512, "%s/.local/share/flatpak/exports/share/applications", home);
	}

	const char *data_dirs = getenv("XDG_DATA_DIRS");
	if (!data_dirs || !*data_dirs) data_dirs = "/usr/local/share:/usr/share";
	char buf[2048];
	snprintf(buf, sizeof(buf), "%s", data_dirs);
	char *save = NULL;
	for (char *tok = strtok_r(buf, ":", &save); tok && n < max; tok = strtok_r(NULL, ":", &save)) {
		if (!*tok) continue;
		char cand[512];
		snprintf(cand, sizeof(cand), "%s/applications", tok);
		bool dup = false;
		for (int i = 0; i < n; i++) {
			if (strcmp(dirs[i], cand) == 0) { dup = true; break; }
		}
		if (!dup) snprintf(dirs[n++], 512, "%s", cand);
	}
	static const char *extra[] = {
		"/usr/share/applications",
		"/var/lib/flatpak/exports/share/applications",
		"/var/lib/snapd/desktop/applications",
		NULL
	};
	for (int e = 0; extra[e] && n < max; e++) {
		bool dup = false;
		for (int i = 0; i < n; i++) {
			if (strcmp(dirs[i], extra[e]) == 0) { dup = true; break; }
		}
		if (!dup) snprintf(dirs[n++], 512, "%s", extra[e]);
	}
	return n;
}

static const char *last_segment(const char *id) {
	const char *dot = strrchr(id, '.');
	return (dot && dot[1]) ? dot + 1 : id;
}

/* Resolve app_id (Wayland) or WM_CLASS (XWayland) to the Icon= of its .desktop file.
 * Qt apps commonly report app_id as "org.kde.foo", "org.foo.Foo" or the binary name,
 * which often differs from the icon name, so the .desktop file is the source of truth. */
static bool resolve_desktop_icon_name(const char *app_id, char *out, size_t out_len) {
	char dirs[12][512];
	int ndirs = collect_app_dirs(dirs, 12);
	struct desktop_info info;
	char path[1024];

	/* Pass 1: direct desktop-file-id lookups */
	char lower[128];
	snprintf(lower, sizeof(lower), "%s", app_id);
	for (char *p = lower; *p; p++) *p = (char)g_ascii_tolower(*p);

	const char *ids[3] = { app_id, lower, last_segment(lower) };
	for (int i = 0; i < 3; i++) {
		for (int d = 0; d < ndirs; d++) {
			snprintf(path, sizeof(path), "%s/%s.desktop", dirs[d], ids[i]);
			if (access(path, R_OK) == 0 && parse_desktop_file(path, &info)) {
				snprintf(out, out_len, "%s", info.icon);
				return true;
			}
		}
	}

	/* Pass 2: scan all .desktop files, best match wins */
	int best_score = 0;
	char best_icon[256] = {0};
	const char *seg = last_segment(app_id);
	for (int d = 0; d < ndirs && best_score < 4; d++) {
		DIR *dir = opendir(dirs[d]);
		if (!dir) continue;
		struct dirent *ent;
		while ((ent = readdir(dir)) != NULL) {
			const char *fname = ent->d_name;
			size_t flen = strlen(fname);
			if (flen <= 8 || strcmp(fname + flen - 8, ".desktop") != 0) continue;

			char stem[256];
			size_t slen = flen - 8;
			if (slen >= sizeof(stem)) continue;
			memcpy(stem, fname, slen);
			stem[slen] = '\0';

			snprintf(path, sizeof(path), "%s/%s", dirs[d], fname);
			if (!parse_desktop_file(path, &info)) continue;

			int score = 0;
			if (info.wmclass[0] && strcasecmp(info.wmclass, app_id) == 0) score = 4;
			else if (strcasecmp(stem, app_id) == 0) score = 4;
			else if (info.exec_base[0] && strcasecmp(info.exec_base, app_id) == 0) score = 3;
			else if (strcasecmp(last_segment(stem), seg) == 0) score = 2;
			else if (info.exec_base[0] && strcasecmp(info.exec_base, seg) == 0) score = 1;

			if (score > best_score) {
				best_score = score;
				snprintf(best_icon, sizeof(best_icon), "%s", info.icon);
				if (score >= 4) break;
			}
		}
		closedir(dir);
	}
	if (best_score > 0 && best_icon[0]) {
		snprintf(out, out_len, "%s", best_icon);
		return true;
	}
	return false;
}

static cairo_surface_t *load_icon_path(const char *icon_path, int size) {
	GError *error = NULL;
	GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(icon_path, size, size, TRUE, &error);
	if (!pb) {
		if (error) g_error_free(error);
		return NULL;
	}
	cairo_surface_t *surf = pixbuf_to_cairo_surface(pb);
	g_object_unref(pb);
	return surf;
}

cairo_surface_t *diamonds_load_app_icon(const char *app_id, const char *alt_name, int size) {
	if (size <= 0) return NULL;
	const char *key = (app_id && *app_id) ? app_id : alt_name;
	if (!key || !*key) return NULL;

	for (int i = 0; i < g_app_cache_count; i++) {
		if (g_app_cache[i].size == size && strcmp(g_app_cache[i].app_id, key) == 0) {
			return g_app_cache[i].surface; /* may be NULL (cached miss) */
		}
	}

	cairo_surface_t *surf = NULL;
	bool owned = false;

	/* 0. Built-in Diamonds apps (not installed when run from ./build) */
	static const struct { const char *id; const char *icons[4]; } diamonds_aliases[] = {
		{ "diamonds-terminal", { "utilities-terminal", "org.gnome.Terminal", "terminal", NULL } },
		{ "diamonds-files",    { "system-file-manager", "org.gnome.Nautilus", "folder", NULL } },
		{ "diamonds-notes",    { "accessories-text-editor", "org.gnome.TextEditor", "text-editor", NULL } },
		{ "diamonds-calc",     { "accessories-calculator", "org.gnome.Calculator", "galculator", NULL } },
		{ "diamonds-photos",   { "image-viewer", "org.gnome.eog", "eog", NULL } },
		{ "diamonds-music",    { "multimedia-audio-player", "audio-x-generic", "rhythmbox", NULL } },
		{ "diamonds-monitor",  { "utilities-system-monitor", "org.gnome.SystemMonitor", "gnome-system-monitor", NULL } },
		{ "diamonds-mail",     { "internet-mail", "mail-client", "evolution", NULL } },
		{ "diamonds-clock",    { "preferences-system-time", "org.gnome.clocks", "clock", NULL } },
		{ "diamonds-store",    { "system-software-install", "org.gnome.Software", "software-store", NULL } },
		{ "diamonds-settings", { "preferences-system", "gnome-control-center", "systemsettings", NULL } },
	};
	for (size_t a = 0; a < sizeof(diamonds_aliases) / sizeof(diamonds_aliases[0]) && !surf; a++) {
		if (strcasecmp(key, diamonds_aliases[a].id) != 0) continue;
		for (int n = 0; diamonds_aliases[a].icons[n] && !surf; n++) {
			char *p = find_icon_file(diamonds_aliases[a].icons[n]);
			if (p) {
				surf = load_icon_path(p, size);
				free(p);
				owned = (surf != NULL);
			}
		}
	}

	/* 1. .desktop file lookup for app_id, then alt_name (e.g. X11 instance) */
	const char *ids[2] = { app_id, alt_name };
	for (int i = 0; i < 2 && !surf; i++) {
		if (!ids[i] || !*ids[i]) continue;
		char icon_name[256];
		if (resolve_desktop_icon_name(ids[i], icon_name, sizeof(icon_name))) {
			char *p = find_icon_file(icon_name);
			if (p) {
				surf = load_icon_path(p, size);
				free(p);
				owned = (surf != NULL);
			}
		}
	}

	/* 2. Direct icon-name lookup (app_id, lowercase, last segment) */
	for (int i = 0; i < 2 && !surf; i++) {
		if (!ids[i] || !*ids[i]) continue;
		char lower[128];
		snprintf(lower, sizeof(lower), "%s", ids[i]);
		for (char *q = lower; *q; q++) *q = (char)g_ascii_tolower(*q);
		const char *names[3] = { ids[i], lower, last_segment(lower) };
		for (int n = 0; n < 3 && !surf; n++) {
			char *p = find_icon_file(names[n]);
			if (p) {
				surf = load_icon_path(p, size);
				free(p);
				owned = (surf != NULL);
			}
		}
	}

	/* 3. Generic keyword fallback (owned by the main cache) */
	if (!surf) {
		surf = diamonds_load_system_icon(key, size);
		owned = false;
	}

	if (g_app_cache_count < MAX_APP_CACHE) {
		struct app_icon_cache_entry *e = &g_app_cache[g_app_cache_count++];
		snprintf(e->app_id, sizeof(e->app_id), "%s", key);
		e->size = size;
		e->surface = surf;
		e->owned = owned;
	} else if (owned && surf) {
		/* Cache full: hand ownership to the main cache if possible, else drop */
		if (g_cache_count < MAX_CACHE) {
			snprintf(g_cache[g_cache_count].key, sizeof(g_cache[g_cache_count].key), "app:%s", key);
			g_cache[g_cache_count].size = size;
			g_cache[g_cache_count].surface = surf;
			g_cache_count++;
		} else {
			cairo_surface_destroy(surf);
			surf = NULL;
		}
	}
	return surf;
}
