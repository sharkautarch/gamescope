#define COLOR_HELPERS_CPP
#include "color_helpers_impl.h"

FAST_MATH_ON

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <string>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/mat3x3.hpp>
#include <glm/gtx/matrix_operation.hpp>
#include <glm/gtx/string_cast.hpp>


glm::vec3 xyY_to_XYZ( const glm::vec2 & xy, float Y )
{
    if ( fabsf( xy.y ) < std::numeric_limits<float>::min() )
    {
        return glm::vec3( 0.f );
    }
    else
    {
        return glm::vec3( Y * xy.x / xy.y, Y, ( 1.f - xy.x - xy.y ) * Y / xy.y );
    }
}

glm::vec2 XYZ_to_xy( const glm::vec3 & XYZ )
{
    float sum = ( XYZ.x + XYZ.y + XYZ.z );
    if ( fabsf( sum ) < std::numeric_limits<float>::min() )
    {
        return glm::vec2( 0.f );
    }
    else
    {
        return glm::vec2( XYZ.x / sum, XYZ.y / sum );
    }
}

glm::vec3 xy_to_xyz( const glm::vec2 & xy )
{
    return glm::vec3( xy.x, xy.y, 1.f - xy.x - xy.y );
}

// Convert xy to CIE 1976 u'v'
glm::vec2 xy_to_uv( const glm::vec2 & xy )
{
    float denom = -2.f * xy.x + 12.f * xy.y + 3.f;
    if ( fabsf( denom ) < std::numeric_limits<float>::min() )
    {
        return glm::vec2( 0.f );
    }

    return glm::vec2( 4.f * xy.x / denom, 9.f * xy.y / denom );
}

// Convert CIE 1976 u'v' to xy
glm::vec2 uv_to_xy( const glm::vec2 & uv )
{
    float denom = 6.f * uv.x - 16.f * uv.y + 12.f;
    if ( fabsf( denom ) < std::numeric_limits<float>::min() )
    {
        return glm::vec2( 0.f );
    }

    return glm::vec2( 9.f * uv.x / denom, 4.f * uv.y / denom );
}

glm::mat3 normalised_primary_matrix( const primaries_t & rgbPrimaries, const glm::vec2 & whitePrimary, float whiteLuminance )
{
    glm::mat3 matPrimaries( xy_to_xyz( rgbPrimaries.r ), xy_to_xyz( rgbPrimaries.g ), xy_to_xyz( rgbPrimaries.b ) );
    glm::vec3 whiteXYZ = xyY_to_XYZ( whitePrimary, whiteLuminance );
    glm::mat3 whiteScale = glm::diagonal3x3( glm::inverse( matPrimaries ) * whiteXYZ );
    return matPrimaries * whiteScale;
}

glm::mat3 chromatic_adaptation_matrix( const glm::vec3 & sourceWhiteXYZ, const glm::vec3 & destWhiteXYZ,
    EChromaticAdaptationMethod eMethod )
{
    static const glm::mat3 k_matBradford( 0.8951f,-0.7502f, 0.0389f, 0.2664f,1.7135f,  -0.0685f, -0.1614f,  0.0367f, 1.0296f );
    glm::mat3 matAdaptation = eMethod == k_EChromaticAdapatationMethod_XYZ ? glm::diagonal3x3( glm::vec3(1) ) : k_matBradford;
    glm::vec3 coneResponseDest = matAdaptation * destWhiteXYZ;
    glm::vec3 coneResponseSource = matAdaptation * sourceWhiteXYZ;
    glm::vec3 scale = coneResponseDest/coneResponseSource;
    return glm::inverse( matAdaptation ) * glm::diagonal3x3( scale ) * matAdaptation;
}

displaycolorimetry_t lerp( const displaycolorimetry_t & a, const displaycolorimetry_t & b, float t )
{
    displaycolorimetry_t result;
    primaries_t a_uv, b_uv;
    a_uv.r = xy_to_uv( a.primaries.r );
    a_uv.g = xy_to_uv( a.primaries.g );
    a_uv.b = xy_to_uv( a.primaries.b );

    b_uv.r = xy_to_uv( b.primaries.r );
    b_uv.g = xy_to_uv( b.primaries.g );
    b_uv.b = xy_to_uv( b.primaries.b );

    glm::vec2 a_white = xy_to_uv( a.white );
    glm::vec2 b_white = xy_to_uv( b.white );

    result.primaries.r.x = flerp( a_uv.r.x, b_uv.r.x, t );
    result.primaries.r.y = flerp( a_uv.r.y, b_uv.r.y, t );
    result.primaries.g.x = flerp( a_uv.g.x, b_uv.g.x, t );
    result.primaries.g.y = flerp( a_uv.g.y, b_uv.g.y, t );
    result.primaries.b.x = flerp( a_uv.b.x, b_uv.b.x, t );
    result.primaries.b.y = flerp( a_uv.b.y, b_uv.b.y, t );
    result.white.x = flerp( a_white.x, b_white.x, t );
    result.white.y = flerp( a_white.y, b_white.y, t );

    result.primaries.r = uv_to_xy( result.primaries.r );
    result.primaries.g = uv_to_xy( result.primaries.g );
    result.primaries.b = uv_to_xy( result.primaries.b );
    result.white = uv_to_xy( result.white );

    return result;
}

colormapping_t lerp( const colormapping_t & a, const colormapping_t & b, float t )
{
    colormapping_t result;
    result.blendEnableMinSat = flerp( a.blendEnableMinSat, b.blendEnableMinSat, t );
    result.blendEnableMaxSat = flerp( a.blendEnableMaxSat, b.blendEnableMaxSat, t );
    result.blendAmountMin = flerp( a.blendAmountMin, b.blendAmountMin, t );
    result.blendAmountMax = flerp( a.blendAmountMax, b.blendAmountMax, t );
    return result;
}

