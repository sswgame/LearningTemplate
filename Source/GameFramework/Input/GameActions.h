/**
 * @file GameActions.h
 * @brief SWGame용 공유 ActionMap과 게임플레이→액션 이름 테이블
 */
#pragma once
#include "GameFramework/GameFrameworkExports.h"

#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
	class ActionMap;

	// ------------------------------------------------------------------------------
	// 1) GameActionIds — 논리 슬롯 → InputMap 액션 이름
	//    XML <gameplayIds>가 없으면 default.input.xml과 같은 폴백 문자열
	// ------------------------------------------------------------------------------
	struct SW_GF_API GameActionIds
	{
		string _moveUp		  = "MoveUp";
		string _moveDown	  = "MoveDown";
		string _moveLeft	  = "MoveLeft";
		string _moveRight	  = "MoveRight";
		string _interact	  = "Interact";
		string _confirm		  = "Confirm";
		string _cancel		  = "Cancel";
		string _continue	  = "Continue";
		string _fightMove0	  = "FightMove0";
		string _fightMove1	  = "FightMove1";
		string _attack		  = "Attack";
		string _dash		  = "Dash";
		string _point		  = "Point";
		string _quickSave	  = "QuickSave";
		string _quickLoad	  = "QuickLoad";
		string _reloadShaders = "ReloadShaders";
		string _reloadEditor  = "ReloadEditor";
		string _reloadGame	  = "ReloadGame";

		/** @brief InputMap <layers>/<layer name>과 일치해야 합니다. */
		string _layerGameplay  = "Gameplay";
		string _layerUI		   = "UI";
		string _layerTitle	   = "Title";
		string _layerCinematic = "Cinematic";
		string _layerDebug	   = "Debug";

		/** @brief InputMap XML의 <gameplayIds>를 읽습니다 (경로는 GameData). */
		bool loadFromResource( string_view assetRelativePath );
	};

	// ------------------------------------------------------------------------------
	// 2) 프로세스 전역 게임플레이 입력
	//    App의 셸 ActionMap과 별개. InputManager에 한 번 바인딩
	// ------------------------------------------------------------------------------
	/** @brief 전역 ID 테이블 (InputMap 로드 시 채워짐). */
	SW_GF_API GameActionIds& gameActionIds();
	/** @brief 전역 게임플레이 ActionMap. */
	SW_GF_API ActionMap& gameActions();
} // namespace sw
