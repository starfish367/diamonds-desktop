/* Diamonds Photos - Lightweight GTK3 + Cairo Image Viewer & Folder Grid
 * Pure C11, GdkPixbuf + Cairo, lazy thumbnail loading, GIF animation support.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <cairo.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "diamonds_gtk.h"

#define MAX_SIBLINGS      2048
#define MAX_THUMB_CACHE   256
#define THUMB_SIZE        128
#define MIN_ZOOM          0.05
#define MAX_ZOOM          20.0

typedef struct {
	char *path;
	char *name;
	int64_t file_size;
	GtkWidget *image_widget;
	bool thumb_loaded;
} PhotoEntry;

typedef struct {
	GtkWidget *window;
	GtkWidget *header_bar;
	GtkWidget *stack;
	GtkWidget *drawing_area;
	GtkWidget *flow_box;
	GtkWidget *lbl_status;
	GtkWidget *btn_back_grid;

	/* Supported extensions from gdk_pixbuf_get_formats() */
	GPtrArray *supported_exts;

	/* Current directory & sibling entries */
	char current_dir[1024];
	PhotoEntry entries[MAX_SIBLINGS];
	int entry_count;
	int current_index;

	/* Thumbnail lazy-load idle state */
	int thumb_load_cursor;
	int thumb_cached_count;
	guint thumb_idle_id;

	/* Current image state */
	GdkPixbuf *pixbuf;
	GdkPixbufAnimation *anim;
	GdkPixbufAnimationIter *anim_iter;
	guint anim_timer_id;
	int orig_width;
	int orig_height;

	/* View transform state */
	bool fit_mode;
	double zoom;
	double pan_x;
	double pan_y;
	bool dragging;
	double drag_start_x;
	double drag_start_y;
	double drag_origin_pan_x;
	double drag_origin_pan_y;

	/* UI flags */
	bool show_info;
	bool is_fullscreen;
} PhotosApp;

static const char *PHOTOS_EXTRA_CSS =
	".thumb-tile { background-color: #1a1b26; border: 1px solid #282a36; border-radius: 8px; padding: 6px; margin: 4px; }"
	".thumb-tile:hover, flowboxchild:selected .thumb-tile { background-color: #242636; border-color: #38bdf8; }"
	".thumb-label { color: #cbd5e1; font-size: 11px; }"
	".toolbar-bar { background-color: #1a1b26; border-top: 1px solid #282a36; padding: 4px 10px; }";

static void init_supported_formats(PhotosApp *app) {
	app->supported_exts = g_ptr_array_new_with_free_func(g_free);
	GSList *formats = gdk_pixbuf_get_formats();
	for (GSList *l = formats; l != NULL; l = l->next) {
		GdkPixbufFormat *fmt = (GdkPixbufFormat *)l->data;
		if (gdk_pixbuf_format_is_disabled(fmt)) continue;
		gchar **exts = gdk_pixbuf_format_get_extensions(fmt);
		if (!exts) continue;
		for (int i = 0; exts[i] != NULL; i++) {
			char *lower = g_ascii_strdown(exts[i], -1);
			bool exists = false;
			for (guint j = 0; j < app->supported_exts->len; j++) {
				if (strcmp((const char *)g_ptr_array_index(app->supported_exts, j), lower) == 0) {
					exists = true;
					break;
				}
			}
			if (!exists) {
				g_ptr_array_add(app->supported_exts, lower);
			} else {
				g_free(lower);
			}
		}
		g_strfreev(exts);
	}
	g_slist_free(formats);

	/* Ensure common fallback extensions are present */
	static const char *fallbacks[] = {
		"png", "jpg", "jpeg", "gif", "bmp", "webp", "svg", "tiff", "tif", "ico", NULL
	};
	for (int i = 0; fallbacks[i] != NULL; i++) {
		bool exists = false;
		for (guint j = 0; j < app->supported_exts->len; j++) {
			if (strcmp((const char *)g_ptr_array_index(app->supported_exts, j), fallbacks[i]) == 0) {
				exists = true;
				break;
			}
		}
		if (!exists) {
			g_ptr_array_add(app->supported_exts, g_strdup(fallbacks[i]));
		}
	}
}

static bool is_supported_image_file(PhotosApp *app, const char *filename) {
	if (!filename || filename[0] == '.') return false;
	const char *dot = strrchr(filename, '.');
	if (!dot || !dot[1]) return false;
	char *ext = g_ascii_strdown(dot + 1, -1);
	bool supported = false;
	for (guint i = 0; i < app->supported_exts->len; i++) {
		if (strcmp((const char *)g_ptr_array_index(app->supported_exts, i), ext) == 0) {
			supported = true;
			break;
		}
	}
	g_free(ext);
	return supported;
}

static void clear_current_image(PhotosApp *app) {
	if (app->anim_timer_id > 0) {
		g_source_remove(app->anim_timer_id);
		app->anim_timer_id = 0;
	}
	if (app->anim_iter) {
		g_object_unref(app->anim_iter);
		app->anim_iter = NULL;
	}
	if (app->anim) {
		g_object_unref(app->anim);
		app->anim = NULL;
	}
	if (app->pixbuf) {
		g_object_unref(app->pixbuf);
		app->pixbuf = NULL;
	}
	app->orig_width = 0;
	app->orig_height = 0;
}

