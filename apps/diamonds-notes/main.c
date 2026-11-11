/* Diamonds Notes - Lightweight GTK3 tabbed text editor for diamonds-desktop.
 * Pure C11 + GTK3 + POSIX.
 */
#include "diamonds_gtk.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_FILE_SIZE_BYTES (20 * 1024 * 1024)
#define MAX_UNDO_STEPS      200
#define COALESCE_TIME_US    600000 /* 600 ms */
#define MIN_FONT_PT         8
#define MAX_FONT_PT         32
#define DEFAULT_FONT_PT     12

typedef enum {
	UNDO_OP_INSERT = 0,
	UNDO_OP_DELETE = 1
} UndoOpType;

typedef struct {
	UndoOpType type;
	int start_offset;
	int end_offset;
	char *text;
	int64_t timestamp_us;
} UndoAction;

typedef struct {
	GtkWidget *scrolled;
	GtkWidget *text_view;
	GtkTextBuffer *buffer;
	GtkWidget *tab_box;
	GtkWidget *tab_label;
	GtkTextTag *match_tag;
	GtkTextTag *current_match_tag;

	char *filepath;       /* NULL if untitled */
	char *draft_path;     /* ~/.local/share/diamonds-notes/draft-N.txt for untitled */
	int untitled_id;
	bool modified;
	bool in_undo_redo;

	UndoAction undo_stack[MAX_UNDO_STEPS];
	size_t undo_len;
	UndoAction redo_stack[MAX_UNDO_STEPS];
	size_t redo_len;
} NoteTab;

typedef struct {
	GtkWidget *window;
	GtkWidget *notebook;

	/* Find & Replace bar */
	GtkWidget *find_revealer;
	GtkWidget *find_entry;
	GtkWidget *replace_row;
	GtkWidget *replace_entry;
	GtkWidget *case_check;
	GtkWidget *match_label;

	/* Header / status toggles */
	GtkWidget *mono_toggle;
	GtkWidget *wrap_toggle;
	GtkWidget *status_pos_label;
	GtkWidget *status_words_label;
	GtkWidget *status_zoom_label;

	GtkCssProvider *font_css_provider;
	bool use_monospace;
	bool use_word_wrap;
	int font_pt;
	int next_untitled_id;
	guint autosave_timer_id;
	char draft_dir[PATH_MAX];
} NotesApp;

static const char *NOTES_EXTRA_CSS =
	".notes-toolbar { background-color: #1a1b26; border-bottom: 1px solid #282a36; padding: 6px 10px; }"
	".find-bar { background-color: #1a1b26; border-bottom: 1px solid #282a36; padding: 8px 12px; }"
	".notes-textview { background-color: #12121a; color: #e2e8f0; padding: 10px; }"
	".notes-textview text { background-color: #12121a; color: #e2e8f0; selection-background-color: #0284c7; }"
	".tab-close-btn { padding: 0px 4px; min-width: 18px; min-height: 18px; border-radius: 4px; border: none; background: transparent; color: #94a3b8; }"
	".tab-close-btn:hover { background-color: rgba(255,255,255,0.1); color: #f8fafc; }"
	".status-bar { background-color: #1a1b26; border-top: 1px solid #282a36; padding: 4px 12px; color: #94a3b8; font-size: 12px; }";

/* Forward declarations */
static NoteTab *get_current_tab(NotesApp *app);
static NoteTab *get_tab_for_page(NotesApp *app, GtkWidget *page_widget);
static void update_tab_title(NoteTab *tab);
static void update_status_bar(NotesApp *app);
static void update_search_highlights(NotesApp *app);
static bool save_tab(NotesApp *app, NoteTab *tab, bool save_as);
static void close_tab(NotesApp *app, NoteTab *tab);

static void free_undo_action(UndoAction *act) {
	if (!act) return;
	if (act->text) {
		g_free(act->text);
		act->text = NULL;
	}
}

static void clear_action_stack(UndoAction *stack, size_t *len) {
	if (!stack || !len) return;
	for (size_t i = 0; i < *len; i++) {
		free_undo_action(&stack[i]);
	}
	*len = 0;
}

static void push_action(UndoAction *stack, size_t *len, UndoOpType type,
                        int start_off, int end_off, const char *text, int64_t ts) {
	if (*len >= MAX_UNDO_STEPS) {
		free_undo_action(&stack[0]);
		memmove(&stack[0], &stack[1], (MAX_UNDO_STEPS - 1) * sizeof(UndoAction));
		*len = MAX_UNDO_STEPS - 1;
	}
	UndoAction *slot = &stack[*len];
	slot->type = type;
	slot->start_offset = start_off;
	slot->end_offset = end_off;
	slot->text = g_strdup(text ? text : "");
	slot->timestamp_us = ts;
	(*len)++;
}

static void show_error_dialog(GtkWindow *parent, const char *title, const char *message) {
	GtkWidget *dlg = gtk_message_dialog_new(
		parent,
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_ERROR,
		GTK_BUTTONS_OK,
		"%s", title);
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), "%s", message);
	gtk_dialog_run(GTK_DIALOG(dlg));
	gtk_widget_destroy(dlg);
}

static void apply_font_css(NotesApp *app) {
	char css[256];
	const char *family = app->use_monospace
		? "Monospace, JetBrains Mono, Fira Code, DejaVu Sans Mono"
		: "Segoe UI, Cantarell, Ubuntu, sans-serif";
	snprintf(css, sizeof(css),
		".notes-textview, .notes-textview text { font-family: %s; font-size: %dpt; }",
		family, app->font_pt);
	gtk_css_provider_load_from_data(app->font_css_provider, css, -1, NULL);

	if (app->status_zoom_label) {
		char zbuf[32];
		int pct = (app->font_pt * 100) / DEFAULT_FONT_PT;
		snprintf(zbuf, sizeof(zbuf), "Zoom: %d%% (%dpt)", pct, app->font_pt);
		gtk_label_set_text(GTK_LABEL(app->status_zoom_label), zbuf);
	}
}

static void ensure_draft_dir(NotesApp *app) {
	const char *data_home = g_get_user_data_dir();
	if (!data_home || !*data_home) {
		const char *home = g_get_home_dir();
		snprintf(app->draft_dir, sizeof(app->draft_dir), "%s/.local/share/diamonds-notes",
			home ? home : "/tmp");
	} else {
		snprintf(app->draft_dir, sizeof(app->draft_dir), "%s/diamonds-notes", data_home);
	}
	g_mkdir_with_parents(app->draft_dir, 0700);
}

