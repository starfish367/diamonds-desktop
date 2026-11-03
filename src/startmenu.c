#define _GNU_SOURCE
#include "startmenu.h"
#include "diamonds.h"
#include "icon_loader.h"
#include "vector_icons.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <stdio.h>
#include <dirent.h>
#include <ctype.h>
#include <math.h>
#include <pwd.h>
#include <sys/types.h>
#include <pixman.h>
#include <pango/pangocairo.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FONT_BASE "Segoe UI, Cantarell, DejaVu Sans, Ubuntu, sans-serif"

static void clean_exec_cmd(char *exec) {
	if (!exec) return;
	char *p = exec;
	while (*p) {
		if (*p == '%' && *(p + 1)) {
			*p = ' ';
			*(p + 1) = ' ';
			p += 2;
		} else {
			p++;
		}
	}
	int len = (int)strlen(exec);
	while (len > 0 && isspace((unsigned char)exec[len - 1])) {
		exec[--len] = '\0';
	}
	p = exec;
	while (*p && isspace((unsigned char)*p)) p++;
	if (p != exec) {
		memmove(exec, p, strlen(p) + 1);
	}
}

static void launch_desktop_cmd(const char *cmd) {
	if (!cmd || !*cmd) return;
	pid_t pid = fork();
	if (pid < 0) return;
	if (pid == 0) {
		setsid();
		signal(SIGPIPE, SIG_DFL);
		char *args[] = { "/bin/sh", "-c", (char *)cmd, NULL };
		execvp(args[0], args);
		_exit(127);
	}
}

static int entry_cmp(const void *a, const void *b) {
	const struct diamonds_desktop_entry *ea = a;
	const struct diamonds_desktop_entry *eb = b;
	return strcasecmp(ea->name, eb->name);
}

static void scan_desktop_directory(struct diamonds_startmenu *menu, const char *dir_path) {
	if (!dir_path || !*dir_path) return;
	DIR *dir = opendir(dir_path);
	if (!dir) return;

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (menu->app_count >= MAX_STARTMENU_APPS) break;

		size_t nlen = strlen(ent->d_name);
		if (nlen < 9 || strcmp(ent->d_name + nlen - 8, ".desktop") != 0) {
			continue;
		}

		char filepath[1024];
		snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, ent->d_name);
		FILE *f = fopen(filepath, "r");
		if (!f) continue;

		char line[1024];
		bool in_entry = false;
		bool no_display = false;
		bool is_app = true;
		char name[96] = {0};
		char exec[256] = {0};
		char icon[128] = {0};

		while (fgets(line, sizeof(line), f)) {
			char *p = line;
			while (*p == ' ' || *p == '\t') p++;

			if (*p == '[') {
				if (strncmp(p, "[Desktop Entry]", 15) == 0) {
					in_entry = true;
				} else {
					in_entry = false;
				}
				continue;
			}
			if (!in_entry) continue;

			if (strncmp(p, "NoDisplay=", 10) == 0) {
				char *v = p + 10;
				while (*v == ' ' || *v == '\t') v++;
				if (strncasecmp(v, "true", 4) == 0 || *v == '1') {
					no_display = true;
				}
				continue;
			}

			if (strncmp(p, "Hidden=", 7) == 0) {
				char *v = p + 7;
				while (*v == ' ' || *v == '\t') v++;
				if (strncasecmp(v, "true", 4) == 0 || *v == '1') {
					no_display = true;
				}
				continue;
			}

			if (strncmp(p, "Type=", 5) == 0) {
				char *v = p + 5;
				while (*v == ' ' || *v == '\t') v++;
				if (strncasecmp(v, "Application", 11) != 0) {
					is_app = false;
				}
				continue;
			}

			/* Name= (only base language, avoid Name[locale]=) */
			if (strncmp(p, "Name=", 5) == 0 && name[0] == '\0') {
				char *val = p + 5;
				char *nl = strchr(val, '\n');
				if (nl) *nl = '\0';
				char *cr = strchr(val, '\r');
				if (cr) *cr = '\0';
				while (*val == ' ' || *val == '\t') val++;
				snprintf(name, sizeof(name), "%s", val);
				continue;
			}

			/* Exec= */
			if (strncmp(p, "Exec=", 5) == 0 && exec[0] == '\0') {
				char *val = p + 5;
				char *nl = strchr(val, '\n');
				if (nl) *nl = '\0';
				char *cr = strchr(val, '\r');
				if (cr) *cr = '\0';
				clean_exec_cmd(val);
				snprintf(exec, sizeof(exec), "%s", val);
				continue;
			}

			/* Icon= */
			if (strncmp(p, "Icon=", 5) == 0 && icon[0] == '\0') {
				char *val = p + 5;
				char *nl = strchr(val, '\n');
				if (nl) *nl = '\0';
				char *cr = strchr(val, '\r');
				if (cr) *cr = '\0';
				while (*val == ' ' || *val == '\t') val++;
				snprintf(icon, sizeof(icon), "%s", val);
				continue;
			}
		}
		fclose(f);

		if (!no_display && is_app && name[0] != '\0' && exec[0] != '\0') {
			bool dup = false;
			for (int j = 0; j < menu->app_count; j++) {
				if (strcasecmp(menu->apps[j].name, name) == 0 ||
				    strcasecmp(menu->apps[j].exec, exec) == 0) {
					dup = true;
					break;
				}
			}
			if (!dup) {
				struct diamonds_desktop_entry *entry = &menu->apps[menu->app_count++];
				snprintf(entry->name, sizeof(entry->name), "%s", name);
				snprintf(entry->exec, sizeof(entry->exec), "%s", exec);
				snprintf(entry->icon, sizeof(entry->icon), "%s", icon);
			}
		}
	}
	closedir(dir);
}

