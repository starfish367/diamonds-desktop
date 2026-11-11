#include "diamonds_gtk.h"

#include <cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <gdk/gdkkeysyms.h>
#include <glib-unix.h>
#include <pango/pangocairo.h>
#include <pty.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define SCROLLBACK_MAX 2000
#define DEFAULT_COLS 80
#define DEFAULT_ROWS 24
#define MAX_COLS 512
#define MAX_ROWS 256
#define CSI_MAX_PARAMS 16
#define OSC_MAX_LEN 512
#define DEFAULT_FONT_SIZE 11

/* Color mode for TermCell */
enum {
	COLOR_DEFAULT_FG = 0,
	COLOR_DEFAULT_BG = 1,
	COLOR_INDEXED    = 2,
	COLOR_TRUECOLOR  = 3
};

/* Attribute flags */
enum {
	ATTR_BOLD      = (1 << 0),
	ATTR_DIM       = (1 << 1),
	ATTR_ITALIC    = (1 << 2),
	ATTR_UNDERLINE = (1 << 3),
	ATTR_REVERSE   = (1 << 4),
	ATTR_STRIKE    = (1 << 5),
	ATTR_WIDE      = (1 << 6),
	ATTR_WIDE_CONT = (1 << 7)
};

typedef struct {
	uint8_t mode;
	uint8_t index;
	uint8_t r, g, b;
} TermColor;

typedef struct {
	uint32_t ch;
	TermColor fg;
	TermColor bg;
	uint8_t attrs;
} TermCell;

typedef struct {
	int cols;
	TermCell *cells;
	bool dirty;
} TermLine;

typedef enum {
	PARSER_NORMAL = 0,
	PARSER_ESC,
	PARSER_ESC_CHARSET,
	PARSER_CSI,
	PARSER_OSC,
	PARSER_OSC_ESC
} ParserState;

typedef struct {
	int row;
	int col;
	TermColor fg;
	TermColor bg;
	uint8_t attrs;
} SavedCursor;

typedef struct TermApp TermApp;

typedef struct {
	TermApp *app;
	GtkWidget *tab_box;
	GtkWidget *tab_label;
	GtkWidget *drawing_area;
	GtkIMContext *im_context;

	int master_fd;
	pid_t child_pid;
	guint fd_watch_id;
	guint child_watch_id;

	int rows;
	int cols;
	int cell_w;
	int cell_h;
	int font_ascent;

	/* Primary and Alternate screens */
	TermLine *primary_lines;
	TermLine *alt_lines;
	TermLine *lines; /* Points to active screen lines */
	bool is_alt_screen;

	/* Scrollback ring buffer */
	TermLine scrollback[SCROLLBACK_MAX];
	int sb_head;  /* Index of oldest line */
	int sb_count; /* Number of stored lines */
	int scroll_offset; /* 0 = at bottom, >0 = lines scrolled back */

	/* Cursor & attributes */
	int cur_row;
	int cur_col;
	bool wrap_next;
	TermColor cur_fg;
	TermColor cur_bg;
	uint8_t cur_attrs;
	SavedCursor saved_primary;
	SavedCursor saved_alt;

	/* Scroll region (inclusive, 0-based) */
	int scroll_top;
	int scroll_bottom;

	/* Terminal modes */
	bool cursor_visible;
	bool app_cursor;
	bool bracketed_paste;
	bool autowrap;

	/* VT & UTF-8 parser state */
	ParserState state;
	uint32_t utf8_codepoint;
	int utf8_bytes_left;

	int csi_params[CSI_MAX_PARAMS];
	int csi_nparams;
	bool csi_has_digit;
	char csi_priv;

	char osc_buf[OSC_MAX_LEN];
	int osc_len;

	/* Selection state (in virtual coordinates: -sb_count .. rows-1) */
	bool selecting;
	bool has_selection;
	int sel_start_vrow;
	int sel_start_col;
	int sel_end_vrow;
	int sel_end_col;

	char title[256];
} TermTab;

struct TermApp {
	GtkWidget *window;
	GtkWidget *header_bar;
	GtkWidget *notebook;
	PangoFontDescription *font_desc;
	int font_size;
	int cell_w;
	int cell_h;
	int font_ascent;
	GList *tabs;
};

static const char *TERMINAL_EXTRA_CSS =
	"notebook { background-color: #12121a; border: none; }"
	"notebook stack { background-color: #12121a; }"
	"notebook header { background-color: #1a1b26; border-bottom: 1px solid #282a36; padding: 0; }"
	"notebook tab { padding: 4px 10px; background-color: transparent; border: none; }"
	"notebook tab:checked { background-color: #12121a; box-shadow: inset 0 -2px #38bdf8; }"
	".tab-close-btn { padding: 0px 4px; min-width: 18px; min-height: 18px; border-radius: 4px; border: none; background: transparent; color: #94a3b8; }"
	".tab-close-btn:hover { background-color: rgba(255,255,255,0.12); color: #f8fafc; }";

/* Standard 16 ANSI colors tuned for Dark Mica #12121a background */
static const uint8_t ANSI_PALETTE[16][3] = {
	{ 0x1a, 0x1b, 0x26 }, /* 0: Black */
	{ 0xf7, 0x76, 0x8e }, /* 1: Red */
	{ 0x9e, 0xce, 0x6a }, /* 2: Green */
	{ 0xe0, 0xaf, 0x68 }, /* 3: Yellow */
	{ 0x7a, 0xa2, 0xf7 }, /* 4: Blue */
	{ 0xbb, 0x9a, 0xf7 }, /* 5: Magenta */
	{ 0x38, 0xbd, 0xf8 }, /* 6: Cyan */
	{ 0xc0, 0xca, 0xf5 }, /* 7: White */
	{ 0x41, 0x48, 0x68 }, /* 8: Bright Black */
	{ 0xff, 0x89, 0x9d }, /* 9: Bright Red */
	{ 0xb9, 0xf2, 0x7c }, /* 10: Bright Green */
	{ 0xff, 0xc7, 0x77 }, /* 11: Bright Yellow */
	{ 0x8d, 0xb6, 0xff }, /* 12: Bright Blue */
	{ 0xc7, 0xa9, 0xff }, /* 13: Bright Magenta */
	{ 0x7d, 0xcf, 0xff }, /* 14: Bright Cyan */
	{ 0xf8, 0xfa, 0xfc }  /* 15: Bright White */
};

static TermCell make_blank_cell(TermColor fg, TermColor bg) {
	TermCell c;
	c.ch = ' ';
	c.fg = fg;
	c.bg = bg;
	c.attrs = 0;
	return c;
}

static TermColor default_fg_color(void) {
	TermColor c = { COLOR_DEFAULT_FG, 0, 0xe2, 0xe8, 0xf0 };
	return c;
}

static TermColor default_bg_color(void) {
	TermColor c = { COLOR_DEFAULT_BG, 0, 0x12, 0x12, 0x1a };
	return c;
}

static void resolve_color(TermColor tc, bool is_fg, bool bold, double *r, double *g, double *b) {
	if (tc.mode == COLOR_DEFAULT_FG) {
		*r = 0xe2 / 255.0;
		*g = 0xe8 / 255.0;
		*b = 0xf0 / 255.0;
		return;
	}
	if (tc.mode == COLOR_DEFAULT_BG) {
		*r = 0x12 / 255.0;
		*g = 0x12 / 255.0;
		*b = 0x1a / 255.0;
		return;
	}
	if (tc.mode == COLOR_TRUECOLOR) {
		*r = tc.r / 255.0;
		*g = tc.g / 255.0;
		*b = tc.b / 255.0;
		return;
	}
	uint8_t idx = tc.index;
	if (is_fg && bold && idx < 8) {
		idx += 8;
	}
	if (idx < 16) {
		*r = ANSI_PALETTE[idx][0] / 255.0;
		*g = ANSI_PALETTE[idx][1] / 255.0;
		*b = ANSI_PALETTE[idx][2] / 255.0;
	} else if (idx < 232) {
		uint8_t n = idx - 16;
		uint8_t rv = (n / 36) % 6;
		uint8_t gv = (n / 6) % 6;
		uint8_t bv = n % 6;
		*r = (rv ? (rv * 40 + 55) : 0) / 255.0;
		*g = (gv ? (gv * 40 + 55) : 0) / 255.0;
		*b = (bv ? (bv * 40 + 55) : 0) / 255.0;
	} else {
		uint8_t gray = (idx - 232) * 10 + 8;
		*r = gray / 255.0;
		*g = gray / 255.0;
		*b = gray / 255.0;
	}
}

static void line_init(TermLine *line, int cols, TermColor fg, TermColor bg) {
	line->cols = cols;
	line->cells = calloc((size_t)cols, sizeof(TermCell));
	line->dirty = true;
	if (!line->cells) {
		line->cols = 0;
		return;
	}
	TermCell blank = make_blank_cell(fg, bg);
	for (int i = 0; i < cols; i++) {
		line->cells[i] = blank;
	}
}

static void line_free(TermLine *line) {
	if (line && line->cells) {
		free(line->cells);
		line->cells = NULL;
		line->cols = 0;
	}
}

static void line_clear_range(TermLine *line, int start, int end, TermColor fg, TermColor bg) {
	if (!line || !line->cells) return;
	if (start < 0) start = 0;
	if (end > line->cols) end = line->cols;
	TermCell blank = make_blank_cell(fg, bg);
	for (int i = start; i < end; i++) {
		line->cells[i] = blank;
	}
	line->dirty = true;
}

static void mark_row_dirty(TermTab *tab, int row) {
	if (row >= 0 && row < tab->rows && tab->lines) {
		tab->lines[row].dirty = true;
	}
}

static void mark_all_dirty(TermTab *tab) {
	if (!tab || !tab->lines) return;
	for (int r = 0; r < tab->rows; r++) {
		tab->lines[r].dirty = true;
	}
}

