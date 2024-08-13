#pragma once

#include <optional>
#include <functional>
#include <span>
#include "../convar.h"

#include <sys/types.h>

namespace gamescope::Process
{
   static inline auto SplitCommaSeparatedList(char* list) {
        std::vector svSplit = Split(list, ",");
        std::vector<int32_t> split(svSplit.size());
        for (size_t i = 0; i < svSplit.size(); i++) {
            auto oParsed = Parse<int32_t>(svSplit[i]);
            split[i] = oParsed ? *oParsed : 0;
        }

        return split;
    }

    static inline auto GetCombinedVector(const std::span<int>& baseArray, std::vector<int> in) noexcept {
        const auto nOldSize = in.size();
        const auto nNewSize = baseArray.size() + nOldSize;

        if (const auto in_max_size = in.max_size(); nNewSize >= in_max_size) {
            __builtin_unreachable();
        }
        std::vector<int> newVec(nNewSize);

        std::ranges::copy(in,        newVec.begin() + baseArray.size());
        std::ranges::copy(baseArray, newVec.begin());

        return newVec;
    }
    void BecomeSubreaper();
    void SetDeathSignal( int nSignal );

    void KillAllChildren( pid_t nParentPid, int nSignal );
    void KillProcess( pid_t nPid, int nSignal );

    std::optional<int> WaitForChild( pid_t nPid );

    // Wait for all children to die,
    // but stop waiting if we hit a specific PID specified by onStopPid.
    // Returns true if we stopped because we hit the pid specified by onStopPid.
    //
    // Similar to what an `init` process would do.
    bool WaitForAllChildren( std::optional<pid_t> onStopPid = std::nullopt );

    bool CloseFd( int nFd );

    void RaiseFdLimit();
    void RestoreFdLimit();
    void ResetSignals();

    void CloseAllFds(std::span<int> nExcludedFds);

    pid_t SpawnProcess( char **argv, std::function<void()> fnPreambleInChild = nullptr, bool bDoubleFork = false, std::vector<int> passThruFds = {} );
    pid_t SpawnProcessInWatchdog( char **argv, bool bRespawn = false, std::function<void()> fnPreambleInChild = nullptr, char* fdPassThruList = nullptr );

    bool HasCapSysNice();
    void SetNice( int nNice );
    void RestoreNice();

    bool SetRealtime();
    void RestoreRealtime();

    const char *GetProcessName();

}