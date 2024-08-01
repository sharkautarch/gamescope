#pragma once
#include <limits>

namespace gamescope::bits {
		template <typename T>
		consteval bool isPowerOfTwo(T n) {
			return (n & (n - 1)) == 0; //https://stackoverflow.com/a/108360
		}

		template <typename T>
		concept UBitCompatible = std::unsigned_integral<T> && std::regular<T> && requires { isPowerOfTwo(sizeof(T)); };
		
		template <typename T> requires UBitCompatible<T>
		static constexpr T __attribute__((const)) setBit(T bits, T pos) {
			constexpr T one = 1;
			return bits | (one << pos);
		}
		
		template <typename T> requires UBitCompatible<T>
		consteval T getAllOnes() {
			return std::numeric_limits<T>::max();
		}
		
		template <typename T> requires UBitCompatible<T>
		static constexpr T __attribute__((const)) getBit(T bits, T pos) {
			return ~unsetBit(bits, pos);
		}
		
		template <typename T> requires UBitCompatible<T>
		static constexpr T __attribute__((const)) unsetBit(T bits, T pos) {
			constexpr T one = 1;
			constexpr auto ones = getAllOnes<T>();
			return bits & (ones ^ (one << pos));
		}

		static_assert(unsetBit<uint16_t>(0b1111'1111'1111'1111u, 3u) == 0b1111'1111'1111'0111u);
		static_assert(unsetBit<uint16_t>(0b1111'1111'1111'0111u, 3u) == 0b1111'1111'1111'0111u);
		static_assert(unsetBit<uint16_t>(0b1010'0011'1011'1111u, 3u) == 0b1010'0011'1011'0111u);

		template <typename T> requires UBitCompatible<T>
		static constexpr T __attribute__((const)) maskOutBitsBelowPos(T bits, T pos) {
			const T startPos = pos ? pos - 1 : 0;
			constexpr auto ones = getAllOnes<T>();
			const T bitmask = ones << startPos;
			return bits & bitmask;
		}

		static_assert(maskOutBitsBelowPos<uint16_t>(0b1111'1111'1111'1111u, 5u) == 0b1111'1111'1111'0000u);
		static_assert(maskOutBitsBelowPos<uint16_t>(0b1010'0011'1011'1111u, 5u) == 0b1010'0011'1011'0000u);

		template <typename T> requires UBitCompatible<T>
		static constexpr T __attribute__((const)) maskOutBitsAbovePos(T bits, T pos) {
			const T startPos = pos + 1;
			constexpr auto ones = getAllOnes<T>();
			constexpr T bitwidth = sizeof(T)*CHAR_BIT; //https://stackoverflow.com/a/3200969
			const T bitmask = ones >> (bitwidth-startPos);
			return bits & bitmask;
		}
			
		static_assert(maskOutBitsAbovePos<uint16_t>(0b1111'1111'1111'1010u, 4u) == 0b0000'0000'0001'1010u);
}