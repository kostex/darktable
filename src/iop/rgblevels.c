/*
    This file is part of darktable,
    copyright (c) 2019 edgardo hoszowski.

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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "bauhaus/bauhaus.h"
#include "common/colorspaces_inline_conversions.h"
#include "common/rgb_norms.h"
#include "develop/imageop.h"
#include "dtgtk/drawingarea.h"
#include "gui/color_picker_proxy.h"

#define DT_GUI_CURVE_EDITOR_INSET DT_PIXEL_APPLY_DPI(5)

DT_MODULE_INTROSPECTION(1, dt_iop_rgblevels_params_t)

#define RGBLEVELS_MIN 0.f
#define RGBLEVELS_MID 0.5f
#define RGBLEVELS_MAX 1.f

typedef enum dt_iop_rgblevels_channel_t
{
  DT_IOP_RGBLEVELS_R = 0,
  DT_IOP_RGBLEVELS_G = 1,
  DT_IOP_RGBLEVELS_B = 2,
  DT_IOP_RGBLEVELS_MAX_CHANNELS = 3
} dt_iop_rgblevels_channel_t;

typedef enum dt_iop_rgblevels_autoscale_t
{
  DT_IOP_RGBLEVELS_LINKED_CHANNELS = 0,
  DT_IOP_RGBLEVELS_INDEPENDENT_CHANNELS = 1
} dt_iop_rgblevels_autoscale_t;

typedef struct dt_iop_rgblevels_params_t
{
  int autoscale; // (DT_IOP_RGBLEVELS_INDEPENDENT_CHANNELS, DT_IOP_RGBLEVELS_LINKED_CHANNELS)
  int preserve_colors;
  float levels[DT_IOP_RGBLEVELS_MAX_CHANNELS][3];
} dt_iop_rgblevels_params_t;

typedef enum dt_iop_rgblevels_pick_t
{
  DT_IOP_RGBLEVELS_PICK_NONE,
  DT_IOP_RGBLEVELS_PICK_BLACK,
  DT_IOP_RGBLEVELS_PICK_GREY,
  DT_IOP_RGBLEVELS_PICK_WHITE
} dt_iop_rgblevels_pick_t;

typedef struct dt_iop_rgblevels_gui_data_t
{
  dt_pthread_mutex_t lock;
  dt_iop_rgblevels_params_t params;

  GtkWidget *cmb_autoscale; // (DT_IOP_RGBLEVELS_INDEPENDENT_CHANNELS, DT_IOP_RGBLEVELS_LINKED_CHANNELS)
  GtkDrawingArea *area;
  GtkWidget *cmb_preserve_colors;
  GtkNotebook *channel_tabs;
  GtkWidget *bt_auto_levels;
  GtkWidget *bt_select_region;

  int call_auto_levels;                         // should we calculate levels automatically?
  int draw_selected_region;                     // are we drawing the selected region?
  float posx_from, posx_to, posy_from, posy_to; // coordinates of the area
  float box_cood[4];                            // normalized coordinates
  int button_down;                              // user pressed the mouse button?

  double mouse_x, mouse_y;
  int dragging, handle_move;
  float drag_start_percentage;
  dt_iop_rgblevels_channel_t channel;
  dt_iop_color_picker_t color_picker;
  float last_picked_color;
  GtkWidget *blackpick, *greypick, *whitepick;
} dt_iop_rgblevels_gui_data_t;

typedef struct dt_iop_rgblevels_data_t
{
  dt_iop_rgblevels_params_t params;
  float inv_gamma[DT_IOP_RGBLEVELS_MAX_CHANNELS];
  float lut[DT_IOP_RGBLEVELS_MAX_CHANNELS][0x10000];
} dt_iop_rgblevels_data_t;

typedef struct dt_iop_rgblevels_global_data_t
{
  int kernel_levels;
} dt_iop_rgblevels_global_data_t;


const char *name()
{
  return _("rgb levels");
}

int default_group()
{
  return IOP_GROUP_TONE;
}

int flags()
{
  return IOP_FLAGS_SUPPORTS_BLENDING;
}

int default_colorspace(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  return iop_cs_rgb;
}

static void _turn_select_region_off(struct dt_iop_module_t *self)
{
  dt_iop_rgblevels_gui_data_t *g = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
  if(g)
  {
    g->button_down = g->draw_selected_region = 0;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_select_region), g->draw_selected_region);
  }
}

static void _turn_selregion_picker_off(struct dt_iop_module_t *self)
{
  _turn_select_region_off(self);
  dt_iop_color_picker_reset(self, TRUE);
}

static void _develop_ui_pipe_finished_callback(gpointer instance, dt_iop_module_t *self)
{
  dt_iop_rgblevels_params_t *p = (dt_iop_rgblevels_params_t *)self->params;
  dt_iop_rgblevels_gui_data_t *g = (dt_iop_rgblevels_gui_data_t *)self->gui_data;

  if(g == NULL) return;

  // FIXME: this doesn't seems the right place to update params and GUI ...
  // update auto levels
  dt_pthread_mutex_lock(&g->lock);
  if(g->call_auto_levels == 2)
  {
    g->call_auto_levels = -1;

    dt_pthread_mutex_unlock(&g->lock);

    memcpy(p, &g->params, sizeof(dt_iop_rgblevels_params_t));

    dt_dev_add_history_item(darktable.develop, self, TRUE);

    dt_pthread_mutex_lock(&g->lock);

    g->call_auto_levels = 0;

    dt_pthread_mutex_unlock(&g->lock);

    const int reset = darktable.gui->reset;
    darktable.gui->reset = 1;

    gui_update(self);

    darktable.gui->reset = reset;
  }
  else
  {
    dt_pthread_mutex_unlock(&g->lock);
  }
}

static void _compute_lut(dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_rgblevels_data_t *d = (dt_iop_rgblevels_data_t *)piece->data;

  // Building the lut for values in the [0,1] range
  if(d->params.autoscale == DT_IOP_RGBLEVELS_LINKED_CHANNELS)
  {
    const int c = 0;
    const float delta = (d->params.levels[c][2] - d->params.levels[c][0]) / 2.0f;
    const float mid = d->params.levels[c][0] + delta;
    const float tmp = (d->params.levels[c][1] - mid) / delta;
    d->inv_gamma[0] = d->inv_gamma[1] = d->inv_gamma[2] = pow(10, tmp);

    for(unsigned int i = 0; i < 0x10000; i++)
    {
      float percentage = (float)i / (float)0x10000ul;
      d->lut[0][i] = d->lut[1][i] = d->lut[2][i] = pow(percentage, d->inv_gamma[c]);
    }
  }
  else
  {
    for(int c = 0; c < 3; c++)
    {
      const float delta = (d->params.levels[c][2] - d->params.levels[c][0]) / 2.0f;
      const float mid = d->params.levels[c][0] + delta;
      const float tmp = (d->params.levels[c][1] - mid) / delta;
      d->inv_gamma[c] = pow(10, tmp);

      for(unsigned int i = 0; i < 0x10000; i++)
      {
        float percentage = (float)i / (float)0x10000ul;
        d->lut[c][i] = pow(percentage, d->inv_gamma[c]);
      }
    }
  }
}

static void _rgblevels_show_hide_controls(dt_iop_rgblevels_params_t *p, dt_iop_rgblevels_gui_data_t *g)
{
  switch(p->autoscale)
  {
    case DT_IOP_RGBLEVELS_INDEPENDENT_CHANNELS:
      gtk_notebook_set_show_tabs(g->channel_tabs, TRUE);
      break;
    case DT_IOP_RGBLEVELS_LINKED_CHANNELS:
      gtk_notebook_set_show_tabs(g->channel_tabs, FALSE);
      break;
  }

  if(p->autoscale == DT_IOP_RGBLEVELS_LINKED_CHANNELS)
    gtk_widget_set_visible(g->cmb_preserve_colors, TRUE);
  else
    gtk_widget_set_visible(g->cmb_preserve_colors, FALSE);
}

int mouse_moved(struct dt_iop_module_t *self, double x, double y, double pressure, int which)
{
  int handled = 0;
  dt_iop_rgblevels_gui_data_t *g = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
  if(g && g->draw_selected_region && g->button_down && self->enabled)
  {
    float pzx, pzy;
    dt_dev_get_pointer_zoom_pos(darktable.develop, x, y, &pzx, &pzy);
    pzx += 0.5f;
    pzy += 0.5f;

    g->posx_to = pzx * darktable.develop->preview_pipe->backbuf_width;
    g->posy_to = pzy * darktable.develop->preview_pipe->backbuf_height;

    dt_control_queue_redraw_center();

    handled = 1;
  }

  return handled;
}

int button_released(struct dt_iop_module_t *self, double x, double y, int which, uint32_t state)
{
  int handled = 0;
  dt_iop_rgblevels_gui_data_t *g = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
  if(g && g->draw_selected_region && self->enabled)
  {
    if(fabsf(g->posx_from - g->posx_to) > 1 && fabsf(g->posy_from - g->posy_to) > 1)
    {
      g->box_cood[0] = g->posx_from;
      g->box_cood[1] = g->posy_from;
      g->box_cood[2] = g->posx_to;
      g->box_cood[3] = g->posy_to;
      dt_dev_distort_backtransform(darktable.develop, g->box_cood, 2);
      g->box_cood[0] /= darktable.develop->preview_pipe->iwidth;
      g->box_cood[1] /= darktable.develop->preview_pipe->iheight;
      g->box_cood[2] /= darktable.develop->preview_pipe->iwidth;
      g->box_cood[3] /= darktable.develop->preview_pipe->iheight;

      g->button_down = 0;
      g->call_auto_levels = 1;

      dt_dev_reprocess_all(self->dev);
    }
    else
      g->button_down = 0;

    handled = 1;
  }

  return handled;
}

int button_pressed(struct dt_iop_module_t *self, double x, double y, double pressure, int which, int type,
                   uint32_t state)
{
  int handled = 0;
  dt_iop_rgblevels_gui_data_t *g = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
  if(g && g->draw_selected_region && self->enabled)
  {
    if((which == 3) || (which == 1 && type == GDK_2BUTTON_PRESS))
    {
      _turn_selregion_picker_off(self);

      handled = 1;
    }
    else if(which == 1)
    {
      float pzx, pzy;
      dt_dev_get_pointer_zoom_pos(darktable.develop, x, y, &pzx, &pzy);
      pzx += 0.5f;
      pzy += 0.5f;

      g->posx_from = g->posx_to = pzx * darktable.develop->preview_pipe->backbuf_width;
      g->posy_from = g->posy_to = pzy * darktable.develop->preview_pipe->backbuf_height;

      g->button_down = 1;

      handled = 1;
    }
  }

  return handled;
}

void gui_post_expose(struct dt_iop_module_t *self, cairo_t *cr, int32_t width, int32_t height, int32_t pointerx,
                     int32_t pointery)
{
  dt_iop_rgblevels_gui_data_t *g = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
  if(g == NULL || !self->enabled) return;
  if(!g->draw_selected_region || !g->button_down) return;
  if(g->posx_from == g->posx_to && g->posy_from == g->posy_to) return;

  dt_develop_t *dev = darktable.develop;
  const float wd = dev->preview_pipe->backbuf_width;
  const float ht = dev->preview_pipe->backbuf_height;
  const float zoom_y = dt_control_get_dev_zoom_y();
  const float zoom_x = dt_control_get_dev_zoom_x();
  const dt_dev_zoom_t zoom = dt_control_get_dev_zoom();
  const int closeup = dt_control_get_dev_closeup();
  const float zoom_scale = dt_dev_get_zoom_scale(dev, zoom, 1 << closeup, 1);

  const float posx_from = fmin(g->posx_from, g->posx_to);
  const float posx_to = fmax(g->posx_from, g->posx_to);
  const float posy_from = fmin(g->posy_from, g->posy_to);
  const float posy_to = fmax(g->posy_from, g->posy_to);

  cairo_save(cr);
  cairo_set_line_width(cr, 1.0 / zoom_scale);
  cairo_set_source_rgb(cr, .2, .2, .2);

  cairo_translate(cr, width / 2.0, height / 2.0f);
  cairo_scale(cr, zoom_scale, zoom_scale);
  cairo_translate(cr, -.5f * wd - zoom_x * wd, -.5f * ht - zoom_y * ht);

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  cairo_rectangle(cr, posx_from, posy_from, (posx_to - posx_from), (posy_to - posy_from));
  cairo_stroke(cr);
  cairo_translate(cr, 1.0 / zoom_scale, 1.0 / zoom_scale);
  cairo_set_source_rgb(cr, .8, .8, .8);
  cairo_rectangle(cr, posx_from + 1.0 / zoom_scale, posy_from, (posx_to - posx_from) - 3. / zoom_scale,
                  (posy_to - posy_from) - 2. / zoom_scale);
  cairo_stroke(cr);

  cairo_restore(cr);
}

static gboolean _area_leave_notify_callback(GtkWidget *widget, GdkEventCrossing *event, dt_iop_module_t *self)
{
  dt_iop_rgblevels_gui_data_t *c = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
  c->mouse_x = c->mouse_y = -1.0;
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean _area_draw_callback(GtkWidget *widget, cairo_t *crf, dt_iop_module_t *self)
{
  dt_iop_rgblevels_gui_data_t *c = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
  dt_iop_rgblevels_params_t *p = (dt_iop_rgblevels_params_t *)self->params;

  dt_develop_t *dev = darktable.develop;
  const int inset = DT_GUI_CURVE_EDITOR_INSET;
  GtkAllocation allocation;
  gtk_widget_get_allocation(GTK_WIDGET(c->area), &allocation);
  int width = allocation.width, height = allocation.height;
  cairo_surface_t *cst = dt_cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  cairo_t *cr = cairo_create(cst);

  // clear bg
  cairo_set_source_rgb(cr, .2, .2, .2);
  cairo_paint(cr);

  cairo_translate(cr, inset, inset);
  width -= 2 * inset;
  height -= 2 * inset;

  cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(1.0));
  cairo_set_source_rgb(cr, .1, .1, .1);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_stroke(cr);

  cairo_set_source_rgb(cr, .3, .3, .3);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_fill(cr);

  // draw grid
  cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(.4));
  cairo_set_source_rgb(cr, .1, .1, .1);
  dt_draw_vertical_lines(cr, 4, 0, 0, width, height);

  // Drawing the vertical line indicators
  cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(2.));

  for(int k = 0; k < 3; k++)
  {
    if(k == c->handle_move && c->mouse_x > 0)
      cairo_set_source_rgb(cr, 1, 1, 1);
    else
      cairo_set_source_rgb(cr, .7, .7, .7);

    cairo_move_to(cr, width * p->levels[c->channel][k], height);
    cairo_rel_line_to(cr, 0, -height);
    cairo_stroke(cr);
  }

  // draw x positions
  cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(1.));
  const float arrw = DT_PIXEL_APPLY_DPI(7.0f);
  for(int k = 0; k < 3; k++)
  {
    switch(k)
    {
      case 0:
        cairo_set_source_rgb(cr, 0, 0, 0);
        break;

      case 1:
        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
        break;

      default:
        cairo_set_source_rgb(cr, 1, 1, 1);
        break;
    }

    cairo_move_to(cr, width * p->levels[c->channel][k], height + inset - 1);
    cairo_rel_line_to(cr, -arrw * .5f, 0);
    cairo_rel_line_to(cr, arrw * .5f, -arrw);
    cairo_rel_line_to(cr, arrw * .5f, arrw);
    cairo_close_path(cr);
    if(c->handle_move == k && c->mouse_x > 0)
      cairo_fill(cr);
    else
      cairo_stroke(cr);
  }

  cairo_translate(cr, 0, height);

  // draw histogram in background
  // only if the module is enabled
  if(self->enabled)
  {
    const int ch = c->channel;
    const uint32_t *hist = self->histogram;
    const float hist_max = dev->histogram_type == DT_DEV_HISTOGRAM_LINEAR ? self->histogram_max[ch]
                                                                          : logf(1.0 + self->histogram_max[ch]);
    if(hist && hist_max > 0.0f)
    {
      cairo_save(cr);
      cairo_scale(cr, width / 255.0, -(height - DT_PIXEL_APPLY_DPI(5)) / hist_max);

      if(p->autoscale == DT_IOP_RGBLEVELS_LINKED_CHANNELS)
      {
        cairo_set_operator(cr, CAIRO_OPERATOR_ADD);

        cairo_set_source_rgba(cr, 1., 0., 0., 0.2);
        dt_draw_histogram_8(cr, hist, 4, 0, dev->histogram_type == DT_DEV_HISTOGRAM_LINEAR);

        cairo_set_source_rgba(cr, 0., 1., 0., 0.2);
        dt_draw_histogram_8(cr, hist, 4, 1, dev->histogram_type == DT_DEV_HISTOGRAM_LINEAR);

        cairo_set_source_rgba(cr, 0., 0., 1., 0.2);
        dt_draw_histogram_8(cr, hist, 4, 2, dev->histogram_type == DT_DEV_HISTOGRAM_LINEAR);
      }
      else if(p->autoscale == DT_IOP_RGBLEVELS_INDEPENDENT_CHANNELS)
      {
        if(ch == DT_IOP_RGBLEVELS_R)
          cairo_set_source_rgba(cr, 1., 0., 0., 0.2);
        else if(ch == DT_IOP_RGBLEVELS_G)
          cairo_set_source_rgba(cr, 0., 1., 0., 0.2);
        else
          cairo_set_source_rgba(cr, 0., 0., 1., 0.2);
        dt_draw_histogram_8(cr, hist, 4, ch, dev->histogram_type == DT_DEV_HISTOGRAM_LINEAR);
      }

      cairo_restore(cr);
    }
  }

  // Cleaning up
  cairo_destroy(cr);
  cairo_set_source_surface(crf, cst, 0, 0);
  cairo_paint(crf);
  cairo_surface_destroy(cst);
  return TRUE;
}

static void _rgblevels_move_handle(dt_iop_module_t *self, const int handle_move, const float new_pos, float *levels,
                                      const float drag_start_percentage)
{
  dt_iop_rgblevels_gui_data_t *c = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
  float min_x = 0.f;
  float max_x = 1.f;

  if((handle_move < 0) || handle_move > 2) return;

  if(levels == NULL) return;

  // Determining the minimum and maximum bounds for the drag handles
  switch(handle_move)
  {
    case 0:
      max_x = fminf(levels[2] - (0.05 / drag_start_percentage), 1);
      max_x = fminf((levels[2] * (1 - drag_start_percentage) - 0.05) / (1 - drag_start_percentage), max_x);
      break;

    case 1:
      min_x = levels[0] + 0.05;
      max_x = levels[2] - 0.05;
      break;

    case 2:
      min_x = fmaxf((0.05 / drag_start_percentage) + levels[0], 0);
      min_x = fmaxf((levels[0] * (1 - drag_start_percentage) + 0.05) / (1 - drag_start_percentage), min_x);
      break;
  }

  levels[handle_move] = fminf(max_x, fmaxf(min_x, new_pos));

  if(handle_move != 1) levels[1] = levels[0] + (drag_start_percentage * (levels[2] - levels[0]));

  c->last_picked_color = -1;
}

static gboolean _area_motion_notify_callback(GtkWidget *widget, GdkEventMotion *event, dt_iop_module_t *self)
{
  dt_iop_rgblevels_gui_data_t *c = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
  dt_iop_rgblevels_params_t *p = (dt_iop_rgblevels_params_t *)self->params;
  const int inset = DT_GUI_CURVE_EDITOR_INSET;
  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);
  int height = allocation.height - 2 * inset, width = allocation.width - 2 * inset;
  if(!c->dragging)
  {
    c->mouse_x = CLAMP(event->x - inset, 0, width);
    c->drag_start_percentage = (p->levels[c->channel][1] - p->levels[c->channel][0]) / (p->levels[c->channel][2] - p->levels[c->channel][0]);
  }
  c->mouse_y = CLAMP(event->y - inset, 0, height);

  if(c->dragging)
  {
    if(c->handle_move >= 0 && c->handle_move < 3)
    {
      const float mx = (CLAMP(event->x - inset, 0, width)) / (float)width;

      _rgblevels_move_handle(self, c->handle_move, mx, p->levels[c->channel], c->drag_start_percentage);
    }
    dt_dev_add_history_item(darktable.develop, self, TRUE);
  }
  else
  {
    c->handle_move = 0;
    const float mx = CLAMP(event->x - inset, 0, width) / (float)width;
    float dist = fabsf(p->levels[c->channel][0] - mx);
    for(int k = 1; k < 3; k++)
    {
      float d2 = fabsf(p->levels[c->channel][k] - mx);
      if(d2 < dist)
      {
        c->handle_move = k;
        dist = d2;
      }
    }
  }
  gtk_widget_queue_draw(widget);

  return TRUE;
}

static gboolean _area_button_press_callback(GtkWidget *widget, GdkEventButton *event, dt_iop_module_t *self)
{
  // set active point
  if(event->button == 1)
  {
    if(darktable.develop->gui_module != self) dt_iop_request_focus(self);

    if(event->type == GDK_2BUTTON_PRESS)
    {
      _turn_selregion_picker_off(self);

      // Reset
      dt_iop_rgblevels_gui_data_t *c = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
      dt_iop_rgblevels_params_t *p = (dt_iop_rgblevels_params_t *)self->params;
      dt_iop_rgblevels_params_t *default_params = (dt_iop_rgblevels_params_t *)self->default_params;

      for(int i = 0; i < 3; i++)
        p->levels[c->channel][i] = default_params->levels[c->channel][i];

      // Needed in case the user scrolls or drags immediately after a reset,
      // as drag_start_percentage is only updated when the mouse is moved.
      c->drag_start_percentage = 0.5;
      dt_dev_add_history_item(darktable.develop, self, TRUE);
      gtk_widget_queue_draw(self->widget);
    }
    else
    {
      _turn_selregion_picker_off(self);

      dt_iop_rgblevels_gui_data_t *c = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
      c->dragging = 1;
    }
    return TRUE;
  }
  return FALSE;
}

static gboolean _area_button_release_callback(GtkWidget *widget, GdkEventButton *event, dt_iop_module_t *self)
{
  if(event->button == 1)
  {
    dt_iop_rgblevels_gui_data_t *c = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
    c->dragging = 0;
    return TRUE;
  }
  return FALSE;
}

static gboolean _area_scroll_callback(GtkWidget *widget, GdkEventScroll *event, dt_iop_module_t *self)
{
  dt_iop_rgblevels_gui_data_t *c = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
  dt_iop_rgblevels_params_t *p = (dt_iop_rgblevels_params_t *)self->params;

  if(((event->state & gtk_accelerator_get_default_mod_mask()) == darktable.gui->sidebar_scroll_mask) != dt_conf_get_bool("darkroom/ui/sidebar_scroll_default")) return FALSE;
  _turn_selregion_picker_off(self);

  if(c->dragging)
  {
    return FALSE;
  }

  if(darktable.develop->gui_module != self) dt_iop_request_focus(self);

  const float interval = 0.002; // Distance moved for each scroll event
  gdouble delta_y;
  if(dt_gui_get_scroll_deltas(event, NULL, &delta_y))
  {
    const float new_position = p->levels[c->channel][c->handle_move] - interval * delta_y;
    _rgblevels_move_handle(self, c->handle_move, new_position, p->levels[c->channel], c->drag_start_percentage);
    dt_dev_add_history_item(darktable.develop, self, TRUE);
    return TRUE;
  }

  return FALSE;
}

static void _auto_levels_callback(GtkButton *button, dt_iop_module_t *self)
{
  if(darktable.gui->reset) return;

  dt_iop_rgblevels_gui_data_t *g = (dt_iop_rgblevels_gui_data_t *)self->gui_data;

  dt_iop_request_focus(self);
  if(self->off)
  {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->off), 1);
    dt_dev_add_history_item(darktable.develop, self, TRUE);
  }

  _turn_selregion_picker_off(self);

  dt_pthread_mutex_lock(&g->lock);
  if(g->call_auto_levels == 0)
  {
    g->box_cood[0] = g->box_cood[1] = g->box_cood[2] = g->box_cood[3] = 0.f;
    g->call_auto_levels = 1;
  }
  dt_pthread_mutex_unlock(&g->lock);

  dt_dev_reprocess_all(self->dev);
}

static void _select_region_toggled_callback(GtkToggleButton *togglebutton, dt_iop_module_t *self)
{
  if(darktable.gui->reset) return;

  dt_iop_rgblevels_gui_data_t *g = (dt_iop_rgblevels_gui_data_t *)self->gui_data;

  dt_iop_request_focus(self);
  if(self->off)
  {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->off), 1);
    dt_dev_add_history_item(darktable.develop, self, TRUE);
  }

  dt_iop_color_picker_reset(self, TRUE);

  dt_pthread_mutex_lock(&g->lock);

  if(gtk_toggle_button_get_active(togglebutton))
  {
    g->draw_selected_region = 1;
  }
  else
    g->draw_selected_region = 0;

  g->posx_from = g->posx_to = g->posy_from = g->posy_to = 0;

  dt_pthread_mutex_unlock(&g->lock);
}

static void _autoscale_callback(GtkWidget *widget, dt_iop_module_t *self)
{
  if(darktable.gui->reset) return;
  dt_iop_rgblevels_gui_data_t *g = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
  dt_iop_rgblevels_params_t *p = (dt_iop_rgblevels_params_t *)self->params;

  _turn_selregion_picker_off(self);

  g->channel = DT_IOP_RGBLEVELS_R;
  gtk_notebook_set_current_page(GTK_NOTEBOOK(g->channel_tabs), DT_IOP_RGBLEVELS_R);
  p->autoscale = dt_bauhaus_combobox_get(widget);

  _rgblevels_show_hide_controls(p, g);

  dt_dev_add_history_item(darktable.develop, self, TRUE);
  gtk_widget_queue_draw(self->widget);
}

static void _preserve_colors_callback(GtkWidget *widget, dt_iop_module_t *self)
{
  if(darktable.gui->reset) return;
  dt_iop_rgblevels_params_t *p = (dt_iop_rgblevels_params_t *)self->params;

  _turn_selregion_picker_off(self);

  p->preserve_colors = dt_bauhaus_combobox_get(widget);

  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void _tab_switch_callback(GtkNotebook *notebook, GtkWidget *page, guint page_num, dt_iop_module_t *self)
{
  if(darktable.gui->reset) return;
  dt_iop_rgblevels_gui_data_t *g = (dt_iop_rgblevels_gui_data_t *)self->gui_data;

  g->channel = (dt_iop_rgblevels_channel_t)page_num;

  gtk_widget_queue_draw(self->widget);
}

static void _color_picker_callback(GtkWidget *button, dt_iop_color_picker_t *self)
{
  _turn_select_region_off(self->module);
  dt_iop_color_picker_callback(button, self);
}

static int _iop_color_picker_get_set(dt_iop_module_t *self, GtkWidget *button)
{
  dt_iop_rgblevels_gui_data_t *g = (dt_iop_rgblevels_gui_data_t *)self->gui_data;

  const dt_iop_rgblevels_pick_t current_picker = g->color_picker.current_picker;

  g->color_picker.current_picker = DT_IOP_RGBLEVELS_PICK_NONE;

  if(button == GTK_WIDGET(g->blackpick))
    g->color_picker.current_picker = DT_IOP_RGBLEVELS_PICK_BLACK;
  else if(button == GTK_WIDGET(g->greypick))
    g->color_picker.current_picker = DT_IOP_RGBLEVELS_PICK_GREY;
  else if(button == GTK_WIDGET(g->whitepick))
    g->color_picker.current_picker = DT_IOP_RGBLEVELS_PICK_WHITE;

  if (current_picker == g->color_picker.current_picker)
    return DT_COLOR_PICKER_ALREADY_SELECTED;
  else
    return g->color_picker.current_picker;
}

static void _iop_color_picker_apply(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_rgblevels_gui_data_t *c = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
  dt_iop_rgblevels_params_t *p = (dt_iop_rgblevels_params_t *)self->params;

  const dt_iop_rgblevels_channel_t channel = c->channel;

  /* we need to save the last picked color to prevent flickering when
   * changing from one picker to another, as the picked_color value does not
   * update as rapidly */
  const float mean_picked_color = *self->picked_color;

  if(self->color_picker_point[0] >= 0.0f
     && self->color_picker_point[1] >= 0.0f
     && self->picked_color_max[0] >= 0.0f
     && mean_picked_color != c->last_picked_color)
  {
    float previous_color[3];
    previous_color[0] = p->levels[channel][0];
    previous_color[1] = p->levels[channel][1];
    previous_color[2] = p->levels[channel][2];

    c->last_picked_color = mean_picked_color;

    if(DT_IOP_RGBLEVELS_PICK_BLACK == c->color_picker.current_picker)
    {
      if(mean_picked_color > p->levels[channel][1])
      {
        p->levels[channel][0] = p->levels[channel][1] - FLT_EPSILON;
      }
      else
      {
        p->levels[channel][0] = mean_picked_color;
      }
    }
    else if(DT_IOP_RGBLEVELS_PICK_GREY == c->color_picker.current_picker)
    {
      if(mean_picked_color < p->levels[channel][0] || mean_picked_color > p->levels[channel][2])
      {
        p->levels[channel][1] = p->levels[channel][1];
      }
      else
      {
        p->levels[channel][1] = mean_picked_color;
      }
    }
    else if(DT_IOP_RGBLEVELS_PICK_WHITE == c->color_picker.current_picker)
    {
      if(mean_picked_color < p->levels[channel][1])
      {
        p->levels[channel][2] = p->levels[channel][1] + FLT_EPSILON;
      }
      else
      {
        p->levels[channel][2] = mean_picked_color;
      }
    }

    if(previous_color[0] != p->levels[channel][0]
       || previous_color[1] != p->levels[channel][1]
       || previous_color[2] != p->levels[channel][2])
    {
      dt_dev_add_history_item(darktable.develop, self, TRUE);
    }
  }

}

