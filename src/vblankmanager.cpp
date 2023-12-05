// Try to figure out when vblank is and notify steamcompmgr to render some time before it

#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <algorithm>
#include <cstdlib>
#include <cfloat>
#include <cmath>
#include <numeric>
#include <math.h>

#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

#include "gpuvis_trace_utils.h"

#include "vblankmanager.hpp"
#include "steamcompmgr.hpp"
#include "wlserver.hpp"
#include "main.hpp"
#include "drm.hpp"
#include <iostream>

#if HAVE_OPENVR
#include "vr_session.hpp"
#endif


#include "nominalFrequency.cc"

#include <float.h>
#include <math.h>



/*  credit for NextAfter() function: https://stackoverflow.com/a/70512041
    Return the next floating-point value after the finite value q.

    This was inspired by Algorithm 3.5 in Siegfried M. Rump, Takeshi Ogita, and
    Shin'ichi Oishi, "Accurate Floating-Point Summation", _Technical Report
    05.12_, Faculty for Information and Communication Sciences, Hamburg
    University of Technology, November 13, 2005.
    
    IEEE-754 and the default rounding mode,
    round-to-nearest-ties-to-even, may be required.
*/
double NextAfter(double q)
{
    /*  Scale is .625 ULP, so multiplying it by any significand in [1, 2)
        yields something in [.625 ULP, 1.25 ULP].
    */
    static const double Scale = 0.625 * DBL_EPSILON;

    /*  Either of the following may be used, according to preference and
        performance characteristics.  In either case, use a fused multiply-add
        (fma) to add to q a number that is in [.625 ULP, 1.25 ULP].  When this
        is rounded to the floating-point format, it must produce the next
        number after q.
    */
#if 0
    // SmallestPositive is the smallest positive floating-point number.
    static const double SmallestPositive = DBL_EPSILON * DBL_MIN;

    if (fabs(q) < 2*DBL_MIN)
        return q + SmallestPositive;

    return fma(fabs(q), Scale, q);
#else
    return fma(fmax(fabs(q), DBL_MIN), Scale, q);
#endif
}

//#define VBLANK_DEBUG

static int g_vblankPipe[2];

std::atomic<uint64_t> g_lastVblank;

// 3ms by default -- a good starting value.
const uint64_t g_uStartingDrawTime = 3'000'000;

// This is the last time a draw took.
std::atomic<uint64_t> g_uVblankDrawTimeNS = { g_uStartingDrawTime };

// 1.3ms by default. (g_uDefaultMinVBlankTime)
// This accounts for some time we cannot account for (which (I think) is the drm_commit -> triggering the pageflip)
// It would be nice to make this lower if we can find a way to track that effectively
// Perhaps the missing time is spent elsewhere, but given we track from the pipe write
// to after the return from `drm_commit` -- I am very doubtful.
uint64_t g_uMinVblankTime = g_uDefaultMinVBlankTime;

// Tuneable
// 0.3ms by default. (g_uDefaultVBlankRedZone)
// This is the leeway we always apply to our buffer.
uint64_t g_uVblankDrawBufferRedZoneNS = g_uDefaultVBlankRedZone;

// Tuneable
// 93% by default. (g_uVBlankRateOfDecayPercentage)
// The rate of decay (as a percentage) of the rolling average -> current draw time
uint64_t g_uVBlankRateOfDecayPercentage = g_uDefaultVBlankRateOfDecayPercentage;

const uint64_t g_uVBlankRateOfDecayMax = 1000;

static std::atomic<uint64_t> g_uRollingMaxDrawTime = { g_uStartingDrawTime };

std::atomic<bool> g_bCurrentlyCompositing = { false };



#if defined(__clang__)
inline void __attribute__((always_inline,target("waitpkg"))) cpu_tpause(const uint64_t counter, const bool do_timed_pause)
#else
inline void __attribute__((always_inline,target("waitpkg"))) cpu_tpause(const uint64_t counter, const bool do_timed_pause)
#endif
{
#define _TPAUSE __builtin_ia32_tpause(0, counter)

#if defined(__clang__)
# define _PAUSE _mm_pause()
#else
# define _PAUSE __builtin_ia32_pause()
#endif

if (do_timed_pause)
{
#if !defined(__clang__)			
# if defined(__x86_64__) || defined(__i386__)
	_TPAUSE;
# else
#  if defined(__aarch64__) || defined(__arm__) //GCC doesn't have an intrinsic for aarch64 yield instruction
	asm volatile("yield"); //https://stackoverflow.com/a/70076751
#  elif __has_builtin(__sync_synchronize)
	__sync_synchronize(); //close enough to a pause intrinsic
	__sync_synchronize();
	__sync_synchronize();
#
#else
# if defined(__x86_64__) || defined(__i386__)
	_TPAUSE;
# else
#  if defined(__aarch64__) || defined(__arm__)
	asm volatile("yield"); //https://stackoverflow.com/a/70076751
#  elif __has_builtin(__sync_synchronize)
	__sync_synchronize(); //close enough to a pause intrinsic
	__sync_synchronize();
	__sync_synchronize();
#  endif
# endif
#  endif
# endif	
#endif
}
else
{
#if !defined(__clang__)			
# if defined(__x86_64__) || defined(__i386__)
	_PAUSE;
# else
#  if defined(__aarch64__) || defined(__arm__) //GCC doesn't have an intrinsic for aarch64 yield instruction
	asm volatile("yield"); //https://stackoverflow.com/a/70076751
#  elif __has_builtin(__sync_synchronize)
	__sync_synchronize(); //close enough to a pause intrinsic
	__sync_synchronize();
	__sync_synchronize();
#
#else
# if defined(__x86_64__) || defined(__i386__)
	_PAUSE;
# else
#  if defined(__aarch64__) || defined(__arm__)
	asm volatile("yield"); //https://stackoverflow.com/a/70076751
#  elif __has_builtin(__sync_synchronize)
	__sync_synchronize(); //close enough to a pause intrinsic
	__sync_synchronize();
	__sync_synchronize();
#  endif
# endif
#  endif
# endif	
#endif
}

#ifdef _TPAUSE
# undef _TPAUSE
#endif

#ifdef _PAUSE
# undef _PAUSE
#endif
}

inline void __attribute__((always_inline)) cpu_pause()
{


#if defined(__clang__)
# define _PAUSE _mm_pause()
#else
# define _PAUSE __builtin_ia32_pause()
#endif



#if !defined(__clang__)			
# if defined(__x86_64__) || defined(__i386__)
	_PAUSE;
# else
#  if defined(__aarch64__) || defined(__arm__) //GCC doesn't have an intrinsic for aarch64 yield instruction
	asm volatile("yield"); //https://stackoverflow.com/a/70076751
#  elif __has_builtin(__sync_synchronize)
	__sync_synchronize(); //close enough to a pause intrinsic
	__sync_synchronize();
	__sync_synchronize();
#
#else
# if defined(__x86_64__) || defined(__i386__)
	_PAUSE;
# else
#  if defined(__aarch64__) || defined(__arm__)
	asm volatile("yield"); //https://stackoverflow.com/a/70076751
#  elif __has_builtin(__sync_synchronize)
	__sync_synchronize(); //close enough to a pause intrinsic
	__sync_synchronize();
	__sync_synchronize();
#  endif
# endif
#  endif
# endif	
#endif



#ifdef _PAUSE
# undef _PAUSE
#endif
}

//measures the time it takes to do a single pause instruction -> 512 trials.
//returns the average of the 25% longest run times
int64_t cpu_pause_get_upper_q_avg()
{
	const int num_test_runs=512;
	int64_t trials[num_test_runs];
	
	for (int i = 0; i < num_test_runs; i++)
	{
		int64_t before = get_time_in_nanos();
		cpu_pause();
		trials[i]=get_time_in_nanos()-before;
	}
	
	std::sort(trials, trials+num_test_runs);
	
	uint64_t sum = 0;
	for (int i = 3*num_test_runs/4; i < num_test_runs; i++) {
		sum += (uint64_t)trials[i];
	}
	
	return static_cast<int64_t>(sum/((uint64_t)num_test_runs/4l));
}