static void write_draft_for_tab(NotesApp *app, NoteTab *tab) {
	(void)app;
	if (!tab || tab->filepath != NULL || !tab->draft_path) return;

	GtkTextIter start, end;
	gtk_text_buffer_get_bounds(tab->buffer, &start, &end);
	char *text = gtk_text_buffer_get_text(tab->buffer, &start, &end, FALSE);
	if (!text) return;

	if (text[0] == '\0' && !tab->modified) {
		unlink(tab->draft_path);
		g_free(text);
		return;
	}

	int fd = open(tab->draft_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd >= 0) {
		size_t total = strlen(text);
		size_t written = 0;
		while (written < total) {
			ssize_t n = write(fd, text + written, total - written);
			if (n <= 0) break;
			written += (size_t)n;
		}
		close(fd);
	}
	g_free(text);
}

static void remove_draft_for_tab(NoteTab *tab) {
	if (!tab || !tab->draft_path) return;
	unlink(tab->draft_path);
}

static gboolean on_autosave_tick(gpointer user_data) {
	NotesApp *app = user_data;
	int n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
	for (int i = 0; i < n_pages; i++) {
		GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
		NoteTab *tab = get_tab_for_page(app, page);
		if (tab && tab->filepath == NULL && tab->modified) {
			write_draft_for_tab(app, tab);
		}
	}
	return G_SOURCE_CONTINUE;
}

static NoteTab *get_tab_for_page(NotesApp *app, GtkWidget *page_widget) {
	(void)app;
	if (!page_widget) return NULL;
	return g_object_get_data(G_OBJECT(page_widget), "note-tab");
}

static NoteTab *get_current_tab(NotesApp *app) {
	int idx = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
	if (idx < 0) return NULL;
	GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), idx);
	return get_tab_for_page(app, page);
}

static void update_tab_title(NoteTab *tab) {
	if (!tab || !tab->tab_label) return;
	char title[256];
	const char *prefix = tab->modified ? "• " : "";
	if (tab->filepath) {
		char *base = g_path_get_basename(tab->filepath);
		snprintf(title, sizeof(title), "%s%s", prefix, base ? base : "Untitled");
		g_free(base);
	} else {
		snprintf(title, sizeof(title), "%sUntitled %d", prefix, tab->untitled_id);
	}
	gtk_label_set_text(GTK_LABEL(tab->tab_label), title);
}

static size_t count_words(const char *s) {
	size_t words = 0;
	bool in_word = false;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		if (isspace(*p)) {
			in_word = false;
		} else if (!in_word) {
			in_word = true;
			words++;
		}
	}
	return words;
}

static void update_status_bar(NotesApp *app) {
	NoteTab *tab = get_current_tab(app);
	if (!tab) {
		gtk_label_set_text(GTK_LABEL(app->status_pos_label), "Ln 1, Col 1");
		gtk_label_set_text(GTK_LABEL(app->status_words_label), "0 words, 0 chars");
		return;
	}

	GtkTextIter iter;
	GtkTextMark *insert_mark = gtk_text_buffer_get_insert(tab->buffer);
	gtk_text_buffer_get_iter_at_mark(tab->buffer, &iter, insert_mark);
	int line = gtk_text_iter_get_line(&iter) + 1;
	int col = gtk_text_iter_get_line_offset(&iter) + 1;

	char pos_buf[64];
	snprintf(pos_buf, sizeof(pos_buf), "Ln %d, Col %d", line, col);
	gtk_label_set_text(GTK_LABEL(app->status_pos_label), pos_buf);

	GtkTextIter start, end;
	gtk_text_buffer_get_bounds(tab->buffer, &start, &end);
	char *text = gtk_text_buffer_get_text(tab->buffer, &start, &end, FALSE);
	int char_count = gtk_text_buffer_get_char_count(tab->buffer);
	size_t words = text ? count_words(text) : 0;
	g_free(text);

	char wbuf[96];
	snprintf(wbuf, sizeof(wbuf), "%zu words, %d chars", words, char_count);
	gtk_label_set_text(GTK_LABEL(app->status_words_label), wbuf);
}

/* Search & Replace helpers */
static void update_search_highlights(NotesApp *app) {
	NoteTab *tab = get_current_tab(app);
	if (!tab) return;

	GtkTextIter start, end;
	gtk_text_buffer_get_bounds(tab->buffer, &start, &end);
	gtk_text_buffer_remove_tag(tab->buffer, tab->match_tag, &start, &end);
	gtk_text_buffer_remove_tag(tab->buffer, tab->current_match_tag, &start, &end);

	if (!gtk_revealer_get_reveal_child(GTK_REVEALER(app->find_revealer))) {
		gtk_label_set_text(GTK_LABEL(app->match_label), "");
		return;
	}

	const char *needle = gtk_entry_get_text(GTK_ENTRY(app->find_entry));
	if (!needle || *needle == '\0') {
		gtk_label_set_text(GTK_LABEL(app->match_label), "0 matches");
		return;
	}

	GtkTextSearchFlags flags = GTK_TEXT_SEARCH_TEXT_ONLY;
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->case_check))) {
		flags |= GTK_TEXT_SEARCH_CASE_INSENSITIVE;
	}

	int count = 0;
	GtkTextIter iter = start;
	GtkTextIter mstart, mend;
	while (gtk_text_iter_forward_search(&iter, needle, flags, &mstart, &mend, NULL)) {
		gtk_text_buffer_apply_tag(tab->buffer, tab->match_tag, &mstart, &mend);
		count++;
		iter = mend;
	}

	char mbuf[64];
	snprintf(mbuf, sizeof(mbuf), "%d match%s", count, count == 1 ? "" : "es");
	gtk_label_set_text(GTK_LABEL(app->match_label), mbuf);
}

static void find_step(NotesApp *app, bool forward) {
	NoteTab *tab = get_current_tab(app);
	if (!tab) return;
	const char *needle = gtk_entry_get_text(GTK_ENTRY(app->find_entry));
	if (!needle || *needle == '\0') return;

	GtkTextSearchFlags flags = GTK_TEXT_SEARCH_TEXT_ONLY;
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->case_check))) {
		flags |= GTK_TEXT_SEARCH_CASE_INSENSITIVE;
	}

	GtkTextIter sel_start, sel_end;
	gtk_text_buffer_get_selection_bounds(tab->buffer, &sel_start, &sel_end);

	GtkTextIter mstart, mend;
	gboolean found = FALSE;
	if (forward) {
		found = gtk_text_iter_forward_search(&sel_end, needle, flags, &mstart, &mend, NULL);
		if (!found) {
			GtkTextIter top;
			gtk_text_buffer_get_start_iter(tab->buffer, &top);
			found = gtk_text_iter_forward_search(&top, needle, flags, &mstart, &mend, NULL);
		}
	} else {
		found = gtk_text_iter_backward_search(&sel_start, needle, flags, &mstart, &mend, NULL);
		if (!found) {
			GtkTextIter bottom;
			gtk_text_buffer_get_end_iter(tab->buffer, &bottom);
			found = gtk_text_iter_backward_search(&bottom, needle, flags, &mstart, &mend, NULL);
		}
	}

	if (found) {
		gtk_text_buffer_select_range(tab->buffer, &mstart, &mend);
		gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(tab->text_view), &mstart, 0.15, FALSE, 0.0, 0.0);
	}
}

