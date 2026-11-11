#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>

typedef struct {
	GtkWidget *window;
	GtkWidget *header_bar;
	GtkWidget *stack;

	/* Wallpaper Page */
	GtkWidget *wallpaper_preview;
	GtkWidget *wallpaper_path_label;
	char current_wallpaper[1024];

	/* Display Resolution Page */
	GtkWidget *combo_resolution;
	GtkWidget *lbl_resolution_status;

	/* Audio Page */
	GtkWidget *volume_slider;
	GtkWidget *volume_label;
	GtkWidget *mute_btn;

	/* System Page */
	GtkWidget *lbl_cpu_model;
	GtkWidget *lbl_cpu_cores;
	GtkWidget *lbl_cpu_usage;
	GtkWidget *cpu_progress;
	GtkWidget *lbl_ram_usage;
	GtkWidget *ram_progress;
	GtkWidget *lbl_os_name;
	GtkWidget *lbl_kernel;

	/* CPU usage calculation state */
	unsigned long long prev_idle;
	unsigned long long prev_total;

	/* Animation Page */
	GtkWidget *combo_animation;
	GtkWidget *lbl_status;

	/* Appearance & Dock Page */
	GtkWidget *mica_opacity_slider;
	GtkWidget *combo_accent;
	GtkWidget *combo_dock_size;
	GtkWidget *lbl_appearance_status;

	/* Input (Keyboard & Mouse) Page */
	GtkWidget *kb_rate_slider;
	GtkWidget *kb_delay_slider;
	GtkWidget *mouse_speed_slider;
	GtkWidget *chk_natural_scroll;
	GtkWidget *lbl_input_status;

	/* Network & Internet Page (Wired Ethernet + Wireless Wi-Fi) */
	GtkWidget *lbl_eth_status;
	GtkWidget *lbl_eth_ip;
	GtkWidget *lbl_eth_mac;
	GtkWidget *lbl_wifi_status;
	GtkWidget *lbl_wifi_ip;
	GtkWidget *wifi_list_box;
	GtkWidget *entry_wifi_ssid;
	GtkWidget *entry_wifi_pass;
	GtkWidget *lbl_net_action_status;
	char eth_iface[32];
	char wifi_iface[32];

	/* Bluetooth Page */
	GtkWidget *lbl_bt_status;
	GtkWidget *bt_list_box;

	/* Power & Session Page */
	GtkWidget *combo_power_profile;
	GtkWidget *combo_screen_timeout;
	GtkWidget *brightness_slider;
	GtkWidget *lbl_power_status;

	/* Storage Page */
	GtkWidget *lbl_disk_root;
	GtkWidget *bar_disk_root;
	GtkWidget *lbl_disk_home;
	GtkWidget *bar_disk_home;

	/* Date & Time Page */
	GtkWidget *lbl_current_time;
	GtkWidget *combo_timezone;
	GtkWidget *chk_ntp;
	GtkWidget *lbl_datetime_status;
} SettingsApp;

static const char *DARK_MICA_CSS =
	"window, dialog, viewport, scrolledwindow {"
	"    background: #14151f;"
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
	"headerbar .subtitle { color: #94a3b8; font-size: 11px; }"
	"headerbar button {"
	"    background: transparent;"
	"    background-image: none;"
	"    border: none;"
	"    border-radius: 6px;"
	"    color: #cbd5e1;"
	"    box-shadow: none;"
	"}"
	"headerbar button:hover {"
	"    background: rgba(255, 255, 255, 0.08);"
	"    color: #ffffff;"
	"}"
	".sidebar, stacksidebar, stacksidebar list, .sidebar list {"
	"    background: #181924;"
	"    background-image: none;"
	"    border-right: 1px solid #282a36;"
	"    color: #cbd5e1;"
	"}"
	".sidebar row, stacksidebar row {"
	"    background: transparent;"
	"    background-image: none;"
	"    color: #cbd5e1;"
	"    border: none;"
	"    border-radius: 8px;"
	"    margin: 3px 8px;"
	"    padding: 9px 12px;"
	"}"
	".sidebar row label, stacksidebar row label {"
	"    color: inherit;"
	"    font-size: 13px;"
	"    font-weight: 500;"
	"}"
	".sidebar row:hover, stacksidebar row:hover {"
	"    background: rgba(255, 255, 255, 0.06);"
	"    color: #ffffff;"
	"}"
	".sidebar row:selected, stacksidebar row:selected {"
	"    background: #0284c7;"
	"    background-image: none;"
	"    color: #ffffff;"
	"    font-weight: bold;"
	"}"
	".page-content {"
	"    padding: 20px 28px;"
	"}"
	".card {"
	"    background: #1a1c29;"
	"    background-image: none;"
	"    border: 1px solid #2a2e42;"
	"    border-radius: 10px;"
	"    padding: 16px 20px;"
	"}"
	".card-title {"
	"    font-size: 14px;"
	"    font-weight: bold;"
	"    color: #38bdf8;"
	"    margin-bottom: 6px;"
	"}"
	"button {"
	"    background: #232638;"
	"    background-image: none;"
	"    color: #e2e8f0;"
	"    border: 1px solid #32374f;"
	"    border-radius: 8px;"
	"    padding: 7px 14px;"
	"    box-shadow: none;"
	"    text-shadow: none;"
	"}"
	"button label { color: inherit; }"
	"button:hover {"
	"    background: #2d3147;"
	"    background-image: none;"
	"    color: #ffffff;"
	"}"
	"button.action-btn {"
	"    background: #0284c7;"
	"    background-image: none;"
	"    color: #ffffff;"
	"    border: none;"
	"    border-radius: 8px;"
	"    padding: 8px 16px;"
	"    font-weight: 600;"
	"}"
	"button.action-btn:hover {"
	"    background: #0369a1;"
	"    background-image: none;"
	"}"
	"entry {"
	"    background: #11131c;"
	"    background-image: none;"
	"    color: #f1f5f9;"
	"    border: 1px solid #2e3348;"
	"    border-radius: 8px;"
	"    padding: 6px 10px;"
	"    caret-color: #38bdf8;"
	"    box-shadow: none;"
	"}"
	"entry:focus { border-color: #38bdf8; }"
	"list, row {"
	"    background: #131520;"
	"    background-image: none;"
	"    color: #e2e8f0;"
	"}"
	"row:hover { background: #1d2030; }"
	"row:selected { background: #0284c7; color: #ffffff; }"
	"progressbar trough {"
	"    background: #20202a;"
	"    background-image: none;"
	"    border-radius: 8px;"
	"    min-height: 12px;"
	"}"
	"progressbar progress {"
	"    background: #38bdf8;"
	"    background-image: none;"
	"    border-radius: 8px;"
	"    min-height: 12px;"
	"}"
	"combobox button {"
	"    border-radius: 8px;"
	"    background: #202230;"
	"    background-image: none;"
	"    color: #e2e8f0;"
	"    border: 1px solid #2e303e;"
	"}"
	"scale trough {"
	"    border-radius: 8px;"
	"    background: #20202a;"
	"    background-image: none;"
	"}"
	"scale highlight {"
	"    border-radius: 8px;"
	"    background: #38bdf8;"
	"    background-image: none;"
	"}"
	"scrollbar, scrollbar trough { background: #14151f; background-image: none; border: none; }"
	"scrollbar slider { background: #34384e; background-image: none; border-radius: 6px; min-width: 6px; min-height: 6px; }";

static void ensure_config_dir(void) {
	const char *home = getenv("HOME");
	if (!home) return;
	char dir[1024];
	snprintf(dir, sizeof(dir), "%s/.config", home);
	mkdir(dir, 0755);
	snprintf(dir, sizeof(dir), "%s/.config/diamonds", home);
	mkdir(dir, 0755);
}

static void save_settings(const char *key, const char *value) {
	ensure_config_dir();
	const char *home = getenv("HOME");
	if (!home) return;
	char conf_path[1024];
	snprintf(conf_path, sizeof(conf_path), "%s/.config/diamonds/settings.conf", home);

	char lines[64][1024];
	int line_count = 0;
	bool key_found = false;

	FILE *fp = fopen(conf_path, "r");
	if (fp) {
		char buf[1024];
		while (fgets(buf, sizeof(buf), fp) && line_count < 60) {
			buf[strcspn(buf, "\r\n")] = '\0';
			if (strncmp(buf, key, strlen(key)) == 0 && buf[strlen(key)] == '=') {
				snprintf(lines[line_count++], sizeof(lines[0]), "%s=%s", key, value);
				key_found = true;
			} else {
				snprintf(lines[line_count++], sizeof(lines[0]), "%s", buf);
			}
		}
		fclose(fp);
	}

	if (!key_found) {
		snprintf(lines[line_count++], sizeof(lines[0]), "%s=%s", key, value);
	}

	fp = fopen(conf_path, "w");
	if (fp) {
		for (int i = 0; i < line_count; i++) {
			fprintf(fp, "%s\n", lines[i]);
		}
		fclose(fp);
	}
}

static void load_setting(const char *key, char *out_val, size_t out_size, const char *fallback) {
	const char *home = getenv("HOME");
	if (!home) {
		snprintf(out_val, out_size, "%s", fallback);
		return;
	}
	char conf_path[1024];
	snprintf(conf_path, sizeof(conf_path), "%s/.config/diamonds/settings.conf", home);

	FILE *fp = fopen(conf_path, "r");
	if (!fp) {
		snprintf(out_val, out_size, "%s", fallback);
		return;
	}

	char buf[1024];
	while (fgets(buf, sizeof(buf), fp)) {
		buf[strcspn(buf, "\r\n")] = '\0';
		if (strncmp(buf, key, strlen(key)) == 0 && buf[strlen(key)] == '=') {
			snprintf(out_val, out_size, "%s", buf + strlen(key) + 1);
			fclose(fp);
			return;
		}
	}
	fclose(fp);
	snprintf(out_val, out_size, "%s", fallback);
}

