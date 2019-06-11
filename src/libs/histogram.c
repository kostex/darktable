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
#include <stdint.h>

#include "bauhaus/bauhaus.h"
#include "common/darktable.h"
#include "common/debug.h"
#include "common/image_cache.h"
#include "control/conf.h"
#include "control/control.h"
#include "develop/develop.h"
#include "gui/accelerators.h"
#include "gui/draw.h"
#include "gui/gtk.h"
#include "libs/lib.h"
#include "libs/lib_api.h"

DT_MODULE(1)

typedef struct dt_lib_histogram_t
{
  float exposure, black;
  int32_t dragging;
  int32_t button_down_x, button_down_y;
  int32_t highlight;
  gboolean red, green, blue;
  float mode_x, mode_w, red_x, green_x, blue_x;
  float color_w, button_h, button_y, button_spacing;
} dt_lib_histogram_t;

const char *name(dt_lib_module_t *self)
{
  return _("histogram");
}

const char **views(dt_lib_module_t *self)
{
  static const char *v[] = {"darkroom", "tethering", NULL};
  return v;
}

uint32_t container(dt_lib_module_t *self)
{
  return DT_UI_CONTAINER_PANEL_RIGHT_TOP;
}

int expandable(dt_lib_module_t *self)
{
  return 0;
}

int position()
{
  return 1001;
}


static void _lib_histogram_change_callback(gpointer instance, gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_control_queue_redraw_widget(self->widget);
}

static void _draw_color_toggle(cairo_t *cr, float x, float y, float width, float height, gboolean state)
{
  float border = MIN(width * .05, height * .05);
  cairo_rectangle(cr, x + border, y + border, width - 2.0 * border, height - 2.0 * border);
  cairo_fill_preserve(cr);
  if(state)
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.5);
  else
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.5);
  cairo_set_line_width(cr, border);
  cairo_stroke(cr);
}

static void _draw_mode_toggle(cairo_t *cr, float x, float y, float width, float height, int type)
{
  cairo_save(cr);
  cairo_translate(cr, x, y);

  // border
  float border = MIN(width * .05, height * .05);
  set_color(cr, darktable.bauhaus->graph_border);
  cairo_rectangle(cr, border, border, width - 2.0 * border, height - 2.0 * border);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.5);
  cairo_set_line_width(cr, border);
  cairo_stroke(cr);

  // icon
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.5);
  cairo_move_to(cr, 2.0 * border, height - 2.0 * border);
  switch(type)
  {
    case DT_DEV_HISTOGRAM_LINEAR:
      cairo_line_to(cr, width - 2.0 * border, 2.0 * border);
      cairo_stroke(cr);
      break;
    case DT_DEV_HISTOGRAM_LOGARITHMIC:
      cairo_curve_to(cr, 2.0 * border, 0.33 * height, 0.66 * width, 2.0 * border, width - 2.0 * border,
                     2.0 * border);
      cairo_stroke(cr);
      break;
    case DT_DEV_HISTOGRAM_WAVEFORM:
    {
      cairo_pattern_t *pattern;
      pattern = cairo_pattern_create_linear(0.0, 1.5 * border, 0.0, height - 3.0 * border);

      cairo_pattern_add_color_stop_rgba(pattern, 0.0, 0.0, 0.0, 0.0, 0.5);
      cairo_pattern_add_color_stop_rgba(pattern, 0.2, 0.2, 0.2, 0.2, 0.5);
      cairo_pattern_add_color_stop_rgba(pattern, 0.5, 1.0, 1.0, 1.0, 0.5);
      cairo_pattern_add_color_stop_rgba(pattern, 0.6, 1.0, 1.0, 1.0, 0.5);
      cairo_pattern_add_color_stop_rgba(pattern, 1.0, 0.2, 0.2, 0.2, 0.5);

      cairo_rectangle(cr, 1.5 * border, 1.5 * border, (width - 3.0 * border) * 0.3, height - 3.0 * border);
      cairo_set_source(cr, pattern);
      cairo_fill(cr);

      cairo_save(cr);
      cairo_scale(cr, 1, -1);
      cairo_translate(cr, 0, -height);
      cairo_rectangle(cr, 1.5 * border + (width - 3.0 * border) * 0.2, 1.5 * border,
                      (width - 3.0 * border) * 0.6, height - 3.0 * border);
      cairo_set_source(cr, pattern);
      cairo_fill(cr);
      cairo_restore(cr);

      cairo_rectangle(cr, 1.5 * border + (width - 3.0 * border) * 0.7, 1.5 * border,
                      (width - 3.0 * border) * 0.3, height - 3.0 * border);
      cairo_set_source(cr, pattern);
      cairo_fill(cr);

      cairo_pattern_destroy(pattern);
      break;
    }
  }
  cairo_restore(cr);
}