static void _iop_color_picker_update(dt_iop_module_t *self)
{
  dt_iop_rgblevels_gui_data_t *g = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
  const dt_iop_rgblevels_pick_t which_colorpicker = g->color_picker.current_picker;
  const int reset = darktable.gui->reset;
  darktable.gui->reset = 1;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->blackpick), which_colorpicker == DT_IOP_RGBLEVELS_PICK_BLACK);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->greypick), which_colorpicker == DT_IOP_RGBLEVELS_PICK_GREY);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->whitepick), which_colorpicker == DT_IOP_RGBLEVELS_PICK_WHITE);
  darktable.gui->reset = reset;
}

void commit_params(dt_iop_module_t *self, dt_iop_params_t *p1, dt_dev_pixelpipe_t *pipe,
                   dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_rgblevels_data_t *d = (dt_iop_rgblevels_data_t *)piece->data;
  dt_iop_rgblevels_params_t *p = (dt_iop_rgblevels_params_t *)p1;

  if(pipe->type == DT_DEV_PIXELPIPE_PREVIEW)
    piece->request_histogram |= (DT_REQUEST_ON);
  else
    piece->request_histogram &= ~(DT_REQUEST_ON);

  memcpy(&(d->params), p, sizeof(dt_iop_rgblevels_params_t));

  for(int i = 0; i < DT_IOP_RGBLEVELS_MAX_CHANNELS; i++)
  {
    for(int c = 0; c < 3; c++)
    {
      if(d->params.autoscale == DT_IOP_RGBLEVELS_LINKED_CHANNELS)
        d->params.levels[i][c] = p->levels[0][c];
      else
        d->params.levels[i][c] = p->levels[i][c];
    }
  }

  _compute_lut(piece);
}

