#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

typedef struct {
	const char *name;
	const char *pkg;
	const char *cmd;
	const char *alt_cmd;
	const char *desc;
	const char *category;
	bool is_installed;
	GtkWidget *row_box;
	GtkWidget *status_lbl;
	GtkWidget *action_btn;
} AppPackage;

static AppPackage g_eco_apps[] = {
	{ "Diamonds Terminal", "diamonds-terminal", "./build/diamonds-terminal", "diamonds-terminal",
	  "Trình giả lập thiết bị đầu cuối PTY VT100/xterm-256color siêu nhẹ", "Diamonds Built-in", true, NULL, NULL, NULL },
	{ "Diamonds Files", "diamonds-files", "./build/diamonds-files", "diamonds-files",
	  "Trình quản lý tệp tin Mica với lịch sử, tìm kiếm, sao chép & thùng rác", "Diamonds Built-in", true, NULL, NULL, NULL },
	{ "Diamonds Notes", "diamonds-notes", "./build/diamonds-notes", "diamonds-notes",
	  "Trình soạn thảo văn bản đa tab, tìm kiếm/thay thế, hoàn tác & tự lưu nháp", "Diamonds Built-in", true, NULL, NULL, NULL },
	{ "Diamonds Calculator", "diamonds-calc", "./build/diamonds-calc", "diamonds-calc",
	  "Máy tính chuẩn & khoa học (Standard / Scientific) có lịch sử phép tính", "Diamonds Built-in", true, NULL, NULL, NULL },
	{ "Diamonds Photos", "diamonds-photos", "./build/diamonds-photos", "diamonds-photos",
	  "Trình xem ảnh & thư viện thu nhỏ, phóng to, xoay ảnh, đặt hình nền", "Diamonds Built-in", true, NULL, NULL, NULL },
	{ "Diamonds Music", "diamonds-music", "./build/diamonds-music", "diamonds-music",
	  "Trình phát nhạc & danh sách phát ~/Music, đọc thẻ ID3v2/FLAC", "Diamonds Built-in", true, NULL, NULL, NULL },
	{ "Diamonds Monitor", "diamonds-monitor", "./build/diamonds-monitor", "diamonds-monitor",
	  "Trình quản lý tác vụ & biểu đồ hiệu năng CPU/RAM/Mạng/Ổ đĩa thời gian thực", "Diamonds Built-in", true, NULL, NULL, NULL },
	{ "Diamonds Mail", "diamonds-mail", "./build/diamonds-mail", "diamonds-mail",
	  "Ứng dụng thư điện tử đọc Maildir/mbox cục bộ & soạn thư chuẩn RFC 5322", "Diamonds Built-in", true, NULL, NULL, NULL },
	{ "Diamonds Clock", "diamonds-clock", "./build/diamonds-clock", "diamonds-clock",
	  "Đồng hồ số, bấm giờ thể thao (Stopwatch + Lap) & hẹn giờ đếm ngược", "Diamonds Built-in", true, NULL, NULL, NULL },
	{ "Diamonds Settings", "diamonds-settings", "./build/diamonds-settings", "diamonds-settings",
	  "Trung tâm cài đặt hình nền, độ phân giải, âm thanh, hiệu ứng & thiết bị", "Diamonds Built-in", true, NULL, NULL, NULL }
};
#define NUM_ECO_APPS (int)(sizeof(g_eco_apps) / sizeof(g_eco_apps[0]))

