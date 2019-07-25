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

#include "common/darktable.h"
#include "common/debug.h"
#include "common/file_location.h"
#include "common/image.h"
#include "common/image_cache.h"
#include "common/imageio.h"
#include "common/imageio_module.h"
#include "common/metadata.h"
#include "common/utility.h"
#include "common/variables.h"
#include "control/conf.h"
#include "control/control.h"
#include "dtgtk/button.h"
#include "dtgtk/paint.h"
#include "gui/gtk.h"
#include "gui/gtkentry.h"
#include "imageio/storage/imageio_storage_api.h"
#ifdef GDK_WINDOWING_QUARTZ
#include "osx/osx.h"
#endif
#include <stdio.h>
#include <stdlib.h>

DT_MODULE(2)

// gui data
typedef struct latex_t
{
  GtkEntry *entry;
  GtkEntry *title_entry;
} latex_t;

// saved params
typedef struct dt_imageio_latex_t
{
  char filename[DT_MAX_PATH_FOR_PARAMS];
  char title[1024];
  char cached_dirname[DT_MAX_PATH_FOR_PARAMS]; // expanded during first img store, not stored in param struct.
  dt_variables_params_t *vp;
  GList *l;
} dt_imageio_latex_t;

// sorted list of all images
typedef struct pair_t
{
  char line[4096];
  int pos;
} pair_t;


const char *name(const struct dt_imageio_module_storage_t *self)
{
  return _("LaTeX book template");
}

void *legacy_params(dt_imageio_module_storage_t *self, const void *const old_params,
                    const size_t old_params_size, const int old_version, const int new_version,
                    size_t *new_size)
{
  if(old_version == 1 && new_version == 2)
  {
    typedef struct dt_imageio_latex_v1_t
    {
      char filename[1024];
      char title[1024];
      char cached_dirname[1024]; // expanded during first img store, not stored in param struct.
      dt_variables_params_t *vp;
      GList *l;
    } dt_imageio_latex_v1_t;

    dt_imageio_latex_t *n = (dt_imageio_latex_t *)malloc(sizeof(dt_imageio_latex_t));
    dt_imageio_latex_v1_t *o = (dt_imageio_latex_v1_t *)old_params;

    g_strlcpy(n->filename, o->filename, sizeof(n->filename));
    g_strlcpy(n->title, o->title, sizeof(n->title));
    g_strlcpy(n->cached_dirname, o->cached_dirname, sizeof(n->cached_dirname));

    *new_size = self->params_size(self);
    return n;
  }
  return NULL;
}

static void button_clicked(GtkWidget *widget, dt_imageio_module_storage_t *self)
{
  latex_t *d = (latex_t *)self->gui_data;
  GtkWidget *win = dt_ui_main_window(darktable.gui->ui);
  GtkWidget *filechooser = gtk_file_chooser_dialog_new(
      _("select directory"), GTK_WINDOW(win), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, _("_cancel"),
      GTK_RESPONSE_CANCEL, _("_select as output destination"), GTK_RESPONSE_ACCEPT, (char *)NULL);
#ifdef GDK_WINDOWING_QUARTZ
  dt_osx_disallow_fullscreen(filechooser);
#endif

  gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(filechooser), FALSE);
  gchar *old = g_strdup(gtk_entry_get_text(d->entry));
  char *c = g_strstr_len(old, -1, "$");
  if(c) *c = '\0';
  gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(filechooser), old);
  g_free(old);
  if(gtk_dialog_run(GTK_DIALOG(filechooser)) == GTK_RESPONSE_ACCEPT)
  {
    gchar *dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(filechooser));
    char *composed = g_build_filename(dir, "$(FILE_NAME)", NULL);

    // composed can now contain '\': on Windows it's the path separator,
    // on other platforms it can be part of a regular folder name.
    // This would later clash with variable substitution, so we have to escape them
    gchar *escaped = dt_util_str_replace(composed, "\\", "\\\\");

    gtk_entry_set_text(GTK_ENTRY(d->entry), escaped); // the signal handler will write this to conf
    g_free(dir);
    g_free(composed);
    g_free(escaped);
  }
  gtk_widget_destroy(filechooser);
}

static void entry_changed_callback(GtkEntry *entry, gpointer user_data)
{
  dt_conf_set_string("plugins/imageio/storage/latex/file_directory", gtk_entry_get_text(entry));
}

static void title_changed_callback(GtkEntry *entry, gpointer user_data)
{
  dt_conf_set_string("plugins/imageio/storage/latex/title", gtk_entry_get_text(entry));
}

