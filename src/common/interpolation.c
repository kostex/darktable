/* --------------------------------------------------------------------------
    This file is part of darktable,
    copyright (c) 2012 Edouard Gomez <ed.gomez@free.fr>

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
* ------------------------------------------------------------------------*/

#include "common/interpolation.h"
#include "common/darktable.h"
#include "control/conf.h"

#include <assert.h>
#include <glib.h>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

/** Border extrapolation modes */
enum border_mode
{
  BORDER_REPLICATE, // aaaa|abcdefg|gggg
  BORDER_WRAP,      // defg|abcdefg|abcd
  BORDER_MIRROR,    // edcb|abcdefg|fedc
  BORDER_CLAMP      // ....|abcdefg|....
};

/* Supporting them all might be overkill, let the compiler trim all
 * unnecessary modes in clip for resampling codepath*/
#define RESAMPLING_BORDER_MODE BORDER_REPLICATE

/* Supporting them all might be overkill, let the compiler trim all
 * unnecessary modes in interpolation codepath */
#define INTERPOLATION_BORDER_MODE BORDER_MIRROR

// Defines minimum alignment requirement for critical SIMD code
#define SSE_ALIGNMENT 64

// Defines the maximum kernel half length
// !! Make sure to sync this with the filter array !!
#define MAX_HALF_FILTER_WIDTH 3

// Add code for timing resampling function
#define DEBUG_RESAMPLING_TIMING 0

// Add debug info messages to stderr
#define DEBUG_PRINT_INFO 0

// Add *verbose* (like one msg per pixel out) debug message to stderr
#define DEBUG_PRINT_VERBOSE 0

/* --------------------------------------------------------------------------
 * Debug helpers
 * ------------------------------------------------------------------------*/

#if DEBUG_RESAMPLING_TIMING
#include <sys/time.h>
#endif

#if DEBUG_PRINT_INFO
#define debug_info(...)                                                                                      \
  do                                                                                                         \
  {                                                                                                          \
    fprintf(stderr, __VA_ARGS__);                                                                            \
  } while(0)
#else
#define debug_info(...)
#endif

#if DEBUG_PRINT_VERBOSE
#define debug_extra(...)                                                                                     \
  do                                                                                                         \
  {                                                                                                          \
    fprintf(stderr, __VA_ARGS__);                                                                            \
  } while(0)
#else
#define debug_extra(...)
#endif

#if DEBUG_RESAMPLING_TIMING
static inline int64_t getts()
{
  struct timeval t;
  gettimeofday(&t, NULL);
  return t.tv_sec * INT64_C(1000000) + t.tv_usec;
}
#endif

/* --------------------------------------------------------------------------
 * Generic helpers
 * ------------------------------------------------------------------------*/

/** Compute ceil value of a float
 * @remark Avoid libc ceil for now. Maybe we'll revert to libc later.
 * @param x Value to ceil
 * @return ceil value
 */
static inline float ceil_fast(float x)
{
  if(x <= 0.f)
  {
    return (float)(int)x;
  }
  else
  {
    return -((float)(int)-x) + 1.f;
  }
}

#if defined(__SSE2__)
/** Compute absolute value
 * @param t Vector of 4 floats
 * @return Vector of their absolute values
 */ static inline __m128 _mm_abs_ps(__m128 t)
{
  static const uint32_t signmask[4] __attribute__((aligned(SSE_ALIGNMENT)))
  = { 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff };
  return _mm_and_ps(*(__m128 *)signmask, t);
}
#endif

/** Clip into specified range
 * @param idx index to filter
 * @param length length of line
 */
static inline int clip(int i, int min, int max, enum border_mode mode)
{
  switch(mode)
  {
    case BORDER_REPLICATE:
      if(i < min)
      {
        i = min;
      }
      else if(i > max)
      {
        i = max;
      }
      break;
    case BORDER_MIRROR:
      if(i < min)
      {
        i = min - i;
      }
      else if(i > max)
      {
        i = 2 * max - i;
      }
      break;
    case BORDER_WRAP:
      if(i < min)
      {
        i = max - (min - i);
      }
      else if(i > max)
      {
        i = min + (i - max);
      }
      break;
    case BORDER_CLAMP:
      if(i < min || i > max)
      {
        /* Should not be used as is, we prevent -1 usage, filtering the taps
         * we clip the sample indexes for. So understand this function is
         * specific to its caller. */
        i = -1;
      }
      break;
  }

  return i;
}

static inline void prepare_tap_boundaries(int *tap_first, int *tap_last, const enum border_mode mode,
                                          const int filterwidth, const int t, const int max)
{
  /* Check lower bound pixel index and skip as many pixels as necessary to
   * fall into range */
  *tap_first = 0;
  if(mode == BORDER_CLAMP && t < 0)
  {
    *tap_first = -t;
  }

  // Same for upper bound pixel
  *tap_last = filterwidth;
  if(mode == BORDER_CLAMP && t + filterwidth >= max)
  {
    *tap_last = max - t;
  }
}

/** Make sure an aligned chunk will not misalign its following chunk
 * proposing an adapted length
 *
 * @param l Length required for current chunk
 * @param align Required alignment for next chunk
 *
 * @return Required length for keeping alignment ok if chaining data chunks
 */
static inline size_t increase_for_alignment(size_t l, size_t align)
{
  align -= 1;
  return (l + align) & (~align);
}

/** Compute an approximate sine.
 * This function behaves correctly for the range [-pi pi] only.
 * It has the following properties:
 * <ul>
 *   <li>It has exact values for 0, pi/2, pi, -pi/2, -pi</li>
 *   <li>It has matching derivatives to sine for these same points</li>
 *   <li>Its relative error margin is <= 1% iirc</li>
 *   <li>It computational cost is 5 mults + 3 adds + 2 abs</li>
 * </ul>
 * @param t Radian parameter
 * @return guess what
 */
static inline float sinf_fast(float t)
{
  static const float a = 4 / (M_PI * M_PI);
  static const float p = 0.225f;

  t = a * t * (M_PI - fabsf(t));

  return t * (p * (fabsf(t) - 1) + 1);
}

#if defined(__SSE2__)
/** Compute an approximate sine (SSE version, four sines a call).
 * This function behaves correctly for the range [-pi pi] only.
 * It has the following properties:
 * <ul>
 *   <li>It has exact values for 0, pi/2, pi, -pi/2, -pi</li>
 *   <li>It has matching derivatives to sine for these same points</li>
 *   <li>Its relative error margin is <= 1% iirc</li>
 *   <li>It computational cost is 5 mults + 3 adds + 2 abs</li>
 * </ul>
 * @param t Radian parameter
 * @return guess what
 */
static inline __m128 sinf_fast_sse(__m128 t)
{
  static const __m128 a
      = { 4.f / (M_PI * M_PI), 4.f / (M_PI * M_PI), 4.f / (M_PI * M_PI), 4.f / (M_PI * M_PI) };
  static const __m128 p = { 0.225f, 0.225f, 0.225f, 0.225f };
  static const __m128 pi = { M_PI, M_PI, M_PI, M_PI };

  // m4 = a*t*(M_PI - fabsf(t));
  __m128 m1 = _mm_abs_ps(t);
  __m128 m2 = _mm_sub_ps(pi, m1);
  __m128 m3 = _mm_mul_ps(t, m2);
  __m128 m4 = _mm_mul_ps(a, m3);

  // p*(m4*fabsf(m4) - m4) + m4;
  __m128 n1 = _mm_abs_ps(m4);
  __m128 n2 = _mm_mul_ps(m4, n1);
  __m128 n3 = _mm_sub_ps(n2, m4);
  __m128 n4 = _mm_mul_ps(p, n3);

  return _mm_add_ps(n4, m4);
}
#endif

/* --------------------------------------------------------------------------
 * Interpolation kernels
 * ------------------------------------------------------------------------*/

/* --------------------------------------------------------------------------
 * Bilinear interpolation
 * ------------------------------------------------------------------------*/

static inline float bilinear(float width, float t)
{
  float r;
  t = fabsf(t);
  if(t > 1.f)
  {
    r = 0.f;
  }
  else
  {
    r = 1.f - t;
  }
  return r;
}

#if defined(__SSE2__)
static inline __m128 bilinear_sse(__m128 width, __m128 t)
{
  static const __m128 one = { 1.f, 1.f, 1.f, 1.f };
  return _mm_sub_ps(one, _mm_abs_ps(t));
}
#endif

/* --------------------------------------------------------------------------
 * Bicubic interpolation
 * ------------------------------------------------------------------------*/

static inline float bicubic(float width, float t)
{
  float r;
  t = fabsf(t);
  if(t >= 2.f)
  {
    r = 0.f;
  }
  else if(t > 1.f && t < 2.f)
  {
    float t2 = t * t;
    r = 0.5f * (t * (-t2 + 5.f * t - 8.f) + 4.f);
  }
  else
  {
    float t2 = t * t;
    r = 0.5f * (t * (3.f * t2 - 5.f * t) + 2.f);
  }
  return r;
}

