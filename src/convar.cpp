#include "convar.h"

namespace gamescope
{
    Dict<ConCommand *>& __attribute__((cold)) ConCommand::GetCommands()
    {
        static Dict<ConCommand *> s_Commands;
        return s_Commands;
    }
}