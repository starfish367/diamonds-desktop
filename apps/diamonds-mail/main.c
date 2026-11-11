/*
 * diamonds-mail: Lightweight Local Maildir & Mbox Client
 * Pure C11 + GTK3, RFC 5322 header folding, RFC 2047 (B/Q) decoding,
 * Quoted-Printable & Base64 decoding, MIME multipart parsing, attachments,
 * and compose/send via sendmail/msmtp or local Sent/Outbox Maildir.
 */
#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#include "diamonds_gtk.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pango/pango.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_FOLDERS 64
#define MAX_MESSAGES 1024
#define MAX_ATTACHMENTS 16
#define MAX_MSG_FILE_BYTES (2 * 1024 * 1024)

typedef enum {
	FOLDER_TYPE_MAILDIR = 0,
	FOLDER_TYPE_MBOX
} FolderType;

typedef struct {
	char label[128];
	char path[512];
	FolderType type;
	bool is_trash;
} MailFolder;

typedef struct {
	char filename[128];
	char content_type[64];
	uint8_t *data;
	size_t size;
} Attachment;

typedef struct {
	char filepath[1024];
	off_t mbox_offset;
	size_t mbox_length;
	bool is_mbox;
	bool unread;

	char from[256];
	char to[256];
	char cc[256];
	char subject[256];
	char date_str[128];
	int64_t timestamp;
} MailSummary;

typedef struct {
	GtkWidget *window;

	/* Left pane: folders */
	GtkListStore *folder_store;
	GtkWidget *folder_view;
	MailFolder folders[MAX_FOLDERS];
	int folder_count;
	int active_folder_idx;

	/* Middle pane: message list */
	GtkWidget *search_entry;
	char search_text[128];
	GtkListStore *msg_store;
	GtkTreeModel *msg_filter;
	GtkTreeModel *msg_sort;
	GtkWidget *msg_view;

	MailSummary messages[MAX_MESSAGES];
	int msg_count;
	int selected_msg_idx;

	/* Right pane: reader */
	GtkWidget *lbl_hdr_subject;
	GtkWidget *lbl_hdr_from;
	GtkWidget *lbl_hdr_to;
	GtkWidget *lbl_hdr_date;
	GtkWidget *attach_box;
	GtkTextBuffer *body_buffer;
	GtkWidget *body_view;

	/* Info bar */
	GtkWidget *info_bar;
	GtkWidget *lbl_info;

	Attachment active_attachments[MAX_ATTACHMENTS];
	int active_attach_count;

	char local_maildir_root[512];
} MailApp;

enum {
	FCOL_LABEL = 0,
	FCOL_INDEX,
	FCOL_COUNT
};

enum {
	MCOL_FROM = 0,
	MCOL_SUBJECT,
	MCOL_DATE,
	MCOL_TIMESTAMP,
	MCOL_WEIGHT,
	MCOL_INDEX,
	MCOL_COUNT
};

static const char *MAIL_EXTRA_CSS =
	".reader-header { background-color: #1a1b26; border-bottom: 1px solid #282a36; padding: 12px 16px; }"
	".reader-subj { font-size: 16px; font-weight: bold; color: #f8fafc; }"
	".reader-meta { font-size: 12px; color: #94a3b8; }"
	".attach-bar { background-color: #1e202e; border-bottom: 1px solid #282a36; padding: 6px 12px; }";

/* Directory creation helper */
static int mkdir_p(const char *path, mode_t mode) {
	char tmp[512];
	snprintf(tmp, sizeof(tmp), "%s", path);
	size_t len = strlen(tmp);
	if (len == 0) return -1;
	if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
			*p = '/';
		}
	}
	if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
	return 0;
}

static int ensure_maildir_box(const char *box_path) {
	char sub[600];
	const char *dirs[3] = {"cur", "new", "tmp"};
	if (mkdir_p(box_path, 0700) != 0) return -1;
	for (int i = 0; i < 3; i++) {
		snprintf(sub, sizeof(sub), "%s/%s", box_path, dirs[i]);
		if (mkdir_p(sub, 0700) != 0) return -1;
	}
	return 0;
}

static bool dir_is_empty(const char *path) {
	DIR *d = opendir(path);
	if (!d) return true;
	struct dirent *ent;
	bool empty = true;
	while ((ent = readdir(d)) != NULL) {
		if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
			empty = false;
			break;
		}
	}
	closedir(d);
	return empty;
}

