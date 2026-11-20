#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/RHIFormatInfo.h"

using namespace sw;

/** @brief [RHIFormatInfoTest] 표가 예전 손 표(블록 · 렌더 가능 · 깊이 · 실수)와 같은 답을 냅니다. */
SW_TEST_CASE( RHIFormatInfoTest, RowsAnswerTheFormatFacts )
{
    SW_EXPECT_EQUAL( 8u, RHIFormatUtil::getBlockInfo( RHIFormat::R16G16B16A16_FLOAT )._blockBytes );
    SW_EXPECT_EQUAL( 12u, RHIFormatUtil::getBytesPerPixel( RHIFormat::R32G32B32_FLOAT ) );
    SW_EXPECT_EQUAL( 4u, RHIFormatUtil::getBlockInfo( RHIFormat::EAC_R11_UNORM )._blockWidth );
    SW_EXPECT_EQUAL( 0u, RHIFormatUtil::getBytesPerPixel( RHIFormat::BC7_UNORM ) );
    SW_EXPECT_EQUAL( 0u, RHIFormatUtil::getBytesPerPixel( RHIFormat::D24_UNORM_S8_UINT ) );

    SW_EXPECT_TRUE( RHIFormatUtil::isRenderable( RHIFormat::R16G16_SNORM ) );
    SW_EXPECT_FALSE( RHIFormatUtil::isRenderable( RHIFormat::R32G32B32_FLOAT ) );
    SW_EXPECT_TRUE( RHIFormatUtil::isDepth( RHIFormat::D24_UNORM_S8_UINT ) );
    SW_EXPECT_FALSE( RHIFormatUtil::isDepth( RHIFormat::R32_FLOAT ) );
    SW_EXPECT_TRUE( RHIFormatUtil::isFloatTexel( RHIFormat::R32G32_FLOAT ) );
    SW_EXPECT_FALSE( RHIFormatUtil::isFloatTexel( RHIFormat::BC6H_UF16 ) );
}

/** @brief [RHIFormatInfoTest] 범위 밖 값은 Unknown 행이고, 첨부도 업로드도 될 수 없습니다. */
SW_TEST_CASE( RHIFormatInfoTest, OutOfRangeReadsTheUnknownRow )
{
    const RHIFormat outOfRange = static_cast<RHIFormat>( kRHIFormatCount );
    SW_EXPECT_TRUE( RHIFormatInfoTable::find( outOfRange )._format == RHIFormat::Unknown );
    SW_EXPECT_FALSE( RHIFormatUtil::isRenderable( outOfRange ) );
    SW_EXPECT_EQUAL( 0u, RHIFormatUtil::getBlockInfo( outOfRange )._blockBytes );
}

/** @brief [RHIFormatInfoTest] 첨부가 되는 형식은 압축이 아니고, 깊이는 첨부가 되며 CPU 바이트를 말하지 않습니다(읽기 배치는 kRHIDepthReadbackFormat 이 말한다). */
SW_TEST_CASE( RHIFormatInfoTest, EveryRowKeepsTheColumnsConsistent )
{
    for ( const RHIFormatInfo& row : RHIFormatInfoTable::kArrRow )
    {
        const bool bCompressed = row._block._blockWidth > 1;
        SW_EXPECT_FALSE( row._bRenderable != 0 && bCompressed );
        SW_EXPECT_FALSE( row._bFloatTexel != 0 && bCompressed );
        if ( row._bDepth != 0 )
        {
            SW_EXPECT_TRUE( row._bRenderable != 0 );
            SW_EXPECT_EQUAL( 0u, row._block._blockBytes );
        }
    }
}

/** @brief [RHIFormatInfoTest] 선호 형식이 없는 표면은 표면이 준 형식을 그대로 고르고 보고합니다. 고를 것이 없으면 실패입니다(R-8 — 예전엔 RGBA8 이라고 보고했다). */
SW_TEST_CASE( RHIFormatInfoTest, SwapChainPickReportsTheAdoptedFormat )
{
    const RHIFormat           arrHDROnly[] = { RHIFormat::R16G16B16A16_FLOAT };
    const optional<RHIFormat> hdr          = RHIFormatUtil::pickSwapChainFormat( RHIFormat::R8G8B8A8_UNORM, span<const RHIFormat>{ arrHDROnly } );
    SW_REQUIRE_TRUE( hdr.has_value() );
    SW_EXPECT_TRUE( *hdr == RHIFormat::R16G16B16A16_FLOAT );

    const RHIFormat           arrBoth[] = { RHIFormat::R8G8B8A8_UNORM, RHIFormat::B8G8R8A8_UNORM };
    const optional<RHIFormat> bgra      = RHIFormatUtil::pickSwapChainFormat( RHIFormat::R16G16B16A16_FLOAT, span<const RHIFormat>{ arrBoth } );
    SW_REQUIRE_TRUE( bgra.has_value() );
    SW_EXPECT_TRUE( *bgra == RHIFormat::B8G8R8A8_UNORM );

    const RHIFormat arrDepthOnly[] = { RHIFormat::D24_UNORM_S8_UINT };
    SW_EXPECT_FALSE( RHIFormatUtil::pickSwapChainFormat( RHIFormat::R8G8B8A8_UNORM, span<const RHIFormat>{ arrDepthOnly } ).has_value() );
    SW_EXPECT_FALSE( RHIFormatUtil::pickSwapChainFormat( RHIFormat::R8G8B8A8_UNORM, span<const RHIFormat>{} ).has_value() );
}

namespace
{
    /** @brief 역매핑을 보는 작은 백엔드 표입니다. */
    struct NativeFormatRow
    {
        RHIFormat _format;
        uint32    _native;
    };
    constexpr NativeFormatRow kArrNativeRow[] = {
        {RHIFormat::R8G8B8A8_UNORM, 37u},
        {RHIFormat::B8G8R8A8_UNORM, 44u},
        {       RHIFormat::Unknown,  0u},
    };
} // namespace

/** @brief [RHIFormatInfoTest] 백엔드 표의 역매핑은 정방향 표 하나로 찾고, 없는 값과 Unknown 행은 찾지 못합니다. */
SW_TEST_CASE( RHIFormatInfoTest, NativeReverseLookupUsesTheForwardTable )
{
    const optional<RHIFormat> bgra = RHIFormatUtil::findOfNative( kArrNativeRow, &NativeFormatRow::_native, 44u );
    SW_REQUIRE_TRUE( bgra.has_value() );
    SW_EXPECT_TRUE( *bgra == RHIFormat::B8G8R8A8_UNORM );
    SW_EXPECT_FALSE( RHIFormatUtil::findOfNative( kArrNativeRow, &NativeFormatRow::_native, 99u ).has_value() );
    SW_EXPECT_FALSE( RHIFormatUtil::findOfNative( kArrNativeRow, &NativeFormatRow::_native, 0u ).has_value() );
}
