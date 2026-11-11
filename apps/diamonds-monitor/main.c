/*
 * diamonds-monitor: Lightweight Win11-style Task Manager & System Monitor
 * Pure C11 + GTK3 + Cairo, reading /proc and /sys directly.
 */
#include "diamonds_gtk.h"
#include <cairo.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_CORES 64
#define RING_SIZE 60
#define MAX_PROCS_TRACKED 4096
#define UID_CACHE_SIZE 256

enum {
	COL_NAME = 0,
	COL_PID,
	COL_USER,
	COL_CPU_VAL,
	COL_CPU_STR,
	COL_MEM_VAL,
	COL_MEM_STR,
	COL_STATE,
	NUM_COLS
};

typedef struct {
	pid_t pid;
	uint64_t ticks;
	bool seen;
} ProcTrack;

typedef struct {
	uint64_t user;
	uint64_t nice;
	uint64_t system;
	uint64_t idle;
	uint64_t iowait;
	uint64_t irq;
	uint64_t softirq;
	uint64_t steal;
} CpuRaw;

typedef struct {
	uid_t uid;
	char name[64];
	bool valid;
} UidCacheEntry;

typedef struct {
	GtkWidget *window;
	GtkWidget *notebook;

	/* Processes Tab */
	GtkWidget *search_entry;
	GtkListStore *proc_store;
	GtkTreeModel *filter_model;
	GtkTreeModel *sort_model;
	GtkWidget *tree_view;
	GtkWidget *lbl_proc_summary;
	GtkWidget *info_bar;
	GtkWidget *lbl_info_msg;
	char search_text[128];

	/* Performance Tab */
	GtkWidget *lbl_cpu_model;
	GtkWidget *lbl_cpu_usage;
	GtkWidget *lbl_uptime;
	GtkWidget *lbl_temp;
	GtkWidget *lbl_mem_usage;
	GtkWidget *lbl_swap_usage;
	GtkWidget *lbl_net_rate;
	GtkWidget *lbl_disk_rate;

	GtkWidget *da_cpu_total;
	GtkWidget *da_cpu_cores;
	GtkWidget *da_mem;
	GtkWidget *da_net;
	GtkWidget *da_disk;

	/* Metrics & Ring buffers */
	int num_cores;
	CpuRaw prev_cpu_total;
	CpuRaw prev_cpu_cores[MAX_CORES];
	bool cpu_init;

	double hist_cpu_total[RING_SIZE];
	double hist_cpu_cores[MAX_CORES][RING_SIZE];
	double hist_mem_pct[RING_SIZE];
	double hist_swap_pct[RING_SIZE];
	double hist_net_rx_kb[RING_SIZE];
	double hist_net_tx_kb[RING_SIZE];
	double hist_disk_rd_kb[RING_SIZE];
	double hist_disk_wr_kb[RING_SIZE];
	int ring_head;

	uint64_t prev_net_rx;
	uint64_t prev_net_tx;
	bool net_init;

	uint64_t prev_disk_rd_sec;
	uint64_t prev_disk_wr_sec;
	bool disk_init;

	int64_t prev_sample_us;
	uint64_t prev_total_cpu_ticks;

	ProcTrack proc_tracks[MAX_PROCS_TRACKED];
	size_t proc_track_count;

	UidCacheEntry uid_cache[UID_CACHE_SIZE];

	char cpu_model_str[256];
	long page_size_kb;
	guint timer_id;
} MonitorApp;

static const char *MONITOR_EXTRA_CSS =
	".perf-card { background-color: #1a1b26; border: 1px solid #282a36; border-radius: 8px; padding: 10px 14px; }"
	".perf-title { font-weight: bold; font-size: 13px; color: #38bdf8; }"
	".perf-big { font-weight: bold; font-size: 18px; color: #f8fafc; }"
	".perf-sub { font-size: 12px; color: #94a3b8; }"
	"infobar.error { background-color: #7f1d1d; color: #f8fafc; }";

/* Read entire small virtual file into buffer safely */
static ssize_t read_file_buf(const char *path, char *buf, size_t bufsz) {
	if (!path || !buf || bufsz < 2) return -1;
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) return -1;
	ssize_t total = 0;
	while ((size_t)total < bufsz - 1) {
		ssize_t r = read(fd, buf + total, bufsz - 1 - (size_t)total);
		if (r < 0) {
			if (errno == EINTR) continue;
			close(fd);
			return -1;
		}
		if (r == 0) break;
		total += r;
	}
	close(fd);
	buf[total] = '\0';
	return total;
}

static const char *lookup_uid_name(MonitorApp *app, uid_t uid) {
	size_t slot = (size_t)uid % UID_CACHE_SIZE;
	if (app->uid_cache[slot].valid && app->uid_cache[slot].uid == uid) {
		return app->uid_cache[slot].name;
	}
	struct passwd *pw = getpwuid(uid);
	app->uid_cache[slot].uid = uid;
	app->uid_cache[slot].valid = true;
	if (pw && pw->pw_name) {
		snprintf(app->uid_cache[slot].name, sizeof(app->uid_cache[slot].name), "%s", pw->pw_name);
	} else {
		snprintf(app->uid_cache[slot].name, sizeof(app->uid_cache[slot].name), "%u", (unsigned)uid);
	}
	return app->uid_cache[slot].name;
}

static void detect_cpu_model(MonitorApp *app) {
	app->cpu_model_str[0] = '\0';

	/* 1. Try /proc/device-tree/model (common on aarch64 SBCs) */
	char dt_buf[256];
	ssize_t n = read_file_buf("/proc/device-tree/model", dt_buf, sizeof(dt_buf));
	if (n > 0) {
		for (ssize_t i = 0; i < n; i++) {
			if (dt_buf[i] == '\n' || dt_buf[i] == '\r') dt_buf[i] = ' ';
		}
		if (dt_buf[0] != '\0') {
			snprintf(app->cpu_model_str, sizeof(app->cpu_model_str), "%s", dt_buf);
		}
	}

	/* 2. Parse /proc/cpuinfo for model name / Hardware / CPU part */
	char *buf = malloc(32768);
	if (!buf) {
		if (app->cpu_model_str[0] == '\0') {
			snprintf(app->cpu_model_str, sizeof(app->cpu_model_str), "Generic Processor");
		}
		return;
	}

	char model_name[128] = {0};
	char hardware[128] = {0};
	char cpu_part[64] = {0};

	if (read_file_buf("/proc/cpuinfo", buf, 32768) > 0) {
		char *saveptr = NULL;
		char *line = strtok_r(buf, "\n", &saveptr);
		while (line) {
			char *colon = strchr(line, ':');
			if (colon) {
				*colon = '\0';
				char *val = colon + 1;
				while (*val && isspace((unsigned char)*val)) val++;
				if (strncmp(line, "model name", 10) == 0 || strncmp(line, "Processor", 9) == 0) {
					if (model_name[0] == '\0' && *val) {
						snprintf(model_name, sizeof(model_name), "%s", val);
					}
				} else if (strncmp(line, "Hardware", 8) == 0) {
					if (hardware[0] == '\0' && *val) {
						snprintf(hardware, sizeof(hardware), "%s", val);
					}
				} else if (strncmp(line, "CPU part", 8) == 0) {
					if (cpu_part[0] == '\0' && *val) {
						snprintf(cpu_part, sizeof(cpu_part), "%s", val);
					}
				}
			}
			line = strtok_r(NULL, "\n", &saveptr);
		}
	}
	free(buf);
	buf = NULL;

	if (model_name[0] != '\0') {
		snprintf(app->cpu_model_str, sizeof(app->cpu_model_str), "%s", model_name);
	} else if (hardware[0] != '\0') {
		snprintf(app->cpu_model_str, sizeof(app->cpu_model_str), "%s", hardware);
	} else if (app->cpu_model_str[0] != '\0' && cpu_part[0] != '\0') {
		char combined[256];
		snprintf(combined, sizeof(combined), "%.180s (ARM Part %.50s)", app->cpu_model_str, cpu_part);
		snprintf(app->cpu_model_str, sizeof(app->cpu_model_str), "%s", combined);
	} else if (cpu_part[0] != '\0') {
		const char *arch_name = "ARMv8 Processor";
		if (strcmp(cpu_part, "0xd03") == 0) arch_name = "ARM Cortex-A53";
		else if (strcmp(cpu_part, "0xd05") == 0) arch_name = "ARM Cortex-A55";
		else if (strcmp(cpu_part, "0xd07") == 0) arch_name = "ARM Cortex-A57";
		else if (strcmp(cpu_part, "0xd08") == 0) arch_name = "ARM Cortex-A72";
		else if (strcmp(cpu_part, "0xd09") == 0) arch_name = "ARM Cortex-A73";
		else if (strcmp(cpu_part, "0xd0a") == 0) arch_name = "ARM Cortex-A75";
		else if (strcmp(cpu_part, "0xd0b") == 0) arch_name = "ARM Cortex-A76";
		snprintf(app->cpu_model_str, sizeof(app->cpu_model_str), "%s (%s)", arch_name, cpu_part);
	} else if (app->cpu_model_str[0] == '\0') {
		snprintf(app->cpu_model_str, sizeof(app->cpu_model_str), "Unknown Processor");
	}
}

