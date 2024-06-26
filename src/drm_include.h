#pragma once

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <drm_mode.h>

#include "wlr_begin.hpp"
#include <wlr/render/dmabuf.h>
#include <wlr/render/drm_format_set.h>
#include "wlr_end.hpp"

#include "hdmi.h"

// Josh: Okay whatever, this header isn't
// available for whatever stupid reason. :v
//#include <drm_color_mgmt.h>
enum drm_color_encoding {
	DRM_COLOR_YCBCR_BT601,
	DRM_COLOR_YCBCR_BT709,
	DRM_COLOR_YCBCR_BT2020,
	DRM_COLOR_ENCODING_MAX,
};

enum drm_color_range {
	DRM_COLOR_YCBCR_LIMITED_RANGE,
	DRM_COLOR_YCBCR_FULL_RANGE,
	DRM_COLOR_RANGE_MAX,
};

enum amdgpu_transfer_function {
	AMDGPU_TRANSFER_FUNCTION_DEFAULT,
	AMDGPU_TRANSFER_FUNCTION_SRGB_EOTF,
	AMDGPU_TRANSFER_FUNCTION_BT709_INV_OETF,
	AMDGPU_TRANSFER_FUNCTION_PQ_EOTF,
	AMDGPU_TRANSFER_FUNCTION_IDENTITY,
	AMDGPU_TRANSFER_FUNCTION_GAMMA22_EOTF,
	AMDGPU_TRANSFER_FUNCTION_GAMMA24_EOTF,
	AMDGPU_TRANSFER_FUNCTION_GAMMA26_EOTF,
	AMDGPU_TRANSFER_FUNCTION_SRGB_INV_EOTF,
	AMDGPU_TRANSFER_FUNCTION_BT709_OETF,
	AMDGPU_TRANSFER_FUNCTION_PQ_INV_EOTF,
	AMDGPU_TRANSFER_FUNCTION_GAMMA22_INV_EOTF,
	AMDGPU_TRANSFER_FUNCTION_GAMMA24_INV_EOTF,
	AMDGPU_TRANSFER_FUNCTION_GAMMA26_INV_EOTF,
	AMDGPU_TRANSFER_FUNCTION_COUNT
};

enum drm_panel_orientation {
	DRM_MODE_PANEL_ORIENTATION_UNKNOWN = -1,
	DRM_MODE_PANEL_ORIENTATION_NORMAL = 0,
	DRM_MODE_PANEL_ORIENTATION_BOTTOM_UP,
	DRM_MODE_PANEL_ORIENTATION_LEFT_UP,
	DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

enum drm_colorspace {
	/* For Default case, driver will set the colorspace */
	DRM_MODE_COLORIMETRY_DEFAULT 		= 0,
	/* CEA 861 Normal Colorimetry options */
	DRM_MODE_COLORIMETRY_NO_DATA		= 0,
	DRM_MODE_COLORIMETRY_SMPTE_170M_YCC	= 1,
	DRM_MODE_COLORIMETRY_BT709_YCC		= 2,
	/* CEA 861 Extended Colorimetry Options */
	DRM_MODE_COLORIMETRY_XVYCC_601		= 3,
	DRM_MODE_COLORIMETRY_XVYCC_709		= 4,
	DRM_MODE_COLORIMETRY_SYCC_601		= 5,
	DRM_MODE_COLORIMETRY_OPYCC_601		= 6,
	DRM_MODE_COLORIMETRY_OPRGB		= 7,
	DRM_MODE_COLORIMETRY_BT2020_CYCC	= 8,
	DRM_MODE_COLORIMETRY_BT2020_RGB		= 9,
	DRM_MODE_COLORIMETRY_BT2020_YCC		= 10,
	/* Additional Colorimetry extension added as part of CTA 861.G */
	DRM_MODE_COLORIMETRY_DCI_P3_RGB_D65	= 11,
	DRM_MODE_COLORIMETRY_DCI_P3_RGB_THEATER	= 12,
	/* Additional Colorimetry Options added for DP 1.4a VSC Colorimetry Format */
	DRM_MODE_COLORIMETRY_RGB_WIDE_FIXED	= 13,
	DRM_MODE_COLORIMETRY_RGB_WIDE_FLOAT	= 14,
	DRM_MODE_COLORIMETRY_BT601_YCC		= 15,
	DRM_MODE_COLORIMETRY_COUNT
};

/* Content type options */
#define DRM_MODE_CONTENT_TYPE_NO_DATA		0
#define DRM_MODE_CONTENT_TYPE_GRAPHICS		1
#define DRM_MODE_CONTENT_TYPE_PHOTO		2
#define DRM_MODE_CONTENT_TYPE_CINEMA		3
#define DRM_MODE_CONTENT_TYPE_GAME		4
