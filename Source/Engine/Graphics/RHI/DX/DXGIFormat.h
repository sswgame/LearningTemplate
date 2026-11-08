/**
 * @file RHIDxgiFormat.h
 * @brief RHIFormat 에서 DXGI_FORMAT 으로의 변환입니다(DX11, DX12 공유).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/RHIFormatInfo.h"
#include "Engine/Graphics/RHI/RHITypes.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    /** @brief RHIFormat 하나의 DXGI 값입니다. D3D 에 없는 형식(ASTC · ETC2 · EAC)과 `Unknown` 은 DXGI_FORMAT_UNKNOWN 이다 — `supportsTextureFormat` 이 거짓이라 쿠킹 변형을 고르지 않는다. */
    struct DXGIFormatRow
    {
        RHIFormat   _format;
        DXGI_FORMAT _dxgiFormat;
    };

    inline constexpr DXGIFormatRow kArrDXGIFormatRow[] = {
        {    RHIFormat::R8G8B8A8_UNORM,     DXGI_FORMAT_R8G8B8A8_UNORM},
        {    RHIFormat::B8G8R8A8_UNORM,     DXGI_FORMAT_B8G8R8A8_UNORM},
        {RHIFormat::R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT},
        { RHIFormat::D24_UNORM_S8_UINT,  DXGI_FORMAT_D24_UNORM_S8_UINT},
        {   RHIFormat::R32G32B32_FLOAT,    DXGI_FORMAT_R32G32B32_FLOAT},
        {      RHIFormat::R32G32_FLOAT,       DXGI_FORMAT_R32G32_FLOAT},
        {         RHIFormat::R32_FLOAT,          DXGI_FORMAT_R32_FLOAT},
        {           RHIFormat::Unknown,            DXGI_FORMAT_UNKNOWN},
        {         RHIFormat::BC1_UNORM,          DXGI_FORMAT_BC1_UNORM},
        {         RHIFormat::BC2_UNORM,          DXGI_FORMAT_BC2_UNORM},
        {         RHIFormat::BC3_UNORM,          DXGI_FORMAT_BC3_UNORM},
        {         RHIFormat::BC4_UNORM,          DXGI_FORMAT_BC4_UNORM},
        {         RHIFormat::BC5_UNORM,          DXGI_FORMAT_BC5_UNORM},
        {         RHIFormat::BC7_UNORM,          DXGI_FORMAT_BC7_UNORM},
        {         RHIFormat::BC6H_UF16,          DXGI_FORMAT_BC6H_UF16},
        {          RHIFormat::R8_UNORM,           DXGI_FORMAT_R8_UNORM},
        {      RHIFormat::R16G16_SNORM,       DXGI_FORMAT_R16G16_SNORM},
        {    RHIFormat::ASTC_4x4_UNORM,            DXGI_FORMAT_UNKNOWN},
        {  RHIFormat::ETC2_RGBA8_UNORM,            DXGI_FORMAT_UNKNOWN},
        {     RHIFormat::EAC_R11_UNORM,            DXGI_FORMAT_UNKNOWN},
        {    RHIFormat::EAC_RG11_UNORM,            DXGI_FORMAT_UNKNOWN},
        {RHIFormat::R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT},
        {      RHIFormat::R16G16_FLOAT,       DXGI_FORMAT_R16G16_FLOAT},
    };
    static_assert( isRHIFormatTableInOrder( kArrDXGIFormatRow ), "kArrRHIDxgiFormatRow needs one row per RHIFormat, in enumerator order" );

    /** @brief RHIFormat 을 DXGI_FORMAT 으로 변환합니다. 범위 밖 값이면 assert 뒤 DXGI_FORMAT_UNKNOWN 을 반환합니다. */
    inline DXGI_FORMAT toDXGIFormat( RHIFormat format )
    {
        const size_t index = static_cast<size_t>( format );
        if ( index < kRHIFormatCount )
            return kArrDXGIFormatRow[index]._dxgiFormat;
        SW_LOG_ASSERT( false, "Unsupported RHIFormat: %#", static_cast<uint32>( format ) );
        return DXGI_FORMAT_UNKNOWN;
    }

    /**
     * @brief 정점 속성 하나가 쓸 DXGI 포맷을 정합니다(DX11, DX12 공유).
     * @details 입력 레이아웃은 **공용 테이블**(`constant::render::getMeshVertexAttributes`)에서 만들고, 그 테이블의 한 항목을
     *          DXGI 포맷으로 옮기는 판단은 두 백엔드가 이 함수 하나를 씁니다. `default:` 를 두지 않는다 — 형식을 더하면 `-Werror=switch` 가 여기를 가리킨다.
     */
    inline DXGI_FORMAT toDXGIVertexFormat( const RHIVertexAttribute& attribute )
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
    uint32 fillDXGIInputElements( RHIMeshVertexFormat format, uint32 consumedMask, TElement ( &outArrElement )[constant::render::kVertexAttributeCount] )
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
            element.Format                      = toDXGIVertexFormat( attribute );
            element.AlignedByteOffset           = attribute._byteOffset;
            element.InputSlot                   = attribute._inputSlot;
            element.InputSlotClass              = bInstance ? kPerInstanceClass : kPerVertexClass;
            element.InstanceDataStepRate        = bInstance ? 1u : 0u;
        }
        return elementCount;
    }

    /**
     * @brief DXGI_FORMAT 을 RHIFormat 으로 되돌립니다. 대응이 없으면(typeless 등) Unknown 입니다.
     * @details 같은 표를 거꾸로 훑습니다. 텍스처 업로드 · 되읽기 때만 불러 표 스물몇 줄이면 충분합니다. DXGI_FORMAT_UNKNOWN 은 표에서 `Unknown` 행이 먼저 나온다.
     */
    inline RHIFormat fromDXGIFormat( DXGI_FORMAT format )
    {
        for ( const DXGIFormatRow& row : kArrDXGIFormatRow )
        {
            if ( row._dxgiFormat == format )
                return row._format;
        }
        return RHIFormat::Unknown;
    }
} // namespace sw

#endif
