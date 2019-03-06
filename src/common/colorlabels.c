/*
    This file is part of darktable,
    copyright (c) 2009--2011 johannes hanika.

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
#include "common/collection.h"
#include "common/darktable.h"
#include "common/debug.h"
#include "common/image_cache.h"
#include "common/undo.h"
#include "control/conf.h"
#include "control/control.h"
#include "gui/gtk.h"
#include <gdk/gdkkeysyms.h>

const char *dt_colorlabels_name[] = {
  "red", "yellow", "green", "blue", "purple",
  NULL // termination
};

typedef struct dt_undo_colorlabels_t
{
  int imgid;
  uint8_t before;
  uint8_t after;
} dt_undo_colorlabels_t;

static void _pop_undo(gpointer user_data, dt_undo_type_t type, dt_undo_data_t *data, dt_undo_action_t action)
{
  if(type == DT_UNDO_COLORLABELS)
  {
    GList *list = (GList *)data;

    while(list)
    {
      dt_undo_colorlabels_t *clabels = (dt_undo_colorlabels_t *)list->data;
      uint8_t labels;
      dt_colorlabels_remove_labels(clabels->imgid);

      if(action == DT_ACTION_UNDO)
        labels = clabels->before;
      else
        labels = clabels->after;

      for(int color=0; color<5; color++)
        if(labels & (1<<color))
          dt_colorlabels_set_label(clabels->imgid, color);

      list = g_list_next(list);
    }
  }
}

static dt_undo_colorlabels_t *_get_labels(int imgid, uint8_t label, gboolean add)
{
  dt_undo_colorlabels_t *result = (dt_undo_colorlabels_t *)malloc(sizeof(dt_undo_colorlabels_t));
  result->before = 0;
  result->imgid = imgid;
  sqlite3_stmt *stmt;

  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
                              "SELECT color FROM main.color_labels WHERE imgid=?1", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);

  while(sqlite3_step(stmt) == SQLITE_ROW)
  {
    const int color = sqlite3_column_int(stmt, 0);
    result->before |= (1 << color);
  }
  sqlite3_finalize(stmt);

  if(add)
    result->after = result->before | label;
  else
    result->after = result->before & (~label);

  return result;
}

GList *_get_labels_selection(uint8_t label, gboolean add)
{
  GList *result = NULL;

  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "SELECT imgid FROM main.selected_images", -1, &stmt, NULL);

  while(sqlite3_step(stmt) == SQLITE_ROW)
  {
    const int imgid = sqlite3_column_int(stmt, 0);
    result = g_list_append(result, _get_labels(imgid, label, add));
  }

  sqlite3_finalize(stmt);

  return result;
}

static void _colorlabels_undo_data_free(gpointer data)
{
  GList *l = (GList *)data;
  g_list_free(l);
}

void dt_colorlabels_remove_labels_selection()
{
  DT_DEBUG_SQLITE3_EXEC(dt_database_get(darktable.db),
                        "DELETE FROM main.color_labels WHERE imgid IN (SELECT imgid FROM main.selected_images)",
                        NULL, NULL, NULL);
}

void dt_colorlabels_remove_labels(const int imgid)
{
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "DELETE FROM main.color_labels WHERE imgid=?1", -1,
                              &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void dt_colorlabels_set_label(const int imgid, const int color)
{
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
                              "INSERT INTO main.color_labels (imgid, color) VALUES (?1, ?2)", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, color);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void dt_colorlabels_remove_label(const int imgid, const int color)
{
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
                              "DELETE FROM main.color_labels WHERE imgid=?1 AND color=?2", -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, color);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void dt_colorlabels_toggle_label_selection(const int color)
{
  sqlite3_stmt *stmt, *stmt2;

  GList *undo = NULL;

  dt_undo_start_group(darktable.undo, DT_UNDO_COLORLABELS);

  // check if all images in selection have that color label, i.e. try to get those which do not have the label
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "SELECT imgid FROM main.selected_images WHERE imgid "
                                                             "NOT IN (SELECT a.imgid FROM main.selected_images AS "
                                                             "a JOIN main.color_labels AS b ON a.imgid = b.imgid "
                                                             "WHERE b.color = ?1)",
                              -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, color);
  if(sqlite3_step(stmt) == SQLITE_ROW)
  {
    undo = _get_labels_selection(1 << color, TRUE);
    // none or only part of images have that color label, so label them all
    DT_DEBUG_SQLITE3_PREPARE_V2(
        dt_database_get(darktable.db),
        "INSERT OR IGNORE INTO main.color_labels (imgid, color) SELECT imgid, ?1 FROM main.selected_images",
        -1, &stmt2, NULL);
    DT_DEBUG_SQLITE3_BIND_INT(stmt2, 1, color);
    sqlite3_step(stmt2);
    sqlite3_finalize(stmt2);
  }
  else
  {
    undo = _get_labels_selection(1 << color, FALSE);
    // none of the selected images without that color label, so delete them all
    DT_DEBUG_SQLITE3_PREPARE_V2(
        dt_database_get(darktable.db),
        "DELETE FROM main.color_labels WHERE imgid IN (SELECT imgid FROM main.selected_images) AND color=?1", -1,
        &stmt2, NULL);
    DT_DEBUG_SQLITE3_BIND_INT(stmt2, 1, color);
    sqlite3_step(stmt2);
    sqlite3_finalize(stmt2);
  }
  sqlite3_finalize(stmt);

  dt_undo_record(darktable.undo, NULL, DT_UNDO_COLORLABELS, (dt_undo_data_t *)undo, &_pop_undo, _colorlabels_undo_data_free);
  dt_undo_end_group(darktable.undo);

  dt_collection_hint_message(darktable.collection);
}

void dt_colorlabels_toggle_label(const int imgid, const int color)
{
  if(imgid <= 0) return;
  sqlite3_stmt *stmt, *stmt2;
  GList *undo = NULL;

  dt_undo_start_group(darktable.undo, DT_UNDO_COLORLABELS);

  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
                              "SELECT * FROM main.color_labels WHERE imgid=?1 AND color=?2 LIMIT 1",
                              -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, color);
  if(sqlite3_step(stmt) == SQLITE_ROW)
  {
    undo = g_list_append(undo, _get_labels(imgid, 1 << color, FALSE));
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
                                "DELETE FROM main.color_labels WHERE imgid=?1 AND color=?2", -1, &stmt2, NULL);
    DT_DEBUG_SQLITE3_BIND_INT(stmt2, 1, imgid);
    DT_DEBUG_SQLITE3_BIND_INT(stmt2, 2, color);
    sqlite3_step(stmt2);
    sqlite3_finalize(stmt2);
  }
  else
  {
    undo = g_list_append(undo, _get_labels(imgid, 1 << color, TRUE));
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
                                "INSERT INTO main.color_labels (imgid, color) VALUES (?1, ?2)", -1, &stmt2, NULL);
    DT_DEBUG_SQLITE3_BIND_INT(stmt2, 1, imgid);
    DT_DEBUG_SQLITE3_BIND_INT(stmt2, 2, color);
    sqlite3_step(stmt2);
    sqlite3_finalize(stmt2);
  }
  sqlite3_finalize(stmt);

  dt_undo_record(darktable.undo, NULL, DT_UNDO_COLORLABELS, (dt_undo_data_t *)undo, &_pop_undo, _colorlabels_undo_data_free);
  dt_undo_end_group(darktable.undo);

  dt_collection_hint_message(darktable.collection);
}

int dt_colorlabels_check_label(const int imgid, const int color)
{
  if(imgid <= 0) return 0;
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
                              "SELECT * FROM main.color_labels WHERE imgid=?1 AND color=?2 LIMIT 1",
                              -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, color);
  if(sqlite3_step(stmt) == SQLITE_ROW)
  {
    sqlite3_finalize(stmt);
    return 1;
  }
  else
  {
    sqlite3_finalize(stmt);
    return 0;
  }
}

gboolean dt_colorlabels_key_accel_callback(GtkAccelGroup *accel_group, GObject *acceleratable, guint keyval,
                                           GdkModifierType modifier, gpointer data)
{
  const int mode = GPOINTER_TO_INT(data);
  int32_t selected;

  selected = dt_view_get_image_to_act_on();

  if(selected <= 0)
  {
    switch(mode)
    {
      case 0:
      case 1:
      case 2:
      case 3:
      case 4: // colors red, yellow, green, blue, purple
        dt_colorlabels_toggle_label_selection(mode);
        break;
      case 5:
      default: // remove all selected
        dt_colorlabels_remove_labels_selection();
        break;
    }
  }
  else
  {
    switch(mode)
    {
      case 0:
      case 1:
      case 2:
      case 3:
      case 4: // colors red, yellow, green, blue, purple
        dt_colorlabels_toggle_label(selected, mode);
        break;
      case 5:
      default: // remove all selected
        dt_colorlabels_remove_labels(selected);
        break;
    }
  }
  // synch to file:
  // TODO: move color labels to image_t cache and sync via write_get!
  dt_image_synch_xmp(selected);
  dt_control_signal_raise(darktable.signals, DT_SIGNAL_FILMROLLS_CHANGED);
  dt_control_queue_redraw_center();
  return TRUE;
}

// FIXME: XMP uses Red, Green, ... while we use red, green, ... What should this function return?
const char *dt_colorlabels_to_string(int label)
{
  if(label < 0 || label >= DT_COLORLABELS_LAST) return ""; // shouldn't happen
  return dt_colorlabels_name[label];
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