bool LoadCubeLut( lut3d_t * lut3d, const char * filename )
{
    // R changes fastest
    // ...
    // LUT_3D_SIZE %d(lutEdgeSize)
    // %f %f %f
    // ...

    lut3d->lutEdgeSize = 0;
    lut3d->data.clear();

    std::ifstream lutfile( filename );
    if ( !lutfile.is_open() || lutfile.bad() )
        return false;

    std::string line;
    while ( std::getline( lutfile, line ) )
    {
        if ( lut3d->lutEdgeSize )
        {
            glm::vec3 val;
            if ( sscanf( line.c_str(), "%f %f %f", &val.r, &val.g, &val.b ) == 3 )
            {
                lut3d->data.push_back( val );
            }
        }
        else if ( sscanf( line.c_str(), "LUT_3D_SIZE %d", &lut3d->lutEdgeSize ) == 1 )
        {
            if ( lut3d->lutEdgeSize < 2 || lut3d->lutEdgeSize > 128 ) // sanity check
            {
                return false;
            }
            lut3d->data.reserve( lut3d->lutEdgeSize * lut3d->lutEdgeSize * lut3d->lutEdgeSize );
        }
    }

    int nExpectedElements = lut3d->lutEdgeSize * lut3d->lutEdgeSize * lut3d->lutEdgeSize;
    bool bValid = ( nExpectedElements > 0 && ( nExpectedElements == (int) lut3d->data.size() ) );
    if ( !bValid )
    {
        lut3d->lutEdgeSize = 0;
        lut3d->data.clear();
    }

    return bValid;
}

int GetLut3DIndexRedFastRGB(int indexR, int indexG, int indexB, int dim)
{
    return (indexR + (int)dim * (indexG + (int)dim * indexB));
}

int GetLut3DIndexRedFastRGB(glm::ivec3 iv, int dim)
{
    return (iv[0] + dim * (iv[1] + dim * iv[2]));
}
using aligned_ivec3 = glm::vec<3, int, (glm::qualifier)3>;
using aligned_ivec2 = glm::vec<3, int, (glm::qualifier)3>;
int GetLut3DIndexRedFastRGB(aligned_ivec3 iv, glm::ivec3 ivDim)
{
    glm::ivec3 compMultiplied = iv*aligned_ivec3(ivDim); //{iv0, iv1*dim, iv2*dim*dim}
    iv.aligned_ivec3::~aligned_ivec3();
    using TV = glm::ivec3;
    ivDim.TV::~TV();
    
    //when doing a horizontal add, we only extract out the final scalar value from the vector
    //at the end, so that we don't end up extracting scalars from vector registers multiple times 
    return (glm::ivec2(compMultiplied) + compMultiplied.zy() + compMultiplied.yx())[0];
}


int GetLut3DIndexRedFastRGB(aligned_ivec2 iv, int iv3, glm::ivec3 ivDim)
{
    glm::ivec2 compMultiplied = iv*aligned_ivec2{ivDim}; //{iv0, iv1*dim, iv2*dim*dim}
    iv.aligned_ivec2::~aligned_ivec2();
    using TV = glm::ivec3;
    ivDim.TV::~TV();
    
    //when doing a horizontal add, we only extract out the final scalar value from the vector
    //at the end, so that we don't end up extracting scalars from vector registers multiple times 
    return (glm::ivec1(compMultiplied) + compMultiplied.y)[0] + iv3*ivDim[2];
}


aligned_ivec3 GetDimAsPowSeries(int dim) 
{
	return aligned_ivec3{1, dim, dim*dim};
}

// Linear
inline void lerp_rgb(float* out, const float* a, const float* b, const float* z)
{
    out[0] = (b[0] - a[0]) * z[0] + a[0];
    out[1] = (b[1] - a[1]) * z[1] + a[1];
    out[2] = (b[2] - a[2]) * z[2] + a[2];
}

inline glm::vec3 lerp_rgb(glm::vec3 a, glm::vec3 b, glm::vec3 z)
{
		return (b-a) * z + a;
}

// Bilinear
inline void lerp_rgb(float* out, const float* a, const float* b, const float* c,
                     const float* d, const float* y, const float* z)
{
    float v1[3];
    float v2[3];
    lerp_rgb(v1, a, b, z);
    lerp_rgb(v2, c, d, z);
    lerp_rgb(out, v1, v2, y);
}

inline glm::vec3 lerp_rgb(
	glm::vec3 const& __restrict__ a, 
	glm::vec3 const& __restrict__ b,
	glm::vec3 const& __restrict__ c,
  glm::vec3 const& __restrict__ d,
  glm::vec3 const& __restrict__ y,
  glm::vec3 const& __restrict__ z)
{
    glm::vec3 v1 = lerp_rgb(a, b, z);
    glm::vec3 v2 = lerp_rgb(c, d, z);
    return lerp_rgb(v1, v2, y);
}

// Trilinear
inline void lerp_rgb(float* out, const float* a, const float* b, const float* c, const float* d,
                     const float* e, const float* f, const float* g, const float* h,
                     const float* x, const float* y, const float* z)
{
    float v1[3];
    float v2[3];
    lerp_rgb(v1, a,b,c,d,y,z);
    lerp_rgb(v2, e,f,g,h,y,z);
    lerp_rgb(out, v1, v2, x);
}

inline glm::vec3 lerp_rgb(
	glm::vec3 const& __restrict__ a, 
	glm::vec3 const& __restrict__ b,
	glm::vec3 const& __restrict__ c,
	glm::vec3 const& __restrict__ d,
  glm::vec3 const& __restrict__ e,
  glm::vec3 const& __restrict__ f,
  glm::vec3 const& __restrict__ g,
  glm::vec3 const& __restrict__ h,
  glm::vec3 const& __restrict__ x,
  glm::vec3 const& __restrict__ y,
  glm::vec3 const& __restrict__ z)
{
    glm::vec3 v1 = lerp_rgb(a,b,c,d,y,z);
    glm::vec3 v2 = lerp_rgb(e,f,g,h,y,z);
    return lerp_rgb(v1, v2, x);
}
using avec4 = glm::vec<4, float, (glm::qualifier)3>;
inline avec4 ClampAndSanitize( avec4 a, float min, float max )
{
#if !( defined(__FAST_MATH__) || defined(__FINITE_MATH_ONLY__) )
    return std::isfinite( a.x ) ? glm::min(glm::max(a, min), max) : glm::vec3{min};
#else
    return glm::min(glm::max(a, min), max);
#endif
}
inline avec4 ClampAndSanitize( glm::vec3 a, float min, float max )
{
		avec4 aa = avec4{a};
#if !( defined(__FAST_MATH__) || defined(__FINITE_MATH_ONLY__) )
    return std::isfinite( aa.x ) ? glm::min(glm::max(aa, min), max) : glm::vec3{min};
#else
    return glm::min(glm::max(aa, min), max);
#endif
}

