#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <time.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

enum {
	COL_PIXBUF,
	COL_NAME,
	COL_SIZE,
	COL_TYPE,
	COL_MODIFIED,
	COL_IS_DIR,
	COL_FULL_PATH,
	NUM_COLS
};

typedef struct {
	GtkWidget *window;
	GtkWidget *header_bar;
	GtkWidget *path_entry;
	GtkWidget *search_entry;
	GtkWidget *tree_view;
	GtkListStore *store;
	GtkWidget *sidebar_list;
	GtkWidget *status_label;
	GtkWidget *btn_back;
	GtkWidget *btn_forward;
	GtkWidget *btn_up;
	GtkWidget *btn_hidden;

	char current_dir[1024];
	char search_filter[256];
	bool show_hidden;

	/* Clipboard for Copy/Cut/Paste */
	char clipboard_path[1024];
	bool clipboard_is_cut;

	/* History for Back/Forward navigation */
	char history[64][1024];
	int history_count;
	int history_index;
} FilesApp;

static const char *DARK_MICA_CSS =
	"window, dialog, viewport, scrolledwindow {"
	"    background: #14141B;"
	"    background-image: none;"
	"    color: #E2E8F0;"
	"    font-family: \"Segoe UI\", Cantarell, Ubuntu, sans-serif;"
	"}"
	"headerbar {"
	"    background: #181822;"
	"    background-image: none;"
	"    border-bottom: 1px solid #2B2D3C;"
	"    color: #F8FAFC;"
	"    box-shadow: none;"
	"}"
	"headerbar .title { color: #F8FAFC; font-weight: bold; }"
	"entry {"
	"    background: #1E202E;"
	"    background-image: none;"
	"    color: #F8FAFC;"
	"    border: 1px solid #33364A;"
	"    border-radius: 6px;"
	"    padding: 5px 10px;"
	"    box-shadow: none;"
	"}"
	"entry:focus {"
	"    border-color: #38BDF8;"
	"}"
	"button {"
	"    background: #1E202E;"
	"    background-image: none;"
	"    color: #E2E8F0;"
	"    border: 1px solid #33364A;"
	"    border-radius: 6px;"
	"    padding: 4px 8px;"
	"    box-shadow: none;"
	"}"
	"button label { color: inherit; }"
	"button:hover {"
	"    background: #282B3E;"
	"    background-image: none;"
	"    color: #FFFFFF;"
	"}"
	"button:checked {"
	"    background-color: #0284C7;"
	"    color: #FFFFFF;"
	"}"
	".sidebar {"
	"    background-color: #161722;"
	"    border-right: 1px solid #2B2D3C;"
	"}"
	".sidebar row {"
	"    padding: 8px 12px;"
	"    border-radius: 6px;"
	"    color: #CBD5E1;"
	"}"
	".sidebar row:hover {"
	"    background-color: rgba(255, 255, 255, 0.08);"
	"}"
	".sidebar row:selected {"
	"    background-color: #0284C7;"
	"    color: #FFFFFF;"
	"}"
	"treeview {"
	"    background-color: #14141B;"
	"    color: #F1F5F9;"
	"}"
	"treeview:selected {"
	"    background-color: #0369A1;"
	"    color: #FFFFFF;"
	"}"
	"treeview header button {"
	"    background-color: #181822;"
	"    color: #94A3B8;"
	"    border: none;"
	"    border-bottom: 1px solid #2B2D3C;"
	"    font-weight: bold;"
	"}"
	"menu {"
	"    background-color: #181822;"
	"    color: #E2E8F0;"
	"    border: 1px solid #2B2D3C;"
	"}"
	"menuitem:hover {"
	"    background-color: #0284C7;"
	"    color: #FFFFFF;"
	"}"
	"statusbar {"
	"    background-color: #181822;"
	"    border-top: 1px solid #2B2D3C;"
	"    color: #94A3B8;"
	"    padding: 4px 10px;"
	"}";

static void format_size(off_t size, char *buf, size_t buf_size) {
	if (size < 1024) {
		snprintf(buf, buf_size, "%ld B", (long)size);
	} else if (size < 1024 * 1024) {
		snprintf(buf, buf_size, "%.1f KB", (double)size / 1024.0);
	} else if (size < 1024 * 1024 * 1024) {
		snprintf(buf, buf_size, "%.1f MB", (double)size / (1024.0 * 1024.0));
	} else {
		snprintf(buf, buf_size, "%.2f GB", (double)size / (1024.0 * 1024.0 * 1024.0));
	}
}

