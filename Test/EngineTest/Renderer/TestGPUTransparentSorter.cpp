#include "pch.h"

#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"
#include "Engine/Renderer/Scene/GPUTransparentSorter.h"

#include "TestFramework/TestFramework.h"

// GPUTransparentSorterTest — 투명 정렬 순서(정렬 레이어 키, 깊이, 후보 번호)와 추가 뷰 순번 테이블. 디바이스도 씬도 없음(nogpu).

/**
 * @brief [GPUTransparentSorterTest] 순서는 정렬 레이어 키가 작은 것, 깊이가 큰 것(먼 것), 후보 번호가 작은 것 순이다
 */
SW_TEST_CASE( GPUTransparentSorterTest, DrawOrderIsLayerThenFartherThenIndex )
{
    const sw::GPUTransparentSortKey lowLayerNear{ 1u, 1.0f, 5u };
    const sw::GPUTransparentSortKey highLayerFar{ 2u, 100.0f, 0u };
    SW_EXPECT_TRUE( sw::GPUTransparentSorter::isDrawnBefore( lowLayerNear, highLayerFar ) );
    SW_EXPECT_FALSE( sw::GPUTransparentSorter::isDrawnBefore( highLayerFar, lowLayerNear ) );

    const sw::GPUTransparentSortKey fartherKey{ 1u, 9.0f, 7u };
    const sw::GPUTransparentSortKey nearerKey{ 1u, 4.0f, 0u };
    SW_EXPECT_TRUE( sw::GPUTransparentSorter::isDrawnBefore( fartherKey, nearerKey ) );
    SW_EXPECT_FALSE( sw::GPUTransparentSorter::isDrawnBefore( nearerKey, fartherKey ) );

    // 같은 깊이는 후보 번호로 가른다. 자기 자신과는 앞서지 않는다(엄격한 약순서).
    const sw::GPUTransparentSortKey sameDepthFirst{ 1u, 4.0f, 2u };
    const sw::GPUTransparentSortKey sameDepthSecond{ 1u, 4.0f, 3u };
    SW_EXPECT_TRUE( sw::GPUTransparentSorter::isDrawnBefore( sameDepthFirst, sameDepthSecond ) );
    SW_EXPECT_FALSE( sw::GPUTransparentSorter::isDrawnBefore( sameDepthSecond, sameDepthFirst ) );
    SW_EXPECT_FALSE( sw::GPUTransparentSorter::isDrawnBefore( sameDepthFirst, sameDepthFirst ) );
}

/**
 * @brief [GPUTransparentSorterTest] 축이 영벡터면 깊이는 눈까지의 거리², 아니면 눈에서 축 방향으로 잰 깊이다
 */
SW_TEST_CASE( GPUTransparentSorterTest, SortKeyDepthUsesDistanceOrAxis )
{
    const sw::float3 eye{ 1.0f, 2.0f, -10.0f };
    const sw::float3 center{ 4.0f, 6.0f, 0.0f };

    const sw::GPUTransparentSortKey byDistance = sw::GPUTransparentSorter::makeTransparentSortKey( 3u, center, eye, sw::float3{}, 9u );
    SW_EXPECT_EQUAL( byDistance._sortKey, 3u );
    SW_EXPECT_EQUAL( byDistance._candidateIndex, 9u );
    SW_EXPECT_NEAR_EQUAL( byDistance._depth, 9.0f + 16.0f + 100.0f, 1e-4f );

    // 직교 2D 카메라: 시선 축(+Z)으로 재면 카메라의 XY 위치가 깊이에 들어가지 않는다.
    const sw::GPUTransparentSortKey byAxis = sw::GPUTransparentSorter::makeTransparentSortKey( 3u, center, eye, sw::float3{ 0.0f, 0.0f, 1.0f }, 9u );
    SW_EXPECT_NEAR_EQUAL( byAxis._depth, 10.0f, 1e-4f );
}

/**
 * @brief [GPUTransparentSorterTest] 거의 정렬된 입력(삽입 정렬)과 뒤집힌 입력(예산 초과 → std::sort)이 같은 순서를 낸다
 */
