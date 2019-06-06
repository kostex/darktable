/*
    This file is part of darktable,
    copyright (c) 2016 johannes hanika.

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
#include "common/locallaplacian.h"

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#if defined(__SSE2__)
#include <xmmintrin.h>
#endif

// downsample width/height to given level
static inline int dl(int size, const int level)
{
  for(int l=0;l<level;l++)
    size = (size-1)/2+1;
  return size;
}

// needs a boundary of 1 or 2px around i,j or else it will crash.
// (translates to a 1px boundary around the corresponding pixel in the coarse buffer)
// more precisely, 1<=i<wd-1 for even wd and
//                 1<=i<wd-2 for odd wd (j likewise with ht)
static inline float ll_expand_gaussian(
    const float *const coarse,
    const int i,
    const int j,
    const int wd,
    const int ht)
{
  assert(i > 0);
  assert(i < wd-1);
  assert(j > 0);
  assert(j < ht-1);
  assert(j/2 + 1 < (ht-1)/2+1);
  assert(i/2 + 1 < (wd-1)/2+1);
  const int cw = (wd-1)/2+1;
  const int ind = (j/2)*cw+i/2;
  // case 0:     case 1:     case 2:     case 3:
  //  x . x . x   x . x . x   x . x . x   x . x . x
  //  . . . . .   . . . . .   . .[.]. .   .[.]. . .
  //  x .[x]. x   x[.]x . x   x . x . x   x . x . x
  //  . . . . .   . . . . .   . . . . .   . . . . .
  //  x . x . x   x . x . x   x . x . x   x . x . x
  switch((i&1) + 2*(j&1))
  {
    case 0: // both are even, 3x3 stencil
      return 4./256. * (
          6.0f*(coarse[ind-cw] + coarse[ind-1] + 6.0f*coarse[ind] + coarse[ind+1] + coarse[ind+cw])
          + coarse[ind-cw-1] + coarse[ind-cw+1] + coarse[ind+cw-1] + coarse[ind+cw+1]);
    case 1: // i is odd, 2x3 stencil
      return 4./256. * (
          24.0*(coarse[ind] + coarse[ind+1]) +
          4.0*(coarse[ind-cw] + coarse[ind-cw+1] + coarse[ind+cw] + coarse[ind+cw+1]));
    case 2: // j is odd, 3x2 stencil
      return 4./256. * (
          24.0*(coarse[ind] + coarse[ind+cw]) +
          4.0*(coarse[ind-1] + coarse[ind+1] + coarse[ind+cw-1] + coarse[ind+cw+1]));
    default: // case 3: // both are odd, 2x2 stencil
      return .25f * (coarse[ind] + coarse[ind+1] + coarse[ind+cw] + coarse[ind+cw+1]);
  }
}

// helper to fill in one pixel boundary by copying it
static inline void ll_fill_boundary1(
    float *const input,
    const int wd,
    const int ht)
{
  for(int j=1;j<ht-1;j++) input[j*wd] = input[j*wd+1];
  for(int j=1;j<ht-1;j++) input[j*wd+wd-1] = input[j*wd+wd-2];
  memcpy(input,    input+wd, sizeof(float)*wd);
  memcpy(input+wd*(ht-1), input+wd*(ht-2), sizeof(float)*wd);
}

// helper to fill in two pixels boundary by copying it
static inline void ll_fill_boundary2(
    float *const input,
    const int wd,
    const int ht)
{
  for(int j=1;j<ht-1;j++) input[j*wd] = input[j*wd+1];
  if(wd & 1) for(int j=1;j<ht-1;j++) input[j*wd+wd-1] = input[j*wd+wd-2];
  else       for(int j=1;j<ht-1;j++) input[j*wd+wd-1] = input[j*wd+wd-2] = input[j*wd+wd-3];
  memcpy(input, input+wd, sizeof(float)*wd);
  if(!(ht & 1)) memcpy(input+wd*(ht-2), input+wd*(ht-3), sizeof(float)*wd);
  memcpy(input+wd*(ht-1), input+wd*(ht-2), sizeof(float)*wd);
}

static inline void gauss_expand(
    const float *const input, // coarse input
    float *const fine,        // upsampled, blurry output
    const int wd,             // fine res
    const int ht)
{
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(fine, input, wd, ht) \
  schedule(static) \
  collapse(2)
#endif
  for(int j=1;j<((ht-1)&~1);j++)  // even ht: two px boundary. odd ht: one px.
    for(int i=1;i<((wd-1)&~1);i++)
      fine[j*wd+i] = ll_expand_gaussian(input, i, j, wd, ht);
  ll_fill_boundary2(fine, wd, ht);
}

#if defined(__SSE2__)
static inline void gauss_reduce_sse2(
    const float *const input, // fine input buffer
    float *const coarse,      // coarse scale, blurred input buf
    const int wd,             // fine res
    const int ht)
{
  // blur, store only coarse res
  const int cw = (wd-1)/2+1, ch = (ht-1)/2+1;

  // this version is inspired by opencv's pyrDown_ :
  // - allocate 5 rows of ring buffer (aligned)
  // - for coarse res y
  //   - fill 5 coarse-res row buffers with 1 4 6 4 1 weights (reuse some from last time)
  //   - do vertical convolution via sse and write to coarse output buf

  const int stride = ((cw+8)&~7); // assure sse alignment of rows
  float *ringbuf = dt_alloc_align(64, sizeof(*ringbuf)*stride*5);
  float *rows[5] = {0};
  int rowj = 0; // we initialised this many rows so far

  for(int j=1;j<ch-1;j++)
  {
    // horizontal pass, convolve with 1 4 6 4 1 kernel and decimate
    for(;rowj<=2*j+2;rowj++)
    {
      float *const row = ringbuf + (rowj % 5)*stride;
      const float *const in = input + rowj*wd;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
      dt_omp_firstprivate(cw, in, row) \
      schedule(static)
#endif
      for(int i=1;i<cw-1;i++)
        row[i] = 6*in[2*i] + 4*(in[2*i-1]+in[2*i+1]) + in[2*i-2] + in[2*i+2];
    }

    // init row pointers
    for(int k=0;k<5;k++)
      rows[k] = ringbuf + ((2*j-2+k)%5)*stride;

    // vertical pass, convolve and decimate using SIMD:
    // note that we're ignoring the (1..cw-1) buffer limit, we'll pull in
    // garbage and fix it later by border filling.
    float *const out = coarse + j*cw;
    const float *const row0 = rows[0], *const row1 = rows[1],
                *const row2 = rows[2], *const row3 = rows[3], *const row4 = rows[4];
    const __m128 four = _mm_set1_ps(4.f), scale = _mm_set1_ps(1.f/256.f);
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    dt_omp_firstprivate(cw, out, scale, four, row0, row1, row2, row3, row4) \
    schedule(static)
#endif
    for(int i=0;i<=cw-8;i+=8)
    {
      __m128 r0, r1, r2, r3, r4, t0, t1;
      r0 = _mm_load_ps(row0 + i);
      r1 = _mm_load_ps(row1 + i);
      r2 = _mm_load_ps(row2 + i);
      r3 = _mm_load_ps(row3 + i);
      r4 = _mm_load_ps(row4 + i);
      r0 = _mm_add_ps(r0, r4);
      r1 = _mm_add_ps(_mm_add_ps(r1, r3), r2);
      r0 = _mm_add_ps(r0, _mm_add_ps(r2, r2));
      t0 = _mm_add_ps(r0, _mm_mul_ps(r1, four));

      r0 = _mm_load_ps(row0 + i + 4);
      r1 = _mm_load_ps(row1 + i + 4);
      r2 = _mm_load_ps(row2 + i + 4);
      r3 = _mm_load_ps(row3 + i + 4);
      r4 = _mm_load_ps(row4 + i + 4);
      r0 = _mm_add_ps(r0, r4);
      r1 = _mm_add_ps(_mm_add_ps(r1, r3), r2);
      r0 = _mm_add_ps(r0, _mm_add_ps(r2, r2));
      t1 = _mm_add_ps(r0, _mm_mul_ps(r1, four));

      t0 = _mm_mul_ps(t0, scale);
      t1 = _mm_mul_ps(t1, scale);

      _mm_storeu_ps(out + i, t0);
      _mm_storeu_ps(out + i + 4, t1);
    }
    // process the rest
    for(int i=cw&~7;i<cw-1;i++)
      out[i] = (6*row2[i] + 4*(row1[i] + row3[i]) + row0[i] + row4[i])*(1.0f/256.0f);
  }
  dt_free_align(ringbuf);
  ll_fill_boundary1(coarse, cw, ch);
}
#endif

static inline void gauss_reduce(
    const float *const input, // fine input buffer
    float *const coarse,      // coarse scale, blurred input buf
    const int wd,             // fine res
    const int ht)
{
  // blur, store only coarse res
  const int cw = (wd-1)/2+1, ch = (ht-1)/2+1;

  // this is the scalar (non-simd) code:
  const float a = 0.4f;
  const float w[5] = {1./4.-a/2., 1./4., a, 1./4., 1./4.-a/2.};
  memset(coarse, 0, sizeof(float)*cw*ch);
  // direct 5x5 stencil only on required pixels:
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(coarse, cw, ch, input, w, wd) \
  schedule(static) \
  collapse(2)
#endif
  for(int j=1;j<ch-1;j++) for(int i=1;i<cw-1;i++)
    for(int jj=-2;jj<=2;jj++) for(int ii=-2;ii<=2;ii++)
      coarse[j*cw+i] += input[(2*j+jj)*wd+2*i+ii] * w[ii+2] * w[jj+2];
  ll_fill_boundary1(coarse, cw, ch);
}

// allocate output buffer with monochrome brightness channel from input, padded
// up by max_supp on all four sides, dimensions written to wd2 ht2
static inline float *ll_pad_input(
    const float *const input,
    const int wd,
    const int ht,
    const int max_supp,
    int *wd2,
    int *ht2,
    local_laplacian_boundary_t *b)
{
  const int stride = 4;
  *wd2 = 2*max_supp + wd;
  *ht2 = 2*max_supp + ht;
  float *const out = dt_alloc_align(64, *wd2**ht2*sizeof(*out));

  if(b && b->mode == 2)
  { // pad by preview buffer
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    dt_omp_firstprivate(ht, input, max_supp, out, wd, stride) \
    shared(wd2, ht2) \
    schedule(dynamic) \
    collapse(2)
#endif // fill regular pixels:
    for(int j=0;j<ht;j++) for(int i=0;i<wd;i++)
      out[(j+max_supp)**wd2+i+max_supp] = input[stride*(wd*j+i)] * 0.01f; // L -> [0,1]

    // for all out of roi pixels on the boundary we wish to pad:
    // compute coordinate in full image.
    // if not out of buf:
    //   compute padded preview pixel coordinate (clamp to padded preview buffer size)
    // else
    //   pad as usual (hi-res sample and hold)
#define LL_FILL(fallback) do {\
    float isx = ((i - max_supp) + b->roi->x)/b->roi->scale;\
    float isy = ((j - max_supp) + b->roi->y)/b->roi->scale;\
    if(isx < 0 || isy >= b->buf->width\
    || isy < 0 || isy >= b->buf->height)\
      out[*wd2*j+i] = (fallback);\
    else\
    {\
      int px = CLAMP(isx / (float)b->buf->width  * b->wd + (b->pwd-b->wd)/2, 0, b->pwd-1);\
      int py = CLAMP(isy / (float)b->buf->height * b->ht + (b->pht-b->ht)/2, 0, b->pht-1);\
      /* TODO: linear interpolation?*/\
      out[*wd2*j+i] = b->pad0[b->pwd*py+px];\
    } } while(0)
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    dt_omp_firstprivate(input, max_supp, out, wd, stride) \
    shared(wd2, ht2, b) \
    schedule(dynamic) \
    collapse(2)
