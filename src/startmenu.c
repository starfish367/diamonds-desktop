#define _GNU_SOURCE
#include "startmenu.h"
#include "diamonds.h"
#include "dock.h"
#include "sound.h"
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
			*p = '\0';
			break;
		}
		p++;
	}
	int len = strlen(exec);
	while (len > 0 && isspace((unsigned char)exec[len - 1])) {
		exec[--len] = '\0';
	}
}

static void launch_desktop_cmd(const char *cmd) {
	if (!cmd || !*cmd) return;
	diamonds_sound_play(DIAMONDS_SOUND_CLICK);
	launch_app(cmd);
}

static int entry_cmp(const void *a, const void *b) {
	const struct diamonds_desktop_entry *ea = a;
	const struct diamonds_desktop_entry *eb = b;
	return strcasecmp(ea->name, eb->name);
}

static void scan_desktop_directory(struct diamonds_startmenu *menu, const char *dir_path) {
	DIR *dir = opendir(dir_path);
	if (!dir) return;

	struct dirent *ent;
	char filepath[1024];

	while ((ent = readdir(dir)) != NULL) {
		if (menu->app_count >= MAX_STARTMENU_APPS) break;
		if (ent->d_name[0] == '.') continue;

		size_t len = strlen(ent->d_name);
		if (len < 9 || strcmp(ent->d_name + len - 8, ".desktop") != 0) {
			continue;
		}

		snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, ent->d_name);
		FILE *fp = fopen(filepath, "r");
		if (!fp) continue;

		char line[512];
		char name[96] = {0};
		char exec[256] = {0};
		char icon[128] = {0};
		bool no_display = false;
		bool in_desktop_entry = false;

		while (fgets(line, sizeof(line), fp)) {
			char *nl = strchr(line, '\n');
			if (nl) *nl = '\0';

			if (line[0] == '[') {
				if (strcmp(line, "[Desktop Entry]") == 0) {
					in_desktop_entry = true;
				} else {
					in_desktop_entry = false;
				}
				continue;
			}
			if (!in_desktop_entry) continue;

			if (strncmp(line, "NoDisplay=true", 14) == 0 ||
			    strncmp(line, "Hidden=true", 11) == 0) {
				no_display = true;
				break;
			}

			if (name[0] == '\0' && strncmp(line, "Name=", 5) == 0) {
				snprintf(name, sizeof(name), "%.90s", line + 5);
			} else if (exec[0] == '\0' && strncmp(line, "Exec=", 5) == 0) {
				snprintf(exec, sizeof(exec), "%.250s", line + 5);
				clean_exec_cmd(exec);
			} else if (icon[0] == '\0' && strncmp(line, "Icon=", 5) == 0) {
				snprintf(icon, sizeof(icon), "%.120s", line + 5);
			}
		}
		fclose(fp);

		if (!no_display && name[0] && exec[0]) {
			bool exists = false;
			for (int i = 0; i < menu->app_count; i++) {
				if (strcasecmp(menu->apps[i].name, name) == 0) {
					exists = true;
					break;
				}
			}
			if (!exists) {
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
	if (!menu) return;
	menu->filtered_count = 0;
	if (menu->search_len == 0 || menu->search_query[0] == '\0') {
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

	double item_height = 42.0;
	double list_viewport_height = 376.0;
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
		"terminal", "terminator", "files", "diamonds-files", "nautilus",
		"thorium", "browser", "chrome", "firefox", "vlc", "setting",
		"diamonds-settings", "editor", "calc", NULL
	};

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

	/* Built-in Diamonds ecosystem apps (always available, run from ./build or PATH).
	 * Added first so installed .desktop duplicates with the same Name are skipped. */
	static const struct diamonds_desktop_entry builtin_apps[] = {
		{ .name = "Diamonds Terminal", .exec = "./build/diamonds-terminal || diamonds-terminal", .icon = "utilities-terminal" },
		{ .name = "Diamonds Files",    .exec = "./build/diamonds-files || diamonds-files",       .icon = "system-file-manager" },
		{ .name = "Diamonds Notes",    .exec = "./build/diamonds-notes || diamonds-notes",       .icon = "accessories-text-editor" },
		{ .name = "Diamonds Calculator", .exec = "./build/diamonds-calc || diamonds-calc",       .icon = "accessories-calculator" },
		{ .name = "Diamonds Photos",   .exec = "./build/diamonds-photos || diamonds-photos",     .icon = "image-viewer" },
		{ .name = "Diamonds Music",    .exec = "./build/diamonds-music || diamonds-music",       .icon = "multimedia-audio-player" },
		{ .name = "Diamonds Monitor",  .exec = "./build/diamonds-monitor || diamonds-monitor",   .icon = "utilities-system-monitor" },
		{ .name = "Diamonds Mail",     .exec = "./build/diamonds-mail || diamonds-mail",         .icon = "internet-mail" },
		{ .name = "Diamonds Clock",    .exec = "./build/diamonds-clock || diamonds-clock",       .icon = "preferences-system-time" },
		{ .name = "Diamonds Store",    .exec = "./build/diamonds-store || diamonds-store",       .icon = "system-software-install" },
		{ .name = "Diamonds Settings", .exec = "./build/diamonds-settings || diamonds-settings", .icon = "preferences-system" },
	};
	for (size_t i = 0; i < sizeof(builtin_apps) / sizeof(builtin_apps[0]) &&
			menu->app_count < MAX_STARTMENU_APPS; i++) {
		menu->apps[menu->app_count++] = builtin_apps[i];
	}

	scan_desktop_directory(menu, "/usr/share/applications");

	const char *home = getenv("HOME");
	if (home && *home) {
		char user_apps_path[1024];
		snprintf(user_apps_path, sizeof(user_apps_path), "%s/.local/share/applications", home);
		scan_desktop_directory(menu, user_apps_path);
	}

	if (menu->app_count == 0) {
		struct diamonds_desktop_entry defaults[] = {
			{ .name = "Terminal",        .exec = "terminator", .icon = "utilities-terminal" },
			{ .name = "Files",           .exec = "./build/diamonds-files || diamonds-files",   .icon = "system-file-manager" },
			{ .name = "Thorium Browser", .exec = "thorium",    .icon = "thorium-browser" },
			{ .name = "VLC Media",       .exec = "vlc",        .icon = "vlc" },
			{ .name = "Settings",        .exec = "./build/diamonds-settings || diamonds-settings", .icon = "preferences-system" },
			{ .name = "Calculator",      .exec = "gnome-calculator || galculator", .icon = "accessories-calculator" },
		};
		for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
			menu->apps[menu->app_count++] = defaults[i];
		}
	}

	qsort(menu->apps, menu->app_count, sizeof(menu->apps[0]), entry_cmp);
	init_pinned_apps(menu);

	menu->search_query[0] = '\0';
	menu->search_len = 0;
	menu->scroll_offset = 0.0;
	menu->hover_app_idx = -1;
	menu->hover_pinned_idx = -1;
	menu->hover_power_btn = 0;
	diamonds_startmenu_filter(menu);
}

struct diamonds_startmenu *diamonds_startmenu_create(struct diamonds_server *server) {
	struct diamonds_startmenu *menu = calloc(1, sizeof(*menu));
	if (!menu) return NULL;

	menu->server = server;
	menu->width = 660;
	menu->height = 540;
	menu->visible = false;
	menu->hover_app_idx = -1;
	menu->hover_pinned_idx = -1;
	menu->hover_power_btn = 0;

	struct passwd *pw = getpwuid(getuid());
	const char *u = pw ? pw->pw_name : getenv("USER");
	snprintf(menu->username, sizeof(menu->username), "%s", (u && *u) ? u : "user");

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

	if (menu->server && menu->server->dock && menu->server->dock->is_full_width) {
		menu->x = (menu->server->dock->width - menu->width) / 2;
	} else {
		menu->x = dock_x + 8;
	}
	menu->y = dock_y - menu->height - 8;
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
		menu->hover_app_idx = -1;
		menu->hover_pinned_idx = -1;
		menu->hover_power_btn = 0;
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

	/* 2. Main Mica Card: #14141B with alpha = 0.96 */
	diamonds_draw_rounded_rect(cr, 0.5, 0.5, w - 1.0, h - 1.0, 12.0);
	cairo_set_source_rgba(cr, 0.078, 0.078, 0.106, 0.96);
	cairo_fill_preserve(cr);

	/* Mica Glass Border #2B2D3C */
	cairo_set_source_rgba(cr, 0.17, 0.18, 0.24, 0.90);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* 3. Top Search Bar: x:18, y:14, w: w - 36, h:38 */
	diamonds_draw_rounded_rect(cr, 18.0, 14.0, w - 36.0, 38.0, 8.0);
	cairo_set_source_rgba(cr, 0.11, 0.11, 0.16, 0.90);
	cairo_fill_preserve(cr);

	if (menu->search_len > 0) {
		cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.85); /* Active blue glow */
	} else {
		cairo_set_source_rgba(cr, 0.20, 0.21, 0.30, 0.60);
	}
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* Search Magnifier Vector */
	cairo_set_source_rgba(cr, 0.55, 0.58, 0.72, 0.95);
	cairo_set_line_width(cr, 1.5);
	cairo_arc(cr, 34.0, 33.0, 5.0, 0.0, 2.0 * M_PI);
	cairo_stroke(cr);
	cairo_move_to(cr, 37.8, 36.8);
	cairo_line_to(cr, 42.5, 41.5);
	cairo_stroke(cr);

	/* Search Text or Placeholder */
	PangoLayout *layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " 9.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);

		if (menu->search_len > 0) {
			pango_layout_set_text(layout, menu->search_query, -1);
			cairo_move_to(cr, 52.0, 24.0);
			cairo_set_source_rgba(cr, 0.96, 0.97, 0.99, 1.0);
			pango_cairo_show_layout(cr, layout);

			/* Blinking / Visible Cursor '|' at end of text */
			int tw, th;
			pango_layout_get_pixel_size(layout, &tw, &th);
			cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 1.0);
			cairo_rectangle(cr, 54.0 + tw, 23.0, 1.5, 18.0);
			cairo_fill(cr);

			/* Clear Button (✕) on the right */
			cairo_set_source_rgba(cr, 0.60, 0.64, 0.76, 0.90);
			cairo_set_line_width(cr, 1.4);
			cairo_move_to(cr, w - 40.0, 27.0);
			cairo_line_to(cr, w - 28.0, 39.0);
			cairo_move_to(cr, w - 28.0, 27.0);
			cairo_line_to(cr, w - 40.0, 39.0);
			cairo_stroke(cr);
		} else {
			pango_layout_set_text(layout, "Type here to search apps, commands...", -1);
			cairo_move_to(cr, 52.0, 24.0);
			cairo_set_source_rgba(cr, 0.45, 0.48, 0.58, 0.85);
			pango_cairo_show_layout(cr, layout);

			/* Initial cursor */
			cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.8);
			cairo_rectangle(cr, 52.0, 23.0, 1.5, 18.0);
			cairo_fill(cr);
		}
		g_object_unref(layout);
	}

	/* 4. LEFT COLUMN: All Applications List (x:18, y:60, w:320, h:412) */
	/* Left Header */
	layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 9");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);

		char ltitle[64];
		if (menu->search_len > 0) {
			snprintf(ltitle, sizeof(ltitle), "SEARCH RESULTS (%d)", menu->filtered_count);
		} else {
			snprintf(ltitle, sizeof(ltitle), "ALL APPLICATIONS (A-Z • %d)", menu->filtered_count);
		}
		pango_layout_set_text(layout, ltitle, -1);
		cairo_move_to(cr, 26.0, 70.0);
		cairo_set_source_rgba(cr, 0.78, 0.82, 0.92, 0.95);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* Left List Viewport with strict cairo_rectangle + cairo_clip */
	cairo_save(cr);
	cairo_rectangle(cr, 18.0, 92.0, 320.0, 376.0);
	cairo_clip(cr);

	double list_y_start = 94.0;
	double row_h = 42.0;
	int start_idx = (int)(menu->scroll_offset / row_h);
	if (start_idx < 0) start_idx = 0;
	int end_idx = start_idx + 10;
	if (end_idx > menu->filtered_count) end_idx = menu->filtered_count;

	for (int i = start_idx; i < end_idx; i++) {
		int app_idx = menu->filtered_indices[i];
		double row_y = list_y_start + (double)i * row_h - menu->scroll_offset;

		/* Transparent by default! Only draw rounded rect on hover! */
		if (menu->hover_app_idx == i) {
			diamonds_draw_rounded_rect(cr, 20.0, row_y, 314.0, 38.0, 6.0);
			cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
			cairo_fill(cr);
		}

		/* App icon 28x28 */
		draw_icon_or_vector(cr, menu->apps[app_idx].icon, menu->apps[app_idx].name, 26.0, row_y + 5.0, 28);

		/* App Name */
		layout = pango_cairo_create_layout(cr);
		if (layout) {
			PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " 9.5");
			pango_layout_set_font_description(layout, desc);
			pango_font_description_free(desc);
			pango_layout_set_text(layout, menu->apps[app_idx].name, -1);
			pango_layout_set_width(layout, 246 * PANGO_SCALE);
			pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

			cairo_move_to(cr, 62.0, row_y + 10.0);
			cairo_set_source_rgba(cr, 0.94, 0.96, 0.99, 1.0);
			pango_cairo_show_layout(cr, layout);
			g_object_unref(layout);
		}
	}
	cairo_restore(cr);

	/* Scrollbar if needed */
	if (menu->max_scroll > 0.0) {
		double track_h = 360.0;
		double thumb_ratio = 376.0 / ((double)menu->filtered_count * row_h);
		if (thumb_ratio > 1.0) thumb_ratio = 1.0;
		double thumb_h = track_h * thumb_ratio;
		if (thumb_h < 24.0) thumb_h = 24.0;
		double scroll_ratio = menu->scroll_offset / menu->max_scroll;
		double thumb_y = 96.0 + (track_h - thumb_h) * scroll_ratio;

		diamonds_draw_rounded_rect(cr, 336.0, thumb_y, 3.0, thumb_h, 1.5);
		cairo_set_source_rgba(cr, 0.35, 0.38, 0.50, 0.7);
		cairo_fill(cr);
	}

	/* Vertical subtle divider between columns */
	cairo_move_to(cr, 344.0, 66.0);
	cairo_line_to(cr, 344.0, 466.0);
	cairo_set_source_rgba(cr, 0.20, 0.21, 0.28, 0.6);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* 5. RIGHT COLUMN: Pinned Grid (x:348, y:60, w:294, h:412) */
	/* Right Header */
	layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 9");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, "PINNED APPS", -1);
		cairo_move_to(cr, 354.0, 70.0);
		cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* 3x3 Pinned Grid: col_w: 92, row_h: 96 */
	int display_pinned = menu->pinned_count < 9 ? menu->pinned_count : 9;

	for (int i = 0; i < display_pinned; i++) {
		int app_idx = menu->pinned_indices[i];
		int col = i % 3;
		int row = i / 3;
		double item_x = 350.0 + (double)col * 96.0;
		double item_y = 96.0 + (double)row * 104.0;
		double item_w = 90.0;
		double item_h = 96.0;

		/* Only show subtle background on hover! NO permanent boxes or borders! */
		if (menu->hover_pinned_idx == i) {
			diamonds_draw_rounded_rect(cr, item_x, item_y, item_w, item_h, 8.0);
			cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
			cairo_fill(cr);
		}

		/* Icon 40x40 centered horizontally */
		draw_icon_or_vector(cr, menu->apps[app_idx].icon, menu->apps[app_idx].name,
			item_x + (item_w - 40.0) / 2.0, item_y + 8.0, 40);

		/* Title centered below icon */
		layout = pango_cairo_create_layout(cr);
		if (layout) {
			PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " 8.5");
			pango_layout_set_font_description(layout, desc);
			pango_font_description_free(desc);
			pango_layout_set_text(layout, menu->apps[app_idx].name, -1);
			pango_layout_set_width(layout, (int)((item_w - 4.0) * PANGO_SCALE));
			pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
			pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

			cairo_move_to(cr, item_x + 2.0, item_y + 54.0);
			cairo_set_source_rgba(cr, 0.90, 0.93, 0.97, 1.0);
			pango_cairo_show_layout(cr, layout);
			g_object_unref(layout);
		}
	}

	/* 6. BOTTOM FOOTER: User info and 3 real Power buttons (y: 480 to 540) */
	cairo_move_to(cr, 0, 480.0);
	cairo_line_to(cr, w, 480.0);
	cairo_set_source_rgba(cr, 0.20, 0.21, 0.28, 0.7);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* User Avatar Silhouette Vector */
	diamonds_draw_vector_avatar(cr, 36.0, 510.0, 15.0);

	/* Real User Name from getpwuid */
	layout = pango_cairo_create_layout(cr);
	if (layout) {
		PangoFontDescription *desc = pango_font_description_from_string(FONT_BASE " Bold 10.5");
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, menu->username, -1);
		int text_w = 0, text_h = 0;
		pango_layout_get_pixel_size(layout, &text_w, &text_h);
		cairo_move_to(cr, 60.0, 510.0 - text_h / 2.0);
		cairo_set_source_rgba(cr, 0.94, 0.96, 0.99, 1.0);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
	}

	/* 3 Circular Power Buttons 32x32px (radius 16px):
	 * Button 1: Lock (cx = 542.0, cy = 510.0)
	 * Button 2: Reboot (cx = 584.0, cy = 510.0)
	 * Button 3: Shutdown (cx = 626.0, cy = 510.0)
	 */

	/* 1. Lock */
	cairo_arc(cr, 542.0, 510.0, 16.0, 0.0, 2.0 * M_PI);
	if (menu->hover_power_btn == 1) {
		cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.35);
	} else {
		cairo_set_source_rgba(cr, 0.16, 0.18, 0.26, 0.85);
	}
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.28, 0.30, 0.42, 0.60);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* Lock vector icon */
	cairo_set_source_rgba(cr, 0.85, 0.88, 0.96, 1.0);
	cairo_set_line_width(cr, 1.4);
	cairo_arc(cr, 542.0, 506.0, 4.0, -M_PI, 0.0);
	cairo_stroke(cr);
	diamonds_draw_rounded_rect(cr, 537.0, 506.0, 10.0, 8.0, 2.0);
	cairo_fill(cr);

	/* 2. Reboot */
	cairo_arc(cr, 584.0, 510.0, 16.0, 0.0, 2.0 * M_PI);
	if (menu->hover_power_btn == 2) {
		cairo_set_source_rgba(cr, 0.96, 0.76, 0.36, 0.40);
	} else {
		cairo_set_source_rgba(cr, 0.20, 0.18, 0.24, 0.85);
	}
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.36, 0.30, 0.42, 0.60);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* Reboot circular arrow icon */
	cairo_set_source_rgba(cr, 0.96, 0.76, 0.36, 1.0);
	cairo_set_line_width(cr, 1.5);
	cairo_arc(cr, 584.0, 510.0, 5.5, -0.4 * M_PI, 1.3 * M_PI);
	cairo_stroke(cr);
	cairo_move_to(cr, 581.0, 504.0);
	cairo_line_to(cr, 585.0, 504.0);
	cairo_line_to(cr, 585.0, 508.0);
	cairo_stroke(cr);

	/* 3. Shutdown */
	cairo_arc(cr, 626.0, 510.0, 16.0, 0.0, 2.0 * M_PI);
	if (menu->hover_power_btn == 3) {
		cairo_set_source_rgba(cr, 0.94, 0.27, 0.27, 0.45);
	} else {
		cairo_set_source_rgba(cr, 0.26, 0.14, 0.18, 0.85);
	}
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.48, 0.22, 0.28, 0.60);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* Shutdown power symbol */
	cairo_set_source_rgba(cr, 0.96, 0.45, 0.45, 1.0);
	cairo_set_line_width(cr, 1.5);
	cairo_arc(cr, 626.0, 510.5, 5.5, -0.35 * M_PI, 1.35 * M_PI);
	cairo_stroke(cr);
	cairo_move_to(cr, 626.0, 504.0);
	cairo_line_to(cr, 626.0, 510.0);
	cairo_stroke(cr);

	cairo_destroy(cr);

	pixman_region32_t damage;
	pixman_region32_init_rect(&damage, 0, 0, (uint32_t)menu->width, (uint32_t)menu->height);
	wlr_scene_buffer_set_buffer_with_damage(menu->scene_buffer, &menu->buffer->base, &damage);
	pixman_region32_fini(&damage);
}

