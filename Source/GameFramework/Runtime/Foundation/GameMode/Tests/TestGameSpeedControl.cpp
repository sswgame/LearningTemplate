#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Runtime/Foundation/GameMode/GameSpeedControl.h"
#include "GameFramework/Runtime/Foundation/Presentation/SimulationViewSync.h"

// GameSpeedControlTest · SimulationViewSyncTest — 디렉터가 같이 쓰는 배속 · 일시정지와 규칙 → 뷰 동기화(감사 K-10, nogpu).

/** @brief [GameSpeedControlTest] 한 단계와 범위는 데이터다 — 범위 끝에서 멈추고, 빠지거나 뒤집힌 범위는 거절한다. 일시정지는 배속을 지우지 않고 뒤집기만 한다. 범위 밖 배속 · 저장값은 거절한다 */
SW_TEST_CASE( GameSpeedControlTest, StepsClampAndRefuseOutOfRange )
{
    sw::GameSpeedControl speed;
    SW_EXPECT_LOG( "GameSpeedControl.RangeInvalid" );
    SW_EXPECT_LOG( "GameSpeedControl.TimeScaleRefused" ); // 범위 없는 배속 · 범위 밖 저장값 · 0 · 100 배속을 거절하며 남긴다
    SW_EXPECT_FALSE( speed.initialize( sw::GameSpeedRange{} ) );
    SW_EXPECT_FALSE( speed.setTimeScale( 1.0f ) ); // 범위가 없으면 어떤 배속도 받지 않는다
    sw::GameSpeedRange range;
    range._minTimeScale = 0.5f;
    range._maxTimeScale = 4.0f;
    range._stepFactor   = 3.0f;
    SW_REQUIRE_TRUE( speed.initialize( range ) );
    speed.applyInput( false, true, false );
    SW_EXPECT_EQUAL( 3.0f, speed.getTimeScale() );
    SW_EXPECT_FALSE( speed.restore( 8.0f, false ) ); // 저장한 배속도 같은 범위로 본다
    range._minTimeScale = 0.25f;
    range._maxTimeScale = 8.0f;
    range._stepFactor   = 2.0f;
    SW_REQUIRE_TRUE( speed.initialize( range ) );
    SW_EXPECT_TRUE( speed.setTimeScale( 1.0f ) );
    SW_EXPECT_EQUAL( 1.0f, speed.getTimeScale() );
    SW_EXPECT_FALSE( speed.isPaused() );
    for ( int32 step = 0; step < 6; ++step )
    {
        speed.applyInput( false, true, false );
    }
    SW_EXPECT_EQUAL( range._maxTimeScale, speed.getTimeScale() );
    for ( int32 step = 0; step < 8; ++step )
    {
        speed.applyInput( false, false, true );
    }
    SW_EXPECT_EQUAL( range._minTimeScale, speed.getTimeScale() );

    speed.applyInput( true, false, false );
    SW_EXPECT_TRUE( speed.isPaused() );
    SW_EXPECT_EQUAL( range._minTimeScale, speed.getTimeScale() );

    SW_EXPECT_FALSE( speed.setTimeScale( 0.0f ) );
    SW_EXPECT_FALSE( speed.restore( 100.0f, false ) );
    SW_EXPECT_TRUE( speed.isPaused() ); // 거절한 복원은 아무것도 바꾸지 않는다
    SW_EXPECT_TRUE( speed.restore( 2.0f, false ) );
    SW_EXPECT_EQUAL( 2.0f, speed.getTimeScale() );
    SW_EXPECT_FALSE( speed.isPaused() );
}

/** @brief [SimulationViewSyncTest] 바뀐 슬롯만 쌓이고, 이번 수집에서 보이지 않은 슬롯은 지워진다. 같은 슬롯이 두 번 쌓여도 오브젝트는 하나다 */
SW_TEST_CASE( SimulationViewSyncTest, OnlyChangedSlotsRespawn )
{
    sw::SimulationViewSync<int32> sync;
    int32                         spawnCount   = 0;
    int32                         destroyCount = 0;
    const auto                    destroy      = [&destroyCount]( sw::GameObjectHandle& inoutHandle )
    {
        if ( inoutHandle.isValid() )
            ++destroyCount;
        inoutHandle = sw::GameObjectHandle{};
    };
    const auto spawn = [&spawnCount]( size_t, int32 id )
    {
        ++spawnCount;
        return sw::GameObjectHandle::make( static_cast<uint64>( 100 + id ), 0 );
    };

    sync.beginCollect();
    sync.show( 0, 7 );
    sync.show( 2, 9 );
    sync.endCollect();
    SW_REQUIRE_TRUE( sync.hasPending() );
    sync.flush( destroy, spawn );
    SW_EXPECT_EQUAL( 2, spawnCount );
    SW_EXPECT_EQUAL( 0, destroyCount );
    SW_EXPECT_FALSE( sync.hasPending() );

    // 같은 모습이면 쌓이지 않는다. 슬롯 2 는 이번에 안 보여 지워지고, 슬롯 0 은 다른 id 로 두 번 쌓여도 하나만 남는다.
    sync.beginCollect();
    sync.show( 0, 8 );
    sync.endCollect();
    sync.beginCollect();
    sync.show( 0, 7 );
    sync.endCollect();
    sync.flush( destroy, spawn );
    SW_EXPECT_EQUAL( 4, spawnCount );
    SW_EXPECT_EQUAL( 3, destroyCount );
    SW_EXPECT_EQUAL( 7, sync.getSlots()[0]._shown );
    SW_EXPECT_TRUE( sync.getSlots()[0]._object.isValid() );
    SW_EXPECT_FALSE( sync.getSlots()[2]._object.isValid() );
}
