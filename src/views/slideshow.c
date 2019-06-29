/*
    This file is part of darktable,
    copyright (c) 2013 johannes hanika.
    copyright (c) 2019 pascal obry.

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

#include "common/collection.h"
#include "common/colorspaces.h"
#include "common/debug.h"
#include "common/dtpthread.h"
#include "common/imageio.h"
#include "common/imageio_module.h"
#include "control/conf.h"
#include "control/control.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "views/view.h"
#include "views/view_api.h"

#include <gdk/gdkkeysyms.h>
#include <stdint.h>

DT_MODULE(1)

typedef enum dt_slideshow_event_t
{
  S_REQUEST_STEP,
  S_REQUEST_STEP_BACK,
} dt_slideshow_event_t;

typedef enum dt_slideshow_slot_t
{
  S_LEFT      = 0,
  S_CURRENT   = 1,
  S_RIGHT     = 2,
  S_SLOT_LAST = 3
} dt_slideshow_slot_t;

typedef struct _slideshow_buf_t
{
  uint32_t *buf;
  uint32_t width;
  uint32_t height;
  int32_t rank;
  gboolean invalidated;
} dt_slideshow_buf_t;

typedef struct dt_slideshow_t
{
  int32_t col_count;
  uint32_t width, height;

  // buffers
  dt_slideshow_buf_t buf[S_SLOT_LAST];
  gboolean init_phase;

  // state machine stuff for image transitions:
  dt_pthread_mutex_t lock;

  uint32_t auto_advance;
  int delay;

  // some magic to hide the mouse pointer
  guint mouse_timeout;
} dt_slideshow_t;

typedef struct dt_slideshow_format_t
{
  int max_width, max_height;
  int width, height;
  char style[128];
  gboolean style_append;
  dt_slideshow_t *d;
  dt_slideshow_slot_t slot;
  int32_t rank;
} dt_slideshow_format_t;

// fwd declare state machine mechanics:
static void _step_state(dt_slideshow_t *d, dt_slideshow_event_t event);
static dt_job_t *process_job_create(dt_slideshow_t *d);

// callbacks for in-memory export
static int bpp(dt_imageio_module_data_t *data)
{
  return 8;
}

static int levels(dt_imageio_module_data_t *data)
{
  return IMAGEIO_RGB | IMAGEIO_INT8;
}

static const char *mime(dt_imageio_module_data_t *data)
{
  return "memory";
}

static int write_image(dt_imageio_module_data_t *datai, const char *filename, const void *in,
                       dt_colorspaces_color_profile_type_t over_type, const char *over_filename,
                       void *exif, int exif_len, int imgid, int num, int total, dt_dev_pixelpipe_t *pipe)
{
  dt_slideshow_format_t *data = (dt_slideshow_format_t *)datai;
  dt_pthread_mutex_lock(&data->d->lock);

  const dt_slideshow_slot_t slot = data->slot;

  if(slot>=S_LEFT && slot<=S_RIGHT && data->d->buf[slot].buf)
  {
    // might have been cleaned up when leaving slide show
    memcpy(data->d->buf[slot].buf, in, sizeof(uint32_t) * datai->width * datai->height);
    data->d->buf[slot].width = datai->width;
    data->d->buf[slot].height = datai->height;
  }
  dt_pthread_mutex_unlock(&data->d->lock);
  return 0;
}

static void shift_left(dt_slideshow_t *d)
{
  uint32_t *tmp_buf = d->buf[S_LEFT].buf;

  for(int k=S_LEFT; k<S_RIGHT; k++)
  {
    d->buf[k].buf         = d->buf[k+1].buf;
    d->buf[k].rank        = d->buf[k+1].rank;
    d->buf[k].width       = d->buf[k+1].width;
    d->buf[k].height      = d->buf[k+1].height;
    d->buf[k].invalidated = d->buf[k+1].invalidated;
  }
  d->buf[S_RIGHT].invalidated = TRUE;
  d->buf[S_RIGHT].rank = d->buf[S_CURRENT].rank + 1;
  d->buf[S_RIGHT].buf = tmp_buf;
}

static void shift_right(dt_slideshow_t *d)
{
  uint32_t *tmp_buf = d->buf[S_RIGHT].buf;

  for(int k=S_RIGHT; k>S_LEFT; k--)
  {
    d->buf[k].buf         = d->buf[k-1].buf;
    d->buf[k].rank        = d->buf[k-1].rank;
    d->buf[k].width       = d->buf[k-1].width;
    d->buf[k].height      = d->buf[k-1].height;
    d->buf[k].invalidated = d->buf[k-1].invalidated;
  }
  d->buf[S_LEFT].invalidated = TRUE;
  d->buf[S_LEFT].rank = d->buf[S_CURRENT].rank - 1;
  d->buf[S_LEFT].buf = tmp_buf;
}

static void requeue_job(dt_slideshow_t *d)
{
  dt_control_add_job(darktable.control, DT_JOB_QUEUE_USER_BG, process_job_create(d));
}

static int process_image(dt_slideshow_t *d, dt_slideshow_slot_t slot)
{
  dt_imageio_module_format_t buf;
  buf.mime = mime;
  buf.levels = levels;
  buf.bpp = bpp;
  buf.write_image = write_image;

  dt_slideshow_format_t dat;
  dat.width = dat.max_width = d->width;
  dat.height = dat.max_height = d->height;
  dat.style[0] = '\0';
  dat.d = d;
  dat.slot = slot;
  dat.rank = d->buf[slot].rank;

  // get random image id from sql
  int32_t id = 0;

  const int32_t rank = dat.rank;

  if(rank<0 || rank>=d->col_count)
  {
    d->buf[slot].invalidated = FALSE;
    return 1;
  }

  const gchar *query = dt_collection_get_query(darktable.collection);
  if(!query) return 1;
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), query, -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, rank);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, 1);
  if(sqlite3_step(stmt) == SQLITE_ROW) id = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  // this is a little slow, might be worth to do an option:
  const gboolean high_quality = dt_conf_get_bool("plugins/slideshow/high_quality");

  if(id)
  {
    // the flags are: ignore exif, display byteorder, high quality, upscale, thumbnail
    dt_imageio_export_with_flags(id, "unused", &buf, (dt_imageio_module_data_t *)&dat, TRUE, TRUE,
                                 high_quality, TRUE, FALSE, NULL, FALSE, DT_COLORSPACE_DISPLAY,
                                 NULL, DT_INTENT_LAST, NULL, NULL, 1, 1);
  }

  d->buf[slot].invalidated = FALSE;

  return 0;
}

static int32_t process_job_run(dt_job_t *job)
{
  dt_slideshow_t *d = dt_control_job_get_params(job);

  if(d->buf[S_CURRENT].invalidated)
  {
    process_image(d, S_CURRENT);
    dt_control_queue_redraw_center();
  }
  else if(d->buf[S_RIGHT].invalidated)
  {
    process_image(d, S_RIGHT);
  }
  else if(d->buf[S_LEFT].invalidated)
  {
    process_image(d, S_LEFT);
  }

  // any other slot to fill?
  if(d->buf[S_LEFT].invalidated || d->buf[S_CURRENT].invalidated || d->buf[S_RIGHT].invalidated)
    requeue_job(d);

  return 0;
}

static dt_job_t *process_job_create(dt_slideshow_t *d)
{
  dt_job_t *job = dt_control_job_create(&process_job_run, "process slideshow image");
  if(!job) return NULL;
  dt_control_job_set_params(job, d, NULL);
  return job;
}

static gboolean auto_advance(gpointer user_data)
{
  dt_slideshow_t *d = (dt_slideshow_t *)user_data;
  if(!d->auto_advance) return FALSE;
  _step_state(d, S_REQUEST_STEP);
  return FALSE;
}

static void _refresh_display(dt_slideshow_t *d)
{
  if(!d->buf[S_CURRENT].invalidated && d->buf[S_CURRENT].rank >= 0)
    dt_control_queue_redraw_center();
}

// state machine stepping
static void _step_state(dt_slideshow_t *d, dt_slideshow_event_t event)
{
  dt_pthread_mutex_lock(&d->lock);

  if(event == S_REQUEST_STEP)
  {
    if(d->buf[S_CURRENT].rank < d->col_count - 1)
    {
      shift_left(d);
      d->buf[S_RIGHT].invalidated = TRUE;
      d->buf[S_RIGHT].rank = d->buf[S_CURRENT].rank + 1;
      _refresh_display(d);
      requeue_job(d);
    }
    else
    {
      dt_control_log(_("end of images. press any key to return to lighttable mode"));
      d->auto_advance = FALSE;
    }
  }
  else if(event == S_REQUEST_STEP_BACK)
  {
    if(d->buf[S_CURRENT].rank > 0)
    {
      shift_right(d);
      d->buf[S_LEFT].invalidated = TRUE;
      d->buf[S_LEFT].rank = d->buf[S_CURRENT].rank - 1;
      _refresh_display(d);
      requeue_job(d);
    }
    else
    {
      dt_control_log(_("end of images. press any key to return to lighttable mode"));
      d->auto_advance = FALSE;
    }
  }

  if(d->auto_advance) g_timeout_add_seconds(d->delay, auto_advance, d);

  dt_pthread_mutex_unlock(&d->lock);
}

// callbacks for a view module:

const char *name(const dt_view_t *self)
{
  return _("slideshow");
}

uint32_t view(const dt_view_t *self)
{
  return DT_VIEW_SLIDESHOW;
}

void init(dt_view_t *self)
{
  self->data = calloc(1, sizeof(dt_slideshow_t));
  dt_slideshow_t *lib = (dt_slideshow_t *)self->data;
  dt_pthread_mutex_init(&lib->lock, 0);
}


void cleanup(dt_view_t *self)
{
  dt_slideshow_t *lib = (dt_slideshow_t *)self->data;
  dt_pthread_mutex_destroy(&lib->lock);
  free(self->data);
}

int try_enter(dt_view_t *self)
{
  /* verify that there are images to display */
  if(dt_collection_get_count(darktable.collection) != 0)
  {
    return 0;
  }
  else
  {
    dt_control_log(_("there are no images in this collection"));
    return 1;
  }
}