static void diamonds_startmenu_filter(struct diamonds_startmenu *menu) {
	menu->filtered_count = 0;
	if (menu->search_query[0] == '\0') {
		for (int i = 0; i < menu->app_count && i < MAX_STARTMENU_APPS; i++) {
			menu->filtered_indices[menu->filtered_count++] = i;
		}
	} else {
		for (int i = 0; i < menu->app_count && i < MAX_STARTMENU_APPS; i++) {
			if (strcasestr(menu->apps[i].name, menu->search_query) ||
			    strcasestr(menu->apps[i].exec, menu->search_query)) {
				menu->filtered_indices[menu->filtered_count++] = i;
			}
		}
	}

	double item_height = 44.0;
	double list_viewport_height = 368.0;
	double total_h = (double)menu->filtered_count * item_height;
	menu->max_scroll = total_h > list_viewport_height ? (total_h - list_viewport_height) : 0.0;
	if (menu->scroll_offset > menu->max_scroll) {
		menu->scroll_offset = menu->max_scroll;
	}
	if (menu->scroll_offset < 0.0) {
		menu->scroll_offset = 0.0;
	}
}

static void init_pinned_apps(struct diamonds_startmenu *menu) {
	menu->pinned_count = 0;
	static const char *fav_keywords[] = {
		"terminal", "terminator", "nautilus", "files", "browser", "thorium",
		"chrome", "firefox", "vlc", "setting", "editor", "code", "calc", NULL
	};

	/* First, match popular desktop application keywords */
	for (int k = 0; fav_keywords[k] && menu->pinned_count < 9; k++) {
		for (int i = 0; i < menu->app_count; i++) {
			if (strcasestr(menu->apps[i].name, fav_keywords[k]) ||
			    strcasestr(menu->apps[i].exec, fav_keywords[k])) {
				bool already = false;
				for (int p = 0; p < menu->pinned_count; p++) {
					if (menu->pinned_indices[p] == i) {
						already = true;
						break;
					}
				}
				if (!already) {
					menu->pinned_indices[menu->pinned_count++] = i;
					if (menu->pinned_count >= 9) break;
				}
			}
		}
	}

	/* Fill remaining slots with first scanned apps */
	for (int i = 0; i < menu->app_count && menu->pinned_count < 9; i++) {
		bool already = false;
		for (int p = 0; p < menu->pinned_count; p++) {
			if (menu->pinned_indices[p] == i) {
				already = true;
				break;
			}
		}
		if (!already) {
			menu->pinned_indices[menu->pinned_count++] = i;
		}
	}
}