int64_t __attribute__((target("avx2,sse4.2,fma,mmx,rdpid,aes,sha,vaes,prfchw,movdir64b,movdiri,pclmul,clflushopt,xsave,xsavec,xsaves,xsaveopt,fsgsbase,movbe,adx,popcnt,vpclmulqdq,waitpkg"))) cpu_tpause_get_possible_max_delay()
{
	const int num_test_runs=512;
	int64_t trials[num_test_runs];
	
	for (int i = 0; i < num_test_runs; i++)
	{
		int64_t before = get_time_in_nanos();
		cpu_tpause(0, 2'000'000ul +  readCycleCount());
		trials[i]=get_time_in_nanos()-before;
	}
	
	std::sort(trials, trials+num_test_runs);
	
	uint64_t sum = 0;
	for (int i = 3*num_test_runs/4; i < num_test_runs; i++) {
		sum += (uint64_t)trials[i];
	}
	
	return static_cast<int64_t>(sum/((uint64_t)num_test_runs/4l));
}

#define HMMM(expr) __builtin_expect_with_probability(expr, 1, .05) //hmmm has slightly higher probability than meh
#define MEH(expr) __builtin_expect_with_probability(expr, 1, .02)
#if defined(__clang__)
inline void __attribute__((always_inline, target("avx2,sse4.2,fma,mmx,rdpid,aes,sha,vaes,prfchw,movdir64b,movdiri,pclmul,clflushopt,xsave,xsavec,xsaves,xsaveopt,fsgsbase,movbe,adx,popcnt,vpclmulqdq,waitpkg"))) spin_wait_w_tpause(const long double nsPerTick_long, const double nsPerTick, const double cpu_pause_time_len, const double compared_to, const int64_t wait_start)
#else
inline void __attribute__((optimize("-fallow-store-data-races","-Os","-falign-functions=32","-falign-jumps", "-falign-loops", "-falign-labels", "-fweb", "-ftree-slp-vectorize", "-fivopts" ,"-ftree-vectorize","-fgcse-sm", "-fgcse-las", "-fgcse-after-reload", "-fsplit-paths","-fsplit-loops","-fipa-pta","-fipa-cp-clone","-fdevirtualize-speculatively"), always_inline, target("avx2,sse4.2,fma,mmx,rdpid,aes,sha,vaes,prfchw,movdir64b,movdiri,pclmul,clflushopt,xsave,xsavec,xsaves,xsaveopt,fsgsbase,movbe,adx,popcnt,vpclmulqdq,waitpkg"))) spin_wait_w_tpause(const long double nsPerTick_long, const double nsPerTick,  const double cpu_pause_time_len, const double compared_to, const int64_t wait_start)
#endif
{
	uint64_t diff;
	double res = 0.0;
	double check_this_first = 0.0;
	long double check_this = 0.0L;
	const double tpause_frac = 
	( cpu_pause_time_len < 1'000'000.0L)
		     ? fmin(compared_to/(nsPerTick_long*2.0L), cpu_pause_time_len)
		     : compared_to/(nsPerTick_long*2.0L);
	const uint64_t compared_int = (uint64_t) floor(tpause_frac);
	const uint64_t __cpu_pause_loop_iter = (uint64_t) floor(compared_to/tpause_frac);
	const uint64_t cpu_pause_loop_iter = (__cpu_pause_loop_iter>1ul)?(__cpu_pause_loop_iter-1ul):(0ul);
	
	
	const int64_t compared_to_const = (int64_t)llround(compared_to);
	
	
	const uint64_t dbl_max_rounded=(uint64_t)floor(DBL_MAX);
	uint64_t prev = readCycleCount();
	#ifdef VBLANK_DEBUG
	
	
	int64_t before = get_time_in_nanos();
	
	#endif
	
	#if defined(__clang__)
	#pragma clang optimize off
	#endif
	
	for (unsigned int i = 1; i < 4*cpu_pause_loop_iter/7; i++)
	{
		
		#if defined(__clang__)
		#pragma clang optimize on
		#endif
		
		
		uint64_t tsc_time_value = readCycleCount() + sqrt(4*i-1)*compared_int/(cpu_pause_loop_iter-2*i*2*i-1);

		
		cpu_tpause((uint64_t)tsc_time_value, true);

		#if defined(__clang__)
		#pragma clang optimize off
		#endif
	}
	#if defined(__clang__)
	#pragma clang optimize on
	#endif
	#ifdef VBLANK_DEBUG
	fprintf(stdout, "total tpause dur: %.2fms\n", (get_time_in_nanos()-before)/ 1'000'000.0);
	#endif
	int i = 0;		
	while ( res < compared_to && get_time_in_nanos() < wait_start + compared_to_const)
	{
		int j=4-i;
		
		while ( res < compared_to && j < 3 )
		{
			j++;
			res = DBL_MAX;
		
			cpu_pause();
		
			diff = readCycleCount() - prev;
			diff = (diff < dbl_max_rounded)*diff + (diff>dbl_max_rounded)*(dbl_max_rounded);
		
			check_this_first = (double)diff * nsPerTick;
			if ( HMMM(isinf(check_this_first)) )
			{
				check_this = (long double)diff * nsPerTick_long;
				if ( MEH(std::fpclassify(check_this) == FP_INFINITE) ) //meh and hmmm: compiler hints that this branch is unlikely to occur
					break;					       //     hopefully might reduce fruitless speculative execution
			}
			
			res = check_this_first;
		}
		
		i++;
	}
	
}

#if defined(__x86_64__)
#	if defined(__clang__)
inline void __attribute__((always_inline)) spin_wait(const long double nsPerTick_long, const double nsPerTick, const int64_t cpu_pause_time_len, const double compared_to, const int64_t wait_start)
#	else
inline void __attribute__((optimize("-fallow-store-data-races","-Os","-falign-functions=32","-falign-jumps", "-falign-loops", "-falign-labels", "-fweb", "-ftree-slp-vectorize", "-fivopts" ,"-ftree-vectorize","-fgcse-sm", "-fgcse-las", "-fgcse-after-reload", "-fsplit-paths","-fsplit-loops","-fipa-pta", "-fipa-cp-clone", "-fdevirtualize-speculatively"), always_inline)) spin_wait(const long double nsPerTick_long, const double nsPerTick, const int64_t cpu_pause_time_len, const double compared_to, const int64_t wait_start)
#	endif
#else
#	if defined(__clang__)
inline void __attribute__((always_inline)) spin_wait(const long double nsPerTick_long, const double nsPerTick, const int64_t cpu_pause_time_len, const double compared_to, const int64_t wait_start)
#	else
inline void __attribute__((optimize("-fallow-store-data-races","-Os","-falign-functions","-falign-jumps", "-falign-loops", "-falign-labels", "-fweb", "-ftree-slp-vectorize", "-fivopts" ,"-ftree-vectorize","-fmerge-all-constants","-fgcse-sm", "-fgcse-las", "-fgcse-after-reload", "-fsplit-paths","-fsplit-loops","-fipa-pta","-fipa-cp-clone","-fdevirtualize-speculatively"), always_inline)) spin_wait(const long double nsPerTick_long, const double nsPerTick, const int64_t cpu_pause_time_len, const double compared_to, const int64_t wait_start)
#	endif
#endif
{
	int64_t diff;
	double res = 0.0;
	double check_this_first = 0.0;
	long double check_this = 0.0L;
	
	const int64_t compared_to_const = (int64_t)llround(compared_to);
	
	const int64_t __cpu_pause_loop_iter = (int64_t) llroundl(compared_to/(2.5L*(long double)cpu_pause_time_len));
	const int64_t cpu_pause_loop_iter = ((__cpu_pause_loop_iter-1l)>0l)?(__cpu_pause_loop_iter-1l):(1l);
	
	uint64_t prev = readCycleCount();
	
	#if defined(__clang__)
	#pragma clang optimize off
	#endif
	for (int64_t k = 0l; k < cpu_pause_loop_iter; k += 2l) {
				cpu_pause();
				cpu_pause();
	}
	#if defined(__clang__)
	#pragma clang optimize on
	#endif
		
	int i = 0;		
	while ( res < compared_to && get_time_in_nanos() < wait_start + compared_to_const)
	{
		int j=4-i;
		
		while ( res < compared_to && j < 3 )
		{
			j++;
			res = DBL_MAX;
		
			cpu_pause();
		
			diff = (int64_t)readCycleCount() - (int64_t)prev;
			if ( HMMM(diff < 0) )
			{
				std::cout << "oh noes\n";
				continue; // in case tsc counter resets or something
			}
		
			check_this_first = (double)diff * nsPerTick;
			if ( HMMM(isinf(check_this_first)) )
			{
				check_this = (long double)diff * nsPerTick_long;
				if ( MEH(std::fpclassify(check_this) == FP_INFINITE) ) //meh and hmmm: compiler hints that this branch is unlikely to occur
					break;					       //     hopefully might reduce fruitless speculative execution
			}
			
			res = check_this_first;
		}
		
		i++;
	}
}



