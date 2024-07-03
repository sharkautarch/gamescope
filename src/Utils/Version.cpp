#include "Version.h"

#include "GamescopeVersion.h"

#include "convar.h"

extern const char *__progname;

namespace gamescope
{
    void PrintVersion()
    {
        console_log.infof( "%s version %s", __progname, gamescope::k_szGamescopeVersion );
    }
}