#if defined(__SSE2__)
static inline __m128 bicubic_sse(__m128 width, __m128 t)
{
  static const __m128 half = { .5f, .5f, .5f, .5f };
  static const __m128 one = { 1.f, 1.f, 1.f, 1.f };
  static const __m128 two = { 2.f, 2.f, 2.f, 2.f };
  static const __m128 three = { 3.f, 3.f, 3.f, 3.f };
  static const __m128 four = { 4.f, 4.f, 4.f, 4.f };
  static const __m128 five = { 5.f, 5.f, 5.f, 5.f };
  static const __m128 eight = { 8.f, 8.f, 8.f, 8.f };

  t = _mm_abs_ps(t);
  __m128 t2 = _mm_mul_ps(t, t);

  /* Compute 1 < t < 2 case:
   * 0.5f*(t*(-t2 + 5.f*t - 8.f) + 4.f)
   * half*(t*(mt2 + t5 - eight) + four)
   * half*(t*(mt2 + t5_sub_8) + four)
   * half*(t*(mt2_add_t5_sub_8) + four) */
  __m128 t5 = _mm_mul_ps(five, t);
  __m128 t5_sub_8 = _mm_sub_ps(t5, eight);
  __m128 zero = _mm_setzero_ps();
  __m128 mt2 = _mm_sub_ps(zero, t2);
  __m128 mt2_add_t5_sub_8 = _mm_add_ps(mt2, t5_sub_8);
  __m128 a = _mm_mul_ps(t, mt2_add_t5_sub_8);
  __m128 b = _mm_add_ps(a, four);
  __m128 r12 = _mm_mul_ps(b, half);

  /* Compute case < 1
   * 0.5f*(t*(3.f*t2 - 5.f*t) + 2.f) */
  __m128 t23 = _mm_mul_ps(three, t2);
  __m128 c = _mm_sub_ps(t23, t5);
  __m128 d = _mm_mul_ps(t, c);
  __m128 e = _mm_add_ps(d, two);
  __m128 r01 = _mm_mul_ps(half, e);

  // Compute masks fr keeping correct components
  __m128 mask01 = _mm_cmple_ps(t, one);
  __m128 mask12 = _mm_cmpgt_ps(t, one);
  r01 = _mm_and_ps(mask01, r01);
  r12 = _mm_and_ps(mask12, r12);


  return _mm_or_ps(r01, r12);
}
#endif

/* --------------------------------------------------------------------------
 * Lanczos interpolation
 * ------------------------------------------------------------------------*/

#define DT_LANCZOS_EPSILON (1e-9f)

#if 0
// Reference version left here for ... documentation
static inline float
lanczos(float width, float t)
{
  float r;

  if (t<-width || t>width)
  {
    r = 0.f;
  }
  else if (t>-DT_LANCZOS_EPSILON && t<DT_LANCZOS_EPSILON)
  {
    r = 1.f;
  }
  else
  {
    r = width*sinf(M_PI*t)*sinf(M_PI*t/width)/(M_PI*M_PI*t*t);
  }
  return r;
}
#endif

/* Fast lanczos version, no calls to math.h functions, too accurate, too slow
 *
 * Based on a forum entry at
 * http://devmaster.net/forums/topic/4648-fast-and-accurate-sinecosine/
 *
 * Apart the fast sine function approximation, the only trick is to compute:
 * sin(pi.t) = sin(a.pi + r.pi) where t = a + r = trunc(t) + r
 *           = sin(a.pi).cos(r.pi) + sin(r.pi).cos(a.pi)
 *           =         0*cos(r.pi) + sin(r.pi).cos(a.pi)
 *           = sign.sin(r.pi) where sign =  1 if the a is even
 *                                       = -1 if the a is odd
 *
 * Of course we know that lanczos func will only be called for
 * the range -width < t < width so we can additionally avoid the
 * range check.  */

static inline float lanczos(float width, float t)
{
  /* Compute a value for sinf(pi.t) in [-pi pi] for which the value will be
   * correct */
  int a = (int)t;
  float r = t - (float)a;

  // Compute the correct sign for sinf(pi.r)
  union
  {
    float f;
    uint32_t i;
  } sign;
  sign.i = ((a & 1) << 31) | 0x3f800000;

  return (DT_LANCZOS_EPSILON + width * sign.f * sinf_fast(M_PI * r) * sinf_fast(M_PI * t / width))
         / (DT_LANCZOS_EPSILON + M_PI * M_PI * t * t);
}

#if defined(__SSE2__)
static inline __m128 lanczos_sse2(__m128 width, __m128 t)
{
  /* Compute a value for sinf(pi.t) in [-pi pi] for which the value will be
   * correct */
  __m128i a = _mm_cvtps_epi32(t);
  __m128 r = _mm_sub_ps(t, _mm_cvtepi32_ps(a));

  // Compute the correct sign for sinf(pi.r)
  static const uint32_t fone[] __attribute__((aligned(SSE_ALIGNMENT)))
  = { 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000 };
  static const uint32_t ione[] __attribute__((aligned(SSE_ALIGNMENT))) = { 1, 1, 1, 1 };
  static const __m128 eps
      = { DT_LANCZOS_EPSILON, DT_LANCZOS_EPSILON, DT_LANCZOS_EPSILON, DT_LANCZOS_EPSILON };
  static const __m128 pi = { M_PI, M_PI, M_PI, M_PI };
  static const __m128 pi2 = { M_PI * M_PI, M_PI * M_PI, M_PI * M_PI, M_PI * M_PI };

  __m128i isign = _mm_and_si128(*(__m128i *)ione, a);
  isign = _mm_slli_epi64(isign, 31);
  isign = _mm_or_si128(*(__m128i *)fone, isign);
  __m128 fsign = _mm_castsi128_ps(isign);

  __m128 num = _mm_mul_ps(width, fsign);
  num = _mm_mul_ps(num, sinf_fast_sse(_mm_mul_ps(pi, r)));
  num = _mm_mul_ps(num, sinf_fast_sse(_mm_div_ps(_mm_mul_ps(pi, t), width)));
  num = _mm_add_ps(eps, num);

  __m128 den = _mm_mul_ps(pi2, _mm_mul_ps(t, t));
  den = _mm_add_ps(eps, den);

  return _mm_div_ps(num, den);
}
#endif

#undef DT_LANCZOS_EPSILON

/* --------------------------------------------------------------------------
 * All our known interpolators
 * ------------------------------------------------------------------------*/

/* !!! !!! !!!
 * Make sure MAX_HALF_FILTER_WIDTH is at least equal to the maximum width
 * of this filter list. Otherwise bad things will happen
 * !!! !!! !!!
 */
static const struct dt_interpolation dt_interpolator[] = {
  {.id = DT_INTERPOLATION_BILINEAR,
   .name = "bilinear",
   .width = 1,
   .func = &bilinear,
#if defined(__SSE2__)
   .funcsse = &bilinear_sse
#endif
  },
  {.id = DT_INTERPOLATION_BICUBIC,
   .name = "bicubic",
   .width = 2,
   .func = &bicubic,
#if defined(__SSE2__)
   .funcsse = &bicubic_sse
#endif
  },
  {.id = DT_INTERPOLATION_LANCZOS2,
   .name = "lanczos2",
   .width = 2,
   .func = &lanczos,
#if defined(__SSE2__)
   .funcsse = &lanczos_sse2
#endif
  },
  {.id = DT_INTERPOLATION_LANCZOS3,
   .name = "lanczos3",
   .width = 3,
   .func = &lanczos,
#if defined(__SSE2__)
   .funcsse = &lanczos_sse2
#endif
  },
};

/* --------------------------------------------------------------------------
 * Kernel utility methods
 * ------------------------------------------------------------------------*/

/** Computes an upsampling filtering kernel
 *
 * @param itor [in] Interpolator used
 * @param kernel [out] resulting itor->width*2 filter taps
 * @param norm [out] Kernel norm
 * @param first [out] first input sample index used
 * @param t [in] Interpolated coordinate */
static inline void compute_upsampling_kernel_plain(const struct dt_interpolation *itor, float *kernel,
                                                   float *norm, int *first, float t)
{
  int f = (int)t - itor->width + 1;
  if(first)
  {
    *first = f;
  }

  /* Find closest integer position and then offset that to match first
   * filtered sample position */
  t = t - (float)f;

  // Will hold kernel norm
  float n = 0.f;

  // Compute the raw kernel
  for(int i = 0; i < 2 * itor->width; i++)
  {
    float tap = itor->func((float)itor->width, t);
    n += tap;
    kernel[i] = tap;
    t -= 1.f;
  }
  if(norm)
  {
    *norm = n;
  }
}

#if defined(__SSE2__)
/** Computes an upsampling filtering kernel (SSE version, four taps per inner loop)
 *
 * @param itor [in] Interpolator used
 * @param kernel [out] resulting itor->width*2 filter taps (array must be at least (itor->width*2+3)/4*4
 *floats long)
 * @param norm [out] Kernel norm
 * @param first [out] first input sample index used
 * @param t [in] Interpolated coordinate
 *
 * @return kernel norm
 */
static inline void compute_upsampling_kernel_sse(const struct dt_interpolation *itor, float *kernel,
                                                 float *norm, int *first, float t)
{
  int f = (int)t - itor->width + 1;
  if(first)
  {
    *first = f;
  }

  /* Find closest integer position and then offset that to match first
   * filtered sample position */
  t = t - (float)f;

  // Prepare t vector to compute four values a loop
  static const __m128 bootstrap = { 0.f, -1.f, -2.f, -3.f };
  static const __m128 iter = { -4.f, -4.f, -4.f, -4.f };
  __m128 vt = _mm_add_ps(_mm_set_ps1(t), bootstrap);
  __m128 vw = _mm_set_ps1((float)itor->width);

  // Prepare counters (math kept stupid for understanding)
  int i = 0;
  int runs = (2 * itor->width + 3) / 4;

  while(i < runs)
  {
    // Compute the values
    __m128 vr = itor->funcsse(vw, vt);

    // Save result
    *(__m128 *)kernel = vr;

    // Prepare next iteration
    vt = _mm_add_ps(vt, iter);
    kernel += 4;
    i++;
  }

  // compute norm now
  if(norm)
  {
    float n = 0.f;
    i = 0;
    kernel -= 4 * runs;
    while(i < 2 * itor->width)
    {
      n += *kernel;
      kernel++;
      i++;
    }
    *norm = n;
  }
}
#endif

static inline void compute_upsampling_kernel(const struct dt_interpolation *itor, float *kernel, float *norm,
                                             int *first, float t)
{
  if(darktable.codepath.OPENMP_SIMD) return compute_upsampling_kernel_plain(itor, kernel, norm, first, t);
#if defined(__SSE2__)
  else if(darktable.codepath.SSE2)
    return compute_upsampling_kernel_sse(itor, kernel, norm, first, t);
#endif
  else
    dt_unreachable_codepath();
}