void init_pipe(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  piece->data = malloc(sizeof(dt_iop_rgblevels_data_t));
}

void cleanup_pipe(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  // clean up everything again.
  free(piece->data);
  piece->data = NULL;
}

void gui_update(dt_iop_module_t *self)
{
  dt_iop_rgblevels_params_t *p = (dt_iop_rgblevels_params_t *)self->params;
  dt_iop_rgblevels_gui_data_t *g = (dt_iop_rgblevels_gui_data_t *)self->gui_data;

  dt_bauhaus_combobox_set(g->cmb_autoscale, p->autoscale);
  dt_bauhaus_combobox_set(g->cmb_preserve_colors, p->preserve_colors);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_select_region), g->draw_selected_region);
  _rgblevels_show_hide_controls(p, g);

  gtk_widget_queue_draw(self->widget);
}

void gui_focus(struct dt_iop_module_t *self, gboolean in)
{
  if(!in) _turn_selregion_picker_off(self);
}

void gui_reset(struct dt_iop_module_t *self)
{
  dt_iop_rgblevels_gui_data_t *g = (dt_iop_rgblevels_gui_data_t *)self->gui_data;

  _turn_selregion_picker_off(self);

  g->channel = DT_IOP_RGBLEVELS_R;

  gtk_widget_queue_draw(self->widget);
}

