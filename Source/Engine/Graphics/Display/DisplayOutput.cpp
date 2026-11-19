#include "pch.h"

#include "Engine/Graphics/Display/DisplayOutput.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

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

    DisplayOutputKind DisplayOutputDesc::getKind( RHIColorSpace colorSpace )
    {
        switch ( colorSpace )
        {
            case RHIColorSpace::SRGBNonLinear:
                return DisplayOutputKind::SDR;
            case RHIColorSpace::HDR10ST2084:
                return DisplayOutputKind::HDR10;
            case RHIColorSpace::ExtendedSRGBLinear:
                return DisplayOutputKind::ScRGB;
        }
        return DisplayOutputKind::SDR;
    }

    DisplayOutputDesc DisplayOutputDesc::resolve( const DisplayOutputDesc& requested, const uint8 supportedMask )
    {
        DisplayOutputDesc resolved = requested;
        // 종이 흰색은 최대 휘도를 넘지 않는다 — 넘으면 톤 곡선 어깨가 1 아래로 내려가 장면이 SDR 보다 어두워진다(UI 합성 배율도 1 을 넘는다).
        resolved._paperWhiteNits = MathUtil::min( requested._paperWhiteNits, requested._maxLuminanceNits );
        const uint8 bit          = makeDisplayOutputBit( requested._kind );
        if ( requested._kind == DisplayOutputKind::SDR || ( supportedMask & bit ) != 0 )
            return resolved;
        SW_LOG_INFO_ID( "Display.OutputUnsupported", "Display output '%#' is not supported by this device or monitor - using SDR", getEnumName( requested._kind ) );
        resolved._kind = DisplayOutputKind::SDR;
        return resolved;
    }
} // namespace sw
