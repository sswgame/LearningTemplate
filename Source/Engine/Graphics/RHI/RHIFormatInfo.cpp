#include "pch.h"

#include "Engine/Graphics/RHI/RHIFormatInfo.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    const utf8* getRHIFormatName( RHIFormat format )
    {
        const utf8* pName = engine::getSubsystem<TypeRegistry>().enumToString( format );
        return pName != nullptr ? pName : "(unknown)";
    }
} // namespace sw
