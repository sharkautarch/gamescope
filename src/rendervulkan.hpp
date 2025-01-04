// Initialize Vulkan and composite stuff with a compute queue

#pragma once

#include <atomic>
#include <stdint.h>
#include <memory>
#include <map>
#include <unordered_map>
#include <array>
#include <bitset>
#include <mutex>
#include <optional>

#include "main.hpp"

#include "gamescope_shared.h"
#include "backend.h"

#include "shaders/descriptor_set_constants.h"

#include "Utils/Bits.h"
#include "Utils/Simd.h"

class CVulkanCmdBuffer;

// 1: Fade Plane (Fade outs between switching focus)
// 2: Video Underlay (The actual video)
// 3: Video Streaming UI (Game, App)
// 4: External Overlay (Mangoapp, etc)
// 5: Primary Overlay (Steam Overlay)
// 6: Cursor

// or

// 1: Fade Plane (Fade outs between switching focus)
// 2: Base Plane (Game, App)
// 3: Override Plane (Dropdowns, etc)
// 4: External Overlay (Mangoapp, etc)
// 5: Primary Overlay (Steam Overlay)
// 6: Cursor
#define k_nMaxLayers 6
#define k_nMaxYcbcrMask 16
#define k_nMaxYcbcrMask_ToPreCompile 3

#define k_nMaxBlurLayers 2

#define kMaxBlurRadius (37u / 2 + 1)

enum BlurMode {
    BLUR_MODE_OFF = 0,
    BLUR_MODE_COND = 1,
    BLUR_MODE_ALWAYS = 2,
};

enum EStreamColorspace : int
{
	k_EStreamColorspace_Unknown = 0,
	k_EStreamColorspace_BT601 = 1,
	k_EStreamColorspace_BT601_Full = 2,
	k_EStreamColorspace_BT709 = 3,
	k_EStreamColorspace_BT709_Full = 4
};

#include <memory>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <wayland-server-core.h>

#include "wlr_begin.hpp"
#include <wlr/render/dmabuf.h>
#include <wlr/render/interface.h>
#include "wlr_end.hpp"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <drm_fourcc.h>
struct VulkanRenderer_t
{
	struct wlr_renderer base;
};

struct VulkanWlrTexture_t
{
	struct wlr_texture base;
	struct wlr_buffer *buf;
};

inline VkFormat ToSrgbVulkanFormat( VkFormat format )
{
	switch ( format )
	{
		case VK_FORMAT_B8G8R8A8_UNORM:	return VK_FORMAT_B8G8R8A8_SRGB;
		case VK_FORMAT_R8G8B8A8_UNORM:	return VK_FORMAT_R8G8B8A8_SRGB;
		default:						return format;
	}
}

inline VkFormat ToLinearVulkanFormat( VkFormat format )
{
	switch ( format )
	{
		case VK_FORMAT_B8G8R8A8_SRGB:	return VK_FORMAT_B8G8R8A8_UNORM;
		case VK_FORMAT_R8G8B8A8_SRGB:	return VK_FORMAT_R8G8B8A8_UNORM;
		default:						return format;
	}
}

inline GamescopeAppTextureColorspace VkColorSpaceToGamescopeAppTextureColorSpace(VkFormat format, VkColorSpaceKHR colorspace)
{
	switch (colorspace)
	{
		default:
		case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
			// We will use image view conversions for these 8888 formats.
			if (ToSrgbVulkanFormat(format) != ToLinearVulkanFormat(format))
				return GAMESCOPE_APP_TEXTURE_COLORSPACE_LINEAR;
			return GAMESCOPE_APP_TEXTURE_COLORSPACE_SRGB;

		case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
			return GAMESCOPE_APP_TEXTURE_COLORSPACE_SCRGB;

		case VK_COLOR_SPACE_HDR10_ST2084_EXT:
			return GAMESCOPE_APP_TEXTURE_COLORSPACE_HDR10_PQ;
	}
}

class CVulkanTexture : public gamescope::RcObject
{
public:
	ENABLE_IN_PLACE_RC
	EXPORT_STATIC_FACTORY_FUNC(CVulkanTexture)
	struct createFlags {

		constexpr createFlags( void )
		{
			bFlippable = false;
			bMappable = false;
			bSampled = false;
			bStorage = false;
			bTransferSrc = false;
			bTransferDst = false;
			bLinear = false;
			bExportable = false;
			bOutputImage = false;
			bColorAttachment = false;
			imageType = VK_IMAGE_TYPE_2D;
		}

		bool bFlippable : 1;
		bool bMappable : 1;
		bool bSampled : 1;
		bool bStorage : 1;
		bool bTransferSrc : 1;
		bool bTransferDst : 1;
		bool bLinear : 1;
		bool bExportable : 1;
		bool bOutputImage : 1;
		bool bColorAttachment : 1;
		VkImageType imageType;
	};
	
	template<bool bIsOwning>
	constexpr CVulkanTexture(gamescope::RcObjectOwnership<bIsOwning> tag) : RcObject{tag} {}
	
	template <bool bIsOwning>
	constexpr __attribute__((noinline)) CVulkanTexture(gamescope::RcObjectOwnership<bIsOwning> tag, uint32_t width, uint32_t height, uint32_t depth, uint32_t drmFormat, createFlags flags, wlr_dmabuf_attributes *pDMA, uint32_t contentWidth, uint32_t contentHeight, gamescope::Rc<CVulkanTexture> pExistingImageToReuseMemory, gamescope::OwningRc<gamescope::IBackendFb> pBackendFb) : RcObject{tag} {
		if (BInit(width, height, depth, drmFormat, flags, pDMA, contentWidth, contentHeight, pExistingImageToReuseMemory, pBackendFb) == false) [[unlikely]] {
			m_bInitialized = false;
		}
	}
	
	template <bool bIsOwning>
	constexpr __attribute__((noinline)) CVulkanTexture(gamescope::RcObjectOwnership<bIsOwning> tag, uint32_t width, uint32_t height, uint32_t depth, uint32_t drmFormat, createFlags flags, wlr_dmabuf_attributes *pDMA, uint32_t contentWidth, uint32_t contentHeight, gamescope::Rc<CVulkanTexture> pExistingImageToReuseMemory) : RcObject{tag} {
		if (BInit(width, height, depth, drmFormat, flags, pDMA, contentWidth, contentHeight, pExistingImageToReuseMemory) == false) [[unlikely]] {
			m_bInitialized = false;
		}
	}
	template <bool bIsOwning>
	constexpr __attribute__((noinline)) CVulkanTexture(gamescope::RcObjectOwnership<bIsOwning> tag, uint32_t width, uint32_t height, uint32_t depth, uint32_t drmFormat, createFlags flags, wlr_dmabuf_attributes *pDMA = nullptr, uint32_t contentWidth = 0, uint32_t contentHeight = 0) : RcObject{tag} {
		if (BInit(width, height, depth, drmFormat, flags, pDMA, contentWidth, contentHeight) == false) [[unlikely]] {
			m_bInitialized = false;
		}
	}
	
	
	CVulkanTexture() = delete;