#endif // left border
    for(int j=max_supp;j<*ht2-max_supp;j++) for(int i=0;i<max_supp;i++)
      LL_FILL(input[stride*wd*(j-max_supp)]* 0.01f);
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    dt_omp_firstprivate(input, max_supp, out, stride, wd) \
    shared(wd2, ht2, b) \
    schedule(dynamic) \
    collapse(2)
#endif // right border
    for(int j=max_supp;j<*ht2-max_supp;j++) for(int i=wd+max_supp;i<*wd2;i++)
      LL_FILL(input[stride*((j-max_supp)*wd+wd-1)] * 0.01f);
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    dt_omp_firstprivate(max_supp, out) \
    shared(wd2, ht2, b) \
    schedule(dynamic) \
    collapse(2)
#endif // top border
    for(int j=0;j<max_supp;j++) for(int i=0;i<*wd2;i++)
      LL_FILL(out[*wd2*max_supp+i]);
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    dt_omp_firstprivate(ht, max_supp, out) \
    shared(wd2, ht2, b) \
    schedule(dynamic) \
    collapse(2)
#endif // bottom border
    for(int j=max_supp+ht;j<*ht2;j++) for(int i=0;i<*wd2;i++)
      LL_FILL(out[*wd2*(max_supp+ht-1)+i]);
