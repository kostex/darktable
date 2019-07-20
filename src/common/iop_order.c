/*
    This file is part of darktable,
    copyright (c) 2018 edgardo hoszowski.

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

#include "common/darktable.h"
#include "common/colorspaces_inline_conversions.h"
#include "common/iop_order.h"
#include "common/debug.h"
#include "develop/imageop.h"
#include "develop/imageop_math.h"
#include "develop/pixelpipe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DT_IOP_ORDER_VERSION 2

static void _ioppr_insert_iop_after(GList **_iop_order_list, GList *history_list, const char *op_new, const char *op_previous, const int dont_move);
static void _ioppr_insert_iop_before(GList **_iop_order_list, GList *history_list, const char *op_new, const char *op_next, const int dont_move);
static void _ioppr_move_iop_after(GList **_iop_order_list, const char *op_current, const char *op_prev, const int dont_move);
static void _ioppr_move_iop_before(GList **_iop_order_list, const char *op_current, const char *op_next, const int dont_move);


/* migrates *_iop_order_list from old_version to the next version (version + 1)
 * limitations:
 * - to move an existing module that is always enabled a new version must be created, otherwise
 *   modules can be added/moved in the current version
 * - a module can't be more than once on the same version
 */
static int _ioppr_legacy_iop_order_step(GList **_iop_order_list, GList *history_list, const int old_version, const int dont_move)
{
  int new_version = -1;

  // version 1 --> 2
  if(old_version == 1)
  {
    _ioppr_move_iop_after(_iop_order_list, "colorin", "demosaic", dont_move);
    _ioppr_move_iop_before(_iop_order_list, "colorout", "clahe", dont_move);
    _ioppr_insert_iop_after(_iop_order_list, history_list, "basicadj", "colorin", dont_move);
    _ioppr_insert_iop_after(_iop_order_list, history_list, "rgbcurve", "levels", dont_move);
    _ioppr_insert_iop_after(_iop_order_list, history_list, "lut3d", "grain", dont_move);
    _ioppr_insert_iop_before(_iop_order_list, history_list, "rgblevels", "rgbcurve", dont_move);
    _ioppr_move_iop_before(_iop_order_list, "dither", "borders", dont_move);

    new_version = 2;
  }

  if(new_version <= 0)
    fprintf(stderr, "[_ioppr_legacy_iop_order_step] missing step migrating from version %i\n", old_version);

  return new_version;
}

// returns a list of dt_iop_order_rule_t
// this do not have versions
GList *dt_ioppr_get_iop_order_rules()
{
  GList *rules = NULL;

  const dt_iop_order_rule_t rule_entry[] = { { "rawprepare", "invert" },
                                                { "invert", "temperature" },
                                                { "temperature", "highlights" },
                                                { "highlights", "cacorrect" },
                                                { "cacorrect", "hotpixels" },
                                                { "hotpixels", "rawdenoise" },
                                                { "rawdenoise", "demosaic" },
                                                { "demosaic", "colorin" },
                                                { "colorin", "colorout" },
                                                { "colorout", "gamma" },
                                                { "\0", "\0" } };

  int i = 0;
  while(rule_entry[i].op_prev[0])
  {
    dt_iop_order_rule_t *rule = calloc(1, sizeof(dt_iop_order_rule_t));

    snprintf(rule->op_prev, sizeof(rule->op_prev), "%s", rule_entry[i].op_prev);
    snprintf(rule->op_next, sizeof(rule->op_next), "%s", rule_entry[i].op_next);

    rules = g_list_append(rules, rule);
    i++;
  }

  return rules;
}

// first version of iop order, must never be modified
// it returns a list with the default iop_order per module, starting at 1.0, increment by 1.0
static GList *_ioppr_get_iop_order_v1()
{
  GList *iop_order_list = NULL;

  const dt_iop_order_entry_t prior_entry[] = { { 0.0, "rawprepare" },
                                                  { 0.0, "invert" },
                                                  { 0.0, "temperature" },
                                                  { 0.0, "highlights" },
                                                  { 0.0, "cacorrect" },
                                                  { 0.0, "hotpixels" },
                                                  { 0.0, "rawdenoise" },
                                                  { 0.0, "demosaic" },
                                                  { 0.0, "mask_manager" },
                                                  { 0.0, "denoiseprofile" },
                                                  { 0.0, "tonemap" },
                                                  { 0.0, "exposure" },
                                                  { 0.0, "spots" },
                                                  { 0.0, "retouch" },
                                                  { 0.0, "lens" },
                                                  { 0.0, "ashift" },
                                                  { 0.0, "liquify" },
                                                  { 0.0, "rotatepixels" },
                                                  { 0.0, "scalepixels" },
                                                  { 0.0, "flip" },
                                                  { 0.0, "clipping" },
                                                  { 0.0, "graduatednd" },
                                                  { 0.0, "basecurve" },
                                                  { 0.0, "bilateral" },
                                                  { 0.0, "profile_gamma" },
                                                  { 0.0, "hazeremoval" },
                                                  { 0.0, "colorin" },
                                                  { 0.0, "colorreconstruct" },
                                                  { 0.0, "colorchecker" },
                                                  { 0.0, "defringe" },
                                                  { 0.0, "equalizer" },
                                                  { 0.0, "vibrance" },
                                                  { 0.0, "colorbalance" },
                                                  { 0.0, "colorize" },
                                                  { 0.0, "colortransfer" },
                                                  { 0.0, "colormapping" },
                                                  { 0.0, "bloom" },
                                                  { 0.0, "nlmeans" },
                                                  { 0.0, "globaltonemap" },
                                                  { 0.0, "shadhi" },
                                                  { 0.0, "atrous" },
                                                  { 0.0, "bilat" },
                                                  { 0.0, "colorzones" },
                                                  { 0.0, "lowlight" },
                                                  { 0.0, "monochrome" },
                                                  { 0.0, "filmic" },
                                                  { 0.0, "colisa" },
                                                  { 0.0, "zonesystem" },
                                                  { 0.0, "tonecurve" },
                                                  { 0.0, "levels" },
                                                  { 0.0, "relight" },
                                                  { 0.0, "colorcorrection" },
                                                  { 0.0, "sharpen" },
                                                  { 0.0, "lowpass" },
                                                  { 0.0, "highpass" },
                                                  { 0.0, "grain" },
                                                  { 0.0, "colorcontrast" },
                                                  { 0.0, "colorout" },
                                                  { 0.0, "channelmixer" },
                                                  { 0.0, "soften" },
                                                  { 0.0, "vignette" },
                                                  { 0.0, "splittoning" },
                                                  { 0.0, "velvia" },
                                                  { 0.0, "clahe" },
                                                  { 0.0, "finalscale" },
                                                  { 0.0, "overexposed" },
                                                  { 0.0, "rawoverexposed" },
                                                  { 0.0, "borders" },
                                                  { 0.0, "watermark" },
                                                  { 0.0, "dither" },
                                                  { 0.0, "gamma" },
                                                  { 0.0, "\0" }
  };

  int i = 0;
  while(prior_entry[i].operation[0] != '\0')
  {
    dt_iop_order_entry_t *order_entry = calloc(1, sizeof(dt_iop_order_entry_t));

    order_entry->iop_order = (double)(i + 1);
    snprintf(order_entry->operation, sizeof(order_entry->operation), "%s", prior_entry[i].operation);

    iop_order_list = g_list_append(iop_order_list, order_entry);
    i++;
  }

  return iop_order_list;
}

// returns the first iop order entry that matches operation == op_name
dt_iop_order_entry_t *dt_ioppr_get_iop_order_entry(GList *iop_order_list, const char *op_name)
{
  dt_iop_order_entry_t *iop_order_entry = NULL;

  GList *iops_order = g_list_first(iop_order_list);
  while(iops_order)
  {
    dt_iop_order_entry_t *order_entry = (dt_iop_order_entry_t *)(iops_order->data);

    if(strcmp(order_entry->operation, op_name) == 0)
    {
      iop_order_entry = order_entry;
      break;
    }

    iops_order = g_list_next(iops_order);
  }

  return iop_order_entry;
}

// returns the iop_order associated with the iop order entry that matches operation == op_name
double dt_ioppr_get_iop_order(GList *iop_order_list, const char *op_name)
{
  double iop_order = DBL_MAX;
  dt_iop_order_entry_t *order_entry = dt_ioppr_get_iop_order_entry(iop_order_list, op_name);

  if(order_entry)
    iop_order = order_entry->iop_order;

  return iop_order;
}

// insert op_new before op_next on *_iop_order_list
// it sets the iop_order on op_new
// if check_history == 1 it check that the generated iop_order do not exists on any module in history
static void _ioppr_insert_iop_before(GList **_iop_order_list, GList *history_list, const char *op_new, const char *op_next, const int check_history)
{
  GList *iop_order_list = *_iop_order_list;

  // check that the new operation don't exists on the list
  if(dt_ioppr_get_iop_order_entry(iop_order_list, op_new) == NULL)
  {
    // create a new iop order entry
    dt_iop_order_entry_t *iop_order_new = (dt_iop_order_entry_t*)calloc(1, sizeof(dt_iop_order_entry_t));
    snprintf(iop_order_new->operation, sizeof(iop_order_new->operation), "%s", op_new);

    // search for the previous one
    int position = 0;
    int found = 0;
    double iop_order_prev = DBL_MAX;
    double iop_order_next = DBL_MAX;
    GList *iops_order = g_list_first(iop_order_list);
    while(iops_order)
    {
      dt_iop_order_entry_t *order_entry = (dt_iop_order_entry_t *)(iops_order->data);
      if(strcmp(order_entry->operation, op_next) == 0)
      {
        iop_order_next = order_entry->iop_order;
        found = 1;
        break;
      }
      iop_order_prev = order_entry->iop_order;
      position++;

      iops_order = g_list_next(iops_order);
    }

    // now we have to check if there's a module with iop_order between iop_order_prev and iop_order_next
    if(found)
    {
      if(!check_history)
      {
        GList *history = g_list_first(history_list);
        while(history)
        {
          dt_dev_history_item_t *hist = (dt_dev_history_item_t *)(history->data);

          if(hist->iop_order >= iop_order_prev && hist->iop_order <= iop_order_next)
            iop_order_prev = hist->iop_order;

          history = g_list_next(history);
        }
      }
    }
    else
      fprintf(stderr, "[_ioppr_insert_iop_before] module %s don't exists on iop order list\n", op_next);

    if(found)
    {
      // set the iop_order
      iop_order_new->iop_order = iop_order_prev + (iop_order_next - iop_order_prev) / 2.0;

      // insert it on the proper order
      iop_order_list = g_list_insert(iop_order_list, iop_order_new, position);
    }
  }
  else
    fprintf(stderr, "[_ioppr_insert_iop_before] module %s already exists on iop order list\n", op_new);

  *_iop_order_list = iop_order_list;
}

// insert op_new after op_prev on *_iop_order_list
// it updates the iop_order on op_new
// if check_history == 1 it check that the generated iop_order do not exists on any module in history
static void _ioppr_insert_iop_after(GList **_iop_order_list, GList *history_list, const char *op_new, const char *op_prev, const int check_history)
{
  GList *iop_order_list = *_iop_order_list;

  // inserting after op_prev is the same as moving before the very next one after op_prev
  dt_iop_order_entry_t *prior_next = NULL;
  GList *iops_order = g_list_last(iop_order_list);
  while(iops_order)
  {
    dt_iop_order_entry_t *order_entry = (dt_iop_order_entry_t *)iops_order->data;
    if(strcmp(order_entry->operation, op_prev) == 0) break;

    prior_next = order_entry;
    iops_order = g_list_previous(iops_order);
  }
  if(prior_next == NULL)
  {
    fprintf(
        stderr,
        "[_ioppr_insert_iop_after] can't find module previous to %s while moving %s after it\n",
        op_prev, op_new);
  }
  else
    _ioppr_insert_iop_before(&iop_order_list, history_list, op_new, prior_next->operation, check_history);

  *_iop_order_list = iop_order_list;
}

// moves op_current before op_next by updating the iop_order
// only if dont_move == FALSE
static void _ioppr_move_iop_before(GList **_iop_order_list, const char *op_current, const char *op_next, const int dont_move)
{
  if(dont_move) return;

  GList *iop_order_list = *_iop_order_list;

  int position = 0;
  int found = 0;
  dt_iop_order_entry_t *iop_order_prev = NULL;
  dt_iop_order_entry_t *iop_order_next = NULL;
  dt_iop_order_entry_t *iop_order_current = NULL;
  GList *iops_order_current = NULL;

  // search for the current one
  GList *iops_order = g_list_first(iop_order_list);
  while(iops_order)
  {
    dt_iop_order_entry_t *order_entry = (dt_iop_order_entry_t *)(iops_order->data);
    if(strcmp(order_entry->operation, op_current) == 0)
    {
      iops_order_current = iops_order;
      iop_order_current = order_entry;
      found = 1;
      break;
    }

    iops_order = g_list_next(iops_order);
  }

  if(found)
  {
    // remove it from the list
    iop_order_list = g_list_remove_link(iop_order_list, iops_order_current);
  }
  else
    fprintf(stderr, "[_ioppr_move_iop_before] current module %s don't exists on iop order list\n", op_current);

  // search for the previous and next one
  if(found)
  {
    found = 0;
    iops_order = g_list_first(iop_order_list);
    while(iops_order)
    {
      dt_iop_order_entry_t *order_entry = (dt_iop_order_entry_t *)(iops_order->data);
      if(strcmp(order_entry->operation, op_next) == 0)
      {
        iop_order_next = order_entry;
        found = 1;
        break;
      }
      iop_order_prev = order_entry;
      position++;

      iops_order = g_list_next(iops_order);
    }
  }

  if(found)
  {
    // set the iop_order
    iop_order_current->iop_order = iop_order_prev->iop_order + (iop_order_next->iop_order - iop_order_prev->iop_order) / 2.0;

    // insert it on the proper order
    iop_order_list = g_list_insert(iop_order_list, iop_order_current, position);
  }
  else
    fprintf(stderr, "[_ioppr_move_iop_before] next module %s don't exists on iop order list\n", op_next);

  *_iop_order_list = iop_order_list;
}

// moves op_current after op_prev by updating the iop_order
// only if dont_move == FALSE
static void _ioppr_move_iop_after(GList **_iop_order_list, const char *op_current, const char *op_prev, const int dont_move)
{
  if(dont_move) return;

  GList *iop_order_list = *_iop_order_list;

  // moving after op_prev is the same as moving before the very next one after op_prev
  dt_iop_order_entry_t *prior_next = NULL;
  GList *iops_order = g_list_last(iop_order_list);
  while(iops_order)
  {
    dt_iop_order_entry_t *order_entry = (dt_iop_order_entry_t *)iops_order->data;
    if(strcmp(order_entry->operation, op_prev) == 0) break;

    prior_next = order_entry;
    iops_order = g_list_previous(iops_order);
  }
  if(prior_next == NULL)
  {
    fprintf(
        stderr,
        "[_ioppr_move_iop_after] can't find module previous to %s while moving %s after it\n",
        op_prev, op_current);
  }
  else
    _ioppr_move_iop_before(&iop_order_list, op_current, prior_next->operation, dont_move);

  *_iop_order_list = iop_order_list;
}