/** Computes a downsampling filtering kernel
 *
 * @param itor [in] Interpolator used
 * @param kernelsize [out] Number of taps
 * @param kernel [out] resulting taps (at least itor->width/inoout elements for no overflow)
 * @param norm [out] Kernel norm
 * @param first [out] index of the first sample for which the kernel is to be applied
 * @param outoinratio [in] "out samples" over "in samples" ratio
 * @param xout [in] Output coordinate */
static inline void compute_downsampling_kernel_plain(const struct dt_interpolation *itor, int *taps,
                                                     int *first, float *kernel, float *norm,
                                                     float outoinratio, int xout)
{
  // Keep this at hand
  float w = (float)itor->width;

  /* Compute the phase difference between output pixel and its
   * input corresponding input pixel */
  float xin = ceil_fast(((float)xout - w) / outoinratio);
  if(first)
  {
    *first = (int)xin;
  }

  // Compute first interpolator parameter
  float t = xin * outoinratio - (float)xout;

  // Will hold kernel norm
  float n = 0.f;

  // Compute all filter taps
  *taps = (int)((w - t) / outoinratio);
  for(int i = 0; i < *taps; i++)
  {
    *kernel = itor->func(w, t);
    n += *kernel;
    t += outoinratio;
    kernel++;
  }

  if(norm)
  {
    *norm = n;
  }
}


#if defined(__SSE2__)
/** Computes a downsampling filtering kernel (SSE version, four taps per inner loop iteration)
 *
 * @param itor [in] Interpolator used
 * @param kernelsize [out] Number of taps
 * @param kernel [out] resulting taps (at least itor->width/inoout + 4 elements for no overflow)
 * @param norm [out] Kernel norm
 * @param first [out] index of the first sample for which the kernel is to be applied
 * @param outoinratio [in] "out samples" over "in samples" ratio
 * @param xout [in] Output coordinate */
static inline void compute_downsampling_kernel_sse(const struct dt_interpolation *itor, int *taps, int *first,
                                                   float *kernel, float *norm, float outoinratio, int xout)
{
  // Keep this at hand
  float w = (float)itor->width;

  /* Compute the phase difference between output pixel and its
   * input corresponding input pixel */
  float xin = ceil_fast(((float)xout - w) / outoinratio);
  if(first)
  {
    *first = (int)xin;
  }

  // Compute first interpolator parameter
  float t = xin * outoinratio - (float)xout;

  // Compute all filter taps
  *taps = (int)((w - t) / outoinratio);

  // Bootstrap vector t
  static const __m128 bootstrap = { 0.f, 1.f, 2.f, 3.f };
  const __m128 iter = _mm_set_ps1(4.f * outoinratio);
  const __m128 vw = _mm_set_ps1(w);
  __m128 vt = _mm_add_ps(_mm_set_ps1(t), _mm_mul_ps(_mm_set_ps1(outoinratio), bootstrap));

  // Prepare counters (math kept stupid for understanding)
  int i = 0;
  int runs = (*taps + 3) / 4;

  while(i < runs)
  {
    // Compute the values
    __m128 vr = itor->funcsse(vw, vt);

    // Save result
    *(__m128 *)kernel = vr;

    // Prepare next iteration
    vt = _mm_add_ps(vt, iter);
    kernel += 4;
    i++;
  }

  // compute norm now
  if(norm)
  {
    float n = 0.f;
    i = 0;
    kernel -= 4 * runs;
    while(i < *taps)
    {
      n += *kernel;
      kernel++;
      i++;
    }
    *norm = n;
  }
}
#endif

static inline void compute_downsampling_kernel(const struct dt_interpolation *itor, int *taps, int *first,
                                               float *kernel, float *norm, float outoinratio, int xout)
{
  if(darktable.codepath.OPENMP_SIMD)
    return compute_downsampling_kernel_plain(itor, taps, first, kernel, norm, outoinratio, xout);
#if defined(__SSE2__)
  else if(darktable.codepath.SSE2)
    return compute_downsampling_kernel_sse(itor, taps, first, kernel, norm, outoinratio, xout);
#endif
  else
    dt_unreachable_codepath();
}

/* --------------------------------------------------------------------------
 * Sample interpolation function (see usage in iop/lens.c and iop/clipping.c)
 * ------------------------------------------------------------------------*/

#define MAX_KERNEL_REQ ((2 * (MAX_HALF_FILTER_WIDTH) + 3) & (~3))

float dt_interpolation_compute_sample(const struct dt_interpolation *itor, const float *in, const float x,
                                      const float y, const int width, const int height,
                                      const int samplestride, const int linestride)
{
  assert(itor->width < (MAX_HALF_FILTER_WIDTH + 1));

  float kernelh[MAX_KERNEL_REQ] __attribute__((aligned(SSE_ALIGNMENT)));
  float kernelv[MAX_KERNEL_REQ] __attribute__((aligned(SSE_ALIGNMENT)));

  // Compute both horizontal and vertical kernels
  float normh;
  float normv;
  compute_upsampling_kernel(itor, kernelh, &normh, NULL, x);
  compute_upsampling_kernel(itor, kernelv, &normv, NULL, y);

  int ix = (int)x;
  int iy = (int)y;

  /* Now 2 cases, the pixel + filter width goes outside the image
   * in that case we have to use index clipping to keep all reads
   * in the input image (slow path) or we are sure it won't fall
   * outside and can do more simple code */
  float r;
  if(ix >= (itor->width - 1) && iy >= (itor->width - 1) && ix < (width - itor->width)
     && iy < (height - itor->width))
  {
    // Inside image boundary case

    // Go to top left pixel
    in = (float *)in + linestride * iy + ix * samplestride;
    in = in - (itor->width - 1) * (samplestride + linestride);

    // Apply the kernel
    float s = 0.f;
    for(int i = 0; i < 2 * itor->width; i++)
    {
      float h = 0.0f;
      for(int j = 0; j < 2 * itor->width; j++)
      {
        h += kernelh[j] * in[j * samplestride];
      }
      s += kernelv[i] * h;
      in += linestride;
    }
    r = s / (normh * normv);
  }
  else if(ix >= 0 && iy >= 0 && ix < width && iy < height)
  {
    // At least a valid coordinate


    // Point to the upper left pixel index wise
    iy -= itor->width - 1;
    ix -= itor->width - 1;

    static const enum border_mode bordermode = INTERPOLATION_BORDER_MODE;
    assert(bordermode != BORDER_CLAMP); // XXX in clamp mode, norms would be wrong

    int xtap_first;
    int xtap_last;
    prepare_tap_boundaries(&xtap_first, &xtap_last, bordermode, 2 * itor->width, ix, width);

    int ytap_first;
    int ytap_last;
    prepare_tap_boundaries(&ytap_first, &ytap_last, bordermode, 2 * itor->width, iy, height);

    // Apply the kernel
    float s = 0.f;
    for(int i = ytap_first; i < ytap_last; i++)
    {
      int clip_y = clip(iy + i, 0, height - 1, bordermode);
      float h = 0.0f;
      for(int j = xtap_first; j < xtap_last; j++)
      {
        int clip_x = clip(ix + j, 0, width - 1, bordermode);
        const float *ipixel = in + clip_y * linestride + clip_x * samplestride;
        h += kernelh[j] * ipixel[0];
      }
      s += kernelv[i] * h;
    }

    r = s / (normh * normv);
  }
  else
  {
    // invalid coordinate
    r = 0.0f;
  }
  return r;
}

/* --------------------------------------------------------------------------
 * Pixel interpolation function (see usage in iop/lens.c and iop/clipping.c)
 * ------------------------------------------------------------------------*/

