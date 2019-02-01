/*
    This file is part of darktable,
    copyright (c) 2011 henrik andersson.
    copyright (c) 2012-2016 tobias ellinghaus.

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
#include "common/darktable.h"
#include "common/debug.h"
#include "common/geo.h"
#include "common/image_cache.h"
#include "common/mipmap_cache.h"
#include "common/undo.h"
#include "control/conf.h"
#include "control/control.h"
#include "gui/accelerators.h"
#include "gui/drag_and_drop.h"
#include "gui/draw.h"
#include "libs/lib.h"
#include "views/view.h"
#include "views/view_api.h"
#include <gdk/gdkkeysyms.h>

#include <osm-gps-map.h>

DT_MODULE(1)

typedef struct dt_undo_geotag_t
{
  int imgid;
  float longitude, latitude, elevation;
} dt_undo_geotag_t;

typedef struct dt_map_t
{
  GtkWidget *center;
  OsmGpsMap *map;
  OsmGpsMapSource_t map_source;
  OsmGpsMapLayer *osd;
  GSList *images;
  GdkPixbuf *image_pin, *place_pin;
  gint selected_image;
  gboolean start_drag;
  struct
  {
    sqlite3_stmt *main_query;
  } statements;
  gboolean drop_filmstrip_activated;
  gboolean filter_images_drawn;
  int max_images_drawn;
} dt_map_t;

typedef struct dt_map_image_t
{
  gint imgid;
  OsmGpsMapImage *image;
  gint width, height;
} dt_map_image_t;

static const int thumb_size = 64, thumb_border = 1, image_pin_size = 13, place_pin_size = 72;
static const uint32_t thumb_frame_color = 0x000000aa;
static const uint32_t pin_outer_color = 0x0000aaaa;
static const uint32_t pin_inner_color = 0xffffffee;
static const uint32_t pin_line_color = 0x000000ff;

/* proxy function to center map view on location at a zoom level */
static void _view_map_center_on_location(const dt_view_t *view, gdouble lon, gdouble lat, gdouble zoom);
/* proxy function to center map view on a bounding box */
static void _view_map_center_on_bbox(const dt_view_t *view, gdouble lon1, gdouble lat1, gdouble lon2, gdouble lat2);
/* proxy function to show or hide the osd */
static void _view_map_show_osd(const dt_view_t *view, gboolean enabled);
/* proxy function to set the map source */
static void _view_map_set_map_source(const dt_view_t *view, OsmGpsMapSource_t map_source);
/* wrapper for setting the map source in the GObject */
static void _view_map_set_map_source_g_object(const dt_view_t *view, OsmGpsMapSource_t map_source);
/* proxy function to check if preferences have changed */
static void _view_map_check_preference_changed(gpointer instance, gpointer user_data);
/* proxy function to add a marker to the map */
static GObject *_view_map_add_marker(const dt_view_t *view, dt_geo_map_display_t type, GList *points);
/* proxy function to remove a marker from the map */
static gboolean _view_map_remove_marker(const dt_view_t *view, dt_geo_map_display_t type, GObject *marker);

/* callback when the collection changs */
static void _view_map_collection_changed(gpointer instance, gpointer user_data);
/* callback when an image is selected in filmstrip, centers map */
static void _view_map_filmstrip_activate_callback(gpointer instance, gpointer user_data);
/* callback when an image is dropped from filmstrip */
static void drag_and_drop_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y,
                                   GtkSelectionData *selection_data, guint target_type, guint time,
                                   gpointer data);
/* callback when the user drags images FROM the map */
static void _view_map_dnd_get_callback(GtkWidget *widget, GdkDragContext *context,
                                       GtkSelectionData *selection_data, guint target_type, guint time,
                                       dt_view_t *self);
/* callback that readds the images to the map */
static void _view_map_changed_callback(OsmGpsMap *map, dt_view_t *self);
/* callback that handles double clicks on the map */
static gboolean _view_map_button_press_callback(GtkWidget *w, GdkEventButton *e, dt_view_t *self);
/* callback when the mouse is moved */
static gboolean _view_map_motion_notify_callback(GtkWidget *w, GdkEventMotion *e, dt_view_t *self);
static gboolean _view_map_dnd_failed_callback(GtkWidget *widget, GdkDragContext *drag_context,
                                              GtkDragResult result, dt_view_t *self);
static void _view_map_dnd_remove_callback(GtkWidget *widget, GdkDragContext *context, gint x, gint y,
                                          GtkSelectionData *selection_data, guint target_type, guint time,
                                          gpointer data);

static void _set_image_location(dt_view_t *self, int imgid, float longitude, float latitude, float elevation,
                                gboolean set_elevation, gboolean record_undo);
static void _get_image_location(int imgid, float *longitude, float *latitude, float *elevation);

static gboolean _view_map_prefs_changed(dt_map_t *lib);
static void _view_map_build_main_query(dt_map_t *lib);

/* center map to on the baricenter of the image list */
static gboolean _view_map_center_on_image_list(dt_view_t *self, const GList *selected_images);
/* center map on the given image */
static void _view_map_center_on_image(dt_view_t *self, const int32_t imgid);

const char *name(dt_view_t *self)
{
  return _("map");
}

uint32_t view(const dt_view_t *self)
{
  return DT_VIEW_MAP;
}

#ifdef USE_LUA

static int latitude_member(lua_State *L)
{
  dt_view_t *module = *(dt_view_t **)lua_touserdata(L, 1);
  dt_map_t *lib = (dt_map_t *)module->data;
  if(lua_gettop(L) != 3)
  {
    if(dt_view_manager_get_current_view(darktable.view_manager) != module)
    {
      lua_pushnumber(L, dt_conf_get_float("plugins/map/latitude"));
    }
    else
    {
      float value;
      g_object_get(G_OBJECT(lib->map), "latitude", &value, NULL);
      lua_pushnumber(L, value);
    }
    return 1;
  }
  else
  {
    luaL_checktype(L, 3, LUA_TNUMBER);
    float lat = lua_tonumber(L, 3);
    lat = CLAMP(lat, -90, 90);
    if(dt_view_manager_get_current_view(darktable.view_manager) != module)
    {
      dt_conf_set_float("plugins/map/latitude", lat);
    }
    else
    {
      float value;
      g_object_get(G_OBJECT(lib->map), "longitude", &value, NULL);
      osm_gps_map_set_center(lib->map, lat, value);
    }
    return 0;
  }
}

static int longitude_member(lua_State *L)
{
  dt_view_t *module = *(dt_view_t **)lua_touserdata(L, 1);
  dt_map_t *lib = (dt_map_t *)module->data;
  if(lua_gettop(L) != 3)
  {
    if(dt_view_manager_get_current_view(darktable.view_manager) != module)
    {
      lua_pushnumber(L, dt_conf_get_float("plugins/map/longitude"));
    }
    else
    {
      float value;
      g_object_get(G_OBJECT(lib->map), "longitude", &value, NULL);
      lua_pushnumber(L, value);
    }
    return 1;
  }
  else
  {
    luaL_checktype(L, 3, LUA_TNUMBER);
    float longi = lua_tonumber(L, 3);
    longi = CLAMP(longi, -180, 180);
    if(dt_view_manager_get_current_view(darktable.view_manager) != module)
    {
      dt_conf_set_float("plugins/map/longitude", longi);
    }
    else
    {
      float value;
      g_object_get(G_OBJECT(lib->map), "latitude", &value, NULL);
      osm_gps_map_set_center(lib->map, value, longi);
    }
    return 0;
  }
}

