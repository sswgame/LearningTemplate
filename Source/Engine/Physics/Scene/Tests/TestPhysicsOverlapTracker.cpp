// PhysicsOverlapTrackerTest — 바디 쌍 겹침을 오브젝트 쌍 진입 · 이탈 · 유지로 바꾸는 계약(DesignBacklog "트리거 Enter · Stay · Exit 계약"). 물리 없음(nogpu).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Physics/Scene/PhysicsOverlapTracker.h"

using namespace sw;

namespace
{
    struct TestPhysicsOverlapTrackerInternal
    {
        static constexpr uint64 kTrigger = 10;
        static constexpr uint64 kHero    = 20;
        static constexpr uint64 kCrate   = 30;

        static PhysicsBodyHandle makeBody( uint32 index ) { return PhysicsBodyHandle::fromPacked( 0x100000000ull | index ); }

        /** @brief 트리거(@p triggerBody)와 대상 바디(@p body)의 알림 하나입니다. 트리거 쪽이 A 가 아니게(큰 id) 둬 정렬도 지나게 한다. */
        static PhysicsOverlapEvent makeEvent( uint64 object, uint32 body, uint32 triggerBody, PhysicsContactPhase phase, bool bOtherTrigger = false )
        {
            PhysicsOverlapEvent event;
            event._objectA   = object;
            event._objectB   = kTrigger;
            event._bodyA     = makeBody( body );
            event._bodyB     = makeBody( triggerBody );
            event._phase     = phase;
            event._bTriggerA = bOtherTrigger;
            event._bTriggerB = true;
            return event;
        }

        static uint32 count( const vector<PhysicsOverlapEvent>& list, PhysicsContactPhase phase )
        {
            uint32 result = 0;
            for ( const PhysicsOverlapEvent& event : list )
            {
                result += event._phase == phase ? 1u : 0u;
            }
            return result;
        }
    };
} // namespace

/** @brief [PhysicsOverlapTrackerTest] 몸이 셋인 오브젝트(래그돌)가 트리거에 들면 진입 한 번, 둘이 나가도 이탈 없음, 마지막이 나갈 때 이탈 한 번 */
SW_TEST_CASE( PhysicsOverlapTrackerTest, ManyBodiesEnterAndExitOnce )
{
    using Internal = TestPhysicsOverlapTrackerInternal;
    PhysicsOverlapTracker       tracker;
    vector<PhysicsOverlapEvent> listTransition;
    vector<PhysicsOverlapEvent> listStay;
    for ( uint32 body = 1; body <= 3; ++body )
    {
        tracker.record( Internal::makeEvent( Internal::kHero, body, 100, PhysicsContactPhase::Begin ) );
    }
    tracker.finishFrame( listTransition, listStay, false );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listTransition.size() ) );
    SW_EXPECT_TRUE( listTransition[0]._phase == PhysicsContactPhase::Begin );
    SW_EXPECT_EQUAL( Internal::kTrigger, listTransition[0]._objectA ); // 작은 id 가 A 다
    SW_EXPECT_EQUAL( 3u, tracker.getBodyPairCount( Internal::kHero, Internal::kTrigger ) );

    tracker.record( Internal::makeEvent( Internal::kHero, 1, 100, PhysicsContactPhase::End ) );
    tracker.record( Internal::makeEvent( Internal::kHero, 2, 100, PhysicsContactPhase::End ) );
    tracker.finishFrame( listTransition, listStay, false );
    SW_EXPECT_TRUE( listTransition.empty() );
    tracker.record( Internal::makeEvent( Internal::kHero, 3, 100, PhysicsContactPhase::End ) );
    tracker.finishFrame( listTransition, listStay, false );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listTransition.size() ) );
    SW_EXPECT_TRUE( listTransition[0]._phase == PhysicsContactPhase::End );
    SW_EXPECT_EQUAL( 0u, tracker.getOverlapCount( Internal::kHero ) );
}

