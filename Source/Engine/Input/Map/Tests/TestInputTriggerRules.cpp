#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Input/Map/InputTriggerRules.h"

using namespace sw;

namespace
{
    struct InputTriggerTestUtil
    {
        static constexpr float32 kStep = 0.01f;

        static ActionBinding makeBinding( ActionTrigger trigger, float32 triggerTime = 0.0f )
        {
            ActionBinding binding{};
            binding._trigger     = trigger;
            binding._triggerTime = triggerTime;
            return binding;
        }

        static ActionBinding makeMouseBinding( ActionTrigger trigger )
        {
            ActionBinding binding           = makeBinding( trigger );
            binding._arrSlot[0]._deviceKind = InputDeviceKind::Mouse;
            return binding;
        }

        /** @brief 보이는 바인딩에 한 프레임을 흘리고 발화 여부를 돌려줍니다. */
        static bool step( const InputTriggerRules& rules, const ActionBinding& binding, InputBindingState& state, bool bDown, float32 deltaSeconds = kStep, int2 mousePos = int2{} )
        {
            InputBindingSample sample{};
            sample._mousePos      = mousePos;
            sample._bDown         = bDown;
            sample._bPhysicalDown = bDown;
            sample._bLive         = true;
            rules.advance( binding, state, deltaSeconds, sample );
            const bool bFired = state._bTriggered == SW_TRUE;
            InputTriggerRules::endFrame( state );
            return bFired;
        }
    };
    using Util = InputTriggerTestUtil;
} // namespace

/** @brief [InputTriggerRulesTest] Pressed · Released · Down 은 각자의 프레임에 발화한다 */
SW_TEST_CASE( InputTriggerRulesTest, EdgesAndLevel )
{
    const InputTriggerRules rules{};
    InputBindingState       pressed{};
    SW_EXPECT_TRUE( Util::step( rules, Util::makeBinding( ActionTrigger::Pressed ), pressed, true ) );
    SW_EXPECT_FALSE( Util::step( rules, Util::makeBinding( ActionTrigger::Pressed ), pressed, true ) );

    InputBindingState released{};
    (void)Util::step( rules, Util::makeBinding( ActionTrigger::Released ), released, true );
    SW_EXPECT_TRUE( Util::step( rules, Util::makeBinding( ActionTrigger::Released ), released, false ) );

    InputBindingState down{};
    SW_EXPECT_TRUE( Util::step( rules, Util::makeBinding( ActionTrigger::Down ), down, true ) );
    SW_EXPECT_TRUE( Util::step( rules, Util::makeBinding( ActionTrigger::Down ), down, true ) );
    SW_EXPECT_FALSE( Util::step( rules, Util::makeBinding( ActionTrigger::Down ), down, false ) );
}

/** @brief [InputTriggerRulesTest] 눌린 채 보이게 된 입력은 새 누름이 아니다 — 메뉴를 연 키가 메뉴 안 동작을 발화시키지 않는다 */
SW_TEST_CASE( InputTriggerRulesTest, HeldWhileBecomingLiveIsNotAPress )
{
    const InputTriggerRules rules{};
    const ActionBinding     binding = Util::makeBinding( ActionTrigger::Pressed );
    InputBindingState       state{};
    InputBindingSample      hidden{};
    hidden._bPhysicalDown = true; // 장치는 눌렸지만 레이어가 꺼져 게임에는 안 보인다
    rules.advance( binding, state, Util::kStep, hidden );
    SW_EXPECT_FALSE( state._bTriggered == SW_TRUE );

    SW_EXPECT_FALSE( Util::step( rules, binding, state, true ) );
    (void)Util::step( rules, binding, state, false );
    SW_EXPECT_TRUE( Util::step( rules, binding, state, true ) ); // 보인 뒤 뗐다 다시 누르면 새 누름이다
}

