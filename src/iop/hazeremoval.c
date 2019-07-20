/*
    This file is part of darktable,
    copyright (c) 2017-2019 Heiko Bauke.

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

/*
    This module implements automatic single-image haze removal as
    described by K. He et al. in

    * Kaiming He, Jian Sun, and Xiaoou Tang, "Single Image Haze
      Removal Using Dark Channel Prior," IEEE Transactions on Pattern
      Analysis and Machine Intelligence, vol. 33, no. 12,
      pp. 2341-2353, Dec. 2011. DOI: 10.1109/TPAMI.2010.168

    * K. He, J. Sun, and X. Tang, "Guided Image Filtering," Lecture
      Notes in Computer Science, pp. 1-14, 2010. DOI:
      10.1007/978-3-642-15549-9_1
*/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "bauhaus/bauhaus.h"
#include "common/darktable.h"
#include "common/guided_filter.h"
#include "develop/imageop.h"
#include "develop/imageop_math.h"
#include "gui/gtk.h"
#include "iop/iop_api.h"

#ifdef HAVE_OPENCL
#include "common/opencl.h"
#endif

#include <float.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

//----------------------------------------------------------------------
// implement the module api
//----------------------------------------------------------------------

DT_MODULE_INTROSPECTION(1, dt_iop_hazeremoval_params_t)

typedef float rgb_pixel[3];

typedef struct dt_iop_hazeremoval_params_t
{
  float strength;
  float distance;
} dt_iop_hazeremoval_params_t;

// types  dt_iop_hazeremoval_params_t and dt_iop_hazeremoval_data_t are
// equal, thus no commit_params function needs to be implemented
typedef dt_iop_hazeremoval_params_t dt_iop_hazeremoval_data_t;

typedef struct dt_iop_hazeremoval_gui_data_t
{
  GtkWidget *strength;
  GtkWidget *distance;
  rgb_pixel A0;
  float distance_max;
  uint64_t hash;
  dt_pthread_mutex_t lock;
} dt_iop_hazeremoval_gui_data_t;

typedef struct dt_iop_hazeremoval_global_data_t
{
  int kernel_hazeremoval_transision_map;
  int kernel_hazeremoval_box_min_x;
  int kernel_hazeremoval_box_min_y;
  int kernel_hazeremoval_box_max_x;
  int kernel_hazeremoval_box_max_y;
  int kernel_hazeremoval_dehaze;
} dt_iop_hazeremoval_global_data_t;


const char *name()
{
  return _("haze removal");
}


int flags()
{
  return IOP_FLAGS_INCLUDE_IN_STYLES | IOP_FLAGS_SUPPORTS_BLENDING;
}


int default_group()
{
  return IOP_GROUP_CORRECT;
}


int default_colorspace(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  return iop_cs_rgb;
}


void init_pipe(struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  piece->data = calloc(1, sizeof(dt_iop_hazeremoval_data_t));
  self->commit_params(self, self->default_params, pipe, piece);
}


void cleanup_pipe(struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  free(piece->data);
  piece->data = NULL;
}


void init_global(dt_iop_module_so_t *self)
{
  dt_iop_hazeremoval_global_data_t *gd = malloc(sizeof(*gd));
  const int program = 27; // hazeremoval.cl, from programs.conf
  gd->kernel_hazeremoval_transision_map = dt_opencl_create_kernel(program, "hazeremoval_transision_map");
  gd->kernel_hazeremoval_box_min_x = dt_opencl_create_kernel(program, "hazeremoval_box_min_x");
  gd->kernel_hazeremoval_box_min_y = dt_opencl_create_kernel(program, "hazeremoval_box_min_y");
  gd->kernel_hazeremoval_box_max_x = dt_opencl_create_kernel(program, "hazeremoval_box_max_x");
  gd->kernel_hazeremoval_box_max_y = dt_opencl_create_kernel(program, "hazeremoval_box_max_y");
  gd->kernel_hazeremoval_dehaze = dt_opencl_create_kernel(program, "hazeremoval_dehaze");
  self->data = gd;
}


void cleanup_global(dt_iop_module_so_t *self)
{
  dt_iop_hazeremoval_global_data_t *gd = self->data;
  dt_opencl_free_kernel(gd->kernel_hazeremoval_transision_map);
  dt_opencl_free_kernel(gd->kernel_hazeremoval_box_min_x);
  dt_opencl_free_kernel(gd->kernel_hazeremoval_box_min_y);
  dt_opencl_free_kernel(gd->kernel_hazeremoval_box_max_x);
  dt_opencl_free_kernel(gd->kernel_hazeremoval_box_max_y);
  dt_opencl_free_kernel(gd->kernel_hazeremoval_dehaze);
  free(self->data);
  self->data = NULL;
}