#undef LL_FILL
  }
  else
  { // pad by replication:
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    dt_omp_firstprivate(input, ht, max_supp, out, wd, stride) \
    shared(wd2, ht2) \
    schedule(dynamic)
#endif
    for(int j=0;j<ht;j++)
    {
      for(int i=0;i<max_supp;i++)
        out[(j+max_supp)**wd2+i] = input[stride*wd*j]* 0.01f; // L -> [0,1]
      for(int i=0;i<wd;i++)
        out[(j+max_supp)**wd2+i+max_supp] = input[stride*(wd*j+i)] * 0.01f; // L -> [0,1]
      for(int i=wd+max_supp;i<*wd2;i++)
        out[(j+max_supp)**wd2+i] = input[stride*(j*wd+wd-1)] * 0.01f; // L -> [0,1]
    }
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    dt_omp_firstprivate(max_supp, out) \
    shared(wd2, ht2) \
    schedule(dynamic)
#endif
    for(int j=0;j<max_supp;j++)
      memcpy(out + *wd2*j, out+max_supp**wd2, sizeof(float)**wd2);
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    schedule(dynamic) \
    dt_omp_firstprivate(ht, max_supp, out) \
    shared(wd2, ht2)
#endif
    for(int j=max_supp+ht;j<*ht2;j++)
      memcpy(out + *wd2*j, out + *wd2*(max_supp+ht-1), sizeof(float)**wd2);
  }
