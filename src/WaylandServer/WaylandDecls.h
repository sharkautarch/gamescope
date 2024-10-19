#pragma once

namespace gamescope::WaylandServer
{
		struct WlSurfaceFreeList {
			std::mutex mut;
			std::vector<unsigned char*> FreedSurfaces;
		};
		using TFreedSurfaceIter = typename std::vector<unsigned char*>::iterator;
		inline WlSurfaceFreeList wlSurfFreeList{};
		extern TFreedSurfaceIter LookupPtrInFreedSurfaces(auto* ptr);
		extern bool IsNonNullIterator(TFreedSurfaceIter it);
		
    class CWaylandResource;

    template <typename... Types>
	class CWaylandProtocol;

    class CLinuxDrmSyncobjManager;
    class CLinuxDrmSyncobjSurface;
    class CLinuxDrmSyncobjTimeline;
    using CLinuxDrmSyncobj = CWaylandProtocol<CLinuxDrmSyncobjManager>;

    class CReshadeManager;
    using CReshade = CWaylandProtocol<CReshadeManager>;

}