	bool BInit( uint32_t width, uint32_t height, uint32_t depth, uint32_t drmFormat, createFlags flags, wlr_dmabuf_attributes *pDMA = nullptr, uint32_t contentWidth = 0, uint32_t contentHeight = 0, gamescope::Rc<CVulkanTexture> pExistingImageToReuseMemory = nullptr, gamescope::OwningRc<gamescope::IBackendFb> pBackendFb = nullptr );
	bool BInitFromSwapchain( VkImage image, uint32_t width, uint32_t height, VkFormat format );

	uint32_t IncRef();
	uint32_t DecRef();

	bool IsInUse();
	bool BIsValid() {
		return m_bInitialized;
	}
	inline VkImageView view( bool linear ) { return linear ? m_linearView : m_srgbView; }
	inline VkImageView linearView() { return m_linearView; }
	inline VkImageView srgbView() { return m_srgbView; }
	inline VkImageView lumaView() { return m_lumaView; }
	inline VkImageView chromaView() { return m_chromaView; }
	inline uint32_t width() { return m_width; }
	inline uint32_t height() { return m_height; }
	inline glm::uvec2 dimensions() { glm::uvec2 ret; memcpy(&ret, &m_width, sizeof(ret)); return ret; }
	inline uint32_t depth() { return m_depth; }
	inline uint32_t contentWidth() {return m_contentWidth; }
	inline uint32_t contentHeight() {return m_contentHeight; }
	inline uint32_t rowPitch() { return m_unRowPitch; }
	inline gamescope::IBackendFb* GetBackendFb() { return m_pBackendFb.get(); }
	inline uint8_t *mappedData() { return m_pMappedData; }
	inline VkFormat format() const { return m_format; }
	inline const struct wlr_dmabuf_attributes& dmabuf() { return m_dmabuf; }
	inline VkImage vkImage() { return m_vkImage; }
	inline bool outputImage() { return m_bOutputImage; }
	inline bool externalImage() { return m_bExternal; }
	inline VkDeviceSize totalSize() const { return m_size; }
	inline uint32_t drmFormat() const { return m_drmFormat; }

	inline uint32_t lumaOffset() const { return m_lumaOffset; }
	inline uint32_t lumaRowPitch() const { return m_lumaPitch; }
	inline uint32_t chromaOffset() const { return m_chromaOffset; }
	inline uint32_t chromaRowPitch() const { return m_chromaPitch; }

	inline EStreamColorspace streamColorspace() const { return m_streamColorspace; }
	inline void setStreamColorspace(EStreamColorspace colorspace) { m_streamColorspace = colorspace; }

	inline bool isYcbcr() const
	{
		return format() == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
	}

	int memoryFence();

	~CVulkanTexture( void );

	uint32_t queueFamily = VK_QUEUE_FAMILY_IGNORED;

private:
	bool m_bInitialized = false;
	bool m_bExternal = false;
	bool m_bOutputImage = false;

	uint32_t m_drmFormat = DRM_FORMAT_INVALID;

	VkImage m_vkImage = VK_NULL_HANDLE;
	VkDeviceMemory m_vkImageMemory = VK_NULL_HANDLE;
	
	VkImageView m_srgbView = VK_NULL_HANDLE;
	VkImageView m_linearView = VK_NULL_HANDLE;

	VkImageView m_lumaView = VK_NULL_HANDLE;
	VkImageView m_chromaView = VK_NULL_HANDLE;

	uint32_t m_width = 0;
	uint32_t m_height = 0;
	uint32_t m_depth = 0;

	uint32_t m_contentWidth = 0;
	uint32_t m_contentHeight = 0;

	uint32_t m_unRowPitch = 0;
	VkDeviceSize m_size = 0;

	uint32_t m_lumaOffset = 0;
	uint32_t m_lumaPitch = 0;
	uint32_t m_chromaOffset = 0;
	uint32_t m_chromaPitch = 0;
	
	// If this texture owns the backend Fb (ie. it's an internal texture)
	gamescope::OwningRc<gamescope::IBackendFb> m_pBackendFb;

	uint8_t *m_pMappedData = nullptr;

	VkFormat m_format = VK_FORMAT_UNDEFINED;

	EStreamColorspace m_streamColorspace = k_EStreamColorspace_Unknown;

	struct wlr_dmabuf_attributes m_dmabuf = {};
};

struct vec2_t
{
	float x, y;
};
using aligned_vec2 = glm::vec<2, float, (glm::qualifier)3>;
using aligned_uvec2 = glm::vec<2, float, (glm::qualifier)3>;
using aligned_vec4 = glm::vec<4, float, (glm::qualifier)3>;
typedef glm::vec<2, uint8_t, (glm::qualifier)3> u8vec2;
typedef uint8_t u8x16_t __attribute__((vector_size(16*sizeof(uint8_t))));

static inline bool float_is_integer(float x)
{
	return fabsf(ceilf(x) - x) <= 0.001f;
}

static inline glm::bvec2  __attribute__((flatten)) float_is_integer(aligned_vec4 x4)
{
	auto predicate = glm::abs(glm::ceil(x4) - x4).data <= (aligned_vec4{0.001f}).data;
	auto xmm = (__m128)predicate;
	auto res = _mm_movemask_ps(xmm);
	glm::bvec2 ret{false};
	memcpy(&ret, &res, sizeof(ret));
	return ret;
}

inline bool close_enough(float a, float b, float epsilon = 0.001f)
{
	return fabsf(a - b) <= epsilon;
}

inline unsigned __attribute__((always_inline, flatten)) close_enough(aligned_vec2 a, float b, float epsilon = 0.001f)
{
	aligned_vec4 a4 = aligned_vec4{__builtin_shufflevector(a.data, a.data, 0, 1, -1, -1)};
	auto xmm = (__m128)(glm::abs(a4 - b).data <= aligned_vec4{epsilon}.data);
	auto res = _mm_movemask_ps(xmm);
	return res | (~0b11u);
}

bool DRMFormatHasAlpha( uint32_t nDRMFormat );

struct FrameInfo_t
{
	bool useFSRLayer0;
	bool useNISLayer0;
	bool bFadingOut;
	BlurMode blurLayer0;
	int blurRadius;

	gamescope::Rc<CVulkanTexture> shaperLut[EOTF_Count];
	gamescope::Rc<CVulkanTexture> lut3D[EOTF_Count];

	bool allowVRR;
	bool applyOutputColorMgmt; // drm only
	EOTF outputEncodingEOTF;

	int layerCount;
	struct Layer_t
	{
		gamescope::Rc<CVulkanTexture> tex;
		int zpos;

		vec2_t offset;
		vec2_t scale;

		float opacity;

		GamescopeUpscaleFilter filter = GamescopeUpscaleFilter::LINEAR;