static void queue_draw_dirty_rows(TermTab *tab) {
	if (!tab || !tab->drawing_area || !gtk_widget_get_realized(tab->drawing_area)) return;
	for (int r = 0; r < tab->rows; r++) {
		if (tab->lines[r].dirty) {
			int start_r = r;
			while (r < tab->rows && tab->lines[r].dirty) {
				tab->lines[r].dirty = false;
				r++;
			}
			int y = start_r * tab->cell_h;
			int h = (r - start_r) * tab->cell_h;
			int w = gtk_widget_get_allocated_width(tab->drawing_area);
			gtk_widget_queue_draw_area(tab->drawing_area, 0, y, w, h);
		}
	}
}

static void push_scrollback_line(TermTab *tab, const TermLine *src) {
	if (!src || !src->cells || src->cols <= 0) return;
	int idx;
	if (tab->sb_count < SCROLLBACK_MAX) {
		idx = (tab->sb_head + tab->sb_count) % SCROLLBACK_MAX;
		tab->sb_count++;
	} else {
		idx = tab->sb_head;
		line_free(&tab->scrollback[idx]);
		tab->sb_head = (tab->sb_head + 1) % SCROLLBACK_MAX;
	}
	tab->scrollback[idx].cols = src->cols;
	tab->scrollback[idx].cells = malloc((size_t)src->cols * sizeof(TermCell));
	if (tab->scrollback[idx].cells) {
		memcpy(tab->scrollback[idx].cells, src->cells, (size_t)src->cols * sizeof(TermCell));
	} else {
		tab->scrollback[idx].cols = 0;
	}
	tab->scrollback[idx].dirty = false;
}

static const TermLine *get_virtual_line(const TermTab *tab, int vrow) {
	if (vrow < 0) {
		int sb_idx = tab->sb_count + vrow;
		if (sb_idx < 0 || sb_idx >= tab->sb_count) return NULL;
		int ring_idx = (tab->sb_head + sb_idx) % SCROLLBACK_MAX;
		return &tab->scrollback[ring_idx];
	}
	if (vrow >= 0 && vrow < tab->rows) {
		return &tab->lines[vrow];
	}
	return NULL;
}

static void scroll_region_up(TermTab *tab, int top, int bottom, int count) {
	if (top < 0) top = 0;
	if (bottom >= tab->rows) bottom = tab->rows - 1;
	if (top > bottom || count <= 0) return;
	if (count > bottom - top + 1) count = bottom - top + 1;

	for (int i = 0; i < count; i++) {
		if (!tab->is_alt_screen && top == 0) {
			push_scrollback_line(tab, &tab->lines[top + i]);
		}
		line_free(&tab->lines[top + i]);
	}

	int move_count = (bottom - top + 1) - count;
	if (move_count > 0) {
		memmove(&tab->lines[top], &tab->lines[top + count], (size_t)move_count * sizeof(TermLine));
	}

	for (int r = bottom - count + 1; r <= bottom; r++) {
		line_init(&tab->lines[r], tab->cols, default_fg_color(), tab->cur_bg);
	}
	for (int r = top; r <= bottom; r++) {
		tab->lines[r].dirty = true;
	}
}

static void scroll_region_down(TermTab *tab, int top, int bottom, int count) {
	if (top < 0) top = 0;
	if (bottom >= tab->rows) bottom = tab->rows - 1;
	if (top > bottom || count <= 0) return;
	if (count > bottom - top + 1) count = bottom - top + 1;

	for (int i = 0; i < count; i++) {
		line_free(&tab->lines[bottom - i]);
	}

	int move_count = (bottom - top + 1) - count;
	if (move_count > 0) {
		memmove(&tab->lines[top + count], &tab->lines[top], (size_t)move_count * sizeof(TermLine));
	}

	for (int r = top; r < top + count; r++) {
		line_init(&tab->lines[r], tab->cols, default_fg_color(), tab->cur_bg);
	}
	for (int r = top; r <= bottom; r++) {
		tab->lines[r].dirty = true;
	}
}

static void write_pty_all(int fd, const char *buf, size_t len) {
	if (fd < 0 || !buf || len == 0) return;
	size_t off = 0;
	while (off < len) {
		ssize_t n = write(fd, buf + off, len - off);
		if (n < 0) {
			if (errno == EINTR) continue;
			break;
		}
		off += (size_t)n;
	}
}

static void term_linefeed(TermTab *tab) {
	tab->wrap_next = false;
	if (tab->cur_row == tab->scroll_bottom) {
		scroll_region_up(tab, tab->scroll_top, tab->scroll_bottom, 1);
	} else if (tab->cur_row < tab->rows - 1) {
		tab->cur_row++;
	}
}

static void term_put_char(TermTab *tab, uint32_t ch) {
	if (!g_unichar_isprint((gunichar)ch) && ch < 0xA0) return;
	if (g_unichar_iszerowidth((gunichar)ch)) return;

	int width = g_unichar_iswide((gunichar)ch) ? 2 : 1;

	if (tab->wrap_next && tab->autowrap) {
		mark_row_dirty(tab, tab->cur_row);
		tab->cur_col = 0;
		term_linefeed(tab);
	}

	if (width == 2 && tab->cur_col == tab->cols - 1) {
		if (tab->autowrap) {
			TermLine *line = &tab->lines[tab->cur_row];
			if (line->cells && tab->cur_col < line->cols) {
				line->cells[tab->cur_col] = make_blank_cell(tab->cur_fg, tab->cur_bg);
				line->dirty = true;
			}
			tab->cur_col = 0;
			term_linefeed(tab);
		} else {
			return;
		}
	}

	if (tab->cur_row < 0) tab->cur_row = 0;
	if (tab->cur_row >= tab->rows) tab->cur_row = tab->rows - 1;
	if (tab->cur_col < 0) tab->cur_col = 0;
	if (tab->cur_col >= tab->cols) tab->cur_col = tab->cols - 1;

	TermLine *line = &tab->lines[tab->cur_row];
	if (!line->cells) return;

	/* Clear broken wide char if overwriting half of one */
	if ((line->cells[tab->cur_col].attrs & ATTR_WIDE_CONT) && tab->cur_col > 0) {
		line->cells[tab->cur_col - 1].ch = ' ';
		line->cells[tab->cur_col - 1].attrs &= ~ATTR_WIDE;
	} else if ((line->cells[tab->cur_col].attrs & ATTR_WIDE) && tab->cur_col + 1 < tab->cols) {
		line->cells[tab->cur_col + 1].ch = ' ';
		line->cells[tab->cur_col + 1].attrs &= ~ATTR_WIDE_CONT;
	}

	TermCell cell;
	cell.ch = ch;
	cell.fg = tab->cur_fg;
	cell.bg = tab->cur_bg;
	cell.attrs = tab->cur_attrs | (width == 2 ? ATTR_WIDE : 0);
	line->cells[tab->cur_col] = cell;

	if (width == 2 && tab->cur_col + 1 < tab->cols) {
		TermCell cont = make_blank_cell(tab->cur_fg, tab->cur_bg);
		cont.attrs = tab->cur_attrs | ATTR_WIDE_CONT;
		line->cells[tab->cur_col + 1] = cont;
	}
	line->dirty = true;

	if (tab->cur_col + width >= tab->cols) {
		tab->cur_col = tab->cols - 1;
		tab->wrap_next = true;
	} else {
		tab->cur_col += width;
		tab->wrap_next = false;
	}
}

static void save_cursor_state(TermTab *tab, SavedCursor *sc) {
	sc->row = tab->cur_row;
	sc->col = tab->cur_col;
	sc->fg = tab->cur_fg;
	sc->bg = tab->cur_bg;
	sc->attrs = tab->cur_attrs;
}

static void restore_cursor_state(TermTab *tab, const SavedCursor *sc) {
	mark_row_dirty(tab, tab->cur_row);
	tab->cur_row = sc->row;
	tab->cur_col = sc->col;
	if (tab->cur_row < 0) tab->cur_row = 0;
	if (tab->cur_row >= tab->rows) tab->cur_row = tab->rows - 1;
	if (tab->cur_col < 0) tab->cur_col = 0;
	if (tab->cur_col >= tab->cols) tab->cur_col = tab->cols - 1;
	tab->cur_fg = sc->fg;
	tab->cur_bg = sc->bg;
	tab->cur_attrs = sc->attrs;
	tab->wrap_next = false;
	mark_row_dirty(tab, tab->cur_row);
}

static void switch_screen_buffer(TermTab *tab, bool to_alt, bool clear_alt) {
	if (tab->is_alt_screen == to_alt) return;
	tab->is_alt_screen = to_alt;
	tab->lines = to_alt ? tab->alt_lines : tab->primary_lines;
	tab->scroll_offset = 0;
	tab->has_selection = false;
	tab->selecting = false;
	if (to_alt && clear_alt) {
		for (int r = 0; r < tab->rows; r++) {
			line_clear_range(&tab->alt_lines[r], 0, tab->cols, default_fg_color(), default_bg_color());
		}
		tab->cur_row = 0;
		tab->cur_col = 0;
		tab->wrap_next = false;
	}
	mark_all_dirty(tab);
}

