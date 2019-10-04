/*
    This file is part of darktable,
    copyright (c) 2019 philippe weyland.


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
#include "bauhaus/bauhaus.h"
#include "common/darktable.h"
#include "common/debug.h"
#include "common/imageio_module.h"
#include "control/conf.h"
#include "control/control.h"
#include "control/signal.h"
#include "dtgtk/button.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "gui/gtkentry.h"
#include "libs/lib.h"
#include "libs/lib_api.h"
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <stdlib.h>

typedef enum dt_lib_tagging_cols_t
{
  DT_LIB_EXPORT_METADATA_COL_XMP = 0,
  DT_LIB_EXPORT_METADATA_COL_FORMULA,
  DT_LIB_EXPORT_METADATA_NUM_COLS
} dt_lib_tagging_cols_t;

typedef struct dt_lib_export_metadata_t
{
  GtkTreeView *view;
  GtkListStore *liststore;
  GtkWidget *dialog;
} dt_lib_export_metadata_t;

// TODO replace the following list by a dynamic exiv2 list able to provide info type
// Here are listed only string or XmpText. Can be added as needed.
const char *dt_export_xmp_keys[]
    = { "Xmp.dc.creator", "Xmp.dc.publisher", "Xmp.dc.title", "Xmp.dc.description", "Xmp.dc.rights",
        "Xmp.dc.subject",

        "Xmp.exif.GPSLatitude", "Xmp.exif.GPSLongitude", "Xmp.exif.GPSAltitude",
        "Xmp.exif.DateTimeOriginal",
        "Xmp.exifEX.LensModel",

        "Exif.Image.DateTimeOriginal", "Exif.Image.Make", "Exif.Image.Model", "Exif.Image.Orientation",
        "Exif.Image.Artist", "Exif.Image.Copyright", "Exif.Image.Rating",

        "Exif.GPSInfo.GPSLatitude", "Exif.GPSInfo.GPSLongitude", "Exif.GPSInfo.GPSAltitude",
        "Exif.GPSInfo.GPSLatitudeRef", "Exif.GPSInfo.GPSLongitudeRef", "Exif.GPSInfo.GPSAltitudeRef",
        "Exif.GPSInfo.GPSVersionID",

        "Exif.Photo.DateTimeOriginal", "Exif.Photo.ExposureTime", "Exif.Photo.ShutterSpeedValue",
        "Exif.Photo.FNumber", "Exif.Photo.ApertureValue", "Exif.Photo.ISOSpeedRatings",
        "Exif.Photo.FocalLengthIn35mmFilm", "Exif.Photo.LensModel", "Exif.Photo.Flash",
        "Exif.Photo.WhiteBalance", "Exif.Photo.UserComment", "Exif.Photo.ColorSpace",

        "Xmp.xmp.CreateDate", "Xmp.xmp.CreatorTool", "Xmp.xmp.Identifier", "Xmp.xmp.Label", "Xmp.xmp.ModifyDate",
        "Xmp.xmp.Nickname", "Xmp.xmp.Rating",

        "Iptc.Application2.Subject", "Iptc.Application2.Keywords", "Iptc.Application2.LocationName",
        "Iptc.Application2.City", "Iptc.Application2.SubLocation", "Iptc.Application2.ProvinceState",
        "Iptc.Application2.CountryName", "Iptc.Application2.Copyright", "Iptc.Application2.Caption",
        "Iptc.Application2.Byline", "Iptc.Application2.ObjectName",

        "Xmp.tiff.ImageWidth", "Xmp.tiff.ImageLength", "Xmp.tiff.Artist", "Xmp.tiff.Copyright"
       };
const guint dt_export_xmp_keys_n = G_N_ELEMENTS(dt_export_xmp_keys);

// find a string on the list
static gboolean find_metadata_iter_per_text(GtkTreeModel *model, GtkTreeIter *iter, gint col, const char *text)
{
  if(!text) return FALSE;
  GtkTreeIter it;
  gboolean valid = gtk_tree_model_get_iter_first(model, &it);
  char *name;
  while (valid)
  {
    gtk_tree_model_get(model, &it, col, &name, -1);
    if (g_strcmp0(text, name) == 0)
    {
      if (iter) *iter = it;
      return TRUE;
    }
    valid = gtk_tree_model_iter_next(model, &it);
  }
  return FALSE;
}

static void add_selected_metadata(GtkTreeView *view, dt_lib_export_metadata_t *d)
{
  GtkTreeIter iter;
  GtkTreeModel *model = gtk_tree_view_get_model(view);
  GtkTreeSelection *selection = gtk_tree_view_get_selection(view);
  if(gtk_tree_selection_get_selected(selection, &model, &iter))
  {
    char *tagname;
    gtk_tree_model_get(model, &iter, DT_LIB_EXPORT_METADATA_COL_XMP, &tagname, -1);
    if (!find_metadata_iter_per_text(GTK_TREE_MODEL(d->liststore), NULL, DT_LIB_EXPORT_METADATA_COL_XMP, tagname))
    {
      gtk_list_store_append(d->liststore, &iter);
      gtk_list_store_set(d->liststore, &iter, DT_LIB_EXPORT_METADATA_COL_XMP, tagname, DT_LIB_EXPORT_METADATA_COL_FORMULA, "", -1);
      selection = gtk_tree_view_get_selection(d->view);
      gtk_tree_selection_select_iter(selection, &iter);
    }
    g_free(tagname);
  }
}

static gboolean click_on_metadata_list(GtkWidget *view, GdkEventButton *event, dt_lib_export_metadata_t *d)
{
  if(event->type == GDK_2BUTTON_PRESS && event->button == 1)
  {

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    GtkTreePath *path = NULL;
    // Get tree path for row that was clicked
    if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(view), (gint)event->x, (gint)event->y, &path, NULL, NULL, NULL))
    {
      gtk_tree_selection_select_path(selection, path);
      if(event->type == GDK_2BUTTON_PRESS && event->button == 1)
      {
        add_selected_metadata(GTK_TREE_VIEW(view), d);
        return TRUE;
      }
    }
    gtk_tree_path_free(path);
  }
  return FALSE;
}

static void add_tag_button_clicked(GtkButton *button, dt_lib_export_metadata_t *d)
{
  GtkWidget *dialog = gtk_dialog_new_with_buttons(_("select tag"), GTK_WINDOW(d->dialog), GTK_DIALOG_DESTROY_WITH_PARENT,
                                       _("add"), GTK_RESPONSE_YES, _("done"), GTK_RESPONSE_NONE, NULL);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 300, -1);
  GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
  gtk_container_add(GTK_CONTAINER(area), vbox);

  GtkWidget *w = gtk_scrolled_window_new(NULL, NULL);
  gtk_widget_set_size_request(w, DT_PIXEL_APPLY_DPI(300), DT_PIXEL_APPLY_DPI(300));
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(w), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX(vbox), w, TRUE, TRUE, 0);
  GtkTreeView *view = GTK_TREE_VIEW(gtk_tree_view_new());
  gtk_container_add(GTK_CONTAINER(w), GTK_WIDGET(view));
  gtk_tree_view_set_headers_visible(view, FALSE);
  gtk_widget_set_tooltip_text(GTK_WIDGET(view), _("list of available tags. click 'add' button or double-click on tag to add the selected one"));
  gtk_tree_selection_set_mode(gtk_tree_view_get_selection(view), GTK_SELECTION_SINGLE);
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
  GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("List", renderer, "text", 0, NULL);
  gtk_tree_view_append_column(view, col);
  GtkListStore *liststore = gtk_list_store_new(1, G_TYPE_STRING);
  for(int i=0; i<dt_export_xmp_keys_n; i++)
  {
    GtkTreeIter iter;
    gtk_list_store_append(liststore, &iter);
    gtk_list_store_set(liststore, &iter, DT_LIB_EXPORT_METADATA_COL_XMP, dt_export_xmp_keys[i], -1);
  }
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(liststore), DT_LIB_EXPORT_METADATA_COL_XMP, GTK_SORT_ASCENDING);
  gtk_tree_view_set_model(view, GTK_TREE_MODEL(liststore));
  g_object_unref(liststore);
  g_signal_connect(G_OBJECT(view), "button-press-event", G_CALLBACK(click_on_metadata_list), (gpointer)d);

  #ifdef GDK_WINDOWING_QUARTZ
    dt_osx_disallow_fullscreen(dialog);
  #endif
    gtk_widget_show_all(dialog);
  while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES)
  {
    add_selected_metadata(view, d);
  }
  gtk_widget_destroy(dialog);
}

static void remove_tag_from_list(dt_lib_export_metadata_t *d)
{
  GtkTreeIter iter;
  GtkTreeModel *model = GTK_TREE_MODEL(d->liststore);
  GtkTreeSelection *selection = gtk_tree_view_get_selection(d->view);
  if(gtk_tree_selection_get_selected(selection, &model, &iter))
  {
    gtk_list_store_remove(d->liststore, &iter);
  }
}
static void delete_tag_button_clicked(GtkButton *button, dt_lib_export_metadata_t *d)
{
  remove_tag_from_list(d);
}

static gboolean key_press_on_list(GtkWidget *widget, GdkEventKey *event, dt_lib_export_metadata_t *d)
{
  if(event->type == GDK_KEY_PRESS && event->keyval == GDK_KEY_Delete && !event->state)
  {
    remove_tag_from_list(d);
    return TRUE;
  }
  return FALSE;
}

static void formula_edited(GtkCellRenderer *renderer, gchar *path, gchar *new_text, dt_lib_export_metadata_t *d)
{
  GtkTreeIter iter;
  if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(d->liststore), &iter, path))
    gtk_list_store_set(d->liststore, &iter, DT_LIB_EXPORT_METADATA_COL_FORMULA, new_text, -1);
}

uint32_t dt_lib_export_metadata_default_flags()
{
  const uint32_t flags = DT_META_EXIF | DT_META_METADATA | DT_META_GEOTAG | DT_META_TAG | DT_META_DT_HISTORY;
  return flags;
}

const char flags_keyword[] = "plugins/lighttable/export/metadata_flags";
const char formula_keyword[] = "plugins/lighttable/export/metadata_formula";

char *dt_lib_export_metadata_get_conf()
{
  char *metadata_presets = NULL;
  if(dt_conf_key_exists(flags_keyword))
  {
    metadata_presets = dt_conf_get_string(flags_keyword);
    int i = 0;
    char *conf_keyword = dt_util_dstrcat(NULL,"%s%d", formula_keyword, i);
    while (dt_conf_key_exists(conf_keyword))
    {
      char *nameformula = dt_conf_get_string(conf_keyword);
      g_free(conf_keyword);
      if(nameformula[0])
      {
        char *formula = g_strstr_len(nameformula, strlen(nameformula), ";");
        if(formula)
        {
          formula[0] = '\0';
          formula ++;
          metadata_presets = dt_util_dstrcat(metadata_presets,"\1%s\1%s", nameformula, formula);
        }
      }
      g_free(nameformula);
      i++;
      conf_keyword = dt_util_dstrcat(NULL,"%s%d", formula_keyword, i);
    }
    g_free(conf_keyword);
  }
  else
  {
    metadata_presets = dt_util_dstrcat(NULL, "%x", dt_lib_export_metadata_default_flags());
  }
  return metadata_presets;
}

void dt_lib_export_metadata_set_conf(const char *metadata_presets)
{
  GList *list = dt_util_str_to_glist("\1", metadata_presets);
  int i = 0;
  char *conf_keyword = NULL;
  char *nameformula = NULL;
  if (list)
  {
    char *flags_hexa = list->data;
    dt_conf_set_string(flags_keyword, flags_hexa);
    list = g_list_remove(list, flags_hexa);
    g_free(flags_hexa);
    if (list)
    {
      for (GList *tags = list; tags; tags = g_list_next(tags))
      {
        const char *tagname = (char *)tags->data;
        tags = g_list_next(tags);
        if (!tags) break;
        const char *formula = (char *)tags->data;
        nameformula = dt_util_dstrcat(NULL,"%s;%s", tagname, formula);
        conf_keyword = dt_util_dstrcat(NULL,"%s%d", formula_keyword, i);
        dt_conf_set_string(conf_keyword, nameformula);
        g_free(nameformula);
        g_free(conf_keyword);
        i++;
      }
    }
  }
  else dt_conf_set_string(flags_keyword, "");
  g_list_free_full(list, g_free);

  // clean up deprecated formulas
  conf_keyword = dt_util_dstrcat(NULL,"%s%d", formula_keyword, i);
  while (dt_conf_key_exists(conf_keyword))
  {
    dt_conf_set_string(conf_keyword, "");
    g_free(conf_keyword);
    i++;
    conf_keyword = dt_util_dstrcat(NULL,"%s%d", formula_keyword, i);
  }
  g_free(conf_keyword);
}

char *dt_lib_export_metadata_configuration_dialog(char *metadata_presets, const gboolean ondisk)
{
  dt_lib_export_metadata_t *d = calloc(1, sizeof(dt_lib_export_metadata_t));

  GtkWidget *win = dt_ui_main_window(darktable.gui->ui);
  GtkWidget *dialog = gtk_dialog_new_with_buttons(_("edit metadata exportation"), GTK_WINDOW(win), GTK_DIALOG_DESTROY_WITH_PARENT,
                                       _("save"), GTK_RESPONSE_YES, _("cancel"), GTK_RESPONSE_NONE, NULL);
  d->dialog = dialog;
  gtk_window_set_default_size(GTK_WINDOW(dialog), 300, -1);
  GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

  GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_container_add(GTK_CONTAINER(area), hbox);

  // general info
  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
  gtk_container_add(GTK_CONTAINER(hbox), vbox);
  GtkWidget *label = gtk_label_new(_("general settings"));
  gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 0);
  GtkWidget *vbox2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(vbox), vbox2, FALSE, TRUE, 0);

  GtkWidget *exiftag = gtk_check_button_new_with_label(_("exif data"));
  gtk_widget_set_tooltip_text(exiftag, _("export exif metadata"));
  gtk_box_pack_start(GTK_BOX(vbox2), exiftag, FALSE, TRUE, 0);
  GtkWidget *dtmetadata = gtk_check_button_new_with_label(_("metadata"));
  gtk_widget_set_tooltip_text(dtmetadata, _("export dt xmp metadata (from metadata editor module)"));
  gtk_box_pack_start(GTK_BOX(vbox2), dtmetadata, FALSE, TRUE, 0);

  GtkWidget *calculated;
  if (!ondisk)
  {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(vbox2), box, FALSE, TRUE, 0);
    GtkWidget *vbox3 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(box), vbox3, FALSE, TRUE, 10);
    calculated = gtk_check_button_new_with_label(_("only embedded"));
    gtk_widget_set_tooltip_text(calculated, _("per default the interface sends some (limited) metadata beside the image to remote storage.\n"
        "to avoid this and let only image embedded dt xmp metadata, check this flag.\n"
        "if remote storage doesn't understand dt xmp metadata, you can use calculated metadata instead"));
    gtk_box_pack_start(GTK_BOX(vbox3), calculated, FALSE, TRUE, 0);
  }

  GtkWidget *geotag = gtk_check_button_new_with_label(_("geo tags"));
  gtk_widget_set_tooltip_text(geotag, _("export geo tags"));
  gtk_box_pack_start(GTK_BOX(vbox2), geotag, FALSE, TRUE, 0);
  GtkWidget *dttag = gtk_check_button_new_with_label(_("tags"));
  gtk_widget_set_tooltip_text(dttag, _("export tags (to Xmp.dc.Subject)"));
  gtk_box_pack_start(GTK_BOX(vbox2), dttag, FALSE, TRUE, 0);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(vbox2), box, FALSE, TRUE, 0);
  GtkWidget *vbox3 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(box), vbox3, FALSE, TRUE, 10);
  GtkWidget *private = gtk_check_button_new_with_label(_("private tags"));
  gtk_widget_set_tooltip_text(private, _("export private tags"));
  gtk_box_pack_start(GTK_BOX(vbox3), private, FALSE, TRUE, 0);
  GtkWidget *synonyms = gtk_check_button_new_with_label(_("synonyms"));
  gtk_widget_set_tooltip_text(synonyms, _("export tags synonyms"));
  gtk_box_pack_start(GTK_BOX(vbox3), synonyms, FALSE, TRUE, 0);
  GtkWidget *omithierarchy = gtk_check_button_new_with_label(_("omit hierarchy"));
  gtk_widget_set_tooltip_text(omithierarchy, _("only the last part of the hierarchical tags is included. can be useful if categories are not used"));
  gtk_box_pack_start(GTK_BOX(vbox3), omithierarchy, FALSE, TRUE, 0);

  GtkWidget *hierarchical = gtk_check_button_new_with_label(_("hierarchical tags"));
  gtk_widget_set_tooltip_text(hierarchical, _("export hierarchical tags (to Xmp.lr.Hierarchical Subject)"));
  gtk_box_pack_start(GTK_BOX(vbox2), hierarchical, FALSE, TRUE, 0);
  GtkWidget *dthistory = gtk_check_button_new_with_label(_("develop history"));
  gtk_widget_set_tooltip_text(dthistory, _("export dt development data (recovery purpose in case of loss of database or xmp file)"));
  gtk_box_pack_start(GTK_BOX(vbox2), dthistory, FALSE, TRUE, 0);

  // specific rules
  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
  gtk_container_add(GTK_CONTAINER(hbox), vbox);
  label = gtk_label_new(_("per metadata settings"));
  gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 0);

  GtkWidget *w = gtk_scrolled_window_new(NULL, NULL);
  gtk_widget_set_size_request(w, DT_PIXEL_APPLY_DPI(450), DT_PIXEL_APPLY_DPI(100));
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(w), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX(vbox), w, TRUE, TRUE, 0);
  GtkTreeView *view = GTK_TREE_VIEW(gtk_tree_view_new());
  d->view = view;
  gtk_container_add(GTK_CONTAINER(w), GTK_WIDGET(view));
  gtk_widget_set_tooltip_text(GTK_WIDGET(view), _("list of available tags"));
  gtk_tree_selection_set_mode(gtk_tree_view_get_selection(view), GTK_SELECTION_SINGLE);
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
  GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("redefined tag", renderer, "text", 0, NULL);
  gtk_tree_view_append_column(view, col);
  renderer = gtk_cell_renderer_text_new();
  g_object_set(renderer, "editable", TRUE, NULL);
  g_signal_connect(G_OBJECT(renderer), "edited", G_CALLBACK(formula_edited), (gpointer)d);
  col = gtk_tree_view_column_new_with_attributes("formula", renderer, "text", 1, NULL);
  gtk_tree_view_append_column(view, col);
  char *tooltip_text = dt_gtkentry_build_completion_tooltip_text(
                        _("list of calculated metadata\n"
                        "if formula is empty, the corresponding metadata is removed from exported file\n"
                        "otherwise the corresponding metadata is calculated and added to exported file\n"
                        "click on formula cell to edit. recognized variables:"),
                        dt_gtkentry_get_default_path_compl_list());
  gtk_widget_set_tooltip_text(GTK_WIDGET(view), tooltip_text);
  g_free(tooltip_text);
  g_signal_connect(G_OBJECT(view), "key_press_event", G_CALLBACK(key_press_on_list), (gpointer)d);

  GtkListStore *liststore = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
  d->liststore = liststore;
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(liststore), DT_LIB_EXPORT_METADATA_COL_XMP, GTK_SORT_ASCENDING);
  gtk_tree_view_set_model(view, GTK_TREE_MODEL(liststore));
  g_object_unref(liststore);
  GList *list = dt_util_str_to_glist("\1", metadata_presets);
  int32_t flags = 0;
  if (list)
  {
    char *flags_hexa = list->data;
    flags = strtol(flags_hexa, NULL, 16);
    list = g_list_remove(list, flags_hexa);
    g_free(flags_hexa);
    if (list)
    {
      for (GList *tags = list; tags; tags = g_list_next(tags))
      {
        GtkTreeIter iter;
        const char *tagname = (char *)tags->data;
        tags = g_list_next(tags);
        if (!tags) break;
        const char *formula = (char *)tags->data;
        gtk_list_store_append(d->liststore, &iter);
        gtk_list_store_set(d->liststore, &iter, DT_LIB_EXPORT_METADATA_COL_XMP, tagname,
          DT_LIB_EXPORT_METADATA_COL_FORMULA, formula, -1);
      }
    }
  }
  g_list_free_full(list, g_free);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(exiftag), flags & DT_META_EXIF);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dtmetadata), flags & DT_META_METADATA);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(geotag), flags & DT_META_GEOTAG);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dttag), flags & DT_META_TAG);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(private), flags & DT_META_PRIVATE_TAG);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(synonyms), flags & DT_META_SYNONYMS_TAG);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(hierarchical), flags & DT_META_HIERARCHICAL_TAG);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dthistory), flags & DT_META_DT_HISTORY);
  if (!ondisk)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(calculated), flags & DT_META_CALCULATED);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(omithierarchy), flags & DT_META_OMIT_HIERARCHY);

  box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(vbox), box, FALSE, TRUE, 0);

  GtkWidget *button = dtgtk_button_new(dtgtk_cairo_paint_plus_simple, CPF_STYLE_FLAT | CPF_DO_NOT_USE_BORDER, NULL);
  gtk_widget_set_tooltip_text(button, _("add an output metadata tag"));
  gtk_box_pack_end(GTK_BOX(box), button, FALSE, TRUE, 0);
  g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(add_tag_button_clicked), (gpointer)d);

  button = dtgtk_button_new(dtgtk_cairo_paint_minus_simple, CPF_STYLE_FLAT | CPF_DO_NOT_USE_BORDER, NULL);
  gtk_widget_set_tooltip_text(button, _("delete metadata tag"));
  gtk_box_pack_end(GTK_BOX(box), button, FALSE, TRUE, 0);
  g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(delete_tag_button_clicked), (gpointer)d);

#ifdef GDK_WINDOWING_QUARTZ
  dt_osx_disallow_fullscreen(dialog);
#endif
  gtk_widget_show_all(dialog);

  char *newlist = metadata_presets;
  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES)
  {
    const gint newflags = (
                    (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(exiftag)) ? DT_META_EXIF : 0) |
                    (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dtmetadata)) ? DT_META_METADATA : 0) |
                    (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(geotag)) ? DT_META_GEOTAG : 0) |
                    (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dttag)) ? DT_META_TAG : 0) |
                    (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(private)) ? DT_META_PRIVATE_TAG : 0) |
                    (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(synonyms)) ? DT_META_SYNONYMS_TAG : 0) |
                    (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(hierarchical)) ? DT_META_HIERARCHICAL_TAG : 0) |
                    (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dthistory)) ? DT_META_DT_HISTORY : 0) |
                    (!ondisk  ? (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(calculated)) ? DT_META_CALCULATED : 0) : 0) |
                    (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(omithierarchy)) ? DT_META_OMIT_HIERARCHY : 0)
                    );

    newlist = dt_util_dstrcat(NULL,"%x", newflags);
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(d->liststore), &iter);
    while(valid)
    {
      char *tagname, *formula;
      gtk_tree_model_get(GTK_TREE_MODEL(d->liststore), &iter, DT_LIB_EXPORT_METADATA_COL_XMP, &tagname,
          DT_LIB_EXPORT_METADATA_COL_FORMULA, &formula, -1);
      // metadata presets are stored into a single string with '\1' as a separator
      newlist = dt_util_dstrcat(newlist,"\1%s\1%s", tagname, formula);
      g_free(tagname);
      g_free(formula);
      valid = gtk_tree_model_iter_next (GTK_TREE_MODEL(d->liststore), &iter);
    }
    g_free(metadata_presets);
    dt_lib_export_metadata_set_conf(newlist);
  }
  gtk_widget_destroy(dialog);
  free(d);
  return newlist;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