/* Wallpaper Page Handlers */
static void update_wallpaper_preview(SettingsApp *app, const char *img_path) {
	if (!img_path || !*img_path) return;
	GError *err = NULL;
	GdkPixbuf *orig = gdk_pixbuf_new_from_file(img_path, &err);
	if (orig) {
		GdkPixbuf *scaled = gdk_pixbuf_scale_simple(orig, 360, 200, GDK_INTERP_BILINEAR);
		gtk_image_set_from_pixbuf(GTK_IMAGE(app->wallpaper_preview), scaled);
		g_object_unref(scaled);
		g_object_unref(orig);
		gtk_label_set_text(GTK_LABEL(app->wallpaper_path_label), img_path);
		snprintf(app->current_wallpaper, sizeof(app->current_wallpaper), "%s", img_path);
	}
}

static void on_btn_browse_wallpaper_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	SettingsApp *app = user_data;
	GtkWidget *dialog = gtk_file_chooser_dialog_new(
		"Chọn hình nền Diamonds Desktop",
		GTK_WINDOW(app->window),
		GTK_FILE_CHOOSER_ACTION_OPEN,
		"_Hủy", GTK_RESPONSE_CANCEL,
		"_Chọn", GTK_RESPONSE_ACCEPT,
		NULL);

	GtkFileFilter *filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "Hình ảnh (*.jpg, *.png, *.webp)");
	gtk_file_filter_add_pattern(filter, "*.jpg");
	gtk_file_filter_add_pattern(filter, "*.jpeg");
	gtk_file_filter_add_pattern(filter, "*.png");
	gtk_file_filter_add_pattern(filter, "*.webp");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		if (filename) {
			update_wallpaper_preview(app, filename);
			save_settings("wallpaper", filename);

			const char *home = getenv("HOME");
			if (home) {
				char cmd[2048];
				snprintf(cmd, sizeof(cmd), "cp \"%s\" \"%s/.config/diamonds/wallpaper.jpg\" 2>/dev/null", filename, home);
				int res = system(cmd);
				(void)res;
			}
			g_free(filename);
		}
	}
	gtk_widget_destroy(dialog);
}

/* Resolution Page Handlers */
static void on_btn_apply_resolution_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	SettingsApp *app = user_data;
	const char *res_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->combo_resolution));
	if (res_id) {
		char cmd[256];
		snprintf(cmd, sizeof(cmd),
			"wlr-randr --output $(wlr-randr 2>/dev/null | grep -o '^[A-Za-z0-9-]*' | head -n1) --mode %s >/dev/null 2>&1 || "
			"xrandr -s %s >/dev/null 2>&1",
			res_id, res_id);
		int r = system(cmd);
		(void)r;

		save_settings("resolution", res_id);

		char status[128];
		snprintf(status, sizeof(status), "✓ Đã áp dụng độ phân giải: %s", res_id);
		gtk_label_set_text(GTK_LABEL(app->lbl_resolution_status), status);
	}
}

/* Audio Page Handlers */
static int read_current_volume(void) {
	FILE *fp = popen("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null | grep -o '[0-9]\\+%' | head -n1", "r");
	if (fp) {
		char buf[32];
		if (fgets(buf, sizeof(buf), fp)) {
			int vol = atoi(buf);
			pclose(fp);
			if (vol >= 0 && vol <= 150) return vol;
		}
		pclose(fp);
	}
	fp = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
	if (fp) {
		char buf[64];
		if (fgets(buf, sizeof(buf), fp)) {
			float vol = 0.0f;
			if (sscanf(buf, "Volume: %f", &vol) == 1) {
				pclose(fp);
				return (int)(vol * 100.0f);
			}
		}
		pclose(fp);
	}
	return 75;
}

static void on_volume_slider_changed(GtkRange *range, gpointer user_data) {
	SettingsApp *app = user_data;
	int val = (int)gtk_range_get_value(range);

	char txt[32];
	snprintf(txt, sizeof(txt), "%d%%", val);
	gtk_label_set_text(GTK_LABEL(app->volume_label), txt);

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %d%% 2>/dev/null || wpctl set-volume @DEFAULT_AUDIO_SINK@ %.2f 2>/dev/null",
		val, (double)val / 100.0);
	int res = system(cmd);
	(void)res;
}

static void on_mute_btn_toggled(GtkToggleButton *btn, gpointer user_data) {
	(void)user_data;
	gboolean active = gtk_toggle_button_get_active(btn);
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "pactl set-sink-mute @DEFAULT_SINK@ %s 2>/dev/null || wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle 2>/dev/null",
		active ? "1" : "0");
	int res = system(cmd);
	(void)res;
}

/* System Page Info & Live Monitor */
static gboolean update_live_stats(gpointer user_data) {
	SettingsApp *app = user_data;

	/* 1. RAM Usage from /proc/meminfo */
	FILE *fp = fopen("/proc/meminfo", "r");
	long total_kb = 0, avail_kb = 0;
	if (fp) {
		char line[256];
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, "MemTotal:", 9) == 0) {
				sscanf(line + 9, "%ld", &total_kb);
			} else if (strncmp(line, "MemAvailable:", 13) == 0) {
				sscanf(line + 13, "%ld", &avail_kb);
			}
		}
		fclose(fp);
	}

	if (total_kb > 0) {
		long used_kb = total_kb - avail_kb;
		double used_gb = (double)used_kb / (1024.0 * 1024.0);
		double total_gb = (double)total_kb / (1024.0 * 1024.0);
		double ratio = (double)used_kb / (double)total_kb;

		char ram_buf[128];
		snprintf(ram_buf, sizeof(ram_buf), "%.2f GB / %.2f GB (%.1f%%)", used_gb, total_gb, ratio * 100.0);
		gtk_label_set_text(GTK_LABEL(app->lbl_ram_usage), ram_buf);
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->ram_progress), ratio);
	}

	/* 2. CPU Usage from /proc/stat */
	fp = fopen("/proc/stat", "r");
	if (fp) {
		char line[256];
		if (fgets(line, sizeof(line), fp)) {
			unsigned long long user, nice, system_val, idle, iowait, irq, softirq;
			if (sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu",
				&user, &nice, &system_val, &idle, &iowait, &irq, &softirq) >= 4) {
				unsigned long long total = user + nice + system_val + idle + iowait + irq + softirq;
				unsigned long long total_diff = total - app->prev_total;
				unsigned long long idle_diff = idle - app->prev_idle;

				if (app->prev_total > 0 && total_diff > 0) {
					double cpu_pct = 100.0 * (double)(total_diff - idle_diff) / (double)total_diff;
					if (cpu_pct < 0.0) cpu_pct = 0.0;
					if (cpu_pct > 100.0) cpu_pct = 100.0;

					char cpu_txt[64];
					snprintf(cpu_txt, sizeof(cpu_txt), "%.1f%%", cpu_pct);
					gtk_label_set_text(GTK_LABEL(app->lbl_cpu_usage), cpu_txt);
					gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->cpu_progress), cpu_pct / 100.0);
				}
				app->prev_total = total;
				app->prev_idle = idle;
			}
		}
		fclose(fp);
	}

	return G_SOURCE_CONTINUE;
}

static void load_system_static_info(SettingsApp *app) {
	FILE *fp = fopen("/proc/cpuinfo", "r");
	char cpu_model[256] = "ARM / x86 Processor";
	int cores = 0;
	if (fp) {
		char line[512];
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, "model name", 10) == 0 || strncmp(line, "Hardware", 8) == 0) {
				char *col = strchr(line, ':');
				if (col) {
					char *val = col + 1;
					while (*val == ' ' || *val == '\t') val++;
					val[strcspn(val, "\r\n")] = '\0';
					snprintf(cpu_model, sizeof(cpu_model), "%s", val);
				}
			}
			if (strncmp(line, "processor", 9) == 0) {
				cores++;
			}
		}
		fclose(fp);
	}
	if (cores == 0) cores = 4;

	struct utsname uts;
	uname(&uts);

	char cpu_buf[512];
	snprintf(cpu_buf, sizeof(cpu_buf), "%s (%s)", cpu_model, uts.machine);
	gtk_label_set_text(GTK_LABEL(app->lbl_cpu_model), cpu_buf);

	char cores_buf[64];
	snprintf(cores_buf, sizeof(cores_buf), "%d Cores", cores);
	gtk_label_set_text(GTK_LABEL(app->lbl_cpu_cores), cores_buf);

	char os_name[512] = "Linux Diamonds Desktop";
	fp = fopen("/etc/os-release", "r");
	if (fp) {
		char line[512];
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
				char *val = line + 12;
				if (*val == '"') val++;
				val[strcspn(val, "\"\r\n")] = '\0';
				snprintf(os_name, sizeof(os_name), "%s", val);
				break;
			}
		}
		fclose(fp);
	}
	gtk_label_set_text(GTK_LABEL(app->lbl_os_name), os_name);

	char kernel_buf[256];
	snprintf(kernel_buf, sizeof(kernel_buf), "%s %s", uts.sysname, uts.release);
	gtk_label_set_text(GTK_LABEL(app->lbl_kernel), kernel_buf);

	update_live_stats(app);
}

/* Animations Page Handlers */
static void on_btn_save_animation_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	SettingsApp *app = user_data;
	const char *active_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->combo_animation));
	if (active_id) {
		save_settings("animation_profile", active_id);
		gtk_label_set_text(GTK_LABEL(app->lbl_status), "✓ Đã lưu cấu hình hoạt ảnh vào ~/.config/diamonds/settings.conf!");
	}
}

static void on_btn_save_appearance_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	SettingsApp *app = user_data;
	int opacity = (int)gtk_range_get_value(GTK_RANGE(app->mica_opacity_slider));
	char op_buf[16];
	snprintf(op_buf, sizeof(op_buf), "%d", opacity);
	save_settings("mica_opacity", op_buf);

	const char *accent = gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->combo_accent));
	if (accent) save_settings("accent_color", accent);

	const char *dock_sz = gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->combo_dock_size));
	if (dock_sz) save_settings("dock_icon_size", dock_sz);

	gtk_label_set_text(GTK_LABEL(app->lbl_appearance_status),
		"✓ Đã lưu giao diện Mica & Dock vào ~/.config/diamonds/settings.conf!");
}

