/* Diamonds Music - Lightweight GTK3 Music Player & Library Manager
 * Pure C11, external backend (mpv IPC / ffplay / pw-play / paplay),
 * bounds-checked ID3v2 & FLAC STREAMINFO/Vorbis tag parser, recursive scanner.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <gtk/gtk.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "diamonds_gtk.h"

#define MAX_LIBRARY_FILES 5000
#define MAX_SCAN_DEPTH    6

typedef enum {
	BACKEND_NONE = 0,
	BACKEND_MPV,
	BACKEND_FFPLAY,
	BACKEND_PWPLAY,
	BACKEND_PAPLAY
} AudioBackend;

typedef enum {
	REPEAT_OFF = 0,
	REPEAT_ALL,
	REPEAT_ONE
} RepeatMode;

enum {
	COL_TITLE = 0,
	COL_ARTIST,
	COL_ALBUM,
	COL_DURATION_STR,
	COL_PATH,
	COL_DURATION_SEC,
	NUM_COLS
};

typedef struct {
	char title[256];
	char artist[256];
	char album[256];
	int duration_sec;
} TrackMeta;

typedef struct {
	GtkWidget *window;
	GtkWidget *header_bar;
	GtkWidget *search_entry;
	GtkWidget *lib_view;
	GtkWidget *queue_view;
	GtkListStore *lib_store;
	GtkTreeModel *lib_filter;
	GtkListStore *queue_store;

	/* Now Playing & Transport Controls */
	GtkWidget *lbl_now_title;
	GtkWidget *lbl_now_meta;
	GtkWidget *lbl_backend;
	GtkWidget *lbl_time;
	GtkWidget *seek_scale;
	GtkWidget *vol_scale;
	GtkWidget *btn_play;
	GtkWidget *btn_shuffle;
	GtkWidget *btn_repeat;

	/* Backend & Child Process State */
	AudioBackend backend;
	char *backend_bin;
	GPid child_pid;
	guint child_watch_id;
	bool is_playing;
	bool is_paused;
	bool suppress_exit_advance;
	char mpv_sock_path[108];

	/* Playback Progress & Queue State */
	char current_path[1024];
	char current_title[256];
	char current_artist[256];
	char current_album[256];
	int current_duration;
	double elapsed_sec;
	bool seek_dragging;
	int volume;
	bool shuffle;
	RepeatMode repeat_mode;
	int queue_index;
	int lib_count;
	guint tick_timer_id;
} MusicApp;

static const char *MUSIC_EXTRA_CSS =
	".player-bar { background-color: #1a1b26; border-top: 1px solid #282a36; padding: 10px 16px; }"
	".now-title { font-size: 15px; font-weight: bold; color: #38bdf8; }"
	".now-meta { font-size: 12px; color: #94a3b8; }"
	".pane-header { background-color: #1a1b26; border-bottom: 1px solid #282a36; padding: 6px 10px; }"
	"button.toggle-active { background-color: #0284c7; color: #ffffff; border-color: #38bdf8; }";

static bool is_supported_audio_ext(const char *filename) {
	if (!filename || filename[0] == '.') return false;
	const char *dot = strrchr(filename, '.');
	if (!dot || !dot[1]) return false;
	static const char *exts[] = {
		"mp3", "flac", "ogg", "opus", "wav", "m4a", "aac", NULL
	};
	for (int i = 0; exts[i] != NULL; i++) {
		if (g_ascii_strcasecmp(dot + 1, exts[i]) == 0) return true;
	}
	return false;
}

static void format_time_mmss(int secs, char *out, size_t out_len) {
	if (secs < 0) secs = 0;
	int m = secs / 60;
	int s = secs % 60;
	snprintf(out, out_len, "%02d:%02d", m, s);
}

static void fallback_title_from_path(const char *path, char *out, size_t out_len) {
	char *base = g_path_get_basename(path);
	char *dot = strrchr(base, '.');
	if (dot && dot != base) *dot = '\0';
	snprintf(out, out_len, "%s", base);
	g_free(base);
}

/* Copy clean printable UTF-8/ASCII string from tag payload */
static void decode_text_frame(const uint8_t *data, size_t len, char *out, size_t out_size) {
	if (len == 0 || out_size == 0) return;
	uint8_t enc = data[0];
	const uint8_t *payload = data + 1;
	size_t plen = len - 1;

	char tmp[512] = {0};
	size_t ti = 0;

	if (enc == 0 || enc == 3) {
		/* ISO-8859-1 or UTF-8 */
		for (size_t i = 0; i < plen && ti + 1 < sizeof(tmp); i++) {
			if (payload[i] == 0) break;
			if (payload[i] >= 0x20) tmp[ti++] = (char)payload[i];
		}
	} else if (enc == 1 || enc == 2) {
		/* UTF-16 with BOM or UTF-16BE: extract basic BMP ASCII/Latin chars safely */
		size_t offset = 0;
		bool big_endian = (enc == 2);
		if (plen >= 2) {
			if (payload[0] == 0xFF && payload[1] == 0xFE) {
				big_endian = false;
				offset = 2;
			} else if (payload[0] == 0xFE && payload[1] == 0xFF) {
				big_endian = true;
				offset = 2;
			}
		}
		for (size_t i = offset; i + 1 < plen && ti + 1 < sizeof(tmp); i += 2) {
			uint16_t ch = big_endian
				? (uint16_t)((payload[i] << 8) | payload[i + 1])
				: (uint16_t)((payload[i + 1] << 8) | payload[i]);
			if (ch == 0) break;
			if (ch >= 0x20 && ch < 0x7F) {
				tmp[ti++] = (char)ch;
			} else if (ch >= 0x80 && ch <= 0x7FF && ti + 2 < sizeof(tmp)) {
				tmp[ti++] = (char)(0xC0 | (ch >> 6));
				tmp[ti++] = (char)(0x80 | (ch & 0x3F));
			}
		}
	}
	tmp[ti] = '\0';
	if (ti > 0 && g_utf8_validate(tmp, -1, NULL)) {
		snprintf(out, out_size, "%s", tmp);
	}
}

