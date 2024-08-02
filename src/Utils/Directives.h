#pragma once

#ifdef __clang__
#define ITERATION_INDEPENDENT_LOOP _Pragma("clang loop vectorize(assume_safety)")
#elif defined(__GNUC__)
#define ITERATION_INDEPENDENT_LOOP _Pragma("GCC ivdep")
#else
#define ITERATION_INDEPENDENT_LOOP
#endif