void gui_init(dt_imageio_module_storage_t *self)
{
  latex_t *d = (latex_t *)malloc(sizeof(latex_t));
  self->gui_data = (void *)d;
  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), hbox, TRUE, TRUE, 0);
  GtkWidget *widget;

  widget = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, TRUE, 0);
  gchar *dir = dt_conf_get_string("plugins/imageio/storage/latex/file_directory");
  if(dir)
  {
    gtk_entry_set_text(GTK_ENTRY(widget), dir);
    g_free(dir);
  }
  d->entry = GTK_ENTRY(widget);
  dt_gui_key_accel_block_on_focus_connect(GTK_WIDGET(d->entry));

  dt_gtkentry_setup_completion(GTK_ENTRY(widget), dt_gtkentry_get_default_path_compl_list());

  char *tooltip_text = dt_gtkentry_build_completion_tooltip_text(
      _("enter the path where to put exported images\nvariables support bash like string manipulation\n"
        "recognized variables:"),
      dt_gtkentry_get_default_path_compl_list());
  gtk_widget_set_tooltip_text(widget, tooltip_text);
  g_signal_connect(G_OBJECT(widget), "changed", G_CALLBACK(entry_changed_callback), self);
  g_free(tooltip_text);

  widget = dtgtk_button_new(dtgtk_cairo_paint_directory, CPF_DO_NOT_USE_BORDER, NULL);
  gtk_widget_set_tooltip_text(widget, _("select directory"));
  gtk_box_pack_start(GTK_BOX(hbox), widget, FALSE, FALSE, 0);
  g_signal_connect(G_OBJECT(widget), "clicked", G_CALLBACK(button_clicked), self);

  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, DT_PIXEL_APPLY_DPI(10));
  gtk_box_pack_start(GTK_BOX(self->widget), hbox, TRUE, TRUE, 0);

  widget = gtk_label_new(_("title"));
  gtk_widget_set_halign(widget, GTK_ALIGN_START);
  g_object_set(G_OBJECT(widget), "xalign", 0.0, (gchar *)0);
  gtk_box_pack_start(GTK_BOX(hbox), widget, FALSE, FALSE, 0);

  d->title_entry = GTK_ENTRY(gtk_entry_new());
  gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(d->title_entry), TRUE, TRUE, 0);
  dt_gui_key_accel_block_on_focus_connect(GTK_WIDGET(d->title_entry));
  // TODO: support title, author, subject, keywords (collect tags?)
  gtk_widget_set_tooltip_text(GTK_WIDGET(d->title_entry), _("enter the title of the book"));
  dir = dt_conf_get_string("plugins/imageio/storage/latex/title");
  if(dir)
  {
    gtk_entry_set_text(GTK_ENTRY(d->title_entry), dir);
    g_free(dir);
  }
  g_signal_connect(G_OBJECT(d->title_entry), "changed", G_CALLBACK(title_changed_callback), self);
}

void gui_cleanup(dt_imageio_module_storage_t *self)
{
  latex_t *d = (latex_t *)self->gui_data;
  dt_gui_key_accel_block_on_focus_disconnect(GTK_WIDGET(d->entry));
  dt_gui_key_accel_block_on_focus_disconnect(GTK_WIDGET(d->title_entry));
  free(self->gui_data);
}

void gui_reset(dt_imageio_module_storage_t *self)
{
  latex_t *d = (latex_t *)self->gui_data;
  dt_conf_set_string("plugins/imageio/storage/latex/file_directory", gtk_entry_get_text(d->entry));
  dt_conf_set_string("plugins/imageio/storage/latex/title", gtk_entry_get_text(d->title_entry));
}

static gint sort_pos(pair_t *a, pair_t *b)
{
  return a->pos - b->pos;
}