/* Bounds-checked ID3v2.3 / ID3v2.4 parser + MP3 duration estimate */
static void parse_id3v2_and_mp3(FILE *fp, int64_t file_size, TrackMeta *meta) {
	uint8_t hdr[10];
	if (fseek(fp, 0, SEEK_SET) != 0) return;
	if (fread(hdr, 1, 10, fp) != 10) return;

	size_t audio_offset = 0;
	if (memcmp(hdr, "ID3", 3) == 0 && hdr[3] >= 2 && hdr[3] <= 4) {
		uint8_t ver = hdr[3];
		uint32_t tag_size = ((uint32_t)(hdr[6] & 0x7F) << 21) |
		                    ((uint32_t)(hdr[7] & 0x7F) << 14) |
		                    ((uint32_t)(hdr[8] & 0x7F) << 7)  |
		                    ((uint32_t)(hdr[9] & 0x7F));
		audio_offset = 10 + tag_size;
		uint32_t read_cap = (tag_size > 65536) ? 65536 : tag_size;
		if (read_cap >= 10) {
			uint8_t *buf = malloc(read_cap);
			if (buf) {
				size_t got = fread(buf, 1, read_cap, fp);
				size_t pos = 0;
				while (pos + 10 <= got) {
					if (buf[pos] == 0) break;
					char fid[5] = { (char)buf[pos], (char)buf[pos + 1], (char)buf[pos + 2], (char)buf[pos + 3], '\0' };
					uint32_t fsize = 0;
					if (ver == 4) {
						fsize = ((uint32_t)(buf[pos + 4] & 0x7F) << 21) |
						        ((uint32_t)(buf[pos + 5] & 0x7F) << 14) |
						        ((uint32_t)(buf[pos + 6] & 0x7F) << 7)  |
						        ((uint32_t)(buf[pos + 7] & 0x7F));
					} else {
						fsize = ((uint32_t)buf[pos + 4] << 24) |
						        ((uint32_t)buf[pos + 5] << 16) |
						        ((uint32_t)buf[pos + 6] << 8)  |
						        ((uint32_t)buf[pos + 7]);
					}
					pos += 10;
					if (fsize == 0 || pos + fsize > got) break;

					if (strcmp(fid, "TIT2") == 0) {
						decode_text_frame(buf + pos, fsize, meta->title, sizeof(meta->title));
					} else if (strcmp(fid, "TPE1") == 0) {
						decode_text_frame(buf + pos, fsize, meta->artist, sizeof(meta->artist));
					} else if (strcmp(fid, "TALB") == 0) {
						decode_text_frame(buf + pos, fsize, meta->album, sizeof(meta->album));
					} else if (strcmp(fid, "TLEN") == 0) {
						char tlen_str[64] = {0};
						decode_text_frame(buf + pos, fsize, tlen_str, sizeof(tlen_str));
						int ms = atoi(tlen_str);
						if (ms > 0) meta->duration_sec = ms / 1000;
					}
					pos += fsize;
				}
				free(buf);
				buf = NULL;
			}
		}
	}

	/* Check ID3v1 at end of file if fields still empty */
	if (file_size >= 128 && (!meta->title[0] || !meta->artist[0])) {
		uint8_t v1[128];
		if (fseek(fp, (long)(file_size - 128), SEEK_SET) == 0 && fread(v1, 1, 128, fp) == 128) {
			if (memcmp(v1, "TAG", 3) == 0) {
				if (!meta->title[0]) {
					char t[31] = {0};
					memcpy(t, v1 + 3, 30);
					if (g_utf8_validate(t, -1, NULL)) snprintf(meta->title, sizeof(meta->title), "%s", g_strstrip(t));
				}
				if (!meta->artist[0]) {
					char a[31] = {0};
					memcpy(a, v1 + 33, 30);
					if (g_utf8_validate(a, -1, NULL)) snprintf(meta->artist, sizeof(meta->artist), "%s", g_strstrip(a));
				}
				if (!meta->album[0]) {
					char al[31] = {0};
					memcpy(al, v1 + 63, 30);
					if (g_utf8_validate(al, -1, NULL)) snprintf(meta->album, sizeof(meta->album), "%s", g_strstrip(al));
				}
			}
		}
	}

	/* Estimate duration from first MPEG frame header if not already known */
	if (meta->duration_sec <= 0 && (int64_t)audio_offset < file_size) {
		if (fseek(fp, (long)audio_offset, SEEK_SET) == 0) {
			uint8_t scan[4096];
			size_t n = fread(scan, 1, sizeof(scan), fp);
			static const int bitrates_v1_l3[16] = {
				0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0
			};
			for (size_t i = 0; i + 4 <= n; i++) {
				if (scan[i] == 0xFF && (scan[i + 1] & 0xE0) == 0xE0) {
					int ver_id = (scan[i + 1] >> 3) & 0x03;
					int layer = (scan[i + 1] >> 1) & 0x03;
					int br_idx = (scan[i + 2] >> 4) & 0x0F;
					if (ver_id == 3 && layer == 1 && br_idx > 0 && br_idx < 15) {
						int kbps = bitrates_v1_l3[br_idx];
						if (kbps > 0) {
							int64_t audio_bytes = file_size - (int64_t)audio_offset;
							meta->duration_sec = (int)((audio_bytes * 8) / (kbps * 1000));
							break;
						}
					}
				}
			}
		}
	}
}

/* Bounds-checked FLAC STREAMINFO & Vorbis Comment parser */
static void parse_flac(FILE *fp, TrackMeta *meta) {
	uint8_t magic[4];
	if (fseek(fp, 0, SEEK_SET) != 0) return;
	if (fread(magic, 1, 4, fp) != 4 || memcmp(magic, "fLaC", 4) != 0) return;

	bool last_block = false;
	int blocks_seen = 0;
	while (!last_block && blocks_seen < 16) {
		blocks_seen++;
		uint8_t bhdr[4];
		if (fread(bhdr, 1, 4, fp) != 4) break;
		last_block = (bhdr[0] & 0x80) != 0;
		uint8_t btype = bhdr[0] & 0x7F;
		uint32_t blen = ((uint32_t)bhdr[1] << 16) | ((uint32_t)bhdr[2] << 8) | (uint32_t)bhdr[3];

		if (btype == 0 && blen >= 18) {
			/* STREAMINFO */
			uint8_t sinfo[34];
			size_t to_read = (blen <= sizeof(sinfo)) ? blen : sizeof(sinfo);
			if (fread(sinfo, 1, to_read, fp) != to_read) break;
			if (blen > to_read) fseek(fp, (long)(blen - to_read), SEEK_CUR);

			uint32_t sample_rate = ((uint32_t)sinfo[10] << 12) |
			                       ((uint32_t)sinfo[11] << 4)  |
			                       ((uint32_t)(sinfo[12] >> 4));
			uint64_t total_samples = ((uint64_t)(sinfo[13] & 0x0F) << 32) |
			                         ((uint64_t)sinfo[14] << 24) |
			                         ((uint64_t)sinfo[15] << 16) |
			                         ((uint64_t)sinfo[16] << 8)  |
			                         ((uint64_t)sinfo[17]);
			if (sample_rate > 0 && total_samples > 0) {
				meta->duration_sec = (int)(total_samples / sample_rate);
			}
		} else if (btype == 4 && blen > 8 && blen <= 65536) {
			/* VORBIS_COMMENT (little-endian lengths) */
			uint8_t *buf = malloc(blen);
			if (!buf) break;
			if (fread(buf, 1, blen, fp) == blen) {
				uint32_t pos = 0;
				if (pos + 4 <= blen) {
					uint32_t vendor_len = (uint32_t)buf[pos] |
					                      ((uint32_t)buf[pos + 1] << 8) |
					                      ((uint32_t)buf[pos + 2] << 16) |
					                      ((uint32_t)buf[pos + 3] << 24);
					pos += 4;
					if (vendor_len <= blen - pos) {
						pos += vendor_len;
						if (pos + 4 <= blen) {
							uint32_t count = (uint32_t)buf[pos] |
							                 ((uint32_t)buf[pos + 1] << 8) |
							                 ((uint32_t)buf[pos + 2] << 16) |
							                 ((uint32_t)buf[pos + 3] << 24);
							pos += 4;
							for (uint32_t i = 0; i < count && i < 128 && pos + 4 <= blen; i++) {
								uint32_t clen = (uint32_t)buf[pos] |
								                ((uint32_t)buf[pos + 1] << 8) |
								                ((uint32_t)buf[pos + 2] << 16) |
								                ((uint32_t)buf[pos + 3] << 24);
								pos += 4;
								if (clen > blen - pos) break;
								const char *cstr = (const char *)(buf + pos);
								if (clen > 6 && g_ascii_strncasecmp(cstr, "TITLE=", 6) == 0) {
									size_t vlen = clen - 6;
									if (vlen >= sizeof(meta->title)) vlen = sizeof(meta->title) - 1;
									memcpy(meta->title, cstr + 6, vlen);
									meta->title[vlen] = '\0';
								} else if (clen > 7 && g_ascii_strncasecmp(cstr, "ARTIST=", 7) == 0) {
									size_t vlen = clen - 7;
									if (vlen >= sizeof(meta->artist)) vlen = sizeof(meta->artist) - 1;
									memcpy(meta->artist, cstr + 7, vlen);
									meta->artist[vlen] = '\0';
								} else if (clen > 6 && g_ascii_strncasecmp(cstr, "ALBUM=", 6) == 0) {
									size_t vlen = clen - 6;
									if (vlen >= sizeof(meta->album)) vlen = sizeof(meta->album) - 1;
									memcpy(meta->album, cstr + 6, vlen);
									meta->album[vlen] = '\0';
								}
								pos += clen;
							}
						}
					}
				}
			}
			free(buf);
			buf = NULL;
		} else {
			if (fseek(fp, (long)blen, SEEK_CUR) != 0) break;
		}
	}
}