// returns a list of dt_iop_order_entry_t
// if *_version == 0 it returns the current version and updates *_version
GList *dt_ioppr_get_iop_order_list(int *_version)
{
  GList *iop_order_list = _ioppr_get_iop_order_v1();
  int old_version = 1;
  const int version = ((_version == NULL) || (*_version == 0)) ? DT_IOP_ORDER_VERSION: *_version;

  while(old_version < version && old_version > 0)
  {
    old_version = _ioppr_legacy_iop_order_step(&iop_order_list, NULL, old_version, FALSE);
  }

  if(old_version != version)
  {
    fprintf(stderr, "[dt_ioppr_get_iop_order_list] error building iop_order_list to version %i\n", version);
  }

  if(_version && *_version == 0 && old_version > 0) *_version = old_version;

  return iop_order_list;
}

// sets the iop_order on each module of *_iop_list
// iop_order is set only for base modules, multi-instances will be flagged as unused with DBL_MAX
// if a module do not exists on iop_order_list it is flagged as unused with DBL_MAX
void dt_ioppr_set_default_iop_order(GList **_iop_list, GList *iop_order_list)
{
  GList *iop_list = *_iop_list;

  GList *modules = g_list_first(iop_list);
  while(modules)
  {
    dt_iop_module_t *mod = (dt_iop_module_t *)(modules->data);

    if(mod->multi_priority == 0)
    {
      mod->iop_order = dt_ioppr_get_iop_order(iop_order_list, mod->op);
    }
    // muti-instances will be set by read history
    else
    {
      mod->iop_order = DBL_MAX;
    }

    modules = g_list_next(modules);
  }
  // we need to set the right order
  iop_list = g_list_sort(iop_list, dt_sort_iop_by_order);

  *_iop_list = iop_list;
}

// returns the first dt_dev_history_item_t on history_list where hist->module == mod
static dt_dev_history_item_t *_ioppr_search_history_by_module(GList *history_list, dt_iop_module_t *mod)
{
  dt_dev_history_item_t *hist_entry = NULL;

  GList *history = g_list_first(history_list);
  while(history)
  {
    dt_dev_history_item_t *hist = (dt_dev_history_item_t *)(history->data);

    if(hist->module == mod)
    {
      hist_entry = hist;
      break;
    }

    history = g_list_next(history);
  }

  return hist_entry;
}

// check if there's duplicate iop_order entries in iop_list
// if so, updates the iop_order to be unique, but only if the module is disabled and not in history
void dt_ioppr_check_duplicate_iop_order(GList **_iop_list, GList *history_list)
{
  GList *iop_list = *_iop_list;
  dt_iop_module_t *mod_prev = NULL;

  // get the first module
  GList *modules = g_list_first(iop_list);
  if(modules)
  {
    mod_prev = (dt_iop_module_t *)(modules->data);
    modules = g_list_next(modules);
  }
  // check for each module if iop_order is the same as the previous one
  // if so, change it, but only if disabled and not in history
  while(modules)
  {
    int reset_list = 0;
    dt_iop_module_t *mod = (dt_iop_module_t *)(modules->data);

    if(mod->iop_order == mod_prev->iop_order && mod->iop_order != DBL_MAX)
    {
      int can_move = 0;

      if(!mod->enabled && _ioppr_search_history_by_module(history_list, mod) == NULL)
      {
        can_move = 1;

        GList *modules1 = g_list_next(modules);
        if(modules1)
        {
          dt_iop_module_t *mod_next = (dt_iop_module_t *)(modules1->data);
          if(mod->iop_order != mod_next->iop_order)
          {
            mod->iop_order += (mod_next->iop_order - mod->iop_order) / 2.0;
          }
          else
          {
            dt_ioppr_check_duplicate_iop_order(&modules, history_list);
            reset_list = 1;
          }
        }
        else
        {
          mod->iop_order += 1.0;
        }
      }
      else if(!mod_prev->enabled && _ioppr_search_history_by_module(history_list, mod_prev) == NULL)
      {
        can_move = 1;

        GList *modules1 = g_list_previous(modules);
        if(modules1) modules1 = g_list_previous(modules1);
        if(modules1)
        {
          dt_iop_module_t *mod_next = (dt_iop_module_t *)(modules1->data);
          if(mod_prev->iop_order != mod_next->iop_order)
          {
            mod_prev->iop_order -= (mod_prev->iop_order - mod_next->iop_order) / 2.0;
          }
          else
          {
            can_move = 0;
            fprintf(stderr, "[dt_ioppr_check_duplicate_iop_order 1] modules %s %s(%f) and %s %s(%f) has the same iop_order\n",
                mod_prev->op, mod_prev->multi_name, mod_prev->iop_order, mod->op, mod->multi_name, mod->iop_order);
          }
        }
        else
        {
          mod_prev->iop_order -= 0.5;
        }
      }

      if(!can_move)
      {
        fprintf(stderr, "[dt_ioppr_check_duplicate_iop_order] modules %s %s(%f) and %s %s(%f) has the same iop_order\n",
            mod_prev->op, mod_prev->multi_name, mod_prev->iop_order, mod->op, mod->multi_name, mod->iop_order);
      }
    }

    if(reset_list)
    {
      modules = g_list_first(iop_list);
      if(modules)
      {
        mod_prev = (dt_iop_module_t *)(modules->data);
        modules = g_list_next(modules);
      }
    }
    else
    {
      mod_prev = mod;
      modules = g_list_next(modules);
    }
  }

  *_iop_list = iop_list;
}

// upgrades iop & iop order to current version
void dt_ioppr_legacy_iop_order(GList **_iop_list, GList **_iop_order_list, GList *history_list, const int _old_version)
{
  GList *iop_list = *_iop_list;
  GList *iop_order_list = *_iop_order_list;
  int dt_version = DT_IOP_ORDER_VERSION;
  int old_version = _old_version;

  // we want to add any module created after this version of iop_order
  // but we won't move existing modules so only add methods will be executed
  while(old_version < dt_version && old_version > 0)
  {
    old_version = _ioppr_legacy_iop_order_step(&iop_order_list, history_list, old_version, TRUE);
  }

  // now that we have a list of iop_order for version new_version but with all new modules
  // we take care of the iop_order of new modules on iop list
  GList *modules = g_list_first(iop_list);
  while(modules)
  {
    dt_iop_module_t *mod = (dt_iop_module_t *)(modules->data);

    if(mod->multi_priority == 0 && mod->iop_order == DBL_MAX)
    {
      mod->iop_order = dt_ioppr_get_iop_order(iop_order_list, mod->op);
      if(mod->iop_order == DBL_MAX)
        fprintf(stderr, "[dt_ioppr_legacy_iop_order] can't find iop_order for module %s\n", mod->op);
    }

    modules = g_list_next(modules);
  }
  // we need to set the right order
  iop_list = g_list_sort(iop_list, dt_sort_iop_by_order);

  // and check for duplicates
  dt_ioppr_check_duplicate_iop_order(&iop_list, history_list);

  *_iop_list = iop_list;
  *_iop_order_list = iop_order_list;
}

// check if all so modules on iop_list have a iop_order defined in iop_order_list
int dt_ioppr_check_so_iop_order(GList *iop_list, GList *iop_order_list)
{
  int iop_order_missing = 0;

  // check if all the modules have their iop_order assigned
  GList *modules = g_list_first(iop_list);
  while(modules)
  {
    dt_iop_module_so_t *mod = (dt_iop_module_so_t *)(modules->data);

    dt_iop_order_entry_t *entry = dt_ioppr_get_iop_order_entry(iop_order_list, mod->op);
    if(entry == NULL)
    {
      iop_order_missing = 1;
      fprintf(stderr, "[dt_ioppr_check_so_iop_order] missing iop_order for module %s\n", mod->op);
    }
    modules = g_list_next(modules);
  }

  return iop_order_missing;
}

static void *_dup_iop_order_entry(const void *src, gpointer data)
{
  dt_iop_order_entry_t *scr_entry = (dt_iop_order_entry_t *)src;
  dt_iop_order_entry_t *new_entry = malloc(sizeof(dt_iop_order_entry_t));
  memcpy(new_entry, scr_entry, sizeof(dt_iop_order_entry_t));
  return (void *)new_entry;
}

// returns a duplicate of iop_order_list
GList *dt_ioppr_iop_order_copy_deep(GList *iop_order_list)
{
  return (GList *)g_list_copy_deep(iop_order_list, _dup_iop_order_entry, NULL);
}

// helper to sort a GList of dt_iop_module_t by iop_order
gint dt_sort_iop_by_order(gconstpointer a, gconstpointer b)
{
  const dt_iop_module_t *am = (const dt_iop_module_t *)a;
  const dt_iop_module_t *bm = (const dt_iop_module_t *)b;
  if(am->iop_order > bm->iop_order) return 1;
  if(am->iop_order < bm->iop_order) return -1;
  return 0;
}

// if module can be placed before than module_next on the pipe
// it returns the new iop_order
// if it cannot be placed it returns -1.0
// this assums that the order is always positive
double dt_ioppr_get_iop_order_before_iop(GList *iop_list, dt_iop_module_t *module, dt_iop_module_t *module_next,
                                  const int validate_order, const int log_error)
{
  if((module->flags() & IOP_FLAGS_FENCE) && validate_order)
  {
    if(log_error)
      fprintf(stderr, "[dt_ioppr_get_iop_order_before_iop] module %s(%f) is a fence, can't move it before %s %s(%f)\n",
          module->op, module->iop_order, module_next->op, module_next->multi_name, module_next->iop_order);
    return -1.0;
  }

  double iop_order = -1.0;

  // module is before on the pipe
  // move it up
  if(module->iop_order < module_next->iop_order)
  {
    // let's first search for module
    GList *modules = g_list_first(iop_list);
    while(modules)
    {
      dt_iop_module_t *mod = (dt_iop_module_t *)modules->data;
      if(mod == module) break;
      modules = g_list_next(modules);
    }

    // we found the module
    if(modules)
    {
      dt_iop_module_t *mod1 = NULL;
      dt_iop_module_t *mod2 = NULL;

      // now search for module_next and the one previous to that, so iop_order can be calculated
      // also check the rules
      modules = g_list_next(modules);
      while(modules)
      {
        dt_iop_module_t *mod = (dt_iop_module_t *)modules->data;

        // if we reach module_next everithing is OK
        if(mod == module_next)
        {
          mod2 = mod;
          break;
        }

        // check for rules
        if(validate_order)
        {
          // check if module can be moved around this one
          if(mod->flags() & IOP_FLAGS_FENCE)
          {
            if(log_error)
              fprintf(stderr, "[dt_ioppr_get_iop_order_before_iop] can't move %s %s(%f) pass %s %s(%f)\n",
                  module->op, module->multi_name, module->iop_order, mod->op, mod->multi_name, mod->iop_order);
            break;
          }

          // is there a rule about swapping this two?
          int rule_found = 0;
          GList *rules = g_list_first(darktable.iop_order_rules);
          while(rules)
          {
            dt_iop_order_rule_t *rule = (dt_iop_order_rule_t *)rules->data;

            if(strcmp(module->op, rule->op_prev) == 0 && strcmp(mod->op, rule->op_next) == 0)
            {
              if(log_error)
                fprintf(stderr, "[dt_ioppr_get_iop_order_before_iop] found rule %s %s while moving %s %s(%f) before %s %s(%f)\n",
                        rule->op_prev, rule->op_next, module->op,  module->multi_name, module->iop_order, module_next->op,  module_next->multi_name, module_next->iop_order);
              rule_found = 1;
              break;
            }

            rules = g_list_next(rules);
          }
          if(rule_found) break;
        }

        mod1 = mod;
        modules = g_list_next(modules);
      }

      // we reach the module_next module
      if(mod2)
      {
        // this is already the previous module!
        if(module == mod1)
        {
          if(log_error)
            fprintf(stderr, "[dt_ioppr_get_iop_order_before_iop] %s %s(%f) is already previous to %s %s(%f)\n",
                module->op, module->multi_name, module->iop_order, module_next->op, module_next->multi_name, module_next->iop_order);
        }
        else if(mod1->iop_order == mod2->iop_order)
        {
          fprintf(stderr, "[dt_ioppr_get_iop_order_before_iop] %s %s(%f) and %s %s(%f) has the same iop_order\n",
              mod1->op, mod1->multi_name, mod1->iop_order, mod2->op, mod2->multi_name, mod2->iop_order);
        }
        else
        {
          // calculate new iop_order
          iop_order = mod1->iop_order + (mod2->iop_order - mod1->iop_order) / 2.0;
          /* fprintf(stderr, "[dt_ioppr_get_iop_order_before_iop] 2-calculated new iop_order=%f for %s(%f) between %s(%f) and %s(%f)\n",
                 iop_order, module->op, module->iop_order, module_next->op, module_next->iop_order, mod->op, mod->iop_order); */
        }
      }
    }
    else
      fprintf(stderr, "[dt_ioppr_get_iop_order_before_iop] can't find module %s %s\n", module->op, module->multi_name);
  }
  // module is next on the pipe
  // move it down
  else if(module->iop_order > module_next->iop_order)
  {
    // let's first search for module
    GList *modules = g_list_last(iop_list);
    while(modules)
    {
      dt_iop_module_t *mod = (dt_iop_module_t *)modules->data;
      if(mod == module) break;
      modules = g_list_previous(modules);
    }

    // we found the module
    if(modules)
    {
      dt_iop_module_t *mod1 = NULL;
      dt_iop_module_t *mod2 = NULL;

      // now search for module_next and the one next to that, so iop_order can be calculated
      // also check the rules
      modules = g_list_previous(modules);
      while(modules)
      {
        dt_iop_module_t *mod = (dt_iop_module_t *)modules->data;

        // we reach the module next to module_next, everithing is OK
        if(mod2 != NULL)
        {
          mod1 = mod;
          break;
        }

        // check for rules
        if(validate_order)
        {
          // check if module can be moved around this one
          if(mod->flags() & IOP_FLAGS_FENCE)
          {
            if(log_error)
              fprintf(stderr, "[dt_ioppr_get_iop_order_before_iop] can't move %s %s(%f) pass %s %s(%f)\n",
                  module->op, module->multi_name, module->iop_order, mod->op, mod->multi_name, mod->iop_order);
            break;
          }

          // is there a rule about swapping this two?
          int rule_found = 0;
          GList *rules = g_list_first(darktable.iop_order_rules);
          while(rules)
          {
            dt_iop_order_rule_t *rule = (dt_iop_order_rule_t *)rules->data;

            if(strcmp(mod->op, rule->op_prev) == 0 && strcmp(module->op, rule->op_next) == 0)
            {
              if(log_error)
                fprintf(stderr, "[dt_ioppr_get_iop_order_before_iop] found rule %s %s while moving %s %s(%f) before %s %s(%f)\n",
                        rule->op_prev, rule->op_next, module->op,  module->multi_name, module->iop_order, module_next->op,  module_next->multi_name, module_next->iop_order);
              rule_found = 1;
              break;
            }

            rules = g_list_next(rules);
          }
          if(rule_found) break;
        }

        if(mod == module_next) mod2 = mod;
        modules = g_list_previous(modules);
      }

      // we reach the module_next module
      if(mod1)
      {
        // this is already the previous module!
        if(module == mod2)
        {
          if(log_error)
            fprintf(stderr, "[dt_ioppr_get_iop_order_before_iop] %s %s(%f) is already previous to %s %s(%f)\n",
                module->op, module->multi_name, module->iop_order, module_next->op, module_next->multi_name, module_next->iop_order);
        }
        else if(mod1->iop_order == mod2->iop_order)
        {
          fprintf(stderr, "[dt_ioppr_get_iop_order_before_iop] %s %s(%f) and %s %s(%f) has the same iop_order\n",
              mod1->op, mod1->multi_name, mod1->iop_order, mod2->op, mod2->multi_name, mod2->iop_order);
        }
        else
        {
          // calculate new iop_order
          iop_order = mod1->iop_order + (mod2->iop_order - mod1->iop_order) / 2.0;
          /* fprintf(stderr, "[dt_ioppr_get_iop_order_before_iop] 2-calculated new iop_order=%f for %s(%f) between %s(%f) and %s(%f)\n",
                 iop_order, module->op, module->iop_order, module_next->op, module_next->iop_order, mod->op, mod->iop_order); */
        }
      }
    }
    else
      fprintf(stderr, "[dt_ioppr_get_iop_order_before_iop] can't find module %s %s\n", module->op, module->multi_name);
  }
  else
  {
    fprintf(stderr, "[dt_ioppr_get_iop_order_before_iop] modules %s %s(%f) and %s %s(%f) has the same iop_order\n",
        module->op, module->multi_name, module->iop_order, module_next->op, module_next->multi_name, module_next->iop_order);
  }

  return iop_order;
}