void reload_defaults(dt_iop_module_t *self)
{
  dt_iop_rgblevels_params_t tmp = {0};
  tmp.autoscale = DT_IOP_RGBLEVELS_LINKED_CHANNELS;
  tmp.preserve_colors = DT_RGB_NORM_LUMINANCE;
  for(int c = 0; c < DT_IOP_RGBLEVELS_MAX_CHANNELS; c++)
  {
    tmp.levels[c][0] = RGBLEVELS_MIN;
    tmp.levels[c][1] = RGBLEVELS_MID;
    tmp.levels[c][2] = RGBLEVELS_MAX;
  }

  memcpy(self->params, &tmp, sizeof(dt_iop_rgblevels_params_t));
  memcpy(self->default_params, &tmp, sizeof(dt_iop_rgblevels_params_t));
}

void init(dt_iop_module_t *self)
{
  self->params = calloc(1, sizeof(dt_iop_rgblevels_params_t));
  self->default_params = calloc(1, sizeof(dt_iop_rgblevels_params_t));
  self->default_enabled = 0;
  self->request_histogram |= (DT_REQUEST_ON);
  self->params_size = sizeof(dt_iop_rgblevels_params_t);
  self->gui_data = NULL;
}

void init_global(dt_iop_module_so_t *self)
{
  const int program = 29; // rgblevels.cl, from programs.conf
  dt_iop_rgblevels_global_data_t *gd
      = (dt_iop_rgblevels_global_data_t *)malloc(sizeof(dt_iop_rgblevels_global_data_t));
  self->data = gd;
  gd->kernel_levels = dt_opencl_create_kernel(program, "rgblevels");
}