static AppPackage g_apps[12] = {
	{ "VLC Media Player", "vlc", "vlc", NULL,
	  "Trình phát đa phương tiện âm thanh & video phổ biến nhất", "Media", false, NULL, NULL, NULL },
	{ "Chromium Web Browser", "chromium", "chromium", "chromium-browser",
	  "Trình duyệt web mã nguồn mở bảo mật và tốc độ cao", "Internet", false, NULL, NULL, NULL },
	{ "GIMP Image Editor", "gimp", "gimp", NULL,
	  "Phần mềm biên tập & xử lý hình ảnh đồ họa chuyên nghiệp", "Graphics", false, NULL, NULL, NULL },
	{ "Htop Process Viewer", "htop", "htop", NULL,
	  "Trình theo dõi tài nguyên & tiến trình hệ thống CLI trực quan", "System", false, NULL, NULL, NULL },
	{ "Neofetch System Info", "neofetch", "neofetch", NULL,
	  "Công cụ hiển thị thông tin cấu hình hệ thống Linux đẹp mắt", "System", false, NULL, NULL, NULL },
	{ "Thunar File Manager", "thunar", "thunar", NULL,
	  "Trình quản lý tệp tin đồ họa nhanh, nhẹ và dễ sử dụng", "Utilities", false, NULL, NULL, NULL },
	{ "Geany IDE Editor", "geany", "geany", NULL,
	  "Trình soạn thảo mã nguồn & IDE lập trình siêu nhẹ", "Development", false, NULL, NULL, NULL },
	{ "Audacious Audio Player", "audacious", "audacious", NULL,
	  "Trình phát nhạc gọn gàng với giao diện tùy biến phong phú", "Media", false, NULL, NULL, NULL },
	{ "MPV Video Player", "mpv", "mpv", NULL,
	  "Trình phát video tối giản, tiết kiệm tài nguyên GPU & CPU", "Media", false, NULL, NULL, NULL },
	{ "Inkscape Vector Graphics", "inkscape", "inkscape", NULL,
	  "Bộ công cụ vẽ và thiết kế đồ họa vector tiêu chuẩn SVG", "Graphics", false, NULL, NULL, NULL },
	{ "FileZilla FTP Client", "filezilla", "filezilla", NULL,
	  "Phần mềm truyền tải tệp tin qua giao thức FTP / SFTP", "Internet", false, NULL, NULL, NULL },
	{ "GParted Partition Editor", "gparted", "gparted", NULL,
	  "Công cụ phân vùng và quản lý ổ đĩa lưu trữ an toàn", "System", false, NULL, NULL, NULL }
};

static const char *STORE_CSS =
	"window, dialog, viewport, scrolledwindow {"
	"    background: #16161e;"
	"    background-image: none;"
	"    color: #e2e8f0;"
	"    font-family: \"Segoe UI\", Cantarell, Ubuntu, sans-serif;"
	"}"
	"headerbar {"
	"    background: #1a1b26;"
	"    background-image: none;"
	"    border-bottom: 1px solid #282a36;"
	"    color: #f8fafc;"
	"    box-shadow: none;"
	"}"
	"headerbar .title { color: #f8fafc; font-weight: bold; }"
	"headerbar .subtitle { color: #94a3b8; }"
	"button { background: #202230; background-image: none; color: #e2e8f0; border: 1px solid #2e3244; border-radius: 8px; box-shadow: none; }"
	"button label { color: inherit; }"
	"entry {"
	"    background: #20202a;"
	"    background-image: none;"
	"    color: #e2e8f0;"
	"    border: 1px solid #2e303e;"
	"    border-radius: 8px;"
	"    padding: 6px 12px;"
	"    box-shadow: none;"
	"}"
	"notebook, notebook stack { background: #16161e; background-image: none; }"
	"notebook header {"
	"    background: #1a1b26;"
	"    background-image: none;"
	"    border-bottom: 1px solid #282a36;"
	"}"
	"notebook tab {"
	"    background: transparent;"
	"    background-image: none;"
	"    padding: 6px 16px;"
	"    color: #94a3b8;"
	"    font-weight: bold;"
	"}"
	"notebook tab label { color: inherit; }"
	"notebook tab:checked {"
	"    color: #38bdf8;"
	"}"
	".app-card {"
	"    background: #1a1b26;"
	"    background-image: none;"
	"    border: 1px solid #282a36;"
	"    border-radius: 8px;"
	"    padding: 12px 18px;"
	"    margin-bottom: 8px;"
	"}"
	".app-card:hover {"
	"    background-color: #1e202f;"
	"    border-color: #38bdf8;"
	"}"
	".app-title {"
	"    font-size: 14px;"
	"    font-weight: bold;"
	"    color: #f8fafc;"
	"}"
	".app-desc {"
	"    font-size: 12px;"
	"    color: #94a3b8;"
	"}"
	".app-category {"
	"    font-size: 11px;"
	"    color: #38bdf8;"
	"    font-weight: 600;"
	"}"
	"button.btn-install {"
	"    background-color: #0284c7;"
	"    color: #ffffff;"
	"    border: none;"
	"    border-radius: 8px;"
	"    padding: 6px 16px;"
	"    font-weight: bold;"
	"    font-size: 12px;"
	"}"
	"button.btn-install:hover {"
	"    background-color: #0369a1;"
	"}"
	"button.btn-launch {"
	"    background-color: #15803d;"
	"    color: #ffffff;"
	"    border: none;"
	"    border-radius: 8px;"
	"    padding: 6px 16px;"
	"    font-weight: bold;"
	"    font-size: 12px;"
	"}"
	"button.btn-launch:hover {"
	"    background-color: #166534;"
	"}"
	"button.btn-refresh {"
	"    background-color: #20202a;"
	"    color: #e2e8f0;"
	"    border: 1px solid #2e303e;"
	"    border-radius: 8px;"
	"    padding: 6px 14px;"
	"}";