// if module can be placed after than module_prev on the pipe
// it returns the new iop_order
// if it cannot be placed it returns -1.0
// this assums that the order is always positive
double dt_ioppr_get_iop_order_after_iop(GList *iop_list, dt_iop_module_t *module, dt_iop_module_t *module_prev,
                                 const int validate_order, const int log_error)
{
  double iop_order = -1.0;

  // moving after module_prev is the same as moving before the very next one after module_prev
  GList *modules = g_list_last(iop_list);
  dt_iop_module_t *module_next = NULL;
  while(modules)
  {
    dt_iop_module_t *mod = (dt_iop_module_t *)modules->data;
    if(mod == module_prev) break;

    module_next = mod;
    modules = g_list_previous(modules);
  }
  if(module_next == NULL)
  {
    fprintf(
        stderr,
        "[dt_ioppr_get_iop_order_after_iop] can't find module previous to %s %s(%f) while moving %s %s(%f) after it\n",
        module_prev->op, module_prev->multi_name, module_prev->iop_order, module->op, module->multi_name,
        module->iop_order);
  }
  else
    iop_order = dt_ioppr_get_iop_order_before_iop(iop_list, module, module_next, validate_order, log_error);

  return iop_order;
}

// changes the module->iop_order so it comes before in the pipe than module_next
// sort dev->iop to reflect the changes
// return 1 if iop_order is changed, 0 otherwise
int dt_ioppr_move_iop_before(GList **_iop_list, dt_iop_module_t *module, dt_iop_module_t *module_next,
                       const int validate_order, const int log_error)
{
  GList *iop_list = *_iop_list;
  int moved = 0;

  // dt_ioppr_check_iop_order(dev, "dt_ioppr_move_iop_before begin");

  const double iop_order = dt_ioppr_get_iop_order_before_iop(iop_list, module, module_next, validate_order, log_error);

  if(iop_order >= 0.0)
  {
    module->iop_order = iop_order;
    iop_list = g_list_sort(iop_list, dt_sort_iop_by_order);
    moved = 1;
  }
  else if(log_error)
    fprintf(stderr, "[dt_ioppr_move_iop_before] module %s is already before %s\n", module->op, module_next->op);

  // dt_ioppr_check_iop_order(dev, "dt_ioppr_move_iop_before end");

  *_iop_list = iop_list;

  return moved;
}

// changes the module->iop_order so it comes after in the pipe than module_prev
// sort dev->iop to reflect the changes
// return 1 if iop_order is changed, 0 otherwise
int dt_ioppr_move_iop_after(GList **_iop_list, dt_iop_module_t *module, dt_iop_module_t *module_prev,
                      const int validate_order, const int log_error)
{
  GList *iop_list = *_iop_list;
  int moved = 0;

  // dt_ioppr_check_iop_order(dev, "dt_ioppr_move_iop_after begin");

  const double iop_order = dt_ioppr_get_iop_order_after_iop(iop_list, module, module_prev, validate_order, log_error);
  if(iop_order >= 0.0)
  {
    module->iop_order = iop_order;
    iop_list = g_list_sort(iop_list, dt_sort_iop_by_order);
    moved = 1;
  }
  else if(log_error)
    fprintf(stderr, "[dt_ioppr_move_iop_after] module %s is already after %s\n", module->op, module_prev->op);

  // dt_ioppr_check_iop_order(dev, "dt_ioppr_move_iop_after end");

  *_iop_list = iop_list;

  return moved;
}

//--------------------------------------------------------------------
// from here just for debug
//--------------------------------------------------------------------
int dt_ioppr_check_db_integrity()
{
  int ret = 0;
  sqlite3_stmt *stmt;

  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "SELECT imgid, operation, module FROM main.history WHERE iop_order <= 0 OR iop_order IS NULL",
                              -1, &stmt, NULL);
  if(sqlite3_step(stmt) == SQLITE_ROW)
  {
    ret = 1;
    fprintf(stderr, "\nThere are unassigned iop_order in the history!!!\n\n");

    int count = 0;
    do
    {
      const int imgid = sqlite3_column_int(stmt, 0);
      const char *opname = (const char *)sqlite3_column_text(stmt, 1);
      const int modversion = sqlite3_column_int(stmt, 2);

      fprintf(stderr, "image: %i module: %s version: %i\n", imgid, (opname) ? opname: "module is NULL", modversion);
    } while(sqlite3_step(stmt) == SQLITE_ROW && count++ < 20);
  }

  sqlite3_finalize(stmt);

  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "SELECT styleid, operation FROM data.style_items WHERE iop_order <= 0 OR iop_order IS NULL",
                              -1, &stmt, NULL);
  if(sqlite3_step(stmt) == SQLITE_ROW)
  {
    ret = 1;
    fprintf(stderr, "\nThere are unassigned iop_order in the styles!!!\n\n");

    int count = 0;
    do
    {
      const int styleid = sqlite3_column_int(stmt, 0);
      const char *opname = (const char *)sqlite3_column_text(stmt, 1);

      fprintf(stderr, "style: %i module: %s\n", styleid, (opname) ? opname: "module is NULL");
    } while(sqlite3_step(stmt) == SQLITE_ROW && count++ < 20);
  }

  sqlite3_finalize(stmt);

  return ret;
}

void dt_ioppr_print_module_iop_order(GList *iop_list, const char *msg)
{
  GList *modules = g_list_first(iop_list);
  while(modules)
  {
    dt_iop_module_t *mod = (dt_iop_module_t *)(modules->data);

    fprintf(stderr, "[%s] module %s %s multi_priority=%i, iop_order=%f\n", msg, mod->op, mod->multi_name, mod->multi_priority, mod->iop_order);

    modules = g_list_next(modules);
  }
}

void dt_ioppr_print_history_iop_order(GList *history_list, const char *msg)
{
  GList *history = g_list_first(history_list);
  while(history)
  {
    dt_dev_history_item_t *hist = (dt_dev_history_item_t *)(history->data);

    fprintf(stderr, "[%s] module %s %s multi_priority=%i, iop_order=%f\n", msg, hist->op_name, hist->multi_name, hist->multi_priority, hist->iop_order);

    history = g_list_next(history);
  }
}

void dt_ioppr_print_iop_order(GList *iop_order_list, const char *msg)
{
  GList *iops_order = g_list_first(iop_order_list);
  while(iops_order)
  {
    dt_iop_order_entry_t *order_entry = (dt_iop_order_entry_t *)(iops_order->data);

    fprintf(stderr, "[%s] operation %s iop_order=%f\n", msg, order_entry->operation, order_entry->iop_order);

    iops_order = g_list_next(iops_order);
  }
}

static GList *_get_fence_modules_list(GList *iop_list)
{
  GList *fences = NULL;
  GList *modules = g_list_first(iop_list);
  while(modules)
  {
    dt_iop_module_t *mod = (dt_iop_module_t *)modules->data;

    if(mod->flags() & IOP_FLAGS_FENCE)
    {
      fences = g_list_append(fences, mod);
    }

    modules = g_list_next(modules);
  }
  return fences;
}

static void _ioppr_check_rules(GList *iop_list, const int imgid, const char *msg)
{
  GList *modules = NULL;

  // check for IOP_FLAGS_FENCE on each module
  // create a list of fences modules
  GList *fences = _get_fence_modules_list(iop_list);

  // check if each module is between the fences
  modules = g_list_first(iop_list);
  while(modules)
  {
    dt_iop_module_t *mod = (dt_iop_module_t *)modules->data;
    if(mod->iop_order == DBL_MAX)
    {
      modules = g_list_next(modules);
      continue;
    }

    dt_iop_module_t *fence_prev = NULL;
    dt_iop_module_t *fence_next = NULL;

    GList *mod_fences = g_list_first(fences);
    while(mod_fences)
    {
      dt_iop_module_t *mod_fence = (dt_iop_module_t *)mod_fences->data;

      // mod should be before this fence
      if(mod->iop_order < mod_fence->iop_order)
      {
        if(fence_next == NULL)
          fence_next = mod_fence;
        else if(mod_fence->iop_order < fence_next->iop_order)
          fence_next = mod_fence;
      }
      // mod should be after this fence
      else if(mod->iop_order > mod_fence->iop_order)
      {
        if(fence_prev == NULL)
          fence_prev = mod_fence;
        else if(mod_fence->iop_order > fence_prev->iop_order)
          fence_prev = mod_fence;
      }

      mod_fences = g_list_next(mod_fences);
    }

    // now check if mod is between the fences
    if(fence_next && mod->iop_order > fence_next->iop_order)
    {
      fprintf(stderr, "[_ioppr_check_rules] found fence %s %s module %s %s(%f) is after %s %s(%f) image %i (%s)\n",
              fence_next->op, fence_next->multi_name, mod->op, mod->multi_name, mod->iop_order, fence_next->op,
              fence_next->multi_name, fence_next->iop_order, imgid, msg);
    }
    if(fence_prev && mod->iop_order < fence_prev->iop_order)
    {
      fprintf(stderr, "[_ioppr_check_rules] found fence %s %s module %s %s(%f) is before %s %s(%f) image %i (%s)\n",
              fence_prev->op, fence_prev->multi_name, mod->op, mod->multi_name, mod->iop_order, fence_prev->op,
              fence_prev->multi_name, fence_prev->iop_order, imgid, msg);
    }


    modules = g_list_next(modules);
  }

  // for each module check if it doesn't break a rule
  modules = g_list_first(iop_list);
  while(modules)
  {
    dt_iop_module_t *mod = (dt_iop_module_t *)modules->data;
    if(mod->iop_order == DBL_MAX)
    {
      modules = g_list_next(modules);
      continue;
    }

    // we have a module, now check each rule
    GList *rules = g_list_first(darktable.iop_order_rules);
    while(rules)
    {
      dt_iop_order_rule_t *rule = (dt_iop_order_rule_t *)rules->data;

      // mod must be before rule->op_next
      if(strcmp(mod->op, rule->op_prev) == 0)
      {
        // check if there's a rule->op_next module before mod
        GList *modules_prev = g_list_previous(modules);
        while(modules_prev)
        {
          dt_iop_module_t *mod_prev = (dt_iop_module_t *)modules_prev->data;

          if(strcmp(mod_prev->op, rule->op_next) == 0)
          {
            fprintf(stderr, "[_ioppr_check_rules] found rule %s %s module %s %s(%f) is after %s %s(%f) image %i (%s)\n",
                    rule->op_prev, rule->op_next, mod->op, mod->multi_name, mod->iop_order, mod_prev->op,
                    mod_prev->multi_name, mod_prev->iop_order, imgid, msg);
          }

          modules_prev = g_list_previous(modules_prev);
        }
      }
      // mod must be after rule->op_prev
      else if(strcmp(mod->op, rule->op_next) == 0)
      {
        // check if there's a rule->op_prev module after mod
        GList *modules_next = g_list_next(modules);
        while(modules_next)
        {
          dt_iop_module_t *mod_next = (dt_iop_module_t *)modules_next->data;

          if(strcmp(mod_next->op, rule->op_prev) == 0)
          {
            fprintf(stderr, "[_ioppr_check_rules] found rule %s %s module %s %s(%f) is before %s %s(%f) image %i (%s)\n",
                    rule->op_prev, rule->op_next, mod->op, mod->multi_name, mod->iop_order, mod_next->op,
                    mod_next->multi_name, mod_next->iop_order, imgid, msg);
          }

          modules_next = g_list_next(modules_next);
        }
      }

      rules = g_list_next(rules);
    }

    modules = g_list_next(modules);
  }

  if(fences) g_list_free(fences);
}