/* Bounds-checked WAV duration parser */
static void parse_wav(FILE *fp, int64_t file_size, TrackMeta *meta) {
	uint8_t hdr[44];
	if (fseek(fp, 0, SEEK_SET) != 0) return;
	if (fread(hdr, 1, 44, fp) != 44) return;
	if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return;
	uint32_t byte_rate = (uint32_t)hdr[28] |
	                     ((uint32_t)hdr[29] << 8) |
	                     ((uint32_t)hdr[30] << 16) |
	                     ((uint32_t)hdr[31] << 24);
	if (byte_rate > 0 && file_size > 44) {
		meta->duration_sec = (int)((file_size - 44) / byte_rate);
	}
}

static void read_track_metadata(const char *path, TrackMeta *meta) {
	memset(meta, 0, sizeof(*meta));
	struct stat st;
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
		fallback_title_from_path(path, meta->title, sizeof(meta->title));
		snprintf(meta->artist, sizeof(meta->artist), "Unknown Artist");
		snprintf(meta->album, sizeof(meta->album), "Unknown Album");
		return;
	}

	FILE *fp = fopen(path, "rb");
	if (fp) {
		uint8_t sig[4] = {0};
		if (fread(sig, 1, 4, fp) == 4) {
			if (memcmp(sig, "fLaC", 4) == 0) {
				parse_flac(fp, meta);
			} else if (memcmp(sig, "RIFF", 4) == 0) {
				parse_wav(fp, (int64_t)st.st_size, meta);
			} else {
				parse_id3v2_and_mp3(fp, (int64_t)st.st_size, meta);
			}
		}
		fclose(fp);
	}

	if (!meta->title[0]) fallback_title_from_path(path, meta->title, sizeof(meta->title));
	if (!meta->artist[0]) snprintf(meta->artist, sizeof(meta->artist), "Unknown Artist");
	if (!meta->album[0]) snprintf(meta->album, sizeof(meta->album), "Unknown Album");
}

/* Backend Detection & IPC */
static void detect_audio_backend(MusicApp *app) {
	char *p = g_find_program_in_path("mpv");
	if (p) {
		app->backend = BACKEND_MPV;
		app->backend_bin = p;
		return;
	}
	p = g_find_program_in_path("ffplay");
	if (p) {
		app->backend = BACKEND_FFPLAY;
		app->backend_bin = p;
		return;
	}
	p = g_find_program_in_path("pw-play");
	if (p) {
		app->backend = BACKEND_PWPLAY;
		app->backend_bin = p;
		return;
	}
	p = g_find_program_in_path("paplay");
	if (p) {
		app->backend = BACKEND_PAPLAY;
		app->backend_bin = p;
		return;
	}
	app->backend = BACKEND_NONE;
	app->backend_bin = NULL;
}

static const char *backend_name(AudioBackend b) {
	switch (b) {
	case BACKEND_MPV:    return "mpv (IPC)";
	case BACKEND_FFPLAY: return "ffplay";
	case BACKEND_PWPLAY: return "pw-play";
	case BACKEND_PAPLAY: return "paplay";
	default:             return "No backend found";
	}
}

static bool mpv_send_ipc(MusicApp *app, const char *json_cmd) {
	if (app->backend != BACKEND_MPV || !app->mpv_sock_path[0] || app->child_pid <= 0) {
		return false;
	}
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) return false;

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", app->mpv_sock_path);

	bool ok = false;
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
		size_t len = strlen(json_cmd);
		ssize_t w = write(fd, json_cmd, len);
		if (w == (ssize_t)len) ok = true;
	}
	close(fd);
	return ok;
}

static void stop_playback_child(MusicApp *app, bool suppress_advance) {
	app->suppress_exit_advance = suppress_advance;
	if (app->child_watch_id > 0) {
		g_source_remove(app->child_watch_id);
		app->child_watch_id = 0;
	}
	if (app->child_pid > 0) {
		kill(app->child_pid, SIGCONT);
		kill(app->child_pid, SIGTERM);
		g_spawn_close_pid(app->child_pid);
		app->child_pid = 0;
	}
	if (app->mpv_sock_path[0]) {
		unlink(app->mpv_sock_path);
	}
	app->is_playing = false;
	app->is_paused = false;
	if (app->btn_play) {
		gtk_button_set_label(GTK_BUTTON(app->btn_play), "▶ Play");
	}
}

static void play_queue_index(MusicApp *app, int idx);

static void advance_track(MusicApp *app, int delta) {
	int total = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(app->queue_store), NULL);
	if (total <= 0) {
		stop_playback_child(app, true);
		return;
	}

	int next_idx = app->queue_index;
	if (app->repeat_mode == REPEAT_ONE && delta > 0) {
		next_idx = app->queue_index;
	} else if (app->shuffle && total > 1) {
		next_idx = g_random_int_range(0, total);
		if (next_idx == app->queue_index) {
			next_idx = (next_idx + 1) % total;
		}
	} else {
		next_idx = app->queue_index + delta;
		if (next_idx >= total) {
			if (app->repeat_mode == REPEAT_ALL) {
				next_idx = 0;
			} else {
				stop_playback_child(app, true);
				return;
			}
		} else if (next_idx < 0) {
			next_idx = (app->repeat_mode == REPEAT_ALL) ? (total - 1) : 0;
		}
	}
	play_queue_index(app, next_idx);
}

