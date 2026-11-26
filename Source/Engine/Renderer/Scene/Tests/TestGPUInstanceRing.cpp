// GPUInstanceRingTest — 인스턴스 배열 링의 따라잡기: 사이 발행에서 배열이 줄었다 다시 늘면, 늘어난 칸이 구간에 없어도 그 뒤를 발행본에 맞춘다(nogpu).
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Renderer/Scene/GPUInstanceRing.h"

using namespace sw;

namespace
{
    struct InstanceRingTestUtil
    {
        /** @brief x 축 @p x 에 놓인 인스턴스입니다. */
        static GPUInstance makeInstance( float32 x )
        {
            GPUInstance instance{};
            instance._world     = float4x4::Identity;
            instance._world._41 = x;
            return instance;
        }
    };
} // namespace

/**
 * @brief [GPUInstanceRingTest] 길이 5 로 발행된 슬롯을 길이 3 · 5 발행 뒤에 다시 쓰면, 구간에 없는 꼬리 [3, 5) 도 마지막 발행본과 같다
 * @details 칸 순 배열은 칸이 끝에 붙어 길이가 바뀐다 — 예전에는 길이가 다르면 통째로 복사했고, 길이만 같으면 구간만 복사해 줄었다 는 사이의 꼬리를 낡은 채로 두었다.
 */
SW_TEST_CASE( GPUInstanceRingTest, ShrinkThenGrowCatchesUpTheTail )
{
    using Util = InstanceRingTestUtil;
    GPUInstanceRing ring;

    // 발행 1 — 슬롯 A, 길이 5. 손잡이를 쥐어 다음 두 발행이 다른 슬롯을 고르게 한다.
    vector<GPUInstance>& first = ring.acquireWrite();
    for ( uint32 slot = 0; slot < 5u; ++slot )
        first.push_back( Util::makeInstance( static_cast<float32>( slot ) ) );
    shared_ptr<const vector<GPUInstance>> pFirst = ring.publish( true, {} );

    // 발행 2 — 슬롯 B, 길이 3(바뀐 구간 없음).
    ring.syncWriteFromPublished();
    ring.acquireWrite().resize( 3u );
    (void)ring.publish( false, {} );

    // 발행 3 — 슬롯 C, 길이 5. 늘어난 칸은 구간에 싣지 않는다(빌더가 끝에 붙인 구멍 칸처럼).
    ring.syncWriteFromPublished();
    vector<GPUInstance>& third = ring.acquireWrite();
    third.push_back( Util::makeInstance( 30.0f ) );
    third.push_back( Util::makeInstance( 40.0f ) );
    (void)ring.publish( false, {} );

    // 슬롯 A 를 놓고 다시 쓴다 — A 는 길이 5 였지만 [3, 5) 는 낡았다.
    pFirst.reset();
    ring.syncWriteFromPublished();
    const vector<GPUInstance>& caughtUp   = ring.acquireWrite();
    const vector<GPUInstance>* pPublished = ring.getPublished();
    SW_REQUIRE_TRUE( pPublished != nullptr );
    SW_REQUIRE_EQUAL( pPublished->size(), caughtUp.size() );
    SW_EXPECT_NEAR( 30.0f, caughtUp[3]._world._41, 0.0f );
    SW_EXPECT_NEAR( 40.0f, caughtUp[4]._world._41, 0.0f );
    SW_EXPECT_EQUAL( 0, Memory::compare( caughtUp.data(), pPublished->data(), caughtUp.size() * sizeof( GPUInstance ) ) );
}

/**
 * @brief [GPUInstanceRingTest] 길이가 그대로면 바뀐 구간만 맞춘다(구간 밖은 슬롯의 옛 내용이 이미 같다)
 */
SW_TEST_CASE( GPUInstanceRingTest, SameLengthCopiesOnlyDirtyRuns )
{
    using Util = InstanceRingTestUtil;
    GPUInstanceRing ring;

    vector<GPUInstance>& first = ring.acquireWrite();
    for ( uint32 slot = 0; slot < 4u; ++slot )
        first.push_back( Util::makeInstance( static_cast<float32>( slot ) ) );
    (void)ring.publish( true, {} );

    // 발행 2 — 새 슬롯(통째로 맞춘 뒤) 칸 2 만 바꾼다.
    ring.syncWriteFromPublished();
    ring.acquireWrite()[2] = Util::makeInstance( 20.0f );
    (void)ring.publish( false, {
                                   GPUInstanceDirtyRange{ 2u, 1u }
    } );

    // 발행 1 의 슬롯이 다시 골라진다 — 칸 2 만 따라잡으면 발행본과 같다.
    ring.syncWriteFromPublished();
    const vector<GPUInstance>& caughtUp = ring.acquireWrite();
    SW_REQUIRE_EQUAL( size_t( 4 ), caughtUp.size() );
    SW_EXPECT_NEAR( 20.0f, caughtUp[2]._world._41, 0.0f );
    SW_EXPECT_NEAR( 1.0f, caughtUp[1]._world._41, 0.0f );
}
