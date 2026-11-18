// GPUSceneSlotBufferTest — 칸 순 인스턴스 버퍼의 CPU 사본: 바뀐 칸만 흩어 올리고, 그리는 순서만 바뀌면 올리지 않으며, 빠진 칸은 구멍 · 다시 쓴 칸은 차례로 가른다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Scene/GPUSceneSlotBuffer.h"

using namespace sw;

namespace
{
    struct SlotBufferTestUtil
    {
        /** @brief x 축 @p x 에 놓인 인스턴스입니다(칸 차례 @p serial). */
        static GPUInstance makeInstance( float32 x, uint32 serial = 1u )
        {
            GPUInstance instance{};
            instance._world        = float4x4::Identity;
            instance._world._41    = x;
            instance._slotSerial   = serial;
            instance._boundsRadius = 1.0f;
            return instance;
        }

        /** @brief 칸 @p listSlot 의 차례대로 놓인(그리는 순서의) 배열입니다 — 칸 s 의 인스턴스는 x = s. */
        static vector<GPUInstance> makeOrdered( const vector<uint32>& listSlot )
        {
            vector<GPUInstance> listOrdered;
            for ( const uint32 slot : listSlot )
                listOrdered.push_back( makeInstance( static_cast<float32>( slot ) ) );
            return listOrdered;
        }

        /** @brief 바뀐 칸 구간을 모으고 올렸다고 기록합니다. */
        static bool takeRuns( GPUSceneSlotBuffer& buffer, vector<GPUInstanceDirtyRange>& outListRun )
        {
            const bool bScatter = buffer.buildRuns( 16u, outListRun );
            buffer.clearPending();
            return bScatter;
        }
    };
} // namespace

/**
 * @brief [GPUSceneSlotBufferTest] 처음은 전체를 올리고, 그 뒤 한 인스턴스가 움직이면 그 칸 하나만 올린다
 * @details 그리는 순서(차례 1)의 인스턴스가 칸 0 에 있다 — 차례 구간이 칸으로 옮겨져야 한다(차례 번호를 칸으로 쓰면 다른 칸을 올린다).
 */
SW_TEST_CASE( GPUSceneSlotBufferTest, MovedInstanceUploadsOnlyItsSlot )
{
    using Util = SlotBufferTestUtil;
    GPUSceneSlotBuffer            buffer;
    const vector<uint32>          listOrder{ 2u, 0u, 1u };
    vector<GPUInstance>           listOrdered = Util::makeOrdered( listOrder );
    vector<GPUInstanceDirtyRange> listRun;
    buffer.apply( listOrdered, &listOrder, 4u, true, {}, true );
    SW_EXPECT_FALSE( Util::takeRuns( buffer, listRun ) ); // 처음 — 전체
    SW_REQUIRE_EQUAL( 4u, static_cast<uint32>( buffer.getSlots().size() ) );
    SW_EXPECT_NEAR( 2.0f, buffer.getSlots()[2]._world._41, 0.0f );
    SW_EXPECT_EQUAL( GPUSceneSlotBuffer::kHoleBatchIndex, buffer.getSlots()[3]._meshBatchIndex ); // 차례가 가리키지 않은 칸은 구멍

    listOrdered[1]._world._41 = 10.0f; // 차례 1 = 칸 0
    buffer.apply( listOrdered, &listOrder, 4u, false, {
                                                          GPUInstanceDirtyRange{ 1u, 1u }
    },
                  false );
    SW_REQUIRE_TRUE( Util::takeRuns( buffer, listRun ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), listRun.size() );
    SW_EXPECT_EQUAL( 0u, listRun[0]._start );
    SW_EXPECT_EQUAL( 1u, listRun[0]._count );
    SW_EXPECT_NEAR( 10.0f, buffer.getSlots()[0]._world._41, 0.0f );
}

/**
 * @brief [GPUSceneSlotBufferTest] 배치를 다시 나눠 그리는 순서만 바뀌면 올릴 칸이 없고, 빠진 칸은 구멍으로 그 칸만 올린다
 * @details 예전 버퍼는 그리는 순서였다 — 배치를 다시 나누면 인스턴스 전체를 올렸다. 칸 순이면 내용이 그대로인 칸은 그대로다.
 */
SW_TEST_CASE( GPUSceneSlotBufferTest, ReorderUploadsNothingAndRemovedSlotBecomesHole )
{
    using Util = SlotBufferTestUtil;
    GPUSceneSlotBuffer            buffer;
    vector<GPUInstanceDirtyRange> listRun;
    const vector<uint32>          listOrder{ 0u, 1u, 2u };
    buffer.apply( Util::makeOrdered( listOrder ), &listOrder, 3u, true, {}, true );
    (void)Util::takeRuns( buffer, listRun );

    const vector<uint32> listReordered{ 2u, 0u, 1u }; // 같은 내용, 다른 그리는 순서
    buffer.apply( Util::makeOrdered( listReordered ), &listReordered, 3u, true, {}, true );
    SW_REQUIRE_TRUE( Util::takeRuns( buffer, listRun ) );
    SW_EXPECT_TRUE( listRun.empty() );

    const vector<uint32> listWithoutOne{ 2u, 0u }; // 칸 1 이 빠졌다
    buffer.apply( Util::makeOrdered( listWithoutOne ), &listWithoutOne, 3u, true, {}, true );
    SW_REQUIRE_TRUE( Util::takeRuns( buffer, listRun ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), listRun.size() );
    SW_EXPECT_EQUAL( 1u, listRun[0]._start );
    SW_EXPECT_EQUAL( 1u, listRun[0]._count );
    SW_EXPECT_EQUAL( GPUSceneSlotBuffer::kHoleBatchIndex, buffer.getSlots()[1]._meshBatchIndex );
}

/**
 * @brief [GPUSceneSlotBufferTest] 같은 칸을 다른 물체가 같은 내용으로 받아도(차례만 다르다) 그 칸을 다시 올린다
 * @details 칸 번호에 묶인 GPU 상태(인스턴스 LOD 전환 — gpu_cull.hlsl 이 상태에 차례를 함께 적는다)가 다시 시작하려면 GPU 의 차례가 바뀌어야 한다.
 */
SW_TEST_CASE( GPUSceneSlotBufferTest, ReusedSlotWithNewSerialIsReuploaded )
{
    using Util = SlotBufferTestUtil;
    GPUSceneSlotBuffer            buffer;
    vector<GPUInstanceDirtyRange> listRun;
    const vector<uint32>          listOrder{ 0u, 1u };
    vector<GPUInstance>           listOrdered{ Util::makeInstance( 0.0f, 1u ), Util::makeInstance( 1.0f, 1u ) };
    buffer.apply( listOrdered, &listOrder, 2u, true, {}, true );
    (void)Util::takeRuns( buffer, listRun );

    listOrdered[1] = Util::makeInstance( 1.0f, 2u ); // 같은 자리 · 같은 내용, 다른 차례
    buffer.apply( listOrdered, &listOrder, 2u, false, {
                                                          GPUInstanceDirtyRange{ 1u, 1u }
    },
                  false );
    SW_REQUIRE_TRUE( Util::takeRuns( buffer, listRun ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), listRun.size() );
    SW_EXPECT_EQUAL( 1u, listRun[0]._start );
    SW_EXPECT_EQUAL( 2u, buffer.getSlots()[1]._slotSerial );
}