		bool blackBorder;
		bool applyColorMgmt; // drm only

		std::shared_ptr<gamescope::BackendBlob> ctm;

		GamescopeAppTextureColorspace colorspace;

		bool isYcbcr() const
		{
			if ( !tex )
				return false;

			return tex->isYcbcr();
		}

		bool hasAlpha() const
		{
			if ( !tex )
				return false;

			return DRMFormatHasAlpha( tex->drmFormat() );
		}

		bool __attribute__((flatten, no_stack_protector)) isScreenSize() const {
			aligned_vec4 vscale{};
			aligned_vec4 voffset{};
			memcpy(&vscale, &scale, sizeof(scale));
			memcpy(&voffset, &offset, sizeof(offset));
			auto enough = close_enough(vscale, 1.0f);
			if ( ~enough )
				return false;
			auto isInt = float_is_integer(voffset);
			return (isInt[0]) & (isInt[1]);
		}

		bool viewConvertsToLinearAutomatically() const {
			return colorspace == GAMESCOPE_APP_TEXTURE_COLORSPACE_LINEAR ||
				colorspace == GAMESCOPE_APP_TEXTURE_COLORSPACE_SCRGB ||
				colorspace == GAMESCOPE_APP_TEXTURE_COLORSPACE_PASSTHRU;
		}

		uint32_t integerWidth() const { return tex->width() / scale.x; }
		uint32_t integerHeight() const { return tex->height() / scale.y; }
		inline glm::uvec2 __attribute__((no_stack_protector, pure)) integerDimensions() const {
			glm::vec2 vscale; std::memcpy(&vscale, &scale, sizeof(scale));
			return (glm::uvec2) ( (((glm::vec2) tex->dimensions()) / vscale ) ); 
		} 
		inline vec2_t __attribute__((no_stack_protector, pure)) offsetPixelCenter() const
		{
			glm::vec2 voffset; std::memcpy(&voffset, &offset, sizeof(offset));
			glm::vec2 vscale; std::memcpy(&vscale, &scale, sizeof(scale));
			return std::bit_cast<vec2_t>(voffset + glm::vec2{.5f}/vscale);
		}
	} layers[ k_nMaxLayers ];

	uint32_t borderMask() const {
		uint32_t result = 0;
		for (int i = 0; i < layerCount; i++)
		{
			if (layers[ i ].blackBorder)
				result |= 1 << i;
		}
		return result;
	}
	uint32_t ycbcrMask() const {
		uint32_t result = 0;
		for (int i = 0; i < layerCount; i++)
		{
			if (layers[ i ].isYcbcr())
				result |= 1 << i;
		}
		return result;
	}
	uint32_t colorspaceMask() const {
		uint32_t result = 0;
		for (int i = 0; i < layerCount; i++)
		{
			result |= layers[ i ].colorspace << (i * GamescopeAppTextureColorspace_Bits);
		}
		return result;
	}
};

extern uint32_t g_uCompositeDebug;
extern gamescope::ConVar<uint32_t> cv_composite_debug;

namespace CompositeDebugFlag
{
	static constexpr uint32_t Markers = 1u << 0;
	static constexpr uint32_t PlaneBorders = 1u << 1;
	static constexpr uint32_t Heatmap = 1u << 2;
	static constexpr uint32_t Heatmap_MSWCG = 1u << 3;
	static constexpr uint32_t Heatmap_Hard = 1u << 4;
	static constexpr uint32_t Markers_Partial = 1u << 5;
	static constexpr uint32_t Tonemap_Reinhard = 1u << 7;
};

VkInstance vulkan_get_instance(void);
bool vulkan_init(VkInstance instance, VkSurfaceKHR surface);
bool vulkan_init_formats(void);
bool vulkan_make_output();

gamescope::OwningRc<CVulkanTexture> vulkan_create_texture_from_dmabuf( struct wlr_dmabuf_attributes *pDMA, gamescope::OwningRc<gamescope::IBackendFb> pBackendFb );
gamescope::OwningRc<CVulkanTexture> vulkan_create_texture_from_bits( uint32_t width, uint32_t height, uint32_t contentWidth, uint32_t contentHeight, uint32_t drmFormat, CVulkanTexture::createFlags texCreateFlags, void *bits );
gamescope::OwningRc<CVulkanTexture> vulkan_create_texture_from_wlr_buffer( struct wlr_buffer *buf, gamescope::OwningRc<gamescope::IBackendFb> pBackendFb );

std::optional<uint64_t> vulkan_composite( struct FrameInfo_t *frameInfo, gamescope::Rc<CVulkanTexture> pScreenshotTexture, bool partial, gamescope::Rc<CVulkanTexture> pOutputOverride = nullptr, bool increment = true, std::unique_ptr<CVulkanCmdBuffer> pInCommandBuffer = nullptr );
void vulkan_wait( uint64_t ulSeqNo, bool bReset );
gamescope::Rc<CVulkanTexture> vulkan_get_last_output_image( bool partial, bool defer );
gamescope::Rc<CVulkanTexture> vulkan_acquire_screenshot_texture(uint32_t width, uint32_t height, bool exportable, uint32_t drmFormat, EStreamColorspace colorspace = k_EStreamColorspace_Unknown);

void vulkan_present_to_window( void );

void vulkan_garbage_collect( void );
bool vulkan_remake_swapchain( void );
bool vulkan_remake_output_images( void );
bool acquire_next_image( void );

bool vulkan_primary_dev_id(dev_t *id);
bool vulkan_supports_modifiers(void);

gamescope::Rc<CVulkanTexture> vulkan_create_1d_lut(uint32_t size);
gamescope::Rc<CVulkanTexture> vulkan_create_3d_lut(uint32_t width, uint32_t height, uint32_t depth);
void vulkan_update_luts(const gamescope::Rc<CVulkanTexture>& lut1d, const gamescope::Rc<CVulkanTexture>& lut3d, void* lut1d_data, void* lut3d_data);

gamescope::Rc<CVulkanTexture> vulkan_get_hacky_blank_texture();

std::optional<uint64_t> vulkan_screenshot( const struct FrameInfo_t *frameInfo, gamescope::Rc<CVulkanTexture> pScreenshotTexture, gamescope::Rc<CVulkanTexture> pYUVOutTexture );

struct wlr_renderer *vulkan_renderer_create( void );

using mat3x4 = std::array<std::array<float, 4>, 3>;

#include "color_helpers_impl.h"

struct gamescope_color_mgmt_t
{
	bool enabled;
	uint32_t externalDirtyCtr;
	nightmode_t nightmode;
	float sdrGamutWideness = -1; // user property to widen gamut
	float flInternalDisplayBrightness = 500.f;
	float flSDROnHDRBrightness = 203.f;
	float flHDRInputGain = 1.f;
	float flSDRInputGain = 1.f;