static void replace_current_match(NotesApp *app) {
	NoteTab *tab = get_current_tab(app);
	if (!tab) return;
	const char *needle = gtk_entry_get_text(GTK_ENTRY(app->find_entry));
	const char *replacement = gtk_entry_get_text(GTK_ENTRY(app->replace_entry));
	if (!needle || *needle == '\0') return;
	if (!replacement) replacement = "";

	GtkTextIter sel_start, sel_end;
	if (gtk_text_buffer_get_selection_bounds(tab->buffer, &sel_start, &sel_end)) {
		char *sel_text = gtk_text_buffer_get_text(tab->buffer, &sel_start, &sel_end, FALSE);
		bool matches = false;
		if (sel_text) {
			if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->case_check))) {
				char *a = g_utf8_casefold(sel_text, -1);
				char *b = g_utf8_casefold(needle, -1);
				matches = (a && b && strcmp(a, b) == 0);
				g_free(a);
				g_free(b);
			} else {
				matches = (strcmp(sel_text, needle) == 0);
			}
			g_free(sel_text);
		}
		if (matches) {
			gtk_text_buffer_delete(tab->buffer, &sel_start, &sel_end);
			gtk_text_buffer_insert(tab->buffer, &sel_start, replacement, -1);
			update_search_highlights(app);
		}
	}
	find_step(app, true);
}

static void replace_all_matches(NotesApp *app) {
	NoteTab *tab = get_current_tab(app);
	if (!tab) return;
	const char *needle = gtk_entry_get_text(GTK_ENTRY(app->find_entry));
	const char *replacement = gtk_entry_get_text(GTK_ENTRY(app->replace_entry));
	if (!needle || *needle == '\0') return;
	if (!replacement) replacement = "";

	GtkTextSearchFlags flags = GTK_TEXT_SEARCH_TEXT_ONLY;
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->case_check))) {
		flags |= GTK_TEXT_SEARCH_CASE_INSENSITIVE;
	}

	GtkTextIter iter, mstart, mend;
	gtk_text_buffer_get_start_iter(tab->buffer, &iter);
	int rlen = g_utf8_strlen(replacement, -1);

	while (gtk_text_iter_forward_search(&iter, needle, flags, &mstart, &mend, NULL)) {
		int start_off = gtk_text_iter_get_offset(&mstart);
		gtk_text_buffer_delete(tab->buffer, &mstart, &mend);
		gtk_text_buffer_get_iter_at_offset(tab->buffer, &mstart, start_off);
		gtk_text_buffer_insert(tab->buffer, &mstart, replacement, -1);
		gtk_text_buffer_get_iter_at_offset(tab->buffer, &iter, start_off + rlen);
	}
	update_search_highlights(app);
}

/* Undo / Redo implementation */
static void perform_undo(NotesApp *app) {
	NoteTab *tab = get_current_tab(app);
	if (!tab || tab->undo_len == 0) return;

	UndoAction act = tab->undo_stack[tab->undo_len - 1];
	tab->undo_len--;

	tab->in_undo_redo = true;
	GtkTextIter start, end;
	if (act.type == UNDO_OP_INSERT) {
		gtk_text_buffer_get_iter_at_offset(tab->buffer, &start, act.start_offset);
		gtk_text_buffer_get_iter_at_offset(tab->buffer, &end, act.end_offset);
		gtk_text_buffer_delete(tab->buffer, &start, &end);
		gtk_text_buffer_get_iter_at_offset(tab->buffer, &start, act.start_offset);
		gtk_text_buffer_place_cursor(tab->buffer, &start);
	} else {
		gtk_text_buffer_get_iter_at_offset(tab->buffer, &start, act.start_offset);
		gtk_text_buffer_insert(tab->buffer, &start, act.text, -1);
		gtk_text_buffer_get_iter_at_offset(tab->buffer, &end, act.end_offset);
		gtk_text_buffer_place_cursor(tab->buffer, &end);
	}
	tab->in_undo_redo = false;

	push_action(tab->redo_stack, &tab->redo_len, act.type,
	            act.start_offset, act.end_offset, act.text, act.timestamp_us);
	free_undo_action(&act);

	tab->modified = true;
	update_tab_title(tab);
	update_status_bar(app);
	update_search_highlights(app);
}

static void perform_redo(NotesApp *app) {
	NoteTab *tab = get_current_tab(app);
	if (!tab || tab->redo_len == 0) return;

	UndoAction act = tab->redo_stack[tab->redo_len - 1];
	tab->redo_len--;

	tab->in_undo_redo = true;
	GtkTextIter start, end;
	if (act.type == UNDO_OP_INSERT) {
		gtk_text_buffer_get_iter_at_offset(tab->buffer, &start, act.start_offset);
		gtk_text_buffer_insert(tab->buffer, &start, act.text, -1);
		gtk_text_buffer_get_iter_at_offset(tab->buffer, &end, act.end_offset);
		gtk_text_buffer_place_cursor(tab->buffer, &end);
	} else {
		gtk_text_buffer_get_iter_at_offset(tab->buffer, &start, act.start_offset);
		gtk_text_buffer_get_iter_at_offset(tab->buffer, &end, act.end_offset);
		gtk_text_buffer_delete(tab->buffer, &start, &end);
		gtk_text_buffer_get_iter_at_offset(tab->buffer, &start, act.start_offset);
		gtk_text_buffer_place_cursor(tab->buffer, &start);
	}
	tab->in_undo_redo = false;

	push_action(tab->undo_stack, &tab->undo_len, act.type,
	            act.start_offset, act.end_offset, act.text, act.timestamp_us);
	free_undo_action(&act);

	tab->modified = true;
	update_tab_title(tab);
	update_status_bar(app);
	update_search_highlights(app);
}