static void on_child_watch_exited(GPid pid, gint status, gpointer user_data) {
	(void)status;
	MusicApp *app = (MusicApp *)user_data;
	g_spawn_close_pid(pid);
	if (app->child_pid == pid) {
		app->child_pid = 0;
		app->child_watch_id = 0;
	}
	if (app->mpv_sock_path[0]) {
		unlink(app->mpv_sock_path);
	}
	app->is_playing = false;
	app->is_paused = false;

	if (!app->suppress_exit_advance) {
		advance_track(app, 1);
	}
	app->suppress_exit_advance = false;
}

static bool spawn_playback_at(MusicApp *app, const char *filepath, double start_sec) {
	if (!filepath || !*filepath || app->backend == BACKEND_NONE || !app->backend_bin) {
		return false;
	}

	stop_playback_child(app, true);

	GPtrArray *argv_arr = g_ptr_array_new_with_free_func(g_free);
	g_ptr_array_add(argv_arr, g_strdup(app->backend_bin));

	if (app->backend == BACKEND_MPV) {
		snprintf(app->mpv_sock_path, sizeof(app->mpv_sock_path),
			"/tmp/diamonds-music-mpv-%d.sock", (int)getpid());
		unlink(app->mpv_sock_path);

		char ipc_arg[160];
		snprintf(ipc_arg, sizeof(ipc_arg), "--input-ipc-server=%s", app->mpv_sock_path);
		char vol_arg[64];
		snprintf(vol_arg, sizeof(vol_arg), "--volume=%d", app->volume);
		char start_arg[64];
		snprintf(start_arg, sizeof(start_arg), "--start=%.1f", start_sec);

		g_ptr_array_add(argv_arr, g_strdup("--no-video"));
		g_ptr_array_add(argv_arr, g_strdup("--really-quiet"));
		g_ptr_array_add(argv_arr, g_strdup(ipc_arg));
		g_ptr_array_add(argv_arr, g_strdup(vol_arg));
		if (start_sec > 0.5) {
			g_ptr_array_add(argv_arr, g_strdup(start_arg));
		}
		g_ptr_array_add(argv_arr, g_strdup("--"));
		g_ptr_array_add(argv_arr, g_strdup(filepath));
	} else if (app->backend == BACKEND_FFPLAY) {
		char vol_str[32];
		snprintf(vol_str, sizeof(vol_str), "%d", app->volume);
		g_ptr_array_add(argv_arr, g_strdup("-nodisp"));
		g_ptr_array_add(argv_arr, g_strdup("-autoexit"));
		g_ptr_array_add(argv_arr, g_strdup("-loglevel"));
		g_ptr_array_add(argv_arr, g_strdup("quiet"));
		g_ptr_array_add(argv_arr, g_strdup("-volume"));
		g_ptr_array_add(argv_arr, g_strdup(vol_str));
		if (start_sec > 0.5) {
			char ss_str[32];
			snprintf(ss_str, sizeof(ss_str), "%.1f", start_sec);
			g_ptr_array_add(argv_arr, g_strdup("-ss"));
			g_ptr_array_add(argv_arr, g_strdup(ss_str));
		}
		g_ptr_array_add(argv_arr, g_strdup(filepath));
	} else {
		/* pw-play or paplay */
		g_ptr_array_add(argv_arr, g_strdup(filepath));
	}
	g_ptr_array_add(argv_arr, NULL);

	GError *err = NULL;
	GPid pid = 0;
	gboolean ok = g_spawn_async(
		NULL,
		(gchar **)argv_arr->pdata,
		NULL,
		G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
		NULL,
		NULL,
		&pid,
		&err);

	g_ptr_array_free(argv_arr, TRUE);

	if (!ok) {
		if (err) g_error_free(err);
		return false;
	}

	app->child_pid = pid;
	app->suppress_exit_advance = false;
	app->child_watch_id = g_child_watch_add(pid, on_child_watch_exited, app);
	app->is_playing = true;
	app->is_paused = false;
	app->elapsed_sec = start_sec;
	gtk_button_set_label(GTK_BUTTON(app->btn_play), "⏸ Pause");
	return true;
}

static void update_now_playing_ui(MusicApp *app) {
	if (!app->current_path[0]) {
		gtk_label_set_text(GTK_LABEL(app->lbl_now_title), "No Track Playing");
		gtk_label_set_text(GTK_LABEL(app->lbl_now_meta), "Select a song from Library or Queue");
		gtk_label_set_text(GTK_LABEL(app->lbl_time), "00:00 / 00:00");
		return;
	}
	gtk_label_set_text(GTK_LABEL(app->lbl_now_title), app->current_title);
	char meta_line[600];
	snprintf(meta_line, sizeof(meta_line), "%s  •  %s", app->current_artist, app->current_album);
	gtk_label_set_text(GTK_LABEL(app->lbl_now_meta), meta_line);

	int max_dur = (app->current_duration > 0) ? app->current_duration : 240;
	gtk_range_set_range(GTK_RANGE(app->seek_scale), 0.0, (double)max_dur);
}

static void play_queue_index(MusicApp *app, int idx) {
	GtkTreeIter iter;
	if (!gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(app->queue_store), &iter, NULL, idx)) {
		return;
	}
	char *title = NULL, *artist = NULL, *album = NULL, *path = NULL;
	int dur = 0;
	gtk_tree_model_get(GTK_TREE_MODEL(app->queue_store), &iter,
		COL_TITLE, &title,
		COL_ARTIST, &artist,
		COL_ALBUM, &album,
		COL_PATH, &path,
		COL_DURATION_SEC, &dur,
		-1);

	if (path) {
		app->queue_index = idx;
		snprintf(app->current_path, sizeof(app->current_path), "%s", path);
		snprintf(app->current_title, sizeof(app->current_title), "%s", title ? title : "Unknown");
		snprintf(app->current_artist, sizeof(app->current_artist), "%s", artist ? artist : "Unknown Artist");
		snprintf(app->current_album, sizeof(app->current_album), "%s", album ? album : "Unknown Album");
		app->current_duration = dur;

		GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->queue_view));
		gtk_tree_selection_select_iter(sel, &iter);

		update_now_playing_ui(app);
		spawn_playback_at(app, app->current_path, 0.0);
	}

	g_free(title);
	g_free(artist);
	g_free(album);
	g_free(path);
}

