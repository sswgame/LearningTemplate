#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Event/EventDispatcher.h"

#include "EngineTest/GameTestUtil.h"

#include "GameFramework/Base/GameEvents.h"
#include "GameFramework/Base/GameService.h"
#include "GameFramework/Transition/GameModeStateMachine.h"

#include "TestFramework/TestFramework.h"

using sw::test::ScopedLocalServiceBinding;

/**
 * @brief [GameModeStateMachineTest] 상태 전환, 핸들러 호출 및 델리게이트 알림 검증
 */
SW_TEST_CASE( GameModeStateMachineTest, Lifecycle )
{
    class TestModeHandler : public sw::IGameModeHandler
    {
    public:
        uint32            _enterCount{ 0 };
        uint32            _updateCount{ 0 };
        uint32            _exitCount{ 0 };
        sw::hashed_string _lastPreviousMode{};
        sw::hashed_string _lastNextMode{};

        void onEnter( const sw::hashed_string& previousMode ) override
        {
            ++_enterCount;
            _lastPreviousMode = previousMode;
        }

        void onUpdate( float32 deltaTime ) override
        {
            (void)deltaTime;
            ++_updateCount;
        }

        void onExit( const sw::hashed_string& nextMode ) override
        {
            ++_exitCount;
            _lastNextMode = nextMode;
        }
    };

    sw::GameModeStateMachine fsm;
    auto                     titleHandler    = sw::make_shared<TestModeHandler>();
    auto                     gameplayHandler = sw::make_shared<TestModeHandler>();

    fsm.registerHandler( sw::GameModes::title(), titleHandler );
    fsm.registerHandler( sw::GameModes::gameplay(), gameplayHandler );

    sw::hashed_string notifiedOld{};
    sw::hashed_string notifiedNew{};
    uint32            notifyCount{ 0 };

    fsm.setOnModeChanged(
        SW_DELEGATE_LAMBDA( sw::GameModeStateMachine::ModeChangedDelegate, [&]( const sw::hashed_string& oldMode, const sw::hashed_string& newMode )
    {
        notifiedOld = oldMode;
        notifiedNew = newMode;
        ++notifyCount;
    } ) );

    // 1) Title 모드로 전이
    SW_EXPECT_TRUE( fsm.transitionTo( sw::GameModes::title() ) );
    SW_EXPECT_TRUE( fsm.getCurrentMode() == sw::GameModes::title() );
    SW_EXPECT_EQUAL( uint32( 1 ), titleHandler->_enterCount );
    SW_EXPECT_EQUAL( uint32( 1 ), notifyCount );
    SW_EXPECT_TRUE( notifiedNew == sw::GameModes::title() );

    // 2) Update 호출
    fsm.update( 0.016f );
    fsm.update( 0.016f );
    SW_EXPECT_EQUAL( uint32( 2 ), titleHandler->_updateCount );

    // 3) Gameplay 모드로 전이
    SW_EXPECT_TRUE( fsm.transitionTo( sw::GameModes::gameplay() ) );
    SW_EXPECT_TRUE( fsm.getCurrentMode() == sw::GameModes::gameplay() );
    SW_EXPECT_TRUE( fsm.getPreviousMode() == sw::GameModes::title() );
    SW_EXPECT_EQUAL( uint32( 1 ), titleHandler->_exitCount );
    SW_EXPECT_TRUE( titleHandler->_lastNextMode == sw::GameModes::gameplay() );
    SW_EXPECT_EQUAL( uint32( 1 ), gameplayHandler->_enterCount );
    SW_EXPECT_TRUE( gameplayHandler->_lastPreviousMode == sw::GameModes::title() );
    SW_EXPECT_EQUAL( uint32( 2 ), notifyCount );

    // 4) Reset
    fsm.reset();
    SW_EXPECT_TRUE( fsm.getCurrentMode().empty() );
    SW_EXPECT_EQUAL( uint32( 1 ), gameplayHandler->_exitCount );
}

