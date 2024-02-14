#pragma once

namespace gamescope
{
    // Backend enum.
    enum GamescopeBackend
    {
        Auto,
        DRM,
        SDL,
        OpenVR,
        Headless,
    };

    // Backend forward declarations.
    class CSDLBackend;
    class CDRMBackend;
    class COpenVRBackend;
    class CHeadlessBackend;
}