static void bootstrap_local_maildir(MailApp *app) {
	const char *home = getenv("HOME");
	if (!home || !*home) home = "/tmp";
	snprintf(app->local_maildir_root, sizeof(app->local_maildir_root),
		"%s/.local/share/diamonds-mail/Maildir", home);

	ensure_maildir_box(app->local_maildir_root);

	const char *std_boxes[] = {".Sent", ".Drafts", ".Trash", ".Outbox"};
	for (size_t i = 0; i < sizeof(std_boxes) / sizeof(std_boxes[0]); i++) {
		char p[600];
		snprintf(p, sizeof(p), "%s/%s", app->local_maildir_root, std_boxes[i]);
		ensure_maildir_box(p);
	}

	char new_dir[600], cur_dir[600];
	snprintf(new_dir, sizeof(new_dir), "%s/new", app->local_maildir_root);
	snprintf(cur_dir, sizeof(cur_dir), "%s/cur", app->local_maildir_root);

	if (dir_is_empty(new_dir) && dir_is_empty(cur_dir)) {
		char msg_path[640];
		time_t now = time(NULL);
		snprintf(msg_path, sizeof(msg_path), "%s/%" PRId64 ".M00001P%d.diamonds",
			new_dir, (int64_t)now, (int)getpid());

		char date_hdr[128];
		struct tm tm_now;
		if (localtime_r(&now, &tm_now)) {
			strftime(date_hdr, sizeof(date_hdr), "%a, %d %b %Y %H:%M:%S %z", &tm_now);
		} else {
			snprintf(date_hdr, sizeof(date_hdr), "Mon, 09 Nov 2026 12:00:00 +0000");
		}

		const char *welcome_template =
			"From: =?UTF-8?B?RGlhbW9uZHMgRGVza3RvcCBUZWFt?= <team@diamonds.local>\r\n"
			"To: User <user@localhost>\r\n"
			"Subject: =?UTF-8?Q?Welcome_to_Diamonds_Mail_=E2=97=86_Lightweight_Wayland_Client?=\r\n"
			"Date: %s\r\n"
			"Message-ID: <welcome-001@diamonds.local>\r\n"
			"MIME-Version: 1.0\r\n"
			"Content-Type: multipart/mixed; boundary=\"diamonds-welcome-boundary\"\r\n"
			"\r\n"
			"--diamonds-welcome-boundary\r\n"
			"Content-Type: text/plain; charset=UTF-8\r\n"
			"Content-Transfer-Encoding: quoted-printable\r\n"
			"\r\n"
			"Welcome to Diamonds Mail!=0D=0A"
			"=0D=0A"
			"This lightweight C11 + GTK3 client reads local Maildir and mbox spools directly=0D=0A"
			"with zero external network daemon overhead.=0D=0A"
			"=0D=0A"
			"Key features supported:=0D=0A"
			"  * Maildir (~/Maildir and ~/.local/share/diamonds-mail/Maildir)=0D=0A"
			"  * Local mbox spools (/var/mail/$USER and ~/mbox)=0D=0A"
			"  * RFC 5322 folded headers & RFC 2047 (Base64 / Quoted-Printable) subjects=0D=0A"
			"  * MIME multipart parsing with attachment extraction and saving=0D=0A"
			"  * Compose, Reply, Forward via sendmail/msmtp or local Outbox/Sent Maildir=0D=0A"
			"\r\n"
			"--diamonds-welcome-boundary\r\n"
			"Content-Type: text/plain; name=\"diamonds-readme.txt\"\r\n"
			"Content-Disposition: attachment; filename=\"diamonds-readme.txt\"\r\n"
			"Content-Transfer-Encoding: base64\r\n"
			"\r\n"
			"RGlhbW9uZHMgRGVza3RvcCAoV2F5bGFuZCArIHdscm9vdHMpClRhcmdldDogYWFyY2g2NCAxLjhH\r\n"
			"QiBSQU0gJiB4ODZfNjQKRnVsbHkgb3Blbi1zb3VyY2UgbmF0aXZlIEMxMSB1dGlsaXRpZXMuCg==\r\n"
			"--diamonds-welcome-boundary--\r\n";

		int fd = open(msg_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
		if (fd >= 0) {
			char formatted[2048];
			int len = snprintf(formatted, sizeof(formatted), welcome_template, date_hdr);
			if (len > 0) {
				size_t to_write = (size_t)len < sizeof(formatted) ? (size_t)len : sizeof(formatted) - 1;
				ssize_t w = write(fd, formatted, to_write);
				(void)w;
			}
			close(fd);
		}
	}
}

/* Base64 & Quoted-Printable Decoders */
static int b64_val(unsigned char c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

static uint8_t *decode_base64(const char *src, size_t src_len, size_t *out_len) {
	if (out_len) *out_len = 0;
	if (!src) return NULL;
	size_t cap = (src_len * 3) / 4 + 8;
	uint8_t *out = malloc(cap);
	if (!out) return NULL;

	size_t o = 0;
	uint32_t acc = 0;
	int bits = 0;
	for (size_t i = 0; i < src_len; i++) {
		unsigned char c = (unsigned char)src[i];
		if (c == '=') break;
		int v = b64_val(c);
		if (v < 0) continue;
		acc = (acc << 6) | (uint32_t)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			if (o < cap - 1) {
				out[o++] = (uint8_t)((acc >> bits) & 0xFFu);
			}
		}
	}
	out[o] = '\0';
	if (out_len) *out_len = o;
	return out;
}

static int hex_val(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return -1;
}

static uint8_t *decode_qp(const char *src, size_t src_len, bool rfc2047_underscore, size_t *out_len) {
	if (out_len) *out_len = 0;
	if (!src) return NULL;
	uint8_t *out = malloc(src_len + 4);
	if (!out) return NULL;

	size_t o = 0;
	for (size_t i = 0; i < src_len; i++) {
		char c = src[i];
		if (rfc2047_underscore && c == '_') {
			out[o++] = ' ';
		} else if (c == '=') {
			if (i + 1 < src_len && src[i + 1] == '\n') {
				i += 1;
			} else if (i + 2 < src_len && src[i + 1] == '\r' && src[i + 2] == '\n') {
				i += 2;
			} else if (i + 2 < src_len) {
				int h1 = hex_val(src[i + 1]);
				int h2 = hex_val(src[i + 2]);
				if (h1 >= 0 && h2 >= 0) {
					out[o++] = (uint8_t)((h1 << 4) | h2);
					i += 2;
				} else {
					out[o++] = (uint8_t)c;
				}
			} else {
				out[o++] = (uint8_t)c;
			}
		} else {
			out[o++] = (uint8_t)c;
		}
	}
	out[o] = '\0';
	if (out_len) *out_len = o;
	return out;
}

/* Convert arbitrary charset bytes to valid UTF-8 string */
static void append_charset_utf8(char *dst, size_t dst_sz, const char *charset, const uint8_t *bytes, size_t len) {
	if (!dst || dst_sz == 0 || !bytes || len == 0) return;
	size_t cur_len = strlen(dst);
	if (cur_len >= dst_sz - 1) return;

	gchar *utf8 = NULL;
	if (charset && *charset && g_ascii_strcasecmp(charset, "UTF-8") != 0 && g_ascii_strcasecmp(charset, "US-ASCII") != 0) {
		gsize bw = 0;
		utf8 = g_convert((const gchar *)bytes, (gssize)len, "UTF-8", charset, NULL, &bw, NULL);
	}
	if (!utf8) {
		utf8 = g_utf8_make_valid((const gchar *)bytes, (gssize)len);
	}
	if (utf8) {
		snprintf(dst + cur_len, dst_sz - cur_len, "%s", utf8);
		g_free(utf8);
	}
}

/* Decode RFC 2047 encoded-words: =?charset?B|Q?encoded_text?= */
static void decode_rfc2047(const char *in, char *out, size_t out_sz) {
	if (!out || out_sz == 0) return;
	out[0] = '\0';
	if (!in) return;

	const char *p = in;
	while (*p && strlen(out) < out_sz - 1) {
		const char *ew_start = strstr(p, "=?");
		if (!ew_start) {
			append_charset_utf8(out, out_sz, "UTF-8", (const uint8_t *)p, strlen(p));
			break;
		}
		if (ew_start > p) {
			/* Check if only whitespace between adjacent encoded words */
			bool all_ws = true;
			for (const char *w = p; w < ew_start; w++) {
				if (!isspace((unsigned char)*w)) { all_ws = false; break; }
			}
			if (!all_ws || p == in) {
				append_charset_utf8(out, out_sz, "UTF-8", (const uint8_t *)p, (size_t)(ew_start - p));
			}
		}

		const char *q1 = strchr(ew_start + 2, '?');
		const char *q2 = (q1 && q1[1] && q1[2] == '?') ? (q1 + 2) : NULL;
		const char *ew_end = q2 ? strstr(q2 + 1, "?=") : NULL;
		if (!q1 || !q2 || !ew_end) {
			append_charset_utf8(out, out_sz, "UTF-8", (const uint8_t *)ew_start, 2);
			p = ew_start + 2;
			continue;
		}

		char charset[64];
		size_t cs_len = (size_t)(q1 - (ew_start + 2));
		if (cs_len >= sizeof(charset)) cs_len = sizeof(charset) - 1;
		memcpy(charset, ew_start + 2, cs_len);
		charset[cs_len] = '\0';

		char enc = (char)toupper((unsigned char)q1[1]);
		const char *payload = q2 + 1;
		size_t pay_len = (size_t)(ew_end - payload);

		size_t dec_len = 0;
		uint8_t *dec = NULL;
		if (enc == 'B') {
			dec = decode_base64(payload, pay_len, &dec_len);
		} else if (enc == 'Q') {
			dec = decode_qp(payload, pay_len, true, &dec_len);
		}

		if (dec) {
			append_charset_utf8(out, out_sz, charset, dec, dec_len);
			free(dec);
			dec = NULL;
		} else {
			append_charset_utf8(out, out_sz, "UTF-8", (const uint8_t *)payload, pay_len);
		}
		p = ew_end + 2;
	}
}

/* Parse RFC 5322 Date header into unix timestamp */
static int64_t parse_rfc5322_date(const char *date_str) {
	if (!date_str || !*date_str) return 0;
	const char *p = date_str;
	const char *comma = strchr(p, ',');
	if (comma) p = comma + 1;
	while (*p && isspace((unsigned char)*p)) p++;

	int day = 1, year = 1970, hh = 0, mm = 0, ss = 0;
	char mon_str[16] = {0};
	char tz_str[16] = {0};

	int n = sscanf(p, "%d %15s %d %d:%d:%d %15s", &day, mon_str, &year, &hh, &mm, &ss, tz_str);
	if (n < 5) {
		n = sscanf(p, "%d %15s %d %d:%d %15s", &day, mon_str, &year, &hh, &mm, tz_str);
		if (n < 5) return 0;
	}
	if (year < 100) year += 2000;

	static const char *months[12] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	int mon = 0;
	for (int i = 0; i < 12; i++) {
		if (g_ascii_strncasecmp(mon_str, months[i], 3) == 0) {
			mon = i;
			break;
		}
	}

	struct tm tm_val;
	memset(&tm_val, 0, sizeof(tm_val));
	tm_val.tm_mday = day;
	tm_val.tm_mon = mon;
	tm_val.tm_year = year - 1900;
	tm_val.tm_hour = hh;
	tm_val.tm_min = mm;
	tm_val.tm_sec = ss;

	time_t t = timegm(&tm_val);
	if (t == (time_t)-1) t = mktime(&tm_val);

	if ((tz_str[0] == '+' || tz_str[0] == '-') && strlen(tz_str) >= 5) {
		int tz_num = atoi(tz_str + 1);
		int tz_h = tz_num / 100;
		int tz_m = tz_num % 100;
		int offset_sec = (tz_h * 3600) + (tz_m * 60);
		if (tz_str[0] == '+') t -= offset_sec;
		else t += offset_sec;
	}
	return (int64_t)t;
}

/* Parse unfolded RFC 5322 headers from header block [hdr_start, hdr_len) */
typedef struct {
	char from[256];
	char to[256];
	char cc[256];
	char subject[256];
	char date[128];
	char content_type[256];
	char transfer_encoding[64];
	char content_disposition[256];
} ParsedHeaders;

static void store_header_pair(ParsedHeaders *h, const char *name, const char *val) {
	while (*val && isspace((unsigned char)*val)) val++;
	if (g_ascii_strcasecmp(name, "From") == 0) {
		decode_rfc2047(val, h->from, sizeof(h->from));
	} else if (g_ascii_strcasecmp(name, "To") == 0) {
		decode_rfc2047(val, h->to, sizeof(h->to));
	} else if (g_ascii_strcasecmp(name, "Cc") == 0) {
		decode_rfc2047(val, h->cc, sizeof(h->cc));
	} else if (g_ascii_strcasecmp(name, "Subject") == 0) {
		decode_rfc2047(val, h->subject, sizeof(h->subject));
	} else if (g_ascii_strcasecmp(name, "Date") == 0) {
		snprintf(h->date, sizeof(h->date), "%s", val);
	} else if (g_ascii_strcasecmp(name, "Content-Type") == 0) {
		snprintf(h->content_type, sizeof(h->content_type), "%s", val);
	} else if (g_ascii_strcasecmp(name, "Content-Transfer-Encoding") == 0) {
		snprintf(h->transfer_encoding, sizeof(h->transfer_encoding), "%s", val);
	} else if (g_ascii_strcasecmp(name, "Content-Disposition") == 0) {
		snprintf(h->content_disposition, sizeof(h->content_disposition), "%s", val);
	}
}

static size_t parse_headers_block(const char *raw, size_t raw_len, ParsedHeaders *out_hdr) {
	memset(out_hdr, 0, sizeof(*out_hdr));
	char cur_name[64] = {0};
	char cur_val[1024] = {0};

	size_t pos = 0;
	while (pos < raw_len) {
		size_t line_end = pos;
		while (line_end < raw_len && raw[line_end] != '\n') line_end++;
		size_t content_end = line_end;
		if (content_end > pos && raw[content_end - 1] == '\r') content_end--;

		size_t line_len = content_end - pos;
		if (line_len == 0) {
			/* Blank line marks end of headers */
			pos = (line_end < raw_len) ? (line_end + 1) : raw_len;
			break;
		}

		const char *line = raw + pos;
		if ((line[0] == ' ' || line[0] == '\t') && cur_name[0] != '\0') {
			/* Folded continuation line */
			const char *trimmed = line;
			size_t tlen = line_len;
			while (tlen > 0 && (*trimmed == ' ' || *trimmed == '\t')) {
				trimmed++;
				tlen--;
			}
			size_t vlen = strlen(cur_val);
			if (vlen + 1 + tlen < sizeof(cur_val) - 1) {
				cur_val[vlen] = ' ';
				memcpy(cur_val + vlen + 1, trimmed, tlen);
				cur_val[vlen + 1 + tlen] = '\0';
			}
		} else {
			if (cur_name[0] != '\0') {
				store_header_pair(out_hdr, cur_name, cur_val);
				cur_name[0] = '\0';
				cur_val[0] = '\0';
			}
			const char *colon = memchr(line, ':', line_len);
			if (colon) {
				size_t nlen = (size_t)(colon - line);
				if (nlen >= sizeof(cur_name)) nlen = sizeof(cur_name) - 1;
				memcpy(cur_name, line, nlen);
				cur_name[nlen] = '\0';

				const char *vstart = colon + 1;
				size_t vlen = line_len - (size_t)(vstart - line);
				while (vlen > 0 && (*vstart == ' ' || *vstart == '\t')) {
					vstart++;
					vlen--;
				}
				if (vlen >= sizeof(cur_val)) vlen = sizeof(cur_val) - 1;
				memcpy(cur_val, vstart, vlen);
				cur_val[vlen] = '\0';
			}
		}

		pos = (line_end < raw_len) ? (line_end + 1) : raw_len;
	}

	if (cur_name[0] != '\0') {
		store_header_pair(out_hdr, cur_name, cur_val);
	}
	return pos;
}

/* Extract MIME parameter e.g. boundary="...", charset="...", filename="..." */
static bool extract_mime_param(const char *hdr_val, const char *param_name, char *out, size_t out_sz) {
	if (!hdr_val || !param_name || !out || out_sz == 0) return false;
	out[0] = '\0';
	size_t plen = strlen(param_name);
	const char *p = hdr_val;
	while (*p) {
		if (g_ascii_strncasecmp(p, param_name, plen) == 0 && p[plen] == '=') {
			const char *v = p + plen + 1;
			if (*v == '"') {
				v++;
				const char *endq = strchr(v, '"');
				size_t len = endq ? (size_t)(endq - v) : strlen(v);
				if (len >= out_sz) len = out_sz - 1;
				memcpy(out, v, len);
				out[len] = '\0';
				return true;
			} else {
				const char *ends = v;
				while (*ends && *ends != ';' && !isspace((unsigned char)*ends)) ends++;
				size_t len = (size_t)(ends - v);
				if (len >= out_sz) len = out_sz - 1;
				memcpy(out, v, len);
				out[len] = '\0';
				return true;
			}
		}
		p++;
	}
	return false;
}

static void clear_active_attachments(MailApp *app) {
	for (int i = 0; i < app->active_attach_count; i++) {
		free(app->active_attachments[i].data);
		app->active_attachments[i].data = NULL;
		app->active_attachments[i].size = 0;
	}
	app->active_attach_count = 0;
}

/* Decode body payload according to Content-Transfer-Encoding */
static uint8_t *decode_part_body(const char *raw_body, size_t body_len, const char *encoding, size_t *out_len) {
	if (out_len) *out_len = 0;
	if (!raw_body) return NULL;
	if (encoding && g_ascii_strncasecmp(encoding, "base64", 6) == 0) {
		return decode_base64(raw_body, body_len, out_len);
	}
	if (encoding && g_ascii_strncasecmp(encoding, "quoted-printable", 16) == 0) {
		return decode_qp(raw_body, body_len, false, out_len);
	}
	uint8_t *copy = malloc(body_len + 1);
	if (!copy) return NULL;
	memcpy(copy, raw_body, body_len);
	copy[body_len] = '\0';
	if (out_len) *out_len = body_len;
	return copy;
}

/* Strip basic HTML tags if only text/html part is available */
static void strip_html_tags(const uint8_t *in, size_t len, char *out, size_t out_sz) {
	size_t o = 0;
	bool in_tag = false;
	for (size_t i = 0; i < len && o < out_sz - 1; i++) {
		char c = (char)in[i];
		if (c == '<') in_tag = true;
		else if (c == '>') in_tag = false;
		else if (!in_tag) out[o++] = c;
	}
	out[o] = '\0';
}

static void parse_mime_entity(MailApp *app, const char *raw, size_t raw_len,
                              char *body_out, size_t body_out_sz, bool *got_plain, int depth) {
	if (depth > 5 || !raw || raw_len == 0) return;

	ParsedHeaders hdr;
	size_t body_offset = parse_headers_block(raw, raw_len, &hdr);
	const char *body_ptr = raw + body_offset;
	size_t body_len = (body_offset <= raw_len) ? (raw_len - body_offset) : 0;

	const char *ctype = hdr.content_type[0] ? hdr.content_type : "text/plain; charset=UTF-8";

	if (g_ascii_strncasecmp(ctype, "multipart/", 10) == 0) {
		char boundary[128];
		if (!extract_mime_param(ctype, "boundary", boundary, sizeof(boundary)) || !boundary[0]) {
			return;
		}
		char delim[140];
		snprintf(delim, sizeof(delim), "--%s", boundary);
		size_t dlen = strlen(delim);

		const char *cur = body_ptr;
		const char *end = body_ptr + body_len;
		const char *first = NULL;

		while (cur + dlen <= end) {
			if (memcmp(cur, delim, dlen) == 0) {
				first = cur;
				break;
			}
			cur++;
		}
		if (!first) return;

		const char *part_p = first + dlen;
		while (part_p < end) {
			if (part_p + 2 <= end && part_p[0] == '-' && part_p[1] == '-') break;
			while (part_p < end && (*part_p == ' ' || *part_p == '\t' || *part_p == '\r')) part_p++;
			if (part_p < end && *part_p == '\n') part_p++;

			/* Find next boundary */
			const char *next_b = NULL;
			for (const char *scan = part_p; scan + dlen <= end; scan++) {
				if (memcmp(scan, delim, dlen) == 0 && (scan == part_p || scan[-1] == '\n')) {
					next_b = scan;
					break;
				}
			}
			size_t part_len = next_b ? (size_t)(next_b - part_p) : (size_t)(end - part_p);
			if (part_len > 0 && part_p[part_len - 1] == '\n') part_len--;
			if (part_len > 0 && part_p[part_len - 1] == '\r') part_len--;

			parse_mime_entity(app, part_p, part_len, body_out, body_out_sz, got_plain, depth + 1);
			if (!next_b) break;
			part_p = next_b + dlen;
		}
		return;
	}

	/* Check if attachment */
	char fname[128] = {0};
	bool is_attach = false;
	if (g_ascii_strncasecmp(hdr.content_disposition, "attachment", 10) == 0) {
		is_attach = true;
	}
	if (extract_mime_param(hdr.content_disposition, "filename", fname, sizeof(fname)) && fname[0]) {
		is_attach = true;
	} else if (extract_mime_param(ctype, "name", fname, sizeof(fname)) && fname[0]) {
		is_attach = true;
	}

	size_t dec_len = 0;
	uint8_t *dec = decode_part_body(body_ptr, body_len, hdr.transfer_encoding, &dec_len);
	if (!dec) return;

	if (is_attach) {
		if (app->active_attach_count < MAX_ATTACHMENTS) {
			Attachment *att = &app->active_attachments[app->active_attach_count++];
			char decoded_fn[128];
			decode_rfc2047(fname[0] ? fname : "attachment.bin", decoded_fn, sizeof(decoded_fn));
			/* Sanitize basename */
			const char *base = strrchr(decoded_fn, '/');
			base = base ? (base + 1) : decoded_fn;
			snprintf(att->filename, sizeof(att->filename), "%s", *base ? base : "attachment.bin");
			snprintf(att->content_type, sizeof(att->content_type), "%.63s", ctype);
			att->data = dec;
			att->size = dec_len;
			return; /* ownership transferred */
		}
		free(dec);
		dec = NULL;
		return;
	}

	char charset[64] = "UTF-8";
	extract_mime_param(ctype, "charset", charset, sizeof(charset));

	if (g_ascii_strncasecmp(ctype, "text/plain", 10) == 0 || depth == 0) {
		if (!*got_plain) body_out[0] = '\0';
		append_charset_utf8(body_out, body_out_sz, charset, dec, dec_len);
		*got_plain = true;
	} else if (g_ascii_strncasecmp(ctype, "text/html", 9) == 0 && !*got_plain) {
		char *stripped = malloc(dec_len + 1);
		if (stripped) {
			strip_html_tags(dec, dec_len, stripped, dec_len + 1);
			body_out[0] = '\0';
			append_charset_utf8(body_out, body_out_sz, charset, (const uint8_t *)stripped, strlen(stripped));
			free(stripped);
			stripped = NULL;
		}
	}

	free(dec);
	dec = NULL;
}

/* Read message raw bytes from either Maildir file or mbox slice */
static char *load_raw_message(const MailSummary *msg, size_t *out_len) {
	if (out_len) *out_len = 0;
	if (!msg) return NULL;

	int fd = open(msg->filepath, O_RDONLY | O_CLOEXEC);
	if (fd < 0) return NULL;

	size_t target_len = 0;
	if (msg->is_mbox) {
		if (lseek(fd, msg->mbox_offset, SEEK_SET) == (off_t)-1) {
			close(fd);
			return NULL;
		}
		target_len = msg->mbox_length;
	} else {
		struct stat st;
		if (fstat(fd, &st) != 0 || st.st_size < 0) {
			close(fd);
			return NULL;
		}
		target_len = (size_t)st.st_size;
	}

	if (target_len > MAX_MSG_FILE_BYTES) target_len = MAX_MSG_FILE_BYTES;
	char *buf = malloc(target_len + 1);
	if (!buf) {
		close(fd);
		return NULL;
	}

	size_t total = 0;
	while (total < target_len) {
		ssize_t r = read(fd, buf + total, target_len - total);
		if (r < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (r == 0) break;
		total += (size_t)r;
	}
	close(fd);
	buf[total] = '\0';
	if (out_len) *out_len = total;
	return buf;
}

/* Scan folders */
static void add_folder_entry(MailApp *app, const char *label, const char *path, FolderType type, bool is_trash) {
	if (app->folder_count >= MAX_FOLDERS) return;
	MailFolder *f = &app->folders[app->folder_count];
	snprintf(f->label, sizeof(f->label), "%s", label);
	snprintf(f->path, sizeof(f->path), "%s", path);
	f->type = type;
	f->is_trash = is_trash;

	GtkTreeIter iter;
	gtk_list_store_append(app->folder_store, &iter);
	gtk_list_store_set(app->folder_store, &iter,
		FCOL_LABEL, f->label,
		FCOL_INDEX, app->folder_count,
		-1);
	app->folder_count++;
}

static bool is_maildir_path(const char *path) {
	char cur[1024];
	snprintf(cur, sizeof(cur), "%s/cur", path);
	struct stat st;
	return (stat(cur, &st) == 0 && S_ISDIR(st.st_mode));
}

static void scan_maildir_subfolders(MailApp *app, const char *root_path, const char *prefix) {
	DIR *d = opendir(root_path);
	if (!d) return;
	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
		if (strcmp(ent->d_name, "cur") == 0 || strcmp(ent->d_name, "new") == 0 || strcmp(ent->d_name, "tmp") == 0) continue;
		/* Standard folders already added explicitly */
		if (strcmp(ent->d_name, ".Sent") == 0 || strcmp(ent->d_name, ".Drafts") == 0 ||
		    strcmp(ent->d_name, ".Trash") == 0) continue;

		char sub_path[768];
		snprintf(sub_path, sizeof(sub_path), "%s/%s", root_path, ent->d_name);
		if (is_maildir_path(sub_path)) {
			const char *clean = (ent->d_name[0] == '.') ? (ent->d_name + 1) : ent->d_name;
			char label[128];
			snprintf(label, sizeof(label), "%.32s%.90s", prefix, clean);
			add_folder_entry(app, label, sub_path, FOLDER_TYPE_MAILDIR, false);
		}
	}
	closedir(d);
}

static void populate_folders(MailApp *app) {
	gtk_list_store_clear(app->folder_store);
	app->folder_count = 0;

	char p[600];
	add_folder_entry(app, "Inbox", app->local_maildir_root, FOLDER_TYPE_MAILDIR, false);

	snprintf(p, sizeof(p), "%s/.Sent", app->local_maildir_root);
	add_folder_entry(app, "Sent", p, FOLDER_TYPE_MAILDIR, false);

	snprintf(p, sizeof(p), "%s/.Drafts", app->local_maildir_root);
	add_folder_entry(app, "Drafts", p, FOLDER_TYPE_MAILDIR, false);

	snprintf(p, sizeof(p), "%s/.Trash", app->local_maildir_root);
	add_folder_entry(app, "Trash", p, FOLDER_TYPE_MAILDIR, true);

	scan_maildir_subfolders(app, app->local_maildir_root, "  • ");

	/* Check ~/Maildir */
	const char *home = getenv("HOME");
	if (home && *home) {
		char home_md[512];
		snprintf(home_md, sizeof(home_md), "%s/Maildir", home);
		if (is_maildir_path(home_md)) {
			add_folder_entry(app, "~/Maildir (Inbox)", home_md, FOLDER_TYPE_MAILDIR, false);
			scan_maildir_subfolders(app, home_md, "~/Maildir/");
		}

		char home_mbox[512];
		snprintf(home_mbox, sizeof(home_mbox), "%s/mbox", home);
		struct stat st;
		if (stat(home_mbox, &st) == 0 && S_ISREG(st.st_mode)) {
			add_folder_entry(app, "~/mbox", home_mbox, FOLDER_TYPE_MBOX, false);
		}
	}

	/* Check /var/mail/$USER */
	const char *user = getenv("USER");
	if (user && *user) {
		char spool[512];
		snprintf(spool, sizeof(spool), "/var/mail/%s", user);
		struct stat st;
		if (stat(spool, &st) == 0 && S_ISREG(st.st_mode)) {
			add_folder_entry(app, "/var/mail spool", spool, FOLDER_TYPE_MBOX, false);
		}
	}
}

static void format_short_date(int64_t ts, const char *fallback, char *out, size_t out_sz) {
	if (ts <= 0) {
		snprintf(out, out_sz, "%s", (fallback && *fallback) ? fallback : "Unknown");
		return;
	}
	time_t t = (time_t)ts;
	struct tm tm_v;
	if (localtime_r(&t, &tm_v)) {
		strftime(out, out_sz, "%Y-%m-%d %H:%M", &tm_v);
	} else {
		snprintf(out, out_sz, "%s", fallback ? fallback : "");
	}
}

static void parse_summary_from_buffer(const char *buf, size_t len, MailSummary *ms) {
	ParsedHeaders hdr;
	parse_headers_block(buf, len, &hdr);
	snprintf(ms->from, sizeof(ms->from), "%s", hdr.from[0] ? hdr.from : "(Unknown Sender)");
	snprintf(ms->to, sizeof(ms->to), "%s", hdr.to);
	snprintf(ms->cc, sizeof(ms->cc), "%s", hdr.cc);
	snprintf(ms->subject, sizeof(ms->subject), "%s", hdr.subject[0] ? hdr.subject : "(No Subject)");
	ms->timestamp = parse_rfc5322_date(hdr.date);
	format_short_date(ms->timestamp, hdr.date, ms->date_str, sizeof(ms->date_str));
}

static void scan_maildir_subdir(MailApp *app, const char *subdir_path, bool is_new_dir) {
	DIR *d = opendir(subdir_path);
	if (!d) return;
	struct dirent *ent;
	while ((ent = readdir(d)) != NULL && app->msg_count < MAX_MESSAGES) {
		if (ent->d_name[0] == '.') continue;
		char fpath[600];
		snprintf(fpath, sizeof(fpath), "%s/%s", subdir_path, ent->d_name);
		struct stat st;
		if (stat(fpath, &st) != 0 || !S_ISREG(st.st_mode)) continue;

		/* Read first 8KB for headers */
		char hdr_buf[8192];
		int fd = open(fpath, O_RDONLY | O_CLOEXEC);
		if (fd < 0) continue;
		ssize_t r = read(fd, hdr_buf, sizeof(hdr_buf) - 1);
		close(fd);
		if (r <= 0) continue;
		hdr_buf[r] = '\0';

		MailSummary *ms = &app->messages[app->msg_count];
		memset(ms, 0, sizeof(*ms));
		snprintf(ms->filepath, sizeof(ms->filepath), "%s", fpath);
		ms->is_mbox = false;

		/* Determine unread status from Maildir flags (:2,S = Seen) */
		bool seen = false;
		const char *info = strstr(ent->d_name, ":2,");
		if (info && strchr(info + 3, 'S') != NULL) seen = true;
		ms->unread = is_new_dir ? true : !seen;

		parse_summary_from_buffer(hdr_buf, (size_t)r, ms);
		if (ms->timestamp <= 0) {
			ms->timestamp = (int64_t)st.st_mtime;
			format_short_date(ms->timestamp, "", ms->date_str, sizeof(ms->date_str));
		}
		app->msg_count++;
	}
	closedir(d);
}

static void scan_mbox_file(MailApp *app, const char *mbox_path) {
	FILE *fp = fopen(mbox_path, "rb");
	if (!fp) return;

	char line[2048];
	off_t cur_msg_start = -1;
	char hdr_accum[8192];
	size_t hdr_len = 0;
	bool in_headers = false;

	while (fgets(line, sizeof(line), fp) != NULL && app->msg_count < MAX_MESSAGES) {
		if (strncmp(line, "From ", 5) == 0) {
			off_t line_pos = ftello(fp) - (off_t)strlen(line);
			if (cur_msg_start >= 0 && app->msg_count < MAX_MESSAGES) {
				MailSummary *prev = &app->messages[app->msg_count++];
				prev->mbox_length = (size_t)(line_pos - cur_msg_start);
				hdr_accum[hdr_len] = '\0';
				parse_summary_from_buffer(hdr_accum, hdr_len, prev);
			}
			cur_msg_start = ftello(fp);
			hdr_len = 0;
			in_headers = true;
			MailSummary *ms = &app->messages[app->msg_count];
			memset(ms, 0, sizeof(*ms));
			snprintf(ms->filepath, sizeof(ms->filepath), "%s", mbox_path);
			ms->is_mbox = true;
			ms->mbox_offset = cur_msg_start;
			ms->unread = false;
		} else if (in_headers) {
			if (strcmp(line, "\n") == 0 || strcmp(line, "\r\n") == 0) {
				in_headers = false;
			} else {
				size_t l = strlen(line);
				if (hdr_len + l < sizeof(hdr_accum) - 1) {
					memcpy(hdr_accum + hdr_len, line, l);
					hdr_len += l;
				}
			}
		}
	}

	if (cur_msg_start >= 0 && app->msg_count < MAX_MESSAGES) {
		off_t end_pos = ftello(fp);
		MailSummary *prev = &app->messages[app->msg_count++];
		prev->mbox_length = (size_t)(end_pos - cur_msg_start);
		hdr_accum[hdr_len] = '\0';
		parse_summary_from_buffer(hdr_accum, hdr_len, prev);
	}

	fclose(fp);
}

static void load_folder_messages(MailApp *app, int folder_idx) {
	if (folder_idx < 0 || folder_idx >= app->folder_count) return;
	app->active_folder_idx = folder_idx;
	app->msg_count = 0;
	app->selected_msg_idx = -1;
	gtk_list_store_clear(app->msg_store);

	MailFolder *f = &app->folders[folder_idx];
	if (f->type == FOLDER_TYPE_MAILDIR) {
		char sub[600];
		snprintf(sub, sizeof(sub), "%s/new", f->path);
		scan_maildir_subdir(app, sub, true);
		snprintf(sub, sizeof(sub), "%s/cur", f->path);
		scan_maildir_subdir(app, sub, false);
	} else {
		scan_mbox_file(app, f->path);
	}

	for (int i = 0; i < app->msg_count; i++) {
		MailSummary *ms = &app->messages[i];
		GtkTreeIter iter;
		gtk_list_store_append(app->msg_store, &iter);
		gtk_list_store_set(app->msg_store, &iter,
			MCOL_FROM, ms->from,
			MCOL_SUBJECT, ms->subject,
			MCOL_DATE, ms->date_str,
			MCOL_TIMESTAMP, (gint64)ms->timestamp,
			MCOL_WEIGHT, ms->unread ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
			MCOL_INDEX, i,
			-1);
	}
}

/* Mark Maildir message as Read (move from new/ to cur/ with :2,S) */
static void mark_maildir_message_read(MailSummary *ms) {
	if (!ms || ms->is_mbox || !ms->unread) return;
	const char *slash = strrchr(ms->filepath, '/');
	if (!slash) return;
	const char *fname = slash + 1;

	/* Find parent Maildir root */
	size_t dir_len = (size_t)(slash - ms->filepath);
	if (dir_len < 4) return;
	char parent_box[512];
	if (strncmp(slash - 4, "/new", 4) == 0 || strncmp(slash - 4, "/cur", 4) == 0) {
		size_t root_len = dir_len - 4;
		if (root_len >= sizeof(parent_box)) root_len = sizeof(parent_box) - 1;
		memcpy(parent_box, ms->filepath, root_len);
		parent_box[root_len] = '\0';
	} else {
		return;
	}

	char new_path[1024];
	const char *colon = strstr(fname, ":2,");
	if (colon) {
		if (strchr(colon + 3, 'S') == NULL) {
			snprintf(new_path, sizeof(new_path), "%s/cur/%sS", parent_box, fname);
		} else {
			snprintf(new_path, sizeof(new_path), "%s/cur/%s", parent_box, fname);
		}
	} else {
		snprintf(new_path, sizeof(new_path), "%s/cur/%s:2,S", parent_box, fname);
	}

	if (strcmp(ms->filepath, new_path) != 0) {
		if (rename(ms->filepath, new_path) == 0) {
			snprintf(ms->filepath, sizeof(ms->filepath), "%s", new_path);
		}
	}
	ms->unread = false;
}

static void show_info_banner(MailApp *app, const char *text, GtkMessageType mtype) {
	gtk_info_bar_set_message_type(GTK_INFO_BAR(app->info_bar), mtype);
	gtk_label_set_text(GTK_LABEL(app->lbl_info), text);
	gtk_widget_show(app->info_bar);
}

static void on_save_attachment_clicked(GtkWidget *btn, gpointer user_data) {
	MailApp *app = user_data;
	int att_idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "att-idx"));
	if (att_idx < 0 || att_idx >= app->active_attach_count) return;
	Attachment *att = &app->active_attachments[att_idx];

	GtkWidget *dlg = gtk_file_chooser_dialog_new(
		"Save Attachment",
		GTK_WINDOW(app->window),
		GTK_FILE_CHOOSER_ACTION_SAVE,
		"Cancel", GTK_RESPONSE_CANCEL,
		"Save", GTK_RESPONSE_ACCEPT,
		NULL);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
	gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), att->filename);

	if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
		char *dest = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
		if (dest) {
			int fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
			if (fd >= 0) {
				ssize_t w = write(fd, att->data, att->size);
				close(fd);
				if (w == (ssize_t)att->size) {
					char msg[600];
					snprintf(msg, sizeof(msg), "Saved attachment to %s", dest);
					show_info_banner(app, msg, GTK_MESSAGE_INFO);
				} else {
					show_info_banner(app, "Failed to write full attachment.", GTK_MESSAGE_ERROR);
				}
			} else {
				show_info_banner(app, "Could not open target file for writing.", GTK_MESSAGE_ERROR);
			}
			g_free(dest);
		}
	}
	gtk_widget_destroy(dlg);
}