void enter(dt_view_t *self)
{
  dt_slideshow_t *d = (dt_slideshow_t *)self->data;

  dt_control_change_cursor(GDK_BLANK_CURSOR);
  d->mouse_timeout = 0;

  dt_ui_panel_show(darktable.gui->ui, DT_UI_PANEL_LEFT, FALSE, TRUE);
  dt_ui_panel_show(darktable.gui->ui, DT_UI_PANEL_RIGHT, FALSE, TRUE);
  dt_ui_panel_show(darktable.gui->ui, DT_UI_PANEL_TOP, FALSE, TRUE);
  dt_ui_panel_show(darktable.gui->ui, DT_UI_PANEL_BOTTOM, FALSE, TRUE);
  dt_ui_panel_show(darktable.gui->ui, DT_UI_PANEL_CENTER_TOP, FALSE, TRUE);
  dt_ui_panel_show(darktable.gui->ui, DT_UI_PANEL_CENTER_BOTTOM, FALSE, TRUE);

  // also hide arrows
  dt_control_queue_redraw();


  // alloc screen-size double buffer
  GtkWidget *window = dt_ui_main_window(darktable.gui->ui);
  GdkRectangle rect;

#if GTK_CHECK_VERSION(3, 22, 0)
  GdkDisplay *display = gtk_widget_get_display(window);
  GdkMonitor *mon = gdk_display_get_monitor_at_window(display, gtk_widget_get_window(window));
  gdk_monitor_get_geometry(mon, &rect);
#else
  GdkScreen *screen = gtk_widget_get_screen(window);
  if(!screen) screen = gdk_screen_get_default();
  int monitor = gdk_screen_get_monitor_at_window(screen, gtk_widget_get_window(window));
  gdk_screen_get_monitor_geometry(screen, monitor, &rect);
#endif

  dt_pthread_mutex_lock(&d->lock);

  d->width = rect.width * darktable.gui->ppd;
  d->height = rect.height * darktable.gui->ppd;

  for(int k=S_LEFT; k<S_SLOT_LAST; k++)
  {
    d->buf[k].buf = dt_alloc_align(64, sizeof(uint32_t) * d->width * d->height);
    d->buf[k].width =  d->width;
    d->buf[k].height = d->height;
    d->buf[k].invalidated = TRUE;
  }

  // if one selected start with it, otherwise start at the current lighttable offset
  GList *selected = dt_collection_get_selected(darktable.collection, 1);
  gint selrank = -1;

  if(selected)
  {
    const gint selid = GPOINTER_TO_INT(selected->data);
    GList *imgids = dt_collection_get_all(darktable.collection, -1);

    GList *l = imgids;
    selrank = 0;
    while(l)
    {
      const gint id = GPOINTER_TO_INT(l->data);
      if(id == selid) break;
      selrank++;
      l = g_list_next(l);
    }

    g_list_free(imgids);
  }

  g_list_free(selected);

  d->buf[S_CURRENT].rank = selrank == -1 ? dt_view_lighttable_get_position(darktable.view_manager) : selrank;
  d->buf[S_LEFT].rank = d->buf[S_CURRENT].rank - 1;
  d->buf[S_RIGHT].rank = d->buf[S_CURRENT].rank + 1;

  d->col_count = dt_collection_get_count(darktable.collection);

  d->auto_advance = 0;
  d->delay = dt_conf_get_int("slideshow_delay");
  // restart from beginning, will first increment counter by step and then prefetch
  dt_pthread_mutex_unlock(&d->lock);

  gtk_widget_grab_focus(dt_ui_center(darktable.gui->ui));

  // start first job
  dt_control_add_job(darktable.control, DT_JOB_QUEUE_USER_BG, process_job_create(d));
  dt_control_log(_("waiting to start slideshow"));
}