static void on_btn_save_input_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	SettingsApp *app = user_data;
	char buf[32];
	snprintf(buf, sizeof(buf), "%d", (int)gtk_range_get_value(GTK_RANGE(app->kb_rate_slider)));
	save_settings("kb_repeat_rate", buf);
	snprintf(buf, sizeof(buf), "%d", (int)gtk_range_get_value(GTK_RANGE(app->kb_delay_slider)));
	save_settings("kb_repeat_delay", buf);
	snprintf(buf, sizeof(buf), "%.2f", gtk_range_get_value(GTK_RANGE(app->mouse_speed_slider)));
	save_settings("mouse_speed", buf);
	save_settings("natural_scroll",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_natural_scroll)) ? "true" : "false");

	gtk_label_set_text(GTK_LABEL(app->lbl_input_status),
		"✓ Đã lưu cấu hình Phím & Chuột vào ~/.config/diamonds/settings.conf!");
}

static void on_launch_ecosystem_app(GtkButton *btn, gpointer user_data) {
	(void)btn;
	const char *bin = (const char *)user_data;
	if (!bin) return;
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "(./build/%s || %s) >/dev/null 2>&1 &", bin, bin);
	int r = system(cmd);
	(void)r;
}

/* ================= NETWORK & INTERNET HANDLERS (ETHERNET + WI-FI) ================= */
static void on_wifi_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
	(void)box;
	SettingsApp *app = user_data;
	if (!row) return;
	const char *ssid = g_object_get_data(G_OBJECT(row), "wifi-ssid");
	if (ssid && *ssid && app->entry_wifi_ssid) {
		gtk_entry_set_text(GTK_ENTRY(app->entry_wifi_ssid), ssid);
	}
}

static void refresh_network_status(SettingsApp *app) {
	if (!app) return;
	snprintf(app->eth_iface, sizeof(app->eth_iface), "eth0");
	snprintf(app->wifi_iface, sizeof(app->wifi_iface), "wlan0");

	char eth_state[64] = "Chưa cắm cáp";
	char eth_ip[64] = "Chưa cấp IP";
	char eth_mac[64] = "N/A";
	bool eth_up = false;

	char wifi_state[128] = "Chưa kết nối";
	char wifi_ip[64] = "Chưa cấp IP";
	bool wifi_up = false;

	DIR *d = opendir("/sys/class/net");
	if (d) {
		struct dirent *ent;
		while ((ent = readdir(d)) != NULL) {
			const char *n = ent->d_name;
			if (strncmp(n, "eth", 3) == 0 || strncmp(n, "enp", 3) == 0 || strncmp(n, "eno", 3) == 0) {
				snprintf(app->eth_iface, sizeof(app->eth_iface), "%.31s", n);
				char p[512];
				snprintf(p, sizeof(p), "/sys/class/net/%s/address", n);
				FILE *fp = fopen(p, "r");
				if (fp) {
					if (fgets(eth_mac, sizeof(eth_mac), fp))
						eth_mac[strcspn(eth_mac, "\r\n")] = '\0';
					fclose(fp);
				}
			} else if (strncmp(n, "wl", 2) == 0) {
				snprintf(app->wifi_iface, sizeof(app->wifi_iface), "%.31s", n);
			}
		}
		closedir(d);
	}

	FILE *ip_fp = popen("ip -4 -br addr 2>/dev/null", "r");
	if (ip_fp) {
		char line[256];
		while (fgets(line, sizeof(line), ip_fp)) {
			char iface[64] = {0}, st[64] = {0}, cidr[64] = {0};
			if (sscanf(line, "%63s %63s %63s", iface, st, cidr) >= 2) {
				if (strncmp(iface, "eth", 3) == 0 || strncmp(iface, "en", 2) == 0) {
					snprintf(app->eth_iface, sizeof(app->eth_iface), "%s", iface);
					if (strcasecmp(st, "UP") == 0 || cidr[0]) {
						eth_up = true;
						snprintf(eth_state, sizeof(eth_state), "Đã kết nối (%s - Cáp RJ45)", iface);
						if (cidr[0]) snprintf(eth_ip, sizeof(eth_ip), "%s", cidr);
					}
				} else if (strncmp(iface, "wl", 2) == 0) {
					snprintf(app->wifi_iface, sizeof(app->wifi_iface), "%s", iface);
					if (strcasecmp(st, "UP") == 0 || cidr[0]) {
						wifi_up = true;
						if (cidr[0]) snprintf(wifi_ip, sizeof(wifi_ip), "%s", cidr);
					}
				}
			}
		}
		pclose(ip_fp);
	}

	FILE *w_fp = popen("nmcli -t -f active,ssid,signal dev wifi 2>/dev/null | grep '^yes:' | head -n1", "r");
	if (w_fp) {
		char buf[128] = {0};
		if (fgets(buf, sizeof(buf), w_fp)) {
			buf[strcspn(buf, "\r\n")] = '\0';
			char *p1 = strchr(buf, ':');
			if (p1) {
				p1++;
				char *p2 = strrchr(p1, ':');
				if (p2) {
					*p2 = '\0';
					snprintf(wifi_state, sizeof(wifi_state), "Đã kết nối: %s (Tín hiệu %s%%)", p1, p2 + 1);
					wifi_up = true;
				} else if (*p1) {
					snprintf(wifi_state, sizeof(wifi_state), "Đã kết nối: %s", p1);
					wifi_up = true;
				}
			}
		}
		pclose(w_fp);
	}

	if (app->lbl_eth_status) {
		char markup[256];
		snprintf(markup, sizeof(markup), "<b>Trạng thái:</b> <span foreground='%s'>%s</span>",
			eth_up ? "#34d399" : "#94a3b8", eth_state);
		gtk_label_set_markup(GTK_LABEL(app->lbl_eth_status), markup);
	}
	if (app->lbl_eth_ip) {
		char buf[128];
		snprintf(buf, sizeof(buf), "Địa chỉ IPv4: %s   |   MAC: %s", eth_ip, eth_mac);
		gtk_label_set_text(GTK_LABEL(app->lbl_eth_ip), buf);
	}
	if (app->lbl_wifi_status) {
		char markup[256];
		snprintf(markup, sizeof(markup), "<b>Trạng thái Wi-Fi (%s):</b> <span foreground='%s'>%s</span>",
			app->wifi_iface, wifi_up ? "#38bdf8" : "#94a3b8", wifi_state);
		gtk_label_set_markup(GTK_LABEL(app->lbl_wifi_status), markup);
	}
	if (app->lbl_wifi_ip) {
		char buf[128];
		snprintf(buf, sizeof(buf), "Địa chỉ IPv4 không dây: %s", wifi_ip);
		gtk_label_set_text(GTK_LABEL(app->lbl_wifi_ip), buf);
	}
}

static void scan_wifi_networks(SettingsApp *app) {
	if (!app || !app->wifi_list_box) return;
	GList *children = gtk_container_get_children(GTK_CONTAINER(app->wifi_list_box));
	for (GList *iter = children; iter != NULL; iter = g_list_next(iter)) {
		gtk_widget_destroy(GTK_WIDGET(iter->data));
	}
	g_list_free(children);

	FILE *fp = popen("nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY dev wifi list --rescan auto 2>/dev/null | head -n 15", "r");
	int count = 0;
	if (fp) {
		char line[256];
		while (fgets(line, sizeof(line), fp)) {
			line[strcspn(line, "\r\n")] = '\0';
			char *in_use = line;
			char *ssid = strchr(in_use, ':');
			if (!ssid) continue;
			*ssid++ = '\0';
			char *sig = strchr(ssid, ':');
			if (!sig) continue;
			*sig++ = '\0';
			char *sec = strchr(sig, ':');
			if (sec) *sec++ = '\0';
			else sec = "Open";
			if (!*ssid) continue;

			bool active = (in_use[0] == '*');
			char row_txt[256];
			snprintf(row_txt, sizeof(row_txt), "%s %s   •   Tín hiệu: %s%%   •   Bảo mật: %s",
				active ? "✔ [Đang kết nối]" : "📶", ssid, sig, (*sec ? sec : "Mở"));

			GtkWidget *row = gtk_list_box_row_new();
			g_object_set_data_full(G_OBJECT(row), "wifi-ssid", g_strdup(ssid), g_free);
			GtkWidget *lbl = gtk_label_new(row_txt);
			gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
			gtk_widget_set_margin_start(lbl, 10);
			gtk_widget_set_margin_end(lbl, 10);
			gtk_widget_set_margin_top(lbl, 6);
			gtk_widget_set_margin_bottom(lbl, 6);
			gtk_container_add(GTK_CONTAINER(row), lbl);
			gtk_container_add(GTK_CONTAINER(app->wifi_list_box), row);
			count++;
		}
		pclose(fp);
	}
	if (count == 0) {
		GtkWidget *lbl = gtk_label_new("Không tìm thấy mạng Wi-Fi mới (hoặc đã kết nối sẵn).");
		gtk_widget_set_margin_top(lbl, 8);
		gtk_widget_set_margin_bottom(lbl, 8);
		gtk_container_add(GTK_CONTAINER(app->wifi_list_box), lbl);
	}
	gtk_widget_show_all(app->wifi_list_box);
	refresh_network_status(app);
}

static void on_btn_eth_connect(GtkButton *btn, gpointer user_data) {
	(void)btn;
	SettingsApp *app = user_data;
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "nmcli dev connect %s >/dev/null 2>&1", app->eth_iface);
	int r = system(cmd);
	(void)r;
	refresh_network_status(app);
	gtk_label_set_text(GTK_LABEL(app->lbl_net_action_status), "✓ Đã kích hoạt kết nối mạng dây Ethernet.");
}

static void on_btn_eth_disconnect(GtkButton *btn, gpointer user_data) {
	(void)btn;
	SettingsApp *app = user_data;
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "nmcli dev disconnect %s >/dev/null 2>&1", app->eth_iface);
	int r = system(cmd);
	(void)r;
	refresh_network_status(app);
	gtk_label_set_text(GTK_LABEL(app->lbl_net_action_status), "Đã tạm ngắt kết nối mạng dây Ethernet.");
}