// Adapted from:
// https://github.com/AcademySoftwareFoundation/OpenColorIO/ops/lut3d/Lut3DOpCPU.cpp
// License available in their repo and in our LICENSE file.

inline glm::vec3 ApplyLut3D_Trilinear( const lut3d_t & lut3d, const glm::vec3 & input )
{
    const float dimMinusOne = float(lut3d.lutEdgeSize) - 1.f;

		// NaNs become 0.
    auto idx = ClampAndSanitize(input * dimMinusOne, 0.f, dimMinusOne);

    glm::ivec3 indexLow = (glm::ivec3)glm::floor(idx);

		// When the idx is exactly equal to an index (e.g. 0,1,2...)
    // then the computation of highIdx is wrong. However,
    // the delta is then equal to zero (e.g. idx-lowIdx),
    // so the highIdx has no impact.
  	glm::ivec3 indexHigh = (glm::ivec3)glm::ceil(idx);
  	glm::ivec4 indexHighLow = glm::ivec4{ indexHigh.zy(), indexLow.xy() }; //{ indexHigh[2], indexHigh[1], indexLow[0], indexLow[1] }
  	glm::ivec4 indexLowHigh = glm::ivec4{ indexLow.zy(), indexHigh.xy() }; //{ indexLow[2], indexLow[1], indexHigh[0], indexHigh[1] }

    glm::vec3 delta = (glm::vec3)idx - (glm::vec3)indexLow;

    // Compute index into LUT for surrounding corners
    const int n000 =
        GetLut3DIndexRedFastRGB(indexHighLow.zw(), indexLowHigh.x, lut3d.lutEdgeSize);
    const int n100 =
        GetLut3DIndexRedFastRGB(indexLowHigh.zyx(), lut3d.lutEdgeSize);
    const int n010 =
        GetLut3DIndexRedFastRGB(indexHighLow.zy(), indexLowHigh.x, lut3d.lutEdgeSize);
    const int n001 =
        GetLut3DIndexRedFastRGB(indexHighLow.zwx(), lut3d.lutEdgeSize);
    const int n110 =
        GetLut3DIndexRedFastRGB(indexLowHigh.zwx(), lut3d.lutEdgeSize);
    const int n101 =
        GetLut3DIndexRedFastRGB(glm::ivec3{ indexLowHigh.zy(), indexHighLow.x }, lut3d.lutEdgeSize);
    const int n011 =
        GetLut3DIndexRedFastRGB(indexHighLow.zyx(), lut3d.lutEdgeSize);
    const int n111 =
        GetLut3DIndexRedFastRGB(glm::ivec3{indexLowHigh.zw(), indexHighLow.x}, lut3d.lutEdgeSize);

    glm::vec3 x {delta[0]}, y{delta[1]}, z{delta[2]};

    glm::vec3 out;
    lerp_rgb((float *) &out,
        (float *) &lut3d.data[n000].r, (float *) &lut3d.data[n001].r,
        (float *) &lut3d.data[n010].r, (float *) &lut3d.data[n011].r,
        (float *) &lut3d.data[n100].r, (float *) &lut3d.data[n101].r,
        (float *) &lut3d.data[n110].r, (float *) &lut3d.data[n111].r,
        std::bit_cast<float*>(&x), std::bit_cast<float*>(&y), std::bit_cast<float*>(&z));

    return out;
}
using avec3 = glm::vec<3, float, (glm::qualifier)3>;
inline glm::vec3 __attribute__((flatten)) ApplyLut3D_Tetrahedral( const lut3d_t & lut3d, const glm::vec3 input )
{
		const int l3dEdgeSize = lut3d.lutEdgeSize;
		if (l3dEdgeSize < 0) {
			__builtin_unreachable();
		}
    const float dimMinusOne = float(l3dEdgeSize) - 1.f;

    // NaNs become 0.
    auto idx = ClampAndSanitize(avec4{ avec3(input * dimMinusOne), 0.f}, 0.f, dimMinusOne);
    glm::ivec3 ivIndexLow = (glm::ivec3)glm::floor(idx);
		const auto yzxMinusXyz = glm::vec3((idx - glm::floor(idx)).yzxw() - (idx - glm::floor(idx)));
		const auto delta = glm::vec4(idx - glm::floor(idx));
		// {y>x, z>y, x>z}
		// if x > y && y > z -> yzxMinusXyz : {-,-,>0} lessThan(yzxMinusXyz, 0) = {true, true, false}
		// if x > y && y <= z && x > z -> yzxMinusXyz : {-,>=0,>0} lessThan(yzxMinusXyz, 0) = {true, false, false} | {false, true, false} | greaterThan(yzxMinusXyz, 0) = {true, true, true}
		// if x > y && z >= x -> yzxMinusXyz : {-,>=0,<=0} lessThan(yzxMinusXyz, 0) = {true, false, ?} | {false, true, false} | greaterThan(yzxMinusXyz, 0) = {true, true, false}
		// if y >= x && z > y -> yzxMinusXyz : {>=0,>0,>0} {true, false, false} | greaterThan(yzxMinusXyz, 0) = {true, true, true}
		// if y >= x && z <= y && z > x -> yzxMinusXyz : {>=0,<=0,<0} lessThan(yzxMinusXyz, 0) | {true, true, false} == {true, true, true}
		// if y >= x && z <= x -> yzxMinusXyz : {>=0,<=0,>=0} lessThan(yzxMinusXyz, 0) | {true, true, false} == {true, true, false}
		// When the idx is exactly equal to an index (e.g. 0,1,2...)
    // then the computation of highIdx is wrong. However,
    // the delta is then equal to zero (e.g. idx-lowIdx),
    // so the highIdx has no impact.
  	glm::ivec3 indexHigh = (glm::ivec3)glm::ceil(idx);
  	glm::ivec4 indexHighLow = glm::ivec4{ indexHigh.zy(), glm::ivec2{ivIndexLow} }; //{ indexHigh[2], indexHigh[1], indexLow[0], indexLow[1] }
  	glm::ivec4 indexLowHigh = glm::ivec4{ ivIndexLow.zy(), glm::ivec2{indexHigh} }; //{ indexLow[2], indexLow[1], indexHigh[0], indexHigh[1] }

		using TV = glm::ivec3;
		indexHigh.TV::~TV();
		ivIndexLow.TV::~TV();
    // Compute index into LUT for surrounding corners
    glm::ivec3 edgeSizeSeries = GetDimAsPowSeries(l3dEdgeSize);
    const int n000 =
        GetLut3DIndexRedFastRGB(indexHighLow.zw(), indexLowHigh[0], edgeSizeSeries);
    const int n100 =
        GetLut3DIndexRedFastRGB(indexLowHigh.zyx(), edgeSizeSeries);
    const int n010 =
        GetLut3DIndexRedFastRGB(indexHighLow.zy(), indexLowHigh[0], edgeSizeSeries);
    const int n001 =
        GetLut3DIndexRedFastRGB(indexHighLow.zwx(), edgeSizeSeries);
    const int n110 =
        GetLut3DIndexRedFastRGB(indexLowHigh.zwx(), edgeSizeSeries);
    const int n101 =
        GetLut3DIndexRedFastRGB(indexLowHigh.zy(), indexHighLow[0], edgeSizeSeries);
    const int n011 =
        GetLut3DIndexRedFastRGB(indexHighLow.zyx(), edgeSizeSeries);
    const int n111 =
        GetLut3DIndexRedFastRGB(indexLowHigh.zw(), indexHighLow[0], edgeSizeSeries);


    using mat4x3 = glm::mat<4,3,float,(glm::qualifier)0>;
    glm::vec3 ldzero = lut3d.data[n000];
    constexpr auto bvecToByte = [](glm::bvec3 bv) -> uint8_t {
    	return bv[0] | (bv[1] <<1) | (bv[2] << 2);
    };
    auto comp = bvecToByte(glm::lessThan(yzxMinusXyz, glm::vec3(0.f)));
    if ( constexpr auto check = bvecToByte(glm::bvec3{true, true, false}); comp == check  ) 
    {
    	//x > y && y > z
    	mat4x3  ldata = mat4x3{ldzero, lut3d.data[n100], lut3d.data[n110], lut3d.data[n111]};
    	auto v1 = delta.wxyz();
    	return glm::operator*(ldata,glm::vec4(v1 - delta)) + ldzero;
    } else if ( constexpr auto orWith = bvecToByte(glm::bvec3{false, true, true}),
    	 					check = bvecToByte(glm::bvec3{true});
    	 					 (comp | orWith) == check ) {
    	// if x > y && y <= z && x > z -> yzxMinusXyz : {-,>=0,>0} lessThan(yzxMinusXyz, 0) = {true, false, false} | {false, true, false} | greaterThan(yzxMinusXyz, 0) = {true, true, true}
		// if x > y && z >= x -> yzxMinusXyz : {-,>=0,<=0} lessThan(yzxMinusXyz, 0) = {true, false, ?} | {false, true, false} | greaterThan(yzxMinusXyz, 0) = {true, true, false}
    	if ( constexpr auto orWith = bvecToByte(glm::bvec3{false, true, false}),
    			 check = bvecToByte(glm::bvec3(true));
    				(comp | orWith | bvecToByte(glm::greaterThan(yzxMinusXyz, glm::vec3(0.f)))) == check ) {
    		// x > y && y <= z && x > z
    		mat4x3 ldata = mat4x3{ldzero, lut3d.data[n100], lut3d.data[n101],  lut3d.data[n111]};
    		auto v1 = delta.wxzy();
    		return glm::operator*(ldata,(v1 - delta)) + ldzero;
    	} else {
    		//x > y && z >= x
    		mat4x3 ldata = mat4x3{ldzero, lut3d.data[n001], lut3d.data[n101], lut3d.data[n111]};
    		auto v1 = delta.wzxy();
    		return glm::operator*(ldata,(v1 - delta)) + ldzero;
    	}
    		
    } else {
    	static constexpr auto gtOrWith = bvecToByte(glm::bvec3{true, false, false});
    	auto compGt = bvecToByte(glm::greaterThan(yzxMinusXyz, glm::vec3(0.f))) | gtOrWith;
    	if ( constexpr auto check = bvecToByte(glm::bvec3{true}); compGt == check ) {
    		//y >= x && z > y
    		mat4x3 ldata = mat4x3{ldzero, lut3d.data[n001], lut3d.data[n011], lut3d.data[n111]};
    		auto v1 = delta.wzyx();
    		return glm::operator*(ldata,(v1 - delta)) + ldzero;
    	
    	} else if ( constexpr auto orWith = bvecToByte(glm::bvec3{true, true, false}),
    							check = bvecToByte(glm::bvec3{true});
    							(comp | orWith) == check ) {
    		//y >= x && z <= y && z > x
    		mat4x3 ldata = mat4x3{ldzero, lut3d.data[n010], lut3d.data[n011], lut3d.data[n111]};
    		auto v1 = delta.wyzx();
    		return glm::operator*(ldata,(v1 - delta)) + ldzero;
    	} else {
    		//y >= x && z <= x
    		mat4x3 ldata = mat4x3{ldzero, lut3d.data[n010], lut3d.data[n110], lut3d.data[n111]};
    		auto v1 = delta.wyxz();
    		return glm::operator*(ldata,(v1 - delta)) + ldzero;
    	}
    	
    }

#if 0
    if (fx > fy) {
        if (fy > fz) {
            out =
                (1 - fx)  * lut3d.data[n000] +
                (fx - fy) * lut3d.data[n100] +
                (fy - fz) * lut3d.data[n110] +
                (fz)      * lut3d.data[n111];
        }
        else if (fx > fz)
        {
            out =
                (1 - fx)  * lut3d.data[n000] +
                (fx - fz) * lut3d.data[n100] +
                (fz - fy) * lut3d.data[n101] +
                (fy)      * lut3d.data[n111];
        }
        else
        {
            out =
                (1 - fz)  * lut3d.data[n000] +
                (fz - fx) * lut3d.data[n001] +
                (fx - fy) * lut3d.data[n101] +
                (fy)      * lut3d.data[n111];
        }
    }
    else
    {
        if (fz > fy)
        {
            out =
                (1 - fz)  * lut3d.data[n000] +
                (fz - fy) * lut3d.data[n001] +
                (fy - fx) * lut3d.data[n011] +
                (fx)      * lut3d.data[n111];
        }
        else if (fz > fx)
        {
            out =
                (1 - fy)  * lut3d.data[n000] +
                (fy - fz) * lut3d.data[n010] +
                (fz - fx) * lut3d.data[n011] +
                (fx)      * lut3d.data[n111];
        }
        else
        {
            out =
                (1 - fy)  * lut3d.data[n000] +
                (fy - fx) * lut3d.data[n010] +
                (fx - fz) * lut3d.data[n110] +
                (fz)      * lut3d.data[n111];
        }
    }
#endif

}


