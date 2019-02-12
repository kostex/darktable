/*
    This file is part of darktable,
    copyright (c) 2011 johannes hanika.

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
#include "control/conf.h"
#include "control/control.h"
#include "develop/blend.h"
#include "develop/imageop.h"
#include "develop/masks.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "iop/iop_api.h"
#include <gtk/gtk.h>
#include <stdlib.h>


// this is the version of the modules parameters,
// and includes version information about compile-time dt
DT_MODULE_INTROSPECTION(2, dt_iop_spots_params_t)

typedef struct dt_iop_spots_params_t
{
  int clone_id[64];
  int clone_algo[64];
} dt_iop_spots_params_t;

typedef struct dt_iop_spots_gui_data_t
{
  GtkLabel *label;
  GtkWidget *bt_path, *bt_circle, *bt_ellipse;
} dt_iop_spots_gui_data_t;

typedef struct dt_iop_spots_params_t dt_iop_spots_data_t;


// this returns a translatable name
const char *name()
{
  return _("spot removal");
}

int default_group()
{
  return IOP_GROUP_CORRECT;
}

int flags()
{
  return IOP_FLAGS_SUPPORTS_BLENDING | IOP_FLAGS_NO_MASKS;
}

int legacy_params(dt_iop_module_t *self, const void *const old_params, const int old_version,
                  void *new_params, const int new_version)
{
  if(old_version == 1 && new_version == 2)
  {
    typedef struct dt_iop_spots_v1_t
    {
      float x, y;
      float xc, yc;
      float radius;
    } dt_iop_spots_v1_t;
    typedef struct dt_iop_spots_params_v1_t
    {
      int num_spots;
      dt_iop_spots_v1_t spot[32];
    } dt_iop_spots_params_v1_t;

    dt_iop_spots_params_v1_t *o = (dt_iop_spots_params_v1_t *)old_params;
    dt_iop_spots_params_t *n = (dt_iop_spots_params_t *)new_params;
    dt_iop_spots_params_t *d = (dt_iop_spots_params_t *)self->default_params;

    *n = *d; // start with a fresh copy of default parameters
    for(int i = 0; i < o->num_spots; i++)
    {
      // we have to register a new circle mask
      dt_masks_form_t *form = dt_masks_create(DT_MASKS_CIRCLE | DT_MASKS_CLONE);

      // spots v1 was before raw orientation changes
      form->version = 1;

      dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *)(malloc(sizeof(dt_masks_point_circle_t)));
      circle->center[0] = o->spot[i].x;
      circle->center[1] = o->spot[i].y;
      circle->radius = o->spot[i].radius;
      circle->border = 0.0f;
      form->points = g_list_append(form->points, circle);
      form->source[0] = o->spot[i].xc;
      form->source[1] = o->spot[i].yc;

      // adapt for raw orientation changes
      dt_masks_legacy_params(self->dev, form, form->version, dt_masks_version());

      dt_masks_gui_form_save_creation(self->dev, self, form, NULL);

      // and add it to the module params
      n->clone_id[i] = form->formid;
      n->clone_algo[i] = 1;
    }
    return 0;
  }
  return 1;
}

static void _resynch_params(struct dt_iop_module_t *self)
{
  dt_iop_spots_params_t *p = (dt_iop_spots_params_t *)self->params;
  dt_develop_blend_params_t *bp = self->blend_params;

  // we create 2 new buffers
  int nid[64] = { 0 };
  int nalgo[64] = { 2 };

  // we go through all forms in blend params
  dt_masks_form_t *grp = dt_masks_get_from_id(darktable.develop, bp->mask_id);
  if(grp && (grp->type & DT_MASKS_GROUP))
  {
    GList *forms = g_list_first(grp->points);
    int i = 0;
    while((i < 64) && forms)
    {
      dt_masks_point_group_t *grpt = (dt_masks_point_group_t *)forms->data;
      nid[i] = grpt->formid;
      for(int j = 0; j < 64; j++)
      {
        if(p->clone_id[j] == nid[i])
        {
          nalgo[i] = p->clone_algo[j];
          break;
        }
      }
      i++;
      forms = g_list_next(forms);
    }
  }

  // we reaffect params
  for(int i = 0; i < 64; i++)
  {
    p->clone_algo[i] = nalgo[i];
    p->clone_id[i] = nid[i];
  }
}


static void _reset_form_creation(GtkWidget *widget, dt_iop_module_t *self)
{
  dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g->bt_path)) ||
      gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g->bt_circle)) ||
      gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g->bt_ellipse)))
  {
    // we unset the creation mode
    dt_masks_change_form_gui(NULL);
  }
  if (widget != g->bt_path) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_path), FALSE);
  if (widget != g->bt_circle) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_circle), FALSE);
  if (widget != g->bt_ellipse) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_ellipse), FALSE);
}

static gboolean _add_path(GtkWidget *widget, GdkEventButton *e, dt_iop_module_t *self)
{
  _reset_form_creation(widget, self);
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) return FALSE;
  // we want to be sure that the iop has focus
  dt_iop_request_focus(self);
  // we create the new form
  dt_masks_form_t *form = dt_masks_create(DT_MASKS_PATH | DT_MASKS_CLONE);
  dt_masks_change_form_gui(form);
  darktable.develop->form_gui->creation = TRUE;
  darktable.develop->form_gui->creation_module = self;
  dt_control_queue_redraw_center();
  return FALSE;
}
static gboolean _add_circle(GtkWidget *widget, GdkEventButton *e, dt_iop_module_t *self)
{
  _reset_form_creation(widget, self);
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) return FALSE;
  // we want to be sure that the iop has focus
  dt_iop_request_focus(self);
  // we create the new form
  dt_masks_form_t *spot = dt_masks_create(DT_MASKS_CIRCLE | DT_MASKS_CLONE);
  dt_masks_change_form_gui(spot);
  darktable.develop->form_gui->creation = TRUE;
  darktable.develop->form_gui->creation_module = self;
  dt_control_queue_redraw_center();
  return FALSE;
}
static gboolean _add_ellipse(GtkWidget *widget, GdkEventButton *e, dt_iop_module_t *self)
{
  _reset_form_creation(widget, self);
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) return FALSE;
  // we want to be sure that the iop has focus
  dt_iop_request_focus(self);
  // we create the new form
  dt_masks_form_t *spot = dt_masks_create(DT_MASKS_ELLIPSE | DT_MASKS_CLONE);
  dt_masks_change_form_gui(spot);
  darktable.develop->form_gui->creation = TRUE;
  darktable.develop->form_gui->creation_module = self;
  dt_control_queue_redraw_center();
  return FALSE;
}


static gboolean masks_form_is_in_roi(dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece,
                                     dt_masks_form_t *form, const dt_iop_roi_t *roi_in,
                                     const dt_iop_roi_t *roi_out)
{
  // we get the area for the form
  int fl, ft, fw, fh;

  if(!dt_masks_get_area(self, piece, form, &fw, &fh, &fl, &ft)) return FALSE;

  // is the form outside of the roi?
  fw *= roi_in->scale, fh *= roi_in->scale, fl *= roi_in->scale, ft *= roi_in->scale;
  if(ft >= roi_out->y + roi_out->height || ft + fh <= roi_out->y || fl >= roi_out->x + roi_out->width
     || fl + fw <= roi_out->x)
    return FALSE;

  return TRUE;
}

void modify_roi_out(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece, dt_iop_roi_t *roi_out,
                    const dt_iop_roi_t *roi_in)
{
  *roi_out = *roi_in;
}

// needed if mask dest is in roi and mask src is not
void modify_roi_in(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece,
                   const dt_iop_roi_t *roi_out, dt_iop_roi_t *roi_in)
{
  *roi_in = *roi_out;

  int roir = roi_in->width + roi_in->x;
  int roib = roi_in->height + roi_in->y;
  int roix = roi_in->x;
  int roiy = roi_in->y;

  // dt_iop_spots_params_t *d = (dt_iop_spots_params_t *)piece->data;
  dt_develop_blend_params_t *bp = self->blend_params;

  // We iterate through all spots or polygons
  dt_masks_form_t *grp = dt_masks_get_from_id_ext(piece->pipe->forms, bp->mask_id);
  if(grp && (grp->type & DT_MASKS_GROUP))
  {
    GList *forms = g_list_first(grp->points);
    while(forms)
    {
      dt_masks_point_group_t *grpt = (dt_masks_point_group_t *)forms->data;
      // we get the spot
      dt_masks_form_t *form = dt_masks_get_from_id_ext(piece->pipe->forms, grpt->formid);
      if(form)
      {
        // if the form is outside the roi, we just skip it
        if(!masks_form_is_in_roi(self, piece, form, roi_in, roi_out))
        {
          forms = g_list_next(forms);
          continue;
        }

        // we get the area for the source
        int fl, ft, fw, fh;

        if(!dt_masks_get_source_area(self, piece, form, &fw, &fh, &fl, &ft))
        {
          forms = g_list_next(forms);
          continue;
        }
        fw *= roi_in->scale, fh *= roi_in->scale, fl *= roi_in->scale, ft *= roi_in->scale;

        // we enlarge the roi if needed
        roiy = fminf(ft, roiy);
        roix = fminf(fl, roix);
        roir = fmaxf(fl + fw, roir);
        roib = fmaxf(ft + fh, roib);
      }
      forms = g_list_next(forms);
    }
  }

  // now we set the values
  const float scwidth = piece->buf_in.width * roi_in->scale, scheight = piece->buf_in.height * roi_in->scale;
  roi_in->x = CLAMP(roix, 0, scwidth - 1);
  roi_in->y = CLAMP(roiy, 0, scheight - 1);
  roi_in->width = CLAMP(roir - roi_in->x, 1, scwidth + .5f - roi_in->x);
  roi_in->height = CLAMP(roib - roi_in->y, 1, scheight + .5f - roi_in->y);
}

static void masks_point_denormalize(dt_dev_pixelpipe_iop_t *piece, const dt_iop_roi_t *roi,
                                    const float *points, size_t points_count, float *new)
{
  const float scalex = piece->pipe->iwidth * roi->scale, scaley = piece->pipe->iheight * roi->scale;

  for(size_t i = 0; i < points_count * 2; i += 2)
  {
    new[i] = points[i] * scalex;
    new[i + 1] = points[i + 1] * scaley;
  }
}

static int masks_point_calc_delta(dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece,
                                  const dt_iop_roi_t *roi, const float *target, const float *source, int *dx,
                                  int *dy)
{
  float points[4];
  masks_point_denormalize(piece, roi, target, 1, points);
  masks_point_denormalize(piece, roi, source, 1, points + 2);

  int res = dt_dev_distort_transform_plus(self->dev, piece->pipe, 0, self->priority, points, 2);
  if(!res) return res;

  *dx = points[0] - points[2];
  *dy = points[1] - points[3];

  return res;
}

static int masks_get_delta(dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, const dt_iop_roi_t *roi,
                           dt_masks_form_t *form, int *dx, int *dy)
{
  int res = 0;

  if(form->type & DT_MASKS_PATH)
  {
    dt_masks_point_path_t *pt = (dt_masks_point_path_t *)form->points->data;

    res = masks_point_calc_delta(self, piece, roi, pt->corner, form->source, dx, dy);
  }
  else if(form->type & DT_MASKS_CIRCLE)
  {
    dt_masks_point_circle_t *pt = (dt_masks_point_circle_t *)form->points->data;

    res = masks_point_calc_delta(self, piece, roi, pt->center, form->source, dx, dy);
  }
  else if(form->type & DT_MASKS_ELLIPSE)
  {
    dt_masks_point_ellipse_t *pt = (dt_masks_point_ellipse_t *)form->points->data;

    res = masks_point_calc_delta(self, piece, roi, pt->center, form->source, dx, dy);
  }

  return res;
}

void _process(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, const float *const in,
              float *const out, const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out, const int ch)
{
  dt_iop_spots_params_t *d = (dt_iop_spots_params_t *)piece->data;
  dt_develop_blend_params_t *bp = self->blend_params;

// we don't modify most of the image:
#ifdef _OPENMP
#pragma omp parallel for schedule(static) default(none)
#endif
  for(int k = 0; k < roi_out->height; k++)
  {
    float *outb = out + (size_t)ch * k * roi_out->width;
    const float *inb = in + (size_t)ch * roi_in->width * (k + roi_out->y - roi_in->y)
                       + ch * (roi_out->x - roi_in->x);
    memcpy(outb, inb, sizeof(float) * roi_out->width * ch);
  }

  // iterate through all forms
  dt_masks_form_t *grp = dt_masks_get_from_id_ext(piece->pipe->forms, bp->mask_id);
  int pos = 0;
  if(grp && (grp->type & DT_MASKS_GROUP))
  {
    GList *forms = g_list_first(grp->points);
    while((pos < 64) && forms)
    {
      dt_masks_point_group_t *grpt = (dt_masks_point_group_t *)forms->data;
      // we get the spot
      dt_masks_form_t *form = dt_masks_get_from_id_ext(piece->pipe->forms, grpt->formid);
      if(!form)
      {
        forms = g_list_next(forms);
        pos++;
        continue;
      }

      // if the form is outside the roi, we just skip it
      if(!masks_form_is_in_roi(self, piece, form, roi_in, roi_out))
      {
        forms = g_list_next(forms);
        pos++;
        continue;
      }

      if(d->clone_algo[pos] == 1 && (form->type & DT_MASKS_CIRCLE))
      {
        dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *)g_list_nth_data(form->points, 0);

        float points[4];
        masks_point_denormalize(piece, roi_in, circle->center, 1, points);
        masks_point_denormalize(piece, roi_in, form->source, 1, points + 2);

        if(!dt_dev_distort_transform_plus(self->dev, piece->pipe, 0, self->priority, points, 2))
        {
          forms = g_list_next(forms);
          pos++;
          continue;
        }

        // convert from world space:
        float radius10[2] = { circle->radius, circle->radius };
        float radf[2];
        masks_point_denormalize(piece, roi_in, radius10, 1, radf);

        const int rad = MIN(radf[0], radf[1]);
        const int posx = points[0] - rad;
        const int posy = points[1] - rad;
        const int posx_source = points[2] - rad;
        const int posy_source = points[3] - rad;
        const int dx = posx - posx_source;
        const int dy = posy - posy_source;
        const int fw = 2 * rad, fh = 2 * rad;

        float *filter = malloc((2 * rad + 1) * sizeof(float));

        if(rad > 0)
        {
          for(int k = -rad; k <= rad; k++)
          {
            const float kk = 1.0f - fabsf(k / (float)rad);
            filter[rad + k] = kk * kk * (3.0f - 2.0f * kk);
          }
        }
        else
        {
          filter[0] = 1.0f;
        }

        for(int yy = posy; yy < posy + fh; yy++)
        {
          // we test if we are inside roi_out
          if(yy < roi_out->y || yy >= roi_out->y + roi_out->height) continue;
          // we test if the source point is inside roi_in
          if(yy - dy < roi_in->y || yy - dy >= roi_in->y + roi_in->height) continue;
          for(int xx = posx; xx < posx + fw; xx++)
          {
            // we test if we are inside roi_out
            if(xx < roi_out->x || xx >= roi_out->x + roi_out->width) continue;
            // we test if the source point is inside roi_in
            if(xx - dx < roi_in->x || xx - dx >= roi_in->x + roi_in->width) continue;

            const float f = filter[xx - posx + 1] * filter[yy - posy + 1];
            for(int c = 0; c < ch; c++)
              out[ch * ((size_t)roi_out->width * (yy - roi_out->y) + xx - roi_out->x) + c]
                  = out[ch * ((size_t)roi_out->width * (yy - roi_out->y) + xx - roi_out->x) + c] * (1.0f - f)
                    + in[ch * ((size_t)roi_in->width * (yy - posy + posy_source - roi_in->y) + xx - posx
                              + posx_source - roi_in->x) + c] * f;
          }
        }

        free(filter);
      }
      else
      {
        // we get the mask
        float *mask = NULL;
        int posx, posy, width, height;
        dt_masks_get_mask(self, piece, form, &mask, &width, &height, &posx, &posy);
        int fts = posy * roi_in->scale, fhs = height * roi_in->scale, fls = posx * roi_in->scale,
            fws = width * roi_in->scale;
        int dx = 0, dy = 0;

        // now we search the delta with the source
        if(!masks_get_delta(self, piece, roi_in, form, &dx, &dy))
        {
          forms = g_list_next(forms);
          pos++;
          free(mask);

          continue;
        }

        if(dx != 0 || dy != 0)
        {
          // now we do the pixel clone
          for(int yy = fts + 1; yy < fts + fhs - 1; yy++)
          {
            // we test if we are inside roi_out
            if(yy < roi_out->y || yy >= roi_out->y + roi_out->height) continue;
            // we test if the source point is inside roi_in
            if(yy - dy < roi_in->y || yy - dy >= roi_in->y + roi_in->height) continue;
            for(int xx = fls + 1; xx < fls + fws - 1; xx++)
            {
              // we test if we are inside roi_out
              if(xx < roi_out->x || xx >= roi_out->x + roi_out->width) continue;
              // we test if the source point is inside roi_in
              if(xx - dx < roi_in->x || xx - dx >= roi_in->x + roi_in->width) continue;

              float f = mask[((int)((yy - fts) / roi_in->scale)) * width
                             + (int)((xx - fls) / roi_in->scale)] * grpt->opacity;

              for(int c = 0; c < ch; c++)
                out[ch * ((size_t)roi_out->width * (yy - roi_out->y) + xx - roi_out->x) + c]
                    = out[ch * ((size_t)roi_out->width * (yy - roi_out->y) + xx - roi_out->x) + c] * (1.0f - f)
                      + in[ch * ((size_t)roi_in->width * (yy - dy - roi_in->y) + xx - dx - roi_in->x) + c] * f;
            }
          }
        }
        free(mask);
      }
      pos++;
      forms = g_list_next(forms);
    }
  }
}

void process(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, const void *const i, void *const o,
             const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  const float *in = (float *)i;
  float *out = (float *)o;
  _process(self, piece, in, out, roi_in, roi_out, piece->colors);
}

void distort_mask(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, const float *const in,
                  float *const out, const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  _process(self, piece, in, out, roi_in, roi_out, 1);
}

/** init, cleanup, commit to pipeline */
void init(dt_iop_module_t *module)
{
  // we don't need global data:
  module->data = NULL; // malloc(sizeof(dt_iop_spots_global_data_t));
  module->params = calloc(1, sizeof(dt_iop_spots_params_t));
  module->default_params = calloc(1, sizeof(dt_iop_spots_params_t));
  // our module is disabled by default
  // by default:
  module->default_enabled = 0;
  module->priority = 171; // module order created by iop_dependencies.py, do not edit!
  module->params_size = sizeof(dt_iop_spots_params_t);
  module->gui_data = NULL;
  // init defaults:
  dt_iop_spots_params_t tmp = (dt_iop_spots_params_t){ { 0 }, { 2 } };

  memcpy(module->params, &tmp, sizeof(dt_iop_spots_params_t));
  memcpy(module->default_params, &tmp, sizeof(dt_iop_spots_params_t));
}

