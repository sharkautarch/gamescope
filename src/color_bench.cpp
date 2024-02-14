#include <benchmark/benchmark.h>

#include "color_helpers.h"

const uint32_t nLutSize1d = 4096;
const uint32_t nLutEdgeSize3d = 17;

uint16_t lut1d[nLutSize1d*4];
uint16_t lut3d[nLutEdgeSize3d*nLutEdgeSize3d*nLutEdgeSize3d*4];

lut1d_t lut1d_float;
lut3d_t lut3d_float;

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
        calcColorTransform( &lut1d_float, nLutSize1d, &lut3d_float, nLutEdgeSize3d, inputColorimetry, inputEOTF,
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

BENCHMARK_MAIN();
