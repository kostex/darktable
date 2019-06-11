/*
    This file is part of darktable,
    copyright (c)2010--2012 Henrik Andersson.
    copyright (c)2012 Ulrich Pegelow.

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

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "common/darktable.h"
#include "develop/develop.h"
#include "gradientslider.h"
#include "gui/gtk.h"

#define CLAMP_RANGE(x, y, z) (CLAMP(x, y, z))

#define DTGTK_GRADIENT_SLIDER_VALUE_CHANGED_DELAY_MAX 50
#define DTGTK_GRADIENT_SLIDER_VALUE_CHANGED_DELAY_MIN 10
#define DTGTK_GRADIENT_SLIDER_DEFAULT_INCREMENT 0.01


static void _gradient_slider_class_init(GtkDarktableGradientSliderClass *klass);
static void _gradient_slider_init(GtkDarktableGradientSlider *slider);
static void _gradient_slider_realize(GtkWidget *widget);
static gboolean _gradient_slider_draw(GtkWidget *widget, cairo_t *cr);
static void _gradient_slider_destroy(GtkWidget *widget);

// Events
static gboolean _gradient_slider_enter_notify_event(GtkWidget *widget, GdkEventCrossing *event);
static gboolean _gradient_slider_button_press(GtkWidget *widget, GdkEventButton *event);
static gboolean _gradient_slider_button_release(GtkWidget *widget, GdkEventButton *event);
static gboolean _gradient_slider_motion_notify(GtkWidget *widget, GdkEventMotion *event);
static gboolean _gradient_slider_scroll_event(GtkWidget *widget, GdkEventScroll *event);
static gboolean _gradient_slider_key_press_event(GtkWidget *widget, GdkEventKey *event);

enum
{
  VALUE_CHANGED,
  LAST_SIGNAL
};

static guint _signals[LAST_SIGNAL] = { 0 };

static gboolean _gradient_slider_postponed_value_change(gpointer data)
{
  if(!GTK_IS_WIDGET(data)) return 0;

  if(DTGTK_GRADIENT_SLIDER(data)->is_changed == TRUE)
  {
    g_signal_emit_by_name(G_OBJECT(data), "value-changed");
    DTGTK_GRADIENT_SLIDER(data)->is_changed = FALSE;
  }

  if(!DTGTK_GRADIENT_SLIDER(data)->is_dragging) DTGTK_GRADIENT_SLIDER(data)->timeout_handle = 0;

  return DTGTK_GRADIENT_SLIDER(data)->is_dragging; // This is called by the gtk mainloop and is threadsafe
}

static inline gboolean _test_if_marker_is_upper_or_down(const gint marker, const gboolean up)
{
  if(up && (marker == GRADIENT_SLIDER_MARKER_LOWER_OPEN ||
                  marker == GRADIENT_SLIDER_MARKER_LOWER_FILLED ||
                  marker == GRADIENT_SLIDER_MARKER_LOWER_OPEN_BIG ||
                  marker == GRADIENT_SLIDER_MARKER_LOWER_FILLED_BIG))
    return FALSE;
  else if(!up && (marker == GRADIENT_SLIDER_MARKER_UPPER_OPEN ||
                  marker == GRADIENT_SLIDER_MARKER_UPPER_FILLED ||
                  marker == GRADIENT_SLIDER_MARKER_UPPER_OPEN_BIG ||
                  marker == GRADIENT_SLIDER_MARKER_UPPER_FILLED_BIG))
    return FALSE;
  else
    return TRUE; // must be a DOUBLE
}

static inline gdouble _screen_to_scale(GtkWidget *widget, gint screen)
{
  GtkDarktableGradientSlider *gslider = DTGTK_GRADIENT_SLIDER(widget);

  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);
  return ((gdouble)screen - gslider->margins) / ((gdouble)allocation.width - 2 * gslider->margins);
}

static inline gint _scale_to_screen(GtkWidget *widget, gdouble scale)
{
  GtkDarktableGradientSlider *gslider = DTGTK_GRADIENT_SLIDER(widget);

  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);
  return (gint)(scale * (allocation.width - 2 * gslider->margins) + gslider->margins);
}

static inline gdouble _get_position_from_screen(GtkWidget *widget, const gdouble x)
{
  GtkDarktableGradientSlider *gslider = DTGTK_GRADIENT_SLIDER(widget);
  gdouble position = roundf(_screen_to_scale(widget, x) / gslider->increment) * gslider->increment;
  return CLAMP_RANGE(position, 0., 1.);
}

static inline gint _get_active_marker(GtkDarktableGradientSlider *gslider)
{
  return (gslider->selected >= 0) ? gslider->selected : gslider->active;
}

static inline void _clamp_marker(GtkDarktableGradientSlider *gslider, const gint selected)
{
  const gdouble min = (selected == 0) ? 0.0f : gslider->position[selected - 1];
  const gdouble max = (selected == gslider->positions - 1) ? 1.0f : gslider->position[selected + 1];
  gslider->position[selected] = CLAMP(gslider->position[selected], min, max);
}

static gint _get_active_marker_internal(GtkWidget *widget, const gdouble x, const gboolean up)
{
  GtkDarktableGradientSlider *gslider = DTGTK_GRADIENT_SLIDER(widget);
  gint lselected = -1;
  const gdouble newposition = _get_position_from_screen(widget, x);

  assert(gslider->positions > 0);

  for(int k = 0; k < gslider->positions; k++)
  {
    if(_test_if_marker_is_upper_or_down(gslider->marker[k], up))
    {
      if(lselected < 0) lselected = k;
      if(fabs(newposition - gslider->position[k]) < fabs(newposition - gslider->position[lselected]))
      {
        lselected = k;
        break;
      }
    }
  }

  return lselected;
}

static gint _get_active_marker_from_screen(GtkWidget *widget, const gdouble x, const gdouble y)
{
  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);

  gboolean up = (y <= allocation.height / 2.f);
  gint lselected = _get_active_marker_internal(widget, x, up);
  if(lselected < 0) lselected = _get_active_marker_internal(widget, x, !up);

  assert(lselected >= 0);

  return lselected;
}

static gdouble _slider_move(GtkWidget *widget, gint k, gdouble value, gint direction)
{
  GtkDarktableGradientSlider *gslider = DTGTK_GRADIENT_SLIDER(widget);

  gdouble newvalue = value;
  const gdouble leftnext = (k == 0) ? 0.0f : gslider->position[k - 1];
  const gdouble rightnext = (k == gslider->positions - 1) ? 1.0f : gslider->position[k + 1];

  switch(direction)
  {
    case MOVE_LEFT:
      if(value < leftnext)
      {
        newvalue = (k == 0) ? fmax(value, 0.0f) : _slider_move(widget, k - 1, value, direction);
      }
      break;
    case MOVE_RIGHT:
      if(value > rightnext)
      {
        newvalue = (k == gslider->positions - 1) ? fmin(value, 1.0f)
                                                 : _slider_move(widget, k + 1, value, direction);
      }
      break;
  }

  gslider->position[k] = newvalue;

  return newvalue;
}

static gboolean _gradient_slider_add_delta_internal(GtkWidget *widget, gdouble delta, guint state, const gint selected)
{
  GtkDarktableGradientSlider *gslider = DTGTK_GRADIENT_SLIDER(widget);

  if(selected == -1) return TRUE;

  float multiplier;

  GdkModifierType modifiers = gtk_accelerator_get_default_mod_mask();
  if((state & modifiers) == GDK_SHIFT_MASK)
  {
    multiplier = dt_conf_get_float("darkroom/ui/scale_rough_step_multiplier");
  }
  else if((state & modifiers) == GDK_CONTROL_MASK)
  {
    multiplier = dt_conf_get_float("darkroom/ui/scale_precise_step_multiplier");
  }
  else
  {
    multiplier = dt_conf_get_float("darkroom/ui/scale_step_multiplier");
  }

  delta *= multiplier;

  gslider->position[selected] = gslider->position[selected] + delta;
  _clamp_marker(gslider, selected);

  gtk_widget_queue_draw(widget);
  g_signal_emit_by_name(G_OBJECT(widget), "value-changed");

  return TRUE;
}

static gboolean _gradient_slider_enter_notify_event(GtkWidget *widget, GdkEventCrossing *event)
{
  GtkDarktableGradientSlider *gslider = DTGTK_GRADIENT_SLIDER(widget);
  gtk_widget_set_state_flags(widget, GTK_STATE_FLAG_PRELIGHT, TRUE);
  gslider->is_entered = TRUE;
  gtk_widget_queue_draw(widget);
  return FALSE;
}

static gboolean _gradient_slider_leave_notify_event(GtkWidget *widget, GdkEventCrossing *event)
{
  GtkDarktableGradientSlider *gslider = DTGTK_GRADIENT_SLIDER(widget);
  gtk_widget_set_state_flags(widget, GTK_STATE_FLAG_NORMAL, TRUE);
  gslider->is_entered = FALSE;
  gslider->active = -1;
  gtk_widget_queue_draw(widget);
  return FALSE;
}

static gboolean _gradient_slider_button_press(GtkWidget *widget, GdkEventButton *event)
{
  GtkDarktableGradientSlider *gslider = DTGTK_GRADIENT_SLIDER(widget);

  // reset slider
  if(event->button == 1 && event->type == GDK_2BUTTON_PRESS && gslider->is_resettable)
  {
    gslider->is_dragging = FALSE;
    gslider->do_reset = TRUE;
    gslider->selected = -1;
    for(int k = 0; k < gslider->positions; k++) gslider->position[k] = gslider->resetvalue[k];
    gtk_widget_queue_draw(widget);
    g_signal_emit_by_name(G_OBJECT(widget), "value-changed");
  }
  else if((event->button == 1 || event->button == 3) && event->type == GDK_BUTTON_PRESS)
  {
    const gint lselected = _get_active_marker_from_screen(widget, event->x, event->y);

    assert(lselected >= 0);
    assert(lselected <= gslider->positions - 1);

    if(event->button == 1) // left mouse button : select and start dragging
    {
      gslider->selected = lselected;
      gslider->do_reset = FALSE;

      const gdouble newposition = _get_position_from_screen(widget, event->x);
      const gint direction = gslider->position[gslider->selected] <= newposition ? MOVE_RIGHT : MOVE_LEFT;

      _slider_move(widget, gslider->selected, newposition, direction);

      gslider->is_changed = TRUE;
      gslider->is_dragging = TRUE;
      // timeout_handle should always be zero here, but check just in case
      int delay = CLAMP_RANGE(darktable.develop->average_delay * 3 / 2,
                              DTGTK_GRADIENT_SLIDER_VALUE_CHANGED_DELAY_MIN,
                              DTGTK_GRADIENT_SLIDER_VALUE_CHANGED_DELAY_MAX);
      if(!gslider->timeout_handle)
        gslider->timeout_handle = g_timeout_add(delay, _gradient_slider_postponed_value_change, widget);
    }
    else if(gslider->positions
            > 1) // right mouse button: switch on/off selection (only if we have more than one marker)
    {
      gslider->is_dragging = FALSE;
      gslider->do_reset = FALSE;

      if(gslider->selected != lselected)
      {
        gslider->selected = lselected;
      }
      else
        gslider->selected = -1;

      gtk_widget_queue_draw(widget);
    }
  }

  return TRUE;
}

static gboolean _gradient_slider_motion_notify(GtkWidget *widget, GdkEventMotion *event)
{
  GtkDarktableGradientSlider *gslider = DTGTK_GRADIENT_SLIDER(widget);

  if(gslider->is_dragging == TRUE && gslider->selected != -1 && gslider->do_reset == FALSE)
  {
    assert(gslider->timeout_handle > 0);

    const gdouble newposition = _get_position_from_screen(widget, event->x);
    const gint direction = gslider->position[gslider->selected] <= newposition ? MOVE_RIGHT : MOVE_LEFT;

    _slider_move(widget, gslider->selected, newposition, direction);

    gslider->is_changed = TRUE;

    gtk_widget_queue_draw(widget);
  }
  else
  {
    gslider->active = _get_active_marker_from_screen(widget, event->x, event->y);
  }

  if(gslider->selected != -1) gtk_widget_grab_focus(widget);

  return TRUE;
}

static gboolean _gradient_slider_button_release(GtkWidget *widget, GdkEventButton *event)
{
  GtkDarktableGradientSlider *gslider = DTGTK_GRADIENT_SLIDER(widget);
  const gint selected = _get_active_marker(gslider);

  if(event->button == 1 && selected != -1 && gslider->do_reset == FALSE)
  {
    // First get some dimension info
    gslider->is_changed = TRUE;
    const gdouble newposition = _get_position_from_screen(widget, event->x);
    const gint direction = gslider->position[selected] <= newposition ? MOVE_RIGHT : MOVE_LEFT;

    _slider_move(widget, selected, newposition, direction);

    gtk_widget_queue_draw(widget);

    gslider->is_dragging = FALSE;
    if(gslider->timeout_handle) g_source_remove(gslider->timeout_handle);
    gslider->timeout_handle = 0;
    g_signal_emit_by_name(G_OBJECT(widget), "value-changed");
  }
  return TRUE;
}

static gboolean _gradient_slider_scroll_event(GtkWidget *widget, GdkEventScroll *event)
{
  GtkDarktableGradientSlider *gslider = DTGTK_GRADIENT_SLIDER(widget);
  const gint selected = _get_active_marker(gslider);
  if(selected == -1) return TRUE;

  gtk_widget_grab_focus(widget);

  gdouble delta_y;
  if(dt_gui_get_scroll_deltas(event, NULL, &delta_y))
  {
    delta_y *= -gslider->increment;
    return _gradient_slider_add_delta_internal(widget, delta_y, event->state, selected);
  }

  return TRUE;
}

static gboolean _gradient_slider_key_press_event(GtkWidget *widget, GdkEventKey *event)
{
  GtkDarktableGradientSlider *gslider = DTGTK_GRADIENT_SLIDER(widget);

  const gint selected = _get_active_marker(gslider);
  if(selected == -1) return TRUE;

  int handled = 0;
  float delta = 0.0f;

  if(event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_KP_Up || event->keyval == GDK_KEY_Right
     || event->keyval == GDK_KEY_KP_Right)
  {
    handled = 1;
    delta = gslider->increment;
  }
  else if(event->keyval == GDK_KEY_Down || event->keyval == GDK_KEY_KP_Down || event->keyval == GDK_KEY_Left
          || event->keyval == GDK_KEY_KP_Left)
  {
    handled = 1;
    delta = -gslider->increment;
  }

  if(!handled) return TRUE;

  return _gradient_slider_add_delta_internal(widget, delta, event->state, selected);
}

static void _gradient_slider_class_init(GtkDarktableGradientSliderClass *klass)
{
  GtkWidgetClass *widget_class = (GtkWidgetClass *)klass;

  widget_class->realize = _gradient_slider_realize;
  widget_class->draw = _gradient_slider_draw;
  widget_class->destroy = _gradient_slider_destroy;

  widget_class->enter_notify_event = _gradient_slider_enter_notify_event;
  widget_class->leave_notify_event = _gradient_slider_leave_notify_event;
  widget_class->button_press_event = _gradient_slider_button_press;
  widget_class->button_release_event = _gradient_slider_button_release;
  widget_class->motion_notify_event = _gradient_slider_motion_notify;
  widget_class->scroll_event = _gradient_slider_scroll_event;
  widget_class->key_press_event = _gradient_slider_key_press_event;

  _signals[VALUE_CHANGED] = g_signal_new("value-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
                                         NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

static void _gradient_slider_init(GtkDarktableGradientSlider *slider)
{
  slider->is_dragging = slider->is_changed = slider->do_reset = slider->is_entered = 0;
  slider->timeout_handle = 0;
  slider->selected = slider->positions == 1 ? 0 : -1;
  slider->active = -1;
}

static void _gradient_slider_realize(GtkWidget *widget)
{
  GdkWindowAttr attributes;
  guint attributes_mask;

  g_return_if_fail(widget != NULL);
  g_return_if_fail(DTGTK_IS_GRADIENT_SLIDER(widget));

  gtk_widget_set_realized(widget, TRUE);

  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);
  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.x = allocation.x;
  attributes.y = allocation.y;
  attributes.width = DT_PIXEL_APPLY_DPI(100);
  attributes.height = DT_PIXEL_APPLY_DPI(17);

  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.event_mask = gtk_widget_get_events(widget) | GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK
                          | GDK_BUTTON_RELEASE_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK
                          | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK | GDK_POINTER_MOTION_MASK
                          | darktable.gui->scroll_mask;
  attributes_mask = GDK_WA_X | GDK_WA_Y;

  gtk_widget_set_can_focus(GTK_WIDGET(widget), TRUE);

  gtk_widget_set_window(widget,
                        gdk_window_new(gtk_widget_get_parent_window(widget), &attributes, attributes_mask));

  gdk_window_set_user_data(gtk_widget_get_window(widget), widget);
}


static void _gradient_slider_destroy(GtkWidget *widget)
{
  GtkDarktableGradientSliderClass *klass;
  g_return_if_fail(widget != NULL);
  g_return_if_fail(DTGTK_IS_GRADIENT_SLIDER(widget));

  if(DTGTK_GRADIENT_SLIDER(widget)->timeout_handle)
    g_source_remove(DTGTK_GRADIENT_SLIDER(widget)->timeout_handle);
  DTGTK_GRADIENT_SLIDER(widget)->timeout_handle = 0;

  if(DTGTK_GRADIENT_SLIDER(widget)->colors)
  {
    g_list_free_full(DTGTK_GRADIENT_SLIDER(widget)->colors, g_free);
    DTGTK_GRADIENT_SLIDER(widget)->colors = NULL;
  }

  // FIXME: or it should be g_type_class_ref () ?
  klass = g_type_class_peek(gtk_widget_get_type());
  if(GTK_WIDGET_CLASS(klass)->destroy)
  {
    (*GTK_WIDGET_CLASS(klass)->destroy)(widget);
  }
}

static gboolean _gradient_slider_draw(GtkWidget *widget, cairo_t *cr)
{
  GtkDarktableGradientSlider *gslider = DTGTK_GRADIENT_SLIDER(widget);

  assert(gslider->position > 0);

  g_return_val_if_fail(widget != NULL, FALSE);
  g_return_val_if_fail(DTGTK_IS_GRADIENT_SLIDER(widget), FALSE);

  GdkRGBA color;
  GtkStyleContext *context = gtk_widget_get_style_context(widget);
  gtk_style_context_get_color(context, gtk_widget_get_state_flags(widget), &color);

  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);
  int width = allocation.width;
  int height = allocation.height;
  int margins = gslider->margins;

  // Begin cairo drawing
  // First build the cairo gradient and then fill the gradient
  float gheight = height / 2.0;
  float gwidth = width - 2 * margins;
  GList *current = NULL;
  cairo_pattern_t *gradient = NULL;
  if((current = g_list_first(gslider->colors)) != NULL)
  {
    gradient = cairo_pattern_create_linear(0, 0, gwidth, gheight);
    do
    {
      _gradient_slider_stop_t *stop = (_gradient_slider_stop_t *)current->data;
      cairo_pattern_add_color_stop_rgb(gradient, stop->position, stop->color.red, stop->color.green,
                                       stop->color.blue);
    } while((current = g_list_next(current)) != NULL);
  }

  if(gradient != NULL) // Do we got a gradient, lets draw it
  {
    cairo_set_line_width(cr, 0.1);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_source(cr, gradient);
    cairo_rectangle(cr, margins, (height - gheight) / 2.0, gwidth, gheight);
    cairo_fill(cr);
    cairo_stroke(cr);
    cairo_pattern_destroy(gradient);
  }

  // Lets draw position arrows

  cairo_set_source_rgba(cr, color.red, color.green, color.blue, 1.0);


  // do we have a picker value to draw?
  if(!isnan(gslider->picker[0]))
  {
    int vx_min = _scale_to_screen(widget, CLAMP_RANGE(gslider->picker[1], 0.0, 1.0));
    int vx_max = _scale_to_screen(widget, CLAMP_RANGE(gslider->picker[2], 0.0, 1.0));
    int vx_avg = _scale_to_screen(widget, CLAMP_RANGE(gslider->picker[0], 0.0, 1.0));

    cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.33);

    cairo_rectangle(cr, vx_min, (height - gheight) / 2.0, fmax((float)vx_max - vx_min, 0.0f), gheight);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, color.red, color.green, color.blue, 1.0);

    cairo_move_to(cr, vx_avg, (height - gheight) / 2.0);
    cairo_line_to(cr, vx_avg, (height + gheight) / 2.0);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
  }

  int indirect[GRADIENT_SLIDER_MAX_POSITIONS];
  const gint selected = _get_active_marker(gslider);
  for(int k = 0; k < gslider->positions; k++)
    indirect[k] = (selected == -1) ? k : (selected + 1 + k) % gslider->positions;


  for(int k = 0; k < gslider->positions; k++)
  {
    const int l = indirect[k];
    const int vx = _scale_to_screen(widget, gslider->position[l]);
    const int mk = gslider->marker[l];
    const int sz = (mk & (1 << 3)) ? 13 : 10; // big or small marker?

    // FIXME: enable this when enter/leave event is working again
    if(l == selected /*&& (gslider->is_entered == TRUE || gslider->is_dragging == TRUE)*/)
    {
      cairo_set_source_rgba(cr, color.red, color.green, color.blue, 1.0);
    }
    else
    {
      cairo_set_source_rgba(cr, color.red * 0.8, color.green * 0.8, color.blue * 0.8, 1.0);
    }


    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);

    if(mk & 0x04) /* upper arrow */
    {
      if(mk & 0x01) /* filled */
        dtgtk_cairo_paint_solid_triangle(cr, (vx - DT_PIXEL_APPLY_DPI(sz) * 0.5),
                                         sz < 10 ? DT_PIXEL_APPLY_DPI(1) : DT_PIXEL_APPLY_DPI(-2),
                                         DT_PIXEL_APPLY_DPI(sz), DT_PIXEL_APPLY_DPI(sz), CPF_DIRECTION_DOWN, NULL);
      else
        dtgtk_cairo_paint_triangle(cr, (vx - DT_PIXEL_APPLY_DPI(sz) * 0.5),
                                   sz < 10 ? DT_PIXEL_APPLY_DPI(1) : DT_PIXEL_APPLY_DPI(-2),
                                   DT_PIXEL_APPLY_DPI(sz), DT_PIXEL_APPLY_DPI(sz), CPF_DIRECTION_DOWN, NULL);
    }

    if(mk & 0x02) /* lower arrow */
    {
      if(mk & 0x01) /* filled */
        dtgtk_cairo_paint_solid_triangle(cr, (vx - DT_PIXEL_APPLY_DPI(sz) * 0.5),
                                         sz < 10 ? height - DT_PIXEL_APPLY_DPI(6) : height
                                                                                    - DT_PIXEL_APPLY_DPI(11),
                                         DT_PIXEL_APPLY_DPI(sz), DT_PIXEL_APPLY_DPI(sz), CPF_DIRECTION_UP, NULL);
      else
        dtgtk_cairo_paint_triangle(cr, (vx - DT_PIXEL_APPLY_DPI(sz) * 0.5),
                                   sz < 10 ? height - DT_PIXEL_APPLY_DPI(6) : height - DT_PIXEL_APPLY_DPI(11),
                                   DT_PIXEL_APPLY_DPI(sz), DT_PIXEL_APPLY_DPI(sz), CPF_DIRECTION_UP, NULL);
    }
  }

  return FALSE;
}