static void handle_sgr(TermTab *tab) {
	if (tab->csi_nparams == 0) {
		tab->csi_params[0] = 0;
		tab->csi_nparams = 1;
	}
	for (int i = 0; i < tab->csi_nparams; i++) {
		int p = tab->csi_params[i];
		if (p == 0) {
			tab->cur_fg = default_fg_color();
			tab->cur_bg = default_bg_color();
			tab->cur_attrs = 0;
		} else if (p == 1) {
			tab->cur_attrs |= ATTR_BOLD;
		} else if (p == 2) {
			tab->cur_attrs |= ATTR_DIM;
		} else if (p == 3) {
			tab->cur_attrs |= ATTR_ITALIC;
		} else if (p == 4) {
			tab->cur_attrs |= ATTR_UNDERLINE;
		} else if (p == 7) {
			tab->cur_attrs |= ATTR_REVERSE;
		} else if (p == 9) {
			tab->cur_attrs |= ATTR_STRIKE;
		} else if (p == 21 || p == 22) {
			tab->cur_attrs &= ~(ATTR_BOLD | ATTR_DIM);
		} else if (p == 23) {
			tab->cur_attrs &= ~ATTR_ITALIC;
		} else if (p == 24) {
			tab->cur_attrs &= ~ATTR_UNDERLINE;
		} else if (p == 27) {
			tab->cur_attrs &= ~ATTR_REVERSE;
		} else if (p == 29) {
			tab->cur_attrs &= ~ATTR_STRIKE;
		} else if (p >= 30 && p <= 37) {
			tab->cur_fg.mode = COLOR_INDEXED;
			tab->cur_fg.index = (uint8_t)(p - 30);
		} else if (p == 39) {
			tab->cur_fg = default_fg_color();
		} else if (p >= 40 && p <= 47) {
			tab->cur_bg.mode = COLOR_INDEXED;
			tab->cur_bg.index = (uint8_t)(p - 40);
		} else if (p == 49) {
			tab->cur_bg = default_bg_color();
		} else if (p >= 90 && p <= 97) {
			tab->cur_fg.mode = COLOR_INDEXED;
			tab->cur_fg.index = (uint8_t)(p - 90 + 8);
		} else if (p >= 100 && p <= 107) {
			tab->cur_bg.mode = COLOR_INDEXED;
			tab->cur_bg.index = (uint8_t)(p - 100 + 8);
		} else if (p == 38 || p == 48) {
			TermColor *tc = (p == 38) ? &tab->cur_fg : &tab->cur_bg;
			if (i + 1 < tab->csi_nparams) {
				int mode = tab->csi_params[i + 1];
				if (mode == 5 && i + 2 < tab->csi_nparams) {
					int idx = tab->csi_params[i + 2];
					if (idx >= 0 && idx <= 255) {
						tc->mode = COLOR_INDEXED;
						tc->index = (uint8_t)idx;
					}
					i += 2;
				} else if (mode == 2 && i + 4 < tab->csi_nparams) {
					int r = tab->csi_params[i + 2];
					int g = tab->csi_params[i + 3];
					int b = tab->csi_params[i + 4];
					tc->mode = COLOR_TRUECOLOR;
					tc->r = (uint8_t)CLAMP(r, 0, 255);
					tc->g = (uint8_t)CLAMP(g, 0, 255);
					tc->b = (uint8_t)CLAMP(b, 0, 255);
					i += 4;
				}
			}
		}
	}
}

static void handle_dec_private_mode(TermTab *tab, bool set) {
	for (int i = 0; i < tab->csi_nparams; i++) {
		int p = tab->csi_params[i];
		switch (p) {
		case 1:
			tab->app_cursor = set;
			break;
		case 7:
			tab->autowrap = set;
			break;
		case 25:
			tab->cursor_visible = set;
			mark_row_dirty(tab, tab->cur_row);
			break;
		case 47:
		case 1047:
			switch_screen_buffer(tab, set, set);
			break;
		case 1048:
			if (set) {
				save_cursor_state(tab, tab->is_alt_screen ? &tab->saved_alt : &tab->saved_primary);
			} else {
				restore_cursor_state(tab, tab->is_alt_screen ? &tab->saved_alt : &tab->saved_primary);
			}
			break;
		case 1049:
			if (set) {
				save_cursor_state(tab, &tab->saved_primary);
				switch_screen_buffer(tab, true, true);
			} else {
				switch_screen_buffer(tab, false, false);
				restore_cursor_state(tab, &tab->saved_primary);
			}
			break;
		case 2004:
			tab->bracketed_paste = set;
			break;
		default:
			break;
		}
	}
}

static int csi_param_or(const TermTab *tab, int idx, int def_val) {
	if (idx >= tab->csi_nparams || tab->csi_params[idx] <= 0) {
		return def_val;
	}
	return tab->csi_params[idx];
}

static void handle_csi_dispatch(TermTab *tab, char final_ch) {
	if (tab->csi_priv == '?') {
		if (final_ch == 'h') {
			handle_dec_private_mode(tab, true);
		} else if (final_ch == 'l') {
			handle_dec_private_mode(tab, false);
		}
		return;
	}
	if (tab->csi_priv != 0) {
		return;
	}

	mark_row_dirty(tab, tab->cur_row);
	int n = csi_param_or(tab, 0, 1);
	TermLine *line = &tab->lines[tab->cur_row];

	switch (final_ch) {
	case 'A': /* CUU - Cursor Up */
		tab->cur_row = MAX(tab->scroll_top, tab->cur_row - n);
		tab->wrap_next = false;
		break;
	case 'B': /* CUD - Cursor Down */
		tab->cur_row = MIN(tab->scroll_bottom, tab->cur_row + n);
		tab->wrap_next = false;
		break;
	case 'C': /* CUF - Cursor Forward */
		tab->cur_col = MIN(tab->cols - 1, tab->cur_col + n);
		tab->wrap_next = false;
		break;
	case 'D': /* CUB - Cursor Back */
		tab->cur_col = MAX(0, tab->cur_col - n);
		tab->wrap_next = false;
		break;
	case 'E': /* CNL - Cursor Next Line */
		tab->cur_row = MIN(tab->scroll_bottom, tab->cur_row + n);
		tab->cur_col = 0;
		tab->wrap_next = false;
		break;
	case 'F': /* CPL - Cursor Previous Line */
		tab->cur_row = MAX(tab->scroll_top, tab->cur_row - n);
		tab->cur_col = 0;
		tab->wrap_next = false;
		break;
	case 'G':
	case '`': /* CHA - Cursor Horizontal Absolute */
		tab->cur_col = CLAMP(n - 1, 0, tab->cols - 1);
		tab->wrap_next = false;
		break;
	case 'H':
	case 'f': { /* CUP - Cursor Position */
		int r = csi_param_or(tab, 0, 1);
		int c = csi_param_or(tab, 1, 1);
		tab->cur_row = CLAMP(r - 1, 0, tab->rows - 1);
		tab->cur_col = CLAMP(c - 1, 0, tab->cols - 1);
		tab->wrap_next = false;
		break;
	}
	case 'd': /* VPA - Vertical Position Absolute */
		tab->cur_row = CLAMP(n - 1, 0, tab->rows - 1);
		tab->wrap_next = false;
		break;
	case 'J': { /* ED - Erase in Display */
		int mode = (tab->csi_nparams > 0) ? tab->csi_params[0] : 0;
		if (mode == 0) {
			line_clear_range(line, tab->cur_col, tab->cols, tab->cur_fg, tab->cur_bg);
			for (int r = tab->cur_row + 1; r < tab->rows; r++) {
				line_clear_range(&tab->lines[r], 0, tab->cols, tab->cur_fg, tab->cur_bg);
			}
		} else if (mode == 1) {
			for (int r = 0; r < tab->cur_row; r++) {
				line_clear_range(&tab->lines[r], 0, tab->cols, tab->cur_fg, tab->cur_bg);
			}
			line_clear_range(line, 0, tab->cur_col + 1, tab->cur_fg, tab->cur_bg);
		} else if (mode == 2 || mode == 3) {
			for (int r = 0; r < tab->rows; r++) {
				line_clear_range(&tab->lines[r], 0, tab->cols, tab->cur_fg, tab->cur_bg);
			}
			if (mode == 3 && !tab->is_alt_screen) {
				for (int i = 0; i < tab->sb_count; i++) {
					int idx = (tab->sb_head + i) % SCROLLBACK_MAX;
					line_free(&tab->scrollback[idx]);
				}
				tab->sb_head = 0;
				tab->sb_count = 0;
				tab->scroll_offset = 0;
			}
		}
		break;
	}
	case 'K': { /* EL - Erase in Line */
		int mode = (tab->csi_nparams > 0) ? tab->csi_params[0] : 0;
		if (mode == 0) {
			line_clear_range(line, tab->cur_col, tab->cols, tab->cur_fg, tab->cur_bg);
		} else if (mode == 1) {
			line_clear_range(line, 0, tab->cur_col + 1, tab->cur_fg, tab->cur_bg);
		} else if (mode == 2) {
			line_clear_range(line, 0, tab->cols, tab->cur_fg, tab->cur_bg);
		}
		break;
	}
	case 'X': /* ECH - Erase Characters */
		line_clear_range(line, tab->cur_col, MIN(tab->cols, tab->cur_col + n), tab->cur_fg, tab->cur_bg);
		break;
	case 'P': { /* DCH - Delete Characters */
		if (line->cells && tab->cur_col < tab->cols) {
			int del = MIN(n, tab->cols - tab->cur_col);
			int rem = tab->cols - tab->cur_col - del;
			if (rem > 0) {
				memmove(&line->cells[tab->cur_col], &line->cells[tab->cur_col + del], (size_t)rem * sizeof(TermCell));
			}
			line_clear_range(line, tab->cols - del, tab->cols, tab->cur_fg, tab->cur_bg);
		}
		break;
	}
	case '@': { /* ICH - Insert Blank Characters */
		if (line->cells && tab->cur_col < tab->cols) {
			int ins = MIN(n, tab->cols - tab->cur_col);
			int rem = tab->cols - tab->cur_col - ins;
			if (rem > 0) {
				memmove(&line->cells[tab->cur_col + ins], &line->cells[tab->cur_col], (size_t)rem * sizeof(TermCell));
			}
			line_clear_range(line, tab->cur_col, tab->cur_col + ins, tab->cur_fg, tab->cur_bg);
		}
		break;
	}
	case 'L': /* IL - Insert Lines */
		if (tab->cur_row >= tab->scroll_top && tab->cur_row <= tab->scroll_bottom) {
			scroll_region_down(tab, tab->cur_row, tab->scroll_bottom, n);
		}
		break;
	case 'M': /* DL - Delete Lines */
		if (tab->cur_row >= tab->scroll_top && tab->cur_row <= tab->scroll_bottom) {
			scroll_region_up(tab, tab->cur_row, tab->scroll_bottom, n);
		}
		break;
	case 'S': /* SU - Scroll Up */
		scroll_region_up(tab, tab->scroll_top, tab->scroll_bottom, n);
		break;
	case 'T': /* SD - Scroll Down */
		scroll_region_down(tab, tab->scroll_top, tab->scroll_bottom, n);
		break;
	case 'r': { /* DECSTBM - Set Top and Bottom Margins */
		int top = csi_param_or(tab, 0, 1) - 1;
		int bot = csi_param_or(tab, 1, tab->rows) - 1;
		top = CLAMP(top, 0, tab->rows - 1);
		bot = CLAMP(bot, 0, tab->rows - 1);
		if (top < bot) {
			tab->scroll_top = top;
			tab->scroll_bottom = bot;
			tab->cur_row = 0;
			tab->cur_col = 0;
			tab->wrap_next = false;
		}
		break;
	}
	case 'm':
		handle_sgr(tab);
		break;
	case 's':
		save_cursor_state(tab, tab->is_alt_screen ? &tab->saved_alt : &tab->saved_primary);
		break;
	case 'u':
		restore_cursor_state(tab, tab->is_alt_screen ? &tab->saved_alt : &tab->saved_primary);
		break;
	case 'n': { /* DSR - Device Status Report */
		int code = (tab->csi_nparams > 0) ? tab->csi_params[0] : 0;
		if (code == 5) {
			write_pty_all(tab->master_fd, "\x1b[0n", 4);
		} else if (code == 6) {
			char resp[64];
			int len = snprintf(resp, sizeof(resp), "\x1b[%d;%dR", tab->cur_row + 1, tab->cur_col + 1);
			if (len > 0) write_pty_all(tab->master_fd, resp, (size_t)len);
		}
		break;
	}
	case 'c': /* DA1 - Primary Device Attributes */
		write_pty_all(tab->master_fd, "\x1b[?1;2c", 7);
		break;
	default:
		break;
	}

	mark_row_dirty(tab, tab->cur_row);
}