static void on_buffer_insert_text(GtkTextBuffer *buffer, GtkTextIter *location,
                                  gchar *text, gint len, gpointer user_data) {
	(void)buffer;
	NoteTab *tab = user_data;
	if (!tab || tab->in_undo_redo || len <= 0) return;

	clear_action_stack(tab->redo_stack, &tab->redo_len);

	int start_off = gtk_text_iter_get_offset(location);
	char *slice = g_strndup(text, (gsize)len);
	int char_len = (int)g_utf8_strlen(slice, -1);
	int end_off = start_off + char_len;
	int64_t now = g_get_monotonic_time();

	/* Coalesce single-character non-newline typing within COALESCE_TIME_US */
	if (tab->undo_len > 0 && char_len == 1 && slice[0] != '\n' && slice[0] != '\r') {
		UndoAction *last = &tab->undo_stack[tab->undo_len - 1];
		if (last->type == UNDO_OP_INSERT &&
		    last->end_offset == start_off &&
		    (now - last->timestamp_us) <= COALESCE_TIME_US) {
			size_t old_bytes = strlen(last->text);
			size_t add_bytes = strlen(slice);
			char *merged = g_malloc(old_bytes + add_bytes + 1);
			memcpy(merged, last->text, old_bytes);
			memcpy(merged + old_bytes, slice, add_bytes + 1);
			g_free(last->text);
			last->text = merged;
			last->end_offset = end_off;
			last->timestamp_us = now;
			g_free(slice);
			return;
		}
	}

	push_action(tab->undo_stack, &tab->undo_len, UNDO_OP_INSERT, start_off, end_off, slice, now);
	g_free(slice);
}

static void on_buffer_delete_range(GtkTextBuffer *buffer, GtkTextIter *start,
                                   GtkTextIter *end, gpointer user_data) {
	NoteTab *tab = user_data;
	if (!tab || tab->in_undo_redo) return;

	int start_off = gtk_text_iter_get_offset(start);
	int end_off = gtk_text_iter_get_offset(end);
	if (end_off <= start_off) return;

	clear_action_stack(tab->redo_stack, &tab->redo_len);

	char *deleted = gtk_text_buffer_get_text(buffer, start, end, FALSE);
	int char_len = end_off - start_off;
	int64_t now = g_get_monotonic_time();

	/* Coalesce consecutive backspace deletions */
	if (tab->undo_len > 0 && char_len == 1 && deleted && deleted[0] != '\n') {
		UndoAction *last = &tab->undo_stack[tab->undo_len - 1];
		if (last->type == UNDO_OP_DELETE &&
		    last->start_offset == end_off &&
		    (now - last->timestamp_us) <= COALESCE_TIME_US) {
			size_t del_bytes = strlen(deleted);
			size_t old_bytes = strlen(last->text);
			char *merged = g_malloc(del_bytes + old_bytes + 1);
			memcpy(merged, deleted, del_bytes);
			memcpy(merged + del_bytes, last->text, old_bytes + 1);
			g_free(last->text);
			last->text = merged;
			last->start_offset = start_off;
			last->timestamp_us = now;
			g_free(deleted);
			return;
		}
	}

	push_action(tab->undo_stack, &tab->undo_len, UNDO_OP_DELETE, start_off, end_off, deleted, now);
	g_free(deleted);
}

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data) {
	(void)buffer;
	NotesApp *app = user_data;
	NoteTab *tab = get_current_tab(app);
	if (!tab) return;
	if (!tab->modified) {
		tab->modified = true;
		update_tab_title(tab);
	}
	if (tab->filepath == NULL) {
		write_draft_for_tab(app, tab);
	}
	update_status_bar(app);
	update_search_highlights(app);
}

static void on_mark_set(GtkTextBuffer *buffer, GtkTextIter *location,
                        GtkTextMark *mark, gpointer user_data) {
	(void)location;
	NotesApp *app = user_data;
	if (mark == gtk_text_buffer_get_insert(buffer)) {
		update_status_bar(app);
	}
}

static void on_tab_close_clicked(GtkButton *btn, gpointer user_data) {
	NotesApp *app = g_object_get_data(G_OBJECT(btn), "notes-app");
	NoteTab *tab = user_data;
	if (app && tab) {
		close_tab(app, tab);
	}
}

static NoteTab *create_new_tab(NotesApp *app, const char *filepath, const char *initial_text,
                               int forced_untitled_id, bool mark_modified) {
	NoteTab *tab = g_malloc0(sizeof(NoteTab));
	tab->untitled_id = (forced_untitled_id > 0) ? forced_untitled_id : app->next_untitled_id++;
	if (forced_untitled_id >= app->next_untitled_id) {
		app->next_untitled_id = forced_untitled_id + 1;
	}

	if (filepath) {
		tab->filepath = g_strdup(filepath);
		tab->draft_path = NULL;
	} else {
		tab->filepath = NULL;
		tab->draft_path = g_strdup_printf("%s/draft-%d.txt", app->draft_dir, tab->untitled_id);
	}

	tab->scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tab->scrolled),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	tab->text_view = gtk_text_view_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(tab->text_view), "notes-textview");
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tab->text_view),
		app->use_word_wrap ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tab->text_view), 12);
	gtk_text_view_set_right_margin(GTK_TEXT_VIEW(tab->text_view), 12);
	gtk_text_view_set_top_margin(GTK_TEXT_VIEW(tab->text_view), 10);
	gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(tab->text_view), 10);

	tab->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tab->text_view));
	tab->match_tag = gtk_text_buffer_create_tag(tab->buffer, "search_match",
		"background", "#0369a1", "foreground", "#ffffff", NULL);
	tab->current_match_tag = gtk_text_buffer_create_tag(tab->buffer, "current_match",
		"background", "#38bdf8", "foreground", "#0f172a", NULL);

	if (initial_text && *initial_text) {
		tab->in_undo_redo = true;
		gtk_text_buffer_set_text(tab->buffer, initial_text, -1);
		GtkTextIter start;
		gtk_text_buffer_get_start_iter(tab->buffer, &start);
		gtk_text_buffer_place_cursor(tab->buffer, &start);
		tab->in_undo_redo = false;
	}

	tab->modified = mark_modified;

	g_signal_connect(tab->buffer, "insert-text", G_CALLBACK(on_buffer_insert_text), tab);
	g_signal_connect(tab->buffer, "delete-range", G_CALLBACK(on_buffer_delete_range), tab);
	g_signal_connect(tab->buffer, "changed", G_CALLBACK(on_buffer_changed), app);
	g_signal_connect(tab->buffer, "mark-set", G_CALLBACK(on_mark_set), app);

	gtk_container_add(GTK_CONTAINER(tab->scrolled), tab->text_view);
	g_object_set_data(G_OBJECT(tab->scrolled), "note-tab", tab);

	/* Tab header box */
	tab->tab_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	tab->tab_label = gtk_label_new("");
	update_tab_title(tab);
	gtk_box_pack_start(GTK_BOX(tab->tab_box), tab->tab_label, FALSE, FALSE, 0);

	GtkWidget *close_btn = gtk_button_new_with_label("✕");
	gtk_style_context_add_class(gtk_widget_get_style_context(close_btn), "tab-close-btn");
	g_object_set_data(G_OBJECT(close_btn), "notes-app", app);
	g_signal_connect(close_btn, "clicked", G_CALLBACK(on_tab_close_clicked), tab);
	gtk_box_pack_start(GTK_BOX(tab->tab_box), close_btn, FALSE, FALSE, 0);
	gtk_widget_show_all(tab->tab_box);
	gtk_widget_show_all(tab->scrolled);

	int page_idx = gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), tab->scrolled, tab->tab_box);
	gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(app->notebook), tab->scrolled, TRUE);
	gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), page_idx);
	gtk_widget_grab_focus(tab->text_view);

	update_status_bar(app);
	return tab;
}

