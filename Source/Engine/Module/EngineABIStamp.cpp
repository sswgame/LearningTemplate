#include "pch.h"

#include "Engine/Module/EngineABIStamp.h"

#include "EngineABIStamp.gen.h"

namespace sw
{
    namespace engine
    {
        const utf8* getEngineABIStamp()
        {
            return SW_ENGINE_ABI_STAMP;
        }
    } // namespace engine
} // namespace sw
