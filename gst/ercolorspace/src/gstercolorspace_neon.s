/*
 * Copyright (c) 2014, Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

.text
.syntax unified
.arm

#ifdef __ANDROID__
.arch armv7a
#endif







/* NV12 -> BGRA */

/*
r0: uchar * Y,
r1: uchar * UV
r2: uchar * RGB
r3: int width
 */

#ifdef __APPLE__
.globl _gst_ercolorspace_transform_nv12_to_bgra_neon
.align 2
_gst_ercolorspace_transform_nv12_to_bgra_neon:
#endif

#ifdef __ANDROID__
.global gst_ercolorspace_transform_nv12_to_bgra_neon
.align 2
gst_ercolorspace_transform_nv12_to_bgra_neon:
#endif

push            {r4-r12, lr}
vstmdb          sp!, {d0-d15}
vstmdb          sp!, {d16-d31}

mov             r4, #8          /* handle 8 values every iteration*/

mov             r11, #255
vdup.8          d21, r11
mov             r11, #0

push            {r0}
/*transform constants:*/
mov             r5, #16
mov             r6, #128
ldr             r7, [pc, #-4]
.word 298
ldr             r8, [pc, #-4]
.word 409
ldr             r9, [pc, #-4]
.word 100
mov             r10, #208
ldr             r11, [pc, #-4]
.word 516
mov             r12, #32
pop             {r0}

loop_nv12:

/* set up source data */
vld1.u8         {d0}, [r0], r4          /*load src data Y */
vld1.u8         {d1}, [r1], r4          /*load src data UV, [v1 u1 v2 u2 v3 u3 v4 u4] */

vshr.u16        d28, d1, #8             /*shift down V, [0 v1 0 v2 0 v3 0 v4]*/
vshl.u16        d29, d1, #8             /*shift up U, [u1 0 u2 0 u3 0 u4 0]*/

vshl.u16        d30, d28, #8            /*shift up V, [v1 0 v2 0 v3 0 v4 0]*/
vshr.u16        d31, d29, #8            /*shift down U, [0 u1 0 u2 0 u3 0 u4]*/

vadd.u8         d1, d28, d30            /*add V, [v1 v1 v2 v2 v3 v3 v4 v4]*/
vadd.u8         d2, d29, d31            /*add U, [u1 u1 u2 u2 u3 u3 u4 u4]*/

/* R channel*/
vmovl.u8        q2, d0
vdup.16         q15, r5                 /* load constant 16*/
vsub.s16        q2, q2, q15             /* C = yarr[] - 16*/
vdup.16         d30, r7                 /* load constant 298 */
vmull.s16       q3, d4, d30             /* 298 * C*/
vmull.s16       q4, d5, d30

vmovl.u8        q2, d1
vdup.16         q15, r6                 /* load constant 128*/
vsub.s16        q12, q2, q15            /* E = uvarr[v] - 128*/
vdup.16         d30, r8                 /* load constant 409*/
vmull.s16       q5, d24, d30            /* 409 * E*/
vmull.s16       q6, d25, d30

vadd.s32        q5, q3, q5              /* 298 * C           + 409 * E*/
vadd.s32        q6, q4, q6

vqrshrun.s32    d26, q5, #8             /* clip(( 298 * C           + 409 * E + 128) >> 8)*/
vqrshrun.s32    d27, q6, #8

vqmovn.u16      d20, q13                /* 16bit to 8bit, R channel done!*/

/* G channel*/
vdup.16         d30, r10                /* load constant 208*/
vmull.s16       q5, d24, d30            /* 208 * E*/
vmull.s16       q6, d25, d30

vsub.s32        q5, q3, q5              /* 298 * C           - 208 * E*/
vsub.s32        q6, q4, q6

vmovl.u8        q2, d2
vdup.16         q15, r6                 /* load constant 128*/
vsub.s16        q11, q2, q15            /* D = uvarr[v] - 128*/
vdup.16         d30, r9                 /* load constant 100*/
vmull.s16       q7, d22, d30            /* 100 * D*/
vmull.s16       q8, d23, d30

vsub.s32        q5, q5, q7              /* 298 * C - 100 * D - 208 * E*/
vsub.s32        q6, q6, q8

vqrshrun.s32    d26, q5, #8             /* clip(( 298 * C - 100 * D - 208 * E + 128) >> 8)*/
vqrshrun.s32    d27, q6, #8

vqmovn.u16      d19, q13                /* 16bit to 8bit, G channel done!*/

/* B channel*/
vdup.16         d30, r11                /* load constant 516*/
vmull.s16       q7, d22, d30            /* 516 * D*/
vmull.s16       q8, d23, d30

vadd.s32        q5, q3, q7              /* 298 * C + 516 * D*/
vadd.s32        q6, q4, q8

vqrshrun.s32    d26, q5, #8             /* clip(( 298 * C + 516 * D           + 128) >> 8)*/
vqrshrun.s32    d27, q6, #8

vqmovn.u16      d18, q13                /* 16bit to 8bit, B channel done!*/

/* store results*/
vst4.u8         {d18, d19, d20, d21}, [r2], r12

subs            r3, #8
bne             loop_nv12

vldmia          sp!, {d16-d31}
vldmia          sp!, {d0-d15}
pop             {r4-r12, pc}









/* NV12 -> RGBA */

/*
r0: uchar * Y,
r1: uchar * UV
r2: uchar * RGB
r3: int width
 */

#ifdef __APPLE__
.globl _gst_ercolorspace_transform_nv12_to_rgba_neon
.align 2
_gst_ercolorspace_transform_nv12_to_rgba_neon:
#endif

#ifdef __ANDROID__
.global gst_ercolorspace_transform_nv12_to_rgba_neon
.align 2
gst_ercolorspace_transform_nv12_to_rgba_neon:
#endif

push            {r4-r12, lr}
vstmdb          sp!, {d0-d15}
vstmdb          sp!, {d16-d31}

mov             r4, #8          /* handle 8 values every iteration*/

mov             r11, #255
vdup.8          d21, r11
mov             r11, #0

push            {r0}
/*transform constants:*/
mov             r5, #16
mov             r6, #128
ldr             r7, [pc, #-4]
.word 298
ldr             r8, [pc, #-4]
.word 409
ldr             r9, [pc, #-4]
.word 100
mov             r10, #208
ldr             r11, [pc, #-4]
.word 516
mov             r12, #32
pop             {r0}

loop_nv12_rgb:

/* set up source data */
vld1.u8         {d0}, [r0], r4          /*load src data Y */
vld1.u8         {d1}, [r1], r4          /*load src data UV, [v1 u1 v2 u2 v3 u3 v4 u4] */

vshr.u16        d28, d1, #8             /*shift down V, [0 v1 0 v2 0 v3 0 v4]*/
vshl.u16        d29, d1, #8             /*shift up U, [u1 0 u2 0 u3 0 u4 0]*/

vshl.u16        d30, d28, #8            /*shift up V, [v1 0 v2 0 v3 0 v4 0]*/
vshr.u16        d31, d29, #8            /*shift down U, [0 u1 0 u2 0 u3 0 u4]*/

vadd.u8         d1, d28, d30            /*add V, [v1 v1 v2 v2 v3 v3 v4 v4]*/
vadd.u8         d2, d29, d31            /*add U, [u1 u1 u2 u2 u3 u3 u4 u4]*/

/* R channel*/
vmovl.u8        q2, d0
vdup.16         q15, r5                 /* load constant 16*/
vsub.s16        q2, q2, q15             /* C = yarr[] - 16*/
vdup.16         d30, r7                 /* load constant 298 */
vmull.s16       q3, d4, d30             /* 298 * C*/
vmull.s16       q4, d5, d30

vmovl.u8        q2, d1
vdup.16         q15, r6                 /* load constant 128*/
vsub.s16        q12, q2, q15            /* E = uvarr[v] - 128*/
vdup.16         d30, r8                 /* load constant 409*/
vmull.s16       q5, d24, d30            /* 409 * E*/
vmull.s16       q6, d25, d30

vadd.s32        q5, q3, q5              /* 298 * C           + 409 * E*/
vadd.s32        q6, q4, q6

vqrshrun.s32    d26, q5, #8             /* clip(( 298 * C           + 409 * E + 128) >> 8)*/
vqrshrun.s32    d27, q6, #8

vqmovn.u16      d18, q13                /* 16bit to 8bit, R channel done!*/

/* G channel*/
vdup.16         d30, r10                /* load constant 208*/
vmull.s16       q5, d24, d30            /* 208 * E*/
vmull.s16       q6, d25, d30

vsub.s32        q5, q3, q5              /* 298 * C           - 208 * E*/
vsub.s32        q6, q4, q6

vmovl.u8        q2, d2
vdup.16         q15, r6                 /* load constant 128*/
vsub.s16        q11, q2, q15            /* D = uvarr[v] - 128*/
vdup.16         d30, r9                 /* load constant 100*/
vmull.s16       q7, d22, d30            /* 100 * D*/
vmull.s16       q8, d23, d30

vsub.s32        q5, q5, q7              /* 298 * C - 100 * D - 208 * E*/
vsub.s32        q6, q6, q8

vqrshrun.s32    d26, q5, #8             /* clip(( 298 * C - 100 * D - 208 * E + 128) >> 8)*/
vqrshrun.s32    d27, q6, #8

vqmovn.u16      d19, q13                /* 16bit to 8bit, G channel done!*/

/* B channel*/
vdup.16         d30, r11                /* load constant 516*/
vmull.s16       q7, d22, d30            /* 516 * D*/
vmull.s16       q8, d23, d30

vadd.s32        q5, q3, q7              /* 298 * C + 516 * D*/
vadd.s32        q6, q4, q8

vqrshrun.s32    d26, q5, #8             /* clip(( 298 * C + 516 * D           + 128) >> 8)*/
vqrshrun.s32    d27, q6, #8

vqmovn.u16      d20, q13                /* 16bit to 8bit, B channel done!*/

/* store results*/
vst4.u8         {d18, d19, d20, d21}, [r2], r12

subs            r3, #8
bne             loop_nv12_rgb

vldmia          sp!, {d16-d31}
vldmia          sp!, {d0-d15}
pop             {r4-r12, pc}










/* NV12 -> I420 */

/*
 r0: uchar * UV
 r1: uint size
 r2: uchar * OU
 r3: uchar * OV
 */

#ifdef __APPLE__
.globl _gst_ercolorspace_transform_nv12_to_i420_neon
.align 2
_gst_ercolorspace_transform_nv12_to_i420_neon:
#endif

#ifdef __ANDROID__
.global gst_ercolorspace_transform_nv12_to_i420_neon
.align 2
gst_ercolorspace_transform_nv12_to_i420_neon:
#endif

push            {r4-r5, lr}
vstmdb          sp!, {d0-d7}


loop_nv12_i420:

/* UV */
/* set up source data */
pld             [r0]
vld2.u8         {d1, d2}, [r0]!      /* load src data UV */
/* store results */
vst1.u8         {d1}, [r2]!          /* store U */
vst1.u8         {d2}, [r3]!          /* store V */

subs            r1, #16
bne             loop_nv12_i420


vldmia          sp!, {d0-d7}
pop             {r4-r5, pc}















/* NV21 -> RGBA */

/*
 r0: uchar * Y,
 r1: uchar * UV
 r2: uchar * RGB
 r3: int width
 */

#ifdef __APPLE__
.globl _gst_ercolorspace_transform_nv21_to_bgra_neon
.align 2
_gst_ercolorspace_transform_nv21_to_bgra_neon:
#endif

#ifdef __ANDROID__
.global gst_ercolorspace_transform_nv21_to_bgra_neon
.align 2
gst_ercolorspace_transform_nv21_to_bgra_neon:
#endif


push            {r4-r12, lr}
vstmdb          sp!, {d0-d15}
vstmdb          sp!, {d16-d31}

mov             r4, #8          /* handle 8 values every iteration*/

mov             r11, #255
vdup.8          d21, r11
mov             r11, #0

push            {r0}
/*transform constants:*/
mov             r5, #16
mov             r6, #128
ldr             r7, [pc, #-4]
.word 298
ldr             r8, [pc, #-4]
.word 409
ldr             r9, [pc, #-4]
.word 100
mov             r10, #208
ldr             r11, [pc, #-4]
.word 516
mov             r12, #32
pop             {r0}

loop_nv21_bgr:

/* set up source data*/
vld1.u8         {d0}, [r0], r4          /*load src data Y*/
vld1.u8         {d1}, [r1], r4          /*load src data VU, [u1 v1 u2 v2 u3 v3 u4 v4] */

vshr.u16        d28, d1, #8             /*shift down U, [0 u1 0 u2 0 u3 0 u4]*/
vshl.u16        d29, d1, #8             /*shift up V, [v1 0 v2 0 v3 0 v4 0]*/

vshl.u16        d30, d28, #8            /*shift up U, [u1 0 u2 0 u3 0 u4 0]*/
vshr.u16        d31, d29, #8            /*shift down V, [0 v1 0 v2 0 v3 0 v4]*/

vadd.u8         d2, d28, d30            /*add U, [u1 u1 u2 u2 u3 u3 u4 u4]*/
vadd.u8         d1, d29, d31            /*add V, [v1 v1 v2 v2 v3 v3 v4 v4]*/

/* R channel*/
vmovl.u8        q2, d0
vdup.16         q15, r5                 /* load constant 16*/
vsub.s16        q2, q2, q15             /* C = yarr[] - 16*/
vdup.16         d30, r7                 /* load constant 298 */
vmull.s16       q3, d4, d30             /* 298 * C*/
vmull.s16       q4, d5, d30

vmovl.u8        q2, d1
vdup.16         q15, r6                 /* load constant 128*/
vsub.s16        q12, q2, q15            /* E = uvarr[v] - 128*/
vdup.16         d30, r8                 /* load constant 409*/
vmull.s16       q5, d24, d30            /* 409 * E*/
vmull.s16       q6, d25, d30

vadd.s32        q5, q3, q5              /* 298 * C           + 409 * E*/
vadd.s32        q6, q4, q6

vqrshrun.s32    d26, q5, #8             /* clip(( 298 * C           + 409 * E + 128) >> 8)*/
vqrshrun.s32    d27, q6, #8

vqmovn.u16      d20, q13                /* 16bit to 8bit, R channel done!*/

/* G channel*/
vdup.16         d30, r10                /* load constant 208*/
vmull.s16       q5, d24, d30            /* 208 * E*/
vmull.s16       q6, d25, d30

vsub.s32        q5, q3, q5              /* 298 * C           - 208 * E*/
vsub.s32        q6, q4, q6

vmovl.u8        q2, d2
vdup.16         q15, r6                 /* load constant 128*/
vsub.s16        q11, q2, q15            /* D = uvarr[v] - 128*/
vdup.16         d30, r9                 /* load constant 100*/
vmull.s16       q7, d22, d30            /* 100 * D*/
vmull.s16       q8, d23, d30

vsub.s32        q5, q5, q7              /* 298 * C - 100 * D - 208 * E*/
vsub.s32        q6, q6, q8

vqrshrun.s32    d26, q5, #8             /* clip(( 298 * C - 100 * D - 208 * E + 128) >> 8)*/
vqrshrun.s32    d27, q6, #8

vqmovn.u16      d19, q13                /* 16bit to 8bit, G channel done!*/

/* B channel*/
vdup.16         d30, r11                /* load constant 516*/
vmull.s16       q7, d22, d30            /* 516 * D*/
vmull.s16       q8, d23, d30

vadd.s32        q5, q3, q7              /* 298 * C + 516 * D*/
vadd.s32        q6, q4, q8

vqrshrun.s32    d26, q5, #8             /* clip(( 298 * C + 516 * D           + 128) >> 8)*/
vqrshrun.s32    d27, q6, #8

vqmovn.u16      d18, q13                /* 16bit to 8bit, B channel done!*/

/* store results*/
vst4.u8         {d18, d19, d20, d21}, [r2], r12

subs            r3, #8
bne             loop_nv21_bgr

vldmia          sp!, {d16-d31}
vldmia          sp!, {d0-d15}
pop             {r4-r12, pc}








/* NV21 -> RGBA */

/*
 r0: uchar * Y,
 r1: uchar * UV
 r2: uchar * RGB
 r3: int width
 */

#ifdef __APPLE__
.globl _gst_ercolorspace_transform_nv21_to_rgba_neon
.align 2
_gst_ercolorspace_transform_nv21_to_rgba_neon:
#endif

#ifdef __ANDROID__
.global gst_ercolorspace_transform_nv21_to_rgba_neon
.align 2
gst_ercolorspace_transform_nv21_to_rgba_neon:
#endif


push            {r4-r12, lr}
vstmdb          sp!, {d0-d15}
vstmdb          sp!, {d16-d31}

mov             r4, #8          /* handle 8 values every iteration*/

mov             r11, #255
vdup.8          d21, r11
mov             r11, #0

push            {r0}
/*transform constants:*/
mov             r5, #16
mov             r6, #128
ldr             r7, [pc, #-4]
.word 298
ldr             r8, [pc, #-4]
.word 409
ldr             r9, [pc, #-4]
.word 100
mov             r10, #208
ldr             r11, [pc, #-4]
.word 516
mov             r12, #32
pop             {r0}

loop_nv21_rgb:

/* set up source data*/
vld1.u8         {d0}, [r0], r4          /*load src data Y*/
vld1.u8         {d1}, [r1], r4          /*load src data UV, [v1 u1 v2 u2 v3 u3 v4 u4] */

vshr.u16        d28, d1, #8             /*shift down V, [0 v1 0 v2 0 v3 0 v4]*/
vshl.u16        d29, d1, #8             /*shift up U, [u1 0 u2 0 u3 0 u4 0]*/

vshl.u16        d30, d28, #8            /*shift up V, [v1 0 v2 0 v3 0 v4 0]*/
vshr.u16        d31, d29, #8            /*shift down U, [0 u1 0 u2 0 u3 0 u4]*/

vadd.u8         d1, d28, d30            /*add V, [v1 v1 v2 v2 v3 v3 v4 v4]*/
vadd.u8         d2, d29, d31            /*add U, [u1 u1 u2 u2 u3 u3 u4 u4]*/

/* R channel*/
vmovl.u8        q2, d0
vdup.16         q15, r5                 /* load constant 16*/
vsub.s16        q2, q2, q15             /* C = yarr[] - 16*/
vdup.16         d30, r7                 /* load constant 298 */
vmull.s16       q3, d4, d30             /* 298 * C*/
vmull.s16       q4, d5, d30

vmovl.u8        q2, d1
vdup.16         q15, r6                 /* load constant 128*/
vsub.s16        q12, q2, q15            /* E = uvarr[v] - 128*/
vdup.16         d30, r8                 /* load constant 409*/
vmull.s16       q5, d24, d30            /* 409 * E*/
vmull.s16       q6, d25, d30

vadd.s32        q5, q3, q5              /* 298 * C           + 409 * E*/
vadd.s32        q6, q4, q6

vqrshrun.s32    d26, q5, #8             /* clip(( 298 * C           + 409 * E + 128) >> 8)*/
vqrshrun.s32    d27, q6, #8

vqmovn.u16      d18, q13                /* 16bit to 8bit, R channel done!*/

/* G channel*/
vdup.16         d30, r10                /* load constant 208*/
vmull.s16       q5, d24, d30            /* 208 * E*/
vmull.s16       q6, d25, d30

vsub.s32        q5, q3, q5              /* 298 * C           - 208 * E*/
vsub.s32        q6, q4, q6

vmovl.u8        q2, d2
vdup.16         q15, r6                 /* load constant 128*/
vsub.s16        q11, q2, q15            /* D = uvarr[v] - 128*/
vdup.16         d30, r9                 /* load constant 100*/
vmull.s16       q7, d22, d30            /* 100 * D*/
vmull.s16       q8, d23, d30

vsub.s32        q5, q5, q7              /* 298 * C - 100 * D - 208 * E*/
vsub.s32        q6, q6, q8

vqrshrun.s32    d26, q5, #8             /* clip(( 298 * C - 100 * D - 208 * E + 128) >> 8)*/
vqrshrun.s32    d27, q6, #8

vqmovn.u16      d19, q13                /* 16bit to 8bit, G channel done!*/

/* B channel*/
vdup.16         d30, r11                /* load constant 516*/
vmull.s16       q7, d22, d30            /* 516 * D*/
vmull.s16       q8, d23, d30

vadd.s32        q5, q3, q7              /* 298 * C + 516 * D*/
vadd.s32        q6, q4, q8

vqrshrun.s32    d26, q5, #8             /* clip(( 298 * C + 516 * D           + 128) >> 8)*/
vqrshrun.s32    d27, q6, #8

vqmovn.u16      d20, q13                /* 16bit to 8bit, B channel done!*/

/* store results*/
vst4.u8         {d18, d19, d20, d21}, [r2], r12

subs            r3, #8
bne             loop_nv21_rgb

vldmia          sp!, {d16-d31}
vldmia          sp!, {d0-d15}
pop             {r4-r12, pc}









/* NV21 -> I420 */

/*
 r0: uchar * UV
 r1: uint size
 r2: uchar * OU
 r3: uchar * OV
 */

#ifdef __APPLE__
.globl _gst_ercolorspace_transform_nv21_to_i420_neon
.align 2
_gst_ercolorspace_transform_nv21_to_i420_neon:
#endif

#ifdef __ANDROID__
.global gst_ercolorspace_transform_nv21_to_i420_neon
.align 2
gst_ercolorspace_transform_nv21_to_i420_neon:
#endif

push            {r4-r5, lr}
vstmdb          sp!, {d0-d7}


loop_nv21_i420:

/* UV */
/* set up source data */
pld             [r0]
vld2.u8         {d1, d2}, [r0]!      /* load src data UV */
/* store results */
vst1.u8         {d1}, [r3]!          /* store V */
vst1.u8         {d2}, [r2]!          /* store U */

subs            r1, #16
bne             loop_nv21_i420


vldmia          sp!, {d0-d7}
pop             {r4-r5, pc}












/* I420 -> BGRA */

/*
r0: uchar * Y,
r1: uchar * U
r2: uchar * V
r3: int width
r4: uchar * RGB
 */

#ifdef __APPLE__
.globl _gst_ercolorspace_transform_i420_to_bgra_neon
.align 2
_gst_ercolorspace_transform_i420_to_bgra_neon:
#endif

#ifdef __ANDROID__
.global gst_ercolorspace_transform_i420_to_bgra_neon
.align 2
gst_ercolorspace_transform_i420_to_bgra_neon:
#endif

push            {r4-r12, lr}

ldr             lr, [sp, #40]   /* load RGB pointer*/

vstmdb          sp!, {d0-d15}
vstmdb          sp!, {d16-d31}

mov             r4, #4          /* handle 8 values every iteration*/

mov             r11, #255
vdup.8          d21, r11
mov             r11, #0

push            {r0}
/*transform constants:*/
mov             r5, #16
mov             r6, #128
ldr             r7, [pc, #-4]
.word 298
ldr             r8, [pc, #-4]
.word 409
mov             r9, #100
mov             r10, #208
ldr             r11, [pc, #-4]
.word 516
mov             r12, #32
pop             {r0}

loop_i420_bgr:

/* set up source data*/
vld1.u8         {d0}, [r0], r4          /*load src data Y*/
add             r0, r0, r4              /*increment pointer by another 4 (to a total of 8)*/
vld1.u8         {d1}, [r1], r4          /*load src data U, [u1 u2 u3 u4 u5 u6 u7 u8] */
vld1.u8         {d2}, [r2], r4          /*load src data V, [v1 v2 v3 v4 v5 v6 v7 v8] */

vmovl.u8        q14, d1                 /*move up, [u1 0 u2 0 u3 0 u4 0]*/
vmovl.u8        q15, d2                 /*move up, [v1 0 v2 0 v3 0 v4 0]*/

vshl.u16        d29, d28, #8            /*shift down U, [0 u1 0 u2 0 u3 0 u4]*/
vshl.u16        d31, d30, #8            /*shift down U, [0 v1 0 v2 0 v3 0 v4]*/

vadd.u8         d1, d30, d31            /*add V, [v1 v1 v2 v2 v3 v3 v4 v4]*/
vadd.u8         d2, d28, d29            /*add U, [u1 u1 u2 u2 u3 u3 u4 u4]*/

/*vmov.u8         d1, d18*/
/*vmov.u8         d2, d18*/
/* R channel*/
vmovl.u8        q2, d0
vdup.16         q15, r5                 /* load constant 16*/
vsub.s16        q2, q2, q15             /* C = yarr[] - 16*/
vdup.16         d30, r7                 /* load constant 298 */
vmull.s16       q3, d4, d30             /* 298 * C*/
vmull.s16       q4, d5, d30

vmovl.u8        q2, d1
vdup.16         q15, r6                 /* load constant 128*/
vsub.s16        q12, q2, q15            /* E = uvarr[v] - 128*/
vdup.16         d30, r8                 /* load constant 409*/
vmull.s16       q5, d24, d30            /* 409 * E*/
vmull.s16       q6, d25, d30

vadd.s32        q5, q3, q5              /* 298 * C           + 409 * E*/
vadd.s32        q6, q4, q6

vqrshrun.s32    d26, q5, #8             /* clip(( 298 * C           + 409 * E + 128) >> 8)*/
vqrshrun.s32    d27, q6, #8

vqmovn.u16      d20, q13                /* 16bit to 8bit, R channel done!*/

/* G channel*/
vdup.16         d30, r10                /* load constant 208*/
vmull.s16       q5, d24, d30            /* 208 * E*/
vmull.s16       q6, d25, d30

vsub.s32        q5, q3, q5              /* 298 * C           - 208 * E*/
vsub.s32        q6, q4, q6

vmovl.u8        q2, d2
vdup.16         q15, r6                 /* load constant 128*/
vsub.s16        q11, q2, q15            /* D = uvarr[v] - 128*/
vdup.16         d30, r9                 /* load constant 100*/
vmull.s16       q7, d22, d30            /* 100 * D*/
vmull.s16       q8, d23, d30

vsub.s32        q5, q5, q7              /* 298 * C - 100 * D - 208 * E*/
vsub.s32        q6, q6, q8

vqrshrun.s32    d26, q5, #8             /* clip(( 298 * C - 100 * D - 208 * E + 128) >> 8)*/
vqrshrun.s32    d27, q6, #8

vqmovn.u16      d19, q13                /* 16bit to 8bit, G channel done!*/

/* B channel*/
vdup.16         d30, r11                /* load constant 516*/
vmull.s16       q7, d22, d30            /* 516 * D*/
vmull.s16       q8, d23, d30

vadd.s32        q5, q3, q7              /* 298 * C + 516 * D*/
vadd.s32        q6, q4, q8

vqrshrun.s32    d26, q5, #8             /* clip(( 298 * C + 516 * D           + 128) >> 8)*/
vqrshrun.s32    d27, q6, #8

vqmovn.u16      d18, q13                /* 16bit to 8bit, B channel done!*/

/* store results*/
vst4.u8         {d18, d19, d20, d21}, [lr], r12

subs            r3, #8
bne             loop_i420_bgr

vldmia          sp!, {d16-d31}
vldmia          sp!, {d0-d15}
pop             {r4-r12, pc}














/* I420 -> RGBA */

/*
r0: uchar * Y,
r1: uchar * U
r2: uchar * V
r3: int width
r4: uchar * RGB
 */

#ifdef __APPLE__
.globl _gst_ercolorspace_transform_i420_to_rgba_neon
.align 2
_gst_ercolorspace_transform_i420_to_rgba_neon:
#endif

#ifdef __ANDROID__
.global gst_ercolorspace_transform_i420_to_rgba_neon
.align 2
gst_ercolorspace_transform_i420_to_rgba_neon:
#endif

push            {r4-r12, lr}

ldr             lr, [sp, #40]   /* load RGB pointer*/

vstmdb          sp!, {d0-d15}
vstmdb          sp!, {d16-d31}

mov             r4, #4          /* handle 8 values every iteration*/

mov             r11, #255
vdup.8          d21, r11
mov             r11, #0

push            {r0}
/*transform constants:*/
mov             r5, #16
mov             r6, #128
ldr             r7, [pc, #-4]
.word 298
ldr             r8, [pc, #-4]
.word 409
mov             r9, #100
mov             r10, #208
ldr             r11, [pc, #-4]
.word 516
mov             r12, #32
pop             {r0}

loop_i420_rgb:

/* set up source data*/
vld1.u8         {d0}, [r0], r4          /*load src data Y*/
add             r0, r0, r4              /*increment pointer by another 4 (to a total of 8)*/
vld1.u8         {d1}, [r1], r4          /*load src data U, [u1 u2 u3 u4 u5 u6 u7 u8] */
vld1.u8         {d2}, [r2], r4          /*load src data V, [v1 v2 v3 v4 v5 v6 v7 v8] */

vmovl.u8        q14, d1                 /*move up, [u1 0 u2 0 u3 0 u4 0]*/
vmovl.u8        q15, d2                 /*move up, [v1 0 v2 0 v3 0 v4 0]*/

vshl.u16        d29, d28, #8            /*shift down U, [0 u1 0 u2 0 u3 0 u4]*/
vshl.u16        d31, d30, #8            /*shift down U, [0 v1 0 v2 0 v3 0 v4]*/

vadd.u8         d1, d30, d31            /*add V, [v1 v1 v2 v2 v3 v3 v4 v4]*/
vadd.u8         d2, d28, d29            /*add U, [u1 u1 u2 u2 u3 u3 u4 u4]*/

/*vmov.u8         d1, d18*/
/*vmov.u8         d2, d18*/
/* R channel*/
vmovl.u8        q2, d0
vdup.16         q15, r5                 /* load constant 16*/
vsub.s16        q2, q2, q15             /* C = yarr[] - 16*/
vdup.16         d30, r7                 /* load constant 298 */
vmull.s16       q3, d4, d30             /* 298 * C*/
vmull.s16       q4, d5, d30

vmovl.u8        q2, d1
vdup.16         q15, r6                 /* load constant 128*/
vsub.s16        q12, q2, q15            /* E = uvarr[v] - 128*/
vdup.16         d30, r8                 /* load constant 409*/
vmull.s16       q5, d24, d30            /* 409 * E*/
vmull.s16       q6, d25, d30

vadd.s32        q5, q3, q5              /* 298 * C           + 409 * E*/
vadd.s32        q6, q4, q6

vqrshrun.s32    d26, q5, #8             /* clip(( 298 * C           + 409 * E + 128) >> 8)*/
vqrshrun.s32    d27, q6, #8

vqmovn.u16      d18, q13                /* 16bit to 8bit, R channel done!*/

/* G channel*/
vdup.16         d30, r10                /* load constant 208*/
vmull.s16       q5, d24, d30            /* 208 * E*/
vmull.s16       q6, d25, d30

vsub.s32        q5, q3, q5              /* 298 * C           - 208 * E*/
vsub.s32        q6, q4, q6

vmovl.u8        q2, d2
vdup.16         q15, r6                 /* load constant 128*/
vsub.s16        q11, q2, q15            /* D = uvarr[v] - 128*/
vdup.16         d30, r9                 /* load constant 100*/
vmull.s16       q7, d22, d30            /* 100 * D*/
vmull.s16       q8, d23, d30

vsub.s32        q5, q5, q7              /* 298 * C - 100 * D - 208 * E*/
vsub.s32        q6, q6, q8

vqrshrun.s32    d26, q5, #8             /* clip(( 298 * C - 100 * D - 208 * E + 128) >> 8)*/
vqrshrun.s32    d27, q6, #8

vqmovn.u16      d19, q13                /* 16bit to 8bit, G channel done!*/

/* B channel*/
vdup.16         d30, r11                /* load constant 516*/
vmull.s16       q7, d22, d30            /* 516 * D*/
vmull.s16       q8, d23, d30

vadd.s32        q5, q3, q7              /* 298 * C + 516 * D*/
vadd.s32        q6, q4, q8

vqrshrun.s32    d26, q5, #8             /* clip(( 298 * C + 516 * D           + 128) >> 8)*/
vqrshrun.s32    d27, q6, #8

vqmovn.u16      d20, q13                /* 16bit to 8bit, B channel done!*/

/* store results*/
vst4.u8         {d18, d19, d20, d21}, [lr], r12

subs            r3, #8
bne             loop_i420_rgb

vldmia          sp!, {d16-d31}
vldmia          sp!, {d0-d15}
pop             {r4-r12, pc}
