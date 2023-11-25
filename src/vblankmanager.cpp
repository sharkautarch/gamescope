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

inline bool __attribute__((always_inline)) _isinf(double val)
{
#ifndef __clang__
	return __builtin_isinf(val);
#else
	return __builtin_isfpclass(val, __FPCLASS_POSINF+_FPCLASS_NEGINF);
#endif
}

#include <waitpkgintrin.h>
inline void __attribute__((always_inline)) cpu_pause(const uint64_t counter, const bool do_timed_pause)
{
#if __has_builtin(__builtin_ia32_tpause)
# define _TPAUSE _tpause(0, counter)
#elif defined(__clang__)
# define _TPAUSE _mm_pause()
#else
# define _TPAUSE __builtin_ia32_pause()
#endif

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
int __attribute__((const)) median_trial(const int l, const int r) //credit for this function: https://www.geeksforgeeks.org/interquartile-range-iqr/
{

    int n = (int)r - (int)l + 1;

    n = (n + 1) / 2 - 1;

    return n + l;

}
//measures the time it takes to do a single pause instruction -> 512 trials.
//returns the average of the 25% longest run times
long int cpu_pause_get_upper_q_avg()
{
	const int num_test_runs=512;
	long int trials[num_test_runs];
	
	for (int i = 0; i < num_test_runs; i++)
	{
		long int before = get_time_in_nanos();
		cpu_pause(0, false);
		trials[i]=get_time_in_nanos()-before;
	}
	
	std::sort(trials, trials+num_test_runs);
	
	long int sum = 0;
	for (int i = 3*num_test_runs/4; i < num_test_runs; i++) {
		sum += trials[i];
	}
	
	return sum/((long int)num_test_runs/4l);
}

#define HMMM(expr) __builtin_expect_with_probability(expr, 1, .05) //hmmm has slightly higher probability than meh
#define MEH(expr) __builtin_expect_with_probability(expr, 1, .02)
inline void __attribute__((optimize("-Oz"))) spin_wait_w_tpause(const long double nsPerTick_long, const double nsPerTick, const double compared_to, const int64_t wait_start)
{
	int64_t diff;
	double res = 0.0;
	double check_this_first = 0.0;
	long double check_this = 0.0L;
	const uint64_t compared_int = (uint64_t)llroundl(compared_to/(nsPerTick_long*2.0L));
	
	const int64_t compared_to_const = (int64_t)llround(compared_to);
	
	int i = 0;
	uint64_t prev = readCycleCount();
	while ( res < compared_to && get_time_in_nanos() < wait_start + compared_to_const)
	{
		for (int j = i;j<2;j++)
		{
			uint64_t tsc_time_value = readCycleCount() + compared_int*(2-j)/( ((2+i)*(2+i)) - 1);
			cpu_pause((uint64_t)tsc_time_value, true);
		}
		
		//compared_to = compared_to - (double) (get_time_in_nanos() - t_before_second_wait);
		//prev=readCycleCount();
		int j=4-i;
		
		while ( res < compared_to && j < 3 )
		{
			j++;
			res = DBL_MAX;
		
			cpu_pause(0, true);
		
			diff = (int64_t)readCycleCount() - (int64_t)prev;
			if ( HMMM(diff < 0) )
			{
				std::cout << "oh noes\n";
				continue; // in case tsc counter resets or something
			}
		
			check_this_first = (double)diff * nsPerTick;
			if ( HMMM(_isinf(check_this_first)) )
			{
				check_this = (long double)diff * nsPerTick_long;
				if ( MEH(std::fpclassify(check_this) == FP_INFINITE) ) //meh and hmmm: compiler hints that this branch is unlikely to occur
				{						       //     hopefully might reduce fruitless speculative execution
					break;
				}
				res = ( ( std::fpclassify(check_this) == FP_NORMAL && check_this <= DBL_MAX) ? check_this :  DBL_MAX);
			}
			res = check_this_first;
		}
		
		//compared_to = compared_to - (double) (get_time_in_nanos() - t_before_second_wait);
		//prev=readCycleCount();
		i++;
	}
}

inline void __attribute__((optimize("-Oz"))) spin_wait(const long double nsPerTick_long, const double nsPerTick, const long int cpu_pause_time_len, const double compared_to, const int64_t wait_start)
{
	int64_t diff;
	double res = 0.0;
	double check_this_first = 0.0;
	long double check_this = 0.0L;
	
	const int64_t compared_to_const = (int64_t)llround(compared_to);
	
	const long int cpu_pause_loop_iter = (long int) llroundl(compared_to/(2.0L*(long double)cpu_pause_time_len));
	
	int i = 0;
	uint64_t prev = readCycleCount();
	
	for (long int k = 0; k < std::max(cpu_pause_loop_iter-1,0l); k += 2) {
				cpu_pause(0, false);
				cpu_pause(0, false);
	}
				
	while ( res < compared_to && get_time_in_nanos() < wait_start + compared_to_const)
	{
		
		//compared_to = compared_to - (double) (get_time_in_nanos() - t_before_second_wait);
		//prev=readCycleCount();
		int j=4-i;
		
		while ( res < compared_to && j < 3 )
		{
			j++;
			res = DBL_MAX;
		
			cpu_pause(0, false);
		
			diff = (int64_t)readCycleCount() - (int64_t)prev;
			if ( HMMM(diff < 0) )
			{
				std::cout << "oh noes\n";
				continue; // in case tsc counter resets or something
			}
		
			check_this_first = (double)diff * nsPerTick;
			if ( HMMM(_isinf(check_this_first)) )
			{
				check_this = (long double)diff * nsPerTick_long;
				if ( MEH(std::fpclassify(check_this) == FP_INFINITE) ) //meh and hmmm: compiler hints that this branch is unlikely to occur
				{						       //     hopefully might reduce fruitless speculative execution
					break;
				}
				res = ( ( std::fpclassify(check_this) == FP_NORMAL && check_this <= DBL_MAX) ? check_this :  DBL_MAX);
			}
			res = check_this_first;
		}
		
		//compared_to = compared_to - (double) (get_time_in_nanos() - t_before_second_wait);
		//prev=readCycleCount();
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

//#define VBLANK_DEBUG

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

    int r1 = med(a, 0, mid_index);

    int r3 = std::min(med(a, mid_index + 1, n), n);
    
    long int sum=0;
    for (int i = r1; i < r3; i++)
    {
    	sum += ( ((long int) a[i]) * 1000 );
    }
    return sum/(r3 - r1);
}
#undef med

inline __attribute__((always_inline)) void sleep_until_nanos_retrying(const long int nanos, const long int offset, const long int sleep_weight)
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
double __attribute__((const, hot )) vblank_next_target(const double _lastVblank, const double offset, const double nsecInterval, const double limitFactor, const double now )
#else
double __attribute__((const,optimize("-fno-trapping-math", "-fsplit-paths","-fsplit-loops","-fipa-pta","-ftree-partial-pre","-fira-hoist-pressure","-fdevirtualize-speculatively","-fgcse-after-reload","-fgcse-sm","-fgcse-las"), hot )) vblank_next_target( const double _lastVblank, const double offset, const double nsecInterval, const double limitFactor, const double now )
#endif
{

	double lastVblank = _lastVblank - offset;
        
        
	double targetPoint = lastVblank + nsecInterval;
	
	double dist_to_target = now - targetPoint;
        
        targetPoint = (dist_to_target>0.0) * (nsecInterval*ceil(dist_to_target/(nsecInterval)))
	       + targetPoint;
	
	double relativePoint = targetPoint - now;
	
	double cappedTargetPoint = fmin(nsecInterval*limitFactor, relativePoint) + now;
	
	return cappedTargetPoint;
}


#ifdef __clang__
void __attribute__((optimize("-fno-unsafe-math-optimizations"), hot, flatten )) vblankThreadRun( const bool neverBusyWait, const bool alwaysBusyWait, const bool cpu_supports_tpause, const long int cpu_pause_time_len, const long double nsPerTick_long  )
#else
void __attribute__((optimize("-fno-unsafe-math-optimizations","-fno-trapping-math", "-fsplit-paths","-fsplit-loops","-fipa-pta","-ftree-partial-pre","-fira-hoist-pressure","-fdevirtualize-speculatively","-fgcse-after-reload","-fgcse-sm","-fgcse-las"), hot, flatten )) vblankThreadRun( const bool neverBusyWait, const bool alwaysBusyWait, const bool cpu_supports_tpause, const long int cpu_pause_time_len, const long double nsPerTick_long  )
#endif
{
	pthread_setname_np( pthread_self(), "gamescope-vblk" );

	// Start off our average with our starting draw time.
	uint64_t rollingMaxDrawTime = g_uStartingDrawTime;

	const uint64_t range = g_uVBlankRateOfDecayMax;
	uint8_t sleep_cycle = 0;
	
	
	const double nsPerTick = (double) nsPerTick_long;
	std::cout << "nsPerTick: " << nsPerTick << "\n";
	
	uint16_t drawtimes[64] = {1};
	uint16_t drawtimes_pending[64];
	std::fill_n(drawtimes, 64, (uint16_t)(((1'000'000'000ul / (g_nNestedRefresh ? g_nNestedRefresh : g_nOutputRefresh)) >> 1)/500 )  );
	int index=0;
	long int centered_mean = 1'000'000'000l / std::max( (long int) (g_nNestedRefresh ? g_nNestedRefresh : g_nOutputRefresh), 120l);
	long int max_drawtime=2*centered_mean;
	
	
	const long int sleep_weights[2] = {60, 40};
	double vblank_begin=0.0;
	long int time_start = get_time_in_nanos();
	uint32_t counter = 0;
	long int lastDrawTime = g_uVblankDrawTimeNS;
	long int lastDrawTime_timestamp = get_time_in_nanos();
	
	
	double first_cycle_sleep_duration = 0.0;
	long double drawTimeTime = get_time_in_nanos();
	long double lastDrawTimeTime = get_time_in_nanos(); 
	
	double lastOffset = (double)centered_mean;
	long double real_delta = 0.0L;
	long double last_real_delta = 0.0L; 
	long double local_min = g_uVBlankDrawTimeMinCompositing;
	long double local_max = (long double)centered_mean + g_uVBlankDrawTimeMinCompositing;
	long double lastRollingMaxDrawTime = (long double)centered_mean;
	
	float delta_trend_counter = 3.0f;
	double offset_dec = 0.0;
	
	
	const uint64_t max_delta_apply = 50'000; //.05ms per sec
	// ^ For non-vrr, base rate of change in rollingMaxDeltaTime 
	
	
	const double targetPoint_max_percent_of_refresh_vblank_waiting = 0.90; //limits how much longer vblankmanager waits before submitting a vblank
	
	const double targetPoint_max_percent_of_refresh_vsync_value = 1.50; //Don't confuse this variable with the one above.
	// ^ this limits how much we stretch out steamcompmanager's vsync in order to line up with the past vblank time reported by steamcompmanager
	
	const double offset_max_percent_of_refresh_vblank_waiting = 0.85;
	//^ similar to targetPoint_max_percent_of_refresh_vblank_waiting
		
	while ( true )
	{
		sleep_cycle++;
		if (sleep_cycle < 2)
			vblank_begin=(double)get_time_in_nanos();

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
		
		long int drawTime;
		long int offset;
	 	
		
		bool bVRR = drm_get_vrr_in_use( &g_DRM );
		if ( !bVRR )
		{
			const long double nsecInterval_dec = (long double) nsecInterval;
			const uint64_t alpha = g_uVBlankRateOfDecayPercentage;
			
			drawTime = g_uVblankDrawTimeNS;
			drawTimeTime = (long double)get_time_in_nanos();
			
			
			if ( sleep_cycle < 2 && g_bCurrentlyCompositing )
				drawTime = fmax(drawTime, g_uVBlankDrawTimeMinCompositing);
			if (sleep_cycle < 2)
				drawtimes_pending[index] = (uint16_t)( (drawTime >> 1)/500 );
			
			
			if (sleep_cycle > 1)
			{	
				rollingMaxDrawTime = ( ( alpha * rollingMaxDrawTime ) + ( range - alpha ) * drawTime ) / (range);
				g_uRollingMaxDrawTime.store(rollingMaxDrawTime, std::memory_order_relaxed);
			}
			else
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
				
				double radians = 2.0*M_PI * (double) ((int64_t)get_time_in_nanos()-(int64_t)lastDrawTime_timestamp) / ( (double) nsecInterval);
				double sinc = (double) (heaviside((int64_t)get_time_in_nanos()-(int64_t)lastDrawTime_timestamp)) * sin(radians)/fmax(radians, 0.0000001);
				
				double delta_check = pow(fmax((double)( sinc*fabs((int64_t)lastDrawTime - (int64_t)drawTime)), 1.0 )/100000.0, 2);
				double delta = fmax( delta_check * (double)(heaviside( (int64_t)nsecInterval/1000000 - ((int) round(2.0*delta_check)))) , 1);
				//						^ branchless way of checking if value delta_check is so large that it'll mess up
				//						  the rollingMaxDrawTime calculations
				double ratio = ((double)drawTime) / ( fmax( ((double) heaviside( (int64_t) nsecInterval - (int64_t)lastDrawTime)) * ( (double) lastDrawTime), drawTime ) );
				rollingMaxDrawTime = (uint64_t)(llroundl( (double) fmax( (double) centered_mean, (double) ( ( alpha * rollingMaxDrawTime ) + ( range - alpha ) * drawTime ) / (range))
				      		* ratio /( delta)));
				if (counter % 300 == 0) {
		        		std::cout << "delta= " << delta << "\n";
		        		std::cout << "(double) ( ( alpha * rollingMaxDrawTime ) + ( range - alpha ) * drawTime ) / (range))* ratio /( delta):\n";
		        		std::cout << (double) (( ( alpha * rollingMaxDrawTime ) + ( range - alpha ) * drawTime ) / (range)) * ratio /( delta) << "\n\n";
		        		std::cout << "ratio= " << ratio << "\n";
					std::cout << "rollingMaxDrawTime after using fmin: " << rollingMaxDrawTime << "\n";
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
			rollingMaxDrawTime = (uint64_t)std::clamp(centered_mean/2, (long int) rollingMaxDrawTime, nsecInterval + nsecInterval/10);
			if (counter % 300 == 0) 
				std::cout << "rollingMaxDrawTime after using std::clamp: " << rollingMaxDrawTime << "\n";
			
			
			offset = rollingMaxDrawTime + redZone;
			if (sleep_cycle > 1)
			{
				offset = std::clamp(std::min(nsecInterval, centered_mean)-nsecInterval/25, offset, nsecInterval+nsecInterval/20);
			}
			else
			{
				offset = std::clamp(std::min( nsecInterval, centered_mean)-nsecInterval/20, offset , nsecInterval+nsecInterval/5);
			}	
			if (counter % 300 == 0) 
				std::cout << "offset: " << offset << " sleep_cycle: "<< sleep_cycle << "\n";	
			

			if (sleep_cycle < 2)
				index++;
			
			if ( sleep_cycle < 2 && index >= 64 )
			{
				
				memcpy(drawtimes, drawtimes_pending, 64 * sizeof(drawtimes_pending[0]));
				index=0;
				const uint16_t n = 64; 
				centered_mean = (centered_mean + clamp(2*nsecInterval/3, IQM(drawtimes, n), 5*nsecInterval/3))/2;
				
				max_drawtime = std::min( 
					      (	
					  	((uint64_t)(std::max(  (uint16_t)((max_drawtime>>1)/500), *std::max_element(std::begin(drawtimes), std::end(drawtimes)))))
					         * 500)
					      <<1
					, (uint64_t)(8*nsecInterval/3));
			}
			
		}
		else
		{
			// VRR:
			// Just ensure that if we missed a frame due to already
			// having a page flip in-flight, that we flush it out with this.
			// Nothing fancy needed, just need to get on the other side of the page flip.
			//
			// We don't use any of the rolling times due to them varying given our
			// 'vblank' time is varying.
			g_uRollingMaxDrawTime = g_uStartingDrawTime;

			offset = 1'000'000 + redZone;
		}
		offset_dec=(double)offset;
		const double offset_dec_capped = fmin(nsecInterval_dec*offset_max_percent_of_refresh_vblank_waiting, offset_dec);
		const double lastVblank = (double)g_lastVblank.load();

#ifdef VBLANK_DEBUG
		// Debug stuff for logging missed vblanks
		static uint64_t vblankIdx = 0;
		
		//static uint64_t lastOffset = g_uVblankDrawTimeNS + redZone;

		if ( sleep_cycle > 1 && (vblankIdx++ % 300 == 0 || drawTime > lround(lastOffset) )
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
		long int targetPoint;


		if ( !neverBusyWait && ( alwaysBusyWait || sleep_cycle > 1 || offset*sleep_weights[sleep_cycle-1] / 100 < 1'000'000ll ) )
		{
			
			
			if (counter % 300 == 0)
				std::cout << "vblank cycle time before second wait: " << ( (long double)get_time_in_nanos()-vblank_begin )/1'000'000.0L << "ms\n";
			
			
			double compared_to;
			double now = (double)get_time_in_nanos();
			if (sleep_cycle > 1)
			{
				compared_to = vblank_next_target( lastVblank, offset_dec_capped*sleep_weights[sleep_cycle-1] / 100, nsecInterval_dec, targetPoint_max_percent_of_refresh_vblank_waiting, now)  - now;
				compared_to = fmax(compared_to - first_cycle_sleep_duration, offset_dec_capped - first_cycle_sleep_duration);
			}
			else
				compared_to = vblank_next_target( lastVblank, offset_dec_capped*sleep_weights[sleep_cycle-1] / 100, nsecInterval_dec, targetPoint_max_percent_of_refresh_vblank_waiting, now) - now;
			const int64_t wait_start = get_time_in_nanos();
			
			if (cpu_supports_tpause)
				spin_wait_w_tpause(nsPerTick_long, nsPerTick, compared_to, wait_start);
			else
				spin_wait(nsPerTick_long, nsPerTick, cpu_pause_time_len, compared_to, wait_start);
			
			if (sleep_cycle < 2)
				first_cycle_sleep_duration=(double)get_time_in_nanos() - vblank_begin;
			targetPoint = vblank_next_target(lastVblank, offset_dec, nsecInterval_dec, targetPoint_max_percent_of_refresh_vsync_value, now);
		}
		else
		{
			if (counter % 300 == 0) {
				if (sleep_cycle < 2)
					std::cout << "vblank cycle time before first sleep: " << ( (long double)get_time_in_nanos()-vblank_begin )/1'000'000.0L << "ms\n";
				else
					std::cout << "vblank cycle time before second sleep: " << ( (long double)get_time_in_nanos()-vblank_begin )/1'000'000.0L << "ms\n";
			}
			double now = (double)get_time_in_nanos();
			if (sleep_cycle < 2)
			{
				double first_cycle_sleep_start = now;
				targetPoint = (uint64_t)llround(fmax( first_cycle_sleep_start + offset_dec_capped*sleep_weights[sleep_cycle-1] / 100, first_cycle_sleep_start +  (vblank_next_target(lastVblank,  offset_dec_capped*sleep_weights[sleep_cycle-1] / (100ll), nsecInterval_dec, targetPoint_max_percent_of_refresh_vblank_waiting, now)-first_cycle_sleep_start) * sleep_weights[sleep_cycle-1] / 100 ));
				sleep_until_nanos_retrying( targetPoint, offset, (int32_t) sleep_weights[1]); 
				
				now = (double)get_time_in_nanos();
				first_cycle_sleep_duration = now - vblank_begin;
			
				if ( now - vblank_begin > fmax(  offset_dec_capped,  lastOffset) )
				{
					offset_dec=fmin(fmax(  offset_dec, lastOffset), nsecInterval_dec+(double)redZone/2.0);
					targetPoint = llround(vblank_next_target(lastVblank, offset_dec, nsecInterval_dec, targetPoint_max_percent_of_refresh_vsync_value, now));
					goto SKIPPING_SECOND_SLEEP;
				}
			}
			else
			{
				targetPoint = lround(vblank_next_target(lastVblank, offset_dec_capped*sleep_weights[sleep_cycle-1] / 100, nsecInterval_dec, targetPoint_max_percent_of_refresh_vblank_waiting,  now));
				targetPoint = now + fmax(targetPoint - now - first_cycle_sleep_duration, offset_dec_capped - first_cycle_sleep_duration);
				sleep_until_nanos( targetPoint );
				now = (double)get_time_in_nanos();
			}
				
			
			targetPoint = vblank_next_target(lastVblank, offset_dec, nsecInterval_dec, targetPoint_max_percent_of_refresh_vsync_value, now);
		}
		
		if (sleep_cycle < 2)
		{
			continue;
		}
		
	SKIPPING_SECOND_SLEEP:;
		VBlankTimeInfo_t time_info =
		{
			.target_vblank_time = static_cast<uint64_t>(targetPoint + offset),
			.pipe_write_time    = static_cast<uint64_t>(get_time_in_nanos()),
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
		
		lastDrawTime=drawTime;
		lastDrawTimeTime=drawTimeTime;
		lastRollingMaxDrawTime=(long double) rollingMaxDrawTime;
		last_real_delta=real_delta;
		lastDrawTime_timestamp=get_time_in_nanos();
		
		
		uint64_t this_time=(get_time_in_nanos() - time_start)/1'000'000'000ul;
		if ( this_time > 5)
		{
			std::cout << counter << " vblanks sent in " << this_time << " seconds\n";
			time_start=get_time_in_nanos();
			counter=0;
		}
		
		
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
			
			if (cpu_supports_tpause)
				spin_wait_w_tpause(nsPerTick_long, nsPerTick, compared_to, wait_start);
			else
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
	long int cpu_pause_time_len = cpu_pause_get_upper_q_avg();
	#define supports_tpause __builtin_cpu_is("sapphirerapids") | __builtin_cpu_is("alderlake") | __builtin_cpu_is("tremont")

	#define NEVER_BUSY_WAIT true,false,supports_tpause,cpu_pause_time_len,CANT_USE_CPU_TIMER
	#define BALANCED_BUSY_WAIT false,false,supports_tpause,cpu_pause_time_len
	#define ALWAYS_BUSY_WAIT false,true,supports_tpause,cpu_pause_time_len
	
	if ( never_busy_wait ) {
		std::thread vblankThread( vblankThreadRun, NEVER_BUSY_WAIT );
		vblankThread.detach();
	}
	else {
		const long double nsPerTick_long = getNsPerTick();
		if (nsPerTick_long == CANT_USE_CPU_TIMER)
		{
			std::thread vblankThread( vblankThreadRun, NEVER_BUSY_WAIT );
			vblankThread.detach();
		}
		else if (always_busy_wait) {
			std::thread vblankThread( vblankThreadRun, ALWAYS_BUSY_WAIT, nsPerTick_long );
			vblankThread.detach();
		}
		else {
			std::thread vblankThread( vblankThreadRun, BALANCED_BUSY_WAIT, nsPerTick_long );
			vblankThread.detach();
		}
	}
	
	return g_vblankPipe[ 0 ];
}

void vblank_mark_possible_vblank( uint64_t nanos )
{
	g_lastVblank = nanos;
}