	// HDR Display Metadata Override & Tonemapping
	ETonemapOperator hdrTonemapOperator = ETonemapOperator_None;
	tonemap_info_t hdrTonemapDisplayMetadata = { 0 };
	tonemap_info_t hdrTonemapSourceMetadata = { 0 };

	// the native colorimetry capabilities of the display
	displaycolorimetry_t displayColorimetry;
	EOTF displayEOTF;

	// the output encoding colorimetry
	// ie. for HDR displays we send an explicit 2020 colorimetry packet.
	// on SDR displays this is the same as displayColorimetry.
	displaycolorimetry_t outputEncodingColorimetry;
	EOTF outputEncodingEOTF;

	// If non-zero, use this as the emulated "virtual" white point for the output
	glm::vec2 outputVirtualWhite = { 0.f, 0.f };
	EChromaticAdaptationMethod chromaticAdaptationMode = k_EChromaticAdapatationMethod_Bradford;

	std::shared_ptr<gamescope::BackendBlob> appHDRMetadata;

	bool operator == (const gamescope_color_mgmt_t&) const = default;
	bool operator != (const gamescope_color_mgmt_t&) const = default;
};

//namespace members from "color_helpers_impl.h":
using rendervulkan::s_nLutEdgeSize3d;
using rendervulkan::s_nLutSize1d;

struct gamescope_color_mgmt_luts
{
	bool bHasLut3D = false;
	bool bHasLut1D = false;
	uint16_t lut3d[s_nLutEdgeSize3d*s_nLutEdgeSize3d*s_nLutEdgeSize3d*4];
	uint16_t lut1d[s_nLutSize1d*4];

	gamescope::Rc<CVulkanTexture> vk_lut3d;
	gamescope::Rc<CVulkanTexture> vk_lut1d;

	bool HasLuts() const
	{
		return bHasLut3D && bHasLut1D;
	}

	void shutdown()
	{
		bHasLut1D = false;
		bHasLut3D = false;
		vk_lut1d = nullptr;
		vk_lut3d = nullptr;
	}

	void reset()
	{
		bHasLut1D = false;
		bHasLut3D = false;
	}
};

struct gamescope_color_mgmt_tracker_t
{
	gamescope_color_mgmt_t pending{};
	gamescope_color_mgmt_t current{};
	uint32_t serial{};
};

extern gamescope_color_mgmt_tracker_t g_ColorMgmt;
extern gamescope_color_mgmt_luts g_ColorMgmtLuts[ EOTF_Count ];

struct VulkanOutput_t
{
	VkSurfaceKHR surface;
	VkSurfaceCapabilitiesKHR surfaceCaps;
	std::vector< VkSurfaceFormatKHR > surfaceFormats;
	std::vector< VkPresentModeKHR > presentModes;


	std::shared_ptr<gamescope::BackendBlob> swapchainHDRMetadata;
	VkSwapchainKHR swapChain;
	VkFence acquireFence;

	uint32_t nOutImage; // swapchain index in nested mode, or ping/pong between two RTs
	std::vector<gamescope::OwningRc<CVulkanTexture>> outputImages;
	std::vector<gamescope::OwningRc<CVulkanTexture>> outputImagesPartialOverlay;
	gamescope::OwningRc<CVulkanTexture> temporaryHackyBlankImage;

	uint32_t uOutputFormat = DRM_FORMAT_INVALID;
	uint32_t uOutputFormatOverlay = DRM_FORMAT_INVALID;

	std::array<gamescope::OwningRc<CVulkanTexture>, 2> pScreenshotImages;

	// NIS and FSR
	gamescope::OwningRc<CVulkanTexture> tmpOutput;

	// NIS
	gamescope::OwningRc<CVulkanTexture> nisScalerImage;
	gamescope::OwningRc<CVulkanTexture> nisUsmImage;
};


enum ShaderType {
	SHADER_TYPE_BLIT = 0,
	SHADER_TYPE_BLUR,
	SHADER_TYPE_BLUR_COND,
	SHADER_TYPE_BLUR_FIRST_PASS,
	SHADER_TYPE_EASU,
	SHADER_TYPE_RCAS,
	SHADER_TYPE_NIS,
	SHADER_TYPE_RGB_TO_NV12,

	SHADER_TYPE_COUNT
};

extern VulkanOutput_t g_output;

struct SamplerState
{
	bool bNearest : 1;
	bool bUnnormalized : 1;

	constexpr SamplerState( void )
	{
		bNearest = false;
		bUnnormalized = false;
	}

	bool operator==( const SamplerState& other ) const
	{
		return this->bNearest == other.bNearest
			&& this->bUnnormalized == other.bUnnormalized;
	}
};

namespace std
{
	template <>
	struct hash<SamplerState>
	{
		size_t operator()( const SamplerState& k ) const
		{
			return k.bNearest | (k.bUnnormalized << 1);
		}
	};
}

struct PipelineInfo_t
{
	ShaderType shaderType;

	uint32_t layerCount;
	uint32_t ycbcrMask;
	uint32_t blurLayerCount;

	uint32_t compositeDebug;

	uint32_t colorspaceMask;
	uint32_t outputEOTF;
	bool itmEnable;

	bool operator==(const PipelineInfo_t& o) const {
		return
		shaderType == o.shaderType &&
		layerCount == o.layerCount &&
		ycbcrMask == o.ycbcrMask &&
		blurLayerCount == o.blurLayerCount &&
		compositeDebug == o.compositeDebug &&
		colorspaceMask == o.colorspaceMask &&
		outputEOTF == o.outputEOTF &&
		itmEnable == o.itmEnable;
	}
};


static inline uint32_t hash_combine(uint32_t old_hash, uint32_t new_hash) {
    return old_hash ^ (new_hash + 0x9e3779b9 + (old_hash << 6) + (old_hash >> 2));
}

namespace std
{
	template <>
	struct hash<PipelineInfo_t>
	{
		size_t operator()( const PipelineInfo_t& k ) const
		{
			uint32_t hash = k.shaderType;
			hash = hash_combine(hash, k.layerCount);
			hash = hash_combine(hash, k.ycbcrMask);
			hash = hash_combine(hash, k.blurLayerCount);
			hash = hash_combine(hash, k.compositeDebug);
			hash = hash_combine(hash, k.colorspaceMask);
			hash = hash_combine(hash, k.outputEOTF);
			hash = hash_combine(hash, k.itmEnable);
			return hash;
		}
	};
}

static inline uint32_t div_roundup(uint32_t x, uint32_t y)
{
	return (x + (y - 1)) / y;
}