#include <climits>
inline int __attribute__((const, always_inline)) heaviside(const int v)
{
	return 1 ^ ((unsigned int)v >> (sizeof(int) * CHAR_BIT - 1)); //credit: http://www.graphics.stanford.edu/~seander/bithacks.html#CopyIntegerSign
}



// The minimum drawtime to use when we are compositing.
// Getting closer and closer to vblank when compositing means that we can get into
// a feedback loop with our clocks. Pick a sane minimum draw time.
const uint64_t g_uVBlankDrawTimeMinCompositing = 2'400'000;



inline int __attribute__((const)) median(const uint16_t l, const uint16_t r) //credit for this function: https://www.geeksforgeeks.org/interquartile-range-iqr/
{

    int n = (int)r - (int)l + 1;

    n = (n + 1) / 2 - 1;

    return n + l;

}


#define med(a,l,r) median(l,r)

inline long int __attribute__((nonnull(1))) IQM(uint16_t* a, const int n) //credit for this function: https://www.geeksforgeeks.org/interquartile-range-iqr/
{
    std::sort(a, a + n);

    int mid_index = med(a, 0, n);

    uint64_t r1 = (uint64_t)med(a, 0, mid_index);

    uint64_t r3 = (uint64_t)std::min(med(a, mid_index + 1, n), n);
    
    uint64_t sum=0;
    for (uint64_t i = r1; i < r3; i++)
    {
    	sum += ( ((uint64_t) a[i]) * 1000 );
    }
    return static_cast<long int>(sum/(r3 - r1));
}
#undef med

inline __attribute__((always_inline)) void sleep_until_nanos_retrying(const int64_t nanos, const long int offset, const long int sleep_weight)
{
	timespec rem;
	rem.tv_sec = 0l;
	rem.tv_nsec = 0l;
	if (sleep_until_nanos_ext(nanos, true, &rem) == 0 || rem.tv_nsec < offset*sleep_weight / (100ll))
		return;
	timespec rem2;
	for(int i = 0; i < 2; i++)
	{
		rem2.tv_sec = 0l;
		rem2.tv_nsec = 0l;
		
		
		if (sleep_until_nanos_ext(nanos, 2, &rem, &rem2) == 0 || rem2.tv_nsec < offset*sleep_weight / (100ll))
			return;
		
		
		rem.tv_sec = 0l;
		rem.tv_nsec = 0l;
		if (sleep_until_nanos_ext(nanos, 2, &rem2, &rem) == 0 || rem.tv_nsec < offset*sleep_weight / (100ll))
			return;
	}
}


#ifdef __clang__
inline double __attribute__((always_inline, hot )) vblank_next_target(const double _lastVblank, const double offset, const double nsecInterval, const double limitFactor, const double now )
#else
inline double __attribute__((always_inline, const,optimize("-O2","-fallow-store-data-races","-fno-unsafe-math-optimizations","-fno-trapping-math", "-fsplit-paths","-fsplit-loops","-fipa-pta","-ftree-partial-pre","-fira-hoist-pressure","-fdevirtualize-speculatively","-fgcse-after-reload","-fgcse-sm","-fgcse-las"), hot )) vblank_next_target( const double _lastVblank, const double offset, const double nsecInterval, const double limitFactor, const double now )
#endif
{

	#if defined(__clang__)
	#pragma clang fp reassociate(off)
	#pragma clang fp contract(on)
	#endif
	
	double lastVblank = _lastVblank - offset;
        
        
	double targetPoint = lastVblank + nsecInterval;
	
	double dist_to_target = now - targetPoint;
        
        targetPoint = (dist_to_target>0.0) * (nsecInterval*ceil(dist_to_target/(nsecInterval)))
	       + targetPoint;
	
	double relativePoint = targetPoint - now;
	
	double cappedTargetPoint = fmin( NextAfter(nsecInterval*limitFactor), relativePoint) + now;
	
	return cappedTargetPoint;
}
 

#ifdef __clang__
inline double __attribute__((always_inline, hot )) vblank_next_target_reloaded(const double _lastVblank, const double offset, const double nsecInterval, const double limitFactor, const double prev, const double now, const double _lastVblank_pending )
#else
inline double __attribute__((always_inline, const,optimize("-O2","-fallow-store-data-races","-fno-unsafe-math-optimizations","-fno-trapping-math", "-fsplit-paths","-fsplit-loops","-fipa-pta","-ftree-partial-pre","-fira-hoist-pressure","-fdevirtualize-speculatively","-fgcse-after-reload","-fgcse-sm","-fgcse-las"), hot )) vblank_next_target_reloaded( const double _lastVblank, const double offset, const double nsecInterval, const double limitFactor, const double prev, const double now, const double _lastVblank_pending )
#endif
{
	const double vblank_diff = _lastVblank_pending-_lastVblank;
	const double cond_time_diff = fmin(vblank_diff,1.0)*(now-prev); // cond_time_diff should always be >= 0 because the time values are obtained from the CLOCK_MONOTONIC clock
	const double targetPoint = vblank_next_target(_lastVblank+vblank_diff, offset, nsecInterval, limitFactor, prev+cond_time_diff);
	return targetPoint;
}