static int zoom_member(lua_State *L)
{
  dt_view_t *module = *(dt_view_t **)lua_touserdata(L, 1);
  dt_map_t *lib = (dt_map_t *)module->data;
  if(lua_gettop(L) != 3)
  {
    if(dt_view_manager_get_current_view(darktable.view_manager) != module)
    {
      lua_pushnumber(L, dt_conf_get_float("plugins/map/zoom"));
    }
    else
    {
      int value;
      g_object_get(G_OBJECT(lib->map), "zoom", &value, NULL);
      lua_pushnumber(L, value);
    }
    return 1;
  }
  else
  {
    // we rely on osm to correctly clamp zoom (checked in osm source
    // lua can have temporarily false values but it will fix itself when entering map
    // unfortunately we can't get the min max when lib->map doesn't exist
    luaL_checktype(L, 3, LUA_TNUMBER);
    int zoom = luaL_checkinteger(L, 3);
    if(dt_view_manager_get_current_view(darktable.view_manager) != module)
    {
      dt_conf_set_int("plugins/map/zoom", zoom);
    }
    else
    {
      osm_gps_map_set_zoom(lib->map, zoom);
    }
    return 0;
  }
}
#endif // USE_LUA

#ifndef HAVE_OSMGPSMAP_110_OR_NEWER
// the following functions were taken from libosmgpsmap
// Copyright (C) Marcus Bauer 2008 <marcus.bauer@gmail.com>
// Copyright (C) 2013 John Stowers <john.stowers@gmail.com>
// Copyright (C) 2014 Martijn Goedhart <goedhart.martijn@gmail.com>

#if FLT_RADIX == 2
  #define LOG2(x) (ilogb(x))
#else
  #define LOG2(x) ((int)floor(log2(abs(x))))
#endif

#define TILESIZE 256

static float deg2rad(float deg)
{
  return (deg * M_PI / 180.0);
}

static int latlon2zoom(int pix_height, int pix_width, float lat1, float lat2, float lon1, float lon2)
{
  float lat1_m = atanh(sin(lat1));
  float lat2_m = atanh(sin(lat2));
  int zoom_lon = LOG2((double)(2 * pix_width * M_PI) / (TILESIZE * (lon2 - lon1)));
  int zoom_lat = LOG2((double)(2 * pix_height * M_PI) / (TILESIZE * (lat2_m - lat1_m)));
  return MIN(zoom_lon, zoom_lat);
}

#undef LOG2
#undef TILESIZE

//  Copyright (C) 2013 John Stowers <john.stowers@gmail.com>
//  Copyright (C) Marcus Bauer 2008 <marcus.bauer@gmail.com>
//  Copyright (C) John Stowers 2009 <john.stowers@gmail.com>
//  Copyright (C) Till Harbaum 2009 <till@harbaum.org>
//
//  Contributions by
//  Everaldo Canuto 2009 <everaldo.canuto@gmail.com>
static void osm_gps_map_zoom_fit_bbox(OsmGpsMap *map, float latitude1, float latitude2, float longitude1, float longitude2)
{
  GtkAllocation allocation;
  int zoom;
  gtk_widget_get_allocation(GTK_WIDGET (map), &allocation);
  zoom = latlon2zoom(allocation.height, allocation.width, deg2rad(latitude1), deg2rad(latitude2), deg2rad(longitude1), deg2rad(longitude2));
  osm_gps_map_set_center(map, (latitude1 + latitude2) / 2, (longitude1 + longitude2) / 2);
  osm_gps_map_set_zoom(map, zoom);
}
#endif // HAVE_OSMGPSMAP_110_OR_NEWER

static GdkPixbuf *init_image_pin()
{
  int w = DT_PIXEL_APPLY_DPI(thumb_size + 2 * thumb_border), h = DT_PIXEL_APPLY_DPI(image_pin_size);
  float r, g, b, a;
  r = ((thumb_frame_color & 0xff000000) >> 24) / 255.0;
  g = ((thumb_frame_color & 0x00ff0000) >> 16) / 255.0;
  b = ((thumb_frame_color & 0x0000ff00) >> 8) / 255.0;
  a = ((thumb_frame_color & 0x000000ff) >> 0) / 255.0;

  cairo_surface_t *cst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  cairo_t *cr = cairo_create(cst);
  cairo_set_source_rgba(cr, r, g, b, a);
  dtgtk_cairo_paint_map_pin(cr, 0, 0, w, h, 0, NULL);
  cairo_destroy(cr);
  uint8_t *data = cairo_image_surface_get_data(cst);
  dt_draw_cairo_to_gdk_pixbuf(data, w, h);
  size_t size = w * h * 4;
  uint8_t *buf = (uint8_t *)malloc(size);
  memcpy(buf, data, size);
  GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(buf, GDK_COLORSPACE_RGB, TRUE, 8, w, h, w * 4,
                                               (GdkPixbufDestroyNotify)free, NULL);
  cairo_surface_destroy(cst);
  return pixbuf;
}

static GdkPixbuf *init_place_pin()
{
  int w = DT_PIXEL_APPLY_DPI(place_pin_size), h = DT_PIXEL_APPLY_DPI(place_pin_size);
  float r, g, b, a;

  cairo_surface_t *cst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  cairo_t *cr = cairo_create(cst);

  // outer shape
  r = ((pin_outer_color & 0xff000000) >> 24) / 255.0;
  g = ((pin_outer_color & 0x00ff0000) >> 16) / 255.0;
  b = ((pin_outer_color & 0x0000ff00) >> 8) / 255.0;
  a = ((pin_outer_color & 0x000000ff) >> 0) / 255.0;
  cairo_set_source_rgba(cr, r, g, b, a);
  cairo_arc(cr, 0.5 * w, 0.333 * h, 0.333 * h - 2, 150.0 * (M_PI / 180.0), 30.0 * (M_PI / 180.0));
  cairo_line_to(cr, 0.5 * w, h - 2);
  cairo_close_path(cr);
  cairo_fill_preserve(cr);

  r = ((pin_line_color & 0xff000000) >> 24) / 255.0;
  g = ((pin_line_color & 0x00ff0000) >> 16) / 255.0;
  b = ((pin_line_color & 0x0000ff00) >> 8) / 255.0;
  a = ((pin_line_color & 0x000000ff) >> 0) / 255.0;
  cairo_set_source_rgba(cr, r, g, b, a);
  cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(1));
  cairo_stroke(cr);

  // inner circle
  r = ((pin_inner_color & 0xff000000) >> 24) / 255.0;
  g = ((pin_inner_color & 0x00ff0000) >> 16) / 255.0;
  b = ((pin_inner_color & 0x0000ff00) >> 8) / 255.0;
  a = ((pin_inner_color & 0x000000ff) >> 0) / 255.0;
  cairo_set_source_rgba(cr, r, g, b, a);
  cairo_arc(cr, 0.5 * w, 0.333 * h, 0.17 * h, 0, 2.0 * M_PI);
  cairo_fill(cr);

  cairo_destroy(cr);
  uint8_t *data = cairo_image_surface_get_data(cst);
  dt_draw_cairo_to_gdk_pixbuf(data, w, h);
  size_t size = w * h * 4;
  uint8_t *buf = (uint8_t *)malloc(size);
  memcpy(buf, data, size);
  GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(buf, GDK_COLORSPACE_RGB, TRUE, 8, w, h, w * 4,
                                               (GdkPixbufDestroyNotify)free, NULL);
  cairo_surface_destroy(cst);
  return pixbuf;
}

