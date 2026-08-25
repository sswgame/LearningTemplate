/**
 * @file BattleState.h
 * @brief 턴제 야생 / 트레이너 전투 (Fight / Run + 얇은 적 정책)
 */
#pragma once
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/TurnBattle/SpeciesData.h"

#include "Core/Common/Types.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) 페이즈 · 명령 — Intro → 선택 → 양쪽 Resolve → Ended
	// ------------------------------------------------------------------------------
	/** @brief 턴제 전투 페이즈 */
	enum class BattlePhase : uint8
	{
		Inactive = 0,
		Intro,
		PlayerChoice,
		ResolvePlayer,
		ResolveFoe,
		Ended
	};

	/** @brief 플레이어가 고른 한 턴 명령 */
	enum class BattleCommand : uint8
	{
		None = 0,
		FightMove0,
		FightMove1,
		Run
	};

	// ------------------------------------------------------------------------------
	// 2) BattleState — 선두 vs 야생/적, 상태 텍스트는 HUD용
	// ------------------------------------------------------------------------------
	/** @brief 턴제 야생/트레이너 전투 상태 */
	class SW_GF_API BattleState
	{
	public:
		/** @brief 비활성 페이즈로 시작합니다. */
		BattleState();

		/** @brief 야생 조우를 시작합니다. */
		void startWildEncounter( const utf8* pSpeciesId = "critter_a" );
		/** @brief 플레이어 선두와 적 종족으로 전투를 시작합니다. */
		void startWithPartyLead( const PartyMember& playerLead, const utf8* pFoeSpeciesId );
		/** @brief 전투 페이즈를 갱신합니다. */
		void update( float32 deltaTime );
		/** @brief Fight 명령을 선택합니다. */
		void selectFight( int32 moveSlot = 0 );
		/** @brief Run 명령을 선택합니다. */
		void selectRun();
		/** @brief 전투를 종료합니다. */
		void endBattle();

		/** @brief 전투가 진행 중인지 반환합니다. */
		bool isActive() const { return _phase != BattlePhase::Inactive && _phase != BattlePhase::Ended; }
		/** @brief 플레이어가 승리했는지 반환합니다. */
		bool playerWon() const { return _bPlayerWon != 0; }
		/** @brief 현재 전투 페이즈를 반환합니다. */
		BattlePhase getPhase() const { return _phase; }
		/** @brief 적 닉네임을 반환합니다. */
		const utf8* getFoeName() const { return _foe._nickname.c_str(); }
		/** @brief HUD용 상태 텍스트를 반환합니다. */
		const utf8* getStatusText() const { return _arrStatusText; }
		/** @brief 플레이어 선두를 반환합니다. */
		const PartyMember& player() const { return _player; }
		/** @brief 플레이어 선두를 반환합니다. */
		PartyMember& player() { return _player; }
		/** @brief 적 파티 멤버를 반환합니다. */
		const PartyMember& foe() const { return _foe; }

	private:
		/** @brief 기술을 적용합니다. */
		void applyMove( PartyMember& attacker, PartyMember& defender, int32 moveSlot, bool playerSide );
		/** @brief 적이 쓸 기술 슬롯을 고릅니다. */
		int32 pickFoeMoveSlot() const;

		PartyMember			   _player;
		PartyMember			   _foe;
		float32				   _phaseTimer;			///< 현재 페이즈 남은 시간
		utf8				   _arrStatusText[160]; ///< HUD 한 줄
		BattlePhase			   _phase;
		BattleCommand		   _pendingCmd;
		uint8				   _bPlayerWon : 1;
		[[maybe_unused]] uint8 _reserved   : 6;
	};
} // namespace sw