int dt_ioppr_check_iop_order(dt_develop_t *dev, const int imgid, const char *msg)
{
  int iop_order_ok = 1;

  // check if gamma is the last iop
  {
    GList *modules = g_list_last(dev->iop);
    while(modules)
    {
      dt_iop_module_t *mod = (dt_iop_module_t *)modules->data;
      if(mod->iop_order != DBL_MAX)
        break;

      modules = g_list_previous(dev->iop);
    }
    if(modules)
    {
      dt_iop_module_t *mod = (dt_iop_module_t *)modules->data;

      if(strcmp(mod->op, "gamma") != 0)
      {
        iop_order_ok = 0;
        fprintf(stderr, "[dt_ioppr_check_iop_order] gamma is not the last iop, last is %s %s(%f) image %i (%s)\n",
                mod->op, mod->multi_name, mod->iop_order,imgid, msg);
      }
    }
    else
    {
      // fprintf(stderr, "[dt_ioppr_check_iop_order] dev->iop is empty image %i (%s)\n",imgid, msg);
    }
  }

  // some other chacks
  {
    GList *modules = g_list_last(dev->iop);
    while(modules)
    {
      dt_iop_module_t *mod = (dt_iop_module_t *)modules->data;
      if(mod->iop_order == DBL_MAX)
      {
        if(mod->enabled)
        {
          iop_order_ok = 0;
          fprintf(stderr, "[dt_ioppr_check_iop_order] module not used but enabled!! %s %s(%f) image %i (%s)\n",
                  mod->op, mod->multi_name, mod->iop_order,imgid, msg);
        }
        if(mod->multi_priority == 0)
        {
          iop_order_ok = 0;
          fprintf(stderr, "[dt_ioppr_check_iop_order] base module set as not used %s %s(%f) image %i (%s)\n",
                  mod->op, mod->multi_name, mod->iop_order,imgid, msg);
        }
      }

      modules = g_list_previous(dev->iop);
    }
  }

  // check if there's duplicate or out-of-order iop_order
  {
    dt_iop_module_t *mod_prev = NULL;
    GList *modules = g_list_first(dev->iop);
    while(modules)
    {
      dt_iop_module_t *mod = (dt_iop_module_t *)modules->data;
      if(mod->iop_order != DBL_MAX)
      {
        if(mod_prev)
        {
          if(mod->iop_order < mod_prev->iop_order)
          {
            iop_order_ok = 0;
            fprintf(stderr,
                    "[dt_ioppr_check_iop_order] module %s %s(%f) should be after %s %s(%f) image %i (%s)\n",
                    mod->op, mod->multi_name, mod->iop_order, mod_prev->op, mod_prev->multi_name,
                    mod_prev->iop_order, imgid, msg);
          }
          else if(mod->iop_order == mod_prev->iop_order)
          {
            iop_order_ok = 0;
            fprintf(
                stderr,
                "[dt_ioppr_check_iop_order] module %s %s(%i)(%f) and %s %s(%i)(%f) has the same order image %i (%s)\n",
                mod->op, mod->multi_name, mod->multi_priority, mod->iop_order, mod_prev->op,
                mod_prev->multi_name, mod_prev->multi_priority, mod_prev->iop_order, imgid, msg);
          }
        }
      }
      mod_prev = mod;
      modules = g_list_next(modules);
    }
  }

  _ioppr_check_rules(dev->iop, imgid, msg);

  GList *history = g_list_first(dev->history);
  while(history)
  {
    dt_dev_history_item_t *hist = (dt_dev_history_item_t *)(history->data);

    if(hist->iop_order == DBL_MAX)
    {
      if(hist->enabled)
      {
        iop_order_ok = 0;
        fprintf(stderr, "[dt_ioppr_check_iop_order] history module not used but enabled!! %s %s(%f) image %i (%s)\n",
            hist->op_name, hist->multi_name, hist->iop_order,imgid, msg);
      }
      if(hist->multi_priority == 0)
      {
        iop_order_ok = 0;
        fprintf(stderr, "[dt_ioppr_check_iop_order] history base module set as not used %s %s(%f) image %i (%s)\n",
            hist->op_name, hist->multi_name, hist->iop_order,imgid, msg);
      }
    }

    history = g_list_next(history);
  }

  return iop_order_ok;
}

//---------------------------------------------------------
// colorspace transforms
//---------------------------------------------------------

static void _transform_from_to_rgb_lab_lcms2(const float *const image_in, float *const image_out, const int width,
                                             const int height, const dt_colorspaces_color_profile_type_t type,
                                             const char *filename, const int intent, const int direction)
{
  const int ch = 4;
  cmsHTRANSFORM *xform = NULL;
  cmsHPROFILE *rgb_profile = NULL;
  cmsHPROFILE *lab_profile = NULL;

  if(type != DT_COLORSPACE_NONE)
  {
    const dt_colorspaces_color_profile_t *profile = dt_colorspaces_get_profile(type, filename, DT_PROFILE_DIRECTION_WORK);
    if(profile) rgb_profile = profile->profile;
  }
  else
    rgb_profile = dt_colorspaces_get_profile(DT_COLORSPACE_LIN_REC2020, "", DT_PROFILE_DIRECTION_WORK)->profile;
  if(rgb_profile)
  {
    cmsColorSpaceSignature rgb_color_space = cmsGetColorSpace(rgb_profile);
    if(rgb_color_space != cmsSigRgbData)
    {
        fprintf(stderr, "working profile color space `%c%c%c%c' not supported\n",
                (char)(rgb_color_space>>24),
                (char)(rgb_color_space>>16),
                (char)(rgb_color_space>>8),
                (char)(rgb_color_space));
        rgb_profile = NULL;
    }
  }
  if(rgb_profile == NULL)
  {
    rgb_profile = dt_colorspaces_get_profile(DT_COLORSPACE_LIN_REC2020, "", DT_PROFILE_DIRECTION_WORK)->profile;
    fprintf(stderr, _("unsupported working profile %s has been replaced by Rec2020 RGB!\n"), filename);
  }

  lab_profile = dt_colorspaces_get_profile(DT_COLORSPACE_LAB, "", DT_PROFILE_DIRECTION_ANY)->profile;

  cmsHPROFILE *input_profile = NULL;
  cmsHPROFILE *output_profile = NULL;
  cmsUInt32Number input_format = TYPE_RGBA_FLT;
  cmsUInt32Number output_format = TYPE_LabA_FLT;

  if(direction == 1) // rgb --> lab
  {
    input_profile = rgb_profile;
    input_format = TYPE_RGBA_FLT;
    output_profile = lab_profile;
    output_format = TYPE_LabA_FLT;
  }
  else // lab -->rgb
  {
    input_profile = lab_profile;
    input_format = TYPE_LabA_FLT;
    output_profile = rgb_profile;
    output_format = TYPE_RGBA_FLT;
  }

  xform = cmsCreateTransform(input_profile, input_format, output_profile, output_format, intent, 0);
  if(xform)
  {
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    dt_omp_firstprivate(image_in, image_out, width, height, ch) \
    shared(xform) \
    schedule(static)
#endif
    for(int y = 0; y < height; y++)
    {
      const float *const in = image_in + y * width * ch;
      float *const out = image_out + y * width * ch;

      cmsDoTransform(xform, in, out, width);
    }
  }
  else
    fprintf(stderr, "[_transform_from_to_rgb_lab_lcms2] cannot create transform\n");

  if(xform) cmsDeleteTransform(xform);
}

static void _transform_rgb_to_rgb_lcms2(const float *const image_in, float *const image_out, const int width,
                                        const int height, const dt_colorspaces_color_profile_type_t type_from,
                                        const char *filename_from,
                                        const dt_colorspaces_color_profile_type_t type_to, const char *filename_to,
                                        const int intent)
{
  const int ch = 4;
  cmsHTRANSFORM *xform = NULL;
  cmsHPROFILE *from_rgb_profile = NULL;
  cmsHPROFILE *to_rgb_profile = NULL;

  if(type_from == DT_COLORSPACE_DISPLAY || type_to == DT_COLORSPACE_DISPLAY || type_from == DT_COLORSPACE_DISPLAY2
     || type_to == DT_COLORSPACE_DISPLAY2)
    pthread_rwlock_rdlock(&darktable.color_profiles->xprofile_lock);

  if(type_from != DT_COLORSPACE_NONE)
  {
    const dt_colorspaces_color_profile_t *profile_from
        = dt_colorspaces_get_profile(type_from, filename_from, DT_PROFILE_DIRECTION_ANY);
    if(profile_from) from_rgb_profile = profile_from->profile;
  }
  else
  {
    fprintf(stderr, "[_transform_rgb_to_rgb_lcms2] invalid from profile\n");
  }

  if(type_to != DT_COLORSPACE_NONE)
  {
    const dt_colorspaces_color_profile_t *profile_to
        = dt_colorspaces_get_profile(type_to, filename_to, DT_PROFILE_DIRECTION_ANY);
    if(profile_to) to_rgb_profile = profile_to->profile;
  }
  else
  {
    fprintf(stderr, "[_transform_rgb_to_rgb_lcms2] invalid to profile\n");
  }

  if(from_rgb_profile)
  {
    cmsColorSpaceSignature rgb_color_space = cmsGetColorSpace(from_rgb_profile);
    if(rgb_color_space != cmsSigRgbData)
    {
      fprintf(stderr, "[_transform_rgb_to_rgb_lcms2] profile color space `%c%c%c%c' not supported\n",
              (char)(rgb_color_space >> 24), (char)(rgb_color_space >> 16), (char)(rgb_color_space >> 8),
              (char)(rgb_color_space));
      from_rgb_profile = NULL;
    }
  }
  if(to_rgb_profile)
  {
    cmsColorSpaceSignature rgb_color_space = cmsGetColorSpace(to_rgb_profile);
    if(rgb_color_space != cmsSigRgbData)
    {
      fprintf(stderr, "[_transform_rgb_to_rgb_lcms2] profile color space `%c%c%c%c' not supported\n",
              (char)(rgb_color_space >> 24), (char)(rgb_color_space >> 16), (char)(rgb_color_space >> 8),
              (char)(rgb_color_space));
      to_rgb_profile = NULL;
    }
  }

  cmsHPROFILE *input_profile = NULL;
  cmsHPROFILE *output_profile = NULL;
  cmsUInt32Number input_format = TYPE_RGBA_FLT;
  cmsUInt32Number output_format = TYPE_RGBA_FLT;

  input_profile = from_rgb_profile;
  input_format = TYPE_RGBA_FLT;
  output_profile = to_rgb_profile;
  output_format = TYPE_RGBA_FLT;

  if(input_profile && output_profile)
    xform = cmsCreateTransform(input_profile, input_format, output_profile, output_format, intent, 0);

  if(type_from == DT_COLORSPACE_DISPLAY || type_to == DT_COLORSPACE_DISPLAY || type_from == DT_COLORSPACE_DISPLAY2
     || type_to == DT_COLORSPACE_DISPLAY2)
    pthread_rwlock_unlock(&darktable.color_profiles->xprofile_lock);

  if(xform)
  {
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    dt_omp_firstprivate(image_in, image_out, width, height, ch) \
    shared(xform) \
    schedule(static)
#endif
    for(int y = 0; y < height; y++)
    {
      const float *const in = image_in + y * width * ch;
      float *const out = image_out + y * width * ch;

      cmsDoTransform(xform, in, out, width);
    }
  }
  else
    fprintf(stderr, "[_transform_rgb_to_rgb_lcms2] cannot create transform\n");

  if(xform) cmsDeleteTransform(xform);
}

static void _transform_lcms2(struct dt_iop_module_t *self, const float *const image_in, float *const image_out,
                             const int width, const int height, const int cst_from, const int cst_to,
                             int *converted_cst, const dt_iop_order_iccprofile_info_t *const profile_info)
{
  if(cst_from == cst_to)
  {
    *converted_cst = cst_to;
    return;
  }

  *converted_cst = cst_to;

  if(cst_from == iop_cs_rgb && cst_to == iop_cs_Lab)
  {
    printf("[_transform_lcms2] transfoming from RGB to Lab (%s %s)\n", self->op, self->multi_name);
    _transform_from_to_rgb_lab_lcms2(image_in, image_out, width, height, profile_info->type,
                                     profile_info->filename, profile_info->intent, 1);
  }
  else if(cst_from == iop_cs_Lab && cst_to == iop_cs_rgb)
  {
    printf("[_transform_lcms2] transfoming from Lab to RGB (%s %s)\n", self->op, self->multi_name);
    _transform_from_to_rgb_lab_lcms2(image_in, image_out, width, height, profile_info->type,
                                     profile_info->filename, profile_info->intent, -1);
  }
  else
  {
    *converted_cst = cst_from;
    fprintf(stderr, "[_transform_lcms2] invalid conversion from %i to %i\n", cst_from, cst_to);
  }
}

static inline void _transform_lcms2_rgb(const float *const image_in, float *const image_out, const int width,
                                        const int height,
                                        const dt_iop_order_iccprofile_info_t *const profile_info_from,
                                        const dt_iop_order_iccprofile_info_t *const profile_info_to)
{
  _transform_rgb_to_rgb_lcms2(image_in, image_out, width, height, profile_info_from->type,
                              profile_info_from->filename, profile_info_to->type, profile_info_to->filename,
                              profile_info_to->intent);
}

static float lerp_lut(const float *const lut, const float v, const int lutsize)
{
  // TODO: check if optimization is worthwhile!
  const float ft = CLAMPS(v * (lutsize - 1), 0, lutsize - 1);
  const int t = ft < lutsize - 2 ? ft : lutsize - 2;
  const float f = ft - t;
  const float l1 = lut[t];
  const float l2 = lut[t + 1];
  return l1 * (1.0f - f) + l2 * f;
}

static inline void _apply_trc_in(const float *const rgb_in, float *rgb_out, const dt_iop_order_iccprofile_info_t *const profile_info)
{
  for(int c = 0; c < 3; c++)
  {
    rgb_out[c] = (profile_info->lut_in[c][0] >= 0.0f) ? ((rgb_in[c] < 1.0f) ? lerp_lut(profile_info->lut_in[c], rgb_in[c], profile_info->lutsize)
                                                        : dt_iop_eval_exp(profile_info->unbounded_coeffs_in[c], rgb_in[c]))
                                                        : rgb_in[c];
  }
}

static inline void _apply_trc_out(const float *const rgb_in, float *rgb_out, const dt_iop_order_iccprofile_info_t *const profile_info)
{
  for(int c = 0; c < 3; c++)
  {
    rgb_out[c] = (profile_info->lut_out[c][0] >= 0.0f) ? ((rgb_in[c] < 1.0f) ? lerp_lut(profile_info->lut_out[c], rgb_in[c], profile_info->lutsize)
                                                        : dt_iop_eval_exp(profile_info->unbounded_coeffs_out[c], rgb_in[c]))
                                                        : rgb_in[c];
  }
}

static void _ioppr_linear_rgb_matrix_to_xyz(const float *const rgb, float *xyz, const dt_iop_order_iccprofile_info_t *const profile_info)
{
  for(int c = 0; c < 3; c++)
  {
    xyz[c] = 0.0f;
    for(int i = 0; i < 3; i++)
    {
      xyz[c] += profile_info->matrix_in[3 * c + i] * rgb[i];
    }
  }
}

static void _ioppr_xyz_to_linear_rgb_matrix(const float *const xyz, float *rgb, const dt_iop_order_iccprofile_info_t *const profile_info)
{
  for(int c = 0; c < 3; c++)
  {
    rgb[c] = 0.0f;
    for(int i = 0; i < 3; i++)
    {
      rgb[c] += profile_info->matrix_out[3 * c + i] * xyz[i];
    }
  }
}

static void _apply_tonecurves(const float *const image_in, float *const image_out, const int width,
                              const int height, const float *const lutr, const float *const lutg,
                              const float *const lutb, const float *const unbounded_coeffsr,
                              const float *const unbounded_coeffsg, const float *const unbounded_coeffsb,
                              const int lutsize)
{
  const int ch = 4;
  const float *const lut[3] = { lutr, lutg, lutb };
  const float *const unbounded_coeffs[3] = { unbounded_coeffsr, unbounded_coeffsg, unbounded_coeffsb };
  const size_t stride = (size_t)ch * width * height;

  // do we have any lut to apply, or is this a linear profile?
  if((lut[0][0] >= 0.0f) && (lut[1][0] >= 0.0f) && (lut[2][0] >= 0.0f))
  {
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    dt_omp_firstprivate(stride, image_in, image_out, lut, lutsize, unbounded_coeffs, ch) \
    schedule(static)
#endif
    for(size_t k = 0; k < stride; k += ch)
    {
      for(int c = 0; c < 3; c++)
      {
        image_out[k + c] = (image_in[k + c] < 1.0f) ? lerp_lut(lut[c], image_in[k + c], lutsize)
                                                    : dt_iop_eval_exp(unbounded_coeffs[c], image_in[k + c]);
      }
    }
  }
  else if((lut[0][0] >= 0.0f) || (lut[1][0] >= 0.0f) || (lut[2][0] >= 0.0f))
  {
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    dt_omp_firstprivate(stride, image_in, image_out, lut, lutsize, unbounded_coeffs, ch) \
    schedule(static)
#endif
    for(size_t k = 0; k < stride; k += ch)
    {
      for(int c = 0; c < 3; c++)
      {
        if(lut[c][0] >= 0.0f)
        {
          image_out[k + c] = (image_in[k + c] < 1.0f) ? lerp_lut(lut[c], image_in[k + c], lutsize)
                                                      : dt_iop_eval_exp(unbounded_coeffs[c], image_in[k + c]);
        }
      }
    }
  }
}