SW_TEST_CASE( GPUTransparentSorterTest, InsertionAndFallbackSortAgree )
{
    constexpr uint32         kCount = 64;
    sw::GPUTransparentSorter sorter;

    // 후보 번호 index 의 깊이는 (kCount - index) 라 정렬 결과는 후보 번호 오름차순이다. 둘째 원소만 하나 뒤로 밀어 둔다.
    {
        sw::vector<sw::GPUTransparentSortKey>& listKey = sorter.resizeKeys( kCount );
        for ( uint32 index = 0; index < kCount; ++index )
        {
            listKey[index] = sw::GPUTransparentSortKey{ 0u, static_cast<float32>( kCount - index ), index };
        }
        const sw::GPUTransparentSortKey swapped = listKey[1];
        listKey[1]                              = listKey[2];
        listKey[2]                              = swapped;
    }
    sw::vector<uint32> listNearlySorted( kCount, sw::invalid_index::kUint32 );
    sorter.sortKeys( listNearlySorted );

    // 완전히 뒤집힌 입력: 이동 횟수가 원소당 예산을 넘어 std::sort 로 넘어간다.
    {
        sw::vector<sw::GPUTransparentSortKey>& listKey = sorter.resizeKeys( kCount );
        for ( uint32 index = 0; index < kCount; ++index )
        {
            const uint32 candidateIndex = kCount - 1 - index;
            listKey[index]              = sw::GPUTransparentSortKey{ 0u, static_cast<float32>( kCount - candidateIndex ), candidateIndex };
        }
    }
    sw::vector<uint32> listReversed( kCount, sw::invalid_index::kUint32 );
    sorter.sortKeys( listReversed );

    for ( uint32 index = 0; index < kCount; ++index )
    {
        SW_EXPECT_EQUAL( listNearlySorted[index], index );
        SW_EXPECT_EQUAL( listReversed[index], index );
    }
}

/**
 * @brief [GPUTransparentSorterTest] 추가 뷰 순서: 꼬리마다 순번, 배치 안은 순번 순 슬롯, 배치는 가장 먼저 그릴 순번 순이다
 */
SW_TEST_CASE( GPUTransparentSorterTest, ViewOrderRanksTailAndBatches )
{
    constexpr uint32         kTailBase = 10;
    sw::GPUTransparentSorter sorter;

    // 꼬리 넷(꼬리 번호 0..3). 이 뷰에서는 꼬리 3 이 가장 멀고, 그다음 1, 2, 0 순이다.
    sw::vector<sw::GPUTransparentSortKey>& listKey = sorter.resizeViewKeys( 4 );
    listKey[0]                                     = sw::GPUTransparentSortKey{ 0u, 1.0f, 0u };
    listKey[1]                                     = sw::GPUTransparentSortKey{ 0u, 3.0f, 1u };
    listKey[2]                                     = sw::GPUTransparentSortKey{ 0u, 2.0f, 2u };
    listKey[3]                                     = sw::GPUTransparentSortKey{ 0u, 4.0f, 3u };

    // 배치 둘: [10, 12) 와 [12, 14).
    sw::vector<sw::GPUMeshBatch> listBatch( 2 );
    listBatch[0]._instanceBase  = kTailBase;
    listBatch[0]._instanceCount = 2;
    listBatch[1]._instanceBase  = kTailBase + 2;
    listBatch[1]._instanceCount = 2;

    // 입자 셋(이 뷰의 시점으로 정렬한 키, 후보 번호 = 입자 그리기 번호). 깊이 5 는 모든 배치 앞, 3.5 는 배치 1(첫 깊이 4)과 배치 0(첫 깊이 3) 사이,
    // 3 은 배치 0 의 첫 키와 깊이가 같아 메시 먼저라 모든 배치 뒤다.
    const sw::vector<sw::GPUTransparentSortKey> listParticleKey{
        sw::GPUTransparentSortKey{0u, 5.0f, 2u},
        sw::GPUTransparentSortKey{0u, 3.5f, 0u},
        sw::GPUTransparentSortKey{0u, 3.0f, 1u}
    };

    const sw::GPUViewTransparentOrder order =
        sorter.computeViewOrder( 77u, kTailBase, listBatch, sw::vector_reference<const sw::GPUTransparentSortKey>( listParticleKey ) );
    SW_EXPECT_EQUAL( order._viewID, static_cast<uint64>( 77u ) );
    SW_EXPECT_EQUAL( order._tailBase, kTailBase );

    SW_ASSERT_NOT_NULL( order._pListRank.get() );
    const sw::vector<uint32>& listRank = *order._pListRank;
    SW_ASSERT_EQUAL( listRank.size(), static_cast<size_t>( 4 ) );
    SW_EXPECT_EQUAL( listRank[0], 3u );
    SW_EXPECT_EQUAL( listRank[1], 1u );
    SW_EXPECT_EQUAL( listRank[2], 2u );
    SW_EXPECT_EQUAL( listRank[3], 0u );

    // 배치 0 안은 꼬리 1(순번 1)이 꼬리 0(순번 3)보다 먼저, 배치 1 안은 꼬리 3(순번 0)이 꼬리 2(순번 2)보다 먼저다.
    SW_ASSERT_NOT_NULL( order._pListTailSlot.get() );
    const sw::vector<uint32>& listTailSlot = *order._pListTailSlot;
    SW_ASSERT_EQUAL( listTailSlot.size(), static_cast<size_t>( 4 ) );
    SW_EXPECT_EQUAL( listTailSlot[0], kTailBase + 1 );
    SW_EXPECT_EQUAL( listTailSlot[1], kTailBase + 0 );
    SW_EXPECT_EQUAL( listTailSlot[2], kTailBase + 3 );
    SW_EXPECT_EQUAL( listTailSlot[3], kTailBase + 2 );

    // 배치 1 의 첫 순번(0)이 배치 0 의 첫 순번(1)보다 앞이라 배치 1 을 먼저 그린다.
    SW_ASSERT_EQUAL( order._listBatchOrder.size(), static_cast<size_t>( 2 ) );
    SW_EXPECT_EQUAL( order._listBatchOrder[0], 1u );
    SW_EXPECT_EQUAL( order._listBatchOrder[1], 0u );

    // 입자 자리는 이 뷰의 배치 순서 안의 자리다(주 순서가 아니다).
    SW_ASSERT_EQUAL( order._listParticleSlot.size(), static_cast<size_t>( 3 ) );
    SW_EXPECT_EQUAL( order._listParticleSlot[0]._particleBatchIndex, 2u );
    SW_EXPECT_EQUAL( order._listParticleSlot[0]._drawPosition, 0u );
    SW_EXPECT_EQUAL( order._listParticleSlot[1]._particleBatchIndex, 0u );
    SW_EXPECT_EQUAL( order._listParticleSlot[1]._drawPosition, 1u );
    SW_EXPECT_EQUAL( order._listParticleSlot[2]._particleBatchIndex, 1u );
    SW_EXPECT_EQUAL( order._listParticleSlot[2]._drawPosition, 2u );
}