#define CLEANUP 3
#ifdef __clang__
static inline void  __attribute__((always_inline, hot)) vrr_vblank(uint8_t * sleep_cycle, long int * offset, double * offset_dec_real, uint32_t * counter )
#else
static inline void  __attribute__((always_inline, optimize("-O2","-fallow-store-data-races","-fno-unsafe-math-optimizations","-fno-trapping-math", "-fsplit-paths","-fsplit-loops","-fipa-pta","-ftree-partial-pre","-fira-hoist-pressure","-fdevirtualize-speculatively","-fgcse-after-reload","-fgcse-sm","-fgcse-las"), hot )) 
vrr_vblank(uint8_t * sleep_cycle, long int * offset, double * offset_dec_real, uint32_t * counter )
#endif
{
	const int refresh = g_nNestedRefresh ? g_nNestedRefresh : g_nOutputRefresh;
	// The redzone is relative to 60Hz, scale it by our
	// target refresh so we don't miss submitting for vblank in DRM.
	// (This fixes 4K@30Hz screens)
	const long int nsecToSec = 1'000'000'000l;
	const drm_screen_type screen_type = drm_get_screen_type( &g_DRM );
	const long int redZone = screen_type == DRM_SCREEN_TYPE_INTERNAL
		? g_uVblankDrawBufferRedZoneNS
		: ( g_uVblankDrawBufferRedZoneNS * 60 * nsecToSec ) / ( refresh * nsecToSec );
	
	
	// VRR:
	// Just ensure that if we missed a frame due to already
	// having a page flip in-flight, that we flush it out with this.
	// Nothing fancy needed, just need to get on the other side of the page flip.
	//
	// We don't use any of the rolling times due to them varying given our
	// 'vblank' time is varying.
	static int64_t time_start = get_time_in_nanos();

	if (*sleep_cycle == CLEANUP)
	{
		uint64_t this_time=(get_time_in_nanos() - time_start)/1'000'000'000ul;
		if ( this_time > 5)
		{
			std::cout << *counter << " vblanks sent in " << this_time << " seconds\n";
			time_start=get_time_in_nanos();
			*counter=0;
		}
		return;
	}
	
	*offset = 1'000'000 + redZone;
	* offset_dec_real = (double)*offset;
}
#ifdef __clang__
static inline void  __attribute__((always_inline, hot)) 

none_vrr_vblank(uint8_t * sleep_cycle, long int * offset, double * offset_dec_real, uint32_t * counter )
#else
static inline void  __attribute__((always_inline, optimize("-O2","-fallow-store-data-races","-fno-unsafe-math-optimizations","-fno-trapping-math", "-fsplit-paths","-fsplit-loops","-fipa-pta","-ftree-partial-pre","-fira-hoist-pressure","-fdevirtualize-speculatively","-fgcse-after-reload","-fgcse-sm","-fgcse-las"), hot )) 

none_vrr_vblank(uint8_t * sleep_cycle, long int * offset, double * offset_dec_real, uint32_t * counter )
#endif
{
	static uint64_t rollingMaxDrawTime = g_uStartingDrawTime; //this will be used to determine the times this thread should sleep for
	static uint64_t rollingMaxDrawTime_real = g_uStartingDrawTime; //this will be used for the values of the VBlankTimeInfo_t that is sent out
	const uint64_t range = g_uVBlankRateOfDecayMax;
	
	const uint64_t max_delta_apply = 50'000; //.05ms per sec
	// ^ For non-vrr, base rate of change in rollingMaxDeltaTime 
	
	
	
	static int64_t time_start = get_time_in_nanos();

	
	static long int drawTime = g_uVblankDrawTimeNS.load();
	
	static long int lastDrawTime = g_uVblankDrawTimeNS;
	
	static long double drawTimeTime = get_time_in_nanos();
	static long double lastDrawTimeTime = get_time_in_nanos(); 
	
	static float delta_trend_counter = 3.0f;
	
		
	static long int centered_mean = 1'000'000'000l / (long int) (g_nNestedRefresh ? g_nNestedRefresh : g_nOutputRefresh);
	static uint16_t drawtimes[64] = {1};
	static uint16_t drawtimes_pending[64];
	
	
	
	
	static long double real_delta = 0.0L;
	static long double last_real_delta = 0.0L; 
	static long double local_min = g_uVBlankDrawTimeMinCompositing;
	static long double local_max = (long double)2*centered_mean + g_uVBlankDrawTimeMinCompositing;
	static long double lastRollingMaxDrawTime = (long double)centered_mean/2;
	
	
	
	const int refresh = g_nNestedRefresh ? g_nNestedRefresh : g_nOutputRefresh;
	const long int nsecInterval = 1'000'000'000l / refresh;
	
	const uint64_t alpha = g_uVBlankRateOfDecayPercentage;
	
	const double nsecInterval_dec = (double)nsecInterval;
	
	// The redzone is relative to 60Hz, scale it by our
	// target refresh so we don't miss submitting for vblank in DRM.
	// (This fixes 4K@30Hz screens)
	const long int nsecToSec = 1'000'000'000l;
	const drm_screen_type screen_type = drm_get_screen_type( &g_DRM );
	const long int redZone = screen_type == DRM_SCREEN_TYPE_INTERNAL
		? g_uVblankDrawBufferRedZoneNS
		: ( g_uVblankDrawBufferRedZoneNS * 60 * nsecToSec ) / ( refresh * nsecToSec );
	
	
		
	static int index=0;
	
	if (*sleep_cycle == CLEANUP)
	{
		lastDrawTime=drawTime;
		lastDrawTimeTime=drawTimeTime;
		lastRollingMaxDrawTime=(long double) rollingMaxDrawTime;
		last_real_delta=real_delta;
		uint64_t this_time=(get_time_in_nanos() - time_start)/1'000'000'000ul;
		if ( this_time > 5)
		{
			std::cout << *counter << " vblanks sent in " << this_time << " seconds\n";
			time_start=get_time_in_nanos();
			*counter=0;
		}
		return;
	}
	
	
	if (*sleep_cycle == 1)
	{
		drawTime = g_uVblankDrawTimeNS.load();	
		drawTimeTime = (long double)get_time_in_nanos();
	}
	
	
	if ( *sleep_cycle == 1 && g_bCurrentlyCompositing )
		drawTime = fmax(drawTime, g_uVBlankDrawTimeMinCompositing);
	if (*sleep_cycle == 1)
		drawtimes_pending[index] = (uint16_t)( drawTime /1000 );
	
	if (*sleep_cycle == 1)
	{
		if ( int64_t(drawTime) - int64_t(redZone / 2) > int64_t(rollingMaxDrawTime_real) )
			rollingMaxDrawTime_real = drawTime;
		else
			rollingMaxDrawTime_real = ( ( alpha * rollingMaxDrawTime_real ) + ( range - alpha ) * drawTime ) / range;
		rollingMaxDrawTime_real = std::min( rollingMaxDrawTime_real, static_cast<uint64_t>(nsecInterval - redZone) );

	}
	
	if (*sleep_cycle == 2)
	{	
		rollingMaxDrawTime = ( ( alpha * rollingMaxDrawTime ) + ( range - alpha ) * drawTime ) / (range);
		g_uRollingMaxDrawTime.store(rollingMaxDrawTime, std::memory_order_relaxed);
	}
	else if (*sleep_cycle == 1)
	{
		real_delta = ((long double)drawTime - (long double)lastDrawTime)/(drawTimeTime - lastDrawTimeTime);
		if ( (double)real_delta < 0.0 || (drawTime < centered_mean && (double)drawTime-(double)g_uVBlankDrawTimeMinCompositing/2.0 <= (double)local_min) )
		{
			if ((double)last_real_delta >= 0.0)
				delta_trend_counter=1.25f;
			
			local_min=(long double)drawTime+(long double)g_uVBlankDrawTimeMinCompositing/4.0L;
			real_delta = -fmaxl( fabsl(real_delta), fabsl( ((long double)drawTime - (long double)rollingMaxDrawTime)/(drawTimeTime - lastDrawTimeTime)));
		}
		
		if ((double)real_delta >= 0.0 || (drawTime >= centered_mean && (double)drawTime+(double)g_uVBlankDrawTimeMinCompositing >= (double)local_max) )
		{
			if ((double)last_real_delta <= 0.0)
				delta_trend_counter=1.25f;
				
			local_max=fmaxl((long double)drawTime-(long double)g_uVBlankDrawTimeMinCompositing/2.0L, local_min);
		}
		
		
		if (*counter % 300 == 0) {
			std::cout << "rollingMaxDrawTime: " << rollingMaxDrawTime << "\n";
		} 
	}
	if ((double)real_delta < 0.0)
	{
		if ((double)last_real_delta < 0.0)
			delta_trend_counter+=1.25f;
		else
			delta_trend_counter=1.25f;
		
		rollingMaxDrawTime=(uint64_t) llroundl(fmaxl((long double)rollingMaxDrawTime, lastRollingMaxDrawTime-fminl(fabsl(real_delta)*nsecInterval_dec, fmax(logf(delta_trend_counter), 4.0)*max_delta_apply)));
	}
	
	if ((double)real_delta >= 0.0)
	{
		if ((double)last_real_delta >= 0.0)
			delta_trend_counter+=1.25f;
		else
			delta_trend_counter=1.25f;
		
		rollingMaxDrawTime=(uint64_t) llroundl(fminl((long double)rollingMaxDrawTime, lastRollingMaxDrawTime+fminl(fabsl(real_delta)*nsecInterval_dec, fmax(logf(delta_trend_counter), 4.0)*max_delta_apply)));
	}
	long int half_centered_mean = std::min(centered_mean/2, nsecInterval);
	rollingMaxDrawTime = (uint64_t)std::clamp( (long int) rollingMaxDrawTime, half_centered_mean, nsecInterval + nsecInterval/10);
	if (*counter % 300 == 0) 
		std::cout << "rollingMaxDrawTime after using std::clamp: " << rollingMaxDrawTime << "\n";
	
	
	*offset = rollingMaxDrawTime + redZone;
	*offset_dec_real = rollingMaxDrawTime_real + redZone;
	if (*sleep_cycle == 2)
	{
		half_centered_mean = std::min(std::min(nsecInterval, centered_mean)/2-nsecInterval/25, nsecInterval+nsecInterval/20);
		*offset = std::clamp(*offset, half_centered_mean, nsecInterval+nsecInterval/20);
	}
	else if (*sleep_cycle == 1)
	{
		half_centered_mean = std::min(std::min(nsecInterval, centered_mean)/2-nsecInterval/20, nsecInterval+nsecInterval/5);
		*offset = std::clamp(*offset, half_centered_mean, nsecInterval+nsecInterval/5);
	}
		
	if (*counter % 300 == 0) 
		std::cout << "offset: " << *offset << " sleep_cycle: "<< *sleep_cycle << "\n";	
	
	
	if ( *sleep_cycle == 1 && index >= 64 )
	{
		const uint16_t n = 64; 
		memcpy(drawtimes, drawtimes_pending, n * sizeof(drawtimes_pending[0]));
		index=0;
		
		centered_mean = (centered_mean + std::clamp(IQM(drawtimes, n), nsecInterval/2, (5*nsecInterval)/3))/2;
	}
	else
		index++;
	
}


