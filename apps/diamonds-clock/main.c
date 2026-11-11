#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

typedef struct {
	GtkWidget *window;
	GtkWidget *lbl_digital_time;
	GtkWidget *lbl_digital_date;
	GtkWidget *lbl_world_utc;
	GtkWidget *lbl_world_tokyo;

	/* Stopwatch */
	GtkWidget *lbl_stopwatch;
	GtkWidget *btn_start_stop;
	GtkWidget *btn_lap;
	GtkWidget *btn_reset;
	GtkWidget *lap_list_box;
	int lap_count;

	bool stopwatch_running;
	gint64 stopwatch_start_time;
	gint64 stopwatch_accumulated;
	guint stopwatch_timer_id;

	/* Countdown Timer */
	GtkWidget *lbl_timer;
	GtkWidget *spin_min;
	GtkWidget *spin_sec;
	GtkWidget *btn_timer_start;
	bool timer_running;
	int timer_remaining_sec;
	guint countdown_id;
} ClockApp;

static const char *CLOCK_CSS =
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
	".clock-card {"
	"    background: #1a1b26;"
	"    background-image: none;"
	"    border: 1px solid #282a36;"
	"    border-radius: 12px;"
	"    padding: 20px;"
	"}"
	".clock-digits {"
	"    font-size: 48px;"
	"    font-weight: 800;"
	"    color: #38bdf8;"
	"    font-family: Monospace, Segoe UI, sans-serif;"
	"    letter-spacing: 2px;"
	"}"
	".clock-date {"
	"    font-size: 14px;"
	"    color: #94a3b8;"
	"    font-weight: 500;"
	"}"
	".card-title {"
	"    font-size: 13px;"
	"    font-weight: 700;"
	"    color: #38bdf8;"
	"    letter-spacing: 1px;"
	"}"
	".stopwatch-digits {"
	"    font-size: 38px;"
	"    font-weight: 800;"
	"    color: #f8fafc;"
	"    font-family: Monospace, sans-serif;"
	"    letter-spacing: 1px;"
	"}"
	"button.btn-start {"
	"    background-color: #0284c7;"
	"    color: #ffffff;"
	"    border: none;"
	"    border-radius: 8px;"
	"    padding: 8px 20px;"
	"    font-weight: bold;"
	"    font-size: 13px;"
	"}"
	"button.btn-start:hover {"
	"    background-color: #0369a1;"
	"}"
	"button.btn-stop {"
	"    background-color: #dc2626;"
	"    color: #ffffff;"
	"    border: none;"
	"    border-radius: 8px;"
	"    padding: 8px 20px;"
	"    font-weight: bold;"
	"    font-size: 13px;"
	"}"
	"button.btn-stop:hover {"
	"    background-color: #b91c1c;"
	"}"
	"button.btn-reset {"
	"    background-color: #20202a;"
	"    color: #e2e8f0;"
	"    border: 1px solid #2e303e;"
	"    border-radius: 8px;"
	"    padding: 8px 20px;"
	"    font-weight: bold;"
	"    font-size: 13px;"
	"}"
	"button.btn-reset:hover {"
	"    background-color: #2b2d3c;"
	"}";

