#include "backend.h"
#include "vblankmanager.hpp"

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

        s_pBackend = pBackend;
        if ( !s_pBackend->Init() )
        {
            delete s_pBackend;
            s_pBackend = nullptr;
            return false;
        }

        return true;
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
}