void cleanup_global(dt_iop_module_so_t *self)
{
  dt_iop_rgblevels_global_data_t *gd = (dt_iop_rgblevels_global_data_t *)self->data;
  dt_opencl_free_kernel(gd->kernel_levels);
  free(self->data);
  self->data = NULL;
}

void cleanup(dt_iop_module_t *self)
{
  free(self->params);
  self->params = NULL;
}

void change_image(struct dt_iop_module_t *self)
{
  dt_iop_rgblevels_gui_data_t *g = (dt_iop_rgblevels_gui_data_t *)self->gui_data;

  g->channel = DT_IOP_RGBLEVELS_R;
  g->call_auto_levels = 0;
  g->draw_selected_region = 0;
  g->posx_from = g->posx_to = g->posy_from = g->posy_to = 0.f;
  g->box_cood[0] = g->box_cood[1] = g->box_cood[2] = g->box_cood[3] = 0.f;
  g->button_down = 0;
}

void gui_init(dt_iop_module_t *self)
{
  self->gui_data = malloc(sizeof(dt_iop_rgblevels_gui_data_t));
  dt_iop_rgblevels_gui_data_t *c = (dt_iop_rgblevels_gui_data_t *)self->gui_data;

  dt_pthread_mutex_init(&c->lock, NULL);
  change_image(self);

  c->mouse_x = c->mouse_y = -1.0;
  c->dragging = 0;
  c->last_picked_color = -1;

  self->widget = GTK_WIDGET(gtk_box_new(GTK_ORIENTATION_VERTICAL, 5));
  dt_gui_add_help_link(self->widget, dt_get_help_url(self->op));

  c->cmb_autoscale = dt_bauhaus_combobox_new(self);
  dt_bauhaus_widget_set_label(c->cmb_autoscale, NULL, _("mode"));
  dt_bauhaus_combobox_add(c->cmb_autoscale, _("RGB, linked channels"));
  dt_bauhaus_combobox_add(c->cmb_autoscale, _("RGB, independent channels"));
  gtk_box_pack_start(GTK_BOX(self->widget), c->cmb_autoscale, TRUE, TRUE, 0);
  gtk_widget_set_tooltip_text(c->cmb_autoscale, _("choose between linked and independent channels."));
  g_signal_connect(G_OBJECT(c->cmb_autoscale), "value-changed", G_CALLBACK(_autoscale_callback), self);

  // tabs
  c->channel_tabs = GTK_NOTEBOOK(gtk_notebook_new());

  gtk_notebook_append_page(GTK_NOTEBOOK(c->channel_tabs), GTK_WIDGET(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0)),
                           gtk_label_new(_("  R  ")));
  gtk_widget_set_tooltip_text(
      gtk_notebook_get_tab_label(c->channel_tabs, gtk_notebook_get_nth_page(c->channel_tabs, -1)),
      _("curve nodes for r channel"));
  gtk_notebook_append_page(GTK_NOTEBOOK(c->channel_tabs), GTK_WIDGET(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0)),
                           gtk_label_new(_("  G  ")));
  gtk_widget_set_tooltip_text(
      gtk_notebook_get_tab_label(c->channel_tabs, gtk_notebook_get_nth_page(c->channel_tabs, -1)),
      _("curve nodes for g channel"));
  gtk_notebook_append_page(GTK_NOTEBOOK(c->channel_tabs), GTK_WIDGET(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0)),
                           gtk_label_new(_("  B  ")));
  gtk_widget_set_tooltip_text(
      gtk_notebook_get_tab_label(c->channel_tabs, gtk_notebook_get_nth_page(c->channel_tabs, -1)),
      _("curve nodes for b channel"));

  gtk_widget_show_all(GTK_WIDGET(gtk_notebook_get_nth_page(c->channel_tabs, c->channel)));
  gtk_notebook_set_current_page(GTK_NOTEBOOK(c->channel_tabs), c->channel);

  GtkWidget *notebook = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(notebook), GTK_WIDGET(c->channel_tabs), FALSE, FALSE, 0);

  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), vbox, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(notebook), TRUE, TRUE, 0);

  g_signal_connect(G_OBJECT(c->channel_tabs), "switch_page", G_CALLBACK(_tab_switch_callback), self);

  c->area = GTK_DRAWING_AREA(dtgtk_drawing_area_new_with_aspect_ratio(9.0 / 16.0));
  GtkWidget *vbox_manual = GTK_WIDGET(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
  gtk_box_pack_start(GTK_BOX(vbox_manual), GTK_WIDGET(c->area), TRUE, TRUE, 0);

  gtk_widget_set_tooltip_text(GTK_WIDGET(c->area),_("drag handles to set black, gray, and white points. "
                                                    "operates on L channel."));

  gtk_widget_add_events(GTK_WIDGET(c->area), GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK
                                             | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
                                             | GDK_LEAVE_NOTIFY_MASK | darktable.gui->scroll_mask);
  g_signal_connect(G_OBJECT(c->area), "draw", G_CALLBACK(_area_draw_callback), self);
  g_signal_connect(G_OBJECT(c->area), "button-press-event", G_CALLBACK(_area_button_press_callback), self);
  g_signal_connect(G_OBJECT(c->area), "button-release-event", G_CALLBACK(_area_button_release_callback), self);
  g_signal_connect(G_OBJECT(c->area), "motion-notify-event", G_CALLBACK(_area_motion_notify_callback), self);
  g_signal_connect(G_OBJECT(c->area), "leave-notify-event", G_CALLBACK(_area_leave_notify_callback), self);
  g_signal_connect(G_OBJECT(c->area), "scroll-event", G_CALLBACK(_area_scroll_callback), self);

  c->blackpick = dtgtk_togglebutton_new(dtgtk_cairo_paint_colorpicker, CPF_STYLE_FLAT, NULL);
  gtk_widget_set_tooltip_text(c->blackpick, _("pick black point from image"));

  c->greypick = dtgtk_togglebutton_new(dtgtk_cairo_paint_colorpicker, CPF_STYLE_FLAT, NULL);
  gtk_widget_set_tooltip_text(c->greypick, _("pick medium gray point from image"));

  c->whitepick = dtgtk_togglebutton_new(dtgtk_cairo_paint_colorpicker, CPF_STYLE_FLAT, NULL);
  gtk_widget_set_tooltip_text(c->whitepick, _("pick white point from image"));

  GdkRGBA color = { 0 };
  color.alpha = 1.0;
  dtgtk_togglebutton_override_color(DTGTK_TOGGLEBUTTON(c->blackpick), &color);
  color.red = color.green = color.blue = 0.5;
  dtgtk_togglebutton_override_color(DTGTK_TOGGLEBUTTON(c->greypick), &color);
  color.red = color.green = color.blue = 1.0;
  dtgtk_togglebutton_override_color(DTGTK_TOGGLEBUTTON(c->whitepick), &color);

  GtkWidget *pick_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(pick_hbox), GTK_WIDGET(c->blackpick), TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(pick_hbox), GTK_WIDGET(c->greypick), TRUE, TRUE, 0);
  gtk_box_pack_end(GTK_BOX(pick_hbox), GTK_WIDGET(c->whitepick), TRUE, TRUE, 0);

  gtk_box_pack_start(GTK_BOX(vbox_manual), pick_hbox, TRUE, TRUE, 0);

  c->bt_auto_levels = gtk_button_new_with_label(_("auto"));
  gtk_widget_set_tooltip_text(c->bt_auto_levels, _("apply auto levels"));

  c->bt_select_region = dtgtk_togglebutton_new(dtgtk_cairo_paint_colorpicker, CPF_STYLE_FLAT, NULL);
  g_object_set(G_OBJECT(c->bt_select_region), "tooltip-text",
               _("apply auto levels based on a region defined by the user\n"
                 "click and drag to draw the area\n"
                 "right click to cancel"),
               (char *)NULL);

  GtkWidget *autolevels_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, DT_PIXEL_APPLY_DPI(10));
  gtk_box_pack_start(GTK_BOX(autolevels_box), c->bt_auto_levels, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(autolevels_box), c->bt_select_region, TRUE, TRUE, 0);

  gtk_box_pack_start(GTK_BOX(vbox_manual), autolevels_box, TRUE, TRUE, 0);

  gtk_box_pack_start(GTK_BOX(self->widget), vbox_manual, TRUE, TRUE, 0);

  gtk_widget_show_all(vbox_manual);

  g_signal_connect(G_OBJECT(c->bt_auto_levels), "clicked", G_CALLBACK(_auto_levels_callback), self);
  g_signal_connect(G_OBJECT(c->bt_select_region), "toggled", G_CALLBACK(_select_region_toggled_callback), self);
  g_signal_connect(G_OBJECT(c->blackpick), "toggled", G_CALLBACK(_color_picker_callback), &c->color_picker);
  g_signal_connect(G_OBJECT(c->greypick), "toggled", G_CALLBACK(_color_picker_callback), &c->color_picker);
  g_signal_connect(G_OBJECT(c->whitepick), "toggled", G_CALLBACK(_color_picker_callback), &c->color_picker);

  c->cmb_preserve_colors = dt_bauhaus_combobox_new(self);
  dt_bauhaus_widget_set_label(c->cmb_preserve_colors, NULL, _("preserve colors"));
  dt_bauhaus_combobox_add(c->cmb_preserve_colors, _("none"));
  dt_bauhaus_combobox_add(c->cmb_preserve_colors, _("luminance"));
  dt_bauhaus_combobox_add(c->cmb_preserve_colors, _("max rgb"));
  dt_bauhaus_combobox_add(c->cmb_preserve_colors, _("average rgb"));
  dt_bauhaus_combobox_add(c->cmb_preserve_colors, _("sum rgb"));
  dt_bauhaus_combobox_add(c->cmb_preserve_colors, _("norm rgb"));
  dt_bauhaus_combobox_add(c->cmb_preserve_colors, _("basic power"));
  gtk_box_pack_start(GTK_BOX(self->widget), c->cmb_preserve_colors, TRUE, TRUE, 0);
  gtk_widget_set_tooltip_text(c->cmb_preserve_colors, _("method to preserve colors when applying contrast"));
  g_signal_connect(G_OBJECT(c->cmb_preserve_colors), "value-changed", G_CALLBACK(_preserve_colors_callback), self);

  dt_iop_init_picker(&c->color_picker,
              self,
              DT_COLOR_PICKER_POINT,
              _iop_color_picker_get_set,
              _iop_color_picker_apply,
              _iop_color_picker_update);

  // add signal handler for preview pipe finish
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_DEVELOP_PREVIEW_PIPE_FINISHED,
                            G_CALLBACK(_develop_ui_pipe_finished_callback), self);
}

