#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Graph/RenderGraphMemoryPlan.h"

using namespace sw;

namespace
{
    RenderGraphMemoryItem makeItem( const char* pName, uint64 sizeBytes, uint64 alignment, uint32 firstLevel, uint32 lastLevel )
    {
        return RenderGraphMemoryItem{ hashed_string( pName ), sizeBytes, alignment, firstLevel, lastLevel };
    }
} // namespace

/** @brief [RenderGraphMemoryPlanTest] 수명이 겹치지 않으면 서술 · 크기가 달라도 같은 바이트에 놓이고, 힙은 가장 큰 것만큼이다 */
SW_TEST_CASE( RenderGraphMemoryPlanTest, DisjointLifetimesShareBytes )
{
    const vector<RenderGraphMemoryItem> listItem{ makeItem( "Bloom", 8 << 20, 65536, 0, 2 ), makeItem( "TonemapLDR", 4 << 20, 65536, 3, 5 ), makeItem( "Outline", 6 << 20, 65536, 6, 7 ) };
    const RenderGraphMemoryPlan         plan = RenderGraphMemoryPlan::build( listItem );
    SW_REQUIRE_EQUAL( listItem.size(), plan._listOffset.size() );
    SW_EXPECT_EQUAL( uint64( 0 ), plan._listOffset[0] );
    SW_EXPECT_EQUAL( uint64( 0 ), plan._listOffset[1] );
    SW_EXPECT_EQUAL( uint64( 0 ), plan._listOffset[2] );
    SW_EXPECT_EQUAL( uint64( 8 << 20 ), plan._heapBytes );
    SW_EXPECT_EQUAL( uint64( 18 << 20 ), plan._sumBytes );
}

/** @brief [RenderGraphMemoryPlanTest] 수명이 겹치는 항목(같은 레벨 끝 · 시작 포함)은 바이트가 겹치지 않고, 오프셋은 정렬의 배수다 */
SW_TEST_CASE( RenderGraphMemoryPlanTest, OverlappingLifetimesGetDisjointAlignedRanges )
{
    const vector<RenderGraphMemoryItem> listItem{ makeItem( "A", 100000, 65536, 0, 3 ), makeItem( "B", 70000, 65536, 3, 4 ), makeItem( "C", 50000, 4096, 1, 2 ), makeItem( "D", 30000, 65536, 5, 6 ) };
    const RenderGraphMemoryPlan         plan = RenderGraphMemoryPlan::build( listItem );
    for ( size_t left = 0; left < listItem.size(); ++left )
    {
        SW_EXPECT_EQUAL( uint64( 0 ), plan._listOffset[left] % listItem[left]._alignment );
        for ( size_t right = left + 1; right < listItem.size(); ++right )
        {
            if ( RenderGraphMemoryPlan::overlapsInTime( listItem[left], listItem[right] ) == false )
                continue;
            const uint64 leftEnd  = plan._listOffset[left] + listItem[left]._sizeBytes;
            const uint64 rightEnd = plan._listOffset[right] + listItem[right]._sizeBytes;
            const bool   bBytes   = plan._listOffset[left] < rightEnd && plan._listOffset[right] < leftEnd;
            SW_EXPECT_FALSE( bBytes );
        }
        SW_EXPECT_TRUE( plan._listOffset[left] + listItem[left]._sizeBytes <= plan._heapBytes );
    }
    // A · B 는 레벨 3 을 함께 써서 겹친다. D 는 누구와도 겹치지 않아 0 에 놓인다.
    SW_EXPECT_TRUE( RenderGraphMemoryPlan::overlapsInTime( listItem[0], listItem[1] ) );
    SW_EXPECT_EQUAL( uint64( 0 ), plan._listOffset[3] );
    SW_EXPECT_TRUE( plan._heapBytes < plan._sumBytes );
}

/** @brief [RenderGraphMemoryPlanTest] 크기 0 인 항목은 놓지 않고, 빈 목록은 빈 힙이다 */
SW_TEST_CASE( RenderGraphMemoryPlanTest, EmptyItemsTakeNoBytes )
{
    SW_EXPECT_EQUAL( uint64( 0 ), RenderGraphMemoryPlan::build( {} )._heapBytes );
    const RenderGraphMemoryPlan plan = RenderGraphMemoryPlan::build( { makeItem( "Zero", 0, 65536, 0, 9 ), makeItem( "One", 4096, 4096, 0, 9 ) } );
    SW_EXPECT_EQUAL( uint64( 4096 ), plan._heapBytes );
    SW_EXPECT_EQUAL( uint64( 0 ), plan._listOffset[1] );
}