static void display_message_in_reader(MailApp *app, int msg_idx) {
	clear_active_attachments(app);
	GList *children = gtk_container_get_children(GTK_CONTAINER(app->attach_box));
	for (GList *l = children; l != NULL; l = l->next) {
		gtk_widget_destroy(GTK_WIDGET(l->data));
	}
	g_list_free(children);
	gtk_widget_hide(app->attach_box);

	if (msg_idx < 0 || msg_idx >= app->msg_count) {
		gtk_label_set_text(GTK_LABEL(app->lbl_hdr_subject), "Select a message to read");
		gtk_label_set_text(GTK_LABEL(app->lbl_hdr_from), "");
		gtk_label_set_text(GTK_LABEL(app->lbl_hdr_to), "");
		gtk_label_set_text(GTK_LABEL(app->lbl_hdr_date), "");
		gtk_text_buffer_set_text(app->body_buffer, "", -1);
		return;
	}

	MailSummary *ms = &app->messages[msg_idx];
	size_t raw_len = 0;
	char *raw = load_raw_message(ms, &raw_len);
	if (!raw) {
		gtk_text_buffer_set_text(app->body_buffer, "(Unable to read message file.)", -1);
		return;
	}

	char *body_text = malloc(262144);
	if (!body_text) {
		free(raw);
		raw = NULL;
		return;
	}
	body_text[0] = '\0';
	bool got_plain = false;
	parse_mime_entity(app, raw, raw_len, body_text, 262144, &got_plain, 0);

	gtk_label_set_text(GTK_LABEL(app->lbl_hdr_subject), ms->subject);

	char from_line[320];
	snprintf(from_line, sizeof(from_line), "From: %s", ms->from);
	gtk_label_set_text(GTK_LABEL(app->lbl_hdr_from), from_line);

	char to_line[600];
	if (ms->cc[0]) {
		snprintf(to_line, sizeof(to_line), "To: %s   |   Cc: %s", ms->to, ms->cc);
	} else {
		snprintf(to_line, sizeof(to_line), "To: %s", ms->to[0] ? ms->to : "(undisclosed)");
	}
	gtk_label_set_text(GTK_LABEL(app->lbl_hdr_to), to_line);
	gtk_label_set_text(GTK_LABEL(app->lbl_hdr_date), ms->date_str);

	gtk_text_buffer_set_text(app->body_buffer, body_text, -1);

	/* Populate attachment bar if any */
	if (app->active_attach_count > 0) {
		GtkWidget *lbl_att = gtk_label_new("Attachments:");
		gtk_style_context_add_class(gtk_widget_get_style_context(lbl_att), "dim");
		gtk_box_pack_start(GTK_BOX(app->attach_box), lbl_att, FALSE, FALSE, 4);

		for (int i = 0; i < app->active_attach_count; i++) {
			Attachment *att = &app->active_attachments[i];
			char blabel[200];
			snprintf(blabel, sizeof(blabel), "Save %s (%zu B)", att->filename, att->size);
			GtkWidget *btn = gtk_button_new_with_label(blabel);
			g_object_set_data(G_OBJECT(btn), "att-idx", GINT_TO_POINTER(i));
			g_signal_connect(btn, "clicked", G_CALLBACK(on_save_attachment_clicked), app);
			gtk_box_pack_start(GTK_BOX(app->attach_box), btn, FALSE, FALSE, 4);
		}
		gtk_widget_show_all(app->attach_box);
	}

	free(body_text);
	body_text = NULL;
	free(raw);
	raw = NULL;
}

