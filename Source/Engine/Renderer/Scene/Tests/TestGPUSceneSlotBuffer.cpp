// GPUSceneSlotBufferTest — 칸 순 인스턴스 버퍼의 CPU 사본: 바뀐 칸 구간만 비교해 올리고, 배치 번호가 그대로인 재구성은 올리지 않으며, 빠진 칸은 구멍 · 다시 쓴 칸은 차례로 가른다(nogpu).
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

        /** @brief 칸 s 의 인스턴스가 x = s 인 칸 순 배열입니다(@p holeSlot 칸은 구멍). */
        static vector<GPUInstance> makeSlots( uint32 slotCount, uint32 holeSlot = constant::invalid_index::kUint32 )
        {
            vector<GPUInstance> listSlot;
            for ( uint32 slot = 0; slot < slotCount; ++slot )
                listSlot.push_back( slot == holeSlot ? GPUSceneSlotBuffer::makeHole() : makeInstance( static_cast<float32>( slot ) ) );
            return listSlot;
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
 * @details 빌더가 준 바뀐 칸 구간만 본다 — 구간 밖의 칸은 비교하지 않는다.
 */
SW_TEST_CASE( GPUSceneSlotBufferTest, MovedInstanceUploadsOnlyItsSlot )
{
    using Util = SlotBufferTestUtil;
    GPUSceneSlotBuffer            buffer;
    vector<GPUInstance>           listSlot = Util::makeSlots( 4u, 3u );
    vector<GPUInstanceDirtyRange> listRun;
    buffer.apply( listSlot, {}, true );
    SW_EXPECT_FALSE( Util::takeRuns( buffer, listRun ) ); // 처음 — 전체
    SW_REQUIRE_EQUAL( 4u, static_cast<uint32>( buffer.getSlots().size() ) );
    SW_EXPECT_NEAR( 2.0f, buffer.getSlots()[2]._world._41, 0.0f );
    SW_EXPECT_EQUAL( GPUSceneSlotBuffer::kHoleBatchIndex, buffer.getSlots()[3]._meshBatchIndex );

    listSlot[0]._world._41 = 10.0f;
    buffer.apply( listSlot, {
                                GPUInstanceDirtyRange{ 0u, 1u }
    },
                  false );
    SW_REQUIRE_TRUE( Util::takeRuns( buffer, listRun ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), listRun.size() );
    SW_EXPECT_EQUAL( 0u, listRun[0]._start );
    SW_EXPECT_EQUAL( 1u, listRun[0]._count );
    SW_EXPECT_NEAR( 10.0f, buffer.getSlots()[0]._world._41, 0.0f );
}

/**
 * @brief [GPUSceneSlotBufferTest] 전체로 받아도 내용이 같은 칸은 올리지 않고, 빠진 칸은 구멍으로 그 칸만 올린다
 * @details 배치를 다시 나눠도 배치 번호가 그대로인 칸은 그대로다 — 빌더가 "전체" 로 보내도 비교가 가른다.
 */
SW_TEST_CASE( GPUSceneSlotBufferTest, SameContentUploadsNothingAndRemovedSlotBecomesHole )
{
    using Util = SlotBufferTestUtil;
    GPUSceneSlotBuffer            buffer;
    vector<GPUInstanceDirtyRange> listRun;
    buffer.apply( Util::makeSlots( 3u ), {}, true );
    (void)Util::takeRuns( buffer, listRun );

    buffer.apply( Util::makeSlots( 3u ), {}, true );
    SW_REQUIRE_TRUE( Util::takeRuns( buffer, listRun ) );
    SW_EXPECT_TRUE( listRun.empty() );

    buffer.apply( Util::makeSlots( 3u, 1u ), {
                                                 GPUInstanceDirtyRange{ 1u, 1u }
    },
                  false ); // 칸 1 이 빠졌다
    SW_REQUIRE_TRUE( Util::takeRuns( buffer, listRun ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), listRun.size() );
    SW_EXPECT_EQUAL( 1u, listRun[0]._start );
    SW_EXPECT_EQUAL( 1u, listRun[0]._count );
    SW_EXPECT_EQUAL( GPUSceneSlotBuffer::kHoleBatchIndex, buffer.getSlots()[1]._meshBatchIndex );
}

/**
 * @brief [GPUSceneSlotBufferTest] 칸이 늘면 구간에 없어도 새 칸을 받아 올리고, 올리지 못한 변경은 다음 업로드까지 쌓인다
 * @details 업로드를 건너뛴 프레임(빈 장면 · 장치 없음)의 변경을 잃으면 GPU 의 칸이 낡은 채로 남는다.
 */
SW_TEST_CASE( GPUSceneSlotBufferTest, GrownSlotsAndSkippedUploadsAccumulate )
{
    using Util = SlotBufferTestUtil;
    GPUSceneSlotBuffer            buffer;
    vector<GPUInstanceDirtyRange> listRun;
    buffer.apply( Util::makeSlots( 2u ), {}, true );
    (void)Util::takeRuns( buffer, listRun );

    vector<GPUInstance> listSlot = Util::makeSlots( 4u );
    listSlot[0]._world._41       = 5.0f;
    buffer.apply( listSlot, {
                                GPUInstanceDirtyRange{ 0u, 1u }
    },
                  false ); // 올리지 않는다
    listSlot[1]._world._41 = 6.0f;
    buffer.apply( listSlot, {
                                GPUInstanceDirtyRange{ 1u, 1u }
    },
                  false );
    SW_REQUIRE_TRUE( Util::takeRuns( buffer, listRun ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), listRun.size() );
    SW_EXPECT_EQUAL( 0u, listRun[0]._start );
    SW_EXPECT_EQUAL( 4u, listRun[0]._count ); // 칸 0 · 1(쌓인 변경) + 칸 2 · 3(새 칸)
    SW_EXPECT_NEAR( 3.0f, buffer.getSlots()[3]._world._41, 0.0f );
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
    vector<GPUInstance>           listSlot{ Util::makeInstance( 0.0f, 1u ), Util::makeInstance( 1.0f, 1u ) };
    buffer.apply( listSlot, {}, true );
    (void)Util::takeRuns( buffer, listRun );

    listSlot[1] = Util::makeInstance( 1.0f, 2u ); // 같은 자리 · 같은 내용, 다른 차례
    buffer.apply( listSlot, {
                                GPUInstanceDirtyRange{ 1u, 1u }
    },
                  false );
    SW_REQUIRE_TRUE( Util::takeRuns( buffer, listRun ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), listRun.size() );
    SW_EXPECT_EQUAL( 1u, listRun[0]._start );
    SW_EXPECT_EQUAL( 2u, buffer.getSlots()[1]._slotSerial );
}