/**
 * @brief [GameModeStateMachineTest] 핸들러가 `onExit` 안에서 **자신을 해제**해도 살아남는다
 * @details 표가 `shared_ptr` 로 핸들러를 쥔다. 부르는 자리가 반복자나 생포인터를 쓰면
 *          핸들러가 `onExit` 안에서 자신을 해제할 때 (1) 들고 있던 반복자가 죽은 채로
 *          `erase( it )` 에 들어가고 (2) 표가 쥔 **마지막 참조**가 사라져 아직 실행 중인
 *          `onExit` 의 `this` 가 파괴된다. 모드를 떠나면서 자기 핸들러를 정리하는 것은
 *          이상한 일이 아니다.
 * @note 부르는 동안 참조를 잡지 않으면 ASAN 이 해제 후 사용으로 잡거나 그 자리에서 죽는다.
 */
SW_TEST_CASE( GameModeStateMachineTest, HandlerCanUnregisterItselfWhileExiting )
{
    class SelfRemovingHandler : public sw::IGameModeHandler
    {
    public:
        sw::GameModeStateMachine* _pFsm{ nullptr };
        sw::hashed_string         _mode{};
        uint32                    _exitCount{ 0 };
        uint32                    _valueReadAfterRemoving{ 0 };

        void onEnter( const sw::hashed_string& ) override {}
        void onUpdate( float32 ) override {}
        void onExit( const sw::hashed_string& ) override
        {
            ++_exitCount;
            if ( _pFsm != nullptr )
                _pFsm->unregisterHandler( _mode );
            // 표에서 떨어진 **뒤에** 자기 멤버를 읽는다 — 상태를 바로 파괴하면 여기서 이미 죽는다.
            _valueReadAfterRemoving = _exitCount;
        }
    };

    // 1) 모드를 옮기다가 나가는 핸들러가 자신을 뗀다.
    {
        sw::GameModeStateMachine fsm;
        auto                     handler = sw::make_shared<SelfRemovingHandler>();
        handler->_pFsm                   = &fsm;
        handler->_mode                   = sw::GameModes::title();
        fsm.registerHandler( sw::GameModes::title(), handler );

        SW_ASSERT_TRUE( fsm.transitionTo( sw::GameModes::title() ) );
        SW_EXPECT_TRUE( fsm.transitionTo( sw::GameModes::gameplay() ) );
        SW_EXPECT_EQUAL( uint32( 1 ), handler->_exitCount );
        SW_EXPECT_EQUAL( uint32( 1 ), handler->_valueReadAfterRemoving );
        SW_EXPECT_TRUE( fsm.getCurrentHandler() == nullptr );
    }

    // 2) 바깥에서 뗄 때도 마찬가지다 — 죽은 반복자로 `erase` 하면 안 된다.
    {
        sw::GameModeStateMachine fsm;
        auto                     handler = sw::make_shared<SelfRemovingHandler>();
        handler->_pFsm                   = &fsm;
        handler->_mode                   = sw::GameModes::title();
        fsm.registerHandler( sw::GameModes::title(), handler );

        SW_ASSERT_TRUE( fsm.transitionTo( sw::GameModes::title() ) );
        fsm.unregisterHandler( sw::GameModes::title() );
        SW_EXPECT_EQUAL( uint32( 1 ), handler->_exitCount );
        SW_EXPECT_EQUAL( uint32( 1 ), handler->_valueReadAfterRemoving );
        SW_EXPECT_TRUE( fsm.getCurrentHandler() == nullptr );
    }

    // 3) `reset()` 도 같은 규칙을 따른다.
    {
        sw::GameModeStateMachine fsm;
        auto                     handler = sw::make_shared<SelfRemovingHandler>();
        handler->_pFsm                   = &fsm;
        handler->_mode                   = sw::GameModes::title();
        fsm.registerHandler( sw::GameModes::title(), handler );

        SW_ASSERT_TRUE( fsm.transitionTo( sw::GameModes::title() ) );
        fsm.reset();
        SW_EXPECT_EQUAL( uint32( 1 ), handler->_exitCount );
        SW_EXPECT_EQUAL( uint32( 1 ), handler->_valueReadAfterRemoving );
        SW_EXPECT_TRUE( fsm.getCurrentMode().empty() );
    }
}