inline glm::vec3 ApplyLut1D_Linear( const lut1d_t & lut, const glm::vec3 & input )
{
    const float dimMinusOne = float(lut.lutSize) - 1.f;
    // NaNs become 0.
    auto idx = ClampAndSanitize(input * dimMinusOne, 0.f, dimMinusOne);

    glm::ivec3 indexLow = (glm::ivec3) glm::floor(idx);

    // When the idx is exactly equal to an index (e.g. 0,1,2...)
    // then the computation of highIdx is wrong. However,
    // the delta is then equal to zero (e.g. idx-lowIdx),
    // so the highIdx has no impact.
    glm::ivec3 indexHigh = (glm::ivec3)glm::ceil(idx);

    glm::vec3 delta = (glm::vec3)idx - (glm::vec3)indexLow;

    auto vLow = glm::vec3{ lut.dataR[indexLow[0]], lut.dataG[indexLow[1]], lut.dataB[indexLow[2]] };
    auto vHigh = glm::vec3{ lut.dataR[indexHigh[0]], lut.dataG[indexHigh[1]], lut.dataB[indexHigh[2]] };

    return lerp_rgb( vLow, vHigh, delta );
}

// Calculate the inverse of a value resulting from linear interpolation
// in a 1d LUT.
// start:       Pointer to the first effective LUT entry (end of flat spot).
// startOffset: Distance between first LUT entry and start.
// end:         Pointer to the last effective LUT entry (start of flat spot).
// scale:       From LUT index units to outDepth units.
// val:         The value to invert.
// Return the result that would produce val if used
// in a forward linear interpolation in the LUT.
inline float FindLutInv(const float * start,
                 const float   startOffset,
                 const float * end,
                 const float   scale,
                 const float   val)
{
    // Note that the LUT data pointed to by start/end must be in increasing order,
    // regardless of whether the original LUT was increasing or decreasing because
    // this function uses std::lower_bound().

    // Clamp the value to the range of the LUT.
    const float cv = std::min( std::max( val, *start ), *end );

    // std::lower_bound()
    // "Returns an iterator pointing to the first element in the range [first,last)
    // which does not compare less than val (but could be equal)."
    // (NB: This is correct using either end or end+1 since lower_bound will return a
    //  value one greater than the second argument if no values in the array are >= cv.)
    // http://www.sgi.com/tech/stl/lower_bound.html
    const float* lowbound = std::lower_bound(start, end, cv);

    // lower_bound() returns first entry >= val so decrement it unless val == *start.
    if (lowbound > start) {
        --lowbound;
    }

    const float* highbound = lowbound;
    if (highbound < end) {
        ++highbound;
    }

    // Delta is the fractional distance of val between the adjacent LUT entries.
    float delta = 0.f;
    if (*highbound > *lowbound) {   // (handle flat spots by leaving delta = 0)
        delta = (cv - *lowbound) / (*highbound - *lowbound);
    }

    // Inds is the index difference from the effective start to lowbound.
    const float inds = (float)( lowbound - start );

    // Correct for the fact that start is not the beginning of the LUT if it
    // starts with a flat spot.
    // (NB: It may seem like lower_bound would automatically find the end of the
    //  flat spot, so start could always simply be the start of the LUT, however
    //  this fails when val equals the flat spot value.)
    const float totalInds = inds + startOffset;

    // Scale converts from units of [0,dim] to [0,outDepth].
    return (totalInds + delta) * scale;
}

