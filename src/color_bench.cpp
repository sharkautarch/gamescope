#include <array>
#include <benchmark/benchmark.h>

#include <algorithm>
#include <stdio.h>
#include "Utils/Algorithm.h"

#include "color_helpers_impl.h"

using color_bench::nLutEdgeSize3d;
using color_bench::nLutSize1d;

uint16_t lut1d[nLutSize1d*4];
uint16_t lut3d[nLutEdgeSize3d*nLutEdgeSize3d*nLutEdgeSize3d*4];

lut1d_t lut1d_float;
lut3d_t lut3d_float;

static lut3d_t g_lut3dLook;
FILE* pNullDev = fopen("/dev/null", "w");
void __attribute__((constructor)) loadLutLook() {
	std::string path = (std::string{getenv("HOME")} + std::string{ "/gamescope/src/color_bench_test_lut.txt"});
	if (!LoadCubeLut(&g_lut3dLook, path.c_str())) {
		fprintf(stderr, "failed to load color_bench_test_lut.txt, aborting\n");
		abort();
	}
}
static void BenchmarkCalcColorTransform(EOTF inputEOTF, benchmark::State &state)
{
    const primaries_t primaries = { { 0.602f, 0.355f }, { 0.340f, 0.574f }, { 0.164f, 0.121f } };
    const glm::vec2 white = { 0.3070f, 0.3220f };
    const glm::vec2 destVirtualWhite = { 0.f, 0.f };

    displaycolorimetry_t inputColorimetry{};
    inputColorimetry.primaries = primaries;
    inputColorimetry.white = white;

    displaycolorimetry_t outputEncodingColorimetry{};
    outputEncodingColorimetry.primaries = primaries;
    outputEncodingColorimetry.white = white;

    colormapping_t colorMapping{};

    tonemapping_t tonemapping{};
    tonemapping.bUseShaper = true;

    nightmode_t nightmode{};
    float flGain = 1.0f;

    for (auto _ : state) {
        calcColorTransform<nLutEdgeSize3d>( &lut1d_float, nLutSize1d, &lut3d_float, inputColorimetry, inputEOTF,
            outputEncodingColorimetry, EOTF_Gamma22,
            destVirtualWhite, k_EChromaticAdapatationMethod_XYZ,
            colorMapping, nightmode, tonemapping, nullptr, flGain );
        for ( size_t i=0, end = lut1d_float.dataR.size(); i<end; ++i )
        {
            lut1d[4*i+0] = quantize_lut_value_16bit( lut1d_float.dataR[i] );
            lut1d[4*i+1] = quantize_lut_value_16bit( lut1d_float.dataG[i] );
            lut1d[4*i+2] = quantize_lut_value_16bit( lut1d_float.dataB[i] );
            lut1d[4*i+3] = 0;
        }
        for ( size_t i=0, end = lut3d_float.data.size(); i<end; ++i )
        {
            lut3d[4*i+0] = quantize_lut_value_16bit( lut3d_float.data[i].r );
            lut3d[4*i+1] = quantize_lut_value_16bit( lut3d_float.data[i].g );
            lut3d[4*i+2] = quantize_lut_value_16bit( lut3d_float.data[i].b );
            lut3d[4*i+3] = 0;
        }
    }
}