static void clear_entries(PhotosApp *app) {
	if (app->thumb_idle_id > 0) {
		g_source_remove(app->thumb_idle_id);
		app->thumb_idle_id = 0;
	}
	for (int i = 0; i < app->entry_count; i++) {
		g_free(app->entries[i].path);
		app->entries[i].path = NULL;
		g_free(app->entries[i].name);
		app->entries[i].name = NULL;
		app->entries[i].image_widget = NULL;
		app->entries[i].thumb_loaded = false;
	}
	app->entry_count = 0;
	app->current_index = -1;
	app->thumb_load_cursor = 0;
	app->thumb_cached_count = 0;

	if (app->flow_box) {
		GList *children = gtk_container_get_children(GTK_CONTAINER(app->flow_box));
		for (GList *l = children; l != NULL; l = l->next) {
			gtk_widget_destroy(GTK_WIDGET(l->data));
		}
		g_list_free(children);
	}
}

static int compare_entries(const void *a, const void *b) {
	const PhotoEntry *pa = (const PhotoEntry *)a;
	const PhotoEntry *pb = (const PhotoEntry *)b;
	return g_ascii_strcasecmp(pa->name, pb->name);
}

static void format_file_size(int64_t bytes, char *out, size_t out_len) {
	if (bytes < 1024) {
		snprintf(out, out_len, "%lld B", (long long)bytes);
	} else if (bytes < 1024 * 1024) {
		snprintf(out, out_len, "%.1f KB", (double)bytes / 1024.0);
	} else {
		snprintf(out, out_len, "%.2f MB", (double)bytes / (1024.0 * 1024.0));
	}
}

static void update_status_bar(PhotosApp *app) {
	if (!app->lbl_status) return;
	const char *visible = gtk_stack_get_visible_child_name(GTK_STACK(app->stack));
	if (visible && strcmp(visible, "grid") == 0) {
		char buf[1200];
		snprintf(buf, sizeof(buf), "Folder: %s  (%d images)", app->current_dir, app->entry_count);
		gtk_label_set_text(GTK_LABEL(app->lbl_status), buf);
		gtk_header_bar_set_title(GTK_HEADER_BAR(app->header_bar), "Diamonds Photos");
		gtk_header_bar_set_subtitle(GTK_HEADER_BAR(app->header_bar), app->current_dir);
		return;
	}

	if (app->current_index >= 0 && app->current_index < app->entry_count && app->pixbuf) {
		PhotoEntry *e = &app->entries[app->current_index];
		char sz[64];
		format_file_size(e->file_size, sz, sizeof(sz));
		char buf[512];
		snprintf(buf, sizeof(buf), "[%d / %d]  %s  •  %d × %d px  •  %s  •  Zoom: %.0f%%",
			app->current_index + 1, app->entry_count, e->name,
			gdk_pixbuf_get_width(app->pixbuf), gdk_pixbuf_get_height(app->pixbuf),
			sz, app->zoom * 100.0);
		gtk_label_set_text(GTK_LABEL(app->lbl_status), buf);
		gtk_header_bar_set_title(GTK_HEADER_BAR(app->header_bar), e->name);
		gtk_header_bar_set_subtitle(GTK_HEADER_BAR(app->header_bar), app->current_dir);
	} else {
		gtk_label_set_text(GTK_LABEL(app->lbl_status), "No image loaded");
		gtk_header_bar_set_title(GTK_HEADER_BAR(app->header_bar), "Diamonds Photos");
		gtk_header_bar_set_subtitle(GTK_HEADER_BAR(app->header_bar), NULL);
	}
}

static void compute_fit_zoom(PhotosApp *app) {
	if (!app->pixbuf || !app->drawing_area) return;
	int aw = gtk_widget_get_allocated_width(app->drawing_area);
	int ah = gtk_widget_get_allocated_height(app->drawing_area);
	if (aw <= 10 || ah <= 10) {
		aw = 800;
		ah = 520;
	}
	int pw = gdk_pixbuf_get_width(app->pixbuf);
	int ph = gdk_pixbuf_get_height(app->pixbuf);
	if (pw <= 0 || ph <= 0) return;

	double zx = (double)(aw - 16) / (double)pw;
	double zy = (double)(ah - 16) / (double)ph;
	double z = (zx < zy) ? zx : zy;
	if (z > 1.0) z = 1.0; /* Do not upscale small images in fit mode by default */
	if (z < MIN_ZOOM) z = MIN_ZOOM;
	app->zoom = z;
	app->pan_x = 0.0;
	app->pan_y = 0.0;
}

static gboolean on_anim_tick(gpointer user_data) {
	PhotosApp *app = (PhotosApp *)user_data;
	if (!app->anim_iter) {
		app->anim_timer_id = 0;
		return G_SOURCE_REMOVE;
	}
	if (gdk_pixbuf_animation_iter_advance(app->anim_iter, NULL)) {
		GdkPixbuf *frame = gdk_pixbuf_animation_iter_get_pixbuf(app->anim_iter);
		if (frame) {
			if (app->pixbuf) g_object_unref(app->pixbuf);
			app->pixbuf = g_object_ref(frame);
			gtk_widget_queue_draw(app->drawing_area);
		}
	}
	int delay = gdk_pixbuf_animation_iter_get_delay_time(app->anim_iter);
	if (delay < 20) delay = 100;
	app->anim_timer_id = g_timeout_add((guint)delay, on_anim_tick, app);
	return G_SOURCE_REMOVE;
}