void cleanup(dt_iop_module_t *module)
{
  free(module->params);
  module->params = NULL;
  free(module->data); // just to be sure
  module->data = NULL;
}

void gui_focus(struct dt_iop_module_t *self, gboolean in)
{
  // dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;
  if(self->enabled)
  {
    if(in)
    {
      // got focus, show all shapes
      dt_masks_set_edit_mode(self, DT_MASKS_EDIT_FULL);
    }
    else
    {
      // lost focus, hide all shapes
      if (darktable.develop->form_gui->creation && darktable.develop->form_gui->creation_module == self)
        dt_masks_change_form_gui(NULL);
      dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_path), FALSE);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_circle), FALSE);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_ellipse), FALSE);
      dt_masks_set_edit_mode(self, DT_MASKS_EDIT_OFF);
    }
  }
}

/** commit is the synch point between core and gui, so it copies params to pipe data. */
void commit_params(struct dt_iop_module_t *self, dt_iop_params_t *params, dt_dev_pixelpipe_t *pipe,
                   dt_dev_pixelpipe_iop_t *piece)
{
  memcpy(piece->data, params, sizeof(dt_iop_spots_params_t));
}

void init_pipe(struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  piece->data = malloc(sizeof(dt_iop_spots_data_t));
  self->commit_params(self, self->default_params, pipe, piece);
}