void init(dt_view_t *self)
{
  self->data = calloc(1, sizeof(dt_map_t));

  dt_map_t *lib = (dt_map_t *)self->data;

  if(darktable.gui)
  {
    lib->image_pin = init_image_pin();
    lib->place_pin = init_place_pin();
    lib->drop_filmstrip_activated = FALSE;

    OsmGpsMapSource_t map_source
        = OSM_GPS_MAP_SOURCE_OPENSTREETMAP; // open street map should be a nice default ...
    gchar *old_map_source = dt_conf_get_string("plugins/map/map_source");
    if(old_map_source && old_map_source[0] != '\0')
    {
      // find the number of the stored map_source
      for(int i = 0; i <= OSM_GPS_MAP_SOURCE_LAST; i++)
      {
        const gchar *new_map_source = osm_gps_map_source_get_friendly_name(i);
        if(!g_strcmp0(old_map_source, new_map_source))
        {
          if(osm_gps_map_source_is_valid(i)) map_source = i;
          break;
        }
      }
    }
    else
      dt_conf_set_string("plugins/map/map_source", osm_gps_map_source_get_friendly_name(map_source));
    g_free(old_map_source);

    lib->map_source = map_source;

    lib->map = g_object_new(OSM_TYPE_GPS_MAP, "map-source", OSM_GPS_MAP_SOURCE_NULL, "proxy-uri",
                            g_getenv("http_proxy"), NULL);

    GtkWidget *parent = gtk_widget_get_parent(gtk_widget_get_parent(dt_ui_center(darktable.gui->ui)));
    gtk_box_pack_start(GTK_BOX(parent), GTK_WIDGET(lib->map), TRUE, TRUE, 0);

    lib->osd = g_object_new(OSM_TYPE_GPS_MAP_OSD, "show-scale", TRUE, "show-coordinates", TRUE, "show-dpad",
                            TRUE, "show-zoom", TRUE,
#ifdef HAVE_OSMGPSMAP_NEWER_THAN_110
                            "show-copyright", TRUE,
#endif
                            NULL);

    if(dt_conf_get_bool("plugins/map/show_map_osd"))
    {
      osm_gps_map_layer_add(OSM_GPS_MAP(lib->map), lib->osd);
    }

    /* allow drag&drop of images from filmstrip */
    gtk_drag_dest_set(GTK_WIDGET(lib->map), GTK_DEST_DEFAULT_ALL, target_list_internal, n_targets_internal,
                      GDK_ACTION_COPY);
    g_signal_connect(GTK_WIDGET(lib->map), "drag-data-received", G_CALLBACK(drag_and_drop_received), self);
    g_signal_connect(GTK_WIDGET(lib->map), "changed", G_CALLBACK(_view_map_changed_callback), self);
    g_signal_connect_after(G_OBJECT(lib->map), "button-press-event",
                           G_CALLBACK(_view_map_button_press_callback), self);
    g_signal_connect(G_OBJECT(lib->map), "motion-notify-event", G_CALLBACK(_view_map_motion_notify_callback),
                     self);

    /* allow drag&drop of images from the map, too */
    g_signal_connect(GTK_WIDGET(lib->map), "drag-data-get", G_CALLBACK(_view_map_dnd_get_callback), self);
    g_signal_connect(GTK_WIDGET(lib->map), "drag-failed", G_CALLBACK(_view_map_dnd_failed_callback), self);
  }

  /* build the query string */
  lib->statements.main_query = NULL;
  _view_map_build_main_query(lib);

#ifdef USE_LUA
  lua_State *L = darktable.lua_state.state;
  luaA_Type my_type = dt_lua_module_entry_get_type(L, "view", self->module_name);
  lua_pushcfunction(L, latitude_member);
  dt_lua_gtk_wrap(L);
  dt_lua_type_register_type(L, my_type, "latitude");
  lua_pushcfunction(L, longitude_member);
  dt_lua_gtk_wrap(L);
  dt_lua_type_register_type(L, my_type, "longitude");
  lua_pushcfunction(L, zoom_member);
  dt_lua_gtk_wrap(L);
  dt_lua_type_register_type(L, my_type, "zoom");

#endif // USE_LUA
  /* connect collection changed signal */
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_COLLECTION_CHANGED,
                            G_CALLBACK(_view_map_collection_changed), (gpointer)self);
  /* connect preference changed signal */
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_PREFERENCES_CHANGE,
                            G_CALLBACK(_view_map_check_preference_changed), (gpointer)self);
}

void cleanup(dt_view_t *self)
{
  dt_map_t *lib = (dt_map_t *)self->data;

  dt_control_signal_disconnect(darktable.signals, G_CALLBACK(_view_map_collection_changed), self);
  dt_control_signal_disconnect(darktable.signals, G_CALLBACK(_view_map_check_preference_changed), self);

  if(darktable.gui)
  {
    g_object_unref(G_OBJECT(lib->image_pin));
    g_object_unref(G_OBJECT(lib->place_pin));
    g_object_unref(G_OBJECT(lib->osd));
    osm_gps_map_image_remove_all(lib->map);
    if(lib->images)
    {
      g_slist_free_full(lib->images, g_free);
      lib->images = NULL;
    }
    // FIXME: it would be nice to cleanly destroy the object, but we are doing this inside expose() so
    // removing the widget can cause segfaults.
    //     g_object_unref(G_OBJECT(lib->map));
  }
  if(lib->statements.main_query) sqlite3_finalize(lib->statements.main_query);
  free(self->data);
}

void configure(dt_view_t *self, int wd, int ht)
{
  // dt_capture_t *lib=(dt_capture_t*)self->data;
}

int try_enter(dt_view_t *self)
{
  return 0;
}

static gboolean _view_map_redraw(gpointer user_data)
{
  dt_view_t *self = (dt_view_t *)user_data;
  dt_map_t *lib = (dt_map_t *)self->data;
  g_signal_emit_by_name(lib->map, "changed");
  return FALSE; // remove the function again
}

