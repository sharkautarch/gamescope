#pragma once

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <drm_mode.h>

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

enum drm_valve1_transfer_function {
	DRM_VALVE1_TRANSFER_FUNCTION_DEFAULT,

	DRM_VALVE1_TRANSFER_FUNCTION_SRGB,
	DRM_VALVE1_TRANSFER_FUNCTION_BT709,
	DRM_VALVE1_TRANSFER_FUNCTION_PQ,
	DRM_VALVE1_TRANSFER_FUNCTION_LINEAR,
	DRM_VALVE1_TRANSFER_FUNCTION_UNITY,
	DRM_VALVE1_TRANSFER_FUNCTION_HLG,
	DRM_VALVE1_TRANSFER_FUNCTION_GAMMA22,
	DRM_VALVE1_TRANSFER_FUNCTION_GAMMA24,
	DRM_VALVE1_TRANSFER_FUNCTION_GAMMA26,
	DRM_VALVE1_TRANSFER_FUNCTION_MAX,
};

/* from CTA-861-G */
#define HDMI_EOTF_SDR 0
#define HDMI_EOTF_TRADITIONAL_HDR 1
#define HDMI_EOTF_ST2084 2
#define HDMI_EOTF_HLG 3

/* For Default case, driver will set the colorspace */
#define DRM_MODE_COLORIMETRY_DEFAULT			0
/* CEA 861 Normal Colorimetry options */
#define DRM_MODE_COLORIMETRY_NO_DATA			0
#define DRM_MODE_COLORIMETRY_SMPTE_170M_YCC		1
#define DRM_MODE_COLORIMETRY_BT709_YCC			2
/* CEA 861 Extended Colorimetry Options */
#define DRM_MODE_COLORIMETRY_XVYCC_601			3
#define DRM_MODE_COLORIMETRY_XVYCC_709			4
#define DRM_MODE_COLORIMETRY_SYCC_601			5
#define DRM_MODE_COLORIMETRY_OPYCC_601			6
#define DRM_MODE_COLORIMETRY_OPRGB			7
#define DRM_MODE_COLORIMETRY_BT2020_CYCC		8
#define DRM_MODE_COLORIMETRY_BT2020_RGB			9
#define DRM_MODE_COLORIMETRY_BT2020_YCC			10
/* Additional Colorimetry extension added as part of CTA 861.G */
#define DRM_MODE_COLORIMETRY_DCI_P3_RGB_D65		11
#define DRM_MODE_COLORIMETRY_DCI_P3_RGB_THEATER		12
/* Additional Colorimetry Options added for DP 1.4a VSC Colorimetry Format */
#define DRM_MODE_COLORIMETRY_RGB_WIDE_FIXED		13
#define DRM_MODE_COLORIMETRY_RGB_WIDE_FLOAT		14
#define DRM_MODE_COLORIMETRY_BT601_YCC			15

/* Content type options */
#define DRM_MODE_CONTENT_TYPE_NO_DATA		0
#define DRM_MODE_CONTENT_TYPE_GRAPHICS		1
#define DRM_MODE_CONTENT_TYPE_PHOTO		2
#define DRM_MODE_CONTENT_TYPE_CINEMA		3
#define DRM_MODE_CONTENT_TYPE_GAME		4