static void _transform_rgb_to_lab_matrix(const float *const image_in, float *const image_out, const int width,
                                         const int height,
                                         const dt_iop_order_iccprofile_info_t *const profile_info)
{
  const int ch = 4;
  const size_t stride = (size_t)(width * height);

  if(profile_info->nonlinearlut)
  {
    _apply_tonecurves(image_in, image_out, width, height, profile_info->lut_in[0], profile_info->lut_in[1],
                      profile_info->lut_in[2], profile_info->unbounded_coeffs_in[0],
                      profile_info->unbounded_coeffs_in[1], profile_info->unbounded_coeffs_in[2],
                      profile_info->lutsize);

#ifdef _OPENMP
#pragma omp parallel for default(none) \
    dt_omp_firstprivate(image_out, profile_info, stride, ch) \
    schedule(static)
#endif
    for(size_t y = 0; y < stride; y++)
    {
      float *const in = image_out + y * ch;

      float xyz[3] = { 0.0f, 0.0f, 0.0f };

      _ioppr_linear_rgb_matrix_to_xyz(in, xyz, profile_info);
      dt_XYZ_to_Lab(xyz, in);
    }
  }
  else
  {
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    dt_omp_firstprivate(image_in, image_out, profile_info, stride, ch) \
    schedule(static)
#endif
    for(size_t y = 0; y < stride; y++)
    {
      const float *const in = image_in + y * ch;
      float *const out = image_out + y * ch;

      float xyz[3] = { 0.0f, 0.0f, 0.0f };

      _ioppr_linear_rgb_matrix_to_xyz(in, xyz, profile_info);
      dt_XYZ_to_Lab(xyz, out);
    }
  }
}

static void _transform_lab_to_rgb_matrix(const float *const image_in, float *const image_out, const int width,
                                         const int height,
                                         const dt_iop_order_iccprofile_info_t *const profile_info)
{
  const int ch = 4;
  const size_t stride = (size_t)width * height;

#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(image_in, image_out, stride, profile_info, ch) \
  schedule(static)
#endif
  for(size_t y = 0; y < stride; y++)
  {
    const float *const in = image_in + y * ch;
    float *const out = image_out + y * ch;

    float xyz[3] = { 0.0f, 0.0f, 0.0f };

    dt_Lab_to_XYZ(in, xyz);
    _ioppr_xyz_to_linear_rgb_matrix(xyz, out, profile_info);
  }

  _apply_tonecurves(image_out, image_out, width, height, profile_info->lut_out[0], profile_info->lut_out[1],
                    profile_info->lut_out[2], profile_info->unbounded_coeffs_out[0],
                    profile_info->unbounded_coeffs_out[1], profile_info->unbounded_coeffs_out[2],
                    profile_info->lutsize);
}

static void _transform_matrix_rgb(const float *const image_in, float *const image_out, const int width,
                                  const int height, const dt_iop_order_iccprofile_info_t *const profile_info_from,
                                  const dt_iop_order_iccprofile_info_t *const profile_info_to)
{
  const int ch = 4;
  const size_t stride = (size_t)(width * height);

  if(profile_info_from->nonlinearlut)
  {
    _apply_tonecurves(image_in, image_out, width, height, profile_info_from->lut_in[0],
                      profile_info_from->lut_in[1], profile_info_from->lut_in[2],
                      profile_info_from->unbounded_coeffs_in[0], profile_info_from->unbounded_coeffs_in[1],
                      profile_info_from->unbounded_coeffs_in[2], profile_info_from->lutsize);

#ifdef _OPENMP
#pragma omp parallel for default(none) \
    dt_omp_firstprivate(stride, image_out, profile_info_from, profile_info_to, ch) \
    schedule(static)
#endif
    for(size_t y = 0; y < stride; y++)
    {
      float *const in = image_out + y * ch;

      float xyz[3] = { 0.0f, 0.0f, 0.0f };

      _ioppr_linear_rgb_matrix_to_xyz(in, xyz, profile_info_from);
      _ioppr_xyz_to_linear_rgb_matrix(xyz, in, profile_info_to);
    }
  }
  else
  {
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    dt_omp_firstprivate(stride, image_in, image_out, profile_info_from, profile_info_to, ch) \
    schedule(static)
#endif
    for(size_t y = 0; y < stride; y++)
    {
      const float *const in = image_in + y * ch;
      float *const out = image_out + y * ch;

      float xyz[3] = { 0.0f, 0.0f, 0.0f };

      _ioppr_linear_rgb_matrix_to_xyz(in, xyz, profile_info_from);
      _ioppr_xyz_to_linear_rgb_matrix(xyz, out, profile_info_to);
    }
  }

  _apply_tonecurves(image_out, image_out, width, height, profile_info_to->lut_out[0], profile_info_to->lut_out[1],
                    profile_info_to->lut_out[2], profile_info_to->unbounded_coeffs_out[0],
                    profile_info_to->unbounded_coeffs_out[1], profile_info_to->unbounded_coeffs_out[2],
                    profile_info_to->lutsize);
}

static int _init_unbounded_coeffs(float *lutr, float *lutg, float *lutb,
    float *unbounded_coeffsr, float *unbounded_coeffsg, float *unbounded_coeffsb, const int lutsize)
{
  int nonlinearlut = 0;
  float *lut[3] = { lutr, lutg, lutb };
  float *unbounded_coeffs[3] = { unbounded_coeffsr, unbounded_coeffsg, unbounded_coeffsb };

  for(int k = 0; k < 3; k++)
  {
    // omit luts marked as linear (negative as marker)
    if(lut[k][0] >= 0.0f)
    {
      const float x[4] = { 0.7f, 0.8f, 0.9f, 1.0f };
      const float y[4] = { lerp_lut(lut[k], x[0], lutsize), lerp_lut(lut[k], x[1], lutsize), lerp_lut(lut[k], x[2], lutsize),
                           lerp_lut(lut[k], x[3], lutsize) };
      dt_iop_estimate_exp(x, y, 4, unbounded_coeffs[k]);

      nonlinearlut++;
    }
    else
      unbounded_coeffs[k][0] = -1.0f;
  }

  return nonlinearlut;
}

static void _transform_matrix(struct dt_iop_module_t *self, const float *const image_in, float *const image_out,
                              const int width, const int height, const int cst_from, const int cst_to,
                              int *converted_cst, const dt_iop_order_iccprofile_info_t *const profile_info)
{
  if(cst_from == cst_to)
  {
    *converted_cst = cst_to;
    return;
  }

  *converted_cst = cst_to;

  if(cst_from == iop_cs_rgb && cst_to == iop_cs_Lab)
  {
    _transform_rgb_to_lab_matrix(image_in, image_out, width, height, profile_info);
  }
  else if(cst_from == iop_cs_Lab && cst_to == iop_cs_rgb)
  {
    _transform_lab_to_rgb_matrix(image_in, image_out, width, height, profile_info);
  }
  else
  {
    *converted_cst = cst_from;
    fprintf(stderr, "[_transform_matrix] invalid conversion from %i to %i\n", cst_from, cst_to);
  }
}

#define DT_IOPPR_LUT_SAMPLES 0x10000

void dt_ioppr_init_profile_info(dt_iop_order_iccprofile_info_t *profile_info, const int lutsize)
{
  profile_info->type = DT_COLORSPACE_NONE;
  profile_info->filename[0] = '\0';
  profile_info->intent = DT_INTENT_PERCEPTUAL;
  profile_info->matrix_in[0] = NAN;
  profile_info->matrix_out[0] = NAN;
  profile_info->unbounded_coeffs_in[0][0] = profile_info->unbounded_coeffs_in[1][0] = profile_info->unbounded_coeffs_in[2][0] = -1.0f;
  profile_info->unbounded_coeffs_out[0][0] = profile_info->unbounded_coeffs_out[1][0] = profile_info->unbounded_coeffs_out[2][0] = -1.0f;
  profile_info->nonlinearlut = 0;
  profile_info->grey = 0.f;
  profile_info->lutsize = (lutsize > 0) ? lutsize: DT_IOPPR_LUT_SAMPLES;
  for(int i = 0; i < 3; i++)
  {
    profile_info->lut_in[i] = malloc(profile_info->lutsize * sizeof(float));
    profile_info->lut_in[i][0] = -1.0f;
    profile_info->lut_out[i] = malloc(profile_info->lutsize * sizeof(float));
    profile_info->lut_out[i][0] = -1.0f;
  }
}

#undef DT_IOPPR_LUT_SAMPLES

void dt_ioppr_cleanup_profile_info(dt_iop_order_iccprofile_info_t *profile_info)
{
  for(int i = 0; i < 3; i++)
  {
    if(profile_info->lut_in[i]) free(profile_info->lut_in[i]);
    if(profile_info->lut_out[i]) free(profile_info->lut_out[i]);
  }
}

/** generate the info for the profile (type, filename) if matrix can be retrieved from lcms2
 * it can be called multiple time between init and cleanup
 * return 0 if OK, non zero otherwise
 */
static int dt_ioppr_generate_profile_info(dt_iop_order_iccprofile_info_t *profile_info, const int type, const char *filename, const int intent)
{
  int err_code = 0;
  cmsHPROFILE *rgb_profile = NULL;

  profile_info->matrix_in[0] = NAN;
  profile_info->matrix_out[0] = NAN;
  for(int i = 0; i < 3; i++)
  {
    profile_info->lut_in[i][0] = -1.0f;
    profile_info->lut_out[i][0] = -1.0f;
  }

  profile_info->nonlinearlut = 0;
  profile_info->grey = 0.1842f;

  profile_info->type = type;
  g_strlcpy(profile_info->filename, filename, sizeof(profile_info->filename));
  profile_info->intent = intent;

  if(type == DT_COLORSPACE_DISPLAY || type == DT_COLORSPACE_DISPLAY2)
    pthread_rwlock_rdlock(&darktable.color_profiles->xprofile_lock);

  const dt_colorspaces_color_profile_t *profile
      = dt_colorspaces_get_profile(type, filename, DT_PROFILE_DIRECTION_ANY);
  if(profile) rgb_profile = profile->profile;

  if(type == DT_COLORSPACE_DISPLAY || type == DT_COLORSPACE_DISPLAY2)
    pthread_rwlock_unlock(&darktable.color_profiles->xprofile_lock);

  // we only allow rgb profiles
  if(rgb_profile)
  {
    cmsColorSpaceSignature rgb_color_space = cmsGetColorSpace(rgb_profile);
    if(rgb_color_space != cmsSigRgbData)
    {
      fprintf(stderr, "working profile color space `%c%c%c%c' not supported\n",
              (char)(rgb_color_space>>24),
              (char)(rgb_color_space>>16),
              (char)(rgb_color_space>>8),
              (char)(rgb_color_space));
      rgb_profile = NULL;
    }
  }

  // get the matrix
  if(rgb_profile)
  {
    if(dt_colorspaces_get_matrix_from_input_profile(rgb_profile, profile_info->matrix_in,
        profile_info->lut_in[0], profile_info->lut_in[1], profile_info->lut_in[2],
        profile_info->lutsize, profile_info->intent) ||
        dt_colorspaces_get_matrix_from_output_profile(rgb_profile, profile_info->matrix_out,
            profile_info->lut_out[0], profile_info->lut_out[1], profile_info->lut_out[2],
            profile_info->lutsize, profile_info->intent))
    {
      profile_info->matrix_in[0] = NAN;
      profile_info->matrix_out[0] = NAN;
      for(int i = 0; i < 3; i++)
      {
        profile_info->lut_in[i][0] = -1.0f;
        profile_info->lut_out[i][0] = -1.0f;
      }
    }
    else if(isnan(profile_info->matrix_in[0]) || isnan(profile_info->matrix_out[0]))
    {
      profile_info->matrix_in[0] = NAN;
      profile_info->matrix_out[0] = NAN;
      for(int i = 0; i < 3; i++)
      {
        profile_info->lut_in[i][0] = -1.0f;
        profile_info->lut_out[i][0] = -1.0f;
      }
    }
  }

  // now try to initialize unbounded mode:
  // we do extrapolation for input values above 1.0f.
  // unfortunately we can only do this if we got the computation
  // in our hands, i.e. for the fast builtin-dt-matrix-profile path.
  if(!isnan(profile_info->matrix_in[0]) && !isnan(profile_info->matrix_out[0]))
  {
    profile_info->nonlinearlut = _init_unbounded_coeffs(profile_info->lut_in[0], profile_info->lut_in[1], profile_info->lut_in[2],
        profile_info->unbounded_coeffs_in[0], profile_info->unbounded_coeffs_in[1], profile_info->unbounded_coeffs_in[2], profile_info->lutsize);
    _init_unbounded_coeffs(profile_info->lut_out[0], profile_info->lut_out[1], profile_info->lut_out[2],
        profile_info->unbounded_coeffs_out[0], profile_info->unbounded_coeffs_out[1], profile_info->unbounded_coeffs_out[2], profile_info->lutsize);
  }

  if(!isnan(profile_info->matrix_in[0]) && !isnan(profile_info->matrix_out[0]) && profile_info->nonlinearlut)
  {
    float rgb[3] = { 0.1842f, 0.1842f, 0.1842f };
    profile_info->grey = dt_ioppr_get_rgb_matrix_luminance(rgb, profile_info);
  }

  return err_code;
}

dt_iop_order_iccprofile_info_t *dt_ioppr_get_profile_info_from_list(struct dt_develop_t *dev, const int profile_type, const char *profile_filename)
{
  dt_iop_order_iccprofile_info_t *profile_info = NULL;

  GList *profiles = g_list_first(dev->allprofile_info);
  while(profiles)
  {
    dt_iop_order_iccprofile_info_t *prof = (dt_iop_order_iccprofile_info_t *)(profiles->data);
    if(prof->type == profile_type && strcmp(prof->filename, profile_filename) == 0)
    {
      profile_info = prof;
      break;
    }
    profiles = g_list_next(profiles);
  }

  return profile_info;
}