static bool load_And_validate_file(GtkWindow *parent, const char *path, char **out_utf8) {
	*out_utf8 = NULL;
	struct stat st;
	if (stat(path, &st) != 0) {
		char msg[512];
		snprintf(msg, sizeof(msg), "Could not stat '%s': %s", path, strerror(errno));
		show_error_dialog(parent, "Failed to Open File", msg);
		return false;
	}
	if (S_ISDIR(st.st_mode)) {
		show_error_dialog(parent, "Cannot Open Directory", "The selected path is a directory.");
		return false;
	}
	if (st.st_size > MAX_FILE_SIZE_BYTES) {
		char msg[256];
		snprintf(msg, sizeof(msg),
			"File '%s' is %.1f MB, which exceeds the 20 MB maximum file size limit.",
			path, (double)st.st_size / (1024.0 * 1024.0));
		show_error_dialog(parent, "File Too Large", msg);
		return false;
	}

	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		char msg[512];
		snprintf(msg, sizeof(msg), "Cannot open '%s': %s", path, strerror(errno));
		show_error_dialog(parent, "Failed to Open File", msg);
		return false;
	}

	size_t alloc_sz = (size_t)st.st_size;
	char *raw = g_malloc(alloc_sz + 1);
	size_t total_read = 0;
	while (total_read < alloc_sz) {
		ssize_t n = read(fd, raw + total_read, alloc_sz - total_read);
		if (n < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (n == 0) break;
		total_read += (size_t)n;
	}
	close(fd);
	raw[total_read] = '\0';

	/* Check for NUL bytes (binary file detection) */
	if (memchr(raw, '\0', total_read) != NULL) {
		g_free(raw);
		show_error_dialog(parent, "Binary File Refused",
			"The file appears to be a binary file (contains NUL bytes) and cannot be opened in Diamonds Notes.");
		return false;
	}

	/* Validate UTF-8 or convert from locale / ISO-8859-1 */
	if (g_utf8_validate(raw, (gssize)total_read, NULL)) {
		*out_utf8 = raw;
		return true;
	}

	gsize bytes_written = 0;
	GError *err = NULL;
	char *converted = g_locale_to_utf8(raw, (gssize)total_read, NULL, &bytes_written, &err);
	if (err) {
		g_error_free(err);
		err = NULL;
	}
	if (!converted || !g_utf8_validate(converted, (gssize)bytes_written, NULL)) {
		g_free(converted);
		converted = g_convert(raw, (gssize)total_read, "UTF-8", "ISO-8859-1", NULL, &bytes_written, &err);
		if (err) {
			g_error_free(err);
			err = NULL;
		}
	}
	g_free(raw);

	if (!converted || !g_utf8_validate(converted, (gssize)bytes_written, NULL)) {
		g_free(converted);
		show_error_dialog(parent, "Invalid Encoding",
			"Could not decode file contents as UTF-8 or system locale encoding.");
		return false;
	}

	*out_utf8 = converted;
	return true;
}

static void open_file_in_editor(NotesApp *app, const char *path) {
	/* If already open in a tab, focus that tab */
	int n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
	for (int i = 0; i < n_pages; i++) {
		GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
		NoteTab *t = get_tab_for_page(app, page);
		if (t && t->filepath && strcmp(t->filepath, path) == 0) {
			gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), i);
			return;
		}
	}

	char *utf8 = NULL;
	if (!load_And_validate_file(GTK_WINDOW(app->window), path, &utf8)) {
		return;
	}

	/* Reuse single empty unmodified untitled tab if present */
	NoteTab *cur = get_current_tab(app);
	if (n_pages == 1 && cur && cur->filepath == NULL && !cur->modified &&
	    gtk_text_buffer_get_char_count(cur->buffer) == 0) {
		remove_draft_for_tab(cur);
		g_free(cur->draft_path);
		cur->draft_path = NULL;
		cur->filepath = g_strdup(path);
		cur->in_undo_redo = true;
		gtk_text_buffer_set_text(cur->buffer, utf8, -1);
		GtkTextIter start;
		gtk_text_buffer_get_start_iter(cur->buffer, &start);
		gtk_text_buffer_place_cursor(cur->buffer, &start);
		cur->in_undo_redo = false;
		cur->modified = false;
		clear_action_stack(cur->undo_stack, &cur->undo_len);
		clear_action_stack(cur->redo_stack, &cur->redo_len);
		update_tab_title(cur);
		update_status_bar(app);
		g_free(utf8);
		return;
	}

	create_new_tab(app, path, utf8, 0, false);
	g_free(utf8);
}

static bool write_file_atomic(GtkWindow *parent, const char *path, const char *content) {
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		char msg[512];
		snprintf(msg, sizeof(msg), "Could not write to '%s': %s", path, strerror(errno));
		show_error_dialog(parent, "Save Failed", msg);
		return false;
	}
	size_t total = strlen(content);
	size_t written = 0;
	while (written < total) {
		ssize_t n = write(fd, content + written, total - written);
		if (n < 0) {
			if (errno == EINTR) continue;
			close(fd);
			char msg[512];
			snprintf(msg, sizeof(msg), "Error writing '%s': %s", path, strerror(errno));
			show_error_dialog(parent, "Save Failed", msg);
			return false;
		}
		written += (size_t)n;
	}
	close(fd);
	return true;
}