// Public functions for multivalue type
GtkWidget *dtgtk_gradient_slider_multivalue_new(gint positions)
{
  assert(positions <= GRADIENT_SLIDER_MAX_POSITIONS);

  GtkDarktableGradientSlider *gslider;
  gslider = g_object_new(dtgtk_gradient_slider_get_type(), NULL);
  gslider->positions = positions;
  gslider->is_resettable = FALSE;
  gslider->is_entered = FALSE;
  gslider->picker[0] = gslider->picker[1] = gslider->picker[2] = NAN;
  gslider->selected = positions == 1 ? 0 : -1;
  gslider->active = -1;
  gslider->increment = DTGTK_GRADIENT_SLIDER_DEFAULT_INCREMENT;
  gslider->margins = GRADIENT_SLIDER_MARGINS_DEFAULT;
  for(int k = 0; k < positions; k++) gslider->position[k] = 0.0;
  for(int k = 0; k < positions; k++) gslider->resetvalue[k] = 0.0;
  for(int k = 0; k < positions; k++) gslider->marker[k] = GRADIENT_SLIDER_MARKER_LOWER_FILLED;
  return (GtkWidget *)gslider;
}


GtkWidget *dtgtk_gradient_slider_multivalue_new_with_color(GdkRGBA start, GdkRGBA end, gint positions)
{
  assert(positions <= GRADIENT_SLIDER_MAX_POSITIONS);

  GtkDarktableGradientSlider *gslider;
  gslider = g_object_new(dtgtk_gradient_slider_get_type(), NULL);
  gslider->positions = positions;
  gslider->is_resettable = FALSE;
  gslider->is_entered = FALSE;
  gslider->picker[0] = gslider->picker[1] = gslider->picker[2] = NAN;
  gslider->selected = positions == 1 ? 0 : -1;
  gslider->active = -1;
  gslider->increment = DTGTK_GRADIENT_SLIDER_DEFAULT_INCREMENT;
  gslider->margins = GRADIENT_SLIDER_MARGINS_DEFAULT;
  for(int k = 0; k < positions; k++) gslider->position[k] = 0.0;
  for(int k = 0; k < positions; k++) gslider->resetvalue[k] = 0.0;
  for(int k = 0; k < positions; k++) gslider->marker[k] = GRADIENT_SLIDER_MARKER_LOWER_FILLED;

  // Construct gradient start color
  _gradient_slider_stop_t *gc = (_gradient_slider_stop_t *)g_malloc(sizeof(_gradient_slider_stop_t));
  gc->position = 0.0;
  memcpy(&gc->color, &start, sizeof(GdkRGBA));
  gslider->colors = g_list_append(gslider->colors, gc);

  // Construct gradient stop color
  gc = (_gradient_slider_stop_t *)g_malloc(sizeof(_gradient_slider_stop_t));
  gc->position = 1.0;
  memcpy(&gc->color, &end, sizeof(GdkRGBA));
  gslider->colors = g_list_append(gslider->colors, gc);


  return (GtkWidget *)gslider;
}


