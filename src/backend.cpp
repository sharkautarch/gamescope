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
		
		
		static size_t s_currentBackendSize = 0;
    static IBackend *s_pBackend = nullptr;
		static std::atomic<size_t> s_backendOffset = 0;
		
		static IBackend *s_pOldBackend = nullptr; //for internal book-keeping only
    IBackend *IBackend::Get()
    {
        return s_pBackend + s_backendOffset.load();
    }
    #if 0
		void* IBackend::operator new(size_t size) {
			printf("operator new()\n");
			//AquireExclusive();
			size_t oldBackendSize = std::exchange(s_currentBackendSize, size);
			if (s_pOldBackend != nullptr) { //delete the old backend for realsies
				assert(oldBackendSize > 0);
				::operator delete(s_pOldBackend); //invoking the *global* deleter (deletes backend & dummy block)
				s_pOldBackend = nullptr;
				s_backendOffset.store(0);
			}
			
			auto* ptr = std::bit_cast<unsigned char*>(::operator new(size*2)); //always allocate enough space for a both the actual class
			// plus a zero-initialized dummy block
			std::memset(ptr+size, 0, size); //zero-initialize the extra dummy block
			return std::bit_cast<void*>(ptr);
		}
		
		void IBackend::operator delete(void* ptr ) {
			printf("operator delete()\n");
			//AquireExclusive();
			s_backendOffset = s_currentBackendSize; 
			//to avoid use-after-frees and nullptr dereferences from doing s_pBackend-><member>
			//we simply defer actual deallocation until either gamescope exits, or gamescope creates a new IBackend
			
			//mitigate other race conditions, we atomically update the s_backendOffset so that IBackend::Get() will safely point to a zero-initialized dummy block
			
			//a big performance benefit to this approach is that on x86_64 & aarch64, simple seq_cst atomic loads have the same overhead as plain loads (the synchronization cost is actually encurred on the atomic store side)
			assert(s_pOldBackend == nullptr);
			s_pOldBackend = std::bit_cast<IBackend*>(ptr);
			std::bit_cast<IBackend*>(ptr)->IBackend::~IBackend();
		}
		#endif
    bool IBackend::Set( IBackend *pBackend )
    {
    		//AquireExclusive();
        if ( s_pBackend )
        {
            delete s_pBackend; //we're intentionally *not* setting s_pBackend to nullptr after deletion
            s_pBackend=nullptr;
            //because IBackend has overrides for new & delete, where the delete override
            //ensures that IBackend::Get will still point to a safe memory region after it is run
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

    CBaseBackendFb::CBaseBackendFb()
    {
    }

    CBaseBackendFb::~CBaseBackendFb()
    {
        // I do not own the client buffer, but I released that in DecRef.
        assert( !HasLiveReferences() );
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

        std::shared_ptr<CReleaseTimelinePoint> pReleasePoint = std::move( m_pReleasePoint );
        m_pReleasePoint = nullptr;

        uint32_t uRefCount = IBackendFb::DecRef();
        if ( uRefCount )
        {
            if ( pReleasePoint )
                m_pReleasePoint = std::move( pReleasePoint );
        }
        else if ( pClientBuffer )
        {
            wlserver_lock();
            wlr_buffer_unlock( pClientBuffer );
            wlserver_unlock();
        }
        return uRefCount;
    }

    void CBaseBackendFb::SetBuffer( wlr_buffer *pClientBuffer )
    {
        if ( m_pClientBuffer == pClientBuffer )
            return;

        assert( m_pClientBuffer == nullptr );
        m_pClientBuffer = pClientBuffer;
        if ( GetRefCount() )
        {
            wlserver_lock();
            wlr_buffer_lock( m_pClientBuffer );
            wlserver_unlock( false );
        }

        m_pReleasePoint = nullptr;
    }

    void CBaseBackendFb::SetReleasePoint( std::shared_ptr<CReleaseTimelinePoint> pReleasePoint )
    {
        m_pReleasePoint = pReleasePoint;

        if ( m_pClientBuffer && GetRefCount() )
        {
            wlserver_lock();
            wlr_buffer_unlock( m_pClientBuffer );
            wlserver_unlock();
            m_pClientBuffer = nullptr;
        }
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

    void CBaseBackend::DumpDebugInfo()
    {
        console_log.infof( "Uses Modifiers: %s", this->UsesModifiers() ? "true" : "false" );
        console_log.infof( "VRR Active: %s", this->IsVRRActive() ? "true" : "false" );
        console_log.infof( "Supports Plane Hardware Cursor: %s (not relevant for nested backends)", this->SupportsPlaneHardwareCursor() ? "true" : "false" );
        console_log.infof( "Supports Tearing: %s", this->SupportsTearing() ? "true" : "false" );
        console_log.infof( "Uses Vulkan Swapchain: %s", this->UsesVulkanSwapchain() ? "true" : "false" );
        console_log.infof( "Is Session Based: %s", this->IsSessionBased() ? "true" : "false" );
        console_log.infof( "Supports Explicit Sync: %s", this->SupportsExplicitSync() ? "true" : "false" );
        console_log.infof( "Current Screen Type: %s", this->GetScreenType() == GAMESCOPE_SCREEN_TYPE_INTERNAL ? "Internal" : "External" );
        console_log.infof( "Is Visible: %s", this->IsVisible() ? "true" : "false" );
        console_log.infof( "Is Nested: %s", this->GetNestedHints() != nullptr ? "true" : "false" );
        console_log.infof( "Needs Frame Sync: %s", this->NeedsFrameSync() ? "true" : "false" );
        console_log.infof( "Total Presents Queued: %lu", this->PresentationFeedback().TotalPresentsQueued() );
        console_log.infof( "Total Presents Completed: %lu", this->PresentationFeedback().TotalPresentsCompleted() );
        console_log.infof( "Current Presents In Flight: %lu", this->PresentationFeedback().CurrentPresentsInFlight() );
    }

    ConCommand cc_backend_info( "backend_info", "Dump debug info about the backend state",
    []( std::span<std::string_view> svArgs )
    {
        if ( !GetBackend() )
            return;

        GetBackend()->DumpDebugInfo();
    });
}