static void _view_map_changed_callback(OsmGpsMap *map, dt_view_t *self)
{
  dt_map_t *lib = (dt_map_t *)self->data;

  OsmGpsMapPoint bb[2];

  /* get bounding box coords */
  osm_gps_map_get_bbox(map, &bb[0], &bb[1]);
  float bb_0_lat = 0.0, bb_0_lon = 0.0, bb_1_lat = 0.0, bb_1_lon = 0.0;
  osm_gps_map_point_get_degrees(&bb[0], &bb_0_lat, &bb_0_lon);
  osm_gps_map_point_get_degrees(&bb[1], &bb_1_lat, &bb_1_lon);

  /* make the bounding box a little bigger to the west and south */
  float lat0 = 0.0, lon0 = 0.0, lat1 = 0.0, lon1 = 0.0;
  OsmGpsMapPoint *pt0 = osm_gps_map_point_new_degrees(0.0, 0.0),
                 *pt1 = osm_gps_map_point_new_degrees(0.0, 0.0);
  osm_gps_map_convert_screen_to_geographic(map, 0, 0, pt0);
  osm_gps_map_convert_screen_to_geographic(map, 1.5 * thumb_size, 1.5 * thumb_size, pt1);
  osm_gps_map_point_get_degrees(pt0, &lat0, &lon0);
  osm_gps_map_point_get_degrees(pt1, &lat1, &lon1);
  osm_gps_map_point_free(pt0);
  osm_gps_map_point_free(pt1);
  double south_border = lat0 - lat1, west_border = lon1 - lon0;

  /* get map view state and store  */
  int zoom;
  float center_lat, center_lon;
  g_object_get(G_OBJECT(map), "zoom", &zoom, "latitude", &center_lat, "longitude", &center_lon, NULL);
  dt_conf_set_float("plugins/map/longitude", center_lon);
  dt_conf_set_float("plugins/map/latitude", center_lat);
  dt_conf_set_int("plugins/map/zoom", zoom);

  /* check if the prefs have changed and rebuild main_query if needed */
  if(_view_map_prefs_changed(lib)) _view_map_build_main_query(lib);

  /* let's reset and reuse the main_query statement */
  DT_DEBUG_SQLITE3_CLEAR_BINDINGS(lib->statements.main_query);
  DT_DEBUG_SQLITE3_RESET(lib->statements.main_query);

  /* bind bounding box coords for the main query */
  DT_DEBUG_SQLITE3_BIND_DOUBLE(lib->statements.main_query, 1, bb_0_lon - west_border);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(lib->statements.main_query, 2, bb_1_lon);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(lib->statements.main_query, 3, bb_0_lat);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(lib->statements.main_query, 4, bb_1_lat - south_border);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(lib->statements.main_query, 5, center_lat);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(lib->statements.main_query, 6, center_lon);

  /* remove the old images */
  if(lib->images)
  {
    // we can't use osm_gps_map_image_remove_all() because we want to keep the marker
    for(GSList *iter = lib->images; iter; iter = g_slist_next(iter))
    {
      dt_map_image_t *image = (dt_map_image_t *)iter->data;
      osm_gps_map_image_remove(map, image->image);
    }
    g_slist_free_full(lib->images, g_free);
    lib->images = NULL;
  }

  /* add all images to the map */
  gboolean needs_redraw = FALSE;
  const int _thumb_size = DT_PIXEL_APPLY_DPI(thumb_size);
  dt_mipmap_size_t mip = dt_mipmap_cache_get_matching_size(darktable.mipmap_cache, _thumb_size, _thumb_size);
  while(sqlite3_step(lib->statements.main_query) == SQLITE_ROW)
  {
    int imgid = sqlite3_column_int(lib->statements.main_query, 0);
    dt_mipmap_buffer_t buf;
    dt_mipmap_cache_get(darktable.mipmap_cache, &buf, imgid, mip, DT_MIPMAP_BEST_EFFORT, 'r');

    if(buf.buf)
    {
      GdkPixbuf *source = NULL, *thumb = NULL;

      for(size_t i = 3; i < (size_t)4 * buf.width * buf.height; i += 4) buf.buf[i] = UINT8_MAX;

      int w = _thumb_size, h = _thumb_size;
      const float _thumb_border = DT_PIXEL_APPLY_DPI(thumb_border), _pin_size = DT_PIXEL_APPLY_DPI(image_pin_size);
      if(buf.width < buf.height)
        w = (buf.width * _thumb_size) / buf.height; // portrait
      else
        h = (buf.height * _thumb_size) / buf.width; // landscape

      // next we get a pixbuf for the image
      source = gdk_pixbuf_new_from_data(buf.buf, GDK_COLORSPACE_RGB, TRUE, 8, buf.width, buf.height,
                                        buf.width * 4, NULL, NULL);
      if(!source) goto map_changed_failure;

      // now we want a slightly larger pixbuf that we can put the image on
      thumb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w + 2 * _thumb_border,
                             h + 2 * _thumb_border + _pin_size);
      if(!thumb) goto map_changed_failure;
      gdk_pixbuf_fill(thumb, thumb_frame_color);

      // put the image onto the frame
      gdk_pixbuf_scale(source, thumb, _thumb_border, _thumb_border, w, h, _thumb_border, _thumb_border,
                       (1.0 * w) / buf.width, (1.0 * h) / buf.height, GDK_INTERP_HYPER);

      // and finally add the pin
      gdk_pixbuf_copy_area(lib->image_pin, 0, 0, w + 2 * _thumb_border, _pin_size, thumb, 0, h + 2 * _thumb_border);

      const dt_image_t *cimg = dt_image_cache_get(darktable.image_cache, imgid, 'r');
      if(!cimg) goto map_changed_failure;
      dt_map_image_t *entry = (dt_map_image_t *)malloc(sizeof(dt_map_image_t));
      if(!entry)
      {
        dt_image_cache_read_release(darktable.image_cache, cimg);
        goto map_changed_failure;
      }
      entry->imgid = imgid;
      entry->image = osm_gps_map_image_add_with_alignment(map, cimg->latitude, cimg->longitude, thumb, 0, 1);
      entry->width = w;
      entry->height = h;
      lib->images = g_slist_prepend(lib->images, entry);
      dt_image_cache_read_release(darktable.image_cache, cimg);

    map_changed_failure:
      if(source) g_object_unref(source);
      if(thumb) g_object_unref(thumb);
    }
    else
      needs_redraw = TRUE;
    dt_mipmap_cache_release(darktable.mipmap_cache, &buf);
  }

  // not exactly thread safe, but should be good enough for updating the display
  static int timeout_event_source = 0;
  if(needs_redraw && timeout_event_source == 0)
    timeout_event_source = g_timeout_add_seconds(
        1, _view_map_redraw, self); // try again in a second, maybe some pictures have loaded by then
  else
    timeout_event_source = 0;

  // activate this callback late in the process as we need the filmstrip proxy to be setup. This is not the
  // case in the initialization phase.
  if(!lib->drop_filmstrip_activated && darktable.view_manager->proxy.filmstrip.module)
  {
    g_signal_connect(
        darktable.view_manager->proxy.filmstrip.widget(darktable.view_manager->proxy.filmstrip.module),
        "drag-data-received", G_CALLBACK(_view_map_dnd_remove_callback), self);
    lib->drop_filmstrip_activated = TRUE;
  }
}

static int _view_map_get_img_at_pos(dt_view_t *self, double x, double y)
{
  dt_map_t *lib = (dt_map_t *)self->data;
  GSList *iter;

  for(iter = lib->images; iter != NULL; iter = iter->next)
  {
    dt_map_image_t *entry = (dt_map_image_t *)iter->data;
    OsmGpsMapImage *image = entry->image;
    OsmGpsMapPoint *pt = (OsmGpsMapPoint *)osm_gps_map_image_get_point(image);
    gint img_x = 0, img_y = 0;
    osm_gps_map_convert_geographic_to_screen(lib->map, pt, &img_x, &img_y);
    img_y -= DT_PIXEL_APPLY_DPI(image_pin_size);
    if(x >= img_x && x <= img_x + entry->width && y <= img_y && y >= img_y - entry->height)
      return entry->imgid;
  }

  return 0;
}

