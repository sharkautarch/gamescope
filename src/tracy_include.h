#pragma once
#include <exception>
#include "tracy/Tracy.hpp"


// (from 3.4.6 of Tracy manual) Tracy doesn't support exiting within a zone.
// workaround is to throw a custom exception,
// then catch exception at the end of the function

extern const char* const sl_steamcompmgr_name;
extern const char* const sl_vblankFrameName;

#ifdef TRACY_ENABLE
#	define EXIT(status) throw ETracyExit(status)
#	define PTHREAD_EXIT(status) throw ETracyExit(status, true)
#	define MAYBE_NORETURN
#	define TRACY_TRY try
#	define TRACY_CATCH catch(const ETracyExit& e) { e.m_bPthreadExit ? pthread_exit(const_cast<void*>(e.m_pStatus)) : exit(e.m_status); }
# define TracyDoubleLockable( type, varname ) tracy::DoubleLockable<type> varname { [] () -> const tracy::SourceLocationData* { static constexpr tracy::SourceLocationData srcloc { nullptr, #type " " #varname, TracyFile, TracyLine, 0 }; return &srcloc; }() }
#else
#	define EXIT(status) exit(status)
#	define PTHREAD_EXIT(status) pthread_exit(status)
#	define MAYBE_NORETURN [[noreturn]]
#	define TRACY_TRY
#	define TRACY_CATCH 
#	define tracy_force_inline
# define TracyDoubleLockable( type, varname ) type varname
#endif

#ifdef TRACY_ENABLE
struct ETracyExit : public std::exception {
		const int m_status;
		const void * const m_pStatus;
		const bool m_bPthreadExit;
		ETracyExit(void* pStatus, bool bPthreadExit) : m_status{0}, m_pStatus{pStatus}, m_bPthreadExit{bPthreadExit} {}
		ETracyExit(int status) : m_status{status}, m_pStatus{nullptr}, m_bPthreadExit{false} {} 
		constexpr const char * what () {
		    return "Custom exception for safely exiting from within a Tracy profiler zone";
		}
};

namespace tracy
{
	template<class T>
	class DoubleLockable
	{
	public:
		  tracy_force_inline DoubleLockable( const SourceLocationData* srcloc )
		      : m_ctx( srcloc )
		  {
		  }

		  DoubleLockable( const DoubleLockable& ) = delete;
		  DoubleLockable& operator=( const DoubleLockable& ) = delete;

			tracy_force_inline void lock()
		  {
		      const auto runAfter = m_ctx.BeforeLock();
		      m_lockable.lock();
		      if( runAfter ) m_ctx.AfterLock();
		  }

		  tracy_force_inline void unlock()
		  {
		      m_lockable.unlock();
		      m_ctx.AfterUnlock();
		  }

		  static tracy_force_inline void doubleLock(DoubleLockable& l1, DoubleLockable& l2)
		  {
		      const auto runAfter1 = l1.m_ctx.BeforeLock();
		      const auto runAfter2 = l2.m_ctx.BeforeLock();
		      std::lock(l1.m_lockable, l2.m_lockable);
		      if( runAfter1 ) l1.m_ctx.AfterLock();
		      if( runAfter2 ) l2.m_ctx.AfterLock();
		  }

		  static tracy_force_inline void doubleUnlock(DoubleLockable& l1, DoubleLockable& l2)
		  {
		      l1.m_lockable.unlock();
		      l2.m_lockable.unlock();
		      l1.m_ctx.AfterUnlock();
		      l2.m_ctx.AfterUnlock();
		  }

		  tracy_force_inline bool try_lock()
		  {
		      const auto acquired = m_lockable.try_lock();
		      m_ctx.AfterTryLock( acquired );
		      return acquired;
		  }

		  tracy_force_inline void Mark( const SourceLocationData* srcloc )
		  {
		      m_ctx.Mark( srcloc );
		  }

		  tracy_force_inline void CustomName( const char* name, size_t size )
		  {
		      m_ctx.CustomName( name, size );
		  }

	private:
		  T m_lockable;
		  LockableCtx m_ctx;
	};
}
#endif