static bool read_thermal_temp(double *out_celsius) {
	DIR *d = opendir("/sys/class/thermal");
	if (!d) return false;
	bool found = false;
	double max_temp = -1000.0;
	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		if (strncmp(ent->d_name, "thermal_zone", 12) == 0) {
			char path[512];
			snprintf(path, sizeof(path), "/sys/class/thermal/%s/temp", ent->d_name);
			char buf[64];
			if (read_file_buf(path, buf, sizeof(buf)) > 0) {
				char *endptr = NULL;
				long val = strtol(buf, &endptr, 10);
				if (endptr != buf && val > 0) {
					double c = (val > 200) ? ((double)val / 1000.0) : (double)val;
					if (c > 0.0 && c < 150.0) {
						if (c > max_temp) max_temp = c;
						found = true;
					}
				}
			}
		}
	}
	closedir(d);
	if (found && out_celsius) *out_celsius = max_temp;
	return found;
}

static uint64_t cpu_raw_total(const CpuRaw *c) {
	return c->user + c->nice + c->system + c->idle + c->iowait + c->irq + c->softirq + c->steal;
}

static uint64_t cpu_raw_busy(const CpuRaw *c) {
	return c->user + c->nice + c->system + c->irq + c->softirq + c->steal;
}

static double calc_cpu_pct(const CpuRaw *prev, const CpuRaw *cur) {
	uint64_t t_prev = cpu_raw_total(prev);
	uint64_t t_cur = cpu_raw_total(cur);
	uint64_t b_prev = cpu_raw_busy(prev);
	uint64_t b_cur = cpu_raw_busy(cur);
	if (t_cur <= t_prev) return 0.0;
	uint64_t dt = t_cur - t_prev;
	uint64_t db = (b_cur >= b_prev) ? (b_cur - b_prev) : 0;
	double pct = ((double)db * 100.0) / (double)dt;
	if (pct < 0.0) pct = 0.0;
	if (pct > 100.0) pct = 100.0;
	return pct;
}

static void format_rate(double kb_s, char *out, size_t outsz) {
	if (kb_s >= 1024.0) {
		snprintf(out, outsz, "%.2f MB/s", kb_s / 1024.0);
	} else {
		snprintf(out, outsz, "%.1f KB/s", kb_s);
	}
}

static void format_mem_kb(uint64_t kb, char *out, size_t outsz) {
	if (kb >= 1048576ULL) {
		snprintf(out, outsz, "%.2f GB", (double)kb / 1048576.0);
	} else if (kb >= 1024ULL) {
		snprintf(out, outsz, "%.1f MB", (double)kb / 1024.0);
	} else {
		snprintf(out, outsz, "%" PRIu64 " KB", kb);
	}
}

