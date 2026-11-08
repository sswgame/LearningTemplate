// GPUInstanceMotionHistoryTest — 화면 속도의 지난 위치 기록(렌더 쪽): 인스턴스 월드 행렬(처음 · 순간이동 · 기록 없음, 멈추면 지난 것 = 이번 것)과 스킨 결과의 두 벌(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Scene/GPUInstanceMotionHistory.h"
#include "Engine/Renderer/Scene/GPUMeshMorphPool.h"

using namespace sw;

namespace
{
    struct MotionHistoryTestUtil
    {
        /** @brief 열쇠 @p key 로 x 축 @p x 에 놓인 인스턴스입니다. */
        static GPUInstance makeInstance( uint64 key, float32 x )
        {
            GPUInstance instance{};
            instance._motionKey = key;
            instance._world     = float4x4::Identity;
            instance._world._41 = x;
            return instance;
        }
    };
} // namespace

/** @brief [GPUInstanceMotionHistoryTest] 첫 프레임은 기록이 없고, 다음 프레임부터 지난번 그린 행렬을 낸다. 멈추면 지난 것이 이번 것이라 움직임이 없다 */
SW_TEST_CASE( GPUInstanceMotionHistoryTest, FirstFrameHasNoHistoryThenFollows )
{
    using Util = MotionHistoryTestUtil;
    GPUInstanceMotionHistory history;
    vector<float4x4>         listPrev;
    vector<GPUInstance>      listInstance{ Util::makeInstance( 7u << 16, 1.0f ) };

    SW_EXPECT_FALSE( history.update( listInstance, listPrev ) ); // 처음 — 비교할 것이 없다
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listPrev.size() ) );
    SW_EXPECT_TRUE( GPUInstanceMotionHistory::isNoHistory( listPrev[0] ) );

    listInstance[0]._world._41 = 3.0f; // 움직였다
    SW_EXPECT_TRUE( history.update( listInstance, listPrev ) );
    SW_EXPECT_FALSE( GPUInstanceMotionHistory::isNoHistory( listPrev[0] ) );
    SW_EXPECT_NEAR( 1.0f, listPrev[0]._41, 1.0e-6f );

    // 멈췄다 — 지난 것(3)이 이번 것과 같아 움직임이 없다. 게임 스레드가 다시 싣지 않은 같은 배열이어도 기록은 나아간다.
    SW_EXPECT_FALSE( history.update( listInstance, listPrev ) );
    SW_EXPECT_NEAR( 3.0f, listPrev[0]._41, 1.0e-6f );
}

/** @brief [GPUInstanceMotionHistoryTest] 순간이동은 열쇠의 차례를 바꾼다 — 바뀐 열쇠는 기록이 없어 긴 속도가 생기지 않는다 */
SW_TEST_CASE( GPUInstanceMotionHistoryTest, TeleportResetsHistory )
{
    using Util = MotionHistoryTestUtil;
    GPUInstanceMotionHistory history;
    vector<float4x4>         listPrev;
    vector<GPUInstance>      listInstance{ Util::makeInstance( ( 9u << 16 ) | 0u, 0.0f ) };
    (void)history.update( listInstance, listPrev );
    listInstance[0] = Util::makeInstance( ( 9u << 16 ) | 1u, 500.0f ); // 같은 컴포넌트, 다음 차례
    SW_EXPECT_FALSE( history.update( listInstance, listPrev ) );
    SW_EXPECT_TRUE( GPUInstanceMotionHistory::isNoHistory( listPrev[0] ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( history.getKeyCount() ) ); // 옛 차례는 그리지 않아 버려졌다
}

/** @brief [GPUInstanceMotionHistoryTest] 열쇠 0(인스턴스 배치 항목) · GPU 회전 인스턴스는 늘 기록이 없고, 그리지 않은 프레임 뒤에 다시 보인 물체도 기록이 없다 */
SW_TEST_CASE( GPUInstanceMotionHistoryTest, UntrackedAndReappearingInstancesHaveNoHistory )
{
    using Util = MotionHistoryTestUtil;
    GPUInstanceMotionHistory history;
    vector<float4x4>         listPrev;
    GPUInstance              spinning = Util::makeInstance( 5u << 16, 0.0f );
    spinning._spinSeed                = 42u;
    vector<GPUInstance> listInstance{ Util::makeInstance( 0u, 0.0f ), spinning, Util::makeInstance( 6u << 16, 0.0f ) };
    (void)history.update( listInstance, listPrev );
    listInstance[0]._world._41 = 1.0f;
    listInstance[1]._world._41 = 1.0f;
    SW_EXPECT_FALSE( history.update( listInstance, listPrev ) ); // 기록 있는 셋째는 멈춰 있다
    SW_EXPECT_TRUE( GPUInstanceMotionHistory::isNoHistory( listPrev[0] ) );
    SW_EXPECT_TRUE( GPUInstanceMotionHistory::isNoHistory( listPrev[1] ) );
    SW_EXPECT_FALSE( GPUInstanceMotionHistory::isNoHistory( listPrev[2] ) );

    // 셋째를 한 프레임 그리지 않았다가 다른 자리에 다시 그린다 — 오래된 행렬로 긴 속도를 내지 않는다.
    vector<GPUInstance> listWithout{ listInstance[0], listInstance[1] };
    (void)history.update( listWithout, listPrev );
    listInstance[2]._world._41 = 100.0f;
    (void)history.update( listInstance, listPrev );
    SW_EXPECT_TRUE( GPUInstanceMotionHistory::isNoHistory( listPrev[2] ) );

    history.reset();
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( history.getKeyCount() ) );
}