static void BenchmarkCalcColorTransform_pLook(EOTF inputEOTF, benchmark::State &state)
{
    const primaries_t primaries = { { 0.602f, 0.355f }, { 0.340f, 0.574f }, { 0.164f, 0.121f } };
    const glm::vec2 white = { 0.3070f, 0.3220f };
    const glm::vec2 destVirtualWhite = { 0.f, 0.f };


		struct data_t {
			uint16_t lut1d[nLutSize1d*4];
			uint16_t lut3d[nLutEdgeSize3d*nLutEdgeSize3d*nLutEdgeSize3d*4];
			lut1d_t lut1d_float;
			lut3d_t lut3d_float;
		  displaycolorimetry_t inputColorimetry;

		  displaycolorimetry_t outputEncodingColorimetry;

		  colormapping_t colorMapping{};

		  tonemapping_t tonemapping;


		  nightmode_t nightmode{};
		  float flGain;
		  lut3d_t lut3dLook;
		};
		
		const data_t data_initial = {
			.inputColorimetry={.primaries = primaries, .white = white},
			.outputEncodingColorimetry={.primaries = primaries, .white = white},
			.tonemapping = {.bUseShaper = true},
			.flGain = 1.f,
			.lut3dLook = g_lut3dLook
		};
		

    for (auto _ : state) {
    		data_t data = data_initial;
        calcColorTransform<nLutEdgeSize3d>( &data.lut1d_float, nLutSize1d, &data.lut3d_float, data.inputColorimetry, inputEOTF,
            data.outputEncodingColorimetry, EOTF_Gamma22,
            destVirtualWhite, k_EChromaticAdapatationMethod_XYZ,
            data.colorMapping, data.nightmode, data.tonemapping, &data.lut3dLook, data.flGain );
        for ( size_t i=0, end = data.lut1d_float.dataR.size(); i<end; ++i )
        {
            data.lut1d[4*i+0] = quantize_lut_value_16bit( data.lut1d_float.dataR[i] );
            data.lut1d[4*i+1] = quantize_lut_value_16bit( data.lut1d_float.dataG[i] );
            data.lut1d[4*i+2] = quantize_lut_value_16bit( data.lut1d_float.dataB[i] );
            data.lut1d[4*i+3] = 0;
        }
        for ( size_t i=0, end = data.lut3d_float.data.size(); i<end; ++i )
        {
            data.lut3d[4*i+0] = quantize_lut_value_16bit( data.lut3d_float.data[i].r );
            data.lut3d[4*i+1] = quantize_lut_value_16bit( data.lut3d_float.data[i].g );
            data.lut3d[4*i+2] = quantize_lut_value_16bit( data.lut3d_float.data[i].b );
            data.lut3d[4*i+3] = 0;
        }
        
        
        fwrite_unlocked(reinterpret_cast<void*>(&data), sizeof(unsigned char), sizeof(data_t), pNullDev); //fwrite results to /dev/null, to prevent compiler from optimizing stuff away
        
        
    }
}

static void BenchmarkCalcColorTransform_pLookOriginal(EOTF inputEOTF, benchmark::State &state)
{
    const primaries_t primaries = { { 0.602f, 0.355f }, { 0.340f, 0.574f }, { 0.164f, 0.121f } };
    const glm::vec2 white = { 0.3070f, 0.3220f };
    const glm::vec2 destVirtualWhite = { 0.f, 0.f };

		struct data_t {
			uint16_t lut1d[nLutSize1d*4];
			uint16_t lut3d[nLutEdgeSize3d*nLutEdgeSize3d*nLutEdgeSize3d*4];
			lut1d_t lut1d_float;
			lut3d_t lut3d_float;
		  displaycolorimetry_t inputColorimetry;

		  displaycolorimetry_t outputEncodingColorimetry;

		  colormapping_t colorMapping{};

		  tonemapping_t tonemapping;


		  nightmode_t nightmode{};
		  float flGain;
		  lut3d_t lut3dLook;
		};
		
		const data_t data_initial = {
			.inputColorimetry={.primaries = primaries, .white = white},
			.outputEncodingColorimetry={.primaries = primaries, .white = white},
			.tonemapping = {.bUseShaper = true},
			.flGain = 1.f,
			.lut3dLook = g_lut3dLook
		};

    for (auto _ : state) {
    		data_t data = data_initial;
        calcColorTransform_Original_pLook<nLutEdgeSize3d>( &data.lut1d_float, nLutSize1d, &data.lut3d_float, data.inputColorimetry, inputEOTF,
            data.outputEncodingColorimetry, EOTF_Gamma22,
            destVirtualWhite, k_EChromaticAdapatationMethod_XYZ,
            data.colorMapping, data.nightmode, data.tonemapping, &data.lut3dLook, data.flGain );
        for ( size_t i=0, end = data.lut1d_float.dataR.size(); i<end; ++i )
        {
            data.lut1d[4*i+0] = quantize_lut_value_16bit( data.lut1d_float.dataR[i] );
            data.lut1d[4*i+1] = quantize_lut_value_16bit( data.lut1d_float.dataG[i] );
            data.lut1d[4*i+2] = quantize_lut_value_16bit( data.lut1d_float.dataB[i] );
            data.lut1d[4*i+3] = 0;
        }
        for ( size_t i=0, end = data.lut3d_float.data.size(); i<end; ++i )
        {
            data.lut3d[4*i+0] = quantize_lut_value_16bit( data.lut3d_float.data[i].r );
            data.lut3d[4*i+1] = quantize_lut_value_16bit( data.lut3d_float.data[i].g );
            data.lut3d[4*i+2] = quantize_lut_value_16bit( data.lut3d_float.data[i].b );
            data.lut3d[4*i+3] = 0;
        }
        
        fwrite_unlocked(reinterpret_cast<void*>(&data), sizeof(unsigned char), sizeof(data_t), pNullDev); //fwrite results to /dev/null, to prevent compiler from optimizing stuff away
    }
}