static void update_nav_buttons(FilesApp *app) {
	gtk_widget_set_sensitive(app->btn_back, app->history_index > 0);
	gtk_widget_set_sensitive(app->btn_forward, app->history_index < app->history_count - 1);
	gtk_widget_set_sensitive(app->btn_up, strcmp(app->current_dir, "/") != 0);
}

typedef struct {
	char name[256];
	char full_path[1024];
	bool is_dir;
	off_t size;
	time_t mtime;
} FileItem;

static int compare_items(const void *a, const void *b) {
	const FileItem *item_a = a;
	const FileItem *item_b = b;
	if (item_a->is_dir && !item_b->is_dir) return -1;
	if (!item_a->is_dir && item_b->is_dir) return 1;
	return strcasecmp(item_a->name, item_b->name);
}

static void load_directory(FilesApp *app, const char *path, bool add_to_history) {
	char *rp = realpath(path, NULL);
	char real_dir[1024];
	if (rp) {
		snprintf(real_dir, sizeof(real_dir), "%s", rp);
		free(rp);
	} else {
		snprintf(real_dir, sizeof(real_dir), "%s", path);
	}

	DIR *dir = opendir(real_dir);
	if (!dir) {
		GtkWidget *dlg = gtk_message_dialog_new(
			GTK_WINDOW(app->window),
			GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_ERROR,
			GTK_BUTTONS_OK,
			"Không thể mở thư mục: %s", real_dir);
		gtk_dialog_run(GTK_DIALOG(dlg));
		gtk_widget_destroy(dlg);
		return;
	}

	snprintf(app->current_dir, sizeof(app->current_dir), "%s", real_dir);
	gtk_entry_set_text(GTK_ENTRY(app->path_entry), app->current_dir);
	gtk_header_bar_set_subtitle(GTK_HEADER_BAR(app->header_bar), app->current_dir);

	if (add_to_history) {
		if (app->history_index < app->history_count - 1) {
			app->history_count = app->history_index + 1;
		}
		if (app->history_count < 64) {
			snprintf(app->history[app->history_count], sizeof(app->history[0]), "%s", app->current_dir);
			app->history_index = app->history_count;
			app->history_count++;
		}
	}
	update_nav_buttons(app);

	gtk_list_store_clear(app->store);

	GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
	GdkPixbuf *folder_pixbuf = gtk_icon_theme_load_icon(icon_theme, "folder", 20, GTK_ICON_LOOKUP_USE_BUILTIN, NULL);
	GdkPixbuf *file_pixbuf = gtk_icon_theme_load_icon(icon_theme, "text-x-generic", 20, GTK_ICON_LOOKUP_USE_BUILTIN, NULL);

	struct dirent *ent;
	FileItem *items = NULL;
	size_t count = 0;
	size_t capacity = 64;
	items = malloc(capacity * sizeof(FileItem));
	if (!items) {
		closedir(dir);
		if (folder_pixbuf) g_object_unref(folder_pixbuf);
		if (file_pixbuf) g_object_unref(file_pixbuf);
		return;
	}

	while ((ent = readdir(dir)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
			continue;
		}
		if (!app->show_hidden && ent->d_name[0] == '.') {
			continue;
		}
		if (app->search_filter[0] != '\0' && !strcasestr(ent->d_name, app->search_filter)) {
			continue;
		}

		if (count >= capacity) {
			capacity *= 2;
			FileItem *next = realloc(items, capacity * sizeof(FileItem));
			if (!next) break;
			items = next;
		}

		FileItem *item = &items[count++];
		snprintf(item->name, sizeof(item->name), "%s", ent->d_name);
		snprintf(item->full_path, sizeof(item->full_path), "%.700s/%.255s", app->current_dir, ent->d_name);

		struct stat st;
		if (stat(item->full_path, &st) == 0) {
			item->is_dir = S_ISDIR(st.st_mode);
			item->size = st.st_size;
			item->mtime = st.st_mtime;
		} else {
			item->is_dir = (ent->d_type == DT_DIR);
			item->size = 0;
			item->mtime = 0;
		}
	}
	closedir(dir);

	if (count > 1) {
		qsort(items, count, sizeof(FileItem), compare_items);
	}

	int folder_count = 0;
	int file_count = 0;

	for (size_t i = 0; i < count; i++) {
		FileItem *item = &items[i];
		GtkTreeIter iter;
		char size_buf[32] = {0};
		const char *type_str = item->is_dir ? "Folder" : "File";

		if (item->is_dir) {
			folder_count++;
			snprintf(size_buf, sizeof(size_buf), "--");
		} else {
			file_count++;
			format_size(item->size, size_buf, sizeof(size_buf));
			const char *dot = strrchr(item->name, '.');
			if (dot) {
				if (strcasecmp(dot, ".c") == 0 || strcasecmp(dot, ".h") == 0) type_str = "C Source";
				else if (strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0 || strcasecmp(dot, ".webp") == 0) type_str = "Image";
				else if (strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".wav") == 0 || strcasecmp(dot, ".flac") == 0 || strcasecmp(dot, ".ogg") == 0) type_str = "Audio";
				else if (strcasecmp(dot, ".mp4") == 0 || strcasecmp(dot, ".mkv") == 0) type_str = "Video";
				else if (strcasecmp(dot, ".pdf") == 0) type_str = "PDF Document";
				else if (strcasecmp(dot, ".txt") == 0 || strcasecmp(dot, ".md") == 0) type_str = "Text Document";
				else if (strcasecmp(dot, ".tar") == 0 || strcasecmp(dot, ".gz") == 0 || strcasecmp(dot, ".zip") == 0) type_str = "Archive";
				else type_str = dot + 1;
			}
		}

		char time_buf[64] = {0};
		if (item->mtime > 0) {
			struct tm *tm_info = localtime(&item->mtime);
			if (tm_info) {
				strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", tm_info);
			}
		}

		GdkPixbuf *pix = item->is_dir ? folder_pixbuf : file_pixbuf;

		gtk_list_store_append(app->store, &iter);
		gtk_list_store_set(app->store, &iter,
			COL_PIXBUF, pix,
			COL_NAME, item->name,
			COL_SIZE, size_buf,
			COL_TYPE, type_str,
			COL_MODIFIED, time_buf,
			COL_IS_DIR, item->is_dir,
			COL_FULL_PATH, item->full_path,
			-1);
	}

	free(items);

	if (folder_pixbuf) g_object_unref(folder_pixbuf);
	if (file_pixbuf) g_object_unref(file_pixbuf);

	char status[128];
	snprintf(status, sizeof(status), "%zu mục (%d thư mục, %d tập tin)", count, folder_count, file_count);
	gtk_label_set_text(GTK_LABEL(app->status_label), status);
}

