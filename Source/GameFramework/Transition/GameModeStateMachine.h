/**
 * @file GameModeStateMachine.h
 * @brief 오버월드, 턴배틀, 액션전투, 타이틀 등 게임 모드 상태 전환 및 생명주기 관리 FSM
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Delegate/Delegate.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	/**
	 * @enum GamePlayMode
	 * @brief 장르 키트 및 게임플레이 메인 모드 열거형
	 */
	enum class GamePlayMode : uint8
	{
		None = 0,
		Title,
		Overworld,
		TurnBattle,
		ActionCombat,
		Cutscene,
		Paused,
		Custom
	};

	/**
	 * @brief 특정 게임플레이 모드 진입/업데이트/종료 시 호출되는 핸들러 인터페이스
	 */
	class SW_GF_API IGameModeHandler
	{
	public:
		IGameModeHandler()									   = default;
		virtual ~IGameModeHandler()							   = default;
		IGameModeHandler( const IGameModeHandler& )			   = default;
		IGameModeHandler& operator=( const IGameModeHandler& ) = default;
		IGameModeHandler( IGameModeHandler&& )				   = default;
		IGameModeHandler& operator=( IGameModeHandler&& )	   = default;

		/** @brief 해당 게임 모드로 전환되어 진입할 때 호출됩니다. */
		virtual void onEnter( GamePlayMode previousMode ) = 0;
		/** @brief 매 프레임 해당 게임 모드 로직을 갱신합니다. */
		virtual void onUpdate( float32 deltaTime ) = 0;
		/** @brief 다른 모드로 전환되어 나갈 때 호출됩니다. */
		virtual void onExit( GamePlayMode nextMode ) = 0;
	};

	/**
	 * @class GameModeStateMachine
	 * @brief 게임플레이 모드(Overworld, TurnBattle, ActionCombat 등)의 상태 전환을 일원화하여 관리하는 FSM
	 */
	class SW_GF_API GameModeStateMachine
	{
	public:
		using ModeChangedDelegate = Delegate<void( GamePlayMode previousMode, GamePlayMode newMode )>;

		GameModeStateMachine();
		~GameModeStateMachine() = default;

		GameModeStateMachine( const GameModeStateMachine& )			   = delete;
		GameModeStateMachine& operator=( const GameModeStateMachine& ) = delete;
		GameModeStateMachine( GameModeStateMachine&& )				   = default;
		GameModeStateMachine& operator=( GameModeStateMachine&& )	   = default;

		/** @brief 특정 모드에 대응하는 핸들러를 등록합니다. */
		void registerHandler( GamePlayMode mode, shared_ptr<IGameModeHandler> pHandler );
		/** @brief 특정 모드의 핸들러를 등록 해제합니다. */
		void unregisterHandler( GamePlayMode mode );

		/** @brief 새로운 게임 모드로 상태를 전이합니다. */
		bool transitionTo( GamePlayMode newMode );

		/** @brief 활성 모드 핸들러의 onUpdate를 호출합니다. */
		void update( float32 deltaTime );

		/** @brief 상태 머신을 초기 상태(None)로 리셋합니다. */
		void reset();

		/** @brief 현재 활성 게임 모드를 반환합니다. */
		GamePlayMode getCurrentMode() const { return _currentMode; }
		/** @brief 직전 게임 모드를 반환합니다. */
		GamePlayMode getPreviousMode() const { return _previousMode; }
		/** @brief 현재 모드의 핸들러를 반환합니다. (없으면 nullptr) */
		IGameModeHandler* getCurrentHandler() const;

		/** @brief 모드 변경 시 호출될 콜백 델리게이트를 설정합니다. */
		void setOnModeChanged( ModeChangedDelegate delegate ) { _onModeChanged = delegate; }

	private:
		GamePlayMode											  _currentMode;
		GamePlayMode											  _previousMode;
		unordered_map<GamePlayMode, shared_ptr<IGameModeHandler>> _mapHandler;
		ModeChangedDelegate										  _onModeChanged;
	};
} // namespace sw