/** @brief [GPUInstanceMotionHistoryTest] 기록은 인스턴스 번호가 아니라 열쇠로 찾는다 — 배치를 다시 나눠 순서가 바뀌어도 지난 행렬이 제 물체를 따라간다 */
SW_TEST_CASE( GPUInstanceMotionHistoryTest, HistoryFollowsKeysNotSlots )
{
    using Util = MotionHistoryTestUtil;
    GPUInstanceMotionHistory history;
    vector<float4x4>         listPrev;
    vector<GPUInstance>      listInstance{ Util::makeInstance( 1u << 16, 10.0f ), Util::makeInstance( 2u << 16, 20.0f ) };
    (void)history.update( listInstance, listPrev );
    vector<GPUInstance> listSwapped{ Util::makeInstance( 2u << 16, 21.0f ), Util::makeInstance( 1u << 16, 11.0f ) };
    SW_EXPECT_TRUE( history.update( listSwapped, listPrev ) );
    SW_EXPECT_NEAR( 20.0f, listPrev[0]._41, 1.0e-6f );
    SW_EXPECT_NEAR( 10.0f, listPrev[1]._41, 1.0e-6f );
}

/**
 * @brief [GPUInstanceMotionHistoryTest] 스킨 결과의 지난 벌은 배치가 그대로이고 지난 프레임에 디스패치했을 때만 믿는다 — 첫 프레임 · 디스패치 없는 프레임 뒤는 기록 없음이고 벌은 프레임마다 바뀐다
 */
SW_TEST_CASE( GPUInstanceMotionHistoryTest, SkinResultHistorySwapsHalves )
{
    GPUMeshMorphPool pool; // 디바이스 없이 장부만 본다(스킨 정점이 없어 벌의 이동은 0 이고 유효 여부만 갈린다)
    pool.advanceSkinHistory();
    SW_EXPECT_EQUAL( GPUMeshMorphPool::kInvalidBase, pool.getSkinPreviousShift() ); // 처음 — 배치가 막 생겼다
    pool.markSkinDispatched();
    pool.advanceSkinHistory();
    SW_EXPECT_TRUE( pool.getSkinPreviousShift() != GPUMeshMorphPool::kInvalidBase ); // 지난 프레임에 쓴 벌이 지난 위치다
    pool.advanceSkinHistory();                                                       // 이번 프레임은 디스패치하지 않았다
    SW_EXPECT_EQUAL( GPUMeshMorphPool::kInvalidBase, pool.getSkinPreviousShift() );
    pool.markSkinDispatched();
    pool.advanceSkinHistory();
    SW_EXPECT_TRUE( pool.getSkinPreviousShift() != GPUMeshMorphPool::kInvalidBase );
    pool.release( nullptr ); // 비우면 배치가 바뀐 것이다
    pool.markSkinDispatched();
    pool.advanceSkinHistory();
    SW_EXPECT_EQUAL( GPUMeshMorphPool::kInvalidBase, pool.getSkinPreviousShift() );
}