static void on_folder_selection_changed(GtkTreeSelection *sel, gpointer user_data) {
	MailApp *app = user_data;
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;
	gint idx = 0;
	gtk_tree_model_get(model, &iter, FCOL_INDEX, &idx, -1);
	load_folder_messages(app, idx);
	display_message_in_reader(app, -1);
}

static void on_msg_selection_changed(GtkTreeSelection *sel, gpointer user_data) {
	MailApp *app = user_data;
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	if (!gtk_tree_selection_get_selected(sel, &model, &iter)) {
		app->selected_msg_idx = -1;
		display_message_in_reader(app, -1);
		return;
	}

	gint msg_idx = -1;
	gtk_tree_model_get(model, &iter, MCOL_INDEX, &msg_idx, -1);
	app->selected_msg_idx = msg_idx;
	if (msg_idx >= 0 && msg_idx < app->msg_count) {
		MailSummary *ms = &app->messages[msg_idx];
		if (ms->unread) {
			mark_maildir_message_read(ms);
			/* Update weight in underlying GtkListStore */
			GtkTreeIter filter_iter, store_iter;
			gtk_tree_model_sort_convert_iter_to_child_iter(
				GTK_TREE_MODEL_SORT(app->msg_sort), &filter_iter, &iter);
			gtk_tree_model_filter_convert_iter_to_child_iter(
				GTK_TREE_MODEL_FILTER(app->msg_filter), &store_iter, &filter_iter);
			gtk_list_store_set(app->msg_store, &store_iter, MCOL_WEIGHT, PANGO_WEIGHT_NORMAL, -1);
		}
		display_message_in_reader(app, msg_idx);
	}
}