static void update_tab_and_window_title(TermTab *tab, const char *new_title) {
	if (!tab || !new_title) return;
	snprintf(tab->title, sizeof(tab->title), "%s", *new_title ? new_title : "Terminal");
	if (tab->tab_label) {
		gtk_label_set_text(GTK_LABEL(tab->tab_label), tab->title);
	}
	if (tab->app && tab->app->notebook && tab->app->header_bar) {
		int cur_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(tab->app->notebook));
		GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(tab->app->notebook), cur_page);
		if (page == tab->drawing_area) {
			gtk_header_bar_set_subtitle(GTK_HEADER_BAR(tab->app->header_bar), tab->title);
			gtk_window_set_title(GTK_WINDOW(tab->app->window), tab->title);
		}
	}
}

static void handle_osc_dispatch(TermTab *tab) {
	tab->osc_buf[tab->osc_len] = '\0';
	char *semi = strchr(tab->osc_buf, ';');
	if (!semi) return;
	*semi = '\0';
	int cmd = atoi(tab->osc_buf);
	const char *arg = semi + 1;
	if (cmd == 0 || cmd == 2) {
		update_tab_and_window_title(tab, arg);
	}
}

static void term_reset_full(TermTab *tab) {
	tab->cur_fg = default_fg_color();
	tab->cur_bg = default_bg_color();
	tab->cur_attrs = 0;
	tab->cur_row = 0;
	tab->cur_col = 0;
	tab->wrap_next = false;
	tab->scroll_top = 0;
	tab->scroll_bottom = tab->rows - 1;
	tab->cursor_visible = true;
	tab->app_cursor = false;
	tab->bracketed_paste = false;
	tab->autowrap = true;
	switch_screen_buffer(tab, false, false);
	for (int r = 0; r < tab->rows; r++) {
		line_clear_range(&tab->primary_lines[r], 0, tab->cols, tab->cur_fg, tab->cur_bg);
		line_clear_range(&tab->alt_lines[r], 0, tab->cols, tab->cur_fg, tab->cur_bg);
	}
}

static void term_feed_byte(TermTab *tab, uint8_t b) {
	/* Handle active OSC state before control characters */
	if (tab->state == PARSER_OSC) {
		if (b == 0x07) { /* BEL terminates OSC */
			handle_osc_dispatch(tab);
			tab->state = PARSER_NORMAL;
		} else if (b == 0x1b) {
			tab->state = PARSER_OSC_ESC;
		} else if (b >= 0x20 && tab->osc_len < OSC_MAX_LEN - 1) {
			tab->osc_buf[tab->osc_len++] = (char)b;
		}
		return;
	}
	if (tab->state == PARSER_OSC_ESC) {
		if (b == '\\') { /* ST terminates OSC */
			handle_osc_dispatch(tab);
		}
		tab->state = PARSER_NORMAL;
		return;
	}

	/* C0 control characters can occur mid-sequence */
	if (b < 0x20 || b == 0x7f) {
		switch (b) {
		case 0x07: /* BEL */
			if (tab->app && tab->app->window) {
				gdk_window_beep(gtk_widget_get_window(tab->app->window));
			}
			break;
		case 0x08: /* BS */
			mark_row_dirty(tab, tab->cur_row);
			if (tab->cur_col > 0) tab->cur_col--;
			tab->wrap_next = false;
			mark_row_dirty(tab, tab->cur_row);
			break;
		case 0x09: /* TAB */
			mark_row_dirty(tab, tab->cur_row);
			tab->cur_col = MIN(tab->cols - 1, (tab->cur_col + 8) & ~7);
			tab->wrap_next = false;
			mark_row_dirty(tab, tab->cur_row);
			break;
		case 0x0a: /* LF */
		case 0x0b: /* VT */
		case 0x0c: /* FF */
			mark_row_dirty(tab, tab->cur_row);
			term_linefeed(tab);
			mark_row_dirty(tab, tab->cur_row);
			break;
		case 0x0d: /* CR */
			mark_row_dirty(tab, tab->cur_row);
			tab->cur_col = 0;
			tab->wrap_next = false;
			mark_row_dirty(tab, tab->cur_row);
			break;
		case 0x1b: /* ESC */
			tab->state = PARSER_ESC;
			tab->utf8_bytes_left = 0;
			break;
		default:
			break;
		}
		return;
	}

	switch (tab->state) {
	case PARSER_ESC:
		if (b == '[') {
			tab->state = PARSER_CSI;
			tab->csi_nparams = 0;
			tab->csi_has_digit = false;
			tab->csi_priv = 0;
			memset(tab->csi_params, 0, sizeof(tab->csi_params));
		} else if (b == ']') {
			tab->state = PARSER_OSC;
			tab->osc_len = 0;
		} else if (b == '(' || b == ')' || b == '*' || b == '+') {
			tab->state = PARSER_ESC_CHARSET;
		} else {
			if (b == '7') {
				save_cursor_state(tab, tab->is_alt_screen ? &tab->saved_alt : &tab->saved_primary);
			} else if (b == '8') {
				restore_cursor_state(tab, tab->is_alt_screen ? &tab->saved_alt : &tab->saved_primary);
			} else if (b == 'M') { /* RI - Reverse Index */
				mark_row_dirty(tab, tab->cur_row);
				if (tab->cur_row == tab->scroll_top) {
					scroll_region_down(tab, tab->scroll_top, tab->scroll_bottom, 1);
				} else if (tab->cur_row > 0) {
					tab->cur_row--;
				}
				tab->wrap_next = false;
				mark_row_dirty(tab, tab->cur_row);
			} else if (b == 'D') { /* IND - Index */
				mark_row_dirty(tab, tab->cur_row);
				term_linefeed(tab);
				mark_row_dirty(tab, tab->cur_row);
			} else if (b == 'E') { /* NEL - Next Line */
				mark_row_dirty(tab, tab->cur_row);
				tab->cur_col = 0;
				term_linefeed(tab);
				mark_row_dirty(tab, tab->cur_row);
			} else if (b == 'c') { /* RIS - Full Reset */
				term_reset_full(tab);
			}
			tab->state = PARSER_NORMAL;
		}
		return;

	case PARSER_ESC_CHARSET:
		tab->state = PARSER_NORMAL;
		return;

	case PARSER_CSI:
		if (b >= '0' && b <= '9') {
			if (tab->csi_nparams == 0) tab->csi_nparams = 1;
			int idx = tab->csi_nparams - 1;
			if (idx < CSI_MAX_PARAMS) {
				int val = tab->csi_params[idx] * 10 + (b - '0');
				tab->csi_params[idx] = MIN(val, 65535);
			}
			tab->csi_has_digit = true;
		} else if (b == ';' || b == ':') {
			if (tab->csi_nparams == 0) tab->csi_nparams = 1;
			if (tab->csi_nparams < CSI_MAX_PARAMS) {
				tab->csi_nparams++;
				tab->csi_params[tab->csi_nparams - 1] = 0;
			}
			tab->csi_has_digit = false;
		} else if (b == '?' || b == '>' || b == ' ') {
			tab->csi_priv = (char)b;
		} else if (b >= 0x40 && b <= 0x7e) {
			handle_csi_dispatch(tab, (char)b);
			tab->state = PARSER_NORMAL;
		}
		return;

	case PARSER_NORMAL:
	default:
		break;
	}

	/* UTF-8 decoding in PARSER_NORMAL */
	if (tab->utf8_bytes_left > 0) {
		if ((b & 0xc0) == 0x80) {
			tab->utf8_codepoint = (tab->utf8_codepoint << 6) | (b & 0x3f);
			tab->utf8_bytes_left--;
			if (tab->utf8_bytes_left == 0) {
				term_put_char(tab, tab->utf8_codepoint);
			}
		} else {
			tab->utf8_bytes_left = 0;
			if (b < 0x80) {
				term_put_char(tab, b);
			}
		}
	} else {
		if (b < 0x80) {
			term_put_char(tab, b);
		} else if ((b & 0xe0) == 0xc0) {
			tab->utf8_codepoint = b & 0x1f;
			tab->utf8_bytes_left = 1;
		} else if ((b & 0xf0) == 0xe0) {
			tab->utf8_codepoint = b & 0x0f;
			tab->utf8_bytes_left = 2;
		} else if ((b & 0xf8) == 0xf0) {
			tab->utf8_codepoint = b & 0x07;
			tab->utf8_bytes_left = 3;
		}
	}
}

