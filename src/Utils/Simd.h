#pragma once


    typedef uint8_t u8x2_t __attribute__((vector_size(2*sizeof(uint8_t))));
    typedef glm::vec<2, uint8_t, (glm::qualifier)3> u8vec2;
    typedef glm::vec<4, uint8_t, (glm::qualifier)3> u8vec4;
    typedef glm::vec<4, uint32_t, (glm::qualifier)3> u32vec4;
    typedef glm::vec<2, uint32_t, (glm::qualifier)3> u32vec2;
    typedef uint8_t u8x4_t __attribute__((vector_size(4*sizeof(uint8_t))));
    typedef uint32_t u32x4_t __attribute__((vector_size(4*sizeof(uint32_t))));
    typedef uint8_t u8x16_t __attribute__((vector_size(16*sizeof(uint8_t))));
    typedef uint16_t u16x8_t __attribute__((vector_size(8*sizeof(uint8_t))));
    typedef uint64_t u64x2_t __attribute__((vector_size(2*sizeof(uint64_t))));
    typedef glm::vec<2, uint16_t, (glm::qualifier)3> u16vec2;
    
    

namespace gamescope
{
		inline __m128i to8bit(__m128i in) {
		#ifdef __x86_64__
		#if defined(__AVX__) || defined(__SSE3__) || defined(__SSE4_1__)
		static constinit const __m128i shufTbl = std::bit_cast<__m128i>(std::array<char, 16>{0, 4, 8, 12, 0,0,0,0, 0,0,0,0, 0,0,0,0});
		return _mm_shuffle_epi8(in, shufTbl); 
		#else
		__m128i mask = _mm_cvtsi32_si128(0xf0f0f0f0);
		in = _mm_and_si128(in, mask);
		in = _mm_packus_epi16(in, in);
		return _mm_packus_epi16(in, in);
		#endif
		#else
		  auto u32 = (u32x4_t)(in);
		  auto tmp = __builtin_convertvector(u32,u8x4_t);
		  return (__m128i) __builtin_shufflevector(tmp, tmp, 0, 1, 2, 3, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1);
		#endif
		}
}