#if 0
  if(b && b->mode == 2)
  {
    FILE *f = fopen("/tmp/padded.pfm", "wb");
    fprintf(f, "PF\n%d %d\n-1.0\n", *wd2, *ht2);
    for(int j=0;j<*ht2;j++)
      for(int i=0;i<*wd2;i++)
        for(int c=0;c<3;c++)
          fwrite(out + *wd2*j+i, 1, sizeof(float), f);
    fclose(f);
  }
#endif
  return out;
}

static inline float ll_laplacian(
    const float *const coarse,   // coarse res gaussian
    const float *const fine,     // fine res gaussian
    const int i,                 // fine index
    const int j,
    const int wd,                // fine width
    const int ht)                // fine height
{
  const float c = ll_expand_gaussian(coarse,
      CLAMPS(i, 1, ((wd-1)&~1)-1), CLAMPS(j, 1, ((ht-1)&~1)-1), wd, ht);
  return fine[j*wd+i] - c;
}

static inline float curve_scalar(
    const float x,
    const float g,
    const float sigma,
    const float shadows,
    const float highlights,
    const float clarity)
{
  const float c = x-g;
  float val;
  // blend in via quadratic bezier
  if     (c >  2*sigma) val = g + sigma + shadows    * (c-sigma);
  else if(c < -2*sigma) val = g - sigma + highlights * (c+sigma);
  else if(c > 0.0f)
  { // shadow contrast
    const float t = CLAMPS(c / (2.0f*sigma), 0.0f, 1.0f);
    const float t2 = t * t;
    const float mt = 1.0f-t;
    val = g + sigma * 2.0f*mt*t + t2*(sigma + sigma*shadows);
  }
  else
  { // highlight contrast
    const float t = CLAMPS(-c / (2.0f*sigma), 0.0f, 1.0f);
    const float t2 = t * t;
    const float mt = 1.0f-t;
    val = g - sigma * 2.0f*mt*t + t2*(- sigma - sigma*highlights);
  }
  // midtone local contrast
  val += clarity * c * dt_fast_expf(-c*c/(2.0*sigma*sigma/3.0f));
  return val;
}

#if defined(__SSE2__)
static inline __m128 curve_vec4(
    const __m128 x,
    const __m128 g,
    const __m128 sigma,
    const __m128 shadows,
    const __m128 highlights,
    const __m128 clarity)
{
  // TODO: pull these non-data dependent constants out of the loop to see
  // whether the compiler fail to do so
  const __m128 const0 = _mm_set_ps1(0x3f800000u);
  const __m128 const1 = _mm_set_ps1(0x402DF854u); // for e^x
  const __m128 sign_mask = _mm_set1_ps(-0.f); // -0.f = 1 << 31
  const __m128 one = _mm_set1_ps(1.0f);
  const __m128 two = _mm_set1_ps(2.0f);
  const __m128 twothirds = _mm_set1_ps(2.0f/3.0f);
  const __m128 twosig = _mm_mul_ps(two, sigma);
  const __m128 sigma2 = _mm_mul_ps(sigma, sigma);
  const __m128 s22 = _mm_mul_ps(twothirds, sigma2);

  const __m128 c = _mm_sub_ps(x, g);
  const __m128 select = _mm_cmplt_ps(c, _mm_setzero_ps());
  // select shadows or highlights as multiplier for linear part, based on c < 0
  const __m128 shadhi = _mm_or_ps(_mm_andnot_ps(select, shadows), _mm_and_ps(select, highlights));
  // flip sign bit of sigma based on c < 0 (c < 0 ? - sigma : sigma)
  const __m128 ssigma = _mm_xor_ps(sigma, _mm_and_ps(select, sign_mask));
  // this contains the linear parts valid for c > 2*sigma or c < - 2*sigma
  const __m128 vlin = _mm_add_ps(g, _mm_add_ps(ssigma, _mm_mul_ps(shadhi, _mm_sub_ps(c, ssigma))));

  const __m128 t = _mm_min_ps(one, _mm_max_ps(_mm_setzero_ps(),
        _mm_div_ps(c, _mm_mul_ps(two, ssigma))));
  const __m128 t2 = _mm_mul_ps(t, t);
  const __m128 mt = _mm_sub_ps(one, t);

  // midtone value fading over to linear part, without local contrast:
  const __m128 vmid = _mm_add_ps(g,
      _mm_add_ps(_mm_mul_ps(_mm_mul_ps(ssigma, two), _mm_mul_ps(mt, t)),
        _mm_mul_ps(t2, _mm_add_ps(ssigma, _mm_mul_ps(ssigma, shadhi)))));

  // c > 2*sigma?
  const __m128 linselect = _mm_cmpgt_ps(_mm_andnot_ps(sign_mask, c), twosig);
  const __m128 val = _mm_or_ps(_mm_and_ps(linselect, vlin), _mm_andnot_ps(linselect, vmid));

  // midtone local contrast
  // dt_fast_expf in sse:
  const __m128 arg = _mm_xor_ps(sign_mask, _mm_div_ps(_mm_mul_ps(c, c), s22));
  const __m128 k0 = _mm_add_ps(const0, _mm_mul_ps(arg, _mm_sub_ps(const1, const0)));
  const __m128 k = _mm_max_ps(k0, _mm_setzero_ps());
  const __m128i ki = _mm_cvtps_epi32(k);
  const __m128 gauss = _mm_load_ps((float*)&ki);
  const __m128 vcon = _mm_mul_ps(clarity, _mm_mul_ps(c, gauss));
  return _mm_add_ps(val, vcon);
}