dt_iop_order_iccprofile_info_t *dt_ioppr_add_profile_info_to_list(struct dt_develop_t *dev, const int profile_type, const char *profile_filename, const int intent)
{
  dt_iop_order_iccprofile_info_t *profile_info = dt_ioppr_get_profile_info_from_list(dev, profile_type, profile_filename);
  if(profile_info == NULL)
  {
    profile_info = malloc(sizeof(dt_iop_order_iccprofile_info_t));
    dt_ioppr_init_profile_info(profile_info, 0);
    const int err = dt_ioppr_generate_profile_info(profile_info, profile_type, profile_filename, intent);
    if(err == 0)
    {
      dev->allprofile_info = g_list_append(dev->allprofile_info, profile_info);
    }
    else
    {
      free(profile_info);
      profile_info = NULL;
    }
  }
  return profile_info;
}

dt_iop_order_iccprofile_info_t *dt_ioppr_get_iop_work_profile_info(struct dt_iop_module_t *module, GList *iop_list)
{
  dt_iop_order_iccprofile_info_t *profile = NULL;

  // first check if the module is between colorin and colorout
  gboolean in_between = FALSE;

  GList *modules = g_list_first(iop_list);
  while(modules)
  {
    dt_iop_module_t *mod = (dt_iop_module_t *)(modules->data);

    // we reach the module, that's it
    if(strcmp(mod->op, module->op) == 0) break;

    // if we reach colorout means that the module is after it
    if(strcmp(mod->op, "colorout") == 0)
    {
      in_between = FALSE;
      break;
    }

    // we reach colorin, so far we're good
    if(strcmp(mod->op, "colorin") == 0)
    {
      in_between = TRUE;
      break;
    }

    modules = g_list_next(modules);
  }

  if(in_between)
  {
    dt_colorspaces_color_profile_type_t type = DT_COLORSPACE_NONE;
    char *filename = NULL;
    dt_develop_t *dev = module->dev;

    dt_ioppr_get_work_profile_type(dev, &type, &filename);
    if(filename) profile = dt_ioppr_add_profile_info_to_list(dev, type, filename, DT_INTENT_PERCEPTUAL);
  }

  return profile;
}

dt_iop_order_iccprofile_info_t *dt_ioppr_set_pipe_work_profile_info(struct dt_develop_t *dev, struct dt_dev_pixelpipe_t *pipe,
    const int type, const char *filename, const int intent)
{
  dt_iop_order_iccprofile_info_t *profile_info = dt_ioppr_add_profile_info_to_list(dev, type, filename, intent);

  if(profile_info == NULL || isnan(profile_info->matrix_in[0]) || isnan(profile_info->matrix_out[0]))
  {
    fprintf(stderr, "[dt_ioppr_set_pipe_work_profile_info] unsupported working profile %i %s, it will be replaced with linear rec2020\n", type, filename);
    profile_info = dt_ioppr_add_profile_info_to_list(dev, DT_COLORSPACE_LIN_REC2020, "", intent);
  }
  pipe->dsc.work_profile_info = profile_info;

  return profile_info;
}

dt_iop_order_iccprofile_info_t *dt_ioppr_get_histogram_profile_info(struct dt_develop_t *dev)
{
  dt_colorspaces_color_profile_type_t histogram_profile_type;
  char *histogram_profile_filename;
  dt_ioppr_get_histogram_profile_type(&histogram_profile_type, &histogram_profile_filename);
  return dt_ioppr_add_profile_info_to_list(dev, histogram_profile_type, histogram_profile_filename,
                                           DT_INTENT_PERCEPTUAL);
}

dt_iop_order_iccprofile_info_t *dt_ioppr_get_pipe_work_profile_info(struct dt_dev_pixelpipe_t *pipe)
{
  return pipe->dsc.work_profile_info;
}

// returns a pointer to the filename of the work profile instead of the actual string data
// pointer must not be stored
void dt_ioppr_get_work_profile_type(struct dt_develop_t *dev, int *profile_type, char **profile_filename)
{
  *profile_type = DT_COLORSPACE_NONE;
  *profile_filename = NULL;

  // use introspection to get the params values
  dt_iop_module_so_t *colorin_so = NULL;
  dt_iop_module_t *colorin = NULL;
  GList *modules = g_list_first(darktable.iop);
  while(modules)
  {
    dt_iop_module_so_t *module_so = (dt_iop_module_so_t *)(modules->data);
    if(!strcmp(module_so->op, "colorin"))
    {
      colorin_so = module_so;
      break;
    }
    modules = g_list_next(modules);
  }
  if(colorin_so && colorin_so->get_p)
  {
    modules = g_list_first(dev->iop);
    while(modules)
    {
      dt_iop_module_t *module = (dt_iop_module_t *)(modules->data);
      if(!strcmp(module->op, "colorin"))
      {
        colorin = module;
        break;
      }
      modules = g_list_next(modules);
    }
  }
  if(colorin)
  {
    dt_colorspaces_color_profile_type_t *_type = colorin_so->get_p(colorin->params, "type_work");
    char *_filename = colorin_so->get_p(colorin->params, "filename_work");
    if(_type && _filename)
    {
      *profile_type = *_type;
      *profile_filename = _filename;
    }
    else
      fprintf(stderr, "[dt_ioppr_get_work_profile_type] can't get colorin parameters\n");
  }
  else
    fprintf(stderr, "[dt_ioppr_get_work_profile_type] can't find colorin iop\n");
}

void dt_ioppr_get_export_profile_type(struct dt_develop_t *dev, int *profile_type, char **profile_filename)
{
  *profile_type = DT_COLORSPACE_NONE;
  *profile_filename = NULL;

  // use introspection to get the params values
  dt_iop_module_so_t *colorout_so = NULL;
  dt_iop_module_t *colorout = NULL;
  GList *modules = g_list_last(darktable.iop);
  while(modules)
  {
    dt_iop_module_so_t *module_so = (dt_iop_module_so_t *)(modules->data);
    if(!strcmp(module_so->op, "colorout"))
    {
      colorout_so = module_so;
      break;
    }
    modules = g_list_previous(modules);
  }
  if(colorout_so && colorout_so->get_p)
  {
    modules = g_list_last(dev->iop);
    while(modules)
    {
      dt_iop_module_t *module = (dt_iop_module_t *)(modules->data);
      if(!strcmp(module->op, "colorout"))
      {
        colorout = module;
        break;
      }
      modules = g_list_previous(modules);
    }
  }
  if(colorout)
  {
    dt_colorspaces_color_profile_type_t *_type = colorout_so->get_p(colorout->params, "type");
    char *_filename = colorout_so->get_p(colorout->params, "filename");
    if(_type && _filename)
    {
      *profile_type = *_type;
      *profile_filename = _filename;
    }
    else
      fprintf(stderr, "[dt_ioppr_get_export_profile_type] can't get colorout parameters\n");
  }
  else
    fprintf(stderr, "[dt_ioppr_get_export_profile_type] can't find colorout iop\n");
}

void dt_ioppr_get_histogram_profile_type(int *profile_type, char **profile_filename)
{
  const dt_colorspaces_color_mode_t mode = darktable.color_profiles->mode;

  // if in gamut check use soft proof
  if(mode != DT_PROFILE_NORMAL || darktable.color_profiles->histogram_type == DT_COLORSPACE_SOFTPROOF)
  {
    *profile_type = darktable.color_profiles->softproof_type;
    *profile_filename = darktable.color_profiles->softproof_filename;
  }
  else if(darktable.color_profiles->histogram_type == DT_COLORSPACE_WORK)
  {
    dt_ioppr_get_work_profile_type(darktable.develop, profile_type, profile_filename);
  }
  else if(darktable.color_profiles->histogram_type == DT_COLORSPACE_EXPORT)
  {
    dt_ioppr_get_export_profile_type(darktable.develop, profile_type, profile_filename);
  }
  else
  {
    *profile_type = darktable.color_profiles->histogram_type;
    *profile_filename = darktable.color_profiles->histogram_filename;
  }
}

float dt_ioppr_get_rgb_matrix_luminance(const float *const rgb, const dt_iop_order_iccprofile_info_t *const profile_info)
{
  float luminance = 0.f;

  if(profile_info->nonlinearlut)
  {
    float linear_rgb[3] = { 0.f };

    _apply_trc_in(rgb, linear_rgb, profile_info);
    luminance = profile_info->matrix_in[3] * linear_rgb[0] + profile_info->matrix_in[4] * linear_rgb[1] + profile_info->matrix_in[5] * linear_rgb[2];
  }
  else
    luminance = profile_info->matrix_in[3] * rgb[0] + profile_info->matrix_in[4] * rgb[1] + profile_info->matrix_in[5] * rgb[2];

  return luminance;
}

void dt_ioppr_rgb_matrix_to_xyz(const float *const rgb, float *xyz, const dt_iop_order_iccprofile_info_t *const profile_info)
{
  if(profile_info->nonlinearlut)
  {
    float linear_rgb[3];
    _apply_trc_in(rgb, linear_rgb, profile_info);
    _ioppr_linear_rgb_matrix_to_xyz(linear_rgb, xyz, profile_info);
  }
  else
    _ioppr_linear_rgb_matrix_to_xyz(rgb, xyz, profile_info);
}

void dt_ioppr_lab_to_rgb_matrix(const float *const lab, float *rgb, const dt_iop_order_iccprofile_info_t *const profile_info)
{
  float xyz[3] = { 0.0f, 0.0f, 0.0f };

  dt_Lab_to_XYZ(lab, xyz);

  _ioppr_xyz_to_linear_rgb_matrix(xyz, rgb, profile_info);

  if(profile_info->nonlinearlut) _apply_trc_out(rgb, rgb, profile_info);
}

void dt_ioppr_rgb_matrix_to_lab(const float *const rgb, float *lab, const dt_iop_order_iccprofile_info_t *const profile_info)
{
  float xyz[3] = { 0.0f, 0.0f, 0.0f };

  dt_ioppr_rgb_matrix_to_xyz(rgb, xyz, profile_info);

  dt_XYZ_to_Lab(xyz, lab);
}

float dt_ioppr_get_profile_info_middle_grey(const dt_iop_order_iccprofile_info_t *const profile_info)
{
  return profile_info->grey;
}

float dt_ioppr_compensate_middle_grey(const float x, const dt_iop_order_iccprofile_info_t *const profile_info)
{
  // we transform the curve nodes from the image colorspace to lab
  float lab[3] = { 0 };
  float rgb[3] = { 0 };

  rgb[0] = rgb[1] = rgb[2] = x;
  dt_ioppr_rgb_matrix_to_lab(rgb, lab, profile_info);
  return lab[0] * .01f;
}

float dt_ioppr_uncompensate_middle_grey(const float x, const dt_iop_order_iccprofile_info_t *const profile_info)
{
  // we transform the curve nodes from lab to the image colorspace
  float lab[3] = { 0 };
  float rgb[3] = { 0 };

  lab[0] = x * 100.f;
  dt_ioppr_lab_to_rgb_matrix(lab, rgb, profile_info);
  return rgb[0];
}

#if defined(__SSE2__x) // FIXME: this is slower than the C version
static __m128 _ioppr_linear_rgb_matrix_to_xyz_sse(const __m128 rgb, const dt_iop_order_iccprofile_info_t *const profile_info)
{
/*  for(int c = 0; c < 3; c++)
  {
    xyz[c] = 0.0f;
    for(int i = 0; i < 3; i++)
    {
      xyz[c] += profile_info->matrix_in[3 * c + i] * rgb[i];
    }
  }*/
  const __m128 m0 = _mm_set_ps(0.0f, profile_info->matrix_in[6], profile_info->matrix_in[3], profile_info->matrix_in[0]);
  const __m128 m1 = _mm_set_ps(0.0f, profile_info->matrix_in[7], profile_info->matrix_in[4], profile_info->matrix_in[1]);
  const __m128 m2 = _mm_set_ps(0.0f, profile_info->matrix_in[8], profile_info->matrix_in[5], profile_info->matrix_in[2]);

  __m128 xyz
      = _mm_add_ps(_mm_mul_ps(m0, _mm_shuffle_ps(rgb, rgb, _MM_SHUFFLE(0, 0, 0, 0))),
                   _mm_add_ps(_mm_mul_ps(m1, _mm_shuffle_ps(rgb, rgb, _MM_SHUFFLE(1, 1, 1, 1))),
                              _mm_mul_ps(m2, _mm_shuffle_ps(rgb, rgb, _MM_SHUFFLE(2, 2, 2, 2)))));
  return xyz;
}

static __m128 _ioppr_xyz_to_linear_rgb_matrix_sse(const __m128 xyz, const dt_iop_order_iccprofile_info_t *const profile_info)
{
/*  for(int c = 0; c < 3; c++)
  {
    rgb[c] = 0.0f;
    for(int i = 0; i < 3; i++)
    {
      rgb[c] += profile_info->matrix_out[3 * c + i] * xyz[i];
    }
  }*/
  const __m128 m0 = _mm_set_ps(0.0f, profile_info->matrix_out[6], profile_info->matrix_out[3], profile_info->matrix_out[0]);
  const __m128 m1 = _mm_set_ps(0.0f, profile_info->matrix_out[7], profile_info->matrix_out[4], profile_info->matrix_out[1]);
  const __m128 m2 = _mm_set_ps(0.0f, profile_info->matrix_out[8], profile_info->matrix_out[5], profile_info->matrix_out[2]);

  __m128 rgb
      = _mm_add_ps(_mm_mul_ps(m0, _mm_shuffle_ps(xyz, xyz, _MM_SHUFFLE(0, 0, 0, 0))),
                   _mm_add_ps(_mm_mul_ps(m1, _mm_shuffle_ps(xyz, xyz, _MM_SHUFFLE(1, 1, 1, 1))),
                              _mm_mul_ps(m2, _mm_shuffle_ps(xyz, xyz, _MM_SHUFFLE(2, 2, 2, 2)))));
  return rgb;
}

static void _transform_rgb_to_lab_matrix_sse(float *const image, const int width, const int height, const dt_iop_order_iccprofile_info_t *const profile_info)
{
  const int ch = 4;
  const size_t stride = (size_t)(width * height);

  _apply_tonecurves(image, width, height, profile_info->lut_in[0], profile_info->lut_in[1], profile_info->lut_in[2],
      profile_info->unbounded_coeffs_in[0], profile_info->unbounded_coeffs_in[1], profile_info->unbounded_coeffs_in[2], profile_info->lutsize);

#ifdef _OPENMP
#pragma omp parallel for default(none) schedule(static)
#endif
  for(size_t y = 0; y < stride; y++)
  {
    float *const in = image + y * ch;

    __m128 xyz = { 0.0f };
    __m128 rgb = _mm_load_ps(in);

    xyz = _ioppr_linear_rgb_matrix_to_xyz_sse(rgb, profile_info);

    rgb = dt_XYZ_to_Lab_sse2(xyz);
    const float a = in[3];
    _mm_stream_ps(in, rgb);
    in[3] = a;
  }
}

static void _transform_lab_to_rgb_matrix_sse(float *const image, const int width, const int height, const dt_iop_order_iccprofile_info_t *const profile_info)
{
  const int ch = 4;
  const size_t stride = (size_t)width * height;

#ifdef _OPENMP
#pragma omp parallel for default(none) schedule(static)
#endif
  for(size_t y = 0; y < stride; y++)
  {
    float *const in = image + y * ch;

    __m128 xyz = { 0.0f };
    __m128 lab = _mm_load_ps(in);

    xyz = dt_Lab_to_XYZ_sse2(lab);
    lab = _ioppr_xyz_to_linear_rgb_matrix_sse(xyz, profile_info);
    const float a = in[3];
    _mm_stream_ps(in, lab);
    in[3] = a;
  }

  _apply_tonecurves(image, width, height, profile_info->lut_out[0], profile_info->lut_out[1], profile_info->lut_out[2],
      profile_info->unbounded_coeffs_out[0], profile_info->unbounded_coeffs_out[1], profile_info->unbounded_coeffs_out[2], profile_info->lutsize);
}