int FindNonFlatStartIndex( const std::vector<float> & data )
{
    if ( !data.empty() )
    {
        for ( size_t nIndex = 1; nIndex < data.size(); ++nIndex )
        {
            if ( data[nIndex] != data[0] )
            {
                return nIndex - 1;
            }
        }
    }

    return 0;
}

void lut1d_t::finalize()
{
    startIndexR = FindNonFlatStartIndex( dataR );
    startIndexG = FindNonFlatStartIndex( dataG );
    startIndexB = FindNonFlatStartIndex( dataB );
}

inline glm::vec3 ApplyLut1D_Inverse_Linear( const lut1d_t & lut, const glm::vec3 & input )
{
    // Disallow inverse if not finalized
    if ( lut.startIndexR < 0 )
    {
        return glm::vec3( -1.f );
    }

    return glm::vec3(
        FindLutInv( lut.dataR.data() + lut.startIndexR, lut.startIndexR, lut.dataR.data() + lut.dataR.size() - 1, 1.f / ( lut.dataR.size() - 1.f ), input.r ),
        FindLutInv( lut.dataG.data() + lut.startIndexG, lut.startIndexG, lut.dataG.data() + lut.dataG.size() - 1, 1.f / ( lut.dataG.size() - 1.f ), input.g ),
        FindLutInv( lut.dataB.data() + lut.startIndexB, lut.startIndexB, lut.dataB.data() + lut.dataB.size() - 1, 1.f / ( lut.dataB.size() - 1.f ), input.b ) );
}