static bool get_selected_item(FilesApp *app, char *out_path, size_t path_len, char *out_name, size_t name_len, bool *out_is_dir) {
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->tree_view));
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return false;

	char *fp = NULL;
	char *nm = NULL;
	gboolean is_d = FALSE;
	gtk_tree_model_get(model, &iter, COL_FULL_PATH, &fp, COL_NAME, &nm, COL_IS_DIR, &is_d, -1);
	if (fp && out_path) snprintf(out_path, path_len, "%s", fp);
	if (nm && out_name) snprintf(out_name, name_len, "%s", nm);
	if (out_is_dir) *out_is_dir = is_d;
	g_free(fp);
	g_free(nm);
	return true;
}

static void open_file_smart(const char *full_path) {
	const char *dot = strrchr(full_path, '.');
	char cmd[1400];
	if (dot && (strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".jpg") == 0 ||
	            strcasecmp(dot, ".jpeg") == 0 || strcasecmp(dot, ".gif") == 0 ||
	            strcasecmp(dot, ".webp") == 0 || strcasecmp(dot, ".bmp") == 0)) {
		snprintf(cmd, sizeof(cmd), "(./build/diamonds-photos \"%s\" || diamonds-photos \"%s\" || xdg-open \"%s\") &",
			full_path, full_path, full_path);
	} else if (dot && (strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".flac") == 0 ||
	                   strcasecmp(dot, ".ogg") == 0 || strcasecmp(dot, ".wav") == 0)) {
		snprintf(cmd, sizeof(cmd), "(./build/diamonds-music \"%s\" || diamonds-music \"%s\" || xdg-open \"%s\") &",
			full_path, full_path, full_path);
	} else if (dot && (strcasecmp(dot, ".txt") == 0 || strcasecmp(dot, ".md") == 0 ||
	                   strcasecmp(dot, ".c") == 0 || strcasecmp(dot, ".h") == 0 ||
	                   strcasecmp(dot, ".conf") == 0 || strcasecmp(dot, ".ini") == 0 ||
	                   strcasecmp(dot, ".json") == 0 || strcasecmp(dot, ".sh") == 0)) {
		snprintf(cmd, sizeof(cmd), "(./build/diamonds-notes \"%s\" || diamonds-notes \"%s\" || xdg-open \"%s\") &",
			full_path, full_path, full_path);
	} else {
		snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" &", full_path);
	}
	int res = system(cmd);
	(void)res;
}