static gboolean _view_map_motion_notify_callback(GtkWidget *widget, GdkEventMotion *e, dt_view_t *self)
{
  dt_map_t *lib = (dt_map_t *)self->data;

  if(lib->start_drag && lib->selected_image > 0)
  {
    for(GSList *iter = lib->images; iter != NULL; iter = iter->next)
    {
      dt_map_image_t *entry = (dt_map_image_t *)iter->data;
      OsmGpsMapImage *image = entry->image;
      if(entry->imgid == lib->selected_image)
      {
        osm_gps_map_image_remove(lib->map, image);
        break;
      }
    }

    lib->start_drag = FALSE;
    GtkTargetList *targets = gtk_target_list_new(target_list_all, n_targets_all);

    // FIXME: for some reason the image is only shown when it's above a certain size,
    // which happens to be > than the normal-DPI one. When dragging from filmstrip it works though.
    const int _thumb_size = DT_PIXEL_APPLY_DPI(thumb_size);
    dt_mipmap_buffer_t buf;
    dt_mipmap_size_t mip = dt_mipmap_cache_get_matching_size(darktable.mipmap_cache, _thumb_size, _thumb_size);
    dt_mipmap_cache_get(darktable.mipmap_cache, &buf, lib->selected_image, mip, DT_MIPMAP_BLOCKING, 'r');

    if(buf.buf)
    {
      GdkPixbuf *source = NULL, *thumb = NULL;

      for(size_t i = 3; i < (size_t)4 * buf.width * buf.height; i += 4) buf.buf[i] = UINT8_MAX;

      int w = _thumb_size, h = _thumb_size;
      const float _thumb_border = DT_PIXEL_APPLY_DPI(thumb_border);
      if(buf.width < buf.height)
        w = (buf.width * _thumb_size) / buf.height; // portrait
      else
        h = (buf.height * _thumb_size) / buf.width; // landscape

      // next we get a pixbuf for the image
      source = gdk_pixbuf_new_from_data(buf.buf, GDK_COLORSPACE_RGB, TRUE, 8, buf.width, buf.height,
                                        buf.width * 4, NULL, NULL);

      // now we want a slightly larger pixbuf that we can put the image on
      thumb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w + 2 * _thumb_border, h + 2 * _thumb_border);
      gdk_pixbuf_fill(thumb, thumb_frame_color);

      // put the image onto the frame
      gdk_pixbuf_scale(source, thumb, _thumb_border, _thumb_border, w, h, _thumb_border, _thumb_border,
                       (1.0 * w) / buf.width, (1.0 * h) / buf.height, GDK_INTERP_HYPER);

      GdkDragContext *context = gtk_drag_begin_with_coordinates(GTK_WIDGET(lib->map), targets,
                                                                GDK_ACTION_COPY, 1, (GdkEvent *)e, -1, -1);

      gtk_drag_set_icon_pixbuf(context, thumb, 0, h + 2 * _thumb_border);

      if(source) g_object_unref(source);
      if(thumb) g_object_unref(thumb);
    }

    dt_mipmap_cache_release(darktable.mipmap_cache, &buf);

    gtk_target_list_unref(targets);
    return TRUE;
  }
  return FALSE;
}