void cleanup_pipe(struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  free(piece->data);
  piece->data = NULL;
}

/** gui callbacks, these are needed. */
void gui_update(dt_iop_module_t *self)
{
  _resynch_params(self);
  dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;
  // update clones count
  dt_masks_form_t *grp = dt_masks_get_from_id(self->dev, self->blend_params->mask_id);
  guint nb = 0;
  if(grp && (grp->type & DT_MASKS_GROUP)) nb = g_list_length(grp->points);
  gchar *str = g_strdup_printf("%d", nb);
  gtk_label_set_text(g->label, str);
  g_free(str);
  // update buttons status
  int b1 = 0, b2 = 0, b3 = 0;
  if(self->dev->form_gui && self->dev->form_visible && self->dev->form_gui->creation
     && self->dev->form_gui->creation_module == self)
  {
    if(self->dev->form_visible->type & DT_MASKS_CIRCLE)
      b1 = 1;
    else if(self->dev->form_visible->type & DT_MASKS_PATH)
      b2 = 1;
    else if(self->dev->form_visible->type & DT_MASKS_ELLIPSE)
      b3 = 1;
  }
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_circle), b1);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_path), b2);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_ellipse), b3);
}

void gui_init(dt_iop_module_t *self)
{
  const int bs = DT_PIXEL_APPLY_DPI(14);
  self->gui_data = malloc(sizeof(dt_iop_spots_gui_data_t));
  dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;

  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  dt_gui_add_help_link(self->widget, dt_get_help_url(self->op));
  GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  GtkWidget *label = gtk_label_new(_("number of strokes:"));
  gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 0);
  g->label = GTK_LABEL(gtk_label_new("-1"));
  gtk_widget_set_tooltip_text(hbox, _("click on a shape and drag on canvas.\nuse the mouse wheel "
                                      "to adjust size.\nright click to remove a shape."));

  g->bt_path = dtgtk_togglebutton_new(dtgtk_cairo_paint_masks_path, CPF_STYLE_FLAT | CPF_DO_NOT_USE_BORDER, NULL);
  g_signal_connect(G_OBJECT(g->bt_path), "button-press-event", G_CALLBACK(_add_path), self);
  gtk_widget_set_tooltip_text(g->bt_path, _("add path"));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_path), FALSE);
  gtk_widget_set_size_request(GTK_WIDGET(g->bt_path), bs, bs);
  gtk_box_pack_end(GTK_BOX(hbox), g->bt_path, FALSE, FALSE, 0);

  g->bt_ellipse
      = dtgtk_togglebutton_new(dtgtk_cairo_paint_masks_ellipse, CPF_STYLE_FLAT | CPF_DO_NOT_USE_BORDER, NULL);
  g_signal_connect(G_OBJECT(g->bt_ellipse), "button-press-event", G_CALLBACK(_add_ellipse), self);
  gtk_widget_set_tooltip_text(g->bt_ellipse, _("add ellipse"));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_ellipse), FALSE);
  gtk_widget_set_size_request(GTK_WIDGET(g->bt_ellipse), bs, bs);
  gtk_box_pack_end(GTK_BOX(hbox), g->bt_ellipse, FALSE, FALSE, 0);

  g->bt_circle
      = dtgtk_togglebutton_new(dtgtk_cairo_paint_masks_circle, CPF_STYLE_FLAT | CPF_DO_NOT_USE_BORDER, NULL);
  g_signal_connect(G_OBJECT(g->bt_circle), "button-press-event", G_CALLBACK(_add_circle), self);
  gtk_widget_set_tooltip_text(g->bt_circle, _("add circle"));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_circle), FALSE);
  gtk_widget_set_size_request(GTK_WIDGET(g->bt_circle), bs, bs);
  gtk_box_pack_end(GTK_BOX(hbox), g->bt_circle, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(g->label), FALSE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), hbox, TRUE, TRUE, 0);
}

