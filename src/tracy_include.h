#pragma once
#include <exception>
#include <optional>
#include "tracy/Tracy.hpp"
#include "tracy/TracyC.h"

// (from 3.4.6 of Tracy manual) Tracy doesn't support exiting within a zone.
// workaround is to throw a custom exception,
// then catch exception at the end of the function

extern const char* const sl_steamcompmgr_name;
extern const char* const sl_vblankFrameName;
extern const char* const sl_img_waiter_fiber;
inline std::optional<tracy::ScopedZone> g_zone_img_waiter;

#ifdef TRACY_ENABLE
#include <pthread.h>
#	define EXIT(status) throw ETracyExit(status)
#	define PTHREAD_EXIT(status) throw ETracyExit(status, true)
#	define MAYBE_NORETURN
#	define TRACY_TRY try
#	define TRACY_CATCH catch(const ETracyExit& e) { e.m_bPthreadExit ? pthread_exit(const_cast<void*>(e.m_pStatus)) : exit(e.m_status); }
# define TRACY_GET_SRC_LOC(name) [](const std::source_location loc = std::source_location::current()) { return tracy::SourceLocationData(name, loc.function_name(), loc.file_name(), loc.line()); }()
# define TRACY_FIBER_ZONE_START(oVariable, name) static constexpr auto __attribute__((no_icf,used)) tracy_loc = TRACY_GET_SRC_LOC(name); oVariable.emplace(&tracy_loc, true)
# define TRACY_FIBER_ZONE_END(oVariable) oVariable.reset()
# define TracyDoubleLockable( type, varname ) tracy::DoubleLockable<type> varname { [] () -> const tracy::SourceLocationData* { static constexpr tracy::SourceLocationData srcloc { nullptr, #type " " #varname, TracyFile, TracyLine, 0 }; return &srcloc; }() }
//pthread lock instrumentation seems to trigger an assert on tracy server, so leaving it commented out for now:
#	if 0
#		define TracyPthreadLockable( type, varname, initializer ) tracy::PthreadLockable varname { [] () -> const tracy::SourceLocationData* { static constexpr tracy::SourceLocationData srcloc { nullptr, #type " " #varname, TracyFile, TracyLine, 0 }; return &srcloc; }() }; \
		using tracy::pthread_mutex_lock; \
		using tracy::pthread_mutex_unlock; \
		using tracy::pthread_mutex_trylock
#	else
#		define TracyPthreadLockable( type, varname, initializer ) type varname = initializer
#	endif

#else
#	define EXIT(status) exit(status)
#	define PTHREAD_EXIT(status) pthread_exit(status)
#	define MAYBE_NORETURN [[noreturn]]
#	define TRACY_TRY
#	define TRACY_CATCH 
#	define tracy_force_inline
# define TRACY_FIBER_ZONE_START(variable, ctx, name)
# define TRACY_FIBER_ZONE_END(oVariable)
# define TracyDoubleLockable( type, varname ) type varname
#	define TracyPthreadLockable( type, varname, initializer ) type varname = initializer
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
	
	class PthreadLockable
	{
	public:
		  tracy_force_inline PthreadLockable( const SourceLocationData* srcloc )
		      : m_ctx( srcloc )
		  {
		  }

		  PthreadLockable( const PthreadLockable& ) = delete;
		  PthreadLockable& operator=( const PthreadLockable& ) = delete;

		  tracy_force_inline void Mark( const SourceLocationData* srcloc )
		  {
		      m_ctx.Mark( srcloc );
		  }

		  tracy_force_inline void CustomName( const char* name, size_t size )
		  {
		      m_ctx.CustomName( name, size );
		  }

	private:
			friend tracy_force_inline void pthread_mutex_lock(PthreadLockable* mut);
			friend tracy_force_inline void pthread_mutex_unlock(PthreadLockable* mut);
			friend tracy_force_inline int pthread_mutex_trylock(PthreadLockable* mut);
			pthread_mutex_t m_lockable = PTHREAD_MUTEX_INITIALIZER;
		  LockableCtx m_ctx;
	};
	
	tracy_force_inline void pthread_mutex_lock(PthreadLockable* mut)
  {
      const auto runAfter = mut->m_ctx.BeforeLock();
      ::pthread_mutex_lock( &(mut->m_lockable) );
      if( runAfter ) mut->m_ctx.AfterLock();
  }

  tracy_force_inline void pthread_mutex_unlock(PthreadLockable* mut)
  {
      ::pthread_mutex_unlock( &(mut->m_lockable) );
      mut->m_ctx.AfterUnlock();
  }
	tracy_force_inline int pthread_mutex_trylock(PthreadLockable* mut)
  {
      const auto acquired = ::pthread_mutex_trylock( &(mut->m_lockable) );
      mut->m_ctx.AfterTryLock( acquired );
      return acquired;
  }
}
#endif