static void BenchmarkCalcColorTransforms_G22(benchmark::State &state)
{
    BenchmarkCalcColorTransform(EOTF_Gamma22, state);
}
BENCHMARK(BenchmarkCalcColorTransforms_G22);

static void BenchmarkCalcColorTransforms_PQ(benchmark::State &state)
{
    BenchmarkCalcColorTransform(EOTF_PQ, state);
}
BENCHMARK(BenchmarkCalcColorTransforms_PQ);

static void BenchmarkCalcColorTransforms(benchmark::State &state)
{
    for ( uint32_t nInputEOTF = 0; nInputEOTF < EOTF_Count; nInputEOTF++ )
        BenchmarkCalcColorTransform((EOTF)nInputEOTF, state);
}
BENCHMARK(BenchmarkCalcColorTransforms);

static void BenchmarkCalcColorTransforms_pLook_G22(benchmark::State &state)
{
    BenchmarkCalcColorTransform_pLook(EOTF_Gamma22, state);
}
BENCHMARK(BenchmarkCalcColorTransforms_pLook_G22);

static void BenchmarkCalcColorTransforms_pLook_PQ(benchmark::State &state)
{
    BenchmarkCalcColorTransform_pLook(EOTF_PQ, state);
}
BENCHMARK(BenchmarkCalcColorTransforms_pLook_PQ);

static void BenchmarkCalcColorTransforms_pLook(benchmark::State &state)
{
    for ( uint32_t nInputEOTF = 0; nInputEOTF < EOTF_Count; nInputEOTF++ )
        BenchmarkCalcColorTransform_pLook((EOTF)nInputEOTF, state);
}
BENCHMARK(BenchmarkCalcColorTransforms_pLook);

static void BenchmarkCalcColorTransforms_pLookOriginal_G22(benchmark::State &state)
{
    BenchmarkCalcColorTransform_pLookOriginal(EOTF_Gamma22, state);
}
BENCHMARK(BenchmarkCalcColorTransforms_pLookOriginal_G22);

static void BenchmarkCalcColorTransforms_pLookOriginal_PQ(benchmark::State &state)
{
    BenchmarkCalcColorTransform_pLookOriginal(EOTF_PQ, state);
}
BENCHMARK(BenchmarkCalcColorTransforms_pLookOriginal_PQ);

static void BenchmarkCalcColorTransforms_pLookOriginal(benchmark::State &state)
{
    for ( uint32_t nInputEOTF = 0; nInputEOTF < EOTF_Count; nInputEOTF++ )
        BenchmarkCalcColorTransform_pLookOriginal((EOTF)nInputEOTF, state);
}
BENCHMARK(BenchmarkCalcColorTransforms_pLookOriginal);

static constexpr uint32_t k_uFindTestValueCountLarge = 524288;
static constexpr uint32_t k_uFindTestValueCountMedium = 16;
static constexpr uint32_t k_uFindTestValueCountSmall = 5;

template <uint32_t uSize>
static __attribute__((noinline)) std::array<int, uSize> GetFindTestValues()
{
    static std::array<int, uSize> s_Values = []()
    {
        std::array<int, uSize> values;
        for ( uint32_t i = 0; i < uSize; i++ )
            values[i] = rand() % 255;

        return values;
    }();

    return s_Values;
}

// Large

