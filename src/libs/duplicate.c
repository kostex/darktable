/*
    This file is part of darktable,
    copyright (c) 2016 Aldric Renaudin.

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

#include "common/darktable.h"
#include "common/debug.h"
#include "common/collection.h"
#include "common/metadata.h"
#include "common/mipmap_cache.h"
#include "common/history.h"
#include "common/styles.h"
#include "common/selection.h"
#include "control/control.h"
#include "control/conf.h"
#include "develop/develop.h"
#include "libs/lib.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "gui/styles.h"

#define DUPLICATE_COMPARE_SIZE 40

DT_MODULE(1)

typedef enum _lib_duplicate_select_t
{
  DT_DUPLICATE_SELECT_NONE = 0,
  DT_DUPLICATE_SELECT_FIRST = 1,
  DT_DUPLICATE_SELECT_CURRENT = 2
} dt_lib_duplicate_select_t;

typedef struct dt_lib_duplicate_t
{
  GtkWidget *duplicate_box;
  int imgid;
  gboolean busy;
  int cur_final_width;
  int cur_final_height;
  gboolean allow_zoom;

  dt_lib_duplicate_select_t select;

  int32_t buf_width;
  int32_t buf_height;
  cairo_surface_t *surface;
  uint8_t *rgbbuf;
  int buf_mip;
  int buf_timestamp;
} dt_lib_duplicate_t;

const char *name(dt_lib_module_t *self)
{
  return _("duplicate manager");
}

const char **views(dt_lib_module_t *self)
{
  static const char *v[] = {"darkroom", NULL};
  return v;
}

uint32_t container(dt_lib_module_t *self)
{
  return DT_UI_CONTAINER_PANEL_LEFT_CENTER;
}

int position()
{
  return 850;
}

static void _lib_duplicate_init_callback(gpointer instance, dt_lib_module_t *self);

static gboolean _lib_duplicate_caption_out_callback(GtkWidget *widget, GdkEvent *event, dt_lib_module_t *self)
{
  int imgid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),"imgid"));

  // we write the content of the textbox to the caption field
  dt_metadata_set(imgid, "Xmp.dc.title", gtk_entry_get_text(GTK_ENTRY(widget)));
  dt_image_synch_xmp(imgid);

  return FALSE;
}

static void _do_select(int imgid)
{
  // to select the duplicate, we reuse the filmstrip proxy
  dt_selection_select_single(darktable.selection, imgid);
  dt_control_set_mouse_over_id(imgid);
  dt_view_filmstrip_scroll_to_image(darktable.view_manager,imgid,TRUE);
}

static void _lib_duplicate_new_clicked_callback(GtkWidget *widget, GdkEventButton *event, dt_lib_module_t *self)
{
  dt_lib_duplicate_t *d = (dt_lib_duplicate_t *)self->data;
  d->select = DT_DUPLICATE_SELECT_NONE;
  const int imgid = darktable.develop->image_storage.id;
  const int newid = dt_image_duplicate(imgid);
  if (newid <= 0) return;
  dt_history_delete_on_image(newid);
  dt_collection_update_query(darktable.collection);
  // to select the duplicate, we reuse the filmstrip proxy
  dt_view_filmstrip_scroll_to_image(darktable.view_manager,newid,TRUE);
}
static void _lib_duplicate_duplicate_clicked_callback(GtkWidget *widget, GdkEventButton *event, dt_lib_module_t *self)
{
  dt_lib_duplicate_t *d = (dt_lib_duplicate_t *)self->data;
  d->select = DT_DUPLICATE_SELECT_NONE;
  const int imgid = darktable.develop->image_storage.id;
  const int newid = dt_image_duplicate(imgid);
  if (newid <= 0) return;
  dt_history_copy_and_paste_on_image(imgid,newid,FALSE,NULL);
  dt_collection_update_query(darktable.collection);
  // to select the duplicate, we reuse the filmstrip proxy
  dt_view_filmstrip_scroll_to_image(darktable.view_manager,newid,TRUE);
}

static void _lib_duplicate_filmrolls_updated(gpointer instance, gpointer self)
{
  _lib_duplicate_init_callback(NULL, self);
  dt_control_signal_raise(darktable.signals, DT_SIGNAL_COLLECTION_CHANGED);
}

static void _lib_duplicate_delete(GtkButton *button, dt_lib_module_t *self)
{
  dt_lib_duplicate_t *d = (dt_lib_duplicate_t *)self->data;
  const int imgid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "imgid"));

  d->select = (imgid == darktable.develop->image_storage.id) ? DT_DUPLICATE_SELECT_FIRST : DT_DUPLICATE_SELECT_CURRENT;

  dt_selection_select_single(darktable.selection, imgid);
  dt_control_set_mouse_over_id(imgid);
  dt_control_delete_images();
}

static void _lib_duplicate_thumb_press_callback(GtkWidget *widget, GdkEventButton *event, dt_lib_module_t *self)
{
  dt_lib_duplicate_t *d = (dt_lib_duplicate_t *)self->data;
  int imgid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),"imgid"));

  if(event->button == 1)
  {
    if(event->type == GDK_BUTTON_PRESS)
    {
      dt_develop_t *dev = darktable.develop;
      if(!dev) return;

      dt_dev_invalidate(dev);
      dt_control_queue_redraw_center();

      dt_dev_invalidate(darktable.develop);

      d->imgid = imgid;
      int fw, fh;
      fw = fh = 0;
      dt_image_get_final_size(imgid, &fw, &fh);
      d->allow_zoom
          = (d->cur_final_width - fw < DUPLICATE_COMPARE_SIZE && d->cur_final_width - fw > -DUPLICATE_COMPARE_SIZE
             && d->cur_final_height - fh < DUPLICATE_COMPARE_SIZE
             && d->cur_final_height - fh > -DUPLICATE_COMPARE_SIZE);
      dt_control_queue_redraw_center();
    }
    else if(event->type == GDK_2BUTTON_PRESS)
    {
      // to select the duplicate, we reuse the filmstrip proxy
      _do_select(imgid);
    }
  }
}

static void _lib_duplicate_thumb_release_callback(GtkWidget *widget, GdkEventButton *event, dt_lib_module_t *self)
{
  dt_lib_duplicate_t *d = (dt_lib_duplicate_t *)self->data;

  d->imgid = 0;
  if(d->busy) dt_control_log_busy_leave();
  d->busy = FALSE;
  dt_control_queue_redraw_center();
}

void gui_post_expose(dt_lib_module_t *self, cairo_t *cri, int32_t width, int32_t height, int32_t pointerx, int32_t pointery)
{
  dt_lib_duplicate_t *d = (dt_lib_duplicate_t *)self->data;

  if (d->imgid == 0) return;

  const int32_t tb = DT_PIXEL_APPLY_DPI(dt_conf_get_int("plugins/darkroom/ui/border_size"));
  int nw = width-2*tb;
  int nh = height-2*tb;

  dt_develop_t *dev = darktable.develop;
  if(!dev->preview_pipe->backbuf || dev->preview_status != DT_DEV_PIXELPIPE_VALID) return;

  int img_wd = dev->preview_pipe->backbuf_width;
  int img_ht = dev->preview_pipe->backbuf_height;

  // and now we get the values to "fit the screen"
  int nimgw, nimgh;
  if (img_ht*nw > img_wd*nh)
  {
    nimgh = nh;
    nimgw = img_wd*nh/img_ht;
  }
  else
  {
    nimgw = nw;
    nimgh = img_ht*nw/img_wd;
  }

  // if image have too different sizes, we show the full preview not zoomed
  float zoom_x = 0.0f;
  float zoom_y = 0.0f;
  float nz = 1.0f;
  if(d->allow_zoom)
  {
    dt_dev_zoom_t zoom = dt_control_get_dev_zoom();
    const int closeup = dt_control_get_dev_closeup();
    zoom_x = dt_control_get_dev_zoom_x();
    zoom_y = dt_control_get_dev_zoom_y();
    const float min_scale = dt_dev_get_zoom_scale(dev, DT_ZOOM_FIT, 1 << closeup, 0);
    const float cur_scale = dt_dev_get_zoom_scale(dev, zoom, 1 << closeup, 0);
    nz = cur_scale / min_scale;
  }

  const float zx = zoom_x * nz * (float)(nimgw + 1.0f);
  const float zy = zoom_y * nz * (float)(nimgh + 1.0f);

  // we erase everything
  dt_gui_gtk_set_source_rgb(cri, DT_GUI_COLOR_DARKROOM_BG);
  cairo_paint(cri);

  //we draw the cached image
  dt_view_image_over_t image_over = DT_VIEW_DESERT;
  dt_view_image_expose_t params = { 0 };
  params.image_over = &image_over;
  params.imgid = d->imgid;
  params.cr = cri;
  params.width = width;
  params.height = height;
  params.zoom = 1;
  params.full_preview = TRUE;
  params.no_deco = TRUE;
  params.full_zoom = nz;
  params.full_x = -zx + 1.0f;
  params.full_y = -zy + 1.0f;


  const int res = dt_view_image_expose(&params);

  if(res)
  {
    if(!d->busy) dt_control_log_busy_enter();
    d->busy = TRUE;
  }
  else
  {
    if(d->busy) dt_control_log_busy_leave();
    d->busy = FALSE;
  }
}

static gboolean _lib_duplicate_thumb_draw_callback (GtkWidget *widget, cairo_t *cr, dt_lib_module_t *self)
{
  dt_lib_duplicate_t *d = (dt_lib_duplicate_t *)self->data;
  dt_develop_t *dev = darktable.develop;

  guint width, height;
  width = gtk_widget_get_allocated_width (widget);
  height = gtk_widget_get_allocated_height (widget);
  dt_gui_gtk_set_source_rgb(cr, DT_GUI_COLOR_DARKROOM_BG);
  cairo_paint(cr);

  int imgid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),"imgid"));
  dt_view_image_over_t image_over = DT_VIEW_DESERT;
  dt_view_image_expose_t params = { 0 };
  params.image_over = &image_over;
  params.imgid = imgid;
  params.cr = cr;
  params.width = width;
  params.height = height;
  params.zoom = 5;
  params.full_preview = TRUE;

  int lk = 0;
  // if this is the actual thumb, we want to use the preview pipe
  if(imgid == dev->preview_pipe->output_imgid)
  {
    // we recreate the surface if needed
    if(dev->preview_pipe->output_backbuf)
    {
      /* re-allocate in case of changed image dimensions */
      if(d->rgbbuf == NULL || dev->preview_pipe->output_backbuf_width != d->buf_width
         || dev->preview_pipe->output_backbuf_height != d->buf_height)
      {
        if(d->surface)
        {
          cairo_surface_destroy(d->surface);
          d->surface = NULL;
        }
        g_free(d->rgbbuf);
        d->buf_width = dev->preview_pipe->output_backbuf_width;
        d->buf_height = dev->preview_pipe->output_backbuf_height;
        d->rgbbuf = g_malloc0((size_t)d->buf_width * d->buf_height * 4 * sizeof(unsigned char));
      }

      /* update buffer if new data is available */
      if(d->rgbbuf && dev->preview_pipe->input_timestamp > d->buf_timestamp)
      {
        if(d->surface)
        {
          cairo_surface_destroy(d->surface);
          d->surface = NULL;
        }

        dt_pthread_mutex_t *mutex = &dev->preview_pipe->backbuf_mutex;
        dt_pthread_mutex_lock(mutex);
        memcpy(d->rgbbuf, dev->preview_pipe->output_backbuf,
               (size_t)d->buf_width * d->buf_height * 4 * sizeof(unsigned char));
        d->buf_timestamp = dev->preview_pipe->input_timestamp;
        dt_pthread_mutex_unlock(mutex);

        const int stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, d->buf_width);
        d->surface = cairo_image_surface_create_for_data(d->rgbbuf, CAIRO_FORMAT_RGB24, d->buf_width,
                                                         d->buf_height, stride);
      }
    }
    params.full_surface = &(d->surface);
    params.full_rgbbuf = &(d->rgbbuf);
    params.full_surface_mip = &(d->buf_mip);
    params.full_surface_id = &imgid;
    params.full_surface_wd = &d->buf_width;
    params.full_surface_ht = &d->buf_height;
    params.full_surface_w_lock = &lk;
  }

  dt_view_image_expose(&params);

  return FALSE;
}

