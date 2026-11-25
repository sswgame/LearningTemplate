// GPUInstanceMotionHistoryTest — 화면 속도의 지난 위치 기록(렌더 쪽): 인스턴스 월드 행렬(처음 · 순간이동 · 기록 없음, 멈추면 지난 것 = 이번 것)과 스킨 결과의 두 벌(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Renderer/Scene/GPUInstanceMotionHistory.h"
#include "Engine/Renderer/Scene/GPUMeshMorphPool.h"
#include "Engine/Renderer/Scene/GPUScene.h"
#include "Engine/Renderer/Scene/GPUSceneSlotBuffer.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"

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

        /** @brief 칸 순 사본(`GPUSceneSlotBuffer`)이 바뀐 칸을 넘기는 것까지 한 프레임입니다. */
        static bool update( GPUInstanceMotionHistory& history, GPUSceneSlotBuffer& slots, const vector<GPUInstance>& listInstance )
        {
            vector<uint32> listChanged;
            slots.apply( listInstance, {}, true );
            slots.takeChangedSlots( listChanged );
            return history.update( slots.getSlots(), listChanged );
        }
    };
} // namespace

/** @brief [GPUInstanceMotionHistoryTest] 첫 프레임은 기록이 없고, 다음 프레임부터 지난번 그린 행렬을 낸다. 멈추면 지난 것이 이번 것이라 움직임이 없다 */
SW_TEST_CASE( GPUInstanceMotionHistoryTest, FirstFrameHasNoHistoryThenFollows )
{
    using Util = MotionHistoryTestUtil;
    GPUInstanceMotionHistory history;
    GPUSceneSlotBuffer       slots;
    const vector<float4x4>&  listPrev = history.getPreviousWorlds();
    vector<GPUInstance>      listInstance{ Util::makeInstance( 7u << 16, 1.0f ) };

    SW_EXPECT_FALSE( Util::update( history, slots, listInstance ) ); // 처음 — 비교할 것이 없다
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listPrev.size() ) );
    SW_EXPECT_TRUE( GPUInstanceMotionHistory::isNoHistory( listPrev[0] ) );

    listInstance[0]._world._41 = 3.0f; // 움직였다
    SW_EXPECT_TRUE( Util::update( history, slots, listInstance ) );
    SW_EXPECT_FALSE( GPUInstanceMotionHistory::isNoHistory( listPrev[0] ) );
    SW_EXPECT_NEAR( 1.0f, listPrev[0]._41, 1.0e-6f );

    // 멈췄다 — 지난 것(3)이 이번 것과 같아 움직임이 없다. 게임 스레드가 다시 싣지 않은 같은 배열이어도 기록은 나아간다.
    SW_EXPECT_FALSE( Util::update( history, slots, listInstance ) );
    SW_EXPECT_NEAR( 3.0f, listPrev[0]._41, 1.0e-6f );
}

/**
 * @brief [GPUInstanceMotionHistoryTest] 물체가 움직였는지는 스냅샷을 받은 자리에서 그 프레임 값으로 정해진다 — 패스 고르기(모션 블러)가 업로드 앞에서 읽는다
 * @details 변이: 업로드에서 정하면 움직이기 시작한 프레임의 고르기가 지난 프레임 값(멈춤)을 읽는다.
 */
SW_TEST_CASE( GPUInstanceMotionHistoryTest, SceneKnowsMotionWhenSnapshotIsApplied )
{
    using Util = MotionHistoryTestUtil;
    GPUScene   scene;
    uint64     exportSerial{ 0 };
    const auto applyAt = [&]( float32 x )
    {
        GPUSceneSnapshot snapshot{};
        snapshot._pListInstance = make_shared<const vector<GPUInstance>>( vector<GPUInstance>{ Util::makeInstance( 7u << 16, x ) } );
        snapshot._exportSerial  = ++exportSerial;
        scene.applyCPUSnapshot( snapshot );
        return scene.hasObjectMotion();
    };
    SW_EXPECT_FALSE( applyAt( 1.0f ) ); // 처음 — 비교할 것이 없다
    SW_EXPECT_TRUE( applyAt( 3.0f ) );  // 움직이기 시작한 그 프레임
    SW_EXPECT_FALSE( applyAt( 3.0f ) ); // 멈춘 그 프레임
}