gint _list_find_by_position(gconstpointer a, gconstpointer b)
{
  _gradient_slider_stop_t *stop = (_gradient_slider_stop_t *)a;
  gfloat position = *((gfloat *)b);
  return (gint)((stop->position * 100.0) - (position * 100.0));
}

void dtgtk_gradient_slider_multivalue_set_stop(GtkDarktableGradientSlider *gslider, gfloat position,
                                               GdkRGBA color)
{
  // First find color at position, if exists update color, otherwise create a new stop at position.
  GList *current = g_list_find_custom(gslider->colors, (gpointer)&position, _list_find_by_position);
  if(current != NULL)
  {
    memcpy(&((_gradient_slider_stop_t *)current->data)->color, &color, sizeof(GdkRGBA));
  }
  else
  {
    // stop didn't exist lets add it
    _gradient_slider_stop_t *gc = (_gradient_slider_stop_t *)g_malloc(sizeof(_gradient_slider_stop_t));
    gc->position = position;
    memcpy(&gc->color, &color, sizeof(GdkRGBA));
    gslider->colors = g_list_append(gslider->colors, gc);
  }
}

void dtgtk_gradient_slider_multivalue_clear_stops(GtkDarktableGradientSlider *gslider)
{
  g_list_free_full(gslider->colors, g_free);
  gslider->colors = NULL;
}