void init(dt_iop_module_t *self)
{
  self->params = calloc(1, sizeof(dt_iop_hazeremoval_params_t));
  self->default_params = calloc(1, sizeof(dt_iop_hazeremoval_params_t));
  self->default_enabled = 0;
  self->params_size = sizeof(dt_iop_hazeremoval_params_t);
  self->gui_data = NULL;
  dt_iop_hazeremoval_params_t tmp = (dt_iop_hazeremoval_params_t){ 0.5f, 0.25f };
  memcpy(self->params, &tmp, sizeof(dt_iop_hazeremoval_params_t));
  memcpy(self->default_params, &tmp, sizeof(dt_iop_hazeremoval_params_t));
}


void cleanup(dt_iop_module_t *self)
{
  free(self->params);
  self->params = NULL;
}


static void strength_callback(GtkWidget *w, dt_iop_module_t *self)
{
  if(self->dt->gui->reset) return;
  dt_iop_hazeremoval_params_t *p = self->params;
  p->strength = dt_bauhaus_slider_get(w);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}


static void distance_callback(GtkWidget *w, dt_iop_module_t *self)
{
  if(self->dt->gui->reset) return;
  dt_iop_hazeremoval_params_t *p = (dt_iop_hazeremoval_params_t *)self->params;
  p->distance = dt_bauhaus_slider_get(w);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}


void gui_update(struct dt_iop_module_t *self)
{
  dt_iop_hazeremoval_gui_data_t *g = (dt_iop_hazeremoval_gui_data_t *)self->gui_data;
  dt_iop_hazeremoval_params_t *p = (dt_iop_hazeremoval_params_t *)self->params;
  dt_bauhaus_slider_set(g->strength, p->strength);
  dt_bauhaus_slider_set(g->distance, p->distance);

  dt_pthread_mutex_lock(&g->lock);
  g->distance_max = NAN;
  g->A0[0] = NAN;
  g->A0[1] = NAN;
  g->A0[2] = NAN;
  g->hash = 0;
  dt_pthread_mutex_unlock(&g->lock);
}


void gui_init(dt_iop_module_t *self)
{
  self->gui_data = malloc(sizeof(dt_iop_hazeremoval_gui_data_t));
  dt_iop_hazeremoval_gui_data_t *g = (dt_iop_hazeremoval_gui_data_t *)self->gui_data;
  dt_iop_hazeremoval_params_t *p = (dt_iop_hazeremoval_params_t *)self->params;

  dt_pthread_mutex_init(&g->lock, NULL);
  g->distance_max = NAN;
  g->A0[0] = NAN;
  g->A0[1] = NAN;
  g->A0[2] = NAN;
  g->hash = 0;

  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, DT_BAUHAUS_SPACE);
  dt_gui_add_help_link(self->widget, dt_get_help_url(self->op));

  g->strength = dt_bauhaus_slider_new_with_range(self, -1, 1, 0.01, p->strength, 2);
  dt_bauhaus_widget_set_label(g->strength, NULL, _("strength"));
  gtk_widget_set_tooltip_text(g->strength, _("amount of haze reduction"));
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(g->strength), TRUE, TRUE, 0);
  g_signal_connect(G_OBJECT(g->strength), "value-changed", G_CALLBACK(strength_callback), self);

  g->distance = dt_bauhaus_slider_new_with_range(self, 0, 1, 0.005, p->distance, 3);
  dt_bauhaus_widget_set_label(g->distance, NULL, _("distance"));
  gtk_widget_set_tooltip_text(g->distance, _("limit haze removal up to a specific spatial depth"));
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(g->distance), TRUE, TRUE, 0);
  g_signal_connect(G_OBJECT(g->distance), "value-changed", G_CALLBACK(distance_callback), self);
}


void gui_cleanup(dt_iop_module_t *self)
{
  dt_iop_hazeremoval_gui_data_t *g = (dt_iop_hazeremoval_gui_data_t *)self->gui_data;
  dt_pthread_mutex_destroy(&g->lock);
  free(self->gui_data);
  self->gui_data = NULL;
}

//----------------------------------------------------------------------
// module local functions and structures required by process function
//----------------------------------------------------------------------

typedef struct tile
{
  int left, right, lower, upper;
} tile;


typedef struct rgb_image
{
  float *data;
  int width, height, stride;
} rgb_image;


typedef struct const_rgb_image
{
  const float *data;
  int width, height, stride;
} const_rgb_image;


typedef struct gray_image
{
  float *data;
  int width, height;
} gray_image;


// allocate space for 1-component image of size width x height
static inline gray_image new_gray_image(int width, int height)
{
  return (gray_image){ dt_alloc_align(64, sizeof(float) * width * height), width, height };
}


// free space for 1-component image
static inline void free_gray_image(gray_image *img_p)
{
  dt_free_align(img_p->data);
  img_p->data = NULL;
}


// copy 1-component image img1 to img2
static inline void copy_gray_image(gray_image img1, gray_image img2)
{
  memcpy(img2.data, img1.data, sizeof(float) * img1.width * img1.height);
}


// minimum of two integers
static inline int min_i(int a, int b)
{
  return a < b ? a : b;
}


// maximum of two integers
static inline int max_i(int a, int b)
{
  return a > b ? a : b;
}


// swap the two floats that the pointers point to
static inline void pointer_swap_f(float *a, float *b)
{
  float t = *a;
  *a = *b;
  *b = t;
}