// sse (4-wide)
void apply_curve_sse2(
    float *const out,
    const float *const in,
    const uint32_t w,
    const uint32_t h,
    const uint32_t padding,
    const float g,
    const float sigma,
    const float shadows,
    const float highlights,
    const float clarity)
{
  // TODO: do all this in avx2 8-wide (should be straight forward):
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(clarity, g, h, highlights, in, out, padding, shadows, sigma, w) \
  schedule(dynamic)
#endif
  for(uint32_t j=padding;j<h-padding;j++)
  {
    const float *in2  = in  + j*w + padding;
    float *out2 = out + j*w + padding;
    // find 4-byte aligned block in the middle:
    const float *const beg = (float *)((size_t)(out2+3)&(size_t)0x10ul);
    const float *const end = (float *)((size_t)(out2+w-padding)&(size_t)0x10ul);
    const float *const fin = out2+w-padding;
    const __m128 g4 = _mm_set1_ps(g);
    const __m128 sig4 = _mm_set1_ps(sigma);
    const __m128 shd4 = _mm_set1_ps(shadows);
    const __m128 hil4 = _mm_set1_ps(highlights);
    const __m128 clr4 = _mm_set1_ps(clarity);
    for(;out2<beg;out2++,in2++)
      *out2 = curve_scalar(*in2, g, sigma, shadows, highlights, clarity);
    for(;out2<end;out2+=4,in2+=4)
      _mm_stream_ps(out2, curve_vec4(_mm_load_ps(in2), g4, sig4, shd4, hil4, clr4));
    for(;out2<fin;out2++,in2++)
      *out2 = curve_scalar(*in2, g, sigma, shadows, highlights, clarity);
    out2 = out + j*w;
    for(int i=0;i<padding;i++)   out2[i] = out2[padding];
    for(int i=w-padding;i<w;i++) out2[i] = out2[w-padding-1];
  }
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(out, padding, w) \
  schedule(dynamic)
#endif
  for(int j=0;j<padding;j++) memcpy(out + w*j, out+padding*w, sizeof(float)*w);
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(h, out, padding, w) \
  schedule(dynamic)
#endif
  for(int j=h-padding;j<h;j++) memcpy(out + w*j, out+w*(h-padding-1), sizeof(float)*w);
}
#endif

// scalar version
void apply_curve(
    float *const out,
    const float *const in,
    const uint32_t w,
    const uint32_t h,
    const uint32_t padding,
    const float g,
    const float sigma,
    const float shadows,
    const float highlights,
    const float clarity)
{
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(clarity, g, h, highlights, in, out, padding, sigma, shadows, w) \
  schedule(dynamic)
#endif
  for(uint32_t j=padding;j<h-padding;j++)
  {
    const float *in2  = in  + j*w + padding;
    float *out2 = out + j*w + padding;
    for(uint32_t i=padding;i<w-padding;i++)
      (*out2++) = curve_scalar(*(in2++), g, sigma, shadows, highlights, clarity);
    out2 = out + j*w;
    for(int i=0;i<padding;i++)   out2[i] = out2[padding];
    for(int i=w-padding;i<w;i++) out2[i] = out2[w-padding-1];
  }
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(out, padding, w) \
  schedule(dynamic)
#endif
  for(int j=0;j<padding;j++) memcpy(out + w*j, out+padding*w, sizeof(float)*w);
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(h, out, padding, w) \
  schedule(dynamic)
#endif
  for(int j=h-padding;j<h;j++) memcpy(out + w*j, out+w*(h-padding-1), sizeof(float)*w);
}