/* Delete / Move to Trash */
static void on_btn_delete_clicked(GtkWidget *btn, gpointer user_data) {
	(void)btn;
	MailApp *app = user_data;
	int idx = app->selected_msg_idx;
	if (idx < 0 || idx >= app->msg_count) {
		show_info_banner(app, "No message selected to delete.", GTK_MESSAGE_WARNING);
		return;
	}

	MailSummary *ms = &app->messages[idx];
	if (ms->is_mbox) {
		show_info_banner(app, "Read-only mbox spool: cannot delete single message in-place.", GTK_MESSAGE_WARNING);
		return;
	}

	MailFolder *cur_f = &app->folders[app->active_folder_idx];
	if (cur_f->is_trash) {
		if (unlink(ms->filepath) == 0) {
			show_info_banner(app, "Message permanently deleted from Trash.", GTK_MESSAGE_INFO);
		} else {
			show_info_banner(app, "Failed to unlink message file.", GTK_MESSAGE_ERROR);
		}
	} else {
		const char *slash = strrchr(ms->filepath, '/');
		const char *fname = slash ? (slash + 1) : "msg";
		char trash_dest[600];
		snprintf(trash_dest, sizeof(trash_dest), "%s/.Trash/cur/%s", app->local_maildir_root, fname);
		if (rename(ms->filepath, trash_dest) == 0) {
			show_info_banner(app, "Message moved to Trash.", GTK_MESSAGE_INFO);
		} else {
			show_info_banner(app, "Failed to move message to Trash.", GTK_MESSAGE_ERROR);
		}
	}

	load_folder_messages(app, app->active_folder_idx);
	display_message_in_reader(app, -1);
}