// calculate the one-dimensional moving maximum over a window of size 2*w+1
// input array x has stride 1, output array y has stride stride_y
static inline void box_max_1d(int N, const float *x, float *y, size_t stride_y, int w)
{
  float m = -(INFINITY);
  for(int i = 0, i_end = min_i(w + 1, N); i < i_end; i++) m = fmaxf(x[i], m);
  for(int i = 0; i < N; i++)
  {
    y[i * stride_y] = m;
    if(i - w >= 0 && x[i - w] == m)
    {
      m = -(INFINITY);
      for(int j = max_i(i - w + 1, 0), j_end = min_i(i + w + 2, N); j < j_end; j++) m = fmaxf(x[j], m);
    }
    if(i + w + 1 < N) m = fmaxf(x[i + w + 1], m);
  }
}


// calculate the two-dimensional moving maximum over a box of size (2*w+1) x (2*w+1)
// does the calculation in-place if input and output images are identical
static void box_max(const gray_image img1, const gray_image img2, const int w)
{
  gray_image img2_bak;
  if(img1.data == img2.data)
  {
#ifdef _OPENMP
#pragma omp parallel default(none) \
    dt_omp_firstprivate(img1, img2, w) \
    private(img2_bak)
#endif
    {
      img2_bak = new_gray_image(img2.width, 1);
#ifdef _OPENMP
#pragma omp for
#endif
      for(int i1 = 0; i1 < img2.height; i1++)
      {
        memcpy(img2_bak.data, img2.data + (size_t)i1 * img2.width, sizeof(float) * img2.width);
        box_max_1d(img2.width, img2_bak.data, img2.data + (size_t)i1 * img2.width, 1, w);
      }
      free_gray_image(&img2_bak);
    }
  }
  else
  {
#ifdef _OPENMP
#pragma omp parallel default(none) \
    dt_omp_firstprivate(img1, img2, w) \
    private(img2_bak)
#endif
    {
#ifdef _OPENMP
#pragma omp for
#endif
      for(int i1 = 0; i1 < img1.height; i1++)
        box_max_1d(img1.width, img1.data + (size_t)i1 * img1.width, img2.data + (size_t)i1 * img2.width, 1, w);
    }
  }
#ifdef _OPENMP
#pragma omp parallel default(none) \
  dt_omp_firstprivate(img1, img2, w) \
  private(img2_bak)
#endif
  {
    img2_bak = new_gray_image(1, img2.height);
#ifdef _OPENMP
#pragma omp for
#endif
    for(int i0 = 0; i0 < img1.width; i0++)
    {
      for(int i1 = 0; i1 < img1.height; i1++) img2_bak.data[i1] = img2.data[i0 + (size_t)i1 * img2.width];
      box_max_1d(img1.height, img2_bak.data, img2.data + i0, img1.width, w);
    }
    free_gray_image(&img2_bak);
  }
}


// calculate the one-dimensional moving minimum over a window of size 2*w+1
// input array x has stride 1, output array y has stride stride_y
static inline void box_min_1d(int N, const float *x, float *y, size_t stride_y, int w)
{
  float m = INFINITY;
  for(int i = 0, i_end = min_i(w + 1, N); i < i_end; i++) m = fminf(x[i], m);
  for(int i = 0; i < N; i++)
  {
    y[i * stride_y] = m;
    if(i - w >= 0 && x[i - w] == m)
    {
      m = INFINITY;
      for(int j = max_i(i - w + 1, 0), j_end = min_i(i + w + 2, N); j < j_end; j++) m = fminf(x[j], m);
    }
    if(i + w + 1 < N) m = fminf(x[i + w + 1], m);
  }
}


// calculate the two-dimensional moving minimum over a box of size (2*w+1) x (2*w+1)
// does the calculation in-place if input and output images are identical
static void box_min(const gray_image img1, const gray_image img2, const int w)
{
  gray_image img2_bak;
  if(img1.data == img2.data)
  {
#ifdef _OPENMP
#pragma omp parallel default(none) \
    dt_omp_firstprivate(img1, img2, w) \
    private(img2_bak)
#endif
    {
      img2_bak = new_gray_image(img2.width, 1);
#ifdef _OPENMP
#pragma omp for
#endif
      for(int i1 = 0; i1 < img2.height; i1++)
      {
        memcpy(img2_bak.data, img2.data + (size_t)i1 * img2.width, sizeof(float) * img2.width);
        box_min_1d(img2.width, img2_bak.data, img2.data + (size_t)i1 * img2.width, 1, w);
      }
      free_gray_image(&img2_bak);
    }
  }
  else
  {
#ifdef _OPENMP
#pragma omp parallel default(none) \
    dt_omp_firstprivate(img1, img2, w) \
    private(img2_bak)
#endif
    {
#ifdef _OPENMP
#pragma omp for
#endif
      for(int i1 = 0; i1 < img1.height; i1++)
        box_min_1d(img1.width, img1.data + (size_t)i1 * img1.width, img2.data + (size_t)i1 * img2.width, 1, w);
    }
  }
#ifdef _OPENMP
#pragma omp parallel default(none) \
  dt_omp_firstprivate(img1, img2, w) \
  private(img2_bak)
#endif
  {
    img2_bak = new_gray_image(1, img2.height);
#ifdef _OPENMP
#pragma omp for schedule(static)
#endif
    for(int i0 = 0; i0 < img1.width; i0++)
    {
      for(int i1 = 0; i1 < img1.height; i1++) img2_bak.data[i1] = img2.data[i0 + (size_t)i1 * img2.width];
      box_min_1d(img1.height, img2_bak.data, img2.data + i0, img1.width, w);
    }
    free_gray_image(&img2_bak);
  }
}


