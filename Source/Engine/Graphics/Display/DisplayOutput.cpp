#include "pch.h"

#include "Engine/Graphics/Display/DisplayOutput.h"

#include "Core/Log/Logger.h"

#include "Engine/Reflection/EnumName.h"

namespace sw
{
    // 능력 비트는 RHI(색 공간)와 출력 종류가 같은 값이다 — 차례가 어긋나면 resolve 가 다른 종류의 비트를 본다.
    static_assert( makeDisplayOutputBit( DisplayOutputKind::SDR ) == makeColorSpaceBit( RHIColorSpace::SRGBNonLinear ), "DisplayOutputKind and RHIColorSpace must share bit order" );
    static_assert( makeDisplayOutputBit( DisplayOutputKind::HDR10 ) == makeColorSpaceBit( RHIColorSpace::HDR10ST2084 ), "DisplayOutputKind and RHIColorSpace must share bit order" );
    static_assert( makeDisplayOutputBit( DisplayOutputKind::ScRGB ) == makeColorSpaceBit( RHIColorSpace::ExtendedSRGBLinear ), "DisplayOutputKind and RHIColorSpace must share bit order" );

    RHIColorSpace DisplayOutputDesc::getColorSpace() const
    {
        switch ( _kind )
        {
            case DisplayOutputKind::SDR:
                return RHIColorSpace::SRGBNonLinear;
            case DisplayOutputKind::HDR10:
                return RHIColorSpace::HDR10ST2084;
            case DisplayOutputKind::ScRGB:
                return RHIColorSpace::ExtendedSRGBLinear;
        }
        return RHIColorSpace::SRGBNonLinear;
    }

    RHIDisplayMetadata DisplayOutputDesc::makeDisplayMetadata() const
    {
        RHIDisplayMetadata metadata;
        metadata._maxLuminanceNits    = _maxLuminanceNits;
        metadata._maxFrameAverageNits = _paperWhiteNits;
        return metadata;
    }

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