static gboolean update_clock_display(gpointer user_data) {
	ClockApp *app = user_data;
	time_t now = time(NULL);
	struct tm local_tm;
	if (localtime_r(&now, &local_tm)) {
		char time_buf[32];
		snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d",
			local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);
		gtk_label_set_text(GTK_LABEL(app->lbl_digital_time), time_buf);

		static const char *weekdays[] = {
			"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"
		};
		char date_buf[128];
		snprintf(date_buf, sizeof(date_buf), "%s, %02d/%02d/%04d",
			weekdays[local_tm.tm_wday % 7], local_tm.tm_mday, local_tm.tm_mon + 1, local_tm.tm_year + 1900);
		gtk_label_set_text(GTK_LABEL(app->lbl_digital_date), date_buf);
	}

	struct tm utc_tm;
	if (gmtime_r(&now, &utc_tm)) {
		char utc_buf[64];
		snprintf(utc_buf, sizeof(utc_buf), "UTC / London: %02d:%02d:%02d",
			utc_tm.tm_hour, utc_tm.tm_min, utc_tm.tm_sec);
		gtk_label_set_text(GTK_LABEL(app->lbl_world_utc), utc_buf);

		time_t jst_t = now + 9 * 3600;
		struct tm jst_tm;
		if (gmtime_r(&jst_t, &jst_tm)) {
			char jst_buf[64];
			snprintf(jst_buf, sizeof(jst_buf), "Tokyo (UTC+9): %02d:%02d:%02d",
				jst_tm.tm_hour, jst_tm.tm_min, jst_tm.tm_sec);
			gtk_label_set_text(GTK_LABEL(app->lbl_world_tokyo), jst_buf);
		}
	}

	return G_SOURCE_CONTINUE;
}

static void format_stopwatch_us(gint64 total_us, char *buf, size_t len) {
	gint64 total_ms = total_us / 1000;
	int hours = (int)(total_ms / 3600000);
	int mins = (int)((total_ms % 3600000) / 60000);
	int secs = (int)((total_ms % 60000) / 1000);
	int hundredths = (int)((total_ms % 1000) / 10);
	snprintf(buf, len, "%02d:%02d:%02d.%02d", hours, mins, secs, hundredths);
}

static void update_stopwatch_label(ClockApp *app, gint64 total_us) {
	char buf[64];
	format_stopwatch_us(total_us, buf, sizeof(buf));
	gtk_label_set_text(GTK_LABEL(app->lbl_stopwatch), buf);
}

static gboolean on_stopwatch_tick(gpointer user_data) {
	ClockApp *app = user_data;
	if (!app->stopwatch_running) return G_SOURCE_REMOVE;

	gint64 now = g_get_monotonic_time();
	gint64 elapsed = (now - app->stopwatch_start_time) + app->stopwatch_accumulated;
	update_stopwatch_label(app, elapsed);

	return G_SOURCE_CONTINUE;
}

static void on_btn_start_stop_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	ClockApp *app = user_data;
	GtkStyleContext *sc = gtk_widget_get_style_context(app->btn_start_stop);

	if (!app->stopwatch_running) {
		app->stopwatch_running = true;
		app->stopwatch_start_time = g_get_monotonic_time();
		app->stopwatch_timer_id = g_timeout_add(30, on_stopwatch_tick, app);

		gtk_button_set_label(GTK_BUTTON(app->btn_start_stop), "Tạm dừng");
		gtk_style_context_remove_class(sc, "btn-start");
		gtk_style_context_add_class(sc, "btn-stop");
	} else {
		app->stopwatch_running = false;
		if (app->stopwatch_timer_id > 0) {
			g_source_remove(app->stopwatch_timer_id);
			app->stopwatch_timer_id = 0;
		}
		gint64 now = g_get_monotonic_time();
		app->stopwatch_accumulated += (now - app->stopwatch_start_time);

		gtk_button_set_label(GTK_BUTTON(app->btn_start_stop), "Tiếp tục");
		gtk_style_context_remove_class(sc, "btn-stop");
		gtk_style_context_add_class(sc, "btn-start");
	}
}

static void on_btn_lap_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	ClockApp *app = user_data;
	gint64 elapsed = app->stopwatch_accumulated;
	if (app->stopwatch_running) {
		elapsed += (g_get_monotonic_time() - app->stopwatch_start_time);
	}
	if (elapsed <= 0) return;

	app->lap_count++;
	char t_str[64], line[128];
	format_stopwatch_us(elapsed, t_str, sizeof(t_str));
	snprintf(line, sizeof(line), "Vòng #%d  —  %s", app->lap_count, t_str);
	GtkWidget *lbl = gtk_label_new(line);
	gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
	gtk_box_pack_start(GTK_BOX(app->lap_list_box), lbl, FALSE, FALSE, 2);
	gtk_widget_show(lbl);
}

