/**
 * @file RHIFormatInfo.h
 * @brief RHIFormat 하나의 사실(블록 · 렌더 가능 · 깊이 · 실수)을 적은 공통 표입니다. 백엔드 변환 표의 순서 대조도 여기 있습니다.
 * @details 형식 하나를 더할 때 공통 지식은 이 표 한 줄이고, 백엔드는 자기 표 한 줄씩이다(GL · Vulkan · DXGI). 표는 열거 순서로 적고 `constexpr` 로 대조한다 —
 *          `-Wswitch-enum` 대신 `static_assert` 가 빠뜨림과 칸 밀림을 잡는다. 언리얼 `GPixelFormats`(형식마다 블록 · 바이트 · 플랫폼 형식 한 줄)와 같은 모양이다(기억).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIEnums.h"

namespace sw
{
    /**
     * @struct RHIFormatBlockInfo
     * @brief 포맷의 저장 단위입니다. 비압축은 1x1 블록에 픽셀 바이트이고, BC 는 4x4 블록에 8바이트 또는 16바이트입니다.
     */
    struct RHIFormatBlockInfo
    {
        uint32 _blockWidth{ 1 };
        uint32 _blockHeight{ 1 };
        uint32 _blockBytes{ 0 }; ///< 0이면 업로드와 읽기 대상이 아니다(깊이, Unknown).
    };
} // namespace sw

namespace sw
{
    /** @brief RHIFormat 하나의 공통 사실입니다. `RHIFormatInfoTable` 이 유일한 원본입니다. */
    struct RHIFormatInfo
    {
        RHIFormat          _format;
        RHIFormatBlockInfo _block;
        uint8              _bRenderable : 1; ///< 렌더 타깃 · 깊이 첨부로 만들 수 있다. 네 백엔드가 모두 보장하는 포맷만이다(블록 압축 · Unknown 은 안 된다. R32G32B32_FLOAT 는 D3D11 · D3D12 · Vulkan 모두 선택 사항이라 뺀다)
        uint8              _bDepth      : 1; ///< 깊이 포맷이다
        uint8              _bFloatTexel : 1; ///< 텍셀이 비압축 실수다(NaN 검사가 훑을 수 있다)
    };
} // namespace sw

namespace sw
{
    /** @brief RHIFormat 열거자 수입니다. 새 포맷은 맨 뒤에 잇고 이 값을 함께 올린다 — 공통 · 백엔드 표의 대조가 모두 이 값을 본다. */
    inline constexpr size_t kRHIFormatCount = static_cast<size_t>( RHIFormat::R16G16_FLOAT ) + 1;

    /** @brief 형식 표 @p arrRow(`_format` 칸을 가진 행)가 RHIFormat 열거자를 빠짐없이 열거 순서로 적었는지 봅니다. 백엔드 표는 이것을 `static_assert` 로 건다. */
    template <typename TRow, size_t kRowCount>
    constexpr bool isRHIFormatTableInOrder( const TRow ( &arrRow )[kRowCount] )
    {
        if ( kRowCount != kRHIFormatCount )
            return false;
        for ( size_t index = 0; index < kRowCount; ++index )
        {
            if ( static_cast<size_t>( arrRow[index]._format ) != index )
                return false;
        }
        return true;
    }