/** @brief [InputTriggerRulesTest] 홀드는 문턱을 넘은 뒤 발화하고, 바인딩 시간이 맵 값보다 먼저다 */
SW_TEST_CASE( InputTriggerRulesTest, HoldThresholdUsesBindingTimeFirst )
{
    const InputTriggerRules rules{};
    const ActionBinding     binding = Util::makeBinding( ActionTrigger::HoldThreshold, 0.5f );
    SW_EXPECT_NEAR( 0.5f, rules.getEffectiveTriggerTime( binding ), 0.0001f );
    SW_EXPECT_NEAR( InputMapDefaults::kHoldThreshold, rules.getEffectiveTriggerTime( Util::makeBinding( ActionTrigger::HoldThreshold ) ), 0.0001f );

    InputBindingState state{};
    SW_EXPECT_FALSE( Util::step( rules, binding, state, true, 0.4f ) );
    SW_EXPECT_TRUE( Util::step( rules, binding, state, true, 0.2f ) );
}

/** @brief [InputTriggerRulesTest] Tap 과 HoldAndRelease 는 뗀 순간 누른 시간으로 갈린다 */
SW_TEST_CASE( InputTriggerRulesTest, TapAndHoldAndReleaseSplitByDuration )
{
    const InputTriggerRules rules{};
    const ActionBinding     tap     = Util::makeBinding( ActionTrigger::Tap, 0.2f );
    const ActionBinding     longKey = Util::makeBinding( ActionTrigger::HoldAndRelease, 0.2f );

    InputBindingState shortTap{};
    (void)Util::step( rules, tap, shortTap, true, 0.1f );
    SW_EXPECT_TRUE( Util::step( rules, tap, shortTap, false ) );
    InputBindingState longTap{};
    (void)Util::step( rules, tap, longTap, true, 0.3f );
    SW_EXPECT_FALSE( Util::step( rules, tap, longTap, false ) );

    InputBindingState shortHold{};
    (void)Util::step( rules, longKey, shortHold, true, 0.1f );
    SW_EXPECT_FALSE( Util::step( rules, longKey, shortHold, false ) );
    InputBindingState longHold{};
    (void)Util::step( rules, longKey, longHold, true, 0.3f );
    SW_EXPECT_TRUE( Util::step( rules, longKey, longHold, false ) );
}

/** @brief [InputTriggerRulesTest] Pulse 는 간격마다 한 번이다 — 간격을 넘긴 뒤 매 프레임 쏘지 않고, 긴 프레임에도 한 번이다 */
SW_TEST_CASE( InputTriggerRulesTest, PulseFiresOncePerInterval )
{
    const InputTriggerRules rules{};
    const ActionBinding     binding = Util::makeBinding( ActionTrigger::Pulse, 0.1f );
    InputBindingState       state{};
    uint32                  fired{ 0 };
    for ( uint32 frame = 0; frame < 25; ++frame ) // 0.25 초면 두 번
    {
        fired += Util::step( rules, binding, state, true ) ? 1u : 0u;
    }
    SW_EXPECT_EQUAL( 2u, fired );

    InputBindingState slow{};
    SW_EXPECT_TRUE( Util::step( rules, binding, slow, true, 0.35f ) );
    SW_EXPECT_FALSE( Util::step( rules, binding, slow, true, 0.01f ) );
    (void)Util::step( rules, binding, slow, false );
    SW_EXPECT_NEAR( 0.0f, slow._pulseTimer, 0.0001f ); // 뗀 뒤에는 처음부터 다시 잰다
}

/** @brief [InputTriggerRulesTest] Repeat 는 누를 때 한 번, 지연 뒤 간격마다 한 번이다 */
SW_TEST_CASE( InputTriggerRulesTest, RepeatFiresOnPressThenAfterDelay )
{
    InputTriggerRules rules{};
    rules._navRepeatDelay       = 0.2f;
    rules._navRepeatRate        = 0.1f;
    const ActionBinding binding = Util::makeBinding( ActionTrigger::Repeat );
    InputBindingState   state{};
    SW_EXPECT_TRUE( Util::step( rules, binding, state, true, 0.05f ) );  // 누름
    SW_EXPECT_FALSE( Util::step( rules, binding, state, true, 0.05f ) ); // 0.10
    SW_EXPECT_FALSE( Util::step( rules, binding, state, true, 0.05f ) ); // 0.15
    SW_EXPECT_TRUE( Util::step( rules, binding, state, true, 0.06f ) );  // 0.21: 지연을 넘은 첫 프레임
    SW_EXPECT_FALSE( Util::step( rules, binding, state, true, 0.05f ) ); // 0.26
    SW_EXPECT_TRUE( Util::step( rules, binding, state, true, 0.05f ) );  // 0.31: 간격 하나
}

