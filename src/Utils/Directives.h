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

#ifdef __clang__
# define FAST_MATH_ON _Pragma("float_control(push)");        \
                     _Pragma("float_control(precise, off)") //https://clang.llvm.org/docs/LanguageExtensions.html#extensions-to-specify-floating-point-flags
# define FAST_MATH_OFF _Pragma("float_control(pop)")

#elif defined(__GNUC__)
# define FAST_MATH_ON  _Pragma("GCC push_options");             \
                      _Pragma("GCC optimize(\"-ffast-math\")")
# define FAST_MATH_OFF _Pragma("GCC pop_options")

#else
# define FAST_MATH_ON
# define FAST_MATH_OFF
#endif
