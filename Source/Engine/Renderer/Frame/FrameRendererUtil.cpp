#include "pch.h"

#include "Engine/Renderer/Frame/FrameRendererUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    RHIFormat FrameRendererUtil::parseAttachmentFormat( string_view formatName )
    {
        RHIFormat format{ RHIFormat::R8G8B8A8_UNORM };
        if ( engine::getSubsystem<TypeRegistry>().enumFromString( formatName, format ) == false )
            return RHIFormat::R8G8B8A8_UNORM;
        return format;
    }

    const utf8* FrameRendererUtil::attachmentFormatName( RHIFormat format )
    {
        const utf8* pName = engine::getSubsystem<TypeRegistry>().enumToString( format );
        return pName != nullptr ? pName : "(unknown)";
    }
} // namespace sw
