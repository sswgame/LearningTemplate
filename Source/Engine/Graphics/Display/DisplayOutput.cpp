#include "pch.h"

#include "Engine/Graphics/Display/DisplayOutput.h"

#include "Core/Log/Logger.h"

#include "Engine/Reflection/EnumName.h"

namespace sw
{
    DisplayOutputDesc DisplayOutputDesc::resolve( const DisplayOutputDesc& requested, const uint8 supportedMask )
    {
        const uint8 bit = makeDisplayOutputBit( requested._kind );
        if ( requested._kind == DisplayOutputKind::SDR || ( supportedMask & bit ) != 0 )
            return requested;
        SW_LOG_INFO_ID( "Display.OutputUnsupported", "Display output '%#' is not supported by this device or monitor - using SDR", getEnumName( requested._kind ) );
        DisplayOutputDesc resolved = requested;
        resolved._kind             = DisplayOutputKind::SDR;
        return resolved;
    }
} // namespace sw