inline glm::vec3 hsv_to_rgb( const glm::vec3 & hsv )
{
    if ( fabsf( hsv.y ) < std::numeric_limits<float>::min() )
    {
       return glm::vec3( hsv.z ) ;
    }

    float flHue = positive_mod( hsv.x, 1.f );
    flHue *= 6.f;

    int i = flHue;        // integer part
	float f = flHue - i;    // fractional part

	float p = hsv.z * ( 1.f - hsv.y );
	float q = hsv.z * ( 1.f - hsv.y * f );
	float t = hsv.z * ( 1.f - hsv.y * ( 1.f - f ) );

	switch(i)
	{
	case 0: return glm::vec3( hsv.z, t, p ); break;
	case 1: return glm::vec3( q, hsv.z, p ); break;
	case 2: return glm::vec3( p, hsv.z, t ); break;
	case 3: return glm::vec3( p, q, hsv.z ); break;
	case 4: return glm::vec3( t, p, hsv.z ); break;
	case 5: return glm::vec3( hsv.z, p, q ); break;
	}

    return glm::vec3( 0 );
}


inline glm::vec3 rgb_to_hsv( const glm::vec3 & rgb )
{
    float flMax = std::max( std::max( rgb.x, rgb.y ), rgb.z );
    float flMin = std::min( std::min( rgb.x, rgb.y ), rgb.z );
    float flDelta = flMax - flMin;

    glm::vec3 hsv;
    hsv.y = ( fabsf( flMax ) < std::numeric_limits<float>::min() ) ? 0.f : flDelta / flMax;
    hsv.z = flMax;

    if (hsv.y == 0.f)
    {
        hsv.x = -1.0f;
    }
    else
    {
        if ( rgb.x == flMax )
        {
            hsv.x = (rgb.y - rgb.z) / flDelta;
        }
        else if ( rgb.y == flMax )
        {
            hsv.x = 2.f + ( rgb.z - rgb.x ) / flDelta;
        }
        else
        {
            hsv.x = 4.f + ( rgb.x - rgb.y ) / flDelta;
        }

        hsv.x /= 6.f;

        if ( hsv.x < 0.f ) 
        {
            hsv.x += 1.f;
        }
    }

    return hsv;
}

bool BOutOfGamut( const glm::vec3 & color )
{
    return ( color.x<0.f || color.x > 1.f || color.y<0.f || color.y > 1.f || color.z<0.f || color.z > 1.f );
}

template <typename T, EOTF c_eotf = EOTF_Count>
inline T calcEOTFToLinear( const T & input, EOTF eotf, const tonemapping_t & tonemapping )
{
		if constexpr (c_eotf == EOTF_PQ) {
			return pq_to_nits( input );
		}
    if ( eotf == EOTF_Gamma22 )
    {
        return glm::fastPow( input, T( 2.2f ) ) * tonemapping.g22_luminance;
    }
    else if ( eotf == EOTF_PQ )
    {
        return pq_to_nits( input );
    } else { __builtin_unreachable(); }
}

template <typename T, EOTF c_eotf = EOTF_Count>
inline T calcLinearToEOTF( const T & input, EOTF eotf, const tonemapping_t & tonemapping )
{
		if constexpr (c_eotf == EOTF_PQ) {
			return nits_to_pq( input );
		}
		
    if ( eotf == EOTF_Gamma22 )
    {
        T val = input;
        if ( tonemapping.g22_luminance > 0.f )
        {
            val = glm::clamp( input / tonemapping.g22_luminance, T( 0.f ), T( 1.f ) );
        }
        return glm::fastPow( val, T( 1.f/2.2f ) );
    }
    else if ( eotf == EOTF_PQ )
    {
        return nits_to_pq(input);
    } else { __builtin_unreachable(); }
}

// input is from 0->1
// TODO: use tone-mapping for white, black, contrast ratio

bool g_bUseSourceEOTFForShaper = false;

//applyShaper seems to be a pretty hot function from testing w/ gamescope_color_microbenchmark
//from testing, it seems that by marking this function w flatten attribute,
//we get way better results compared to baseline, and compared to trying to mark this or other functions w/ always_inline.
//It is likely that by only promoting aggressive inlining into applyShaper, w/o inlining more things outside of it,
//that we hit the sweetspot in balancing instruction cache usage vs function call overhead.  
template <typename T>
inline T __attribute__((flatten, no_stack_protector)) applyShaper( const T & input, EOTF source, EOTF dest, const tonemapping_t & tonemapping, float flGain )
{
    if ( ( source == dest && flGain == 1.f ) || !tonemapping.bUseShaper )
    {
        return input;
    }

		if (source == EOTF_PQ && dest == EOTF_PQ) {
			//this funky branch here helps reduce the amount of intermediate branching when source == dest == EOTF_PQ (not doing the same for EOTF_Gamma22 because it's already significantly faster than EOTF_PQ),
			//and from measurements from gamescope_color_microbench,
			//this, along with adding the flatten attrib to this function,
			//reduces BenchmarkCalcColorTransforms ns per iteration from ~913.986ns -> ~740.109ns
			//reduces BenchmarkCalcColorTransforms_PQ ns per iteration from ~564.575ns -> ~407.889ns
			//		^(note those two above stats are from compiling w/ c/cpp flags: "-fno-plt -march=native" & w/ meson flags: -Db_ndebug=true -Db_lto=true on an intel alderlake cpu)
			//		(compiling without -march=native [which limits simd stuff to sse2], BenchmarkCalcColorTransforms improves from ~2.058 micros per iteration -> ~1.638 micros per iteration, BenchmarkCalcColorTransforms_PQ improves from ~1.125 micros per iteration -> 899.500ns per iteration)
    	T flLinear = flGain * calcEOTFToLinear<T, EOTF_PQ>( input, EOTF_PQ, tonemapping );
    	flLinear = tonemapping.apply( flLinear );

    	return calcLinearToEOTF<T, EOTF_PQ>( flLinear, EOTF_PQ, tonemapping );
  	}

  	T flLinear = flGain * calcEOTFToLinear( input, source, tonemapping );
    flLinear = tonemapping.apply( flLinear );

    return calcLinearToEOTF( flLinear, g_bUseSourceEOTFForShaper ? source : dest, tonemapping );
}

bool g_bHuePreservationWhenClipping = false;