#define VULKAN_INSTANCE_FUNCTIONS \
	VK_FUNC(CreateDevice) \
	VK_FUNC(EnumerateDeviceExtensionProperties) \
	VK_FUNC(EnumeratePhysicalDevices) \
	VK_FUNC(GetDeviceProcAddr) \
	VK_FUNC(GetPhysicalDeviceFeatures2) \
	VK_FUNC(GetPhysicalDeviceFormatProperties) \
	VK_FUNC(GetPhysicalDeviceFormatProperties2) \
	VK_FUNC(GetPhysicalDeviceImageFormatProperties2) \
	VK_FUNC(GetPhysicalDeviceMemoryProperties) \
	VK_FUNC(GetPhysicalDeviceQueueFamilyProperties) \
	VK_FUNC(GetPhysicalDeviceProperties) \
	VK_FUNC(GetPhysicalDeviceProperties2) \
	VK_FUNC(GetPhysicalDeviceSurfaceCapabilitiesKHR) \
	VK_FUNC(GetPhysicalDeviceSurfaceFormatsKHR) \
	VK_FUNC(GetPhysicalDeviceSurfacePresentModesKHR) \
	VK_FUNC(GetPhysicalDeviceSurfaceSupportKHR)

#define VULKAN_DEVICE_FUNCTIONS \
	VK_FUNC(AcquireNextImageKHR) \
	VK_FUNC(AllocateCommandBuffers) \
	VK_FUNC(AllocateDescriptorSets) \
	VK_FUNC(AllocateMemory) \
	VK_FUNC(BeginCommandBuffer) \
	VK_FUNC(BindBufferMemory) \
	VK_FUNC(BindImageMemory) \
	VK_FUNC(CmdBeginRendering) \
	VK_FUNC(CmdBindDescriptorSets) \
	VK_FUNC(CmdBindPipeline) \
	VK_FUNC(CmdClearColorImage) \
	VK_FUNC(CmdCopyBufferToImage) \
	VK_FUNC(CmdCopyImage) \
	VK_FUNC(CmdDispatch) \
	VK_FUNC(CmdDraw) \
	VK_FUNC(CmdEndRendering) \
	VK_FUNC(CmdPipelineBarrier) \
	VK_FUNC(CmdPushConstants) \
	VK_FUNC(CreateBuffer) \
	VK_FUNC(CreateCommandPool) \
	VK_FUNC(CreateComputePipelines) \
	VK_FUNC(CreateDescriptorPool) \
	VK_FUNC(CreateDescriptorSetLayout) \
	VK_FUNC(CreateFence) \
	VK_FUNC(CreateGraphicsPipelines) \
	VK_FUNC(CreateImage) \
	VK_FUNC(CreateImageView) \
	VK_FUNC(CreatePipelineLayout) \
	VK_FUNC(CreateSampler) \
	VK_FUNC(CreateSamplerYcbcrConversion) \
	VK_FUNC(CreateSemaphore) \
	VK_FUNC(GetSemaphoreFdKHR) \
	VK_FUNC(ImportSemaphoreFdKHR) \
	VK_FUNC(CreateShaderModule) \
	VK_FUNC(CreateSwapchainKHR) \
	VK_FUNC(DestroyBuffer) \
	VK_FUNC(DestroyDescriptorPool) \
	VK_FUNC(DestroyDescriptorSetLayout) \
	VK_FUNC(DestroyImage) \
	VK_FUNC(DestroyImageView) \
	VK_FUNC(DestroyPipeline) \
	VK_FUNC(DestroySemaphore) \
	VK_FUNC(DestroyPipelineLayout) \
	VK_FUNC(DestroySampler) \
	VK_FUNC(DestroySwapchainKHR) \
	VK_FUNC(EndCommandBuffer) \
	VK_FUNC(FreeCommandBuffers) \
	VK_FUNC(FreeDescriptorSets) \
	VK_FUNC(FreeMemory) \
	VK_FUNC(GetBufferMemoryRequirements) \
	VK_FUNC(GetDeviceQueue) \
	VK_FUNC(GetImageDrmFormatModifierPropertiesEXT) \
	VK_FUNC(GetImageMemoryRequirements) \
	VK_FUNC(GetImageSubresourceLayout) \
	VK_FUNC(GetMemoryFdKHR) \
	VK_FUNC(GetSemaphoreCounterValue) \
	VK_FUNC(GetSwapchainImagesKHR) \
	VK_FUNC(MapMemory) \
	VK_FUNC(QueuePresentKHR) \
	VK_FUNC(QueueSubmit) \
	VK_FUNC(QueueWaitIdle) \
	VK_FUNC(ResetCommandBuffer) \
	VK_FUNC(ResetFences) \
	VK_FUNC(UnmapMemory) \
	VK_FUNC(UpdateDescriptorSets) \
	VK_FUNC(WaitForFences) \
	VK_FUNC(WaitForPresentKHR) \
	VK_FUNC(WaitSemaphores) \
	VK_FUNC(SetHdrMetadataEXT)

template<typename T, typename U = T>
constexpr T align(T what, U to) {
return (what + to - 1) & ~(to - 1);
}

class CVulkanDevice;

struct VulkanTimelineSemaphore_t
{
	~VulkanTimelineSemaphore_t();

	CVulkanDevice *pDevice = nullptr;
	VkSemaphore pVkSemaphore = VK_NULL_HANDLE;

	int GetFd() const;
};

struct VulkanTimelinePoint_t
{
	std::shared_ptr<VulkanTimelineSemaphore_t> pTimelineSemaphore;
	uint64_t ulPoint;
};

class CVulkanDevice
{
public:
	bool BInit(VkInstance instance, VkSurfaceKHR surface);

	VkSampler sampler(SamplerState key);
	VkPipeline pipeline(ShaderType type, uint32_t layerCount = 1, uint32_t ycbcrMask = 0, uint32_t blur_layers = 0, uint32_t colorspace_mask = 0, uint32_t output_eotf = EOTF_Gamma22, bool itm_enable = false);
	int32_t findMemoryType( VkMemoryPropertyFlags properties, uint32_t requiredTypeBits );
	std::unique_ptr<CVulkanCmdBuffer> commandBuffer();
	uint64_t submit( std::unique_ptr<CVulkanCmdBuffer> cmdBuf);
	uint64_t submitInternal( CVulkanCmdBuffer* cmdBuf );
	void wait(uint64_t sequence, bool reset = true);
	void waitIdle(bool reset = true);
	void garbageCollect();
	inline VkDescriptorSet descriptorSet()
	{
		VkDescriptorSet ret = m_descriptorSets[m_currentDescriptorSet];
		m_currentDescriptorSet = (m_currentDescriptorSet + 1) % m_descriptorSets.size();
		return ret;
	}

	std::shared_ptr<VulkanTimelineSemaphore_t> CreateTimelineSemaphore( uint64_t ulStartingPoint, bool bShared = false );
	std::shared_ptr<VulkanTimelineSemaphore_t> ImportTimelineSemaphore( gamescope::CTimeline *pTimeline );

	static constexpr uint32_t upload_buffer_size = 1920 * 1080 * 4;