static void _lib_duplicate_init_callback(gpointer instance, dt_lib_module_t *self)
{
  dt_lib_duplicate_t *d = (dt_lib_duplicate_t *)self->data;

  d->imgid = 0;
  gtk_container_foreach(GTK_CONTAINER(d->duplicate_box), (GtkCallback)gtk_widget_destroy, 0);
  // retrieve all the versions of the image
  sqlite3_stmt *stmt;
  dt_develop_t *dev = darktable.develop;

  int first_imgid = -1;
  int count = 0;

  // we get a summarize of all versions of the image
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "SELECT i.version, i.id, m.value FROM images AS "
                                                             "i LEFT JOIN meta_data AS m ON m.id = i.id AND "
                                                             "m.key = ?3 WHERE film_id = ?1 AND filename = "
                                                             "?2 ORDER BY i.version",
                              -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, dev->image_storage.film_id);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 2, dev->image_storage.filename, -1, SQLITE_TRANSIENT);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 3, DT_METADATA_XMP_DC_TITLE);

  GtkWidget *bt = NULL;

  while(sqlite3_step(stmt) == SQLITE_ROW)
  {
    GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *dr = gtk_drawing_area_new();
    const int imgid = sqlite3_column_int(stmt, 1);

    // select original picture
    if (first_imgid == -1) first_imgid = imgid;

    gtk_widget_set_size_request (dr, 100, 100);
    g_object_set_data (G_OBJECT (dr),"imgid",GINT_TO_POINTER(imgid));
    gtk_widget_add_events(dr, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    g_signal_connect (G_OBJECT (dr), "draw", G_CALLBACK (_lib_duplicate_thumb_draw_callback), self);
    if (imgid != dev->image_storage.id)
    {
      g_signal_connect(G_OBJECT(dr), "button-press-event", G_CALLBACK(_lib_duplicate_thumb_press_callback), self);
      g_signal_connect(G_OBJECT(dr), "button-release-event", G_CALLBACK(_lib_duplicate_thumb_release_callback), self);
    }

    gchar chl[256];
    gchar *path = (gchar *)sqlite3_column_text(stmt, 2);
    g_snprintf(chl, sizeof(chl), "%d", sqlite3_column_int(stmt, 0));

    GtkWidget *tb = gtk_entry_new();
    if(path) gtk_entry_set_text(GTK_ENTRY(tb), path);
    gtk_entry_set_width_chars(GTK_ENTRY(tb), 15);
    g_object_set_data (G_OBJECT (tb),"imgid",GINT_TO_POINTER(imgid));
    g_signal_connect(G_OBJECT(tb), "focus-out-event", G_CALLBACK(_lib_duplicate_caption_out_callback), self);
    dt_gui_key_accel_block_on_focus_connect(GTK_WIDGET(tb));
    GtkWidget *lb = gtk_label_new (g_strdup(chl));
    bt = dtgtk_button_new(dtgtk_cairo_paint_cancel, CPF_STYLE_FLAT | CPF_DO_NOT_USE_BORDER, NULL);
    g_object_set_data(G_OBJECT(bt), "imgid", GINT_TO_POINTER(imgid));
    g_signal_connect(G_OBJECT(bt), "clicked", G_CALLBACK(_lib_duplicate_delete), self);

    gtk_box_pack_start(GTK_BOX(hb), dr, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), tb, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), lb, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), bt, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(d->duplicate_box), hb, FALSE, FALSE, 0);
    count++;
  }
  sqlite3_finalize (stmt);

  switch(d->select)
  {
     case DT_DUPLICATE_SELECT_FIRST:
       _do_select(first_imgid);
       break;
     case DT_DUPLICATE_SELECT_CURRENT:
         _do_select(darktable.develop->image_storage.id);
       break;
     case DT_DUPLICATE_SELECT_NONE:
     default:
       break;
  }
  d->select = DT_DUPLICATE_SELECT_NONE;

  gtk_widget_show_all(d->duplicate_box);

  // we have a single image, do not allow it to be removed so hide last bt
  if(count==1)
  {
    gtk_widget_set_sensitive(bt, FALSE);
    gtk_widget_set_visible(bt, FALSE);
  }

  // and we store the final size of the current image
  if(dev->image_storage.id >= 0)
    dt_image_get_final_size(dev->image_storage.id, &d->cur_final_width, &d->cur_final_height);
}