// calculate the dark channel (minimal color component over a box of size (2*w+1) x (2*w+1) )
static void dark_channel(const const_rgb_image img1, const gray_image img2, const int w)
{
  const size_t size = (size_t)img1.height * img1.width;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(img1, img2, size) \
  schedule(static)
#endif
  for(size_t i = 0; i < size; i++)
  {
    const float *pixel = img1.data + i * img1.stride;
    float m = pixel[0];
    m = fminf(pixel[1], m);
    m = fminf(pixel[2], m);
    img2.data[i] = m;
  }
  box_min(img2, img2, w);
}


// calculate the transition map
static void transition_map(const const_rgb_image img1, const gray_image img2, const int w, const float *const A0,
                           const float strength)
{
  const size_t size = (size_t)img1.height * img1.width;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(A0, img1, img2, size, strength) \
  schedule(static)
#endif
  for(size_t i = 0; i < size; i++)
  {
    const float *pixel = img1.data + i * img1.stride;
    float m = pixel[0] / A0[0];
    m = fminf(pixel[1] / A0[1], m);
    m = fminf(pixel[2] / A0[2], m);
    img2.data[i] = 1.f - m * strength;
  }
  box_max(img2, img2, w);
}


// partition the array [first, last) using the pivot value val, i.e.,
// reorder the elements in the range [first, last) in such a way that
// all elements that are less than the pivot precede the elements
// which are larger or equal the pivot
static float *partition(float *first, float *last, float val)
{
  for(; first != last; ++first)
  {
    if(!((*first) < val)) break;
  }
  if(first == last) return first;
  for(float *i = first + 1; i != last; ++i)
  {
    if((*i) < val)
    {
      pointer_swap_f(i, first);
      ++first;
    }
  }
  return first;
}


// quick select algorithm, arranges the range [first, last) such that
// the element pointed to by nth is the same as the element that would
// be in that position if the entire range [first, last) had been
// sorted, additionally, none of the elements in the range [nth, last)
// is less than any of the elements in the range [first, nth)
void quick_select(float *first, float *nth, float *last)
{
  if(first == last) return;
  for(;;)
  {
    // select pivot by median of three heuristic for better performance
    float *p1 = first;
    float *pivot = first + (last - first) / 2;
    float *p3 = last - 1;
    if(!(*p1 < *pivot)) pointer_swap_f(p1, pivot);
    if(!(*p1 < *p3)) pointer_swap_f(p1, p3);
    if(!(*pivot < *p3)) pointer_swap_f(pivot, p3);
    pointer_swap_f(pivot, last - 1); // move pivot to end
    partition(first, last - 1, *(last - 1));
    pointer_swap_f(last - 1, pivot); // move pivot to its final place
    if(nth == pivot)
      break;
    else if(nth < pivot)
      last = pivot;
    else
      first = pivot + 1;
  }
}