static void update_performance_metrics(MonitorApp *app, double dt_sec, uint64_t *out_cpu_delta_ticks) {
	int idx = app->ring_head;
	if (out_cpu_delta_ticks) *out_cpu_delta_ticks = 0;

	/* 1. /proc/stat */
	char *stat_buf = malloc(16384);
	if (stat_buf && read_file_buf("/proc/stat", stat_buf, 16384) > 0) {
		CpuRaw cur_total = {0};
		CpuRaw cur_cores[MAX_CORES];
		memset(cur_cores, 0, sizeof(cur_cores));
		int max_core_seen = 0;

		char *saveptr = NULL;
		char *line = strtok_r(stat_buf, "\n", &saveptr);
		while (line) {
			if (strncmp(line, "cpu ", 4) == 0) {
				sscanf(line + 4,
					"%" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
					&cur_total.user, &cur_total.nice, &cur_total.system, &cur_total.idle,
					&cur_total.iowait, &cur_total.irq, &cur_total.softirq, &cur_total.steal);
			} else if (strncmp(line, "cpu", 3) == 0 && isdigit((unsigned char)line[3])) {
				int core_id = atoi(line + 3);
				if (core_id >= 0 && core_id < MAX_CORES) {
					char *sp = strchr(line, ' ');
					if (sp) {
						sscanf(sp + 1,
							"%" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
							&cur_cores[core_id].user, &cur_cores[core_id].nice,
							&cur_cores[core_id].system, &cur_cores[core_id].idle,
							&cur_cores[core_id].iowait, &cur_cores[core_id].irq,
							&cur_cores[core_id].softirq, &cur_cores[core_id].steal);
						if (core_id + 1 > max_core_seen) max_core_seen = core_id + 1;
					}
				}
			}
			line = strtok_r(NULL, "\n", &saveptr);
		}

		if (max_core_seen > 0) app->num_cores = max_core_seen;
		else if (app->num_cores <= 0) app->num_cores = 1;

		uint64_t total_ticks = cpu_raw_total(&cur_total);
		if (app->cpu_init) {
			if (total_ticks > app->prev_total_cpu_ticks && out_cpu_delta_ticks) {
				*out_cpu_delta_ticks = total_ticks - app->prev_total_cpu_ticks;
			}
			double total_pct = calc_cpu_pct(&app->prev_cpu_total, &cur_total);
			app->hist_cpu_total[idx] = total_pct;
			for (int c = 0; c < app->num_cores; c++) {
				app->hist_cpu_cores[c][idx] = calc_cpu_pct(&app->prev_cpu_cores[c], &cur_cores[c]);
			}
			char ubuf[128];
			snprintf(ubuf, sizeof(ubuf), "%.1f%% (%d cores)", total_pct, app->num_cores);
			gtk_label_set_text(GTK_LABEL(app->lbl_cpu_usage), ubuf);
		}
		app->prev_cpu_total = cur_total;
		for (int c = 0; c < app->num_cores; c++) {
			app->prev_cpu_cores[c] = cur_cores[c];
		}
		app->prev_total_cpu_ticks = total_ticks;
		app->cpu_init = true;
	}
	free(stat_buf);
	stat_buf = NULL;

	/* 2. /proc/uptime */
	char up_buf[128];
	if (read_file_buf("/proc/uptime", up_buf, sizeof(up_buf)) > 0) {
		double up_sec = 0.0;
		if (sscanf(up_buf, "%lf", &up_sec) == 1) {
			uint64_t s = (uint64_t)up_sec;
			uint64_t days = s / 86400ULL;
			uint64_t hrs = (s % 86400ULL) / 3600ULL;
			uint64_t mins = (s % 3600ULL) / 60ULL;
			uint64_t secs = s % 60ULL;
			char tbuf[128];
			if (days > 0) {
				snprintf(tbuf, sizeof(tbuf), "Uptime: %" PRIu64 "d %02" PRIu64 ":%02" PRIu64 ":%02" PRIu64,
					days, hrs, mins, secs);
			} else {
				snprintf(tbuf, sizeof(tbuf), "Uptime: %02" PRIu64 ":%02" PRIu64 ":%02" PRIu64,
					hrs, mins, secs);
			}
			gtk_label_set_text(GTK_LABEL(app->lbl_uptime), tbuf);
		}
	}

	/* 3. Temperature */
	double temp_c = 0.0;
	if (read_thermal_temp(&temp_c)) {
		char tbuf[64];
		snprintf(tbuf, sizeof(tbuf), "Temp: %.1f °C", temp_c);
		gtk_label_set_text(GTK_LABEL(app->lbl_temp), tbuf);
	} else {
		gtk_label_set_text(GTK_LABEL(app->lbl_temp), "Temp: N/A");
	}

	/* 4. /proc/meminfo */
	char mem_buf[4096];
	if (read_file_buf("/proc/meminfo", mem_buf, sizeof(mem_buf)) > 0) {
		uint64_t mem_total = 0, mem_free = 0, mem_avail = 0, buffers = 0, cached = 0;
		uint64_t swap_total = 0, swap_free = 0;
		bool has_avail = false;

		char *saveptr = NULL;
		char *line = strtok_r(mem_buf, "\n", &saveptr);
		while (line) {
			uint64_t val = 0;
			if (sscanf(line, "MemTotal: %" SCNu64, &val) == 1) mem_total = val;
			else if (sscanf(line, "MemFree: %" SCNu64, &val) == 1) mem_free = val;
			else if (sscanf(line, "MemAvailable: %" SCNu64, &val) == 1) { mem_avail = val; has_avail = true; }
			else if (sscanf(line, "Buffers: %" SCNu64, &val) == 1) buffers = val;
			else if (sscanf(line, "Cached: %" SCNu64, &val) == 1) cached = val;
			else if (sscanf(line, "SwapTotal: %" SCNu64, &val) == 1) swap_total = val;
			else if (sscanf(line, "SwapFree: %" SCNu64, &val) == 1) swap_free = val;
			line = strtok_r(NULL, "\n", &saveptr);
		}

		if (!has_avail) mem_avail = mem_free + buffers + cached;
		uint64_t mem_used = (mem_total > mem_avail) ? (mem_total - mem_avail) : 0;
		double mem_pct = (mem_total > 0) ? ((double)mem_used * 100.0 / (double)mem_total) : 0.0;
		app->hist_mem_pct[idx] = mem_pct;

		uint64_t swap_used = (swap_total > swap_free) ? (swap_total - swap_free) : 0;
		double swap_pct = (swap_total > 0) ? ((double)swap_used * 100.0 / (double)swap_total) : 0.0;
		app->hist_swap_pct[idx] = swap_pct;

		char u_str[32], t_str[32], mbuf[128];
		format_mem_kb(mem_used, u_str, sizeof(u_str));
		format_mem_kb(mem_total, t_str, sizeof(t_str));
		snprintf(mbuf, sizeof(mbuf), "RAM: %s / %s (%.1f%%)", u_str, t_str, mem_pct);
		gtk_label_set_text(GTK_LABEL(app->lbl_mem_usage), mbuf);

		format_mem_kb(swap_used, u_str, sizeof(u_str));
		format_mem_kb(swap_total, t_str, sizeof(t_str));
		snprintf(mbuf, sizeof(mbuf), "Swap: %s / %s (%.1f%%)", u_str, t_str, swap_pct);
		gtk_label_set_text(GTK_LABEL(app->lbl_swap_usage), mbuf);
	}

	/* 5. /proc/net/dev */
	char net_buf[4096];
	if (read_file_buf("/proc/net/dev", net_buf, sizeof(net_buf)) > 0) {
		uint64_t rx_sum = 0, tx_sum = 0;
		char *saveptr = NULL;
		char *line = strtok_r(net_buf, "\n", &saveptr);
		while (line) {
			char *colon = strchr(line, ':');
			if (colon) {
				*colon = '\0';
				char *ifname = line;
				while (*ifname && isspace((unsigned char)*ifname)) ifname++;
				if (strcmp(ifname, "lo") != 0) {
					uint64_t r_bytes = 0, r_pkt = 0, r_err = 0, r_drop = 0;
					uint64_t r_fifo = 0, r_frame = 0, r_comp = 0, r_mcast = 0;
					uint64_t t_bytes = 0;
					if (sscanf(colon + 1,
						"%" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
						&r_bytes, &r_pkt, &r_err, &r_drop, &r_fifo, &r_frame, &r_comp, &r_mcast, &t_bytes) == 9) {
						rx_sum += r_bytes;
						tx_sum += t_bytes;
					}
				}
			}
			line = strtok_r(NULL, "\n", &saveptr);
		}

		double rx_kb_s = 0.0, tx_kb_s = 0.0;
		if (app->net_init && dt_sec > 0.05) {
			if (rx_sum >= app->prev_net_rx) {
				rx_kb_s = ((double)(rx_sum - app->prev_net_rx) / 1024.0) / dt_sec;
			}
			if (tx_sum >= app->prev_net_tx) {
				tx_kb_s = ((double)(tx_sum - app->prev_net_tx) / 1024.0) / dt_sec;
			}
		}
		app->prev_net_rx = rx_sum;
		app->prev_net_tx = tx_sum;
		app->net_init = true;

		app->hist_net_rx_kb[idx] = rx_kb_s;
		app->hist_net_tx_kb[idx] = tx_kb_s;

		char rx_s[32], tx_s[32], nbuf[128];
		format_rate(rx_kb_s, rx_s, sizeof(rx_s));
		format_rate(tx_kb_s, tx_s, sizeof(tx_s));
		snprintf(nbuf, sizeof(nbuf), "Down: %s  |  Up: %s", rx_s, tx_s);
		gtk_label_set_text(GTK_LABEL(app->lbl_net_rate), nbuf);
	}

	/* 6. /proc/diskstats */
	char *disk_buf = malloc(8192);
	if (disk_buf && read_file_buf("/proc/diskstats", disk_buf, 8192) > 0) {
		uint64_t rd_sec_sum = 0, wr_sec_sum = 0;
		char *saveptr = NULL;
		char *line = strtok_r(disk_buf, "\n", &saveptr);
		while (line) {
			unsigned int maj = 0, min = 0;
			char dev[64] = {0};
			uint64_t rd_ios = 0, rd_merges = 0, rd_sec = 0, rd_ticks = 0;
			uint64_t wr_ios = 0, wr_merges = 0, wr_sec = 0;
			if (sscanf(line, "%u %u %63s %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
				&maj, &min, dev, &rd_ios, &rd_merges, &rd_sec, &rd_ticks, &wr_ios, &wr_merges, &wr_sec) == 10) {
				/* Include whole block devices (sdX, vdX, nvmeXnY, mmcblkX) and skip partitions & loop/ram */
				bool whole_disk = false;
				size_t len = strlen(dev);
				if ((strncmp(dev, "sd", 2) == 0 || strncmp(dev, "vd", 2) == 0 || strncmp(dev, "hd", 2) == 0) &&
				    len == 3 && isalpha((unsigned char)dev[2])) {
					whole_disk = true;
				} else if (strncmp(dev, "nvme", 4) == 0 && strchr(dev, 'p') == NULL) {
					whole_disk = true;
				} else if (strncmp(dev, "mmcblk", 6) == 0 && strchr(dev, 'p') == NULL) {
					whole_disk = true;
				}
				if (whole_disk) {
					rd_sec_sum += rd_sec;
					wr_sec_sum += wr_sec;
				}
			}
			line = strtok_r(NULL, "\n", &saveptr);
		}

		double rd_kb_s = 0.0, wr_kb_s = 0.0;
		if (app->disk_init && dt_sec > 0.05) {
			if (rd_sec_sum >= app->prev_disk_rd_sec) {
				rd_kb_s = ((double)(rd_sec_sum - app->prev_disk_rd_sec) * 0.5) / dt_sec;
			}
			if (wr_sec_sum >= app->prev_disk_wr_sec) {
				wr_kb_s = ((double)(wr_sec_sum - app->prev_disk_wr_sec) * 0.5) / dt_sec;
			}
		}
		app->prev_disk_rd_sec = rd_sec_sum;
		app->prev_disk_wr_sec = wr_sec_sum;
		app->disk_init = true;

		app->hist_disk_rd_kb[idx] = rd_kb_s;
		app->hist_disk_wr_kb[idx] = wr_kb_s;

		char rd_s[32], wr_s[32], dbuf[128];
		format_rate(rd_kb_s, rd_s, sizeof(rd_s));
		format_rate(wr_kb_s, wr_s, sizeof(wr_s));
		snprintf(dbuf, sizeof(dbuf), "Read: %s  |  Write: %s", rd_s, wr_s);
		gtk_label_set_text(GTK_LABEL(app->lbl_disk_rate), dbuf);
	}
	free(disk_buf);
	disk_buf = NULL;

	app->ring_head = (app->ring_head + 1) % RING_SIZE;

	gtk_widget_queue_draw(app->da_cpu_total);
	gtk_widget_queue_draw(app->da_cpu_cores);
	gtk_widget_queue_draw(app->da_mem);
	gtk_widget_queue_draw(app->da_net);
	gtk_widget_queue_draw(app->da_disk);
}