GType dtgtk_gradient_slider_multivalue_get_type()
{
  static GType dtgtk_gradient_slider_type = 0;
  if(!dtgtk_gradient_slider_type)
  {
    static const GTypeInfo dtgtk_gradient_slider_info = {
      sizeof(GtkDarktableGradientSliderClass), (GBaseInitFunc)NULL, (GBaseFinalizeFunc)NULL,
      (GClassInitFunc)_gradient_slider_class_init, NULL, /* class_finalize */
      NULL,                                              /* class_data */
      sizeof(GtkDarktableGradientSlider), 0,             /* n_preallocs */
      (GInstanceInitFunc)_gradient_slider_init,
    };
    dtgtk_gradient_slider_type = g_type_register_static(GTK_TYPE_WIDGET, "GtkDarktableGradientSlider",
                                                        &dtgtk_gradient_slider_info, 0);
  }
  return dtgtk_gradient_slider_type;
}

gdouble dtgtk_gradient_slider_multivalue_get_value(GtkDarktableGradientSlider *gslider, gint pos)
{
  assert(pos <= gslider->positions);

  return gslider->position[pos];
}

void dtgtk_gradient_slider_multivalue_set_value(GtkDarktableGradientSlider *gslider, gdouble value, gint pos)
{
  assert(pos <= gslider->positions);

  gslider->position[pos] = value;
  gslider->selected = gslider->positions == 1 ? 0 : -1;
  g_signal_emit_by_name(G_OBJECT(gslider), "value-changed");
  gtk_widget_queue_draw(GTK_WIDGET(gslider));
}