static gboolean _lib_histogram_draw_callback(GtkWidget *widget, cairo_t *crf, gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_histogram_t *d = (dt_lib_histogram_t *)self->data;

  dt_develop_t *dev = darktable.develop;
  uint32_t *hist = dev->histogram;
  float hist_max = dev->histogram_type == DT_DEV_HISTOGRAM_LINEAR ? dev->histogram_max
                                                                  : logf(1.0 + dev->histogram_max);
  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);
  int width = allocation.width, height = allocation.height;
  cairo_surface_t *cst = dt_cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  cairo_t *cr = cairo_create(cst);

  gtk_render_background(gtk_widget_get_style_context(widget), cr, 0, 0, width, height);
  cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(.5)); // borders width

  // Get the mode and color buttons position
  if(d->mode_x == 0)
  {
    d->color_w = 0.06 * width;
    d->button_spacing = 0.02 * width;
    d->button_h = 0.06 * width;
    d->button_y = d->button_spacing;
    d->mode_w = d->color_w;
    d->mode_x = width - 3 * (d->color_w + d->button_spacing) - (d->mode_w + d->button_spacing);
    d->red_x = width - 3 * (d->color_w + d->button_spacing);
    d->green_x = width - 2 * (d->color_w + d->button_spacing);
    d->blue_x = width - (d->color_w + d->button_spacing);
  }

  // TODO: probably this should move to the configure-event callback! That would be future proof if we ever
  // (again) allow to resize the side panels.
  const gint stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);

  // this code assumes that the first expose comes before the first (preview) pipe is processed and that the
  // size of the widget doesn't change!
  if(dev->histogram_waveform_width == 0)
  {
    dev->histogram_waveform = (uint32_t *)calloc(height * stride / 4, sizeof(uint32_t));
    dev->histogram_waveform_stride = stride;
    dev->histogram_waveform_height = height;
    dev->histogram_waveform_width = width;
  }

  // Draw frame and background
  cairo_save(cr);
  cairo_rectangle(cr, 0, 0, width, height);
  set_color(cr, darktable.bauhaus->graph_border);
  cairo_stroke_preserve(cr);
  set_color(cr, darktable.bauhaus->graph_bg);
  cairo_fill(cr);
  cairo_restore(cr);

  if(d->highlight == 1)
  {
    cairo_set_source_rgb(cr, .5, .5, .5);
    cairo_rectangle(cr, 0, 0, .2 * width, height);
    cairo_fill(cr);
  }
  else if(d->highlight == 2)
  {
    cairo_set_source_rgb(cr, .5, .5, .5);
    cairo_rectangle(cr, 0.2 * width, 0, width, height);
    cairo_fill(cr);
  }

  // draw grid
  set_color(cr, darktable.bauhaus->graph_grid);

  if(dev->histogram_type == DT_DEV_HISTOGRAM_WAVEFORM)
    dt_draw_waveform_lines(cr, 0, 0, width, height);
  else
    dt_draw_grid(cr, 4, 0, 0, width, height);

  // draw histogram
  if(hist_max > 0.0f)
  {
    cairo_save(cr);
    if(dev->histogram_type == DT_DEV_HISTOGRAM_WAVEFORM)
    {
      // make the color channel selector work:
      uint8_t *buf = (uint8_t *)malloc(sizeof(uint8_t) * height * stride);
      uint8_t mask[3] = { d->blue, d->green, d->red };
      memcpy(buf, dev->histogram_waveform, sizeof(uint8_t) * height * stride);
      for(int y = 0; y < height; y++)
        for(int x = 0; x < width; x++)
          for(int k = 0; k < 3; k++)
          {
            buf[y * stride + x * 4 + k] *= mask[k];
          }

      cairo_surface_t *source
          = cairo_image_surface_create_for_data(buf, CAIRO_FORMAT_ARGB32, width, height, stride);

      cairo_set_source_surface(cr, source, 0.0, 0.0);
      cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
      cairo_paint(cr);
      cairo_surface_destroy(source);
      free(buf);
    }
    else
    {
      cairo_translate(cr, 0, height);
      cairo_scale(cr, width / 255.0, -(height - 10) / hist_max);
      cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
      cairo_set_line_width(cr, 1.);
      if(d->red)
      {
        cairo_set_source_rgba(cr, 1., 0., 0., 0.5);
        dt_draw_histogram_8(cr, hist, 4, 0, dev->histogram_type == DT_DEV_HISTOGRAM_LINEAR);
      }
      if(d->green)
      {
        cairo_set_source_rgba(cr, 0., 1., 0., 0.5);
        dt_draw_histogram_8(cr, hist, 4, 1, dev->histogram_type == DT_DEV_HISTOGRAM_LINEAR);
      }
      if(d->blue)
      {
        cairo_set_source_rgba(cr, 0., 0., 1., 0.5);
        dt_draw_histogram_8(cr, hist, 4, 2, dev->histogram_type == DT_DEV_HISTOGRAM_LINEAR);
      }
      cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    }
    cairo_restore(cr);
  }

  // buttons to control the display of the histogram: linear/log, r, g, b
  if(d->highlight != 0)
  {
    _draw_mode_toggle(cr, d->mode_x, d->button_y, d->mode_w, d->button_h, dev->histogram_type);
    cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, 0.33);
    _draw_color_toggle(cr, d->red_x, d->button_y, d->color_w, d->button_h, d->red);
    cairo_set_source_rgba(cr, 0.0, 1.0, 0.0, 0.33);
    _draw_color_toggle(cr, d->green_x, d->button_y, d->color_w, d->button_h, d->green);
    cairo_set_source_rgba(cr, 0.0, 0.0, 1.0, 0.33);
    _draw_color_toggle(cr, d->blue_x, d->button_y, d->color_w, d->button_h, d->blue);
  }

  cairo_destroy(cr);
  cairo_set_source_surface(crf, cst, 0, 0);
  cairo_paint(crf);
  cairo_surface_destroy(cst);
  return TRUE;
}