static uint64_t get_and_update_proc_ticks(MonitorApp *app, pid_t pid, uint64_t cur_ticks, bool *found_prev) {
	for (size_t i = 0; i < app->proc_track_count; i++) {
		if (app->proc_tracks[i].pid == pid) {
			uint64_t prev = app->proc_tracks[i].ticks;
			app->proc_tracks[i].ticks = cur_ticks;
			app->proc_tracks[i].seen = true;
			*found_prev = true;
			return (cur_ticks >= prev) ? (cur_ticks - prev) : 0;
		}
	}
	*found_prev = false;
	if (app->proc_track_count < MAX_PROCS_TRACKED) {
		size_t idx = app->proc_track_count++;
		app->proc_tracks[idx].pid = pid;
		app->proc_tracks[idx].ticks = cur_ticks;
		app->proc_tracks[idx].seen = true;
	}
	return 0;
}

static const char *state_description(char st) {
	switch (st) {
	case 'R': return "Running";
	case 'S': return "Sleeping";
	case 'D': return "Disk Sleep";
	case 'Z': return "Zombie";
	case 'T':
	case 't': return "Stopped";
	case 'I': return "Idle";
	default: return "Unknown";
	}
}

static void refresh_process_list(MonitorApp *app, uint64_t cpu_delta_ticks) {
	/* Preserve current selection PID and scroll adjustments */
	pid_t selected_pid = -1;
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->tree_view));
	GtkTreeModel *smodel = NULL;
	GtkTreeIter siter;
	if (gtk_tree_selection_get_selected(sel, &smodel, &siter)) {
		gint pid_val = -1;
		gtk_tree_model_get(smodel, &siter, COL_PID, &pid_val, -1);
		selected_pid = (pid_t)pid_val;
	}

	GtkAdjustment *vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(app->tree_view));
	double saved_vscroll = vadj ? gtk_adjustment_get_value(vadj) : 0.0;

	/* Reset seen flags in proc tracker */
	for (size_t i = 0; i < app->proc_track_count; i++) {
		app->proc_tracks[i].seen = false;
	}

	/* Build map of existing rows in GtkListStore by PID for in-place update */
	DIR *proc_dir = opendir("/proc");
	if (!proc_dir) return;

	GHashTable *pid_to_iter = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
	GtkTreeIter iter;
	gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(app->proc_store), &iter);
	while (valid) {
		gint pid_val = 0;
		gtk_tree_model_get(GTK_TREE_MODEL(app->proc_store), &iter, COL_PID, &pid_val, -1);
		GtkTreeIter *copy = g_malloc(sizeof(GtkTreeIter));
		*copy = iter;
		g_hash_table_insert(pid_to_iter, GINT_TO_POINTER(pid_val), copy);
		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(app->proc_store), &iter);
	}

	size_t total_procs = 0;
	size_t running_procs = 0;

	struct dirent *ent;
	while ((ent = readdir(proc_dir)) != NULL) {
		if (!isdigit((unsigned char)ent->d_name[0])) continue;
		char *endptr = NULL;
		long lpid = strtol(ent->d_name, &endptr, 10);
		if (*endptr != '\0' || lpid <= 0) continue;
		pid_t pid = (pid_t)lpid;

		char stat_path[128];
		snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", (int)pid);

		struct stat st_info;
		if (stat(stat_path, &st_info) != 0) continue;
		uid_t uid = st_info.st_uid;

		char buf[1024];
		if (read_file_buf(stat_path, buf, sizeof(buf)) <= 0) continue;

		/* Parse comm between first '(' and last ')' */
		char *lpar = strchr(buf, '(');
		char *rpar = strrchr(buf, ')');
		if (!lpar || !rpar || rpar <= lpar) continue;

		char comm[128];
		size_t comm_len = (size_t)(rpar - lpar - 1);
		if (comm_len >= sizeof(comm)) comm_len = sizeof(comm) - 1;
		memcpy(comm, lpar + 1, comm_len);
		comm[comm_len] = '\0';

		char *after = rpar + 1;
		while (*after == ' ') after++;
		char state_ch = *after;
		if (state_ch == '\0') continue;

		/* Tokenize remaining fields after state (field 3) */
		char *tok_save = NULL;
		char *tok = strtok_r(after, " ", &tok_save);
		int field = 3;
		uint64_t utime = 0, stime = 0;
		int64_t rss_pages = 0;
		while (tok != NULL) {
			if (field == 14) utime = strtoull(tok, NULL, 10);
			else if (field == 15) stime = strtoull(tok, NULL, 10);
			else if (field == 24) {
				rss_pages = strtoll(tok, NULL, 10);
				break;
			}
			tok = strtok_r(NULL, " ", &tok_save);
			field++;
		}

		if (rss_pages < 0) rss_pages = 0;
		uint64_t rss_kb = (uint64_t)rss_pages * (uint64_t)app->page_size_kb;

		bool found_prev = false;
		uint64_t dticks = get_and_update_proc_ticks(app, pid, utime + stime, &found_prev);
		double cpu_pct = 0.0;
		if (found_prev && cpu_delta_ticks > 0) {
			cpu_pct = ((double)dticks * 100.0 * (double)app->num_cores) / (double)cpu_delta_ticks;
			if (cpu_pct > 100.0 * app->num_cores) cpu_pct = 100.0 * app->num_cores;
		}

		char cpu_str[32];
		snprintf(cpu_str, sizeof(cpu_str), "%.1f%%", cpu_pct);

		char mem_str[32];
		format_mem_kb(rss_kb, mem_str, sizeof(mem_str));

		const char *user_str = lookup_uid_name(app, uid);
		const char *state_str = state_description(state_ch);

		total_procs++;
		if (state_ch == 'R') running_procs++;

		GtkTreeIter *existing = g_hash_table_lookup(pid_to_iter, GINT_TO_POINTER((gint)pid));
		if (existing) {
			gtk_list_store_set(app->proc_store, existing,
				COL_NAME, comm,
				COL_USER, user_str,
				COL_CPU_VAL, cpu_pct,
				COL_CPU_STR, cpu_str,
				COL_MEM_VAL, (guint64)rss_kb,
				COL_MEM_STR, mem_str,
				COL_STATE, state_str,
				-1);
			g_hash_table_remove(pid_to_iter, GINT_TO_POINTER((gint)pid));
		} else {
			GtkTreeIter new_iter;
			gtk_list_store_append(app->proc_store, &new_iter);
			gtk_list_store_set(app->proc_store, &new_iter,
				COL_NAME, comm,
				COL_PID, (gint)pid,
				COL_USER, user_str,
				COL_CPU_VAL, cpu_pct,
				COL_CPU_STR, cpu_str,
				COL_MEM_VAL, (guint64)rss_kb,
				COL_MEM_STR, mem_str,
				COL_STATE, state_str,
				-1);
		}
	}
	closedir(proc_dir);

	/* Remove dead processes that remained in pid_to_iter */
	GHashTableIter hiter;
	gpointer key, val;
	g_hash_table_iter_init(&hiter, pid_to_iter);
	while (g_hash_table_iter_next(&hiter, &key, &val)) {
		GtkTreeIter *dead_iter = (GtkTreeIter *)val;
		gtk_list_store_remove(app->proc_store, dead_iter);
	}
	g_hash_table_destroy(pid_to_iter);

	/* Compact proc_tracks */
	size_t write_idx = 0;
	for (size_t i = 0; i < app->proc_track_count; i++) {
		if (app->proc_tracks[i].seen) {
			if (write_idx != i) app->proc_tracks[write_idx] = app->proc_tracks[i];
			write_idx++;
		}
	}
	app->proc_track_count = write_idx;

	/* Restore selection if PID still exists */
	if (selected_pid > 0) {
		GtkTreeIter citer;
		gboolean ok = gtk_tree_model_get_iter_first(app->sort_model, &citer);
		while (ok) {
			gint rpid = 0;
			gtk_tree_model_get(app->sort_model, &citer, COL_PID, &rpid, -1);
			if ((pid_t)rpid == selected_pid) {
				gtk_tree_selection_select_iter(sel, &citer);
				break;
			}
			ok = gtk_tree_model_iter_next(app->sort_model, &citer);
		}
	}

	if (vadj) {
		gtk_adjustment_set_value(vadj, saved_vscroll);
	}

	char sbuf[128];
	snprintf(sbuf, sizeof(sbuf), "Processes: %zu (%zu running)", total_procs, running_procs);
	gtk_label_set_text(GTK_LABEL(app->lbl_proc_summary), sbuf);
}