// calculate diffusive ambient light and the maximal depth in the image
// depth is estimated by the local amount of haze and given in units of the
// characteristic haze depth, i.e., the distance over which object light is
// reduced by the factor exp(-1)
static float ambient_light(const const_rgb_image img, int w1, rgb_pixel *pA0)
{
  const float dark_channel_quantil = 0.95f; // quantil for determining the most hazy pixels
  const float bright_quantil = 0.95f; // quantil for determining the brightest pixels among the most hazy pixels
  const int width = img.width;
  const int height = img.height;
  const size_t size = (size_t)width * height;
  // calculate dark channel, which is an estimate for local amount of haze
  gray_image dark_ch = new_gray_image(width, height);
  dark_channel(img, dark_ch, w1);
  // determine the brightest pixels among the most hazy pixels
  gray_image bright_hazy = new_gray_image(width, height);
  // first determine the most hazy pixels
  copy_gray_image(dark_ch, bright_hazy);
  size_t p = (size_t)(size * dark_channel_quantil);
  quick_select(bright_hazy.data, bright_hazy.data + p, bright_hazy.data + size);
  const float crit_haze_level = bright_hazy.data[p];
  size_t N_most_hazy = 0;
  for(size_t i = 0; i < size; i++)
    if(dark_ch.data[i] >= crit_haze_level)
    {
      const float *pixel_in = img.data + i * img.stride;
      // next line prevents parallelization via OpenMP
      bright_hazy.data[N_most_hazy] = pixel_in[0] + pixel_in[1] + pixel_in[2];
      N_most_hazy++;
    }
  p = (size_t)(N_most_hazy * bright_quantil);
  quick_select(bright_hazy.data, bright_hazy.data + p, bright_hazy.data + N_most_hazy);
  const float crit_brightness = bright_hazy.data[p];
  free_gray_image(&bright_hazy);
  // average over the brightest pixels among the most hazy pixels to
  // estimate the diffusive ambient light
  float A0_r = 0, A0_g = 0, A0_b = 0;
  size_t N_bright_hazy = 0;
  const float *const data = dark_ch.data;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(crit_brightness, crit_haze_level, data, img, size) \
  schedule(static) \
  reduction(+ : N_bright_hazy, A0_r, A0_g, A0_b)
#endif
  for(size_t i = 0; i < size; i++)
  {
    const float *pixel_in = img.data + i * img.stride;
    if((data[i] >= crit_haze_level) && (pixel_in[0] + pixel_in[1] + pixel_in[2] >= crit_brightness))
    {
      A0_r += pixel_in[0];
      A0_g += pixel_in[1];
      A0_b += pixel_in[2];
      N_bright_hazy++;
    }
  }
  if(N_bright_hazy > 0)
  {
    A0_r /= N_bright_hazy;
    A0_g /= N_bright_hazy;
    A0_b /= N_bright_hazy;
  }
  (*pA0)[0] = A0_r;
  (*pA0)[1] = A0_g;
  (*pA0)[2] = A0_b;
  free_gray_image(&dark_ch);
  // for almost haze free images it may happen that crit_haze_level=0, this means
  // there is a very large image depth, in this case a large number is returned, that
  // is small enough to avoid overflow in later processing
  // the critical haze level is at dark_channel_quantil (not 100%) to be insensitive
  // to extreme outliners, compensate for that by some factor slightly larger than
  // unity when calculating the maximal image depth
  return crit_haze_level > 0 ? -1.125f * logf(crit_haze_level) : logf(FLT_MAX) / 2; // return the maximal depth
}


void process(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, const void *const ivoid,
             void *const ovoid, const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  dt_iop_hazeremoval_gui_data_t *g = self->gui_data;
  dt_iop_hazeremoval_params_t *d = piece->data;

  const int ch = piece->colors;
  const int width = roi_in->width;
  const int height = roi_in->height;
  const size_t size = (size_t)width * height;
  const int w1 = 6; // window size (positive integer) for determining the dark channel and the transition map
  const int w2 = 9; // window size (positive integer) for the guided filter

  // module parameters
  const float strength = d->strength; // strength of haze removal
  const float distance = d->distance; // maximal distance from camera to remove haze
  const float eps = sqrtf(0.025f);    // regularization parameter for guided filter

  const const_rgb_image img_in = (const_rgb_image){ ivoid, width, height, ch };
  const rgb_image img_out = (rgb_image){ ovoid, width, height, ch };

  // estimate diffusive ambient light and image depth
  rgb_pixel A0;
  A0[0] = NAN;
  A0[1] = NAN;
  A0[2] = NAN;
  float distance_max = NAN;

  // hazeremoval module needs the color and the haziness (which yields
  // distance_max) of the most hazy region of the image.  In pixelpipe
  // FULL we can not reliably get this value as the pixelpipe might
  // only see part of the image (region of interest).  Therefore, we
  // try to get A0 and distance_max from the PREVIEW pixelpipe which
  // luckily stores it for us.
  if(self->dev->gui_attached && g && piece->pipe->type == DT_DEV_PIXELPIPE_FULL)
  {
    dt_pthread_mutex_lock(&g->lock);
    const uint64_t hash = g->hash;
    dt_pthread_mutex_unlock(&g->lock);
    // Note that the case 'hash == 0' on first invocation in a session
    // implies that g->distance_max is NAN, which initiates special
    // handling below to avoid inconsistent results.  In all other
    // cases we make sure that the preview pipe has left us with
    // proper readings for distance_max and A0.  If data are not yet
    // there we need to wait (with timeout).
    if(hash != 0
       && !dt_dev_sync_pixelpipe_hash(self->dev, piece->pipe, self->iop_order, DT_DEV_TRANSFORM_DIR_BACK_INCL,
                                      &g->lock, &g->hash))
      dt_control_log(_("inconsistent output"));
    dt_pthread_mutex_lock(&g->lock);
    A0[0] = g->A0[0];
    A0[1] = g->A0[1];
    A0[2] = g->A0[2];
    distance_max = g->distance_max;
    dt_pthread_mutex_unlock(&g->lock);
  }
  // In all other cases we calculate distance_max and A0 here.
  if(isnan(distance_max)) distance_max = ambient_light(img_in, w1, &A0);
  // PREVIEW pixelpipe stores values.
  if(self->dev->gui_attached && g && piece->pipe->type == DT_DEV_PIXELPIPE_PREVIEW)
  {
    uint64_t hash = dt_dev_hash_plus(self->dev, piece->pipe, self->iop_order, DT_DEV_TRANSFORM_DIR_BACK_INCL);
    dt_pthread_mutex_lock(&g->lock);
    g->A0[0] = A0[0];
    g->A0[1] = A0[1];
    g->A0[2] = A0[2];
    g->distance_max = distance_max;
    g->hash = hash;
    dt_pthread_mutex_unlock(&g->lock);
  }

  // calculate the transition map
  gray_image trans_map = new_gray_image(width, height);
  transition_map(img_in, trans_map, w1, A0, strength);

  // refine the transition map
  box_min(trans_map, trans_map, w1);
  gray_image trans_map_filtered = new_gray_image(width, height);
  // apply guided filter with no clipping
  guided_filter(img_in.data, trans_map.data, trans_map_filtered.data, width, height, ch, w2, eps, 1.f, -FLT_MAX,
                FLT_MAX);

  // finally, calculate the haze-free image
  const float t_min
      = fmaxf(expf(-distance * distance_max), 1.f / 1024); // minimum allowed value for transition map
  const float *const c_A0 = A0;
  const gray_image c_trans_map_filtered = trans_map_filtered;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(c_A0, c_trans_map_filtered, img_in, img_out, size, t_min) \
  schedule(static)
#endif
  for(size_t i = 0; i < size; i++)
  {
    float t = fmaxf(c_trans_map_filtered.data[i], t_min);
    const float *pixel_in = img_in.data + i * img_in.stride;
    float *pixel_out = img_out.data + i * img_out.stride;
    pixel_out[0] = (pixel_in[0] - c_A0[0]) / t + c_A0[0];
    pixel_out[1] = (pixel_in[1] - c_A0[1]) / t + c_A0[1];
    pixel_out[2] = (pixel_in[2] - c_A0[2]) / t + c_A0[2];
  }

  free_gray_image(&trans_map);
  free_gray_image(&trans_map_filtered);

  if(piece->pipe->mask_display & DT_DEV_PIXELPIPE_DISPLAY_MASK)
    dt_iop_alpha_copy(ivoid, ovoid, roi_out->width, roi_out->height);
}