/* Progress Timer */
static gboolean on_progress_tick(gpointer user_data) {
	MusicApp *app = (MusicApp *)user_data;
	if (app->is_playing && !app->is_paused && app->child_pid > 0) {
		app->elapsed_sec += 0.5;
		if (app->current_duration > 0 && app->elapsed_sec > app->current_duration) {
			app->elapsed_sec = app->current_duration;
		}
		if (!app->seek_dragging) {
			gtk_range_set_value(GTK_RANGE(app->seek_scale), app->elapsed_sec);
		}
		char e_str[32], d_str[32], full[80];
		format_time_mmss((int)app->elapsed_sec, e_str, sizeof(e_str));
		format_time_mmss(app->current_duration, d_str, sizeof(d_str));
		snprintf(full, sizeof(full), "%s / %s", e_str, d_str);
		gtk_label_set_text(GTK_LABEL(app->lbl_time), full);
	}
	return G_SOURCE_CONTINUE;
}

/* Add Track to Library / Queue */
static void add_file_to_store(GtkListStore *store, const char *filepath) {
	TrackMeta meta;
	read_track_metadata(filepath, &meta);
	char dur_str[32];
	if (meta.duration_sec > 0) {
		format_time_mmss(meta.duration_sec, dur_str, sizeof(dur_str));
	} else {
		snprintf(dur_str, sizeof(dur_str), "--:--");
	}

	GtkTreeIter iter;
	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter,
		COL_TITLE, meta.title,
		COL_ARTIST, meta.artist,
		COL_ALBUM, meta.album,
		COL_DURATION_STR, dur_str,
		COL_PATH, filepath,
		COL_DURATION_SEC, meta.duration_sec,
		-1);
}

static void scan_music_dir_recursive(MusicApp *app, const char *dir_path, int depth) {
	if (depth > MAX_SCAN_DEPTH || app->lib_count >= MAX_LIBRARY_FILES) return;
	DIR *dir = opendir(dir_path);
	if (!dir) return;

	struct dirent *de;
	while ((de = readdir(dir)) != NULL && app->lib_count < MAX_LIBRARY_FILES) {
		if (de->d_name[0] == '.') continue;
		char full[2048];
		snprintf(full, sizeof(full), "%s/%s", dir_path, de->d_name);
		struct stat st;
		if (lstat(full, &st) != 0) continue;

		if (S_ISDIR(st.st_mode)) {
			scan_music_dir_recursive(app, full, depth + 1);
		} else if (S_ISREG(st.st_mode) && is_supported_audio_ext(de->d_name)) {
			add_file_to_store(app->lib_store, full);
			app->lib_count++;
		}
	}
	closedir(dir);
}

/* Search Filter Callback */
static gboolean lib_filter_visible_func(GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data) {
	MusicApp *app = (MusicApp *)user_data;
	if (!app->search_entry) return TRUE;
	const char *query = gtk_entry_get_text(GTK_ENTRY(app->search_entry));
	if (!query || !*query) return TRUE;

	char *title = NULL, *artist = NULL, *album = NULL;
	gtk_tree_model_get(model, iter,
		COL_TITLE, &title,
		COL_ARTIST, &artist,
		COL_ALBUM, &album,
		-1);

	char *q_down = g_ascii_strdown(query, -1);
	char *t_down = g_ascii_strdown(title ? title : "", -1);
	char *ar_down = g_ascii_strdown(artist ? artist : "", -1);
	char *al_down = g_ascii_strdown(album ? album : "", -1);

	gboolean match = (strstr(t_down, q_down) != NULL) ||
	                 (strstr(ar_down, q_down) != NULL) ||
	                 (strstr(al_down, q_down) != NULL);

	g_free(q_down);
	g_free(t_down);
	g_free(ar_down);
	g_free(al_down);
	g_free(title);
	g_free(artist);
	g_free(album);
	return match;
}

static void on_search_changed(GtkEditable *editable, gpointer user_data) {
	(void)editable;
	MusicApp *app = (MusicApp *)user_data;
	gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(app->lib_filter));
}

/* UI Event Callbacks */
static void on_lib_row_activated(GtkTreeView *tree_view, GtkTreePath *path,
                                 GtkTreeViewColumn *column, gpointer user_data) {
	(void)tree_view;
	(void)column;
	MusicApp *app = (MusicApp *)user_data;
	GtkTreeIter iter;
	if (!gtk_tree_model_get_iter(app->lib_filter, &iter, path)) return;

	char *title = NULL, *artist = NULL, *album = NULL, *dur_str = NULL, *filepath = NULL;
	int dur_sec = 0;
	gtk_tree_model_get(app->lib_filter, &iter,
		COL_TITLE, &title,
		COL_ARTIST, &artist,
		COL_ALBUM, &album,
		COL_DURATION_STR, &dur_str,
		COL_PATH, &filepath,
		COL_DURATION_SEC, &dur_sec,
		-1);

	if (filepath) {
		GtkTreeIter qiter;
		gtk_list_store_append(app->queue_store, &qiter);
		gtk_list_store_set(app->queue_store, &qiter,
			COL_TITLE, title,
			COL_ARTIST, artist,
			COL_ALBUM, album,
			COL_DURATION_STR, dur_str,
			COL_PATH, filepath,
			COL_DURATION_SEC, dur_sec,
			-1);
		int count = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(app->queue_store), NULL);
		play_queue_index(app, count - 1);
	}

	g_free(title);
	g_free(artist);
	g_free(album);
	g_free(dur_str);
	g_free(filepath);
}

static void on_queue_row_activated(GtkTreeView *tree_view, GtkTreePath *path,
                                   GtkTreeViewColumn *column, gpointer user_data) {
	(void)tree_view;
	(void)column;
	MusicApp *app = (MusicApp *)user_data;
	int *indices = gtk_tree_path_get_indices(path);
	if (indices) {
		play_queue_index(app, indices[0]);
	}
}

static void on_btn_add_to_queue_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	MusicApp *app = (MusicApp *)user_data;
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->lib_view));
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;

	char *title = NULL, *artist = NULL, *album = NULL, *dur_str = NULL, *filepath = NULL;
	int dur_sec = 0;
	gtk_tree_model_get(model, &iter,
		COL_TITLE, &title,
		COL_ARTIST, &artist,
		COL_ALBUM, &album,
		COL_DURATION_STR, &dur_str,
		COL_PATH, &filepath,
		COL_DURATION_SEC, &dur_sec,
		-1);

	if (filepath) {
		GtkTreeIter qiter;
		gtk_list_store_append(app->queue_store, &qiter);
		gtk_list_store_set(app->queue_store, &qiter,
			COL_TITLE, title,
			COL_ARTIST, artist,
			COL_ALBUM, album,
			COL_DURATION_STR, dur_str,
			COL_PATH, filepath,
			COL_DURATION_SEC, dur_sec,
			-1);
	}
	g_free(title);
	g_free(artist);
	g_free(album);
	g_free(dur_str);
	g_free(filepath);
}

static void on_btn_remove_queue_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	MusicApp *app = (MusicApp *)user_data;
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->queue_view));
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
		gtk_list_store_remove(app->queue_store, &iter);
	}
}