/** @brief [InputTriggerRulesTest] 연타는 간격 안의 누름만 센다 — 간격을 넘기면 처음부터다 */
SW_TEST_CASE( InputTriggerRulesTest, DoubleTapCountsPressesInsideTheWindow )
{
    const InputTriggerRules rules{};
    const ActionBinding     binding = Util::makeBinding( ActionTrigger::DoubleTap, 0.3f );
    InputBindingState       quick{};
    SW_EXPECT_FALSE( Util::step( rules, binding, quick, true ) );
    (void)Util::step( rules, binding, quick, false, 0.1f );
    SW_EXPECT_TRUE( Util::step( rules, binding, quick, true ) );

    InputBindingState slow{};
    SW_EXPECT_FALSE( Util::step( rules, binding, slow, true ) );
    (void)Util::step( rules, binding, slow, false, 0.5f );
    SW_EXPECT_FALSE( Util::step( rules, binding, slow, true ) );
}

/** @brief [InputTriggerRulesTest] MultiTap 은 정한 횟수에 쏘고 다시 센다. 2 미만은 2 로 읽는다 */
SW_TEST_CASE( InputTriggerRulesTest, MultiTapFiresAtItsCountAndRestarts )
{
    const InputTriggerRules rules{};
    ActionBinding           binding = Util::makeBinding( ActionTrigger::MultiTap, 0.3f );
    binding._tapCount               = 1;
    SW_EXPECT_EQUAL( 2u, InputTriggerRules::getRequiredTapCount( binding ) );
    binding._tapCount = 3;
    SW_EXPECT_EQUAL( 3u, InputTriggerRules::getRequiredTapCount( binding ) );

    InputBindingState state{};
    uint32            fired{ 0 };
    for ( uint32 press = 0; press < 6; ++press )
    {
        fired += Util::step( rules, binding, state, true ) ? 1u : 0u;
        (void)Util::step( rules, binding, state, false );
    }
    SW_EXPECT_EQUAL( 2u, fired );
}

/** @brief [InputTriggerRulesTest] 마우스 연타는 위치도 본다 — 멀리 떨어진 두 번은 더블클릭이 아니다. 키는 위치를 보지 않는다 */
SW_TEST_CASE( InputTriggerRulesTest, MouseDoubleClickNeedsNearbyPresses )
{
    const InputTriggerRules rules{};
    const ActionBinding     mouse = Util::makeMouseBinding( ActionTrigger::DoubleClicked );
    InputBindingState       distant{};
    (void)Util::step( rules, mouse, distant, true, Util::kStep, int2{ 0, 0 } );
    (void)Util::step( rules, mouse, distant, false, Util::kStep, int2{ 0, 0 } );
    SW_EXPECT_FALSE( Util::step( rules, mouse, distant, true, Util::kStep, int2{ 100, 0 } ) );

    InputBindingState nearby{};
    (void)Util::step( rules, mouse, nearby, true, Util::kStep, int2{ 0, 0 } );
    (void)Util::step( rules, mouse, nearby, false, Util::kStep, int2{ 0, 0 } );
    SW_EXPECT_TRUE( Util::step( rules, mouse, nearby, true, Util::kStep, int2{ 2, 0 } ) );

    const ActionBinding key = Util::makeBinding( ActionTrigger::DoubleClicked );
    InputBindingState   keyState{};
    (void)Util::step( rules, key, keyState, true, Util::kStep, int2{ 0, 0 } );
    (void)Util::step( rules, key, keyState, false, Util::kStep, int2{ 0, 0 } );
    SW_EXPECT_TRUE( Util::step( rules, key, keyState, true, Util::kStep, int2{ 100, 0 } ) );
}