static gboolean _lib_histogram_motion_notify_callback(GtkWidget *widget, GdkEventMotion *event,
                                                      gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_histogram_t *d = (dt_lib_histogram_t *)self->data;

  /* check if exposure hooks are available */
  gboolean hooks_available = dt_dev_exposure_hooks_available(darktable.develop);

  if(!hooks_available) return TRUE;

  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);
  if(d->dragging && d->highlight == 2)
  {
    float exposure = d->exposure + (event->x - d->button_down_x) * 4.0f / (float)allocation.width;
    dt_dev_exposure_set_exposure(darktable.develop, exposure);
  }
  else if(d->dragging && d->highlight == 1)
  {
    float black = d->black - (event->x - d->button_down_x) * .1f / (float)allocation.width;
    dt_dev_exposure_set_black(darktable.develop, black);
  }
  else
  {
    const float x = event->x;
    const float y = event->y;
    const float pos = x / (float)(allocation.width);


    if(pos < 0 || pos > 1.0)
      ;
    else if(x > d->mode_x && x < d->mode_x + d->mode_w && y > d->button_y && y < d->button_y + d->button_h)
    {
      d->highlight = 3;
      switch(darktable.develop->histogram_type)
      {
        case DT_DEV_HISTOGRAM_LOGARITHMIC:
          gtk_widget_set_tooltip_text(widget, _("set histogram mode to linear"));
          break;
        case DT_DEV_HISTOGRAM_LINEAR:
          gtk_widget_set_tooltip_text(widget, _("set histogram mode to waveform"));
          break;
        case DT_DEV_HISTOGRAM_WAVEFORM:
          gtk_widget_set_tooltip_text(widget, _("set histogram mode to logarithmic"));
          break;
        case DT_DEV_HISTOGRAM_N:
          g_assert_not_reached();
      }
    }
    else if(x > d->red_x && x < d->red_x + d->color_w && y > d->button_y && y < d->button_y + d->button_h)
    {
      d->highlight = 4;
      gtk_widget_set_tooltip_text(widget, d->red ? _("click to hide red channel") : _("click to show red channel"));
    }
    else if(x > d->green_x && x < d->green_x + d->color_w && y > d->button_y && y < d->button_y + d->button_h)
    {
      d->highlight = 5;
      gtk_widget_set_tooltip_text(widget, d->red ? _("click to hide green channel")
                                                 : _("click to show green channel"));
    }
    else if(x > d->blue_x && x < d->blue_x + d->color_w && y > d->button_y && y < d->button_y + d->button_h)
    {
      d->highlight = 6;
      gtk_widget_set_tooltip_text(widget, d->red ? _("click to hide blue channel") : _("click to show blue channel"));
    }
    else if(pos < 0.2)
    {
      d->highlight = 1;
      gtk_widget_set_tooltip_text(widget, _("drag to change black point,\ndoubleclick resets"));
    }
    else
    {
      d->highlight = 2;
      gtk_widget_set_tooltip_text(widget, _("drag to change exposure,\ndoubleclick resets"));
    }
    gtk_widget_queue_draw(widget);
  }
  gint x, y; // notify gtk for motion_hint.