#ifdef HAVE_OPENCL

// calculate diffusive ambient light and the maximal depth in the image
// depth is estimated by the local amount of haze and given in units of the
// characteristic haze depth, i.e., the distance over which object light is
// reduced by the factor exp(-1)
// some parts of the calculation are not suitable for a parallel implementation,
// thus we copy data to host memory fall back to a cpu routine
static float ambient_light_cl(struct dt_iop_module_t *self, int devid, cl_mem img, int w1, rgb_pixel *pA0)
{
  const int width = dt_opencl_get_image_width(img);
  const int height = dt_opencl_get_image_height(img);
  const int element_size = dt_opencl_get_image_element_size(img);
  float *in = dt_alloc_align(64, (size_t)width * height * element_size);
  int err = dt_opencl_read_host_from_device(devid, in, img, width, height, element_size);
  if(err != CL_SUCCESS) goto error;
  const const_rgb_image img_in = (const_rgb_image){ in, width, height, element_size / sizeof(float) };
  const float max_depth = ambient_light(img_in, w1, pA0);
  dt_free_align(in);
  return max_depth;
error:
  dt_print(DT_DEBUG_OPENCL, "[hazeremoval, ambient_light_cl] unknown error: %d\n", err);
  dt_free_align(in);
  return 0.f;
}


static int box_min_cl(struct dt_iop_module_t *self, int devid, cl_mem in, cl_mem out, const int w)
{
  dt_iop_hazeremoval_global_data_t *gd = self->global_data;
  const int width = dt_opencl_get_image_width(in);
  const int height = dt_opencl_get_image_height(in);
  void *temp = dt_opencl_alloc_device(devid, width, height, (int)sizeof(float));

  const int kernel_x = gd->kernel_hazeremoval_box_min_x;
  dt_opencl_set_kernel_arg(devid, kernel_x, 0, sizeof(width), &width);
  dt_opencl_set_kernel_arg(devid, kernel_x, 1, sizeof(height), &height);
  dt_opencl_set_kernel_arg(devid, kernel_x, 2, sizeof(in), &in);
  dt_opencl_set_kernel_arg(devid, kernel_x, 3, sizeof(temp), &temp);
  dt_opencl_set_kernel_arg(devid, kernel_x, 4, sizeof(w), &w);
  const size_t sizes_x[] = { 1, ROUNDUPWD(height) };
  int err = dt_opencl_enqueue_kernel_2d(devid, kernel_x, sizes_x);
  if(err != CL_SUCCESS) goto error;

  const int kernel_y = gd->kernel_hazeremoval_box_min_y;
  dt_opencl_set_kernel_arg(devid, kernel_y, 0, sizeof(width), &width);
  dt_opencl_set_kernel_arg(devid, kernel_y, 1, sizeof(height), &height);
  dt_opencl_set_kernel_arg(devid, kernel_y, 2, sizeof(temp), &temp);
  dt_opencl_set_kernel_arg(devid, kernel_y, 3, sizeof(out), &out);
  dt_opencl_set_kernel_arg(devid, kernel_y, 4, sizeof(w), &w);
  const size_t sizes_y[] = { ROUNDUPWD(width), 1 };
  err = dt_opencl_enqueue_kernel_2d(devid, kernel_y, sizes_y);

error:
  if(err != CL_SUCCESS) dt_print(DT_DEBUG_OPENCL, "[hazeremoval, box_min_cl] unknown error: %d\n", err);
  dt_opencl_release_mem_object(temp);
  return err;
}


