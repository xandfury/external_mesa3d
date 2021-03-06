/*
 * Copyright (C) 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Jason Ekstrand (jason@jlekstrand.net)
 */

#include <math.h>
#include "util/rounding.h" /* for _mesa_roundeven */
#include "util/half_float.h"
#include "util/bigmath.h"
#include "nir_constant_expressions.h"

#define MAX_UINT_FOR_SIZE(bits) (UINT64_MAX >> (64 - (bits)))

/**
 * Evaluate one component of packSnorm4x8.
 */
static uint8_t
pack_snorm_1x8(float x)
{
    /* From section 8.4 of the GLSL 4.30 spec:
     *
     *    packSnorm4x8
     *    ------------
     *    The conversion for component c of v to fixed point is done as
     *    follows:
     *
     *      packSnorm4x8: round(clamp(c, -1, +1) * 127.0)
     *
     * We must first cast the float to an int, because casting a negative
     * float to a uint is undefined.
     */
   return (uint8_t) (int)
          _mesa_roundevenf(CLAMP(x, -1.0f, +1.0f) * 127.0f);
}

/**
 * Evaluate one component of packSnorm2x16.
 */
static uint16_t
pack_snorm_1x16(float x)
{
    /* From section 8.4 of the GLSL ES 3.00 spec:
     *
     *    packSnorm2x16
     *    -------------
     *    The conversion for component c of v to fixed point is done as
     *    follows:
     *
     *      packSnorm2x16: round(clamp(c, -1, +1) * 32767.0)
     *
     * We must first cast the float to an int, because casting a negative
     * float to a uint is undefined.
     */
   return (uint16_t) (int)
          _mesa_roundevenf(CLAMP(x, -1.0f, +1.0f) * 32767.0f);
}

/**
 * Evaluate one component of unpackSnorm4x8.
 */
static float
unpack_snorm_1x8(uint8_t u)
{
    /* From section 8.4 of the GLSL 4.30 spec:
     *
     *    unpackSnorm4x8
     *    --------------
     *    The conversion for unpacked fixed-point value f to floating point is
     *    done as follows:
     *
     *       unpackSnorm4x8: clamp(f / 127.0, -1, +1)
     */
   return CLAMP((int8_t) u / 127.0f, -1.0f, +1.0f);
}

/**
 * Evaluate one component of unpackSnorm2x16.
 */
static float
unpack_snorm_1x16(uint16_t u)
{
    /* From section 8.4 of the GLSL ES 3.00 spec:
     *
     *    unpackSnorm2x16
     *    ---------------
     *    The conversion for unpacked fixed-point value f to floating point is
     *    done as follows:
     *
     *       unpackSnorm2x16: clamp(f / 32767.0, -1, +1)
     */
   return CLAMP((int16_t) u / 32767.0f, -1.0f, +1.0f);
}

/**
 * Evaluate one component packUnorm4x8.
 */
static uint8_t
pack_unorm_1x8(float x)
{
    /* From section 8.4 of the GLSL 4.30 spec:
     *
     *    packUnorm4x8
     *    ------------
     *    The conversion for component c of v to fixed point is done as
     *    follows:
     *
     *       packUnorm4x8: round(clamp(c, 0, +1) * 255.0)
     */
   return (uint8_t) (int)
          _mesa_roundevenf(CLAMP(x, 0.0f, 1.0f) * 255.0f);
}

/**
 * Evaluate one component packUnorm2x16.
 */
static uint16_t
pack_unorm_1x16(float x)
{
    /* From section 8.4 of the GLSL ES 3.00 spec:
     *
     *    packUnorm2x16
     *    -------------
     *    The conversion for component c of v to fixed point is done as
     *    follows:
     *
     *       packUnorm2x16: round(clamp(c, 0, +1) * 65535.0)
     */
   return (uint16_t) (int)
          _mesa_roundevenf(CLAMP(x, 0.0f, 1.0f) * 65535.0f);
}

/**
 * Evaluate one component of unpackUnorm4x8.
 */
static float
unpack_unorm_1x8(uint8_t u)
{
    /* From section 8.4 of the GLSL 4.30 spec:
     *
     *    unpackUnorm4x8
     *    --------------
     *    The conversion for unpacked fixed-point value f to floating point is
     *    done as follows:
     *
     *       unpackUnorm4x8: f / 255.0
     */
   return (float) u / 255.0f;
}

/**
 * Evaluate one component of unpackUnorm2x16.
 */
static float
unpack_unorm_1x16(uint16_t u)
{
    /* From section 8.4 of the GLSL ES 3.00 spec:
     *
     *    unpackUnorm2x16
     *    ---------------
     *    The conversion for unpacked fixed-point value f to floating point is
     *    done as follows:
     *
     *       unpackUnorm2x16: f / 65535.0
     */
   return (float) u / 65535.0f;
}

/**
 * Evaluate one component of packHalf2x16.
 */
static uint16_t
pack_half_1x16(float x)
{
   return _mesa_float_to_half(x);
}

/**
 * Evaluate one component of unpackHalf2x16.
 */
static float
unpack_half_1x16(uint16_t u)
{
   return _mesa_half_to_float(u);
}

/* Some typed vector structures to make things like src0.y work */
typedef int8_t int1_t;
typedef uint8_t uint1_t;
typedef float float16_t;
typedef float float32_t;
typedef double float64_t;
typedef bool bool1_t;
typedef bool bool8_t;
typedef bool bool16_t;
typedef bool bool32_t;
typedef bool bool64_t;
struct float16_vec {
   float16_t x;
   float16_t y;
   float16_t z;
   float16_t w;
};
struct float32_vec {
   float32_t x;
   float32_t y;
   float32_t z;
   float32_t w;
};
struct float64_vec {
   float64_t x;
   float64_t y;
   float64_t z;
   float64_t w;
};
struct int1_vec {
   int1_t x;
   int1_t y;
   int1_t z;
   int1_t w;
};
struct int8_vec {
   int8_t x;
   int8_t y;
   int8_t z;
   int8_t w;
};
struct int16_vec {
   int16_t x;
   int16_t y;
   int16_t z;
   int16_t w;
};
struct int32_vec {
   int32_t x;
   int32_t y;
   int32_t z;
   int32_t w;
};
struct int64_vec {
   int64_t x;
   int64_t y;
   int64_t z;
   int64_t w;
};
struct uint1_vec {
   uint1_t x;
   uint1_t y;
   uint1_t z;
   uint1_t w;
};
struct uint8_vec {
   uint8_t x;
   uint8_t y;
   uint8_t z;
   uint8_t w;
};
struct uint16_vec {
   uint16_t x;
   uint16_t y;
   uint16_t z;
   uint16_t w;
};
struct uint32_vec {
   uint32_t x;
   uint32_t y;
   uint32_t z;
   uint32_t w;
};
struct uint64_vec {
   uint64_t x;
   uint64_t y;
   uint64_t z;
   uint64_t w;
};
struct bool1_vec {
   bool1_t x;
   bool1_t y;
   bool1_t z;
   bool1_t w;
};
struct bool32_vec {
   bool32_t x;
   bool32_t y;
   bool32_t z;
   bool32_t w;
};