static void Benchmark_Find_Large_Gamescope(benchmark::State &state)
{
    std::array<int, k_uFindTestValueCountLarge> values = GetFindTestValues<k_uFindTestValueCountLarge>();

    for (auto _ : state)
    {
        auto iter = gamescope::Algorithm::Find( values.begin(), values.end(), 765678478 );
        benchmark::DoNotOptimize( iter );
    }
}
BENCHMARK(Benchmark_Find_Large_Gamescope);

static void Benchmark_Find_Large_Std(benchmark::State &state)
{
    std::array<int, k_uFindTestValueCountLarge> values = GetFindTestValues<k_uFindTestValueCountLarge>();

    for (auto _ : state)
    {
        auto iter = std::find( values.begin(), values.end(), 765678478 );
        benchmark::DoNotOptimize( iter );
    }
}
BENCHMARK(Benchmark_Find_Large_Std);

static void Benchmark_Contains_Large_Gamescope(benchmark::State &state)
{
    std::array<int, k_uFindTestValueCountLarge> values = GetFindTestValues<k_uFindTestValueCountLarge>();

    for (auto _ : state)
    {
        bool bContains = gamescope::Algorithm::ContainsNoShortcut( values.begin(), values.end(), 765678478 );
        benchmark::DoNotOptimize( bContains );
    }
}
BENCHMARK(Benchmark_Contains_Large_Gamescope);

//

static void Benchmark_Find_Medium_Gamescope(benchmark::State &state)
{
    std::array<int, k_uFindTestValueCountMedium> values = GetFindTestValues<k_uFindTestValueCountMedium>();

    for (auto _ : state)
    {
        auto iter = gamescope::Algorithm::Find( values.begin(), values.end(), 765678478 );
        benchmark::DoNotOptimize( iter );
    }
}
BENCHMARK(Benchmark_Find_Medium_Gamescope);

static void Benchmark_Find_Medium_Std(benchmark::State &state)
{
    std::array<int, k_uFindTestValueCountMedium> values = GetFindTestValues<k_uFindTestValueCountMedium>();

    for (auto _ : state)
    {
        auto iter = std::find( values.begin(), values.end(), 765678478 );
        benchmark::DoNotOptimize( iter );
    }
}
BENCHMARK(Benchmark_Find_Medium_Std);

static void Benchmark_Contains_Medium_Gamescope(benchmark::State &state)
{
    std::array<int, k_uFindTestValueCountMedium> values = GetFindTestValues<k_uFindTestValueCountMedium>();

    for (auto _ : state)
    {
        bool bContains = gamescope::Algorithm::ContainsNoShortcut( values.begin(), values.end(), 765678478 );
        benchmark::DoNotOptimize( bContains );
    }
}
BENCHMARK(Benchmark_Contains_Medium_Gamescope);

//

static void Benchmark_Find_Small_Gamescope(benchmark::State &state)
{
    std::array<int, k_uFindTestValueCountSmall> values = GetFindTestValues<k_uFindTestValueCountSmall>();

    for (auto _ : state)
    {
        auto iter = gamescope::Algorithm::Find( values.begin(), values.end(), 765678478 );
        benchmark::DoNotOptimize( iter );
    }
}
BENCHMARK(Benchmark_Find_Small_Gamescope);

static void Benchmark_Find_Small_Std(benchmark::State &state)
{
    std::array<int, k_uFindTestValueCountSmall> values = GetFindTestValues<k_uFindTestValueCountSmall>();

    for (auto _ : state)
    {
        auto iter = std::find( values.begin(), values.end(), 765678478 );
        benchmark::DoNotOptimize( iter );
    }
}
BENCHMARK(Benchmark_Find_Small_Std);

static void Benchmark_Contains_Small_Gamescope(benchmark::State &state)
{
    std::array<int, k_uFindTestValueCountSmall> values = GetFindTestValues<k_uFindTestValueCountSmall>();

    for (auto _ : state)
    {
        bool bContains = gamescope::Algorithm::ContainsNoShortcut( values.begin(), values.end(), 765678478 );
        benchmark::DoNotOptimize( bContains );
    }
}
BENCHMARK(Benchmark_Contains_Small_Gamescope);

BENCHMARK_MAIN();