static int box_max_cl(struct dt_iop_module_t *self, int devid, cl_mem in, cl_mem out, const int w)
{
  dt_iop_hazeremoval_global_data_t *gd = self->global_data;
  const int width = dt_opencl_get_image_width(in);
  const int height = dt_opencl_get_image_height(in);
  void *temp = dt_opencl_alloc_device(devid, width, height, (int)sizeof(float));

  const int kernel_x = gd->kernel_hazeremoval_box_max_x;
  dt_opencl_set_kernel_arg(devid, kernel_x, 0, sizeof(width), &width);
  dt_opencl_set_kernel_arg(devid, kernel_x, 1, sizeof(height), &height);
  dt_opencl_set_kernel_arg(devid, kernel_x, 2, sizeof(in), &in);
  dt_opencl_set_kernel_arg(devid, kernel_x, 3, sizeof(temp), &temp);
  dt_opencl_set_kernel_arg(devid, kernel_x, 4, sizeof(w), &w);
  const size_t sizes_x[] = { 1, ROUNDUPWD(height) };
  int err = dt_opencl_enqueue_kernel_2d(devid, kernel_x, sizes_x);
  if(err != CL_SUCCESS) goto error;

  const int kernel_y = gd->kernel_hazeremoval_box_max_y;
  dt_opencl_set_kernel_arg(devid, kernel_y, 0, sizeof(width), &width);
  dt_opencl_set_kernel_arg(devid, kernel_y, 1, sizeof(height), &height);
  dt_opencl_set_kernel_arg(devid, kernel_y, 2, sizeof(temp), &temp);
  dt_opencl_set_kernel_arg(devid, kernel_y, 3, sizeof(out), &out);
  dt_opencl_set_kernel_arg(devid, kernel_y, 4, sizeof(w), &w);
  const size_t sizes_y[] = { ROUNDUPWD(width), 1 };
  err = dt_opencl_enqueue_kernel_2d(devid, kernel_y, sizes_y);

error:
  if(err != CL_SUCCESS) dt_print(DT_DEBUG_OPENCL, "[hazeremoval, box_max_cl] unknown error: %d\n", err);
  dt_opencl_release_mem_object(temp);
  return err;
}


static int transition_map_cl(struct dt_iop_module_t *self, int devid, cl_mem img1, cl_mem img2, const int w1,
                             const float strength, const float *const A0)
{
  dt_iop_hazeremoval_global_data_t *gd = self->global_data;
  const int width = dt_opencl_get_image_width(img1);
  const int height = dt_opencl_get_image_height(img1);

  const int kernel = gd->kernel_hazeremoval_transision_map;
  dt_opencl_set_kernel_arg(devid, kernel, 0, sizeof(width), &width);
  dt_opencl_set_kernel_arg(devid, kernel, 1, sizeof(height), &height);
  dt_opencl_set_kernel_arg(devid, kernel, 2, sizeof(img1), &img1);
  dt_opencl_set_kernel_arg(devid, kernel, 3, sizeof(img2), &img2);
  dt_opencl_set_kernel_arg(devid, kernel, 4, sizeof(strength), &strength);
  dt_opencl_set_kernel_arg(devid, kernel, 5, sizeof(A0[0]), &A0[0]);
  dt_opencl_set_kernel_arg(devid, kernel, 6, sizeof(A0[1]), &A0[1]);
  dt_opencl_set_kernel_arg(devid, kernel, 7, sizeof(A0[2]), &A0[2]);
  size_t sizes[2] = { ROUNDUPWD(width), ROUNDUPWD(height) };
  int err = dt_opencl_enqueue_kernel_2d(devid, kernel, sizes);
  if(err != CL_SUCCESS)
  {
    dt_print(DT_DEBUG_OPENCL, "[hazeremoval, transition_map_cl] unknown error: %d\n", err);
    return err;
  }
  err = box_max_cl(self, devid, img2, img2, w1);

  return err;
}


static int dehaze_cl(struct dt_iop_module_t *self, int devid, cl_mem img_in, cl_mem trans_map, cl_mem img_out,
                     const float t_min, const float *const A0)
{
  dt_iop_hazeremoval_global_data_t *gd = self->global_data;
  const int width = dt_opencl_get_image_width(img_in);
  const int height = dt_opencl_get_image_height(img_in);

  const int kernel = gd->kernel_hazeremoval_dehaze;
  dt_opencl_set_kernel_arg(devid, kernel, 0, sizeof(width), &width);
  dt_opencl_set_kernel_arg(devid, kernel, 1, sizeof(height), &height);
  dt_opencl_set_kernel_arg(devid, kernel, 2, sizeof(img_in), &img_in);
  dt_opencl_set_kernel_arg(devid, kernel, 3, sizeof(trans_map), &trans_map);
  dt_opencl_set_kernel_arg(devid, kernel, 4, sizeof(img_out), &img_out);
  dt_opencl_set_kernel_arg(devid, kernel, 5, sizeof(t_min), &t_min);
  dt_opencl_set_kernel_arg(devid, kernel, 6, sizeof(A0[0]), &A0[0]);
  dt_opencl_set_kernel_arg(devid, kernel, 7, sizeof(A0[1]), &A0[1]);
  dt_opencl_set_kernel_arg(devid, kernel, 8, sizeof(A0[2]), &A0[2]);
  size_t sizes[2] = { ROUNDUPWD(width), ROUNDUPWD(height) };
  int err = dt_opencl_enqueue_kernel_2d(devid, kernel, sizes);
  if(err != CL_SUCCESS) dt_print(DT_DEBUG_OPENCL, "[hazeremoval, dehaze_cl] unknown error: %d\n", err);
  return err;
}