static void on_row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data) {
	(void)tree_view;
	(void)column;
	FilesApp *app = user_data;
	GtkTreeIter iter;

	if (gtk_tree_model_get_iter(GTK_TREE_MODEL(app->store), &iter, path)) {
		gboolean is_dir;
		char *full_path;
		gtk_tree_model_get(GTK_TREE_MODEL(app->store), &iter,
			COL_IS_DIR, &is_dir,
			COL_FULL_PATH, &full_path,
			-1);

		if (is_dir) {
			load_directory(app, full_path, true);
		} else {
			open_file_smart(full_path);
		}
		g_free(full_path);
	}
}

static void on_new_folder_clicked(GtkWidget *w, gpointer user_data) {
	(void)w;
	FilesApp *app = user_data;
	GtkWidget *dlg = gtk_dialog_new_with_buttons("Tạo thư mục mới",
		GTK_WINDOW(app->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		"_Hủy", GTK_RESPONSE_CANCEL, "_Tạo", GTK_RESPONSE_ACCEPT, NULL);
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
	GtkWidget *entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), "New Folder");
	gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 8);
	gtk_widget_show_all(dlg);
	if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
		const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
		if (name && *name && !strchr(name, '/')) {
			char new_dir[1280];
			snprintf(new_dir, sizeof(new_dir), "%s/%s", app->current_dir, name);
			mkdir(new_dir, 0755);
			load_directory(app, app->current_dir, false);
		}
	}
	gtk_widget_destroy(dlg);
}

static void action_rename_selected(FilesApp *app) {
	char full_path[1024], old_name[256];
	if (!get_selected_item(app, full_path, sizeof(full_path), old_name, sizeof(old_name), NULL)) return;

	GtkWidget *dlg = gtk_dialog_new_with_buttons("Đổi tên",
		GTK_WINDOW(app->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		"_Hủy", GTK_RESPONSE_CANCEL, "_Lưu", GTK_RESPONSE_ACCEPT, NULL);
	GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
	GtkWidget *entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), old_name);
	gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 8);
	gtk_widget_show_all(dlg);
	if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
		const char *new_name = gtk_entry_get_text(GTK_ENTRY(entry));
		if (new_name && *new_name && !strchr(new_name, '/')) {
			char new_path[1280];
			snprintf(new_path, sizeof(new_path), "%s/%s", app->current_dir, new_name);
			rename(full_path, new_path);
			load_directory(app, app->current_dir, false);
		}
	}
	gtk_widget_destroy(dlg);
}

static void action_trash_selected(FilesApp *app) {
	char full_path[1024], name[256];
	if (!get_selected_item(app, full_path, sizeof(full_path), name, sizeof(name), NULL)) return;

	GFile *gf = g_file_new_for_path(full_path);
	GError *err = NULL;
	if (!g_file_trash(gf, NULL, &err)) {
		if (err) g_error_free(err);
		remove(full_path);
	}
	g_object_unref(gf);
	load_directory(app, app->current_dir, false);
}

static void action_copy_selected(FilesApp *app, bool is_cut) {
	if (get_selected_item(app, app->clipboard_path, sizeof(app->clipboard_path), NULL, 0, NULL)) {
		app->clipboard_is_cut = is_cut;
		char msg[1200];
		snprintf(msg, sizeof(msg), "%s: %s", is_cut ? "Đã cắt" : "Đã sao chép", app->clipboard_path);
		gtk_label_set_text(GTK_LABEL(app->status_label), msg);
	}
}