static bool save_tab(NotesApp *app, NoteTab *tab, bool save_as) {
	if (!tab) return false;

	char *chosen_path = NULL;
	if (save_as || !tab->filepath) {
		GtkWidget *dlg = gtk_file_chooser_dialog_new(
			save_as ? "Save File As" : "Save File",
			GTK_WINDOW(app->window),
			GTK_FILE_CHOOSER_ACTION_SAVE,
			"_Cancel", GTK_RESPONSE_CANCEL,
			"_Save", GTK_RESPONSE_ACCEPT,
			NULL);
		gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
		if (tab->filepath) {
			gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dlg), tab->filepath);
		} else {
			char defname[64];
			snprintf(defname, sizeof(defname), "Untitled-%d.txt", tab->untitled_id);
			gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), defname);
		}
		if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
			chosen_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
		}
		gtk_widget_destroy(dlg);
		if (!chosen_path) return false;
	} else {
		chosen_path = g_strdup(tab->filepath);
	}

	GtkTextIter start, end;
	gtk_text_buffer_get_bounds(tab->buffer, &start, &end);
	char *text = gtk_text_buffer_get_text(tab->buffer, &start, &end, FALSE);
	bool ok = write_file_atomic(GTK_WINDOW(app->window), chosen_path, text ? text : "");
	g_free(text);

	if (ok) {
		if (tab->draft_path) {
			remove_draft_for_tab(tab);
			g_free(tab->draft_path);
			tab->draft_path = NULL;
		}
		g_free(tab->filepath);
		tab->filepath = chosen_path;
		tab->modified = false;
		update_tab_title(tab);
	} else {
		g_free(chosen_path);
	}
	return ok;
}

static void destroy_tab_struct(NoteTab *tab) {
	if (!tab) return;
	clear_action_stack(tab->undo_stack, &tab->undo_len);
	clear_action_stack(tab->redo_stack, &tab->redo_len);
	if (tab->filepath) {
		g_free(tab->filepath);
		tab->filepath = NULL;
	}
	if (tab->draft_path) {
		g_free(tab->draft_path);
		tab->draft_path = NULL;
	}
	g_free(tab);
}

/* Returns true if the tab was closed (or safe to close), false if user cancelled */
static bool confirm_and_dispose_tab(NotesApp *app, NoteTab *tab) {
	if (!tab) return true;
	if (tab->modified) {
		char name[256];
		if (tab->filepath) {
			char *base = g_path_get_basename(tab->filepath);
			snprintf(name, sizeof(name), "%s", base ? base : "Untitled");
			g_free(base);
		} else {
			snprintf(name, sizeof(name), "Untitled %d", tab->untitled_id);
		}

		GtkWidget *dlg = gtk_message_dialog_new(
			GTK_WINDOW(app->window),
			GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_WARNING,
			GTK_BUTTONS_NONE,
			"Save changes to \"%s\" before closing?", name);
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg),
			"Your changes will be lost if you choose Discard.");
		gtk_dialog_add_buttons(GTK_DIALOG(dlg),
			"_Discard", GTK_RESPONSE_NO,
			"_Cancel", GTK_RESPONSE_CANCEL,
			"_Save", GTK_RESPONSE_YES,
			NULL);
		gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
		gtk_widget_destroy(dlg);

		if (resp == GTK_RESPONSE_CANCEL || resp == GTK_RESPONSE_DELETE_EVENT) {
			return false;
		}
		if (resp == GTK_RESPONSE_YES) {
			if (!save_tab(app, tab, false)) {
				return false;
			}
		} else if (resp == GTK_RESPONSE_NO) {
			remove_draft_for_tab(tab);
		}
	} else {
		remove_draft_for_tab(tab);
	}
	return true;
}

static void close_tab(NotesApp *app, NoteTab *tab) {
	if (!confirm_and_dispose_tab(app, tab)) return;

	int page_num = gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), tab->scrolled);
	if (page_num >= 0) {
		gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), page_num);
	}
	destroy_tab_struct(tab);

	if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook)) == 0) {
		create_new_tab(app, NULL, "", 0, false);
	}
	update_status_bar(app);
	update_search_highlights(app);
}

static void restore_drafts(NotesApp *app) {
	DIR *dir = opendir(app->draft_dir);
	if (!dir) return;

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		int id = 0;
		if (sscanf(ent->d_name, "draft-%d.txt", &id) == 1 && id > 0) {
			char *full = g_build_filename(app->draft_dir, ent->d_name, NULL);
			char *utf8 = NULL;
			if (load_And_validate_file(NULL, full, &utf8)) {
				if (utf8 && *utf8 != '\0') {
					create_new_tab(app, NULL, utf8, id, true);
				} else {
					unlink(full);
				}
				g_free(utf8);
			}
			g_free(full);
		}
	}
	closedir(dir);
}

/* UI Callbacks */
static void on_new_clicked(GtkWidget *w, gpointer user_data) {
	(void)w;
	NotesApp *app = user_data;
	create_new_tab(app, NULL, "", 0, false);
}

static void on_open_clicked(GtkWidget *w, gpointer user_data) {
	(void)w;
	NotesApp *app = user_data;
	GtkWidget *dlg = gtk_file_chooser_dialog_new(
		"Open File",
		GTK_WINDOW(app->window),
		GTK_FILE_CHOOSER_ACTION_OPEN,
		"_Cancel", GTK_RESPONSE_CANCEL,
		"_Open", GTK_RESPONSE_ACCEPT,
		NULL);
	gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dlg), TRUE);
	if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
		GSList *files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dlg));
		for (GSList *it = files; it != NULL; it = it->next) {
			char *fname = it->data;
			if (fname) {
				open_file_in_editor(app, fname);
				g_free(fname);
			}
		}
		g_slist_free(files);
	}
	gtk_widget_destroy(dlg);
}

static void on_save_clicked(GtkWidget *w, gpointer user_data) {
	(void)w;
	NotesApp *app = user_data;
	NoteTab *tab = get_current_tab(app);
	if (tab) save_tab(app, tab, false);
}

static void on_save_as_clicked(GtkWidget *w, gpointer user_data) {
	(void)w;
	NotesApp *app = user_data;
	NoteTab *tab = get_current_tab(app);
	if (tab) save_tab(app, tab, true);
}

static void show_find_bar(NotesApp *app, bool with_replace) {
	gtk_revealer_set_reveal_child(GTK_REVEALER(app->find_revealer), TRUE);
	gtk_widget_set_visible(app->replace_row, with_replace);
	gtk_widget_grab_focus(app->find_entry);
	update_search_highlights(app);
}

static void on_find_btn_clicked(GtkWidget *w, gpointer user_data) {
	(void)w;
	NotesApp *app = user_data;
	if (gtk_revealer_get_reveal_child(GTK_REVEALER(app->find_revealer))) {
		gtk_revealer_set_reveal_child(GTK_REVEALER(app->find_revealer), FALSE);
		update_search_highlights(app);
	} else {
		show_find_bar(app, true);
	}
}

