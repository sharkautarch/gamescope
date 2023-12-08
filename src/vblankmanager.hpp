#pragma once

/*#include <stdio.h>
#include <stdlib.h>*/
// Try to figure out when vblank is and notify steamcompmgr to render some time before it

union draw_info_t 
{
	uint64_t encoded;
	uint16_t bit_fields[4];
};
//#include <stdint.h>

/*static inline int putb(uintmax_t n)
{
    char b[(sizeof n * CHAR_BIT) + 1];
    char *p = b + sizeof b;
    *--p = '\0';
    do {
        *--p = '0' + (n & 1);
    } while (n >>= 1);
    return puts(p);
}*/
static inline draw_info_t draw_info_encode( const uint64_t drawTime, const uint16_t iterations)
{
	draw_info_t draw_info;
	draw_info.bit_fields[3] = iterations;
	
	const uint64_t drawTime_trimmed = std::min(drawTime, 281'474'976'710'655ul); //trim drawTime to the maximum number that can be held in 48 bits
	//fprintf(stdout, "\n\ndrawTime_trimmed: %li\n\n", drawTime_trimmed);
	
	draw_info.bit_fields[0] = drawTime_trimmed & 0xFFFF;
	draw_info.bit_fields[1] = (drawTime_trimmed >> 16) & 0xFFFF;
	draw_info.bit_fields[2] = (drawTime_trimmed >> 32) & 0xFFFF;
	
	/*printf("binary format:\n");
	for (int i = 2; i > -1; i--)
	{
		putb(draw_info.bit_fields[i]);
	
		printf(" ");
	}
	printf("\n\n");*/
	

	return draw_info;
}

static inline uint16_t draw_info_get_iterations ( const draw_info_t * draw_info)
{
	return draw_info->bit_fields[3];
}

static inline uint64_t draw_info_get_drawTime(const draw_info_t * draw_info)
{
	uint64_t drawTime = 0;
	
	const uint16_t drawTime_16 = draw_info->bit_fields[0];
	const uint32_t drawTime_32 = (uint32_t) (draw_info->bit_fields[1]) << 16;
	
	const uint64_t drawTime_64 = (uint64_t)(draw_info->bit_fields[2]) << 32;
	
	drawTime = drawTime_64 | ( (uint64_t) drawTime_32) | ( (uint64_t) drawTime_16);
	
	return drawTime;
}

struct VBlankTimeInfo_t
{
        uint64_t target_vblank_time;
        uint64_t pipe_write_time;
};

int vblank_init( const bool never_busy_wait, const bool always_busy_wait );

extern std::atomic<uint64_t> g_lastVblank;

inline void vblank_mark_possible_vblank( uint64_t nanos )
{
	g_lastVblank = nanos;
}

inline void vblank_mark_possible_vblank_weaker( uint64_t nanos )
{
	g_lastVblank.store(nanos, std::memory_order_release);
}

extern std::atomic<uint64_t> g_uVblankDrawTimeNS;




const unsigned int g_uDefaultVBlankRedZone = 1'650'000;
const unsigned int g_uDefaultMinVBlankTime = 350'000; // min vblank time for fps limiter to care about
const unsigned int g_uDefaultVBlankRateOfDecayPercentage = 980;

// 3ms by default -- a good starting value.
const uint64_t g_uStartingDrawTime = 3'000'000;

extern uint64_t g_uVblankDrawBufferRedZoneNS;
extern uint64_t g_uVBlankRateOfDecayPercentage;

extern std::atomic<bool> g_bCurrentlyCompositing;


