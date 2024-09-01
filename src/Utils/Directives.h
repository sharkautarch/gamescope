#pragma once

namespace gamescope::Directives {
  struct FlagSwitcher {
    unsigned long long m_csr = 0ull;
    inline FlagSwitcher();
    inline ~FlagSwitcher();
  };
}

#if defined(__x86__) || defined(__x86_64__)
# include <xmmintrin.h>
# include <pmmintrin.h>
# define SET_FAST_MATH_FLAGS gamescope::Directives::FlagSwitcher switcher{};

 inline gamescope::Directives::FlagSwitcher::FlagSwitcher() : m_csr{_mm_getcsr()} {
   _mm_setcsr( (unsigned int)m_csr | _MM_DENORMALS_ZERO_ON | _MM_FLUSH_ZERO_ON );
 }

 inline gamescope::Directives::FlagSwitcher::~FlagSwitcher() {
   _mm_setcsr( (unsigned int)m_csr );
 }

#elif defined(__aarch64__) && __has_builtin(__builtin_aarch64_get_fpcr64) && __has_builtin(__builtin_aarch64_set_fpcr64)
# define SET_FAST_MATH_FLAGS gamescope::Directives::FlagSwitcher switcher{};

 static constexpr unsigned long long fz_bit = 0x1'00'00'00;
 //based on this stuff: https://github.com/DLTcollab/sse2neon/blob/706d3b58025364c2371cafcf9b16e32ff7e630ed/sse2neon.h#L2433
 //and this: https://stackoverflow.com/a/59001820
 static constexpr unsigned long long fz16_bit =  0x8'00'00;

 inline gamescope::Directives::FlagSwitcher::FlagSwitcher() : m_csr{__builtin_aarch64_get_fpcr64()} {
   __builtin_aarch64_set_fpcr64(m_csr | fz_bit | fz16_bit);
 }

 inline gamescope::Directives::FlagSwitcher::~FlagSwitcher() {
   __builtin_aarch64_set_fpcr64(m_csr);
 }

#else
# define SET_FAST_MATH_FLAGS

#endif

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