static bool is_cell_selected(const TermTab *tab, int vrow, int col) {
	if (!tab->has_selection) return false;
	int r1 = tab->sel_start_vrow, c1 = tab->sel_start_col;
	int r2 = tab->sel_end_vrow, c2 = tab->sel_end_col;
	if (r1 > r2 || (r1 == r2 && c1 > c2)) {
		int tr = r1, tc = c1;
		r1 = r2;
		c1 = c2;
		r2 = tr;
		c2 = tc;
	}
	if (vrow < r1 || vrow > r2) return false;
	if (r1 == r2) return col >= c1 && col <= c2;
	if (vrow == r1) return col >= c1;
	if (vrow == r2) return col <= c2;
	return true;
}

static char *extract_selection_text(const TermTab *tab) {
	if (!tab || !tab->has_selection) return NULL;
	int r1 = tab->sel_start_vrow, c1 = tab->sel_start_col;
	int r2 = tab->sel_end_vrow, c2 = tab->sel_end_col;
	if (r1 > r2 || (r1 == r2 && c1 > c2)) {
		int tr = r1, tc = c1;
		r1 = r2;
		c1 = c2;
		r2 = tr;
		c2 = tc;
	}

	GString *str = g_string_new(NULL);
	for (int vr = r1; vr <= r2; vr++) {
		const TermLine *line = get_virtual_line(tab, vr);
		if (!line || !line->cells) continue;
		int start_c = (vr == r1) ? MAX(0, c1) : 0;
		int end_c = (vr == r2) ? MIN(line->cols - 1, c2) : (line->cols - 1);

		/* Trim trailing spaces unless mid-line selection */
		int last_non_space = start_c - 1;
		for (int c = start_c; c <= end_c; c++) {
			if (line->cells[c].attrs & ATTR_WIDE_CONT) continue;
			uint32_t ch = line->cells[c].ch ? line->cells[c].ch : ' ';
			if (ch != ' ' || vr == r2) {
				last_non_space = c;
			}
		}

		for (int c = start_c; c <= last_non_space; c++) {
			if (line->cells[c].attrs & ATTR_WIDE_CONT) continue;
			uint32_t ch = line->cells[c].ch ? line->cells[c].ch : ' ';
			g_string_append_unichar(str, (gunichar)ch);
		}
		if (vr < r2) {
			g_string_append_c(str, '\n');
		}
	}
	return g_string_free(str, FALSE);
}

static void copy_selection_to_clipboard(TermTab *tab, GdkAtom selection) {
	char *text = extract_selection_text(tab);
	if (!text) return;
	if (*text) {
		GtkClipboard *cb = gtk_clipboard_get(selection);
		if (cb) {
			gtk_clipboard_set_text(cb, text, -1);
		}
	}
	g_free(text);
}

static void on_clipboard_received(GtkClipboard *clipboard, const gchar *text, gpointer user_data) {
	(void)clipboard;
	TermTab *tab = user_data;
	if (!tab || !text || !*text || tab->master_fd < 0) return;

	if (tab->scroll_offset != 0) {
		tab->scroll_offset = 0;
		mark_all_dirty(tab);
		queue_draw_dirty_rows(tab);
	}

	if (tab->bracketed_paste) {
		write_pty_all(tab->master_fd, "\x1b[200~", 6);
	}
	write_pty_all(tab->master_fd, text, strlen(text));
	if (tab->bracketed_paste) {
		write_pty_all(tab->master_fd, "\x1b[201~", 6);
	}
}

static void paste_from_clipboard(TermTab *tab) {
	if (!tab) return;
	GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	if (cb) {
		gtk_clipboard_request_text(cb, on_clipboard_received, tab);
	}
}

static gboolean on_drawing_area_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
	TermTab *tab = user_data;
	if (!tab || !tab->app) return FALSE;

	double clip_x1, clip_y1, clip_x2, clip_y2;
	cairo_clip_extents(cr, &clip_x1, &clip_y1, &clip_x2, &clip_y2);

	/* Fill background #12121a */
	cairo_set_source_rgb(cr, 0x12 / 255.0, 0x12 / 255.0, 0x1a / 255.0);
	cairo_paint(cr);

	int first_row = MAX(0, (int)(clip_y1 / tab->cell_h));
	int last_row = MIN(tab->rows - 1, (int)(clip_y2 / tab->cell_h));

	PangoLayout *layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, tab->app->font_desc);

	PangoFontDescription *bold_desc = pango_font_description_copy_static(tab->app->font_desc);
	pango_font_description_set_weight(bold_desc, PANGO_WEIGHT_BOLD);

	for (int sr = first_row; sr <= last_row; sr++) {
		int vrow = sr - tab->scroll_offset;
		const TermLine *line = get_virtual_line(tab, vrow);
		if (!line || !line->cells) continue;

		int y = sr * tab->cell_h;
		int max_c = MIN(tab->cols, line->cols);

		/* Pass 1: Cell backgrounds & selection highlight */
		for (int c = 0; c < max_c; c++) {
			const TermCell *cell = &line->cells[c];
			if (cell->attrs & ATTR_WIDE_CONT) continue;

			int cw = (cell->attrs & ATTR_WIDE) ? (tab->cell_w * 2) : tab->cell_w;
			bool rev = (cell->attrs & ATTR_REVERSE) != 0;
			bool sel = is_cell_selected(tab, vrow, c);

			double br, bg, bb;
			if (sel) {
				br = 0x02 / 255.0;
				bg = 0x84 / 255.0;
				bb = 0xc7 / 255.0;
			} else if (rev) {
				resolve_color(cell->fg, true, (cell->attrs & ATTR_BOLD) != 0, &br, &bg, &bb);
			} else {
				resolve_color(cell->bg, false, false, &br, &bg, &bb);
			}

			if (sel || rev || cell->bg.mode != COLOR_DEFAULT_BG) {
				cairo_set_source_rgb(cr, br, bg, bb);
				cairo_rectangle(cr, c * tab->cell_w, y, cw, tab->cell_h);
				cairo_fill(cr);
			}
		}

		/* Pass 2: Block cursor in #38BDF8 (blinking-free) */
		bool draw_cursor_here = (tab->cursor_visible && vrow == tab->cur_row);
		if (draw_cursor_here && tab->cur_col >= 0 && tab->cur_col < tab->cols) {
			int cur_w = tab->cell_w;
			if (tab->cur_col < max_c && (line->cells[tab->cur_col].attrs & ATTR_WIDE)) {
				cur_w = tab->cell_w * 2;
			}
			cairo_set_source_rgb(cr, 0x38 / 255.0, 0xbd / 255.0, 0xf8 / 255.0);
			if (gtk_widget_has_focus(widget)) {
				cairo_rectangle(cr, tab->cur_col * tab->cell_w, y, cur_w, tab->cell_h);
				cairo_fill(cr);
			} else {
				cairo_set_line_width(cr, 1.0);
				cairo_rectangle(cr, tab->cur_col * tab->cell_w + 0.5, y + 0.5, cur_w - 1.0, tab->cell_h - 1.0);
				cairo_stroke(cr);
			}
		}

		/* Pass 3: Foreground glyphs & underlines */
		for (int c = 0; c < max_c; c++) {
			const TermCell *cell = &line->cells[c];
			if (cell->attrs & ATTR_WIDE_CONT) continue;

			int cw = (cell->attrs & ATTR_WIDE) ? (tab->cell_w * 2) : tab->cell_w;
			bool rev = (cell->attrs & ATTR_REVERSE) != 0;
			bool sel = is_cell_selected(tab, vrow, c);
			bool is_cursor = (draw_cursor_here && c == tab->cur_col && gtk_widget_has_focus(widget));

			double fr, fg, fb;
			if (is_cursor) {
				fr = 0x12 / 255.0;
				fg = 0x12 / 255.0;
				fb = 0x1a / 255.0;
			} else if (sel) {
				fr = 1.0;
				fg = 1.0;
				fb = 1.0;
			} else if (rev) {
				resolve_color(cell->bg, false, false, &fr, &fg, &fb);
			} else {
				resolve_color(cell->fg, true, (cell->attrs & ATTR_BOLD) != 0, &fr, &fg, &fb);
			}

			if (cell->attrs & ATTR_DIM) {
				fr *= 0.65;
				fg *= 0.65;
				fb *= 0.65;
			}

			if (cell->ch > ' ' && cell->ch != 0x7f) {
				char utf8[8];
				int ulen = g_unichar_to_utf8((gunichar)cell->ch, utf8);
				pango_layout_set_font_description(layout,
					(cell->attrs & ATTR_BOLD) ? bold_desc : tab->app->font_desc);
				pango_layout_set_text(layout, utf8, ulen);

				cairo_set_source_rgb(cr, fr, fg, fb);
				cairo_move_to(cr, c * tab->cell_w, y);
				pango_cairo_show_layout(cr, layout);
			}

			if (cell->attrs & ATTR_UNDERLINE) {
				cairo_set_source_rgb(cr, fr, fg, fb);
				cairo_rectangle(cr, c * tab->cell_w, y + tab->cell_h - 2, cw, 1);
				cairo_fill(cr);
			}
			if (cell->attrs & ATTR_STRIKE) {
				cairo_set_source_rgb(cr, fr, fg, fb);
				cairo_rectangle(cr, c * tab->cell_w, y + tab->cell_h / 2, cw, 1);
				cairo_fill(cr);
			}
		}
	}

	pango_font_description_free(bold_desc);
	g_object_unref(layout);
	return TRUE;
}

