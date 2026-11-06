// ScreenTransitionTest — 화면 페이드와 전환 차례: 어둡기 곡선, 액션은 까만 화면에서 한 번, 액션 안의 새 전환, 초기화(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/ScreenTransitionManager.h"

using namespace sw;

namespace
{
    constexpr float32 kTolerance = 1.0e-4f;

    struct TransitionProbe
    {
        ScreenTransitionManager* _pManager{ nullptr };
        int32                    _actionCount{ 0 };
        int32                    _chainedCount{ 0 };
        float32                  _alphaAtAction{ -1.0f };

        void onAction()
        {
            ++_actionCount;
            _alphaAtAction = _pManager->fade().getOverlayAlpha();
        }

        void onChainedAction() { ++_chainedCount; }

        void onActionStartingAnother()
        {
            ++_actionCount;
            _pManager->beginTransition( Delegate<void()>::create<&TransitionProbe::onChainedAction>( this ), 1.0f, 1.0f );
        }
    };
} // namespace

/** @brief [ScreenTransitionTest] 페이드: 어두워지면 검게 유지하고, 밝아지면 쉰다. 길이 0 은 다음 갱신에 끝난다 */
SW_TEST_CASE( ScreenTransitionTest, FadeCurve )
{
    ScreenFade fade;
    SW_EXPECT_FALSE( fade.isBusy() );
    fade.beginFadeOut( 1.0f );
    fade.update( 0.25f );
    SW_EXPECT_NEAR( 0.25f, fade.getOverlayAlpha(), kTolerance );
    SW_EXPECT_FALSE( fade.isFinished() );
    fade.update( 1.0f );
    SW_EXPECT_TRUE( fade.getPhase() == FadePhase::HoldBlack );
    SW_EXPECT_NEAR( 1.0f, fade.getOverlayAlpha(), kTolerance );
    SW_EXPECT_TRUE( fade.isFinished() );

    fade.update( 10.0f ); // 검은 채로 있다
    SW_EXPECT_NEAR( 1.0f, fade.getOverlayAlpha(), kTolerance );

    fade.beginFadeIn( 1.0f );
    fade.update( 0.25f );
    SW_EXPECT_NEAR( 0.75f, fade.getOverlayAlpha(), kTolerance );
    fade.update( 1.0f );
    SW_EXPECT_TRUE( fade.getPhase() == FadePhase::Idle );
    SW_EXPECT_NEAR( 0.0f, fade.getOverlayAlpha(), kTolerance );

    fade.beginFadeOut( 0.0f );
    fade.update( 0.01f );
    SW_EXPECT_TRUE( fade.getPhase() == FadePhase::HoldBlack );
}

/** @brief [ScreenTransitionTest] 어두워지는 도중 · 밝아지는 도중에 다시 어둡게 하면 지금 어둡기에서 잇는다(번쩍이지 않는다) */
SW_TEST_CASE( ScreenTransitionTest, FadeOutContinuesFromCurrentAlpha )
{
    ScreenFade fade;
    fade.beginFadeOut( 1.0f );
    fade.update( 2.0f );
    fade.beginFadeIn( 1.0f );
    fade.update( 0.5f );
    SW_REQUIRE_TRUE( MathUtil::abs( fade.getOverlayAlpha() - 0.5f ) < kTolerance );

    fade.beginFadeOut( 1.0f );
    SW_EXPECT_NEAR( 0.5f, fade.getOverlayAlpha(), kTolerance );
    fade.update( 0.25f );
    SW_EXPECT_NEAR( 0.75f, fade.getOverlayAlpha(), kTolerance );
}

/** @brief [ScreenTransitionTest] 전환: 까매진 뒤 액션을 한 번 부르고 밝아진 뒤 끝난다 */
SW_TEST_CASE( ScreenTransitionTest, ActionRunsOnceAtBlack )
{
    ScreenTransitionManager manager;
    TransitionProbe         probe;
    probe._pManager = &manager;
    SW_EXPECT_FALSE( manager.isBusy() );

    manager.beginTransition( Delegate<void()>::create<&TransitionProbe::onAction>( &probe ), 1.0f, 1.0f );
    SW_EXPECT_TRUE( manager.isBusy() );
    SW_EXPECT_TRUE( manager.getPhase() == ScreenTransitionManager::Phase::FadeOut );
    manager.update( 0.5f );
    SW_EXPECT_EQUAL( 0, probe._actionCount );

    manager.update( 0.5f );
    SW_EXPECT_EQUAL( 1, probe._actionCount );
    SW_EXPECT_NEAR( 1.0f, probe._alphaAtAction, kTolerance );
    SW_EXPECT_TRUE( manager.getPhase() == ScreenTransitionManager::Phase::FadeIn );

    manager.update( 0.5f );
    SW_EXPECT_TRUE( manager.isBusy() );
    manager.update( 0.5f );
    manager.update( 0.01f );
    SW_EXPECT_EQUAL( 1, probe._actionCount );
    SW_EXPECT_FALSE( manager.isBusy() );
    SW_EXPECT_TRUE( manager.getPhase() == ScreenTransitionManager::Phase::None );
}

/** @brief [ScreenTransitionTest] 액션 안에서 새 전환을 걸면 까만 채로 이어져 그 액션도 불린다(앞 전환의 밝아지기가 덮지 않는다) */
SW_TEST_CASE( ScreenTransitionTest, ActionMayStartAnotherTransition )
{
    ScreenTransitionManager manager;
    TransitionProbe         probe;
    probe._pManager = &manager;
    manager.beginTransition( Delegate<void()>::create<&TransitionProbe::onActionStartingAnother>( &probe ), 1.0f, 1.0f );
    manager.update( 1.0f );
    SW_EXPECT_EQUAL( 1, probe._actionCount );
    SW_EXPECT_TRUE( manager.getPhase() == ScreenTransitionManager::Phase::FadeOut );
    SW_EXPECT_NEAR( 1.0f, manager.fade().getOverlayAlpha(), kTolerance ); // 번쩍이지 않는다

    manager.update( 0.01f );
    SW_EXPECT_EQUAL( 1, probe._chainedCount );
    SW_EXPECT_TRUE( manager.getPhase() == ScreenTransitionManager::Phase::FadeIn );
}

/** @brief [ScreenTransitionTest] 초기화하면 액션을 부르지 않고 어둡기가 사라진다 */
SW_TEST_CASE( ScreenTransitionTest, ResetDropsPendingAction )
{
    ScreenTransitionManager manager;
    TransitionProbe         probe;
    probe._pManager = &manager;
    manager.beginTransition( Delegate<void()>::create<&TransitionProbe::onAction>( &probe ), 1.0f, 1.0f );
    manager.update( 0.5f );
    manager.reset();
    SW_EXPECT_FALSE( manager.isBusy() );
    manager.update( 5.0f );
    SW_EXPECT_EQUAL( 0, probe._actionCount );
    SW_EXPECT_NEAR( 0.0f, manager.fade().getOverlayAlpha(), kTolerance );
}