/**
 * @brief [GPUTransparentSorterTest] 입자 자리: 레이어 키가 먼저이고, 키를 모르는 배치(꼬리 밖) 앞에는 모든 입자가 선다
 */
SW_TEST_CASE( GPUTransparentSorterTest, ParticlePlacementFollowsLayerAndDepth )
{
    // 배치 넷(그리는 순서): 레이어 0 깊이 10, 레이어 0 깊이 2, 레이어 1 깊이 50, 키 없음.
    const sw::vector<sw::GPUTransparentSortKey> listBatchHeadKey{
        sw::GPUTransparentSortKey{                                         0u, 10.0f, 0u},
        sw::GPUTransparentSortKey{                                         0u,  2.0f, 1u},
        sw::GPUTransparentSortKey{                                         1u, 50.0f, 2u},
        sw::GPUTransparentSortKey{sw::GPUTransparentSorter::kNoBatchHeadLayer,  0.0f, 3u}
    };
    // 입자 넷(정렬됨): 레이어 0 깊이 20(맨 앞), 레이어 0 깊이 1(레이어 0 배치 뒤), 레이어 1 깊이 99(레이어 1 배치 앞), 레이어 1 깊이 0(키 없는 배치 앞).
    const sw::vector<sw::GPUTransparentSortKey> listParticleKey{
        sw::GPUTransparentSortKey{0u, 20.0f, 3u},
        sw::GPUTransparentSortKey{0u,  1.0f, 0u},
        sw::GPUTransparentSortKey{1u, 99.0f, 2u},
        sw::GPUTransparentSortKey{1u,  0.0f, 1u}
    };
    sw::vector<sw::GPUTransparentParticleSlot> listSlot;
    sw::GPUTransparentSorter::placeParticles( sw::vector_reference<const sw::GPUTransparentSortKey>( listBatchHeadKey ),
                                              sw::vector_reference<const sw::GPUTransparentSortKey>( listParticleKey ), listSlot );

    SW_ASSERT_EQUAL( listSlot.size(), static_cast<size_t>( 4 ) );
    SW_EXPECT_EQUAL( listSlot[0]._particleBatchIndex, 3u );
    SW_EXPECT_EQUAL( listSlot[0]._drawPosition, 0u );
    SW_EXPECT_EQUAL( listSlot[1]._particleBatchIndex, 0u );
    SW_EXPECT_EQUAL( listSlot[1]._drawPosition, 2u );
    SW_EXPECT_EQUAL( listSlot[2]._particleBatchIndex, 2u );
    SW_EXPECT_EQUAL( listSlot[2]._drawPosition, 2u );
    SW_EXPECT_EQUAL( listSlot[3]._particleBatchIndex, 1u );
    SW_EXPECT_EQUAL( listSlot[3]._drawPosition, 3u );

    // 배치가 없으면 모두 자리 0 이고 입자 순서는 그대로다.
    sw::GPUTransparentSorter::placeParticles( sw::vector_reference<const sw::GPUTransparentSortKey>(),
                                              sw::vector_reference<const sw::GPUTransparentSortKey>( listParticleKey ), listSlot );
    SW_ASSERT_EQUAL( listSlot.size(), static_cast<size_t>( 4 ) );
    for ( const sw::GPUTransparentParticleSlot& slot : listSlot )
    {
        SW_EXPECT_EQUAL( slot._drawPosition, 0u );
    }
}