void dtgtk_gradient_slider_multivalue_set_values(GtkDarktableGradientSlider *gslider, gdouble *values)
{
  for(int k = 0; k < gslider->positions; k++) gslider->position[k] = values[k];
  gslider->selected = gslider->positions == 1 ? 0 : -1;
  g_signal_emit_by_name(G_OBJECT(gslider), "value-changed");
  gtk_widget_queue_draw(GTK_WIDGET(gslider));
}

void dtgtk_gradient_slider_multivalue_set_marker(GtkDarktableGradientSlider *gslider, gint mark, gint pos)
{
  assert(pos <= gslider->positions);

  gslider->marker[pos] = mark;
  gtk_widget_queue_draw(GTK_WIDGET(gslider));
}

void dtgtk_gradient_slider_multivalue_set_markers(GtkDarktableGradientSlider *gslider, gint *markers)
{
  for(int k = 0; k < gslider->positions; k++) gslider->marker[k] = markers[k];
  gtk_widget_queue_draw(GTK_WIDGET(gslider));
}

void dtgtk_gradient_slider_multivalue_set_resetvalue(GtkDarktableGradientSlider *gslider, gdouble value,
                                                     gint pos)
{
  assert(pos <= gslider->positions);

  gslider->resetvalue[pos] = value;
  gslider->is_resettable = TRUE;
}