int process_cl(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, cl_mem img_in, cl_mem img_out,
               const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  dt_iop_hazeremoval_gui_data_t *g = self->gui_data;
  dt_iop_hazeremoval_params_t *d = piece->data;

  const int ch = piece->colors;
  const int devid = piece->pipe->devid;
  const int width = roi_in->width;
  const int height = roi_in->height;
  const int w1 = 6; // window size (positive integer) for determining the dark channel and the transition map
  const int w2 = 9; // window size (positive integer) for the guided filter

  // module parameters
  const float strength = d->strength; // strength of haze removal
  const float distance = d->distance; // maximal distance from camera to remove haze
  const float eps = sqrtf(0.025f);    // regularization parameter for guided filter

  // estimate diffusive ambient light and image depth
  rgb_pixel A0;
  A0[0] = NAN;
  A0[1] = NAN;
  A0[2] = NAN;
  float distance_max = NAN;

  // hazeremoval module needs the color and the haziness (which yields
  // distance_max) of the most hazy region of the image.  In pixelpipe
  // FULL we can not reliably get this value as the pixelpipe might
  // only see part of the image (region of interest).  Therefore, we
  // try to get A0 and distance_max from the PREVIEW pixelpipe which
  // luckily stores it for us.
  if(self->dev->gui_attached && g && piece->pipe->type == DT_DEV_PIXELPIPE_FULL)
  {
    dt_pthread_mutex_lock(&g->lock);
    const uint64_t hash = g->hash;
    dt_pthread_mutex_unlock(&g->lock);
    // Note that the case 'hash == 0' on first invocation in a session
    // implies that g->distance_max is NAN, which initiates special
    // handling below to avoid inconsistent results.  In all other
    // cases we make sure that the preview pipe has left us with
    // proper readings for distance_max and A0.  If data are not yet
    // there we need to wait (with timeout).
    if(hash != 0
       && !dt_dev_sync_pixelpipe_hash(self->dev, piece->pipe, self->iop_order, DT_DEV_TRANSFORM_DIR_BACK_INCL,
                                      &g->lock, &g->hash))
      dt_control_log(_("inconsistent output"));
    dt_pthread_mutex_lock(&g->lock);
    A0[0] = g->A0[0];
    A0[1] = g->A0[1];
    A0[2] = g->A0[2];
    distance_max = g->distance_max;
    dt_pthread_mutex_unlock(&g->lock);
  }
  // In all other cases we calculate distance_max and A0 here.
  if(isnan(distance_max)) distance_max = ambient_light_cl(self, devid, img_in, w1, &A0);
  // PREVIEW pixelpipe stores values.
  if(self->dev->gui_attached && g && piece->pipe->type == DT_DEV_PIXELPIPE_PREVIEW)
  {
    uint64_t hash = dt_dev_hash_plus(self->dev, piece->pipe, self->iop_order, DT_DEV_TRANSFORM_DIR_BACK_INCL);
    dt_pthread_mutex_lock(&g->lock);
    g->A0[0] = A0[0];
    g->A0[1] = A0[1];
    g->A0[2] = A0[2];
    g->distance_max = distance_max;
    g->hash = hash;
    dt_pthread_mutex_unlock(&g->lock);
  }

  // calculate the transition map
  void *trans_map = dt_opencl_alloc_device(devid, width, height, (int)sizeof(float));
  transition_map_cl(self, devid, img_in, trans_map, w1, strength, A0);
  // refine the transition map
  box_min_cl(self, devid, trans_map, trans_map, w1);
  void *trans_map_filtered = dt_opencl_alloc_device(devid, width, height, (int)sizeof(float));
  // apply guided filter with no clipping
  guided_filter_cl(devid, img_in, trans_map, trans_map_filtered, width, height, ch, w2, eps, 1.f, -CL_FLT_MAX,
                   CL_FLT_MAX);

  // finally, calculate the haze-free image
  const float t_min
      = fmaxf(expf(-distance * distance_max), 1.f / 1024); // minimum allowed value for transition map
  dehaze_cl(self, devid, img_in, trans_map_filtered, img_out, t_min, A0);

  dt_opencl_release_mem_object(trans_map);
  dt_opencl_release_mem_object(trans_map_filtered);

  return TRUE;
}
#endif

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