static gboolean on_monitor_tick(gpointer user_data) {
	MonitorApp *app = user_data;
	int64_t now_us = g_get_monotonic_time();
	double dt_sec = 1.5;
	if (app->prev_sample_us > 0 && now_us > app->prev_sample_us) {
		dt_sec = (double)(now_us - app->prev_sample_us) / 1000000.0;
	}
	app->prev_sample_us = now_us;

	uint64_t cpu_delta_ticks = 0;
	update_performance_metrics(app, dt_sec, &cpu_delta_ticks);
	refresh_process_list(app, cpu_delta_ticks);

	return G_SOURCE_CONTINUE;
}

/* Cairo Line Graph Drawer */
static void draw_graph_grid(cairo_t *cr, int w, int h) {
	cairo_set_source_rgb(cr, 0.07, 0.07, 0.10);
	cairo_paint(cr);

	cairo_set_source_rgba(cr, 0.22, 0.24, 0.32, 0.55);
	cairo_set_line_width(cr, 1.0);
	for (int i = 1; i < 4; i++) {
		double y = (double)h * (double)i / 4.0;
		cairo_move_to(cr, 0, y);
		cairo_line_to(cr, w, y);
	}
	for (int i = 1; i < 6; i++) {
		double x = (double)w * (double)i / 6.0;
		cairo_move_to(cr, x, 0);
		cairo_line_to(cr, x, h);
	}
	cairo_stroke(cr);
}

static void draw_series_line(cairo_t *cr, int w, int h, const double *ring, int head,
                             double max_val, double r, double g, double b, bool fill_under) {
	if (max_val <= 0.0001) max_val = 1.0;

	cairo_new_path(cr);
	for (int i = 0; i < RING_SIZE; i++) {
		int idx = (head + i) % RING_SIZE;
		double val = ring[idx];
		if (val < 0.0) val = 0.0;
		if (val > max_val) val = max_val;
		double x = (double)i * (double)w / (double)(RING_SIZE - 1);
		double y = (double)h - ((val / max_val) * (double)(h - 6)) - 3.0;
		if (i == 0) cairo_move_to(cr, x, y);
		else cairo_line_to(cr, x, y);
	}

	cairo_set_source_rgb(cr, r, g, b);
	cairo_set_line_width(cr, 2.0);
	if (fill_under) {
		cairo_stroke_preserve(cr);
		cairo_line_to(cr, w, h);
		cairo_line_to(cr, 0, h);
		cairo_close_path(cr);
		cairo_set_source_rgba(cr, r, g, b, 0.18);
		cairo_fill(cr);
	} else {
		cairo_stroke(cr);
	}
}

static gboolean on_draw_cpu_total(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
	MonitorApp *app = user_data;
	int w = gtk_widget_get_allocated_width(widget);
	int h = gtk_widget_get_allocated_height(widget);
	draw_graph_grid(cr, w, h);
	draw_series_line(cr, w, h, app->hist_cpu_total, app->ring_head, 100.0, 0.22, 0.74, 0.97, true);
	return FALSE;
}