static bool load_image_at_index(PhotosApp *app, int index) {
	if (index < 0 || index >= app->entry_count) return false;
	clear_current_image(app);

	const char *path = app->entries[index].path;
	GError *err = NULL;
	GdkPixbufAnimation *anim = gdk_pixbuf_animation_new_from_file(path, &err);
	if (!anim) {
		if (err) g_error_free(err);
		return false;
	}

	app->current_index = index;
	app->anim = anim;
	if (gdk_pixbuf_animation_is_static_image(anim)) {
		GdkPixbuf *static_pb = gdk_pixbuf_animation_get_static_image(anim);
		if (static_pb) {
			GdkPixbuf *oriented = gdk_pixbuf_apply_embedded_orientation(static_pb);
			app->pixbuf = oriented ? oriented : g_object_ref(static_pb);
		}
	} else {
		app->anim_iter = gdk_pixbuf_animation_get_iter(anim, NULL);
		GdkPixbuf *frame = gdk_pixbuf_animation_iter_get_pixbuf(app->anim_iter);
		if (frame) {
			app->pixbuf = g_object_ref(frame);
		}
		int delay = gdk_pixbuf_animation_iter_get_delay_time(app->anim_iter);
		if (delay > 0) {
			if (delay < 20) delay = 100;
			app->anim_timer_id = g_timeout_add((guint)delay, on_anim_tick, app);
		}
	}

	if (!app->pixbuf) {
		clear_current_image(app);
		return false;
	}

	app->orig_width = gdk_pixbuf_get_width(app->pixbuf);
	app->orig_height = gdk_pixbuf_get_height(app->pixbuf);
	app->fit_mode = true;
	compute_fit_zoom(app);
	update_status_bar(app);
	gtk_widget_queue_draw(app->drawing_area);
	return true;
}

static gboolean lazy_load_next_thumbnail(gpointer user_data) {
	PhotosApp *app = (PhotosApp *)user_data;
	while (app->thumb_load_cursor < app->entry_count) {
		int idx = app->thumb_load_cursor++;
		PhotoEntry *e = &app->entries[idx];
		if (e->thumb_loaded || !e->image_widget) continue;
		if (app->thumb_cached_count >= MAX_THUMB_CACHE) {
			app->thumb_idle_id = 0;
			return G_SOURCE_REMOVE;
		}
		GError *err = NULL;
		GdkPixbuf *thumb = gdk_pixbuf_new_from_file_at_scale(
			e->path, THUMB_SIZE, THUMB_SIZE, TRUE, &err);
		if (err) g_error_free(err);
		if (thumb) {
			gtk_image_set_from_pixbuf(GTK_IMAGE(e->image_widget), thumb);
			g_object_unref(thumb);
			e->thumb_loaded = true;
			app->thumb_cached_count++;
		}
		return G_SOURCE_CONTINUE;
	}
	app->thumb_idle_id = 0;
	return G_SOURCE_REMOVE;
}

static void populate_grid_widgets(PhotosApp *app) {
	for (int i = 0; i < app->entry_count; i++) {
		PhotoEntry *e = &app->entries[i];
		GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
		gtk_style_context_add_class(gtk_widget_get_style_context(box), "thumb-tile");
		gtk_widget_set_size_request(box, THUMB_SIZE + 20, THUMB_SIZE + 38);

		GtkWidget *img = gtk_image_new_from_icon_name("image-x-generic", GTK_ICON_SIZE_DIALOG);
		gtk_widget_set_size_request(img, THUMB_SIZE, THUMB_SIZE);
		gtk_box_pack_start(GTK_BOX(box), img, TRUE, TRUE, 0);
		e->image_widget = img;

		GtkWidget *lbl = gtk_label_new(e->name);
		gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "thumb-label");
		gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
		gtk_label_set_max_width_chars(GTK_LABEL(lbl), 16);
		gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);

		gtk_widget_show_all(box);
		gtk_flow_box_insert(GTK_FLOW_BOX(app->flow_box), box, -1);
	}
	app->thumb_load_cursor = 0;
	if (app->entry_count > 0 && app->thumb_idle_id == 0) {
		app->thumb_idle_id = g_idle_add(lazy_load_next_thumbnail, app);
	}
}

static void scan_directory(PhotosApp *app, const char *dir_path, const char *select_filename) {
	clear_current_image(app);
	clear_entries(app);

	snprintf(app->current_dir, sizeof(app->current_dir), "%s", dir_path);
	DIR *dir = opendir(dir_path);
	if (!dir) {
		update_status_bar(app);
		return;
	}

	struct dirent *de;
	while ((de = readdir(dir)) != NULL && app->entry_count < MAX_SIBLINGS) {
		if (!is_supported_image_file(app, de->d_name)) continue;
		char full[2048];
		snprintf(full, sizeof(full), "%s/%s", dir_path, de->d_name);
		struct stat st;
		if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;

		PhotoEntry *e = &app->entries[app->entry_count++];
		e->path = g_strdup(full);
		e->name = g_strdup(de->d_name);
		e->file_size = (int64_t)st.st_size;
		e->image_widget = NULL;
		e->thumb_loaded = false;
	}
	closedir(dir);

	if (app->entry_count > 1) {
		qsort(app->entries, (size_t)app->entry_count, sizeof(PhotoEntry), compare_entries);
	}

	populate_grid_widgets(app);

	if (select_filename && *select_filename) {
		int found = -1;
		for (int i = 0; i < app->entry_count; i++) {
			if (strcmp(app->entries[i].name, select_filename) == 0) {
				found = i;
				break;
			}
		}
		if (found >= 0 && load_image_at_index(app, found)) {
			gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "viewer");
			gtk_widget_set_sensitive(app->btn_back_grid, TRUE);
			update_status_bar(app);
			return;
		}
	}

	gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "grid");
	gtk_widget_set_sensitive(app->btn_back_grid, FALSE);
	update_status_bar(app);
}

static void open_path(PhotosApp *app, const char *path) {
	if (!path || !*path) return;
	struct stat st;
	if (stat(path, &st) != 0) return;

	if (S_ISDIR(st.st_mode)) {
		char *real = realpath(path, NULL);
		scan_directory(app, real ? real : path, NULL);
		free(real);
		real = NULL;
	} else if (S_ISREG(st.st_mode)) {
		char *real = realpath(path, NULL);
		const char *use_path = real ? real : path;
		char *dir = g_path_get_dirname(use_path);
		char *base = g_path_get_basename(use_path);
		scan_directory(app, dir, base);
		g_free(dir);
		g_free(base);
		free(real);
		real = NULL;
	}
}