static gboolean _view_map_button_press_callback(GtkWidget *w, GdkEventButton *e, dt_view_t *self)
{
  dt_map_t *lib = (dt_map_t *)self->data;
  if(e->button == 1)
  {
    // check if the click was on an image or just some random position
    lib->selected_image = _view_map_get_img_at_pos(self, e->x, e->y);
    if(e->type == GDK_BUTTON_PRESS && lib->selected_image > 0)
    {
      lib->start_drag = TRUE;
      return TRUE;
    }
    if(e->type == GDK_2BUTTON_PRESS)
    {
      if(lib->selected_image > 0)
      {
        // open the image in darkroom
        dt_control_set_mouse_over_id(lib->selected_image);
        dt_ctl_switch_mode_to("darkroom");
        return TRUE;
      }
      else
      {
        // zoom into that position
        float longitude, latitude;
        OsmGpsMapPoint *pt = osm_gps_map_point_new_degrees(0.0, 0.0);
        osm_gps_map_convert_screen_to_geographic(lib->map, e->x, e->y, pt);
        osm_gps_map_point_get_degrees(pt, &latitude, &longitude);
        osm_gps_map_point_free(pt);
        int zoom, max_zoom;
        g_object_get(G_OBJECT(lib->map), "zoom", &zoom, "max-zoom", &max_zoom, NULL);
        zoom = MIN(zoom + 1, max_zoom);
        _view_map_center_on_location(self, longitude, latitude, zoom);
      }
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean _display_selected(gpointer user_data)
{
  dt_view_t *self = (dt_view_t *)user_data;
  gboolean done = FALSE;

  GList *selected_images = dt_collection_get_selected(darktable.collection, -1);
  if(selected_images)
  {
    done = _view_map_center_on_image_list(self, selected_images);
    g_list_free(selected_images);
  }

  if(!done)
  {
    dt_map_t *lib = (dt_map_t *)self->data;
    GList *collection_images = dt_collection_get_all(darktable.collection, lib->max_images_drawn);
    if(collection_images)
    {
      done = _view_map_center_on_image_list(self, collection_images);
      g_list_free(collection_images);
    }
  }
  return FALSE; // don't call again
}

void enter(dt_view_t *self)
{
  dt_map_t *lib = (dt_map_t *)self->data;

  lib->selected_image = 0;
  lib->start_drag = FALSE;

  /* set the correct map source */
  _view_map_set_map_source_g_object(self, lib->map_source);

  /* replace center widget */
  GtkWidget *parent = gtk_widget_get_parent(gtk_widget_get_parent(dt_ui_center(darktable.gui->ui)));
  gtk_widget_hide(gtk_widget_get_parent(dt_ui_center(darktable.gui->ui)));

  gtk_box_reorder_child(GTK_BOX(parent), GTK_WIDGET(lib->map), 2);

  gtk_widget_show_all(GTK_WIDGET(lib->map));

  /* setup proxy functions */
  darktable.view_manager->proxy.map.view = self;
  darktable.view_manager->proxy.map.center_on_location = _view_map_center_on_location;
  darktable.view_manager->proxy.map.center_on_bbox = _view_map_center_on_bbox;
  darktable.view_manager->proxy.map.show_osd = _view_map_show_osd;
  darktable.view_manager->proxy.map.set_map_source = _view_map_set_map_source;
  darktable.view_manager->proxy.map.add_marker = _view_map_add_marker;
  darktable.view_manager->proxy.map.remove_marker = _view_map_remove_marker;

  /* restore last zoom,location in map */
  float lon = dt_conf_get_float("plugins/map/longitude");
  lon = CLAMP(lon, -180, 180);
  float lat = dt_conf_get_float("plugins/map/latitude");
  lat = CLAMP(lat, -90, 90);
  const int zoom = dt_conf_get_int("plugins/map/zoom");

  osm_gps_map_set_center_and_zoom(lib->map, lat, lon, zoom);

  /* connect signal for filmstrip image activate */
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_VIEWMANAGER_FILMSTRIP_ACTIVATE,
                            G_CALLBACK(_view_map_filmstrip_activate_callback), self);

  /* scroll filmstrip to the first selected image */
  GList *selected_images = dt_collection_get_selected(darktable.collection, 1);
  if(selected_images)
  {
    dt_view_filmstrip_scroll_to_image(darktable.view_manager, GPOINTER_TO_INT(selected_images->data), FALSE);
    g_list_free(selected_images);
  }

  g_timeout_add(250, _display_selected, self);
}

void leave(dt_view_t *self)
{
  /* disable the map source again. no need to risk network traffic while we are not in map mode. */
  _view_map_set_map_source_g_object(self, OSM_GPS_MAP_SOURCE_NULL);

  /* disconnect from filmstrip image activate */
  dt_control_signal_disconnect(darktable.signals, G_CALLBACK(_view_map_filmstrip_activate_callback),
                               (gpointer)self);

  dt_map_t *lib = (dt_map_t *)self->data;

  gtk_widget_hide(GTK_WIDGET(lib->map));
  gtk_widget_show_all(gtk_widget_get_parent(dt_ui_center(darktable.gui->ui)));

  /* reset proxy */
  darktable.view_manager->proxy.map.view = NULL;
}

void mouse_moved(dt_view_t *self, double x, double y, double pressure, int which)
{
  // redraw center on mousemove
  dt_control_queue_redraw_center();
}

void init_key_accels(dt_view_t *self)
{
  dt_accel_register_view(self, NC_("accel", "undo"), GDK_KEY_z, GDK_CONTROL_MASK);
  dt_accel_register_view(self, NC_("accel", "redo"), GDK_KEY_y, GDK_CONTROL_MASK);
  // Film strip shortcuts
  dt_accel_register_view(self, NC_("accel", "toggle film strip"), GDK_KEY_f, GDK_CONTROL_MASK);
}

static gboolean _view_map_undo_callback(GtkAccelGroup *accel_group, GObject *acceleratable, guint keyval,
                                        GdkModifierType modifier, gpointer data)
{
  dt_undo_do_undo(darktable.undo, DT_UNDO_GEOTAG);
  return TRUE;
}

static gboolean _view_map_redo_callback(GtkAccelGroup *accel_group, GObject *acceleratable, guint keyval,
                                        GdkModifierType modifier, gpointer data)
{
  dt_undo_do_redo(darktable.undo, DT_UNDO_GEOTAG);
  return TRUE;
}

static gboolean film_strip_key_accel(GtkAccelGroup *accel_group, GObject *acceleratable, guint keyval,
                                     GdkModifierType modifier, gpointer data)
{
  dt_lib_module_t *m = darktable.view_manager->proxy.filmstrip.module;
  gboolean vs = dt_lib_is_visible(m);
  dt_lib_set_visible(m, !vs);
  return TRUE;
}

void connect_key_accels(dt_view_t *self)
{
  // undo/redo
  GClosure *closure = g_cclosure_new(G_CALLBACK(_view_map_undo_callback), (gpointer)self, NULL);
  dt_accel_connect_view(self, "undo", closure);
  closure = g_cclosure_new(G_CALLBACK(_view_map_redo_callback), (gpointer)self, NULL);
  dt_accel_connect_view(self, "redo", closure);
  // Film strip shortcuts
  closure = g_cclosure_new(G_CALLBACK(film_strip_key_accel), (gpointer)self, NULL);
  dt_accel_connect_view(self, "toggle film strip", closure);
}


static void _view_map_center_on_location(const dt_view_t *view, gdouble lon, gdouble lat, gdouble zoom)
{
  dt_map_t *lib = (dt_map_t *)view->data;
  osm_gps_map_set_center_and_zoom(lib->map, lat, lon, zoom);
}

static void _view_map_center_on_bbox(const dt_view_t *view, gdouble lon1, gdouble lat1, gdouble lon2, gdouble lat2)
{
  dt_map_t *lib = (dt_map_t *)view->data;
  osm_gps_map_zoom_fit_bbox(lib->map, lat1, lat2, lon1, lon2);
}

static void _view_map_show_osd(const dt_view_t *view, gboolean enabled)
{
  dt_map_t *lib = (dt_map_t *)view->data;

  gboolean old_value = dt_conf_get_bool("plugins/map/show_map_osd");
  if(enabled == old_value) return;

  dt_conf_set_bool("plugins/map/show_map_osd", enabled);
  if(enabled)
    osm_gps_map_layer_add(OSM_GPS_MAP(lib->map), lib->osd);
  else
    osm_gps_map_layer_remove(OSM_GPS_MAP(lib->map), lib->osd);

  g_signal_emit_by_name(lib->map, "changed");
}

static void _view_map_set_map_source_g_object(const dt_view_t *view, OsmGpsMapSource_t map_source)
{
  dt_map_t *lib = (dt_map_t *)view->data;

  GValue value = {
    0,
  };
  g_value_init(&value, G_TYPE_INT);
  g_value_set_int(&value, map_source);
  g_object_set_property(G_OBJECT(lib->map), "map-source", &value);
  g_value_unset(&value);
}

static void _view_map_set_map_source(const dt_view_t *view, OsmGpsMapSource_t map_source)
{
  dt_map_t *lib = (dt_map_t *)view->data;

  if(map_source == lib->map_source) return;

  lib->map_source = map_source;
  dt_conf_set_string("plugins/map/map_source", osm_gps_map_source_get_friendly_name(map_source));
  _view_map_set_map_source_g_object(view, map_source);
}

static OsmGpsMapImage *_view_map_add_pin(const dt_view_t *view, GList *points)
{
  dt_map_t *lib = (dt_map_t *)view->data;
  dt_geo_map_display_point_t *p = (dt_geo_map_display_point_t *)points->data;
  return osm_gps_map_image_add_with_alignment(lib->map, p->lat, p->lon, lib->place_pin, 0.5, 1);
}

static gboolean _view_map_remove_pin(const dt_view_t *view, OsmGpsMapImage *pin)
{
  dt_map_t *lib = (dt_map_t *)view->data;
  return osm_gps_map_image_remove(lib->map, pin);
}

#ifdef HAVE_OSMGPSMAP_110_OR_NEWER
static OsmGpsMapPolygon *_view_map_add_polygon(const dt_view_t *view, GList *points)
{
  dt_map_t *lib = (dt_map_t *)view->data;

  OsmGpsMapPolygon *poly = osm_gps_map_polygon_new();
  OsmGpsMapTrack* track = osm_gps_map_track_new();

  for(GList *iter = g_list_first(points); iter; iter = g_list_next(iter))
  {
    dt_geo_map_display_point_t *p = (dt_geo_map_display_point_t *)iter->data;
    OsmGpsMapPoint* point = osm_gps_map_point_new_degrees(p->lat, p->lon);
    osm_gps_map_track_add_point(track, point);
  }

  g_object_set(poly, "track", track, (gchar *)0);
  g_object_set(poly, "editable", FALSE, (gchar *)0);
  g_object_set(poly, "shaded", FALSE, (gchar *)0);

  osm_gps_map_polygon_add(lib->map, poly);

  return poly;
}

static gboolean _view_map_remove_polygon(const dt_view_t *view, OsmGpsMapPolygon *polygon)
{
  dt_map_t *lib = (dt_map_t *)view->data;
  return osm_gps_map_polygon_remove(lib->map, polygon);
}
#endif

static OsmGpsMapTrack *_view_map_add_track(const dt_view_t *view, GList *points)
{
  dt_map_t *lib = (dt_map_t *)view->data;

  OsmGpsMapTrack* track = osm_gps_map_track_new();

  for(GList *iter = g_list_first(points); iter; iter = g_list_next(iter))
  {
    dt_geo_map_display_point_t *p = (dt_geo_map_display_point_t *)iter->data;
    OsmGpsMapPoint* point = osm_gps_map_point_new_degrees(p->lat, p->lon);
    osm_gps_map_track_add_point(track, point);
  }

  g_object_set(track, "editable", FALSE, (gchar *)0);

  osm_gps_map_track_add(lib->map, track);

  return track;
}

static gboolean _view_map_remove_track(const dt_view_t *view, OsmGpsMapTrack *track)
{
  dt_map_t *lib = (dt_map_t *)view->data;
  return osm_gps_map_track_remove(lib->map, track);
}

static GObject *_view_map_add_marker(const dt_view_t *view, dt_geo_map_display_t type, GList *points)
{
  switch(type)
  {
    case MAP_DISPLAY_POINT: return G_OBJECT(_view_map_add_pin(view, points));
    case MAP_DISPLAY_TRACK: return G_OBJECT(_view_map_add_track(view, points));
#ifdef HAVE_OSMGPSMAP_110_OR_NEWER
    case MAP_DISPLAY_POLYGON: return G_OBJECT(_view_map_add_polygon(view, points));
#endif
    default: return NULL;
  }
}

static gboolean _view_map_remove_marker(const dt_view_t *view, dt_geo_map_display_t type, GObject *marker)
{
  if(type == MAP_DISPLAY_NONE) return FALSE;

  switch(type)
  {
    case MAP_DISPLAY_POINT: return _view_map_remove_pin(view, OSM_GPS_MAP_IMAGE(marker));
    case MAP_DISPLAY_TRACK: return _view_map_remove_track(view, OSM_GPS_MAP_TRACK(marker));
#ifdef HAVE_OSMGPSMAP_110_OR_NEWER
    case MAP_DISPLAY_POLYGON: return _view_map_remove_polygon(view, OSM_GPS_MAP_POLYGON(marker));
#endif
    default: return FALSE;
  }
}


static void _view_map_check_preference_changed(gpointer instance, gpointer user_data)
{
  dt_view_t *view = (dt_view_t *)user_data;
  dt_map_t *lib = (dt_map_t *)view->data;

  if(_view_map_prefs_changed(lib)) g_signal_emit_by_name(lib->map, "changed");
}

static void _view_map_collection_changed(gpointer instance, gpointer user_data)
{
  dt_view_t *self = (dt_view_t *)user_data;
   dt_map_t *lib = (dt_map_t *)self->data;

  if(darktable.view_manager->proxy.map.view)
  {
    GList *collection_images = dt_collection_get_all(darktable.collection, lib->max_images_drawn);
    if(collection_images)
    {
      _view_map_center_on_image_list(self, collection_images);
      g_list_free(collection_images);
    }
  }

  if(dt_conf_get_bool("plugins/map/filter_images_drawn"))
  {
    /* only redraw when map mode is currently active, otherwise enter() does the magic */
    if(darktable.view_manager->proxy.map.view) g_signal_emit_by_name(lib->map, "changed");
  }
}

static void _view_map_center_on_image(dt_view_t *self, const int32_t imgid)
{
  if(imgid)
  {
    const dt_map_t *lib = (dt_map_t *)self->data;
    float longitude = 0;
    float latitude = 0;
    float elevation = 0;
    _get_image_location(imgid, &longitude, &latitude, &elevation);

    if(!isnan(longitude) && !isnan(latitude))
    {
      int zoom;
      g_object_get(G_OBJECT(lib->map), "zoom", &zoom, NULL);
      _view_map_center_on_location(self, longitude, latitude, zoom);
    }
  }
}

static gboolean _view_map_center_on_image_list(dt_view_t *self, const GList *selected_images)
{
  // TODO: do something better than this approximation
  const float FIVE_KM = (0.01f * 1.852) * 5.0; // minimum context around single image/place

  GList const *l = selected_images;
  float max_longitude = -INFINITY;
  float max_latitude = -INFINITY;
  float min_longitude = INFINITY;
  float min_latitude = INFINITY;
  int count = 0;

  while(l)
  {
    const int imgid = GPOINTER_TO_INT(l->data);
    float lon = 0, lat = 0, el = 0;
    _get_image_location(imgid, &lon, &lat, &el);

    if(!isnan(lon) && !isnan(lat))
    {
      max_longitude = MAX(max_longitude, lon);
      min_longitude = MIN(min_longitude, lon);
      max_latitude = MAX(max_latitude, lat);
      min_latitude = MIN(min_latitude, lat);
      count++;
    }
    l = g_list_next(l);
  }

  if(count>0)
  {
    // enlarge the bounding box to avoid having the pictures on the border, and this will give a bit of context.

    float d_lon = max_longitude - min_longitude;
    float d_lat = max_latitude - min_latitude;

    if(d_lon>1.0)
      d_lon /= 100.0;
    else
      d_lon = (FIVE_KM - d_lon) / 2.0;

    if(d_lat>1.0)
      d_lat /= 100.0;
    else
      d_lat = (FIVE_KM - d_lat) / 2.0;

    max_longitude = CLAMP(max_longitude + d_lon, -180, 180);
    min_longitude = CLAMP(min_longitude - d_lon, -180, 180);

    max_latitude = CLAMP(max_latitude + d_lat, -90, 90);
    min_latitude = CLAMP(min_latitude - d_lat, -90, 90);

    _view_map_center_on_bbox(self, min_longitude, min_latitude, max_longitude, max_latitude);
    return TRUE;
  }
  else
    return FALSE;
}

static void _view_map_filmstrip_activate_callback(gpointer instance, gpointer user_data)
{
  dt_view_t *self = (dt_view_t *)user_data;
  const int32_t imgid = dt_view_filmstrip_get_activated_imgid(darktable.view_manager);
  _view_map_center_on_image(self, imgid);
}

static void pop_undo(gpointer user_data, dt_undo_type_t type, dt_undo_data_t *data)
{
  dt_view_t *self = (dt_view_t *)user_data;
  dt_map_t *lib = (dt_map_t *)self->data;

  if(type == DT_UNDO_GEOTAG)
  {
    dt_undo_geotag_t *geotag = (dt_undo_geotag_t *)data;
    _set_image_location(self, geotag->imgid, geotag->longitude, geotag->latitude, geotag->elevation, TRUE, FALSE);
    g_signal_emit_by_name(lib->map, "changed");
  }
}

static void _push_position(dt_view_t *self, int imgid, float longitude, float latitude, float elevation)
{
  dt_undo_geotag_t *geotag = malloc(sizeof(dt_undo_geotag_t));

  geotag->imgid = imgid;
  geotag->longitude = longitude;
  geotag->latitude = latitude;
  geotag->elevation = elevation;

  dt_undo_record(darktable.undo, self, DT_UNDO_GEOTAG, (dt_undo_data_t *)geotag, &pop_undo, free);
}

static void _get_image_location(int imgid, float *longitude, float *latitude, float *elevation)
{
  const dt_image_t *img = dt_image_cache_get(darktable.image_cache, imgid, 'r');
  *longitude = img->longitude;
  *latitude = img->latitude;
  *elevation = img->elevation;
  dt_image_cache_read_release(darktable.image_cache, img);
}

static void _check_imgid(gpointer user_data, dt_undo_type_t type, dt_undo_data_t *item)
{
  dt_undo_geotag_t *geotag = (dt_undo_geotag_t *)item;
  int *state = (int *)user_data;
  if (geotag->imgid == state[0])
    state[1] = 1;
}

static gboolean _in_undo(int imgid)
{
  int state[2];
  state[0] = imgid;
  state[1] = 0;
  dt_undo_iterate_internal(darktable.undo, DT_UNDO_GEOTAG, &state, _check_imgid);
  return state[1];
}

static void _set_image_location(dt_view_t *self, int imgid, float longitude, float latitude, float elevation,
                                gboolean set_elevation, gboolean record_undo)
{
  dt_image_t *img = dt_image_cache_get(darktable.image_cache, imgid, 'w');

  img->longitude = longitude;
  img->latitude = latitude;
  if(set_elevation) img->elevation = elevation;

  if(record_undo) _push_position(self, imgid, img->longitude, img->latitude, img->elevation);

  dt_image_cache_write_release(darktable.image_cache, img, DT_IMAGE_CACHE_SAFE);

  dt_control_signal_raise(darktable.signals, DT_SIGNAL_MOUSE_OVER_IMAGE_CHANGE);
}

static void _view_map_add_image_to_map(dt_view_t *self, int imgid, gint x, gint y)
{
  dt_map_t *lib = (dt_map_t *)self->data;
  float longitude, latitude;
  OsmGpsMapPoint *pt = osm_gps_map_point_new_degrees(0.0, 0.0);
  osm_gps_map_convert_screen_to_geographic(lib->map, x, y, pt);
  osm_gps_map_point_get_degrees(pt, &latitude, &longitude);
  osm_gps_map_point_free(pt);

  _set_image_location(self, imgid, longitude, latitude, 0.0, FALSE, TRUE);
}

static void _view_map_record_current_location(dt_view_t *self, int imgid)
{
  float longitude, latitude, elevation;
  _get_image_location(imgid, &longitude, &latitude, &elevation);
  if (!_in_undo(imgid))
    _push_position(self, imgid, longitude, latitude, elevation);
}

static void drag_and_drop_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y,
                                   GtkSelectionData *selection_data, guint target_type, guint time,
                                   gpointer data)
{
  dt_view_t *self = (dt_view_t *)data;
  dt_map_t *lib = (dt_map_t *)self->data;

  gboolean success = FALSE;

  if((selection_data != NULL) && (gtk_selection_data_get_length(selection_data) >= 0)
     && target_type == DND_TARGET_IMGID)
  {
    int *imgid = (int *)gtk_selection_data_get_data(selection_data);
    if(*imgid > 0)
    {
      _view_map_record_current_location(self, *imgid);
      _view_map_add_image_to_map(self, *imgid, x, y);
      success = TRUE;
    }
    else if(*imgid == -1) // everything which is selected
    {
      sqlite3_stmt *stmt;

      // record initial image position for images not yet in the undo list

      DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "SELECT DISTINCT imgid FROM main.selected_images",
                                  -1, &stmt, NULL);

      // create an undo group for current image position

      while(sqlite3_step(stmt) == SQLITE_ROW)
        _view_map_record_current_location(self, sqlite3_column_int(stmt, 0));

      DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "SELECT DISTINCT imgid FROM main.selected_images",
                                  -1, &stmt, NULL);
      while(sqlite3_step(stmt) == SQLITE_ROW)
        _view_map_add_image_to_map(self, sqlite3_column_int(stmt, 0), x, y);
      sqlite3_finalize(stmt);
      success = TRUE;
    }
  }
  gtk_drag_finish(context, success, FALSE, time);
  if(success) g_signal_emit_by_name(lib->map, "changed");
}

