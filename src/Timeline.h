#pragma once

#include <cstdint>
#include <utility>
#include <memory>
#include <limits>

#include "Utils/NonCopyable.h"

namespace gamescope
{
    enum class TimelinePointType
    {
        Acquire,
        Release,
    };

    class CTimeline : public NonCopyable
    {
    public:
        CTimeline( int32_t nSyncobjFd );
        CTimeline( int32_t nSyncobjFd, uint32_t uSyncobjHandle );

        CTimeline( CTimeline &&other )
            : m_nSyncobjFd{ std::exchange( other.m_nSyncobjFd, -1 ) }
            , m_uSyncobjHandle{ std::exchange( other.m_uSyncobjHandle, 0 ) }
        {
        }
        ~CTimeline();

        static std::shared_ptr<CTimeline> CreateFromSyncobjFd( int32_t nSyncobjFd );

        static int32_t GetDrmRenderFD();

        bool IsValid() const { return m_uSyncobjHandle != 0; }

        int32_t GetSyncobjFd() const { return m_nSyncobjFd; }
        uint32_t GetSyncobjHandle() const { return m_uSyncobjHandle; }
    private:
        int32_t m_nSyncobjFd = -1;
        uint32_t m_uSyncobjHandle = 0;
    };

    template <TimelinePointType Type>
    class CTimelinePoint : public NonCopyable
    {
    public:
        static constexpr std::pair<int32_t, bool> k_InvalidEvent = { -1, false };
        static constexpr std::pair<int32_t, bool> k_AlreadySignalledEvent = { -1, true };

        CTimelinePoint( std::shared_ptr<CTimeline> pTimeline, uint64_t ulPoint );
        ~CTimelinePoint();

        void SetPoint( uint64_t ulPoint ) { m_ulPoint = ulPoint; }

              std::shared_ptr<CTimeline> &GetTimeline()       { return m_pTimeline; }
        const std::shared_ptr<CTimeline> &GetTimeline() const { return m_pTimeline; }

        constexpr bool ShouldSignalOnDestruction() const { return Type == TimelinePointType::Release; }

        bool Wait( int64_t lTimeout = std::numeric_limits<int64_t>::max() );

        std::pair<int32_t, bool> CreateEventFd();
    private:

        std::shared_ptr<CTimeline> m_pTimeline;
        uint64_t m_ulPoint = 0;

    };

    using CAcquireTimelinePoint = CTimelinePoint<TimelinePointType::Acquire>;
    using CReleaseTimelinePoint = CTimelinePoint<TimelinePointType::Release>;

}