/** @brief [PhysicsOverlapTrackerTest] 같은 프레임에 한 대상이 나가고 다른 대상이 들면 이탈이 먼저다 — 알림이 진입 먼저 와도 */
SW_TEST_CASE( PhysicsOverlapTrackerTest, ExitComesBeforeEnterInOneFrame )
{
    using Internal = TestPhysicsOverlapTrackerInternal;
    PhysicsOverlapTracker       tracker;
    vector<PhysicsOverlapEvent> listTransition;
    vector<PhysicsOverlapEvent> listStay;
    tracker.record( Internal::makeEvent( Internal::kHero, 1, 100, PhysicsContactPhase::Begin ) );
    tracker.finishFrame( listTransition, listStay, false );

    tracker.record( Internal::makeEvent( Internal::kCrate, 2, 100, PhysicsContactPhase::Begin ) );
    tracker.record( Internal::makeEvent( Internal::kHero, 1, 100, PhysicsContactPhase::End ) );
    tracker.finishFrame( listTransition, listStay, false );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( listTransition.size() ) );
    SW_EXPECT_TRUE( listTransition[0]._phase == PhysicsContactPhase::End );
    SW_EXPECT_EQUAL( Internal::kHero, listTransition[0]._objectB );
    SW_EXPECT_TRUE( listTransition[1]._phase == PhysicsContactPhase::Begin );
    SW_EXPECT_EQUAL( Internal::kCrate, listTransition[1]._objectB );
}

/** @brief [PhysicsOverlapTrackerTest] 한 프레임에 들었다 난 것(큰 dt · 빠른 공)은 진입과 이탈을 둘 다 낸다. 안에 있다 나갔다 다시 든 것(바디 다시 짓기 · 트리거 껐다 켬)은 아무것도 내지 않는다 */
SW_TEST_CASE( PhysicsOverlapTrackerTest, PassThroughAndRebuildInOneFrame )
{
    using Internal = TestPhysicsOverlapTrackerInternal;
    PhysicsOverlapTracker       tracker;
    vector<PhysicsOverlapEvent> listTransition;
    vector<PhysicsOverlapEvent> listStay;
    tracker.record( Internal::makeEvent( Internal::kHero, 1, 100, PhysicsContactPhase::Begin ) );
    tracker.record( Internal::makeEvent( Internal::kHero, 1, 100, PhysicsContactPhase::End ) );
    tracker.finishFrame( listTransition, listStay, true );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( listTransition.size() ) );
    SW_EXPECT_TRUE( listTransition[0]._phase == PhysicsContactPhase::Begin );
    SW_EXPECT_TRUE( listTransition[1]._phase == PhysicsContactPhase::End );
    SW_EXPECT_TRUE( listStay.empty() ); // 안에 남지 않았다
    SW_EXPECT_EQUAL( 0u, tracker.getOverlapCount( Internal::kHero ) );

    tracker.record( Internal::makeEvent( Internal::kHero, 1, 100, PhysicsContactPhase::Begin ) );
    tracker.finishFrame( listTransition, listStay, false );
    tracker.record( Internal::makeEvent( Internal::kHero, 1, 100, PhysicsContactPhase::End ) );   // 옛 바디가 지워지고
    tracker.record( Internal::makeEvent( Internal::kHero, 7, 100, PhysicsContactPhase::Begin ) ); // 새 바디가 같은 자리에서 닿는다
    tracker.finishFrame( listTransition, listStay, false );
    SW_EXPECT_TRUE( listTransition.empty() );
    SW_EXPECT_EQUAL( 1u, tracker.getBodyPairCount( Internal::kHero, Internal::kTrigger ) );
}

