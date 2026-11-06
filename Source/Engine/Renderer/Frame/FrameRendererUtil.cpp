#include "pch.h"

#include "Engine/Renderer/Frame/FrameRendererUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    const utf8* FrameRendererUtil::attachmentFormatName( RHIFormat format )
    {
        const utf8* pName = engine::getSubsystem<TypeRegistry>().enumToString( format );
        return pName != nullptr ? pName : "(unknown)";
    }
} // namespace sw