void leave(dt_view_t *self)
{
  dt_slideshow_t *d = (dt_slideshow_t *)self->data;

  if(d->mouse_timeout > 0) g_source_remove(d->mouse_timeout);
  d->mouse_timeout = 0;
  dt_control_change_cursor(GDK_LEFT_PTR);
  d->auto_advance = 0;
  dt_view_lighttable_set_position(darktable.view_manager, d->buf[S_CURRENT].rank);
  dt_pthread_mutex_lock(&d->lock);

  for(int k=S_LEFT; k<S_SLOT_LAST; k++)
  {
    dt_free_align(d->buf[k].buf);
    d->buf[k].buf = NULL;
  }
  dt_pthread_mutex_unlock(&d->lock);
}

void expose(dt_view_t *self, cairo_t *cr, int32_t width, int32_t height, int32_t pointerx, int32_t pointery)
{
  // draw front buffer.
  dt_slideshow_t *d = (dt_slideshow_t *)self->data;

  dt_pthread_mutex_lock(&d->lock);
  cairo_paint(cr);

  const dt_slideshow_buf_t *slot = &(d->buf[S_CURRENT]);

  if(slot->buf && slot->rank >= 0 && !slot->invalidated)
  {
    // cope with possible resize of the window
    const float tr_width = d->width < slot->width ? 0.f : (d->width - slot->width) * .5f / darktable.gui->ppd;
    const float tr_height = d->height < slot->height ? 0.f : (d->height - slot->height) * .5f / darktable.gui->ppd;

    cairo_save(cr);
    cairo_translate(cr, tr_width, tr_height);
    cairo_surface_t *surface = NULL;
    const int32_t stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, slot->width);
    surface = dt_cairo_image_surface_create_for_data((uint8_t *)slot->buf, CAIRO_FORMAT_RGB24, slot->width,
                                                     slot->height, stride);
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_rectangle(cr, 0, 0, slot->width/darktable.gui->ppd, slot->height/darktable.gui->ppd);
    cairo_fill(cr);
    cairo_surface_destroy(surface);
    cairo_restore(cr);
  }

  // adjust image size to window size
  d->width = width * darktable.gui->ppd;
  d->height = height * darktable.gui->ppd;
  dt_pthread_mutex_unlock(&d->lock);
}

