/*
    This file is part of darktable,
    copyright (c) 2011-2014 johannes hanika.

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

#include "common/image_cache.h"
#include "common/darktable.h"
#include "common/debug.h"
#include "common/exif.h"
#include "common/image.h"
#include "control/conf.h"
#include "develop/develop.h"

#include <sqlite3.h>

void dt_image_cache_allocate(void *data, dt_cache_entry_t *entry)
{
  entry->cost = sizeof(dt_image_t);

  dt_image_t *img = (dt_image_t *)g_malloc(sizeof(dt_image_t));
  dt_image_init(img);
  entry->data = img;
  // load stuff from db and store in cache:
  char *str;
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(
      dt_database_get(darktable.db),
      "SELECT id, group_id, film_id, width, height, filename, maker, model, lens, exposure, "
      "aperture, iso, focal_length, datetime_taken, flags, crop, orientation, focus_distance, "
      "raw_parameters, longitude, latitude, altitude, color_matrix, colorspace, version, raw_black, "
      "raw_maximum FROM main.images WHERE id = ?1",
      -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, entry->key);
  if(sqlite3_step(stmt) == SQLITE_ROW)
  {
    img->id = sqlite3_column_int(stmt, 0);
    img->group_id = sqlite3_column_int(stmt, 1);
    img->film_id = sqlite3_column_int(stmt, 2);
    img->width = sqlite3_column_int(stmt, 3);
    img->height = sqlite3_column_int(stmt, 4);
    img->crop_x = img->crop_y = img->crop_width = img->crop_height = 0;
    img->filename[0] = img->exif_maker[0] = img->exif_model[0] = img->exif_lens[0]
        = img->exif_datetime_taken[0] = '\0';
    str = (char *)sqlite3_column_text(stmt, 5);
    if(str) g_strlcpy(img->filename, str, sizeof(img->filename));
    str = (char *)sqlite3_column_text(stmt, 6);
    if(str) g_strlcpy(img->exif_maker, str, sizeof(img->exif_maker));
    str = (char *)sqlite3_column_text(stmt, 7);
    if(str) g_strlcpy(img->exif_model, str, sizeof(img->exif_model));
    str = (char *)sqlite3_column_text(stmt, 8);
    if(str) g_strlcpy(img->exif_lens, str, sizeof(img->exif_lens));
    img->exif_exposure = sqlite3_column_double(stmt, 9);
    img->exif_aperture = sqlite3_column_double(stmt, 10);
    img->exif_iso = sqlite3_column_double(stmt, 11);
    img->exif_focal_length = sqlite3_column_double(stmt, 12);
    str = (char *)sqlite3_column_text(stmt, 13);
    if(str) g_strlcpy(img->exif_datetime_taken, str, sizeof(img->exif_datetime_taken));
    img->flags = sqlite3_column_int(stmt, 14);
    img->loader = LOADER_UNKNOWN;
    img->exif_crop = sqlite3_column_double(stmt, 15);
    img->orientation = sqlite3_column_int(stmt, 16);
    img->exif_focus_distance = sqlite3_column_double(stmt, 17);
    if(img->exif_focus_distance >= 0 && img->orientation >= 0) img->exif_inited = 1;
    uint32_t tmp = sqlite3_column_int(stmt, 18);
    memcpy(&img->legacy_flip, &tmp, sizeof(dt_image_raw_parameters_t));
    if(sqlite3_column_type(stmt, 19) == SQLITE_FLOAT)
      img->geoloc.longitude = sqlite3_column_double(stmt, 19);
    else
      img->geoloc.longitude = NAN;
    if(sqlite3_column_type(stmt, 20) == SQLITE_FLOAT)
      img->geoloc.latitude = sqlite3_column_double(stmt, 20);
    else
      img->geoloc.latitude = NAN;
    if(sqlite3_column_type(stmt, 21) == SQLITE_FLOAT)
      img->geoloc.elevation = sqlite3_column_double(stmt, 21);
    else
      img->geoloc.elevation = NAN;
    const void *color_matrix = sqlite3_column_blob(stmt, 22);
    if(color_matrix)
      memcpy(img->d65_color_matrix, color_matrix, sizeof(img->d65_color_matrix));
    else
      img->d65_color_matrix[0] = NAN;
    g_free(img->profile);
    img->profile = NULL;
    img->profile_size = 0;
    img->colorspace = sqlite3_column_int(stmt, 23);
    img->version = sqlite3_column_int(stmt, 24);
    img->raw_black_level = sqlite3_column_int(stmt, 25);
    for(uint8_t i = 0; i < 4; i++) img->raw_black_level_separate[i] = 0;
    img->raw_white_point = sqlite3_column_int(stmt, 26);

    // buffer size? colorspace?
    if(img->flags & DT_IMAGE_LDR)
    {
      img->buf_dsc.channels = 4;
      img->buf_dsc.datatype = TYPE_FLOAT;
      img->buf_dsc.cst = iop_cs_rgb;
    }
    else if(img->flags & DT_IMAGE_HDR)
    {
      if(img->flags & DT_IMAGE_RAW)
      {
        img->buf_dsc.channels = 1;
        img->buf_dsc.datatype = TYPE_FLOAT;
        img->buf_dsc.cst = iop_cs_RAW;
      }
      else
      {
        img->buf_dsc.channels = 4;
        img->buf_dsc.datatype = TYPE_FLOAT;
        img->buf_dsc.cst = iop_cs_rgb;
      }
    }
    else
    {
      // raw
      img->buf_dsc.channels = 1;
      img->buf_dsc.datatype = TYPE_UINT16;
      img->buf_dsc.cst = iop_cs_RAW;
    }
  }
  else
  {
    img->id = -1;
    fprintf(stderr, "[image_cache_allocate] failed to open image %d from database: %s\n", entry->key,
            sqlite3_errmsg(dt_database_get(darktable.db)));
  }
  sqlite3_finalize(stmt);
  img->cache_entry = entry; // init backref
  // could downgrade lock write->read on entry->lock if we were using concurrencykit..
  dt_image_refresh_makermodel(img);
}

void dt_image_cache_deallocate(void *data, dt_cache_entry_t *entry)
{
  dt_image_t *img = (dt_image_t *)entry->data;
  g_free(img->profile);
  g_free(img);
}

void dt_image_cache_init(dt_image_cache_t *cache)
{
  // the image cache does no serialization.
  // (unsafe. data should be in db/xmp, not in any other additional cache,
  // also, it should be relatively fast to get the image_t structs from sql.)
  // TODO: actually an independent conf var?
  //       too large: dangerous and wasteful?
  //       can we get away with a fixed size?
  const uint32_t max_mem = 50 * 1024 * 1024;
  uint32_t num = (uint32_t)(1.5f * max_mem / sizeof(dt_image_t));
  dt_cache_init(&cache->cache, sizeof(dt_image_t), max_mem);
  dt_cache_set_allocate_callback(&cache->cache, &dt_image_cache_allocate, cache);
  dt_cache_set_cleanup_callback(&cache->cache, &dt_image_cache_deallocate, cache);

  dt_print(DT_DEBUG_CACHE, "[image_cache] has %d entries\n", num);
}

void dt_image_cache_cleanup(dt_image_cache_t *cache)
{
  dt_cache_cleanup(&cache->cache);
}

void dt_image_cache_print(dt_image_cache_t *cache)
{
  printf("[image cache] fill %.2f/%.2f MB (%.2f%%)\n", cache->cache.cost / (1024.0 * 1024.0),
         cache->cache.cost_quota / (1024.0 * 1024.0),
         (float)cache->cache.cost / (float)cache->cache.cost_quota);
}

dt_image_t *dt_image_cache_get(dt_image_cache_t *cache, const uint32_t imgid, char mode)
{
  if(imgid <= 0) return NULL;
  dt_cache_entry_t *entry = dt_cache_get(&cache->cache, imgid, mode);
  ASAN_UNPOISON_MEMORY_REGION(entry->data, sizeof(dt_image_t));
  dt_image_t *img = (dt_image_t *)entry->data;
  img->cache_entry = entry;
  return img;
}

dt_image_t *dt_image_cache_testget(dt_image_cache_t *cache, const uint32_t imgid, char mode)
{
  if(imgid <= 0) return 0;
  dt_cache_entry_t *entry = dt_cache_testget(&cache->cache, imgid, mode);
  if(!entry) return 0;
  ASAN_UNPOISON_MEMORY_REGION(entry->data, sizeof(dt_image_t));
  dt_image_t *img = (dt_image_t *)entry->data;
  img->cache_entry = entry;
  return img;
}

// drops the read lock on an image struct
void dt_image_cache_read_release(dt_image_cache_t *cache, const dt_image_t *img)
{
  if(!img || img->id <= 0) return;
  // just force the dt_image_t struct to make sure it has been locked before.
  dt_cache_release(&cache->cache, img->cache_entry);
}

// drops the write privileges on an image struct.
// this triggers a write-through to sql, and if the setting
// is present, also to xmp sidecar files (safe setting).
void dt_image_cache_write_release(dt_image_cache_t *cache, dt_image_t *img, dt_image_cache_write_mode_t mode)
{
  union {
      struct dt_image_raw_parameters_t s;
      uint32_t u;
  } flip;
  if(img->id <= 0) return;
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(
      dt_database_get(darktable.db),
      "UPDATE main.images SET width = ?1, height = ?2, filename = ?3, maker = ?4, model = ?5, "
      "lens = ?6, exposure = ?7, aperture = ?8, iso = ?9, focal_length = ?10, "
      "focus_distance = ?11, film_id = ?12, datetime_taken = ?13, flags = ?14, "
      "crop = ?15, orientation = ?16, raw_parameters = ?17, group_id = ?18, longitude = ?19, "
      "latitude = ?20, altitude = ?21, color_matrix = ?22, colorspace = ?23, raw_black = ?24, "
      "raw_maximum = ?25 WHERE id = ?26",
      -1, &stmt, NULL);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, img->width);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, img->height);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 3, img->filename, -1, SQLITE_STATIC);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 4, img->exif_maker, -1, SQLITE_STATIC);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 5, img->exif_model, -1, SQLITE_STATIC);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 6, img->exif_lens, -1, SQLITE_STATIC);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 7, img->exif_exposure);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 8, img->exif_aperture);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 9, img->exif_iso);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 10, img->exif_focal_length);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 11, img->exif_focus_distance);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 12, img->film_id);
  DT_DEBUG_SQLITE3_BIND_TEXT(stmt, 13, img->exif_datetime_taken, -1, SQLITE_STATIC);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 14, img->flags);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 15, img->exif_crop);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 16, img->orientation);
  flip.s = img->legacy_flip;
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 17, flip.u);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 18, img->group_id);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 19, img->geoloc.longitude);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 20, img->geoloc.latitude);
  DT_DEBUG_SQLITE3_BIND_DOUBLE(stmt, 21, img->geoloc.elevation);
  DT_DEBUG_SQLITE3_BIND_BLOB(stmt, 22, &img->d65_color_matrix, sizeof(img->d65_color_matrix), SQLITE_STATIC);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 23, img->colorspace);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 24, img->raw_black_level);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 25, img->raw_white_point);
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 26, img->id);
  int rc = sqlite3_step(stmt);
  if(rc != SQLITE_DONE) fprintf(stderr, "[image_cache_write_release] sqlite3 error %d\n", rc);
  sqlite3_finalize(stmt);

  // TODO: make this work in relaxed mode, too.
  if(mode == DT_IMAGE_CACHE_SAFE)
  {
    // rest about sidecars:
    // also synch dttags file:
    dt_image_write_sidecar_file(img->id);
  }
  dt_cache_release(&cache->cache, img->cache_entry);
}


// remove the image from the cache
void dt_image_cache_remove(dt_image_cache_t *cache, const uint32_t imgid)
{
  dt_cache_remove(&cache->cache, imgid);
}



// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