static void action_paste(FilesApp *app) {
	if (!app->clipboard_path[0]) return;
	const char *base = strrchr(app->clipboard_path, '/');
	base = base ? base + 1 : app->clipboard_path;
	char dst[2050];
	snprintf(dst, sizeof(dst), "%s/%s", app->current_dir, base);
	if (strcmp(app->clipboard_path, dst) == 0) return;

	char cmd[3200];
	if (app->clipboard_is_cut) {
		snprintf(cmd, sizeof(cmd), "mv -f \"%s\" \"%s\"", app->clipboard_path, dst);
		app->clipboard_path[0] = '\0';
	} else {
		snprintf(cmd, sizeof(cmd), "cp -rf \"%s\" \"%s\"", app->clipboard_path, dst);
	}
	int r = system(cmd);
	(void)r;
	load_directory(app, app->current_dir, false);
}

static void on_ctx_open(GtkMenuItem *mi, gpointer ud) {
	(void)mi;
	FilesApp *app = ud;
	char fp[1024];
	bool is_d = false;
	if (get_selected_item(app, fp, sizeof(fp), NULL, 0, &is_d)) {
		if (is_d) load_directory(app, fp, true);
		else open_file_smart(fp);
	}
}
static void on_ctx_copy(GtkMenuItem *mi, gpointer ud) { (void)mi; action_copy_selected(ud, false); }
static void on_ctx_cut(GtkMenuItem *mi, gpointer ud) { (void)mi; action_copy_selected(ud, true); }
static void on_ctx_paste(GtkMenuItem *mi, gpointer ud) { (void)mi; action_paste(ud); }
static void on_ctx_rename(GtkMenuItem *mi, gpointer ud) { (void)mi; action_rename_selected(ud); }
static void on_ctx_trash(GtkMenuItem *mi, gpointer ud) { (void)mi; action_trash_selected(ud); }

static gboolean on_tree_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
	(void)widget;
	FilesApp *app = user_data;
	if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
		GtkWidget *menu = gtk_menu_new();
		GtkWidget *mi_open = gtk_menu_item_new_with_label("Mở (Open)");
		GtkWidget *mi_newf = gtk_menu_item_new_with_label("Thư mục mới (New Folder)");
		GtkWidget *mi_copy = gtk_menu_item_new_with_label("Sao chép (Ctrl+C)");
		GtkWidget *mi_cut  = gtk_menu_item_new_with_label("Cắt (Ctrl+X)");
		GtkWidget *mi_paste= gtk_menu_item_new_with_label("Dán (Ctrl+V)");
		GtkWidget *mi_ren  = gtk_menu_item_new_with_label("Đổi tên (F2)");
		GtkWidget *mi_del  = gtk_menu_item_new_with_label("Chuyển vào thùng rác (Delete)");

		g_signal_connect(mi_open, "activate", G_CALLBACK(on_ctx_open), app);
		g_signal_connect(mi_newf, "activate", G_CALLBACK(on_new_folder_clicked), app);
		g_signal_connect(mi_copy, "activate", G_CALLBACK(on_ctx_copy), app);
		g_signal_connect(mi_cut,  "activate", G_CALLBACK(on_ctx_cut), app);
		g_signal_connect(mi_paste,"activate", G_CALLBACK(on_ctx_paste), app);
		g_signal_connect(mi_ren,  "activate", G_CALLBACK(on_ctx_rename), app);
		g_signal_connect(mi_del,  "activate", G_CALLBACK(on_ctx_trash), app);

		gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_open);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_newf);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_copy);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_cut);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_paste);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_ren);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_del);

		gtk_widget_show_all(menu);
		gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
		return FALSE;
	}
	return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	(void)widget;
	FilesApp *app = user_data;
	gboolean ctrl = (event->state & GDK_CONTROL_MASK) != 0;
	if (ctrl && (event->keyval == GDK_KEY_h || event->keyval == GDK_KEY_H)) {
		app->show_hidden = !app->show_hidden;
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->btn_hidden), app->show_hidden);
		return TRUE;
	}
	if (gtk_window_get_focus(GTK_WINDOW(app->window)) == app->path_entry ||
	    gtk_window_get_focus(GTK_WINDOW(app->window)) == app->search_entry) {
		return FALSE;
	}
	if (ctrl && (event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_C)) {
		action_copy_selected(app, false);
		return TRUE;
	}
	if (ctrl && (event->keyval == GDK_KEY_x || event->keyval == GDK_KEY_X)) {
		action_copy_selected(app, true);
		return TRUE;
	}
	if (ctrl && (event->keyval == GDK_KEY_v || event->keyval == GDK_KEY_V)) {
		action_paste(app);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_F2) {
		action_rename_selected(app);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_Delete) {
		action_trash_selected(app);
		return TRUE;
	}
	return FALSE;
}