void gui_cleanup(dt_iop_module_t *self)
{
  dt_control_signal_disconnect(darktable.signals, G_CALLBACK(_develop_ui_pipe_finished_callback), self);

  dt_iop_rgblevels_gui_data_t *g = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
  if(g)
  {
    dt_pthread_mutex_destroy(&g->lock);
  }
  free(self->gui_data);
  self->gui_data = NULL;
}

static void _get_selected_area(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece,
                               dt_iop_rgblevels_gui_data_t *g, const dt_iop_roi_t *const roi_in, int *box_out)
{
  box_out[0] = box_out[1] = box_out[2] = box_out[3] = 0;

  if(g)
  {
    const int width = roi_in->width;
    const int height = roi_in->height;
    float box_cood[4] = { g->box_cood[0], g->box_cood[1], g->box_cood[2], g->box_cood[3] };

    box_cood[0] *= piece->pipe->iwidth;
    box_cood[1] *= piece->pipe->iheight;
    box_cood[2] *= piece->pipe->iwidth;
    box_cood[3] *= piece->pipe->iheight;

    dt_dev_distort_transform_plus(self->dev, piece->pipe, self->iop_order, DT_DEV_TRANSFORM_DIR_BACK_INCL,
                                  box_cood, 2);

    box_cood[0] *= roi_in->scale;
    box_cood[1] *= roi_in->scale;
    box_cood[2] *= roi_in->scale;
    box_cood[3] *= roi_in->scale;

    box_cood[0] -= roi_in->x;
    box_cood[1] -= roi_in->y;
    box_cood[2] -= roi_in->x;
    box_cood[3] -= roi_in->y;

    int box[4];

    // re-order edges of bounding box
    box[0] = fminf(box_cood[0], box_cood[2]);
    box[1] = fminf(box_cood[1], box_cood[3]);
    box[2] = fmaxf(box_cood[0], box_cood[2]);
    box[3] = fmaxf(box_cood[1], box_cood[3]);

    // do not continue if box is completely outside of roi
    if(!(box[0] >= width || box[1] >= height || box[2] < 0 || box[3] < 0))
    {
      // clamp bounding box to roi
      for(int k = 0; k < 4; k += 2) box[k] = MIN(width - 1, MAX(0, box[k]));
      for(int k = 1; k < 4; k += 2) box[k] = MIN(height - 1, MAX(0, box[k]));

      // safety check: area needs to have minimum 1 pixel width and height
      if(!(box[2] - box[0] < 1 || box[3] - box[1] < 1))
      {
        box_out[0] = box[0];
        box_out[1] = box[1];
        box_out[2] = box[2];
        box_out[3] = box[3];
      }
    }
  }
}