void gui_reset(struct dt_iop_module_t *self)
{
  // hide the previous masks
  dt_masks_reset_form_gui();
}

void gui_cleanup(dt_iop_module_t *self)
{
  // dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;
  // nothing else necessary, gtk will clean up the labels

  free(self->gui_data);
  self->gui_data = NULL;
}

void init_key_accels (dt_iop_module_so_t *module)
{
  dt_accel_register_iop (module, TRUE, NC_("accel", "spot circle tool"),   0, 0);
  dt_accel_register_iop (module, TRUE, NC_("accel", "spot ellipse tool"),   0, 0);
  dt_accel_register_iop (module, TRUE, NC_("accel", "spot path tool"),     0, 0);
  dt_accel_register_iop (module, TRUE, NC_("accel", "spot show or hide"),  0, 0);
}

static gboolean _add_circle_key_accel(GtkAccelGroup *accel_group, GObject *acceleratable, guint keyval,
                                      GdkModifierType modifier, gpointer data)
{
  dt_iop_module_t *module = (dt_iop_module_t *)data;
  const dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *) module->gui_data;
  _add_circle(GTK_WIDGET(g->bt_circle), NULL, module);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_circle), TRUE);
  return TRUE;
}

static gboolean _add_ellipse_key_accel(GtkAccelGroup *accel_group, GObject *acceleratable, guint keyval,
                                       GdkModifierType modifier, gpointer data)
{
  dt_iop_module_t *module = (dt_iop_module_t *)data;
  const dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *) module->gui_data;
  _add_ellipse(GTK_WIDGET(g->bt_ellipse), NULL, module);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_ellipse), TRUE);
  return TRUE;
}