void local_laplacian_internal(
    const float *const input,   // input buffer in some Labx or yuvx format
    float *const out,           // output buffer with colour
    const int wd,               // width and
    const int ht,               // height of the input buffer
    const float sigma,          // user param: separate shadows/midtones/highlights
    const float shadows,        // user param: lift shadows
    const float highlights,     // user param: compress highlights
    const float clarity,        // user param: increase clarity/local contrast
    const int use_sse2,         // flag whether to use SSE version
    local_laplacian_boundary_t *b)
{
#define max_levels 30
#define num_gamma 6
  // don't divide by 2 more often than we can:
  const int num_levels = MIN(max_levels, 31-__builtin_clz(MIN(wd,ht)));
  int last_level = num_levels-1;
  if(b && b->mode == 2) // higher number here makes it less prone to aliasing and slower.
    last_level = num_levels > 4 ? 4 : num_levels-1;
  const int max_supp = 1<<last_level;
  int w, h;
  float *padded[max_levels] = {0};
  if(b && b->mode == 2)
    padded[0] = ll_pad_input(input, wd, ht, max_supp, &w, &h, b);
  else
    padded[0] = ll_pad_input(input, wd, ht, max_supp, &w, &h, 0);

  // allocate pyramid pointers for padded input
  for(int l=1;l<=last_level;l++)
    padded[l] = dt_alloc_align(64, sizeof(float)*dl(w,l)*dl(h,l));

  // allocate pyramid pointers for output
  float *output[max_levels] = {0};
  for(int l=0;l<=last_level;l++)
    output[l] = dt_alloc_align(64, sizeof(float)*dl(w,l)*dl(h,l));

  // create gauss pyramid of padded input, write coarse directly to output
#if defined(__SSE2__)
  if(use_sse2)
  {
    for(int l=1;l<last_level;l++)
      gauss_reduce_sse2(padded[l-1], padded[l], dl(w,l-1), dl(h,l-1));
    gauss_reduce_sse2(padded[last_level-1], output[last_level], dl(w,last_level-1), dl(h,last_level-1));
  }
  else
#endif
  {
    for(int l=1;l<last_level;l++)
      gauss_reduce(padded[l-1], padded[l], dl(w,l-1), dl(h,l-1));
    gauss_reduce(padded[last_level-1], output[last_level], dl(w,last_level-1), dl(h,last_level-1));
  }

  // evenly sample brightness [0,1]:
  float gamma[num_gamma] = {0.0f};
  for(int k=0;k<num_gamma;k++) gamma[k] = (k+.5f)/(float)num_gamma;
  // for(int k=0;k<num_gamma;k++) gamma[k] = k/(num_gamma-1.0f);

  // allocate memory for intermediate laplacian pyramids
  float *buf[num_gamma][max_levels] = {{0}};
  for(int k=0;k<num_gamma;k++) for(int l=0;l<=last_level;l++)
    buf[k][l] = dt_alloc_align(64, sizeof(float)*dl(w,l)*dl(h,l));

  // the paper says remapping only level 3 not 0 does the trick, too
  // (but i really like the additional octave of sharpness we get,
  // willing to pay the cost).
  for(int k=0;k<num_gamma;k++)
  { // process images
#if defined(__SSE2__)
    if(use_sse2)
      apply_curve_sse2(buf[k][0], padded[0], w, h, max_supp, gamma[k], sigma, shadows, highlights, clarity);
    else // brackets in next line needed for silly gcc warning:
#endif
    {apply_curve(buf[k][0], padded[0], w, h, max_supp, gamma[k], sigma, shadows, highlights, clarity);}

    // create gaussian pyramids
    for(int l=1;l<=last_level;l++)
#if defined(__SSE2__)
      if(use_sse2)
        gauss_reduce_sse2(buf[k][l-1], buf[k][l], dl(w,l-1), dl(h,l-1));
      else
#endif
        gauss_reduce(buf[k][l-1], buf[k][l], dl(w,l-1), dl(h,l-1));
  }

  // resample output[last_level] from preview
  // requires to transform from padded/downsampled to full image and then
  // to padded/downsampled in preview
  if(b && b->mode == 2)
  {
    const float isize = powf(2.0f, last_level) / b->roi->scale; // pixel size of coarsest level in image space
    const float psize = isize / b->buf->width * b->wd; // pixel footprint rescaled to preview buffer
    const float pl = log2f(psize); // mip level in preview buffer
    const int pl0 = CLAMP((int)pl, 0, b->num_levels-1), pl1 = CLAMP((int)(pl+1), 0, b->num_levels-1);
    const float weight = CLAMP(pl-pl0, 0, 1); // weight between mip levels
    const float mul0 = 1.0/powf(2.0f, pl0);
    const float mul1 = 1.0/powf(2.0f, pl1);
    const float mul = powf(2.0f, last_level);
    const int pw = dl(w,last_level), ph = dl(h,last_level);
    const int pw0 = dl(b->pwd, pl0), ph0 = dl(b->pht, pl0);
    const int pw1 = dl(b->pwd, pl1), ph1 = dl(b->pht, pl1);
#if 0
    {
    FILE *f = fopen("/tmp/coarse.pfm", "wb");
    fprintf(f, "PF\n%d %d\n-1.0\n", pw0, ph0);
    for(int j=0;j<ph0;j++)
      for(int i=0;i<pw0;i++)
        for(int c=0;c<3;c++)
          fwrite(b->output[pl0] + pw0*j+i, 1, sizeof(float), f);
    fclose(f);
    }
#endif
#if 0
    {
    FILE *f = fopen("/tmp/oldcoarse.pfm", "wb");
    fprintf(f, "PF\n%d %d\n-1.0\n", pw, ph);
    for(int j=0;j<ph;j++)
      for(int i=0;i<pw;i++)
        for(int c=0;c<3;c++)
          fwrite(output[last_level] + pw*j+i, 1, sizeof(float), f);
    fclose(f);
    }
#endif
#ifdef _OPENMP
#pragma omp parallel for schedule(static) collapse(2) default(shared)
#endif
    for(int j=0;j<ph;j++) for(int i=0;i<pw;i++)
    {
      // image coordinates in full buffer
      float ix = ((i*mul - max_supp) + b->roi->x)/b->roi->scale;
      float iy = ((j*mul - max_supp) + b->roi->y)/b->roi->scale;
      // coordinates in padded preview buffer (
      float px = CLAMP(ix / (float)b->buf->width  * b->wd + (b->pwd-b->wd)/2.0f, 0, b->pwd);
      float py = CLAMP(iy / (float)b->buf->height * b->ht + (b->pht-b->ht)/2.0f, 0, b->pht);
      // trilinear lookup:
      int px0 = CLAMP(px*mul0, 0, pw0-1);
      int py0 = CLAMP(py*mul0, 0, ph0-1);
      int px1 = CLAMP(px*mul1, 0, pw1-1);
      int py1 = CLAMP(py*mul1, 0, ph1-1);
#if 1
      float f0x = CLAMP(px*mul0 - px0, 0.0f, 1.0f);
      float f0y = CLAMP(py*mul0 - py0, 0.0f, 1.0f);
      float f1x = CLAMP(px*mul1 - px1, 0.0f, 1.0f);
      float f1y = CLAMP(py*mul1 - py1, 0.0f, 1.0f);
      float c0 =
        (1.0f-f0x)*(1.0f-f0y)*b->output[pl0][CLAMP(py0  , 0, ph0-1)*pw0 + CLAMP(px0  , 0, pw0-1)]+
        (     f0x)*(1.0f-f0y)*b->output[pl0][CLAMP(py0  , 0, ph0-1)*pw0 + CLAMP(px0+1, 0, pw0-1)]+
        (1.0f-f0x)*(     f0y)*b->output[pl0][CLAMP(py0+1, 0, ph0-1)*pw0 + CLAMP(px0  , 0, pw0-1)]+
        (     f0x)*(     f0y)*b->output[pl0][CLAMP(py0+1, 0, ph0-1)*pw0 + CLAMP(px0+1, 0, pw0-1)];
      float c1 =
        (1.0f-f1x)*(1.0f-f1y)*b->output[pl1][CLAMP(py1  , 0, ph1-1)*pw1 + CLAMP(px1  , 0, pw1-1)]+
        (     f1x)*(1.0f-f1y)*b->output[pl1][CLAMP(py1  , 0, ph1-1)*pw1 + CLAMP(px1+1, 0, pw1-1)]+
        (1.0f-f1x)*(     f1y)*b->output[pl1][CLAMP(py1+1, 0, ph1-1)*pw1 + CLAMP(px1  , 0, pw1-1)]+
        (     f1x)*(     f1y)*b->output[pl1][CLAMP(py1+1, 0, ph1-1)*pw1 + CLAMP(px1+1, 0, pw1-1)];
#else
      float c0 = b->output[pl0][py0*pw0 + px0];
      float c1 = b->output[pl1][py1*pw1 + px1];
#endif
      output[last_level][j*pw+i] = weight * c1 + (1.0f-weight) * c0;
    }
#if 0
    {
    FILE *f = fopen("/tmp/newcoarse.pfm", "wb");
    fprintf(f, "PF\n%d %d\n-1.0\n", pw, ph);
    for(int j=0;j<ph;j++)
      for(int i=0;i<pw;i++)
        for(int c=0;c<3;c++)
          fwrite(output[last_level] + pw*j+i, 1, sizeof(float), f);
    fclose(f);
    }
#endif
  }

  // assemble output pyramid coarse to fine
  for(int l=last_level-1;l >= 0; l--)
  {
    const int pw = dl(w,l), ph = dl(h,l);

    gauss_expand(output[l+1], output[l], pw, ph);
    // go through all coefficients in the upsampled gauss buffer:
#ifdef _OPENMP
#pragma omp parallel for default(none) \
    dt_omp_firstprivate(ph, pw) \
    shared(w,h,buf,output,l,gamma,padded) \
    schedule(static) \
    collapse(2)
#endif
    for(int j=0;j<ph;j++) for(int i=0;i<pw;i++)
    {
      const float v = padded[l][j*pw+i];
      int hi = 1;
      for(;hi<num_gamma-1 && gamma[hi] <= v;hi++);
      int lo = hi-1;
      const float a = CLAMPS((v - gamma[lo])/(gamma[hi]-gamma[lo]), 0.0f, 1.0f);
      const float l0 = ll_laplacian(buf[lo][l+1], buf[lo][l], i, j, pw, ph);
      const float l1 = ll_laplacian(buf[hi][l+1], buf[hi][l], i, j, pw, ph);
      output[l][j*pw+i] += l0 * (1.0f-a) + l1 * a;
      // we could do this to save on memory (no need for finest buf[][]).
      // unfortunately it results in a quite noticeable loss of sharpness, i think
      // the extra level is worth it.
      // else if(l == 0) // use finest scale from input to not amplify noise (and use less memory)
      //   output[l][j*pw+i] += ll_laplacian(padded[l+1], padded[l], i, j, pw, ph);
    }
  }
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(ht, input, max_supp, out, wd) \
  shared(w,output,buf) \
  schedule(dynamic) \
  collapse(2)
#endif
  for(int j=0;j<ht;j++) for(int i=0;i<wd;i++)
  {
    out[4*(j*wd+i)+0] = 100.0f * output[0][(j+max_supp)*w+max_supp+i]; // [0,1] -> L
    out[4*(j*wd+i)+1] = input[4*(j*wd+i)+1]; // copy original colour channels
    out[4*(j*wd+i)+2] = input[4*(j*wd+i)+2];
  }
  if(b && b->mode == 1)
  { // output the buffers for later re-use
    b->pad0 = padded[0];
    b->wd = wd;
    b->ht = ht;
    b->pwd = w;
    b->pht = h;
    b->num_levels = num_levels;
    for(int l=0;l<num_levels;l++) b->output[l] = output[l];
  }
  // free all buffers except the ones passed out for preview rendering
  for(int l=0;l<max_levels;l++)
  {
    if(!b || b->mode != 1 || l)   dt_free_align(padded[l]);
    if(!b || b->mode != 1)        dt_free_align(output[l]);
    for(int k=0; k<num_gamma;k++) dt_free_align(buf[k][l]);
  }
#undef num_levels
#undef num_gamma
}


size_t local_laplacian_memory_use(const int width,     // width of input image
                                  const int height)    // height of input image
{
#define max_levels 30
#define num_gamma 6
  const int num_levels = MIN(max_levels, 31-__builtin_clz(MIN(width,height)));
  const int max_supp = 1<<(num_levels-1);
  const int paddwd = width  + 2*max_supp;
  const int paddht = height + 2*max_supp;

  size_t memory_use = 0;

  for(int l=0;l<num_levels;l++)
    memory_use += (size_t)(2 + num_gamma) * dl(paddwd, l) * dl(paddht, l) * sizeof(float);

  return memory_use;
#undef num_levels
#undef num_gamma
}

size_t local_laplacian_singlebuffer_size(const int width,     // width of input image
                                         const int height)    // height of input image
{
#define max_levels 30
#define num_gamma 6
  const int num_levels = MIN(max_levels, 31-__builtin_clz(MIN(width,height)));
  const int max_supp = 1<<(num_levels-1);
  const int paddwd = width  + 2*max_supp;
  const int paddht = height + 2*max_supp;

  return (size_t)dl(paddwd, 0) * dl(paddht, 0) * sizeof(float);
#undef num_levels
#undef num_gamma
}