/* Search filter */
static gboolean msg_filter_visible_func(GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
	MailApp *app = data;
	if (app->search_text[0] == '\0') return TRUE;

	gchar *from = NULL;
	gchar *subj = NULL;
	gtk_tree_model_get(model, iter, MCOL_FROM, &from, MCOL_SUBJECT, &subj, -1);

	gchar *needle = g_ascii_strdown(app->search_text, -1);
	bool match = false;
	if (from) {
		gchar *h = g_ascii_strdown(from, -1);
		if (strstr(h, needle) != NULL) match = true;
		g_free(h);
	}
	if (!match && subj) {
		gchar *h = g_ascii_strdown(subj, -1);
		if (strstr(h, needle) != NULL) match = true;
		g_free(h);
	}
	g_free(needle);
	g_free(from);
	g_free(subj);
	return match ? TRUE : FALSE;
}

static void on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
	MailApp *app = user_data;
	const char *txt = gtk_entry_get_text(GTK_ENTRY(entry));
	snprintf(app->search_text, sizeof(app->search_text), "%s", txt ? txt : "");
	gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(app->msg_filter));
}

/* Compose Window & Mail Submission */
typedef struct {
	MailApp *app;
	GtkWidget *win;
	GtkWidget *ent_to;
	GtkWidget *ent_cc;
	GtkWidget *ent_subj;
	GtkTextBuffer *buf_body;
} ComposeDialog;

static bool find_mta_in_path(char *out_path, size_t out_sz) {
	const char *candidates[] = {"msmtp", "sendmail"};
	const char *path_env = getenv("PATH");
	if (!path_env || !*path_env) path_env = "/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin";

	for (size_t c = 0; c < 2; c++) {
		char copy[1024];
		snprintf(copy, sizeof(copy), "%s", path_env);
		char *saveptr = NULL;
		char *dir = strtok_r(copy, ":", &saveptr);
		while (dir) {
			char full[512];
			snprintf(full, sizeof(full), "%s/%s", dir, candidates[c]);
			if (access(full, X_OK) == 0) {
				snprintf(out_path, out_sz, "%s", full);
				return true;
			}
			dir = strtok_r(NULL, ":", &saveptr);
		}
	}
	return false;
}

static bool save_message_to_maildir_box(const char *box_path, const char *subfolder,
                                        const char *raw_msg, size_t len, const char *flags) {
	ensure_maildir_box(box_path);
	static unsigned int seq = 1;
	char fpath[640];
	snprintf(fpath, sizeof(fpath), "%s/%s/%" PRId64 ".M%uP%d.diamonds%s",
		box_path, subfolder, (int64_t)time(NULL), seq++, (int)getpid(), flags ? flags : "");
	int fd = open(fpath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0) return false;
	ssize_t w = write(fd, raw_msg, len);
	close(fd);
	return (w == (ssize_t)len);
}