static void
evaluate_b2f16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool1_t src0 =
                  _src[0][_i].b;

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool32_t src0 =
                  _src[0][_i].i32;

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b2f32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool1_t src0 =
                  _src[0][_i].b;

            float32_t dst = src0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool32_t src0 =
                  _src[0][_i].i32;

            float32_t dst = src0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b2f64(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool1_t src0 =
                  _src[0][_i].b;

            float64_t dst = src0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool32_t src0 =
                  _src[0][_i].i32;

            float64_t dst = src0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b2i1(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool1_t src0 =
                  _src[0][_i].b;

            int1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool32_t src0 =
                  _src[0][_i].i32;

            int1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b2i16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool1_t src0 =
                  _src[0][_i].b;

            int16_t dst = src0;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool32_t src0 =
                  _src[0][_i].i32;

            int16_t dst = src0;

            _dst_val[_i].i16 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b2i32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool1_t src0 =
                  _src[0][_i].b;

            int32_t dst = src0;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool32_t src0 =
                  _src[0][_i].i32;

            int32_t dst = src0;

            _dst_val[_i].i32 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b2i64(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool1_t src0 =
                  _src[0][_i].b;

            int64_t dst = src0;

            _dst_val[_i].i64 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool32_t src0 =
                  _src[0][_i].i32;

            int64_t dst = src0;

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b2i8(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool1_t src0 =
                  _src[0][_i].b;

            int8_t dst = src0;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool32_t src0 =
                  _src[0][_i].i32;

            int8_t dst = src0;

            _dst_val[_i].i8 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b32all_fequal2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
         0,
         0,
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
         0,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
         0,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
         0,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
         0,
         0,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
         0,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b32all_fequal3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
            _mesa_half_to_float(_src[0][2].u16),
         0,
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
            _mesa_half_to_float(_src[1][2].u16),
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
            _src[1][2].f32,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
            _src[0][2].f64,
         0,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
            _src[1][2].f64,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b32all_fequal4(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
            _mesa_half_to_float(_src[0][2].u16),
            _mesa_half_to_float(_src[0][3].u16),
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
            _mesa_half_to_float(_src[1][2].u16),
            _mesa_half_to_float(_src[1][3].u16),
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z) && (src0.w == src1.w));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
            _src[0][3].f32,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
            _src[1][2].f32,
            _src[1][3].f32,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z) && (src0.w == src1.w));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
            _src[0][2].f64,
            _src[0][3].f64,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
            _src[1][2].f64,
            _src[1][3].f64,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z) && (src0.w == src1.w));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b32all_iequal2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   


      const struct int1_vec src0 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][1].b,
         0,
         0,
      };

      const struct int1_vec src1 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][1].b,
         0,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 8: {
         
   


      const struct int8_vec src0 = {
            _src[0][0].i8,
            _src[0][1].i8,
         0,
         0,
      };

      const struct int8_vec src1 = {
            _src[1][0].i8,
            _src[1][1].i8,
         0,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 16: {
         
   


      const struct int16_vec src0 = {
            _src[0][0].i16,
            _src[0][1].i16,
         0,
         0,
      };

      const struct int16_vec src1 = {
            _src[1][0].i16,
            _src[1][1].i16,
         0,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct int32_vec src0 = {
            _src[0][0].i32,
            _src[0][1].i32,
         0,
         0,
      };

      const struct int32_vec src1 = {
            _src[1][0].i32,
            _src[1][1].i32,
         0,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct int64_vec src0 = {
            _src[0][0].i64,
            _src[0][1].i64,
         0,
         0,
      };

      const struct int64_vec src1 = {
            _src[1][0].i64,
            _src[1][1].i64,
         0,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b32all_iequal3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   


      const struct int1_vec src0 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][1].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][2].b,
         0,
      };

      const struct int1_vec src1 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][1].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][2].b,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 8: {
         
   


      const struct int8_vec src0 = {
            _src[0][0].i8,
            _src[0][1].i8,
            _src[0][2].i8,
         0,
      };

      const struct int8_vec src1 = {
            _src[1][0].i8,
            _src[1][1].i8,
            _src[1][2].i8,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 16: {
         
   


      const struct int16_vec src0 = {
            _src[0][0].i16,
            _src[0][1].i16,
            _src[0][2].i16,
         0,
      };

      const struct int16_vec src1 = {
            _src[1][0].i16,
            _src[1][1].i16,
            _src[1][2].i16,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct int32_vec src0 = {
            _src[0][0].i32,
            _src[0][1].i32,
            _src[0][2].i32,
         0,
      };

      const struct int32_vec src1 = {
            _src[1][0].i32,
            _src[1][1].i32,
            _src[1][2].i32,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct int64_vec src0 = {
            _src[0][0].i64,
            _src[0][1].i64,
            _src[0][2].i64,
         0,
      };

      const struct int64_vec src1 = {
            _src[1][0].i64,
            _src[1][1].i64,
            _src[1][2].i64,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b32all_iequal4(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   


      const struct int1_vec src0 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][1].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][2].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][3].b,
      };

      const struct int1_vec src1 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][1].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][2].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][3].b,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z) && (src0.w == src1.w));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 8: {
         
   


      const struct int8_vec src0 = {
            _src[0][0].i8,
            _src[0][1].i8,
            _src[0][2].i8,
            _src[0][3].i8,
      };

      const struct int8_vec src1 = {
            _src[1][0].i8,
            _src[1][1].i8,
            _src[1][2].i8,
            _src[1][3].i8,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z) && (src0.w == src1.w));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 16: {
         
   


      const struct int16_vec src0 = {
            _src[0][0].i16,
            _src[0][1].i16,
            _src[0][2].i16,
            _src[0][3].i16,
      };

      const struct int16_vec src1 = {
            _src[1][0].i16,
            _src[1][1].i16,
            _src[1][2].i16,
            _src[1][3].i16,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z) && (src0.w == src1.w));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct int32_vec src0 = {
            _src[0][0].i32,
            _src[0][1].i32,
            _src[0][2].i32,
            _src[0][3].i32,
      };

      const struct int32_vec src1 = {
            _src[1][0].i32,
            _src[1][1].i32,
            _src[1][2].i32,
            _src[1][3].i32,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z) && (src0.w == src1.w));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct int64_vec src0 = {
            _src[0][0].i64,
            _src[0][1].i64,
            _src[0][2].i64,
            _src[0][3].i64,
      };

      const struct int64_vec src1 = {
            _src[1][0].i64,
            _src[1][1].i64,
            _src[1][2].i64,
            _src[1][3].i64,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z) && (src0.w == src1.w));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b32any_fnequal2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
         0,
         0,
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
         0,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
         0,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
         0,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
         0,
         0,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
         0,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b32any_fnequal3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
            _mesa_half_to_float(_src[0][2].u16),
         0,
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
            _mesa_half_to_float(_src[1][2].u16),
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
            _src[1][2].f32,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
            _src[0][2].f64,
         0,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
            _src[1][2].f64,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b32any_fnequal4(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
            _mesa_half_to_float(_src[0][2].u16),
            _mesa_half_to_float(_src[0][3].u16),
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
            _mesa_half_to_float(_src[1][2].u16),
            _mesa_half_to_float(_src[1][3].u16),
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z) || (src0.w != src1.w));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
            _src[0][3].f32,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
            _src[1][2].f32,
            _src[1][3].f32,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z) || (src0.w != src1.w));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
            _src[0][2].f64,
            _src[0][3].f64,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
            _src[1][2].f64,
            _src[1][3].f64,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z) || (src0.w != src1.w));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b32any_inequal2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   


      const struct int1_vec src0 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][1].b,
         0,
         0,
      };

      const struct int1_vec src1 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][1].b,
         0,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 8: {
         
   


      const struct int8_vec src0 = {
            _src[0][0].i8,
            _src[0][1].i8,
         0,
         0,
      };

      const struct int8_vec src1 = {
            _src[1][0].i8,
            _src[1][1].i8,
         0,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 16: {
         
   


      const struct int16_vec src0 = {
            _src[0][0].i16,
            _src[0][1].i16,
         0,
         0,
      };

      const struct int16_vec src1 = {
            _src[1][0].i16,
            _src[1][1].i16,
         0,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct int32_vec src0 = {
            _src[0][0].i32,
            _src[0][1].i32,
         0,
         0,
      };

      const struct int32_vec src1 = {
            _src[1][0].i32,
            _src[1][1].i32,
         0,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct int64_vec src0 = {
            _src[0][0].i64,
            _src[0][1].i64,
         0,
         0,
      };

      const struct int64_vec src1 = {
            _src[1][0].i64,
            _src[1][1].i64,
         0,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b32any_inequal3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   


      const struct int1_vec src0 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][1].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][2].b,
         0,
      };

      const struct int1_vec src1 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][1].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][2].b,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 8: {
         
   


      const struct int8_vec src0 = {
            _src[0][0].i8,
            _src[0][1].i8,
            _src[0][2].i8,
         0,
      };

      const struct int8_vec src1 = {
            _src[1][0].i8,
            _src[1][1].i8,
            _src[1][2].i8,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 16: {
         
   


      const struct int16_vec src0 = {
            _src[0][0].i16,
            _src[0][1].i16,
            _src[0][2].i16,
         0,
      };

      const struct int16_vec src1 = {
            _src[1][0].i16,
            _src[1][1].i16,
            _src[1][2].i16,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct int32_vec src0 = {
            _src[0][0].i32,
            _src[0][1].i32,
            _src[0][2].i32,
         0,
      };

      const struct int32_vec src1 = {
            _src[1][0].i32,
            _src[1][1].i32,
            _src[1][2].i32,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct int64_vec src0 = {
            _src[0][0].i64,
            _src[0][1].i64,
            _src[0][2].i64,
         0,
      };

      const struct int64_vec src1 = {
            _src[1][0].i64,
            _src[1][1].i64,
            _src[1][2].i64,
         0,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b32any_inequal4(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   


      const struct int1_vec src0 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][1].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][2].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][3].b,
      };

      const struct int1_vec src1 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][1].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][2].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][3].b,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z) || (src0.w != src1.w));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 8: {
         
   


      const struct int8_vec src0 = {
            _src[0][0].i8,
            _src[0][1].i8,
            _src[0][2].i8,
            _src[0][3].i8,
      };

      const struct int8_vec src1 = {
            _src[1][0].i8,
            _src[1][1].i8,
            _src[1][2].i8,
            _src[1][3].i8,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z) || (src0.w != src1.w));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 16: {
         
   


      const struct int16_vec src0 = {
            _src[0][0].i16,
            _src[0][1].i16,
            _src[0][2].i16,
            _src[0][3].i16,
      };

      const struct int16_vec src1 = {
            _src[1][0].i16,
            _src[1][1].i16,
            _src[1][2].i16,
            _src[1][3].i16,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z) || (src0.w != src1.w));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct int32_vec src0 = {
            _src[0][0].i32,
            _src[0][1].i32,
            _src[0][2].i32,
            _src[0][3].i32,
      };

      const struct int32_vec src1 = {
            _src[1][0].i32,
            _src[1][1].i32,
            _src[1][2].i32,
            _src[1][3].i32,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z) || (src0.w != src1.w));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct int64_vec src0 = {
            _src[0][0].i64,
            _src[0][1].i64,
            _src[0][2].i64,
            _src[0][3].i64,
      };

      const struct int64_vec src1 = {
            _src[1][0].i64,
            _src[1][1].i64,
            _src[1][2].i64,
            _src[1][3].i64,
      };

      struct bool32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z) || (src0.w != src1.w));

            _dst_val[0].i32 = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_b32csel(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool32_t src0 =
                  _src[0][_i].i32;
               const uint1_t src1 =
                  _src[1][_i].b;
               const uint1_t src2 =
                  _src[2][_i].b;

            uint1_t dst = src0 ? src1 : src2;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool32_t src0 =
                  _src[0][_i].i32;
               const uint8_t src1 =
                  _src[1][_i].u8;
               const uint8_t src2 =
                  _src[2][_i].u8;

            uint8_t dst = src0 ? src1 : src2;

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool32_t src0 =
                  _src[0][_i].i32;
               const uint16_t src1 =
                  _src[1][_i].u16;
               const uint16_t src2 =
                  _src[2][_i].u16;

            uint16_t dst = src0 ? src1 : src2;

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool32_t src0 =
                  _src[0][_i].i32;
               const uint32_t src1 =
                  _src[1][_i].u32;
               const uint32_t src2 =
                  _src[2][_i].u32;

            uint32_t dst = src0 ? src1 : src2;

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool32_t src0 =
                  _src[0][_i].i32;
               const uint64_t src1 =
                  _src[1][_i].u64;
               const uint64_t src2 =
                  _src[2][_i].u64;

            uint64_t dst = src0 ? src1 : src2;

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ball_fequal2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
         0,
         0,
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
         0,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
         0,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
         0,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
         0,
         0,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
         0,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y));

            _dst_val[0].b = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ball_fequal3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
            _mesa_half_to_float(_src[0][2].u16),
         0,
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
            _mesa_half_to_float(_src[1][2].u16),
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
            _src[1][2].f32,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
            _src[0][2].f64,
         0,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
            _src[1][2].f64,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z));

            _dst_val[0].b = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ball_fequal4(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
            _mesa_half_to_float(_src[0][2].u16),
            _mesa_half_to_float(_src[0][3].u16),
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
            _mesa_half_to_float(_src[1][2].u16),
            _mesa_half_to_float(_src[1][3].u16),
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z) && (src0.w == src1.w));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
            _src[0][3].f32,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
            _src[1][2].f32,
            _src[1][3].f32,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z) && (src0.w == src1.w));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
            _src[0][2].f64,
            _src[0][3].f64,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
            _src[1][2].f64,
            _src[1][3].f64,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z) && (src0.w == src1.w));

            _dst_val[0].b = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ball_iequal2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   


      const struct int1_vec src0 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][1].b,
         0,
         0,
      };

      const struct int1_vec src1 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][1].b,
         0,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 8: {
         
   


      const struct int8_vec src0 = {
            _src[0][0].i8,
            _src[0][1].i8,
         0,
         0,
      };

      const struct int8_vec src1 = {
            _src[1][0].i8,
            _src[1][1].i8,
         0,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 16: {
         
   


      const struct int16_vec src0 = {
            _src[0][0].i16,
            _src[0][1].i16,
         0,
         0,
      };

      const struct int16_vec src1 = {
            _src[1][0].i16,
            _src[1][1].i16,
         0,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct int32_vec src0 = {
            _src[0][0].i32,
            _src[0][1].i32,
         0,
         0,
      };

      const struct int32_vec src1 = {
            _src[1][0].i32,
            _src[1][1].i32,
         0,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct int64_vec src0 = {
            _src[0][0].i64,
            _src[0][1].i64,
         0,
         0,
      };

      const struct int64_vec src1 = {
            _src[1][0].i64,
            _src[1][1].i64,
         0,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y));

            _dst_val[0].b = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ball_iequal3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   


      const struct int1_vec src0 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][1].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][2].b,
         0,
      };

      const struct int1_vec src1 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][1].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][2].b,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 8: {
         
   


      const struct int8_vec src0 = {
            _src[0][0].i8,
            _src[0][1].i8,
            _src[0][2].i8,
         0,
      };

      const struct int8_vec src1 = {
            _src[1][0].i8,
            _src[1][1].i8,
            _src[1][2].i8,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 16: {
         
   


      const struct int16_vec src0 = {
            _src[0][0].i16,
            _src[0][1].i16,
            _src[0][2].i16,
         0,
      };

      const struct int16_vec src1 = {
            _src[1][0].i16,
            _src[1][1].i16,
            _src[1][2].i16,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct int32_vec src0 = {
            _src[0][0].i32,
            _src[0][1].i32,
            _src[0][2].i32,
         0,
      };

      const struct int32_vec src1 = {
            _src[1][0].i32,
            _src[1][1].i32,
            _src[1][2].i32,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct int64_vec src0 = {
            _src[0][0].i64,
            _src[0][1].i64,
            _src[0][2].i64,
         0,
      };

      const struct int64_vec src1 = {
            _src[1][0].i64,
            _src[1][1].i64,
            _src[1][2].i64,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z));

            _dst_val[0].b = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ball_iequal4(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   


      const struct int1_vec src0 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][1].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][2].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][3].b,
      };

      const struct int1_vec src1 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][1].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][2].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][3].b,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z) && (src0.w == src1.w));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 8: {
         
   


      const struct int8_vec src0 = {
            _src[0][0].i8,
            _src[0][1].i8,
            _src[0][2].i8,
            _src[0][3].i8,
      };

      const struct int8_vec src1 = {
            _src[1][0].i8,
            _src[1][1].i8,
            _src[1][2].i8,
            _src[1][3].i8,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z) && (src0.w == src1.w));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 16: {
         
   


      const struct int16_vec src0 = {
            _src[0][0].i16,
            _src[0][1].i16,
            _src[0][2].i16,
            _src[0][3].i16,
      };

      const struct int16_vec src1 = {
            _src[1][0].i16,
            _src[1][1].i16,
            _src[1][2].i16,
            _src[1][3].i16,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z) && (src0.w == src1.w));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct int32_vec src0 = {
            _src[0][0].i32,
            _src[0][1].i32,
            _src[0][2].i32,
            _src[0][3].i32,
      };

      const struct int32_vec src1 = {
            _src[1][0].i32,
            _src[1][1].i32,
            _src[1][2].i32,
            _src[1][3].i32,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z) && (src0.w == src1.w));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct int64_vec src0 = {
            _src[0][0].i64,
            _src[0][1].i64,
            _src[0][2].i64,
            _src[0][3].i64,
      };

      const struct int64_vec src1 = {
            _src[1][0].i64,
            _src[1][1].i64,
            _src[1][2].i64,
            _src[1][3].i64,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z) && (src0.w == src1.w));

            _dst_val[0].b = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_bany_fnequal2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
         0,
         0,
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
         0,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
         0,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
         0,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
         0,
         0,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
         0,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y));

            _dst_val[0].b = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_bany_fnequal3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
            _mesa_half_to_float(_src[0][2].u16),
         0,
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
            _mesa_half_to_float(_src[1][2].u16),
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
            _src[1][2].f32,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
            _src[0][2].f64,
         0,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
            _src[1][2].f64,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z));

            _dst_val[0].b = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_bany_fnequal4(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
            _mesa_half_to_float(_src[0][2].u16),
            _mesa_half_to_float(_src[0][3].u16),
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
            _mesa_half_to_float(_src[1][2].u16),
            _mesa_half_to_float(_src[1][3].u16),
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z) || (src0.w != src1.w));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
            _src[0][3].f32,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
            _src[1][2].f32,
            _src[1][3].f32,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z) || (src0.w != src1.w));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
            _src[0][2].f64,
            _src[0][3].f64,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
            _src[1][2].f64,
            _src[1][3].f64,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z) || (src0.w != src1.w));

            _dst_val[0].b = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_bany_inequal2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   


      const struct int1_vec src0 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][1].b,
         0,
         0,
      };

      const struct int1_vec src1 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][1].b,
         0,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 8: {
         
   


      const struct int8_vec src0 = {
            _src[0][0].i8,
            _src[0][1].i8,
         0,
         0,
      };

      const struct int8_vec src1 = {
            _src[1][0].i8,
            _src[1][1].i8,
         0,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 16: {
         
   


      const struct int16_vec src0 = {
            _src[0][0].i16,
            _src[0][1].i16,
         0,
         0,
      };

      const struct int16_vec src1 = {
            _src[1][0].i16,
            _src[1][1].i16,
         0,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct int32_vec src0 = {
            _src[0][0].i32,
            _src[0][1].i32,
         0,
         0,
      };

      const struct int32_vec src1 = {
            _src[1][0].i32,
            _src[1][1].i32,
         0,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct int64_vec src0 = {
            _src[0][0].i64,
            _src[0][1].i64,
         0,
         0,
      };

      const struct int64_vec src1 = {
            _src[1][0].i64,
            _src[1][1].i64,
         0,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y));

            _dst_val[0].b = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_bany_inequal3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   


      const struct int1_vec src0 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][1].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][2].b,
         0,
      };

      const struct int1_vec src1 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][1].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][2].b,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 8: {
         
   


      const struct int8_vec src0 = {
            _src[0][0].i8,
            _src[0][1].i8,
            _src[0][2].i8,
         0,
      };

      const struct int8_vec src1 = {
            _src[1][0].i8,
            _src[1][1].i8,
            _src[1][2].i8,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 16: {
         
   


      const struct int16_vec src0 = {
            _src[0][0].i16,
            _src[0][1].i16,
            _src[0][2].i16,
         0,
      };

      const struct int16_vec src1 = {
            _src[1][0].i16,
            _src[1][1].i16,
            _src[1][2].i16,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct int32_vec src0 = {
            _src[0][0].i32,
            _src[0][1].i32,
            _src[0][2].i32,
         0,
      };

      const struct int32_vec src1 = {
            _src[1][0].i32,
            _src[1][1].i32,
            _src[1][2].i32,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct int64_vec src0 = {
            _src[0][0].i64,
            _src[0][1].i64,
            _src[0][2].i64,
         0,
      };

      const struct int64_vec src1 = {
            _src[1][0].i64,
            _src[1][1].i64,
            _src[1][2].i64,
         0,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z));

            _dst_val[0].b = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_bany_inequal4(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   


      const struct int1_vec src0 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][1].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][2].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[0][3].b,
      };

      const struct int1_vec src1 = {
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][0].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][1].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][2].b,
             /* 1-bit integers use a 0/-1 convention */
             -(int1_t)_src[1][3].b,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z) || (src0.w != src1.w));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 8: {
         
   


      const struct int8_vec src0 = {
            _src[0][0].i8,
            _src[0][1].i8,
            _src[0][2].i8,
            _src[0][3].i8,
      };

      const struct int8_vec src1 = {
            _src[1][0].i8,
            _src[1][1].i8,
            _src[1][2].i8,
            _src[1][3].i8,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z) || (src0.w != src1.w));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 16: {
         
   


      const struct int16_vec src0 = {
            _src[0][0].i16,
            _src[0][1].i16,
            _src[0][2].i16,
            _src[0][3].i16,
      };

      const struct int16_vec src1 = {
            _src[1][0].i16,
            _src[1][1].i16,
            _src[1][2].i16,
            _src[1][3].i16,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z) || (src0.w != src1.w));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 32: {
         
   


      const struct int32_vec src0 = {
            _src[0][0].i32,
            _src[0][1].i32,
            _src[0][2].i32,
            _src[0][3].i32,
      };

      const struct int32_vec src1 = {
            _src[1][0].i32,
            _src[1][1].i32,
            _src[1][2].i32,
            _src[1][3].i32,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z) || (src0.w != src1.w));

            _dst_val[0].b = -(int)dst.x;

         break;
      }
      case 64: {
         
   


      const struct int64_vec src0 = {
            _src[0][0].i64,
            _src[0][1].i64,
            _src[0][2].i64,
            _src[0][3].i64,
      };

      const struct int64_vec src1 = {
            _src[1][0].i64,
            _src[1][1].i64,
            _src[1][2].i64,
            _src[1][3].i64,
      };

      struct bool1_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z) || (src0.w != src1.w));

            _dst_val[0].b = -(int)dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_bcsel(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;
               const uint1_t src2 =
                  _src[2][_i].b;

            uint1_t dst = src0 ? src1 : src2;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool1_t src0 =
                  _src[0][_i].b;
               const uint8_t src1 =
                  _src[1][_i].u8;
               const uint8_t src2 =
                  _src[2][_i].u8;

            uint8_t dst = src0 ? src1 : src2;

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool1_t src0 =
                  _src[0][_i].b;
               const uint16_t src1 =
                  _src[1][_i].u16;
               const uint16_t src2 =
                  _src[2][_i].u16;

            uint16_t dst = src0 ? src1 : src2;

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool1_t src0 =
                  _src[0][_i].b;
               const uint32_t src1 =
                  _src[1][_i].u32;
               const uint32_t src2 =
                  _src[2][_i].u32;

            uint32_t dst = src0 ? src1 : src2;

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const bool1_t src0 =
                  _src[0][_i].b;
               const uint64_t src1 =
                  _src[1][_i].u64;
               const uint64_t src2 =
                  _src[2][_i].u64;

            uint64_t dst = src0 ? src1 : src2;

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_bfi(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;
               const uint32_t src2 =
                  _src[2][_i].u32;

            uint32_t dst;

            
unsigned mask = src0, insert = src1, base = src2;
if (mask == 0) {
   dst = base;
} else {
   unsigned tmp = mask;
   while (!(tmp & 1)) {
      tmp >>= 1;
      insert <<= 1;
   }
   dst = (base & ~mask) | (insert & mask);
}


            _dst_val[_i].u32 = dst;
      }

}
static void
evaluate_bfm(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            uint32_t dst;

            
int bits = src0, offset = src1;
if (offset < 0 || bits < 0 || offset > 31 || bits > 31 || offset + bits > 32)
   dst = 0; /* undefined */
else
   dst = ((1u << bits) - 1) << offset;


            _dst_val[_i].u32 = dst;
      }

}
static void
evaluate_bit_count(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;

            uint32_t dst;

            
dst = 0;
for (unsigned bit = 0; bit < bit_size; bit++) {
   if ((src0 >> bit) & 1)
      dst++;
}


            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;

            uint32_t dst;

            
dst = 0;
for (unsigned bit = 0; bit < bit_size; bit++) {
   if ((src0 >> bit) & 1)
      dst++;
}


            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;

            uint32_t dst;

            
dst = 0;
for (unsigned bit = 0; bit < bit_size; bit++) {
   if ((src0 >> bit) & 1)
      dst++;
}


            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;

            uint32_t dst;

            
dst = 0;
for (unsigned bit = 0; bit < bit_size; bit++) {
   if ((src0 >> bit) & 1)
      dst++;
}


            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;

            uint32_t dst;

            
dst = 0;
for (unsigned bit = 0; bit < bit_size; bit++) {
   if ((src0 >> bit) & 1)
      dst++;
}


            _dst_val[_i].u32 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_bitfield_insert(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                                    
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;
               const int32_t src2 =
                  _src[2][_i].i32;
               const int32_t src3 =
                  _src[3][_i].i32;

            uint32_t dst;

            
unsigned base = src0, insert = src1;
int offset = src2, bits = src3;
if (bits == 0) {
   dst = base;
} else if (offset < 0 || bits < 0 || bits + offset > 32) {
   dst = 0;
} else {
   unsigned mask = ((1ull << bits) - 1) << offset;
   dst = (base & ~mask) | ((insert << offset) & mask);
}


            _dst_val[_i].u32 = dst;
      }

}
static void
evaluate_bitfield_reverse(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;

            uint32_t dst;

            
/* we're not winning any awards for speed here, but that's ok */
dst = 0;
for (unsigned bit = 0; bit < 32; bit++)
   dst |= ((src0 >> bit) & 1) << (31 - bit);


            _dst_val[_i].u32 = dst;
      }

}
static void
evaluate_cube_face_coord(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
         0,
      };

      struct float32_vec dst;

         
dst.x = dst.y = 0.0;
float absX = fabs(src0.x);
float absY = fabs(src0.y);
float absZ = fabs(src0.z);

float ma = 0.0;
if (absX >= absY && absX >= absZ) { ma = 2 * src0.x; }
if (absY >= absX && absY >= absZ) { ma = 2 * src0.y; }
if (absZ >= absX && absZ >= absY) { ma = 2 * src0.z; }

if (src0.x >= 0 && absX >= absY && absX >= absZ) { dst.x = -src0.z; dst.y = -src0.y; }
if (src0.x < 0 && absX >= absY && absX >= absZ) { dst.x = src0.z; dst.y = -src0.y; }
if (src0.y >= 0 && absY >= absX && absY >= absZ) { dst.x = src0.x; dst.y = src0.z; }
if (src0.y < 0 && absY >= absX && absY >= absZ) { dst.x = src0.x; dst.y = -src0.z; }
if (src0.z >= 0 && absZ >= absX && absZ >= absY) { dst.x = src0.x; dst.y = -src0.y; }
if (src0.z < 0 && absZ >= absX && absZ >= absY) { dst.x = -src0.x; dst.y = -src0.y; }

dst.x = dst.x / ma + 0.5;
dst.y = dst.y / ma + 0.5;


            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;

}
static void
evaluate_cube_face_index(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
         0,
      };

      struct float32_vec dst;

         