static gboolean _hide_mouse(gpointer user_data)
{
  dt_view_t *self = (dt_view_t *)user_data;
  dt_slideshow_t *d = (dt_slideshow_t *)self->data;
  d->mouse_timeout = 0;
  dt_control_change_cursor(GDK_BLANK_CURSOR);
  return FALSE;
}


void mouse_moved(dt_view_t *self, double x, double y, double pressure, int which)
{
  dt_slideshow_t *d = (dt_slideshow_t *)self->data;

  if(d->mouse_timeout > 0) g_source_remove(d->mouse_timeout);
  else dt_control_change_cursor(GDK_LEFT_PTR);
  d->mouse_timeout = g_timeout_add_seconds(1, _hide_mouse, self);
}


int button_released(dt_view_t *self, double x, double y, int which, uint32_t state)
{
  return 0;
}


int button_pressed(dt_view_t *self, double x, double y, double pressure, int which, int type, uint32_t state)
{
  dt_slideshow_t *d = (dt_slideshow_t *)self->data;

  if(which == 1)
    _step_state(d, S_REQUEST_STEP);
  else if(which == 3)
    _step_state(d, S_REQUEST_STEP_BACK);
  else
    return 1;

  return 0;
}

int key_released(dt_view_t *self, guint key, guint state)
{
  return 0;
}