/* Drawing Area Render */
static gboolean on_drawing_area_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
	PhotosApp *app = (PhotosApp *)user_data;
	int aw = gtk_widget_get_allocated_width(widget);
	int ah = gtk_widget_get_allocated_height(widget);

	/* Dark Mica canvas background */
	cairo_set_source_rgb(cr, 0.086, 0.086, 0.118);
	cairo_paint(cr);

	if (!app->pixbuf) {
		cairo_set_source_rgb(cr, 0.58, 0.64, 0.72);
		cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
		cairo_set_font_size(cr, 15.0);
		cairo_move_to(cr, aw / 2.0 - 70.0, ah / 2.0);
		cairo_show_text(cr, "No image loaded");
		return FALSE;
	}

	if (app->fit_mode) {
		compute_fit_zoom(app);
	}

	int pw = gdk_pixbuf_get_width(app->pixbuf);
	int ph = gdk_pixbuf_get_height(app->pixbuf);
	double draw_w = pw * app->zoom;
	double draw_h = ph * app->zoom;
	double ox = (aw - draw_w) * 0.5 + app->pan_x;
	double oy = (ah - draw_h) * 0.5 + app->pan_y;

	cairo_save(cr);
	cairo_translate(cr, ox, oy);

	/* Subtle dark checkerboard for transparent images */
	if (gdk_pixbuf_get_has_alpha(app->pixbuf)) {
		cairo_save(cr);
		cairo_rectangle(cr, 0, 0, draw_w, draw_h);
		cairo_clip(cr);
		int cell = 12;
		int cols = (int)(draw_w / cell) + 1;
		int rows = (int)(draw_h / cell) + 1;
		for (int r = 0; r < rows; r++) {
			for (int c = 0; c < cols; c++) {
				if ((r + c) & 1) {
					cairo_set_source_rgb(cr, 0.14, 0.15, 0.20);
				} else {
					cairo_set_source_rgb(cr, 0.11, 0.11, 0.15);
				}
				cairo_rectangle(cr, c * cell, r * cell, cell, cell);
				cairo_fill(cr);
			}
		}
		cairo_restore(cr);
	}

	cairo_scale(cr, app->zoom, app->zoom);
	gdk_cairo_set_source_pixbuf(cr, app->pixbuf, 0, 0);
	cairo_pattern_t *pat = cairo_get_source(cr);
	cairo_pattern_set_filter(pat, (app->zoom >= 3.0) ? CAIRO_FILTER_NEAREST : CAIRO_FILTER_BILINEAR);
	cairo_paint(cr);
	cairo_restore(cr);

	/* Image Info Overlay (toggle with 'I' or Info button) */
	if (app->show_info && app->current_index >= 0 && app->current_index < app->entry_count) {
		PhotoEntry *e = &app->entries[app->current_index];
		char sz[64];
		format_file_size(e->file_size, sz, sizeof(sz));

		double box_x = 16.0, box_y = 16.0, box_w = 320.0, box_h = 112.0;
		cairo_save(cr);
		cairo_set_source_rgba(cr, 0.10, 0.106, 0.149, 0.88);
		cairo_rectangle(cr, box_x, box_y, box_w, box_h);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0.22, 0.74, 0.97, 0.6);
		cairo_set_line_width(cr, 1.0);
		cairo_stroke(cr);

		cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, 13.0);
		cairo_set_source_rgb(cr, 0.22, 0.74, 0.97);
		cairo_move_to(cr, box_x + 12.0, box_y + 22.0);
		cairo_show_text(cr, e->name);

		cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
		cairo_set_font_size(cr, 12.0);
		cairo_set_source_rgb(cr, 0.88, 0.91, 0.94);

		char line1[128], line2[128], line3[128];
		snprintf(line1, sizeof(line1), "Dimensions: %d × %d px", pw, ph);
		snprintf(line2, sizeof(line2), "File Size: %s  (%d / %d)", sz, app->current_index + 1, app->entry_count);
		snprintf(line3, sizeof(line3), "Zoom: %.0f%% (%s)", app->zoom * 100.0, app->fit_mode ? "Fit" : "Manual");

		cairo_move_to(cr, box_x + 12.0, box_y + 46.0);
		cairo_show_text(cr, line1);
		cairo_move_to(cr, box_x + 12.0, box_y + 68.0);
		cairo_show_text(cr, line2);
		cairo_move_to(cr, box_x + 12.0, box_y + 90.0);
		cairo_show_text(cr, line3);
		cairo_restore(cr);
	}

	return FALSE;
}

/* Zoom & Transform Helpers */
static void set_zoom_level(PhotosApp *app, double new_zoom) {
	if (!app->pixbuf) return;
	if (new_zoom < MIN_ZOOM) new_zoom = MIN_ZOOM;
	if (new_zoom > MAX_ZOOM) new_zoom = MAX_ZOOM;
	app->fit_mode = false;
	app->zoom = new_zoom;
	update_status_bar(app);
	gtk_widget_queue_draw(app->drawing_area);
}

static void rotate_current_image(PhotosApp *app, GdkPixbufRotation angle) {
	if (!app->pixbuf) return;
	GdkPixbuf *rotated = gdk_pixbuf_rotate_simple(app->pixbuf, angle);
	if (rotated) {
		g_object_unref(app->pixbuf);
		app->pixbuf = rotated;
		if (app->fit_mode) compute_fit_zoom(app);
		update_status_bar(app);
		gtk_widget_queue_draw(app->drawing_area);
	}
}