static void load_desktop_entries(struct diamonds_startmenu *menu) {
	menu->app_count = 0;

	/* 1. Scan system directory */
	scan_desktop_directory(menu, "/usr/share/applications");

	/* 2. Scan user local directory */
	const char *home = getenv("HOME");
	if (home && *home) {
		char user_apps_path[1024];
		snprintf(user_apps_path, sizeof(user_apps_path), "%s/.local/share/applications", home);
		scan_desktop_directory(menu, user_apps_path);
	}

	/* 3. Fallback defaults if scanned list is empty */
	if (menu->app_count == 0) {
		struct diamonds_desktop_entry defaults[] = {
			{ .name = "Terminal",        .exec = "terminator", .icon = "utilities-terminal" },
			{ .name = "Files",           .exec = "nautilus",   .icon = "system-file-manager" },
			{ .name = "Thorium Browser", .exec = "thorium",    .icon = "thorium-browser" },
			{ .name = "VLC Media",       .exec = "vlc",        .icon = "vlc" },
			{ .name = "Settings",        .exec = "systemsettings5 || gnome-control-center", .icon = "preferences-system" },
			{ .name = "Calculator",      .exec = "gnome-calculator || galculator", .icon = "accessories-calculator" },
		};
		for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
			menu->apps[menu->app_count++] = defaults[i];
		}
	}

	/* 4. Sort all applications alphabetically A-Z */
	qsort(menu->apps, menu->app_count, sizeof(menu->apps[0]), entry_cmp);

	/* 5. Initialize pinned apps */
	init_pinned_apps(menu);

	/* 6. Populate filtered indices */
	menu->search_query[0] = '\0';
	menu->search_query_len = 0;
	menu->scroll_offset = 0.0;
	diamonds_startmenu_filter(menu);
}

struct diamonds_startmenu *diamonds_startmenu_create(struct diamonds_server *server) {
	struct diamonds_startmenu *menu = calloc(1, sizeof(*menu));
	if (!menu) return NULL;

	menu->server = server;
	menu->width = 640;
	menu->height = 540;
	menu->visible = false;

	/* Get user name */
	struct passwd *pw = getpwuid(getuid());
	const char *u = pw ? pw->pw_name : getenv("USER");
	snprintf(menu->username, sizeof(menu->username), "%s", (u && *u) ? u : "Diamonds User");

	load_desktop_entries(menu);

	menu->buffer = diamonds_cairo_buffer_create(menu->width, menu->height);
	if (!menu->buffer) {
		free(menu);
		return NULL;
	}

	menu->scene_buffer = wlr_scene_buffer_create(server->tree_overlay, &menu->buffer->base);
	if (!menu->scene_buffer) {
		wlr_buffer_drop(&menu->buffer->base);
		free(menu);
		return NULL;
	}

	wlr_scene_node_set_enabled(&menu->scene_buffer->node, false);
	diamonds_startmenu_render(menu);
	return menu;
}

void diamonds_startmenu_destroy(struct diamonds_startmenu *menu) {
	if (!menu) return;
	if (menu->scene_buffer) {
		wlr_scene_node_destroy(&menu->scene_buffer->node);
	}
	if (menu->buffer) {
		wlr_buffer_drop(&menu->buffer->base);
	}
	free(menu);
}

void diamonds_startmenu_reposition(struct diamonds_startmenu *menu, int32_t dock_x, int32_t dock_y) {
	if (!menu || !menu->scene_buffer) return;

	menu->x = dock_x + 8;
	menu->y = dock_y - menu->height - 12;
	if (menu->x < 12) menu->x = 12;
	if (menu->y < 36) menu->y = 36;

	wlr_scene_node_set_position(&menu->scene_buffer->node, menu->x, menu->y);
}

void diamonds_startmenu_set_visible(struct diamonds_startmenu *menu, bool visible) {
	if (!menu || !menu->scene_buffer) return;
	menu->visible = visible;
	wlr_scene_node_set_enabled(&menu->scene_buffer->node, visible);
	if (visible) {
		wlr_scene_node_raise_to_top(&menu->scene_buffer->node);
		diamonds_startmenu_render(menu);
	}
}

void diamonds_startmenu_toggle(struct diamonds_startmenu *menu) {
	if (!menu) return;
	diamonds_startmenu_set_visible(menu, !menu->visible);
}

