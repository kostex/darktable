/*
    This file is part of darktable,
    copyright (c) 2011 Henrik Andersson.

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

#include "common/colorlabels.h"
#include "control/control.h"
#include "dtgtk/button.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "libs/lib.h"
#include "libs/lib_api.h"

DT_MODULE(1)

typedef struct dt_lib_colorlabels_t
{
  char *tooltips[6];
  GtkWidget *buttons[6];
} dt_lib_colorlabels_t;

/* callback when a colorlabel button is clicked */
static void _lib_colorlabels_button_clicked_callback(GtkWidget *w, gpointer user_data);

const char *name(dt_lib_module_t *self)
{
  return _("colorlabels");
}

const char **views(dt_lib_module_t *self)
{
  static const char *v[] = {"lighttable", "tethering", NULL};
  return v;
}

uint32_t container(dt_lib_module_t *self)
{
  return DT_UI_CONTAINER_PANEL_CENTER_BOTTOM_LEFT;
}

int expandable(dt_lib_module_t *self)
{
  return 0;
}

int position()
{
  return 1001;
}

void gui_init(dt_lib_module_t *self)
{
  /* initialize ui widgets */
  dt_lib_colorlabels_t *d = (dt_lib_colorlabels_t *)g_malloc0(sizeof(dt_lib_colorlabels_t));
  self->data = (void *)d;

  /* setup list of tooltips */
  d->tooltips[0] = _("toggle red label\nof selected images");
  d->tooltips[1] = _("toggle yellow label\nof selected images");
  d->tooltips[2] = _("toggle green label\nof selected images");
  d->tooltips[3] = _("toggle blue label\nof selected images");
  d->tooltips[4] = _("toggle purple label\nof selected images");
  d->tooltips[5] = _("clear all labels of selected images");

  /* create buttons */
  self->widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget *button;
  for(int k = 0; k < 6; k++)
  {
    button = dtgtk_button_new(dtgtk_cairo_paint_label, (k | 8 | CPF_BG_TRANSPARENT | CPF_DO_NOT_USE_BORDER), NULL);
    d->buttons[k] = button;
    gtk_widget_set_size_request(button, DT_PIXEL_APPLY_DPI(16), DT_PIXEL_APPLY_DPI(16));
    gtk_widget_set_tooltip_text(button, d->tooltips[k]);
    gtk_box_pack_start(GTK_BOX(self->widget), button, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(_lib_colorlabels_button_clicked_callback),
                     GINT_TO_POINTER(k));
  }
}

void gui_cleanup(dt_lib_module_t *self)
{
  g_free(self->data);
  self->data = NULL;
}

static void _lib_colorlabels_button_clicked_callback(GtkWidget *w, gpointer user_data)
{
  dt_colorlabels_key_accel_callback(NULL, NULL, 0, 0, user_data);
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
