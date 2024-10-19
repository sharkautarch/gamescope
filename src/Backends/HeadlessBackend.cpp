#include "backend.h"
#include "Backends/HeadlessBackend.hpp"
#include "rendervulkan.hpp"
#include "wlserver.hpp"
#include "refresh_rate.h"

namespace gamescope 
{
	/////////////////////////
	// Backend Instantiator
	/////////////////////////

	template <>
	bool IBackend::Set<CHeadlessBackend>()
	{
		return Set( new CHeadlessBackend{} );
	}

}