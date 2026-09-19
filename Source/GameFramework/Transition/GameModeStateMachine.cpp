#include "pch.h"

#include "GameFramework/Transition/GameModeStateMachine.h"

#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "GameModeStateMachine" );

    GameModeStateMachine::GameModeStateMachine()
        : _currentMode{}
        , _previousMode{}
        , _mapHandler{}
        , _onModeChanged{}
        , _bIsTransitioning{ false }
    {
    }

    shared_ptr<IGameModeHandler> GameModeStateMachine::findHandler( const hashed_string& mode ) const
    {
        const auto it = _mapHandler.find( mode );
        return it != _mapHandler.end() ? it->second : shared_ptr<IGameModeHandler>{};
    }

    void GameModeStateMachine::registerHandler( const hashed_string& mode, shared_ptr<IGameModeHandler> pHandler )
    {
        if ( pHandler == nullptr )
        {
            unregisterHandler( mode );
            return;
        }

        _mapHandler[mode] = std::move( pHandler );
    }

    void GameModeStateMachine::unregisterHandler( const hashed_string& mode )
    {
        // **먼저 표에서 뗀 다음에 알린다.** 예전에는 `onExit` 을 부르고 나서 그때까지 들고 있던
        // 반복자로 `erase` 했다. 핸들러가 `onExit` 안에서 무엇이든 등록·해제하면 그 반복자는
        // 이미 죽은 것이다. 그리고 `shared_ptr` 를 **지역 변수로 받아** 두어야 한다 — 표에서
        // 떼는 순간이 마지막 참조면 핸들러는 `onExit` 을 **실행하는 도중에** 파괴된다.
        shared_ptr<IGameModeHandler> pHandler = findHandler( mode );
        if ( pHandler == nullptr )
            return;

        _mapHandler.erase( mode );
        if ( _currentMode == mode )
        {
            _currentMode = {};
            pHandler->onExit( GameModes::none() );
        }
    }

    bool GameModeStateMachine::transitionTo( const hashed_string& newMode )
    {
        if ( _currentMode == newMode )
            return true;

        if ( _bIsTransitioning )
        {
            SW_LOG_WARNING( "Re-entrant mode transition to %# ignored while transitioning.", newMode.c_str() );
            return false;
        }

        _bIsTransitioning = true;
        struct TransitionGuard
        {
            bool& _flag;
            ~TransitionGuard() { _flag = false; }
        } guard{ _bIsTransitioning };

        const hashed_string oldMode = _currentMode;

        // 1) Exit previous mode handler
        // 핸들러를 부르기 전에 **`shared_ptr` 를 지역으로 받는다** — 핸들러가 그 안에서 자신을
        // 해제하면 표가 쥔 마지막 참조가 사라져, 아직 실행 중인 `onExit` 의 `this` 가 죽는다.
        // 그리고 **나가는 중에는 어느 모드에도 있지 않다.** 여기서 `_currentMode` 를 비우지
        // 않으면, `onExit` 안에서 `unregisterHandler( oldMode )` 를 부를 때 그쪽이 "아직 현재
        // 모드다" 로 보고 `onExit` 을 **한 번 더** 부른다. 나머지 두 나가는 길
        // (`unregisterHandler` · `reset`)은 이미 상태를 먼저 옮기고 알린다 — 여기만 달랐다.
        const shared_ptr<IGameModeHandler> pOldHandler = findHandler( oldMode );
        _previousMode                                  = oldMode;
        _currentMode                                   = {};
        if ( pOldHandler != nullptr )
            pOldHandler->onExit( newMode );

        _currentMode = newMode;

        // 2) Enter new mode handler
        if ( const shared_ptr<IGameModeHandler> pNewHandler = findHandler( newMode ) )
            pNewHandler->onEnter( oldMode );

        // 3) Broadcast notification
        if ( _onModeChanged.isBound() )
            _onModeChanged( oldMode, newMode );

        SW_LOG_INFO( "Mode transitioned: %# -> %#", oldMode.c_str(), newMode.c_str() );
        return true;
    }

    void GameModeStateMachine::update( float32 deltaTime )
    {
        if ( const shared_ptr<IGameModeHandler> pHandler = findHandler( _currentMode ) )
            pHandler->onUpdate( deltaTime );
    }

    void GameModeStateMachine::reset()
    {
        const shared_ptr<IGameModeHandler> pHandler = _currentMode.empty() ? shared_ptr<IGameModeHandler>{}
                                                                           : findHandler( _currentMode );

        // 상태를 **먼저** 옮기고 알린다 — 핸들러가 `onExit` 안에서 이 객체를 들여다볼 수 있고,
        // 그때 보이는 것은 "이미 나간 뒤" 여야 한다.
        _previousMode = _currentMode;
        _currentMode  = {};
        if ( pHandler != nullptr )
            pHandler->onExit( GameModes::none() );
    }

    IGameModeHandler* GameModeStateMachine::getCurrentHandler() const
    {
        const auto it = _mapHandler.find( _currentMode );
        if ( it != _mapHandler.end() )
            return it->second.get();
        return nullptr;
    }
} // namespace sw
