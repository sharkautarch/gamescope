#pragma once

#include <getopt.h>

#include <atomic>

extern const char *gamescope_optstring;
static inline constexpr struct option gamescope_options[] = {
	{ "help", no_argument, nullptr, 0 },
	{ "version", no_argument, nullptr, 0 },
	{ "nested-width", required_argument, nullptr, 'w' },
	{ "nested-height", required_argument, nullptr, 'h' },
	{ "nested-refresh", required_argument, nullptr, 'r' },
	{ "max-scale", required_argument, nullptr, 'm' },
	{ "scaler", required_argument, nullptr, 'S' },
	{ "filter", required_argument, nullptr, 'F' },
	{ "output-width", required_argument, nullptr, 'W' },
	{ "output-height", required_argument, nullptr, 'H' },
	{ "sharpness", required_argument, nullptr, 0 },
	{ "fsr-sharpness", required_argument, nullptr, 0 },
	{ "rt", no_argument, nullptr, 0 },
	{ "prefer-vk-device", required_argument, 0 },
	{ "expose-wayland", no_argument, 0 },
	{ "mouse-sensitivity", required_argument, nullptr, 's' },
	{ "mangoapp", no_argument, nullptr, 0 },
	{ "pass-fds-to-child", required_argument, nullptr, 0 },

	{ "backend", required_argument, nullptr, 0 },

	// nested mode options
	{ "nested-unfocused-refresh", required_argument, nullptr, 'o' },
	{ "borderless", no_argument, nullptr, 'b' },
	{ "fullscreen", no_argument, nullptr, 'f' },
	{ "grab", no_argument, nullptr, 'g' },
	{ "force-grab-cursor", no_argument, nullptr, 0 },
	{ "display-index", required_argument, nullptr, 0 },

	// embedded mode options
	{ "disable-layers", no_argument, nullptr, 0 },
	{ "debug-layers", no_argument, nullptr, 0 },
	{ "prefer-output", required_argument, nullptr, 'O' },
	{ "default-touch-mode", required_argument, nullptr, 0 },
	{ "generate-drm-mode", required_argument, nullptr, 0 },
	{ "immediate-flips", no_argument, nullptr, 0 },
	{ "adaptive-sync", no_argument, nullptr, 0 },
	{ "framerate-limit", required_argument, nullptr, 0 },

	// openvr options
#if HAVE_OPENVR
	{ "vr-overlay-key", required_argument, nullptr, 0 },
	{ "vr-overlay-explicit-name", required_argument, nullptr, 0 },
	{ "vr-overlay-default-name", required_argument, nullptr, 0 },
	{ "vr-overlay-icon", required_argument, nullptr, 0 },
	{ "vr-overlay-show-immediately", no_argument, nullptr, 0 },
	{ "vr-overlay-enable-control-bar", no_argument, nullptr, 0 },
	{ "vr-overlay-enable-control-bar-keyboard", no_argument, nullptr, 0 },
	{ "vr-overlay-enable-control-bar-close", no_argument, nullptr, 0 },
	{ "vr-overlay-modal", no_argument, nullptr, 0 },
	{ "vr-overlay-physical-width", required_argument, nullptr, 0 },
	{ "vr-overlay-physical-curvature", required_argument, nullptr, 0 },
	{ "vr-overlay-physical-pre-curve-pitch", required_argument, nullptr, 0 },
	{ "vr-scroll-speed", required_argument, nullptr, 0 },
#endif

	// wlserver options
	{ "xwayland-count", required_argument, nullptr, 0 },

	// steamcompmgr options
	{ "cursor", required_argument, nullptr, 0 },
	{ "cursor-hotspot", required_argument, nullptr, 0 },
	{ "cursor-scale-height", required_argument, nullptr, 0 },
	{ "ready-fd", required_argument, nullptr, 'R' },
	{ "stats-path", required_argument, nullptr, 'T' },
	{ "hide-cursor-delay", required_argument, nullptr, 'C' },
	{ "debug-focus", no_argument, nullptr, 0 },
	{ "synchronous-x11", no_argument, nullptr, 0 },
	{ "debug-hud", no_argument, nullptr, 'v' },
	{ "debug-events", no_argument, nullptr, 0 },
	{ "steam", no_argument, nullptr, 'e' },
	{ "force-composition", no_argument, nullptr, 'c' },
	{ "composite-debug", no_argument, nullptr, 0 },
	{ "disable-xres", no_argument, nullptr, 'x' },
	{ "fade-out-duration", required_argument, nullptr, 0 },
	{ "force-orientation", required_argument, nullptr, 0 },
	{ "force-windows-fullscreen", no_argument, nullptr, 0 },

	{ "disable-color-management", no_argument, nullptr, 0 },
	{ "sdr-gamut-wideness", required_argument, nullptr, 0 },
	{ "hdr-enabled", no_argument, nullptr, 0 },
	{ "hdr-sdr-content-nits", required_argument, nullptr, 0 },
	{ "hdr-itm-enabled", no_argument, nullptr, 0 },
	{ "hdr-itm-sdr-nits", required_argument, nullptr, 0 },
	{ "hdr-itm-target-nits", required_argument, nullptr, 0 },
	{ "hdr-debug-force-support", no_argument, nullptr, 0 },
	{ "hdr-debug-force-output", no_argument, nullptr, 0 },
	{ "hdr-debug-heatmap", no_argument, nullptr, 0 },

	{ "reshade-effect", required_argument, nullptr, 0 },
	{ "reshade-technique-idx", required_argument, nullptr, 0 },

	// Steam Deck options
	{ "mura-map", required_argument, nullptr, 0 },

	{} // keep last
};

extern std::atomic< bool > g_bRun;

#include <glm/fwd.hpp>
extern glm::ivec2 g_ivNestedResolution;

extern int g_nNestedRefresh; // mHz
extern int g_nNestedUnfocusedRefresh; // mHz
extern int g_nNestedDisplayIndex;

extern uint32_t g_nOutputWidth;
extern uint32_t g_nOutputHeight;
extern bool g_bForceRelativeMouse;
extern int g_nOutputRefresh; // mHz
extern bool g_bOutputHDREnabled;
extern bool g_bForceInternal;

extern bool g_bFullscreen;

extern bool g_bGrabbed;

extern float g_mouseSensitivity;
extern const char *g_sOutputName;

enum class GamescopeUpscaleFilter : uint32_t
{
    LINEAR = 0,
    NEAREST,
    FSR,
    NIS,
    PIXEL,

    FROM_VIEW = 0xF, // internal
};

static constexpr bool DoesHardwareSupportUpscaleFilter( GamescopeUpscaleFilter eFilter )
{
    // Could do nearest someday... AMDGPU DC supports custom tap placement to an extent.

    return eFilter == GamescopeUpscaleFilter::LINEAR;
}

enum class GamescopeUpscaleScaler : uint32_t
{
    AUTO,
    INTEGER,
    FIT,
    FILL,
    STRETCH,
};

extern GamescopeUpscaleFilter g_upscaleFilter;
extern GamescopeUpscaleScaler g_upscaleScaler;
extern GamescopeUpscaleFilter g_wantedUpscaleFilter;
extern GamescopeUpscaleScaler g_wantedUpscaleScaler;
extern int g_upscaleFilterSharpness;

extern bool g_bBorderlessOutputWindow;

extern bool g_bExposeWayland;

extern bool g_bRt;

extern int g_nXWaylandCount;

extern uint32_t g_preferVendorID;
extern uint32_t g_preferDeviceID;