static void flip_current_image(PhotosApp *app, gboolean horizontal) {
	if (!app->pixbuf) return;
	GdkPixbuf *flipped = gdk_pixbuf_flip(app->pixbuf, horizontal);
	if (flipped) {
		g_object_unref(app->pixbuf);
		app->pixbuf = flipped;
		gtk_widget_queue_draw(app->drawing_area);
	}
}

static void navigate_relative(PhotosApp *app, int delta) {
	if (app->entry_count <= 0) return;
	int next = app->current_index + delta;
	if (next < 0) next = app->entry_count - 1;
	if (next >= app->entry_count) next = 0;
	if (load_image_at_index(app, next)) {
		gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "viewer");
		gtk_widget_set_sensitive(app->btn_back_grid, TRUE);
	}
}

/* Set as Wallpaper: write to ~/.config/diamonds/wallpaper and ~/.config/diamonds/settings.conf */
static void action_set_wallpaper(PhotosApp *app) {
	if (app->current_index < 0 || app->current_index >= app->entry_count) return;
	const char *img_path = app->entries[app->current_index].path;
	const char *home = getenv("HOME");
	if (!home || !img_path) return;

	char dir[1024];
	snprintf(dir, sizeof(dir), "%s/.config", home);
	mkdir(dir, 0755);
	snprintf(dir, sizeof(dir), "%s/.config/diamonds", home);
	mkdir(dir, 0755);

	/* 1. Write path to ~/.config/diamonds/wallpaper */
	char wall_file[1024];
	snprintf(wall_file, sizeof(wall_file), "%s/.config/diamonds/wallpaper", home);
	FILE *fp = fopen(wall_file, "w");
	if (fp) {
		fprintf(fp, "%s\n", img_path);
		fclose(fp);
	}

	/* 2. Update wallpaper=<path> in ~/.config/diamonds/settings.conf (used by diamonds_settings.c) */
	char conf_path[1024];
	snprintf(conf_path, sizeof(conf_path), "%s/.config/diamonds/settings.conf", home);
	char lines[64][1024];
	int line_count = 0;
	bool key_found = false;

	fp = fopen(conf_path, "r");
	if (fp) {
		char buf[1024];
		while (fgets(buf, sizeof(buf), fp) && line_count < 60) {
			buf[strcspn(buf, "\r\n")] = '\0';
			if (strncmp(buf, "wallpaper=", 10) == 0) {
				snprintf(lines[line_count++], sizeof(lines[0]), "wallpaper=%s", img_path);
				key_found = true;
			} else {
				snprintf(lines[line_count++], sizeof(lines[0]), "%s", buf);
			}
		}
		fclose(fp);
	}
	if (!key_found && line_count < 62) {
		snprintf(lines[line_count++], sizeof(lines[0]), "wallpaper=%s", img_path);
	}
	fp = fopen(conf_path, "w");
	if (fp) {
		for (int i = 0; i < line_count; i++) {
			fprintf(fp, "%s\n", lines[i]);
		}
		fclose(fp);
	}

	char msg[1200];
	snprintf(msg, sizeof(msg), "✓ Wallpaper set to: %s", app->entries[app->current_index].name);
	gtk_label_set_text(GTK_LABEL(app->lbl_status), msg);
}