static bool check_is_installed(const AppPackage *app) {
	if (app->cmd[0] == '.' && access(app->cmd, X_OK) == 0) {
		return true;
	}
	char *path = g_find_program_in_path(app->cmd);
	if (path) {
		g_free(path);
		return true;
	}
	if (app->alt_cmd) {
		path = g_find_program_in_path(app->alt_cmd);
		if (path) {
			g_free(path);
			return true;
		}
	}
	return false;
}

static void launch_app(const AppPackage *app) {
	char exec_buf[512];
	if (app->alt_cmd) {
		snprintf(exec_buf, sizeof(exec_buf), "(%s || %s) >/dev/null 2>&1 &", app->cmd, app->alt_cmd);
	} else {
		snprintf(exec_buf, sizeof(exec_buf), "%s >/dev/null 2>&1 &", app->cmd);
	}
	int r = system(exec_buf);
	(void)r;
}

static void install_app(const char *pkg) {
	char cmd[1024];
	snprintf(cmd, sizeof(cmd),
		"./build/diamonds-terminal -e \"bash -c 'sudo apt update && sudo apt install -y %s; read'\" >/dev/null 2>&1 || "
		"x-terminal-emulator -e \"bash -c 'echo Đang cài đặt gói %s...; sudo apt update && sudo apt install -y %s; echo; echo Đã hoàn tất! Nhấn Enter để thoát...; read'\" >/dev/null 2>&1 || "
		"terminator -e \"bash -c 'sudo apt update && sudo apt install -y %s; read'\" >/dev/null 2>&1 &",
		pkg, pkg, pkg, pkg);
	int r = system(cmd);
	(void)r;
}

static void on_action_button_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	AppPackage *app = (AppPackage *)user_data;
	if (app->is_installed) {
		launch_app(app);
	} else {
		install_app(app->pkg);
	}
}

static void refresh_app_row(AppPackage *app) {
	app->is_installed = check_is_installed(app);
	if (app->is_installed) {
		gtk_label_set_markup(GTK_LABEL(app->status_lbl), "<span color='#4ade80'>✓ Đã sẵn sàng</span>");
		gtk_button_set_label(GTK_BUTTON(app->action_btn), "Khởi chạy");
		GtkStyleContext *sc = gtk_widget_get_style_context(app->action_btn);
		gtk_style_context_remove_class(sc, "btn-install");
		gtk_style_context_add_class(sc, "btn-launch");
	} else {
		gtk_label_set_markup(GTK_LABEL(app->status_lbl), "<span color='#94a3b8'>Chưa cài đặt</span>");
		gtk_button_set_label(GTK_BUTTON(app->action_btn), "Cài đặt");
		GtkStyleContext *sc = gtk_widget_get_style_context(app->action_btn);
		gtk_style_context_remove_class(sc, "btn-launch");
		gtk_style_context_add_class(sc, "btn-install");
	}
}

static void on_btn_refresh_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	(void)user_data;
	for (int i = 0; i < NUM_ECO_APPS; i++) refresh_app_row(&g_eco_apps[i]);
	for (int i = 0; i < 12; i++) refresh_app_row(&g_apps[i]);
}

static void filter_list(AppPackage *list, int count, const char *text) {
	for (int i = 0; i < count; i++) {
		if (!text || text[0] == '\0') {
			gtk_widget_show(list[i].row_box);
		} else if (strcasestr(list[i].name, text) ||
		           strcasestr(list[i].desc, text) ||
		           strcasestr(list[i].category, text) ||
		           strcasestr(list[i].pkg, text)) {
			gtk_widget_show(list[i].row_box);
		} else {
			gtk_widget_hide(list[i].row_box);
		}
	}
}

static void on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
	(void)user_data;
	const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
	filter_list(g_eco_apps, NUM_ECO_APPS, text);
	filter_list(g_apps, 12, text);
}