static void draw_app_vector_fallback(cairo_t *cr, const char *name, double cx, double cy, double size) {
	if (strcasestr(name, "term") || strcasestr(name, "bash") || strcasestr(name, "console")) {
		diamonds_draw_vector_terminal(cr, cx, cy, size);
	} else if (strcasestr(name, "file") || strcasestr(name, "nautilus") || strcasestr(name, "thunar")) {
		diamonds_draw_vector_files(cr, cx, cy, size);
	} else if (strcasestr(name, "browser") || strcasestr(name, "thorium") || strcasestr(name, "chrome") || strcasestr(name, "firefox")) {
		diamonds_draw_vector_browser(cr, cx, cy, size);
	} else if (strcasestr(name, "vlc") || strcasestr(name, "media") || strcasestr(name, "music") || strcasestr(name, "video")) {
		diamonds_draw_vector_media(cr, cx, cy, size);
	} else if (strcasestr(name, "setting") || strcasestr(name, "config") || strcasestr(name, "control")) {
		diamonds_draw_vector_settings(cr, cx, cy, size);
	} else {
		diamonds_draw_vector_store(cr, cx, cy, size);
	}
}

static void draw_icon_or_vector(cairo_t *cr, const char *icon_key, const char *app_name, double draw_x, double draw_y, int size) {
	cairo_surface_t *surf = diamonds_load_desktop_icon(icon_key, app_name, size);
	if (surf) {
		int sw = cairo_image_surface_get_width(surf);
		int sh = cairo_image_surface_get_height(surf);
		cairo_save(cr);
		cairo_translate(cr, draw_x, draw_y);
		if (sw > 0 && sh > 0 && (sw != size || sh != size)) {
			cairo_scale(cr, (double)size / (double)sw, (double)size / (double)sh);
		}
		cairo_set_source_surface(cr, surf, 0, 0);
		cairo_paint(cr);
		cairo_restore(cr);
	} else {
		draw_app_vector_fallback(cr, app_name, draw_x + (double)size / 2.0, draw_y + (double)size / 2.0, (double)size * 0.85);
	}
}