int store(dt_imageio_module_storage_t *self, dt_imageio_module_data_t *sdata, const int imgid,
          dt_imageio_module_format_t *format, dt_imageio_module_data_t *fdata, const int num, const int total,
          const gboolean high_quality, const gboolean upscale, dt_colorspaces_color_profile_type_t icc_type,
          const gchar *icc_filename, dt_iop_color_intent_t icc_intent)
{
  dt_imageio_latex_t *d = (dt_imageio_latex_t *)sdata;

  char filename[PATH_MAX] = { 0 };
  char dirname[PATH_MAX] = { 0 };
  gboolean from_cache = FALSE;
  dt_image_full_path(imgid, dirname, sizeof(dirname), &from_cache);
  // we're potentially called in parallel. have sequence number synchronized:
  dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
  {

    // if filenamepattern is a directory just add ${FILE_NAME} as default..
    if(g_file_test(d->filename, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)
       || ((d->filename + strlen(d->filename))[0] == '/' || (d->filename + strlen(d->filename))[0] == '\\'))
      snprintf(d->filename + strlen(d->filename), sizeof(d->filename) - strlen(d->filename), "$(FILE_NAME)");

    // avoid braindead export which is bound to overwrite at random:
    if(total > 1 && !g_strrstr(d->filename, "$"))
    {
      snprintf(d->filename + strlen(d->filename), sizeof(d->filename) - strlen(d->filename), "_$(SEQUENCE)");
    }

    gchar *fixed_path = dt_util_fix_path(d->filename);
    g_strlcpy(d->filename, fixed_path, sizeof(d->filename));
    g_free(fixed_path);

    d->vp->filename = dirname;
    d->vp->jobcode = "export";
    d->vp->imgid = imgid;
    d->vp->sequence = num;

    gchar *result_filename = dt_variables_expand(d->vp, d->filename, TRUE);
    g_strlcpy(filename, result_filename, sizeof(filename));
    g_free(result_filename);

    g_strlcpy(dirname, filename, sizeof(dirname));

    const char *ext = format->extension(fdata);
    char *c = dirname + strlen(dirname);
    for(; c > dirname && *c != '/'; c--)
      ;
    if(*c == '/') *c = '\0';
    if(g_mkdir_with_parents(dirname, 0755))
    {
      fprintf(stderr, "[imageio_storage_latex] could not create directory: `%s'!\n", dirname);
      dt_control_log(_("could not create directory `%s'!"), dirname);
      dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);
      return 1;
    }

    // store away dir.
    snprintf(d->cached_dirname, sizeof(d->cached_dirname), "%s", dirname);

    c = filename + strlen(filename);
    //     for(; c>filename && *c != '.' && *c != '/' ; c--);
    //     if(c <= filename || *c=='/') c = filename + strlen(filename);

    sprintf(c, ".%s", ext);

    // save image to list, in order:
    pair_t *pair = malloc(sizeof(pair_t));


#if 0 // let's see if we actually want titles and such to be exported:
    char *title = NULL, *description = NULL, *tags = NULL;
    GList *res_title, *res_desc, *res_subj;

    res_title = dt_metadata_get(imgid, "Xmp.dc.title", NULL);
    if(res_title)
    {
      title = res_title->data;
    }

    res_desc = dt_metadata_get(imgid, "Xmp.dc.description", NULL);
    if(res_desc)
    {
      description = res_desc->data;
    }

    res_subj = dt_metadata_get(imgid, "Xmp.dc.subject", NULL);
    if(res_subj)
    {
      // don't show the internal tags (darktable|...)
      res_subj = g_list_first(res_subj);
      GList *iter = res_subj;
      while(iter)
      {
        GList *next = g_list_next(iter);
        if(g_str_has_prefix(iter->data, "darktable|"))
        {
          g_free(iter->data);
          res_subj = g_list_delete_link(res_subj, iter);
        }
        iter = next;
      }
      tags = dt_util_glist_to_str(", ", res_subj);
      g_list_free_full(res_subj, g_free);
    }
#endif

    char relfilename[PATH_MAX] = { 0 };
    c = filename + strlen(filename);
    for(; c > filename && *c != '/'; c--)
      ;
    if(*c == '/') c++;
    if(c <= filename) c = filename;
    snprintf(relfilename, sizeof(relfilename), "%s", c);

    snprintf(pair->line, sizeof(pair->line),
             "\\begin{minipage}{\\imgwidth}%%\n"
             "\\drawtrimcorners%%\n"
             "\\vskip0pt plus 1filll\n"
             "\\begin{minipage}{\\imgwidth}%%\n"
             " \\hfil\\includegraphics[width=\\imgwidth,height=\\imgheight,keepaspectratio]{%s}\\hfil\n"
             "  %% put text under image here\n"
             "\\end{minipage}\n"
             "\\end{minipage}\n"
             "\\newpage\n\n",
             relfilename);

    pair->pos = num;
    // g_list_free_full(res_title, &g_free);
    // g_list_free_full(res_desc, &g_free);
    // g_list_free_full(res_subj, &g_free);
    // g_free(tags);
    d->l = g_list_insert_sorted(d->l, pair, (GCompareFunc)sort_pos);
  } // end of critical block
  dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);

  /* export image to file */
  dt_imageio_export(imgid, filename, format, fdata, high_quality, upscale, FALSE, icc_type, icc_filename, icc_intent,
                    self, sdata, num, total);

  printf("[export_job] exported to `%s'\n", filename);
  dt_control_log(ngettext("%d/%d exported to `%s'", "%d/%d exported to `%s'", num),
                 num, total, filename);
  return 0;
}