int key_pressed(dt_view_t *self, guint key, guint state)
{
  dt_slideshow_t *d = (dt_slideshow_t *)self->data;
  dt_control_accels_t *accels = &darktable.control->accels;

  if(key == accels->slideshow_start.accel_key && state == accels->slideshow_start.accel_mods)
  {
    if(!d->auto_advance)
    {
      d->auto_advance = 1;
      _step_state(d, S_REQUEST_STEP);
    }
    else
    {
      d->auto_advance = 0;
      dt_control_log(_("slideshow paused"));
    }
    return 0;
  }
  else if(key == GDK_KEY_Up || key == GDK_KEY_KP_Add)
  {
    d->delay = CLAMP(d->delay + 1, 1, 60);
    dt_control_log(ngettext("slideshow delay set to %d second", "slideshow delay set to %d seconds", d->delay), d->delay);
    dt_conf_set_int("slideshow_delay", d->delay);
    return 0;
  }
  else if(key == GDK_KEY_Down || key == GDK_KEY_KP_Subtract)
  {
    d->delay = CLAMP(d->delay - 1, 1, 60);
    dt_control_log(ngettext("slideshow delay set to %d second", "slideshow delay set to %d seconds", d->delay), d->delay);
    dt_conf_set_int("slideshow_delay", d->delay);
    return 0;
  }
  else if(key == GDK_KEY_Left || key == GDK_KEY_Shift_L)
  {
    _step_state(d, S_REQUEST_STEP_BACK);
    return 0;
  }
  else if(key == GDK_KEY_Right || key == GDK_KEY_Shift_R)
  {
    _step_state(d, S_REQUEST_STEP);
    return 0;
  }

  // go back to lt mode
  dt_ctl_switch_mode_to("lighttable");
  return 0;
}

void init_key_accels(dt_view_t *self)
{
  dt_accel_register_view(self, NC_("accel", "start and stop"), GDK_KEY_space, 0);
}

void connect_key_accels(dt_view_t *self)
{
}

GSList *mouse_actions(const dt_view_t *self)
{
  GSList *lm = NULL;
  dt_mouse_action_t *a = NULL;

  a = (dt_mouse_action_t *)calloc(1, sizeof(dt_mouse_action_t));
  a->action = DT_MOUSE_ACTION_LEFT;
  g_strlcpy(a->name, _("go to next image"), sizeof(a->name));
  lm = g_slist_append(lm, a);

  a = (dt_mouse_action_t *)calloc(1, sizeof(dt_mouse_action_t));
  a->action = DT_MOUSE_ACTION_RIGHT;
  g_strlcpy(a->name, _("go to previous image"), sizeof(a->name));
  lm = g_slist_append(lm, a);

  return lm;
}
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