static void _view_map_dnd_get_callback(GtkWidget *widget, GdkDragContext *context,
                                       GtkSelectionData *selection_data, guint target_type, guint time,
                                       dt_view_t *self)
{
  dt_map_t *lib = (dt_map_t *)self->data;

  g_assert(selection_data != NULL);

  int imgid = lib->selected_image;

  switch(target_type)
  {
    case DND_TARGET_IMGID:
      gtk_selection_data_set(selection_data, gtk_selection_data_get_target(selection_data), _DWORD,
                             (guchar *)&imgid, sizeof(imgid));
      break;
    default: // return the location of the file as a last resort
    case DND_TARGET_URI:
    {
      gchar pathname[PATH_MAX] = { 0 };
      gboolean from_cache = TRUE;
      dt_image_full_path(imgid, pathname, sizeof(pathname), &from_cache);
      gchar *uri = g_strdup_printf("file://%s", pathname); // TODO: should we add the host?
      gtk_selection_data_set(selection_data, gtk_selection_data_get_target(selection_data), _BYTE,
                             (guchar *)uri, strlen(uri));
      g_free(uri);
      break;
    }
  }
}

static void _view_map_dnd_remove_callback(GtkWidget *widget, GdkDragContext *context, gint x, gint y,
                                          GtkSelectionData *selection_data, guint target_type, guint time,
                                          gpointer data)
{
  dt_view_t *self = (dt_view_t *)data;
  dt_map_t *lib = (dt_map_t *)self->data;

  gboolean success = FALSE;

  if((selection_data != NULL) && (gtk_selection_data_get_length(selection_data) >= 0)
     && target_type == DND_TARGET_IMGID)
  {
    int *imgid = (int *)gtk_selection_data_get_data(selection_data);
    if(*imgid > 0)
    {
      //  the image was dropped into the filmstrip, let's remove it in this case
      _set_image_location(self, *imgid, NAN, NAN, NAN, TRUE, TRUE);
      success = TRUE;
    }
  }
  gtk_drag_finish(context, success, FALSE, time);
  if(success) g_signal_emit_by_name(lib->map, "changed");
}