void diamonds_startmenu_render(struct diamonds_startmenu *menu) {
	if (!menu || !menu->buffer || !menu->buffer->cairo_surface) return;

	cairo_t *cr = cairo_create(menu->buffer->cairo_surface);
	if (!cr) return;

	double w = (double)menu->width;
	double h = (double)menu->height;

	/* 1. Clear background surface */
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	/* 2. Main Mica Card: #131422 (alpha 0.96) with 14px rounded corners */
	diamonds_draw_rounded_rect(cr, 0.5, 0.5, w - 1.0, h - 1.0, 14.0);
	cairo_set_source_rgba(cr, 0.075, 0.078, 0.133, 0.96);
	cairo_fill_preserve(cr);

	/* Mica Glass Border #32354a */
	cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.25);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* 3. Top Search Bar: x:16, y:14, w:608, h:38 */
	diamonds_draw_rounded_rect(cr, 16.0, 14.0, w - 32.0, 38.0, 10.0);
	cairo_set_source_rgba(cr, 0.11, 0.11, 0.17, 0.92);
	cairo_fill_preserve(cr);

	if (menu->search_query_len > 0) {
		cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.65); /* Focused glow */
	} else {
		cairo_set_source_rgba(cr, 0.20, 0.21, 0.31, 0.60);
	}
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* Search Magnifier Vector */
	cairo_set_source_rgba(cr, 0.55, 0.58, 0.72, 0.95);
	cairo_set_line_width(cr, 1.5);
	cairo_arc(cr, 32.0, 33.0, 5.0, 0.0, 2.0 * M_PI);
	cairo_stroke(cr);
	cairo_move_to(cr, 35.8, 36.8);
	cairo_line_to(cr, 40.5, 41.5);
	cairo_stroke(cr);

	/* Search Text or Placeholder */
	PangoLayout *layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " 9.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);

		if (menu->search_query_len > 0) {
			pango_layout_set_text(layout, menu->search_query, -1);
			cairo_move_to(cr, 48.0, 24.0);
			cairo_set_source_rgba(cr, 0.95, 0.96, 0.98, 1.0);
			pango_cairo_show_layout(cr, layout);

			/* Cursor */
			int tw, th;
			pango_layout_get_pixel_size(layout, &tw, &th);
			cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.9);
			cairo_rectangle(cr, 50.0 + tw, 23.0, 1.5, 18.0);
			cairo_fill(cr);

			/* Clear Button (✕) on the right */
			cairo_set_source_rgba(cr, 0.60, 0.64, 0.76, 0.85);
			cairo_set_line_width(cr, 1.4);
			cairo_move_to(cr, w - 38.0, 28.0);
			cairo_line_to(cr, w - 28.0, 38.0);
			cairo_move_to(cr, w - 28.0, 28.0);
			cairo_line_to(cr, w - 38.0, 38.0);
			cairo_stroke(cr);
		} else {
			pango_layout_set_text(layout, "Type here to search apps, commands...", -1);
			cairo_move_to(cr, 48.0, 24.0);
			cairo_set_source_rgba(cr, 0.45, 0.48, 0.60, 0.85);
			pango_cairo_show_layout(cr, layout);
		}
		g_object_unref(layout);
	}

	/* 4. LEFT COLUMN: All Applications List (x:16, y:60, w:336, h:412) */
	diamonds_draw_rounded_rect(cr, 16.0, 60.0, 336.0, 412.0, 10.0);
	cairo_set_source_rgba(cr, 0.095, 0.10, 0.15, 0.70);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.18, 0.19, 0.28, 0.50);
	cairo_set_line_width(cr, 0.8);
	cairo_stroke(cr);

	/* Left Header */
	layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 9");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);

		char ltitle[64];
		if (menu->search_query_len > 0) {
			snprintf(ltitle, sizeof(ltitle), "SEARCH RESULTS (%d)", menu->filtered_count);
		} else {
			snprintf(ltitle, sizeof(ltitle), "ALL APPLICATIONS (A-Z • %d)", menu->filtered_count);
		}
		pango_layout_set_text(layout, ltitle, -1);
		cairo_move_to(cr, 28.0, 70.0);
		cairo_set_source_rgba(cr, 0.82, 0.85, 0.94, 0.95);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* Left List Viewport: y:92 to 464 (height: 372) */
	cairo_save(cr);
	diamonds_draw_rounded_rect(cr, 18.0, 92.0, 332.0, 372.0, 8.0);
	cairo_clip(cr);

	double list_y_start = 94.0;
	double row_h = 44.0;
	int start_idx = (int)(menu->scroll_offset / row_h);
	if (start_idx < 0) start_idx = 0;
	int end_idx = start_idx + 10;
	if (end_idx > menu->filtered_count) end_idx = menu->filtered_count;

	for (int i = start_idx; i < end_idx; i++) {
		int app_idx = menu->filtered_indices[i];
		double row_y = list_y_start + (double)i * row_h - menu->scroll_offset;

		/* Row card */
		diamonds_draw_rounded_rect(cr, 22.0, row_y, 322.0, 38.0, 8.0);
		cairo_set_source_rgba(cr, 0.13, 0.14, 0.20, 0.50);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0.20, 0.22, 0.32, 0.30);
		cairo_set_line_width(cr, 0.6);
		cairo_stroke(cr);

		/* App icon */
		draw_icon_or_vector(cr, menu->apps[app_idx].icon, menu->apps[app_idx].name, 28.0, row_y + 5.0, 28);

		/* App Name */
		layout = pango_cairo_create_layout(cr);
		if (layout) {
			PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " 9.5");
			pango_layout_set_font_description(layout, desc);
			pango_font_description_free(desc);
			pango_layout_set_text(layout, menu->apps[app_idx].name, -1);
			pango_layout_set_width(layout, 250 * PANGO_SCALE);
			pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

			cairo_move_to(cr, 64.0, row_y + 10.0);
			cairo_set_source_rgba(cr, 0.92, 0.94, 0.98, 1.0);
			pango_cairo_show_layout(cr, layout);
			g_object_unref(layout);
		}
	}
	cairo_restore(cr);

	/* Scrollbar if needed */
	if (menu->max_scroll > 0.0) {
		double track_h = 360.0;
		double thumb_ratio = 372.0 / ((double)menu->filtered_count * row_h);
		if (thumb_ratio > 1.0) thumb_ratio = 1.0;
		double thumb_h = track_h * thumb_ratio;
		if (thumb_h < 24.0) thumb_h = 24.0;
		double scroll_ratio = menu->scroll_offset / menu->max_scroll;
		double thumb_y = 96.0 + (track_h - thumb_h) * scroll_ratio;

		/* Scrollbar thumb */
		diamonds_draw_rounded_rect(cr, 346.0, thumb_y, 3.5, thumb_h, 2.0);
		cairo_set_source_rgba(cr, 0.35, 0.38, 0.52, 0.8);
		cairo_fill(cr);
	}

	/* 5. RIGHT COLUMN: Pinned Grid (x:362, y:60, w:262, h:412) */
	diamonds_draw_rounded_rect(cr, 362.0, 60.0, 262.0, 412.0, 10.0);
	cairo_set_source_rgba(cr, 0.095, 0.10, 0.15, 0.70);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.18, 0.19, 0.28, 0.50);
	cairo_set_line_width(cr, 0.8);
	cairo_stroke(cr);

	/* Right Header */
	layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 9");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, "PINNED APPS", -1);
		cairo_move_to(cr, 374.0, 70.0);
		cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* 3x3 Pinned Grid: col_w: 78, row_h: 94, gap_x: 6, gap_y: 10 */
	double grid_x = 372.0;
	double grid_y = 96.0;
	int display_pinned = menu->pinned_count < 9 ? menu->pinned_count : 9;

	for (int i = 0; i < display_pinned; i++) {
		int app_idx = menu->pinned_indices[i];
		int col = i % 3;
		int row = i / 3;
		double item_x = grid_x + (double)col * 80.0;
		double item_y = grid_y + (double)row * 102.0;

		/* Card background */
		diamonds_draw_rounded_rect(cr, item_x, item_y, 76.0, 92.0, 9.0);
		cairo_set_source_rgba(cr, 0.13, 0.14, 0.20, 0.75);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0.22, 0.24, 0.35, 0.40);
		cairo_set_line_width(cr, 0.7);
		cairo_stroke(cr);

		/* Icon 36x36 centered */
		draw_icon_or_vector(cr, menu->apps[app_idx].icon, menu->apps[app_idx].name, item_x + 20.0, item_y + 12.0, 36);

		/* Title */
		layout = pango_cairo_create_layout(cr);
		if (layout) {
			PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " 8");
			pango_layout_set_font_description(layout, desc);
			pango_font_description_free(desc);
			pango_layout_set_text(layout, menu->apps[app_idx].name, -1);
			pango_layout_set_width(layout, 72 * PANGO_SCALE);
			pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
			pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

			cairo_move_to(cr, item_x + 2.0, item_y + 56.0);
			cairo_set_source_rgba(cr, 0.90, 0.92, 0.96, 1.0);
			pango_cairo_show_layout(cr, layout);
			g_object_unref(layout);
		}
	}

	/* 6. BOTTOM FOOTER: User info and 3 real Power buttons (y: 480 to 540) */
	cairo_move_to(cr, 0, 480.0);
	cairo_line_to(cr, w, 480.0);
	cairo_set_source_rgba(cr, 0.20, 0.21, 0.30, 0.7);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* User Avatar Silhouette Vector */
	diamonds_draw_vector_avatar(cr, 34.0, 510.0, 15.0);

	/* User Name */
	layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 10");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, menu->username, -1);
		cairo_move_to(cr, 58.0, 497.0);
		cairo_set_source_rgba(cr, 0.94, 0.96, 0.99, 1.0);
		pango_cairo_show_layout(cr, layout);

		desc = pango_font_description_from_string(FONT_BASE " 7.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, "Standard User • Diamonds Shell", -1);
		cairo_move_to(cr, 58.0, 515.0);
		cairo_set_source_rgba(cr, 0.50, 0.54, 0.66, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* 3 Real Power Buttons at Bottom Right:
	 * Button 1: Lock (x: 456, y: 494, w: 50, h: 32)
	 * Button 2: Reboot (x: 514, y: 494, w: 52, h: 32)
	 * Button 3: Shutdown (x: 574, y: 494, w: 50, h: 32)
	 */
	/* 1. Lock */
	diamonds_draw_rounded_rect(cr, 456.0, 494.0, 50.0, 32.0, 8.0);
	cairo_set_source_rgba(cr, 0.14, 0.16, 0.24, 0.85);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.24, 0.26, 0.38, 0.60);
	cairo_set_line_width(cr, 0.8);
	cairo_stroke(cr);

	/* Lock vector icon (cx = 481, cy = 510) */
	cairo_set_source_rgba(cr, 0.75, 0.80, 0.92, 1.0);
	cairo_set_line_width(cr, 1.4);
	cairo_arc(cr, 481.0, 506.0, 4.0, -M_PI, 0.0);
	cairo_stroke(cr);
	diamonds_draw_rounded_rect(cr, 476.0, 506.0, 10.0, 8.0, 2.0);
	cairo_fill(cr);

	/* 2. Reboot */
	diamonds_draw_rounded_rect(cr, 514.0, 494.0, 52.0, 32.0, 8.0);
	cairo_set_source_rgba(cr, 0.18, 0.16, 0.22, 0.85);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.32, 0.28, 0.38, 0.60);
	cairo_set_line_width(cr, 0.8);
	cairo_stroke(cr);

	/* Reboot circular arrow icon (cx = 540, cy = 510) */
	cairo_set_source_rgba(cr, 0.96, 0.76, 0.36, 1.0); /* Amber */
	cairo_set_line_width(cr, 1.5);
	cairo_arc(cr, 540.0, 510.0, 5.5, -0.4 * M_PI, 1.3 * M_PI);
	cairo_stroke(cr);
	cairo_move_to(cr, 537.0, 504.0);
	cairo_line_to(cr, 541.0, 504.0);
	cairo_line_to(cr, 541.0, 508.0);
	cairo_stroke(cr);

	/* 3. Shutdown */
	diamonds_draw_rounded_rect(cr, 574.0, 494.0, 50.0, 32.0, 8.0);
	cairo_set_source_rgba(cr, 0.25, 0.12, 0.15, 0.85);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.45, 0.20, 0.25, 0.60);
	cairo_set_line_width(cr, 0.8);
	cairo_stroke(cr);

	/* Power icon (cx = 599, cy = 510) */
	cairo_set_source_rgba(cr, 0.95, 0.35, 0.35, 1.0); /* Red */
	cairo_set_line_width(cr, 1.5);
	cairo_arc(cr, 599.0, 510.0, 6.0, -0.6 * M_PI, 1.6 * M_PI);
	cairo_stroke(cr);
	cairo_move_to(cr, 599.0, 504.0);
	cairo_line_to(cr, 599.0, 509.0);
	cairo_stroke(cr);

	cairo_destroy(cr);

	/* Damage buffer */
	pixman_region32_t damage;
	pixman_region32_init_rect(&damage, 0, 0, (uint32_t)menu->width, (uint32_t)menu->height);
	wlr_scene_buffer_set_buffer_with_damage(menu->scene_buffer, &menu->buffer->base, &damage);
	pixman_region32_fini(&damage);
}