static bool pipe_message_to_mta(const char *mta_path, const char *raw_msg, size_t len) {
	int pipefd[2];
	if (pipe(pipefd) != 0) return false;

	pid_t pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return false;
	}
	if (pid == 0) {
		close(pipefd[1]);
		dup2(pipefd[0], STDIN_FILENO);
		close(pipefd[0]);
		execl(mta_path, mta_path, "-t", "-i", (char *)NULL);
		_exit(127);
	}
	close(pipefd[0]);
	size_t written = 0;
	while (written < len) {
		ssize_t w = write(pipefd[1], raw_msg + written, len - written);
		if (w < 0) {
			if (errno == EINTR) continue;
			break;
		}
		written += (size_t)w;
	}
	close(pipefd[1]);

	int status = 0;
	if (waitpid(pid, &status, 0) < 0) return false;
	return (WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void on_compose_send_clicked(GtkWidget *btn, gpointer user_data) {
	(void)btn;
	ComposeDialog *cd = user_data;
	MailApp *app = cd->app;

	const char *to = gtk_entry_get_text(GTK_ENTRY(cd->ent_to));
	const char *cc = gtk_entry_get_text(GTK_ENTRY(cd->ent_cc));
	const char *subj = gtk_entry_get_text(GTK_ENTRY(cd->ent_subj));

	if (!to || !*to) {
		show_info_banner(app, "Recipient (To:) cannot be empty.", GTK_MESSAGE_WARNING);
		return;
	}

	GtkTextIter start, end;
	gtk_text_buffer_get_bounds(cd->buf_body, &start, &end);
	gchar *body = gtk_text_buffer_get_text(cd->buf_body, &start, &end, FALSE);

	time_t now = time(NULL);
	struct tm tm_now;
	char date_hdr[128];
	if (localtime_r(&now, &tm_now)) {
		strftime(date_hdr, sizeof(date_hdr), "%a, %d %b %Y %H:%M:%S %z", &tm_now);
	} else {
		snprintf(date_hdr, sizeof(date_hdr), "Mon, 09 Nov 2026 12:00:00 +0000");
	}

	const char *user = getenv("USER");
	if (!user || !*user) user = "user";

	size_t body_len = body ? strlen(body) : 0;
	size_t cap = body_len + 2048;
	char *raw_msg = malloc(cap);
	if (!raw_msg) {
		g_free(body);
		return;
	}

	int n = snprintf(raw_msg, cap,
		"From: %s@localhost\r\n"
		"To: %s\r\n"
		"%s%s%s"
		"Subject: %s\r\n"
		"Date: %s\r\n"
		"MIME-Version: 1.0\r\n"
		"Content-Type: text/plain; charset=UTF-8\r\n"
		"Content-Transfer-Encoding: 8bit\r\n"
		"\r\n"
		"%s\r\n",
		user,
		to,
		(cc && *cc) ? "Cc: " : "",
		(cc && *cc) ? cc : "",
		(cc && *cc) ? "\r\n" : "",
		(subj && *subj) ? subj : "(No Subject)",
		date_hdr,
		body ? body : "");

	size_t msg_len = (n > 0 && (size_t)n < cap) ? (size_t)n : strlen(raw_msg);

	char mta_path[512] = {0};
	bool mta_ok = false;
	if (find_mta_in_path(mta_path, sizeof(mta_path))) {
		mta_ok = pipe_message_to_mta(mta_path, raw_msg, msg_len);
	}

	char sent_box[600];
	snprintf(sent_box, sizeof(sent_box), "%s/.Sent", app->local_maildir_root);
	save_message_to_maildir_box(sent_box, "cur", raw_msg, msg_len, ":2,S");

	if (mta_ok) {
		char info[600];
		snprintf(info, sizeof(info), "Message dispatched via %s and archived in Sent.", mta_path);
		show_info_banner(app, info, GTK_MESSAGE_INFO);
	} else {
		char outbox[600];
		snprintf(outbox, sizeof(outbox), "%s/.Outbox", app->local_maildir_root);
		save_message_to_maildir_box(outbox, "new", raw_msg, msg_len, "");
		show_info_banner(app,
			"No working MTA (sendmail/msmtp) configured — saved to Sent & local Outbox Maildir.",
			GTK_MESSAGE_INFO);
	}

	free(raw_msg);
	raw_msg = NULL;
	g_free(body);

	gtk_widget_destroy(cd->win);
	load_folder_messages(app, app->active_folder_idx);
}

static void on_compose_save_draft_clicked(GtkWidget *btn, gpointer user_data) {
	(void)btn;
	ComposeDialog *cd = user_data;
	MailApp *app = cd->app;

	const char *to = gtk_entry_get_text(GTK_ENTRY(cd->ent_to));
	const char *cc = gtk_entry_get_text(GTK_ENTRY(cd->ent_cc));
	const char *subj = gtk_entry_get_text(GTK_ENTRY(cd->ent_subj));

	GtkTextIter start, end;
	gtk_text_buffer_get_bounds(cd->buf_body, &start, &end);
	gchar *body = gtk_text_buffer_get_text(cd->buf_body, &start, &end, FALSE);

	time_t now = time(NULL);
	struct tm tm_now;
	char date_hdr[128];
	if (localtime_r(&now, &tm_now)) {
		strftime(date_hdr, sizeof(date_hdr), "%a, %d %b %Y %H:%M:%S %z", &tm_now);
	} else {
		snprintf(date_hdr, sizeof(date_hdr), "Mon, 09 Nov 2026 12:00:00 +0000");
	}

	size_t cap = (body ? strlen(body) : 0) + 1024;
	char *raw_msg = malloc(cap);
	if (raw_msg) {
		int n = snprintf(raw_msg, cap,
			"From: user@localhost\r\n"
			"To: %s\r\n"
			"Cc: %s\r\n"
			"Subject: %s\r\n"
			"Date: %s\r\n"
			"Content-Type: text/plain; charset=UTF-8\r\n"
			"\r\n"
			"%s\r\n",
			to ? to : "", cc ? cc : "", subj ? subj : "(Draft)", date_hdr, body ? body : "");
		size_t msg_len = (n > 0 && (size_t)n < cap) ? (size_t)n : strlen(raw_msg);
		char drafts_box[600];
		snprintf(drafts_box, sizeof(drafts_box), "%s/.Drafts", app->local_maildir_root);
		save_message_to_maildir_box(drafts_box, "cur", raw_msg, msg_len, ":2,DS");
		free(raw_msg);
		raw_msg = NULL;
		show_info_banner(app, "Saved message to Drafts.", GTK_MESSAGE_INFO);
	}
	g_free(body);
	gtk_widget_destroy(cd->win);
	load_folder_messages(app, app->active_folder_idx);
}

static void open_compose_window(MailApp *app, const char *prefill_to, const char *prefill_subj, const char *prefill_body) {
	ComposeDialog *cd = g_malloc0(sizeof(ComposeDialog));
	cd->app = app;

	cd->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(cd->win), "Compose Message");
	gtk_window_set_default_size(GTK_WINDOW(cd->win), 620, 480);
	gtk_window_set_transient_for(GTK_WINDOW(cd->win), GTK_WINDOW(app->window));
	g_signal_connect_swapped(cd->win, "destroy", G_CALLBACK(g_free), cd);

	GtkWidget *hbar = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hbar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(hbar), "Compose Mail");

	GtkWidget *btn_send = gtk_button_new_with_label("Send");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_send), "action-btn");
	g_signal_connect(btn_send, "clicked", G_CALLBACK(on_compose_send_clicked), cd);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(hbar), btn_send);

	GtkWidget *btn_draft = gtk_button_new_with_label("Save Draft");
	g_signal_connect(btn_draft, "clicked", G_CALLBACK(on_compose_save_draft_clicked), cd);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(hbar), btn_draft);

	gtk_window_set_titlebar(GTK_WINDOW(cd->win), hbar);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
	gtk_container_add(GTK_CONTAINER(cd->win), vbox);

	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 8);

	GtkWidget *l_to = gtk_label_new("To:");
	gtk_label_set_xalign(GTK_LABEL(l_to), 1.0f);
	cd->ent_to = gtk_entry_new();
	gtk_widget_set_hexpand(cd->ent_to, TRUE);
	if (prefill_to) gtk_entry_set_text(GTK_ENTRY(cd->ent_to), prefill_to);

	GtkWidget *l_cc = gtk_label_new("Cc:");
	gtk_label_set_xalign(GTK_LABEL(l_cc), 1.0f);
	cd->ent_cc = gtk_entry_new();
	gtk_widget_set_hexpand(cd->ent_cc, TRUE);

	GtkWidget *l_sub = gtk_label_new("Subject:");
	gtk_label_set_xalign(GTK_LABEL(l_sub), 1.0f);
	cd->ent_subj = gtk_entry_new();
	gtk_widget_set_hexpand(cd->ent_subj, TRUE);
	if (prefill_subj) gtk_entry_set_text(GTK_ENTRY(cd->ent_subj), prefill_subj);

	gtk_grid_attach(GTK_GRID(grid), l_to, 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), cd->ent_to, 1, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), l_cc, 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), cd->ent_cc, 1, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), l_sub, 0, 2, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), cd->ent_subj, 1, 2, 1, 1);
	gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 0);

	GtkWidget *tv = gtk_text_view_new();
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 10);
	gtk_text_view_set_right_margin(GTK_TEXT_VIEW(tv), 10);
	gtk_text_view_set_top_margin(GTK_TEXT_VIEW(tv), 8);
	cd->buf_body = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
	if (prefill_body) gtk_text_buffer_set_text(cd->buf_body, prefill_body, -1);

	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(scroll), tv);
	gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

	gtk_widget_show_all(cd->win);
}

static void on_btn_compose_clicked(GtkWidget *btn, gpointer user_data) {
	(void)btn;
	MailApp *app = user_data;
	open_compose_window(app, "", "", "");
}

static char *build_quoted_body(MailApp *app, const MailSummary *ms) {
	GtkTextIter start, end;
	gtk_text_buffer_get_bounds(app->body_buffer, &start, &end);
	gchar *orig = gtk_text_buffer_get_text(app->body_buffer, &start, &end, FALSE);
	if (!orig) return NULL;

	size_t cap = strlen(orig) * 2 + 512;
	char *quoted = malloc(cap);
	if (!quoted) {
		g_free(orig);
		return NULL;
	}

	int n = snprintf(quoted, cap, "\n\n--- On %s, %s wrote: ---\n", ms->date_str, ms->from);
	size_t pos = (n > 0) ? (size_t)n : 0;

	const char *p = orig;
	while (*p && pos + 4 < cap) {
		quoted[pos++] = '>';
		quoted[pos++] = ' ';
		while (*p && *p != '\n' && pos + 2 < cap) {
			quoted[pos++] = *p++;
		}
		if (*p == '\n') {
			quoted[pos++] = *p++;
		}
	}
	quoted[pos] = '\0';
	g_free(orig);
	return quoted;
}

static void on_btn_reply_clicked(GtkWidget *btn, gpointer user_data) {
	(void)btn;
	MailApp *app = user_data;
	if (app->selected_msg_idx < 0 || app->selected_msg_idx >= app->msg_count) {
		show_info_banner(app, "Select a message to reply to.", GTK_MESSAGE_WARNING);
		return;
	}
	MailSummary *ms = &app->messages[app->selected_msg_idx];
	char subj[300];
	if (g_ascii_strncasecmp(ms->subject, "Re:", 3) == 0) {
		snprintf(subj, sizeof(subj), "%s", ms->subject);
	} else {
		snprintf(subj, sizeof(subj), "Re: %s", ms->subject);
	}
	char *quoted = build_quoted_body(app, ms);
	open_compose_window(app, ms->from, subj, quoted ? quoted : "");
	free(quoted);
	quoted = NULL;
}

static void on_btn_forward_clicked(GtkWidget *btn, gpointer user_data) {
	(void)btn;
	MailApp *app = user_data;
	if (app->selected_msg_idx < 0 || app->selected_msg_idx >= app->msg_count) {
		show_info_banner(app, "Select a message to forward.", GTK_MESSAGE_WARNING);
		return;
	}
	MailSummary *ms = &app->messages[app->selected_msg_idx];
	char subj[300];
	if (g_ascii_strncasecmp(ms->subject, "Fwd:", 4) == 0) {
		snprintf(subj, sizeof(subj), "%s", ms->subject);
	} else {
		snprintf(subj, sizeof(subj), "Fwd: %s", ms->subject);
	}
	char *quoted = build_quoted_body(app, ms);
	open_compose_window(app, "", subj, quoted ? quoted : "");
	free(quoted);
	quoted = NULL;
}

static void on_btn_refresh_clicked(GtkWidget *btn, gpointer user_data) {
	(void)btn;
	MailApp *app = user_data;
	int saved_idx = app->active_folder_idx;
	populate_folders(app);
	if (saved_idx >= 0 && saved_idx < app->folder_count) {
		load_folder_messages(app, saved_idx);
	}
}

static void on_app_destroy(GtkWidget *w, gpointer user_data) {
	(void)w;
	MailApp *app = user_data;
	clear_active_attachments(app);
	if (app->msg_sort) {
		g_object_unref(app->msg_sort);
		app->msg_sort = NULL;
	}
	if (app->msg_filter) {
		g_object_unref(app->msg_filter);
		app->msg_filter = NULL;
	}
	if (app->msg_store) {
		g_object_unref(app->msg_store);
		app->msg_store = NULL;
	}
	if (app->folder_store) {
		g_object_unref(app->folder_store);
		app->folder_store = NULL;
	}
}

