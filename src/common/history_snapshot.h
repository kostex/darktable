/*
    This file is part of darktable,
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

#pragma once

#include "common/undo.h"

typedef struct dt_undo_lt_history_t
{
  int32_t imgid;
  int before;
  int before_history_end;
  int after;
  int after_history_end;
} dt_undo_lt_history_t;

dt_undo_lt_history_t *dt_history_snapshot_item_init(void);

void dt_history_snapshot_undo_create(int32_t imgid, int *snap_id, int *history_end);

void dt_history_snapshot_undo_pop(gpointer user_data, dt_undo_type_t type, dt_undo_data_t data, dt_undo_action_t action);

void dt_history_snapshot_undo_lt_history_data_free(gpointer data);

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