static gboolean on_draw_cpu_cores(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
	MonitorApp *app = user_data;
	int w = gtk_widget_get_allocated_width(widget);
	int h = gtk_widget_get_allocated_height(widget);
	draw_graph_grid(cr, w, h);

	static const double palette[8][3] = {
		{0.22, 0.74, 0.97},
		{0.20, 0.83, 0.60},
		{0.96, 0.62, 0.04},
		{0.94, 0.33, 0.31},
		{0.65, 0.55, 0.98},
		{0.93, 0.45, 0.71},
		{0.18, 0.83, 0.75},
		{0.98, 0.80, 0.08}
	};

	int cores = app->num_cores > 0 ? app->num_cores : 1;
	if (cores > MAX_CORES) cores = MAX_CORES;
	for (int c = 0; c < cores; c++) {
		const double *col = palette[c % 8];
		draw_series_line(cr, w, h, app->hist_cpu_cores[c], app->ring_head, 100.0,
			col[0], col[1], col[2], false);
	}
	return FALSE;
}

static gboolean on_draw_mem(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
	MonitorApp *app = user_data;
	int w = gtk_widget_get_allocated_width(widget);
	int h = gtk_widget_get_allocated_height(widget);
	draw_graph_grid(cr, w, h);
	draw_series_line(cr, w, h, app->hist_mem_pct, app->ring_head, 100.0, 0.65, 0.55, 0.98, true);
	draw_series_line(cr, w, h, app->hist_swap_pct, app->ring_head, 100.0, 0.96, 0.62, 0.04, false);
	return FALSE;
}

static gboolean on_draw_net(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
	MonitorApp *app = user_data;
	int w = gtk_widget_get_allocated_width(widget);
	int h = gtk_widget_get_allocated_height(widget);
	draw_graph_grid(cr, w, h);

	double max_kb = 64.0;
	for (int i = 0; i < RING_SIZE; i++) {
		if (app->hist_net_rx_kb[i] > max_kb) max_kb = app->hist_net_rx_kb[i];
		if (app->hist_net_tx_kb[i] > max_kb) max_kb = app->hist_net_tx_kb[i];
	}
	draw_series_line(cr, w, h, app->hist_net_rx_kb, app->ring_head, max_kb * 1.15, 0.20, 0.83, 0.60, true);
	draw_series_line(cr, w, h, app->hist_net_tx_kb, app->ring_head, max_kb * 1.15, 0.96, 0.62, 0.04, false);
	return FALSE;
}

static gboolean on_draw_disk(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
	MonitorApp *app = user_data;
	int w = gtk_widget_get_allocated_width(widget);
	int h = gtk_widget_get_allocated_height(widget);
	draw_graph_grid(cr, w, h);

	double max_kb = 64.0;
	for (int i = 0; i < RING_SIZE; i++) {
		if (app->hist_disk_rd_kb[i] > max_kb) max_kb = app->hist_disk_rd_kb[i];
		if (app->hist_disk_wr_kb[i] > max_kb) max_kb = app->hist_disk_wr_kb[i];
	}
	draw_series_line(cr, w, h, app->hist_disk_rd_kb, app->ring_head, max_kb * 1.15, 0.22, 0.74, 0.97, true);
	draw_series_line(cr, w, h, app->hist_disk_wr_kb, app->ring_head, max_kb * 1.15, 0.94, 0.33, 0.31, false);
	return FALSE;
}

/* Search Filter & Process Actions */
static gboolean proc_filter_visible_func(GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
	MonitorApp *app = data;
	if (app->search_text[0] == '\0') return TRUE;

	gchar *name = NULL;
	gchar *user = NULL;
	gint pid = 0;
	gtk_tree_model_get(model, iter, COL_NAME, &name, COL_USER, &user, COL_PID, &pid, -1);

	char pid_str[32];
	snprintf(pid_str, sizeof(pid_str), "%d", (int)pid);

	bool match = false;
	if (name && g_ascii_strcasecmp(name, app->search_text) == 0) {
		match = true;
	} else {
		gchar *needle = g_ascii_strdown(app->search_text, -1);
		if (name) {
			gchar *hay = g_ascii_strdown(name, -1);
			if (strstr(hay, needle) != NULL) match = true;
			g_free(hay);
		}
		if (!match && user) {
			gchar *hay = g_ascii_strdown(user, -1);
			if (strstr(hay, needle) != NULL) match = true;
			g_free(hay);
		}
		if (!match && strstr(pid_str, needle) != NULL) {
			match = true;
		}
		g_free(needle);
	}

	g_free(name);
	g_free(user);
	return match ? TRUE : FALSE;
}

static void on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
	MonitorApp *app = user_data;
	const char *txt = gtk_entry_get_text(GTK_ENTRY(entry));
	snprintf(app->search_text, sizeof(app->search_text), "%s", txt ? txt : "");
	gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(app->filter_model));
}

static void show_error_banner(MonitorApp *app, const char *msg) {
	gtk_label_set_text(GTK_LABEL(app->lbl_info_msg), msg);
	gtk_widget_show(app->info_bar);
}

static bool get_selected_process(MonitorApp *app, pid_t *out_pid, char *out_name, size_t namesz) {
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->tree_view));
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return false;

	gint pid_val = -1;
	gchar *name = NULL;
	gtk_tree_model_get(model, &iter, COL_PID, &pid_val, COL_NAME, &name, -1);
	if (out_pid) *out_pid = (pid_t)pid_val;
	if (out_name && namesz > 0) {
		snprintf(out_name, namesz, "%s", name ? name : "process");
	}
	g_free(name);
	return pid_val > 0;
}

static void send_signal_to_selected(MonitorApp *app, int sig, bool confirm_first) {
	pid_t pid = -1;
	char name[128] = {0};
	if (!get_selected_process(app, &pid, name, sizeof(name))) {
		show_error_banner(app, "Please select a process first.");
		return;
	}

	if (confirm_first) {
		GtkWidget *dlg = gtk_message_dialog_new(
			GTK_WINDOW(app->window),
			GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_WARNING,
			GTK_BUTTONS_NONE,
			"Force kill process \"%s\" (PID %d)?", name, (int)pid);
		gtk_message_dialog_format_secondary_text(
			GTK_MESSAGE_DIALOG(dlg),
			"Sending SIGKILL immediately terminates the process and may cause unsaved data loss.");
		gtk_dialog_add_buttons(GTK_DIALOG(dlg),
			"Cancel", GTK_RESPONSE_CANCEL,
			"Kill Process (SIGKILL)", GTK_RESPONSE_ACCEPT,
			NULL);
		gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
		gtk_widget_destroy(dlg);
		if (resp != GTK_RESPONSE_ACCEPT) return;
	}

	if (kill(pid, sig) != 0) {
		int err = errno;
		char errbuf[256];
		snprintf(errbuf, sizeof(errbuf), "Failed to send %s to %s (PID %d): %s (%d)",
			(sig == SIGKILL) ? "SIGKILL" : "SIGTERM",
			name, (int)pid, strerror(err), err);
		show_error_banner(app, errbuf);
	} else {
		gtk_widget_hide(app->info_bar);
		on_monitor_tick(app);
	}
}

static void on_btn_end_task(GtkWidget *btn, gpointer user_data) {
	(void)btn;
	MonitorApp *app = user_data;
	send_signal_to_selected(app, SIGTERM, false);
}