	inline VkDevice device() { return m_device; }
	inline VkPhysicalDevice physDev() {return m_physDev; }
	inline VkInstance instance() { return m_instance; }
	inline VkQueue queue() {return m_queue;}
	inline VkQueue generalQueue() {return m_generalQueue;}
	inline VkCommandPool commandPool() {return m_commandPool;}
	inline VkCommandPool generalCommandPool() {return m_generalCommandPool;}
	inline uint32_t queueFamily() {return m_queueFamily;}
	inline uint32_t generalQueueFamily() {return m_generalQueueFamily;}
	inline VkBuffer uploadBuffer() {return m_uploadBuffer;}
	inline VkPipelineLayout pipelineLayout() {return m_pipelineLayout;}
	inline int drmRenderFd() {return m_drmRendererFd;}
	inline bool supportsModifiers() {return m_bSupportsModifiers;}
	inline bool hasDrmPrimaryDevId() {return m_bHasDrmPrimaryDevId;}
	inline dev_t primaryDevId() {return m_drmPrimaryDevId;}
	inline bool supportsFp16() {return m_bSupportsFp16;}

	inline void *uploadBufferData(uint32_t size)
	{
		assert(size <= upload_buffer_size);

		m_uploadBufferOffset = align(m_uploadBufferOffset, 16);
		if (m_uploadBufferOffset + size > upload_buffer_size)
		{
			fprintf(stderr, "Exceeded uploadBufferData\n");
			waitIdle(false);
		}

		uint8_t *ptr = ((uint8_t*)m_uploadBufferData) + m_uploadBufferOffset;
		m_uploadBufferOffset += size;
		return std::assume_aligned<16>(ptr);
	}

	#define VK_FUNC(x) PFN_vk##x x = nullptr;
	struct
	{
		VULKAN_INSTANCE_FUNCTIONS
		VULKAN_DEVICE_FUNCTIONS
	} vk;
	#undef VK_FUNC

	void resetCmdBuffers(uint64_t sequence);

protected:
	friend class CVulkanCmdBuffer;

	bool selectPhysDev(VkSurfaceKHR surface);
	bool createDevice();
	bool createLayouts();
	bool createPools();
	bool createShaders();
	bool createScratchResources();
	VkPipeline compilePipeline(uint32_t layerCount, uint32_t ycbcrMask, ShaderType type, uint32_t blur_layer_count, uint32_t composite_debug, uint32_t colorspace_mask, uint32_t output_eotf, bool itm_enable);
	void compileAllPipelines();

	VkDevice m_device = nullptr;
	VkPhysicalDevice m_physDev = nullptr;
	VkInstance m_instance = nullptr;
	VkQueue m_queue = nullptr;
	VkQueue m_generalQueue = nullptr;
	VkSamplerYcbcrConversion m_ycbcrConversion = VK_NULL_HANDLE;
	VkSampler m_ycbcrSampler = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	VkCommandPool m_commandPool = VK_NULL_HANDLE;
	VkCommandPool m_generalCommandPool = VK_NULL_HANDLE;

	uint32_t m_queueFamily = -1;
	uint32_t m_generalQueueFamily = -1;

	int m_drmRendererFd = -1;
	dev_t m_drmPrimaryDevId = 0;

	bool m_bSupportsFp16 = false;
	bool m_bHasDrmPrimaryDevId = false;
	bool m_bSupportsModifiers = false;
	bool m_bInitialized = false;


	VkPhysicalDeviceMemoryProperties m_memoryProperties;

	std::unordered_map< SamplerState, VkSampler > m_samplerCache;
	std::array<VkShaderModule, SHADER_TYPE_COUNT> m_shaderModules;
	std::unordered_map<PipelineInfo_t, VkPipeline> m_pipelineMap;
	std::mutex m_pipelineMutex;

	// currently just one set, no need to double buffer because we
	// vkQueueWaitIdle after each submit.
	// should be moved to the output if we are going to support multiple outputs
	std::array<VkDescriptorSet, 3> m_descriptorSets;
	uint32_t m_currentDescriptorSet = 0;

	VkBuffer m_uploadBuffer;
	VkDeviceMemory m_uploadBufferMemory;
	void *m_uploadBufferData;
	uint32_t m_uploadBufferOffset = 0;

	VkSemaphore m_scratchTimelineSemaphore;
	std::atomic<uint64_t> m_submissionSeqNo = { 0 };
	std::vector<std::unique_ptr<CVulkanCmdBuffer>> m_unusedCmdBufs;
	std::map<uint64_t, std::unique_ptr<CVulkanCmdBuffer>> m_pendingCmdBufs;
};

struct TextureState
{
	bool discarded : 1;
	bool dirty : 1;
	bool needsPresentLayout : 1;
	bool needsExport : 1;
	bool needsImport : 1;

	TextureState()
	{
		discarded = false;
		dirty = false;
		needsPresentLayout = false;
		needsExport = false;
		needsImport = false;
	}
};

typedef enum class pipeline_task {
	reshade,
	shader,
	copy,
	end
} pipeline_task_t;

typedef struct {
	unsigned int curr_sync_point;
	unsigned int total_sync_points; 
} shader_sync_info_t;

typedef enum class reshade_target {
	init,
	runtime
} reshade_target_t;

typedef union {
		shader_sync_info_t shader_sync_info;
		reshade_target_t reshade_target;
} barrier_info_t;

//#define DEBUG_BARRIER 1
static constexpr auto __attribute__((const)) u16SetBit(uint16_t bits, uint16_t pos) {
	return gamescope::bits::setBit<uint16_t>(bits, pos); 
}

static constexpr auto __attribute__((const)) u16UnsetBit(uint16_t bits, uint16_t pos) {
	return gamescope::bits::unsetBit<uint16_t>(bits, pos); 
}

static constexpr auto __attribute__((const)) u16MaskOutBitsBelowPos(uint16_t bits, uint16_t pos) {
	return gamescope::bits::maskOutBitsBelowPos<uint16_t>(bits, pos); 
}

static constexpr auto __attribute__((const)) u16MaskOutBitsAbovePos(uint16_t bits, uint16_t pos) {
	return gamescope::bits::maskOutBitsAbovePos<uint16_t>(bits, pos); 
}

class CVulkanCmdBuffer
{
public:
	CVulkanCmdBuffer(CVulkanDevice *parent, VkCommandBuffer cmdBuffer, VkQueue queue, uint32_t queueFamily);
	~CVulkanCmdBuffer();
	CVulkanCmdBuffer() = delete;
	CVulkanCmdBuffer(CVulkanCmdBuffer& other) = delete;
	CVulkanCmdBuffer(const CVulkanCmdBuffer& other) = delete;
	CVulkanCmdBuffer(CVulkanCmdBuffer&& other) = delete;
	CVulkanCmdBuffer& operator=(const CVulkanCmdBuffer& other) = delete;
	CVulkanCmdBuffer& operator=(CVulkanCmdBuffer& other) = delete;
	CVulkanCmdBuffer& operator=(CVulkanCmdBuffer&& other) = delete;

