#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/RHIFormatInfo.h"

using namespace sw;

/** @brief [RHIFormatInfoTest] 표가 예전 손 표(블록 · 렌더 가능 · 깊이 · 실수)와 같은 답을 냅니다. */
SW_TEST_CASE( RHIFormatInfoTest, RowsAnswerTheFormatFacts )
{
    SW_EXPECT_EQUAL( 8u, getRHIFormatBlockInfo( RHIFormat::R16G16B16A16_FLOAT )._blockBytes );
    SW_EXPECT_EQUAL( 12u, getRHIFormatBytesPerPixel( RHIFormat::R32G32B32_FLOAT ) );
    SW_EXPECT_EQUAL( 4u, getRHIFormatBlockInfo( RHIFormat::EAC_R11_UNORM )._blockWidth );
    SW_EXPECT_EQUAL( 0u, getRHIFormatBytesPerPixel( RHIFormat::BC7_UNORM ) );
    SW_EXPECT_EQUAL( 0u, getRHIFormatBytesPerPixel( RHIFormat::D24_UNORM_S8_UINT ) );

    SW_EXPECT_TRUE( isRHIFormatRenderable( RHIFormat::R16G16_SNORM ) );
    SW_EXPECT_FALSE( isRHIFormatRenderable( RHIFormat::R32G32B32_FLOAT ) );
    SW_EXPECT_TRUE( isRHIFormatDepth( RHIFormat::D24_UNORM_S8_UINT ) );
    SW_EXPECT_FALSE( isRHIFormatDepth( RHIFormat::R32_FLOAT ) );
    SW_EXPECT_TRUE( isRHIFormatFloatTexel( RHIFormat::R32G32_FLOAT ) );
    SW_EXPECT_FALSE( isRHIFormatFloatTexel( RHIFormat::BC6H_UF16 ) );
}

/** @brief [RHIFormatInfoTest] 범위 밖 값은 Unknown 행이고, 첨부도 업로드도 될 수 없습니다. */
SW_TEST_CASE( RHIFormatInfoTest, OutOfRangeReadsTheUnknownRow )
{
    const RHIFormat outOfRange = static_cast<RHIFormat>( kRHIFormatCount );
    SW_EXPECT_TRUE( RHIFormatInfo::find( outOfRange )._format == RHIFormat::Unknown );
    SW_EXPECT_FALSE( isRHIFormatRenderable( outOfRange ) );
    SW_EXPECT_EQUAL( 0u, getRHIFormatBlockInfo( outOfRange )._blockBytes );
}

/** @brief [RHIFormatInfoTest] 첨부가 되는 형식은 압축이 아니고, 깊이는 첨부가 되며 CPU 바이트를 말하지 않습니다(읽기 배치는 kRHIDepthReadbackFormat 이 말한다). */
SW_TEST_CASE( RHIFormatInfoTest, EveryRowKeepsTheColumnsConsistent )
{
    for ( const RHIFormatInfo& row : RHIFormatInfo::kArrRow )
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