/* Delete to Trash with Confirmation */
static void action_delete_to_trash(PhotosApp *app) {
	if (app->current_index < 0 || app->current_index >= app->entry_count) return;
	PhotoEntry *e = &app->entries[app->current_index];

	GtkWidget *dlg = gtk_message_dialog_new(
		GTK_WINDOW(app->window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_QUESTION,
		GTK_BUTTONS_YES_NO,
		"Move \"%s\" to Trash?", e->name);
	int resp = gtk_dialog_run(GTK_DIALOG(dlg));
	gtk_widget_destroy(dlg);
	if (resp != GTK_RESPONSE_YES) return;

	GFile *gf = g_file_new_for_path(e->path);
	GError *err = NULL;
	gboolean ok = g_file_trash(gf, NULL, &err);
	g_object_unref(gf);

	if (!ok) {
		GtkWidget *edlg = gtk_message_dialog_new(
			GTK_WINDOW(app->window),
			GTK_DIALOG_MODAL,
			GTK_MESSAGE_ERROR,
			GTK_BUTTONS_OK,
			"Failed to move file to Trash: %s",
			err ? err->message : "Unknown error");
		if (err) g_error_free(err);
		gtk_dialog_run(GTK_DIALOG(edlg));
		gtk_widget_destroy(edlg);
		return;
	}

	int removed_idx = app->current_index;
	char next_name[512] = {0};
	if (app->entry_count > 1) {
		int next_idx = (removed_idx + 1 < app->entry_count) ? (removed_idx + 1) : (removed_idx - 1);
		snprintf(next_name, sizeof(next_name), "%s", app->entries[next_idx].name);
	}
	char dir_copy[1024];
	snprintf(dir_copy, sizeof(dir_copy), "%s", app->current_dir);
	scan_directory(app, dir_copy, next_name[0] ? next_name : NULL);
}

static void toggle_fullscreen(PhotosApp *app) {
	app->is_fullscreen = !app->is_fullscreen;
	if (app->is_fullscreen) {
		gtk_window_fullscreen(GTK_WINDOW(app->window));
	} else {
		gtk_window_unfullscreen(GTK_WINDOW(app->window));
	}
}

/* Mouse & Keyboard Input Callbacks */
static gboolean on_scroll_event(GtkWidget *widget, GdkEventScroll *ev, gpointer user_data) {
	(void)widget;
	PhotosApp *app = (PhotosApp *)user_data;
	if (!app->pixbuf) return FALSE;

	if (ev->state & GDK_CONTROL_MASK) {
		double factor = 1.0;
		if (ev->direction == GDK_SCROLL_UP) {
			factor = 1.2;
		} else if (ev->direction == GDK_SCROLL_DOWN) {
			factor = 1.0 / 1.2;
		} else if (ev->direction == GDK_SCROLL_SMOOTH) {
			if (ev->delta_y < 0) factor = 1.15;
			else if (ev->delta_y > 0) factor = 1.0 / 1.15;
		}
		if (factor != 1.0) {
			set_zoom_level(app, app->zoom * factor);
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *ev, gpointer user_data) {
	(void)widget;
	PhotosApp *app = (PhotosApp *)user_data;
	if (ev->button == 1 && app->pixbuf) {
		app->dragging = true;
		app->drag_start_x = ev->x;
		app->drag_start_y = ev->y;
		app->drag_origin_pan_x = app->pan_x;
		app->drag_origin_pan_y = app->pan_y;
		return TRUE;
	}
	return FALSE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *ev, gpointer user_data) {
	(void)widget;
	PhotosApp *app = (PhotosApp *)user_data;
	if (ev->button == 1) {
		app->dragging = false;
		return TRUE;
	}
	return FALSE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *ev, gpointer user_data) {
	PhotosApp *app = (PhotosApp *)user_data;
	if (app->dragging && app->pixbuf) {
		app->fit_mode = false;
		app->pan_x = app->drag_origin_pan_x + (ev->x - app->drag_start_x);
		app->pan_y = app->drag_origin_pan_y + (ev->y - app->drag_start_y);
		gtk_widget_queue_draw(widget);
		return TRUE;
	}
	return FALSE;
}

static void on_flowbox_child_activated(GtkFlowBox *box, GtkFlowBoxChild *child, gpointer user_data) {
	(void)box;
	PhotosApp *app = (PhotosApp *)user_data;
	int idx = gtk_flow_box_child_get_index(child);
	if (load_image_at_index(app, idx)) {
		gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "viewer");
		gtk_widget_set_sensitive(app->btn_back_grid, TRUE);
		update_status_bar(app);
	}
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *ev, gpointer user_data) {
	(void)widget;
	PhotosApp *app = (PhotosApp *)user_data;
	const char *visible = gtk_stack_get_visible_child_name(GTK_STACK(app->stack));
	bool in_viewer = (visible && strcmp(visible, "viewer") == 0);

	if (ev->keyval == GDK_KEY_F11 || ev->keyval == GDK_KEY_f || ev->keyval == GDK_KEY_F) {
		toggle_fullscreen(app);
		return TRUE;
	}
	if (ev->keyval == GDK_KEY_Escape) {
		if (app->is_fullscreen) {
			toggle_fullscreen(app);
			return TRUE;
		}
		if (in_viewer) {
			gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "grid");
			gtk_widget_set_sensitive(app->btn_back_grid, FALSE);
			update_status_bar(app);
			return TRUE;
		}
	}

	if (!in_viewer) return FALSE;

	switch (ev->keyval) {
	case GDK_KEY_Left:
		navigate_relative(app, -1);
		return TRUE;
	case GDK_KEY_Right:
		navigate_relative(app, 1);
		return TRUE;
	case GDK_KEY_plus:
	case GDK_KEY_equal:
	case GDK_KEY_KP_Add:
		set_zoom_level(app, app->zoom * 1.25);
		return TRUE;
	case GDK_KEY_minus:
	case GDK_KEY_KP_Subtract:
		set_zoom_level(app, app->zoom / 1.25);
		return TRUE;
	case GDK_KEY_1:
	case GDK_KEY_KP_1:
		app->pan_x = 0.0;
		app->pan_y = 0.0;
		set_zoom_level(app, 1.0);
		return TRUE;
	case GDK_KEY_0:
	case GDK_KEY_KP_0:
		app->fit_mode = true;
		compute_fit_zoom(app);
		update_status_bar(app);
		gtk_widget_queue_draw(app->drawing_area);
		return TRUE;
	case GDK_KEY_r:
		rotate_current_image(app, (ev->state & GDK_SHIFT_MASK)
			? GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE : GDK_PIXBUF_ROTATE_CLOCKWISE);
		return TRUE;
	case GDK_KEY_R:
		rotate_current_image(app, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
		return TRUE;
	case GDK_KEY_h:
	case GDK_KEY_H:
		flip_current_image(app, TRUE);
		return TRUE;
	case GDK_KEY_v:
	case GDK_KEY_V:
		flip_current_image(app, FALSE);
		return TRUE;
	case GDK_KEY_i:
	case GDK_KEY_I:
		app->show_info = !app->show_info;
		gtk_widget_queue_draw(app->drawing_area);
		return TRUE;
	case GDK_KEY_Delete:
		action_delete_to_trash(app);
		return TRUE;
	default:
		break;
	}
	return FALSE;
}

/* Button Callbacks */
static void on_btn_open_file_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	PhotosApp *app = (PhotosApp *)user_data;
	GtkWidget *dlg = gtk_file_chooser_dialog_new(
		"Open Image", GTK_WINDOW(app->window),
		GTK_FILE_CHOOSER_ACTION_OPEN,
		"_Cancel", GTK_RESPONSE_CANCEL,
		"_Open", GTK_RESPONSE_ACCEPT, NULL);

	GtkFileFilter *filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "Supported Images");
	gtk_file_filter_add_pixbuf_formats(filter);
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), filter);

	if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
		char *fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
		if (fn) {
			open_path(app, fn);
			g_free(fn);
		}
	}
	gtk_widget_destroy(dlg);
}

static void on_btn_open_folder_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	PhotosApp *app = (PhotosApp *)user_data;
	GtkWidget *dlg = gtk_file_chooser_dialog_new(
		"Open Folder", GTK_WINDOW(app->window),
		GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
		"_Cancel", GTK_RESPONSE_CANCEL,
		"_Select", GTK_RESPONSE_ACCEPT, NULL);

	if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
		char *fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
		if (fn) {
			open_path(app, fn);
			g_free(fn);
		}
	}
	gtk_widget_destroy(dlg);
}