static void _lib_duplicate_mipmap_updated_callback(gpointer instance, dt_lib_module_t *self)
{
  dt_lib_duplicate_t *d = (dt_lib_duplicate_t *)self->data;
  // we store the final size of the current image
  if(darktable.develop->image_storage.id >= 0)
    dt_image_get_final_size(darktable.develop->image_storage.id, &d->cur_final_width, &d->cur_final_height);

  gtk_widget_queue_draw (d->duplicate_box);
  dt_control_queue_redraw_center();
}

void gui_init(dt_lib_module_t *self)
{
  /* initialize ui widgets */
  dt_lib_duplicate_t *d = (dt_lib_duplicate_t *)g_malloc0(sizeof(dt_lib_duplicate_t));
  self->data = (void *)d;

  d->imgid = 0;
  d->buf_height = 0;
  d->buf_width = 0;
  d->rgbbuf = NULL;
  d->surface = NULL;
  d->buf_timestamp = 0;
  d->buf_mip = dt_mipmap_cache_get_matching_size(darktable.mipmap_cache, 100, 100);

  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_name(self->widget, "duplicate-ui");
  dt_gui_add_help_link(self->widget, dt_get_help_url(self->plugin_name));

  GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sw), DT_PIXEL_APPLY_DPI(300));
  d->duplicate_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget *bt = gtk_label_new(_("existing duplicates"));
  gtk_box_pack_start(GTK_BOX(hb), bt, FALSE, FALSE, 0);
  bt = dtgtk_button_new(dtgtk_cairo_paint_plus, CPF_STYLE_FLAT | CPF_DO_NOT_USE_BORDER, NULL);
  g_object_set(G_OBJECT(bt), "tooltip-text", _("create a 'virgin' duplicate of the image without any development"), (char *)NULL);
  g_signal_connect(G_OBJECT(bt), "button-press-event", G_CALLBACK(_lib_duplicate_new_clicked_callback), self);
  gtk_box_pack_end(GTK_BOX(hb), bt, FALSE, FALSE, 0);
  bt = dtgtk_button_new(dtgtk_cairo_paint_multiinstance, CPF_STYLE_FLAT | CPF_DO_NOT_USE_BORDER, NULL);
  g_object_set(G_OBJECT(bt), "tooltip-text", _("create a duplicate of the image with same history stack"), (char *)NULL);
  g_signal_connect(G_OBJECT(bt), "button-press-event", G_CALLBACK(_lib_duplicate_duplicate_clicked_callback), self);
  gtk_box_pack_end(GTK_BOX(hb), bt, FALSE, FALSE, 0);


  /* add duplicate list and buttonbox to widget */
  gtk_box_pack_start(GTK_BOX(self->widget), hb, FALSE, FALSE, 0);
  gtk_container_add(GTK_CONTAINER(sw), d->duplicate_box);
  gtk_box_pack_start(GTK_BOX(self->widget), sw, FALSE, FALSE, 0);

  gtk_widget_show_all(self->widget);

  dt_control_signal_connect(darktable.signals, DT_SIGNAL_DEVELOP_IMAGE_CHANGED, G_CALLBACK(_lib_duplicate_init_callback), self);
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_DEVELOP_INITIALIZE, G_CALLBACK(_lib_duplicate_init_callback), self);
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_COLLECTION_CHANGED,
                            G_CALLBACK(_lib_duplicate_init_callback), self);
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_DEVELOP_MIPMAP_UPDATED, G_CALLBACK(_lib_duplicate_mipmap_updated_callback), (gpointer)self);
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_FILMROLLS_CHANGED, G_CALLBACK(_lib_duplicate_filmrolls_updated), self);
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_DEVELOP_PREVIEW_PIPE_FINISHED,
                            G_CALLBACK(_lib_duplicate_mipmap_updated_callback), self);
}

void gui_cleanup(dt_lib_module_t *self)
{
  dt_control_signal_disconnect(darktable.signals, G_CALLBACK(_lib_duplicate_init_callback), self);
  dt_control_signal_disconnect(darktable.signals, G_CALLBACK(_lib_duplicate_mipmap_updated_callback), self);
  g_free(self->data);
  self->data = NULL;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
