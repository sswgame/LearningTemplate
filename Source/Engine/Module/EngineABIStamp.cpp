#include "pch.h"

#include "Engine/Module/EngineABIStamp.h"

#include "Engine/Module/EngineABIStampMarker.h"

#include "EngineABIStamp.gen.h"

namespace sw
{
    namespace engine
    {
        const utf8* getEngineABIStamp()
        {
            return SW_ENGINE_ABI_STAMP_MARKER SW_ENGINE_ABI_STAMP_DIGEST;
        }
    } // namespace engine
} // namespace sw
