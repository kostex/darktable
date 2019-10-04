/*
    This file is part of darktable,
    copyright (c) 2010 Henrik Andersson.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "paint.h"
#include "gui/draw.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.141592654
#endif

void dtgtk_cairo_paint_empty(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  cairo_translate(cr, x, y);
  cairo_scale(cr, w, h);
  cairo_stroke(cr);
  cairo_identity_matrix(cr);
}
void dtgtk_cairo_paint_color(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  cairo_translate(cr, x, y);
  cairo_scale(cr, w, h);
  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_rectangle(cr, 0.1, 0.1, 0.8, 0.8);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.6);
  cairo_stroke(cr);
  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_presets(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, 0.1, 0.1);
  cairo_line_to(cr, 0.9, 0.1);
  cairo_move_to(cr, 0.1, 0.5);
  cairo_line_to(cr, 0.9, 0.5);
  cairo_move_to(cr, 0.1, 0.9);
  cairo_line_to(cr, 0.9, 0.9);
  cairo_stroke(cr);

  cairo_identity_matrix(cr);
}


void dtgtk_cairo_paint_triangle(cairo_t *cr, gint x, int y, gint w, gint h, gint flags, void *data)
{
  /* initialize rotation and flip matrices */
  cairo_matrix_t hflip_matrix;
  cairo_matrix_init(&hflip_matrix, -1, 0, 0, 1, 1, 0);

  double C = cos(-(M_PI / 2.0)), S = sin(-(M_PI / 2.0)); // -90 degrees
  C = flags & CPF_DIRECTION_DOWN ? cos(-(M_PI * 1.5)) : C;
  S = flags & CPF_DIRECTION_DOWN ? sin(-(M_PI * 1.5)) : S;
  cairo_matrix_t rotation_matrix;
  cairo_matrix_init(&rotation_matrix, C, S, -S, C, 0.5 - C * 0.5 + S * 0.5, 0.5 - S * 0.5 - C * 0.5);

  /* scale and transform*/
  const gint s = w < h ? w : h;
  cairo_save(cr);
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  if(flags & CPF_DIRECTION_UP || flags & CPF_DIRECTION_DOWN)
    cairo_transform(cr, &rotation_matrix);
  else if(flags & CPF_DIRECTION_LEFT) // Flip x transformation
    cairo_transform(cr, &hflip_matrix);


  cairo_move_to(cr, 0.1, 0.1);
  cairo_line_to(cr, 0.5, 0.5);
  cairo_line_to(cr, 0.1, 0.9);
  cairo_line_to(cr, 0.1, 0.1);
  cairo_stroke(cr);
  cairo_identity_matrix(cr);
  cairo_restore(cr);
}



void dtgtk_cairo_paint_solid_triangle(cairo_t *cr, gint x, int y, gint w, gint h, gint flags, void *data)
{
  /* initialize rotation and flip matrices */
  cairo_matrix_t hflip_matrix;
  cairo_matrix_init(&hflip_matrix, -1, 0, 0, 1, 1, 0);

  double C = cos(-(M_PI / 2.0)), S = sin(-(M_PI / 2.0)); // -90 degrees
  C = flags & CPF_DIRECTION_DOWN ? cos(-(M_PI * 1.5)) : C;
  S = flags & CPF_DIRECTION_DOWN ? sin(-(M_PI * 1.5)) : S;
  cairo_matrix_t rotation_matrix;
  cairo_matrix_init(&rotation_matrix, C, S, -S, C, 0.5 - C * 0.5 + S * 0.5, 0.5 - S * 0.5 - C * 0.5);

  /* scale and transform*/
  const gint s = w < h ? w : h;
  cairo_save(cr);
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  if(flags & CPF_DIRECTION_UP || flags & CPF_DIRECTION_DOWN)
    cairo_transform(cr, &rotation_matrix);
  else if(flags & CPF_DIRECTION_LEFT) // Flip x transformation
    cairo_transform(cr, &hflip_matrix);

  cairo_move_to(cr, 0.1, 0.1);
  cairo_line_to(cr, 0.5, 0.5);
  cairo_line_to(cr, 0.1, 0.9);
  cairo_line_to(cr, 0.1, 0.1);
  cairo_stroke_preserve(cr);
  cairo_fill(cr);
  cairo_identity_matrix(cr);
  cairo_restore(cr);
}



