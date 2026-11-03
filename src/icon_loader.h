#ifndef DIAMONDS_ICON_LOADER_H
#define DIAMONDS_ICON_LOADER_H

#include <cairo.h>

void diamonds_icon_loader_init(void);
void diamonds_icon_loader_cleanup(void);

/* Load system icon by app keyword (e.g. "terminal", "files", "browser", "vlc", "settings", "trash")
 * Searches /usr/share/icons/hicolor, /usr/share/icons/Yaru, /usr/share/icons/Adwaita, /usr/share/pixmaps.
 * Returns a cached Cairo image surface scaled to size x size (premultiplied ARGB32),
 * or NULL if not found on disk (caller should fallback to vector rendering).
 */
cairo_surface_t *diamonds_load_system_icon(const char *app_key, int size);

/* Load desktop application icon directly from its .desktop Icon= string or app name.
 * Searches icon themes, /usr/share/pixmaps, ~/.local/share/icons, and absolute paths.
 * Returns a cached Cairo image surface scaled to size x size (premultiplied ARGB32),
 * or NULL if not found on disk.
 */
cairo_surface_t *diamonds_load_desktop_icon(const char *icon_key, const char *app_name, int size);

#endif /* DIAMONDS_ICON_LOADER_H */