// FIXME: this is slower than the C version
static void _transform_matrix_sse(struct dt_iop_module_t *self, float *const image, const int width, const int height,
    const int cst_from, const int cst_to, int *converted_cst, const dt_iop_order_iccprofile_info_t *const profile_info)
{
  if(cst_from == cst_to)
  {
    *converted_cst = cst_to;
    return;
  }

  *converted_cst = cst_to;

  if(cst_from == iop_cs_rgb && cst_to == iop_cs_Lab)
  {
    _transform_rgb_to_lab_matrix_sse(image, width, height, profile_info);
  }
  else if(cst_from == iop_cs_Lab && cst_to == iop_cs_rgb)
  {
    _transform_lab_to_rgb_matrix_sse(image, width, height, profile_info);
  }
  else
  {
    *converted_cst = cst_from;
    fprintf(stderr, "[_transform_matrix_sse] invalid conversion from %i to %i\n", cst_from, cst_to);
  }
}

static void _transform_matrix_rgb_sse(float *const image, const int width, const int height,
                                      const dt_iop_order_iccprofile_info_t *const profile_info_from,
                                      const dt_iop_order_iccprofile_info_t *const profile_info_to)
{
  const int ch = 4;
  const size_t stride = (size_t)(width * height);

  _apply_tonecurves(image, width, height, profile_info_from->lut_in[0], profile_info_from->lut_in[1],
                    profile_info_from->lut_in[2], profile_info_from->unbounded_coeffs_in[0],
                    profile_info_from->unbounded_coeffs_in[1], profile_info_from->unbounded_coeffs_in[2],
                    profile_info_from->lutsize);

#ifdef _OPENMP
#pragma omp parallel for default(none) schedule(static)
#endif
  for(size_t y = 0; y < stride; y++)
  {
    float *const in = image + y * ch;

    __m128 xyz = { 0.0f };
    __m128 rgb = _mm_load_ps(in);

    xyz = _ioppr_linear_rgb_matrix_to_xyz_sse(rgb, profile_info_from);
    rgb = _ioppr_xyz_to_linear_rgb_matrix_sse(xyz, profile_info_to);

    const float a = in[3];
    _mm_stream_ps(in, rgb);
    in[3] = a;
  }

  _apply_tonecurves(image, width, height, profile_info_to->lut_out[0], profile_info_to->lut_out[1],
                    profile_info_to->lut_out[2], profile_info_to->unbounded_coeffs_out[0],
                    profile_info_to->unbounded_coeffs_out[1], profile_info_to->unbounded_coeffs_out[2],
                    profile_info_to->lutsize);
}
#endif

void dt_ioppr_transform_image_colorspace(struct dt_iop_module_t *self, const float *const image_in,
                                         float *const image_out, const int width, const int height,
                                         const int cst_from, const int cst_to, int *converted_cst,
                                         const dt_iop_order_iccprofile_info_t *const profile_info)
{
  if(cst_from == cst_to)
  {
    *converted_cst = cst_to;
    return;
  }
  if(profile_info == NULL)
  {
    fprintf(stderr, "[dt_ioppr_transform_image_colorspace] module %s must be between input color profile and output color profile\n", self->op);
    *converted_cst = cst_from;
    return;
  }
  if(profile_info->type == DT_COLORSPACE_NONE)
  {
    *converted_cst = cst_from;
    return;
  }

  dt_times_t start_time = { 0 }, end_time = { 0 };
  if(darktable.unmuted & DT_DEBUG_PERF) dt_get_times(&start_time);

  // matrix should be never NAN, this is only to test it against lcms2!
  if(!isnan(profile_info->matrix_in[0]) && !isnan(profile_info->matrix_out[0]))
  {
    // FIXME: sse is slower than the C version
    // if(darktable.codepath.OPENMP_SIMD)
    _transform_matrix(self, image_in, image_out, width, height, cst_from, cst_to, converted_cst, profile_info);
    /*
    #if defined(__SSE2__)
        else if(darktable.codepath.SSE2)
          _transform_matrix_sse(self, image, width, height, cst_from, cst_to, converted_cst, profile_info);
    #endif
        else
          dt_unreachable_codepath();
    */
    if(darktable.unmuted & DT_DEBUG_PERF)
    {
      dt_get_times(&end_time);
      fprintf(stderr, "image colorspace transform %s-->%s took %.3f secs (%.3f CPU) [%s %s]\n",
          (cst_from == iop_cs_rgb) ? "RGB": "Lab", (cst_to == iop_cs_rgb) ? "RGB": "Lab",
          end_time.clock - start_time.clock, end_time.user - start_time.user, self->op, self->multi_name);
    }
  }
  else
  {
    _transform_lcms2(self, image_in, image_out, width, height, cst_from, cst_to, converted_cst, profile_info);

    if(darktable.unmuted & DT_DEBUG_PERF)
    {
      dt_get_times(&end_time);
      fprintf(stderr, "image colorspace transform %s-->%s took %.3f secs (%.3f lcms2) [%s %s]\n",
          (cst_from == iop_cs_rgb) ? "RGB": "Lab", (cst_to == iop_cs_rgb) ? "RGB": "Lab",
          end_time.clock - start_time.clock, end_time.user - start_time.user, self->op, self->multi_name);
    }
  }

  if(*converted_cst == cst_from)
    fprintf(stderr, "[dt_ioppr_transform_image_colorspace] invalid conversion from %i to %i\n", cst_from, cst_to);
}

void dt_ioppr_transform_image_colorspace_rgb(const float *const image_in, float *const image_out, const int width,
                                             const int height,
                                             const dt_iop_order_iccprofile_info_t *const profile_info_from,
                                             const dt_iop_order_iccprofile_info_t *const profile_info_to,
                                             const char *message)
{
  if(profile_info_from->type == DT_COLORSPACE_NONE || profile_info_to->type == DT_COLORSPACE_NONE)
  {
    return;
  }
  if(profile_info_from->type == profile_info_to->type
     && strcmp(profile_info_from->filename, profile_info_to->filename) == 0)
  {
    if(image_in != image_out)
      memcpy(image_out, image_in, width * height * 4 * sizeof(float));

    return;
  }

  dt_times_t start_time = { 0 }, end_time = { 0 };
  if(darktable.unmuted & DT_DEBUG_PERF) dt_get_times(&start_time);

  if(!isnan(profile_info_from->matrix_in[0]) && !isnan(profile_info_from->matrix_out[0])
     && !isnan(profile_info_to->matrix_in[0]) && !isnan(profile_info_to->matrix_out[0]))
  {
    // FIXME: sse is slower than the C version
    // if(darktable.codepath.OPENMP_SIMD)
    _transform_matrix_rgb(image_in, image_out, width, height, profile_info_from, profile_info_to);
    /*
    #if defined(__SSE2__)
        else if(darktable.codepath.SSE2)
          _transform_matrix_rgb_sse(self, image, width, height, profile_info_from, profile_info_to);
    #endif
        else
          dt_unreachable_codepath();
    */
    if(darktable.unmuted & DT_DEBUG_PERF)
    {
      dt_get_times(&end_time);
      fprintf(stderr, "image colorspace transform RGB-->RGB took %.3f secs (%.3f CPU) [%s]\n",
              end_time.clock - start_time.clock, end_time.user - start_time.user, (message) ? message : "");
    }
  }
  else
  {
    _transform_lcms2_rgb(image_in, image_out, width, height, profile_info_from, profile_info_to);

    if(darktable.unmuted & DT_DEBUG_PERF)
    {
      dt_get_times(&end_time);
      fprintf(stderr, "image colorspace transform RGB-->RGB took %.3f secs (%.3f lcms2) [%s]\n",
              end_time.clock - start_time.clock, end_time.user - start_time.user, (message) ? message : "");
    }
  }
}

#ifdef HAVE_OPENCL
dt_colorspaces_cl_global_t *dt_colorspaces_init_cl_global()
{
  dt_colorspaces_cl_global_t *g = (dt_colorspaces_cl_global_t *)malloc(sizeof(dt_colorspaces_cl_global_t));

  const int program = 23; // colorspaces.cl, from programs.conf
  g->kernel_colorspaces_transform_lab_to_rgb_matrix = dt_opencl_create_kernel(program, "colorspaces_transform_lab_to_rgb_matrix");
  g->kernel_colorspaces_transform_rgb_matrix_to_lab = dt_opencl_create_kernel(program, "colorspaces_transform_rgb_matrix_to_lab");
  g->kernel_colorspaces_transform_rgb_matrix_to_rgb
      = dt_opencl_create_kernel(program, "colorspaces_transform_rgb_matrix_to_rgb");
  return g;
}

void dt_colorspaces_free_cl_global(dt_colorspaces_cl_global_t *g)
{
  if(!g) return;

  // destroy kernels
  dt_opencl_free_kernel(g->kernel_colorspaces_transform_lab_to_rgb_matrix);
  dt_opencl_free_kernel(g->kernel_colorspaces_transform_rgb_matrix_to_lab);
  dt_opencl_free_kernel(g->kernel_colorspaces_transform_rgb_matrix_to_rgb);

  free(g);
}

void dt_ioppr_get_profile_info_cl(const dt_iop_order_iccprofile_info_t *const profile_info, dt_colorspaces_iccprofile_info_cl_t *profile_info_cl)
{
  for(int i = 0; i < 9; i++)
  {
    profile_info_cl->matrix_in[i] = profile_info->matrix_in[i];
    profile_info_cl->matrix_out[i] = profile_info->matrix_out[i];
  }
  profile_info_cl->lutsize = profile_info->lutsize;
  for(int i = 0; i < 3; i++)
  {
    for(int j = 0; j < 3; j++)
    {
      profile_info_cl->unbounded_coeffs_in[i][j] = profile_info->unbounded_coeffs_in[i][j];
      profile_info_cl->unbounded_coeffs_out[i][j] = profile_info->unbounded_coeffs_out[i][j];
    }
  }
  profile_info_cl->nonlinearlut = profile_info->nonlinearlut;
  profile_info_cl->grey = profile_info->grey;
}

cl_float *dt_ioppr_get_trc_cl(const dt_iop_order_iccprofile_info_t *const profile_info)
{
  cl_float *trc = malloc(profile_info->lutsize * 6 * sizeof(cl_float));
  if(trc)
  {
    int x = 0;
    for(int c = 0; c < 3; c++)
      for(int y = 0; y < profile_info->lutsize; y++, x++)
        trc[x] = profile_info->lut_in[c][y];
    for(int c = 0; c < 3; c++)
      for(int y = 0; y < profile_info->lutsize; y++, x++)
        trc[x] = profile_info->lut_out[c][y];
  }
  return trc;
}

cl_int dt_ioppr_build_iccprofile_params_cl(const dt_iop_order_iccprofile_info_t *const profile_info,
                                           const int devid, dt_colorspaces_iccprofile_info_cl_t **_profile_info_cl,
                                           cl_float **_profile_lut_cl, cl_mem *_dev_profile_info,
                                           cl_mem *_dev_profile_lut)
{
  cl_int err = CL_SUCCESS;

  dt_colorspaces_iccprofile_info_cl_t *profile_info_cl = calloc(1, sizeof(dt_colorspaces_iccprofile_info_cl_t));
  cl_float *profile_lut_cl = NULL;
  cl_mem dev_profile_info = NULL;
  cl_mem dev_profile_lut = NULL;

  if(profile_info)
  {
    dt_ioppr_get_profile_info_cl(profile_info, profile_info_cl);
    profile_lut_cl = dt_ioppr_get_trc_cl(profile_info);

    dev_profile_info = dt_opencl_copy_host_to_device_constant(devid, sizeof(*profile_info_cl), profile_info_cl);
    if(dev_profile_info == NULL)
    {
      fprintf(stderr, "[dt_ioppr_build_iccprofile_params_cl] error allocating memory 5\n");
      err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
      goto cleanup;
    }

    dev_profile_lut = dt_opencl_copy_host_to_device(devid, profile_lut_cl, 256, 256 * 6, sizeof(float));
    if(dev_profile_lut == NULL)
    {
      fprintf(stderr, "[dt_ioppr_build_iccprofile_params_cl] error allocating memory 6\n");
      err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
      goto cleanup;
    }
  }
  else
  {
    profile_lut_cl = malloc(1 * 6 * sizeof(cl_float));

    dev_profile_lut = dt_opencl_copy_host_to_device(devid, profile_lut_cl, 1, 1 * 6, sizeof(float));
    if(dev_profile_lut == NULL)
    {
      fprintf(stderr, "[dt_ioppr_build_iccprofile_params_cl] error allocating memory 6\n");
      err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
      goto cleanup;
    }
  }

cleanup:
  *_profile_info_cl = profile_info_cl;
  *_profile_lut_cl = profile_lut_cl;
  *_dev_profile_info = dev_profile_info;
  *_dev_profile_lut = dev_profile_lut;

  return err;
}

void dt_ioppr_free_iccprofile_params_cl(dt_colorspaces_iccprofile_info_cl_t **_profile_info_cl,
                                        cl_float **_profile_lut_cl, cl_mem *_dev_profile_info,
                                        cl_mem *_dev_profile_lut)
{
  dt_colorspaces_iccprofile_info_cl_t *profile_info_cl = *_profile_info_cl;
  cl_float *profile_lut_cl = *_profile_lut_cl;
  cl_mem dev_profile_info = *_dev_profile_info;
  cl_mem dev_profile_lut = *_dev_profile_lut;

  if(profile_info_cl) free(profile_info_cl);
  if(dev_profile_info) dt_opencl_release_mem_object(dev_profile_info);
  if(dev_profile_lut) dt_opencl_release_mem_object(dev_profile_lut);
  if(profile_lut_cl) free(profile_lut_cl);

  *_profile_info_cl = NULL;
  *_profile_lut_cl = NULL;
  *_dev_profile_info = NULL;
  *_dev_profile_lut = NULL;
}