static void on_sidebar_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
	(void)box;
	if (!row) return;
	FilesApp *app = user_data;
	const char *target = g_object_get_data(G_OBJECT(row), "path");
	if (target) {
		load_directory(app, target, true);
	}
}

static void on_path_entry_activate(GtkEntry *entry, gpointer user_data) {
	FilesApp *app = user_data;
	const char *text = gtk_entry_get_text(entry);
	if (text && *text) {
		load_directory(app, text, true);
	}
}

static void on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
	FilesApp *app = user_data;
	const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
	snprintf(app->search_filter, sizeof(app->search_filter), "%s", text ? text : "");
	load_directory(app, app->current_dir, false);
}

static void on_hidden_toggled(GtkToggleButton *btn, gpointer user_data) {
	FilesApp *app = user_data;
	app->show_hidden = gtk_toggle_button_get_active(btn);
	load_directory(app, app->current_dir, false);
}

static void on_btn_back_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	FilesApp *app = user_data;
	if (app->history_index > 0) {
		app->history_index--;
		load_directory(app, app->history[app->history_index], false);
		update_nav_buttons(app);
	}
}

static void on_btn_forward_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	FilesApp *app = user_data;
	if (app->history_index < app->history_count - 1) {
		app->history_index++;
		load_directory(app, app->history[app->history_index], false);
		update_nav_buttons(app);
	}
}

static void on_btn_up_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	FilesApp *app = user_data;
	char *slash = strrchr(app->current_dir, '/');
	if (slash && slash != app->current_dir) {
		*slash = '\0';
		load_directory(app, app->current_dir, true);
	} else if (slash == app->current_dir && strlen(app->current_dir) > 1) {
		load_directory(app, "/", true);
	}
}

static void on_btn_refresh_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	FilesApp *app = user_data;
	load_directory(app, app->current_dir, false);
}

static void add_sidebar_item(GtkListBox *list, const char *label, const char *icon_name, const char *path) {
	GtkWidget *row = gtk_list_box_row_new();
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_container_set_border_width(GTK_CONTAINER(box), 4);

	GtkWidget *icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
	GtkWidget *lbl = gtk_label_new(label);
	gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);

	gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), lbl, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(row), box);

	g_object_set_data_full(G_OBJECT(row), "path", g_strdup(path), g_free);
	gtk_list_box_insert(list, row, -1);
}