static void on_btn_grid_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	PhotosApp *app = (PhotosApp *)user_data;
	gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "grid");
	gtk_widget_set_sensitive(app->btn_back_grid, FALSE);
	update_status_bar(app);
}

static void on_btn_prev_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	navigate_relative((PhotosApp *)user_data, -1);
}

static void on_btn_next_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	navigate_relative((PhotosApp *)user_data, 1);
}

static void on_btn_zoom_in_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	PhotosApp *app = (PhotosApp *)user_data;
	set_zoom_level(app, app->zoom * 1.25);
}

static void on_btn_zoom_out_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	PhotosApp *app = (PhotosApp *)user_data;
	set_zoom_level(app, app->zoom / 1.25);
}

static void on_btn_zoom_fit_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	PhotosApp *app = (PhotosApp *)user_data;
	app->fit_mode = true;
	compute_fit_zoom(app);
	update_status_bar(app);
	gtk_widget_queue_draw(app->drawing_area);
}

static void on_btn_zoom_100_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	PhotosApp *app = (PhotosApp *)user_data;
	app->pan_x = 0.0;
	app->pan_y = 0.0;
	set_zoom_level(app, 1.0);
}

static void on_btn_rot_left_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	rotate_current_image((PhotosApp *)user_data, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
}

static void on_btn_rot_right_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	rotate_current_image((PhotosApp *)user_data, GDK_PIXBUF_ROTATE_CLOCKWISE);
}

static void on_btn_flip_h_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	flip_current_image((PhotosApp *)user_data, TRUE);
}

static void on_btn_info_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	PhotosApp *app = (PhotosApp *)user_data;
	app->show_info = !app->show_info;
	gtk_widget_queue_draw(app->drawing_area);
}

static void on_btn_wallpaper_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	action_set_wallpaper((PhotosApp *)user_data);
}

static void on_btn_trash_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	action_delete_to_trash((PhotosApp *)user_data);
}

static void on_app_destroy(GtkWidget *w, gpointer user_data) {
	(void)w;
	PhotosApp *app = (PhotosApp *)user_data;
	clear_current_image(app);
	clear_entries(app);
	if (app->supported_exts) {
		g_ptr_array_free(app->supported_exts, TRUE);
		app->supported_exts = NULL;
	}
}