static void _auto_levels(const float *const img, const int width, const int height, int *box_area,
                           dt_iop_rgblevels_params_t *p, const int _channel, const dt_iop_order_iccprofile_info_t *const work_profile)
{
  int y_from, y_to, x_from, x_to;
  const int ch = 4;
  const int channel = (p->autoscale == DT_IOP_RGBLEVELS_INDEPENDENT_CHANNELS) ? _channel : 0;
  if(box_area[2] > box_area[0] && box_area[3] > box_area[1])
  {
    y_from = box_area[1];
    y_to = box_area[3];
    x_from = box_area[0];
    x_to = box_area[2];
  }
  else
  {
    y_from = 0;
    y_to = height - 1;
    x_from = 0;
    x_to = width - 1;
  }

  float max = -INFINITY;
  float min = INFINITY;

  for(int y = y_from; y <= y_to; y++)
  {
    const float *const in = img + ch * width * y;
    for(int x = x_from; x <= x_to; x++)
    {
      const float *const pixel = in + x * ch;

      if(p->autoscale == DT_IOP_RGBLEVELS_INDEPENDENT_CHANNELS || p->preserve_colors == DT_RGB_NORM_NONE)
      {
        if(p->autoscale == DT_IOP_RGBLEVELS_INDEPENDENT_CHANNELS)
        {
          if(pixel[channel] >= 0.f)
          {
            max = fmaxf(max, pixel[channel]);
            min = fminf(min, pixel[channel]);
          }
        }
        else
        {
          for(int c = 0; c < 3; c++)
          {
            if(pixel[c] >= 0.f)
            {
              max = fmaxf(max, pixel[c]);
              min = fminf(min, pixel[c]);
            }
          }
        }
      }
      else
      {
        const float lum = dt_rgb_norm(pixel, p->preserve_colors, work_profile);
        if(lum >= 0.f)
        {
          max = fmaxf(max, lum);
          min = fminf(min, lum);
        }
      }
    }
  }

  p->levels[channel][0] = CLAMP(min, 0.f, 1.f);
  p->levels[channel][2] = CLAMP(max, 0.f, 1.f);
  p->levels[channel][1] = (p->levels[channel][2] + p->levels[channel][0]) / 2.f;
}

void process(dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, const void *const ivoid, void *const ovoid,
             const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  const int ch = piece->colors;
  const dt_iop_rgblevels_data_t *const d = (dt_iop_rgblevels_data_t *)piece->data;
  dt_iop_rgblevels_params_t *p = (dt_iop_rgblevels_params_t *)&d->params;
  dt_iop_rgblevels_gui_data_t *g = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
  const dt_iop_order_iccprofile_info_t *const work_profile = dt_ioppr_get_pipe_work_profile_info(piece->pipe);

  // process auto levels
  if(g && piece->pipe->type == DT_DEV_PIXELPIPE_PREVIEW)
  {
    dt_pthread_mutex_lock(&g->lock);
    if(g->call_auto_levels == 1 && !darktable.gui->reset)
    {
      g->call_auto_levels = -1;

      dt_pthread_mutex_unlock(&g->lock);

      memcpy(&g->params, p, sizeof(dt_iop_rgblevels_params_t));

      int box[4] = { 0 };
      _get_selected_area(self, piece, g, roi_in, box);
      _auto_levels((const float *const)ivoid, roi_in->width, roi_in->height, box, &(g->params), g->channel, work_profile);

      dt_pthread_mutex_lock(&g->lock);

      g->call_auto_levels = 2;

      dt_pthread_mutex_unlock(&g->lock);
    }
    else
    {
      dt_pthread_mutex_unlock(&g->lock);
    }
  }

  const float mult[3] = { 1.f / (d->params.levels[0][2] - d->params.levels[0][0]),
                          1.f / (d->params.levels[1][2] - d->params.levels[1][0]),
                          1.f / (d->params.levels[2][2] - d->params.levels[2][0]) };

#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(work_profile, d, ch, ivoid, ovoid, roi_out, mult) \
  schedule(static)
#endif
  for(int k = 0; k < roi_out->height; k++)
  {
    float *in = (float *)ivoid + (size_t)k * ch * roi_out->width;
    float *out = (float *)ovoid + (size_t)k * ch * roi_out->width;

    for(int j = 0; j < roi_out->width; j++, in += ch, out += ch)
    {
      if(d->params.autoscale == DT_IOP_RGBLEVELS_INDEPENDENT_CHANNELS || d->params.preserve_colors == DT_RGB_NORM_NONE)
      {
        for(int c = 0; c < 3; c++)
        {
          const float L_in = in[c];

          if(L_in <= d->params.levels[c][0])
          {
            // Anything below the lower threshold just clips to zero
            out[c] = 0.0f;
          }
          else if(L_in >= d->params.levels[c][2])
          {
            const float percentage = (L_in - d->params.levels[c][0]) * mult[c];
            out[c] = pow(percentage, d->inv_gamma[c]);
          }
          else
          {
            // Within the expected input range we can use the lookup table
            const float percentage = (L_in - d->params.levels[c][0]) * mult[c];
            out[c] = d->lut[c][CLAMP((int)(percentage * 0x10000ul), 0, 0xffff)];
          }
        }
      }
      else
      {
        const int ch_levels = 0;

        float ratio = 1.f;
        const float lum = dt_rgb_norm(in, d->params.preserve_colors, work_profile);
        if(lum > d->params.levels[ch_levels][0])
        {
          float curve_lum;
          if(lum >= d->params.levels[ch_levels][2])
          {
            const float percentage = (lum - d->params.levels[ch_levels][0]) * mult[ch_levels];
            curve_lum = pow(percentage, d->inv_gamma[ch_levels]);
          }
          else
          {
            // Within the expected input range we can use the lookup table
            const float percentage = (lum - d->params.levels[ch_levels][0]) * mult[ch_levels];
            curve_lum = d->lut[ch_levels][CLAMP((int)(percentage * 0x10000ul), 0, 0xffff)];
          }

          ratio = curve_lum / lum;

          for(int c = 0; c < 3; c++)
          {
            out[c] = (ratio * in[c]);
          }
        }
        else
        {
          for(int c = 0; c < 3; c++) out[c] = 0.f;
        }
      }

      out[3] = in[3];
    }
  }
}

