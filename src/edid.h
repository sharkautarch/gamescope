#pragma once

#include <span>
#include <cstdint>
#include <optional>
#include <vector>
#include <glm/fwd.hpp>
#ifdef __x86_64__
#pragma GCC target("sse4.1")
 
#include <smmintrin.h>
#endif
namespace gamescope
{
    struct BackendConnectorHDRInfo;

    const char *GetPatchedEdidPath();
    void WritePatchedEdid( std::span<const uint8_t> pEdid, const BackendConnectorHDRInfo &hdrInfo, bool bRotate );

    std::vector<uint8_t> GenerateSimpleEdid( __m128i resolution );
    //typedef uint32_t u32x4_t __attribute__((vector_size(4*sizeof(uint32_t))));
    inline std::vector<uint8_t> GenerateSimpleEdid( auto resolution ) {
    	__m128i xmm;
    	memcpy(&xmm, &resolution, 8);
    	return GenerateSimpleEdid(xmm);
    }

    std::optional<std::vector<uint8_t>> PatchEdid( std::span<const uint8_t> pEdid, const BackendConnectorHDRInfo &hdrInfo, bool bRotate );
}