/**
 * @brief [GameModeStateMachineTest] 일시정지 모드에 들어가면 `GamePausedEvent`, 나가면(다른 모드 · 리셋 · 핸들러 해제) `GameResumedEvent` 가 "game" 채널로 온다
 * @details 일시정지가 아닌 모드끼리 옮기는 것은 아무것도 내지 않는다. 같은 모드로 다시 옮기는 것(이미 그 모드)도 내지 않는다.
 */
SW_TEST_CASE( GameModeStateMachineTest, PausedModePublishesPausedAndResumed )
{
    sw::EventDispatcher                                  dispatcher;
    const ScopedLocalServiceBinding<sw::EventDispatcher> scopedDispatcher{ dispatcher };
    uint32                                               pausedCount{ 0 };
    uint32                                               resumedCount{ 0 };
    dispatcher.subscribe<sw::GamePausedEvent>( sw::gameEventChannel(), SW_DELEGATE_LAMBDA( sw::Delegate<void( const sw::GamePausedEvent& )>, [&pausedCount]( const sw::GamePausedEvent& )
    { ++pausedCount; } ) );
    dispatcher.subscribe<sw::GameResumedEvent>( sw::gameEventChannel(), SW_DELEGATE_LAMBDA( sw::Delegate<void( const sw::GameResumedEvent& )>, [&resumedCount]( const sw::GameResumedEvent& )
    { ++resumedCount; } ) );

    class EmptyHandler : public sw::IGameModeHandler
    {
    public:
        void onEnter( const sw::hashed_string& ) override {}
        void onUpdate( float32 ) override {}
        void onExit( const sw::hashed_string& ) override {}
    };

    sw::GameModeStateMachine fsm;
    // 일시정지가 아닌 모드끼리는 아무것도 내지 않는다.
    SW_EXPECT_TRUE( fsm.transitionTo( sw::GameModes::title() ) );
    SW_EXPECT_TRUE( fsm.transitionTo( sw::GameModes::gameplay() ) );
    SW_EXPECT_EQUAL( 0u, pausedCount );
    SW_EXPECT_EQUAL( 0u, resumedCount );

    // 들어가고 · 같은 모드로 다시(아무것도 안 함) · 나간다.
    SW_EXPECT_TRUE( fsm.transitionTo( sw::GameModes::paused() ) );
    SW_EXPECT_EQUAL( 1u, pausedCount );
    SW_EXPECT_TRUE( fsm.transitionTo( sw::GameModes::paused() ) );
    SW_EXPECT_EQUAL( 1u, pausedCount );
    SW_EXPECT_TRUE( fsm.transitionTo( sw::GameModes::gameplay() ) );
    SW_EXPECT_EQUAL( 1u, resumedCount );

    // 리셋으로 나가도 재개다.
    SW_EXPECT_TRUE( fsm.transitionTo( sw::GameModes::paused() ) );
    fsm.reset();
    SW_EXPECT_EQUAL( 2u, pausedCount );
    SW_EXPECT_EQUAL( 2u, resumedCount );

    // 현재 모드(일시정지)의 핸들러를 떼어 모드가 비어도 재개다.
    fsm.registerHandler( sw::GameModes::paused(), sw::make_shared<EmptyHandler>() );
    SW_EXPECT_TRUE( fsm.transitionTo( sw::GameModes::paused() ) );
    fsm.unregisterHandler( sw::GameModes::paused() );
    SW_EXPECT_EQUAL( 3u, pausedCount );
    SW_EXPECT_EQUAL( 3u, resumedCount );
}