#ifdef HAVE_OPENCL
int process_cl(dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, cl_mem dev_in, cl_mem dev_out,
               const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  const dt_iop_order_iccprofile_info_t *const work_profile = dt_ioppr_get_pipe_work_profile_info(piece->pipe);

  const int ch = piece->colors;
  dt_iop_rgblevels_data_t *d = (dt_iop_rgblevels_data_t *)piece->data;
  dt_iop_rgblevels_params_t *p = (dt_iop_rgblevels_params_t *)&d->params;
  dt_iop_rgblevels_gui_data_t *g = (dt_iop_rgblevels_gui_data_t *)self->gui_data;
  dt_iop_rgblevels_global_data_t *gd = (dt_iop_rgblevels_global_data_t *)self->global_data;

  cl_int err = CL_SUCCESS;

  float *src_buffer = NULL;

  cl_mem dev_lutr = NULL;
  cl_mem dev_lutg = NULL;
  cl_mem dev_lutb = NULL;

  cl_mem dev_levels = NULL;
  cl_mem dev_inv_gamma = NULL;

  cl_mem dev_profile_info = NULL;
  cl_mem dev_profile_lut = NULL;
  dt_colorspaces_iccprofile_info_cl_t *profile_info_cl;
  cl_float *profile_lut_cl = NULL;

  const int use_work_profile = (work_profile == NULL) ? 0 : 1;

  const int devid = piece->pipe->devid;

  const int width = roi_out->width;
  const int height = roi_out->height;

  // process auto levels
  if(g && piece->pipe->type == DT_DEV_PIXELPIPE_PREVIEW)
  {
    dt_pthread_mutex_lock(&g->lock);
    if(g->call_auto_levels == 1 && !darktable.gui->reset)
    {
      g->call_auto_levels = -1;

      dt_pthread_mutex_unlock(&g->lock);

      // get the image, this works only in C
      src_buffer = dt_alloc_align(64, width * height * ch * sizeof(float));
      if(src_buffer == NULL)
      {
        fprintf(stderr, "[rgblevels process_cl] error allocating memory for temp table 1\n");
        err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
        goto cleanup;
      }

      err = dt_opencl_copy_device_to_host(devid, src_buffer, dev_in, width, height, ch * sizeof(float));
      if(err != CL_SUCCESS)
      {
        fprintf(stderr, "[rgblevels process_cl] error allocating memory for temp table 2\n");
        goto cleanup;
      }

      memcpy(&g->params, p, sizeof(dt_iop_rgblevels_params_t));

      int box[4] = { 0 };
      _get_selected_area(self, piece, g, roi_in, box);
      _auto_levels(src_buffer, roi_in->width, roi_in->height, box, &(g->params), g->channel, work_profile);

      dt_free_align(src_buffer);
      src_buffer = NULL;

      dt_pthread_mutex_lock(&g->lock);

      g->call_auto_levels = 2;

      dt_pthread_mutex_unlock(&g->lock);
    }
    else
    {
      dt_pthread_mutex_unlock(&g->lock);
    }
  }

  const int autoscale = d->params.autoscale;
  const int preserve_colors = d->params.preserve_colors;

  dev_lutr = dt_opencl_copy_host_to_device(devid, d->lut[0], 256, 256, sizeof(float));
  if(dev_lutr == NULL)
  {
    fprintf(stderr, "[rgblevels process_cl] error allocating memory 1\n");
    err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
    goto cleanup;
  }
  dev_lutg = dt_opencl_copy_host_to_device(devid, d->lut[1], 256, 256, sizeof(float));
  if(dev_lutg == NULL)
  {
    fprintf(stderr, "[rgblevels process_cl] error allocating memory 2\n");
    err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
    goto cleanup;
  }
  dev_lutb = dt_opencl_copy_host_to_device(devid, d->lut[2], 256, 256, sizeof(float));
  if(dev_lutb == NULL)
  {
    fprintf(stderr, "[rgblevels process_cl] error allocating memory 3\n");
    err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
    goto cleanup;
  }

  dev_levels = dt_opencl_copy_host_to_device_constant(devid, sizeof(float) * 3 * 3, (float *)d->params.levels);
  if(dev_levels == NULL)
  {
    fprintf(stderr, "[rgblevels process_cl] error allocating memory 4\n");
    err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
    goto cleanup;
  }

  dev_inv_gamma = dt_opencl_copy_host_to_device_constant(devid, sizeof(float) * 3, (float *)d->inv_gamma);
  if(dev_inv_gamma == NULL)
  {
    fprintf(stderr, "[rgblevels process_cl] error allocating memory 5\n");
    err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
    goto cleanup;
  }

  err = dt_ioppr_build_iccprofile_params_cl(work_profile, devid, &profile_info_cl, &profile_lut_cl,
                                            &dev_profile_info, &dev_profile_lut);
  if(err != CL_SUCCESS) goto cleanup;

  size_t sizes[2] = { ROUNDUPWD(width), ROUNDUPHT(height) };
  dt_opencl_set_kernel_arg(devid, gd->kernel_levels, 0, sizeof(cl_mem), &dev_in);
  dt_opencl_set_kernel_arg(devid, gd->kernel_levels, 1, sizeof(cl_mem), &dev_out);
  dt_opencl_set_kernel_arg(devid, gd->kernel_levels, 2, sizeof(int), &width);
  dt_opencl_set_kernel_arg(devid, gd->kernel_levels, 3, sizeof(int), &height);
  dt_opencl_set_kernel_arg(devid, gd->kernel_levels, 4, sizeof(int), (void *)&autoscale);
  dt_opencl_set_kernel_arg(devid, gd->kernel_levels, 5, sizeof(int), (void *)&preserve_colors);
  dt_opencl_set_kernel_arg(devid, gd->kernel_levels, 6, sizeof(cl_mem), &dev_lutr);
  dt_opencl_set_kernel_arg(devid, gd->kernel_levels, 7, sizeof(cl_mem), &dev_lutg);
  dt_opencl_set_kernel_arg(devid, gd->kernel_levels, 8, sizeof(cl_mem), &dev_lutb);
  dt_opencl_set_kernel_arg(devid, gd->kernel_levels, 9, sizeof(cl_mem), (void *)&dev_levels);
  dt_opencl_set_kernel_arg(devid, gd->kernel_levels, 10, sizeof(cl_mem), (void *)&dev_inv_gamma);
  dt_opencl_set_kernel_arg(devid, gd->kernel_levels, 11, sizeof(cl_mem), (void *)&dev_profile_info);
  dt_opencl_set_kernel_arg(devid, gd->kernel_levels, 12, sizeof(cl_mem), (void *)&dev_profile_lut);
  dt_opencl_set_kernel_arg(devid, gd->kernel_levels, 13, sizeof(int), (void *)&use_work_profile);
  err = dt_opencl_enqueue_kernel_2d(devid, gd->kernel_levels, sizes);
  if(err != CL_SUCCESS)
  {
    fprintf(stderr, "[rgblevels process_cl] error %i enqueue kernel\n", err);
    goto cleanup;
  }

cleanup:
  if(dev_lutr) dt_opencl_release_mem_object(dev_lutr);
  if(dev_lutg) dt_opencl_release_mem_object(dev_lutg);
  if(dev_lutb) dt_opencl_release_mem_object(dev_lutb);
  if(dev_levels) dt_opencl_release_mem_object(dev_levels);
  if(dev_inv_gamma) dt_opencl_release_mem_object(dev_inv_gamma);
  dt_ioppr_free_iccprofile_params_cl(&profile_info_cl, &profile_lut_cl, &dev_profile_info, &dev_profile_lut);

  if(src_buffer) dt_free_align(src_buffer);

  if(err != CL_SUCCESS) dt_print(DT_DEBUG_OPENCL, "[opencl_rgblevels] couldn't enqueue kernel! %d\n", err);

  return (err == CL_SUCCESS) ? TRUE : FALSE;
}
#endif

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