template <uint32_t lutEdgeSize3d>
void calcColorTransform( lut1d_t * pShaper, int nLutSize1d,
	lut3d_t * pLut3d,
	const displaycolorimetry_t & source, EOTF sourceEOTF,
	const displaycolorimetry_t & dest,  EOTF destEOTF,
    const glm::vec2 & destVirtualWhite, EChromaticAdaptationMethod eMethod,
    const colormapping_t & mapping, const nightmode_t & nightmode, const tonemapping_t & tonemapping,
    const lut3d_t * pLook, float flGain )
{
    // Generate shaper lut
    // Note: while this is typically a 1D approximation of our end to end transform,
    // it need not be! Conceptually this is just to determine the interpolation properties...
    // The 3d lut should be considered a 'matched' pair where the transform is only complete
    // when applying both.  I.e., you can put ANY transform in here, and it should work.

    static constexpr int32_t nLutEdgeSize3d = static_cast<int32_t>(lutEdgeSize3d);
    assert( (sourceEOTF==EOTF_Gamma22 || sourceEOTF==EOTF_PQ) && (destEOTF==EOTF_Gamma22 || destEOTF==EOTF_PQ) );
    if ( pShaper )
    {
        float flScale = 1.f / ( (float) nLutSize1d - 1.f );
        pShaper->resize( nLutSize1d );

        for ( int nVal=0; nVal<nLutSize1d; ++nVal )
        {
            glm::vec3 sourceColorEOTFEncoded = { nVal * flScale, nVal * flScale, nVal * flScale };
            glm::vec3 shapedSourceColor = applyShaper( sourceColorEOTFEncoded, sourceEOTF, destEOTF, tonemapping, flGain );
            pShaper->dataR[nVal] = shapedSourceColor.r;
            pShaper->dataG[nVal] = shapedSourceColor.g;
            pShaper->dataB[nVal] = shapedSourceColor.b;
        }

        pShaper->finalize();
    }

    if ( pLut3d )
    {
        glm::mat3 xyz_from_dest = normalised_primary_matrix( dest.primaries, dest.white, 1.f );
        glm::mat3 dest_from_xyz = glm::inverse( xyz_from_dest );

        glm::mat3 xyz_from_source = normalised_primary_matrix( source.primaries, source.white, 1.f );
        glm::mat3 dest_from_source = dest_from_xyz * xyz_from_source; // XYZ scaling for white point adjustment

        // Precalc night mode scalars & digital gain
        // amount and saturation are overdetermined but we separate the two as they conceptually represent
        // different quantities, and this preserves forwards algorithmic compatibility
        glm::vec3 nightModeMultHSV( nightmode.hue, clamp01( nightmode.saturation * nightmode.amount ), 1.f );
        glm::vec3 vMultLinear = glm::pow( hsv_to_rgb( nightModeMultHSV ), glm::vec3( 2.2f ) );
        vMultLinear = vMultLinear * flGain;

        // Calculate the virtual white point adaptation
        glm::mat3x3 whitePointDestAdaptation = glm::mat3x3( 1.f ); // identity
        if ( destVirtualWhite.x > 0.01f && destVirtualWhite.y > 0.01f )
        {
            // if source white is within tiny tolerance of sourceWhitePointOverride
            // don't do the override? (aka two quantizations of d65)
            glm::mat3x3 virtualWhiteXYZFromPhysicalWhiteXYZ = chromatic_adaptation_matrix(
                 xy_to_xyz( dest.white ), xy_to_xyz( destVirtualWhite ), eMethod );
            whitePointDestAdaptation = dest_from_xyz * virtualWhiteXYZFromPhysicalWhiteXYZ * xyz_from_dest;

            // Consider lerp-ing the gain limiting between 0-1? That would allow partial clipping
            // so that contrast ratios wouldnt be sacrified too bad with alternate white points
            static const bool k_bLimitGain = true;
            if ( k_bLimitGain )
            {
                glm::vec3 white = whitePointDestAdaptation * glm::vec3(1.f, 1.f, 1.f );
                float whiteMax = std::max( white.r, std::max( white.g, white.b ) );
                float normScale = 1.f / whiteMax;
                whitePointDestAdaptation = whitePointDestAdaptation * glm::diagonal3x3( glm::vec3( normScale ) );
            }
        }

        // Precalculate source color EOTF encoded per-edge.
        glm::vec3 vSourceColorEOTFEncodedEdge[nLutEdgeSize3d];
        float flEdgeScale = 1.f / ( (float) nLutEdgeSize3d - 1.f );
        for ( int nIndex = 0; nIndex < nLutEdgeSize3d; ++nIndex )
        {
            vSourceColorEOTFEncodedEdge[nIndex] = glm::vec3( nIndex * flEdgeScale );
            if ( pShaper )
            {
                vSourceColorEOTFEncodedEdge[nIndex] = ApplyLut1D_Inverse_Linear( *pShaper, vSourceColorEOTFEncodedEdge[nIndex] );
            }
        }

        pLut3d->resize( nLutEdgeSize3d );
    
        for ( int nBlue=0; nBlue<nLutEdgeSize3d; ++nBlue )
        {
            for ( int nGreen=0; nGreen<nLutEdgeSize3d; ++nGreen )
            {
                for ( int nRed=0; nRed<nLutEdgeSize3d; ++nRed )
                {
                    glm::vec3 sourceColorEOTFEncoded = glm::vec3( vSourceColorEOTFEncodedEdge[nRed].r, vSourceColorEOTFEncodedEdge[nGreen].g, vSourceColorEOTFEncodedEdge[nBlue].b );

                    if ( pLook && !pLook->data.empty() )
                    {
                        sourceColorEOTFEncoded = ApplyLut3D_Tetrahedral( *pLook, sourceColorEOTFEncoded );
                    }

                    // Convert to linearized display referred for source colorimetry
                    glm::vec3 sourceColorLinear = calcEOTFToLinear( sourceColorEOTFEncoded, sourceEOTF, tonemapping );

                    // Convert to dest colorimetry (linearized display referred)
                    glm::vec3 destColorLinear = dest_from_source * sourceColorLinear;

                    // Do a naive blending with native gamut based on saturation
                    // ( A very simplified form of gamut mapping )
                    // float colorSaturation = rgb_to_hsv( sourceColor ).y;
                    float colorSaturation = rgb_to_hsv( sourceColorLinear ).y;
                    float amount = cfit( colorSaturation, mapping.blendEnableMinSat, mapping.blendEnableMaxSat, mapping.blendAmountMin, mapping.blendAmountMax );
                    destColorLinear = glm::mix( destColorLinear, sourceColorLinear, amount );

                    // Apply linear Mult
                    destColorLinear = vMultLinear * destColorLinear;

                    // Apply destination virtual white point mapping
                    destColorLinear = whitePointDestAdaptation * destColorLinear;

                    // Apply tonemapping
                    destColorLinear = tonemapping.apply( destColorLinear );

                    // Hue preservation
                    if ( g_bHuePreservationWhenClipping )
                    {
                        float flMax = std::max( std::max( destColorLinear.r, destColorLinear.g ), destColorLinear.b );
                        // TODO: Don't use g22_luminance here or in tonemapping, use whatever maxContentLightLevel is for the connector.
                        if ( flMax > tonemapping.g22_luminance + 1.0f )
                        {
                            destColorLinear /= flMax;
                            destColorLinear *= tonemapping.g22_luminance;
                        }
                    }

                    // Apply dest EOTF
                    glm::vec3 destColorEOTFEncoded = calcLinearToEOTF( destColorLinear, destEOTF, tonemapping );

                    // Write LUT
                    pLut3d->data[GetLut3DIndexRedFastRGB( nRed, nGreen, nBlue, nLutEdgeSize3d )] = destColorEOTFEncoded;
                }
            }
        }
    }
}

