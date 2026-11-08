#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"

using namespace sw;

/** @brief [RHIStructuredBufferRegionsTest] 같은 배열은 조각이 없고, 바뀐 원소 하나는 그 원소만의 조각입니다(원본 · 목적 오프셋이 같다). */
SW_TEST_CASE( RHIStructuredBufferRegionsTest, SingleChangedElement )
{
    const uint32                arrPrevious[6]{ 1, 2, 3, 4, 5, 6 };
    uint32                      arrCurrent[6]{ 1, 2, 3, 4, 5, 6 };
    vector<RHIBufferCopyRegion> listRegion;
    RHIStructuredBuffer::collectChangedRegions( arrPrevious, 6, arrCurrent, 6, sizeof( uint32 ), 0, listRegion );
    SW_EXPECT_TRUE( listRegion.empty() );

    arrCurrent[3] = 40;
    RHIStructuredBuffer::collectChangedRegions( arrPrevious, 6, arrCurrent, 6, sizeof( uint32 ), 0, listRegion );
    SW_REQUIRE_EQUAL( listRegion.size(), static_cast<size_t>( 1 ) );
    SW_EXPECT_EQUAL( listRegion[0]._srcOffset, 12u );
    SW_EXPECT_EQUAL( listRegion[0]._dstOffset, 12u );
    SW_EXPECT_EQUAL( listRegion[0]._sizeBytes, 4u );
}

/** @brief [RHIStructuredBufferRegionsTest] 사이가 붙일 거리 이하인 두 구간은 한 조각이고, 넘으면 두 조각입니다. */
SW_TEST_CASE( RHIStructuredBufferRegionsTest, MergesRunsWithinTheGap )
{
    const uint32                arrPrevious[8]{ 0, 0, 0, 0, 0, 0, 0, 0 };
    const uint32                arrCurrent[8]{ 1, 0, 0, 1, 0, 0, 0, 0 };
    vector<RHIBufferCopyRegion> listRegion;
    RHIStructuredBuffer::collectChangedRegions( arrPrevious, 8, arrCurrent, 8, sizeof( uint32 ), 2, listRegion );
    SW_REQUIRE_EQUAL( listRegion.size(), static_cast<size_t>( 1 ) );
    SW_EXPECT_EQUAL( listRegion[0]._dstOffset, 0u );
    SW_EXPECT_EQUAL( listRegion[0]._sizeBytes, 16u ); // 원소 0..3

    RHIStructuredBuffer::collectChangedRegions( arrPrevious, 8, arrCurrent, 8, sizeof( uint32 ), 1, listRegion );
    SW_REQUIRE_EQUAL( listRegion.size(), static_cast<size_t>( 2 ) );
    SW_EXPECT_EQUAL( listRegion[1]._dstOffset, 12u );
    SW_EXPECT_EQUAL( listRegion[1]._sizeBytes, 4u );
}

/** @brief [RHIStructuredBufferRegionsTest] 이번 배열이 더 길면 늘어난 꼬리는 모두 바뀐 것이고, 지난 배열이 없으면 통째로 한 조각입니다. */
SW_TEST_CASE( RHIStructuredBufferRegionsTest, GrownTailAndNoPrevious )
{
    const uint32                arrPrevious[2]{ 7, 8 };
    const uint32                arrCurrent[4]{ 7, 8, 9, 10 };
    vector<RHIBufferCopyRegion> listRegion;
    RHIStructuredBuffer::collectChangedRegions( arrPrevious, 2, arrCurrent, 4, sizeof( uint32 ), 0, listRegion );
    SW_REQUIRE_EQUAL( listRegion.size(), static_cast<size_t>( 1 ) );
    SW_EXPECT_EQUAL( listRegion[0]._dstOffset, 8u );
    SW_EXPECT_EQUAL( listRegion[0]._sizeBytes, 8u );

    RHIStructuredBuffer::collectChangedRegions( nullptr, 0, arrCurrent, 4, sizeof( uint32 ), 0, listRegion );
    SW_REQUIRE_EQUAL( listRegion.size(), static_cast<size_t>( 1 ) );
    SW_EXPECT_EQUAL( listRegion[0]._dstOffset, 0u );
    SW_EXPECT_EQUAL( listRegion[0]._sizeBytes, 16u );
}