#ifdef __clang__
#	if defined(__x86_64__)

	void __attribute__((target_clones("default,arch=core2,arch=znver1,arch=znver2,arch=westmere,arch=skylake") )) 
	
	vblankThreadRun( const bool neverBusyWait, const bool alwaysBusyWait, const int64_t cpu_pause_time_len, const long double nsPerTick_long  )
#	else

void __attribute__((hot, flatten )) 
vblankThreadRun( const bool neverBusyWait, const bool alwaysBusyWait, const int64_t cpu_pause_time_len, const long double nsPerTick_long  )
#	endif
#else
#	if defined(__x86_64__)

void __attribute__((optimize("-O2","-fno-unsafe-math-optimizations","-fno-trapping-math", "-fsplit-paths","-fsplit-loops","-fipa-pta","-ftree-partial-pre","-fira-hoist-pressure","-fdevirtualize-speculatively","-fgcse-after-reload","-fgcse-sm","-fgcse-las"), hot, flatten, target_clones("default,arch=core2,arch=znver1,arch=znver2,arch=westmere,arch=skylake") )) 
vblankThreadRun( const bool neverBusyWait, const bool alwaysBusyWait, const int64_t cpu_pause_time_len, const long double nsPerTick_long  )
#	else

	void __attribute__((optimize("-O2","-fno-unsafe-math-optimizations","-fno-trapping-math", "-fsplit-paths","-fsplit-loops","-fipa-pta","-ftree-partial-pre","-fira-hoist-pressure","-fdevirtualize-speculatively","-fgcse-after-reload","-fgcse-sm","-fgcse-las"), hot, flatten )) 
	
	vblankThreadRun( const bool neverBusyWait, const bool alwaysBusyWait, const int64_t cpu_pause_time_len, const long double nsPerTick_long  )
