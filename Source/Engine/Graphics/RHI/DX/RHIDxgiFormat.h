/**
 * @file RHIDxgiFormat.h
 * @brief RHIFormat → DXGI_FORMAT 변환입니다(DX11 · DX12 공유).
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/RHITypes.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    /** @brief RHIFormat 을 DXGI_FORMAT 으로 변환합니다. 지원하지 않는 포맷이면 assert 뒤 DXGI_FORMAT_UNKNOWN 을 반환합니다. */
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
            case RHIFormat::BC1_UNORM:
                return DXGI_FORMAT_BC1_UNORM;
            case RHIFormat::BC2_UNORM:
                return DXGI_FORMAT_BC2_UNORM;
            case RHIFormat::BC3_UNORM:
                return DXGI_FORMAT_BC3_UNORM;
            case RHIFormat::BC4_UNORM:
                return DXGI_FORMAT_BC4_UNORM;
            case RHIFormat::BC5_UNORM:
                return DXGI_FORMAT_BC5_UNORM;
            case RHIFormat::BC7_UNORM:
                return DXGI_FORMAT_BC7_UNORM;
            default:
                break;
        }
        SW_LOG_ASSERT( false, "Unsupported RHIFormat: %#", static_cast<uint32>( format ) );
        return DXGI_FORMAT_UNKNOWN;
    }

    /**
     * @brief 정점 속성 하나가 쓸 DXGI 포맷을 정합니다(DX11 · DX12 공유).
     * @details 입력 레이아웃은 **공용 표**(`constant::arrVertexAttribute`)에서 만들지만, 그 표의 한 줄을
     *          DXGI 포맷으로 옮기는 이 판단은 두 백엔드가 **각자** 적고 있었습니다. 성분 수를 하나 더하면
     *          (예: 스칼라 float 속성) 한쪽만 고치기 쉬운 자리이고, 그러면 **그 백엔드만 정점이 어긋난
     *          채로 그려집니다.** 이 저장소가 여러 번 겪은 "백엔드 하나만 다른 그림" 의 전형입니다.
     */
    inline DXGI_FORMAT toDxgiVertexFormat( const RHIVertexAttribute& attribute )
    {
        if ( attribute._bUint != SW_FALSE )
            return DXGI_FORMAT_R32_UINT;

        switch ( attribute._componentCount )
        {
            case 4:
                return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case 2:
                return DXGI_FORMAT_R32G32_FLOAT;
            default:
                break;
        }
        return DXGI_FORMAT_R32G32B32_FLOAT;
    }

    /**
     * @brief DXGI_FORMAT 을 RHIFormat 으로 되돌립니다. 대응이 없으면(typeless 등) Unknown 입니다.
     * @details DXGI_FORMAT 은 값이 110 개가 넘는 **플랫폼 enum** 이고 이 엔진이 다루는 것은 그중 일부입니다.
     *          -Wswitch-enum 은 default 가 있어도 모든 값을 적으라고 하는데, 여기서는 그 목록을 유지할 수도
     *          없고 유지할 이유도 없습니다. 새 DXGI 포맷이 생기면 Unknown 이 맞는 답입니다. 반대 방향인
     *          toDxgiFormat 은 경고를 그대로 받습니다: RHIFormat 은 **우리 enum** 이라 늘어나면 알려 줘야 합니다.
     */
    #if defined( __clang__ )
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wswitch-enum"
    #endif
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
            case DXGI_FORMAT_BC1_UNORM:
                return RHIFormat::BC1_UNORM;
            case DXGI_FORMAT_BC2_UNORM:
                return RHIFormat::BC2_UNORM;
            case DXGI_FORMAT_BC3_UNORM:
                return RHIFormat::BC3_UNORM;
            case DXGI_FORMAT_BC4_UNORM:
                return RHIFormat::BC4_UNORM;
            case DXGI_FORMAT_BC5_UNORM:
                return RHIFormat::BC5_UNORM;
            case DXGI_FORMAT_BC7_UNORM:
                return RHIFormat::BC7_UNORM;
            default:
                return RHIFormat::Unknown;
        }
    }
    #if defined( __clang__ )
        #pragma clang diagnostic pop
    #endif
} // namespace sw

#endif