void dtgtk_cairo_paint_arrow(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  cairo_matrix_t hflip_matrix;
  cairo_matrix_init(&hflip_matrix, -1, 0, 0, 1, 1, 0);

  double C = cos(-(M_PI / 2.0)), S = sin(-(M_PI / 2.0)); // -90 degrees
  C = flags & CPF_DIRECTION_UP ? cos(-(M_PI * 1.5)) : C;
  S = flags & CPF_DIRECTION_UP ? sin(-(M_PI * 1.5)) : S;
  cairo_matrix_t rotation_matrix;
  cairo_matrix_init(&rotation_matrix, C, S, -S, C, 0.5 - C * 0.5 + S * 0.5, 0.5 - S * 0.5 - C * 0.5);

  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  if(flags & CPF_DIRECTION_UP || flags & CPF_DIRECTION_DOWN)
    cairo_transform(cr, &rotation_matrix);
  else if(flags & CPF_DIRECTION_RIGHT) // Flip x transformation
    cairo_transform(cr, &hflip_matrix);

  cairo_move_to(cr, 0.2, 0.1);
  cairo_line_to(cr, 0.9, 0.5);
  cairo_line_to(cr, 0.2, 0.9);
  cairo_stroke(cr);
  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_solid_arrow(cairo_t *cr, gint x, int y, gint w, gint h, gint flags, void *data)
{
  /* initialize rotation and flip matrices */
  cairo_matrix_t hflip_matrix;
  cairo_matrix_init(&hflip_matrix, -1, 0, 0, 1, 1, 0);

  double C = cos(-(M_PI / 2.0)), S = sin(-(M_PI / 2.0)); // -90 degrees
  C = flags & CPF_DIRECTION_DOWN ? cos(-(M_PI * 1.5)) : C;
  S = flags & CPF_DIRECTION_DOWN ? sin(-(M_PI * 1.5)) : S;
  cairo_matrix_t rotation_matrix;
  cairo_matrix_init(&rotation_matrix, C, S, -S, C, 0.5 - C * 0.5 + S * 0.5, 0.5 - S * 0.5 - C * 0.5);

  /* scale and transform*/
  gint s = w < h ? w : h;
  s *= 1.8; // for some reason, this one needs expansion
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_width(cr, 0.1);

  if(flags & CPF_DIRECTION_UP || flags & CPF_DIRECTION_DOWN)
    cairo_transform(cr, &rotation_matrix);
  else if(flags & CPF_DIRECTION_LEFT) // Flip x transformation
    cairo_transform(cr, &hflip_matrix);

  cairo_move_to(cr, 0.2, 0.1);
  cairo_line_to(cr, 0.9, 0.5);
  cairo_line_to(cr, 0.2, 0.9);
  cairo_fill(cr);
  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_flip(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  double C = cos(-1.570796327), S = sin(-1.570796327);
  cairo_matrix_t rotation_matrix;
  cairo_matrix_init(&rotation_matrix, C, S, -S, C, 0.5 - C * 0.5 + S * 0.5, 0.5 - S * 0.5 - C * 0.5);
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  if((flags & CPF_DIRECTION_UP)) // Rotate -90 degrees
    cairo_transform(cr, &rotation_matrix);

  cairo_move_to(cr, 0.05, 0.50);
  cairo_line_to(cr, 0.05, 0);
  cairo_line_to(cr, 0.95, 0.50);
  cairo_line_to(cr, 0.2, 0.50);
  cairo_stroke(cr);
  cairo_set_line_width(cr, 0.04);
  cairo_move_to(cr, 0.05, 0.62);
  cairo_line_to(cr, 0.05, 1.0);
  cairo_line_to(cr, 0.95, 0.62);
  cairo_stroke(cr);
  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_reset(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_arc(cr, 0.5, 0.5, 0.46, 0, 6.2832);
  cairo_move_to(cr, 0.5, 0.32);
  cairo_line_to(cr, 0.5, 0.68);
  cairo_stroke(cr);
  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_store(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, 0.275, 0.1);
  cairo_line_to(cr, 0.1, 0.1);
  cairo_line_to(cr, 0.1, 0.9);
  cairo_line_to(cr, 0.9, 0.9);
  cairo_line_to(cr, 0.9, 0.175);
  cairo_line_to(cr, 0.825, 0.1);
  cairo_line_to(cr, 0.825, 0.5);
  cairo_line_to(cr, 0.275, 0.5);
  cairo_line_to(cr, 0.275, 0.1);

  cairo_stroke(cr);
  cairo_set_line_width(cr, 0.1);
  cairo_rectangle(cr, 0.5, 0.025, 0.17, 0.275);
  cairo_fill(cr);

  cairo_stroke(cr);
  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_switch(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_arc(cr, 0.5, 0.5, 0.46, (-50 * 3.145 / 180), (230 * 3.145 / 180));
  cairo_move_to(cr, 0.5, 0.0);
  cairo_line_to(cr, 0.5, 0.5);
  cairo_stroke(cr);

  if((flags & CPF_ACTIVE)) // If active add some green diffuse light
  {
    cairo_arc(cr, 0.5, 0.5, 0.45, 0.0, 2*M_PI);
    cairo_clip(cr);
    cairo_paint_with_alpha(cr, 0.33);
  }

  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_plus(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  dtgtk_cairo_paint_plusminus(cr, x, y, w, h, flags | CPF_ACTIVE, data);
}

void dtgtk_cairo_paint_plusminus(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  cairo_save(cr);
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_arc(cr, 0.5, 0.5, 0.45, 0, 2 * M_PI);
  cairo_stroke(cr);

  if((flags & CPF_ACTIVE))
  {
    cairo_move_to(cr, 0.5, 0.2);
    cairo_line_to(cr, 0.5, 0.8);
    cairo_move_to(cr, 0.2, 0.5);
    cairo_line_to(cr, 0.8, 0.5);
    cairo_stroke(cr);
  }
  else
  {
    cairo_arc(cr, 0.5, 0.5, 0.45, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 1.0);
    cairo_move_to(cr, 0.2, 0.5);
    cairo_line_to(cr, 0.8, 0.5);
    cairo_stroke(cr);
  }

  cairo_identity_matrix(cr);
  cairo_restore(cr);
}

void dtgtk_cairo_paint_sorting(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  cairo_save(cr);
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  cairo_set_line_width(cr, 0.1);
  cairo_move_to(cr, 0.4, 0.1);
  cairo_line_to(cr, 0.4, 0.9);
  cairo_line_to(cr, 0.2, 0.7);
  cairo_move_to(cr, 0.6, 0.9);
  cairo_line_to(cr, 0.6, 0.1);
  cairo_line_to(cr, 0.8, 0.3);
  cairo_stroke(cr);

  cairo_identity_matrix(cr);
  cairo_restore(cr);
}

void dtgtk_cairo_paint_plus_simple(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  cairo_save(cr);
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  cairo_set_line_width(cr, 0.1);
  cairo_move_to(cr, 0.5, 0.1);
  cairo_line_to(cr, 0.5, 0.9);
  cairo_move_to(cr, 0.1, 0.5);
  cairo_line_to(cr, 0.9, 0.5);
  cairo_stroke(cr);

  cairo_identity_matrix(cr);
  cairo_restore(cr);
}

void dtgtk_cairo_paint_minus_simple(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  cairo_save(cr);
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  cairo_set_line_width(cr, 0.1);
  cairo_move_to(cr, 0.1, 0.5);
  cairo_line_to(cr, 0.9, 0.5);
  cairo_stroke(cr);

  cairo_identity_matrix(cr);
  cairo_restore(cr);
}

void dtgtk_cairo_paint_multiply_small(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  cairo_save(cr);
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  cairo_set_line_width(cr, 0.1);
  cairo_move_to(cr, 0.3, 0.3);
  cairo_line_to(cr, 0.7, 0.7);
  cairo_move_to(cr, 0.7, 0.3);
  cairo_line_to(cr, 0.3, 0.7);
  cairo_stroke(cr);

  cairo_identity_matrix(cr);
  cairo_restore(cr);
}

void dtgtk_cairo_paint_treelist(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  cairo_save(cr);
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  cairo_set_line_width(cr, 0.2);
  cairo_move_to(cr, 0.05, 0.05);
  cairo_line_to(cr, 0.125, 0.05);
  cairo_move_to(cr, 0.25, 0.35);
  cairo_line_to(cr, 0.325, 0.35);
  cairo_move_to(cr, 0.45, 0.65);
  cairo_line_to(cr, 0.525, 0.65);
  cairo_move_to(cr, 0.25, 0.95);
  cairo_line_to(cr, 0.325, 0.95);
  cairo_stroke(cr);

  cairo_set_line_width(cr, 0.1);
  cairo_move_to(cr, 0.35, 0.05);
  cairo_line_to(cr, 0.95, 0.05);
  cairo_move_to(cr, 0.55, 0.35);
  cairo_line_to(cr, 0.95, 0.35);
  cairo_move_to(cr, 0.75, 0.65);
  cairo_line_to(cr, 0.95, 0.65);
  cairo_move_to(cr, 0.55, 0.95);
  cairo_line_to(cr, 0.95, 0.95);
  cairo_stroke(cr);

  cairo_identity_matrix(cr);
  cairo_restore(cr);
}

void dtgtk_cairo_paint_invert(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_arc(cr, 0.5, 0.5, 0.46, 0, 2.0 * M_PI);
  cairo_stroke(cr);
  cairo_arc(cr, 0.5, 0.5, 0.46, 3.0 * M_PI / 2.0, M_PI / 2.0);
  cairo_fill(cr);
  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_eye(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  cairo_arc(cr, 0.5, 0.5, 0.1, 0, 6.2832);
  cairo_stroke(cr);

  cairo_translate(cr, 0, 0.20);
  cairo_save(cr);
  cairo_scale(cr, 1.0, 0.60);
  cairo_arc(cr, 0.5, 0.5, 0.45, 0, 6.2832);
  cairo_restore(cr);
  cairo_stroke(cr);
  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_masks_eye(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_width(cr, 0.08);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  double dashed[] = { 0.2, 0.2 };
  int len = sizeof(dashed) / sizeof(dashed[0]);
  cairo_set_dash(cr, dashed, len, 0);

  cairo_arc(cr, 0.75, 0.75, 0.75, 2.8, 4.7124);
  cairo_set_line_width(cr, 0.1);
  cairo_stroke(cr);

  cairo_move_to(cr, 0.4, 0.1);
  cairo_line_to(cr, 0.3, 0.8);
  cairo_line_to(cr, 0.55, 0.716667);
  cairo_line_to(cr, 0.65, 1.016667);
  cairo_line_to(cr, 0.75, 0.983333);
  cairo_line_to(cr, 0.65, 0.683333);
  cairo_line_to(cr, 0.9, 0.6);
  cairo_line_to(cr, 0.4, 0.1);
  cairo_fill(cr);

  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_masks_circle(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  if(flags & CPF_ACTIVE)
    cairo_set_line_width(cr, 0.25);
  else
    cairo_set_line_width(cr, 0.08);
  cairo_arc(cr, 0.5, 0.5, 0.4, 0, 6.2832);
  cairo_stroke(cr);
  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_masks_ellipse(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  if(flags & CPF_ACTIVE)
    cairo_set_line_width(cr, 0.25);
  else
    cairo_set_line_width(cr, 0.08);
  cairo_save(cr);
  cairo_scale(cr, 0.707, 1);
  cairo_translate(cr, 0.15, 0);
  cairo_arc(cr, 0.5, 0.5, 0.4, 0, 6.2832);
  cairo_restore(cr);
  cairo_stroke(cr);
  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_masks_gradient(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  if(flags & CPF_ACTIVE)
    cairo_set_line_width(cr, 0.25);
  else
    cairo_set_line_width(cr, 0.08);
  cairo_rectangle(cr, 0.1, 0.1, 0.8, 0.8);
  cairo_stroke_preserve(cr);
  cairo_pattern_t *pat = NULL;
  pat = cairo_pattern_create_linear(0.5, 0.1, 0.5, 0.9);
  cairo_pattern_add_color_stop_rgba(pat, 0.1, 0.6, 0.6, 0.6, 0.9);
  cairo_pattern_add_color_stop_rgba(pat, 0.9, 0.2, 0.2, 0.2, 0.9);
  cairo_set_source(cr, pat);
  cairo_fill(cr);
  cairo_pattern_destroy(pat);
  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_masks_path(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  if(flags & CPF_ACTIVE)
    cairo_set_line_width(cr, 0.25);
  else
    cairo_set_line_width(cr, 0.08);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, 0.1, 0.9);
  cairo_curve_to(cr, 0.1, 0.5, 0.9, 0.6, 0.9, 0.1);
  cairo_stroke(cr);
  cairo_move_to(cr, 0.5, 0.5);
  cairo_line_to(cr, 0.3, 0.1);
  cairo_set_line_width(cr, 0.1);
  cairo_stroke(cr);
  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_masks_vertgradient(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  if(flags & CPF_ACTIVE)
    cairo_set_line_width(cr, 0.25);
  else
    cairo_set_line_width(cr, 0.08);
  cairo_rectangle(cr, 0.1, 0.1, 0.9, 0.9);
  cairo_stroke_preserve(cr);
  cairo_pattern_t *pat = NULL;
  pat = cairo_pattern_create_linear(0.1, 0.5, 0.9, 0.5);
  cairo_pattern_add_color_stop_rgba(pat, 0.1, 0.6, 0.6, 0.6, 0.9);
  cairo_pattern_add_color_stop_rgba(pat, 1.0, 0.2, 0.2, 0.2, 0.9);
  cairo_rectangle(cr, 0.1, 0.1, 0.8, 0.8);
  cairo_set_source(cr, pat);
  cairo_fill(cr);
  cairo_pattern_destroy(pat);
  cairo_identity_matrix(cr);
}


void dtgtk_cairo_paint_masks_brush_and_inverse(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  if(flags & CPF_ACTIVE)
    cairo_set_line_width(cr, 0.25);
  else
    cairo_set_line_width(cr, 0.08);
  cairo_translate(cr, 0.1, 0.1);
  cairo_scale(cr, 0.8, 0.8);
  cairo_move_to(cr, 0.0, 1.0);
  cairo_line_to(cr, 0.1, 0.7);
  cairo_line_to(cr, 0.8, 0.0);
  cairo_line_to(cr, 1.0, 0.2);
  cairo_line_to(cr, 0.3, 0.9);
  cairo_line_to(cr, 0.0, 1.0);
  //  cairo_fill_preserve(cr);
  cairo_stroke(cr);

  //cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_set_line_width(cr, 0.1);
  cairo_arc(cr, 0.5, 0.5, 0.4, 0, 2.0 * M_PI);
  cairo_stroke(cr);
  cairo_arc(cr, 0.5, 0.5, 0.4, 3.0 * M_PI / 2.0, M_PI / 2.0);
  cairo_fill(cr);

  cairo_stroke(cr);
  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_masks_brush(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  if(flags & CPF_ACTIVE)
    cairo_set_line_width(cr, 0.25);
  else
    cairo_set_line_width(cr, 0.08);
  cairo_translate(cr, 0.1, 0.1);
  cairo_scale(cr, 0.8, 0.8);
  cairo_move_to(cr, 0.0, 1.0);
  cairo_line_to(cr, 0.1, 0.7);
  cairo_line_to(cr, 0.8, 0.0);
  cairo_line_to(cr, 1.0, 0.2);
  cairo_line_to(cr, 0.3, 0.9);
  cairo_line_to(cr, 0.0, 1.0);
  //  cairo_fill_preserve(cr);
  cairo_stroke(cr);
  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_masks_uniform(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.) - (s / 2.), y + (h / 2.) - (s / 2.));
  cairo_scale(cr, s, s);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_width(cr, 0.1);

  cairo_arc(cr, 0.5, 0.5, 0.5, -M_PI, M_PI);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_masks_drawn(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
  cairo_set_line_width(cr, 0.1);

  // draw the body of the pencil (filled)
  cairo_move_to(cr, 0.9, 0.6);
  cairo_line_to(cr, 0.3, 0.0);
  cairo_line_to(cr, 0.0, 0.3);
  cairo_line_to(cr, 0.6, 0.9);
  // cairo_stroke_preserve(cr);
  cairo_fill(cr);
  cairo_stroke(cr);
  // draw the tip of the pencil
  cairo_move_to(cr, 1.0, 1.0);
  cairo_line_to(cr, 0.9, 0.6);
  cairo_line_to(cr, 0.6, 0.9);
  cairo_line_to(cr, 1.0, 1.0);
  cairo_stroke(cr);
  cairo_identity_matrix(cr);
}

/** draws an arc with a B&W gradient following the arc path.
 *  nb_steps must be adjusted depending on the displayed size of the element, 16 is fine for small buttons*/
void _gradient_arc(cairo_t *cr, double lw, int nb_steps, double x_center, double y_center, double radius,
                   double angle_from, double angle_to, double color_from, double color_to)
{
  cairo_set_line_width(cr, lw);

  double *portions = malloc((1 + nb_steps) * sizeof(double));

  // note: cairo angles seems to be shifted by M_PI relatively to the unit circle
  angle_from = angle_from + M_PI;
  angle_to = angle_to + M_PI;
  double step = (angle_to - angle_from) / nb_steps;
  for(int i = 0; i < nb_steps; i++) portions[i] = angle_from + i * step;
  portions[nb_steps] = angle_to;

  for(int i = 0; i < nb_steps; i++)
  {
    double color = color_from + i * (color_to - color_from) / nb_steps;
    cairo_set_source_rgb(cr, color, color, color);
    cairo_arc(cr, x_center, y_center, radius, portions[i], portions[i + 1]);
    cairo_stroke(cr);
  }
  free(portions);
}

void dtgtk_cairo_paint_masks_parametric(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
  _gradient_arc(cr, 0.125, 16, 0.5, 0.5, 0.5, -M_PI / 3.0, M_PI + M_PI / 3.0, 0.3, 0.9);

  cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
  cairo_set_line_width(cr, 0.05);
  // draw one tick up right
  cairo_move_to(cr, 1, 0.2);
  cairo_line_to(cr, 1.2, 0.2);
  cairo_line_to(cr, 1.1, 0.0);
  cairo_fill(cr);
  // draw another tick center right
  cairo_move_to(cr, 1.1, 0.6);
  cairo_line_to(cr, 1.325, 0.55);
  cairo_line_to(cr, 1.275, 0.75);
  cairo_fill(cr);

  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_masks_drawn_and_parametric(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags,
                                                  void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
  _gradient_arc(cr, 0.125, 16, 0.75, 0.6, 0.4, -M_PI / 3.0, M_PI + M_PI / 3.0, 0.3, 0.9);

  cairo_set_line_width(cr, 0.05);
  cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
  // draw one tick up right
  cairo_move_to(cr, 1.2, 0.35);
  cairo_line_to(cr, 1.35, 0.35);
  cairo_line_to(cr, 1.275, 0.15);
  cairo_fill(cr);
  // draw another tick center right
  cairo_move_to(cr, 1.25, 0.7);
  cairo_line_to(cr, 1.4, 0.6);
  cairo_line_to(cr, 1.4, 0.8);
  cairo_fill(cr);

  cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
  double szf = 0.8;     // size factor to reduce the size of the pencil
  double shift = -0.10; // shift factor on x axis
  // draw the body of the pencil (filled)
  cairo_move_to(cr, 0.9 * szf + shift, 0.6 * szf);
  cairo_line_to(cr, 0.3 * szf + shift, 0.0 * szf);
  cairo_line_to(cr, 0.0 * szf + shift, 0.3 * szf);
  cairo_line_to(cr, 0.6 * szf + shift, 0.9 * szf);
  cairo_fill(cr);
  cairo_stroke(cr);
  // draw the tip of the pencil
  cairo_move_to(cr, 1.0 * szf + shift, 1.0 * szf);
  cairo_line_to(cr, 0.9 * szf + shift, 0.6 * szf);
  cairo_line_to(cr, 0.6 * szf + shift, 0.9 * szf);
  cairo_line_to(cr, 1.0 * szf + shift, 1.0 * szf);
  cairo_stroke(cr);

  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_masks_raster(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
  cairo_arc(cr, 0.5, 0.5, 0.5, 0, 2 * M_PI);
  cairo_clip(cr);
  cairo_new_path(cr);

  for(int i = 0; i < 4; i++)
    for(int j = 0; j < 4; j++)
    {
      double color = (i + j) % 2 ? 0.2 : 0.9;
      cairo_set_source_rgb(cr, color, color, color);
      cairo_rectangle(cr, i / 4.0, j / 4.0, 1.0 / 4.0, 1.0 / 4.0);
      cairo_fill(cr);
    }
}


void dtgtk_cairo_paint_masks_multi(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_arc(cr, 0.3, 0.3, 0.3, 0, 6.2832);
  cairo_stroke(cr);
  cairo_move_to(cr, 0.0, 1.0);
  cairo_curve_to(cr, 0.0, 0.5, 1.0, 0.6, 1.0, 0.0);
  cairo_stroke(cr);
  cairo_identity_matrix(cr);
}
void dtgtk_cairo_paint_masks_inverse(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_arc(cr, 0.5, 0.5, 0.46, 0, 2.0 * M_PI);
  cairo_stroke(cr);
  cairo_arc(cr, 0.5, 0.5, 0.46, 3.0 * M_PI / 2.0, M_PI / 2.0);
  cairo_fill(cr);
  cairo_identity_matrix(cr);
}
void dtgtk_cairo_paint_masks_union(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s * 1.4, s);

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
  cairo_arc(cr, -0.05, 0.5, 0.45, 0, 6.2832);
  cairo_arc(cr, 0.764, 0.5, 0.45, 0, 6.2832);
  cairo_fill(cr);
  cairo_identity_matrix(cr);
}
void dtgtk_cairo_paint_masks_intersection(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s * 1.4, s);

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_width(cr, 0.1);
  cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
  cairo_arc(cr, 0.05, 0.5, 0.45, 0, 6.3);
  cairo_new_sub_path(cr);
  cairo_arc(cr, 0.65, 0.5, 0.45, 0, 6.3);
  cairo_stroke(cr);
  cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
  cairo_new_sub_path(cr);
  cairo_arc(cr, 0.05, 0.5, 0.45, -1.0416, 1.0416);
  cairo_arc(cr, 0.65, 0.5, 0.45, 2.1, 4.1832);
  cairo_close_path(cr);
  cairo_fill(cr);
  cairo_identity_matrix(cr);
}
void dtgtk_cairo_paint_masks_difference(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s * 1.4, s);

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_width(cr, 0.1);
  cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
  cairo_arc(cr, 0.65, 0.5, 0.45, 0, 6.3);
  cairo_stroke(cr);
  cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
  cairo_new_sub_path(cr);
  cairo_arc(cr, 0.05, 0.5, 0.45, 1.0416, 5.2416);
  cairo_arc_negative(cr, 0.65, 0.5, 0.45, 4.1832, 2.1);
  cairo_close_path(cr);
  cairo_fill(cr);
  cairo_identity_matrix(cr);
}
void dtgtk_cairo_paint_masks_exclusion(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s * 1.4, s);

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
  cairo_arc(cr, 0.0, 0.5, 0.45, 0, 6.2832);
  cairo_arc_negative(cr, 0.714, 0.5, 0.45, 0, 6.2832);
  cairo_fill(cr);
  cairo_identity_matrix(cr);
}
void dtgtk_cairo_paint_masks_used(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_width(cr, 0.1);
  cairo_arc(cr, 0.5, 0.5, 0.35, 0, 6.2832);
  cairo_move_to(cr, 0.5, 0.15);
  cairo_line_to(cr, 0.5, 0.5);
  cairo_stroke(cr);
  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_eye_toggle(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  cairo_arc(cr, 0.5, 0.5, 0.1, 0, 6.2832);
  cairo_stroke(cr);

  cairo_translate(cr, 0, 0.20);
  cairo_save(cr);
  cairo_scale(cr, 1.0, 0.60);
  cairo_arc(cr, 0.5, 0.5, 0.45, 0, 6.2832);
  cairo_restore(cr);
  cairo_stroke(cr);

  cairo_translate(cr, 0, -0.20);
  if((flags & CPF_ACTIVE))
  {
    cairo_set_source_rgba(cr, 0.6, 0.1, 0.1, 1.0);
    cairo_move_to(cr, 0.1, 0.9);
    cairo_line_to(cr, 0.9, 0.1);
    cairo_stroke(cr);
  }

  cairo_identity_matrix(cr);
}



void dtgtk_cairo_paint_timer(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  cairo_set_line_width(cr, 0.1);
  cairo_arc(cr, 0.5, 0.5, 0.5, (-80 * 3.145 / 180), (150 * 3.145 / 180));
  cairo_line_to(cr, 0.5, 0.5);

  cairo_stroke(cr);

  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_filmstrip(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  gdouble sw = 0.6;
  gdouble bend = 0.3;

  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_scale(cr, 0.7, 0.7);
  cairo_translate(cr, 0.15, 0.15);

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  /* s curve left */
  cairo_set_line_width(cr, 0.1);
  cairo_move_to(cr, 0.0, 1.0);
  cairo_curve_to(cr, 0.0, 0.0 + bend, (1.0 - sw), 1.0 - bend, (1.0 - sw), 0.0);
  cairo_stroke(cr);

  /* s curve down */
  cairo_move_to(cr, 1.0, 0.0);
  cairo_curve_to(cr, 1.0, 1.0 - bend, sw, 0.0 + bend, sw, 1.0);
  cairo_stroke(cr);

  /* filmstrip start,stop and divider */
  cairo_set_line_width(cr, 0.05);
  cairo_move_to(cr, 0, 1.0);
  cairo_line_to(cr, sw, 1.0);
  cairo_stroke(cr);
  cairo_move_to(cr, 1 - sw, 0.0);
  cairo_line_to(cr, 1.0, 0.0);
  cairo_stroke(cr);

  cairo_set_line_width(cr, 0.07);
  cairo_move_to(cr, 1 - sw, 0.5);
  cairo_line_to(cr, sw, 0.5);
  cairo_stroke(cr);


  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_directory(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  cairo_save(cr);
  cairo_set_source_rgb(cr, .8, .8, .8);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_translate(cr, x + .05 * w, y + .05 * h);
  cairo_scale(cr, .9 * w, .9 * h);
  cairo_set_line_width(cr, 1. / w);
  cairo_rectangle(cr, 0., 0., 1., 1.);
  cairo_stroke(cr);
  cairo_move_to(cr, 0., .2);
  cairo_line_to(cr, .5, .2);
  cairo_line_to(cr, .6, 0.);
  cairo_stroke(cr);
  cairo_restore(cr);
}

void dtgtk_cairo_paint_refresh(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  if(flags & 1)
  {
    cairo_translate(cr, 1, 0);
    cairo_scale(cr, -1, 1);
  }

  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, 0.65, 0.1);
  cairo_line_to(cr, 0.5, 0.2);
  cairo_line_to(cr, 0.65, 0.3);
  cairo_stroke(cr);

  cairo_set_line_width(cr, 0.10);
  cairo_arc(cr, 0.5, 0.5, 0.35, (-80 * 3.145 / 180), (220 * 3.145 / 180));
  cairo_stroke(cr);

  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_perspective(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  if((flags & 3) == 1)
  {
    cairo_move_to(cr, 0.1, 0.9);
    cairo_line_to(cr, 0.3, 0.1);
    cairo_line_to(cr, 0.7, 0.1);
    cairo_line_to(cr, 0.9, 0.9);
    cairo_line_to(cr, 0.1, 0.9);
    cairo_stroke(cr);
  }
  else if((flags & 3) == 2)
  {
    cairo_move_to(cr, 0.1, 0.9);
    cairo_line_to(cr, 0.9, 0.7);
    cairo_line_to(cr, 0.9, 0.3);
    cairo_line_to(cr, 0.1, 0.1);
    cairo_line_to(cr, 0.1, 0.9);
    cairo_stroke(cr);
  }
  else if((flags & 3) == 3)
  {
    cairo_move_to(cr, 0.1, 0.9);
    cairo_line_to(cr, 0.9, 0.7);
    cairo_line_to(cr, 0.8, 0.2);
    cairo_line_to(cr, 0.3, 0.1);
    cairo_line_to(cr, 0.1, 0.9);
    cairo_stroke(cr);
  }
}

void dtgtk_cairo_paint_structure(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  cairo_move_to(cr, 0.1, 0.1);
  cairo_line_to(cr, 0.0, 0.9);
  cairo_stroke(cr);
  cairo_move_to(cr, 0.366, 0.1);
  cairo_line_to(cr, 0.33, 0.9);
  cairo_stroke(cr);
  cairo_move_to(cr, 0.633, 0.1);
  cairo_line_to(cr, 0.66, 0.9);
  cairo_stroke(cr);
  cairo_move_to(cr, 0.9, 0.1);
  cairo_line_to(cr, 1.0, 0.9);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_cancel(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, 0.9, 0.1);
  cairo_line_to(cr, 0.1, 0.9);
  cairo_stroke(cr);
  cairo_move_to(cr, 0.9, 0.9);
  cairo_line_to(cr, 0.1, 0.1);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_aspectflip(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  cairo_save(cr);
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  if(flags & 1)
  {
    cairo_translate(cr, 0, 1);
    cairo_scale(cr, 1, -1);
  }

  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, 0.65, 0.0);
  cairo_line_to(cr, 0.5, 0.05);
  cairo_line_to(cr, 0.6, 0.25);
  cairo_stroke(cr);

  cairo_set_line_width(cr, 0.1);
  cairo_arc(cr, 0.5, 0.5, 0.45, (-80 * 3.145 / 180), (220 * 3.145 / 180));
  cairo_stroke(cr);
  cairo_restore(cr);
}

void dtgtk_cairo_paint_styles(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, 0.07);
  cairo_arc(cr, 0.2, 0.8, 0.2, 0.0, 2.0 * M_PI);
  cairo_stroke(cr);
  cairo_arc(cr, 0.7, 0.7, 0.3, 0.0, 2.0 * M_PI);
  cairo_stroke(cr);
  cairo_arc(cr, 0.4, 0.2, 0.25, 0.0, 2.0 * M_PI);
  cairo_stroke(cr);

  /* if its a popup menu */
  if(flags)
  {
    cairo_move_to(cr, 0.9, -0.2);
    cairo_line_to(cr, 0.7, 0.3);
    cairo_line_to(cr, 1.1, 0.3);
    cairo_fill(cr);
  }
}

void dtgtk_cairo_paint_label(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  gboolean def = FALSE;
  const gint s = (w < h ? w : h);
  double r = 0.4;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  /* fill base color */
  cairo_arc(cr, 0.5, 0.5, r, 0.0, 2.0 * M_PI);
  float alpha = 1.0;

  if((flags & 8) && !(flags & CPF_PRELIGHT)) alpha = 0.6;

  switch((flags & 7))
  {
    case 0:
      cairo_set_source_rgba(cr, 0.9, 0.0, 0.0, alpha);
      break; // red
    case 1:
      cairo_set_source_rgba(cr, 0.9, 0.9, 0.0, alpha);
      break; // yellow
    case 2:
      cairo_set_source_rgba(cr, 0.0, 0.9, 0.0, alpha);
      break; // green
    case 3:
      cairo_set_source_rgba(cr, 0.0, 0.0, 0.9, alpha);
      break; // blue
    case 4:
      cairo_set_source_rgba(cr, 0.9, 0.0, 0.9, alpha);
      break; // magenta
    case 7:
      // don't fill
      cairo_set_source_rgba(cr, 0, 0, 0, 0);
      break;
    default:
      cairo_set_source_rgba(cr, 0.75, 0.75, 0.75, alpha);
      def = TRUE;
      break; // gray
  }
  cairo_fill(cr);

  /* draw cross overlay if highlighted */
  if(def == TRUE && (flags & CPF_PRELIGHT))
  {
    cairo_set_line_width(cr, 0.15);
    cairo_set_source_rgba(cr, 0.5, 0.0, 0.0, 0.8);
    cairo_move_to(cr, 0.0, 0.0);
    cairo_line_to(cr, 1.0, 1.0);
    cairo_move_to(cr, 0.9, 0.1);
    cairo_line_to(cr, 0.1, 0.9);
    cairo_stroke(cr);
  }
}

void dtgtk_cairo_paint_local_copy(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  if(!flags) return;

  const gint s = (w < h ? w : h);
  double r = 0.4;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  /* fill base color */
  cairo_arc(cr, 0.5, 0.5, r, 0.0, 2.0 * M_PI);

  cairo_set_source_rgba(cr, 1, 1, 1, 1);
  cairo_fill(cr);

  /* draw outline */
  cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.5);
  cairo_set_line_width(cr, 0.1);
  cairo_arc(cr, 0.5, 0.5, r, 0.0, 2.0 * M_PI);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_colorpicker(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = (w < h ? w : h);
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  /* draw pipette */

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  // drop
  cairo_set_line_width(cr, 0.15);
  cairo_move_to(cr, 0.08, 1. - 0.01);
  cairo_line_to(cr, 0.08, 1. - 0.09);
  cairo_stroke(cr);

  cairo_set_line_width(cr, 0.2);
  // cross line
  cairo_move_to(cr, 0.48, 1. - 0.831);
  cairo_line_to(cr, 0.739, 1. - 0.482);
  // shaft
  cairo_move_to(cr, 0.124, 1. - 0.297);
  cairo_line_to(cr, 0.823, 1. - 0.814);
  cairo_stroke(cr);

  // end
  cairo_set_line_width(cr, 0.35);
  cairo_move_to(cr, 0.823, 1. - 0.814);
  cairo_line_to(cr, 0.648, 1. - 0.685);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_colorpicker_set_values(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = (w < h ? w : h);
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  /* draw pipette */

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  // drop
  cairo_set_line_width(cr, 0.15);
  cairo_move_to(cr, 0.08, 1. - 0.01 + 0.05);
  cairo_line_to(cr, 0.08, 1. - 0.09 + 0.05);
  cairo_stroke(cr);

  cairo_set_line_width(cr, 0.2);
  // cross line
  cairo_move_to(cr, 0.48, 1. - 0.831 + 0.05);
  cairo_line_to(cr, 0.739, 1. - 0.482 + 0.05);
  // shaft
  cairo_move_to(cr, 0.124, 1. - 0.297 + 0.05);
  cairo_line_to(cr, 0.823, 1. - 0.814 + 0.05);
  cairo_stroke(cr);

  // end
  cairo_set_line_width(cr, 0.35);
  cairo_move_to(cr, 0.823, 1. - 0.814 + 0.05);
  cairo_line_to(cr, 0.648, 1. - 0.685 + 0.05);
  cairo_stroke(cr);

  // plus sign
  cairo_set_line_width(cr, 0.2);
  cairo_move_to(cr, 0.20, 0.01);
  cairo_line_to(cr, 0.20, 0.41);
  cairo_stroke(cr);
  cairo_move_to(cr, 0.01, 0.20);
  cairo_line_to(cr, 0.41, 0.20);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_showmask(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = (w < h ? w : h);
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_width(cr, 0.1);

  /* draw rectangle */
  cairo_rectangle(cr, 0.0, 0.0, 1.0, 1.0);
  cairo_fill(cr);
  cairo_stroke(cr);


  /* draw circle */
  cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 1.0);
  cairo_arc(cr, 0.5, 0.5, 0.30, -M_PI, M_PI);
  cairo_fill(cr);
}



void dtgtk_cairo_paint_preferences(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = (w < h ? w / 1.75 : h / 1.75);
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, .25);
  cairo_arc(cr, 0.5, 0.5, 0.6, 0., 2.0f * M_PI);
  cairo_stroke(cr);

  double dashes = .35;
  cairo_set_dash(cr, &dashes, 1, 0);
  cairo_arc(cr, 0.5, 0.5, 0.8, 0., 2.0f * M_PI);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_overlays(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = (w < h ? w / 1.75 : h / 1.75);
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_width(cr, .1);

  dt_draw_star(cr, 0.5, 0.5, 1.0, 1.0/2.5);

  cairo_stroke(cr);
}

void dtgtk_cairo_paint_help(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  PangoLayout *layout;
  PangoRectangle ink;
  // grow is needed because ink.* are int and everything gets rounded to 1 or so otherwise,
  // leading to imprecise positioning
  static const float grow = 12.0;
  layout = pango_cairo_create_layout(cr);
  const gint s = (w < h ? w : h);
  cairo_translate(cr, x + (w / 2.0), y + (h / 2.0));
  cairo_scale(cr, s / grow, s / grow);

  pango_layout_set_text(layout, "?", -1);
  pango_layout_get_pixel_extents(layout, &ink, NULL);
  cairo_move_to(cr, 0 - ink.x - ink.width / 2.0, 0 - ink.y - ink.height / 2.0);
  pango_cairo_show_layout(cr, layout);
  g_object_unref(layout);
}

// TODO: Find better icon
void dtgtk_cairo_paint_grouping(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  PangoLayout *layout;
  PangoRectangle ink;
  // grow is needed because ink.* are int and everything gets rounded to 1 or so otherwise,
  // leading to imprecise positioning
  static const float grow = 12.0;
  layout = pango_cairo_create_layout(cr);
  const gint s = (w < h ? w : h);
  cairo_translate(cr, x + (w / 2.0), y + (h / 2.0));
  cairo_scale(cr, s / grow, s / grow);

  pango_layout_set_text(layout, "G", -1);
  pango_layout_get_pixel_extents(layout, &ink, NULL);
  cairo_move_to(cr, 0 - ink.x - ink.width / 2.0, 0 - ink.y - ink.height / 2.0);
  pango_cairo_show_layout(cr, layout);
  g_object_unref(layout);
}

void dtgtk_cairo_paint_alignment(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, 0.3);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  switch(flags >> 13)
  {
    case 1: // Top left
      cairo_move_to(cr, 0.9, 0.1);
      cairo_line_to(cr, 0.1, 0.1);
      cairo_line_to(cr, 0.1, 0.9);
      break;

    case 2: // Top center
      cairo_move_to(cr, 0.1, 0.1);
      cairo_line_to(cr, 0.9, 0.1);
      break;

    case 4: // Top right
      cairo_move_to(cr, 0.1, 0.1);
      cairo_line_to(cr, 0.9, 0.1);
      cairo_line_to(cr, 0.9, 0.9);
      break;

    case 8: // left
      cairo_move_to(cr, 0.1, 0.1);
      cairo_line_to(cr, 0.1, 0.9);
      break;
    case 16: // center
      cairo_move_to(cr, 0.1, 0.5);
      cairo_line_to(cr, 0.9, 0.5);
      cairo_move_to(cr, 0.5, 0.1);
      cairo_line_to(cr, 0.5, 0.9);
      break;
    case 32: // right
      cairo_move_to(cr, 0.9, 0.1);
      cairo_line_to(cr, 0.9, 0.9);
      break;

    case 64: // bottom left
      cairo_move_to(cr, 0.9, 0.9);
      cairo_line_to(cr, 0.1, 0.9);
      cairo_line_to(cr, 0.1, 0.1);
      break;

    case 128: // bottom center
      cairo_move_to(cr, 0.1, 0.9);
      cairo_line_to(cr, 0.9, 0.9);
      break;

    case 256: // bottom right
      cairo_move_to(cr, 0.1, 0.9);
      cairo_line_to(cr, 0.9, 0.9);
      cairo_line_to(cr, 0.9, 0.1);
      break;
  }
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_or(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, 0.2);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  cairo_move_to(cr, 0.1, 0.3);
  cairo_curve_to(cr, 0.1, 1.1, 0.9, 1.1, 0.9, 0.3);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_and(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, 0.2);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  cairo_move_to(cr, 0.1, 0.9);
  cairo_curve_to(cr, 0.1, 0.1, 0.9, 0.1, 0.9, 0.9);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_andnot(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, 0.2);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  cairo_move_to(cr, 0.1, 0.1);
  cairo_line_to(cr, 0.9, 0.9);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_dropdown(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, 0.2);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  cairo_move_to(cr, 0.1, 0.3);
  cairo_line_to(cr, 0.5, 0.7);
  cairo_line_to(cr, 0.9, 0.3);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_bracket(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_width(cr, 0.012);
  cairo_rectangle(cr, 0.05, 0.05, 0.45, 0.45);
  cairo_stroke(cr);
  cairo_set_line_width(cr, 0.025);
  cairo_rectangle(cr, 0.55, 0.05, 0.45, 0.45);
  cairo_stroke(cr);
  cairo_set_line_width(cr, 0.05);
  cairo_rectangle(cr, 0.05, 0.55, 0.45, 0.45);
  cairo_stroke(cr);
  cairo_set_line_width(cr, 0.1);
  cairo_rectangle(cr, 0.55, 0.55, 0.45, 0.45);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_lock(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.) - (s / 2.), y + (h / 2.) - (s / 2.));
  cairo_scale(cr, s, s);

  // Adding the lock body
  cairo_rectangle(cr, 0.25, 0.5, .5, .45);
  cairo_fill(cr);

  // Adding the lock shank
  cairo_set_line_width(cr, 0.2);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
  cairo_translate(cr, .5, .5);
  cairo_scale(cr, .2, .4);
  cairo_arc(cr, 0, 0, 1, M_PI, 0);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_check_mark(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.) - (s / 2.), y + (h / 2.) - (s / 2.));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, 0.1);
  cairo_move_to(cr, 0.20, 0.45);
  cairo_line_to(cr, 0.45, 0.90);
  cairo_line_to(cr, 0.90, 0.20);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_overexposed(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  float line_width = 0.1;

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_width(cr, line_width);

  /* the triangle */
  cairo_move_to(cr, 1.0 - (line_width / 2.0), (line_width / 2.0));
  cairo_line_to(cr, (line_width / 2.0), 1.0 - (line_width / 2.0));
  cairo_line_to(cr, 1.0 - (line_width / 2.0), 1.0 - (line_width / 2.0));
  cairo_fill(cr);

  /* outer rect */
  cairo_rectangle(cr, (line_width / 2.0), (line_width / 2.0), 1.0 - line_width, 1.0 - line_width);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_rawoverexposed(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const float alpha = (flags & CPF_ACTIVE ? 1.0 : 0.4);
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  float line_width = 0.1;

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_width(cr, line_width);

  cairo_save(cr);

  const double step = ((line_width / 2.0) + (1.0 - line_width) / 2.0);

  // draw 4 CFA-like colored squares

  cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, alpha); // red
  cairo_rectangle(cr, (line_width / 2.0), (line_width / 2.0), step, step);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 0.0, 1.0, 0.0, alpha); // green
  cairo_rectangle(cr, step, (line_width / 2.0), step, step);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 0.0, 1.0, 0.0, alpha); // green
  cairo_rectangle(cr, (line_width / 2.0), step, step, step);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 0.0, 0.0, 1.0, alpha); // blue
  cairo_rectangle(cr, step, step, step, step);
  cairo_fill(cr);

  cairo_restore(cr);

  /* outer rect */
  cairo_rectangle(cr, (line_width / 2.0), (line_width / 2.0), 1.0 - line_width, 1.0 - line_width);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_gamut_check(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.) - (s / 2.), y + (h / 2.) - (s / 2.));
  cairo_scale(cr, s, s);

  cairo_save(cr);
  cairo_set_line_width(cr, 0.1);

  // the triangle
  cairo_move_to(cr, 0.0, 1 - 0.067);
  cairo_line_to(cr, 0.5, 1 - 0.933);
  cairo_line_to(cr, 1.0, 1 - 0.067);
  cairo_close_path(cr);

  // exclamation mark
  // the dot
  cairo_new_sub_path(cr);
  cairo_move_to(cr, 0.42, 1 - 0.11);
  cairo_line_to(cr, 0.42, 1 - 0.25);
  cairo_line_to(cr, 0.58, 1 - 0.25);
  cairo_line_to(cr, 0.58, 1 - 0.11);
  cairo_close_path(cr);

  // the line
  cairo_new_sub_path(cr);
  cairo_move_to(cr, 0.447, 1 - 0.29);
  cairo_line_to(cr, 0.415, 1 - 0.552);
  cairo_line_to(cr, 0.415, 1 - 0.683);
  cairo_line_to(cr, 0.585, 1 - 0.683);
  cairo_line_to(cr, 0.585, 1 - 0.552);
  cairo_line_to(cr, 0.552, 1 - 0.29);
  cairo_close_path(cr);

  cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
  cairo_fill(cr);

  cairo_restore(cr);
}

void dtgtk_cairo_paint_softproof(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.) - (s / 2.), y + (h / 2.) - (s / 2.));
  cairo_scale(cr, s, s);

  cairo_save(cr);
  cairo_set_line_width(cr, 0.1);

  // the horse shoe
  cairo_move_to(cr, 0.30, 1 - 0.0);
  cairo_curve_to(cr, 0.14, 1 - 0.09, 0.03, 1 - 0.88, 0.18, 1 - 1);
  cairo_curve_to(cr, 0.32, 1 - 1.08, 0.69, 1 - 0.63, 0.97, 1 - 0.32);
  cairo_close_path(cr);

  // triangle
  cairo_new_sub_path(cr);
  cairo_move_to(cr, 0.28, 1 - 0.07);
  cairo_line_to(cr, 0.37, 1 - 0.75);
  cairo_line_to(cr, 0.82, 1 - 0.42);
  cairo_close_path(cr);

  cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
  cairo_fill(cr);

  cairo_restore(cr);
}

void dtgtk_cairo_paint_display(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_save(cr);

  cairo_translate(cr, x + (w / 2.) - (s / 2.), y + (h / 2.) - (s / 2.));
  cairo_scale(cr, s, s);
  cairo_scale(cr, 1, -1);
  cairo_translate(cr, 0, -1);

  cairo_move_to(cr, 0.0, 0.98);
  cairo_line_to(cr, 1.0, 0.98);
  cairo_line_to(cr, 1.0, 0.28);
  cairo_line_to(cr, 0.58, 0.28);
  cairo_line_to(cr, 0.58, 0.13);
  cairo_line_to(cr, 0.85, 0.13);
  cairo_line_to(cr, 0.85, 0.03);
  cairo_line_to(cr, 0.15, 0.03);
  cairo_line_to(cr, 0.15, 0.13);
  cairo_line_to(cr, 0.42, 0.13);
  cairo_line_to(cr, 0.42, 0.28);
  cairo_line_to(cr, 0.0, 0.28);
  cairo_close_path(cr);

  cairo_move_to(cr, 0.1, 0.88);
  cairo_line_to(cr, 0.9, 0.88);
  cairo_line_to(cr, 0.9, 0.38);
  cairo_line_to(cr, 0.1, 0.38);
  cairo_close_path(cr);

  cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
  cairo_fill(cr);

  cairo_restore(cr);
}

void dtgtk_cairo_paint_display2(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_save(cr);

  cairo_translate(cr, x + (w / 2.) - (s / 2.), y + (h / 2.) - (s / 2.));
  cairo_scale(cr, s, s);
  cairo_scale(cr, 1, -1);
  cairo_translate(cr, 0, -1);

  double offset = 0.1;

  for(int i = 0; i < 2; i++)
  {
    cairo_move_to(cr, 0.0 + offset, 0.98 + offset);
    cairo_line_to(cr, 1.0 + offset, 0.98 + offset);
    cairo_line_to(cr, 1.0 + offset, 0.28 + offset);
    cairo_line_to(cr, 0.58 + offset, 0.28 + offset);
    cairo_line_to(cr, 0.58 + offset, 0.13 + offset);
    cairo_line_to(cr, 0.85 + offset, 0.13 + offset);
    cairo_line_to(cr, 0.85 + offset, 0.03 + offset);
    cairo_line_to(cr, 0.15 + offset, 0.03 + offset);
    cairo_line_to(cr, 0.15 + offset, 0.13 + offset);
    cairo_line_to(cr, 0.42 + offset, 0.13 + offset);
    cairo_line_to(cr, 0.42 + offset, 0.28 + offset);
    cairo_line_to(cr, 0.0 + offset, 0.28 + offset);
    cairo_close_path(cr);

    cairo_move_to(cr, 0.1 + offset, 0.88 + offset);
    cairo_line_to(cr, 0.9 + offset, 0.88 + offset);
    cairo_line_to(cr, 0.9 + offset, 0.38 + offset);
    cairo_line_to(cr, 0.1 + offset, 0.38 + offset);
    cairo_close_path(cr);

    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_fill(cr);

    offset = -0.1;
  }

  cairo_restore(cr);
}

void dtgtk_cairo_paint_rect_landscape(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_width(cr, 0.10);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, 0.0, 0.3);
  cairo_line_to(cr, 1.0, 0.3);
  cairo_line_to(cr, 1.0, 0.7);
  cairo_line_to(cr, 0.0, 0.7);
  cairo_line_to(cr, 0.0, 0.3);
  cairo_stroke(cr);

  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_rect_portrait(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_width(cr, 0.10);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, 0.3, 0.0);
  cairo_line_to(cr, 0.7, 0.0);
  cairo_line_to(cr, 0.7, 1.0);
  cairo_line_to(cr, 0.3, 1.0);
  cairo_line_to(cr, 0.3, 0.0);
  cairo_stroke(cr);

  cairo_identity_matrix(cr);
}

void dtgtk_cairo_paint_zoom(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = (w < h ? w : h);
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  /* draw magnifying glass */

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  // handle
  cairo_set_line_width(cr, 0.15);
  cairo_move_to(cr, 0.9, 1.0 - 0.1);
  cairo_line_to(cr, 0.65, 1.0 - 0.35);
  cairo_stroke(cr);

  // lens
  cairo_set_line_width(cr, 0.1);
  cairo_arc(cr, 0.35, 1.0 - 0.65, 0.3, -M_PI, M_PI);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_multiinstance(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  cairo_save(cr);
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);
  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_rectangle(cr, 0.35, 0.35, 0.6, 0.6);
  cairo_stroke(cr);
  cairo_rectangle(cr, 0.05, 0.05, 0.9, 0.9);
  cairo_rectangle(cr, 0.85, 0.25, -0.65, 0.65);
  cairo_clip(cr);
  cairo_rectangle(cr, 0.05, 0.05, 0.6, 0.6);
  cairo_stroke_preserve(cr);
  cairo_fill(cr);
  cairo_restore(cr);
}

void dtgtk_cairo_paint_modulegroup_active(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.0) - (s / 2.0), y + (h / 2.0) - (s / 2.0));
  cairo_scale(cr, s, s);

  cairo_set_line_width(cr, 0.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  cairo_arc(cr, 0.5, 0.5, 0.40, (-50 * 3.145 / 180), (230 * 3.145 / 180));
  cairo_move_to(cr, 0.5, 0.05);
  cairo_line_to(cr, 0.5, 0.40);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_modulegroup_favorites(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.) - (s / 2.), y + (h / 2.) - (s / 2.));
  cairo_scale(cr, s, s);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_width(cr, 0.1);
  const float r1 = 0.2;
  const float r2 = 0.4;
  const float d = 2.0 * M_PI * 0.1f;
  const float dx[10] = { sinf(0.0),   sinf(d),     sinf(2 * d), sinf(3 * d), sinf(4 * d),
                         sinf(5 * d), sinf(6 * d), sinf(7 * d), sinf(8 * d), sinf(9 * d) };
  const float dy[10] = { cosf(0.0),   cosf(d),     cosf(2 * d), cosf(3 * d), cosf(4 * d),
                         cosf(5 * d), cosf(6 * d), cosf(7 * d), cosf(8 * d), cosf(9 * d) };
  cairo_move_to(cr, 0.5 + r1 * dx[0], 0.5 - r1 * dy[0]);
  for(int k = 1; k < 10; k++)
    if(k & 1)
      cairo_line_to(cr, 0.5 + r2 * dx[k], 0.5 - r2 * dy[k]);
    else
      cairo_line_to(cr, 0.5 + r1 * dx[k], 0.5 - r1 * dy[k]);
  cairo_close_path(cr);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_modulegroup_basic(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.) - (s / 2.), y + (h / 2.) - (s / 2.));
  cairo_scale(cr, s, s);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_width(cr, 0.1);

  /* draw circle */
  cairo_arc(cr, 0.5, 0.5, 0.40, -M_PI, M_PI);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_modulegroup_tone(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.) - (s / 2.), y + (h / 2.) - (s / 2.));
  cairo_scale(cr, s, s);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_width(cr, 0.1);

  /* draw circle */
  cairo_arc(cr, 0.5, 0.5, 0.40, -M_PI, M_PI);
  cairo_stroke(cr);

  /* fill circle */
  cairo_pattern_t *pat = NULL;
  pat = cairo_pattern_create_linear(0, 0, 1, 0);
  cairo_pattern_add_color_stop_rgba(pat, 0, 1, 1, 1, 1);
  cairo_pattern_add_color_stop_rgba(pat, 1, 1, 1, 1, 0);
  cairo_set_source(cr, pat);
  cairo_arc(cr, 0.5, 0.5, 0.40, -M_PI, M_PI);
  cairo_fill(cr);
  cairo_pattern_destroy(pat);
}