#	endif
#endif
{
	#if defined(__clang__)
	#pragma clang fp reassociate(off)
	#pragma clang fp contract(on)
	#endif
	
	pthread_setname_np( pthread_self(), "gamescope-vblk" );
	
	const double nsPerTick = (double) nsPerTick_long;
	std::cout << "nsPerTick: " << nsPerTick << "\n";
	
	
	double vblank_begin=0.0;
	
	uint32_t counter = 0;
	
	
	double first_cycle_sleep_duration = 0.0;
	
	
	const double targetPoint_max_percent_of_refresh_vblank_waiting = 0.90; //limits how much longer vblankmanager waits before submitting a vblank
	
	const double targetPoint_max_percent_of_refresh_vsync_value = 2.0; //Don't confuse this variable with the one above.
	// ^ this limits how much we stretch out steamcompmanager's vsync in order to line up with the past vblank time reported by steamcompmanager
	
	const double offset_max_percent_of_refresh_vblank_waiting = 0.85;
	//^ similar to targetPoint_max_percent_of_refresh_vblank_waiting
	
	double lastVblank = (double)g_lastVblank.load();
	double lastOffset = (double)(1'000'000'000l / (long int) (g_nNestedRefresh ? g_nNestedRefresh : g_nOutputRefresh));
	
	
	const long int sleep_weights[2] = {60, 40};
	
	uint8_t sleep_cycle = 0;	
	while ( true )
	{
		sleep_cycle++;
		if (sleep_cycle == 1)
		{
			vblank_begin=(double)get_time_in_nanos();
			lastVblank = (double)g_lastVblank.load();
		}
		const int refresh = g_nNestedRefresh ? g_nNestedRefresh : g_nOutputRefresh;
		const long int nsecInterval = 1'000'000'000l / refresh;
		const double nsecInterval_dec = (double)nsecInterval;
		
		// The redzone is relative to 60Hz, scale it by our
		// target refresh so we don't miss submitting for vblank in DRM.
		// (This fixes 4K@30Hz screens)
		const long int nsecToSec = 1'000'000'000l;
		const drm_screen_type screen_type = drm_get_screen_type( &g_DRM );
		const long int redZone = screen_type == DRM_SCREEN_TYPE_INTERNAL
			? g_uVblankDrawBufferRedZoneNS
			: ( g_uVblankDrawBufferRedZoneNS * 60 * nsecToSec ) / ( refresh * nsecToSec );
		const double vblank_adj_factor = 60.0 / ( (double)((std::max(refresh,g_nOutputRefresh))) );
	
	
	
		
	 	long int offset;
		double offset_dec_real = (double)redZone;
		
		bool bVRR = drm_get_vrr_in_use( &g_DRM );
		if ( !bVRR )
			none_vrr_vblank(&sleep_cycle, &offset, &offset_dec_real, &counter);
		else
			vrr_vblank(&sleep_cycle, &offset, &offset_dec_real, &counter);
			
		double offset_dec=(double)offset;
		const double offset_dec_capped = fmin(nsecInterval_dec*offset_max_percent_of_refresh_vblank_waiting, offset_dec);
		

#ifdef VBLANK_DEBUG
		// Debug stuff for logging missed vblanks
		static uint64_t vblankIdx = 0;
		
		//static uint64_t lastOffset = g_uVblankDrawTimeNS + redZone;

		if ( sleep_cycle == 2 && (vblankIdx++ % 300 == 0 || drawTime > lround(lastOffset) ))
		{
			if ( drawTime > (int)lastOffset )
				fprintf( stderr, " !! missed vblank " );

			fprintf( stderr, "redZone: %.2fms decayRate: %lu%% - rollingMaxDrawTime: %.2fms lastDrawTime: %.2fms lastOffset: %.2fms - drawTime: %.2fms offset: %.2fms\n",
				redZone / 1'000'000.0,
				g_uVBlankRateOfDecayPercentage,
				rollingMaxDrawTime / 1'000'000.0,
				lastDrawTime / 1'000'000.0,
				lastOffset / 1'000'000.0,
				drawTime / 1'000'000.0,
				offset / 1'000'000.0 );
		}


#endif
		int64_t targetPoint;


		if ( !neverBusyWait && ( alwaysBusyWait || sleep_cycle == 2 || offset*sleep_weights[sleep_cycle-1] / 100 < 1'000'000ll ) )
		{
			
			
			if (counter % 300 == 0)
				std::cout << "vblank cycle time before second wait: " << ( (long double)get_time_in_nanos()-vblank_begin )/1'000'000.0L << "ms\n";
			
			
			double compared_to;
			
			if (sleep_cycle == 2)
			{
				compared_to = vblank_next_target( lastVblank, offset_dec_capped*sleep_weights[sleep_cycle-1] / 100, nsecInterval_dec, targetPoint_max_percent_of_refresh_vblank_waiting, vblank_begin)  - vblank_begin;
				compared_to = fmax(compared_to - first_cycle_sleep_duration, offset_dec_capped - first_cycle_sleep_duration);
			}
			else
				compared_to = vblank_next_target( lastVblank, offset_dec_capped*sleep_weights[sleep_cycle-1] / 100, nsecInterval_dec, targetPoint_max_percent_of_refresh_vblank_waiting, vblank_begin) - vblank_begin;
			const int64_t wait_start = get_time_in_nanos();
			
			spin_wait(nsPerTick_long, nsPerTick, cpu_pause_time_len, compared_to, wait_start);
			
			if (sleep_cycle == 1)
				first_cycle_sleep_duration=(double)get_time_in_nanos() - vblank_begin;
		}
		else
		{
			if (counter % 300 == 0) {
				if (sleep_cycle == 1)
					std::cout << "vblank cycle time before first sleep: " << ( (long double)get_time_in_nanos()-vblank_begin )/1'000'000.0L << "ms\n";
				else
					std::cout << "vblank cycle time before second sleep: " << ( (long double)get_time_in_nanos()-vblank_begin )/1'000'000.0L << "ms\n";
			}
			double now = (double)get_time_in_nanos();
			if (sleep_cycle == 1)
			{
				targetPoint = (uint64_t)llround(fmax( now + offset_dec_capped*sleep_weights[sleep_cycle-1] / 100, now +  (vblank_next_target(lastVblank,  offset_dec_capped*sleep_weights[sleep_cycle-1] / (100ll), nsecInterval_dec, targetPoint_max_percent_of_refresh_vblank_waiting, vblank_begin)-vblank_begin) * sleep_weights[sleep_cycle-1] / 100 ));
				sleep_until_nanos_retrying( targetPoint, offset, (int32_t) sleep_weights[1]); 
				
				now = (double)get_time_in_nanos();
				first_cycle_sleep_duration = now - vblank_begin;
			
				if ( now - vblank_begin > fmax(  offset_dec_capped,  lastOffset) )
				{
					offset_dec=fmin(fmax(  offset_dec, lastOffset), nsecInterval_dec+(double)redZone/2.0);
					goto SKIPPING_SECOND_SLEEP;
				}
			}
			else
			{
				targetPoint = lround(vblank_next_target(lastVblank, offset_dec_capped*sleep_weights[sleep_cycle-1] / 100, nsecInterval_dec, targetPoint_max_percent_of_refresh_vblank_waiting,  vblank_begin));
				targetPoint = now + fmax(targetPoint - vblank_begin - first_cycle_sleep_duration, offset_dec_capped - first_cycle_sleep_duration);
				sleep_until_nanos( targetPoint );
			}
				
			
			
		}
		
		if (sleep_cycle == 1)
		{
			continue;
		}
		
	SKIPPING_SECOND_SLEEP:;
		double now = (double) get_time_in_nanos();
		double lastVblank_pending = (double)g_lastVblank.load();
		targetPoint = vblank_next_target_reloaded(lastVblank, offset_dec_real, nsecInterval_dec, targetPoint_max_percent_of_refresh_vsync_value, vblank_begin, now, lastVblank_pending);
		VBlankTimeInfo_t time_info =
		{
			.target_vblank_time = static_cast<uint64_t>(targetPoint + lround(offset_dec_real)),
			.pipe_write_time    = static_cast<uint64_t>(now),
		};
		if (counter % 300 == 0)
			std::cout << "vblank cycle time before write(): " << ( (long double)(get_time_in_nanos()-vblank_begin) )/1'000'000.0L << "ms\n";
		ssize_t ret = write( g_vblankPipe[ 1 ], &time_info, sizeof( time_info ) );
		if ( ret <= 0 )
		{
			perror( "vblankmanager: write failed" );
		}
		else
		{
			gpuvis_trace_printf( "sent vblank" );
			if (counter % 300 == 0)
				std::cout << "vblank cycle time after write(): " << ( (long double)(get_time_in_nanos()-vblank_begin) )/1'000'000.0L << "ms\n";
			counter++;
		}
		
		
		
		
		sleep_cycle=CLEANUP;
		if ( !bVRR )
			none_vrr_vblank(&sleep_cycle, &offset, &offset_dec_real, &counter);
		else
			vrr_vblank(&sleep_cycle, &offset, &offset_dec_real, &counter);
		
		
		const uint64_t adjusted_extra_sleep = (uint64_t)llroundl(1'000'000.0*std::sqrt(vblank_adj_factor));
		
		//ensure we don't wait longer post-vblank, if waiting longer could cause us to send vblanks out at a lower rate than the refresh rate:
		const double time_elapsed_since_vblank_sent = (double)get_time_in_nanos() - vblank_begin;
		const double post_vblank_idle_time = nsecInterval_dec - time_elapsed_since_vblank_sent;
		
		const bool skip_post_vblank_idle = (post_vblank_idle_time<=0);  
		// Get on the other side of it now
		if ( !skip_post_vblank_idle && !alwaysBusyWait && neverBusyWait )
		{
			const uint64_t post_vblank_idle_time_int = (uint64_t)llround(post_vblank_idle_time);
			sleep_until_nanos_retrying( get_time_in_nanos() + std::min(offset + adjusted_extra_sleep, post_vblank_idle_time_int), offset, (int32_t) sleep_weights[1] );
		}
		else if (!skip_post_vblank_idle)
		{
			double compared_to = (double) ( offset + adjusted_extra_sleep);
			compared_to = fmin(compared_to, post_vblank_idle_time);
			const int64_t wait_start = get_time_in_nanos();
			
			spin_wait(nsPerTick_long, nsPerTick, cpu_pause_time_len, compared_to, wait_start);
			
			if (counter % 300 == 0)
				std::cout << "post-vblank TPAUSE wait loop duration: " << (double)((int64_t) get_time_in_nanos() - wait_start)/1'000'000.0L << "ms\n";
			
		}
		
		if (counter % 300 == 0)
			std::cout << "total vblank period: " << (double)( get_time_in_nanos() - vblank_begin)/1'000'000.0L << "ms\n";
			
		
		sleep_cycle=0;
		
		vblank_begin=0;
		lastOffset = offset_dec;
		
	}
}

#ifdef __clang__
#	if defined(__x86_64__)
	void __attribute__((target("avx2,sse4.2,fma,mmx,rdpid,aes,sha,vaes,prfchw,movdir64b,movdiri,pclmul,clflushopt,xsave,xsavec,xsaves,xsaveopt,fsgsbase,movbe,adx,popcnt,vpclmulqdq,waitpkg") ))
	
vblankThreadRun_tpause( const bool neverBusyWait, const bool alwaysBusyWait, const double cpu_pause_time_len, const long double nsPerTick_long  )
#	endif
#else
#	if defined(__x86_64__)
void __attribute__((optimize("-O2","-fno-unsafe-math-optimizations","-fno-trapping-math", "-fsplit-paths","-fsplit-loops","-fipa-pta","-ftree-partial-pre","-fira-hoist-pressure","-fdevirtualize-speculatively","-fgcse-after-reload","-fgcse-sm","-fgcse-las"), hot, flatten, target("avx2,sse4.2,fma,mmx,rdpid,aes,sha,vaes,prfchw,movdir64b,movdiri,pclmul,clflushopt,xsave,xsavec,xsaves,xsaveopt,fsgsbase,movbe,adx,popcnt,vpclmulqdq,waitpkg") )) 

vblankThreadRun_tpause( const bool neverBusyWait, const bool alwaysBusyWait, const double cpu_pause_time_len, const long double nsPerTick_long  )
#	endif
#endif
{
	#if defined(__clang__)
	#pragma clang fp reassociate(off)
	#pragma clang fp contract(on)
	#endif
	
	pthread_setname_np( pthread_self(), "gamescope-vblk" );
	
	const double nsPerTick = (double) nsPerTick_long;
	std::cout << "nsPerTick: " << nsPerTick << "\n";
	
	
	double vblank_begin=0.0;
	
	
	uint32_t counter = 0;
	
	
	
	double first_cycle_sleep_duration = 0.0;
	
	
	const double targetPoint_max_percent_of_refresh_vblank_waiting = 0.90; //limits how much longer vblankmanager waits before submitting a vblank
	
	const double targetPoint_max_percent_of_refresh_vsync_value = 2.0; //Don't confuse this variable with the one above.
	// ^ this limits how much we stretch out steamcompmanager's vsync in order to line up with the past vblank time reported by steamcompmanager
	
	const double offset_max_percent_of_refresh_vblank_waiting = 0.85;
	//^ similar to targetPoint_max_percent_of_refresh_vblank_waiting
	
	double lastVblank = (double)g_lastVblank.load();
	double lastOffset = (double)(1'000'000'000l / (long int) (g_nNestedRefresh ? g_nNestedRefresh : g_nOutputRefresh));
	
	
	const long int sleep_weights[2] = {60, 40};
	
	uint8_t sleep_cycle = 0;	
	while ( true )
	{
		sleep_cycle++;
		if (sleep_cycle == 1)
		{
			vblank_begin=(double)get_time_in_nanos();
			lastVblank = (double)g_lastVblank.load();
		}
		const int refresh = g_nNestedRefresh ? g_nNestedRefresh : g_nOutputRefresh;
		const long int nsecInterval = 1'000'000'000l / refresh;
		const double nsecInterval_dec = (double)nsecInterval;
		// The redzone is relative to 60Hz, scale it by our
		// target refresh so we don't miss submitting for vblank in DRM.
		// (This fixes 4K@30Hz screens)
		const long int nsecToSec = 1'000'000'000l;
		const drm_screen_type screen_type = drm_get_screen_type( &g_DRM );
		const long int redZone = screen_type == DRM_SCREEN_TYPE_INTERNAL
			? g_uVblankDrawBufferRedZoneNS
			: ( g_uVblankDrawBufferRedZoneNS * 60 * nsecToSec ) / ( refresh * nsecToSec );
		const double vblank_adj_factor = 60.0 / ( (double)((std::max(refresh,g_nOutputRefresh))) );
	
		
		// The redzone is relative to 60Hz, scale it by our
		// target refresh so we don't miss submitting for vblank in DRM.
		// (This fixes 4K@30Hz screens)
		
	 	long int offset;
		double offset_dec_real = (double)redZone;
		
		bool bVRR = drm_get_vrr_in_use( &g_DRM );
		if ( !bVRR )
			none_vrr_vblank(&sleep_cycle, &offset, &offset_dec_real, &counter);
		else
			vrr_vblank(&sleep_cycle, &offset, &offset_dec_real, &counter);
			
		double offset_dec=(double)offset;
		const double offset_dec_capped = fmin(nsecInterval_dec*offset_max_percent_of_refresh_vblank_waiting, offset_dec);
		

#ifdef VBLANK_DEBUG
		// Debug stuff for logging missed vblanks
		static uint64_t vblankIdx = 0;
		
		//static uint64_t lastOffset = g_uVblankDrawTimeNS + redZone;

		if ( sleep_cycle == 2 && (vblankIdx++ % 300 == 0 || drawTime > lround(lastOffset) ))
		{
			if ( drawTime > (int)lastOffset )
				fprintf( stderr, " !! missed vblank " );

			fprintf( stderr, "redZone: %.2fms decayRate: %lu%% - rollingMaxDrawTime: %.2fms lastDrawTime: %.2fms lastOffset: %.2fms - drawTime: %.2fms offset: %.2fms\n",
				redZone / 1'000'000.0,
				g_uVBlankRateOfDecayPercentage,
				rollingMaxDrawTime / 1'000'000.0,
				lastDrawTime / 1'000'000.0,
				lastOffset / 1'000'000.0,
				drawTime / 1'000'000.0,
				offset / 1'000'000.0 );
		}


#endif
		int64_t targetPoint;


		if ( !neverBusyWait && ( alwaysBusyWait || sleep_cycle == 2 || offset*sleep_weights[sleep_cycle-1] / 100 < 1'000'000ll ) )
		{
			
			
			if (counter % 300 == 0)
				std::cout << "vblank cycle time before second wait: " << ( (long double)get_time_in_nanos()-vblank_begin )/1'000'000.0L << "ms\n";
			
			
			double compared_to;
			
			if (sleep_cycle == 2)
			{
				compared_to = vblank_next_target( lastVblank, offset_dec_capped*sleep_weights[sleep_cycle-1] / 100, nsecInterval_dec, targetPoint_max_percent_of_refresh_vblank_waiting, vblank_begin)  - vblank_begin;
				compared_to = fmax(compared_to - first_cycle_sleep_duration, offset_dec_capped - first_cycle_sleep_duration);
			}
			else
				compared_to = vblank_next_target( lastVblank, offset_dec_capped*sleep_weights[sleep_cycle-1] / 100, nsecInterval_dec, targetPoint_max_percent_of_refresh_vblank_waiting, vblank_begin) - vblank_begin;
			const int64_t wait_start = get_time_in_nanos();
			
			spin_wait_w_tpause(nsPerTick_long, nsPerTick, cpu_pause_time_len, compared_to, wait_start);
						
			if (sleep_cycle == 1)
				first_cycle_sleep_duration=(double)get_time_in_nanos() - vblank_begin;
		}
		else
		{
			if (counter % 300 == 0) {
				if (sleep_cycle == 1)
					std::cout << "vblank cycle time before first sleep: " << ( (long double)get_time_in_nanos()-vblank_begin )/1'000'000.0L << "ms\n";
				else
					std::cout << "vblank cycle time before second sleep: " << ( (long double)get_time_in_nanos()-vblank_begin )/1'000'000.0L << "ms\n";
			}
			double now = (double)get_time_in_nanos();
			if (sleep_cycle == 1)
			{
				targetPoint = (uint64_t)llround(fmax( now + offset_dec_capped*sleep_weights[sleep_cycle-1] / 100, now +  (vblank_next_target(lastVblank,  offset_dec_capped*sleep_weights[sleep_cycle-1] / (100ll), nsecInterval_dec, targetPoint_max_percent_of_refresh_vblank_waiting, vblank_begin)-vblank_begin) * sleep_weights[sleep_cycle-1] / 100 ));
				sleep_until_nanos_retrying( targetPoint, offset, (int32_t) sleep_weights[1]); 
				
				now = (double)get_time_in_nanos();
				first_cycle_sleep_duration = now - vblank_begin;
			
				if ( now - vblank_begin > fmax(  offset_dec_capped,  lastOffset) )
				{
					offset_dec=fmin(fmax(  offset_dec, lastOffset), nsecInterval_dec+(double)redZone/2.0);
					goto SKIPPING_SECOND_SLEEP;
				}
			}
			else
			{
				targetPoint = lround(vblank_next_target(lastVblank, offset_dec_capped*sleep_weights[sleep_cycle-1] / 100, nsecInterval_dec, targetPoint_max_percent_of_refresh_vblank_waiting,  vblank_begin));
				targetPoint = now + fmax(targetPoint - vblank_begin - first_cycle_sleep_duration, offset_dec_capped - first_cycle_sleep_duration);
				sleep_until_nanos( targetPoint );
			}
			
		}
		
		if (sleep_cycle == 1)
		{
			continue;
		}
		
	SKIPPING_SECOND_SLEEP:;
		double now = (double) get_time_in_nanos();
		double lastVblank_pending = (double)g_lastVblank.load();
		targetPoint = vblank_next_target_reloaded(lastVblank, offset_dec_real, nsecInterval_dec, targetPoint_max_percent_of_refresh_vsync_value, vblank_begin, now, lastVblank_pending);
		VBlankTimeInfo_t time_info =
		{
			.target_vblank_time = static_cast<uint64_t>(targetPoint + lround(offset_dec_real)),
			.pipe_write_time    = static_cast<uint64_t>(now),
		};
		if (counter % 300 == 0)
			std::cout << "vblank cycle time before write(): " << ( (long double)(get_time_in_nanos()-vblank_begin) )/1'000'000.0L << "ms\n";
		ssize_t ret = write( g_vblankPipe[ 1 ], &time_info, sizeof( time_info ) );
		if ( ret <= 0 )
		{
			perror( "vblankmanager: write failed" );
		}
		else
		{
			gpuvis_trace_printf( "sent vblank" );
			if (counter % 300 == 0)
				std::cout << "vblank cycle time after write(): " << ( (long double)(get_time_in_nanos()-vblank_begin) )/1'000'000.0L << "ms\n";
			counter++;
		}
		
		sleep_cycle=CLEANUP;
		if ( !bVRR )
			none_vrr_vblank(&sleep_cycle, &offset, &offset_dec_real, &counter);
		else
			vrr_vblank(&sleep_cycle, &offset, &offset_dec_real, &counter);
		
		const uint64_t adjusted_extra_sleep = (uint64_t)llroundl(1'000'000.0*std::sqrt(vblank_adj_factor));
		
		//ensure we don't wait longer post-vblank, if waiting longer could cause us to send vblanks out at a lower rate than the refresh rate:
		const double time_elapsed_since_vblank_sent = (double)get_time_in_nanos() - vblank_begin;
		const double post_vblank_idle_time = nsecInterval_dec - time_elapsed_since_vblank_sent;
		
		const bool skip_post_vblank_idle = (post_vblank_idle_time<=0);  
		// Get on the other side of it now
		if ( !skip_post_vblank_idle && !alwaysBusyWait && neverBusyWait )
		{
			const uint64_t post_vblank_idle_time_int = (uint64_t)llround(post_vblank_idle_time);
			sleep_until_nanos_retrying( get_time_in_nanos() + std::min(offset + adjusted_extra_sleep, post_vblank_idle_time_int), offset, (int32_t) sleep_weights[1] );
		}
		else if (!skip_post_vblank_idle)
		{
			double compared_to = (double) ( offset + adjusted_extra_sleep);
			compared_to = fmin(compared_to, post_vblank_idle_time);
			const int64_t wait_start = get_time_in_nanos();
			
			spin_wait_w_tpause(nsPerTick_long, nsPerTick, cpu_pause_time_len, compared_to, wait_start);
			
			if (counter % 300 == 0)
				std::cout << "post-vblank TPAUSE wait loop duration: " << (double)((int64_t) get_time_in_nanos() - wait_start)/1'000'000.0L << "ms\n";
			
		}
		
		if (counter % 300 == 0)
			std::cout << "total vblank period: " << (double)( get_time_in_nanos() - vblank_begin)/1'000'000.0L << "ms\n";
			
		sleep_cycle=0;
		
		vblank_begin=0;
		lastOffset = offset_dec;
		
		
	}
}


#if HAVE_OPENVR
void vblankThreadVR()
{
	pthread_setname_np( pthread_self(), "gamescope-vblkvr" );

	while ( true )
	{
		vrsession_wait_until_visible();

		// Includes redzone.
		vrsession_framesync( ~0u );

		uint64_t now = get_time_in_nanos();

		VBlankTimeInfo_t time_info =
		{
			.target_vblank_time = now + 3'000'000, // not right. just a stop-gap for now.
			.pipe_write_time    = now,
		};

		ssize_t ret = write( g_vblankPipe[ 1 ], &time_info, sizeof( time_info ) );
		if ( ret <= 0 )
		{
			perror( "vblankmanager: write failed" );
		}
		else
		{
			gpuvis_trace_printf( "sent vblank" );
		}
	}
}
#endif

int vblank_init( const bool never_busy_wait, const bool always_busy_wait )
{
	if ( pipe2( g_vblankPipe, O_CLOEXEC | O_NONBLOCK ) != 0 )
	{
		perror( "vblankmanager: pipe failed" );
		return -1;
	}
	
	g_lastVblank = get_time_in_nanos();

#if HAVE_OPENVR
	if ( BIsVRSession() )
	{
		std::thread vblankThread( vblankThreadVR );
		vblankThread.detach();
		return g_vblankPipe[ 0 ];
	}
#endif
	
	bool supports_tpause = __builtin_cpu_is("sapphirerapids") | __builtin_cpu_is("alderlake") | __builtin_cpu_is("tremont");

	#define NEVER_BUSY_WAIT true,false,cpu_pause_time_len,CANT_USE_CPU_TIMER
	#define BALANCED_BUSY_WAIT false,false,cpu_pause_time_len
	#define ALWAYS_BUSY_WAIT false,true,cpu_pause_time_len
	
	if ( never_busy_wait ) {
		int64_t cpu_pause_time_len = 0;
		std::thread vblankThread( vblankThreadRun, NEVER_BUSY_WAIT );
		vblankThread.detach();
	}
	else {
		const long double nsPerTick_long = getNsPerTick();
		if (nsPerTick_long == CANT_USE_CPU_TIMER)
		{
			int64_t cpu_pause_time_len = 0;
			std::thread vblankThread( vblankThreadRun, NEVER_BUSY_WAIT );
			vblankThread.detach();
		}
		else if (supports_tpause) {
			double cpu_pause_time_len = (double)cpu_tpause_get_possible_max_delay();
			if (always_busy_wait) {
				std::thread vblankThread( vblankThreadRun_tpause, ALWAYS_BUSY_WAIT, nsPerTick_long );
				vblankThread.detach();
			}
			else {
				std::thread vblankThread( vblankThreadRun_tpause, BALANCED_BUSY_WAIT, nsPerTick_long );
				vblankThread.detach();
			}
		}
		else {
			int64_t cpu_pause_time_len = cpu_pause_get_upper_q_avg();
			if (always_busy_wait) {
				std::thread vblankThread( vblankThreadRun, ALWAYS_BUSY_WAIT, nsPerTick_long );
				vblankThread.detach();
			}
			else {
				std::thread vblankThread( vblankThreadRun, BALANCED_BUSY_WAIT, nsPerTick_long );
				vblankThread.detach();
			}
		}
	}
	
	return g_vblankPipe[ 0 ];
}

void vblank_mark_possible_vblank( uint64_t nanos )
{
	g_lastVblank = nanos;
}
