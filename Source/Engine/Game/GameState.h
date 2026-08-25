/**
 * @file GameState.h
 * @brief 앱/에디터 Play 상태
 */
#pragma once
#include "Core/Common/Macros.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) GameState — Play/Pause/Stop (에디터 트랜스포트 · 인게임 일시정지)
	//    AppState(Splash/Title/Playing/Editor)와 별개
	// ------------------------------------------------------------------------------
	enum class GameState
	{
		Stopped,
		Playing,
		Paused
	};

	/** @brief 현재 Play 상태를 반환합니다. */
	SW_API GameState getGameState();
	/** @brief Play 상태를 바꿉니다. */
	SW_API void setGameState( GameState state );

	// ------------------------------------------------------------------------------
	// 2) GameStartMode — Title → Playing 한 번만 소비
	//    set 후 consume이 읽고 NewGame으로 되돌림
	// ------------------------------------------------------------------------------
	enum class GameStartMode : uint8
	{
		NewGame = 0,
		Continue
	};

	/** @brief 다음 Playing 전환이 새 게임인지 이어하기인지 설정합니다. */
	SW_API void setGameStartMode( GameStartMode mode );
	/** @brief 시작 모드를 읽고 기본값(NewGame)으로 되돌립니다. */
	SW_API GameStartMode consumeGameStartMode();
} // namespace sw