static void on_btn_kill_task(GtkWidget *btn, gpointer user_data) {
	(void)btn;
	MonitorApp *app = user_data;
	send_signal_to_selected(app, SIGKILL, true);
}

static gboolean on_tree_button_press(GtkWidget *treeview, GdkEventButton *event, gpointer user_data) {
	MonitorApp *app = user_data;
	if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
		GtkTreePath *path = NULL;
		if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview),
			(gint)event->x, (gint)event->y, &path, NULL, NULL, NULL)) {
			GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
			gtk_tree_selection_select_path(sel, path);
			gtk_tree_path_free(path);

			GtkWidget *menu = gtk_menu_new();
			GtkWidget *mi_term = gtk_menu_item_new_with_label("End Task (SIGTERM)");
			GtkWidget *mi_kill = gtk_menu_item_new_with_label("Force Kill (SIGKILL)...");
			g_signal_connect(mi_term, "activate", G_CALLBACK(on_btn_end_task), app);
			g_signal_connect(mi_kill, "activate", G_CALLBACK(on_btn_kill_task), app);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_term);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_kill);
			gtk_widget_show_all(menu);
			gtk_menu_popup_at_pointer(GTK_MENU(menu), (const GdkEvent *)event);
			return TRUE;
		}
	}
	return FALSE;
}

static void add_tree_col(GtkTreeView *tv, const char *title, int display_col, int sort_col, bool right_align, int min_w) {
	GtkCellRenderer *rend = gtk_cell_renderer_text_new();
	if (right_align) {
		g_object_set(rend, "xalign", 1.0f, NULL);
	}
	GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(title, rend, "text", display_col, NULL);
	gtk_tree_view_column_set_sort_column_id(col, sort_col);
	gtk_tree_view_column_set_resizable(col, TRUE);
	gtk_tree_view_column_set_min_width(col, min_w);
	gtk_tree_view_append_column(tv, col);
}

static GtkWidget *build_processes_tab(MonitorApp *app) {
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

	/* Top toolbar: Search + Action buttons */
	GtkWidget *top_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	app->search_entry = gtk_search_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(app->search_entry), "Filter by name, PID, or user...");
	gtk_widget_set_size_request(app->search_entry, 280, -1);
	g_signal_connect(app->search_entry, "search-changed", G_CALLBACK(on_search_changed), app);
	gtk_box_pack_start(GTK_BOX(top_bar), app->search_entry, FALSE, FALSE, 0);

	app->lbl_proc_summary = gtk_label_new("Processes: 0");
	gtk_style_context_add_class(gtk_widget_get_style_context(app->lbl_proc_summary), "dim");
	gtk_box_pack_start(GTK_BOX(top_bar), app->lbl_proc_summary, FALSE, FALSE, 8);

	GtkWidget *btn_kill = gtk_button_new_with_label("Kill (SIGKILL)");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_kill), "destructive-action");
	g_signal_connect(btn_kill, "clicked", G_CALLBACK(on_btn_kill_task), app);
	gtk_box_pack_end(GTK_BOX(top_bar), btn_kill, FALSE, FALSE, 0);

	GtkWidget *btn_end = gtk_button_new_with_label("End Task");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_end), "action-btn");
	g_signal_connect(btn_end, "clicked", G_CALLBACK(on_btn_end_task), app);
	gtk_box_pack_end(GTK_BOX(top_bar), btn_end, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(vbox), top_bar, FALSE, FALSE, 0);

	/* Error info bar (hidden by default) */
	app->info_bar = gtk_info_bar_new();
	gtk_info_bar_set_message_type(GTK_INFO_BAR(app->info_bar), GTK_MESSAGE_ERROR);
	gtk_info_bar_set_show_close_button(GTK_INFO_BAR(app->info_bar), TRUE);
	g_signal_connect(app->info_bar, "response", G_CALLBACK(gtk_widget_hide), NULL);
	app->lbl_info_msg = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(app->lbl_info_msg), 0.0f);
	GtkWidget *content = gtk_info_bar_get_content_area(GTK_INFO_BAR(app->info_bar));
	gtk_container_add(GTK_CONTAINER(content), app->lbl_info_msg);
	gtk_widget_set_no_show_all(app->info_bar, TRUE);
	gtk_box_pack_start(GTK_BOX(vbox), app->info_bar, FALSE, FALSE, 0);

	/* TreeView + ListStore + Filter + Sort */
	app->proc_store = gtk_list_store_new(NUM_COLS,
		G_TYPE_STRING,  /* COL_NAME */
		G_TYPE_INT,     /* COL_PID */
		G_TYPE_STRING,  /* COL_USER */
		G_TYPE_DOUBLE,  /* COL_CPU_VAL */
		G_TYPE_STRING,  /* COL_CPU_STR */
		G_TYPE_UINT64,  /* COL_MEM_VAL */
		G_TYPE_STRING,  /* COL_MEM_STR */
		G_TYPE_STRING   /* COL_STATE */
	);

	app->filter_model = gtk_tree_model_filter_new(GTK_TREE_MODEL(app->proc_store), NULL);
	gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(app->filter_model),
		proc_filter_visible_func, app, NULL);

	app->sort_model = gtk_tree_model_sort_new_with_model(app->filter_model);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(app->sort_model),
		COL_CPU_VAL, GTK_SORT_DESCENDING);

	app->tree_view = gtk_tree_view_new_with_model(app->sort_model);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(app->tree_view), TRUE);
	g_signal_connect(app->tree_view, "button-press-event", G_CALLBACK(on_tree_button_press), app);

	add_tree_col(GTK_TREE_VIEW(app->tree_view), "Name", COL_NAME, COL_NAME, false, 190);
	add_tree_col(GTK_TREE_VIEW(app->tree_view), "PID", COL_PID, COL_PID, true, 70);
	add_tree_col(GTK_TREE_VIEW(app->tree_view), "User", COL_USER, COL_USER, false, 95);
	add_tree_col(GTK_TREE_VIEW(app->tree_view), "CPU %", COL_CPU_STR, COL_CPU_VAL, true, 85);
	add_tree_col(GTK_TREE_VIEW(app->tree_view), "Memory (RSS)", COL_MEM_STR, COL_MEM_VAL, true, 110);
	add_tree_col(GTK_TREE_VIEW(app->tree_view), "State", COL_STATE, COL_STATE, false, 90);

	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scroll), app->tree_view);
	gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

	return vbox;
}