bool diamonds_startmenu_handle_click(struct diamonds_startmenu *menu, double lx, double ly) {
	if (!menu || !menu->visible) return false;

	/* If clicked outside startmenu bounds, dismiss menu */
	if (lx < menu->x || lx > menu->x + menu->width ||
	    ly < menu->y || ly > menu->y + menu->height) {
		diamonds_startmenu_set_visible(menu, false);
		return true;
	}

	double rel_x = lx - menu->x;
	double rel_y = ly - menu->y;

	/* 1. Check Search Bar Clear Button (✕) */
	if (menu->search_query_len > 0 &&
	    rel_x >= (double)menu->width - 44.0 && rel_x <= (double)menu->width - 24.0 &&
	    rel_y >= 20.0 && rel_y <= 46.0) {
		menu->search_query[0] = '\0';
		menu->search_query_len = 0;
		diamonds_startmenu_filter(menu);
		diamonds_startmenu_render(menu);
		return true;
	}

	/* 2. Check 3 Power Buttons */
	/* Lock */
	if (rel_x >= 456.0 && rel_x <= 506.0 && rel_y >= 494.0 && rel_y <= 526.0) {
		diamonds_startmenu_set_visible(menu, false);
		launch_desktop_cmd("swaylock -f -c 000000 || loginctl lock-session || dm-tool lock &");
		return true;
	}

	/* Reboot */
	if (rel_x >= 514.0 && rel_x <= 566.0 && rel_y >= 494.0 && rel_y <= 526.0) {
		diamonds_startmenu_set_visible(menu, false);
		launch_desktop_cmd("systemctl reboot || reboot &");
		return true;
	}

	/* Shutdown */
	if (rel_x >= 574.0 && rel_x <= 624.0 && rel_y >= 494.0 && rel_y <= 526.0) {
		diamonds_startmenu_set_visible(menu, false);
		launch_desktop_cmd("systemctl poweroff || poweroff &");
		return true;
	}

	/* 3. Check Left List items (y: 92 to 464, x: 20 to 344) */
	if (rel_x >= 20.0 && rel_x <= 344.0 && rel_y >= 92.0 && rel_y <= 464.0) {
		double list_rel_y = (rel_y - 94.0) + menu->scroll_offset;
		int row_idx = (int)(list_rel_y / 44.0);
		if (row_idx >= 0 && row_idx < menu->filtered_count) {
			int app_idx = menu->filtered_indices[row_idx];
			diamonds_startmenu_set_visible(menu, false);
			launch_desktop_cmd(menu->apps[app_idx].exec);
			return true;
		}
	}

	/* 4. Check Right Pinned Grid (y: 96 to 400, x: 372 to 612) */
	if (rel_x >= 372.0 && rel_x <= 612.0 && rel_y >= 96.0 && rel_y <= 402.0) {
		int col = (int)((rel_x - 372.0) / 80.0);
		int row = (int)((rel_y - 96.0) / 102.0);
		if (col >= 0 && col < 3 && row >= 0 && row < 3) {
			int p_idx = row * 3 + col;
			if (p_idx < menu->pinned_count) {
				int app_idx = menu->pinned_indices[p_idx];
				diamonds_startmenu_set_visible(menu, false);
				launch_desktop_cmd(menu->apps[app_idx].exec);
				return true;
			}
		}
	}

	return true;
}