static void on_btn_wifi_toggle_on(GtkButton *btn, gpointer user_data) {
	(void)btn;
	SettingsApp *app = user_data;
	int r = system("nmcli radio wifi on >/dev/null 2>&1");
	(void)r;
	refresh_network_status(app);
	gtk_label_set_text(GTK_LABEL(app->lbl_net_action_status), "✓ Đã bật sóng Wi-Fi.");
}

static void on_btn_wifi_toggle_off(GtkButton *btn, gpointer user_data) {
	(void)btn;
	SettingsApp *app = user_data;
	int r = system("nmcli radio wifi off >/dev/null 2>&1");
	(void)r;
	refresh_network_status(app);
	gtk_label_set_text(GTK_LABEL(app->lbl_net_action_status), "Đã tắt sóng Wi-Fi.");
}

static void on_btn_wifi_scan(GtkButton *btn, gpointer user_data) {
	(void)btn;
	SettingsApp *app = user_data;
	scan_wifi_networks(app);
	gtk_label_set_text(GTK_LABEL(app->lbl_net_action_status), "✓ Đã làm mới danh sách mạng dây & Wi-Fi.");
}

static void on_btn_wifi_connect(GtkButton *btn, gpointer user_data) {
	(void)btn;
	SettingsApp *app = user_data;
	const char *ssid = gtk_entry_get_text(GTK_ENTRY(app->entry_wifi_ssid));
	const char *pass = gtk_entry_get_text(GTK_ENTRY(app->entry_wifi_pass));
	if (!ssid || !*ssid) {
		gtk_label_set_text(GTK_LABEL(app->lbl_net_action_status), "⚠ Vui lòng chọn hoặc nhập tên mạng Wi-Fi (SSID)!");
		return;
	}
	char cmd[512];
	if (pass && *pass) {
		snprintf(cmd, sizeof(cmd), "nmcli dev wifi connect \"%s\" password \"%s\" >/dev/null 2>&1 &", ssid, pass);
	} else {
		snprintf(cmd, sizeof(cmd), "nmcli dev wifi connect \"%s\" >/dev/null 2>&1 &", ssid);
	}
	int r = system(cmd);
	(void)r;
	char msg[256];
	snprintf(msg, sizeof(msg), "Đang kết nối tới mạng Wi-Fi \"%s\"...", ssid);
	gtk_label_set_text(GTK_LABEL(app->lbl_net_action_status), msg);
}

/* ================= BLUETOOTH HANDLERS ================= */
static void refresh_bluetooth_devices(SettingsApp *app) {
	if (!app || !app->bt_list_box) return;
	GList *children = gtk_container_get_children(GTK_CONTAINER(app->bt_list_box));
	for (GList *iter = children; iter != NULL; iter = g_list_next(iter)) {
		gtk_widget_destroy(GTK_WIDGET(iter->data));
	}
	g_list_free(children);

	FILE *fp = popen("bluetoothctl devices 2>/dev/null | head -n 12", "r");
	int count = 0;
	if (fp) {
		char line[256];
		while (fgets(line, sizeof(line), fp)) {
			line[strcspn(line, "\r\n")] = '\0';
			if (!*line) continue;
			GtkWidget *lbl = gtk_label_new(line);
			gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
			gtk_widget_set_margin_start(lbl, 10);
			gtk_widget_set_margin_top(lbl, 6);
			gtk_widget_set_margin_bottom(lbl, 6);
			gtk_container_add(GTK_CONTAINER(app->bt_list_box), lbl);
			count++;
		}
		pclose(fp);
	}
	if (count == 0) {
		GtkWidget *lbl = gtk_label_new("Chưa có thiết bị Bluetooth nào được ghép đôi.");
		gtk_widget_set_margin_top(lbl, 8);
		gtk_widget_set_margin_bottom(lbl, 8);
		gtk_container_add(GTK_CONTAINER(app->bt_list_box), lbl);
	}
	gtk_widget_show_all(app->bt_list_box);
}

static void on_btn_bt_on(GtkButton *btn, gpointer user_data) {
	(void)btn;
	SettingsApp *app = user_data;
	int r = system("rfkill unblock bluetooth >/dev/null 2>&1; bluetoothctl power on >/dev/null 2>&1 &");
	(void)r;
	gtk_label_set_text(GTK_LABEL(app->lbl_bt_status), "Trạng thái Bluetooth: Đang bật (Sẵn sàng ghép đôi)");
	refresh_bluetooth_devices(app);
}

static void on_btn_bt_off(GtkButton *btn, gpointer user_data) {
	(void)btn;
	SettingsApp *app = user_data;
	int r = system("bluetoothctl power off >/dev/null 2>&1; rfkill block bluetooth >/dev/null 2>&1 &");
	(void)r;
	gtk_label_set_text(GTK_LABEL(app->lbl_bt_status), "Trạng thái Bluetooth: Đã tắt");
}

/* ================= POWER & STORAGE & DATETIME HANDLERS ================= */
static void on_brightness_changed(GtkRange *range, gpointer user_data) {
	(void)user_data;
	int pct = (int)gtk_range_get_value(range);
	char cmd[256];
	snprintf(cmd, sizeof(cmd),
		"brightnessctl set %d%% >/dev/null 2>&1 || "
		"(echo $(( %d * 255 / 100 )) | tee /sys/class/backlight/*/brightness) >/dev/null 2>&1 &",
		pct, pct);
	int r = system(cmd);
	(void)r;
}

static void on_btn_save_power(GtkButton *btn, gpointer user_data) {
	(void)btn;
	SettingsApp *app = user_data;
	const char *prof = gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->combo_power_profile));
	const char *timeout = gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->combo_screen_timeout));
	if (prof) {
		save_settings("power_profile", prof);
		char cmd[128];
		snprintf(cmd, sizeof(cmd), "powerprofilesctl set %s >/dev/null 2>&1 &", prof);
		int r = system(cmd);
		(void)r;
	}
	if (timeout) {
		save_settings("screen_timeout", timeout);
	}
	gtk_label_set_text(GTK_LABEL(app->lbl_power_status), "✓ Đã áp dụng cấu hình Nguồn điện & Tiết kiệm năng lượng!");
}

static void on_btn_power_session(GtkButton *btn, gpointer user_data) {
	(void)btn;
	const char *action = (const char *)user_data;
	if (!action) return;
	int r = system(action);
	(void)r;
}

static void refresh_storage_stats(SettingsApp *app) {
	struct statvfs vfs;
	if (statvfs("/", &vfs) == 0 && vfs.f_blocks > 0) {
		double total = (double)vfs.f_blocks * vfs.f_frsize / (1024.0 * 1024.0 * 1024.0);
		double avail = (double)vfs.f_bavail * vfs.f_frsize / (1024.0 * 1024.0 * 1024.0);
		double used = total - avail;
		double ratio = total > 0 ? (used / total) : 0.0;
		char buf[160];
		snprintf(buf, sizeof(buf), "Phân vùng Hệ thống (/): Đã dùng %.2f GB / %.2f GB (Trống %.2f GB - %.1f%%)",
			used, total, avail, ratio * 100.0);
		gtk_label_set_text(GTK_LABEL(app->lbl_disk_root), buf);
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->bar_disk_root), ratio);
	}
	if (statvfs("/mnt/home_a", &vfs) == 0 && vfs.f_blocks > 0) {
		double total = (double)vfs.f_blocks * vfs.f_frsize / (1024.0 * 1024.0 * 1024.0);
		double avail = (double)vfs.f_bavail * vfs.f_frsize / (1024.0 * 1024.0 * 1024.0);
		double used = total - avail;
		double ratio = total > 0 ? (used / total) : 0.0;
		char buf[160];
		snprintf(buf, sizeof(buf), "Phân vùng Dữ liệu (/mnt/home_a): Đã dùng %.2f GB / %.2f GB (Trống %.2f GB - %.1f%%)",
			used, total, avail, ratio * 100.0);
		gtk_label_set_text(GTK_LABEL(app->lbl_disk_home), buf);
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->bar_disk_home), ratio);
	}
}

static void on_btn_save_datetime(GtkButton *btn, gpointer user_data) {
	(void)btn;
	SettingsApp *app = user_data;
	const char *tz = gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->combo_timezone));
	gboolean ntp = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_ntp));
	if (tz) save_settings("timezone", tz);
	save_settings("ntp_sync", ntp ? "true" : "false");
	gtk_label_set_text(GTK_LABEL(app->lbl_datetime_status), "✓ Đã lưu cấu hình Ngày, Giờ & Múi giờ hệ thống!");
}