void dtgtk_gradient_slider_multivalue_set_resetvalues(GtkDarktableGradientSlider *gslider, gdouble *values)
{
  for(int k = 0; k < gslider->positions; k++) gslider->resetvalue[k] = values[k];
  gslider->is_resettable = TRUE;
}

void dtgtk_gradient_slider_multivalue_set_picker(GtkDarktableGradientSlider *gslider, gdouble value)
{
  gslider->picker[0] = gslider->picker[1] = gslider->picker[2] = value;
  gtk_widget_queue_draw(GTK_WIDGET(gslider));
}

void dtgtk_gradient_slider_multivalue_set_picker_meanminmax(GtkDarktableGradientSlider *gslider, gdouble mean,
                                                            gdouble min, gdouble max)
{
  gslider->picker[0] = mean;
  gslider->picker[1] = min;
  gslider->picker[2] = max;
  gtk_widget_queue_draw(GTK_WIDGET(gslider));
}

void dtgtk_gradient_slider_multivalue_set_margins(GtkDarktableGradientSlider *gslider, gint value)
{
  gslider->margins = value;
}


gboolean dtgtk_gradient_slider_multivalue_is_dragging(GtkDarktableGradientSlider *gslider)
{
  return gslider->is_dragging;
}

void dtgtk_gradient_slider_multivalue_set_increment(GtkDarktableGradientSlider *gslider, gdouble value)
{
  gslider->increment = value;
}