static gboolean _add_path_key_accel(GtkAccelGroup *accel_group, GObject *acceleratable, guint keyval,
                                    GdkModifierType modifier, gpointer data)
{
  dt_iop_module_t *module = (dt_iop_module_t *)data;
  const dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *) module->gui_data;
  _add_path(GTK_WIDGET(g->bt_path), NULL, module);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_path), TRUE);
  return TRUE;
}

static gboolean _show_hide_key_accel(GtkAccelGroup *accel_group, GObject *acceleratable, guint keyval,
                                     GdkModifierType modifier, gpointer data)
{
  dt_iop_module_t *module = (dt_iop_module_t *)data;
  dt_masks_set_edit_mode(module, module->dev->form_gui->edit_mode == DT_MASKS_EDIT_FULL ? DT_MASKS_EDIT_OFF : DT_MASKS_EDIT_FULL);
  return TRUE;
}

void connect_key_accels (dt_iop_module_t *module)
{
  GClosure *closure;

  closure = g_cclosure_new(G_CALLBACK(_add_circle_key_accel), (gpointer)module, NULL);
  dt_accel_connect_iop (module, "spot circle tool", closure);

  closure = g_cclosure_new(G_CALLBACK(_add_ellipse_key_accel), (gpointer)module, NULL);
  dt_accel_connect_iop (module, "spot ellipse tool", closure);

  closure = g_cclosure_new(G_CALLBACK(_add_path_key_accel), (gpointer)module, NULL);
  dt_accel_connect_iop (module, "spot path tool", closure);

  closure = g_cclosure_new(G_CALLBACK(_show_hide_key_accel), (gpointer)module, NULL);
  dt_accel_connect_iop (module, "spot show or hide", closure);
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