int main(int argc, char **argv) {
	g_set_prgname("diamonds-files");
	gtk_init(&argc, &argv);

	GtkSettings *gset = gtk_settings_get_default();
	if (gset) {
		g_object_set(gset, "gtk-application-prefer-dark-theme", TRUE, NULL);
	}

	GtkCssProvider *css_provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(css_provider, DARK_MICA_CSS, -1, NULL);
	gtk_style_context_add_provider_for_screen(
		gdk_screen_get_default(),
		GTK_STYLE_PROVIDER(css_provider),
		GTK_STYLE_PROVIDER_PRIORITY_USER + 10);

	FilesApp app = {0};

	app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(app.window), "Diamonds Files");
	gtk_window_set_default_size(GTK_WINDOW(app.window), 880, 540);
	g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	g_signal_connect(app.window, "key-press-event", G_CALLBACK(on_key_press), &app);

	/* HeaderBar */
	app.header_bar = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(app.header_bar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(app.header_bar), "Files");
	gtk_window_set_titlebar(GTK_WINDOW(app.window), app.header_bar);

	/* Nav buttons in HeaderBar */
	GtkWidget *nav_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

	app.btn_back = gtk_button_new_from_icon_name("go-previous", GTK_ICON_SIZE_BUTTON);
	g_signal_connect(app.btn_back, "clicked", G_CALLBACK(on_btn_back_clicked), &app);
	gtk_box_pack_start(GTK_BOX(nav_box), app.btn_back, FALSE, FALSE, 0);

	app.btn_forward = gtk_button_new_from_icon_name("go-next", GTK_ICON_SIZE_BUTTON);
	g_signal_connect(app.btn_forward, "clicked", G_CALLBACK(on_btn_forward_clicked), &app);
	gtk_box_pack_start(GTK_BOX(nav_box), app.btn_forward, FALSE, FALSE, 0);

	app.btn_up = gtk_button_new_from_icon_name("go-up", GTK_ICON_SIZE_BUTTON);
	g_signal_connect(app.btn_up, "clicked", G_CALLBACK(on_btn_up_clicked), &app);
	gtk_box_pack_start(GTK_BOX(nav_box), app.btn_up, FALSE, FALSE, 0);

	GtkWidget *btn_refresh = gtk_button_new_from_icon_name("view-refresh", GTK_ICON_SIZE_BUTTON);
	g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_btn_refresh_clicked), &app);
	gtk_box_pack_start(GTK_BOX(nav_box), btn_refresh, FALSE, FALSE, 0);

	GtkWidget *btn_new_folder = gtk_button_new_from_icon_name("folder-new", GTK_ICON_SIZE_BUTTON);
	gtk_widget_set_tooltip_text(btn_new_folder, "Tạo thư mục mới");
	g_signal_connect(btn_new_folder, "clicked", G_CALLBACK(on_new_folder_clicked), &app);
	gtk_box_pack_start(GTK_BOX(nav_box), btn_new_folder, FALSE, FALSE, 0);

	app.btn_hidden = gtk_toggle_button_new_with_label(".*");
	gtk_widget_set_tooltip_text(app.btn_hidden, "Hiện tệp ẩn (Ctrl+H)");
	g_signal_connect(app.btn_hidden, "toggled", G_CALLBACK(on_hidden_toggled), &app);
	gtk_box_pack_start(GTK_BOX(nav_box), app.btn_hidden, FALSE, FALSE, 0);

	gtk_header_bar_pack_start(GTK_HEADER_BAR(app.header_bar), nav_box);

	/* Path Entry */
	app.path_entry = gtk_entry_new();
	gtk_widget_set_size_request(app.path_entry, 280, -1);
	g_signal_connect(app.path_entry, "activate", G_CALLBACK(on_path_entry_activate), &app);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app.header_bar), app.path_entry);

	/* Search Entry */
	app.search_entry = gtk_search_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(app.search_entry), "Lọc tệp...");
	gtk_widget_set_size_request(app.search_entry, 160, -1);
	g_signal_connect(app.search_entry, "search-changed", G_CALLBACK(on_search_changed), &app);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(app.header_bar), app.search_entry);

	/* Main Layout */
	GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(app.window), main_vbox);

	GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(main_vbox), paned, TRUE, TRUE, 0);
	gtk_paned_set_position(GTK_PANED(paned), 180);

	/* Sidebar */
	GtkWidget *sidebar_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_style_context_add_class(gtk_widget_get_style_context(sidebar_scroll), "sidebar");

	app.sidebar_list = gtk_list_box_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(app.sidebar_list), "sidebar");
	g_signal_connect(app.sidebar_list, "row-selected", G_CALLBACK(on_sidebar_row_selected), &app);
	gtk_container_add(GTK_CONTAINER(sidebar_scroll), app.sidebar_list);
	gtk_paned_pack1(GTK_PANED(paned), sidebar_scroll, FALSE, FALSE);

	/* Add Shortcuts */
	const char *home_dir = getenv("HOME");
	if (!home_dir) {
		struct passwd *pw = getpwuid(getuid());
		home_dir = pw ? pw->pw_dir : "/";
	}

	char desktop_dir[1024], docs_dir[1024], down_dir[1024], pic_dir[1024], music_dir[1024], trash_dir[1024];
	snprintf(desktop_dir, sizeof(desktop_dir), "%s/Desktop", home_dir);
	snprintf(docs_dir, sizeof(docs_dir), "%s/Documents", home_dir);
	snprintf(down_dir, sizeof(down_dir), "%s/Downloads", home_dir);
	snprintf(pic_dir, sizeof(pic_dir), "%s/Pictures", home_dir);
	snprintf(music_dir, sizeof(music_dir), "%s/Music", home_dir);
	snprintf(trash_dir, sizeof(trash_dir), "%s/.local/share/Trash/files", home_dir);

	add_sidebar_item(GTK_LIST_BOX(app.sidebar_list), "Home", "user-home", home_dir);
	add_sidebar_item(GTK_LIST_BOX(app.sidebar_list), "Desktop", "user-desktop", desktop_dir);
	add_sidebar_item(GTK_LIST_BOX(app.sidebar_list), "Documents", "folder-documents", docs_dir);
	add_sidebar_item(GTK_LIST_BOX(app.sidebar_list), "Downloads", "folder-download", down_dir);
	add_sidebar_item(GTK_LIST_BOX(app.sidebar_list), "Pictures", "folder-pictures", pic_dir);
	add_sidebar_item(GTK_LIST_BOX(app.sidebar_list), "Music", "folder-music", music_dir);
	add_sidebar_item(GTK_LIST_BOX(app.sidebar_list), "Root (/)", "drive-harddisk", "/");
	add_sidebar_item(GTK_LIST_BOX(app.sidebar_list), "Trash", "user-trash", trash_dir);

	/* Right: TreeView in ScrolledWindow */
	GtkWidget *tree_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_paned_pack2(GTK_PANED(paned), tree_scroll, TRUE, TRUE);

	app.store = gtk_list_store_new(NUM_COLS,
		GDK_TYPE_PIXBUF, /* Icon */
		G_TYPE_STRING,   /* Name */
		G_TYPE_STRING,   /* Size */
		G_TYPE_STRING,   /* Type */
		G_TYPE_STRING,   /* Modified */
		G_TYPE_BOOLEAN,  /* Is_Dir */
		G_TYPE_STRING);  /* Full Path */

	app.tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app.store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(app.tree_view), TRUE);
	g_signal_connect(app.tree_view, "row-activated", G_CALLBACK(on_row_activated), &app);
	g_signal_connect(app.tree_view, "button-press-event", G_CALLBACK(on_tree_button_press), &app);
	gtk_container_add(GTK_CONTAINER(tree_scroll), app.tree_view);

	/* Column 1: Icon + Name */
	GtkCellRenderer *pix_renderer = gtk_cell_renderer_pixbuf_new();
	GtkCellRenderer *text_renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *col_name = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(col_name, "Name");
	gtk_tree_view_column_pack_start(col_name, pix_renderer, FALSE);
	gtk_tree_view_column_pack_start(col_name, text_renderer, TRUE);
	gtk_tree_view_column_add_attribute(col_name, pix_renderer, "pixbuf", COL_PIXBUF);
	gtk_tree_view_column_add_attribute(col_name, text_renderer, "text", COL_NAME);
	gtk_tree_view_column_set_resizable(col_name, TRUE);
	gtk_tree_view_column_set_min_width(col_name, 220);
	gtk_tree_view_column_set_sort_column_id(col_name, COL_NAME);
	gtk_tree_view_append_column(GTK_TREE_VIEW(app.tree_view), col_name);

	/* Column 2: Size */
	GtkCellRenderer *size_renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *col_size = gtk_tree_view_column_new_with_attributes(
		"Size", size_renderer, "text", COL_SIZE, NULL);
	gtk_tree_view_column_set_resizable(col_size, TRUE);
	gtk_tree_view_column_set_min_width(col_size, 90);
	gtk_tree_view_append_column(GTK_TREE_VIEW(app.tree_view), col_size);

	/* Column 3: Type */
	GtkCellRenderer *type_renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *col_type = gtk_tree_view_column_new_with_attributes(
		"Type", type_renderer, "text", COL_TYPE, NULL);
	gtk_tree_view_column_set_resizable(col_type, TRUE);
	gtk_tree_view_column_set_min_width(col_type, 110);
	gtk_tree_view_column_set_sort_column_id(col_type, COL_TYPE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(app.tree_view), col_type);

	/* Column 4: Date Modified */
	GtkCellRenderer *time_renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *col_time = gtk_tree_view_column_new_with_attributes(
		"Date Modified", time_renderer, "text", COL_MODIFIED, NULL);
	gtk_tree_view_column_set_resizable(col_time, TRUE);
	gtk_tree_view_column_set_min_width(col_time, 140);
	gtk_tree_view_column_set_sort_column_id(col_time, COL_MODIFIED);
	gtk_tree_view_append_column(GTK_TREE_VIEW(app.tree_view), col_time);

	/* Status Bar */
	GtkWidget *status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(status_bar), "statusbar");
	app.status_label = gtk_label_new("0 items");
	gtk_box_pack_start(GTK_BOX(status_bar), app.status_label, FALSE, FALSE, 6);
	gtk_box_pack_end(GTK_BOX(main_vbox), status_bar, FALSE, FALSE, 0);

	/* Initial load */
	const char *start_dir = (argc > 1) ? argv[1] : home_dir;
	load_directory(&app, start_dir, true);

	gtk_widget_show_all(app.window);
	gtk_main();

	return 0;
}