static void on_find_close_clicked(GtkWidget *w, gpointer user_data) {
	(void)w;
	NotesApp *app = user_data;
	gtk_revealer_set_reveal_child(GTK_REVEALER(app->find_revealer), FALSE);
	update_search_highlights(app);
	NoteTab *tab = get_current_tab(app);
	if (tab) gtk_widget_grab_focus(tab->text_view);
}

static void on_find_entry_changed(GtkEditable *e, gpointer user_data) {
	(void)e;
	NotesApp *app = user_data;
	update_search_highlights(app);
}

static void on_find_next_clicked(GtkWidget *w, gpointer user_data) {
	(void)w;
	NotesApp *app = user_data;
	find_step(app, true);
}

static void on_find_prev_clicked(GtkWidget *w, gpointer user_data) {
	(void)w;
	NotesApp *app = user_data;
	find_step(app, false);
}

static void on_replace_clicked(GtkWidget *w, gpointer user_data) {
	(void)w;
	NotesApp *app = user_data;
	replace_current_match(app);
}

static void on_replace_all_clicked(GtkWidget *w, gpointer user_data) {
	(void)w;
	NotesApp *app = user_data;
	replace_all_matches(app);
}

static void on_mono_toggled(GtkToggleButton *btn, gpointer user_data) {
	NotesApp *app = user_data;
	app->use_monospace = gtk_toggle_button_get_active(btn);
	apply_font_css(app);
}

static void on_wrap_toggled(GtkToggleButton *btn, gpointer user_data) {
	NotesApp *app = user_data;
	app->use_word_wrap = gtk_toggle_button_get_active(btn);
	int n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
	for (int i = 0; i < n_pages; i++) {
		GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
		NoteTab *tab = get_tab_for_page(app, page);
		if (tab) {
			gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tab->text_view),
				app->use_word_wrap ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
		}
	}
}

static void on_switch_page(GtkNotebook *nb, GtkWidget *page, guint page_num, gpointer user_data) {
	(void)nb;
	(void)page;
	(void)page_num;
	NotesApp *app = user_data;
	update_status_bar(app);
	update_search_highlights(app);
}

static void adjust_zoom(NotesApp *app, int delta) {
	if (delta == 0) {
		app->font_pt = DEFAULT_FONT_PT;
	} else {
		int next = app->font_pt + delta;
		if (next < MIN_FONT_PT) next = MIN_FONT_PT;
		if (next > MAX_FONT_PT) next = MAX_FONT_PT;
		app->font_pt = next;
	}
	apply_font_css(app);
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *ev, gpointer user_data) {
	(void)widget;
	NotesApp *app = user_data;
	GdkModifierType mods = ev->state & gtk_accelerator_get_default_mod_mask();
	guint key = gdk_keyval_to_lower(ev->keyval);

	if (ev->keyval == GDK_KEY_Escape &&
	    gtk_revealer_get_reveal_child(GTK_REVEALER(app->find_revealer))) {
		on_find_close_clicked(NULL, app);
		return TRUE;
	}

	if ((mods & GDK_CONTROL_MASK) && !(mods & GDK_MOD1_MASK)) {
		bool shift = (mods & GDK_SHIFT_MASK) != 0;
		if (!shift && key == GDK_KEY_n) {
			create_new_tab(app, NULL, "", 0, false);
			return TRUE;
		}
		if (!shift && key == GDK_KEY_o) {
			on_open_clicked(NULL, app);
			return TRUE;
		}
		if (!shift && key == GDK_KEY_s) {
			on_save_clicked(NULL, app);
			return TRUE;
		}
		if (shift && key == GDK_KEY_s) {
			on_save_as_clicked(NULL, app);
			return TRUE;
		}
		if (!shift && key == GDK_KEY_w) {
			NoteTab *tab = get_current_tab(app);
			if (tab) close_tab(app, tab);
			return TRUE;
		}
		if (!shift && key == GDK_KEY_f) {
			show_find_bar(app, false);
			return TRUE;
		}
		if (!shift && key == GDK_KEY_h) {
			show_find_bar(app, true);
			return TRUE;
		}
		if (!shift && key == GDK_KEY_z) {
			perform_undo(app);
			return TRUE;
		}
		if ((!shift && key == GDK_KEY_y) || (shift && key == GDK_KEY_z)) {
			perform_redo(app);
			return TRUE;
		}
		if (ev->keyval == GDK_KEY_plus || ev->keyval == GDK_KEY_equal ||
		    ev->keyval == GDK_KEY_KP_Add) {
			adjust_zoom(app, 1);
			return TRUE;
		}
		if (ev->keyval == GDK_KEY_minus || ev->keyval == GDK_KEY_KP_Subtract) {
			adjust_zoom(app, -1);
			return TRUE;
		}
		if (ev->keyval == GDK_KEY_0 || ev->keyval == GDK_KEY_KP_0) {
			adjust_zoom(app, 0);
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean on_window_delete_event(GtkWidget *win, GdkEvent *ev, gpointer user_data) {
	(void)win;
	(void)ev;
	NotesApp *app = user_data;

	int n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
	for (int i = n_pages - 1; i >= 0; i--) {
		GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
		NoteTab *tab = get_tab_for_page(app, page);
		if (!tab) continue;

		/* Autosave untitled drafts before asking, or ask confirmation for modified tabs */
		if (tab->modified) {
			gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), i);
			if (!confirm_and_dispose_tab(app, tab)) {
				/* User cancelled quit; keep untitled draft saved on disk */
				if (tab->filepath == NULL) {
					write_draft_for_tab(app, tab);
				}
				return TRUE; /* Stop window destruction */
			}
		} else if (tab->filepath == NULL) {
			remove_draft_for_tab(tab);
		}
		gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), i);
		destroy_tab_struct(tab);
	}

	if (app->autosave_timer_id > 0) {
		g_source_remove(app->autosave_timer_id);
		app->autosave_timer_id = 0;
	}
	return FALSE; /* Allow window destroy */
}