int dt_ioppr_transform_image_colorspace_cl(struct dt_iop_module_t *self, const int devid, cl_mem dev_img_in,
                                           cl_mem dev_img_out, const int width, const int height,
                                           const int cst_from, const int cst_to, int *converted_cst,
                                           const dt_iop_order_iccprofile_info_t *const profile_info)
{
  cl_int err = CL_SUCCESS;

  if(cst_from == cst_to)
  {
    *converted_cst = cst_to;
    return TRUE;
  }
  if(profile_info == NULL)
  {
    fprintf(stderr, "[dt_ioppr_transform_image_colorspace_cl] module %s must be between input color profile and output color profile\n", self->op);
    *converted_cst = cst_from;
    return FALSE;
  }
  if(profile_info->type == DT_COLORSPACE_NONE)
  {
    *converted_cst = cst_from;
    return FALSE;
  }

  const int ch = 4;
  float *src_buffer = NULL;
  int in_place = (dev_img_in == dev_img_out);

  int kernel_transform = 0;
  cl_mem dev_tmp = NULL;
  cl_mem dev_profile_info = NULL;
  cl_mem dev_lut = NULL;
  dt_colorspaces_iccprofile_info_cl_t profile_info_cl;
  cl_float *lut_cl = NULL;

  *converted_cst = cst_from;

  // if we have a matrix use opencl
  if(!isnan(profile_info->matrix_in[0]) && !isnan(profile_info->matrix_out[0]))
  {
    dt_times_t start_time = { 0 }, end_time = { 0 };
    if(darktable.unmuted & DT_DEBUG_PERF) dt_get_times(&start_time);

    size_t origin[] = { 0, 0, 0 };
    size_t region[] = { width, height, 1 };

    if(cst_from == iop_cs_rgb && cst_to == iop_cs_Lab)
    {
      kernel_transform = darktable.opencl->colorspaces->kernel_colorspaces_transform_rgb_matrix_to_lab;
    }
    else if(cst_from == iop_cs_Lab && cst_to == iop_cs_rgb)
    {
      kernel_transform = darktable.opencl->colorspaces->kernel_colorspaces_transform_lab_to_rgb_matrix;
    }
    else
    {
      err = CL_INVALID_KERNEL;
      *converted_cst = cst_from;
      fprintf(stderr, "[dt_ioppr_transform_image_colorspace_cl] invalid conversion from %i to %i\n", cst_from, cst_to);
      goto cleanup;
    }

    dt_ioppr_get_profile_info_cl(profile_info, &profile_info_cl);
    lut_cl = dt_ioppr_get_trc_cl(profile_info);

    if(in_place)
    {
      dev_tmp = dt_opencl_alloc_device(devid, width, height, 4 * sizeof(float));
      if(dev_tmp == NULL)
      {
        fprintf(stderr,
                "[dt_ioppr_transform_image_colorspace_cl] error allocating memory for color transformation 4\n");
        err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
        goto cleanup;
      }

      err = dt_opencl_enqueue_copy_image(devid, dev_img_in, dev_tmp, origin, origin, region);
      if(err != CL_SUCCESS)
      {
        fprintf(stderr, "[dt_ioppr_transform_image_colorspace_cl] error on copy image for color transformation\n");
        goto cleanup;
      }
    }
    else
    {
      dev_tmp = dev_img_in;
    }

    dev_profile_info = dt_opencl_copy_host_to_device_constant(devid, sizeof(profile_info_cl), &profile_info_cl);
    if(dev_profile_info == NULL)
    {
      fprintf(stderr, "[dt_ioppr_transform_image_colorspace_cl] error allocating memory for color transformation 5\n");
      err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
      goto cleanup;
    }
    dev_lut = dt_opencl_copy_host_to_device(devid, lut_cl, 256, 256 * 6, sizeof(float));
    if(dev_lut == NULL)
    {
      fprintf(stderr, "[dt_ioppr_transform_image_colorspace_cl] error allocating memory for color transformation 6\n");
      err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
      goto cleanup;
    }

    size_t sizes[] = { ROUNDUPWD(width), ROUNDUPHT(height), 1 };

    dt_opencl_set_kernel_arg(devid, kernel_transform, 0, sizeof(cl_mem), (void *)&dev_tmp);
    dt_opencl_set_kernel_arg(devid, kernel_transform, 1, sizeof(cl_mem), (void *)&dev_img_out);
    dt_opencl_set_kernel_arg(devid, kernel_transform, 2, sizeof(int), (void *)&width);
    dt_opencl_set_kernel_arg(devid, kernel_transform, 3, sizeof(int), (void *)&height);
    dt_opencl_set_kernel_arg(devid, kernel_transform, 4, sizeof(cl_mem), (void *)&dev_profile_info);
    dt_opencl_set_kernel_arg(devid, kernel_transform, 5, sizeof(cl_mem), (void *)&dev_lut);
    err = dt_opencl_enqueue_kernel_2d(devid, kernel_transform, sizes);
    if(err != CL_SUCCESS)
    {
      fprintf(stderr, "[dt_ioppr_transform_image_colorspace_cl] error %i enqueue kernel for color transformation\n", err);
      goto cleanup;
    }

    *converted_cst = cst_to;

    if(darktable.unmuted & DT_DEBUG_PERF)
    {
      dt_get_times(&end_time);
      fprintf(stderr, "image colorspace transform %s-->%s took %.3f secs (%.3f GPU) [%s %s]\n",
          (cst_from == iop_cs_rgb) ? "RGB": "Lab", (cst_to == iop_cs_rgb) ? "RGB": "Lab",
          end_time.clock - start_time.clock, end_time.user - start_time.user, self->op, self->multi_name);
    }
  }
  else
  {
    // no matrix, call lcms2
    src_buffer = dt_alloc_align(64, width * height * ch * sizeof(float));
    if(src_buffer == NULL)
    {
      fprintf(stderr, "[dt_ioppr_transform_image_colorspace_cl] error allocating memory for color transformation 1\n");
      err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
      goto cleanup;
    }

    err = dt_opencl_copy_device_to_host(devid, src_buffer, dev_img_in, width, height, ch * sizeof(float));
    if(err != CL_SUCCESS)
    {
      fprintf(stderr, "[dt_ioppr_transform_image_colorspace_cl] error allocating memory for color transformation 2\n");
      goto cleanup;
    }

    // just call the CPU version for now
    dt_ioppr_transform_image_colorspace(self, src_buffer, src_buffer, width, height, cst_from, cst_to,
                                        converted_cst, profile_info);

    err = dt_opencl_write_host_to_device(devid, src_buffer, dev_img_out, width, height, ch * sizeof(float));
    if(err != CL_SUCCESS)
    {
      fprintf(stderr, "[dt_ioppr_transform_image_colorspace_cl] error allocating memory for color transformation 3\n");
      goto cleanup;
    }
  }

cleanup:
  if(src_buffer) dt_free_align(src_buffer);
  if(dev_tmp && in_place) dt_opencl_release_mem_object(dev_tmp);
  if(dev_profile_info) dt_opencl_release_mem_object(dev_profile_info);
  if(dev_lut) dt_opencl_release_mem_object(dev_lut);
  if(lut_cl) free(lut_cl);

  return (err == CL_SUCCESS) ? TRUE : FALSE;
}

int dt_ioppr_transform_image_colorspace_rgb_cl(const int devid, cl_mem dev_img_in, cl_mem dev_img_out,
                                               const int width, const int height,
                                               const dt_iop_order_iccprofile_info_t *const profile_info_from,
                                               const dt_iop_order_iccprofile_info_t *const profile_info_to,
                                               const char *message)
{
  cl_int err = CL_SUCCESS;

  if(profile_info_from->type == DT_COLORSPACE_NONE || profile_info_to->type == DT_COLORSPACE_NONE)
  {
    return FALSE;
  }
  if(profile_info_from->type == profile_info_to->type
     && strcmp(profile_info_from->filename, profile_info_to->filename) == 0)
  {
    if(dev_img_in != dev_img_out)
    {
      size_t origin[] = { 0, 0, 0 };
      size_t region[] = { width, height, 1 };

      err = dt_opencl_enqueue_copy_image(devid, dev_img_in, dev_img_out, origin, origin, region);
      if(err != CL_SUCCESS)
      {
        fprintf(stderr,
                "[dt_ioppr_transform_image_colorspace_rgb_cl] error on copy image for color transformation\n");
        return FALSE;
      }
    }

    return TRUE;
  }

  const int ch = 4;
  float *src_buffer = NULL;
  int in_place = (dev_img_in == dev_img_out);

  int kernel_transform = 0;
  cl_mem dev_tmp = NULL;

  cl_mem dev_profile_info_from = NULL;
  cl_mem dev_lut_from = NULL;
  dt_colorspaces_iccprofile_info_cl_t profile_info_from_cl;
  cl_float *lut_from_cl = NULL;

  cl_mem dev_profile_info_to = NULL;
  cl_mem dev_lut_to = NULL;
  dt_colorspaces_iccprofile_info_cl_t profile_info_to_cl;
  cl_float *lut_to_cl = NULL;

  // if we have a matrix use opencl
  if(!isnan(profile_info_from->matrix_in[0]) && !isnan(profile_info_from->matrix_out[0])
     && !isnan(profile_info_to->matrix_in[0]) && !isnan(profile_info_to->matrix_out[0]))
  {
    dt_times_t start_time = { 0 }, end_time = { 0 };
    if(darktable.unmuted & DT_DEBUG_PERF) dt_get_times(&start_time);

    size_t origin[] = { 0, 0, 0 };
    size_t region[] = { width, height, 1 };

    kernel_transform = darktable.opencl->colorspaces->kernel_colorspaces_transform_rgb_matrix_to_rgb;

    dt_ioppr_get_profile_info_cl(profile_info_from, &profile_info_from_cl);
    lut_from_cl = dt_ioppr_get_trc_cl(profile_info_from);

    dt_ioppr_get_profile_info_cl(profile_info_to, &profile_info_to_cl);
    lut_to_cl = dt_ioppr_get_trc_cl(profile_info_to);

    if(in_place)
    {
      dev_tmp = dt_opencl_alloc_device(devid, width, height, 4 * sizeof(float));
      if(dev_tmp == NULL)
      {
        fprintf(
            stderr,
            "[dt_ioppr_transform_image_colorspace_rgb_cl] error allocating memory for color transformation 4\n");
        err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
        goto cleanup;
      }

      err = dt_opencl_enqueue_copy_image(devid, dev_img_in, dev_tmp, origin, origin, region);
      if(err != CL_SUCCESS)
      {
        fprintf(stderr,
                "[dt_ioppr_transform_image_colorspace_rgb_cl] error on copy image for color transformation\n");
        goto cleanup;
      }
    }
    else
    {
      dev_tmp = dev_img_in;
    }

    dev_profile_info_from
        = dt_opencl_copy_host_to_device_constant(devid, sizeof(profile_info_from_cl), &profile_info_from_cl);
    if(dev_profile_info_from == NULL)
    {
      fprintf(stderr,
              "[dt_ioppr_transform_image_colorspace_rgb_cl] error allocating memory for color transformation 5\n");
      err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
      goto cleanup;
    }
    dev_lut_from = dt_opencl_copy_host_to_device(devid, lut_from_cl, 256, 256 * 6, sizeof(float));
    if(dev_lut_from == NULL)
    {
      fprintf(stderr,
              "[dt_ioppr_transform_image_colorspace_rgb_cl] error allocating memory for color transformation 6\n");
      err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
      goto cleanup;
    }

    dev_profile_info_to
        = dt_opencl_copy_host_to_device_constant(devid, sizeof(profile_info_to_cl), &profile_info_to_cl);
    if(dev_profile_info_to == NULL)
    {
      fprintf(stderr,
              "[dt_ioppr_transform_image_colorspace_rgb_cl] error allocating memory for color transformation 7\n");
      err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
      goto cleanup;
    }
    dev_lut_to = dt_opencl_copy_host_to_device(devid, lut_to_cl, 256, 256 * 6, sizeof(float));
    if(dev_lut_to == NULL)
    {
      fprintf(stderr,
              "[dt_ioppr_transform_image_colorspace_rgb_cl] error allocating memory for color transformation 8\n");
      err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
      goto cleanup;
    }

    size_t sizes[] = { ROUNDUPWD(width), ROUNDUPHT(height), 1 };

    dt_opencl_set_kernel_arg(devid, kernel_transform, 0, sizeof(cl_mem), (void *)&dev_tmp);
    dt_opencl_set_kernel_arg(devid, kernel_transform, 1, sizeof(cl_mem), (void *)&dev_img_out);
    dt_opencl_set_kernel_arg(devid, kernel_transform, 2, sizeof(int), (void *)&width);
    dt_opencl_set_kernel_arg(devid, kernel_transform, 3, sizeof(int), (void *)&height);
    dt_opencl_set_kernel_arg(devid, kernel_transform, 4, sizeof(cl_mem), (void *)&dev_profile_info_from);
    dt_opencl_set_kernel_arg(devid, kernel_transform, 5, sizeof(cl_mem), (void *)&dev_lut_from);
    dt_opencl_set_kernel_arg(devid, kernel_transform, 6, sizeof(cl_mem), (void *)&dev_profile_info_to);
    dt_opencl_set_kernel_arg(devid, kernel_transform, 7, sizeof(cl_mem), (void *)&dev_lut_to);
    err = dt_opencl_enqueue_kernel_2d(devid, kernel_transform, sizes);
    if(err != CL_SUCCESS)
    {
      fprintf(stderr,
              "[dt_ioppr_transform_image_colorspace_rgb_cl] error %i enqueue kernel for color transformation\n",
              err);
      goto cleanup;
    }

    if(darktable.unmuted & DT_DEBUG_PERF)
    {
      dt_get_times(&end_time);
      fprintf(stderr, "image colorspace transform RGB-->RGB took %.3f secs (%.3f GPU) [%s]\n",
              end_time.clock - start_time.clock, end_time.user - start_time.user, (message) ? message : "");
    }
  }
  else
  {
    // no matrix, call lcms2
    src_buffer = dt_alloc_align(64, width * height * ch * sizeof(float));
    if(src_buffer == NULL)
    {
      fprintf(stderr,
              "[dt_ioppr_transform_image_colorspace_rgb_cl] error allocating memory for color transformation 1\n");
      err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
      goto cleanup;
    }

    err = dt_opencl_copy_device_to_host(devid, src_buffer, dev_img_in, width, height, ch * sizeof(float));
    if(err != CL_SUCCESS)
    {
      fprintf(stderr,
              "[dt_ioppr_transform_image_colorspace_rgb_cl] error allocating memory for color transformation 2\n");
      goto cleanup;
    }

    // just call the CPU version for now
    dt_ioppr_transform_image_colorspace_rgb(src_buffer, src_buffer, width, height, profile_info_from,
                                            profile_info_to, message);

    err = dt_opencl_write_host_to_device(devid, src_buffer, dev_img_out, width, height, ch * sizeof(float));
    if(err != CL_SUCCESS)
    {
      fprintf(stderr,
              "[dt_ioppr_transform_image_colorspace_rgb_cl] error allocating memory for color transformation 3\n");
      goto cleanup;
    }
  }

cleanup:
  if(src_buffer) dt_free_align(src_buffer);
  if(dev_tmp && in_place) dt_opencl_release_mem_object(dev_tmp);

  if(dev_profile_info_from) dt_opencl_release_mem_object(dev_profile_info_from);
  if(dev_lut_from) dt_opencl_release_mem_object(dev_lut_from);
  if(lut_from_cl) free(lut_from_cl);

  if(dev_profile_info_to) dt_opencl_release_mem_object(dev_profile_info_to);
  if(dev_lut_to) dt_opencl_release_mem_object(dev_lut_to);
  if(lut_to_cl) free(lut_to_cl);

  return (err == CL_SUCCESS) ? TRUE : FALSE;
}
#endif
