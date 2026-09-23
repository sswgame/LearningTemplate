/**
 * @file GameModeStateMachine.h
 * @brief 게임 모드(Title, Gameplay, Paused 등) 상태 전환과 수명주기를 관리하는 범용 FSM 입니다.
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
     * @brief 특정 게임플레이 모드에 들어가고 · 갱신하고 · 나갈 때 불리는 핸들러 인터페이스입니다.
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

        /** @brief 이 게임 모드로 전환되어 들어올 때 불립니다. */
        virtual void onEnter( const hashed_string& previousMode ) = 0;
        /** @brief 매 프레임 해당 게임 모드 로직을 갱신합니다. */
        virtual void onUpdate( float32 deltaTime ) = 0;
        /** @brief 다른 모드로 전환되어 나갈 때 불립니다. */
        virtual void onExit( const hashed_string& nextMode ) = 0;
    };

    /**
     * @class GameModeStateMachine
     * @brief 장르에 매이지 않고 게임 상태 모드 사이의 수명주기를 관리하는 범용 FSM 입니다.
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

        /** @brief 활성 모드 핸들러의 onUpdate 를 부릅니다. */
        void update( float32 deltaTime );

        /** @brief 상태 머신을 초기 상태(None)로 리셋합니다. */
        void reset();

        /** @brief 현재 활성 게임 모드를 반환합니다. */
        const hashed_string& getCurrentMode() const { return _currentMode; }
        /** @brief 직전 게임 모드를 반환합니다. */
        const hashed_string& getPreviousMode() const { return _previousMode; }
        /** @brief 현재 모드의 핸들러를 반환합니다(없으면 nullptr). */
        IGameModeHandler* getCurrentHandler() const;

        /** @brief 모드가 바뀔 때 부를 콜백 델리게이트를 설정합니다. */
        void setOnModeChanged( ModeChangedDelegate delegate ) { _onModeChanged = std::move( delegate ); }

    private:
        /**
         * @brief 모드의 핸들러를 **소유권을 한 몫 들고** 반환합니다. 없으면 빈 포인터입니다.
         * @details 핸들러를 부르는 자리는 모두 이것을 거칩니다. 표의 반복자나 생포인터로 부르면,
         *          핸들러가 그 안에서 자신을 해제했을 때 (1) 반복자가 죽고 (2) 표가 쥔 마지막
         *          참조가 사라져 **실행 중인 콜백의 `this` 가 파괴됩니다.** 한 몫을 들고 있으면
         *          콜백이 끝날 때까지는 살아 있습니다.
         */
        shared_ptr<IGameModeHandler> findHandler( const hashed_string& mode ) const;

        hashed_string                                              _currentMode;
        hashed_string                                              _previousMode;
        unordered_map<hashed_string, shared_ptr<IGameModeHandler>> _mapHandler;
        ModeChangedDelegate                                        _onModeChanged;
        bool                                                       _bIsTransitioning;
    };
} // namespace sw
