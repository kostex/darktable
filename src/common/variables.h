/*
    This file is part of darktable,
    copyright (c) 2010 henrik andersson.
    copyright (c) 2010--2017 tobias ellinghaus.

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

#pragma once

#include <glib.h>
#include <stdint.h>

typedef struct dt_variables_params_t
{
  /** used for expanding variables that uses filename $(FILE_FOLDER) $(FILE_NAME) and $(FILE_EXTENSION). */
  const gchar *filename;

  /** used for expanding variable $(JOBCODE) */
  const gchar *jobcode;

  /** used for expanding variables such as $(IMAGE_WIDTH) $(IMAGE_HEIGHT). */
  uint32_t imgid;

  /** used as thread-safe sequence number. only used if >= 0. */
  int sequence;

  /** internal variables data */
  struct dt_variables_data_t *data;

} dt_variables_params_t;

/** allocate and initializes a dt_variables_params_t. */
void dt_variables_params_init(dt_variables_params_t **params);
/** destroys an initialized dt_variables_params_t, pointer is garbage after this call. */
void dt_variables_params_destroy(dt_variables_params_t *params);
/** set max image width and height defined for an export session in a dt_variables_params_t. */
void dt_variables_set_max_width_height(dt_variables_params_t *params, int max_width, int max_height);
/** set the time in a dt_variables_params_t. */
void dt_variables_set_time(dt_variables_params_t *params, time_t time);
/** set the time to use for EXIF variables */
void dt_variables_set_exif_time(dt_variables_params_t *params, time_t time);

/** expands variables in string. the result should be freed with g_free(). */
char *dt_variables_expand(dt_variables_params_t *params, gchar *source, gboolean iterate);
/** reset sequence number */
void dt_variables_reset_sequence(dt_variables_params_t *params);

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
