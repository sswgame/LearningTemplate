#include "pch.h"

#include "Engine/Module/EngineAbiStamp.h"

#include "EngineAbiStamp.gen.h"

namespace sw
{
    namespace engine
    {
        const utf8* getEngineAbiStamp()
        {
            return SW_ENGINE_ABI_STAMP;
        }
    } // namespace engine
} // namespace sw