static gboolean _view_map_dnd_failed_callback(GtkWidget *widget, GdkDragContext *drag_context,
                                              GtkDragResult result, dt_view_t *self)
{
  dt_map_t *lib = (dt_map_t *)self->data;

  g_signal_emit_by_name(lib->map, "changed");

  return TRUE;
}

static gboolean _view_map_prefs_changed(dt_map_t *lib)
{
  gboolean prefs_changed = FALSE;
  int max_images_drawn = dt_conf_get_int("plugins/map/max_images_drawn");
  gboolean filter_images_drawn = dt_conf_get_bool("plugins/map/filter_images_drawn");

  if(lib->max_images_drawn != max_images_drawn) prefs_changed = TRUE;
  if(lib->filter_images_drawn != filter_images_drawn) prefs_changed = TRUE;

  return prefs_changed;
}

static void _view_map_build_main_query(dt_map_t *lib)
{
  char *geo_query;

  if(lib->statements.main_query) sqlite3_finalize(lib->statements.main_query);

  lib->max_images_drawn = dt_conf_get_int("plugins/map/max_images_drawn");
  if(lib->max_images_drawn == 0) lib->max_images_drawn = 100;
  lib->filter_images_drawn = dt_conf_get_bool("plugins/map/filter_images_drawn");
  geo_query = g_strdup_printf("SELECT * FROM (SELECT id, latitude FROM %s WHERE longitude >= ?1 AND "
                              "longitude <= ?2 AND latitude <= ?3 AND latitude >= ?4 AND longitude NOT NULL AND "
                              "latitude NOT NULL ORDER BY ABS(latitude - ?5), ABS(longitude - ?6) LIMIT 0, %d) "
                              "ORDER BY (180 - latitude), id",
                              lib->filter_images_drawn
                              ? "main.images i INNER JOIN memory.collected_images c ON i.id = c.imgid"
                              : "main.images",
                              lib->max_images_drawn);

  /* prepare the main query statement */
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), geo_query, -1, &lib->statements.main_query, NULL);

  g_free(geo_query);
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