/** @brief [GPUInstanceMotionHistoryTest] 순간이동은 열쇠의 차례를 바꾼다 — 바뀐 열쇠는 기록이 없어 긴 속도가 생기지 않는다 */
SW_TEST_CASE( GPUInstanceMotionHistoryTest, TeleportResetsHistory )
{
    using Util = MotionHistoryTestUtil;
    GPUInstanceMotionHistory history;
    GPUSceneSlotBuffer       slots;
    const vector<float4x4>&  listPrev = history.getPreviousWorlds();
    vector<GPUInstance>      listInstance{ Util::makeInstance( ( 9u << 16 ) | 0u, 0.0f ) };
    (void)Util::update( history, slots, listInstance );
    listInstance[0] = Util::makeInstance( ( 9u << 16 ) | 1u, 500.0f ); // 같은 컴포넌트, 다음 차례
    SW_EXPECT_FALSE( Util::update( history, slots, listInstance ) );
    SW_EXPECT_TRUE( GPUInstanceMotionHistory::isNoHistory( listPrev[0] ) );
    listInstance[0] = Util::makeInstance( ( 9u << 16 ) | 0u, 1.0f ); // 옛 차례로 돌아와도 그리지 않은 프레임이 있었다 — 기록이 없다
    (void)Util::update( history, slots, listInstance );
    SW_EXPECT_TRUE( GPUInstanceMotionHistory::isNoHistory( listPrev[0] ) );
}

/** @brief [GPUInstanceMotionHistoryTest] 열쇠 0(인스턴스 배치 항목) · GPU 회전 인스턴스는 늘 기록이 없고, 그리지 않은 프레임 뒤에 다시 보인 물체도 기록이 없다 */
SW_TEST_CASE( GPUInstanceMotionHistoryTest, UntrackedAndReappearingInstancesHaveNoHistory )
{
    using Util = MotionHistoryTestUtil;
    GPUInstanceMotionHistory history;
    GPUSceneSlotBuffer       slots;
    const vector<float4x4>&  listPrev = history.getPreviousWorlds();
    GPUInstance              spinning = Util::makeInstance( 5u << 16, 0.0f );
    spinning._spinSeed                = 42u;
    vector<GPUInstance> listInstance{ Util::makeInstance( 0u, 0.0f ), spinning, Util::makeInstance( 6u << 16, 0.0f ) };
    (void)Util::update( history, slots, listInstance );
    listInstance[0]._world._41 = 1.0f;
    listInstance[1]._world._41 = 1.0f;
    SW_EXPECT_FALSE( Util::update( history, slots, listInstance ) ); // 기록 있는 셋째는 멈춰 있다
    SW_EXPECT_TRUE( GPUInstanceMotionHistory::isNoHistory( listPrev[0] ) );
    SW_EXPECT_TRUE( GPUInstanceMotionHistory::isNoHistory( listPrev[1] ) );
    SW_EXPECT_FALSE( GPUInstanceMotionHistory::isNoHistory( listPrev[2] ) );

    // 셋째를 한 프레임 그리지 않았다가 다른 자리에 다시 그린다 — 오래된 행렬로 긴 속도를 내지 않는다.
    vector<GPUInstance> listWithout{ listInstance[0], listInstance[1] };
    (void)Util::update( history, slots, listWithout );
    listInstance[2]._world._41 = 100.0f;
    (void)Util::update( history, slots, listInstance );
    SW_EXPECT_TRUE( GPUInstanceMotionHistory::isNoHistory( listPrev[2] ) );

    history.reset();
    SW_EXPECT_TRUE( history.getPreviousWorlds().empty() );
}

/** @brief [GPUInstanceMotionHistoryTest] 기록은 인스턴스 번호가 아니라 열쇠로 찾는다 — 배치를 다시 나눠 순서가 바뀌어도 지난 행렬이 제 물체를 따라간다 */
SW_TEST_CASE( GPUInstanceMotionHistoryTest, HistoryFollowsKeysNotSlots )
{
    using Util = MotionHistoryTestUtil;
    GPUInstanceMotionHistory history;
    GPUSceneSlotBuffer       slots;
    const vector<float4x4>&  listPrev = history.getPreviousWorlds();
    vector<GPUInstance>      listInstance{ Util::makeInstance( 1u << 16, 10.0f ), Util::makeInstance( 2u << 16, 20.0f ) };
    (void)Util::update( history, slots, listInstance );
    vector<GPUInstance> listSwapped{ Util::makeInstance( 2u << 16, 21.0f ), Util::makeInstance( 1u << 16, 11.0f ) };
    SW_EXPECT_TRUE( Util::update( history, slots, listSwapped ) );
    SW_EXPECT_NEAR( 20.0f, listPrev[0]._41, 1.0e-6f );
    SW_EXPECT_NEAR( 10.0f, listPrev[1]._41, 1.0e-6f );
}