#if GTK_CHECK_VERSION(3, 20, 0)
  gdk_window_get_device_position(gtk_widget_get_window(widget),
      gdk_seat_get_pointer(gdk_display_get_default_seat(
          gdk_window_get_display(event->window))),
      &x, &y, 0);
#else
  gdk_window_get_device_position(event->window,
                                 gdk_device_manager_get_client_pointer(
                                     gdk_display_get_device_manager(gdk_window_get_display(event->window))),
                                 &x, &y, NULL);
#endif

  return TRUE;
}

static gboolean _lib_histogram_button_press_callback(GtkWidget *widget, GdkEventButton *event,
                                                     gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_histogram_t *d = (dt_lib_histogram_t *)self->data;

  /* check if exposure hooks are available */
  gboolean hooks_available = dt_dev_exposure_hooks_available(darktable.develop);

  if(!hooks_available) return TRUE;

  if(event->type == GDK_2BUTTON_PRESS)
  {
    dt_dev_exposure_reset_defaults(darktable.develop);
  }
  else
  {
    if(d->highlight == 3) // mode button
    {
      darktable.develop->histogram_type = (darktable.develop->histogram_type + 1) % DT_DEV_HISTOGRAM_N;
      dt_conf_set_string("plugins/darkroom/histogram/mode",
                         dt_dev_histogram_type_names[darktable.develop->histogram_type]);
      // we need to reprocess the preview pipe
      if(darktable.develop->histogram_type == DT_DEV_HISTOGRAM_WAVEFORM)
      {
        darktable.develop->preview_status = DT_DEV_PIXELPIPE_DIRTY;
        darktable.develop->preview_pipe->cache_obsolete = 1;
        dt_control_queue_redraw();
      }
    }
    else if(d->highlight == 4) // red button
    {
      d->red = !d->red;
      dt_conf_set_bool("plugins/darkroom/histogram/show_red", d->red);
    }
    else if(d->highlight == 5) // green button
    {
      d->green = !d->green;
      dt_conf_set_bool("plugins/darkroom/histogram/show_green", d->green);
    }
    else if(d->highlight == 6) // blue button
    {
      d->blue = !d->blue;
      dt_conf_set_bool("plugins/darkroom/histogram/show_blue", d->blue);
    }
    else
    {
      d->dragging = 1;

      if(d->highlight == 2) d->exposure = dt_dev_exposure_get_exposure(darktable.develop);

      if(d->highlight == 1) d->black = dt_dev_exposure_get_black(darktable.develop);

      d->button_down_x = event->x;
      d->button_down_y = event->y;
    }
  }
  // update for good measure
  dt_control_queue_redraw_widget(self->widget);

  return TRUE;
}

static gboolean _lib_histogram_scroll_callback(GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_histogram_t *d = (dt_lib_histogram_t *)self->data;

  float ce = dt_dev_exposure_get_exposure(darktable.develop);
  float cb = dt_dev_exposure_get_black(darktable.develop);

  int delta_y;
  // note are using unit rather than smooth scroll events, as
  // exposure changes can get laggy if handling a multitude of smooth
  // scroll events
  if(dt_gui_get_scroll_unit_deltas(event, NULL, &delta_y))
  {
    if(d->highlight == 2)
      dt_dev_exposure_set_exposure(darktable.develop, ce - 0.15f * delta_y);
    else if(d->highlight == 1)
      dt_dev_exposure_set_black(darktable.develop, cb + 0.001f * delta_y);
  }

  return TRUE;
}

static gboolean _lib_histogram_button_release_callback(GtkWidget *widget, GdkEventButton *event,
                                                       gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_histogram_t *d = (dt_lib_histogram_t *)self->data;
  d->dragging = 0;
  return TRUE;
}