	inline VkCommandBuffer rawBuffer() {return m_cmdBuffer;}
	void reset();

// std::countl_zero is used when clearing m_textureBlock + in dispatch(), and on some architectures, lzcnt instruction is slightly faster than bsr (where bsr would be used if not compiling w/ lzcnt support).
// lzcnt is only *one* cpu cycle on zen1-zen4 amd cpus, while bsr takes *four* cycles on zen1-3 cpus: https://www.agner.org/optimize/instruction_tables.pdf
// (lzcnt is also faster than bsr on intel knights landing)

// the lzcnt instruction is decoded into bsr on cpus that don't support lzcnt anyways: https://en.wikipedia.org/wiki/X86_Bit_manipulation_instruction_set#ABM_(Advanced_Bit_Manipulation)
// also, the input to the lzcnt instruction is never zero (it's explicitly skipped if the input is zero), so there'll never be undefined behavior if lzcnt gets decoded to bsr 
#ifdef __x86_64__
#	define CVULKANCMDBUFFER_TARGET_ATTR __attribute__((target("lzcnt")))
#else
#	define CVULKANCMDBUFFER_TARGET_ATTR
#endif
	void begin() CVULKANCMDBUFFER_TARGET_ATTR;
	void end();
	void clearState() CVULKANCMDBUFFER_TARGET_ATTR;
	void clearBoundTexturesAboveSlot(uint16_t slot) CVULKANCMDBUFFER_TARGET_ATTR;
	void bindTexture(uint32_t slot, gamescope::Rc<CVulkanTexture> texture);
	void bindColorMgmtLuts(uint32_t slot, gamescope::Rc<CVulkanTexture> lut1d, gamescope::Rc<CVulkanTexture> lut3d);
	void setTextureStorage(bool storage);
	void setTextureSrgb(uint32_t slot, bool srgb);
	void setSamplerNearest(uint32_t slot, bool nearest);
	void setSamplerUnnormalized(uint32_t slot, bool unnormalized);
	void bindTarget(gamescope::Rc<CVulkanTexture> target);
	
	template<class PushData, class... Args>
	void uploadConstants(Args&&... args);
	void bindPipeline(VkPipeline pipeline);
	void dispatch(uint32_t x, uint32_t y = 1, uint32_t z = 1, unsigned int total_dispatches = 1, unsigned int curr_dispatch_no = 1) CVULKANCMDBUFFER_TARGET_ATTR;
	void copyImage(gamescope::Rc<CVulkanTexture> src, gamescope::Rc<CVulkanTexture> dst);
	void copyBufferToImage(VkBuffer buffer, VkDeviceSize offset, uint32_t stride, gamescope::Rc<CVulkanTexture> dst);


	void prepareSrcImage(CVulkanTexture *image);
	void prepareDestImage(CVulkanTexture *image);
	void discardImage(CVulkanTexture *image);
	void markDirty(CVulkanTexture *image);
	
	template <pipeline_task_t task>
	void insertBarrier(barrier_info_t barrier_info)
	{
		std::vector<VkImageMemoryBarrier> barriers;

		uint32_t externalQueue = m_device->supportsModifiers() ? VK_QUEUE_FAMILY_FOREIGN_EXT : VK_QUEUE_FAMILY_EXTERNAL_KHR;

		VkImageSubresourceRange subResRange =
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.levelCount = 1,
			.layerCount = 1
		};


		VkFlags srcStageMask = m_previousCopy ? VK_PIPELINE_STAGE_TRANSFER_BIT : 0;
		VkAccessFlags src_write_bits = m_previousCopy ? VK_ACCESS_TRANSFER_WRITE_BIT : 0;

		VkFlags dstStageMask = 0;
		VkAccessFlags dst_write_bits = 0;
		VkAccessFlags dst_read_bits = 0;

		bool flush = false;

		switch (task) {
			case ( pipeline_task::reshade ): {
				if (barrier_info.reshade_target == reshade_target::init) {
					srcStageMask = dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
					dst_write_bits = src_write_bits = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
					dst_read_bits = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
				} else {
					dstStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
					dst_read_bits |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
				}
				break;
			}
			case ( pipeline_task::shader ): {

				const bool isFirst = (barrier_info.shader_sync_info.curr_sync_point == 1u);
				//const bool isLast = (barrier_info.shader_sync_info.curr_sync_point == barrier_info.shader_sync_info.total_sync_points);
				const bool multipleShaders = barrier_info.shader_sync_info.total_sync_points > 1u;

	#ifdef DEBUG_BARRIER
				printf("\n pipeline_task::shader\n");
				printf("\n isFirst = %s, isLast = %s\ncurr_sync_point = %u, total_sync_points = %u\n", isFirst ? "true" : "false", isLast ? "true" : "false", barrier_info.shader_sync_info.curr_sync_point, barrier_info.shader_sync_info.total_sync_points);
	#endif

				src_write_bits |= (!isFirst ? VK_ACCESS_SHADER_WRITE_BIT : 0);

				dst_read_bits = multipleShaders ? VK_ACCESS_SHADER_READ_BIT : 0;
				/* ^ TODO: if we ever move to syncronization2, could change dst_read_bits to 
				 * shader sampler read, when multipleShaders == true && isLast == true
				 */

				dst_write_bits = VK_ACCESS_SHADER_WRITE_BIT;

				srcStageMask |= (!isFirst ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : 0);

				dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

				break;

			} case ( pipeline_task::copy ): {
	#ifdef DEBUG_BARRIER
				printf("\n pipeline_task::copy\n");
	#endif

				dst_read_bits = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
				dst_write_bits = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;

				dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
				break;

			} case ( pipeline_task::end ): {
				flush = true;

	#ifdef DEBUG_BARRIER
				printf("\n pipeline_task::end\n");
	#endif
				dst_read_bits = dst_write_bits = 0;
				srcStageMask |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
				break;
			}
			default:
			{
				__builtin_unreachable();
				break;
			}
		}

		if (srcStageMask == 0)
			srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		if (dstStageMask == 0)
			dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;