static void resize_screen_lines(TermLine **lines_ptr, int old_rows, int new_rows, int new_cols, TermColor fg, TermColor bg) {
	TermLine *old_lines = *lines_ptr;
	TermLine *new_lines = calloc((size_t)new_rows, sizeof(TermLine));
	if (!new_lines) return;

	int copy_rows = MIN(old_rows, new_rows);
	for (int r = 0; r < copy_rows; r++) {
		line_init(&new_lines[r], new_cols, fg, bg);
		if (old_lines && old_lines[r].cells && new_lines[r].cells) {
			int copy_cols = MIN(old_lines[r].cols, new_cols);
			memcpy(new_lines[r].cells, old_lines[r].cells, (size_t)copy_cols * sizeof(TermCell));
		}
	}
	for (int r = copy_rows; r < new_rows; r++) {
		line_init(&new_lines[r], new_cols, fg, bg);
	}

	if (old_lines) {
		for (int r = 0; r < old_rows; r++) {
			line_free(&old_lines[r]);
		}
		free(old_lines);
	}
	*lines_ptr = new_lines;
}

static void term_tab_resize(TermTab *tab, int new_cols, int new_rows) {
	new_cols = CLAMP(new_cols, 10, MAX_COLS);
	new_rows = CLAMP(new_rows, 2, MAX_ROWS);
	if (new_cols == tab->cols && new_rows == tab->rows) return;

	int old_rows = tab->rows;
	resize_screen_lines(&tab->primary_lines, old_rows, new_rows, new_cols, default_fg_color(), default_bg_color());
	resize_screen_lines(&tab->alt_lines, old_rows, new_rows, new_cols, default_fg_color(), default_bg_color());

	tab->lines = tab->is_alt_screen ? tab->alt_lines : tab->primary_lines;
	tab->cols = new_cols;
	tab->rows = new_rows;
	tab->cur_row = CLAMP(tab->cur_row, 0, tab->rows - 1);
	tab->cur_col = CLAMP(tab->cur_col, 0, tab->cols - 1);
	tab->scroll_top = 0;
	tab->scroll_bottom = tab->rows - 1;
	tab->wrap_next = false;
	mark_all_dirty(tab);

	if (tab->master_fd >= 0) {
		struct winsize ws;
		memset(&ws, 0, sizeof(ws));
		ws.ws_col = (unsigned short)tab->cols;
		ws.ws_row = (unsigned short)tab->rows;
		ws.ws_xpixel = (unsigned short)(tab->cols * tab->cell_w);
		ws.ws_ypixel = (unsigned short)(tab->rows * tab->cell_h);
		(void)ioctl(tab->master_fd, TIOCSWINSZ, &ws);
	}
}

static void on_drawing_area_size_allocate(GtkWidget *widget, GdkRectangle *allocation, gpointer user_data) {
	(void)widget;
	TermTab *tab = user_data;
	if (!tab || tab->cell_w <= 0 || tab->cell_h <= 0) return;

	int new_cols = MAX(10, allocation->width / tab->cell_w);
	int new_rows = MAX(2, allocation->height / tab->cell_h);
	term_tab_resize(tab, new_cols, new_rows);
}

static void scroll_view_by(TermTab *tab, int delta) {
	if (!tab || tab->is_alt_screen) return;
	int new_off = CLAMP(tab->scroll_offset + delta, 0, tab->sb_count);
	if (new_off != tab->scroll_offset) {
		tab->scroll_offset = new_off;
		mark_all_dirty(tab);
		queue_draw_dirty_rows(tab);
	}
}

static gboolean on_scroll_event(GtkWidget *widget, GdkEventScroll *event, gpointer user_data) {
	(void)widget;
	TermTab *tab = user_data;
	if (!tab) return FALSE;

	if (event->direction == GDK_SCROLL_UP) {
		scroll_view_by(tab, 3);
		return TRUE;
	}
	if (event->direction == GDK_SCROLL_DOWN) {
		scroll_view_by(tab, -3);
		return TRUE;
	}
	if (event->direction == GDK_SCROLL_SMOOTH) {
		double dx = 0.0, dy = 0.0;
		if (gdk_event_get_scroll_deltas((const GdkEvent *)event, &dx, &dy)) {
			if (dy < -0.1) scroll_view_by(tab, 3);
			else if (dy > 0.1) scroll_view_by(tab, -3);
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
	TermTab *tab = user_data;
	if (!tab) return FALSE;

	gtk_widget_grab_focus(widget);

	if (event->button == 1 && event->type == GDK_BUTTON_PRESS) {
		int col = CLAMP((int)(event->x / tab->cell_w), 0, tab->cols - 1);
		int srow = CLAMP((int)(event->y / tab->cell_h), 0, tab->rows - 1);
		int vrow = srow - tab->scroll_offset;

		tab->selecting = true;
		tab->has_selection = false;
		tab->sel_start_vrow = vrow;
		tab->sel_start_col = col;
		tab->sel_end_vrow = vrow;
		tab->sel_end_col = col;
		mark_all_dirty(tab);
		queue_draw_dirty_rows(tab);
		return TRUE;
	}
	if (event->button == 2 && event->type == GDK_BUTTON_PRESS) {
		GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
		if (cb) {
			gtk_clipboard_request_text(cb, on_clipboard_received, tab);
		}
		return TRUE;
	}
	return FALSE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
	(void)widget;
	TermTab *tab = user_data;
	if (!tab || !tab->selecting) return FALSE;

	int col = CLAMP((int)(event->x / tab->cell_w), 0, tab->cols - 1);
	int srow = CLAMP((int)(event->y / tab->cell_h), 0, tab->rows - 1);
	int vrow = srow - tab->scroll_offset;

	if (vrow != tab->sel_end_vrow || col != tab->sel_end_col || !tab->has_selection) {
		tab->sel_end_vrow = vrow;
		tab->sel_end_col = col;
		tab->has_selection = (vrow != tab->sel_start_vrow || col != tab->sel_start_col);
		mark_all_dirty(tab);
		queue_draw_dirty_rows(tab);
	}
	return TRUE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
	(void)widget;
	TermTab *tab = user_data;
	if (!tab) return FALSE;

	if (event->button == 1 && tab->selecting) {
		tab->selecting = false;
		if (tab->has_selection) {
			copy_selection_to_clipboard(tab, GDK_SELECTION_PRIMARY);
			copy_selection_to_clipboard(tab, GDK_SELECTION_CLIPBOARD);
		}
		return TRUE;
	}
	return FALSE;
}

static void on_im_commit(GtkIMContext *context, const gchar *str, gpointer user_data) {
	(void)context;
	TermTab *tab = user_data;
	if (!tab || !str || !*str || tab->master_fd < 0) return;

	if (tab->scroll_offset != 0) {
		tab->scroll_offset = 0;
		mark_all_dirty(tab);
		queue_draw_dirty_rows(tab);
	}
	write_pty_all(tab->master_fd, str, strlen(str));
}

/* Forward declarations for tab & font actions */
static TermTab *app_create_tab(TermApp *app);
static void app_close_tab(TermTab *tab);
static void app_set_font_size(TermApp *app, int new_size);

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	(void)widget;
	TermTab *tab = user_data;
	if (!tab || !tab->app) return FALSE;

	GdkModifierType mods = event->state & gtk_accelerator_get_default_mod_mask();
	bool ctrl = (mods & GDK_CONTROL_MASK) != 0;
	bool shift = (mods & GDK_SHIFT_MASK) != 0;
	bool alt = (mods & GDK_MOD1_MASK) != 0;
	guint kv = event->keyval;

	/* Application shortcuts: Ctrl+Shift+C / V / T / W */
	if (ctrl && shift) {
		if (kv == GDK_KEY_C || kv == GDK_KEY_c) {
			copy_selection_to_clipboard(tab, GDK_SELECTION_CLIPBOARD);
			return TRUE;
		}
		if (kv == GDK_KEY_V || kv == GDK_KEY_v) {
			paste_from_clipboard(tab);
			return TRUE;
		}
		if (kv == GDK_KEY_T || kv == GDK_KEY_t) {
			app_create_tab(tab->app);
			return TRUE;
		}
		if (kv == GDK_KEY_W || kv == GDK_KEY_w) {
			app_close_tab(tab);
			return TRUE;
		}
	}

	/* Zoom shortcuts: Ctrl+plus / minus / 0 */
	if (ctrl && !alt) {
		if (kv == GDK_KEY_plus || kv == GDK_KEY_equal || kv == GDK_KEY_KP_Add) {
			app_set_font_size(tab->app, tab->app->font_size + 1);
			return TRUE;
		}
		if (kv == GDK_KEY_minus || kv == GDK_KEY_underscore || kv == GDK_KEY_KP_Subtract) {
			app_set_font_size(tab->app, tab->app->font_size - 1);
			return TRUE;
		}
		if (kv == GDK_KEY_0 || kv == GDK_KEY_KP_0) {
			app_set_font_size(tab->app, DEFAULT_FONT_SIZE);
			return TRUE;
		}
	}

	/* Shift + PageUp / PageDown scrollback */
	if (shift && !ctrl && !alt) {
		if (kv == GDK_KEY_Page_Up || kv == GDK_KEY_KP_Page_Up) {
			scroll_view_by(tab, MAX(1, tab->rows - 2));
			return TRUE;
		}
		if (kv == GDK_KEY_Page_Down || kv == GDK_KEY_KP_Page_Down) {
			scroll_view_by(tab, -MAX(1, tab->rows - 2));
			return TRUE;
		}
	}

	/* Reset scrollback offset when sending input to terminal */
	const char *seq = NULL;
	char seq_buf[32];

	switch (kv) {
	case GDK_KEY_Up:
	case GDK_KEY_KP_Up:
		seq = tab->app_cursor ? "\x1bOA" : "\x1b[A";
		break;
	case GDK_KEY_Down:
	case GDK_KEY_KP_Down:
		seq = tab->app_cursor ? "\x1bOB" : "\x1b[B";
		break;
	case GDK_KEY_Right:
	case GDK_KEY_KP_Right:
		seq = tab->app_cursor ? "\x1bOC" : "\x1b[C";
		break;
	case GDK_KEY_Left:
	case GDK_KEY_KP_Left:
		seq = tab->app_cursor ? "\x1bOD" : "\x1b[D";
		break;
	case GDK_KEY_Home:
	case GDK_KEY_KP_Home:
		seq = tab->app_cursor ? "\x1bOH" : "\x1b[H";
		break;
	case GDK_KEY_End:
	case GDK_KEY_KP_End:
		seq = tab->app_cursor ? "\x1bOF" : "\x1b[F";
		break;
	case GDK_KEY_Page_Up:
	case GDK_KEY_KP_Page_Up:
		seq = "\x1b[5~";
		break;
	case GDK_KEY_Page_Down:
	case GDK_KEY_KP_Page_Down:
		seq = "\x1b[6~";
		break;
	case GDK_KEY_Insert:
	case GDK_KEY_KP_Insert:
		seq = "\x1b[2~";
		break;
	case GDK_KEY_Delete:
	case GDK_KEY_KP_Delete:
		seq = "\x1b[3~";
		break;
	case GDK_KEY_F1:  seq = "\x1bOP"; break;
	case GDK_KEY_F2:  seq = "\x1bOQ"; break;
	case GDK_KEY_F3:  seq = "\x1bOR"; break;
	case GDK_KEY_F4:  seq = "\x1bOS"; break;
	case GDK_KEY_F5:  seq = "\x1b[15~"; break;
	case GDK_KEY_F6:  seq = "\x1b[17~"; break;
	case GDK_KEY_F7:  seq = "\x1b[18~"; break;
	case GDK_KEY_F8:  seq = "\x1b[19~"; break;
	case GDK_KEY_F9:  seq = "\x1b[20~"; break;
	case GDK_KEY_F10: seq = "\x1b[21~"; break;
	case GDK_KEY_F11: seq = "\x1b[23~"; break;
	case GDK_KEY_F12: seq = "\x1b[24~"; break;
	case GDK_KEY_BackSpace:
		seq = "\x7f";
		break;
	case GDK_KEY_Return:
	case GDK_KEY_KP_Enter:
		seq = "\r";
		break;
	case GDK_KEY_Tab:
	case GDK_KEY_KP_Tab:
		seq = shift ? "\x1b[Z" : "\t";
		break;
	case GDK_KEY_ISO_Left_Tab:
		seq = "\x1b[Z";
		break;
	case GDK_KEY_Escape:
		seq = "\x1b";
		break;
	default:
		break;
	}

	if (seq) {
		if (tab->scroll_offset != 0) {
			tab->scroll_offset = 0;
			mark_all_dirty(tab);
			queue_draw_dirty_rows(tab);
		}
		if (alt) write_pty_all(tab->master_fd, "\x1b", 1);
		write_pty_all(tab->master_fd, seq, strlen(seq));
		return TRUE;
	}

	/* Ctrl + letter / symbol */
	if (ctrl) {
		int c_out = -1;
		if (kv >= GDK_KEY_a && kv <= GDK_KEY_z) {
			c_out = (int)(kv - GDK_KEY_a + 1);
		} else if (kv >= GDK_KEY_A && kv <= GDK_KEY_Z) {
			c_out = (int)(kv - GDK_KEY_A + 1);
		} else if (kv == GDK_KEY_space || kv == GDK_KEY_at) {
			c_out = 0;
		} else if (kv == GDK_KEY_bracketleft) {
			c_out = 27;
		} else if (kv == GDK_KEY_backslash) {
			c_out = 28;
		} else if (kv == GDK_KEY_bracketright) {
			c_out = 29;
		} else if (kv == GDK_KEY_asciicircum) {
			c_out = 30;
		} else if (kv == GDK_KEY_underscore) {
			c_out = 31;
		}
		if (c_out >= 0) {
			char out_byte = (char)c_out;
			if (tab->scroll_offset != 0) {
				tab->scroll_offset = 0;
				mark_all_dirty(tab);
				queue_draw_dirty_rows(tab);
			}
			if (alt) write_pty_all(tab->master_fd, "\x1b", 1);
			write_pty_all(tab->master_fd, &out_byte, 1);
			return TRUE;
		}
	}

	/* Alt + printable key sends ESC prefix */
	if (alt && !ctrl) {
		uint32_t uc = gdk_keyval_to_unicode(kv);
		if (uc >= 0x20) {
			seq_buf[0] = '\x1b';
			int ulen = g_unichar_to_utf8((gunichar)uc, seq_buf + 1);
			if (tab->scroll_offset != 0) {
				tab->scroll_offset = 0;
				mark_all_dirty(tab);
				queue_draw_dirty_rows(tab);
			}
			write_pty_all(tab->master_fd, seq_buf, (size_t)(1 + ulen));
			return TRUE;
		}
	}

	/* Delegate normal text input to GtkIMContextSimple */
	if (tab->im_context && gtk_im_context_filter_keypress(tab->im_context, event)) {
		return TRUE;
	}

	return FALSE;
}

static gboolean on_key_release(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
	(void)widget;
	TermTab *tab = user_data;
	if (tab && tab->im_context && gtk_im_context_filter_keypress(tab->im_context, event)) {
		return TRUE;
	}
	return FALSE;
}

static gboolean on_focus_change(GtkWidget *widget, GdkEventFocus *event, gpointer user_data) {
	(void)widget;
	TermTab *tab = user_data;
	if (!tab) return FALSE;
	if (tab->im_context) {
		if (event->in) gtk_im_context_focus_in(tab->im_context);
		else gtk_im_context_focus_out(tab->im_context);
	}
	mark_row_dirty(tab, tab->cur_row);
	queue_draw_dirty_rows(tab);
	return FALSE;
}

static gboolean on_pty_readable(gint fd, GIOCondition condition, gpointer user_data) {
	TermTab *tab = user_data;
	if (!tab) return G_SOURCE_REMOVE;

	uint8_t buf[4096];
	bool got_data = false;

	if (condition & G_IO_IN) {
		for (int iter = 0; iter < 8; iter++) {
			ssize_t n = read(fd, buf, sizeof(buf));
			if (n > 0) {
				got_data = true;
				for (ssize_t i = 0; i < n; i++) {
					term_feed_byte(tab, buf[i]);
				}
			} else if (n == 0) {
				condition |= G_IO_HUP;
				break;
			} else {
				if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
				condition |= G_IO_ERR;
				break;
			}
		}
	}

	if (got_data) {
		queue_draw_dirty_rows(tab);
	}

	if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
		tab->fd_watch_id = 0;
		app_close_tab(tab);
		return G_SOURCE_REMOVE;
	}

	return G_SOURCE_CONTINUE;
}