float absX = fabs(src0.x);
float absY = fabs(src0.y);
float absZ = fabs(src0.z);
if (src0.x >= 0 && absX >= absY && absX >= absZ) dst.x = 0;
if (src0.x < 0 && absX >= absY && absX >= absZ) dst.x = 1;
if (src0.y >= 0 && absY >= absX && absY >= absZ) dst.x = 2;
if (src0.y < 0 && absY >= absX && absY >= absZ) dst.x = 3;
if (src0.z >= 0 && absZ >= absX && absZ >= absY) dst.x = 4;
if (src0.z < 0 && absZ >= absX && absZ >= absY) dst.x = 5;


            _dst_val[0].f32 = dst.x;

}
static void
evaluate_extract_i16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            int1_t dst = (int16_t)(src0 >> (src1 * 16));

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            int8_t dst = (int16_t)(src0 >> (src1 * 16));

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            int16_t dst = (int16_t)(src0 >> (src1 * 16));

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst = (int16_t)(src0 >> (src1 * 16));

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            int64_t dst = (int16_t)(src0 >> (src1 * 16));

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_extract_i8(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            int1_t dst = (int8_t)(src0 >> (src1 * 8));

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            int8_t dst = (int8_t)(src0 >> (src1 * 8));

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            int16_t dst = (int8_t)(src0 >> (src1 * 8));

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst = (int8_t)(src0 >> (src1 * 8));

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            int64_t dst = (int8_t)(src0 >> (src1 * 8));

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_extract_u16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            uint1_t dst = (uint16_t)(src0 >> (src1 * 16));

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            uint8_t dst = (uint16_t)(src0 >> (src1 * 16));

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            uint16_t dst = (uint16_t)(src0 >> (src1 * 16));

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint32_t dst = (uint16_t)(src0 >> (src1 * 16));

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            uint64_t dst = (uint16_t)(src0 >> (src1 * 16));

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_extract_u8(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            uint1_t dst = (uint8_t)(src0 >> (src1 * 8));

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            uint8_t dst = (uint8_t)(src0 >> (src1 * 8));

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            uint16_t dst = (uint8_t)(src0 >> (src1 * 8));

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint32_t dst = (uint8_t)(src0 >> (src1 * 8));

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            uint64_t dst = (uint8_t)(src0 >> (src1 * 8));

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_f2b1(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            bool1_t dst = src0 != 0;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            bool1_t dst = src0 != 0;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            bool1_t dst = src0 != 0;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_f2b32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            bool32_t dst = src0 != 0;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            bool32_t dst = src0 != 0;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            bool32_t dst = src0 != 0;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_f2f16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_f2f16_rtne(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_f2f16_rtz(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_f2f32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float32_t dst = src0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = src0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float32_t dst = src0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_f2f64(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float64_t dst = src0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float64_t dst = src0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = src0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_f2i1(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            int1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            int1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            int1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_f2i16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            int16_t dst = src0;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            int16_t dst = src0;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            int16_t dst = src0;

            _dst_val[_i].i16 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_f2i32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            int32_t dst = src0;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            int32_t dst = src0;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            int32_t dst = src0;

            _dst_val[_i].i32 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_f2i64(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            int64_t dst = src0;

            _dst_val[_i].i64 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            int64_t dst = src0;

            _dst_val[_i].i64 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            int64_t dst = src0;

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_f2i8(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            int8_t dst = src0;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            int8_t dst = src0;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            int8_t dst = src0;

            _dst_val[_i].i8 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_f2u1(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            uint1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            uint1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            uint1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_f2u16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            uint16_t dst = src0;

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            uint16_t dst = src0;

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            uint16_t dst = src0;

            _dst_val[_i].u16 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_f2u32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            uint32_t dst = src0;

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            uint32_t dst = src0;

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            uint32_t dst = src0;

            _dst_val[_i].u32 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_f2u64(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            uint64_t dst = src0;

            _dst_val[_i].u64 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            uint64_t dst = src0;

            _dst_val[_i].u64 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            uint64_t dst = src0;

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_f2u8(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            uint8_t dst = src0;

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            uint8_t dst = src0;

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            uint8_t dst = src0;

            _dst_val[_i].u8 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fabs(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = fabs(src0);

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = fabs(src0);

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = fabs(src0);

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fadd(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);

            float16_t dst = src0 + src1;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            float32_t dst = src0 + src1;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;

            float64_t dst = src0 + src1;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fall_equal2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
         0,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
         0,
         0,
      };

      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y)) ? 1.0f : 0.0f;

            _dst_val[0].f32 = dst.x;

}
static void
evaluate_fall_equal3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
            _src[1][2].f32,
         0,
      };

      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z)) ? 1.0f : 0.0f;

            _dst_val[0].f32 = dst.x;

}
static void
evaluate_fall_equal4(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
            _src[0][3].f32,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
            _src[1][2].f32,
            _src[1][3].f32,
      };

      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x == src1.x) && (src0.y == src1.y) && (src0.z == src1.z) && (src0.w == src1.w)) ? 1.0f : 0.0f;

            _dst_val[0].f32 = dst.x;

}
static void
evaluate_fand(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            float32_t dst = ((src0 != 0.0f) && (src1 != 0.0f)) ? 1.0f : 0.0f;

            _dst_val[_i].f32 = dst;
      }

}
static void
evaluate_fany_nequal2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
         0,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
         0,
         0,
      };

      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y)) ? 1.0f : 0.0f;

            _dst_val[0].f32 = dst.x;

}
static void
evaluate_fany_nequal3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
            _src[1][2].f32,
         0,
      };

      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z)) ? 1.0f : 0.0f;

            _dst_val[0].f32 = dst.x;

}
static void
evaluate_fany_nequal4(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
            _src[0][3].f32,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
            _src[1][2].f32,
            _src[1][3].f32,
      };

      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x != src1.x) || (src0.y != src1.y) || (src0.z != src1.z) || (src0.w != src1.w)) ? 1.0f : 0.0f;

            _dst_val[0].f32 = dst.x;

}
static void
evaluate_fceil(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = bit_size == 64 ? ceil(src0) : ceilf(src0);

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = bit_size == 64 ? ceil(src0) : ceilf(src0);

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = bit_size == 64 ? ceil(src0) : ceilf(src0);

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fcos(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = bit_size == 64 ? cos(src0) : cosf(src0);

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = bit_size == 64 ? cos(src0) : cosf(src0);

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = bit_size == 64 ? cos(src0) : cosf(src0);

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fcsel(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;
               const float32_t src2 =
                  _src[2][_i].f32;

            float32_t dst = (src0 != 0.0f) ? src1 : src2;

            _dst_val[_i].f32 = dst;
      }

}
static void
evaluate_fddx(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               
            float16_t dst = 0.0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               
            float32_t dst = 0.0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               
            float64_t dst = 0.0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fddx_coarse(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               
            float16_t dst = 0.0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               
            float32_t dst = 0.0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               
            float64_t dst = 0.0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fddx_fine(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               
            float16_t dst = 0.0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               
            float32_t dst = 0.0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               
            float64_t dst = 0.0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fddy(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               
            float16_t dst = 0.0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               
            float32_t dst = 0.0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               
            float64_t dst = 0.0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fddy_coarse(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               
            float16_t dst = 0.0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               
            float32_t dst = 0.0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               
            float64_t dst = 0.0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fddy_fine(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               
            float16_t dst = 0.0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               
            float32_t dst = 0.0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               
            float64_t dst = 0.0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fdiv(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);

            float16_t dst = src0 / src1;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            float32_t dst = src0 / src1;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;

            float64_t dst = src0 / src1;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fdot2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
         0,
         0,
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
         0,
         0,
      };

      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x * src1.x) + (src0.y * src1.y));

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
         0,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
         0,
         0,
      };

      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x * src1.x) + (src0.y * src1.y));

            _dst_val[0].f32 = dst.x;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
         0,
         0,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
         0,
         0,
      };

      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x * src1.x) + (src0.y * src1.y));

            _dst_val[0].f64 = dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fdot3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
            _mesa_half_to_float(_src[0][2].u16),
         0,
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
            _mesa_half_to_float(_src[1][2].u16),
         0,
      };

      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x * src1.x) + (src0.y * src1.y) + (src0.z * src1.z));

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
            _src[1][2].f32,
         0,
      };

      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x * src1.x) + (src0.y * src1.y) + (src0.z * src1.z));

            _dst_val[0].f32 = dst.x;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
            _src[0][2].f64,
         0,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
            _src[1][2].f64,
         0,
      };

      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x * src1.x) + (src0.y * src1.y) + (src0.z * src1.z));

            _dst_val[0].f64 = dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fdot4(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
            _mesa_half_to_float(_src[0][2].u16),
            _mesa_half_to_float(_src[0][3].u16),
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
            _mesa_half_to_float(_src[1][2].u16),
            _mesa_half_to_float(_src[1][3].u16),
      };

      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x * src1.x) + (src0.y * src1.y) + (src0.z * src1.z) + (src0.w * src1.w));

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
            _src[0][3].f32,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
            _src[1][2].f32,
            _src[1][3].f32,
      };

      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x * src1.x) + (src0.y * src1.y) + (src0.z * src1.z) + (src0.w * src1.w));

            _dst_val[0].f32 = dst.x;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
            _src[0][2].f64,
            _src[0][3].f64,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
            _src[1][2].f64,
            _src[1][3].f64,
      };

      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x * src1.x) + (src0.y * src1.y) + (src0.z * src1.z) + (src0.w * src1.w));

            _dst_val[0].f64 = dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fdot_replicated2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
         0,
         0,
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
         0,
         0,
      };

      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x * src1.x) + (src0.y * src1.y));

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);
            _dst_val[1].u16 = _mesa_float_to_half(dst.y);
            _dst_val[2].u16 = _mesa_float_to_half(dst.z);
            _dst_val[3].u16 = _mesa_float_to_half(dst.w);

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
         0,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
         0,
         0,
      };

      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x * src1.x) + (src0.y * src1.y));

            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;
            _dst_val[2].f32 = dst.z;
            _dst_val[3].f32 = dst.w;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
         0,
         0,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
         0,
         0,
      };

      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x * src1.x) + (src0.y * src1.y));

            _dst_val[0].f64 = dst.x;
            _dst_val[1].f64 = dst.y;
            _dst_val[2].f64 = dst.z;
            _dst_val[3].f64 = dst.w;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fdot_replicated3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
            _mesa_half_to_float(_src[0][2].u16),
         0,
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
            _mesa_half_to_float(_src[1][2].u16),
         0,
      };

      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x * src1.x) + (src0.y * src1.y) + (src0.z * src1.z));

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);
            _dst_val[1].u16 = _mesa_float_to_half(dst.y);
            _dst_val[2].u16 = _mesa_float_to_half(dst.z);
            _dst_val[3].u16 = _mesa_float_to_half(dst.w);

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
            _src[1][2].f32,
         0,
      };

      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x * src1.x) + (src0.y * src1.y) + (src0.z * src1.z));

            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;
            _dst_val[2].f32 = dst.z;
            _dst_val[3].f32 = dst.w;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
            _src[0][2].f64,
         0,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
            _src[1][2].f64,
         0,
      };

      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x * src1.x) + (src0.y * src1.y) + (src0.z * src1.z));

            _dst_val[0].f64 = dst.x;
            _dst_val[1].f64 = dst.y;
            _dst_val[2].f64 = dst.z;
            _dst_val[3].f64 = dst.w;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fdot_replicated4(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
            _mesa_half_to_float(_src[0][2].u16),
            _mesa_half_to_float(_src[0][3].u16),
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
            _mesa_half_to_float(_src[1][2].u16),
            _mesa_half_to_float(_src[1][3].u16),
      };

      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x * src1.x) + (src0.y * src1.y) + (src0.z * src1.z) + (src0.w * src1.w));

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);
            _dst_val[1].u16 = _mesa_float_to_half(dst.y);
            _dst_val[2].u16 = _mesa_float_to_half(dst.z);
            _dst_val[3].u16 = _mesa_float_to_half(dst.w);

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
            _src[0][3].f32,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
            _src[1][2].f32,
            _src[1][3].f32,
      };

      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x * src1.x) + (src0.y * src1.y) + (src0.z * src1.z) + (src0.w * src1.w));

            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;
            _dst_val[2].f32 = dst.z;
            _dst_val[3].f32 = dst.w;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
            _src[0][2].f64,
            _src[0][3].f64,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
            _src[1][2].f64,
            _src[1][3].f64,
      };

      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = ((src0.x * src1.x) + (src0.y * src1.y) + (src0.z * src1.z) + (src0.w * src1.w));

            _dst_val[0].f64 = dst.x;
            _dst_val[1].f64 = dst.y;
            _dst_val[2].f64 = dst.z;
            _dst_val[3].f64 = dst.w;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fdph(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
            _mesa_half_to_float(_src[0][2].u16),
         0,
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
            _mesa_half_to_float(_src[1][2].u16),
            _mesa_half_to_float(_src[1][3].u16),
      };

      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = src0.x * src1.x + src0.y * src1.y + src0.z * src1.z + src1.w;

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
            _src[1][2].f32,
            _src[1][3].f32,
      };

      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = src0.x * src1.x + src0.y * src1.y + src0.z * src1.z + src1.w;

            _dst_val[0].f32 = dst.x;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
            _src[0][2].f64,
         0,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
            _src[1][2].f64,
            _src[1][3].f64,
      };

      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = src0.x * src1.x + src0.y * src1.y + src0.z * src1.z + src1.w;

            _dst_val[0].f64 = dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fdph_replicated(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   


      const struct float16_vec src0 = {
            _mesa_half_to_float(_src[0][0].u16),
            _mesa_half_to_float(_src[0][1].u16),
            _mesa_half_to_float(_src[0][2].u16),
         0,
      };

      const struct float16_vec src1 = {
            _mesa_half_to_float(_src[1][0].u16),
            _mesa_half_to_float(_src[1][1].u16),
            _mesa_half_to_float(_src[1][2].u16),
            _mesa_half_to_float(_src[1][3].u16),
      };

      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = src0.x * src1.x + src0.y * src1.y + src0.z * src1.z + src1.w;

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);
            _dst_val[1].u16 = _mesa_float_to_half(dst.y);
            _dst_val[2].u16 = _mesa_float_to_half(dst.z);
            _dst_val[3].u16 = _mesa_float_to_half(dst.w);

         break;
      }
      case 32: {
         
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
            _src[1][1].f32,
            _src[1][2].f32,
            _src[1][3].f32,
      };

      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = src0.x * src1.x + src0.y * src1.y + src0.z * src1.z + src1.w;

            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;
            _dst_val[2].f32 = dst.z;
            _dst_val[3].f32 = dst.w;

         break;
      }
      case 64: {
         
   


      const struct float64_vec src0 = {
            _src[0][0].f64,
            _src[0][1].f64,
            _src[0][2].f64,
         0,
      };

      const struct float64_vec src1 = {
            _src[1][0].f64,
            _src[1][1].f64,
            _src[1][2].f64,
            _src[1][3].f64,
      };

      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = src0.x * src1.x + src0.y * src1.y + src0.z * src1.z + src1.w;

            _dst_val[0].f64 = dst.x;
            _dst_val[1].f64 = dst.y;
            _dst_val[2].f64 = dst.z;
            _dst_val[3].f64 = dst.w;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_feq(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);

            bool1_t dst = src0 == src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            bool1_t dst = src0 == src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;

            bool1_t dst = src0 == src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_feq32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);

            bool32_t dst = src0 == src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            bool32_t dst = src0 == src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;

            bool32_t dst = src0 == src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fexp2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = exp2f(src0);

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = exp2f(src0);

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = exp2f(src0);

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ffloor(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = bit_size == 64 ? floor(src0) : floorf(src0);

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = bit_size == 64 ? floor(src0) : floorf(src0);

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = bit_size == 64 ? floor(src0) : floorf(src0);

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ffma(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);
               const float src2 =
                  _mesa_half_to_float(_src[2][_i].u16);

            float16_t dst = src0 * src1 + src2;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;
               const float32_t src2 =
                  _src[2][_i].f32;

            float32_t dst = src0 * src1 + src2;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;
               const float64_t src2 =
                  _src[2][_i].f64;

            float64_t dst = src0 * src1 + src2;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ffract(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = src0 - (bit_size == 64 ? floor(src0) : floorf(src0));

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = src0 - (bit_size == 64 ? floor(src0) : floorf(src0));

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = src0 - (bit_size == 64 ? floor(src0) : floorf(src0));

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fge(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);

            bool1_t dst = src0 >= src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            bool1_t dst = src0 >= src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;

            bool1_t dst = src0 >= src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fge32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);

            bool32_t dst = src0 >= src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            bool32_t dst = src0 >= src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;

            bool32_t dst = src0 >= src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_find_lsb(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;

            int32_t dst;

            
dst = -1;
for (unsigned bit = 0; bit < bit_size; bit++) {
   if ((src0 >> bit) & 1) {
      dst = bit;
      break;
   }
}


            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;

            int32_t dst;

            
dst = -1;
for (unsigned bit = 0; bit < bit_size; bit++) {
   if ((src0 >> bit) & 1) {
      dst = bit;
      break;
   }
}


            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;

            int32_t dst;

            
dst = -1;
for (unsigned bit = 0; bit < bit_size; bit++) {
   if ((src0 >> bit) & 1) {
      dst = bit;
      break;
   }
}


            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;

            int32_t dst;

            
dst = -1;
for (unsigned bit = 0; bit < bit_size; bit++) {
   if ((src0 >> bit) & 1) {
      dst = bit;
      break;
   }
}


            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;

            int32_t dst;

            
dst = -1;
for (unsigned bit = 0; bit < bit_size; bit++) {
   if ((src0 >> bit) & 1) {
      dst = bit;
      break;
   }
}


            _dst_val[_i].i32 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_flog2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = log2f(src0);

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = log2f(src0);

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = log2f(src0);

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_flrp(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);
               const float src2 =
                  _mesa_half_to_float(_src[2][_i].u16);

            float16_t dst = src0 * (1 - src2) + src1 * src2;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;
               const float32_t src2 =
                  _src[2][_i].f32;

            float32_t dst = src0 * (1 - src2) + src1 * src2;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;
               const float64_t src2 =
                  _src[2][_i].f64;

            float64_t dst = src0 * (1 - src2) + src1 * src2;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_flt(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);

            bool1_t dst = src0 < src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            bool1_t dst = src0 < src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;

            bool1_t dst = src0 < src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_flt32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);

            bool32_t dst = src0 < src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            bool32_t dst = src0 < src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;

            bool32_t dst = src0 < src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fmax(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);

            float16_t dst = fmaxf(src0, src1);

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            float32_t dst = fmaxf(src0, src1);

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;

            float64_t dst = fmaxf(src0, src1);

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fmax3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);
               const float src2 =
                  _mesa_half_to_float(_src[2][_i].u16);

            float16_t dst = fmaxf(src0, fmaxf(src1, src2));

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;
               const float32_t src2 =
                  _src[2][_i].f32;

            float32_t dst = fmaxf(src0, fmaxf(src1, src2));

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;
               const float64_t src2 =
                  _src[2][_i].f64;

            float64_t dst = fmaxf(src0, fmaxf(src1, src2));

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fmed3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);
               const float src2 =
                  _mesa_half_to_float(_src[2][_i].u16);

            float16_t dst = fmaxf(fminf(fmaxf(src0, src1), src2), fminf(src0, src1));

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;
               const float32_t src2 =
                  _src[2][_i].f32;

            float32_t dst = fmaxf(fminf(fmaxf(src0, src1), src2), fminf(src0, src1));

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;
               const float64_t src2 =
                  _src[2][_i].f64;

            float64_t dst = fmaxf(fminf(fmaxf(src0, src1), src2), fminf(src0, src1));

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fmin(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);

            float16_t dst = fminf(src0, src1);

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            float32_t dst = fminf(src0, src1);

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;

            float64_t dst = fminf(src0, src1);

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fmin3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);
               const float src2 =
                  _mesa_half_to_float(_src[2][_i].u16);

            float16_t dst = fminf(src0, fminf(src1, src2));

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;
               const float32_t src2 =
                  _src[2][_i].f32;

            float32_t dst = fminf(src0, fminf(src1, src2));

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;
               const float64_t src2 =
                  _src[2][_i].f64;

            float64_t dst = fminf(src0, fminf(src1, src2));

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fmod(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);

            float16_t dst = src0 - src1 * floorf(src0 / src1);

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            float32_t dst = src0 - src1 * floorf(src0 / src1);

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;

            float64_t dst = src0 - src1 * floorf(src0 / src1);

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fmov(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = src0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = src0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fmul(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);

            float16_t dst = src0 * src1;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            float32_t dst = src0 * src1;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;

            float64_t dst = src0 * src1;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fne(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);

            bool1_t dst = src0 != src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            bool1_t dst = src0 != src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;

            bool1_t dst = src0 != src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fne32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);

            bool32_t dst = src0 != src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            bool32_t dst = src0 != src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;

            bool32_t dst = src0 != src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fneg(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = -src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = -src0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = -src0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fnoise1_1(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);

         break;
      }
      case 32: {
         
   

         
      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f32 = dst.x;

         break;
      }
      case 64: {
         
   

         
      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f64 = dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fnoise1_2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);

         break;
      }
      case 32: {
         
   

         
      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f32 = dst.x;

         break;
      }
      case 64: {
         
   

         
      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f64 = dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fnoise1_3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);

         break;
      }
      case 32: {
         
   

         
      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f32 = dst.x;

         break;
      }
      case 64: {
         
   

         
      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f64 = dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fnoise1_4(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);

         break;
      }
      case 32: {
         
   

         
      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f32 = dst.x;

         break;
      }
      case 64: {
         
   

         
      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f64 = dst.x;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fnoise2_1(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);
            _dst_val[1].u16 = _mesa_float_to_half(dst.y);

         break;
      }
      case 32: {
         
   

         
      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;

         break;
      }
      case 64: {
         
   

         
      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f64 = dst.x;
            _dst_val[1].f64 = dst.y;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fnoise2_2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);
            _dst_val[1].u16 = _mesa_float_to_half(dst.y);

         break;
      }
      case 32: {
         
   

         
      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;

         break;
      }
      case 64: {
         
   

         
      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f64 = dst.x;
            _dst_val[1].f64 = dst.y;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fnoise2_3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);
            _dst_val[1].u16 = _mesa_float_to_half(dst.y);

         break;
      }
      case 32: {
         
   

         
      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;

         break;
      }
      case 64: {
         
   

         
      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f64 = dst.x;
            _dst_val[1].f64 = dst.y;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fnoise2_4(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);
            _dst_val[1].u16 = _mesa_float_to_half(dst.y);

         break;
      }
      case 32: {
         
   

         
      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;

         break;
      }
      case 64: {
         
   

         
      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f64 = dst.x;
            _dst_val[1].f64 = dst.y;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fnoise3_1(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);
            _dst_val[1].u16 = _mesa_float_to_half(dst.y);
            _dst_val[2].u16 = _mesa_float_to_half(dst.z);

         break;
      }
      case 32: {
         
   

         
      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;
            _dst_val[2].f32 = dst.z;

         break;
      }
      case 64: {
         
   

         
      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f64 = dst.x;
            _dst_val[1].f64 = dst.y;
            _dst_val[2].f64 = dst.z;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fnoise3_2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);
            _dst_val[1].u16 = _mesa_float_to_half(dst.y);
            _dst_val[2].u16 = _mesa_float_to_half(dst.z);

         break;
      }
      case 32: {
         
   

         
      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;
            _dst_val[2].f32 = dst.z;

         break;
      }
      case 64: {
         
   

         
      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f64 = dst.x;
            _dst_val[1].f64 = dst.y;
            _dst_val[2].f64 = dst.z;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fnoise3_3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);
            _dst_val[1].u16 = _mesa_float_to_half(dst.y);
            _dst_val[2].u16 = _mesa_float_to_half(dst.z);

         break;
      }
      case 32: {
         
   

         
      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;
            _dst_val[2].f32 = dst.z;

         break;
      }
      case 64: {
         
   

         
      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f64 = dst.x;
            _dst_val[1].f64 = dst.y;
            _dst_val[2].f64 = dst.z;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fnoise3_4(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);
            _dst_val[1].u16 = _mesa_float_to_half(dst.y);
            _dst_val[2].u16 = _mesa_float_to_half(dst.z);

         break;
      }
      case 32: {
         
   

         
      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;
            _dst_val[2].f32 = dst.z;

         break;
      }
      case 64: {
         
   

         
      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f64 = dst.x;
            _dst_val[1].f64 = dst.y;
            _dst_val[2].f64 = dst.z;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fnoise4_1(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);
            _dst_val[1].u16 = _mesa_float_to_half(dst.y);
            _dst_val[2].u16 = _mesa_float_to_half(dst.z);
            _dst_val[3].u16 = _mesa_float_to_half(dst.w);

         break;
      }
      case 32: {
         
   

         
      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;
            _dst_val[2].f32 = dst.z;
            _dst_val[3].f32 = dst.w;

         break;
      }
      case 64: {
         
   

         
      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f64 = dst.x;
            _dst_val[1].f64 = dst.y;
            _dst_val[2].f64 = dst.z;
            _dst_val[3].f64 = dst.w;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fnoise4_2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);
            _dst_val[1].u16 = _mesa_float_to_half(dst.y);
            _dst_val[2].u16 = _mesa_float_to_half(dst.z);
            _dst_val[3].u16 = _mesa_float_to_half(dst.w);

         break;
      }
      case 32: {
         
   

         
      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;
            _dst_val[2].f32 = dst.z;
            _dst_val[3].f32 = dst.w;

         break;
      }
      case 64: {
         
   

         
      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f64 = dst.x;
            _dst_val[1].f64 = dst.y;
            _dst_val[2].f64 = dst.z;
            _dst_val[3].f64 = dst.w;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fnoise4_3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);
            _dst_val[1].u16 = _mesa_float_to_half(dst.y);
            _dst_val[2].u16 = _mesa_float_to_half(dst.z);
            _dst_val[3].u16 = _mesa_float_to_half(dst.w);

         break;
      }
      case 32: {
         
   

         
      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;
            _dst_val[2].f32 = dst.z;
            _dst_val[3].f32 = dst.w;

         break;
      }
      case 64: {
         
   

         
      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f64 = dst.x;
            _dst_val[1].f64 = dst.y;
            _dst_val[2].f64 = dst.z;
            _dst_val[3].f64 = dst.w;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fnoise4_4(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      struct float16_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].u16 = _mesa_float_to_half(dst.x);
            _dst_val[1].u16 = _mesa_float_to_half(dst.y);
            _dst_val[2].u16 = _mesa_float_to_half(dst.z);
            _dst_val[3].u16 = _mesa_float_to_half(dst.w);

         break;
      }
      case 32: {
         
   

         
      struct float32_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;
            _dst_val[2].f32 = dst.z;
            _dst_val[3].f32 = dst.w;

         break;
      }
      case 64: {
         
   

         
      struct float64_vec dst;

         dst.x = dst.y = dst.z = dst.w = 0.0f;

            _dst_val[0].f64 = dst.x;
            _dst_val[1].f64 = dst.y;
            _dst_val[2].f64 = dst.z;
            _dst_val[3].f64 = dst.w;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fnot(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = bit_size == 64 ? ((src0 == 0.0) ? 1.0 : 0.0f) : ((src0 == 0.0f) ? 1.0f : 0.0f);

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = bit_size == 64 ? ((src0 == 0.0) ? 1.0 : 0.0f) : ((src0 == 0.0f) ? 1.0f : 0.0f);

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = bit_size == 64 ? ((src0 == 0.0) ? 1.0 : 0.0f) : ((src0 == 0.0f) ? 1.0f : 0.0f);

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_for(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            float32_t dst = ((src0 != 0.0f) || (src1 != 0.0f)) ? 1.0f : 0.0f;

            _dst_val[_i].f32 = dst;
      }

}
static void
evaluate_fpow(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);

            float16_t dst = bit_size == 64 ? powf(src0, src1) : pow(src0, src1);

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            float32_t dst = bit_size == 64 ? powf(src0, src1) : pow(src0, src1);

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;

            float64_t dst = bit_size == 64 ? powf(src0, src1) : pow(src0, src1);

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fquantize2f16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = (fabs(src0) < ldexpf(1.0, -14)) ? copysignf(0.0f, src0) : _mesa_half_to_float(_mesa_float_to_half(src0));

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = (fabs(src0) < ldexpf(1.0, -14)) ? copysignf(0.0f, src0) : _mesa_half_to_float(_mesa_float_to_half(src0));

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = (fabs(src0) < ldexpf(1.0, -14)) ? copysignf(0.0f, src0) : _mesa_half_to_float(_mesa_float_to_half(src0));

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_frcp(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = bit_size == 64 ? 1.0 / src0 : 1.0f / src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = bit_size == 64 ? 1.0 / src0 : 1.0f / src0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = bit_size == 64 ? 1.0 / src0 : 1.0f / src0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_frem(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);

            float16_t dst = src0 - src1 * truncf(src0 / src1);

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            float32_t dst = src0 - src1 * truncf(src0 / src1);

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;

            float64_t dst = src0 - src1 * truncf(src0 / src1);

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_frexp_exp(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            int32_t dst;

            frexp(src0, &dst);

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            int32_t dst;

            frexp(src0, &dst);

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            int32_t dst;

            frexp(src0, &dst);

            _dst_val[_i].i32 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_frexp_sig(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst;

            int n; dst = frexp(src0, &n);

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst;

            int n; dst = frexp(src0, &n);

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst;

            int n; dst = frexp(src0, &n);

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fround_even(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = bit_size == 64 ? _mesa_roundeven(src0) : _mesa_roundevenf(src0);

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = bit_size == 64 ? _mesa_roundeven(src0) : _mesa_roundevenf(src0);

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = bit_size == 64 ? _mesa_roundeven(src0) : _mesa_roundevenf(src0);

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_frsq(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = bit_size == 64 ? 1.0 / sqrt(src0) : 1.0f / sqrtf(src0);

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = bit_size == 64 ? 1.0 / sqrt(src0) : 1.0f / sqrtf(src0);

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = bit_size == 64 ? 1.0 / sqrt(src0) : 1.0f / sqrtf(src0);

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fsat(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = bit_size == 64 ? ((src0 > 1.0) ? 1.0 : ((src0 <= 0.0) ? 0.0 : src0)) : ((src0 > 1.0f) ? 1.0f : ((src0 <= 0.0f) ? 0.0f : src0));

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = bit_size == 64 ? ((src0 > 1.0) ? 1.0 : ((src0 <= 0.0) ? 0.0 : src0)) : ((src0 > 1.0f) ? 1.0f : ((src0 <= 0.0f) ? 0.0f : src0));

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = bit_size == 64 ? ((src0 > 1.0) ? 1.0 : ((src0 <= 0.0) ? 0.0 : src0)) : ((src0 > 1.0f) ? 1.0f : ((src0 <= 0.0f) ? 0.0f : src0));

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fsign(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = bit_size == 64 ? ((src0 == 0.0) ? 0.0 : ((src0 > 0.0) ? 1.0 : -1.0)) : ((src0 == 0.0f) ? 0.0f : ((src0 > 0.0f) ? 1.0f : -1.0f));

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = bit_size == 64 ? ((src0 == 0.0) ? 0.0 : ((src0 > 0.0) ? 1.0 : -1.0)) : ((src0 == 0.0f) ? 0.0f : ((src0 > 0.0f) ? 1.0f : -1.0f));

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = bit_size == 64 ? ((src0 == 0.0) ? 0.0 : ((src0 > 0.0) ? 1.0 : -1.0)) : ((src0 == 0.0f) ? 0.0f : ((src0 > 0.0f) ? 1.0f : -1.0f));

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fsin(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = bit_size == 64 ? sin(src0) : sinf(src0);

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = bit_size == 64 ? sin(src0) : sinf(src0);

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = bit_size == 64 ? sin(src0) : sinf(src0);

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fsqrt(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = bit_size == 64 ? sqrt(src0) : sqrtf(src0);

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = bit_size == 64 ? sqrt(src0) : sqrtf(src0);

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = bit_size == 64 ? sqrt(src0) : sqrtf(src0);

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fsub(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);

            float16_t dst = src0 - src1;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            float32_t dst = src0 - src1;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;

            float64_t dst = src0 - src1;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ftrunc(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);

            float16_t dst = bit_size == 64 ? trunc(src0) : truncf(src0);

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;

            float32_t dst = bit_size == 64 ? trunc(src0) : truncf(src0);

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;

            float64_t dst = bit_size == 64 ? trunc(src0) : truncf(src0);

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_fxor(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            float32_t dst = (src0 != 0.0f && src1 == 0.0f) || (src0 == 0.0f && src1 != 0.0f) ? 1.0f : 0.0f;

            _dst_val[_i].f32 = dst;
      }

}
static void
evaluate_i2b1(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;

            bool1_t dst = src0 != 0;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;

            bool1_t dst = src0 != 0;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;

            bool1_t dst = src0 != 0;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;

            bool1_t dst = src0 != 0;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;

            bool1_t dst = src0 != 0;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_i2b32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;

            bool32_t dst = src0 != 0;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;

            bool32_t dst = src0 != 0;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;

            bool32_t dst = src0 != 0;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;

            bool32_t dst = src0 != 0;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;

            bool32_t dst = src0 != 0;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_i2f16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_i2f32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;

            float32_t dst = src0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;

            float32_t dst = src0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;

            float32_t dst = src0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;

            float32_t dst = src0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;

            float32_t dst = src0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_i2f64(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;

            float64_t dst = src0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;

            float64_t dst = src0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;

            float64_t dst = src0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;

            float64_t dst = src0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;

            float64_t dst = src0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_i2i1(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;

            int1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;

            int1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;

            int1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;

            int1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;

            int1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_i2i16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;

            int16_t dst = src0;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;

            int16_t dst = src0;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;

            int16_t dst = src0;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;

            int16_t dst = src0;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;

            int16_t dst = src0;

            _dst_val[_i].i16 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_i2i32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;

            int32_t dst = src0;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;

            int32_t dst = src0;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;

            int32_t dst = src0;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;

            int32_t dst = src0;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;

            int32_t dst = src0;

            _dst_val[_i].i32 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_i2i64(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;

            int64_t dst = src0;

            _dst_val[_i].i64 = dst;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;

            int64_t dst = src0;

            _dst_val[_i].i64 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;

            int64_t dst = src0;

            _dst_val[_i].i64 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;

            int64_t dst = src0;

            _dst_val[_i].i64 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;

            int64_t dst = src0;

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_i2i8(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;

            int8_t dst = src0;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;

            int8_t dst = src0;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;

            int8_t dst = src0;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;

            int8_t dst = src0;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;

            int8_t dst = src0;

            _dst_val[_i].i8 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_iabs(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;

            int1_t dst = (src0 < 0) ? -src0 : src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;

            int8_t dst = (src0 < 0) ? -src0 : src0;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;

            int16_t dst = (src0 < 0) ? -src0 : src0;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;

            int32_t dst = (src0 < 0) ? -src0 : src0;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;

            int64_t dst = (src0 < 0) ? -src0 : src0;

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_iadd(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            int1_t dst = src0 + src1;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            int8_t dst = src0 + src1;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            int16_t dst = src0 + src1;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst = src0 + src1;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            int64_t dst = src0 + src1;

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_iadd_sat(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            int1_t dst = 
      src1 > 0 ?
         (src0 + src1 < src0 ? (1ull << (bit_size - 1)) - 1 : src0 + src1) :
         (src0 < src0 + src1 ? (1ull << (bit_size - 1))     : src0 + src1)
;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            int8_t dst = 
      src1 > 0 ?
         (src0 + src1 < src0 ? (1ull << (bit_size - 1)) - 1 : src0 + src1) :
         (src0 < src0 + src1 ? (1ull << (bit_size - 1))     : src0 + src1)
;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            int16_t dst = 
      src1 > 0 ?
         (src0 + src1 < src0 ? (1ull << (bit_size - 1)) - 1 : src0 + src1) :
         (src0 < src0 + src1 ? (1ull << (bit_size - 1))     : src0 + src1)
;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst = 
      src1 > 0 ?
         (src0 + src1 < src0 ? (1ull << (bit_size - 1)) - 1 : src0 + src1) :
         (src0 < src0 + src1 ? (1ull << (bit_size - 1))     : src0 + src1)
;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            int64_t dst = 
      src1 > 0 ?
         (src0 + src1 < src0 ? (1ull << (bit_size - 1)) - 1 : src0 + src1) :
         (src0 < src0 + src1 ? (1ull << (bit_size - 1))     : src0 + src1)
;

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_iand(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            uint1_t dst = src0 & src1;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            uint8_t dst = src0 & src1;

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            uint16_t dst = src0 & src1;

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint32_t dst = src0 & src1;

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            uint64_t dst = src0 & src1;

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ibfe(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;
               const int32_t src2 =
                  _src[2][_i].i32;

            int32_t dst;

            
int base = src0;
int offset = src1, bits = src2;
if (bits == 0) {
   dst = 0;
} else if (bits < 0 || offset < 0) {
   dst = 0; /* undefined */
} else if (offset + bits < 32) {
   dst = (base << (32 - bits - offset)) >> (32 - bits);
} else {
   dst = base >> offset;
}


            _dst_val[_i].i32 = dst;
      }

}
static void
evaluate_ibitfield_extract(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;
               const int32_t src2 =
                  _src[2][_i].i32;

            int32_t dst;

            
int base = src0;
int offset = src1, bits = src2;
if (bits == 0) {
   dst = 0;
} else if (offset < 0 || bits < 0 || offset + bits > 32) {
   dst = 0;
} else {
   dst = (base << (32 - offset - bits)) >> offset; /* use sign-extending shift */
}


            _dst_val[_i].i32 = dst;
      }

}
static void
evaluate_idiv(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            int1_t dst = src1 == 0 ? 0 : (src0 / src1);

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            int8_t dst = src1 == 0 ? 0 : (src0 / src1);

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            int16_t dst = src1 == 0 ? 0 : (src0 / src1);

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst = src1 == 0 ? 0 : (src0 / src1);

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            int64_t dst = src1 == 0 ? 0 : (src0 / src1);

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ieq(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            bool1_t dst = src0 == src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            bool1_t dst = src0 == src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            bool1_t dst = src0 == src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            bool1_t dst = src0 == src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            bool1_t dst = src0 == src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ieq32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            bool32_t dst = src0 == src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            bool32_t dst = src0 == src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            bool32_t dst = src0 == src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            bool32_t dst = src0 == src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            bool32_t dst = src0 == src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ifind_msb(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;

            int32_t dst;

            
dst = -1;
for (int bit = 31; bit >= 0; bit--) {
   /* If src0 < 0, we're looking for the first 0 bit.
    * if src0 >= 0, we're looking for the first 1 bit.
    */
   if ((((src0 >> bit) & 1) && (src0 >= 0)) ||
      (!((src0 >> bit) & 1) && (src0 < 0))) {
      dst = bit;
      break;
   }
}


            _dst_val[_i].i32 = dst;
      }

}
static void
evaluate_ige(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            bool1_t dst = src0 >= src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            bool1_t dst = src0 >= src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            bool1_t dst = src0 >= src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            bool1_t dst = src0 >= src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            bool1_t dst = src0 >= src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ige32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            bool32_t dst = src0 >= src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            bool32_t dst = src0 >= src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            bool32_t dst = src0 >= src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            bool32_t dst = src0 >= src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            bool32_t dst = src0 >= src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ihadd(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            int1_t dst = (src0 & src1) + ((src0 ^ src1) >> 1);

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            int8_t dst = (src0 & src1) + ((src0 ^ src1) >> 1);

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            int16_t dst = (src0 & src1) + ((src0 ^ src1) >> 1);

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst = (src0 & src1) + ((src0 ^ src1) >> 1);

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            int64_t dst = (src0 & src1) + ((src0 ^ src1) >> 1);

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ilt(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            bool1_t dst = src0 < src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            bool1_t dst = src0 < src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            bool1_t dst = src0 < src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            bool1_t dst = src0 < src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            bool1_t dst = src0 < src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ilt32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            bool32_t dst = src0 < src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            bool32_t dst = src0 < src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            bool32_t dst = src0 < src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            bool32_t dst = src0 < src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            bool32_t dst = src0 < src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_imax(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            int1_t dst = src1 > src0 ? src1 : src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            int8_t dst = src1 > src0 ? src1 : src0;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            int16_t dst = src1 > src0 ? src1 : src0;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst = src1 > src0 ? src1 : src0;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            int64_t dst = src1 > src0 ? src1 : src0;

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_imax3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src2 = -(int1_t)_src[2][_i].b;

            int1_t dst = MAX2(src0, MAX2(src1, src2));

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;
               const int8_t src2 =
                  _src[2][_i].i8;

            int8_t dst = MAX2(src0, MAX2(src1, src2));

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;
               const int16_t src2 =
                  _src[2][_i].i16;

            int16_t dst = MAX2(src0, MAX2(src1, src2));

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;
               const int32_t src2 =
                  _src[2][_i].i32;

            int32_t dst = MAX2(src0, MAX2(src1, src2));

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;
               const int64_t src2 =
                  _src[2][_i].i64;

            int64_t dst = MAX2(src0, MAX2(src1, src2));

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_imed3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src2 = -(int1_t)_src[2][_i].b;

            int1_t dst = MAX2(MIN2(MAX2(src0, src1), src2), MIN2(src0, src1));

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;
               const int8_t src2 =
                  _src[2][_i].i8;

            int8_t dst = MAX2(MIN2(MAX2(src0, src1), src2), MIN2(src0, src1));

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;
               const int16_t src2 =
                  _src[2][_i].i16;

            int16_t dst = MAX2(MIN2(MAX2(src0, src1), src2), MIN2(src0, src1));

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;
               const int32_t src2 =
                  _src[2][_i].i32;

            int32_t dst = MAX2(MIN2(MAX2(src0, src1), src2), MIN2(src0, src1));

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;
               const int64_t src2 =
                  _src[2][_i].i64;

            int64_t dst = MAX2(MIN2(MAX2(src0, src1), src2), MIN2(src0, src1));

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_imin(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            int1_t dst = src1 > src0 ? src0 : src1;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            int8_t dst = src1 > src0 ? src0 : src1;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            int16_t dst = src1 > src0 ? src0 : src1;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst = src1 > src0 ? src0 : src1;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            int64_t dst = src1 > src0 ? src0 : src1;

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_imin3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src2 = -(int1_t)_src[2][_i].b;

            int1_t dst = MIN2(src0, MIN2(src1, src2));

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;
               const int8_t src2 =
                  _src[2][_i].i8;

            int8_t dst = MIN2(src0, MIN2(src1, src2));

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;
               const int16_t src2 =
                  _src[2][_i].i16;

            int16_t dst = MIN2(src0, MIN2(src1, src2));

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;
               const int32_t src2 =
                  _src[2][_i].i32;

            int32_t dst = MIN2(src0, MIN2(src1, src2));

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;
               const int64_t src2 =
                  _src[2][_i].i64;

            int64_t dst = MIN2(src0, MIN2(src1, src2));

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_imod(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            int1_t dst = src1 == 0 ? 0 : ((src0 % src1 == 0 || (src0 >= 0) == (src1 >= 0)) ?                 src0 % src1 : src0 % src1 + src1);

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            int8_t dst = src1 == 0 ? 0 : ((src0 % src1 == 0 || (src0 >= 0) == (src1 >= 0)) ?                 src0 % src1 : src0 % src1 + src1);

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            int16_t dst = src1 == 0 ? 0 : ((src0 % src1 == 0 || (src0 >= 0) == (src1 >= 0)) ?                 src0 % src1 : src0 % src1 + src1);

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst = src1 == 0 ? 0 : ((src0 % src1 == 0 || (src0 >= 0) == (src1 >= 0)) ?                 src0 % src1 : src0 % src1 + src1);

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            int64_t dst = src1 == 0 ? 0 : ((src0 % src1 == 0 || (src0 >= 0) == (src1 >= 0)) ?                 src0 % src1 : src0 % src1 + src1);

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_imov(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;

            int1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;

            int8_t dst = src0;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;

            int16_t dst = src0;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;

            int32_t dst = src0;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;

            int64_t dst = src0;

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_imul(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            int1_t dst = src0 * src1;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            int8_t dst = src0 * src1;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            int16_t dst = src0 * src1;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst = src0 * src1;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            int64_t dst = src0 * src1;

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_imul_2x32_64(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int64_t dst = (int64_t)src0 * (int64_t)src1;

            _dst_val[_i].i64 = dst;
      }

}
static void
evaluate_imul_high(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            int1_t dst;

            
if (bit_size == 64) {
   /* We need to do a full 128-bit x 128-bit multiply in order for the sign
    * extension to work properly.  The casts are kind-of annoying but needed
    * to prevent compiler warnings.
    */
   uint32_t src0_u32[4] = {
      src0,
      (int64_t)src0 >> 32,
      (int64_t)src0 >> 63,
      (int64_t)src0 >> 63,
   };
   uint32_t src1_u32[4] = {
      src1,
      (int64_t)src1 >> 32,
      (int64_t)src1 >> 63,
      (int64_t)src1 >> 63,
   };
   uint32_t prod_u32[4];
   ubm_mul_u32arr(prod_u32, src0_u32, src1_u32);
   dst = (uint64_t)prod_u32[2] | ((uint64_t)prod_u32[3] << 32);
} else {
   dst = ((int64_t)src0 * (int64_t)src1) >> bit_size;
}


            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            int8_t dst;

            
if (bit_size == 64) {
   /* We need to do a full 128-bit x 128-bit multiply in order for the sign
    * extension to work properly.  The casts are kind-of annoying but needed
    * to prevent compiler warnings.
    */
   uint32_t src0_u32[4] = {
      src0,
      (int64_t)src0 >> 32,
      (int64_t)src0 >> 63,
      (int64_t)src0 >> 63,
   };
   uint32_t src1_u32[4] = {
      src1,
      (int64_t)src1 >> 32,
      (int64_t)src1 >> 63,
      (int64_t)src1 >> 63,
   };
   uint32_t prod_u32[4];
   ubm_mul_u32arr(prod_u32, src0_u32, src1_u32);
   dst = (uint64_t)prod_u32[2] | ((uint64_t)prod_u32[3] << 32);
} else {
   dst = ((int64_t)src0 * (int64_t)src1) >> bit_size;
}


            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            int16_t dst;

            
if (bit_size == 64) {
   /* We need to do a full 128-bit x 128-bit multiply in order for the sign
    * extension to work properly.  The casts are kind-of annoying but needed
    * to prevent compiler warnings.
    */
   uint32_t src0_u32[4] = {
      src0,
      (int64_t)src0 >> 32,
      (int64_t)src0 >> 63,
      (int64_t)src0 >> 63,
   };
   uint32_t src1_u32[4] = {
      src1,
      (int64_t)src1 >> 32,
      (int64_t)src1 >> 63,
      (int64_t)src1 >> 63,
   };
   uint32_t prod_u32[4];
   ubm_mul_u32arr(prod_u32, src0_u32, src1_u32);
   dst = (uint64_t)prod_u32[2] | ((uint64_t)prod_u32[3] << 32);
} else {
   dst = ((int64_t)src0 * (int64_t)src1) >> bit_size;
}


            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst;

            
if (bit_size == 64) {
   /* We need to do a full 128-bit x 128-bit multiply in order for the sign
    * extension to work properly.  The casts are kind-of annoying but needed
    * to prevent compiler warnings.
    */
   uint32_t src0_u32[4] = {
      src0,
      (int64_t)src0 >> 32,
      (int64_t)src0 >> 63,
      (int64_t)src0 >> 63,
   };
   uint32_t src1_u32[4] = {
      src1,
      (int64_t)src1 >> 32,
      (int64_t)src1 >> 63,
      (int64_t)src1 >> 63,
   };
   uint32_t prod_u32[4];
   ubm_mul_u32arr(prod_u32, src0_u32, src1_u32);
   dst = (uint64_t)prod_u32[2] | ((uint64_t)prod_u32[3] << 32);
} else {
   dst = ((int64_t)src0 * (int64_t)src1) >> bit_size;
}


            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            int64_t dst;

            
if (bit_size == 64) {
   /* We need to do a full 128-bit x 128-bit multiply in order for the sign
    * extension to work properly.  The casts are kind-of annoying but needed
    * to prevent compiler warnings.
    */
   uint32_t src0_u32[4] = {
      src0,
      (int64_t)src0 >> 32,
      (int64_t)src0 >> 63,
      (int64_t)src0 >> 63,
   };
   uint32_t src1_u32[4] = {
      src1,
      (int64_t)src1 >> 32,
      (int64_t)src1 >> 63,
      (int64_t)src1 >> 63,
   };
   uint32_t prod_u32[4];
   ubm_mul_u32arr(prod_u32, src0_u32, src1_u32);
   dst = (uint64_t)prod_u32[2] | ((uint64_t)prod_u32[3] << 32);
} else {
   dst = ((int64_t)src0 * (int64_t)src1) >> bit_size;
}


            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ine(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            bool1_t dst = src0 != src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            bool1_t dst = src0 != src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            bool1_t dst = src0 != src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            bool1_t dst = src0 != src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            bool1_t dst = src0 != src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ine32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            bool32_t dst = src0 != src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            bool32_t dst = src0 != src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            bool32_t dst = src0 != src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            bool32_t dst = src0 != src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            bool32_t dst = src0 != src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ineg(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;

            int1_t dst = -src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;

            int8_t dst = -src0;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;

            int16_t dst = -src0;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;

            int32_t dst = -src0;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;

            int64_t dst = -src0;

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_inot(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;

            int1_t dst = ~src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;

            int8_t dst = ~src0;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;

            int16_t dst = ~src0;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;

            int32_t dst = ~src0;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;

            int64_t dst = ~src0;

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ior(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            uint1_t dst = src0 | src1;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            uint8_t dst = src0 | src1;

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            uint16_t dst = src0 | src1;

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint32_t dst = src0 | src1;

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            uint64_t dst = src0 | src1;

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_irem(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            int1_t dst = src1 == 0 ? 0 : src0 % src1;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            int8_t dst = src1 == 0 ? 0 : src0 % src1;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            int16_t dst = src1 == 0 ? 0 : src0 % src1;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst = src1 == 0 ? 0 : src0 % src1;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            int64_t dst = src1 == 0 ? 0 : src0 % src1;

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_irhadd(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            int1_t dst = (src0 | src1) + ((src0 ^ src1) >> 1);

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            int8_t dst = (src0 | src1) + ((src0 ^ src1) >> 1);

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            int16_t dst = (src0 | src1) + ((src0 ^ src1) >> 1);

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst = (src0 | src1) + ((src0 ^ src1) >> 1);

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            int64_t dst = (src0 | src1) + ((src0 ^ src1) >> 1);

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ishl(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               const uint32_t src1 =
                  _src[1][_i].u32;

            int1_t dst = src0 << (src1 & (sizeof(src0) * 8 - 1));

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const uint32_t src1 =
                  _src[1][_i].u32;

            int8_t dst = src0 << (src1 & (sizeof(src0) * 8 - 1));

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const uint32_t src1 =
                  _src[1][_i].u32;

            int16_t dst = src0 << (src1 & (sizeof(src0) * 8 - 1));

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            int32_t dst = src0 << (src1 & (sizeof(src0) * 8 - 1));

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const uint32_t src1 =
                  _src[1][_i].u32;

            int64_t dst = src0 << (src1 & (sizeof(src0) * 8 - 1));

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ishr(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               const uint32_t src1 =
                  _src[1][_i].u32;

            int1_t dst = src0 >> (src1 & (sizeof(src0) * 8 - 1));

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const uint32_t src1 =
                  _src[1][_i].u32;

            int8_t dst = src0 >> (src1 & (sizeof(src0) * 8 - 1));

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const uint32_t src1 =
                  _src[1][_i].u32;

            int16_t dst = src0 >> (src1 & (sizeof(src0) * 8 - 1));

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            int32_t dst = src0 >> (src1 & (sizeof(src0) * 8 - 1));

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const uint32_t src1 =
                  _src[1][_i].u32;

            int64_t dst = src0 >> (src1 & (sizeof(src0) * 8 - 1));

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_isign(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;

            int1_t dst = (src0 == 0) ? 0 : ((src0 > 0) ? 1 : -1);

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;

            int8_t dst = (src0 == 0) ? 0 : ((src0 > 0) ? 1 : -1);

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;

            int16_t dst = (src0 == 0) ? 0 : ((src0 > 0) ? 1 : -1);

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;

            int32_t dst = (src0 == 0) ? 0 : ((src0 > 0) ? 1 : -1);

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;

            int64_t dst = (src0 == 0) ? 0 : ((src0 > 0) ? 1 : -1);

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_isub(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            int1_t dst = src0 - src1;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            int8_t dst = src0 - src1;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            int16_t dst = src0 - src1;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst = src0 - src1;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            int64_t dst = src0 - src1;

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_isub_sat(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src0 = -(int1_t)_src[0][_i].b;
               /* 1-bit integers use a 0/-1 convention */
               const int1_t src1 = -(int1_t)_src[1][_i].b;

            int1_t dst = 
      src1 < 0 ?
         (src0 - src1 < src0 ? (1ull << (bit_size - 1)) - 1 : src0 - src1) :
         (src0 < src0 - src1 ? (1ull << (bit_size - 1))     : src0 - src1)
;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int8_t src0 =
                  _src[0][_i].i8;
               const int8_t src1 =
                  _src[1][_i].i8;

            int8_t dst = 
      src1 < 0 ?
         (src0 - src1 < src0 ? (1ull << (bit_size - 1)) - 1 : src0 - src1) :
         (src0 < src0 - src1 ? (1ull << (bit_size - 1))     : src0 - src1)
;

            _dst_val[_i].i8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int16_t src0 =
                  _src[0][_i].i16;
               const int16_t src1 =
                  _src[1][_i].i16;

            int16_t dst = 
      src1 < 0 ?
         (src0 - src1 < src0 ? (1ull << (bit_size - 1)) - 1 : src0 - src1) :
         (src0 < src0 - src1 ? (1ull << (bit_size - 1))     : src0 - src1)
;

            _dst_val[_i].i16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst = 
      src1 < 0 ?
         (src0 - src1 < src0 ? (1ull << (bit_size - 1)) - 1 : src0 - src1) :
         (src0 < src0 - src1 ? (1ull << (bit_size - 1))     : src0 - src1)
;

            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int64_t src0 =
                  _src[0][_i].i64;
               const int64_t src1 =
                  _src[1][_i].i64;

            int64_t dst = 
      src1 < 0 ?
         (src0 - src1 < src0 ? (1ull << (bit_size - 1)) - 1 : src0 - src1) :
         (src0 < src0 - src1 ? (1ull << (bit_size - 1))     : src0 - src1)
;

            _dst_val[_i].i64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ixor(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            uint1_t dst = src0 ^ src1;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            uint8_t dst = src0 ^ src1;

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            uint16_t dst = src0 ^ src1;

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint32_t dst = src0 ^ src1;

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            uint64_t dst = src0 ^ src1;

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ldexp(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const int32_t src1 =
                  _src[1][_i].i32;

            float16_t dst;

            
dst = (bit_size == 64) ? ldexp(src0, src1) : ldexpf(src0, src1);
/* flush denormals to zero. */
if (!isnormal(dst))
   dst = copysignf(0.0f, src0);


            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const int32_t src1 =
                  _src[1][_i].i32;

            float32_t dst;

            
dst = (bit_size == 64) ? ldexp(src0, src1) : ldexpf(src0, src1);
/* flush denormals to zero. */
if (!isnormal(dst))
   dst = copysignf(0.0f, src0);


            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const int32_t src1 =
                  _src[1][_i].i32;

            float64_t dst;

            
dst = (bit_size == 64) ? ldexp(src0, src1) : ldexpf(src0, src1);
/* flush denormals to zero. */
if (!isnormal(dst))
   dst = copysignf(0.0f, src0);


            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_pack_32_2x16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct uint16_vec src0 = {
            _src[0][0].u16,
            _src[0][1].u16,
         0,
         0,
      };

      struct uint32_vec dst;

         dst.x = src0.x | ((uint32_t)src0.y << 16);

            _dst_val[0].u32 = dst.x;

}
static void
evaluate_pack_32_2x16_split(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            uint32_t dst = src0 | ((uint32_t)src1 << 16);

            _dst_val[_i].u32 = dst;
      }

}
static void
evaluate_pack_64_2x32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct uint32_vec src0 = {
            _src[0][0].u32,
            _src[0][1].u32,
         0,
         0,
      };

      struct uint64_vec dst;

         dst.x = src0.x | ((uint64_t)src0.y << 32);

            _dst_val[0].u64 = dst.x;

}
static void
evaluate_pack_64_2x32_split(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint64_t dst = src0 | ((uint64_t)src1 << 32);

            _dst_val[_i].u64 = dst;
      }

}
static void
evaluate_pack_64_4x16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct uint16_vec src0 = {
            _src[0][0].u16,
            _src[0][1].u16,
            _src[0][2].u16,
            _src[0][3].u16,
      };

      struct uint64_vec dst;

         dst.x = src0.x | ((uint64_t)src0.y << 16) | ((uint64_t)src0.z << 32) | ((uint64_t)src0.w << 48);

            _dst_val[0].u64 = dst.x;

}
static void
evaluate_pack_half_2x16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
         0,
         0,
      };

      struct uint32_vec dst;

         
dst.x = (uint32_t) pack_half_1x16(src0.x);
dst.x |= ((uint32_t) pack_half_1x16(src0.y)) << 16;


            _dst_val[0].u32 = dst.x;

}
static void
evaluate_pack_half_2x16_split(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
         0,
         0,
         0,
      };

      const struct float32_vec src1 = {
            _src[1][0].f32,
         0,
         0,
         0,
      };

      struct uint32_vec dst;

         dst.x = dst.y = dst.z = dst.w = pack_half_1x16(src0.x) | (pack_half_1x16(src1.x) << 16);

            _dst_val[0].u32 = dst.x;

}
static void
evaluate_pack_snorm_2x16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
         0,
         0,
      };

      struct uint32_vec dst;

         
dst.x = (uint32_t) pack_snorm_1x16(src0.x);
dst.x |= ((uint32_t) pack_snorm_1x16(src0.y)) << 16;


            _dst_val[0].u32 = dst.x;

}
static void
evaluate_pack_snorm_4x8(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
            _src[0][3].f32,
      };

      struct uint32_vec dst;

         
dst.x = (uint32_t) pack_snorm_1x8(src0.x);
dst.x |= ((uint32_t) pack_snorm_1x8(src0.y)) << 8;
dst.x |= ((uint32_t) pack_snorm_1x8(src0.z)) << 16;
dst.x |= ((uint32_t) pack_snorm_1x8(src0.w)) << 24;


            _dst_val[0].u32 = dst.x;

}
static void
evaluate_pack_unorm_2x16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
         0,
         0,
      };

      struct uint32_vec dst;

         
dst.x = (uint32_t) pack_unorm_1x16(src0.x);
dst.x |= ((uint32_t) pack_unorm_1x16(src0.y)) << 16;


            _dst_val[0].u32 = dst.x;

}
static void
evaluate_pack_unorm_4x8(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct float32_vec src0 = {
            _src[0][0].f32,
            _src[0][1].f32,
            _src[0][2].f32,
            _src[0][3].f32,
      };

      struct uint32_vec dst;

         
dst.x = (uint32_t) pack_unorm_1x8(src0.x);
dst.x |= ((uint32_t) pack_unorm_1x8(src0.y)) << 8;
dst.x |= ((uint32_t) pack_unorm_1x8(src0.z)) << 16;
dst.x |= ((uint32_t) pack_unorm_1x8(src0.w)) << 24;


            _dst_val[0].u32 = dst.x;

}
static void
evaluate_pack_uvec2_to_uint(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct uint32_vec src0 = {
            _src[0][0].u32,
            _src[0][1].u32,
         0,
         0,
      };

      struct uint32_vec dst;

         
dst.x = (src0.x & 0xffff) | (src0.y << 16);


            _dst_val[0].u32 = dst.x;

}
static void
evaluate_pack_uvec4_to_uint(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct uint32_vec src0 = {
            _src[0][0].u32,
            _src[0][1].u32,
            _src[0][2].u32,
            _src[0][3].u32,
      };

      struct uint32_vec dst;

         
dst.x = (src0.x <<  0) |
        (src0.y <<  8) |
        (src0.z << 16) |
        (src0.w << 24);


            _dst_val[0].u32 = dst.x;

}
static void
evaluate_seq(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            float32_t dst = (src0 == src1) ? 1.0f : 0.0f;

            _dst_val[_i].f32 = dst;
      }

}
static void
evaluate_sge(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float src0 =
                  _mesa_half_to_float(_src[0][_i].u16);
               const float src1 =
                  _mesa_half_to_float(_src[1][_i].u16);

            float16_t dst = (src0 >= src1) ? 1.0f : 0.0f;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            float32_t dst = (src0 >= src1) ? 1.0f : 0.0f;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float64_t src0 =
                  _src[0][_i].f64;
               const float64_t src1 =
                  _src[1][_i].f64;

            float64_t dst = (src0 >= src1) ? 1.0f : 0.0f;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_slt(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            float32_t dst = (src0 < src1) ? 1.0f : 0.0f;

            _dst_val[_i].f32 = dst;
      }

}
static void
evaluate_sne(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const float32_t src0 =
                  _src[0][_i].f32;
               const float32_t src1 =
                  _src[1][_i].f32;

            float32_t dst = (src0 != src1) ? 1.0f : 0.0f;

            _dst_val[_i].f32 = dst;
      }

}
static void
evaluate_u2f16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;

            float16_t dst = src0;

            _dst_val[_i].u16 = _mesa_float_to_half(dst);
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_u2f32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;

            float32_t dst = src0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;

            float32_t dst = src0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;

            float32_t dst = src0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;

            float32_t dst = src0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;

            float32_t dst = src0;

            _dst_val[_i].f32 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_u2f64(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;

            float64_t dst = src0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;

            float64_t dst = src0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;

            float64_t dst = src0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;

            float64_t dst = src0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;

            float64_t dst = src0;

            _dst_val[_i].f64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_u2u1(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;

            uint1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;

            uint1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;

            uint1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;

            uint1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;

            uint1_t dst = src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_u2u16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;

            uint16_t dst = src0;

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;

            uint16_t dst = src0;

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;

            uint16_t dst = src0;

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;

            uint16_t dst = src0;

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;

            uint16_t dst = src0;

            _dst_val[_i].u16 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_u2u32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;

            uint32_t dst = src0;

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;

            uint32_t dst = src0;

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;

            uint32_t dst = src0;

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;

            uint32_t dst = src0;

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;

            uint32_t dst = src0;

            _dst_val[_i].u32 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_u2u64(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;

            uint64_t dst = src0;

            _dst_val[_i].u64 = dst;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;

            uint64_t dst = src0;

            _dst_val[_i].u64 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;

            uint64_t dst = src0;

            _dst_val[_i].u64 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;

            uint64_t dst = src0;

            _dst_val[_i].u64 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;

            uint64_t dst = src0;

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_u2u8(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;

            uint8_t dst = src0;

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;

            uint8_t dst = src0;

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;

            uint8_t dst = src0;

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;

            uint8_t dst = src0;

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;

            uint8_t dst = src0;

            _dst_val[_i].u8 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_uadd_carry(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            uint1_t dst = src0 + src1 < src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            uint8_t dst = src0 + src1 < src0;

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            uint16_t dst = src0 + src1 < src0;

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint32_t dst = src0 + src1 < src0;

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            uint64_t dst = src0 + src1 < src0;

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_uadd_sat(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            uint1_t dst = (src0 + src1) < src0 ? MAX_UINT_FOR_SIZE(sizeof(src0) * 8) : (src0 + src1);

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            uint8_t dst = (src0 + src1) < src0 ? MAX_UINT_FOR_SIZE(sizeof(src0) * 8) : (src0 + src1);

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            uint16_t dst = (src0 + src1) < src0 ? MAX_UINT_FOR_SIZE(sizeof(src0) * 8) : (src0 + src1);

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint32_t dst = (src0 + src1) < src0 ? MAX_UINT_FOR_SIZE(sizeof(src0) * 8) : (src0 + src1);

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            uint64_t dst = (src0 + src1) < src0 ? MAX_UINT_FOR_SIZE(sizeof(src0) * 8) : (src0 + src1);

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ubfe(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const int32_t src1 =
                  _src[1][_i].i32;
               const int32_t src2 =
                  _src[2][_i].i32;

            uint32_t dst;

            
unsigned base = src0;
int offset = src1, bits = src2;
if (bits == 0) {
   dst = 0;
} else if (bits < 0 || offset < 0) {
   dst = 0; /* undefined */
} else if (offset + bits < 32) {
   dst = (base << (32 - bits - offset)) >> (32 - bits);
} else {
   dst = base >> offset;
}


            _dst_val[_i].u32 = dst;
      }

}
static void
evaluate_ubitfield_extract(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const int32_t src1 =
                  _src[1][_i].i32;
               const int32_t src2 =
                  _src[2][_i].i32;

            uint32_t dst;

            
unsigned base = src0;
int offset = src1, bits = src2;
if (bits == 0) {
   dst = 0;
} else if (bits < 0 || offset < 0 || offset + bits > 32) {
   dst = 0; /* undefined per the spec */
} else {
   dst = (base >> offset) & ((1ull << bits) - 1);
}


            _dst_val[_i].u32 = dst;
      }

}
static void
evaluate_udiv(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            uint1_t dst = src1 == 0 ? 0 : (src0 / src1);

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            uint8_t dst = src1 == 0 ? 0 : (src0 / src1);

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            uint16_t dst = src1 == 0 ? 0 : (src0 / src1);

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint32_t dst = src1 == 0 ? 0 : (src0 / src1);

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            uint64_t dst = src1 == 0 ? 0 : (src0 / src1);

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ufind_msb(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;

            int32_t dst;

            
dst = -1;
for (int bit = bit_size - 1; bit >= 0; bit--) {
   if ((src0 >> bit) & 1) {
      dst = bit;
      break;
   }
}


            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 8: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;

            int32_t dst;

            
dst = -1;
for (int bit = bit_size - 1; bit >= 0; bit--) {
   if ((src0 >> bit) & 1) {
      dst = bit;
      break;
   }
}


            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 16: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;

            int32_t dst;

            
dst = -1;
for (int bit = bit_size - 1; bit >= 0; bit--) {
   if ((src0 >> bit) & 1) {
      dst = bit;
      break;
   }
}


            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 32: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;

            int32_t dst;

            
dst = -1;
for (int bit = bit_size - 1; bit >= 0; bit--) {
   if ((src0 >> bit) & 1) {
      dst = bit;
      break;
   }
}


            _dst_val[_i].i32 = dst;
      }

         break;
      }
      case 64: {
         
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;

            int32_t dst;

            
dst = -1;
for (int bit = bit_size - 1; bit >= 0; bit--) {
   if ((src0 >> bit) & 1) {
      dst = bit;
      break;
   }
}


            _dst_val[_i].i32 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_uge(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            bool1_t dst = src0 >= src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            bool1_t dst = src0 >= src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            bool1_t dst = src0 >= src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            bool1_t dst = src0 >= src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            bool1_t dst = src0 >= src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_uge32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            bool32_t dst = src0 >= src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            bool32_t dst = src0 >= src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            bool32_t dst = src0 >= src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            bool32_t dst = src0 >= src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            bool32_t dst = src0 >= src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_uhadd(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            uint1_t dst = (src0 & src1) + ((src0 ^ src1) >> 1);

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            uint8_t dst = (src0 & src1) + ((src0 ^ src1) >> 1);

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            uint16_t dst = (src0 & src1) + ((src0 ^ src1) >> 1);

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint32_t dst = (src0 & src1) + ((src0 ^ src1) >> 1);

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            uint64_t dst = (src0 & src1) + ((src0 ^ src1) >> 1);

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ult(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            bool1_t dst = src0 < src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            bool1_t dst = src0 < src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            bool1_t dst = src0 < src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            bool1_t dst = src0 < src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            bool1_t dst = src0 < src1;

            _dst_val[_i].b = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ult32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            bool32_t dst = src0 < src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            bool32_t dst = src0 < src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            bool32_t dst = src0 < src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            bool32_t dst = src0 < src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            bool32_t dst = src0 < src1;

            _dst_val[_i].i32 = -(int)dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_umax(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            uint1_t dst = src1 > src0 ? src1 : src0;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            uint8_t dst = src1 > src0 ? src1 : src0;

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            uint16_t dst = src1 > src0 ? src1 : src0;

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint32_t dst = src1 > src0 ? src1 : src0;

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            uint64_t dst = src1 > src0 ? src1 : src0;

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_umax3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;
               const uint1_t src2 =
                  _src[2][_i].b;

            uint1_t dst = MAX2(src0, MAX2(src1, src2));

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;
               const uint8_t src2 =
                  _src[2][_i].u8;

            uint8_t dst = MAX2(src0, MAX2(src1, src2));

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;
               const uint16_t src2 =
                  _src[2][_i].u16;

            uint16_t dst = MAX2(src0, MAX2(src1, src2));

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;
               const uint32_t src2 =
                  _src[2][_i].u32;

            uint32_t dst = MAX2(src0, MAX2(src1, src2));

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;
               const uint64_t src2 =
                  _src[2][_i].u64;

            uint64_t dst = MAX2(src0, MAX2(src1, src2));

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_umax_4x8(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst;

            
dst = 0;
for (int i = 0; i < 32; i += 8) {
   dst |= MAX2((src0 >> i) & 0xff, (src1 >> i) & 0xff) << i;
}


            _dst_val[_i].i32 = dst;
      }

}
static void
evaluate_umed3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;
               const uint1_t src2 =
                  _src[2][_i].b;

            uint1_t dst = MAX2(MIN2(MAX2(src0, src1), src2), MIN2(src0, src1));

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;
               const uint8_t src2 =
                  _src[2][_i].u8;

            uint8_t dst = MAX2(MIN2(MAX2(src0, src1), src2), MIN2(src0, src1));

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;
               const uint16_t src2 =
                  _src[2][_i].u16;

            uint16_t dst = MAX2(MIN2(MAX2(src0, src1), src2), MIN2(src0, src1));

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;
               const uint32_t src2 =
                  _src[2][_i].u32;

            uint32_t dst = MAX2(MIN2(MAX2(src0, src1), src2), MIN2(src0, src1));

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;
               const uint64_t src2 =
                  _src[2][_i].u64;

            uint64_t dst = MAX2(MIN2(MAX2(src0, src1), src2), MIN2(src0, src1));

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_umin(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            uint1_t dst = src1 > src0 ? src0 : src1;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            uint8_t dst = src1 > src0 ? src0 : src1;

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            uint16_t dst = src1 > src0 ? src0 : src1;

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint32_t dst = src1 > src0 ? src0 : src1;

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            uint64_t dst = src1 > src0 ? src0 : src1;

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_umin3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;
               const uint1_t src2 =
                  _src[2][_i].b;

            uint1_t dst = MIN2(src0, MIN2(src1, src2));

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;
               const uint8_t src2 =
                  _src[2][_i].u8;

            uint8_t dst = MIN2(src0, MIN2(src1, src2));

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;
               const uint16_t src2 =
                  _src[2][_i].u16;

            uint16_t dst = MIN2(src0, MIN2(src1, src2));

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;
               const uint32_t src2 =
                  _src[2][_i].u32;

            uint32_t dst = MIN2(src0, MIN2(src1, src2));

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                           
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;
               const uint64_t src2 =
                  _src[2][_i].u64;

            uint64_t dst = MIN2(src0, MIN2(src1, src2));

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_umin_4x8(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst;

            
dst = 0;
for (int i = 0; i < 32; i += 8) {
   dst |= MIN2((src0 >> i) & 0xff, (src1 >> i) & 0xff) << i;
}


            _dst_val[_i].i32 = dst;
      }

}
static void
evaluate_umod(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            uint1_t dst = src1 == 0 ? 0 : src0 % src1;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            uint8_t dst = src1 == 0 ? 0 : src0 % src1;

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            uint16_t dst = src1 == 0 ? 0 : src0 % src1;

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint32_t dst = src1 == 0 ? 0 : src0 % src1;

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            uint64_t dst = src1 == 0 ? 0 : src0 % src1;

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_umul_2x32_64(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint64_t dst = (uint64_t)src0 * (uint64_t)src1;

            _dst_val[_i].u64 = dst;
      }

}
static void
evaluate_umul_high(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            uint1_t dst;

            
if (bit_size == 64) {
   /* The casts are kind-of annoying but needed to prevent compiler warnings. */
   uint32_t src0_u32[2] = { src0, (uint64_t)src0 >> 32 };
   uint32_t src1_u32[2] = { src1, (uint64_t)src1 >> 32 };
   uint32_t prod_u32[4];
   ubm_mul_u32arr(prod_u32, src0_u32, src1_u32);
   dst = (uint64_t)prod_u32[2] | ((uint64_t)prod_u32[3] << 32);
} else {
   dst = ((uint64_t)src0 * (uint64_t)src1) >> bit_size;
}


            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            uint8_t dst;

            
if (bit_size == 64) {
   /* The casts are kind-of annoying but needed to prevent compiler warnings. */
   uint32_t src0_u32[2] = { src0, (uint64_t)src0 >> 32 };
   uint32_t src1_u32[2] = { src1, (uint64_t)src1 >> 32 };
   uint32_t prod_u32[4];
   ubm_mul_u32arr(prod_u32, src0_u32, src1_u32);
   dst = (uint64_t)prod_u32[2] | ((uint64_t)prod_u32[3] << 32);
} else {
   dst = ((uint64_t)src0 * (uint64_t)src1) >> bit_size;
}


            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            uint16_t dst;

            
if (bit_size == 64) {
   /* The casts are kind-of annoying but needed to prevent compiler warnings. */
   uint32_t src0_u32[2] = { src0, (uint64_t)src0 >> 32 };
   uint32_t src1_u32[2] = { src1, (uint64_t)src1 >> 32 };
   uint32_t prod_u32[4];
   ubm_mul_u32arr(prod_u32, src0_u32, src1_u32);
   dst = (uint64_t)prod_u32[2] | ((uint64_t)prod_u32[3] << 32);
} else {
   dst = ((uint64_t)src0 * (uint64_t)src1) >> bit_size;
}


            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint32_t dst;

            
if (bit_size == 64) {
   /* The casts are kind-of annoying but needed to prevent compiler warnings. */
   uint32_t src0_u32[2] = { src0, (uint64_t)src0 >> 32 };
   uint32_t src1_u32[2] = { src1, (uint64_t)src1 >> 32 };
   uint32_t prod_u32[4];
   ubm_mul_u32arr(prod_u32, src0_u32, src1_u32);
   dst = (uint64_t)prod_u32[2] | ((uint64_t)prod_u32[3] << 32);
} else {
   dst = ((uint64_t)src0 * (uint64_t)src1) >> bit_size;
}


            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            uint64_t dst;

            
if (bit_size == 64) {
   /* The casts are kind-of annoying but needed to prevent compiler warnings. */
   uint32_t src0_u32[2] = { src0, (uint64_t)src0 >> 32 };
   uint32_t src1_u32[2] = { src1, (uint64_t)src1 >> 32 };
   uint32_t prod_u32[4];
   ubm_mul_u32arr(prod_u32, src0_u32, src1_u32);
   dst = (uint64_t)prod_u32[2] | ((uint64_t)prod_u32[3] << 32);
} else {
   dst = ((uint64_t)src0 * (uint64_t)src1) >> bit_size;
}


            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_umul_unorm_4x8(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst;

            
dst = 0;
for (int i = 0; i < 32; i += 8) {
   int src0_chan = (src0 >> i) & 0xff;
   int src1_chan = (src1 >> i) & 0xff;
   dst |= ((src0_chan * src1_chan) / 255) << i;
}


            _dst_val[_i].i32 = dst;
      }

}
static void
evaluate_unpack_32_2x16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct uint32_vec src0 = {
            _src[0][0].u32,
         0,
         0,
         0,
      };

      struct uint16_vec dst;

         dst.x = src0.x; dst.y = src0.x >> 16;

            _dst_val[0].u16 = dst.x;
            _dst_val[1].u16 = dst.y;

}
static void
evaluate_unpack_32_2x16_split_x(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;

            uint16_t dst = src0;

            _dst_val[_i].u16 = dst;
      }

}
static void
evaluate_unpack_32_2x16_split_y(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;

            uint16_t dst = src0 >> 16;

            _dst_val[_i].u16 = dst;
      }

}
static void
evaluate_unpack_64_2x32(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct uint64_vec src0 = {
            _src[0][0].u64,
         0,
         0,
         0,
      };

      struct uint32_vec dst;

         dst.x = src0.x; dst.y = src0.x >> 32;

            _dst_val[0].u32 = dst.x;
            _dst_val[1].u32 = dst.y;

}
static void
evaluate_unpack_64_2x32_split_x(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;

            uint32_t dst = src0;

            _dst_val[_i].u32 = dst;
      }

}
static void
evaluate_unpack_64_2x32_split_y(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;

            uint32_t dst = src0 >> 32;

            _dst_val[_i].u32 = dst;
      }

}
static void
evaluate_unpack_64_4x16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct uint64_vec src0 = {
            _src[0][0].u64,
         0,
         0,
         0,
      };

      struct uint16_vec dst;

         dst.x = src0.x; dst.y = src0.x >> 16; dst.z = src0.x >> 32; dst.w = src0.w >> 48;

            _dst_val[0].u16 = dst.x;
            _dst_val[1].u16 = dst.y;
            _dst_val[2].u16 = dst.z;
            _dst_val[3].u16 = dst.w;

}
static void
evaluate_unpack_half_2x16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct uint32_vec src0 = {
            _src[0][0].u32,
         0,
         0,
         0,
      };

      struct float32_vec dst;

         
dst.x = unpack_half_1x16((uint16_t)(src0.x & 0xffff));
dst.y = unpack_half_1x16((uint16_t)(src0.x << 16));


            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;

}
static void
evaluate_unpack_half_2x16_split_x(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;

            float32_t dst = unpack_half_1x16((uint16_t)(src0 & 0xffff));

            _dst_val[_i].f32 = dst;
      }

}
static void
evaluate_unpack_half_2x16_split_y(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

         
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;

            float32_t dst = unpack_half_1x16((uint16_t)(src0 >> 16));

            _dst_val[_i].f32 = dst;
      }

}
static void
evaluate_unpack_snorm_2x16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct uint32_vec src0 = {
            _src[0][0].u32,
         0,
         0,
         0,
      };

      struct float32_vec dst;

         
dst.x = unpack_snorm_1x16((uint16_t)(src0.x & 0xffff));
dst.y = unpack_snorm_1x16((uint16_t)(src0.x << 16));


            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;

}
static void
evaluate_unpack_snorm_4x8(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct uint32_vec src0 = {
            _src[0][0].u32,
         0,
         0,
         0,
      };

      struct float32_vec dst;

         
dst.x = unpack_snorm_1x8((uint8_t)(src0.x & 0xff));
dst.y = unpack_snorm_1x8((uint8_t)((src0.x >> 8) & 0xff));
dst.z = unpack_snorm_1x8((uint8_t)((src0.x >> 16) & 0xff));
dst.w = unpack_snorm_1x8((uint8_t)(src0.x >> 24));


            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;
            _dst_val[2].f32 = dst.z;
            _dst_val[3].f32 = dst.w;

}
static void
evaluate_unpack_unorm_2x16(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct uint32_vec src0 = {
            _src[0][0].u32,
         0,
         0,
         0,
      };

      struct float32_vec dst;

         
dst.x = unpack_unorm_1x16((uint16_t)(src0.x & 0xffff));
dst.y = unpack_unorm_1x16((uint16_t)(src0.x << 16));


            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;

}
static void
evaluate_unpack_unorm_4x8(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   


      const struct uint32_vec src0 = {
            _src[0][0].u32,
         0,
         0,
         0,
      };

      struct float32_vec dst;

         
dst.x = unpack_unorm_1x8((uint8_t)(src0.x & 0xff));
dst.y = unpack_unorm_1x8((uint8_t)((src0.x >> 8) & 0xff));
dst.z = unpack_unorm_1x8((uint8_t)((src0.x >> 16) & 0xff));
dst.w = unpack_unorm_1x8((uint8_t)(src0.x >> 24));


            _dst_val[0].f32 = dst.x;
            _dst_val[1].f32 = dst.y;
            _dst_val[2].f32 = dst.z;
            _dst_val[3].f32 = dst.w;

}
static void
evaluate_urhadd(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            uint1_t dst = (src0 | src1) + ((src0 ^ src1) >> 1);

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            uint8_t dst = (src0 | src1) + ((src0 ^ src1) >> 1);

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            uint16_t dst = (src0 | src1) + ((src0 ^ src1) >> 1);

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint32_t dst = (src0 | src1) + ((src0 ^ src1) >> 1);

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            uint64_t dst = (src0 | src1) + ((src0 ^ src1) >> 1);

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_usadd_4x8(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst;

            
dst = 0;
for (int i = 0; i < 32; i += 8) {
   dst |= MIN2(((src0 >> i) & 0xff) + ((src1 >> i) & 0xff), 0xff) << i;
}


            _dst_val[_i].i32 = dst;
      }

}
static void
evaluate_ushr(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint1_t dst = src0 >> (src1 & (sizeof(src0) * 8 - 1));

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint8_t dst = src0 >> (src1 & (sizeof(src0) * 8 - 1));

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint16_t dst = src0 >> (src1 & (sizeof(src0) * 8 - 1));

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint32_t dst = src0 >> (src1 & (sizeof(src0) * 8 - 1));

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint64_t dst = src0 >> (src1 & (sizeof(src0) * 8 - 1));

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_ussub_4x8(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                 UNUSED unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const int32_t src0 =
                  _src[0][_i].i32;
               const int32_t src1 =
                  _src[1][_i].i32;

            int32_t dst;

            
dst = 0;
for (int i = 0; i < 32; i += 8) {
   int src0_chan = (src0 >> i) & 0xff;
   int src1_chan = (src1 >> i) & 0xff;
   if (src0_chan > src1_chan)
      dst |= (src0_chan - src1_chan) << i;
}


            _dst_val[_i].i32 = dst;
      }

}
static void
evaluate_usub_borrow(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            uint1_t dst = src0 < src1;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            uint8_t dst = src0 < src1;

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            uint16_t dst = src0 < src1;

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint32_t dst = src0 < src1;

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            uint64_t dst = src0 < src1;

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_usub_sat(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint1_t src0 =
                  _src[0][_i].b;
               const uint1_t src1 =
                  _src[1][_i].b;

            uint1_t dst = src0 < src1 ? 0 : src0 - src1;

            /* 1-bit integers get truncated */
            _dst_val[_i].b = dst & 1;
      }

         break;
      }
      case 8: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint8_t src0 =
                  _src[0][_i].u8;
               const uint8_t src1 =
                  _src[1][_i].u8;

            uint8_t dst = src0 < src1 ? 0 : src0 - src1;

            _dst_val[_i].u8 = dst;
      }

         break;
      }
      case 16: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint16_t src0 =
                  _src[0][_i].u16;
               const uint16_t src1 =
                  _src[1][_i].u16;

            uint16_t dst = src0 < src1 ? 0 : src0 - src1;

            _dst_val[_i].u16 = dst;
      }

         break;
      }
      case 32: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint32_t src0 =
                  _src[0][_i].u32;
               const uint32_t src1 =
                  _src[1][_i].u32;

            uint32_t dst = src0 < src1 ? 0 : src0 - src1;

            _dst_val[_i].u32 = dst;
      }

         break;
      }
      case 64: {
         
   

                  
      for (unsigned _i = 0; _i < num_components; _i++) {
               const uint64_t src0 =
                  _src[0][_i].u64;
               const uint64_t src1 =
                  _src[1][_i].u64;

            uint64_t dst = src0 < src1 ? 0 : src0 - src1;

            _dst_val[_i].u64 = dst;
      }

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_vec2(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   


      const struct uint1_vec src0 = {
            _src[0][0].b,
         0,
         0,
         0,
      };

      const struct uint1_vec src1 = {
            _src[1][0].b,
         0,
         0,
         0,
      };

      struct uint1_vec dst;

         
dst.x = src0.x;
dst.y = src1.x;


            /* 1-bit integers get truncated */
            _dst_val[0].b = dst.x & 1;
            /* 1-bit integers get truncated */
            _dst_val[1].b = dst.y & 1;

         break;
      }
      case 8: {
         
   


      const struct uint8_vec src0 = {
            _src[0][0].u8,
         0,
         0,
         0,
      };

      const struct uint8_vec src1 = {
            _src[1][0].u8,
         0,
         0,
         0,
      };

      struct uint8_vec dst;

         
dst.x = src0.x;
dst.y = src1.x;


            _dst_val[0].u8 = dst.x;
            _dst_val[1].u8 = dst.y;

         break;
      }
      case 16: {
         
   


      const struct uint16_vec src0 = {
            _src[0][0].u16,
         0,
         0,
         0,
      };

      const struct uint16_vec src1 = {
            _src[1][0].u16,
         0,
         0,
         0,
      };

      struct uint16_vec dst;

         
dst.x = src0.x;
dst.y = src1.x;


            _dst_val[0].u16 = dst.x;
            _dst_val[1].u16 = dst.y;

         break;
      }
      case 32: {
         
   


      const struct uint32_vec src0 = {
            _src[0][0].u32,
         0,
         0,
         0,
      };

      const struct uint32_vec src1 = {
            _src[1][0].u32,
         0,
         0,
         0,
      };

      struct uint32_vec dst;

         
dst.x = src0.x;
dst.y = src1.x;


            _dst_val[0].u32 = dst.x;
            _dst_val[1].u32 = dst.y;

         break;
      }
      case 64: {
         
   


      const struct uint64_vec src0 = {
            _src[0][0].u64,
         0,
         0,
         0,
      };

      const struct uint64_vec src1 = {
            _src[1][0].u64,
         0,
         0,
         0,
      };

      struct uint64_vec dst;

         
dst.x = src0.x;
dst.y = src1.x;


            _dst_val[0].u64 = dst.x;
            _dst_val[1].u64 = dst.y;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_vec3(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   


      const struct uint1_vec src0 = {
            _src[0][0].b,
         0,
         0,
         0,
      };

      const struct uint1_vec src1 = {
            _src[1][0].b,
         0,
         0,
         0,
      };

      const struct uint1_vec src2 = {
            _src[2][0].b,
         0,
         0,
         0,
      };

      struct uint1_vec dst;

         
dst.x = src0.x;
dst.y = src1.x;
dst.z = src2.x;


            /* 1-bit integers get truncated */
            _dst_val[0].b = dst.x & 1;
            /* 1-bit integers get truncated */
            _dst_val[1].b = dst.y & 1;
            /* 1-bit integers get truncated */
            _dst_val[2].b = dst.z & 1;

         break;
      }
      case 8: {
         
   


      const struct uint8_vec src0 = {
            _src[0][0].u8,
         0,
         0,
         0,
      };

      const struct uint8_vec src1 = {
            _src[1][0].u8,
         0,
         0,
         0,
      };

      const struct uint8_vec src2 = {
            _src[2][0].u8,
         0,
         0,
         0,
      };

      struct uint8_vec dst;

         
dst.x = src0.x;
dst.y = src1.x;
dst.z = src2.x;


            _dst_val[0].u8 = dst.x;
            _dst_val[1].u8 = dst.y;
            _dst_val[2].u8 = dst.z;

         break;
      }
      case 16: {
         
   


      const struct uint16_vec src0 = {
            _src[0][0].u16,
         0,
         0,
         0,
      };

      const struct uint16_vec src1 = {
            _src[1][0].u16,
         0,
         0,
         0,
      };

      const struct uint16_vec src2 = {
            _src[2][0].u16,
         0,
         0,
         0,
      };

      struct uint16_vec dst;

         
dst.x = src0.x;
dst.y = src1.x;
dst.z = src2.x;


            _dst_val[0].u16 = dst.x;
            _dst_val[1].u16 = dst.y;
            _dst_val[2].u16 = dst.z;

         break;
      }
      case 32: {
         
   


      const struct uint32_vec src0 = {
            _src[0][0].u32,
         0,
         0,
         0,
      };

      const struct uint32_vec src1 = {
            _src[1][0].u32,
         0,
         0,
         0,
      };

      const struct uint32_vec src2 = {
            _src[2][0].u32,
         0,
         0,
         0,
      };

      struct uint32_vec dst;

         
dst.x = src0.x;
dst.y = src1.x;
dst.z = src2.x;


            _dst_val[0].u32 = dst.x;
            _dst_val[1].u32 = dst.y;
            _dst_val[2].u32 = dst.z;

         break;
      }
      case 64: {
         
   


      const struct uint64_vec src0 = {
            _src[0][0].u64,
         0,
         0,
         0,
      };

      const struct uint64_vec src1 = {
            _src[1][0].u64,
         0,
         0,
         0,
      };

      const struct uint64_vec src2 = {
            _src[2][0].u64,
         0,
         0,
         0,
      };

      struct uint64_vec dst;

         
dst.x = src0.x;
dst.y = src1.x;
dst.z = src2.x;


            _dst_val[0].u64 = dst.x;
            _dst_val[1].u64 = dst.y;
            _dst_val[2].u64 = dst.z;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}
static void
evaluate_vec4(nir_const_value *_dst_val,
                 MAYBE_UNUSED unsigned num_components,
                  unsigned bit_size,
                 MAYBE_UNUSED nir_const_value **_src)
{
      switch (bit_size) {
      case 1: {
         
   


      const struct uint1_vec src0 = {
            _src[0][0].b,
         0,
         0,
         0,
      };

      const struct uint1_vec src1 = {
            _src[1][0].b,
         0,
         0,
         0,
      };

      const struct uint1_vec src2 = {
            _src[2][0].b,
         0,
         0,
         0,
      };

      const struct uint1_vec src3 = {
            _src[3][0].b,
         0,
         0,
         0,
      };

      struct uint1_vec dst;

         
dst.x = src0.x;
dst.y = src1.x;
dst.z = src2.x;
dst.w = src3.x;


            /* 1-bit integers get truncated */
            _dst_val[0].b = dst.x & 1;
            /* 1-bit integers get truncated */
            _dst_val[1].b = dst.y & 1;
            /* 1-bit integers get truncated */
            _dst_val[2].b = dst.z & 1;
            /* 1-bit integers get truncated */
            _dst_val[3].b = dst.w & 1;

         break;
      }
      case 8: {
         
   


      const struct uint8_vec src0 = {
            _src[0][0].u8,
         0,
         0,
         0,
      };

      const struct uint8_vec src1 = {
            _src[1][0].u8,
         0,
         0,
         0,
      };

      const struct uint8_vec src2 = {
            _src[2][0].u8,
         0,
         0,
         0,
      };

      const struct uint8_vec src3 = {
            _src[3][0].u8,
         0,
         0,
         0,
      };

      struct uint8_vec dst;

         
dst.x = src0.x;
dst.y = src1.x;
dst.z = src2.x;
dst.w = src3.x;


            _dst_val[0].u8 = dst.x;
            _dst_val[1].u8 = dst.y;
            _dst_val[2].u8 = dst.z;
            _dst_val[3].u8 = dst.w;

         break;
      }
      case 16: {
         
   


      const struct uint16_vec src0 = {
            _src[0][0].u16,
         0,
         0,
         0,
      };

      const struct uint16_vec src1 = {
            _src[1][0].u16,
         0,
         0,
         0,
      };

      const struct uint16_vec src2 = {
            _src[2][0].u16,
         0,
         0,
         0,
      };

      const struct uint16_vec src3 = {
            _src[3][0].u16,
         0,
         0,
         0,
      };

      struct uint16_vec dst;

         
dst.x = src0.x;
dst.y = src1.x;
dst.z = src2.x;
dst.w = src3.x;


            _dst_val[0].u16 = dst.x;
            _dst_val[1].u16 = dst.y;
            _dst_val[2].u16 = dst.z;
            _dst_val[3].u16 = dst.w;

         break;
      }
      case 32: {
         
   


      const struct uint32_vec src0 = {
            _src[0][0].u32,
         0,
         0,
         0,
      };

      const struct uint32_vec src1 = {
            _src[1][0].u32,
         0,
         0,
         0,
      };

      const struct uint32_vec src2 = {
            _src[2][0].u32,
         0,
         0,
         0,
      };

      const struct uint32_vec src3 = {
            _src[3][0].u32,
         0,
         0,
         0,
      };

      struct uint32_vec dst;

         
dst.x = src0.x;
dst.y = src1.x;
dst.z = src2.x;
dst.w = src3.x;


            _dst_val[0].u32 = dst.x;
            _dst_val[1].u32 = dst.y;
            _dst_val[2].u32 = dst.z;
            _dst_val[3].u32 = dst.w;

         break;
      }
      case 64: {
         
   


      const struct uint64_vec src0 = {
            _src[0][0].u64,
         0,
         0,
         0,
      };

      const struct uint64_vec src1 = {
            _src[1][0].u64,
         0,
         0,
         0,
      };

      const struct uint64_vec src2 = {
            _src[2][0].u64,
         0,
         0,
         0,
      };

      const struct uint64_vec src3 = {
            _src[3][0].u64,
         0,
         0,
         0,
      };

      struct uint64_vec dst;

         
dst.x = src0.x;
dst.y = src1.x;
dst.z = src2.x;
dst.w = src3.x;


            _dst_val[0].u64 = dst.x;
            _dst_val[1].u64 = dst.y;
            _dst_val[2].u64 = dst.z;
            _dst_val[3].u64 = dst.w;

         break;
      }

      default:
         unreachable("unknown bit width");
      }
}

void
nir_eval_const_opcode(nir_op op, nir_const_value *dest,
                      unsigned num_components, unsigned bit_width,
                      nir_const_value **src)
{
   switch (op) {
   case nir_op_b2f16:
      return evaluate_b2f16(dest, num_components, bit_width, src);
   case nir_op_b2f32:
      return evaluate_b2f32(dest, num_components, bit_width, src);
   case nir_op_b2f64:
      return evaluate_b2f64(dest, num_components, bit_width, src);
   case nir_op_b2i1:
      return evaluate_b2i1(dest, num_components, bit_width, src);
   case nir_op_b2i16:
      return evaluate_b2i16(dest, num_components, bit_width, src);
   case nir_op_b2i32:
      return evaluate_b2i32(dest, num_components, bit_width, src);
   case nir_op_b2i64:
      return evaluate_b2i64(dest, num_components, bit_width, src);
   case nir_op_b2i8:
      return evaluate_b2i8(dest, num_components, bit_width, src);
   case nir_op_b32all_fequal2:
      return evaluate_b32all_fequal2(dest, num_components, bit_width, src);
   case nir_op_b32all_fequal3:
      return evaluate_b32all_fequal3(dest, num_components, bit_width, src);
   case nir_op_b32all_fequal4:
      return evaluate_b32all_fequal4(dest, num_components, bit_width, src);
   case nir_op_b32all_iequal2:
      return evaluate_b32all_iequal2(dest, num_components, bit_width, src);
   case nir_op_b32all_iequal3:
      return evaluate_b32all_iequal3(dest, num_components, bit_width, src);
   case nir_op_b32all_iequal4:
      return evaluate_b32all_iequal4(dest, num_components, bit_width, src);
   case nir_op_b32any_fnequal2:
      return evaluate_b32any_fnequal2(dest, num_components, bit_width, src);
   case nir_op_b32any_fnequal3:
      return evaluate_b32any_fnequal3(dest, num_components, bit_width, src);
   case nir_op_b32any_fnequal4:
      return evaluate_b32any_fnequal4(dest, num_components, bit_width, src);
   case nir_op_b32any_inequal2:
      return evaluate_b32any_inequal2(dest, num_components, bit_width, src);
   case nir_op_b32any_inequal3:
      return evaluate_b32any_inequal3(dest, num_components, bit_width, src);
   case nir_op_b32any_inequal4:
      return evaluate_b32any_inequal4(dest, num_components, bit_width, src);
   case nir_op_b32csel:
      return evaluate_b32csel(dest, num_components, bit_width, src);
   case nir_op_ball_fequal2:
      return evaluate_ball_fequal2(dest, num_components, bit_width, src);
   case nir_op_ball_fequal3:
      return evaluate_ball_fequal3(dest, num_components, bit_width, src);
   case nir_op_ball_fequal4:
      return evaluate_ball_fequal4(dest, num_components, bit_width, src);
   case nir_op_ball_iequal2:
      return evaluate_ball_iequal2(dest, num_components, bit_width, src);
   case nir_op_ball_iequal3:
      return evaluate_ball_iequal3(dest, num_components, bit_width, src);
   case nir_op_ball_iequal4:
      return evaluate_ball_iequal4(dest, num_components, bit_width, src);
   case nir_op_bany_fnequal2:
      return evaluate_bany_fnequal2(dest, num_components, bit_width, src);
   case nir_op_bany_fnequal3:
      return evaluate_bany_fnequal3(dest, num_components, bit_width, src);
   case nir_op_bany_fnequal4:
      return evaluate_bany_fnequal4(dest, num_components, bit_width, src);
   case nir_op_bany_inequal2:
      return evaluate_bany_inequal2(dest, num_components, bit_width, src);
   case nir_op_bany_inequal3:
      return evaluate_bany_inequal3(dest, num_components, bit_width, src);
   case nir_op_bany_inequal4:
      return evaluate_bany_inequal4(dest, num_components, bit_width, src);
   case nir_op_bcsel:
      return evaluate_bcsel(dest, num_components, bit_width, src);
   case nir_op_bfi:
      return evaluate_bfi(dest, num_components, bit_width, src);
   case nir_op_bfm:
      return evaluate_bfm(dest, num_components, bit_width, src);
   case nir_op_bit_count:
      return evaluate_bit_count(dest, num_components, bit_width, src);
   case nir_op_bitfield_insert:
      return evaluate_bitfield_insert(dest, num_components, bit_width, src);
   case nir_op_bitfield_reverse:
      return evaluate_bitfield_reverse(dest, num_components, bit_width, src);
   case nir_op_cube_face_coord:
      return evaluate_cube_face_coord(dest, num_components, bit_width, src);
   case nir_op_cube_face_index:
      return evaluate_cube_face_index(dest, num_components, bit_width, src);
   case nir_op_extract_i16:
      return evaluate_extract_i16(dest, num_components, bit_width, src);
   case nir_op_extract_i8:
      return evaluate_extract_i8(dest, num_components, bit_width, src);
   case nir_op_extract_u16:
      return evaluate_extract_u16(dest, num_components, bit_width, src);
   case nir_op_extract_u8:
      return evaluate_extract_u8(dest, num_components, bit_width, src);
   case nir_op_f2b1:
      return evaluate_f2b1(dest, num_components, bit_width, src);
   case nir_op_f2b32:
      return evaluate_f2b32(dest, num_components, bit_width, src);
   case nir_op_f2f16:
      return evaluate_f2f16(dest, num_components, bit_width, src);
   case nir_op_f2f16_rtne:
      return evaluate_f2f16_rtne(dest, num_components, bit_width, src);
   case nir_op_f2f16_rtz:
      return evaluate_f2f16_rtz(dest, num_components, bit_width, src);
   case nir_op_f2f32:
      return evaluate_f2f32(dest, num_components, bit_width, src);
   case nir_op_f2f64:
      return evaluate_f2f64(dest, num_components, bit_width, src);
   case nir_op_f2i1:
      return evaluate_f2i1(dest, num_components, bit_width, src);
   case nir_op_f2i16:
      return evaluate_f2i16(dest, num_components, bit_width, src);
   case nir_op_f2i32:
      return evaluate_f2i32(dest, num_components, bit_width, src);
   case nir_op_f2i64:
      return evaluate_f2i64(dest, num_components, bit_width, src);
   case nir_op_f2i8:
      return evaluate_f2i8(dest, num_components, bit_width, src);
   case nir_op_f2u1:
      return evaluate_f2u1(dest, num_components, bit_width, src);
   case nir_op_f2u16:
      return evaluate_f2u16(dest, num_components, bit_width, src);
   case nir_op_f2u32:
      return evaluate_f2u32(dest, num_components, bit_width, src);
   case nir_op_f2u64:
      return evaluate_f2u64(dest, num_components, bit_width, src);
   case nir_op_f2u8:
      return evaluate_f2u8(dest, num_components, bit_width, src);
   case nir_op_fabs:
      return evaluate_fabs(dest, num_components, bit_width, src);
   case nir_op_fadd:
      return evaluate_fadd(dest, num_components, bit_width, src);
   case nir_op_fall_equal2:
      return evaluate_fall_equal2(dest, num_components, bit_width, src);
   case nir_op_fall_equal3:
      return evaluate_fall_equal3(dest, num_components, bit_width, src);
   case nir_op_fall_equal4:
      return evaluate_fall_equal4(dest, num_components, bit_width, src);
   case nir_op_fand:
      return evaluate_fand(dest, num_components, bit_width, src);
   case nir_op_fany_nequal2:
      return evaluate_fany_nequal2(dest, num_components, bit_width, src);
   case nir_op_fany_nequal3:
      return evaluate_fany_nequal3(dest, num_components, bit_width, src);
   case nir_op_fany_nequal4:
      return evaluate_fany_nequal4(dest, num_components, bit_width, src);
   case nir_op_fceil:
      return evaluate_fceil(dest, num_components, bit_width, src);
   case nir_op_fcos:
      return evaluate_fcos(dest, num_components, bit_width, src);
   case nir_op_fcsel:
      return evaluate_fcsel(dest, num_components, bit_width, src);
   case nir_op_fddx:
      return evaluate_fddx(dest, num_components, bit_width, src);
   case nir_op_fddx_coarse:
      return evaluate_fddx_coarse(dest, num_components, bit_width, src);
   case nir_op_fddx_fine:
      return evaluate_fddx_fine(dest, num_components, bit_width, src);
   case nir_op_fddy:
      return evaluate_fddy(dest, num_components, bit_width, src);
   case nir_op_fddy_coarse:
      return evaluate_fddy_coarse(dest, num_components, bit_width, src);
   case nir_op_fddy_fine:
      return evaluate_fddy_fine(dest, num_components, bit_width, src);
   case nir_op_fdiv:
      return evaluate_fdiv(dest, num_components, bit_width, src);
   case nir_op_fdot2:
      return evaluate_fdot2(dest, num_components, bit_width, src);
   case nir_op_fdot3:
      return evaluate_fdot3(dest, num_components, bit_width, src);
   case nir_op_fdot4:
      return evaluate_fdot4(dest, num_components, bit_width, src);
   case nir_op_fdot_replicated2:
      return evaluate_fdot_replicated2(dest, num_components, bit_width, src);
   case nir_op_fdot_replicated3:
      return evaluate_fdot_replicated3(dest, num_components, bit_width, src);
   case nir_op_fdot_replicated4:
      return evaluate_fdot_replicated4(dest, num_components, bit_width, src);
   case nir_op_fdph:
      return evaluate_fdph(dest, num_components, bit_width, src);
   case nir_op_fdph_replicated:
      return evaluate_fdph_replicated(dest, num_components, bit_width, src);
   case nir_op_feq:
      return evaluate_feq(dest, num_components, bit_width, src);
   case nir_op_feq32:
      return evaluate_feq32(dest, num_components, bit_width, src);
   case nir_op_fexp2:
      return evaluate_fexp2(dest, num_components, bit_width, src);
   case nir_op_ffloor:
      return evaluate_ffloor(dest, num_components, bit_width, src);
   case nir_op_ffma:
      return evaluate_ffma(dest, num_components, bit_width, src);
   case nir_op_ffract:
      return evaluate_ffract(dest, num_components, bit_width, src);
   case nir_op_fge:
      return evaluate_fge(dest, num_components, bit_width, src);
   case nir_op_fge32:
      return evaluate_fge32(dest, num_components, bit_width, src);
   case nir_op_find_lsb:
      return evaluate_find_lsb(dest, num_components, bit_width, src);
   case nir_op_flog2:
      return evaluate_flog2(dest, num_components, bit_width, src);
   case nir_op_flrp:
      return evaluate_flrp(dest, num_components, bit_width, src);
   case nir_op_flt:
      return evaluate_flt(dest, num_components, bit_width, src);
   case nir_op_flt32:
      return evaluate_flt32(dest, num_components, bit_width, src);
   case nir_op_fmax:
      return evaluate_fmax(dest, num_components, bit_width, src);
   case nir_op_fmax3:
      return evaluate_fmax3(dest, num_components, bit_width, src);
   case nir_op_fmed3:
      return evaluate_fmed3(dest, num_components, bit_width, src);
   case nir_op_fmin:
      return evaluate_fmin(dest, num_components, bit_width, src);
   case nir_op_fmin3:
      return evaluate_fmin3(dest, num_components, bit_width, src);
   case nir_op_fmod:
      return evaluate_fmod(dest, num_components, bit_width, src);
   case nir_op_fmov:
      return evaluate_fmov(dest, num_components, bit_width, src);
   case nir_op_fmul:
      return evaluate_fmul(dest, num_components, bit_width, src);
   case nir_op_fne:
      return evaluate_fne(dest, num_components, bit_width, src);
   case nir_op_fne32:
      return evaluate_fne32(dest, num_components, bit_width, src);
   case nir_op_fneg:
      return evaluate_fneg(dest, num_components, bit_width, src);
   case nir_op_fnoise1_1:
      return evaluate_fnoise1_1(dest, num_components, bit_width, src);
   case nir_op_fnoise1_2:
      return evaluate_fnoise1_2(dest, num_components, bit_width, src);
   case nir_op_fnoise1_3:
      return evaluate_fnoise1_3(dest, num_components, bit_width, src);
   case nir_op_fnoise1_4:
      return evaluate_fnoise1_4(dest, num_components, bit_width, src);
   case nir_op_fnoise2_1:
      return evaluate_fnoise2_1(dest, num_components, bit_width, src);
   case nir_op_fnoise2_2:
      return evaluate_fnoise2_2(dest, num_components, bit_width, src);
   case nir_op_fnoise2_3:
      return evaluate_fnoise2_3(dest, num_components, bit_width, src);
   case nir_op_fnoise2_4:
      return evaluate_fnoise2_4(dest, num_components, bit_width, src);
   case nir_op_fnoise3_1:
      return evaluate_fnoise3_1(dest, num_components, bit_width, src);
   case nir_op_fnoise3_2:
      return evaluate_fnoise3_2(dest, num_components, bit_width, src);
   case nir_op_fnoise3_3:
      return evaluate_fnoise3_3(dest, num_components, bit_width, src);
   case nir_op_fnoise3_4:
      return evaluate_fnoise3_4(dest, num_components, bit_width, src);
   case nir_op_fnoise4_1:
      return evaluate_fnoise4_1(dest, num_components, bit_width, src);
   case nir_op_fnoise4_2:
      return evaluate_fnoise4_2(dest, num_components, bit_width, src);
   case nir_op_fnoise4_3:
      return evaluate_fnoise4_3(dest, num_components, bit_width, src);
   case nir_op_fnoise4_4:
      return evaluate_fnoise4_4(dest, num_components, bit_width, src);
   case nir_op_fnot:
      return evaluate_fnot(dest, num_components, bit_width, src);
   case nir_op_for:
      return evaluate_for(dest, num_components, bit_width, src);
   case nir_op_fpow:
      return evaluate_fpow(dest, num_components, bit_width, src);
   case nir_op_fquantize2f16:
      return evaluate_fquantize2f16(dest, num_components, bit_width, src);
   case nir_op_frcp:
      return evaluate_frcp(dest, num_components, bit_width, src);
   case nir_op_frem:
      return evaluate_frem(dest, num_components, bit_width, src);
   case nir_op_frexp_exp:
      return evaluate_frexp_exp(dest, num_components, bit_width, src);
   case nir_op_frexp_sig:
      return evaluate_frexp_sig(dest, num_components, bit_width, src);
   case nir_op_fround_even:
      return evaluate_fround_even(dest, num_components, bit_width, src);
   case nir_op_frsq:
      return evaluate_frsq(dest, num_components, bit_width, src);
   case nir_op_fsat:
      return evaluate_fsat(dest, num_components, bit_width, src);
   case nir_op_fsign:
      return evaluate_fsign(dest, num_components, bit_width, src);
   case nir_op_fsin:
      return evaluate_fsin(dest, num_components, bit_width, src);
   case nir_op_fsqrt:
      return evaluate_fsqrt(dest, num_components, bit_width, src);
   case nir_op_fsub:
      return evaluate_fsub(dest, num_components, bit_width, src);
   case nir_op_ftrunc:
      return evaluate_ftrunc(dest, num_components, bit_width, src);
   case nir_op_fxor:
      return evaluate_fxor(dest, num_components, bit_width, src);
   case nir_op_i2b1:
      return evaluate_i2b1(dest, num_components, bit_width, src);
   case nir_op_i2b32:
      return evaluate_i2b32(dest, num_components, bit_width, src);
   case nir_op_i2f16:
      return evaluate_i2f16(dest, num_components, bit_width, src);
   case nir_op_i2f32:
      return evaluate_i2f32(dest, num_components, bit_width, src);
   case nir_op_i2f64:
      return evaluate_i2f64(dest, num_components, bit_width, src);
   case nir_op_i2i1:
      return evaluate_i2i1(dest, num_components, bit_width, src);
   case nir_op_i2i16:
      return evaluate_i2i16(dest, num_components, bit_width, src);
   case nir_op_i2i32:
      return evaluate_i2i32(dest, num_components, bit_width, src);
   case nir_op_i2i64:
      return evaluate_i2i64(dest, num_components, bit_width, src);
   case nir_op_i2i8:
      return evaluate_i2i8(dest, num_components, bit_width, src);
   case nir_op_iabs:
      return evaluate_iabs(dest, num_components, bit_width, src);
   case nir_op_iadd:
      return evaluate_iadd(dest, num_components, bit_width, src);
   case nir_op_iadd_sat:
      return evaluate_iadd_sat(dest, num_components, bit_width, src);
   case nir_op_iand:
      return evaluate_iand(dest, num_components, bit_width, src);
   case nir_op_ibfe:
      return evaluate_ibfe(dest, num_components, bit_width, src);
   case nir_op_ibitfield_extract:
      return evaluate_ibitfield_extract(dest, num_components, bit_width, src);
   case nir_op_idiv:
      return evaluate_idiv(dest, num_components, bit_width, src);
   case nir_op_ieq:
      return evaluate_ieq(dest, num_components, bit_width, src);
   case nir_op_ieq32:
      return evaluate_ieq32(dest, num_components, bit_width, src);
   case nir_op_ifind_msb:
      return evaluate_ifind_msb(dest, num_components, bit_width, src);
   case nir_op_ige:
      return evaluate_ige(dest, num_components, bit_width, src);
   case nir_op_ige32:
      return evaluate_ige32(dest, num_components, bit_width, src);
   case nir_op_ihadd:
      return evaluate_ihadd(dest, num_components, bit_width, src);
   case nir_op_ilt:
      return evaluate_ilt(dest, num_components, bit_width, src);
   case nir_op_ilt32:
      return evaluate_ilt32(dest, num_components, bit_width, src);
   case nir_op_imax:
      return evaluate_imax(dest, num_components, bit_width, src);
   case nir_op_imax3:
      return evaluate_imax3(dest, num_components, bit_width, src);
   case nir_op_imed3:
      return evaluate_imed3(dest, num_components, bit_width, src);
   case nir_op_imin:
      return evaluate_imin(dest, num_components, bit_width, src);
   case nir_op_imin3:
      return evaluate_imin3(dest, num_components, bit_width, src);
   case nir_op_imod:
      return evaluate_imod(dest, num_components, bit_width, src);
   case nir_op_imov:
      return evaluate_imov(dest, num_components, bit_width, src);
   case nir_op_imul:
      return evaluate_imul(dest, num_components, bit_width, src);
   case nir_op_imul_2x32_64:
      return evaluate_imul_2x32_64(dest, num_components, bit_width, src);
   case nir_op_imul_high:
      return evaluate_imul_high(dest, num_components, bit_width, src);
   case nir_op_ine:
      return evaluate_ine(dest, num_components, bit_width, src);
   case nir_op_ine32:
      return evaluate_ine32(dest, num_components, bit_width, src);
   case nir_op_ineg:
      return evaluate_ineg(dest, num_components, bit_width, src);
   case nir_op_inot:
      return evaluate_inot(dest, num_components, bit_width, src);
   case nir_op_ior:
      return evaluate_ior(dest, num_components, bit_width, src);
   case nir_op_irem:
      return evaluate_irem(dest, num_components, bit_width, src);
   case nir_op_irhadd:
      return evaluate_irhadd(dest, num_components, bit_width, src);
   case nir_op_ishl:
      return evaluate_ishl(dest, num_components, bit_width, src);
   case nir_op_ishr:
      return evaluate_ishr(dest, num_components, bit_width, src);
   case nir_op_isign:
      return evaluate_isign(dest, num_components, bit_width, src);
   case nir_op_isub:
      return evaluate_isub(dest, num_components, bit_width, src);
   case nir_op_isub_sat:
      return evaluate_isub_sat(dest, num_components, bit_width, src);
   case nir_op_ixor:
      return evaluate_ixor(dest, num_components, bit_width, src);
   case nir_op_ldexp:
      return evaluate_ldexp(dest, num_components, bit_width, src);
   case nir_op_pack_32_2x16:
      return evaluate_pack_32_2x16(dest, num_components, bit_width, src);
   case nir_op_pack_32_2x16_split:
      return evaluate_pack_32_2x16_split(dest, num_components, bit_width, src);
   case nir_op_pack_64_2x32:
      return evaluate_pack_64_2x32(dest, num_components, bit_width, src);
   case nir_op_pack_64_2x32_split:
      return evaluate_pack_64_2x32_split(dest, num_components, bit_width, src);
   case nir_op_pack_64_4x16:
      return evaluate_pack_64_4x16(dest, num_components, bit_width, src);
   case nir_op_pack_half_2x16:
      return evaluate_pack_half_2x16(dest, num_components, bit_width, src);
   case nir_op_pack_half_2x16_split:
      return evaluate_pack_half_2x16_split(dest, num_components, bit_width, src);
   case nir_op_pack_snorm_2x16:
      return evaluate_pack_snorm_2x16(dest, num_components, bit_width, src);
   case nir_op_pack_snorm_4x8:
      return evaluate_pack_snorm_4x8(dest, num_components, bit_width, src);
   case nir_op_pack_unorm_2x16:
      return evaluate_pack_unorm_2x16(dest, num_components, bit_width, src);
   case nir_op_pack_unorm_4x8:
      return evaluate_pack_unorm_4x8(dest, num_components, bit_width, src);
   case nir_op_pack_uvec2_to_uint:
      return evaluate_pack_uvec2_to_uint(dest, num_components, bit_width, src);
   case nir_op_pack_uvec4_to_uint:
      return evaluate_pack_uvec4_to_uint(dest, num_components, bit_width, src);
   case nir_op_seq:
      return evaluate_seq(dest, num_components, bit_width, src);
   case nir_op_sge:
      return evaluate_sge(dest, num_components, bit_width, src);
   case nir_op_slt:
      return evaluate_slt(dest, num_components, bit_width, src);
   case nir_op_sne:
      return evaluate_sne(dest, num_components, bit_width, src);
   case nir_op_u2f16:
      return evaluate_u2f16(dest, num_components, bit_width, src);
   case nir_op_u2f32:
      return evaluate_u2f32(dest, num_components, bit_width, src);
   case nir_op_u2f64:
      return evaluate_u2f64(dest, num_components, bit_width, src);
   case nir_op_u2u1:
      return evaluate_u2u1(dest, num_components, bit_width, src);
   case nir_op_u2u16:
      return evaluate_u2u16(dest, num_components, bit_width, src);
   case nir_op_u2u32:
      return evaluate_u2u32(dest, num_components, bit_width, src);
   case nir_op_u2u64:
      return evaluate_u2u64(dest, num_components, bit_width, src);
   case nir_op_u2u8:
      return evaluate_u2u8(dest, num_components, bit_width, src);
   case nir_op_uadd_carry:
      return evaluate_uadd_carry(dest, num_components, bit_width, src);
   case nir_op_uadd_sat:
      return evaluate_uadd_sat(dest, num_components, bit_width, src);
   case nir_op_ubfe:
      return evaluate_ubfe(dest, num_components, bit_width, src);
   case nir_op_ubitfield_extract:
      return evaluate_ubitfield_extract(dest, num_components, bit_width, src);
   case nir_op_udiv:
      return evaluate_udiv(dest, num_components, bit_width, src);
   case nir_op_ufind_msb:
      return evaluate_ufind_msb(dest, num_components, bit_width, src);
   case nir_op_uge:
      return evaluate_uge(dest, num_components, bit_width, src);
   case nir_op_uge32:
      return evaluate_uge32(dest, num_components, bit_width, src);
   case nir_op_uhadd:
      return evaluate_uhadd(dest, num_components, bit_width, src);
   case nir_op_ult:
      return evaluate_ult(dest, num_components, bit_width, src);
   case nir_op_ult32:
      return evaluate_ult32(dest, num_components, bit_width, src);
   case nir_op_umax:
      return evaluate_umax(dest, num_components, bit_width, src);
   case nir_op_umax3:
      return evaluate_umax3(dest, num_components, bit_width, src);
   case nir_op_umax_4x8:
      return evaluate_umax_4x8(dest, num_components, bit_width, src);
   case nir_op_umed3:
      return evaluate_umed3(dest, num_components, bit_width, src);
   case nir_op_umin:
      return evaluate_umin(dest, num_components, bit_width, src);
   case nir_op_umin3:
      return evaluate_umin3(dest, num_components, bit_width, src);
   case nir_op_umin_4x8:
      return evaluate_umin_4x8(dest, num_components, bit_width, src);
   case nir_op_umod:
      return evaluate_umod(dest, num_components, bit_width, src);
   case nir_op_umul_2x32_64:
      return evaluate_umul_2x32_64(dest, num_components, bit_width, src);
   case nir_op_umul_high:
      return evaluate_umul_high(dest, num_components, bit_width, src);
   case nir_op_umul_unorm_4x8:
      return evaluate_umul_unorm_4x8(dest, num_components, bit_width, src);
   case nir_op_unpack_32_2x16:
      return evaluate_unpack_32_2x16(dest, num_components, bit_width, src);
   case nir_op_unpack_32_2x16_split_x:
      return evaluate_unpack_32_2x16_split_x(dest, num_components, bit_width, src);
   case nir_op_unpack_32_2x16_split_y:
      return evaluate_unpack_32_2x16_split_y(dest, num_components, bit_width, src);
   case nir_op_unpack_64_2x32:
      return evaluate_unpack_64_2x32(dest, num_components, bit_width, src);
   case nir_op_unpack_64_2x32_split_x:
      return evaluate_unpack_64_2x32_split_x(dest, num_components, bit_width, src);
   case nir_op_unpack_64_2x32_split_y:
      return evaluate_unpack_64_2x32_split_y(dest, num_components, bit_width, src);
   case nir_op_unpack_64_4x16:
      return evaluate_unpack_64_4x16(dest, num_components, bit_width, src);
   case nir_op_unpack_half_2x16:
      return evaluate_unpack_half_2x16(dest, num_components, bit_width, src);
   case nir_op_unpack_half_2x16_split_x:
      return evaluate_unpack_half_2x16_split_x(dest, num_components, bit_width, src);
   case nir_op_unpack_half_2x16_split_y:
      return evaluate_unpack_half_2x16_split_y(dest, num_components, bit_width, src);
   case nir_op_unpack_snorm_2x16:
      return evaluate_unpack_snorm_2x16(dest, num_components, bit_width, src);
   case nir_op_unpack_snorm_4x8:
      return evaluate_unpack_snorm_4x8(dest, num_components, bit_width, src);
   case nir_op_unpack_unorm_2x16:
      return evaluate_unpack_unorm_2x16(dest, num_components, bit_width, src);
   case nir_op_unpack_unorm_4x8:
      return evaluate_unpack_unorm_4x8(dest, num_components, bit_width, src);
   case nir_op_urhadd:
      return evaluate_urhadd(dest, num_components, bit_width, src);
   case nir_op_usadd_4x8:
      return evaluate_usadd_4x8(dest, num_components, bit_width, src);
   case nir_op_ushr:
      return evaluate_ushr(dest, num_components, bit_width, src);
   case nir_op_ussub_4x8:
      return evaluate_ussub_4x8(dest, num_components, bit_width, src);
   case nir_op_usub_borrow:
      return evaluate_usub_borrow(dest, num_components, bit_width, src);
   case nir_op_usub_sat:
      return evaluate_usub_sat(dest, num_components, bit_width, src);
   case nir_op_vec2:
      return evaluate_vec2(dest, num_components, bit_width, src);
   case nir_op_vec3:
      return evaluate_vec3(dest, num_components, bit_width, src);
   case nir_op_vec4:
      return evaluate_vec4(dest, num_components, bit_width, src);
   default:
      unreachable("shouldn't get here");
   }
}
