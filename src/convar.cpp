#include "convar.h"

namespace gamescope
{
    Dict<ConCommand *>& ConCommand::GetCommands()
    {
        static Dict<ConCommand *> s_Commands;
        return s_Commands;
    }
}