static void on_child_exit(GPid pid, gint status, gpointer user_data) {
	(void)status;
	TermTab *tab = user_data;
	if (!tab) return;
	g_spawn_close_pid(pid);
	tab->child_pid = -1;
	tab->child_watch_id = 0;
	app_close_tab(tab);
}

static void free_tab_resources(TermTab *tab) {
	if (!tab) return;

	if (tab->fd_watch_id > 0) {
		g_source_remove(tab->fd_watch_id);
		tab->fd_watch_id = 0;
	}
	if (tab->child_watch_id > 0) {
		g_source_remove(tab->child_watch_id);
		tab->child_watch_id = 0;
	}
	if (tab->master_fd >= 0) {
		close(tab->master_fd);
		tab->master_fd = -1;
	}
	if (tab->child_pid > 0) {
		kill(tab->child_pid, SIGHUP);
		tab->child_pid = -1;
	}
	if (tab->im_context) {
		g_object_unref(tab->im_context);
		tab->im_context = NULL;
	}
	if (tab->primary_lines) {
		for (int r = 0; r < tab->rows; r++) {
			line_free(&tab->primary_lines[r]);
		}
		free(tab->primary_lines);
		tab->primary_lines = NULL;
	}
	if (tab->alt_lines) {
		for (int r = 0; r < tab->rows; r++) {
			line_free(&tab->alt_lines[r]);
		}
		free(tab->alt_lines);
		tab->alt_lines = NULL;
	}
	tab->lines = NULL;

	for (int i = 0; i < SCROLLBACK_MAX; i++) {
		line_free(&tab->scrollback[i]);
	}
}

static void app_close_tab(TermTab *tab) {
	if (!tab || !tab->app) return;
	TermApp *app = tab->app;

	if (!g_list_find(app->tabs, tab)) return;
	app->tabs = g_list_remove(app->tabs, tab);

	int page_num = gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), tab->drawing_area);
	free_tab_resources(tab);
	if (page_num >= 0) {
		gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), page_num);
	}
	free(tab);

	int remaining = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
	if (remaining <= 0) {
		gtk_widget_destroy(app->window);
	} else {
		gtk_notebook_set_show_tabs(GTK_NOTEBOOK(app->notebook), remaining > 1);
	}
}

static void on_tab_close_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	TermTab *tab = user_data;
	app_close_tab(tab);
}

static bool spawn_shell_pty(TermTab *tab) {
	struct winsize ws;
	memset(&ws, 0, sizeof(ws));
	ws.ws_col = (unsigned short)tab->cols;
	ws.ws_row = (unsigned short)tab->rows;
	ws.ws_xpixel = (unsigned short)(tab->cols * tab->cell_w);
	ws.ws_ypixel = (unsigned short)(tab->rows * tab->cell_h);

	int master_fd = -1;
	pid_t pid = forkpty(&master_fd, NULL, NULL, &ws);
	if (pid < 0) {
		return false;
	}

	if (pid == 0) {
		/* Child process */
		setenv("TERM", "xterm-256color", 1);
		setenv("COLORTERM", "truecolor", 1);
		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		signal(SIGTSTP, SIG_DFL);
		signal(SIGTTIN, SIG_DFL);
		signal(SIGTTOU, SIG_DFL);
		signal(SIGCHLD, SIG_DFL);

		const char *shell = getenv("SHELL");
		if (shell && *shell && access(shell, X_OK) == 0) {
			execl(shell, shell, (char *)NULL);
		}
		if (access("/bin/bash", X_OK) == 0) {
			execl("/bin/bash", "/bin/bash", (char *)NULL);
		}
		execl("/bin/sh", "/bin/sh", (char *)NULL);
		_exit(127);
	}

	int flags = fcntl(master_fd, F_GETFL, 0);
	if (flags >= 0) {
		(void)fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);
	}
	int cloexec = fcntl(master_fd, F_GETFD, 0);
	if (cloexec >= 0) {
		(void)fcntl(master_fd, F_SETFD, cloexec | FD_CLOEXEC);
	}

	tab->master_fd = master_fd;
	tab->child_pid = pid;
	tab->fd_watch_id = g_unix_fd_add(master_fd,
		(GIOCondition)(G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL),
		on_pty_readable, tab);
	tab->child_watch_id = g_child_watch_add(pid, on_child_exit, tab);
	return true;
}

