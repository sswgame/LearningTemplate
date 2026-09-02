/**
 * @file GameModeStateMachine.h
 * @brief 게임 모드(Title, Gameplay, Paused 등) 상태 전환 및 생명주기 관리 범용 FSM
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    namespace GameModes
    {
        inline hashed_string none()
        {
            static const hashed_string k{ "None" };
            return k;
        }
        inline hashed_string title()
        {
            static const hashed_string k{ "Title" };
            return k;
        }
        inline hashed_string gameplay()
        {
            static const hashed_string k{ "Gameplay" };
            return k;
        }
        inline hashed_string paused()
        {
            static const hashed_string k{ "Paused" };
            return k;
        }
        inline hashed_string cutscene()
        {
            static const hashed_string k{ "Cutscene" };
            return k;
        }
    } // namespace GameModes

    /**
     * @brief 특정 게임플레이 모드 진입/업데이트/종료 시 호출되는 핸들러 인터페이스
     */
    class SW_GF_API IGameModeHandler
    {
    public:
        IGameModeHandler()                                     = default;
        virtual ~IGameModeHandler()                            = default;
        IGameModeHandler( const IGameModeHandler& )            = default;
        IGameModeHandler& operator=( const IGameModeHandler& ) = default;
        IGameModeHandler( IGameModeHandler&& )                 = default;
        IGameModeHandler& operator=( IGameModeHandler&& )      = default;

        /** @brief 해당 게임 모드로 전환되어 진입할 때 호출됩니다. */
        virtual void onEnter( const hashed_string& previousMode ) = 0;
        /** @brief 매 프레임 해당 게임 모드 로직을 갱신합니다. */
        virtual void onUpdate( float32 deltaTime ) = 0;
        /** @brief 다른 모드로 전환되어 나갈 때 호출됩니다. */
        virtual void onExit( const hashed_string& nextMode ) = 0;
    };

    /**
     * @class GameModeStateMachine
     * @brief 장르에 구애받지 않고 게임 상태 모드 간의 라이프사이클을 관리하는 범용 FSM
     */
    class SW_GF_API GameModeStateMachine
    {
    public:
        using ModeChangedDelegate = Delegate<void( const hashed_string& previousMode, const hashed_string& newMode )>;

        GameModeStateMachine();
        ~GameModeStateMachine() = default;

        GameModeStateMachine( const GameModeStateMachine& )            = delete;
        GameModeStateMachine& operator=( const GameModeStateMachine& ) = delete;
        GameModeStateMachine( GameModeStateMachine&& )                 = default;
        GameModeStateMachine& operator=( GameModeStateMachine&& )      = default;

        /** @brief 특정 모드에 대응하는 핸들러를 등록합니다. */
        void registerHandler( const hashed_string& mode, shared_ptr<IGameModeHandler> pHandler );
        /** @brief 특정 모드의 핸들러를 등록 해제합니다. */
        void unregisterHandler( const hashed_string& mode );

        /** @brief 새로운 게임 모드로 상태를 전이합니다. */
        bool transitionTo( const hashed_string& newMode );

        /** @brief 활성 모드 핸들러의 onUpdate를 호출합니다. */
        void update( float32 deltaTime );

        /** @brief 상태 머신을 초기 상태(None)로 리셋합니다. */
        void reset();

        /** @brief 현재 활성 게임 모드를 반환합니다. */
        const hashed_string& getCurrentMode() const { return _currentMode; }
        /** @brief 직전 게임 모드를 반환합니다. */
        const hashed_string& getPreviousMode() const { return _previousMode; }
        /** @brief 현재 모드의 핸들러를 반환합니다. (없으면 nullptr) */
        IGameModeHandler* getCurrentHandler() const;

        /** @brief 모드 변경 시 호출될 콜백 델리게이트를 설정합니다. */
        void setOnModeChanged( ModeChangedDelegate delegate ) { _onModeChanged = delegate; }

    private:
        hashed_string                                              _currentMode;
        hashed_string                                              _previousMode;
        unordered_map<hashed_string, shared_ptr<IGameModeHandler>> _mapHandler;
        ModeChangedDelegate                                        _onModeChanged;
        bool                                                       _bIsTransitioning{ false };
    };
} // namespace sw