static void on_btn_clear_queue_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	MusicApp *app = (MusicApp *)user_data;
	stop_playback_child(app, true);
	gtk_list_store_clear(app->queue_store);
	app->queue_index = -1;
	app->current_path[0] = '\0';
	update_now_playing_ui(app);
}

static void on_btn_play_pause_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	MusicApp *app = (MusicApp *)user_data;
	if (!app->is_playing) {
		int qcount = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(app->queue_store), NULL);
		if (qcount > 0) {
			int idx = (app->queue_index >= 0 && app->queue_index < qcount) ? app->queue_index : 0;
			play_queue_index(app, idx);
		}
		return;
	}

	if (!app->is_paused) {
		/* Pause */
		if (!mpv_send_ipc(app, "{\"command\": [\"set_property\", \"pause\", true]}\n")) {
			if (app->child_pid > 0) kill(app->child_pid, SIGSTOP);
		}
		app->is_paused = true;
		gtk_button_set_label(GTK_BUTTON(app->btn_play), "▶ Resume");
	} else {
		/* Resume */
		if (!mpv_send_ipc(app, "{\"command\": [\"set_property\", \"pause\", false]}\n")) {
			if (app->child_pid > 0) kill(app->child_pid, SIGCONT);
		}
		app->is_paused = false;
		gtk_button_set_label(GTK_BUTTON(app->btn_play), "⏸ Pause");
	}
}

static void on_btn_stop_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	MusicApp *app = (MusicApp *)user_data;
	stop_playback_child(app, true);
	app->elapsed_sec = 0.0;
	gtk_range_set_value(GTK_RANGE(app->seek_scale), 0.0);
}

static void on_btn_prev_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	advance_track((MusicApp *)user_data, -1);
}

static void on_btn_next_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	advance_track((MusicApp *)user_data, 1);
}

static void on_btn_shuffle_clicked(GtkButton *btn, gpointer user_data) {
	MusicApp *app = (MusicApp *)user_data;
	app->shuffle = !app->shuffle;
	GtkStyleContext *sc = gtk_widget_get_style_context(GTK_WIDGET(btn));
	if (app->shuffle) {
		gtk_style_context_add_class(sc, "toggle-active");
	} else {
		gtk_style_context_remove_class(sc, "toggle-active");
	}
}

static void on_btn_repeat_clicked(GtkButton *btn, gpointer user_data) {
	MusicApp *app = (MusicApp *)user_data;
	app->repeat_mode = (RepeatMode)((app->repeat_mode + 1) % 3);
	GtkStyleContext *sc = gtk_widget_get_style_context(GTK_WIDGET(btn));
	if (app->repeat_mode == REPEAT_OFF) {
		gtk_button_set_label(btn, "🔁 Repeat: Off");
		gtk_style_context_remove_class(sc, "toggle-active");
	} else if (app->repeat_mode == REPEAT_ALL) {
		gtk_button_set_label(btn, "🔁 Repeat: All");
		gtk_style_context_add_class(sc, "toggle-active");
	} else {
		gtk_button_set_label(btn, "🔂 Repeat: One");
		gtk_style_context_add_class(sc, "toggle-active");
	}
}

/* Seek Bar Interaction */
static gboolean on_seek_button_press(GtkWidget *w, GdkEventButton *ev, gpointer user_data) {
	(void)w;
	(void)ev;
	MusicApp *app = (MusicApp *)user_data;
	app->seek_dragging = true;
	return FALSE;
}

static gboolean on_seek_button_release(GtkWidget *w, GdkEventButton *ev, gpointer user_data) {
	(void)ev;
	MusicApp *app = (MusicApp *)user_data;
	app->seek_dragging = false;
	if (!app->is_playing || !app->current_path[0]) return FALSE;

	double target = gtk_range_get_value(GTK_RANGE(w));
	app->elapsed_sec = target;
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "{\"command\": [\"seek\", %.1f, \"absolute\"]}\n", target);
	if (!mpv_send_ipc(app, cmd)) {
		if (app->backend == BACKEND_FFPLAY) {
			spawn_playback_at(app, app->current_path, target);
		}
	}
	return FALSE;
}

/* Volume Slider */
static void on_volume_changed(GtkRange *range, gpointer user_data) {
	MusicApp *app = (MusicApp *)user_data;
	app->volume = (int)gtk_range_get_value(range);
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "{\"command\": [\"set_property\", \"volume\", %d]}\n", app->volume);
	if (!mpv_send_ipc(app, cmd)) {
		char sys_cmd[160];
		snprintf(sys_cmd, sizeof(sys_cmd),
			"pactl set-sink-volume @DEFAULT_SINK@ %d%% 2>/dev/null || "
			"wpctl set-volume @DEFAULT_AUDIO_SINK@ %.2f 2>/dev/null",
			app->volume, (double)app->volume / 100.0);
		int r = system(sys_cmd);
		(void)r;
	}
}

/* File / Folder Choosers & Drag-and-Drop */
static void on_btn_add_files_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	MusicApp *app = (MusicApp *)user_data;
	GtkWidget *dlg = gtk_file_chooser_dialog_new(
		"Add Audio Files", GTK_WINDOW(app->window),
		GTK_FILE_CHOOSER_ACTION_OPEN,
		"_Cancel", GTK_RESPONSE_CANCEL,
		"_Add", GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dlg), TRUE);

	if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
		GSList *files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dlg));
		for (GSList *l = files; l != NULL; l = l->next) {
			const char *fn = (const char *)l->data;
			if (is_supported_audio_ext(fn)) {
				add_file_to_store(app->lib_store, fn);
				add_file_to_store(app->queue_store, fn);
			}
			g_free(l->data);
		}
		g_slist_free(files);
	}
	gtk_widget_destroy(dlg);
}

static void on_btn_scan_folder_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	MusicApp *app = (MusicApp *)user_data;
	GtkWidget *dlg = gtk_file_chooser_dialog_new(
		"Scan Music Folder", GTK_WINDOW(app->window),
		GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
		"_Cancel", GTK_RESPONSE_CANCEL,
		"_Scan", GTK_RESPONSE_ACCEPT, NULL);

	if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
		char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
		if (folder) {
			gtk_list_store_clear(app->lib_store);
			app->lib_count = 0;
			scan_music_dir_recursive(app, folder, 0);
			g_free(folder);
		}
	}
	gtk_widget_destroy(dlg);
}

static void on_drag_data_received(GtkWidget *widget, GdkDragContext *context,
                                  gint x, gint y, GtkSelectionData *data,
                                  guint info, guint time, gpointer user_data) {
	(void)widget;
	(void)x;
	(void)y;
	(void)info;
	MusicApp *app = (MusicApp *)user_data;
	gchar **uris = gtk_selection_data_get_uris(data);
	if (uris) {
		for (int i = 0; uris[i] != NULL; i++) {
			gchar *path = g_filename_from_uri(uris[i], NULL, NULL);
			if (path) {
				struct stat st;
				if (stat(path, &st) == 0) {
					if (S_ISDIR(st.st_mode)) {
						scan_music_dir_recursive(app, path, 0);
					} else if (S_ISREG(st.st_mode) && is_supported_audio_ext(path)) {
						add_file_to_store(app->lib_store, path);
						add_file_to_store(app->queue_store, path);
					}
				}
				g_free(path);
			}
		}
		g_strfreev(uris);
	}
	gtk_drag_finish(context, TRUE, FALSE, time);
}