void dtgtk_cairo_paint_modulegroup_color(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.) - (s / 2.), y + (h / 2.) - (s / 2.));
  cairo_scale(cr, s, s);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_width(cr, 0.1);

  /* draw circle */
  cairo_arc(cr, 0.5, 0.5, 0.40, -M_PI, M_PI);
  cairo_stroke(cr);

  /* fill circle */
  float a = 0.6;
  cairo_pattern_t *pat = NULL;
  pat = cairo_pattern_create_linear(0, 0, 1, 0);
  cairo_pattern_add_color_stop_rgba(pat, 0.0, 1, 0, 0, a);
  cairo_pattern_add_color_stop_rgba(pat, 0.1, 1, 0, 0, a);
  cairo_pattern_add_color_stop_rgba(pat, 0.5, 0, 1, 0, a);
  cairo_pattern_add_color_stop_rgba(pat, 0.9, 0, 0, 1, a);
  cairo_pattern_add_color_stop_rgba(pat, 1.0, 0, 0, 1, a);
  cairo_set_source(cr, pat);
  cairo_arc(cr, 0.5, 0.5, 0.40, -M_PI, M_PI);
  cairo_fill(cr);
  cairo_pattern_destroy(pat);
}

void dtgtk_cairo_paint_modulegroup_correct(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.) - (s / 2.), y + (h / 2.) - (s / 2.));
  cairo_scale(cr, s, s);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_width(cr, 0.1);

  /* draw circle */
  cairo_arc(cr, 0.42, 0.5, 0.40, 0, M_PI);
  cairo_stroke(cr);
  cairo_arc(cr, 0.58, 0.5, 0.40, M_PI, 0);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_modulegroup_effect(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.) - (s / 2.), y + (h / 2.) - (s / 2.));
  cairo_scale(cr, s, s);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_width(cr, 0.1);

  /* draw circle */
  cairo_arc(cr, 0.5, 0.5, 0.40, -M_PI, M_PI);
  cairo_stroke(cr);

  /* sparkles */
  cairo_set_line_width(cr, 0.06);

  cairo_move_to(cr, 0.378, 0.502);
  cairo_line_to(cr, 0.522, 0.549);
  cairo_line_to(cr, 0.564, 0.693);
  cairo_line_to(cr, 0.653, 0.569);
  cairo_line_to(cr, 0.802, 0.573);
  cairo_line_to(cr, 0.712, 0.449);
  cairo_line_to(cr, 0.762, 0.308);
  cairo_line_to(cr, 0.618, 0.356);
  cairo_line_to(cr, 0.500, 0.264);
  cairo_line_to(cr, 0.500, 0.417);
  cairo_close_path(cr);

  cairo_move_to(cr, 0.269, 0.717);
  cairo_line_to(cr, 0.322, 0.735);
  cairo_line_to(cr, 0.337, 0.787);
  cairo_line_to(cr, 0.370, 0.742);
  cairo_line_to(cr, 0.424, 0.743);
  cairo_line_to(cr, 0.391, 0.698);
  cairo_line_to(cr, 0.409, 0.646);
  cairo_line_to(cr, 0.357, 0.664);
  cairo_line_to(cr, 0.314, 0.630);
  cairo_line_to(cr, 0.314, 0.686);

  cairo_move_to(cr, 0.217, 0.366);
  cairo_line_to(cr, 0.271, 0.384);
  cairo_line_to(cr, 0.286, 0.437);
  cairo_line_to(cr, 0.319, 0.391);
  cairo_line_to(cr, 0.374, 0.393);
  cairo_line_to(cr, 0.341, 0.347);
  cairo_line_to(cr, 0.360, 0.295);
  cairo_line_to(cr, 0.306, 0.312);
  cairo_line_to(cr, 0.263, 0.279);
  cairo_line_to(cr, 0.263, 0.335);

  cairo_close_path(cr);

  cairo_stroke(cr);
}

void dtgtk_cairo_paint_modulegroup_all(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  gint s = w < h ? w : h;
  cairo_translate(cr, x + (w / 2.) - (s / 2.), y + (h / 2.) - (s / 2.));
  cairo_scale(cr, s, s);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_width(cr, 0.1);

  
  cairo_move_to(cr, 0.2, 0.8);
  cairo_line_to(cr, 0.5, 0.2);
  cairo_line_to(cr, 0.8, 0.8);
  cairo_move_to(cr, 0.3, 0.6);
  cairo_line_to(cr, 0.7, 0.6);
  cairo_stroke(cr);
}

void dtgtk_cairo_paint_map_pin(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  const gint s = w < h ? w : h;
  cairo_scale(cr, s, s);
  cairo_move_to(cr, 0.2, 0.0);
  cairo_line_to(cr, 0.0, 1.0);
  cairo_line_to(cr, 0.7, 0.0);
  cairo_close_path(cr);
  cairo_fill(cr);
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
