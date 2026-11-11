/* Shared Diamonds GTK3 theme + helpers (header-only).
 * Dark Mica graphite palette, accent #38BDF8 / #0284C7, no blur.
 * Usage:
 *   gtk_init(&argc, &argv);
 *   diamonds_gtk_apply_theme(NULL);            // or extra app-specific CSS
 *   GtkWidget *win = diamonds_gtk_window_new("Notes", 800, 560);
 */
#ifndef DIAMONDS_GTK_H
#define DIAMONDS_GTK_H

#include <gtk/gtk.h>

#define DIAMONDS_GTK_BASE_CSS \
	"window, dialog, viewport, scrolledwindow { background: #16161e; background-image: none; color: #e2e8f0;" \
	"  font-family: \"Segoe UI\", Cantarell, Ubuntu, sans-serif; }" \
	"headerbar { background: #1a1b26; background-image: none; border-bottom: 1px solid #282a36; color: #f8fafc;" \
	"  box-shadow: none; min-height: 36px; padding: 0 6px; }" \
	"headerbar .title { color: #f8fafc; font-weight: bold; }" \
	"headerbar .subtitle { color: #94a3b8; font-size: 11px; }" \
	"headerbar button { background: transparent; background-image: none; border: none; border-radius: 6px; color: #cbd5e1; box-shadow: none; }" \
	"headerbar button:hover { background: rgba(255,255,255,0.08); color: #ffffff; }" \
	".sidebar, stacksidebar, stacksidebar list, .sidebar list { background: #1a1b26; background-image: none; border-right: 1px solid #282a36; color: #cbd5e1; }" \
	".sidebar row, stacksidebar row { background: transparent; background-image: none; color: #cbd5e1; border-radius: 6px; margin: 2px 6px; padding: 8px 10px; }" \
	".sidebar row label, stacksidebar row label { color: inherit; }" \
	".sidebar row:hover, stacksidebar row:hover { background: rgba(255,255,255,0.06); color: #ffffff; }" \
	".sidebar row:selected, stacksidebar row:selected { background: #0284c7; background-image: none; color: #ffffff; font-weight: bold; }" \
	".sidebar button { background: transparent; background-image: none; color: #94a3b8; border: none; border-radius: 8px;" \
	"  margin: 3px 8px; padding: 8px 12px; }" \
	".sidebar button:hover { background: rgba(255,255,255,0.06); color: #ffffff; }" \
	".sidebar button:checked { background: #0284c7; color: #ffffff; }" \
	".card { background: #1a1b26; background-image: none; border: 1px solid #282a36; border-radius: 8px; padding: 12px 16px; }" \
	".card-title { font-weight: bold; color: #38bdf8; }" \
	".dim { color: #94a3b8; }" \
	"button { background: #202230; background-image: none; color: #e2e8f0; border: 1px solid #2e3244; border-radius: 8px;" \
	"  box-shadow: none; text-shadow: none; -gtk-icon-shadow: none; }" \
	"button label { color: inherit; }" \
	"button:hover { background: #2a2d3e; background-image: none; color: #ffffff; }" \
	"button.action-btn, button.suggested-action { background: #0284c7; background-image: none; color: #ffffff; border: none; }" \
	"button.action-btn:hover, button.suggested-action:hover { background: #0369a1; background-image: none; }" \
	"button.destructive-action { background: #b91c1c; background-image: none; color: #ffffff; border: none; }" \
	"entry, textview text, textview, spinbutton { background: #12131c; background-image: none; color: #e2e8f0; caret-color: #38bdf8; }" \
	"entry { border: 1px solid #2e3244; border-radius: 8px; padding: 6px 10px; box-shadow: none; }" \
	"entry:focus { border-color: #38bdf8; }" \
	"treeview, list, row { background: #16161e; background-image: none; color: #e2e8f0; }" \
	"treeview:selected, row:selected { background: #0284c7; background-image: none; color: #ffffff; }" \
	"treeview header button { background: #1a1b26; background-image: none; border: none; border-bottom: 1px solid #282a36;" \
	"  border-radius: 0; color: #94a3b8; }" \
	"progressbar trough { background: #20202a; background-image: none; border-radius: 8px; min-height: 8px; }" \
	"progressbar progress { background: #38bdf8; background-image: none; border-radius: 8px; min-height: 8px; }" \
	"scale trough { border-radius: 8px; background: #20202a; background-image: none; min-height: 4px; }" \
	"scale highlight { border-radius: 8px; background: #38bdf8; background-image: none; }" \
	"combobox button { background: #202230; background-image: none; color: #e2e8f0; }" \
	"notebook, notebook stack { background: #16161e; background-image: none; }" \
	"notebook header { background: #1a1b26; background-image: none; border-bottom: 1px solid #282a36; }" \
	"notebook tab { background: transparent; background-image: none; padding: 6px 14px; color: #94a3b8; border: none; }" \
	"notebook tab label { color: inherit; }" \
	"notebook tab:checked { background: rgba(56,189,248,0.12); color: #ffffff; box-shadow: inset 0 -2px #38bdf8; }" \
	"scrollbar, scrollbar trough { background: #16161e; background-image: none; border: none; }" \
	"scrollbar slider { background: #3a3c4e; background-image: none; border-radius: 8px; min-width: 6px; min-height: 6px; }" \
	"statusbar, .statusbar { background: #1a1b26; background-image: none; color: #94a3b8; border-top: 1px solid #282a36; }"

/* Apply the shared theme, plus optional app-specific CSS (may be NULL). */
static inline void diamonds_gtk_apply_theme(const char *extra_css) {
	GtkSettings *settings = gtk_settings_get_default();
	if (settings) {
		g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);
	}
	GtkCssProvider *provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(provider, DIAMONDS_GTK_BASE_CSS, -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
		GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER + 10);
	g_object_unref(provider);

	if (extra_css && *extra_css) {
		GtkCssProvider *extra = gtk_css_provider_new();
		gtk_css_provider_load_from_data(extra, extra_css, -1, NULL);
		gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
			GTK_STYLE_PROVIDER(extra), GTK_STYLE_PROVIDER_PRIORITY_USER + 11);
		g_object_unref(extra);
	}
}

/* Top-level window with a HeaderBar title; destroying it quits gtk_main(). */
static inline GtkWidget *diamonds_gtk_window_new(const char *title, int w, int h) {
	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), title);
	gtk_window_set_default_size(GTK_WINDOW(win), w, h);
	g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	return win;
}

#endif /* DIAMONDS_GTK_H */
