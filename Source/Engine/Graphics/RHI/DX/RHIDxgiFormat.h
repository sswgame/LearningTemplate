/**
 * @file RHIDxgiFormat.h
 * @brief RHIFormat → DXGI_FORMAT 변환 (DX11/DX12 공유)
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/RHITypes.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    /** @brief RHIFormat을 DXGI_FORMAT으로 변환합니다. 미지원 포맷이면 어서트 후 DXGI_FORMAT_UNKNOWN. */
    inline DXGI_FORMAT toDxgiFormat( RHIFormat format )
    {
        switch ( format )
        {
            case RHIFormat::R8G8B8A8_UNORM:
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            case RHIFormat::B8G8R8A8_UNORM:
                return DXGI_FORMAT_B8G8R8A8_UNORM;
            case RHIFormat::R16G16B16A16_FLOAT:
                return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case RHIFormat::D24_UNORM_S8_UINT:
                return DXGI_FORMAT_D24_UNORM_S8_UINT;
            case RHIFormat::R32G32B32_FLOAT:
                return DXGI_FORMAT_R32G32B32_FLOAT;
            case RHIFormat::R32G32_FLOAT:
                return DXGI_FORMAT_R32G32_FLOAT;
            case RHIFormat::R32_FLOAT:
                return DXGI_FORMAT_R32_FLOAT;
            case RHIFormat::Unknown:
                return DXGI_FORMAT_UNKNOWN;
            default:
                break;
        }
        SW_LOG_ASSERT( false, "Unsupported RHIFormat: %#", static_cast<uint32>( format ) );
        return DXGI_FORMAT_UNKNOWN;
    }

    /** @brief DXGI_FORMAT 을 RHIFormat 으로 되돌립니다. 대응이 없으면(typeless 등) Unknown. */
    inline RHIFormat fromDxgiFormat( DXGI_FORMAT format )
    {
        switch ( format )
        {
            case DXGI_FORMAT_R8G8B8A8_UNORM:
                return RHIFormat::R8G8B8A8_UNORM;
            case DXGI_FORMAT_B8G8R8A8_UNORM:
                return RHIFormat::B8G8R8A8_UNORM;
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
                return RHIFormat::R16G16B16A16_FLOAT;
            case DXGI_FORMAT_D24_UNORM_S8_UINT:
                return RHIFormat::D24_UNORM_S8_UINT;
            case DXGI_FORMAT_R32G32B32_FLOAT:
                return RHIFormat::R32G32B32_FLOAT;
            case DXGI_FORMAT_R32G32_FLOAT:
                return RHIFormat::R32G32_FLOAT;
            case DXGI_FORMAT_R32_FLOAT:
                return RHIFormat::R32_FLOAT;
            default:
                return RHIFormat::Unknown;
        }
    }
} // namespace sw

#endif