static void measure_font_metrics(TermApp *app) {
	char font_str[64];
	snprintf(font_str, sizeof(font_str), "Monospace %d", app->font_size);
	if (app->font_desc) {
		pango_font_description_free(app->font_desc);
		app->font_desc = NULL;
	}
	app->font_desc = pango_font_description_from_string(font_str);

	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t *cr = cairo_create(surf);
	PangoLayout *layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, app->font_desc);
	pango_layout_set_text(layout, "W", 1);

	int w = 8, h = 16;
	pango_layout_get_pixel_size(layout, &w, &h);
	app->cell_w = MAX(4, w);
	app->cell_h = MAX(8, h + 2);
	app->font_ascent = h;

	g_object_unref(layout);
	cairo_destroy(cr);
	cairo_surface_destroy(surf);
}

static void app_set_font_size(TermApp *app, int new_size) {
	new_size = CLAMP(new_size, 7, 32);
	if (new_size == app->font_size) return;
	app->font_size = new_size;
	measure_font_metrics(app);

	for (GList *l = app->tabs; l != NULL; l = l->next) {
		TermTab *tab = l->data;
		tab->cell_w = app->cell_w;
		tab->cell_h = app->cell_h;
		tab->font_ascent = app->font_ascent;

		int alloc_w = gtk_widget_get_allocated_width(tab->drawing_area);
		int alloc_h = gtk_widget_get_allocated_height(tab->drawing_area);
		if (alloc_w > tab->cell_w && alloc_h > tab->cell_h) {
			term_tab_resize(tab, alloc_w / tab->cell_w, alloc_h / tab->cell_h);
		}
		mark_all_dirty(tab);
		gtk_widget_queue_draw(tab->drawing_area);
	}
}

static TermTab *app_create_tab(TermApp *app) {
	TermTab *tab = calloc(1, sizeof(TermTab));
	if (!tab) return NULL;

	tab->app = app;
	tab->master_fd = -1;
	tab->child_pid = -1;
	tab->cols = DEFAULT_COLS;
	tab->rows = DEFAULT_ROWS;
	tab->cell_w = app->cell_w;
	tab->cell_h = app->cell_h;
	tab->font_ascent = app->font_ascent;
	snprintf(tab->title, sizeof(tab->title), "Terminal");

	tab->primary_lines = calloc((size_t)tab->rows, sizeof(TermLine));
	tab->alt_lines = calloc((size_t)tab->rows, sizeof(TermLine));
	if (!tab->primary_lines || !tab->alt_lines) {
		free_tab_resources(tab);
		free(tab);
		return NULL;
	}
	for (int r = 0; r < tab->rows; r++) {
		line_init(&tab->primary_lines[r], tab->cols, default_fg_color(), default_bg_color());
		line_init(&tab->alt_lines[r], tab->cols, default_fg_color(), default_bg_color());
	}
	tab->lines = tab->primary_lines;
	term_reset_full(tab);
	save_cursor_state(tab, &tab->saved_primary);
	save_cursor_state(tab, &tab->saved_alt);

	tab->drawing_area = gtk_drawing_area_new();
	gtk_widget_set_can_focus(tab->drawing_area, TRUE);
	gtk_widget_set_hexpand(tab->drawing_area, TRUE);
	gtk_widget_set_vexpand(tab->drawing_area, TRUE);
	gtk_widget_add_events(tab->drawing_area,
		GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK |
		GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
		GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK |
		GDK_SMOOTH_SCROLL_MASK | GDK_FOCUS_CHANGE_MASK);

	tab->im_context = gtk_im_context_simple_new();
	g_signal_connect(tab->im_context, "commit", G_CALLBACK(on_im_commit), tab);

	g_signal_connect(tab->drawing_area, "draw", G_CALLBACK(on_drawing_area_draw), tab);
	g_signal_connect(tab->drawing_area, "size-allocate", G_CALLBACK(on_drawing_area_size_allocate), tab);
	g_signal_connect(tab->drawing_area, "key-press-event", G_CALLBACK(on_key_press), tab);
	g_signal_connect(tab->drawing_area, "key-release-event", G_CALLBACK(on_key_release), tab);
	g_signal_connect(tab->drawing_area, "button-press-event", G_CALLBACK(on_button_press), tab);
	g_signal_connect(tab->drawing_area, "button-release-event", G_CALLBACK(on_button_release), tab);
	g_signal_connect(tab->drawing_area, "motion-notify-event", G_CALLBACK(on_motion_notify), tab);
	g_signal_connect(tab->drawing_area, "scroll-event", G_CALLBACK(on_scroll_event), tab);
	g_signal_connect(tab->drawing_area, "focus-in-event", G_CALLBACK(on_focus_change), tab);
	g_signal_connect(tab->drawing_area, "focus-out-event", G_CALLBACK(on_focus_change), tab);

	/* Notebook tab header with label + close button */
	tab->tab_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	tab->tab_label = gtk_label_new(tab->title);
	gtk_label_set_ellipsize(GTK_LABEL(tab->tab_label), PANGO_ELLIPSIZE_END);
	gtk_label_set_width_chars(GTK_LABEL(tab->tab_label), 14);
	GtkWidget *close_btn = gtk_button_new_with_label("×");
	gtk_widget_set_can_focus(close_btn, FALSE);
	gtk_style_context_add_class(gtk_widget_get_style_context(close_btn), "tab-close-btn");
	g_signal_connect(close_btn, "clicked", G_CALLBACK(on_tab_close_clicked), tab);

	gtk_box_pack_start(GTK_BOX(tab->tab_box), tab->tab_label, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(tab->tab_box), close_btn, FALSE, FALSE, 0);
	gtk_widget_show_all(tab->tab_box);
	gtk_widget_show(tab->drawing_area);

	if (!spawn_shell_pty(tab)) {
		free_tab_resources(tab);
		free(tab);
		return NULL;
	}

	app->tabs = g_list_append(app->tabs, tab);
	int idx = gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), tab->drawing_area, tab->tab_box);
	gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(app->notebook), tab->drawing_area, TRUE);
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(app->notebook),
		gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook)) > 1);
	gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), idx);
	gtk_widget_grab_focus(tab->drawing_area);

	return tab;
}

static void on_new_tab_btn_clicked(GtkButton *btn, gpointer user_data) {
	(void)btn;
	TermApp *app = user_data;
	app_create_tab(app);
}

static void on_notebook_switch_page(GtkNotebook *notebook, GtkWidget *page, guint page_num, gpointer user_data) {
	(void)notebook;
	(void)page_num;
	TermApp *app = user_data;
	for (GList *l = app->tabs; l != NULL; l = l->next) {
		TermTab *tab = l->data;
		if (tab->drawing_area == page) {
			gtk_header_bar_set_subtitle(GTK_HEADER_BAR(app->header_bar), tab->title);
			gtk_window_set_title(GTK_WINDOW(app->window), tab->title);
			gtk_widget_grab_focus(tab->drawing_area);
			break;
		}
	}
}

static void on_window_destroy(GtkWidget *widget, gpointer user_data) {
	(void)widget;
	TermApp *app = user_data;
	if (!app) return;

	while (app->tabs) {
		TermTab *tab = app->tabs->data;
		app->tabs = g_list_delete_link(app->tabs, app->tabs);
		free_tab_resources(tab);
		free(tab);
	}
	if (app->font_desc) {
		pango_font_description_free(app->font_desc);
		app->font_desc = NULL;
	}
}

int main(int argc, char **argv) {
	g_set_prgname("diamonds-terminal");
	gtk_init(&argc, &argv);
	diamonds_gtk_apply_theme(TERMINAL_EXTRA_CSS);

	TermApp app;
	memset(&app, 0, sizeof(app));
	app.font_size = DEFAULT_FONT_SIZE;
	measure_font_metrics(&app);

	int init_w = DEFAULT_COLS * app.cell_w;
	int init_h = DEFAULT_ROWS * app.cell_h + 40;
	app.window = diamonds_gtk_window_new("Diamonds Terminal", init_w, init_h);
	g_signal_connect(app.window, "destroy", G_CALLBACK(on_window_destroy), &app);

	app.header_bar = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(app.header_bar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(app.header_bar), "Diamonds Terminal");
	gtk_header_bar_set_subtitle(GTK_HEADER_BAR(app.header_bar), "Terminal");

	GtkWidget *new_tab_btn = gtk_button_new_with_label("+");
	gtk_widget_set_tooltip_text(new_tab_btn, "New Tab (Ctrl+Shift+T)");
	gtk_widget_set_can_focus(new_tab_btn, FALSE);
	g_signal_connect(new_tab_btn, "clicked", G_CALLBACK(on_new_tab_btn_clicked), &app);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app.header_bar), new_tab_btn);

	gtk_window_set_titlebar(GTK_WINDOW(app.window), app.header_bar);

	app.notebook = gtk_notebook_new();
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(app.notebook), TRUE);
	gtk_notebook_set_show_border(GTK_NOTEBOOK(app.notebook), FALSE);
	gtk_widget_set_can_focus(app.notebook, FALSE);
	g_signal_connect(app.notebook, "switch-page", G_CALLBACK(on_notebook_switch_page), &app);
	gtk_container_add(GTK_CONTAINER(app.window), app.notebook);

	if (!app_create_tab(&app)) {
		fprintf(stderr, "diamonds-terminal: failed to create initial PTY tab\n");
		return 1;
	}

	gtk_widget_show_all(app.window);
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(app.notebook), FALSE);
	gtk_main();

	return 0;
}