static void on_btn_reset_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	ClockApp *app = user_data;

	app->stopwatch_running = false;
	if (app->stopwatch_timer_id > 0) {
		g_source_remove(app->stopwatch_timer_id);
		app->stopwatch_timer_id = 0;
	}
	app->stopwatch_accumulated = 0;
	app->lap_count = 0;
	update_stopwatch_label(app, 0);

	GList *children = gtk_container_get_children(GTK_CONTAINER(app->lap_list_box));
	for (GList *l = children; l != NULL; l = l->next) {
		gtk_widget_destroy(GTK_WIDGET(l->data));
	}
	g_list_free(children);

	gtk_button_set_label(GTK_BUTTON(app->btn_start_stop), "Bắt đầu");
	GtkStyleContext *sc = gtk_widget_get_style_context(app->btn_start_stop);
	gtk_style_context_remove_class(sc, "btn-stop");
	gtk_style_context_add_class(sc, "btn-start");
}

/* Countdown Timer */
static void update_timer_label(ClockApp *app) {
	int m = app->timer_remaining_sec / 60;
	int s = app->timer_remaining_sec % 60;
	char buf[32];
	snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
	gtk_label_set_text(GTK_LABEL(app->lbl_timer), buf);
}

static gboolean on_countdown_tick(gpointer user_data) {
	ClockApp *app = user_data;
	if (!app->timer_running) return G_SOURCE_REMOVE;
	if (app->timer_remaining_sec > 0) {
		app->timer_remaining_sec--;
		update_timer_label(app);
	}
	if (app->timer_remaining_sec <= 0) {
		app->timer_running = false;
		app->countdown_id = 0;
		gtk_button_set_label(GTK_BUTTON(app->btn_timer_start), "Bắt đầu đếm ngược");
		GdkDisplay *d = gdk_display_get_default();
		if (d) gdk_display_beep(d);
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static void on_timer_start_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	ClockApp *app = user_data;
	if (!app->timer_running) {
		if (app->timer_remaining_sec <= 0) {
			int m = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->spin_min));
			int s = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->spin_sec));
			app->timer_remaining_sec = m * 60 + s;
		}
		if (app->timer_remaining_sec <= 0) return;
		app->timer_running = true;
		update_timer_label(app);
		app->countdown_id = g_timeout_add_seconds(1, on_countdown_tick, app);
		gtk_button_set_label(GTK_BUTTON(app->btn_timer_start), "Tạm dừng");
	} else {
		app->timer_running = false;
		if (app->countdown_id > 0) {
			g_source_remove(app->countdown_id);
			app->countdown_id = 0;
		}
		gtk_button_set_label(GTK_BUTTON(app->btn_timer_start), "Tiếp tục");
	}
}

static void on_timer_reset_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	ClockApp *app = user_data;
	app->timer_running = false;
	if (app->countdown_id > 0) {
		g_source_remove(app->countdown_id);
		app->countdown_id = 0;
	}
	int m = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->spin_min));
	int s = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->spin_sec));
	app->timer_remaining_sec = m * 60 + s;
	update_timer_label(app);
	gtk_button_set_label(GTK_BUTTON(app->btn_timer_start), "Bắt đầu đếm ngược");
}