bool diamonds_startmenu_handle_motion(struct diamonds_startmenu *menu, double lx, double ly) {
	if (!menu || !menu->visible) return false;

	if (lx < menu->x || lx > menu->x + menu->width ||
	    ly < menu->y || ly > menu->y + menu->height) {
		if (menu->hover_app_idx != -1 || menu->hover_pinned_idx != -1 || menu->hover_power_btn != 0) {
			menu->hover_app_idx = -1;
			menu->hover_pinned_idx = -1;
			menu->hover_power_btn = 0;
			diamonds_startmenu_render(menu);
		}
		return false;
	}

	double rel_x = lx - menu->x;
	double rel_y = ly - menu->y;

	int old_app = menu->hover_app_idx;
	int old_pin = menu->hover_pinned_idx;
	int old_pwr = menu->hover_power_btn;

	menu->hover_app_idx = -1;
	menu->hover_pinned_idx = -1;
	menu->hover_power_btn = 0;

	/* 1. Left List (y: 92 to 468, x: 18 to 336) */
	if (rel_x >= 18.0 && rel_x <= 336.0 && rel_y >= 92.0 && rel_y <= 468.0) {
		double list_rel_y = (rel_y - 94.0) + menu->scroll_offset;
		int row_idx = (int)(list_rel_y / 42.0);
		if (row_idx >= 0 && row_idx < menu->filtered_count) {
			menu->hover_app_idx = row_idx;
		}
	}
	/* 2. Right Pinned Grid (y: 96 to 408, x: 348 to 642) */
	else if (rel_x >= 348.0 && rel_x <= 642.0 && rel_y >= 96.0 && rel_y <= 408.0) {
		int col = (int)((rel_x - 350.0) / 96.0);
		int row = (int)((rel_y - 96.0) / 104.0);
		if (col >= 0 && col < 3 && row >= 0 && row < 3) {
			int p = row * 3 + col;
			if (p < menu->pinned_count) {
				menu->hover_pinned_idx = p;
			}
		}
	}
	/* 3. Power buttons (y: 494 to 526) */
	else if (rel_y >= 494.0 && rel_y <= 526.0) {
		double d_lock = hypot(rel_x - 542.0, rel_y - 510.0);
		double d_reb  = hypot(rel_x - 584.0, rel_y - 510.0);
		double d_shut = hypot(rel_x - 626.0, rel_y - 510.0);
		if (d_lock <= 16.0) menu->hover_power_btn = 1;
		else if (d_reb <= 16.0) menu->hover_power_btn = 2;
		else if (d_shut <= 16.0) menu->hover_power_btn = 3;
	}

	if (menu->hover_app_idx != old_app ||
	    menu->hover_pinned_idx != old_pin ||
	    menu->hover_power_btn != old_pwr) {
		diamonds_startmenu_render(menu);
	}
	return true;
}