/** @brief [PhysicsOverlapTrackerTest] 상대의 감지 범위(트리거)와 몸은 따로 센다 — 범위가 먼저 들어 있어도 몸이 들면 진입이 온다(받는 쪽이 `_bOtherTrigger` 로 가른다) */
SW_TEST_CASE( PhysicsOverlapTrackerTest, TriggerAndBodyAreCountedApart )
{
    using Internal = TestPhysicsOverlapTrackerInternal;
    PhysicsOverlapTracker       tracker;
    vector<PhysicsOverlapEvent> listTransition;
    vector<PhysicsOverlapEvent> listStay;
    tracker.record( Internal::makeEvent( Internal::kHero, 5, 100, PhysicsContactPhase::Begin, true ) ); // 범위
    tracker.finishFrame( listTransition, listStay, false );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listTransition.size() ) );
    SW_EXPECT_TRUE( listTransition[0]._bTriggerB ); // 대상 쪽(B — 큰 id) 이 트리거다

    tracker.record( Internal::makeEvent( Internal::kHero, 1, 100, PhysicsContactPhase::Begin ) ); // 몸
    tracker.finishFrame( listTransition, listStay, false );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listTransition.size() ) );
    SW_EXPECT_FALSE( listTransition[0]._bTriggerB );
    SW_EXPECT_EQUAL( 2u, tracker.getOverlapCount( Internal::kTrigger ) ); // 범위 쌍과 몸 쌍
    SW_EXPECT_EQUAL( 2u, tracker.getBodyPairCount( Internal::kHero, Internal::kTrigger ) );
}

/** @brief [PhysicsOverlapTrackerTest] 유지는 안에 있는 오브젝트 쌍마다 프레임 한 번(진입 프레임 포함, 몸이 여럿이어도 한 번), 듣는 쪽이 없으면 만들지 않는다. 모르는 쌍의 끝 · 유지 알림은 무시한다 */
SW_TEST_CASE( PhysicsOverlapTrackerTest, StayOncePerPairPerFrame )
{
    using Internal = TestPhysicsOverlapTrackerInternal;
    PhysicsOverlapTracker       tracker;
    vector<PhysicsOverlapEvent> listTransition;
    vector<PhysicsOverlapEvent> listStay;
    tracker.record( Internal::makeEvent( Internal::kCrate, 9, 100, PhysicsContactPhase::End ) );  // 모르는 쌍
    tracker.record( Internal::makeEvent( Internal::kCrate, 9, 100, PhysicsContactPhase::Stay ) ); // 유지 알림은 세지 않는다
    tracker.finishFrame( listTransition, listStay, true );
    SW_EXPECT_TRUE( listTransition.empty() );
    SW_EXPECT_TRUE( listStay.empty() );

    tracker.record( Internal::makeEvent( Internal::kHero, 1, 100, PhysicsContactPhase::Begin ) );
    tracker.record( Internal::makeEvent( Internal::kHero, 2, 100, PhysicsContactPhase::Begin ) );
    tracker.record( Internal::makeEvent( Internal::kCrate, 3, 100, PhysicsContactPhase::Begin ) );
    tracker.finishFrame( listTransition, listStay, true );
    SW_EXPECT_EQUAL( 2u, Internal::count( listTransition, PhysicsContactPhase::Begin ) );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( listStay.size() ) );
    SW_EXPECT_EQUAL( Internal::kTrigger, listStay[0]._objectA ); // 열쇠 순서 — (10, 20) 다음 (10, 30)
    SW_EXPECT_EQUAL( Internal::kHero, listStay[0]._objectB );
    SW_EXPECT_EQUAL( Internal::kCrate, listStay[1]._objectB );

    tracker.finishFrame( listTransition, listStay, false );
    SW_EXPECT_TRUE( listStay.empty() );
    tracker.finishFrame( listTransition, listStay, true ); // 알림 없는 프레임에도 안에 있으면 유지
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listStay.size() ) );
    tracker.clear();
    tracker.finishFrame( listTransition, listStay, true );
    SW_EXPECT_TRUE( listTransition.empty() && listStay.empty() );
}