static GtkWidget *create_graph_card(const char *title, GtkWidget **out_header_right,
                                    GtkWidget **out_sub_left, GtkWidget **out_sub_right,
                                    GtkWidget **out_da, GCallback draw_cb, MonitorApp *app, int min_h) {
	GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_style_context_add_class(gtk_widget_get_style_context(card), "perf-card");

	GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *lbl_title = gtk_label_new(title);
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_title), "perf-title");
	gtk_label_set_xalign(GTK_LABEL(lbl_title), 0.0f);
	gtk_box_pack_start(GTK_BOX(top), lbl_title, FALSE, FALSE, 0);

	if (out_header_right) {
		*out_header_right = gtk_label_new("");
		gtk_style_context_add_class(gtk_widget_get_style_context(*out_header_right), "perf-big");
		gtk_label_set_xalign(GTK_LABEL(*out_header_right), 1.0f);
		gtk_box_pack_end(GTK_BOX(top), *out_header_right, FALSE, FALSE, 0);
	}
	gtk_box_pack_start(GTK_BOX(card), top, FALSE, FALSE, 0);

	if (out_sub_left || out_sub_right) {
		GtkWidget *sub = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
		if (out_sub_left) {
			*out_sub_left = gtk_label_new("");
			gtk_style_context_add_class(gtk_widget_get_style_context(*out_sub_left), "perf-sub");
			gtk_label_set_xalign(GTK_LABEL(*out_sub_left), 0.0f);
			gtk_box_pack_start(GTK_BOX(sub), *out_sub_left, FALSE, FALSE, 0);
		}
		if (out_sub_right) {
			*out_sub_right = gtk_label_new("");
			gtk_style_context_add_class(gtk_widget_get_style_context(*out_sub_right), "perf-sub");
			gtk_label_set_xalign(GTK_LABEL(*out_sub_right), 1.0f);
			gtk_box_pack_end(GTK_BOX(sub), *out_sub_right, FALSE, FALSE, 0);
		}
		gtk_box_pack_start(GTK_BOX(card), sub, FALSE, FALSE, 0);
	}

	GtkWidget *da = gtk_drawing_area_new();
	gtk_widget_set_size_request(da, -1, min_h);
	g_signal_connect(da, "draw", draw_cb, app);
	gtk_box_pack_start(GTK_BOX(card), da, TRUE, TRUE, 0);
	if (out_da) *out_da = da;

	return card;
}

static GtkWidget *build_performance_tab(MonitorApp *app) {
	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

	/* CPU Model & System header banner */
	GtkWidget *sys_card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_style_context_add_class(gtk_widget_get_style_context(sys_card), "perf-card");

	app->lbl_cpu_model = gtk_label_new(app->cpu_model_str);
	gtk_style_context_add_class(gtk_widget_get_style_context(app->lbl_cpu_model), "perf-big");
	gtk_label_set_xalign(GTK_LABEL(app->lbl_cpu_model), 0.0f);
	gtk_label_set_ellipsize(GTK_LABEL(app->lbl_cpu_model), PANGO_ELLIPSIZE_END);
	gtk_box_pack_start(GTK_BOX(sys_card), app->lbl_cpu_model, TRUE, TRUE, 0);

	app->lbl_temp = gtk_label_new("Temp: --");
	gtk_style_context_add_class(gtk_widget_get_style_context(app->lbl_temp), "perf-sub");
	gtk_box_pack_end(GTK_BOX(sys_card), app->lbl_temp, FALSE, FALSE, 6);

	app->lbl_uptime = gtk_label_new("Uptime: --");
	gtk_style_context_add_class(gtk_widget_get_style_context(app->lbl_uptime), "perf-sub");
	gtk_box_pack_end(GTK_BOX(sys_card), app->lbl_uptime, FALSE, FALSE, 6);

	gtk_box_pack_start(GTK_BOX(vbox), sys_card, FALSE, FALSE, 0);

	/* Grid of 2x2 graphs + per-core graph */
	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
	gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);

	GtkWidget *card_cpu_total = create_graph_card(
		"CPU Total Utilization (60s)", &app->lbl_cpu_usage, NULL, NULL,
		&app->da_cpu_total, G_CALLBACK(on_draw_cpu_total), app, 110);
	gtk_grid_attach(GTK_GRID(grid), card_cpu_total, 0, 0, 1, 1);

	GtkWidget *card_cpu_cores = create_graph_card(
		"Per-Core CPU Load", NULL, NULL, NULL,
		&app->da_cpu_cores, G_CALLBACK(on_draw_cpu_cores), app, 110);
	gtk_grid_attach(GTK_GRID(grid), card_cpu_cores, 1, 0, 1, 1);

	GtkWidget *card_mem = create_graph_card(
		"Memory & Swap Usage", NULL, &app->lbl_mem_usage, &app->lbl_swap_usage,
		&app->da_mem, G_CALLBACK(on_draw_mem), app, 110);
	gtk_grid_attach(GTK_GRID(grid), card_mem, 0, 1, 2, 1);

	GtkWidget *card_net = create_graph_card(
		"Network Throughput (/proc/net/dev)", NULL, &app->lbl_net_rate, NULL,
		&app->da_net, G_CALLBACK(on_draw_net), app, 100);
	gtk_grid_attach(GTK_GRID(grid), card_net, 0, 2, 1, 1);

	GtkWidget *card_disk = create_graph_card(
		"Disk I/O (/proc/diskstats)", NULL, &app->lbl_disk_rate, NULL,
		&app->da_disk, G_CALLBACK(on_draw_disk), app, 100);
	gtk_grid_attach(GTK_GRID(grid), card_disk, 1, 2, 1, 1);

	gtk_box_pack_start(GTK_BOX(vbox), grid, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(scroll), vbox);
	return scroll;
}

static void on_window_destroy(GtkWidget *w, gpointer user_data) {
	(void)w;
	MonitorApp *app = user_data;
	if (app->timer_id > 0) {
		g_source_remove(app->timer_id);
		app->timer_id = 0;
	}
	if (app->sort_model) {
		g_object_unref(app->sort_model);
		app->sort_model = NULL;
	}
	if (app->filter_model) {
		g_object_unref(app->filter_model);
		app->filter_model = NULL;
	}
	if (app->proc_store) {
		g_object_unref(app->proc_store);
		app->proc_store = NULL;
	}
}

int main(int argc, char **argv) {
	g_set_prgname("diamonds-monitor");
	gtk_init(&argc, &argv);
	diamonds_gtk_apply_theme(MONITOR_EXTRA_CSS);

	MonitorApp app;
	memset(&app, 0, sizeof(app));
	long psize = sysconf(_SC_PAGESIZE);
	app.page_size_kb = (psize > 0) ? (psize / 1024) : 4;
	if (app.page_size_kb <= 0) app.page_size_kb = 4;
	app.num_cores = (int)sysconf(_SC_NPROCESSORS_ONLN);
	if (app.num_cores <= 0) app.num_cores = 1;
	if (app.num_cores > MAX_CORES) app.num_cores = MAX_CORES;

	detect_cpu_model(&app);

	app.window = diamonds_gtk_window_new("Diamonds Task Manager", 900, 620);
	g_signal_connect(app.window, "destroy", G_CALLBACK(on_window_destroy), &app);

	GtkWidget *hbar = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hbar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(hbar), "Task Manager");
	gtk_header_bar_set_subtitle(GTK_HEADER_BAR(hbar), app.cpu_model_str);
	gtk_window_set_titlebar(GTK_WINDOW(app.window), hbar);

	app.notebook = gtk_notebook_new();
	gtk_container_add(GTK_CONTAINER(app.window), app.notebook);

	GtkWidget *proc_tab = build_processes_tab(&app);
	GtkWidget *perf_tab = build_performance_tab(&app);

	gtk_notebook_append_page(GTK_NOTEBOOK(app.notebook), proc_tab, gtk_label_new("Processes"));
	gtk_notebook_append_page(GTK_NOTEBOOK(app.notebook), perf_tab, gtk_label_new("Performance"));

	/* Initial prime + 1.5s timer */
	on_monitor_tick(&app);
	app.timer_id = g_timeout_add(1500, on_monitor_tick, &app);

	gtk_widget_show_all(app.window);
	gtk_widget_hide(app.info_bar);
	gtk_main();

	return 0;
}