/** @brief [InputTriggerRulesTest] 끌기 문턱을 넘은 누름은 Click 이 아니고, Drag 는 처음 넘은 프레임에 한 번이다 */
SW_TEST_CASE( InputTriggerRulesTest, DragAndClickSplitByThreshold )
{
    const InputTriggerRules rules{};
    const ActionBinding     drag  = Util::makeMouseBinding( ActionTrigger::Drag );
    const ActionBinding     click = Util::makeMouseBinding( ActionTrigger::Click );

    InputBindingState dragState{};
    SW_EXPECT_FALSE( Util::step( rules, drag, dragState, true, Util::kStep, int2{ 0, 0 } ) );
    SW_EXPECT_FALSE( Util::step( rules, drag, dragState, true, Util::kStep, int2{ 3, 0 } ) );
    SW_EXPECT_TRUE( Util::step( rules, drag, dragState, true, Util::kStep, int2{ 30, 0 } ) );
    SW_EXPECT_FALSE( Util::step( rules, drag, dragState, true, Util::kStep, int2{ 60, 0 } ) );

    InputBindingState stayed{};
    (void)Util::step( rules, click, stayed, true, Util::kStep, int2{ 0, 0 } );
    SW_EXPECT_TRUE( Util::step( rules, click, stayed, false, Util::kStep, int2{ 3, 0 } ) );

    InputBindingState moved{};
    (void)Util::step( rules, click, moved, true, Util::kStep, int2{ 0, 0 } );
    (void)Util::step( rules, click, moved, true, Util::kStep, int2{ 30, 0 } );
    SW_EXPECT_FALSE( Util::step( rules, click, moved, false, Util::kStep, int2{ 30, 0 } ) );

    // 끌기 표시는 그 누름으로 끝난다 — 다음 클릭은 클릭이다
    (void)Util::step( rules, click, moved, true, Util::kStep, int2{ 30, 0 } );
    SW_EXPECT_TRUE( Util::step( rules, click, moved, false, Util::kStep, int2{ 30, 0 } ) );
}

/** @brief [InputTriggerRulesTest] 키 · 패드 슬롯은 끌 수 없다 — 포인터가 움직여도 Click 이고 Drag 는 쏘지 않는다 */
SW_TEST_CASE( InputTriggerRulesTest, KeySlotsCannotDrag )
{
    const InputTriggerRules rules{};
    const ActionBinding     click = Util::makeBinding( ActionTrigger::Click );
    InputBindingState       state{};
    (void)Util::step( rules, click, state, true, Util::kStep, int2{ 0, 0 } );
    (void)Util::step( rules, click, state, true, Util::kStep, int2{ 300, 0 } );
    SW_EXPECT_TRUE( Util::step( rules, click, state, false, Util::kStep, int2{ 300, 0 } ) );

    const ActionBinding drag = Util::makeBinding( ActionTrigger::Drag );
    InputBindingState   dragState{};
    (void)Util::step( rules, drag, dragState, true, Util::kStep, int2{ 0, 0 } );
    SW_EXPECT_FALSE( Util::step( rules, drag, dragState, true, Util::kStep, int2{ 300, 0 } ) );
}

/** @brief [InputTriggerRulesTest] 시간 칸이 없는 트리거의 유효 시간은 0 이다 */
SW_TEST_CASE( InputTriggerRulesTest, TriggersWithoutTimeReportZero )
{
    const InputTriggerRules rules{};
    SW_EXPECT_NEAR( 0.0f, rules.getEffectiveTriggerTime( Util::makeBinding( ActionTrigger::Pressed, 1.0f ) ), 0.0001f );
    SW_EXPECT_NEAR( 0.0f, rules.getEffectiveTriggerTime( Util::makeBinding( ActionTrigger::Click, 1.0f ) ), 0.0001f );
    SW_EXPECT_NEAR( InputMapDefaults::kTapMaxTime, rules.getEffectiveTriggerTime( Util::makeBinding( ActionTrigger::Tap ) ), 0.0001f );
    SW_EXPECT_NEAR( InputMapDefaults::kPulseInterval, rules.getEffectiveTriggerTime( Util::makeBinding( ActionTrigger::Pulse ) ), 0.0001f );
}
