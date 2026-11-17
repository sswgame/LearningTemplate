// AutomationReadinessTest — 붙잡는 조건의 세 값(준비됨 · 해당 없음 · 진행 중)과 정지 시계(진척이 그대로인 횟수 — `_stallFrames`, 벽시계 감시 값의 활동 수가 그대로인 초 — `_stallWallSeconds`).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Automation/Scenario/AutomationReadiness.h"
#include "Engine/Automation/Watch/AutomationWatch.h"

using namespace sw;

/** @brief [AutomationReadinessTest] 해당 없음은 진행 중이 아니다 — 막지 않는다(TAA 없는 Forward 뷰가 시한까지 기다리던 실패 1) */
SW_TEST_CASE( AutomationReadinessTest, NotApplicableDoesNotWait )
{
    SW_EXPECT_FALSE( AutomationReadiness::notApplicable().isWaiting() );
    SW_EXPECT_TRUE( AutomationReadiness::notApplicable().isNotApplicable() );
    SW_EXPECT_FALSE( AutomationReadiness::ready().isWaiting() );
    SW_EXPECT_FALSE( AutomationReadiness::ready().isNotApplicable() );
    const AutomationReadiness waiting = AutomationReadiness::waiting( 7, "the camera moves" );
    SW_EXPECT_TRUE( waiting.isWaiting() );
    SW_EXPECT_EQUAL( 7u, static_cast<uint32>( waiting.getProgress() ) );
    SW_EXPECT_EQUAL( string( "the camera moves" ), string( waiting.getWhat() ) );
}

/**
 * @brief [AutomationReadinessTest] 진척 없는 진행 중은 정지 시계가 매번 하나씩 늘고, 진척 · 조건이 바뀌거나 기다릴 것이 없어지면 0 부터다
 * @details 실행기는 이 수가 `_stallFrames` 에 닿으면 그 단계를 `WaitStalled` 로 끝낸다. 변이: 진척을 보지 않으면 둘째 단언이, 조건 이름을 보지 않으면 셋째 단언이 진다.
 */
SW_TEST_CASE( AutomationReadinessTest, StallClockCountsUnchangedProgress )
{
    static constexpr const utf8* kTemporal = "RenderTemporalAccumulation";
    static constexpr const utf8* kExposure = "RenderAutoExposure";
    AutomationStallClock         clock;
    SW_EXPECT_EQUAL( 0u, clock.advance( kTemporal, 0 ) );
    SW_EXPECT_EQUAL( 1u, clock.advance( kTemporal, 0 ) );
    SW_EXPECT_EQUAL( 2u, clock.advance( kTemporal, 0 ) ); // 카메라가 계속 움직인다 — 정지 프레임이 0 에 머문다
    SW_EXPECT_EQUAL( 0u, clock.advance( kTemporal, 1 ) ); // 나아갔다
    SW_EXPECT_EQUAL( 0u, clock.advance( kExposure, 1 ) ); // 다른 조건을 기다린다
    SW_EXPECT_EQUAL( 1u, clock.advance( kExposure, 1 ) );
    SW_EXPECT_EQUAL( 0u, clock.advance( nullptr, 0 ) ); // 기다릴 것이 없다
    SW_EXPECT_EQUAL( 0u, clock.getUnchangedCount() );
}

namespace
{
    uint64 g_testActivity = 0; ///< 아래 테스트의 바깥 일(읽은 바이트 같은 수)

    [[nodiscard]] uint64 readTestActivity( const GameObjectManager* /*pManager*/ )
    {
        return g_testActivity;
    }
} // namespace

/**
 * @brief [AutomationReadinessTest] 벽시계 감시 값의 활동 수가 그대로면 정지 초가 쌓여 정지 시한에 닿고, 수가 늘면 0 부터 다시 센다
 * @details 실행기(`AutomationRunner::holdWhileWallWatchActive`)는 이 초가 `_stallWallSeconds` 에 닿으면 `<WaitUntil timeoutWallSeconds>` 를 `WaitStalled` 로 끝낸다.
 *          변이: 활동 수를 보지 않으면 늘어난 뒤의 단언이, 감시 값 이름을 보지 않으면 다른 감시 값으로 바꾼 뒤의 단언이 진다.
 */
SW_TEST_CASE( AutomationReadinessTest, WallStallClockFollowsWatchActivity )
{
    static constexpr const utf8*          kPackaging        = "Editor.PackagingState";
    static constexpr const utf8*          kAssets           = "Assets.Pending";
    static constexpr float64              kStallWallSeconds = 60.0;
    const AutomationWatchRegistration     registration{ kPackaging, "test", nullptr, nullptr, false, false, true, nullptr, &readTestActivity };
    AutomationWallStallClock              clock;
    const AutomationWatchActivityFunction pActivity = registration._pActivityFunction;
    g_testActivity                                  = 10;
    SW_EXPECT_NEAR( 0.0, clock.advance( kPackaging, pActivity( nullptr ), 100.0 ), 1.0e-9 );
    SW_EXPECT_NEAR( 30.0, clock.advance( kPackaging, pActivity( nullptr ), 130.0 ), 1.0e-9 );
    SW_EXPECT_TRUE( clock.advance( kPackaging, pActivity( nullptr ), 160.0 ) >= kStallWallSeconds ); // 출력 줄이 60 초 동안 그대로 — 정지
    g_testActivity = 11;
    SW_EXPECT_NEAR( 0.0, clock.advance( kPackaging, pActivity( nullptr ), 161.0 ), 1.0e-9 ); // 나아갔다 — 정지 아님
    SW_EXPECT_TRUE( clock.advance( kPackaging, pActivity( nullptr ), 200.0 ) < kStallWallSeconds );
    SW_EXPECT_NEAR( 0.0, clock.advance( kAssets, pActivity( nullptr ), 210.0 ), 1.0e-9 ); // 다른 감시 값을 기다린다
    SW_EXPECT_NEAR( 0.0, clock.advance( nullptr, 0, 300.0 ), 1.0e-9 );                    // 기다릴 것이 없다
    SW_EXPECT_NEAR( 0.0, clock.advance( kAssets, pActivity( nullptr ), 400.0 ), 1.0e-9 ); // 되돌린 뒤 처음 본 것은 0 부터
}
