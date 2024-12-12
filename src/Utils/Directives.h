#pragma once

#ifdef __clang__
#define ITERATION_INDEPENDENT_LOOP _Pragma("clang loop vectorize(assume_safety)")
#elif defined(__GNUC__)
#define ITERATION_INDEPENDENT_LOOP _Pragma("GCC ivdep")
#else
#define ITERATION_INDEPENDENT_LOOP
#endif

#ifdef __clang__
#define UNPREDICTABLE(condition) __builtin_unpredictable(condition)
#elif defined(__GNUC__)
#define UNPREDICTABLE(condition) __builtin_expect_with_probability( (long)(condition), (long)1, 0.50 )
#else
#define UNPREDICTABLE(condition) (condition)
#endif

#define UNPREDICTABLE_TERNARY(condition, ifTrue, ifFalse) UNPREDICTABLE((condition)) ? (ifTrue) : (ifFalse)