static void add_tree_columns(GtkTreeView *tv) {
	GtkCellRenderer *r_text = gtk_cell_renderer_text_new();
	g_object_set(r_text, "ellipsize", PANGO_ELLIPSIZE_END, NULL);

	GtkTreeViewColumn *c_title = gtk_tree_view_column_new_with_attributes(
		"Title", r_text, "text", COL_TITLE, NULL);
	gtk_tree_view_column_set_expand(c_title, TRUE);
	gtk_tree_view_column_set_resizable(c_title, TRUE);
	gtk_tree_view_append_column(tv, c_title);

	GtkTreeViewColumn *c_artist = gtk_tree_view_column_new_with_attributes(
		"Artist", r_text, "text", COL_ARTIST, NULL);
	gtk_tree_view_column_set_min_width(c_artist, 120);
	gtk_tree_view_column_set_resizable(c_artist, TRUE);
	gtk_tree_view_append_column(tv, c_artist);

	GtkCellRenderer *r_dur = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *c_dur = gtk_tree_view_column_new_with_attributes(
		"Time", r_dur, "text", COL_DURATION_STR, NULL);
	gtk_tree_view_append_column(tv, c_dur);
}

static void on_app_destroy(GtkWidget *w, gpointer user_data) {
	(void)w;
	MusicApp *app = (MusicApp *)user_data;
	if (app->tick_timer_id > 0) {
		g_source_remove(app->tick_timer_id);
		app->tick_timer_id = 0;
	}
	stop_playback_child(app, true);
	if (app->backend_bin) {
		g_free(app->backend_bin);
		app->backend_bin = NULL;
	}
	if (app->lib_filter) {
		g_object_unref(app->lib_filter);
		app->lib_filter = NULL;
	}
	if (app->lib_store) {
		g_object_unref(app->lib_store);
		app->lib_store = NULL;
	}
	if (app->queue_store) {
		g_object_unref(app->queue_store);
		app->queue_store = NULL;
	}
}

