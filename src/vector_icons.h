#ifndef DIAMONDS_VECTOR_ICONS_H
#define DIAMONDS_VECTOR_ICONS_H

#include <cairo.h>

void diamonds_draw_vector_terminal(cairo_t *cr, double cx, double cy, double size);
void diamonds_draw_vector_files(cairo_t *cr, double cx, double cy, double size);
void diamonds_draw_vector_browser(cairo_t *cr, double cx, double cy, double size);
void diamonds_draw_vector_media(cairo_t *cr, double cx, double cy, double size);
void diamonds_draw_vector_store(cairo_t *cr, double cx, double cy, double size);
void diamonds_draw_vector_clock(cairo_t *cr, double cx, double cy, double size);
void diamonds_draw_vector_mail(cairo_t *cr, double cx, double cy, double size);
void diamonds_draw_vector_settings(cairo_t *cr, double cx, double cy, double size);
void diamonds_draw_vector_trash(cairo_t *cr, double cx, double cy, double size);
void diamonds_draw_vector_avatar(cairo_t *cr, double cx, double cy, double radius);
void diamonds_draw_vector_document(cairo_t *cr, double x, double y, double w, double h, const char *kind);
void diamonds_draw_vector_diamond_logo(cairo_t *cr, double cx, double cy, double size);

#endif /* DIAMONDS_VECTOR_ICONS_H */