static void dt_interpolation_compute_pixel4c_plain(const struct dt_interpolation *itor, const float *in,
                                                   float *out, const float x, const float y, const int width,
                                                   const int height, const int linestride)
{
  assert(itor->width < (MAX_HALF_FILTER_WIDTH + 1));

  // Quite a bit of space for kernels
  float kernelh[MAX_KERNEL_REQ] __attribute__((aligned(SSE_ALIGNMENT)));
  float kernelv[MAX_KERNEL_REQ] __attribute__((aligned(SSE_ALIGNMENT)));

  // Compute both horizontal and vertical kernels
  float normh;
  float normv;
  compute_upsampling_kernel(itor, kernelh, &normh, NULL, x);
  compute_upsampling_kernel(itor, kernelv, &normv, NULL, y);

  // Precompute the inverse of the filter norm for later use
  const float oonorm = (1.f / (normh * normv));

  /* Now 2 cases, the pixel + filter width goes outside the image
   * in that case we have to use index clipping to keep all reads
   * in the input image (slow path) or we are sure it won't fall
   * outside and can do more simple code */
  int ix = (int)x;
  int iy = (int)y;

  if(ix >= (itor->width - 1) && iy >= (itor->width - 1) && ix < (width - itor->width)
     && iy < (height - itor->width))
  {
    // Inside image boundary case

    // Go to top left pixel
    in = (float *)in + linestride * iy + ix * 4;
    in = in - (itor->width - 1) * (4 + linestride);

    // Apply the kernel
    float pixel[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for(int i = 0; i < 2 * itor->width; i++)
    {
      float h[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
      for(int j = 0; j < 2 * itor->width; j++)
      {
        for(int c = 0; c < 3; c++) h[c] += kernelh[j] * in[j * 4 + c];
      }
      for(int c = 0; c < 3; c++) pixel[c] += kernelv[i] * h[c];
      in += linestride;
    }

    for(int c = 0; c < 3; c++) out[c] = oonorm * pixel[c];
  }
  else if(ix >= 0 && iy >= 0 && ix < width && iy < height)
  {
    // At least a valid coordinate

    // Point to the upper left pixel index wise
    iy -= itor->width - 1;
    ix -= itor->width - 1;

    static const enum border_mode bordermode = INTERPOLATION_BORDER_MODE;
    assert(bordermode != BORDER_CLAMP); // XXX in clamp mode, norms would be wrong

    int xtap_first;
    int xtap_last;
    prepare_tap_boundaries(&xtap_first, &xtap_last, bordermode, 2 * itor->width, ix, width);

    int ytap_first;
    int ytap_last;
    prepare_tap_boundaries(&ytap_first, &ytap_last, bordermode, 2 * itor->width, iy, height);

    // Apply the kernel
    float pixel[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for(int i = ytap_first; i < ytap_last; i++)
    {
      int clip_y = clip(iy + i, 0, height - 1, bordermode);
      float h[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
      for(int j = xtap_first; j < xtap_last; j++)
      {
        int clip_x = clip(ix + j, 0, width - 1, bordermode);
        const float *ipixel = in + clip_y * linestride + clip_x * 4;
        for(int c = 0; c < 3; c++) h[c] += kernelh[j] * ipixel[c];
      }
      for(int c = 0; c < 3; c++) pixel[c] += kernelv[i] * h[c];
    }

    for(int c = 0; c < 3; c++) out[c] = oonorm * pixel[c];
  }
  else
  {
    for(int c = 0; c < 3; c++) out[c] = 0.0f;
  }
}

#if defined(__SSE2__)
static void dt_interpolation_compute_pixel4c_sse(const struct dt_interpolation *itor, const float *in,
                                                 float *out, const float x, const float y, const int width,
                                                 const int height, const int linestride)
{
  assert(itor->width < (MAX_HALF_FILTER_WIDTH + 1));

  // Quite a bit of space for kernels
  float kernelh[MAX_KERNEL_REQ] __attribute__((aligned(SSE_ALIGNMENT)));
  float kernelv[MAX_KERNEL_REQ] __attribute__((aligned(SSE_ALIGNMENT)));
  __m128 vkernelh[2 * MAX_HALF_FILTER_WIDTH];
  __m128 vkernelv[2 * MAX_HALF_FILTER_WIDTH];

  // Compute both horizontal and vertical kernels
  float normh;
  float normv;
  compute_upsampling_kernel(itor, kernelh, &normh, NULL, x);
  compute_upsampling_kernel(itor, kernelv, &normv, NULL, y);

  // We will process four components a time, duplicate the information
  for(int i = 0; i < 2 * itor->width; i++)
  {
    vkernelh[i] = _mm_set_ps1(kernelh[i]);
    vkernelv[i] = _mm_set_ps1(kernelv[i]);
  }

  // Precompute the inverse of the filter norm for later use
  __m128 oonorm = _mm_set_ps1(1.f / (normh * normv));

  /* Now 2 cases, the pixel + filter width goes outside the image
   * in that case we have to use index clipping to keep all reads
   * in the input image (slow path) or we are sure it won't fall
   * outside and can do more simple code */
  int ix = (int)x;
  int iy = (int)y;

  if(ix >= (itor->width - 1) && iy >= (itor->width - 1) && ix < (width - itor->width)
     && iy < (height - itor->width))
  {
    // Inside image boundary case

    // Go to top left pixel
    in = (float *)in + linestride * iy + ix * 4;
    in = in - (itor->width - 1) * (4 + linestride);

    // Apply the kernel
    __m128 pixel = _mm_setzero_ps();
    for(int i = 0; i < 2 * itor->width; i++)
    {
      __m128 h = _mm_setzero_ps();
      for(int j = 0; j < 2 * itor->width; j++)
      {
        h = _mm_add_ps(h, _mm_mul_ps(vkernelh[j], *(__m128 *)&in[j * 4]));
      }
      pixel = _mm_add_ps(pixel, _mm_mul_ps(vkernelv[i], h));
      in += linestride;
    }

    *(__m128 *)out = _mm_mul_ps(pixel, oonorm);
  }
  else if(ix >= 0 && iy >= 0 && ix < width && iy < height)
  {
    // At least a valid coordinate

    // Point to the upper left pixel index wise
    iy -= itor->width - 1;
    ix -= itor->width - 1;

    static const enum border_mode bordermode = INTERPOLATION_BORDER_MODE;
    assert(bordermode != BORDER_CLAMP); // XXX in clamp mode, norms would be wrong

    int xtap_first;
    int xtap_last;
    prepare_tap_boundaries(&xtap_first, &xtap_last, bordermode, 2 * itor->width, ix, width);

    int ytap_first;
    int ytap_last;
    prepare_tap_boundaries(&ytap_first, &ytap_last, bordermode, 2 * itor->width, iy, height);

    // Apply the kernel
    __m128 pixel = _mm_setzero_ps();
    for(int i = ytap_first; i < ytap_last; i++)
    {
      int clip_y = clip(iy + i, 0, height - 1, bordermode);
      __m128 h = _mm_setzero_ps();
      for(int j = xtap_first; j < xtap_last; j++)
      {
        int clip_x = clip(ix + j, 0, width - 1, bordermode);
        const float *ipixel = in + clip_y * linestride + clip_x * 4;
        h = _mm_add_ps(h, _mm_mul_ps(vkernelh[j], *(__m128 *)ipixel));
      }
      pixel = _mm_add_ps(pixel, _mm_mul_ps(vkernelv[i], h));
    }

    *(__m128 *)out = _mm_mul_ps(pixel, oonorm);
  }
  else
  {
    *(__m128 *)out = _mm_set_ps1(0.0f);
  }
}
#endif

void dt_interpolation_compute_pixel4c(const struct dt_interpolation *itor, const float *in, float *out,
                                      const float x, const float y, const int width, const int height,
                                      const int linestride)
{
  if(darktable.codepath.OPENMP_SIMD)
    return dt_interpolation_compute_pixel4c_plain(itor, in, out, x, y, width, height, linestride);
#if defined(__SSE2__)
  else if(darktable.codepath.SSE2)
    return dt_interpolation_compute_pixel4c_sse(itor, in, out, x, y, width, height, linestride);
#endif
  else
    dt_unreachable_codepath();
}

static void dt_interpolation_compute_pixel1c_plain(const struct dt_interpolation *itor, const float *in,
                                                   float *out, const float x, const float y, const int width,
                                                   const int height, const int linestride)
{
  assert(itor->width < (MAX_HALF_FILTER_WIDTH + 1));

  // Quite a bit of space for kernels
  float kernelh[MAX_KERNEL_REQ] __attribute__((aligned(SSE_ALIGNMENT)));
  float kernelv[MAX_KERNEL_REQ] __attribute__((aligned(SSE_ALIGNMENT)));

  // Compute both horizontal and vertical kernels
  float normh;
  float normv;
  compute_upsampling_kernel(itor, kernelh, &normh, NULL, x);
  compute_upsampling_kernel(itor, kernelv, &normv, NULL, y);

  // Precompute the inverse of the filter norm for later use
  const float oonorm = (1.f / (normh * normv));

  /* Now 2 cases, the pixel + filter width goes outside the image
   * in that case we have to use index clipping to keep all reads
   * in the input image (slow path) or we are sure it won't fall
   * outside and can do more simple code */
  int ix = (int)x;
  int iy = (int)y;

  if(ix >= (itor->width - 1) && iy >= (itor->width - 1) && ix < (width - itor->width)
    && iy < (height - itor->width))
  {
    // Inside image boundary case

    // Go to top left pixel
    in = (float *)in + linestride * iy + ix;
    in = in - (itor->width - 1) * (1 + linestride);

    // Apply the kernel
    float pixel = 0.0f;
    for(int i = 0; i < 2 * itor->width; i++)
    {
      float h = 0.0f;
      for(int j = 0; j < 2 * itor->width; j++)
      {
        h += kernelh[j] * in[j];
      }
      pixel += kernelv[i] * h;
      in += linestride;
    }

    *out = oonorm * pixel;
  }
  else if(ix >= 0 && iy >= 0 && ix < width && iy < height)
  {
    // At least a valid coordinate

    // Point to the upper left pixel index wise
    iy -= itor->width - 1;
    ix -= itor->width - 1;

    static const enum border_mode bordermode = INTERPOLATION_BORDER_MODE;
    assert(bordermode != BORDER_CLAMP); // XXX in clamp mode, norms would be wrong

    int xtap_first;
    int xtap_last;
    prepare_tap_boundaries(&xtap_first, &xtap_last, bordermode, 2 * itor->width, ix, width);

    int ytap_first;
    int ytap_last;
    prepare_tap_boundaries(&ytap_first, &ytap_last, bordermode, 2 * itor->width, iy, height);

    // Apply the kernel
    float pixel = 0.0f;
    for(int i = ytap_first; i < ytap_last; i++)
    {
      int clip_y = clip(iy + i, 0, height - 1, bordermode);
      float h = 0.0f;
      for(int j = xtap_first; j < xtap_last; j++)
      {
        int clip_x = clip(ix + j, 0, width - 1, bordermode);
        const float *ipixel = in + clip_y * linestride + clip_x;
        h += kernelh[j] * *ipixel;
      }
      pixel += kernelv[i] * h;
    }

    *out = oonorm * pixel;
  }
  else
  {
    *out = 0.0f;
  }
}

void dt_interpolation_compute_pixel1c(const struct dt_interpolation *itor, const float *in, float *out,
                                      const float x, const float y, const int width, const int height,
                                      const int linestride)
{
  return dt_interpolation_compute_pixel1c_plain(itor, in, out, x, y, width, height, linestride);
}

/* --------------------------------------------------------------------------
 * Interpolation factory
 * ------------------------------------------------------------------------*/

const struct dt_interpolation *dt_interpolation_new(enum dt_interpolation_type type)
{
  const struct dt_interpolation *itor = NULL;

  if(type == DT_INTERPOLATION_USERPREF)
  {
    // Find user preferred interpolation method
    gchar *uipref = dt_conf_get_string("plugins/lighttable/export/pixel_interpolator");
    for(int i = DT_INTERPOLATION_FIRST; uipref && i < DT_INTERPOLATION_LAST; i++)
    {
      if(!strcmp(uipref, dt_interpolator[i].name))
      {
        // Found the one
        itor = &dt_interpolator[i];
        break;
      }
    }
    g_free(uipref);

    /* In the case the search failed (!uipref or name not found),
     * prepare later search pass with default fallback */
    type = DT_INTERPOLATION_DEFAULT;
  }
  if(!itor)
  {
    // Did not find the userpref one or we've been asked for a specific one
    for(int i = DT_INTERPOLATION_FIRST; i < DT_INTERPOLATION_LAST; i++)
    {
      if(dt_interpolator[i].id == type)
      {
        itor = &dt_interpolator[i];
        break;
      }
      if(dt_interpolator[i].id == DT_INTERPOLATION_DEFAULT)
      {
        itor = &dt_interpolator[i];
      }
    }
  }

  return itor;
}

/* --------------------------------------------------------------------------
 * Image resampling
 * ------------------------------------------------------------------------*/

/** Prepares a 1D resampling plan
 *
 * This consists of the following information
 * <ul>
 * <li>A list of lengths that tell how many pixels are relevant for the
 *    next output</li>
 * <li>A list of required filter kernels</li>
 * <li>A list of sample indexes</li>
 * </ul>
 *
 * How to apply the resampling plan:
 * <ol>
 * <li>Pick a length from the length array</li>
 * <li>until length is reached
 *     <ol>
 *     <li>pick a kernel tap></li>
 *     <li>pick the relevant sample according to the picked index</li>
 *     <li>multiply them and accumulate</li>
 *     </ol>
 * </li>
 * <li>here goes a single output sample</li>
 * </ol>
 *
 * This until you reach the number of output pixels
 *
 * @param itor interpolator used to resample
 * @param in [in] Number of input samples
 * @param out [in] Number of output samples
 * @param plength [out] Array of lengths for each pixel filtering (number
 * of taps/indexes to use). This array mus be freed with dt_free_align() when you're
 * done with the plan.
 * @param pkernel [out] Array of filter kernel taps
 * @param pindex [out] Array of sample indexes to be used for applying each kernel tap
 * arrays of information
 * @param pmeta [out] Array of int triplets (length, kernel, index) telling where to start for an arbitrary
 * out position meta[3*out]
 * @return 0 for success, !0 for failure
 */
static int prepare_resampling_plan(const struct dt_interpolation *itor, int in, const int in_x0, int out,
                                   const int out_x0, float scale, int **plength, float **pkernel,
                                   int **pindex, int **pmeta)
{
  // Safe return values
  *plength = NULL;
  *pkernel = NULL;
  *pindex = NULL;
  if(pmeta)
  {
    *pmeta = NULL;
  }

  if(scale == 1.f)
  {
    // No resampling required
    return 0;
  }

  // Compute common upsampling/downsampling memory requirements
  int maxtapsapixel;
  if(scale > 1.f)
  {
    // Upscale... the easy one. The values are exact
    maxtapsapixel = 2 * itor->width;
  }
  else
  {
    // Downscale... going for worst case values memory wise
    maxtapsapixel = ceil_fast((float)2 * (float)itor->width / scale);
  }

  int nlengths = out;
  int nindex = maxtapsapixel * out;
  int nkernel = maxtapsapixel * out;
  size_t lengthreq = increase_for_alignment(nlengths * sizeof(int), SSE_ALIGNMENT);
  size_t indexreq = increase_for_alignment(nindex * sizeof(int), SSE_ALIGNMENT);
  size_t kernelreq = increase_for_alignment(nkernel * sizeof(float), SSE_ALIGNMENT);
  size_t scratchreq = maxtapsapixel * sizeof(float) + 4 * sizeof(float);
  // NB: because sse versions compute four taps a time
  size_t metareq = pmeta ? 3 * sizeof(int) * out : 0;

  void *blob = NULL;
  size_t totalreq = kernelreq + lengthreq + indexreq + scratchreq + metareq;
  blob = dt_alloc_align(SSE_ALIGNMENT, totalreq);
  if(!blob)
  {
    return 1;
  }

  int *lengths = (int *)blob;
  blob = (char *)blob + lengthreq;
  int *index = (int *)blob;
  blob = (char *)blob + indexreq;
  float *kernel = (float *)blob;
  blob = (char *)blob + kernelreq;
  float *scratchpad = scratchreq ? (float *)blob : NULL;
  blob = (char *)blob + scratchreq;
  int *meta = metareq ? (int *)blob : NULL;
//   blob = (char *)blob + metareq;

  /* setting this as a const should help the compilers trim all unnecessary
   * codepaths */
  const enum border_mode bordermode = RESAMPLING_BORDER_MODE;

  /* Upscale and downscale differ in subtle points, getting rid of code
   * duplication might have been tricky and i prefer keeping the code
   * as straight as possible */
  if(scale > 1.f)
  {
    int kidx = 0;
    int iidx = 0;
    int lidx = 0;
    int midx = 0;
    for(int x = 0; x < out; x++)
    {
      if(meta)
      {
        meta[midx++] = lidx;
        meta[midx++] = kidx;
        meta[midx++] = iidx;
      }

      // Projected position in input samples
      float fx = (float)(out_x0 + x) / scale;

      // Compute the filter kernel at that position
      int first;
      compute_upsampling_kernel(itor, scratchpad, NULL, &first, fx);

      /* Check lower and higher bound pixel index and skip as many pixels as
       * necessary to fall into range */
      int tap_first;
      int tap_last;
      prepare_tap_boundaries(&tap_first, &tap_last, bordermode, 2 * itor->width, first, in);

      // Track number of taps that will be used
      lengths[lidx++] = tap_last - tap_first;

      // Precompute the inverse of the norm
      float norm = 0.f;
      for(int tap = tap_first; tap < tap_last; tap++)
      {
        norm += scratchpad[tap];
      }
      norm = 1.f / norm;

      /* Unlike single pixel or single sample code, here it's interesting to
       * precompute the normalized filter kernel as this will avoid dividing
       * by the norm for all processed samples/pixels
       * NB: use the same loop to put in place the index list */
      first += tap_first;
      for(int tap = tap_first; tap < tap_last; tap++)
      {
        kernel[kidx++] = scratchpad[tap] * norm;
        index[iidx++] = clip(first++, 0, in - 1, bordermode);
      }
    }
  }
  else
  {
    int kidx = 0;
    int iidx = 0;
    int lidx = 0;
    int midx = 0;
    for(int x = 0; x < out; x++)
    {
      if(meta)
      {
        meta[midx++] = lidx;
        meta[midx++] = kidx;
        meta[midx++] = iidx;
      }

      // Compute downsampling kernel centered on output position
      int taps;
      int first;
      compute_downsampling_kernel(itor, &taps, &first, scratchpad, NULL, scale, out_x0 + x);

      /* Check lower and higher bound pixel index and skip as many pixels as
       * necessary to fall into range */
      int tap_first;
      int tap_last;
      prepare_tap_boundaries(&tap_first, &tap_last, bordermode, taps, first, in);

      // Track number of taps that will be used
      lengths[lidx++] = tap_last - tap_first;

      // Precompute the inverse of the norm
      float norm = 0.f;
      for(int tap = tap_first; tap < tap_last; tap++)
      {
        norm += scratchpad[tap];
      }
      norm = 1.f / norm;

      /* Unlike single pixel or single sample code, here it's interesting to
       * precompute the normalized filter kernel as this will avoid dividing
       * by the norm for all processed samples/pixels
       * NB: use the same loop to put in place the index list */
      first += tap_first;
      for(int tap = tap_first; tap < tap_last; tap++)
      {
        kernel[kidx++] = scratchpad[tap] * norm;
        index[iidx++] = clip(first++, 0, in - 1, bordermode);
      }
    }
  }

  // Validate plan wrt caller
  *plength = lengths;
  *pindex = index;
  *pkernel = kernel;
  if(pmeta)
  {
    *pmeta = meta;
  }

  return 0;
}

static void dt_interpolation_resample_plain(const struct dt_interpolation *itor, float *out,
                                            const dt_iop_roi_t *const roi_out, const int32_t out_stride,
                                            const float *const in, const dt_iop_roi_t *const roi_in,
                                            const int32_t in_stride)
{
  int *hindex = NULL;
  int *hlength = NULL;
  float *hkernel = NULL;
  int *vindex = NULL;
  int *vlength = NULL;
  float *vkernel = NULL;
  int *vmeta = NULL;

  int r;

  debug_info("resampling %p (%dx%d@%dx%d scale %f) -> %p (%dx%d@%dx%d scale %f)\n", in, roi_in->width,
             roi_in->height, roi_in->x, roi_in->y, roi_in->scale, out, roi_out->width, roi_out->height,
             roi_out->x, roi_out->y, roi_out->scale);

  // Fast code path for 1:1 copy, only cropping area can change
  if(roi_out->scale == 1.f)
  {
    const int x0 = roi_out->x * 4 * sizeof(float);
#if DEBUG_RESAMPLING_TIMING
    int64_t ts_resampling = getts();
#endif
#ifdef _OPENMP
#pragma omp parallel for default(none) shared(out)
#endif
    for(int y = 0; y < roi_out->height; y++)
    {
      float *i = (float *)((char *)in + (size_t)in_stride * (y + roi_out->y) + x0);
      float *o = (float *)((char *)out + (size_t)out_stride * y);
      memcpy(o, i, out_stride);
    }
#if DEBUG_RESAMPLING_TIMING
    ts_resampling = getts() - ts_resampling;
    fprintf(stderr, "resampling %p plan:0us resampling:%" PRId64 "us\n", in, ts_resampling);
#endif
    // All done, so easy case
    return;
  }

// Generic non 1:1 case... much more complicated :D
#if DEBUG_RESAMPLING_TIMING
  int64_t ts_plan = getts();
#endif

  // Prepare resampling plans once and for all
  r = prepare_resampling_plan(itor, roi_in->width, roi_in->x, roi_out->width, roi_out->x, roi_out->scale,
                              &hlength, &hkernel, &hindex, NULL);
  if(r)
  {
    goto exit;
  }

  r = prepare_resampling_plan(itor, roi_in->height, roi_in->y, roi_out->height, roi_out->y, roi_out->scale,
                              &vlength, &vkernel, &vindex, &vmeta);
  if(r)
  {
    goto exit;
  }

#if DEBUG_RESAMPLING_TIMING
  ts_plan = getts() - ts_plan;
#endif

#if DEBUG_RESAMPLING_TIMING
  int64_t ts_resampling = getts();
#endif

// Process each output line
#ifdef _OPENMP
#pragma omp parallel for default(none) shared(out, hindex, hlength, hkernel, vindex, vlength, vkernel, vmeta)
#endif
  for(int oy = 0; oy < roi_out->height; oy++)
  {
    // Initialize column resampling indexes
    int vlidx = vmeta[3 * oy + 0]; // V(ertical) L(ength) I(n)d(e)x
    int vkidx = vmeta[3 * oy + 1]; // V(ertical) K(ernel) I(n)d(e)x
    int viidx = vmeta[3 * oy + 2]; // V(ertical) I(ndex) I(n)d(e)x

    // Initialize row resampling indexes
    int hlidx = 0; // H(orizontal) L(ength) I(n)d(e)x
    int hkidx = 0; // H(orizontal) K(ernel) I(n)d(e)x
    int hiidx = 0; // H(orizontal) I(ndex) I(n)d(e)x

    // Number of lines contributing to the output line
    int vl = vlength[vlidx++]; // V(ertical) L(ength)

    // Process each output column
    for(int ox = 0; ox < roi_out->width; ox++)
    {
      debug_extra("output %p [% 4d % 4d]\n", out, ox, oy);

      // This will hold the resulting pixel
      float vs[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

      // Number of horizontal samples contributing to the output
      int hl = hlength[hlidx++]; // H(orizontal) L(ength)

      for(int iy = 0; iy < vl; iy++)
      {
        // This is our input line
        const float *i = (float *)((char *)in + (size_t)in_stride * vindex[viidx++]);

        float vhs[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

        for(int ix = 0; ix < hl; ix++)
        {
          // Apply the precomputed filter kernel
          size_t baseidx = (size_t)hindex[hiidx++] * 4;
          const float htap = hkernel[hkidx++];
          for(int c = 0; c < 3; c++) vhs[c] += i[baseidx + c] * htap;
        }

        // Accumulate contribution from this line
        const float vtap = vkernel[vkidx++];
        for(int c = 0; c < 3; c++) vs[c] += vhs[c] * vtap;

        // Reset horizontal resampling context
        hkidx -= hl;
        hiidx -= hl;
      }

      // Output pixel is ready
      float *o = (float *)((char *)out + (size_t)oy * out_stride + (size_t)ox * 4 * sizeof(float));
      for(int c = 0; c < 3; c++) o[c] = vs[c];

      // Reset vertical resampling context
      viidx -= vl;
      vkidx -= vl;

      // Progress in horizontal context
      hiidx += hl;
      hkidx += hl;
    }
  }

#if DEBUG_RESAMPLING_TIMING
  ts_resampling = getts() - ts_resampling;
  fprintf(stderr, "resampling %p plan:%" PRId64 "us resampling:%" PRId64 "us\n", in, ts_plan, ts_resampling);
#endif

exit:
  /* Free the resampling plans. It's nasty to optimize allocs like that, but
   * it simplifies the code :-D. The length array is in fact the only memory
   * allocated. */
  dt_free_align(hlength);
  dt_free_align(vlength);
}

#if defined(__SSE2__)
static void dt_interpolation_resample_sse(const struct dt_interpolation *itor, float *out,
                                          const dt_iop_roi_t *const roi_out, const int32_t out_stride,
                                          const float *const in, const dt_iop_roi_t *const roi_in,
                                          const int32_t in_stride)
{
  int *hindex = NULL;
  int *hlength = NULL;
  float *hkernel = NULL;
  int *vindex = NULL;
  int *vlength = NULL;
  float *vkernel = NULL;
  int *vmeta = NULL;

  int r;

  debug_info("resampling %p (%dx%d@%dx%d scale %f) -> %p (%dx%d@%dx%d scale %f)\n", in, roi_in->width,
             roi_in->height, roi_in->x, roi_in->y, roi_in->scale, out, roi_out->width, roi_out->height,
             roi_out->x, roi_out->y, roi_out->scale);

  // Fast code path for 1:1 copy, only cropping area can change
  if(roi_out->scale == 1.f)
  {
    const int x0 = roi_out->x * 4 * sizeof(float);
#if DEBUG_RESAMPLING_TIMING
    int64_t ts_resampling = getts();
#endif
#ifdef _OPENMP
#pragma omp parallel for default(none) shared(out)
#endif
    for(int y = 0; y < roi_out->height; y++)
    {
      float *i = (float *)((char *)in + (size_t)in_stride * (y + roi_out->y) + x0);
      float *o = (float *)((char *)out + (size_t)out_stride * y);
      memcpy(o, i, out_stride);
    }
#if DEBUG_RESAMPLING_TIMING
    ts_resampling = getts() - ts_resampling;
    fprintf(stderr, "resampling %p plan:0us resampling:%" PRId64 "us\n", in, ts_resampling);
#endif
    // All done, so easy case
    return;
  }

// Generic non 1:1 case... much more complicated :D
#if DEBUG_RESAMPLING_TIMING
  int64_t ts_plan = getts();
#endif

  // Prepare resampling plans once and for all
  r = prepare_resampling_plan(itor, roi_in->width, roi_in->x, roi_out->width, roi_out->x, roi_out->scale,
                              &hlength, &hkernel, &hindex, NULL);
  if(r)
  {
    goto exit;
  }

  r = prepare_resampling_plan(itor, roi_in->height, roi_in->y, roi_out->height, roi_out->y, roi_out->scale,
                              &vlength, &vkernel, &vindex, &vmeta);
  if(r)
  {
    goto exit;
  }

#if DEBUG_RESAMPLING_TIMING
  ts_plan = getts() - ts_plan;
#endif

#if DEBUG_RESAMPLING_TIMING
  int64_t ts_resampling = getts();
#endif

// Process each output line
#ifdef _OPENMP
#pragma omp parallel for default(none) shared(out, hindex, hlength, hkernel, vindex, vlength, vkernel, vmeta)
#endif
  for(int oy = 0; oy < roi_out->height; oy++)
  {
    // Initialize column resampling indexes
    int vlidx = vmeta[3 * oy + 0]; // V(ertical) L(ength) I(n)d(e)x
    int vkidx = vmeta[3 * oy + 1]; // V(ertical) K(ernel) I(n)d(e)x
    int viidx = vmeta[3 * oy + 2]; // V(ertical) I(ndex) I(n)d(e)x

    // Initialize row resampling indexes
    int hlidx = 0; // H(orizontal) L(ength) I(n)d(e)x
    int hkidx = 0; // H(orizontal) K(ernel) I(n)d(e)x
    int hiidx = 0; // H(orizontal) I(ndex) I(n)d(e)x

    // Number of lines contributing to the output line
    int vl = vlength[vlidx++]; // V(ertical) L(ength)

    // Process each output column
    for(int ox = 0; ox < roi_out->width; ox++)
    {
      debug_extra("output %p [% 4d % 4d]\n", out, ox, oy);

      // This will hold the resulting pixel
      __m128 vs = _mm_setzero_ps();

      // Number of horizontal samples contributing to the output
      int hl = hlength[hlidx++]; // H(orizontal) L(ength)

      for(int iy = 0; iy < vl; iy++)
      {
        // This is our input line
        const float *i = (float *)((char *)in + (size_t)in_stride * vindex[viidx++]);

        __m128 vhs = _mm_setzero_ps();

        for(int ix = 0; ix < hl; ix++)
        {
          // Apply the precomputed filter kernel
          size_t baseidx = (size_t)hindex[hiidx++] * 4;
          float htap = hkernel[hkidx++];
          __m128 vhtap = _mm_set_ps1(htap);
          vhs = _mm_add_ps(vhs, _mm_mul_ps(*(__m128 *)&i[baseidx], vhtap));
        }

        // Accumulate contribution from this line
        float vtap = vkernel[vkidx++];
        __m128 vvtap = _mm_set_ps1(vtap);
        vs = _mm_add_ps(vs, _mm_mul_ps(vhs, vvtap));

        // Reset horizontal resampling context
        hkidx -= hl;
        hiidx -= hl;
      }

      // Output pixel is ready
      float *o = (float *)((char *)out + (size_t)oy * out_stride + (size_t)ox * 4 * sizeof(float));
      _mm_stream_ps(o, vs);

      // Reset vertical resampling context
      viidx -= vl;
      vkidx -= vl;

      // Progress in horizontal context
      hiidx += hl;
      hkidx += hl;
    }
  }

  _mm_sfence();

#if DEBUG_RESAMPLING_TIMING
  ts_resampling = getts() - ts_resampling;
  fprintf(stderr, "resampling %p plan:%" PRId64 "us resampling:%" PRId64 "us\n", in, ts_plan, ts_resampling);
#endif

exit:
  /* Free the resampling plans. It's nasty to optimize allocs like that, but
   * it simplifies the code :-D. The length array is in fact the only memory
   * allocated. */
  dt_free_align(hlength);
  dt_free_align(vlength);
}
#endif

/** Applies resampling (re-scaling) on *full* input and output buffers.
 *  roi_in and roi_out define the part of the buffers that is affected.
 */
void dt_interpolation_resample(const struct dt_interpolation *itor, float *out,
                               const dt_iop_roi_t *const roi_out, const int32_t out_stride,
                               const float *const in, const dt_iop_roi_t *const roi_in,
                               const int32_t in_stride)
{
  if(darktable.codepath.OPENMP_SIMD)
    return dt_interpolation_resample_plain(itor, out, roi_out, out_stride, in, roi_in, in_stride);
#if defined(__SSE2__)
  else if(darktable.codepath.SSE2)
    return dt_interpolation_resample_sse(itor, out, roi_out, out_stride, in, roi_in, in_stride);
#endif
  else
    dt_unreachable_codepath();
}

/** Applies resampling (re-scaling) on a specific region-of-interest of an image. The input
 *  and output buffers hold exactly those roi's. roi_in and roi_out define the relative
 *  positions of the roi's within the full input and output image, respectively.
 */
void dt_interpolation_resample_roi(const struct dt_interpolation *itor, float *out,
                                   const dt_iop_roi_t *const roi_out, const int32_t out_stride,
                                   const float *const in, const dt_iop_roi_t *const roi_in,
                                   const int32_t in_stride)
{
  dt_iop_roi_t oroi = *roi_out;
  oroi.x = oroi.y = 0;

  dt_iop_roi_t iroi = *roi_in;
  iroi.x = iroi.y = 0;

  dt_interpolation_resample(itor, out, &oroi, out_stride, in, &iroi, in_stride);
}

#ifdef HAVE_OPENCL
dt_interpolation_cl_global_t *dt_interpolation_init_cl_global()
{
  dt_interpolation_cl_global_t *g
      = (dt_interpolation_cl_global_t *)malloc(sizeof(dt_interpolation_cl_global_t));

  const int program = 2; // basic.cl, from programs.conf
  g->kernel_interpolation_resample = dt_opencl_create_kernel(program, "interpolation_resample");
  return g;
}

void dt_interpolation_free_cl_global(dt_interpolation_cl_global_t *g)
{
  if(!g) return;
  // destroy kernels
  dt_opencl_free_kernel(g->kernel_interpolation_resample);
  free(g);
}

static uint32_t roundToNextPowerOfTwo(uint32_t x)
{
  x--;
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  x++;
  return x;
}

/** Applies resampling (re-scaling) on *full* input and output buffers.
 *  roi_in and roi_out define the part of the buffers that is affected.
 */
int dt_interpolation_resample_cl(const struct dt_interpolation *itor, int devid, cl_mem dev_out,
                                 const dt_iop_roi_t *const roi_out, cl_mem dev_in,
                                 const dt_iop_roi_t *const roi_in)
{
  int *hindex = NULL;
  int *hlength = NULL;
  float *hkernel = NULL;
  int *hmeta = NULL;
  int *vindex = NULL;
  int *vlength = NULL;
  float *vkernel = NULL;
  int *vmeta = NULL;

  int r;
  cl_int err = -999;

  cl_mem dev_hindex = NULL;
  cl_mem dev_hlength = NULL;
  cl_mem dev_hkernel = NULL;
  cl_mem dev_hmeta = NULL;
  cl_mem dev_vindex = NULL;
  cl_mem dev_vlength = NULL;
  cl_mem dev_vkernel = NULL;
  cl_mem dev_vmeta = NULL;

  debug_info("resampling_cl %p (%dx%d@%dx%d scale %f) -> %p (%dx%d@%dx%d scale %f)\n", (void *)dev_in,
             roi_in->width, roi_in->height, roi_in->x, roi_in->y, roi_in->scale, (void *)dev_out,
             roi_out->width, roi_out->height, roi_out->x, roi_out->y, roi_out->scale);

  // Fast code path for 1:1 copy, only cropping area can change
  if(roi_out->scale == 1.f)
  {
#if DEBUG_RESAMPLING_TIMING
    int64_t ts_resampling = getts();
#endif
    size_t iorigin[] = { roi_out->x, roi_out->y, 0 };
    size_t oorigin[] = { 0, 0, 0 };
    size_t region[] = { roi_out->width, roi_out->height, 1 };

    // copy original input from dev_in -> dev_out as starting point
    err = dt_opencl_enqueue_copy_image(devid, dev_in, dev_out, iorigin, oorigin, region);
    if(err != CL_SUCCESS) goto error;

#if DEBUG_RESAMPLING_TIMING
    ts_resampling = getts() - ts_resampling;
    fprintf(stderr, "resampling_cl %p plan:0us resampling:%" PRId64 "us\n", (void *)dev_in, ts_resampling);
#endif
    // All done, so easy case
    return CL_SUCCESS;
  }

// Generic non 1:1 case... much more complicated :D
#if DEBUG_RESAMPLING_TIMING
  int64_t ts_plan = getts();
#endif

  // Prepare resampling plans once and for all
  r = prepare_resampling_plan(itor, roi_in->width, roi_in->x, roi_out->width, roi_out->x, roi_out->scale,
                              &hlength, &hkernel, &hindex, &hmeta);
  if(r)
  {
    goto error;
  }

  r = prepare_resampling_plan(itor, roi_in->height, roi_in->y, roi_out->height, roi_out->y, roi_out->scale,
                              &vlength, &vkernel, &vindex, &vmeta);
  if(r)
  {
    goto error;
  }

  int hmaxtaps = -1, vmaxtaps = -1;
  for(int k = 0; k < roi_out->width; k++) hmaxtaps = MAX(hmaxtaps, hlength[k]);
  for(int k = 0; k < roi_out->height; k++) vmaxtaps = MAX(vmaxtaps, vlength[k]);

#if DEBUG_RESAMPLING_TIMING
  ts_plan = getts() - ts_plan;
#endif

#if DEBUG_RESAMPLING_TIMING
  int64_t ts_resampling = getts();
#endif

  // strategy: process image column-wise (local[0] = 1). For each row generate
  // a number of parallel work items each taking care of one horizontal convolution,
  // then sum over work items to do the vertical convolution

  const int kernel = darktable.opencl->interpolation->kernel_interpolation_resample;
  const int width = roi_out->width;
  const int height = roi_out->height;

  // make sure blocksize is not too large
  const int taps = roundToNextPowerOfTwo(vmaxtaps); // the number of work items per row rounded up to a power of 2
                                                    // (for quick recursive reduction)
  int vblocksize;

  dt_opencl_local_buffer_t locopt
    = (dt_opencl_local_buffer_t){ .xoffset = 0, .xfactor = 1, .yoffset = 0, .yfactor = 1,
                                  .cellsize = 4 * sizeof(float), .overhead = hmaxtaps * sizeof(float) + hmaxtaps * sizeof(int),
                                  .sizex = 1, .sizey = (1 << 16) * taps };

  if(dt_opencl_local_buffer_opt(devid, kernel, &locopt))
    vblocksize = locopt.sizey;
  else
    vblocksize = 1;

  if(vblocksize < taps)
  {
    // our strategy does not work: the vertical number of taps exceeds the vertical workgroupsize;
    // there is no point in continuing on the GPU - that would be way too slow; let's delegate the stuff to
    // the CPU then.
    dt_print(
        DT_DEBUG_OPENCL,
        "[opencl_resampling] resampling plan cannot efficiently be run on the GPU - fall back to CPU.\n");
    goto error;
  }

  size_t sizes[3] = { ROUNDUPWD(width), ROUNDUP(height * taps, vblocksize), 1 };
  size_t local[3] = { 1, vblocksize, 1 };

  // store resampling plan to device memory
  // hindex, vindex, hkernel, vkernel: (v|h)maxtaps might be too small, so store a bit more than needed
  dev_hindex = dt_opencl_copy_host_to_device_constant(devid, sizeof(int) * width * (hmaxtaps + 1), hindex);
  if(dev_hindex == NULL) goto error;

  dev_hlength = dt_opencl_copy_host_to_device_constant(devid, sizeof(int) * width, hlength);
  if(dev_hlength == NULL) goto error;

  dev_hkernel
      = dt_opencl_copy_host_to_device_constant(devid, sizeof(float) * width * (hmaxtaps + 1), hkernel);
  if(dev_hkernel == NULL) goto error;

  dev_hmeta = dt_opencl_copy_host_to_device_constant(devid, sizeof(int) * width * 3, hmeta);
  if(dev_hmeta == NULL) goto error;

  dev_vindex = dt_opencl_copy_host_to_device_constant(devid, sizeof(int) * height * (vmaxtaps + 1), vindex);
  if(dev_vindex == NULL) goto error;

  dev_vlength = dt_opencl_copy_host_to_device_constant(devid, sizeof(int) * height, vlength);
  if(dev_vlength == NULL) goto error;

  dev_vkernel
      = dt_opencl_copy_host_to_device_constant(devid, sizeof(float) * height * (vmaxtaps + 1), vkernel);
  if(dev_vkernel == NULL) goto error;

  dev_vmeta = dt_opencl_copy_host_to_device_constant(devid, sizeof(int) * height * 3, vmeta);
  if(dev_vmeta == NULL) goto error;

  dt_opencl_set_kernel_arg(devid, kernel, 0, sizeof(cl_mem), (void *)&dev_in);
  dt_opencl_set_kernel_arg(devid, kernel, 1, sizeof(cl_mem), (void *)&dev_out);
  dt_opencl_set_kernel_arg(devid, kernel, 2, sizeof(int), (void *)&width);
  dt_opencl_set_kernel_arg(devid, kernel, 3, sizeof(int), (void *)&height);
  dt_opencl_set_kernel_arg(devid, kernel, 4, sizeof(cl_mem), (void *)&dev_hmeta);
  dt_opencl_set_kernel_arg(devid, kernel, 5, sizeof(cl_mem), (void *)&dev_vmeta);
  dt_opencl_set_kernel_arg(devid, kernel, 6, sizeof(cl_mem), (void *)&dev_hlength);
  dt_opencl_set_kernel_arg(devid, kernel, 7, sizeof(cl_mem), (void *)&dev_vlength);
  dt_opencl_set_kernel_arg(devid, kernel, 8, sizeof(cl_mem), (void *)&dev_hindex);
  dt_opencl_set_kernel_arg(devid, kernel, 9, sizeof(cl_mem), (void *)&dev_vindex);
  dt_opencl_set_kernel_arg(devid, kernel, 10, sizeof(cl_mem), (void *)&dev_hkernel);
  dt_opencl_set_kernel_arg(devid, kernel, 11, sizeof(cl_mem), (void *)&dev_vkernel);
  dt_opencl_set_kernel_arg(devid, kernel, 12, sizeof(int), (void *)&hmaxtaps);
  dt_opencl_set_kernel_arg(devid, kernel, 13, sizeof(int), (void *)&taps);
  dt_opencl_set_kernel_arg(devid, kernel, 14, hmaxtaps * sizeof(float), NULL);
  dt_opencl_set_kernel_arg(devid, kernel, 15, hmaxtaps * sizeof(int), NULL);
  dt_opencl_set_kernel_arg(devid, kernel, 16, vblocksize * 4 * sizeof(float), NULL);
  err = dt_opencl_enqueue_kernel_2d_with_local(devid, kernel, sizes, local);
  if(err != CL_SUCCESS) goto error;

#if DEBUG_RESAMPLING_TIMING
  ts_resampling = getts() - ts_resampling;
  fprintf(stderr, "resampling_cl %p plan:%" PRId64 "us resampling:%" PRId64 "us\n", (void *)dev_in, ts_plan,
          ts_resampling);
#endif

  dt_opencl_release_mem_object(dev_hindex);
  dt_opencl_release_mem_object(dev_hlength);
  dt_opencl_release_mem_object(dev_hkernel);
  dt_opencl_release_mem_object(dev_hmeta);
  dt_opencl_release_mem_object(dev_vindex);
  dt_opencl_release_mem_object(dev_vlength);
  dt_opencl_release_mem_object(dev_vkernel);
  dt_opencl_release_mem_object(dev_vmeta);
  dt_free_align(hlength);
  dt_free_align(vlength);
  return CL_SUCCESS;

error:
  dt_opencl_release_mem_object(dev_hindex);
  dt_opencl_release_mem_object(dev_hlength);
  dt_opencl_release_mem_object(dev_hkernel);
  dt_opencl_release_mem_object(dev_hmeta);
  dt_opencl_release_mem_object(dev_vindex);
  dt_opencl_release_mem_object(dev_vlength);
  dt_opencl_release_mem_object(dev_vkernel);
  dt_opencl_release_mem_object(dev_vmeta);
  dt_free_align(hlength);
  dt_free_align(vlength);
  dt_print(DT_DEBUG_OPENCL, "[opencl_resampling] couldn't enqueue kernel! %d\n", err);
  return err;
}

/** Applies resampling (re-scaling) on a specific region-of-interest of an image. The input
 *  and output buffers hold exactly those roi's. roi_in and roi_out define the relative
 *  positions of the roi's within the full input and output image, respectively.
 */
int dt_interpolation_resample_roi_cl(const struct dt_interpolation *itor, int devid, cl_mem dev_out,
                                     const dt_iop_roi_t *const roi_out, cl_mem dev_in,
                                     const dt_iop_roi_t *const roi_in)
{
  dt_iop_roi_t oroi = *roi_out;
  oroi.x = oroi.y = 0;

  dt_iop_roi_t iroi = *roi_in;
  iroi.x = iroi.y = 0;

  return dt_interpolation_resample_cl(itor, devid, dev_out, &oroi, dev_in, &iroi);
}
#endif

static void dt_interpolation_resample_1c_plain(const struct dt_interpolation *itor, float *out,
                                               const dt_iop_roi_t *const roi_out, const int32_t out_stride,
                                               const float *const in, const dt_iop_roi_t *const roi_in,
                                               const int32_t in_stride)
{
  int *hindex = NULL;
  int *hlength = NULL;
  float *hkernel = NULL;
  int *vindex = NULL;
  int *vlength = NULL;
  float *vkernel = NULL;
  int *vmeta = NULL;

  int r;

  debug_info("resampling %p (%dx%d@%dx%d scale %f) -> %p (%dx%d@%dx%d scale %f)\n", in, roi_in->width,
             roi_in->height, roi_in->x, roi_in->y, roi_in->scale, out, roi_out->width, roi_out->height,
             roi_out->x, roi_out->y, roi_out->scale);

  // Fast code path for 1:1 copy, only cropping area can change
  if(roi_out->scale == 1.f)
  {
    const int x0 = roi_out->x * sizeof(float);
#if DEBUG_RESAMPLING_TIMING
    int64_t ts_resampling = getts();
#endif
#ifdef _OPENMP
#pragma omp parallel for default(none) shared(out)
#endif
    for(int y = 0; y < roi_out->height; y++)
    {
      float *i = (float *)((char *)in + (size_t)in_stride * (y + roi_out->y) + x0);
      float *o = (float *)((char *)out + (size_t)out_stride * y);
      memcpy(o, i, out_stride);
    }
#if DEBUG_RESAMPLING_TIMING
    ts_resampling = getts() - ts_resampling;
    fprintf(stderr, "resampling %p plan:0us resampling:%" PRId64 "us\n", in, ts_resampling);
#endif
    // All done, so easy case
    return;
  }

  // Generic non 1:1 case... much more complicated :D
#if DEBUG_RESAMPLING_TIMING
  int64_t ts_plan = getts();
#endif

  // Prepare resampling plans once and for all
  r = prepare_resampling_plan(itor, roi_in->width, roi_in->x, roi_out->width, roi_out->x, roi_out->scale,
                              &hlength, &hkernel, &hindex, NULL);
  if(r)
  {
    goto exit;
  }

  r = prepare_resampling_plan(itor, roi_in->height, roi_in->y, roi_out->height, roi_out->y, roi_out->scale,
                              &vlength, &vkernel, &vindex, &vmeta);
  if(r)
  {
    goto exit;
  }

#if DEBUG_RESAMPLING_TIMING
  ts_plan = getts() - ts_plan;
#endif

#if DEBUG_RESAMPLING_TIMING
  int64_t ts_resampling = getts();
#endif

  // Process each output line
#ifdef _OPENMP
#pragma omp parallel for default(none) shared(out, hindex, hlength, hkernel, vindex, vlength, vkernel, vmeta)
#endif
  for(int oy = 0; oy < roi_out->height; oy++)
  {
    // Initialize column resampling indexes
    int vlidx = vmeta[3 * oy + 0]; // V(ertical) L(ength) I(n)d(e)x
    int vkidx = vmeta[3 * oy + 1]; // V(ertical) K(ernel) I(n)d(e)x
    int viidx = vmeta[3 * oy + 2]; // V(ertical) I(ndex) I(n)d(e)x

    // Initialize row resampling indexes
    int hlidx = 0; // H(orizontal) L(ength) I(n)d(e)x
    int hkidx = 0; // H(orizontal) K(ernel) I(n)d(e)x
    int hiidx = 0; // H(orizontal) I(ndex) I(n)d(e)x

    // Number of lines contributing to the output line
    int vl = vlength[vlidx++]; // V(ertical) L(ength)

    // Process each output column
    for(int ox = 0; ox < roi_out->width; ox++)
    {
      debug_extra("output %p [% 4d % 4d]\n", out, ox, oy);

      // This will hold the resulting pixel
      float vs = 0.0f;

      // Number of horizontal samples contributing to the output
      int hl = hlength[hlidx++]; // H(orizontal) L(ength)

      for(int iy = 0; iy < vl; iy++)
      {
        // This is our input line
        const float *i = (float *)((char *)in + (size_t)in_stride * vindex[viidx++]);

        float vhs = 0.0f;

        for(int ix = 0; ix < hl; ix++)
        {
          // Apply the precomputed filter kernel
          size_t baseidx = (size_t)hindex[hiidx++];
          const float htap = hkernel[hkidx++];
          vhs += i[baseidx] * htap;
        }

        // Accumulate contribution from this line
        const float vtap = vkernel[vkidx++];
        vs += vhs * vtap;

        // Reset horizontal resampling context
        hkidx -= hl;
        hiidx -= hl;
      }

      // Output pixel is ready
      float *o = (float *)((char *)out + (size_t)oy * out_stride + (size_t)ox * sizeof(float));
      *o = vs;

      // Reset vertical resampling context
      viidx -= vl;
      vkidx -= vl;

      // Progress in horizontal context
      hiidx += hl;
      hkidx += hl;
    }
  }

#if DEBUG_RESAMPLING_TIMING
  ts_resampling = getts() - ts_resampling;
  fprintf(stderr, "resampling %p plan:%" PRId64 "us resampling:%" PRId64 "us\n", in, ts_plan, ts_resampling);
#endif

  exit:
  /* Free the resampling plans. It's nasty to optimize allocs like that, but
   * it simplifies the code :-D. The length array is in fact the only memory
   * allocated. */
  dt_free_align(hlength);
  dt_free_align(vlength);
}

/** Applies resampling (re-scaling) on *full* input and output buffers.
 *  roi_in and roi_out define the part of the buffers that is affected.
 */
void dt_interpolation_resample_1c(const struct dt_interpolation *itor, float *out,
                                  const dt_iop_roi_t *const roi_out, const int32_t out_stride,
                                  const float *const in, const dt_iop_roi_t *const roi_in,
                                  const int32_t in_stride)
{
  return dt_interpolation_resample_1c_plain(itor, out, roi_out, out_stride, in, roi_in, in_stride);
}

/** Applies resampling (re-scaling) on a specific region-of-interest of an image. The input
 *  and output buffers hold exactly those roi's. roi_in and roi_out define the relative
 *  positions of the roi's within the full input and output image, respectively.
 */
void dt_interpolation_resample_roi_1c(const struct dt_interpolation *itor, float *out,
                                      const dt_iop_roi_t *const roi_out, const int32_t out_stride,
                                      const float *const in, const dt_iop_roi_t *const roi_in,
                                      const int32_t in_stride)
{
  dt_iop_roi_t oroi = *roi_out;
  oroi.x = oroi.y = 0;

  dt_iop_roi_t iroi = *roi_in;
  iroi.x = iroi.y = 0;

  dt_interpolation_resample_1c(itor, out, &oroi, out_stride, in, &iroi, in_stride);
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
