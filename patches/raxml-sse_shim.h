/* RISC-V portability shim: replace x86 SSE types with scalar equivalents */
#ifndef SSE_SHIM_H
#define SSE_SHIM_H

#if defined(__riscv) || defined(__aarch64__)
  typedef double __m128d;
  typedef float  __m128;
  typedef long long __m128i;
  #define _mm_setzero_pd() 0.0
  #define _mm_load_pd(x)  (*(x))
  #define _mm_store_pd(x,v) (*(x) = (v))
#else
  #include <xmmintrin.h>
  #include <emmintrin.h>
#endif

#endif