int main(int argc, char **argv) {
	g_set_prgname("diamonds-photos");
	gtk_init(&argc, &argv);
	diamonds_gtk_apply_theme(PHOTOS_EXTRA_CSS);

	PhotosApp app = {0};
	app.current_index = -1;
	app.fit_mode = true;
	app.zoom = 1.0;
	init_supported_formats(&app);

	app.window = diamonds_gtk_window_new("Diamonds Photos", 960, 640);
	g_signal_connect(app.window, "destroy", G_CALLBACK(on_app_destroy), &app);
	g_signal_connect(app.window, "key-press-event", G_CALLBACK(on_key_press), &app);

	/* HeaderBar */
	app.header_bar = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(app.header_bar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(app.header_bar), "Diamonds Photos");
	gtk_window_set_titlebar(GTK_WINDOW(app.window), app.header_bar);

	app.btn_back_grid = gtk_button_new_with_label("⊞ Grid");
	gtk_widget_set_tooltip_text(app.btn_back_grid, "Return to Folder Grid (Esc)");
	g_signal_connect(app.btn_back_grid, "clicked", G_CALLBACK(on_btn_grid_clicked), &app);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app.header_bar), app.btn_back_grid);

	GtkWidget *btn_open = gtk_button_new_with_label("Open Image");
	g_signal_connect(btn_open, "clicked", G_CALLBACK(on_btn_open_file_clicked), &app);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app.header_bar), btn_open);

	GtkWidget *btn_folder = gtk_button_new_with_label("Open Folder");
	g_signal_connect(btn_folder, "clicked", G_CALLBACK(on_btn_open_folder_clicked), &app);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app.header_bar), btn_folder);

	GtkWidget *btn_wall = gtk_button_new_with_label("Set Wallpaper");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_wall), "action-btn");
	g_signal_connect(btn_wall, "clicked", G_CALLBACK(on_btn_wallpaper_clicked), &app);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(app.header_bar), btn_wall);

	GtkWidget *btn_trash = gtk_button_new_with_label("Trash");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_trash), "destructive-action");
	g_signal_connect(btn_trash, "clicked", G_CALLBACK(on_btn_trash_clicked), &app);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(app.header_bar), btn_trash);

	GtkWidget *btn_info = gtk_button_new_with_label("Info (I)");
	g_signal_connect(btn_info, "clicked", G_CALLBACK(on_btn_info_clicked), &app);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(app.header_bar), btn_info);

	/* Main vertical layout */
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(app.window), vbox);

	app.stack = gtk_stack_new();
	gtk_stack_set_transition_type(GTK_STACK(app.stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
	gtk_box_pack_start(GTK_BOX(vbox), app.stack, TRUE, TRUE, 0);

	/* Page 1: Folder Grid View */
	GtkWidget *grid_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(grid_scroll),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	app.flow_box = gtk_flow_box_new();
	gtk_widget_set_valign(app.flow_box, GTK_ALIGN_START);
	gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(app.flow_box), 12);
	gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(app.flow_box), GTK_SELECTION_SINGLE);
	gtk_flow_box_set_activate_on_single_click(GTK_FLOW_BOX(app.flow_box), TRUE);
	g_signal_connect(app.flow_box, "child-activated", G_CALLBACK(on_flowbox_child_activated), &app);
	gtk_container_add(GTK_CONTAINER(grid_scroll), app.flow_box);
	gtk_stack_add_named(GTK_STACK(app.stack), grid_scroll, "grid");

	/* Page 2: Single Image Viewer (Cairo DrawingArea + bottom controls) */
	GtkWidget *viewer_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	app.drawing_area = gtk_drawing_area_new();
	gtk_widget_set_hexpand(app.drawing_area, TRUE);
	gtk_widget_set_vexpand(app.drawing_area, TRUE);
	gtk_widget_add_events(app.drawing_area,
		GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK |
		GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
	g_signal_connect(app.drawing_area, "draw", G_CALLBACK(on_drawing_area_draw), &app);
	g_signal_connect(app.drawing_area, "scroll-event", G_CALLBACK(on_scroll_event), &app);
	g_signal_connect(app.drawing_area, "button-press-event", G_CALLBACK(on_button_press), &app);
	g_signal_connect(app.drawing_area, "button-release-event", G_CALLBACK(on_button_release), &app);
	g_signal_connect(app.drawing_area, "motion-notify-event", G_CALLBACK(on_motion_notify), &app);
	gtk_box_pack_start(GTK_BOX(viewer_vbox), app.drawing_area, TRUE, TRUE, 0);

	/* Viewer bottom toolbar */
	GtkWidget *ctrl_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_style_context_add_class(gtk_widget_get_style_context(ctrl_bar), "toolbar-bar");
	gtk_widget_set_halign(ctrl_bar, GTK_ALIGN_FILL);

	GtkWidget *btn_prev = gtk_button_new_with_label("◀ Prev");
	g_signal_connect(btn_prev, "clicked", G_CALLBACK(on_btn_prev_clicked), &app);
	gtk_box_pack_start(GTK_BOX(ctrl_bar), btn_prev, FALSE, FALSE, 0);

	GtkWidget *btn_next = gtk_button_new_with_label("Next ▶");
	g_signal_connect(btn_next, "clicked", G_CALLBACK(on_btn_next_clicked), &app);
	gtk_box_pack_start(GTK_BOX(ctrl_bar), btn_next, FALSE, FALSE, 0);

	GtkWidget *btn_zout = gtk_button_new_with_label("Zoom -");
	g_signal_connect(btn_zout, "clicked", G_CALLBACK(on_btn_zoom_out_clicked), &app);
	gtk_box_pack_start(GTK_BOX(ctrl_bar), btn_zout, FALSE, FALSE, 0);

	GtkWidget *btn_zin = gtk_button_new_with_label("Zoom +");
	g_signal_connect(btn_zin, "clicked", G_CALLBACK(on_btn_zoom_in_clicked), &app);
	gtk_box_pack_start(GTK_BOX(ctrl_bar), btn_zin, FALSE, FALSE, 0);

	GtkWidget *btn_fit = gtk_button_new_with_label("Fit (0)");
	g_signal_connect(btn_fit, "clicked", G_CALLBACK(on_btn_zoom_fit_clicked), &app);
	gtk_box_pack_start(GTK_BOX(ctrl_bar), btn_fit, FALSE, FALSE, 0);

	GtkWidget *btn_100 = gtk_button_new_with_label("1:1 (1)");
	g_signal_connect(btn_100, "clicked", G_CALLBACK(on_btn_zoom_100_clicked), &app);
	gtk_box_pack_start(GTK_BOX(ctrl_bar), btn_100, FALSE, FALSE, 0);

	GtkWidget *btn_rot_l = gtk_button_new_with_label("⟲ Rotate L");
	g_signal_connect(btn_rot_l, "clicked", G_CALLBACK(on_btn_rot_left_clicked), &app);
	gtk_box_pack_start(GTK_BOX(ctrl_bar), btn_rot_l, FALSE, FALSE, 0);

	GtkWidget *btn_rot_r = gtk_button_new_with_label("⟳ Rotate R");
	g_signal_connect(btn_rot_r, "clicked", G_CALLBACK(on_btn_rot_right_clicked), &app);
	gtk_box_pack_start(GTK_BOX(ctrl_bar), btn_rot_r, FALSE, FALSE, 0);

	GtkWidget *btn_flip = gtk_button_new_with_label("⇄ Flip");
	g_signal_connect(btn_flip, "clicked", G_CALLBACK(on_btn_flip_h_clicked), &app);
	gtk_box_pack_start(GTK_BOX(ctrl_bar), btn_flip, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(viewer_vbox), ctrl_bar, FALSE, FALSE, 0);
	gtk_stack_add_named(GTK_STACK(app.stack), viewer_vbox, "viewer");

	/* Status Bar */
	GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_style_context_add_class(gtk_widget_get_style_context(status_box), "statusbar");
	app.lbl_status = gtk_label_new("Ready");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_status), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(app.lbl_status), PANGO_ELLIPSIZE_END);
	gtk_widget_set_margin_start(app.lbl_status, 10);
	gtk_widget_set_margin_top(app.lbl_status, 4);
	gtk_widget_set_margin_bottom(app.lbl_status, 4);
	gtk_box_pack_start(GTK_BOX(status_box), app.lbl_status, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), status_box, FALSE, FALSE, 0);

	gtk_widget_show_all(app.window);

	/* Initial target: argv[1] or ~/Pictures (fallback $HOME) */
	if (argc > 1 && argv[1] && *argv[1]) {
		open_path(&app, argv[1]);
	} else {
		const char *home = getenv("HOME");
		char pics[1024];
		if (home) {
			snprintf(pics, sizeof(pics), "%s/Pictures", home);
			struct stat st;
			if (stat(pics, &st) == 0 && S_ISDIR(st.st_mode)) {
				open_path(&app, pics);
			} else {
				open_path(&app, home);
			}
		}
	}

	gtk_main();
	return 0;
}