int main(int argc, char **argv) {
	g_set_prgname("diamonds-mail");
	gtk_init(&argc, &argv);
	diamonds_gtk_apply_theme(MAIL_EXTRA_CSS);

	MailApp app;
	memset(&app, 0, sizeof(app));
	app.selected_msg_idx = -1;

	bootstrap_local_maildir(&app);

	app.window = diamonds_gtk_window_new("Diamonds Mail", 1040, 640);
	g_signal_connect(app.window, "destroy", G_CALLBACK(on_app_destroy), &app);

	/* HeaderBar */
	GtkWidget *hbar = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hbar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(hbar), "Diamonds Mail");
	gtk_header_bar_set_subtitle(GTK_HEADER_BAR(hbar), "Local Maildir & Mbox Client");

	GtkWidget *btn_compose = gtk_button_new_with_label("Compose");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_compose), "action-btn");
	g_signal_connect(btn_compose, "clicked", G_CALLBACK(on_btn_compose_clicked), &app);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(hbar), btn_compose);

	GtkWidget *btn_reply = gtk_button_new_with_label("Reply");
	g_signal_connect(btn_reply, "clicked", G_CALLBACK(on_btn_reply_clicked), &app);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(hbar), btn_reply);

	GtkWidget *btn_fwd = gtk_button_new_with_label("Forward");
	g_signal_connect(btn_fwd, "clicked", G_CALLBACK(on_btn_forward_clicked), &app);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(hbar), btn_fwd);

	GtkWidget *btn_del = gtk_button_new_with_label("Delete");
	gtk_style_context_add_class(gtk_widget_get_style_context(btn_del), "destructive-action");
	g_signal_connect(btn_del, "clicked", G_CALLBACK(on_btn_delete_clicked), &app);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(hbar), btn_del);

	app.search_entry = gtk_search_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(app.search_entry), "Search Subject or From...");
	gtk_widget_set_size_request(app.search_entry, 240, -1);
	g_signal_connect(app.search_entry, "search-changed", G_CALLBACK(on_search_changed), &app);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(hbar), app.search_entry);

	GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh");
	g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_btn_refresh_clicked), &app);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(hbar), btn_refresh);

	gtk_window_set_titlebar(GTK_WINDOW(app.window), hbar);

	/* Root VBox: InfoBar + 3-Pane Layout */
	GtkWidget *root_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(app.window), root_vbox);

	app.info_bar = gtk_info_bar_new();
	gtk_info_bar_set_show_close_button(GTK_INFO_BAR(app.info_bar), TRUE);
	g_signal_connect(app.info_bar, "response", G_CALLBACK(gtk_widget_hide), NULL);
	app.lbl_info = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_info), 0.0f);
	GtkWidget *ib_content = gtk_info_bar_get_content_area(GTK_INFO_BAR(app.info_bar));
	gtk_container_add(GTK_CONTAINER(ib_content), app.lbl_info);
	gtk_widget_set_no_show_all(app.info_bar, TRUE);
	gtk_box_pack_start(GTK_BOX(root_vbox), app.info_bar, FALSE, FALSE, 0);

	GtkWidget *outer_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	GtkWidget *inner_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_set_position(GTK_PANED(outer_paned), 190);
	gtk_paned_set_position(GTK_PANED(inner_paned), 360);
	gtk_box_pack_start(GTK_BOX(root_vbox), outer_paned, TRUE, TRUE, 0);

	/* Pane 1: Folder list */
	app.folder_store = gtk_list_store_new(FCOL_COUNT, G_TYPE_STRING, G_TYPE_INT);
	app.folder_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app.folder_store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(app.folder_view), FALSE);
	gtk_style_context_add_class(gtk_widget_get_style_context(app.folder_view), "sidebar");

	GtkCellRenderer *f_rend = gtk_cell_renderer_text_new();
	g_object_set(f_rend, "ypad", 6, "xpad", 8, NULL);
	GtkTreeViewColumn *f_col = gtk_tree_view_column_new_with_attributes("Folders", f_rend, "text", FCOL_LABEL, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(app.folder_view), f_col);

	GtkTreeSelection *f_sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(app.folder_view));
	g_signal_connect(f_sel, "changed", G_CALLBACK(on_folder_selection_changed), &app);

	GtkWidget *f_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(f_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(f_scroll), app.folder_view);
	gtk_paned_pack1(GTK_PANED(outer_paned), f_scroll, FALSE, FALSE);
	gtk_paned_pack2(GTK_PANED(outer_paned), inner_paned, TRUE, FALSE);

	/* Pane 2: Message List */
	app.msg_store = gtk_list_store_new(MCOL_COUNT,
		G_TYPE_STRING,  /* MCOL_FROM */
		G_TYPE_STRING,  /* MCOL_SUBJECT */
		G_TYPE_STRING,  /* MCOL_DATE */
		G_TYPE_INT64,   /* MCOL_TIMESTAMP */
		G_TYPE_INT,     /* MCOL_WEIGHT */
		G_TYPE_INT      /* MCOL_INDEX */
	);
	app.msg_filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(app.msg_store), NULL);
	gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(app.msg_filter),
		msg_filter_visible_func, &app, NULL);

	app.msg_sort = gtk_tree_model_sort_new_with_model(app.msg_filter);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(app.msg_sort),
		MCOL_TIMESTAMP, GTK_SORT_DESCENDING);

	app.msg_view = gtk_tree_view_new_with_model(app.msg_sort);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(app.msg_view), TRUE);

	const char *titles[3] = {"From", "Subject", "Date"};
	int cols[3] = {MCOL_FROM, MCOL_SUBJECT, MCOL_DATE};
	int sort_cols[3] = {MCOL_FROM, MCOL_SUBJECT, MCOL_TIMESTAMP};
	int widths[3] = {110, 140, 100};
	for (int i = 0; i < 3; i++) {
		GtkCellRenderer *r = gtk_cell_renderer_text_new();
		g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
		GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(
			titles[i], r, "text", cols[i], "weight", MCOL_WEIGHT, NULL);
		gtk_tree_view_column_set_sort_column_id(c, sort_cols[i]);
		gtk_tree_view_column_set_resizable(c, TRUE);
		gtk_tree_view_column_set_min_width(c, widths[i]);
		gtk_tree_view_append_column(GTK_TREE_VIEW(app.msg_view), c);
	}

	GtkTreeSelection *m_sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(app.msg_view));
	g_signal_connect(m_sel, "changed", G_CALLBACK(on_msg_selection_changed), &app);

	GtkWidget *m_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(m_scroll), app.msg_view);
	gtk_paned_pack1(GTK_PANED(inner_paned), m_scroll, FALSE, FALSE);

	/* Pane 3: Reader Pane */
	GtkWidget *reader_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	GtkWidget *hdr_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_style_context_add_class(gtk_widget_get_style_context(hdr_box), "reader-header");

	app.lbl_hdr_subject = gtk_label_new("Select a message to read");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_hdr_subject), "reader-subj");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_hdr_subject), 0.0f);
	gtk_label_set_line_wrap(GTK_LABEL(app.lbl_hdr_subject), TRUE);
	gtk_label_set_selectable(GTK_LABEL(app.lbl_hdr_subject), TRUE);
	gtk_box_pack_start(GTK_BOX(hdr_box), app.lbl_hdr_subject, FALSE, FALSE, 0);

	GtkWidget *meta_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	app.lbl_hdr_from = gtk_label_new("");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_hdr_from), "reader-meta");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_hdr_from), 0.0f);
	gtk_label_set_selectable(GTK_LABEL(app.lbl_hdr_from), TRUE);
	gtk_box_pack_start(GTK_BOX(meta_row), app.lbl_hdr_from, TRUE, TRUE, 0);

	app.lbl_hdr_date = gtk_label_new("");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_hdr_date), "reader-meta");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_hdr_date), 1.0f);
	gtk_box_pack_end(GTK_BOX(meta_row), app.lbl_hdr_date, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hdr_box), meta_row, FALSE, FALSE, 0);

	app.lbl_hdr_to = gtk_label_new("");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.lbl_hdr_to), "reader-meta");
	gtk_label_set_xalign(GTK_LABEL(app.lbl_hdr_to), 0.0f);
	gtk_label_set_selectable(GTK_LABEL(app.lbl_hdr_to), TRUE);
	gtk_box_pack_start(GTK_BOX(hdr_box), app.lbl_hdr_to, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(reader_vbox), hdr_box, FALSE, FALSE, 0);

	app.attach_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_style_context_add_class(gtk_widget_get_style_context(app.attach_box), "attach-bar");
	gtk_widget_set_no_show_all(app.attach_box, TRUE);
	gtk_box_pack_start(GTK_BOX(reader_vbox), app.attach_box, FALSE, FALSE, 0);

	app.body_view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(app.body_view), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(app.body_view), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app.body_view), GTK_WRAP_WORD_CHAR);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app.body_view), 16);
	gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app.body_view), 16);
	gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app.body_view), 12);
	gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app.body_view), 12);
	app.body_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app.body_view));

	GtkWidget *b_scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(b_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(b_scroll), app.body_view);
	gtk_box_pack_start(GTK_BOX(reader_vbox), b_scroll, TRUE, TRUE, 0);

	gtk_paned_pack2(GTK_PANED(inner_paned), reader_vbox, TRUE, FALSE);

	populate_folders(&app);

	/* Select Inbox on startup */
	GtkTreeIter first_iter;
	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(app.folder_store), &first_iter)) {
		gtk_tree_selection_select_iter(f_sel, &first_iter);
	}
	if (gtk_tree_model_get_iter_first(app.msg_sort, &first_iter)) {
		gtk_tree_selection_select_iter(m_sel, &first_iter);
	}

	gtk_widget_show_all(app.window);
	gtk_widget_hide(app.info_bar);
	gtk_main();

	return 0;
}
