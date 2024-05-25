#include "backend.h"
#include "vblankmanager.hpp"
#include "convar.h"
#include "wlserver.hpp"

#include "wlr_begin.hpp"
#include <wlr/types/wlr_buffer.h>
#include "wlr_end.hpp"

extern void sleep_until_nanos(uint64_t nanos);
extern bool env_to_bool(const char *env);

namespace gamescope
{
    /////////////
    // IBackend
    /////////////

    static IBackend *s_pBackend = nullptr;

    IBackend *IBackend::Get()
    {
        return s_pBackend;
    }

    bool IBackend::Set( IBackend *pBackend )
    {
        if ( s_pBackend )
        {
            delete s_pBackend;
            s_pBackend = nullptr;
        }

        if ( pBackend )
        {
            s_pBackend = pBackend;
            if ( !s_pBackend->Init() )
            {
                delete s_pBackend;
                s_pBackend = nullptr;
                return false;
            }
        }

        return true;
    }

    /////////////////
    // CBaseBackendFb
    /////////////////

    CBaseBackendFb::CBaseBackendFb( wlr_buffer *pClientBuffer )
        : m_pClientBuffer{ pClientBuffer }
    {
    }

    CBaseBackendFb::~CBaseBackendFb()
    {
        // I do not own the client buffer, but I released that in DecRef.
        assert( !HasLiveReferences() );
        m_pClientBuffer = nullptr;
    }

    uint32_t CBaseBackendFb::IncRef()
    {
        uint32_t uRefCount = IBackendFb::IncRef();
        if ( m_pClientBuffer && !uRefCount )
        {
            wlserver_lock();
            wlr_buffer_lock( m_pClientBuffer );
            wlserver_unlock( false );
        }
        return uRefCount;
    }
    uint32_t CBaseBackendFb::DecRef()
    {
        wlr_buffer *pClientBuffer = m_pClientBuffer;
        uint32_t uRefCount = IBackendFb::DecRef();
        if ( pClientBuffer && !uRefCount )
        {
            wlserver_lock();
            wlr_buffer_unlock( pClientBuffer );
            wlserver_unlock();
        }
        return uRefCount;
    }

    /////////////////
    // CBaseBackend
    /////////////////

    bool CBaseBackend::NeedsFrameSync() const
    {
        const bool bForceTimerFd = env_to_bool( getenv( "GAMESCOPE_DISABLE_TIMERFD" ) );
        return bForceTimerFd;
    }

    INestedHints *CBaseBackend::GetNestedHints()
    {
        return nullptr;
    }

    VBlankScheduleTime CBaseBackend::FrameSync()
    {
        VBlankScheduleTime schedule = GetVBlankTimer().CalcNextWakeupTime( false );
        sleep_until_nanos( schedule.ulScheduledWakeupPoint );
        return schedule;
    }

    ConVar<bool> cv_touch_external_display_trackpad( "touch_external_display_trackpad", false, "If we are using an external display, should we treat the internal display's touch as a trackpad insteaad?" );
    ConVar<TouchClickMode> cv_touch_click_mode( "touch_click_mode", TouchClickModes::Left, "The default action to perform on touch." );
    TouchClickMode CBaseBackend::GetTouchClickMode()
    {
        if ( cv_touch_external_display_trackpad && this->GetCurrentConnector() )
        {
            gamescope::GamescopeScreenType screenType = this->GetCurrentConnector()->GetScreenType();
            if ( screenType == gamescope::GAMESCOPE_SCREEN_TYPE_EXTERNAL && cv_touch_click_mode == TouchClickMode::Passthrough )
                return TouchClickMode::Trackpad;
        }

        return cv_touch_click_mode;
    }
}