int main(int argc, char **argv) {
	g_set_prgname("diamonds-settings");
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

	SettingsApp app = {0};

	app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(app.window), "Diamonds Settings");
	gtk_window_set_default_size(GTK_WINDOW(app.window), 860, 540);
	g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	/* HeaderBar */
	app.header_bar = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(app.header_bar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(app.header_bar), "Diamonds Settings");
	gtk_header_bar_set_subtitle(GTK_HEADER_BAR(app.header_bar), "Trung tâm Cài đặt Hệ thống (Wayland)");
	gtk_window_set_titlebar(GTK_WINDOW(app.window), app.header_bar);

	/* Main horizontal box */
	GtkWidget *main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add(GTK_CONTAINER(app.window), main_hbox);

	/* Left Sidebar inside ScrolledWindow */
	GtkWidget *sidebar_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request(sidebar_scroll, 220, -1);

	GtkWidget *sidebar = gtk_stack_sidebar_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(sidebar), "sidebar");
	gtk_container_add(GTK_CONTAINER(sidebar_scroll), sidebar);
	gtk_box_pack_start(GTK_BOX(main_hbox), sidebar_scroll, FALSE, FALSE, 0);

	/* Right Content Stack inside ScrolledWindow */
	GtkWidget *content_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(content_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

	app.stack = gtk_stack_new();
	gtk_stack_set_transition_type(GTK_STACK(app.stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
	gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar), GTK_STACK(app.stack));
	gtk_container_add(GTK_CONTAINER(content_scroll), app.stack);
	gtk_box_pack_start(GTK_BOX(main_hbox), content_scroll, TRUE, TRUE, 0);

	/* ================= PAGE 0A: Network & Internet (Ethernet + Wi-Fi) ================= */
	GtkWidget *page_net = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_style_context_add_class(gtk_widget_get_style_context(page_net), "page-content");

	/* Card 1: Wired Ethernet (Mạng cắm dây RJ45) */
	GtkWidget *card_eth = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_style_context_add_class(gtk_widget_get_style_context(card_eth), "card");

	GtkWidget *lbl_eth_title = gtk_label_new("🔌 Mạng cắm dây (Wired Ethernet - LAN RJ45)");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_eth_title), "card-title");
	gtk_label_set_xalign(GTK_LABEL(lbl_eth_title), 0.0);
	gtk_box_pack_start(GTK_BOX(card_eth), lbl_eth_title, FALSE, FALSE, 0);

	app.lbl_eth_status = gtk_label_new("Đang kiểm tra cổng mạng dây...");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_eth_status), 0.0);
	gtk_box_pack_start(GTK_BOX(card_eth), app.lbl_eth_status, FALSE, FALSE, 0);

	app.lbl_eth_ip = gtk_label_new("Địa chỉ IPv4: ...");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_eth_ip), 0.0);
	gtk_box_pack_start(GTK_BOX(card_eth), app.lbl_eth_ip, FALSE, FALSE, 0);

	GtkWidget *eth_btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	GtkWidget *btn_eth_on = gtk_button_new_with_label("Kết nối Mạng dây");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_eth_on), "action-btn");
	g_signal_connect(btn_eth_on, "clicked", G_CALLBACK(on_btn_eth_connect), &app);
	GtkWidget *btn_eth_off = gtk_button_new_with_label("Ngắt Mạng dây");
	g_signal_connect(btn_eth_off, "clicked", G_CALLBACK(on_btn_eth_disconnect), &app);
	gtk_box_pack_start(GTK_BOX(eth_btn_box), btn_eth_on, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(eth_btn_box), btn_eth_off, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(card_eth), eth_btn_box, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(page_net), card_eth, FALSE, FALSE, 0);

	/* Card 2: Wireless Wi-Fi (Mạng không dây) */
	GtkWidget *card_wifi = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_style_context_add_class(gtk_widget_get_style_context(card_wifi), "card");

	GtkWidget *lbl_wifi_title = gtk_label_new("📶 Mạng không dây (Wireless Wi-Fi 802.11)");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_wifi_title), "card-title");
	gtk_label_set_xalign(GTK_LABEL(lbl_wifi_title), 0.0);
	gtk_box_pack_start(GTK_BOX(card_wifi), lbl_wifi_title, FALSE, FALSE, 0);

	app.lbl_wifi_status = gtk_label_new("Đang kiểm tra trạng thái Wi-Fi...");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_wifi_status), 0.0);
	gtk_box_pack_start(GTK_BOX(card_wifi), app.lbl_wifi_status, FALSE, FALSE, 0);

	app.lbl_wifi_ip = gtk_label_new("Địa chỉ IPv4 không dây: ...");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_wifi_ip), 0.0);
	gtk_box_pack_start(GTK_BOX(card_wifi), app.lbl_wifi_ip, FALSE, FALSE, 0);

	GtkWidget *wifi_ctrl_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	GtkWidget *btn_wifi_on = gtk_button_new_with_label("Bật Wi-Fi");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_wifi_on), "action-btn");
	g_signal_connect(btn_wifi_on, "clicked", G_CALLBACK(on_btn_wifi_toggle_on), &app);
	GtkWidget *btn_wifi_off = gtk_button_new_with_label("Tắt Wi-Fi");
	g_signal_connect(btn_wifi_off, "clicked", G_CALLBACK(on_btn_wifi_toggle_off), &app);
	GtkWidget *btn_wifi_scan = gtk_button_new_with_label("🔄 Quét mạng Wi-Fi");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_wifi_scan), "action-btn");
	g_signal_connect(btn_wifi_scan, "clicked", G_CALLBACK(on_btn_wifi_scan), &app);
	gtk_box_pack_start(GTK_BOX(wifi_ctrl_box), btn_wifi_on, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(wifi_ctrl_box), btn_wifi_off, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(wifi_ctrl_box), btn_wifi_scan, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(card_wifi), wifi_ctrl_box, FALSE, FALSE, 0);

	GtkWidget *wifi_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_size_request(wifi_scroll, -1, 130);
	app.wifi_list_box = gtk_list_box_new();
	g_signal_connect(app.wifi_list_box, "row-selected", G_CALLBACK(on_wifi_row_selected), &app);
	gtk_container_add(GTK_CONTAINER(wifi_scroll), app.wifi_list_box);
	gtk_box_pack_start(GTK_BOX(card_wifi), wifi_scroll, FALSE, FALSE, 0);

	GtkWidget *conn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	app.entry_wifi_ssid = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_wifi_ssid), "Tên mạng Wi-Fi (SSID)...");
	app.entry_wifi_pass = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(app.entry_wifi_pass), "Mật khẩu Wi-Fi...");
	gtk_entry_set_visibility(GTK_ENTRY(app.entry_wifi_pass), FALSE);
	GtkWidget *btn_wifi_conn = gtk_button_new_with_label("Kết nối Wi-Fi");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_wifi_conn), "action-btn");
	g_signal_connect(btn_wifi_conn, "clicked", G_CALLBACK(on_btn_wifi_connect), &app);
	gtk_box_pack_start(GTK_BOX(conn_box), app.entry_wifi_ssid, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(conn_box), app.entry_wifi_pass, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(conn_box), btn_wifi_conn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(card_wifi), conn_box, FALSE, FALSE, 0);

	app.lbl_net_action_status = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_net_action_status), 0.0);
	gtk_box_pack_start(GTK_BOX(card_wifi), app.lbl_net_action_status, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(page_net), card_wifi, FALSE, FALSE, 0);
	gtk_stack_add_titled(GTK_STACK(app.stack), page_net, "network", "Mạng & Internet");

	refresh_network_status(&app);
	scan_wifi_networks(&app);

	/* ================= PAGE 0B: Bluetooth ================= */
	GtkWidget *page_bt = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_style_context_add_class(gtk_widget_get_style_context(page_bt), "page-content");

	GtkWidget *card_bt = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(card_bt), "card");

	GtkWidget *lbl_bt_title = gtk_label_new("Thiết bị Không dây Bluetooth");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_bt_title), "card-title");
	gtk_label_set_xalign(GTK_LABEL(lbl_bt_title), 0.0);
	gtk_box_pack_start(GTK_BOX(card_bt), lbl_bt_title, FALSE, FALSE, 0);

	app.lbl_bt_status = gtk_label_new("Trạng thái Bluetooth: Đang bật");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_bt_status), 0.0);
	gtk_box_pack_start(GTK_BOX(card_bt), app.lbl_bt_status, FALSE, FALSE, 0);

	GtkWidget *bt_btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	GtkWidget *btn_bt_on = gtk_button_new_with_label("Bật Bluetooth & Quét");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_bt_on), "action-btn");
	g_signal_connect(btn_bt_on, "clicked", G_CALLBACK(on_btn_bt_on), &app);
	GtkWidget *btn_bt_off = gtk_button_new_with_label("Tắt Bluetooth");
	g_signal_connect(btn_bt_off, "clicked", G_CALLBACK(on_btn_bt_off), &app);
	gtk_box_pack_start(GTK_BOX(bt_btn_box), btn_bt_on, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bt_btn_box), btn_bt_off, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(card_bt), bt_btn_box, FALSE, FALSE, 0);

	app.bt_list_box = gtk_list_box_new();
	gtk_box_pack_start(GTK_BOX(card_bt), app.bt_list_box, FALSE, FALSE, 0);
	refresh_bluetooth_devices(&app);

	gtk_box_pack_start(GTK_BOX(page_bt), card_bt, FALSE, FALSE, 0);
	gtk_stack_add_titled(GTK_STACK(app.stack), page_bt, "bluetooth", "Bluetooth");

	/* ================= PAGE 1: Personalization (Wallpaper) ================= */
	GtkWidget *page_wall = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_style_context_add_class(gtk_widget_get_style_context(page_wall), "page-content");

	GtkWidget *card_wall = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(card_wall), "card");

	GtkWidget *lbl_w_title = gtk_label_new("Hình nền Desktop (Wallpaper)");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_w_title), "card-title");
	gtk_label_set_xalign(GTK_LABEL(lbl_w_title), 0.0);
	gtk_box_pack_start(GTK_BOX(card_wall), lbl_w_title, FALSE, FALSE, 0);

	app.wallpaper_preview = gtk_image_new();
	gtk_widget_set_size_request(app.wallpaper_preview, 360, 200);
	gtk_box_pack_start(GTK_BOX(card_wall), app.wallpaper_preview, FALSE, FALSE, 0);

	app.wallpaper_path_label = gtk_label_new("Chưa chọn hình nền");
	gtk_label_set_xalign(GTK_LABEL(app.wallpaper_path_label), 0.0);
	gtk_label_set_line_wrap(GTK_LABEL(app.wallpaper_path_label), TRUE);
	gtk_box_pack_start(GTK_BOX(card_wall), app.wallpaper_path_label, FALSE, FALSE, 0);

	GtkWidget *btn_browse_wall = gtk_button_new_with_label("Chọn ảnh từ máy...");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_browse_wall), "action-btn");
	g_signal_connect(btn_browse_wall, "clicked", G_CALLBACK(on_btn_browse_wallpaper_clicked), &app);
	gtk_box_pack_start(GTK_BOX(card_wall), btn_browse_wall, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(page_wall), card_wall, FALSE, FALSE, 0);
	gtk_stack_add_titled(GTK_STACK(app.stack), page_wall, "wallpaper", "Personalization");

	char saved_wall[1024];
	load_setting("wallpaper", saved_wall, sizeof(saved_wall), "/usr/share/backgrounds/default.jpg");
	update_wallpaper_preview(&app, saved_wall);

	/* ================= PAGE 2: Display & Resolution ================= */
	GtkWidget *page_display = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_style_context_add_class(gtk_widget_get_style_context(page_display), "page-content");

	GtkWidget *card_display = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(card_display), "card");

	GtkWidget *lbl_d_title = gtk_label_new("Độ phân giải màn hình (Display Resolution)");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_d_title), "card-title");
	gtk_label_set_xalign(GTK_LABEL(lbl_d_title), 0.0);
	gtk_box_pack_start(GTK_BOX(card_display), lbl_d_title, FALSE, FALSE, 0);

	app.combo_resolution = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_resolution), "1920x1080", "1920 x 1080 (16:9 Full HD)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_resolution), "1600x900", "1600 x 900 (16:9 HD+)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_resolution), "1366x768", "1366 x 768 (16:9 HD Standard)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_resolution), "1280x720", "1280 x 720 (16:9 720p)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_resolution), "1024x768", "1024 x 768 (4:3 XGA)");

	char saved_res[64];
	load_setting("resolution", saved_res, sizeof(saved_res), "1920x1080");
	gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.combo_resolution), saved_res);
	gtk_box_pack_start(GTK_BOX(card_display), app.combo_resolution, FALSE, FALSE, 0);

	GtkWidget *btn_apply_res = gtk_button_new_with_label("Áp dụng độ phân giải");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_apply_res), "action-btn");
	g_signal_connect(btn_apply_res, "clicked", G_CALLBACK(on_btn_apply_resolution_clicked), &app);
	gtk_box_pack_start(GTK_BOX(card_display), btn_apply_res, FALSE, FALSE, 0);

	app.lbl_resolution_status = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_resolution_status), 0.0);
	gtk_box_pack_start(GTK_BOX(card_display), app.lbl_resolution_status, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(page_display), card_display, FALSE, FALSE, 0);
	gtk_stack_add_titled(GTK_STACK(app.stack), page_display, "display", "Display");

	/* ================= PAGE 3: Sound & Audio ================= */
	GtkWidget *page_audio = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_style_context_add_class(gtk_widget_get_style_context(page_audio), "page-content");

	GtkWidget *card_audio = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
	gtk_style_context_add_class(gtk_widget_get_style_context(card_audio), "card");

	GtkWidget *lbl_a_title = gtk_label_new("Âm thanh & Đầu ra (Sound & Volume)");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_a_title), "card-title");
	gtk_label_set_xalign(GTK_LABEL(lbl_a_title), 0.0);
	gtk_box_pack_start(GTK_BOX(card_audio), lbl_a_title, FALSE, FALSE, 0);

	GtkWidget *slider_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	app.volume_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
	gtk_widget_set_size_request(app.volume_slider, 300, -1);
	int cur_vol = read_current_volume();
	gtk_range_set_value(GTK_RANGE(app.volume_slider), cur_vol);
	g_signal_connect(app.volume_slider, "value-changed", G_CALLBACK(on_volume_slider_changed), &app);
	gtk_box_pack_start(GTK_BOX(slider_box), app.volume_slider, TRUE, TRUE, 0);

	char vol_txt[32];
	snprintf(vol_txt, sizeof(vol_txt), "%d%%", cur_vol);
	app.volume_label = gtk_label_new(vol_txt);
	gtk_widget_set_size_request(app.volume_label, 50, -1);
	gtk_box_pack_start(GTK_BOX(slider_box), app.volume_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(card_audio), slider_box, FALSE, FALSE, 0);

	app.mute_btn = gtk_check_button_new_with_label("Tắt tiếng (Mute Audio)");
	g_signal_connect(app.mute_btn, "toggled", G_CALLBACK(on_mute_btn_toggled), &app);
	gtk_box_pack_start(GTK_BOX(card_audio), app.mute_btn, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(page_audio), card_audio, FALSE, FALSE, 0);
	gtk_stack_add_titled(GTK_STACK(app.stack), page_audio, "sound", "Sound & Audio");

	/* ================= PAGE 4: System Information ================= */
	GtkWidget *page_sys = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_style_context_add_class(gtk_widget_get_style_context(page_sys), "page-content");

	GtkWidget *card_sys = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(card_sys), "card");

	GtkWidget *lbl_s_title = gtk_label_new("Thông số phần cứng & Hiệu năng (Live Monitor)");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_s_title), "card-title");
	gtk_label_set_xalign(GTK_LABEL(lbl_s_title), 0.0);
	gtk_box_pack_start(GTK_BOX(card_sys), lbl_s_title, FALSE, FALSE, 0);

	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 16);

	/* Labels */
	GtkWidget *l1 = gtk_label_new("Processor:");
	gtk_label_set_xalign(GTK_LABEL(l1), 0.0);
	app.lbl_cpu_model = gtk_label_new("Loading...");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_cpu_model), 0.0);
	gtk_grid_attach(GTK_GRID(grid), l1, 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), app.lbl_cpu_model, 1, 0, 1, 1);

	GtkWidget *l2 = gtk_label_new("Cores:");
	gtk_label_set_xalign(GTK_LABEL(l2), 0.0);
	app.lbl_cpu_cores = gtk_label_new("Loading...");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_cpu_cores), 0.0);
	gtk_grid_attach(GTK_GRID(grid), l2, 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), app.lbl_cpu_cores, 1, 1, 1, 1);

	GtkWidget *l_cpu_u = gtk_label_new("CPU Usage:");
	gtk_label_set_xalign(GTK_LABEL(l_cpu_u), 0.0);
	app.lbl_cpu_usage = gtk_label_new("0.0%");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_cpu_usage), 0.0);
	gtk_grid_attach(GTK_GRID(grid), l_cpu_u, 0, 2, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), app.lbl_cpu_usage, 1, 2, 1, 1);

	app.cpu_progress = gtk_progress_bar_new();
	gtk_grid_attach(GTK_GRID(grid), app.cpu_progress, 1, 3, 1, 1);

	GtkWidget *l3 = gtk_label_new("RAM Usage (/proc/meminfo):");
	gtk_label_set_xalign(GTK_LABEL(l3), 0.0);
	app.lbl_ram_usage = gtk_label_new("Loading...");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_ram_usage), 0.0);
	gtk_grid_attach(GTK_GRID(grid), l3, 0, 4, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), app.lbl_ram_usage, 1, 4, 1, 1);

	app.ram_progress = gtk_progress_bar_new();
	gtk_grid_attach(GTK_GRID(grid), app.ram_progress, 1, 5, 1, 1);

	GtkWidget *l4 = gtk_label_new("OS Name:");
	gtk_label_set_xalign(GTK_LABEL(l4), 0.0);
	app.lbl_os_name = gtk_label_new("Loading...");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_os_name), 0.0);
	gtk_grid_attach(GTK_GRID(grid), l4, 0, 6, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), app.lbl_os_name, 1, 6, 1, 1);

	GtkWidget *l5 = gtk_label_new("Kernel:");
	gtk_label_set_xalign(GTK_LABEL(l5), 0.0);
	app.lbl_kernel = gtk_label_new("Loading...");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_kernel), 0.0);
	gtk_grid_attach(GTK_GRID(grid), l5, 0, 7, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), app.lbl_kernel, 1, 7, 1, 1);

	gtk_box_pack_start(GTK_BOX(card_sys), grid, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(page_sys), card_sys, FALSE, FALSE, 0);
	gtk_stack_add_titled(GTK_STACK(app.stack), page_sys, "system", "System Info");

	load_system_static_info(&app);
	g_timeout_add(1000, update_live_stats, &app);

	/* ================= PAGE 5: Window Animations ================= */
	GtkWidget *page_anim = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_style_context_add_class(gtk_widget_get_style_context(page_anim), "page-content");

	GtkWidget *card_anim = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
	gtk_style_context_add_class(gtk_widget_get_style_context(card_anim), "card");

	GtkWidget *lbl_an_title = gtk_label_new("Hiệu ứng cửa sổ & Motion Profile");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_an_title), "card-title");
	gtk_label_set_xalign(GTK_LABEL(lbl_an_title), 0.0);
	gtk_box_pack_start(GTK_BOX(card_anim), lbl_an_title, FALSE, FALSE, 0);

	GtkWidget *lbl_an_desc = gtk_label_new(
		"Chọn giải thuật toán học điều khiển animation mở/đóng và snap cửa sổ trong Diamonds Compositor:");
	gtk_label_set_xalign(GTK_LABEL(lbl_an_desc), 0.0);
	gtk_label_set_line_wrap(GTK_LABEL(lbl_an_desc), TRUE);
	gtk_box_pack_start(GTK_BOX(card_anim), lbl_an_desc, FALSE, FALSE, 0);

	app.combo_animation = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_animation),
		"cubic_bezier", "1. Cubic Bezier Ease-Out (Mặc định - Phản hồi nhanh)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_animation),
		"spring_physics", "2. Spring Physics (Độ nảy đàn hồi phong cách macOS)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_animation),
		"fluent_snap", "3. Fluent Snap (Lướt êm ái phong cách Windows 11)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_animation),
		"fade_scale", "4. Fade & Scale (Thu nhỏ & Mờ dần tối giản)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_animation),
		"instant", "5. Instant (Không hiệu ứng - Tối đa hiệu năng RAM & GPU)");

	char saved_anim[64];
	load_setting("animation_profile", saved_anim, sizeof(saved_anim), "cubic_bezier");
	gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.combo_animation), saved_anim);
	gtk_box_pack_start(GTK_BOX(card_anim), app.combo_animation, FALSE, FALSE, 0);

	GtkWidget *btn_save_anim = gtk_button_new_with_label("Lưu cấu hình hoạt ảnh");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_save_anim), "action-btn");
	g_signal_connect(btn_save_anim, "clicked", G_CALLBACK(on_btn_save_animation_clicked), &app);
	gtk_box_pack_start(GTK_BOX(card_anim), btn_save_anim, FALSE, FALSE, 0);

	app.lbl_status = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_status), 0.0);
	gtk_box_pack_start(GTK_BOX(card_anim), app.lbl_status, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(page_anim), card_anim, FALSE, FALSE, 0);
	gtk_stack_add_titled(GTK_STACK(app.stack), page_anim, "animations", "Window Animations");

	/* ================= PAGE 6: Appearance & Dock ================= */
	GtkWidget *page_appear = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_style_context_add_class(gtk_widget_get_style_context(page_appear), "page-content");

	GtkWidget *card_appear = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(card_appear), "card");

	GtkWidget *lbl_ap_title = gtk_label_new("Giao diện Kính Mica & Thanh Diamonds Dock");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_ap_title), "card-title");
	gtk_label_set_xalign(GTK_LABEL(lbl_ap_title), 0.0);
	gtk_box_pack_start(GTK_BOX(card_appear), lbl_ap_title, FALSE, FALSE, 0);

	GtkWidget *lbl_mica = gtk_label_new("Độ trong suốt Mica (Mica Opacity %):");
	gtk_label_set_xalign(GTK_LABEL(lbl_mica), 0.0);
	gtk_box_pack_start(GTK_BOX(card_appear), lbl_mica, FALSE, FALSE, 0);

	app.mica_opacity_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 70.0, 100.0, 1.0);
	char saved_op[32];
	load_setting("mica_opacity", saved_op, sizeof(saved_op), "92");
	gtk_range_set_value(GTK_RANGE(app.mica_opacity_slider), atof(saved_op));
	gtk_box_pack_start(GTK_BOX(card_appear), app.mica_opacity_slider, FALSE, FALSE, 0);

	GtkWidget *lbl_acc = gtk_label_new("Màu nhấn chủ đạo (Accent Color):");
	gtk_label_set_xalign(GTK_LABEL(lbl_acc), 0.0);
	gtk_box_pack_start(GTK_BOX(card_appear), lbl_acc, FALSE, FALSE, 0);

	app.combo_accent = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_accent), "sky_blue", "Sky Blue (#38BDF8 - Mặc định)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_accent), "emerald",  "Emerald Green (#34D399)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_accent), "violet",   "Fluent Violet (#A78BFA)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_accent), "amber",    "Warm Amber (#FBBF24)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_accent), "rose",     "Crimson Rose (#FB7185)");
	char saved_acc[32];
	load_setting("accent_color", saved_acc, sizeof(saved_acc), "sky_blue");
	gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.combo_accent), saved_acc);
	gtk_box_pack_start(GTK_BOX(card_appear), app.combo_accent, FALSE, FALSE, 0);

	GtkWidget *lbl_dsz = gtk_label_new("Kích thước biểu tượng Dock (Dock Icon Size):");
	gtk_label_set_xalign(GTK_LABEL(lbl_dsz), 0.0);
	gtk_box_pack_start(GTK_BOX(card_appear), lbl_dsz, FALSE, FALSE, 0);

	app.combo_dock_size = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_dock_size), "20", "Nhỏ (20 px)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_dock_size), "24", "Tiêu chuẩn (24 px)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_dock_size), "32", "Lớn (32 px)");
	char saved_dsz[32];
	load_setting("dock_icon_size", saved_dsz, sizeof(saved_dsz), "24");
	gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.combo_dock_size), saved_dsz);
	gtk_box_pack_start(GTK_BOX(card_appear), app.combo_dock_size, FALSE, FALSE, 0);

	GtkWidget *btn_save_ap = gtk_button_new_with_label("Lưu cấu hình Giao diện");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_save_ap), "action-btn");
	g_signal_connect(btn_save_ap, "clicked", G_CALLBACK(on_btn_save_appearance_clicked), &app);
	gtk_box_pack_start(GTK_BOX(card_appear), btn_save_ap, FALSE, FALSE, 0);

	app.lbl_appearance_status = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_appearance_status), 0.0);
	gtk_box_pack_start(GTK_BOX(card_appear), app.lbl_appearance_status, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(page_appear), card_appear, FALSE, FALSE, 0);
	gtk_stack_add_titled(GTK_STACK(app.stack), page_appear, "appearance", "Appearance & Dock");

	/* ================= PAGE 7: Keyboard & Mouse ================= */
	GtkWidget *page_input = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_style_context_add_class(gtk_widget_get_style_context(page_input), "page-content");

	GtkWidget *card_input = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(card_input), "card");

	GtkWidget *lbl_in_title = gtk_label_new("Độ nhạy Bàn phím & Chuột (Input Devices)");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_in_title), "card-title");
	gtk_label_set_xalign(GTK_LABEL(lbl_in_title), 0.0);
	gtk_box_pack_start(GTK_BOX(card_input), lbl_in_title, FALSE, FALSE, 0);

	GtkWidget *lbl_kbr = gtk_label_new("Tốc độ lặp phím (Repeat Rate - chars/sec):");
	gtk_label_set_xalign(GTK_LABEL(lbl_kbr), 0.0);
	gtk_box_pack_start(GTK_BOX(card_input), lbl_kbr, FALSE, FALSE, 0);
	app.kb_rate_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 10.0, 60.0, 1.0);
	char saved_kbr[32];
	load_setting("kb_repeat_rate", saved_kbr, sizeof(saved_kbr), "30");
	gtk_range_set_value(GTK_RANGE(app.kb_rate_slider), atof(saved_kbr));
	gtk_box_pack_start(GTK_BOX(card_input), app.kb_rate_slider, FALSE, FALSE, 0);

	GtkWidget *lbl_kbd = gtk_label_new("Độ trễ lặp phím (Repeat Delay - ms):");
	gtk_label_set_xalign(GTK_LABEL(lbl_kbd), 0.0);
	gtk_box_pack_start(GTK_BOX(card_input), lbl_kbd, FALSE, FALSE, 0);
	app.kb_delay_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 150.0, 800.0, 25.0);
	char saved_kbd[32];
	load_setting("kb_repeat_delay", saved_kbd, sizeof(saved_kbd), "300");
	gtk_range_set_value(GTK_RANGE(app.kb_delay_slider), atof(saved_kbd));
	gtk_box_pack_start(GTK_BOX(card_input), app.kb_delay_slider, FALSE, FALSE, 0);

	GtkWidget *lbl_ms = gtk_label_new("Tốc độ con trỏ chuột (Pointer Acceleration):");
	gtk_label_set_xalign(GTK_LABEL(lbl_ms), 0.0);
	gtk_box_pack_start(GTK_BOX(card_input), lbl_ms, FALSE, FALSE, 0);
	app.mouse_speed_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -1.0, 1.0, 0.1);
	char saved_ms[32];
	load_setting("mouse_speed", saved_ms, sizeof(saved_ms), "0.0");
	gtk_range_set_value(GTK_RANGE(app.mouse_speed_slider), atof(saved_ms));
	gtk_box_pack_start(GTK_BOX(card_input), app.mouse_speed_slider, FALSE, FALSE, 0);

	app.chk_natural_scroll = gtk_check_button_new_with_label("Cuộn tự nhiên kiểu macOS (Natural Scrolling)");
	char saved_ns[32];
	load_setting("natural_scroll", saved_ns, sizeof(saved_ns), "false");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.chk_natural_scroll), strcmp(saved_ns, "true") == 0);
	gtk_box_pack_start(GTK_BOX(card_input), app.chk_natural_scroll, FALSE, FALSE, 0);

	GtkWidget *btn_save_in = gtk_button_new_with_label("Lưu cấu hình Phím & Chuột");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_save_in), "action-btn");
	g_signal_connect(btn_save_in, "clicked", G_CALLBACK(on_btn_save_input_clicked), &app);
	gtk_box_pack_start(GTK_BOX(card_input), btn_save_in, FALSE, FALSE, 0);

	app.lbl_input_status = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_input_status), 0.0);
	gtk_box_pack_start(GTK_BOX(card_input), app.lbl_input_status, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(page_input), card_input, FALSE, FALSE, 0);
	gtk_stack_add_titled(GTK_STACK(app.stack), page_input, "input", "Keyboard & Mouse");

	/* ================= PAGE 8: Power & Session (Nguồn điện & Phiên) ================= */
	GtkWidget *page_power = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_style_context_add_class(gtk_widget_get_style_context(page_power), "page-content");

	GtkWidget *card_power = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(card_power), "card");

	GtkWidget *lbl_pw_title = gtk_label_new("⚡ Quản lý Nguồn điện, Độ sáng & Phiên làm việc");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_pw_title), "card-title");
	gtk_label_set_xalign(GTK_LABEL(lbl_pw_title), 0.0);
	gtk_box_pack_start(GTK_BOX(card_power), lbl_pw_title, FALSE, FALSE, 0);

	GtkWidget *lbl_br = gtk_label_new("Độ sáng màn hình (Screen Brightness %):");
	gtk_label_set_xalign(GTK_LABEL(lbl_br), 0.0);
	gtk_box_pack_start(GTK_BOX(card_power), lbl_br, FALSE, FALSE, 0);

	app.brightness_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 5.0, 100.0, 1.0);
	gtk_range_set_value(GTK_RANGE(app.brightness_slider), 85.0);
	g_signal_connect(app.brightness_slider, "value-changed", G_CALLBACK(on_brightness_changed), &app);
	gtk_box_pack_start(GTK_BOX(card_power), app.brightness_slider, FALSE, FALSE, 0);

	GtkWidget *lbl_pprof = gtk_label_new("Chế độ hiệu năng CPU/GPU (Power Profile):");
	gtk_label_set_xalign(GTK_LABEL(lbl_pprof), 0.0);
	gtk_box_pack_start(GTK_BOX(card_power), lbl_pprof, FALSE, FALSE, 0);

	app.combo_power_profile = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_power_profile), "performance", "Hiệu suất tối đa (Performance)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_power_profile), "balanced",    "Cân bằng (Balanced - Mặc định)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_power_profile), "power-saver", "Tiết kiệm điện năng (Power Saver)");
	char saved_pprof[32];
	load_setting("power_profile", saved_pprof, sizeof(saved_pprof), "balanced");
	gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.combo_power_profile), saved_pprof);
	gtk_box_pack_start(GTK_BOX(card_power), app.combo_power_profile, FALSE, FALSE, 0);

	GtkWidget *lbl_stime = gtk_label_new("Thời gian tự động tắt màn hình (Screen Blank Timeout):");
	gtk_label_set_xalign(GTK_LABEL(lbl_stime), 0.0);
	gtk_box_pack_start(GTK_BOX(card_power), lbl_stime, FALSE, FALSE, 0);

	app.combo_screen_timeout = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_screen_timeout), "300",  "Sau 5 phút không hoạt động");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_screen_timeout), "600",  "Sau 10 phút không hoạt động");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_screen_timeout), "900",  "Sau 15 phút không hoạt động");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_screen_timeout), "0",    "Không bao giờ tắt màn hình");
	char saved_st[32];
	load_setting("screen_timeout", saved_st, sizeof(saved_st), "600");
	gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.combo_screen_timeout), saved_st);
	gtk_box_pack_start(GTK_BOX(card_power), app.combo_screen_timeout, FALSE, FALSE, 0);

	GtkWidget *btn_save_pw = gtk_button_new_with_label("Lưu cấu hình Nguồn điện");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_save_pw), "action-btn");
	g_signal_connect(btn_save_pw, "clicked", G_CALLBACK(on_btn_save_power), &app);
	gtk_box_pack_start(GTK_BOX(card_power), btn_save_pw, FALSE, FALSE, 0);

	app.lbl_power_status = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_power_status), 0.0);
	gtk_box_pack_start(GTK_BOX(card_power), app.lbl_power_status, FALSE, FALSE, 0);

	GtkWidget *sess_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	GtkWidget *btn_suspend = gtk_button_new_with_label("🌙 Tạm ngủ (Suspend)");
	g_signal_connect(btn_suspend, "clicked", G_CALLBACK(on_btn_power_session), "systemctl suspend >/dev/null 2>&1 &");
	GtkWidget *btn_reboot = gtk_button_new_with_label("🔄 Khởi động lại");
	g_signal_connect(btn_reboot, "clicked", G_CALLBACK(on_btn_power_session), "systemctl reboot >/dev/null 2>&1 &");
	GtkWidget *btn_poweroff = gtk_button_new_with_label("⏻ Tắt máy");
	g_signal_connect(btn_poweroff, "clicked", G_CALLBACK(on_btn_power_session), "systemctl poweroff >/dev/null 2>&1 &");
	gtk_box_pack_start(GTK_BOX(sess_box), btn_suspend, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sess_box), btn_reboot, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sess_box), btn_poweroff, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(card_power), sess_box, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(page_power), card_power, FALSE, FALSE, 0);
	gtk_stack_add_titled(GTK_STACK(app.stack), page_power, "power", "Nguồn điện & Pin");

	/* ================= PAGE 9: Storage & Disks (Bộ nhớ lưu trữ) ================= */
	GtkWidget *page_storage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_style_context_add_class(gtk_widget_get_style_context(page_storage), "page-content");

	GtkWidget *card_storage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(card_storage), "card");

	GtkWidget *lbl_st_title = gtk_label_new("💾 Quản lý Dung lượng Ổ đĩa & Phân vùng");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_st_title), "card-title");
	gtk_label_set_xalign(GTK_LABEL(lbl_st_title), 0.0);
	gtk_box_pack_start(GTK_BOX(card_storage), lbl_st_title, FALSE, FALSE, 0);

	app.lbl_disk_root = gtk_label_new("Phân vùng Hệ thống (/): ...");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_disk_root), 0.0);
	gtk_box_pack_start(GTK_BOX(card_storage), app.lbl_disk_root, FALSE, FALSE, 0);
	app.bar_disk_root = gtk_progress_bar_new();
	gtk_box_pack_start(GTK_BOX(card_storage), app.bar_disk_root, FALSE, FALSE, 0);

	app.lbl_disk_home = gtk_label_new("Phân vùng Dữ liệu (/mnt/home_a): ...");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_disk_home), 0.0);
	gtk_box_pack_start(GTK_BOX(card_storage), app.lbl_disk_home, FALSE, FALSE, 0);
	app.bar_disk_home = gtk_progress_bar_new();
	gtk_box_pack_start(GTK_BOX(card_storage), app.bar_disk_home, FALSE, FALSE, 0);

	GtkWidget *btn_open_files = gtk_button_new_with_label("📁 Mở Trình quản lý Tệp (Diamonds Files)");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_open_files), "action-btn");
	g_signal_connect(btn_open_files, "clicked", G_CALLBACK(on_launch_ecosystem_app), (gpointer)"diamonds-files");
	gtk_box_pack_start(GTK_BOX(card_storage), btn_open_files, FALSE, FALSE, 0);

	refresh_storage_stats(&app);
	gtk_box_pack_start(GTK_BOX(page_storage), card_storage, FALSE, FALSE, 0);
	gtk_stack_add_titled(GTK_STACK(app.stack), page_storage, "storage", "Bộ nhớ & Ổ đĩa");

	/* ================= PAGE 10: Date, Time & Language (Ngày, Giờ & Ngôn ngữ) ================= */
	GtkWidget *page_dt = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_style_context_add_class(gtk_widget_get_style_context(page_dt), "page-content");

	GtkWidget *card_dt = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(card_dt), "card");

	GtkWidget *lbl_dt_title = gtk_label_new("🕒 Ngày, Giờ, Múi giờ & Ngôn ngữ");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_dt_title), "card-title");
	gtk_label_set_xalign(GTK_LABEL(lbl_dt_title), 0.0);
	gtk_box_pack_start(GTK_BOX(card_dt), lbl_dt_title, FALSE, FALSE, 0);

	app.chk_ntp = gtk_check_button_new_with_label("Tự động đồng bộ thời gian qua mạng Internet (NTP)");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.chk_ntp), TRUE);
	gtk_box_pack_start(GTK_BOX(card_dt), app.chk_ntp, FALSE, FALSE, 0);

	GtkWidget *lbl_tz = gtk_label_new("Múi giờ hệ thống (Timezone):");
	gtk_label_set_xalign(GTK_LABEL(lbl_tz), 0.0);
	gtk_box_pack_start(GTK_BOX(card_dt), lbl_tz, FALSE, FALSE, 0);

	app.combo_timezone = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_timezone), "Asia/Ho_Chi_Minh", "Asia/Ho_Chi_Minh (GMT+07:00 - Việt Nam)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_timezone), "Asia/Bangkok",     "Asia/Bangkok (GMT+07:00)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_timezone), "Asia/Tokyo",       "Asia/Tokyo (GMT+09:00)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_timezone), "Europe/London",    "Europe/London (GMT+00:00)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_timezone), "America/New_York", "America/New_York (GMT-05:00)");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app.combo_timezone), "UTC",              "UTC (Giờ chuẩn Quốc tế)");
	char saved_tz[64];
	load_setting("timezone", saved_tz, sizeof(saved_tz), "Asia/Ho_Chi_Minh");
	gtk_combo_box_set_active_id(GTK_COMBO_BOX(app.combo_timezone), saved_tz);
	gtk_box_pack_start(GTK_BOX(card_dt), app.combo_timezone, FALSE, FALSE, 0);

	GtkWidget *btn_save_dt = gtk_button_new_with_label("Lưu cấu hình Ngày & Giờ");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_save_dt), "action-btn");
	g_signal_connect(btn_save_dt, "clicked", G_CALLBACK(on_btn_save_datetime), &app);
	gtk_box_pack_start(GTK_BOX(card_dt), btn_save_dt, FALSE, FALSE, 0);

	app.lbl_datetime_status = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_datetime_status), 0.0);
	gtk_box_pack_start(GTK_BOX(card_dt), app.lbl_datetime_status, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(page_dt), card_dt, FALSE, FALSE, 0);
	gtk_stack_add_titled(GTK_STACK(app.stack), page_dt, "datetime", "Ngày & Giờ");

	/* ================= PAGE 11: Diamonds Ecosystem Apps ================= */
	GtkWidget *page_apps = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
	gtk_style_context_add_class(gtk_widget_get_style_context(page_apps), "page-content");

	GtkWidget *card_apps = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(card_apps), "card");

	GtkWidget *lbl_apps_title = gtk_label_new("Bộ Ứng Dụng Diamonds Ecosystem (Built-in Apps)");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_apps_title), "card-title");
	gtk_label_set_xalign(GTK_LABEL(lbl_apps_title), 0.0);
	gtk_box_pack_start(GTK_BOX(card_apps), lbl_apps_title, FALSE, FALSE, 0);

	static const struct { const char *label; const char *bin; } eco_apps[] = {
		{ "Terminal (diamonds-terminal)",     "diamonds-terminal" },
		{ "Files (diamonds-files)",           "diamonds-files" },
		{ "Notes / Editor (diamonds-notes)",  "diamonds-notes" },
		{ "Calculator (diamonds-calc)",       "diamonds-calc" },
		{ "Photos Viewer (diamonds-photos)",  "diamonds-photos" },
		{ "Music Player (diamonds-music)",    "diamonds-music" },
		{ "Task Monitor (diamonds-monitor)",  "diamonds-monitor" },
		{ "Mail Client (diamonds-mail)",      "diamonds-mail" },
		{ "Clock & Alarms (diamonds-clock)",  "diamonds-clock" },
		{ "App Store (diamonds-store)",       "diamonds-store" },
	};
	GtkWidget *apps_grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(apps_grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(apps_grid), 12);
	for (size_t i = 0; i < sizeof(eco_apps) / sizeof(eco_apps[0]); i++) {
		GtkWidget *btn = gtk_button_new_with_label(eco_apps[i].label);
		gtk_style_context_add_class(gtk_widget_get_style_context(btn), "action-btn");
		g_signal_connect(btn, "clicked", G_CALLBACK(on_launch_ecosystem_app), (gpointer)eco_apps[i].bin);
		gtk_grid_attach(GTK_GRID(apps_grid), btn, (int)(i % 2), (int)(i / 2), 1, 1);
	}
	gtk_box_pack_start(GTK_BOX(card_apps), apps_grid, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(page_apps), card_apps, FALSE, FALSE, 0);
	gtk_stack_add_titled(GTK_STACK(app.stack), page_apps, "ecosystem", "Ứng dụng Hệ thống");

	gtk_widget_show_all(app.window);

	if (argc > 1 && argv[1] && *argv[1]) {
		GtkWidget *target_child = gtk_stack_get_child_by_name(GTK_STACK(app.stack), argv[1]);
		if (target_child) {
			gtk_stack_set_visible_child(GTK_STACK(app.stack), target_child);
		}
	}

	gtk_main();

	return 0;
}