// Public functions for single value type
GtkWidget *dtgtk_gradient_slider_new()
{
  return dtgtk_gradient_slider_multivalue_new(1);
}

GtkWidget *dtgtk_gradient_slider_new_with_color(GdkRGBA start, GdkRGBA end)
{
  return dtgtk_gradient_slider_multivalue_new_with_color(start, end, 1);
}

void dtgtk_gradient_slider_set_stop(GtkDarktableGradientSlider *gslider, gfloat position, GdkRGBA color)
{
  dtgtk_gradient_slider_multivalue_set_stop(gslider, position, color);
}

GType dtgtk_gradient_slider_get_type()
{
  return dtgtk_gradient_slider_multivalue_get_type();
}

gdouble dtgtk_gradient_slider_get_value(GtkDarktableGradientSlider *gslider)
{
  return dtgtk_gradient_slider_multivalue_get_value(gslider, 0);
}

void dtgtk_gradient_slider_multivalue_get_values(GtkDarktableGradientSlider *gslider, gdouble *values)
{
  for(int k = 0; k < gslider->positions; k++) values[k] = gslider->position[k];
}

void dtgtk_gradient_slider_set_value(GtkDarktableGradientSlider *gslider, gdouble value)
{
  dtgtk_gradient_slider_multivalue_set_value(gslider, value, 0);
}