int main(int argc, char **argv) {
	g_set_prgname("diamonds-music");
	gtk_init(&argc, &argv);
	diamonds_gtk_apply_theme(MUSIC_EXTRA_CSS);

	MusicApp app = {0};
	app.volume = 80;
	app.queue_index = -1;
	detect_audio_backend(&app);

	app.window = diamonds_gtk_window_new("Diamonds Music", 960, 600);
	g_signal_connect(app.window, "destroy", G_CALLBACK(on_app_destroy), &app);

	/* Enable Drag & Drop for audio files/folders */
	gtk_drag_dest_set(app.window, GTK_DEST_DEFAULT_ALL, NULL, 0, GDK_ACTION_COPY);
	gtk_drag_dest_add_uri_targets(app.window);
	g_signal_connect(app.window, "drag-data-received", G_CALLBACK(on_drag_data_received), &app);

	/* HeaderBar */
	app.header_bar = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(app.header_bar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(app.header_bar), "Diamonds Music");
	char sub[128];
	snprintf(sub, sizeof(sub), "Audio Backend: %s", backend_name(app.backend));
	gtk_header_bar_set_subtitle(GTK_HEADER_BAR(app.header_bar), sub);
	gtk_window_set_titlebar(GTK_WINDOW(app.window), app.header_bar);

	GtkWidget *btn_add_files = gtk_button_new_with_label("+ Add Files");
	g_signal_connect(btn_add_files, "clicked", G_CALLBACK(on_btn_add_files_clicked), &app);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app.header_bar), btn_add_files);

	GtkWidget *btn_scan = gtk_button_new_with_label("Scan Folder");
	g_signal_connect(btn_scan, "clicked", G_CALLBACK(on_btn_scan_folder_clicked), &app);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app.header_bar), btn_scan);

	app.search_entry = gtk_search_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(app.search_entry), "Filter library (title, artist, album)...");
	gtk_widget_set_size_request(app.search_entry, 260, -1);
	g_signal_connect(app.search_entry, "changed", G_CALLBACK(on_search_changed), &app);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(app.header_bar), app.search_entry);

	/* Main vertical container */
	GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(app.window), main_vbox);

	/* Split Pane: Library (Left) & Playlist Queue (Right) */
	GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_set_position(GTK_PANED(paned), 540);
	gtk_box_pack_start(GTK_BOX(main_vbox), paned, TRUE, TRUE, 0);

	/* Models */
	app.lib_store = gtk_list_store_new(NUM_COLS,
		G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
	app.lib_filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(app.lib_store), NULL);
	gtk_tree_model_filter_set_visible_func(
		GTK_TREE_MODEL_FILTER(app.lib_filter), lib_filter_visible_func, &app, NULL);

	app.queue_store = gtk_list_store_new(NUM_COLS,
		G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);

	/* Left Pane: Music Library */
	GtkWidget *lib_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *lib_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_style_context_add_class(gtk_widget_get_style_context(lib_hdr), "pane-header");
	GtkWidget *lbl_lib = gtk_label_new("Music Library (Double-click to play)");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_lib), "card-title");
	gtk_box_pack_start(GTK_BOX(lib_hdr), lbl_lib, FALSE, FALSE, 0);

	GtkWidget *btn_enq = gtk_button_new_with_label("Queue Selected →");
	g_signal_connect(btn_enq, "clicked", G_CALLBACK(on_btn_add_to_queue_clicked), &app);
	gtk_box_pack_end(GTK_BOX(lib_hdr), btn_enq, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(lib_vbox), lib_hdr, FALSE, FALSE, 0);

	GtkWidget *lib_scroll = gtk_scrolled_window_new(NULL, NULL);
	app.lib_view = gtk_tree_view_new_with_model(app.lib_filter);
	add_tree_columns(GTK_TREE_VIEW(app.lib_view));
	g_signal_connect(app.lib_view, "row-activated", G_CALLBACK(on_lib_row_activated), &app);
	gtk_container_add(GTK_CONTAINER(lib_scroll), app.lib_view);
	gtk_box_pack_start(GTK_BOX(lib_vbox), lib_scroll, TRUE, TRUE, 0);
	gtk_paned_pack1(GTK_PANED(paned), lib_vbox, TRUE, FALSE);

	/* Right Pane: Playlist Queue */
	GtkWidget *q_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *q_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_style_context_add_class(gtk_widget_get_style_context(q_hdr), "pane-header");
	GtkWidget *lbl_q = gtk_label_new("Playlist Queue");
	gtk_style_context_add_class(gtk_widget_get_style_context(lbl_q), "card-title");
	gtk_box_pack_start(GTK_BOX(q_hdr), lbl_q, FALSE, FALSE, 0);

	GtkWidget *btn_q_clear = gtk_button_new_with_label("Clear");
	g_signal_connect(btn_q_clear, "clicked", G_CALLBACK(on_btn_clear_queue_clicked), &app);
	gtk_box_pack_end(GTK_BOX(q_hdr), btn_q_clear, FALSE, FALSE, 0);

	GtkWidget *btn_q_rem = gtk_button_new_with_label("Remove");
	g_signal_connect(btn_q_rem, "clicked", G_CALLBACK(on_btn_remove_queue_clicked), &app);
	gtk_box_pack_end(GTK_BOX(q_hdr), btn_q_rem, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(q_vbox), q_hdr, FALSE, FALSE, 0);

	GtkWidget *q_scroll = gtk_scrolled_window_new(NULL, NULL);
	app.queue_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app.queue_store));
	add_tree_columns(GTK_TREE_VIEW(app.queue_view));
	g_signal_connect(app.queue_view, "row-activated", G_CALLBACK(on_queue_row_activated), &app);
	gtk_container_add(GTK_CONTAINER(q_scroll), app.queue_view);
	gtk_box_pack_start(GTK_BOX(q_vbox), q_scroll, TRUE, TRUE, 0);
	gtk_paned_pack2(GTK_PANED(paned), q_vbox, TRUE, FALSE);

	/* Bottom Player Control Deck */
	GtkWidget *player_bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_style_context_add_class(gtk_widget_get_style_context(player_bar), "player-bar");

	/* Seek row */
	GtkWidget *seek_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	app.seek_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 240.0, 1.0);
	gtk_scale_set_draw_value(GTK_SCALE(app.seek_scale), FALSE);
	g_signal_connect(app.seek_scale, "button-press-event", G_CALLBACK(on_seek_button_press), &app);
	g_signal_connect(app.seek_scale, "button-release-event", G_CALLBACK(on_seek_button_release), &app);
	gtk_box_pack_start(GTK_BOX(seek_row), app.seek_scale, TRUE, TRUE, 0);

	app.lbl_time = gtk_label_new("00:00 / 00:00");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_time), "dim");
	gtk_box_pack_end(GTK_BOX(seek_row), app.lbl_time, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(player_bar), seek_row, FALSE, FALSE, 0);

	/* Transport & Volume row */
	GtkWidget *ctrl_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

	GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_widget_set_size_request(info_box, 260, -1);
	app.lbl_now_title = gtk_label_new("No Track Playing");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_now_title), "now-title");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_now_title), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(app.lbl_now_title), PANGO_ELLIPSIZE_END);
	gtk_box_pack_start(GTK_BOX(info_box), app.lbl_now_title, FALSE, FALSE, 0);

	app.lbl_now_meta = gtk_label_new("Select a song from Library or Queue");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_now_meta), "now-meta");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_now_meta), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(app.lbl_now_meta), PANGO_ELLIPSIZE_END);
	gtk_box_pack_start(GTK_BOX(info_box), app.lbl_now_meta, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(ctrl_row), info_box, FALSE, FALSE, 0);

	/* Center transport buttons */
	GtkWidget *transport_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign(transport_box, GTK_ALIGN_CENTER);

	GtkWidget *btn_prev = gtk_button_new_with_label("⏮ Prev");
	g_signal_connect(btn_prev, "clicked", G_CALLBACK(on_btn_prev_clicked), &app);
	gtk_box_pack_start(GTK_BOX(transport_box), btn_prev, FALSE, FALSE, 0);

	app.btn_play = gtk_button_new_with_label("▶ Play");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.btn_play), "action-btn");
	g_signal_connect(app.btn_play, "clicked", G_CALLBACK(on_btn_play_pause_clicked), &app);
	gtk_box_pack_start(GTK_BOX(transport_box), app.btn_play, FALSE, FALSE, 0);

	GtkWidget *btn_stop = gtk_button_new_with_label("⏹ Stop");
	g_signal_connect(btn_stop, "clicked", G_CALLBACK(on_btn_stop_clicked), &app);
	gtk_box_pack_start(GTK_BOX(transport_box), btn_stop, FALSE, FALSE, 0);

	GtkWidget *btn_next = gtk_button_new_with_label("Next ⏭");
	g_signal_connect(btn_next, "clicked", G_CALLBACK(on_btn_next_clicked), &app);
	gtk_box_pack_start(GTK_BOX(transport_box), btn_next, FALSE, FALSE, 0);

	app.btn_shuffle = gtk_button_new_with_label("🔀 Shuffle");
	g_signal_connect(app.btn_shuffle, "clicked", G_CALLBACK(on_btn_shuffle_clicked), &app);
	gtk_box_pack_start(GTK_BOX(transport_box), app.btn_shuffle, FALSE, FALSE, 0);

	app.btn_repeat = gtk_button_new_with_label("🔁 Repeat: Off");
	g_signal_connect(app.btn_repeat, "clicked", G_CALLBACK(on_btn_repeat_clicked), &app);
	gtk_box_pack_start(GTK_BOX(transport_box), app.btn_repeat, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(ctrl_row), transport_box, TRUE, TRUE, 0);

	/* Right volume slider */
	GtkWidget *vol_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *lbl_vol = gtk_label_new("🔊");
	gtk_box_pack_start(GTK_BOX(vol_box), lbl_vol, FALSE, FALSE, 0);
	app.vol_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
	gtk_scale_set_draw_value(GTK_SCALE(app.vol_scale), FALSE);
	gtk_range_set_value(GTK_RANGE(app.vol_scale), app.volume);
	gtk_widget_set_size_request(app.vol_scale, 110, -1);
	g_signal_connect(app.vol_scale, "value-changed", G_CALLBACK(on_volume_changed), &app);
	gtk_box_pack_start(GTK_BOX(vol_box), app.vol_scale, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(ctrl_row), vol_box, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(player_bar), ctrl_row, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(main_vbox), player_bar, FALSE, FALSE, 0);

	/* Scan ~/Music initially */
	const char *home = getenv("HOME");
	if (home) {
		char music_dir[1024];
		snprintf(music_dir, sizeof(music_dir), "%s/Music", home);
		struct stat st;
		if (stat(music_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
			scan_music_dir_recursive(&app, music_dir, 0);
		}
	}

	/* Handle argv files/directories */
	bool played_argv = false;
	for (int i = 1; i < argc; i++) {
		struct stat st;
		if (stat(argv[i], &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) {
			scan_music_dir_recursive(&app, argv[i], 0);
		} else if (S_ISREG(st.st_mode) && is_supported_audio_ext(argv[i])) {
			add_file_to_store(app.lib_store, argv[i]);
			add_file_to_store(app.queue_store, argv[i]);
			if (!played_argv) {
				int count = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(app.queue_store), NULL);
				play_queue_index(&app, count - 1);
				played_argv = true;
			}
		}
	}

	app.tick_timer_id = g_timeout_add(500, on_progress_tick, &app);

	gtk_widget_show_all(app.window);
	gtk_main();
	return 0;
}