static gboolean _lib_histogram_enter_notify_callback(GtkWidget *widget, GdkEventCrossing *event,
                                                     gpointer user_data)
{
  dt_control_change_cursor(GDK_HAND1);
  return TRUE;
}

static gboolean _lib_histogram_leave_notify_callback(GtkWidget *widget, GdkEventCrossing *event,
                                                     gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_histogram_t *d = (dt_lib_histogram_t *)self->data;
  d->dragging = 0;
  d->highlight = 0;
  dt_control_change_cursor(GDK_LEFT_PTR);
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean _lib_histogram_collapse_callback(GtkAccelGroup *accel_group,
                                                GObject *acceleratable, guint keyval,
                                                GdkModifierType modifier, gpointer data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)data;

  // Get the state
  gint visible = dt_lib_is_visible(self);

  // Inverse the visibility
  dt_lib_set_visible(self, !visible);

  return TRUE;
}

void gui_init(dt_lib_module_t *self)
{
  /* initialize ui widgets */
  dt_lib_histogram_t *d = (dt_lib_histogram_t *)g_malloc0(sizeof(dt_lib_histogram_t));
  self->data = (void *)d;

  d->red = dt_conf_get_bool("plugins/darkroom/histogram/show_red");
  d->green = dt_conf_get_bool("plugins/darkroom/histogram/show_green");
  d->blue = dt_conf_get_bool("plugins/darkroom/histogram/show_blue");

  /* create drawingarea */
  self->widget = gtk_drawing_area_new();
  gtk_widget_set_name(self->widget, "main-histogram");
  dt_gui_add_help_link(self->widget, dt_get_help_url(self->plugin_name));

  gtk_widget_add_events(self->widget, GDK_LEAVE_NOTIFY_MASK | GDK_ENTER_NOTIFY_MASK | GDK_POINTER_MOTION_MASK
                                      | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                      darktable.gui->scroll_mask);

  /* connect callbacks */
  gtk_widget_set_tooltip_text(self->widget, _("drag to change exposure,\ndoubleclick resets"));
  g_signal_connect(G_OBJECT(self->widget), "draw", G_CALLBACK(_lib_histogram_draw_callback), self);
  g_signal_connect(G_OBJECT(self->widget), "button-press-event",
                   G_CALLBACK(_lib_histogram_button_press_callback), self);
  g_signal_connect(G_OBJECT(self->widget), "button-release-event",
                   G_CALLBACK(_lib_histogram_button_release_callback), self);
  g_signal_connect(G_OBJECT(self->widget), "motion-notify-event",
                   G_CALLBACK(_lib_histogram_motion_notify_callback), self);
  g_signal_connect(G_OBJECT(self->widget), "leave-notify-event",
                   G_CALLBACK(_lib_histogram_leave_notify_callback), self);
  g_signal_connect(G_OBJECT(self->widget), "enter-notify-event",
                   G_CALLBACK(_lib_histogram_enter_notify_callback), self);
  g_signal_connect(G_OBJECT(self->widget), "scroll-event", G_CALLBACK(_lib_histogram_scroll_callback), self);

  /* set size of navigation draw area */
  const int panel_width = dt_conf_get_int("panel_width");
  gtk_widget_set_size_request(self->widget, -1, panel_width * 0.5f);

  /* connect to preview pipe finished  signal */
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_DEVELOP_PREVIEW_PIPE_FINISHED,
                            G_CALLBACK(_lib_histogram_change_callback), self);
}

void gui_cleanup(dt_lib_module_t *self)
{
  /* disconnect callback from  signal */
  dt_control_signal_disconnect(darktable.signals, G_CALLBACK(_lib_histogram_change_callback), self);

  dt_develop_t *dev = darktable.develop;

  free(dev->histogram_waveform);
  dev->histogram_waveform = NULL;
  dev->histogram_waveform_stride = 0;
  dev->histogram_waveform_height = 0;
  dev->histogram_waveform_width = 0;

  g_free(self->data);
  self->data = NULL;
}

void init_key_accels(dt_lib_module_t *self)
{
  dt_accel_register_lib(self, NC_("accel", "hide histogram"), GDK_KEY_H, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
}

void connect_key_accels(dt_lib_module_t *self)
{
  dt_accel_connect_lib(self, "hide histogram",
                     g_cclosure_new(G_CALLBACK(_lib_histogram_collapse_callback), self, NULL));
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