static void copy_res(const char *src, const char *dst)
{
  char share[PATH_MAX] = { 0 };
  dt_loc_get_datadir(share, sizeof(share));
  gchar *sourcefile = g_build_filename(share, src, NULL);
  char *content = NULL;
  FILE *fin = g_fopen(sourcefile, "rb");
  FILE *fout = g_fopen(dst, "wb");

  if(fin && fout)
  {
    fseek(fin, 0, SEEK_END);
    size_t end = ftell(fin);
    rewind(fin);
    content = (char *)g_malloc_n(end, sizeof(char));
    if(content == NULL) goto END;
    if(fread(content, sizeof(char), end, fin) != end) goto END;
    if(fwrite(content, sizeof(char), end, fout) != end) goto END;
  }

END:
  if(fout != NULL) fclose(fout);
  if(fin != NULL) fclose(fin);

  g_free(content);
  g_free(sourcefile);
}

void finalize_store(dt_imageio_module_storage_t *self, dt_imageio_module_data_t *dd)
{
  dt_imageio_latex_t *d = (dt_imageio_latex_t *)dd;
  char filename[PATH_MAX] = { 0 };
  snprintf(filename, sizeof(filename), "%s", d->cached_dirname);
  char *c = filename + strlen(filename);

  sprintf(c, "/photobook.cls");
  copy_res("/latex/photobook.cls", filename);

  sprintf(c, "/main.tex");

  const char *title = d->title;

  FILE *f = g_fopen(filename, "wb");
  if(!f) return;
  fprintf(f, "\\newcommand{\\dttitle}{%s}\n"
             "\\newcommand{\\dtauthor}{the author}\n"
             "\\newcommand{\\dtsubject}{the matter}\n"
             "\\newcommand{\\dtkeywords}{this, that}\n"
             "\\documentclass{photobook} %% use [draftmode] for preview\n"
             "\\color{white}\n"
             "\\pagecolor{black}\n"
             "\\begin{document}\n"
             "\\maketitle\n"
             "\\pagestyle{empty}\n",
          title);

  while(d->l)
  {
    pair_t *p = (pair_t *)d->l->data;
    fprintf(f, "%s", p->line);
    free(p);
    d->l = g_list_delete_link(d->l, d->l);
  }

  fprintf(f, "\\end{document}"
             "%% created with %s\n",
          darktable_package_string);
  fclose(f);
}

size_t params_size(dt_imageio_module_storage_t *self)
{
  return sizeof(dt_imageio_latex_t) - 2 * sizeof(void *) - DT_MAX_PATH_FOR_PARAMS;
}

void init(dt_imageio_module_storage_t *self)
{
#ifdef USE_LUA
  dt_lua_register_module_member(darktable.lua_state.state, self, dt_imageio_latex_t, filename,
                                char_path_length);
  dt_lua_register_module_member(darktable.lua_state.state, self, dt_imageio_latex_t, title, char_1024);
#endif
}
void *get_params(dt_imageio_module_storage_t *self)
{
  dt_imageio_latex_t *d = (dt_imageio_latex_t *)calloc(1, sizeof(dt_imageio_latex_t));
  d->vp = NULL;
  d->l = NULL;
  dt_variables_params_init(&d->vp);

  char *text = dt_conf_get_string("plugins/imageio/storage/latex/file_directory");
  g_strlcpy(d->filename, text, sizeof(d->filename));
  g_free(text);

  text = dt_conf_get_string("plugins/imageio/storage/latex/title");
  g_strlcpy(d->title, text, sizeof(d->title));
  g_free(text);

  return d;
}

void free_params(dt_imageio_module_storage_t *self, dt_imageio_module_data_t *params)
{
  if(!params) return;
  dt_imageio_latex_t *d = (dt_imageio_latex_t *)params;
  dt_variables_params_destroy(d->vp);
  free(params);
}

int set_params(dt_imageio_module_storage_t *self, const void *params, const int size)
{
  if(size != self->params_size(self)) return 1;
  dt_imageio_latex_t *d = (dt_imageio_latex_t *)params;
  latex_t *g = (latex_t *)self->gui_data;
  gtk_entry_set_text(GTK_ENTRY(g->entry), d->filename);
  dt_conf_set_string("plugins/imageio/storage/latex/file_directory", d->filename);
  gtk_entry_set_text(GTK_ENTRY(g->title_entry), d->title);
  dt_conf_set_string("plugins/imageio/storage/latex/title", d->title);
  return 0;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