void dtgtk_gradient_slider_set_marker(GtkDarktableGradientSlider *gslider, gint mark)
{
  dtgtk_gradient_slider_multivalue_set_marker(gslider, mark, 0);
}

void dtgtk_gradient_slider_set_resetvalue(GtkDarktableGradientSlider *gslider, gdouble value)
{
  dtgtk_gradient_slider_multivalue_set_resetvalue(gslider, value, 0);
}

void dtgtk_gradient_slider_set_picker(GtkDarktableGradientSlider *gslider, gdouble value)
{
  gslider->picker[0] = gslider->picker[1] = gslider->picker[2] = value;
  gtk_widget_queue_draw(GTK_WIDGET(gslider));
}

void dtgtk_gradient_slider_set_picker_meanminmax(GtkDarktableGradientSlider *gslider, gdouble mean,
                                                 gdouble min, gdouble max)
{
  gslider->picker[0] = mean;
  gslider->picker[1] = min;
  gslider->picker[2] = max;
  gtk_widget_queue_draw(GTK_WIDGET(gslider));
}

void dtgtk_gradient_slider_set_margins(GtkDarktableGradientSlider *gslider, gint value)
{
  gslider->margins = value;
}

gboolean dtgtk_gradient_slider_is_dragging(GtkDarktableGradientSlider *gslider)
{
  return gslider->is_dragging;
}

void dtgtk_gradient_slider_set_increment(GtkDarktableGradientSlider *gslider, gdouble value)
{
  gslider->increment = value;
}


// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