    /** @brief 형식 공통 표입니다. 값 번호가 곧 행 번호입니다(`RHIFormat` 은 기존 번호를 바꾸지 않는다). */
    struct RHIFormatInfoTable
    {
        static constexpr RHIFormatInfo kArrRow[] = {
            {    RHIFormat::R8G8B8A8_UNORM,  { 1, 1, 4 }, 1, 0, 0},
            {    RHIFormat::B8G8R8A8_UNORM,  { 1, 1, 4 }, 1, 0, 0},
            {RHIFormat::R16G16B16A16_FLOAT,  { 1, 1, 8 }, 1, 0, 1},
            { RHIFormat::D24_UNORM_S8_UINT,  { 1, 1, 0 }, 1, 1, 0},
            {   RHIFormat::R32G32B32_FLOAT, { 1, 1, 12 }, 0, 0, 1},
            {      RHIFormat::R32G32_FLOAT,  { 1, 1, 8 }, 1, 0, 1},
            {         RHIFormat::R32_FLOAT,  { 1, 1, 4 }, 1, 0, 1},
            {           RHIFormat::Unknown,  { 1, 1, 0 }, 0, 0, 0},
            {         RHIFormat::BC1_UNORM,  { 4, 4, 8 }, 0, 0, 0},
            {         RHIFormat::BC2_UNORM, { 4, 4, 16 }, 0, 0, 0},
            {         RHIFormat::BC3_UNORM, { 4, 4, 16 }, 0, 0, 0},
            {         RHIFormat::BC4_UNORM,  { 4, 4, 8 }, 0, 0, 0},
            {         RHIFormat::BC5_UNORM, { 4, 4, 16 }, 0, 0, 0},
            {         RHIFormat::BC7_UNORM, { 4, 4, 16 }, 0, 0, 0},
            {         RHIFormat::BC6H_UF16, { 4, 4, 16 }, 0, 0, 0},
            {          RHIFormat::R8_UNORM,  { 1, 1, 1 }, 1, 0, 0},
            {      RHIFormat::R16G16_SNORM,  { 1, 1, 4 }, 1, 0, 0},
            {    RHIFormat::ASTC_4x4_UNORM, { 4, 4, 16 }, 0, 0, 0},
            {  RHIFormat::ETC2_RGBA8_UNORM, { 4, 4, 16 }, 0, 0, 0},
            {     RHIFormat::EAC_R11_UNORM,  { 4, 4, 8 }, 0, 0, 0},
            {    RHIFormat::EAC_RG11_UNORM, { 4, 4, 16 }, 0, 0, 0},
            {RHIFormat::R32G32B32A32_FLOAT, { 1, 1, 16 }, 0, 0, 1},
            {      RHIFormat::R16G16_FLOAT,  { 1, 1, 4 }, 1, 0, 1},
        };

        /** @brief 포맷의 행입니다. 범위 밖 값이면 `Unknown` 행입니다. */
        static constexpr const RHIFormatInfo& find( RHIFormat format )
        {
            const size_t index = static_cast<size_t>( format );
            return index < kRHIFormatCount ? kArrRow[index] : kArrRow[static_cast<size_t>( RHIFormat::Unknown )];
        }
    };
    static_assert( isRHIFormatTableInOrder( RHIFormatInfoTable::kArrRow ), "RHIFormatInfoTable::kArrRow needs one row per RHIFormat, in enumerator order" );

    /** @brief 포맷의 블록 정보를 반환합니다. 네 백엔드가 밉 크기와 행 바이트를 같은 규칙으로 계산하게 하는 유일한 원본입니다. */
    inline constexpr RHIFormatBlockInfo getRHIFormatBlockInfo( RHIFormat format )
    {
        return RHIFormatInfoTable::find( format )._block;
    }

    /** @brief 블록 압축 포맷인지 확인합니다. */
    inline constexpr bool isRHIFormatBlockCompressed( RHIFormat format )
    {
        return getRHIFormatBlockInfo( format )._blockWidth > 1;
    }

    /** @brief 렌더 타깃(컬러 첨부) 또는 깊이 첨부로 만들 수 있는 포맷인지 확인합니다. 네 백엔드가 모두 보장하는 포맷만 true 입니다. */
    inline constexpr bool isRHIFormatRenderable( RHIFormat format )
    {
        return RHIFormatInfoTable::find( format )._bRenderable != 0;
    }

    /** @brief 깊이 포맷인지 확인합니다. */
    inline constexpr bool isRHIFormatDepth( RHIFormat format )
    {
        return RHIFormatInfoTable::find( format )._bDepth != 0;
    }

    /** @brief 텍셀이 비압축 실수인 포맷인지 확인합니다. */
    inline constexpr bool isRHIFormatFloatTexel( RHIFormat format )
    {
        return RHIFormatInfoTable::find( format )._bFloatTexel != 0;
    }

    /** @brief 비압축 컬러 포맷의 픽셀당 바이트를 반환합니다. 압축, 깊이, Unknown 은 0 입니다. */
    inline constexpr uint32 getRHIFormatBytesPerPixel( RHIFormat format )
    {
        const RHIFormatBlockInfo info = getRHIFormatBlockInfo( format );
        return info._blockWidth == 1 ? info._blockBytes : 0;
    }
} // namespace sw