		for (auto& pair : m_textureState)
		{
			CVulkanTexture *image = pair.first;
			TextureState& state = pair.second;
			assert(!flush || !state.needsImport);

			bool isExport = flush && state.needsExport;
			bool isPresent = flush && state.needsPresentLayout;

			if (!state.discarded && !state.dirty && !state.needsImport && !isExport && !isPresent)
				continue;


			if (image->queueFamily == VK_QUEUE_FAMILY_IGNORED)
				image->queueFamily = m_queueFamily;

			static constexpr VkAccessFlags src_read_bits = 0u; //*_READ on .srcAccessMask for CmdPipelineBarrier is always the same as a no-op
								//https://github.com/KhronosGroup/Vulkan-Docs/issues/131 
			VkImageMemoryBarrier memoryBarrier =
			{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = ( state.dirty ? (src_write_bits | src_read_bits) : 0u)
						| ( (isPresent & state.dirty) ? VK_ACCESS_SHADER_WRITE_BIT : 0u),

				.dstAccessMask = dst_read_bits | dst_write_bits,
				.oldLayout = ( (state.discarded || state.needsImport) ) ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL,
				.newLayout =  isPresent ? GetBackend()->GetPresentLayout() : VK_IMAGE_LAYOUT_GENERAL,
				.srcQueueFamilyIndex = isExport ? image->queueFamily : state.needsImport ? externalQueue : image->queueFamily,
				.dstQueueFamilyIndex = isExport ? externalQueue : state.needsImport ? m_queueFamily : m_queueFamily,
				.image = image->vkImage(),
				.subresourceRange = subResRange
			};

	/*#ifdef DEBUG_BARRIER
			char buf[256] = ".oldLayout = ";
			strcat(buf, string_VkImageLayout(memoryBarrier.oldLayout));
			const char * next = "\n.newLayout = ";
			strcat(buf, next);
			strcat(buf, string_VkImageLayout(memoryBarrier.newLayout));
			const char * next2 = "\n";
			strcat(buf, next2);
			printf(buf);
	#endif*/



			barriers.push_back(memoryBarrier);

			state.discarded = false;
			state.dirty = false;
			state.needsImport = false;
		}

		// TODO replace VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
		// ^ Done ^_^
		m_device->vk.CmdPipelineBarrier(m_cmdBuffer, srcStageMask, dstStageMask,
										0, 0, nullptr, 0, nullptr, barriers.size(), barriers.data());

		m_previousCopy = ( task == pipeline_task::copy );
	}

	VkQueue queue() { return m_queue; }
	uint32_t queueFamily() { return m_queueFamily; }
	
	inline void setBoundTexture(uint16_t slot, gamescope::Rc<CVulkanTexture>& texture) {
		m_textureBlock.boundTextures[slot] = texture.get();
	}
	inline void unsetBoundTexture(uint16_t slot) {
		m_textureBlock.boundTextures[slot] = nullptr;
	}

	inline auto __attribute__((pure)) m_getBoundTextures() const { return m_textureBlock.boundTextures; }
	
	inline auto& m_getTarget() { return m_textureBlock.target; }
	inline auto& m_getSamplerState() { return m_textureBlock.samplerState; }
	inline auto& m_getUseSrgb() { return m_textureBlock.useSrgb; }
	
	CVulkanTexture** __attribute__((pure)) getShaperLut(uint32_t i) {
		if (i < VKR_LUT3D_COUNT) {
			CVulkanTexture** __restrict__ ptr = &(std::assume_aligned<16>(m_shaperLut.data())[i]);
			return ptr;
		} else {
			CVulkanTexture** __restrict__ ptr = static_cast<CVulkanTexture**__restrict__>(nullptr);
			return ptr; 
		}
	}
	CVulkanTexture** __attribute__((pure)) getLut3D(uint32_t i) {
		if (i < VKR_LUT3D_COUNT) {
			CVulkanTexture** __restrict__ ptr = &(std::assume_aligned<16>(m_lut3D.data())[i]);
			return ptr;
		} else {
			CVulkanTexture** __restrict__ ptr = static_cast<CVulkanTexture**__restrict__>(nullptr);
			return ptr;
		}
	}
	
	bool __attribute__((pure)) BNonContiguousBoundTextures() const {
		return std::popcount(m_boundTextureBits) != std::countl_zero(m_boundTextureBits);
	}
	
	inline void setBoundTextureBit(uint16_t pos) {
		m_boundTextureBits = u16SetBit(m_boundTextureBits, pos);
	}
	
	inline void unsetBoundTextureBit(uint32_t pos) {
		m_boundTextureBits = u16UnsetBit(m_boundTextureBits, pos);
	}

	//not applying __attribute__((target("lzcnt"))) over here, because there's no guard against using zero on std::countl_zero() for this function
	//w/ normal x86_64 compile cpu target, lzcnt is not available, and std::countl_zero will ensure it doesn't feed zero into bsf when using the bsf instruction
	//when allowing the compiler to use lzcnt, std::countl_zero can feed zero into lzcnt, which would be an issue on cpus that decode lzcnt into bsf
	// (for all of the places where I apply __attribute__((target("lzcnt"))), there's no possibility of zero being fed into std::countl_zero())
	inline uint16_t __attribute__((pure)) getNumberOfBoundTextures() const {
		return VKR_SAMPLER_SLOTS - std::countl_zero(m_boundTextureBits);
	}

	void AddDependency( std::shared_ptr<VulkanTimelineSemaphore_t> pTimelineSemaphore, uint64_t ulPoint );
	void AddSignal( std::shared_ptr<VulkanTimelineSemaphore_t> pTimelineSemaphore, uint64_t ulPoint );

	const std::vector<VulkanTimelinePoint_t> &GetExternalDependencies() const { return m_ExternalDependencies; }
	const std::vector<VulkanTimelinePoint_t> &GetExternalSignals() const { return m_ExternalSignals; }

private:

	VkCommandBuffer m_cmdBuffer;
	CVulkanDevice *m_device;

	VkQueue m_queue;
	uint32_t m_queueFamily;

	bool m_previousCopy = false;
	
	// Per Use State
	std::unordered_map<CVulkanTexture *, TextureState> m_textureState; //56 
	std::vector<gamescope::Rc<CVulkanTexture>> m_textureRefs; //24 bytes (on gcc)
	
	// Draw State
	uint16_t m_boundTextureBits;
	struct alignas(32) m_textureBlock {			
			std::bitset<VKR_SAMPLER_SLOTS> useSrgb;
			CVulkanTexture* target;
			std::array<SamplerState, VKR_SAMPLER_SLOTS> samplerState;
			CVulkanTexture* boundTextures[VKR_SAMPLER_SLOTS];
	} m_textureBlock alignas(32);

	std::array<CVulkanTexture *, VKR_LUT3D_COUNT> m_shaperLut;
	std::array<CVulkanTexture *, VKR_LUT3D_COUNT> m_lut3D;

	std::vector<VulkanTimelinePoint_t> m_ExternalDependencies;
	std::vector<VulkanTimelinePoint_t> m_ExternalSignals;

	uint32_t m_renderBufferOffset = 0;
};

uint32_t VulkanFormatToDRM( VkFormat vkFormat, std::optional<bool> obHasAlphaOverride = std::nullopt );
VkFormat DRMFormatToVulkan( uint32_t nDRMFormat, bool bSrgb );
bool DRMFormatHasAlpha( uint32_t nDRMFormat );
uint32_t DRMFormatGetBPP( uint32_t nDRMFormat );

gamescope::OwningRc<CVulkanTexture> vulkan_create_flat_texture( uint32_t width, uint32_t height, uint8_t r, uint8_t g, uint8_t b, uint8_t a );

bool vulkan_supports_hdr10();

void vulkan_wait_idle();

extern CVulkanDevice g_device;
