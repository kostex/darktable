/*
    This file is part of darktable,
    copyright (c) 2018 Pascal Obry.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

/*
  This API encapsulate the color picker behavior for IOP module. Providing
  4 routines (get_set, apply, reset and update, it will handle multiple
  color pickers in a module.

  A simpler version require only apply to be passed and the pciker widget when
  a single color picker is available in a module.
*/

#define ALREADY_SELECTED -1

#include <gtk/gtk.h>
#include "develop/imageop.h"

typedef enum _iop_color_picker_kind_t
{
  DT_COLOR_PICKER_POINT = 0,
  DT_COLOR_PICKER_AREA
} dt_iop_color_picker_kind_t;

typedef struct _iop_color_picker_t
{
  dt_iop_module_t *module;
  dt_iop_color_picker_kind_t kind;
  unsigned short current_picker;
  GtkWidget *colorpick;
  float pick_pos[9][2]; // last picker positions (max 9 picker per module)
  /* get and set the selected picker corresponding to button, the module must record the previous
     selected picker and return ALREADY_SELECTED if the same picker has been selected. The return
     value corresponds to the module internal picker id. */
  int (*get_set)(dt_iop_module_t *self, GtkWidget *button);
  /* apply the picked color to the selected picker (internal picker id, if multiple are available
     on the module */
  void (*apply)(dt_iop_module_t *self);
  /* update the picker icon to correspond to the current selected picker if any */
  void (*update)(dt_iop_module_t *self);
} dt_iop_color_picker_t;

/* init color picker, this must be called when all picker widgets are created */
void init_picker (dt_iop_color_picker_t *picker,
                  dt_iop_module_t *module,
                  dt_iop_color_picker_kind_t kind,
                  int (*get_set)(dt_iop_module_t *self, GtkWidget *button),
                  void (*apply)(dt_iop_module_t *self),
                  void (*update)(dt_iop_module_t *self));

/* init for a single color picker in iop, this must be called when all picker widget are created */
void init_single_picker (dt_iop_color_picker_t *picker,
                         dt_iop_module_t *module,
                         GtkWidget *colorpick,
                         dt_iop_color_picker_kind_t kind,
                         void (*apply)(dt_iop_module_t *self));

/* the color picker callback which must be used for every picker, as an example:

      g_signal_connect(G_OBJECT(g->button), "quad-pressed",
                       G_CALLBACK(dt_iop_color_picker_callback), &g->color_picker);

or for a simple togglebutton:

      g_signal_connect(G_OBJECT(g->color_picker_button), "toggled",
                       G_CALLBACK(dt_iop_color_picker_callback), &g->color_picker);
*/
void dt_iop_color_picker_callback(GtkWidget *button, dt_iop_color_picker_t *self);

/* call proxy get_set */
int dt_iop_color_picker_get_set(dt_iop_color_picker_t *picker, GtkWidget *button);
/* call proxy apply */
void dt_iop_color_picker_apply(dt_iop_color_picker_t *picker);
/* call proxy update */
void dt_iop_color_picker_update(dt_iop_color_picker_t *picker);
/* reset current color picker, and if update is TRUE also call update proxy */
void dt_iop_color_picker_reset(dt_iop_color_picker_t *picker, gboolean update);

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