static void populate_cards(GtkWidget *list_box, AppPackage *arr, int count) {
	for (int i = 0; i < count; i++) {
		AppPackage *app = &arr[i];

		GtkWidget *card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
		gtk_style_context_add_class(gtk_widget_get_style_context(card), "app-card");
		app->row_box = card;

		GtkWidget *details_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

		GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
		GtkWidget *lbl_name = gtk_label_new(app->name);
		gtk_style_context_add_class(gtk_widget_get_style_context(lbl_name), "app-title");
		gtk_label_set_xalign(GTK_LABEL(lbl_name), 0.0);
		gtk_box_pack_start(GTK_BOX(title_box), lbl_name, FALSE, FALSE, 0);

		char cat_buf[64];
		snprintf(cat_buf, sizeof(cat_buf), "• %s", app->category);
		GtkWidget *lbl_cat = gtk_label_new(cat_buf);
		gtk_style_context_add_class(gtk_widget_get_style_context(lbl_cat), "app-category");
		gtk_box_pack_start(GTK_BOX(title_box), lbl_cat, FALSE, FALSE, 0);

		gtk_box_pack_start(GTK_BOX(details_box), title_box, FALSE, FALSE, 0);

		GtkWidget *lbl_desc = gtk_label_new(app->desc);
		gtk_style_context_add_class(gtk_widget_get_style_context(lbl_desc), "app-desc");
		gtk_label_set_xalign(GTK_LABEL(lbl_desc), 0.0);
		gtk_label_set_line_wrap(GTK_LABEL(lbl_desc), TRUE);
		gtk_box_pack_start(GTK_BOX(details_box), lbl_desc, FALSE, FALSE, 0);

		gtk_box_pack_start(GTK_BOX(card), details_box, TRUE, TRUE, 0);

		GtkWidget *action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
		gtk_widget_set_valign(action_box, GTK_ALIGN_CENTER);

		app->status_lbl = gtk_label_new("");
		gtk_box_pack_start(GTK_BOX(action_box), app->status_lbl, FALSE, FALSE, 0);

		app->action_btn = gtk_button_new();
		g_signal_connect(app->action_btn, "clicked", G_CALLBACK(on_action_button_clicked), app);
		gtk_box_pack_start(GTK_BOX(action_box), app->action_btn, FALSE, FALSE, 0);

		gtk_box_pack_start(GTK_BOX(card), action_box, FALSE, FALSE, 0);

		refresh_app_row(app);
		gtk_box_pack_start(GTK_BOX(list_box), card, FALSE, FALSE, 0);
	}
}

int main(int argc, char **argv) {
	g_set_prgname("diamonds-store");
	gtk_init(&argc, &argv);

	GtkSettings *gset = gtk_settings_get_default();
	if (gset) {
		g_object_set(gset, "gtk-application-prefer-dark-theme", TRUE, NULL);
	}

	GtkCssProvider *css_provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(css_provider, STORE_CSS, -1, NULL);
	gtk_style_context_add_provider_for_screen(
		gdk_screen_get_default(),
		GTK_STYLE_PROVIDER(css_provider),
		GTK_STYLE_PROVIDER_PRIORITY_USER + 10);

	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "Diamonds App Store");
	gtk_window_set_default_size(GTK_WINDOW(window), 840, 580);
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	/* HeaderBar */
	GtkWidget *header_bar = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), "Diamonds App Store");
	gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header_bar), "Hệ sinh thái Diamonds & Kho ứng dụng Linux");
	gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);

	GtkWidget *btn_refresh = gtk_button_new_with_label("⟳ Làm mới");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_refresh), "btn-refresh");
	g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_btn_refresh_clicked), NULL);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header_bar), btn_refresh);

	/* Main Container */
	GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width(GTK_CONTAINER(main_vbox), 16);
	gtk_container_add(GTK_CONTAINER(window), main_vbox);

	/* Search bar */
	GtkWidget *search_entry = gtk_search_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), "Tìm kiếm ứng dụng (Diamonds, Terminal, Notes, VLC, GIMP...)...");
	g_signal_connect(search_entry, "search-changed", G_CALLBACK(on_search_changed), NULL);
	gtk_box_pack_start(GTK_BOX(main_vbox), search_entry, FALSE, FALSE, 0);

	/* Notebook with 2 tabs */
	GtkWidget *notebook = gtk_notebook_new();
	gtk_box_pack_start(GTK_BOX(main_vbox), notebook, TRUE, TRUE, 0);

	/* Tab 1: Diamonds Ecosystem */
	GtkWidget *scroll_eco = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_eco), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	GtkWidget *box_eco = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_set_border_width(GTK_CONTAINER(box_eco), 10);
	gtk_container_add(GTK_CONTAINER(scroll_eco), box_eco);
	populate_cards(box_eco, g_eco_apps, NUM_ECO_APPS);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scroll_eco, gtk_label_new("💎 Ứng dụng Diamonds (Built-in)"));

	/* Tab 2: Linux Packages */
	GtkWidget *scroll_pkg = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_pkg), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	GtkWidget *box_pkg = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_set_border_width(GTK_CONTAINER(box_pkg), 10);
	gtk_container_add(GTK_CONTAINER(scroll_pkg), box_pkg);
	populate_cards(box_pkg, g_apps, 12);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scroll_pkg, gtk_label_new("📦 Kho gói Hệ thống (APT)"));

	gtk_widget_show_all(window);
	gtk_main();

	return 0;
}