int main(int argc, char **argv) {
	g_set_prgname("diamonds-clock");
	gtk_init(&argc, &argv);

	GtkSettings *gset = gtk_settings_get_default();
	if (gset) {
		g_object_set(gset, "gtk-application-prefer-dark-theme", TRUE, NULL);
	}

	GtkCssProvider *css_provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(css_provider, CLOCK_CSS, -1, NULL);
	gtk_style_context_add_provider_for_screen(
		gdk_screen_get_default(),
		GTK_STYLE_PROVIDER(css_provider),
		GTK_STYLE_PROVIDER_PRIORITY_USER + 10);

	ClockApp app = {0};

	app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(app.window), "Diamonds Clock & Stopwatch");
	gtk_window_set_default_size(GTK_WINDOW(app.window), 640, 520);
	g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	/* HeaderBar */
	GtkWidget *header_bar = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), "Diamonds Clock");
	gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header_bar), "Đồng hồ Thế giới, Bấm giờ & Hẹn giờ");
	gtk_window_set_titlebar(GTK_WINDOW(app.window), header_bar);

	GtkWidget *notebook = gtk_notebook_new();
	gtk_container_add(GTK_CONTAINER(app.window), notebook);

	/* Tab 1: Clock & Stopwatch */
	GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
	gtk_container_set_border_width(GTK_CONTAINER(main_vbox), 18);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), main_vbox, gtk_label_new("🕒 Đồng hồ & Bấm giờ"));

	/* 1. Digital Clock Card */
	GtkWidget *clock_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_style_context_add_class(gtk_widget_get_style_context(clock_card), "clock-card");

	GtkWidget *lbl_c_title = gtk_label_new("GIỜ HỆ THỐNG HIỆN TẠI");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_c_title), "card-title");
	gtk_label_set_xalign(GTK_LABEL(lbl_c_title), 0.5);
	gtk_box_pack_start(GTK_BOX(clock_card), lbl_c_title, FALSE, FALSE, 0);

	app.lbl_digital_time = gtk_label_new("00:00:00");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_digital_time), "clock-digits");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_digital_time), 0.5);
	gtk_box_pack_start(GTK_BOX(clock_card), app.lbl_digital_time, FALSE, FALSE, 2);

	app.lbl_digital_date = gtk_label_new("Đang tải...");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_digital_date), "clock-date");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_digital_date), 0.5);
	gtk_box_pack_start(GTK_BOX(clock_card), app.lbl_digital_date, FALSE, FALSE, 0);

	GtkWidget *world_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
	gtk_widget_set_halign(world_box, GTK_ALIGN_CENTER);
	app.lbl_world_utc = gtk_label_new("UTC: --:--:--");
	app.lbl_world_tokyo = gtk_label_new("Tokyo: --:--:--");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_world_utc), "clock-date");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_world_tokyo), "clock-date");
	gtk_box_pack_start(GTK_BOX(world_box), app.lbl_world_utc, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(world_box), app.lbl_world_tokyo, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(clock_card), world_box, FALSE, FALSE, 4);

	gtk_box_pack_start(GTK_BOX(main_vbox), clock_card, FALSE, FALSE, 0);

	/* 2. Stopwatch Card */
	GtkWidget *sw_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_style_context_add_class(gtk_widget_get_style_context(sw_card), "clock-card");

	GtkWidget *lbl_sw_title = gtk_label_new("ĐỒNG HỒ BẤM GIỜ (STOPWATCH)");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_sw_title), "card-title");
	gtk_label_set_xalign(GTK_LABEL(lbl_sw_title), 0.5);
	gtk_box_pack_start(GTK_BOX(sw_card), lbl_sw_title, FALSE, FALSE, 0);

	app.lbl_stopwatch = gtk_label_new("00:00:00.00");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_stopwatch), "stopwatch-digits");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_stopwatch), 0.5);
	gtk_box_pack_start(GTK_BOX(sw_card), app.lbl_stopwatch, FALSE, FALSE, 4);

	GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_widget_set_halign(btn_box, GTK_ALIGN_CENTER);

	app.btn_start_stop = gtk_button_new_with_label("Bắt đầu");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.btn_start_stop), "btn-start");
	g_signal_connect(app.btn_start_stop, "clicked", G_CALLBACK(on_btn_start_stop_clicked), &app);
	gtk_box_pack_start(GTK_BOX(btn_box), app.btn_start_stop, FALSE, FALSE, 0);

	app.btn_lap = gtk_button_new_with_label("Ghi vòng (Lap)");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.btn_lap), "btn-reset");
	g_signal_connect(app.btn_lap, "clicked", G_CALLBACK(on_btn_lap_clicked), &app);
	gtk_box_pack_start(GTK_BOX(btn_box), app.btn_lap, FALSE, FALSE, 0);

	app.btn_reset = gtk_button_new_with_label("Đặt lại");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.btn_reset), "btn-reset");
	g_signal_connect(app.btn_reset, "clicked", G_CALLBACK(on_btn_reset_clicked), &app);
	gtk_box_pack_start(GTK_BOX(btn_box), app.btn_reset, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(sw_card), btn_box, FALSE, FALSE, 2);

	GtkWidget *lap_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_size_request(lap_scroll, -1, 70);
	app.lap_list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_container_add(GTK_CONTAINER(lap_scroll), app.lap_list_box);
	gtk_box_pack_start(GTK_BOX(sw_card), lap_scroll, TRUE, TRUE, 4);

	gtk_box_pack_start(GTK_BOX(main_vbox), sw_card, TRUE, TRUE, 0);

	/* Tab 2: Countdown Timer */
	GtkWidget *timer_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_container_set_border_width(GTK_CONTAINER(timer_vbox), 20);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), timer_vbox, gtk_label_new("⏳ Hẹn giờ đếm ngược"));

	GtkWidget *timer_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
	gtk_style_context_add_class(gtk_widget_get_style_context(timer_card), "clock-card");

	GtkWidget *lbl_tm_title = gtk_label_new("HẸN GIỜ ĐẾM NGƯỢC (COUNTDOWN TIMER)");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_tm_title), "card-title");
	gtk_label_set_xalign(GTK_LABEL(lbl_tm_title), 0.5);
	gtk_box_pack_start(GTK_BOX(timer_card), lbl_tm_title, FALSE, FALSE, 0);

	app.lbl_timer = gtk_label_new("05:00");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_timer), "clock-digits");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_timer), 0.5);
	gtk_box_pack_start(GTK_BOX(timer_card), app.lbl_timer, FALSE, FALSE, 6);

	GtkWidget *spin_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_widget_set_halign(spin_box, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(spin_box), gtk_label_new("Phút:"), FALSE, FALSE, 0);
	app.spin_min = gtk_spin_button_new_with_range(0, 180, 1);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(app.spin_min), 5);
	gtk_box_pack_start(GTK_BOX(spin_box), app.spin_min, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(spin_box), gtk_label_new("Giây:"), FALSE, FALSE, 0);
	app.spin_sec = gtk_spin_button_new_with_range(0, 59, 5);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(app.spin_sec), 0);
	gtk_box_pack_start(GTK_BOX(spin_box), app.spin_sec, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(timer_card), spin_box, FALSE, FALSE, 4);

	GtkWidget *tbtn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_widget_set_halign(tbtn_box, GTK_ALIGN_CENTER);
	app.btn_timer_start = gtk_button_new_with_label("Bắt đầu đếm ngược");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.btn_timer_start), "btn-start");
	g_signal_connect(app.btn_timer_start, "clicked", G_CALLBACK(on_timer_start_clicked), &app);
	gtk_box_pack_start(GTK_BOX(tbtn_box), app.btn_timer_start, FALSE, FALSE, 0);

	GtkWidget *btn_timer_reset = gtk_button_new_with_label("Đặt lại");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_timer_reset), "btn-reset");
	g_signal_connect(btn_timer_reset, "clicked", G_CALLBACK(on_timer_reset_clicked), &app);
	gtk_box_pack_start(GTK_BOX(tbtn_box), btn_timer_reset, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(timer_card), tbtn_box, FALSE, FALSE, 6);
	gtk_box_pack_start(GTK_BOX(timer_vbox), timer_card, TRUE, TRUE, 0);

	app.timer_remaining_sec = 300;
	update_clock_display(&app);
	g_timeout_add_seconds(1, update_clock_display, &app);

	gtk_widget_show_all(app.window);
	gtk_main();

	return 0;
}