int main(int argc, char **argv) {
	g_set_prgname("diamonds-notes");
	gtk_init(&argc, &argv);
	diamonds_gtk_apply_theme(NOTES_EXTRA_CSS);

	NotesApp app;
	memset(&app, 0, sizeof(app));
	app.use_monospace = true;
	app.use_word_wrap = true;
	app.font_pt = DEFAULT_FONT_PT;
	app.next_untitled_id = 1;
	ensure_draft_dir(&app);

	app.font_css_provider = gtk_css_provider_new();
	gtk_style_context_add_provider_for_screen(
		gdk_screen_get_default(),
		GTK_STYLE_PROVIDER(app.font_css_provider),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);
	apply_font_css(&app);

	app.window = diamonds_gtk_window_new("Diamonds Notes", 880, 600);
	g_signal_connect(app.window, "delete-event", G_CALLBACK(on_window_delete_event), &app);
	g_signal_connect(app.window, "key-press-event", G_CALLBACK(on_key_press), &app);

	/* HeaderBar */
	GtkWidget *header = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Diamonds Notes");
	gtk_window_set_titlebar(GTK_WINDOW(app.window), header);

	GtkWidget *btn_new = gtk_button_new_with_label("New");
	GtkWidget *btn_open = gtk_button_new_with_label("Open");
	GtkWidget *btn_save = gtk_button_new_with_label("Save");
	GtkWidget *btn_save_as = gtk_button_new_with_label("Save As");
	g_signal_connect(btn_new, "clicked", G_CALLBACK(on_new_clicked), &app);
	g_signal_connect(btn_open, "clicked", G_CALLBACK(on_open_clicked), &app);
	g_signal_connect(btn_save, "clicked", G_CALLBACK(on_save_clicked), &app);
	g_signal_connect(btn_save_as, "clicked", G_CALLBACK(on_save_as_clicked), &app);

	gtk_header_bar_pack_start(GTK_HEADER_BAR(header), btn_new);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header), btn_open);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header), btn_save);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header), btn_save_as);

	app.mono_toggle = gtk_toggle_button_new_with_label("Monospace");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.mono_toggle), app.use_monospace);
	g_signal_connect(app.mono_toggle, "toggled", G_CALLBACK(on_mono_toggled), &app);

	app.wrap_toggle = gtk_toggle_button_new_with_label("Wrap");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.wrap_toggle), app.use_word_wrap);
	g_signal_connect(app.wrap_toggle, "toggled", G_CALLBACK(on_wrap_toggled), &app);

	GtkWidget *btn_find = gtk_button_new_with_label("Find");
	g_signal_connect(btn_find, "clicked", G_CALLBACK(on_find_btn_clicked), &app);

	gtk_header_bar_pack_end(GTK_HEADER_BAR(header), app.wrap_toggle);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(header), app.mono_toggle);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(header), btn_find);

	/* Main vertical layout */
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(app.window), vbox);

	/* Find & Replace Revealer */
	app.find_revealer = gtk_revealer_new();
	gtk_revealer_set_transition_type(GTK_REVEALER(app.find_revealer),
		GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
	GtkWidget *find_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_style_context_add_class(gtk_widget_get_style_context(find_box), "find-bar");

	GtkWidget *find_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *find_lbl = gtk_label_new("Find:");
	app.find_entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(app.find_entry), "Search text...");
	g_signal_connect(app.find_entry, "changed", G_CALLBACK(on_find_entry_changed), &app);
	g_signal_connect(app.find_entry, "activate", G_CALLBACK(on_find_next_clicked), &app);

	GtkWidget *btn_prev = gtk_button_new_with_label("Prev");
	GtkWidget *btn_next = gtk_button_new_with_label("Next");
	g_signal_connect(btn_prev, "clicked", G_CALLBACK(on_find_prev_clicked), &app);
	g_signal_connect(btn_next, "clicked", G_CALLBACK(on_find_next_clicked), &app);

	app.case_check = gtk_check_button_new_with_label("Match Case");
	g_signal_connect(app.case_check, "toggled", G_CALLBACK(on_find_entry_changed), &app);

	app.match_label = gtk_label_new("0 matches");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.match_label), "dim");

	GtkWidget *btn_close_find = gtk_button_new_with_label("✕");
	g_signal_connect(btn_close_find, "clicked", G_CALLBACK(on_find_close_clicked), &app);

	gtk_box_pack_start(GTK_BOX(find_row), find_lbl, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(find_row), app.find_entry, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(find_row), btn_prev, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(find_row), btn_next, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(find_row), app.case_check, FALSE, FALSE, 4);
	gtk_box_pack_start(GTK_BOX(find_row), app.match_label, FALSE, FALSE, 6);
	gtk_box_pack_end(GTK_BOX(find_row), btn_close_find, FALSE, FALSE, 0);

	app.replace_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *rep_lbl = gtk_label_new("Replace:");
	app.replace_entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(app.replace_entry), "Replace with...");
	GtkWidget *btn_rep_one = gtk_button_new_with_label("Replace");
	GtkWidget *btn_rep_all = gtk_button_new_with_label("Replace All");
	g_signal_connect(btn_rep_one, "clicked", G_CALLBACK(on_replace_clicked), &app);
	g_signal_connect(btn_rep_all, "clicked", G_CALLBACK(on_replace_all_clicked), &app);

	gtk_box_pack_start(GTK_BOX(app.replace_row), rep_lbl, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(app.replace_row), app.replace_entry, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(app.replace_row), btn_rep_one, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(app.replace_row), btn_rep_all, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(find_box), find_row, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(find_box), app.replace_row, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(app.find_revealer), find_box);
	gtk_box_pack_start(GTK_BOX(vbox), app.find_revealer, FALSE, FALSE, 0);

	/* Notebook */
	app.notebook = gtk_notebook_new();
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(app.notebook), TRUE);
	g_signal_connect(app.notebook, "switch-page", G_CALLBACK(on_switch_page), &app);
	gtk_box_pack_start(GTK_BOX(vbox), app.notebook, TRUE, TRUE, 0);

	/* Status Bar */
	GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	gtk_style_context_add_class(gtk_widget_get_style_context(status_box), "status-bar");
	app.status_pos_label = gtk_label_new("Ln 1, Col 1");
	app.status_words_label = gtk_label_new("0 words, 0 chars");
	app.status_zoom_label = gtk_label_new("Zoom: 100% (12pt)");
	GtkWidget *enc_label = gtk_label_new("UTF-8");

	gtk_box_pack_start(GTK_BOX(status_box), app.status_pos_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(status_box), app.status_words_label, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(status_box), enc_label, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(status_box), app.status_zoom_label, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(vbox), status_box, FALSE, FALSE, 0);

	gtk_widget_show_all(app.window);
	gtk_revealer_set_reveal_child(GTK_REVEALER(app.find_revealer), FALSE);

	/* Restore any untitled autosave drafts and open argv files */
	restore_drafts(&app);
	for (int i = 1; i < argc; i++) {
		open_file_in_editor(&app, argv[i]);
	}
	if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(app.notebook)) == 0) {
		create_new_tab(&app, NULL, "", 0, false);
	}

	app.autosave_timer_id = g_timeout_add_seconds(5, on_autosave_tick, &app);

	gtk_main();

	if (app.font_css_provider) {
		g_object_unref(app.font_css_provider);
		app.font_css_provider = NULL;
	}
	return 0;
}
