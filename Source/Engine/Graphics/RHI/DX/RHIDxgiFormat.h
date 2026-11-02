/**
 * @file RHIDxgiFormat.h
 * @brief RHIFormat 에서 DXGI_FORMAT 으로의 변환입니다(DX11, DX12 공유).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/EnginePlatformHeaders.h"
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
            case RHIFormat::BC6H_UF16:
                return DXGI_FORMAT_BC6H_UF16;
            case RHIFormat::R8_UNORM:
                return DXGI_FORMAT_R8_UNORM;
            case RHIFormat::R16G16_SNORM:
                return DXGI_FORMAT_R16G16_SNORM;
        }
        SW_LOG_ASSERT( false, "Unsupported RHIFormat: %#", static_cast<uint32>( format ) );
        return DXGI_FORMAT_UNKNOWN;
    }

    /**
     * @brief 정점 속성 하나가 쓸 DXGI 포맷을 정합니다(DX11, DX12 공유).
     * @details 입력 레이아웃은 **공용 테이블**(`constant::render::getMeshVertexAttributes`)에서 만들고, 그 테이블의 한 항목을
     *          DXGI 포맷으로 옮기는 판단은 두 백엔드가 이 함수 하나를 씁니다. `default:` 를 두지 않는다 — 형식을 더하면 `-Werror=switch` 가 여기를 가리킨다.
     */
    inline DXGI_FORMAT toDxgiVertexFormat( const RHIVertexAttribute& attribute )
    {
        switch ( attribute._format )
        {
            case RHIVertexAttributeFormat::Float2:
                return DXGI_FORMAT_R32G32_FLOAT;
            case RHIVertexAttributeFormat::Float3:
                return DXGI_FORMAT_R32G32B32_FLOAT;
            case RHIVertexAttributeFormat::Float4:
                return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case RHIVertexAttributeFormat::Uint1:
                return DXGI_FORMAT_R32_UINT;
            case RHIVertexAttributeFormat::Snorm8x4:
                return DXGI_FORMAT_R8G8B8A8_SNORM;
        }
        SW_LOG_ASSERT( false, "Unsupported RHIVertexAttributeFormat: %#", static_cast<uint32>( attribute._format ) );
        return DXGI_FORMAT_UNKNOWN;
    }

    /**
     * @brief 메시 정점 형식의 공용 테이블에서 셰이더가 읽는 속성(@p consumedMask)만 골라 입력 요소를 채우고 그 수를 반환합니다(DX11, DX12 공유).
     * @details D3D11 · D3D12 의 입력 요소 구조는 칸이 같고 분류 값만 제 enum 이다 — 정점 · 인스턴스 분류 값을 받아 한 몸으로 채운다.
     */
    template <auto kPerVertexClass, auto kPerInstanceClass, typename TElement>
    uint32 fillDxgiInputElements( RHIMeshVertexFormat format, uint32 consumedMask, TElement ( &outArrElement )[constant::render::kVertexAttributeCount] )
    {
        const RHIVertexAttribute* const pAttributeTable = constant::render::getMeshVertexAttributes( format );
        uint32                          elementCount{ 0 };
        for ( uint32 attributeIndex = 0; attributeIndex < constant::render::kVertexAttributeCount; ++attributeIndex )
        {
            if ( ( consumedMask & ( 1u << attributeIndex ) ) == 0 )
                continue;
            const RHIVertexAttribute& attribute = pAttributeTable[attributeIndex];
            const bool                bInstance = attribute._bPerInstance != SW_FALSE;
            TElement&                 element   = outArrElement[elementCount++];
            element.SemanticName                = attribute._pSemanticName;
            element.Format                      = toDxgiVertexFormat( attribute );
            element.AlignedByteOffset           = attribute._byteOffset;
            element.InputSlot                   = attribute._inputSlot;
            element.InputSlotClass              = bInstance ? kPerInstanceClass : kPerVertexClass;
            element.InstanceDataStepRate        = bInstance ? 1u : 0u;
        }
        return elementCount;
    }

    /**
     * @brief DXGI_FORMAT 을 RHIFormat 으로 되돌립니다. 대응이 없으면(typeless 등) Unknown 입니다.
     * @details DXGI_FORMAT 은 값이 110 개가 넘는 **플랫폼 enum** 이고 이 엔진이 다루는 것은 그중 일부입니다. 대응이 없는 값은 `default:` 가
     *          Unknown 으로 돌립니다(새 DXGI 포맷도 Unknown 이 맞는 답입니다). 반대 방향인 toDxgiFormat 은 `default:` 없이 모든
     *          RHIFormat 을 다룹니다. RHIFormat 은 **우리 enum** 이라 늘면 `-Werror=switch` 가 빌드를 실패시킵니다.
     */
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
            case DXGI_FORMAT_BC6H_UF16:
                return RHIFormat::BC6H_UF16;
            case DXGI_FORMAT_R8_UNORM:
                return RHIFormat::R8_UNORM;
            case DXGI_FORMAT_R16G16_SNORM:
                return RHIFormat::R16G16_SNORM;
            default:
                return RHIFormat::Unknown;
        }
    }
} // namespace sw

#endif