/**
 * @brief [GPUInstanceMotionHistoryTest] 같은 칸을 다른 물체가 받으면 지난 월드 행렬을 이어받지 않는다 — 칸 순 버퍼에서 칸 번호로 읽어도 새 물체는 기록이 없다
 * @details 인스턴스 버퍼가 칸 순이라 지난 행렬 버퍼도 칸 순이다(binding.hlsli `InstanceData::loadPreviousWorld` 가 칸 번호로 읽는다). 지운 물체의 칸을 같은 프레임에 다른 물체가 받아도
 *          기록은 열쇠로 찾으므로 새 열쇠는 기록 없음이다 — 지운 물체의 위치에서 긴 속도가 생기지 않는다. 옆 칸의 기록은 그대로다.
 */
SW_TEST_CASE( GPUInstanceMotionHistoryTest, ReusedSlotDoesNotInheritHistory )
{
    using Util = MotionHistoryTestUtil;
    GPUInstanceMotionHistory history;
    GPUSceneSlotBuffer       slots;
    const vector<float4x4>&  listPrev = history.getPreviousWorlds();
    vector<GPUInstance>      listSlot{ Util::makeInstance( 3u << 16, 0.0f ), Util::makeInstance( 4u << 16, 50.0f ) };
    (void)Util::update( history, slots, listSlot );
    listSlot[0]._world._41 = 1.0f;                                  // 칸 0 은 같은 물체가 움직였다
    listSlot[1]            = Util::makeInstance( 8u << 16, 51.0f ); // 칸 1 은 지우고 다른 물체가 받았다
    SW_EXPECT_TRUE( Util::update( history, slots, listSlot ) );
    SW_EXPECT_NEAR( 0.0f, listPrev[0]._41, 1.0e-6f );
    SW_EXPECT_TRUE( GPUInstanceMotionHistory::isNoHistory( listPrev[1] ) );
}

/**
 * @brief [GPUInstanceMotionHistoryTest] 올릴 칸은 움직인 칸과 막 멈춘 칸뿐이다 — 인스턴스 수와 무관하고, 멈춘 다음 프레임에는 없다
 * @details 변이: 멈춘 칸을 맞추지 않으면 둘째 프레임의 수가, 모든 칸을 다시 적으면 첫 수가 진다.
 */
SW_TEST_CASE( GPUInstanceMotionHistoryTest, UploadsOnlyMovedAndSettledSlots )
{
    using Util = MotionHistoryTestUtil;
    GPUInstanceMotionHistory history;
    GPUSceneSlotBuffer       slots;
    vector<GPUInstance>      listInstance;
    for ( uint32 index = 0; index < 100; ++index )
    {
        listInstance.push_back( Util::makeInstance( static_cast<uint64>( index + 1 ) << 16, static_cast<float32>( index ) ) );
    }
    (void)Util::update( history, slots, listInstance );
    (void)Util::update( history, slots, listInstance ); // 기록 없음 → 이번 행렬
    history.getDirtySlots().clear();

    listInstance[40]._world._41 += 1.0f;
    SW_EXPECT_TRUE( Util::update( history, slots, listInstance ) );
    SW_EXPECT_EQUAL( 0u, history.getDirtySlots().getCount() ); // 지난 행렬은 어제 것 그대로다 — 이번 행렬만 바뀌었다
    listInstance[40]._world._41 += 1.0f;
    SW_EXPECT_TRUE( Util::update( history, slots, listInstance ) );
    SW_EXPECT_EQUAL( 1u, history.getDirtySlots().getCount() );
    history.getDirtySlots().clear();
    SW_EXPECT_FALSE( Util::update( history, slots, listInstance ) ); // 멈췄다 — 그 칸만 맞춘다
    SW_EXPECT_EQUAL( 1u, history.getDirtySlots().getCount() );
    history.getDirtySlots().clear();
    SW_EXPECT_FALSE( Util::update( history, slots, listInstance ) );
    SW_EXPECT_EQUAL( 0u, history.getDirtySlots().getCount() );
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
