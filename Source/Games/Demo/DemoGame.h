/**
 * @file DemoGame.h
 * @brief DemoGame — HD-2D 오버월드 / 전투 게임 모듈
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/GameInstanceBase.h"
#include "GameFramework/Data/GameData.h"
#include "GameFramework/Data/MonsterDataCatalog.h"
#include "GameFramework/Kits/ActionCombat/ActionRoom.h"
#include "GameFramework/Kits/Overworld/PlayerController.h"
#include "GameFramework/Kits/Overworld/TileMap.h"
#include "GameFramework/Kits/Overworld/ZoneRuntime.h"
#include "GameFramework/Kits/TurnBattle/BattleState.h"
#include "GameFramework/Kits/TurnBattle/SaveGame.h"
#include "GameFramework/Kits/TurnBattle/SpeciesData.h"
#include "GameFramework/Transition/TransitionOrchestrator.h"
#include "GameFramework/UI/RuntimeHud.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) OverworldCameraBias — 직교/탑다운형 바이어스
	//    렌더 훅은 추후. 지금은 포커스·거리만 유지
	// ------------------------------------------------------------------------------
	/** @brief HD-2D 1차: 직교/탑다운형 카메라 바이어스 */
	struct OverworldCameraBias
	{
		float32 _pitchDeg{ 35.0f };
		float32 _yawDeg{ 45.0f };
		float32 _distance{ 12.0f };
		float32 _focusWorldX{ 0.0f };
		float32 _focusWorldY{ 0.0f };
		float32 _focusWorldZ{ 0.0f };
	};

	// ------------------------------------------------------------------------------
	// 2) DemoGame — 오버월드 / 턴제 / 액션 룸을 한 모듈에서 전환
	// ------------------------------------------------------------------------------
	/** @brief HD-2D 오버월드 / 전투 데모 게임 */
	class DemoGame : public GameInstanceBase
	{
	public:
		/** @brief 맵·전투·HUD를 기본 상태로 둡니다. */
		DemoGame();
		/** @brief 소유한 키트 상태를 파괴합니다. */
		virtual ~DemoGame() override;

	protected:
		/** @brief 팩·맵·입력을 초기화합니다. */
		bool onInitialize() override;
		/** @brief 게임 리소스를 해제합니다. */
		void onShutdown() override;
		/** @brief 전환·오버월드·전투 중 활성 모드만 갱신합니다. */
		void onUpdate( float32 deltaTime ) override;

	private:
		// ------------------------------------------------------------------------------
		// 3) 맵 · 세이브 · 파티
		// ------------------------------------------------------------------------------
		/** @brief 맵을 로드하고 플레이어를 스폰합니다. */
		bool loadMap( string_view mapPath, int32 spawnX = 1, int32 spawnY = 1 );
		/** @brief 맵에 대응하는 씬 로드를 요청합니다. */
		void requestSceneForMap( string_view mapPath );
		/** @brief 월드 상태를 세이브에 동기화합니다. */
		void syncSaveFromWorld();
		/** @brief 세이브를 월드에 적용합니다. */
		bool applySaveToWorld();
		/** @brief 새 게임 파티를 초기화합니다. */
		void initNewGameParty();
		/** @brief 세이브에서 파티를 적용합니다. */
		void applyPartyFromSave();
		/** @brief 전투에서 파티 선두를 동기화합니다. */
		void syncPartyLeadFromBattle();

		// ------------------------------------------------------------------------------
		// 4) 프레임 — HUD / 오버월드 / 턴제 / 액션
		// ------------------------------------------------------------------------------
		/** @brief HUD 게이지·대사를 갱신합니다. */
		void updateHud();
		/** @brief 오버월드 이동·워프·조우를 갱신합니다. */
		void updateOverworld( float32 deltaTime );
		/** @brief 턴제 전투 입력을 갱신합니다. */
		void updateBattle( float32 deltaTime );
		/** @brief 액션 룸 전투를 갱신합니다. */
		void updateActionCombat( float32 deltaTime );
		/** @brief 존 역할에 맞춰 액션 룸을 켜거나 끕니다. */
		void syncActionRoomForZone();
		/** @brief 존 BGM을 재생합니다. */
		void playZoneBgm();
		/** @brief 액션 대시를 적용합니다. */
		void applyActionDash();
		/** @brief HD-2D 카메라 바이어스를 플레이어에 맞춥니다. */
		void updateHd2dCameraBias();

		// ------------------------------------------------------------------------------
		// 5) 전환 — 워프 / 전투 / 복귀
		// ------------------------------------------------------------------------------
		/** @brief 워프 전환을 시작합니다. */
		void beginWarpTransition( string_view mapPath, int32 spawnX, int32 spawnY );
		/** @brief 전투 전환을 시작합니다. */
		void beginBattleTransition();
		/** @brief 오버월드 복귀 전환을 시작합니다. */
		void beginReturnTransition();
		/** @brief 전환 콜백을 이 인스턴스에 연결합니다. */
		void wireTransitionCallbacks();
		/** @brief 전투 맵/룸 로드를 시작합니다. */
		void startBattleLoad();
		/** @brief 전투 복귀 로드를 완료합니다. */
		void finishBattleReturnLoad();
		/** @brief 오버월드 입력을 받을 수 있는지 반환합니다. */
		bool canAcceptOverworldInput() const;
		/** @brief 액션 존(던전/보스)인지 반환합니다. */
		bool isActionZone() const;

		TileMap				   _tileMap;
		PlayerController	   _player;
		ZoneRuntime			   _zones;
		BattleState			   _battle;
		ActionRoom			   _actionRoom;
		GameData			   _data;
		MonsterDataCatalog	   _monsterCatalog;
		SpeciesCatalog		   _speciesCatalog;
		SaveGame			   _save;
		TransitionOrchestrator _transitions;
		RuntimeHud			   _hud;
		OverworldCameraBias	   _cameraBias;
		vector<PartyMember>	   _partyList;
		string				   _currentMapPath;
		string				   _returnMapPath; ///< 전투 직전 오버월드 맵
		string				   _returnScenePath;
		int32				   _returnPlayerX;
		int32				   _returnPlayerY;
		uint8				   _bTitleHandedOff		 : 1; ///< Title → Playing 핸드오프 완료
		uint8				   _bBattleReturnPending : 1;
		[[maybe_unused]] uint8 _reserved			 : 6;
	};
} // namespace sw
