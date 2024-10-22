#include "backend.h"
#include "Backends/HeadlessBackend.hpp"
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
		
		
    inline std::atomic<IBackend*> __attribute__((visibility("default")))  s_pBackend = nullptr;
		IBackend *IBackend::Get()
    {
        return s_pBackend.load();
    }
    
		
		///////////////////////////////////////////
		//	IBackend Internal Book-keeping bits: //
		///////////////////////////////////////////
		static inline size_t s_currentBackendSize = 0;
		static inline gamescope::CHeadlessBackend* s_pHeadless = nullptr;
		static inline size_t s_backendOffset = 0;
		static inline IBackend *s_pOldBackend = nullptr; //for internal book-keeping only
    
    
    //order of usage (B<num> Backend obj):
    // operator new B1 -> ReplaceBackend(B1) -> operator new B2 -> B1->DestroyBackend() -> ReplaceBackend(B2)
		void* IBackend::operator new(size_t size) {
			printf("operator new()\n");
			AcquireExclusive();
			s_currentBackendSize = size;
			auto* ptr = malloc(size*2); //always allocate enough space for both the actual class and a headless backend
			auto* pChars = std::bit_cast<unsigned char*>(ptr);
			auto* pHeadlessAddr = std::bit_cast<gamescope::CHeadlessBackend*>(pChars + size);
			s_pHeadless = std::construct_at(pHeadlessAddr);
			//^construct headless backend from the extra allocated memory
			assert(size >= sizeof(gamescope::CHeadlessBackend));
			return ptr;
		}
		
		void IBackend::DestroyBackend() {
			printf("operator delete()\n");
			AcquireExclusive();
			auto* pCharsPtr = std::bit_cast<unsigned char*>(this);
			if (s_pOldBackend != nullptr && (s_backendOffset > 0)
						&& pCharsPtr != nullptr && std::bit_cast<IBackend*>(pCharsPtr - s_backendOffset) == s_pBackend.load(std::memory_order_relaxed)
						&& std::bit_cast<IBackend*>(pCharsPtr - s_backendOffset) == s_pOldBackend) [[unlikely]] 
			{
					return; //do nothing if we've already marked the backend for deletion, 
					//and have already set the current backend to the dummy headless backend
			}
			
			assert(s_pOldBackend == nullptr);
			assert(s_backendOffset == 0);
			s_backendOffset = s_currentBackendSize; 
			//to avoid use-after-frees and nullptr dereferences from doing s_pBackend-><member>
			//we simply defer actual deallocation until either gamescope exits, or gamescope creates a new IBackend
			
			//to mitigate other race conditions, we atomically update s_pBackend so that it will safely point to a valid CHeadlessBackend object
			
			//a big performance benefit to this approach is that on x86_64 & aarch64, simple seq_cst atomic loads have the same overhead as plain loads (the synchronization [of loads w/ store] cost is actually encurred on the atomic store side)
			
			
			assert(pCharsPtr + s_backendOffset == std::bit_cast<unsigned char*>(s_pHeadless));
			s_pOldBackend = this;
			this->IBackend::~IBackend(); //manually call the backend's destructor, without deallocating the memory
			s_pBackend = dynamic_cast<IBackend*>(s_pHeadless);
		}
		
		
		static inline void __attribute__((nonnull (1)))
			ReplaceBackend(IBackend* pNewBackend) 
		{
			s_pBackend = pNewBackend;
			size_t oldBackendSize = std::exchange(s_backendOffset, 0);
			if (auto* pOld = std::exchange(s_pOldBackend, nullptr);
					 pOld != nullptr)
		  { //delete the old backend for realsies
				assert(oldBackendSize > 0);
				s_pHeadless=nullptr;
				free(reinterpret_cast<void*>(pOld)); //invoking the *global* deleter (deletes backend & dummy headless backend block)
			}
		} 
		
		///////////////////////////////////////////
		///////////////////////////////////////////
    bool IBackend::Set( IBackend *pBackend )
    {
    		AcquireExclusive();
        if ( s_pBackend )
        {
            GetBackend()->DestroyBackend(); //we're intentionally *not* setting s_pBackend to nullptr after deletion.
            //the DestroyBackend() method ensures that 
            //IBackend::Get will still point to a safe memory region (a CHeadlessBackend object) after it is run
        }

        if ( pBackend )
        {
            ReplaceBackend(pBackend);
            if ( !GetBackend()->Init() )
            {
                pBackend->DestroyBackend();
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