bool diamonds_startmenu_handle_key(struct diamonds_startmenu *menu, xkb_keysym_t sym, enum wl_keyboard_key_state state) {
	if (!menu || !menu->visible) return false;

	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		return true; /* Absorb release */
	}

	if (sym == XKB_KEY_Escape) {
		diamonds_startmenu_set_visible(menu, false);
		return true;
	}

	if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		if (menu->filtered_count > 0) {
			int app_idx = menu->filtered_indices[0];
			diamonds_startmenu_set_visible(menu, false);
			launch_desktop_cmd(menu->apps[app_idx].exec);
			return true;
		}
		return true;
	}

	if (sym == XKB_KEY_BackSpace) {
		if (menu->search_query_len > 0) {
			menu->search_query[--menu->search_query_len] = '\0';
			diamonds_startmenu_filter(menu);
			diamonds_startmenu_render(menu);
		}
		return true;
	}

	if (sym == XKB_KEY_Up) {
		menu->scroll_offset -= 44.0;
		if (menu->scroll_offset < 0.0) menu->scroll_offset = 0.0;
		diamonds_startmenu_render(menu);
		return true;
	}

	if (sym == XKB_KEY_Down) {
		menu->scroll_offset += 44.0;
		if (menu->scroll_offset > menu->max_scroll) menu->scroll_offset = menu->max_scroll;
		diamonds_startmenu_render(menu);
		return true;
	}

	/* Printable ASCII */
	if (sym >= 0x20 && sym <= 0x7E) {
		if (menu->search_query_len < (int)sizeof(menu->search_query) - 1) {
			menu->search_query[menu->search_query_len++] = (char)sym;
			menu->search_query[menu->search_query_len] = '\0';
			diamonds_startmenu_filter(menu);
			diamonds_startmenu_render(menu);
		}
		return true;
	}

	return true;
}

bool diamonds_startmenu_handle_scroll(struct diamonds_startmenu *menu, double lx, double ly, double delta) {
	if (!menu || !menu->visible) return false;

	if (lx < menu->x || lx > menu->x + menu->width ||
	    ly < menu->y || ly > menu->y + menu->height) {
		return false;
	}

	menu->scroll_offset += delta * 24.0;
	if (menu->scroll_offset < 0.0) menu->scroll_offset = 0.0;
	if (menu->scroll_offset > menu->max_scroll) menu->scroll_offset = menu->max_scroll;

	diamonds_startmenu_render(menu);
	return true;
}