bool BIsWideGamut( const displaycolorimetry_t & nativeDisplayOutput )
{
    // Use red as a sentinal for a wide-gamut display
    return ( nativeDisplayOutput.primaries.r.x > 0.650f && nativeDisplayOutput.primaries.r.y < 0.320f );
}

void buildSDRColorimetry( displaycolorimetry_t * pColorimetry, colormapping_t *pMapping,
	float flSDRGamutWideness, const displaycolorimetry_t & nativeDisplayOutput  )
{
    if ( BIsWideGamut( nativeDisplayOutput) )
    {
        // If not set, make it native.
        if (flSDRGamutWideness < 0 )
            flSDRGamutWideness = 1.0f;

        displaycolorimetry_t r709NativeWhite = displaycolorimetry_709;
        r709NativeWhite.white = nativeDisplayOutput.white;

        // 0.0: 709
        // 1.0: Native
        colormapping_t noRemap;
        noRemap.blendEnableMinSat = 0.7f;
        noRemap.blendEnableMaxSat = 1.0f;
        noRemap.blendAmountMin = 0.0f;
        noRemap.blendAmountMax = 0.0f;
        *pMapping = noRemap;
        *pColorimetry = lerp( r709NativeWhite, nativeDisplayOutput, flSDRGamutWideness );
    }
    else
    {
        // If not set, make it native.
        if (flSDRGamutWideness < 0 )
            flSDRGamutWideness = 0.0f;

        // 0.0: Native
        // 0.5: Generic wide gamut display w/smooth mapping
        // 1.0: Generic wide gamut display w/harsh mapping

        // This is a full blending to the unit cube, starting at 70% max sat
        // Creates a smooth transition from in-gamut to out of gamut
        colormapping_t smoothRemap;
        smoothRemap.blendEnableMinSat = 0.7f;
        smoothRemap.blendEnableMaxSat = 1.0f;
        smoothRemap.blendAmountMin = 0.0f;
        smoothRemap.blendAmountMax = 1.f;

        // Assume linear saturation computation
        // This is a partial (25%) blending to the unit cube
        // Allows some (but not full) clipping
        colormapping_t partialRemap;
        partialRemap.blendEnableMinSat = 0.7f;
        partialRemap.blendEnableMaxSat = 1.0f;
        partialRemap.blendAmountMin = 0.0f;
        partialRemap.blendAmountMax = 0.25;

        displaycolorimetry_t wideGamutNativeWhite = displaycolorimetry_widegamutgeneric;
        wideGamutNativeWhite.white = nativeDisplayOutput.white;

        if ( flSDRGamutWideness < 0.5f )
        {
            float t = cfit( flSDRGamutWideness, 0.f, 0.5f, 0.0f, 1.0f );
            *pColorimetry = lerp( nativeDisplayOutput, wideGamutNativeWhite, t );
            *pMapping = smoothRemap;
        }
        else
        {
            float t = cfit( flSDRGamutWideness, 0.5f, 1.0f, 0.0f, 1.0f );
            *pColorimetry = wideGamutNativeWhite;
            *pMapping = lerp( smoothRemap, partialRemap, t );
        }
    }
}

void buildPQColorimetry( displaycolorimetry_t * pColorimetry, colormapping_t *pMapping, const displaycolorimetry_t & nativeDisplayOutput )
{
    *pColorimetry = displaycolorimetry_2020;

    colormapping_t noRemap;
    noRemap.blendEnableMinSat = 0.0f;
    noRemap.blendEnableMaxSat = 1.0f;
    noRemap.blendAmountMin = 0.0f;
    noRemap.blendAmountMax = 0.0f;
    *pMapping = noRemap;
}

bool approxEqual( const glm::vec3 & a, const glm::vec3 & b, float flTolerance = 1e-5f )
{
    glm::vec3 v = glm::abs(a - b);
    return ( v.x < flTolerance && v.y < flTolerance && v.z < flTolerance );
}

const glm::mat3 k_xyz_from_709 = normalised_primary_matrix( displaycolorimetry_709.primaries, displaycolorimetry_709.white, 1.f );
const glm::mat3 k_709_from_xyz = glm::inverse( k_xyz_from_709 );

const glm::mat3 k_xyz_from_2020 = normalised_primary_matrix( displaycolorimetry_2020.primaries, displaycolorimetry_2020.white, 1.f );
const glm::mat3 k_2020_from_xyz = glm::inverse( k_xyz_from_2020 );

const glm::mat3 k_2020_from_709 = k_2020_from_xyz * k_xyz_from_709;

FAST_MATH_OFF