bool diamonds_startmenu_handle_click(struct diamonds_startmenu *menu, double lx, double ly) {
	if (!menu || !menu->visible) return false;

	if (lx < menu->x || lx > menu->x + menu->width ||
	    ly < menu->y || ly > menu->y + menu->height) {
		diamonds_startmenu_set_visible(menu, false);
		return true;
	}

	double rel_x = lx - menu->x;
	double rel_y = ly - menu->y;

	/* 1. Check Search Bar Clear Button (✕) */
	if (menu->search_len > 0 &&
	    rel_x >= (double)menu->width - 44.0 && rel_x <= (double)menu->width - 24.0 &&
	    rel_y >= 20.0 && rel_y <= 46.0) {
		menu->search_query[0] = '\0';
		menu->search_len = 0;
		diamonds_startmenu_filter(menu);
		diamonds_startmenu_render(menu);
		return true;
	}

	/* 2. Check 3 Circular Power Buttons */
	/* Lock (cx = 542.0, cy = 510.0, r = 16.0) */
	if (hypot(rel_x - 542.0, rel_y - 510.0) <= 18.0) {
		diamonds_startmenu_set_visible(menu, false);
		launch_desktop_cmd("swaylock -f -c 000000 || loginctl lock-session || dm-tool lock &");
		return true;
	}

	/* Reboot (cx = 584.0, cy = 510.0, r = 16.0) */
	if (hypot(rel_x - 584.0, rel_y - 510.0) <= 18.0) {
		diamonds_startmenu_set_visible(menu, false);
		launch_desktop_cmd("systemctl reboot || reboot &");
		return true;
	}

	/* Shutdown (cx = 626.0, cy = 510.0, r = 16.0) */
	if (hypot(rel_x - 626.0, rel_y - 510.0) <= 18.0) {
		diamonds_startmenu_set_visible(menu, false);
		launch_desktop_cmd("systemctl poweroff || poweroff &");
		return true;
	}

	/* 3. Check Left List items (y: 92 to 468, x: 18 to 336) */
	if (rel_x >= 18.0 && rel_x <= 336.0 && rel_y >= 92.0 && rel_y <= 468.0) {
		double list_rel_y = (rel_y - 94.0) + menu->scroll_offset;
		int row_idx = (int)(list_rel_y / 42.0);
		if (row_idx >= 0 && row_idx < menu->filtered_count) {
			int app_idx = menu->filtered_indices[row_idx];
			diamonds_startmenu_set_visible(menu, false);
			launch_desktop_cmd(menu->apps[app_idx].exec);
			return true;
		}
	}

	/* 4. Check Right Pinned Grid (y: 96 to 408, x: 348 to 642) */
	if (rel_x >= 348.0 && rel_x <= 642.0 && rel_y >= 96.0 && rel_y <= 408.0) {
		int col = (int)((rel_x - 350.0) / 96.0);
		int row = (int)((rel_y - 96.0) / 104.0);
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

bool diamonds_startmenu_handle_key_event(struct diamonds_startmenu *menu,
		struct xkb_state *xkb_state, uint32_t keycode,
		const xkb_keysym_t *syms, int nsyms) {
	if (!menu || !menu->visible) return false;

	for (int i = 0; i < nsyms; i++) {
		xkb_keysym_t sym = syms[i];

		if (sym == XKB_KEY_Escape) {
			if (menu->search_len > 0) {
				menu->search_len = 0;
				menu->search_query[0] = '\0';
				diamonds_startmenu_filter(menu);
				diamonds_startmenu_render(menu);
			} else {
				diamonds_startmenu_set_visible(menu, false);
			}
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
			if (menu->search_len > 0) {
				menu->search_len--;
				while (menu->search_len > 0 &&
				       (menu->search_query[menu->search_len] & 0xC0) == 0x80) {
					menu->search_len--;
				}
				menu->search_query[menu->search_len] = '\0';
				menu->scroll_offset = 0.0;
				diamonds_startmenu_filter(menu);
				diamonds_startmenu_render(menu);
			}
			return true;
		}

		if (sym == XKB_KEY_Up) {
			menu->scroll_offset -= 42.0;
			if (menu->scroll_offset < 0.0) menu->scroll_offset = 0.0;
			diamonds_startmenu_render(menu);
			return true;
		}

		if (sym == XKB_KEY_Down) {
			menu->scroll_offset += 42.0;
			if (menu->scroll_offset > menu->max_scroll) menu->scroll_offset = menu->max_scroll;
			diamonds_startmenu_render(menu);
			return true;
		}
	}

	char utf8_buf[32] = {0};
	int len = xkb_state_key_get_utf8(xkb_state, keycode, utf8_buf, sizeof(utf8_buf) - 1);
	if (len > 0 && (unsigned char)utf8_buf[0] >= 0x20 && utf8_buf[0] != 0x7F) {
		if (menu->search_len + len < (int)sizeof(menu->search_query) - 1) {
			memcpy(menu->search_query + menu->search_len, utf8_buf, len);
			menu->search_len += len;
			menu->search_query[menu->search_len] = '\0';
			menu->scroll_offset = 0.0;
			diamonds_startmenu_filter(menu);
			diamonds_startmenu_render(menu);
		}
		return true;
	}

	return true;
}

bool diamonds_startmenu_handle_key(struct diamonds_startmenu *menu, xkb_keysym_t sym, enum wl_keyboard_key_state state) {
	if (!menu || !menu->visible) return false;

	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		return true;
	}

	if (sym == XKB_KEY_Escape) {
		if (menu->search_len > 0) {
			menu->search_len = 0;
			menu->search_query[0] = '\0';
			diamonds_startmenu_filter(menu);
			diamonds_startmenu_render(menu);
		} else {
			diamonds_startmenu_set_visible(menu, false);
		}
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
		if (menu->search_len > 0) {
			menu->search_len--;
			menu->search_query[menu->search_len] = '\0';
			diamonds_startmenu_filter(menu);
			diamonds_startmenu_render(menu);
		}
		return true;
	}

	if (sym == XKB_KEY_Up) {
		menu->scroll_offset -= 42.0;
		if (menu->scroll_offset < 0.0) menu->scroll_offset = 0.0;
		diamonds_startmenu_render(menu);
		return true;
	}

	if (sym == XKB_KEY_Down) {
		menu->scroll_offset += 42.0;
		if (menu->scroll_offset > menu->max_scroll) menu->scroll_offset = menu->max_scroll;
		diamonds_startmenu_render(menu);
		return true;
	}

	if (sym >= 0x20 && sym <= 0x7E) {
		if (menu->search_len < (int)sizeof(menu->search_query) - 1) {
			menu->search_query[menu->search_len++] = (char)sym;
			menu->search_query[menu->search_len] = '\0';
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
