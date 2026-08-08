#pragma once
/**
 * @file BattleState.h
 * @brief Turn-based wild / trainer battle (Fight / Run + thin foe policy)
 */

#include "Core/Common/Types.h"
#include "Game/Data/SpeciesData.h"

namespace sw
{
	enum class BattlePhase : uint8
	{
		Inactive = 0,
		Intro,
		PlayerChoice,
		ResolvePlayer,
		ResolveFoe,
		Ended
	};

	enum class BattleCommand : uint8
	{
		None = 0,
		FightMove0,
		FightMove1,
		Run
	};

	class BattleState
	{
	public:
		BattleState();

		void startWildEncounter( const char* speciesId = "critter_a" );
		void startWithPartyLead( const PartyMember& playerLead, const char* foeSpeciesId );
		void update( float32 deltaTime );
		void chooseFight( int32 moveSlot = 0 );
		void chooseRun();
		void endBattle();

		bool		isActive() const { return _phase != BattlePhase::Inactive && _phase != BattlePhase::Ended; }
		bool		didPlayerWin() const { return _bPlayerWon != 0; }
		BattlePhase getPhase() const { return _phase; }
		const char* getFoeName() const { return _foe._nickname.c_str(); }
		const char* getStatusText() const { return _statusText; }
		const PartyMember& getPlayer() const { return _player; }
		const PartyMember& getFoe() const { return _foe; }
		PartyMember&	   getPlayerMutable() { return _player; }

	private:
		void applyMove( PartyMember& attacker, PartyMember& defender, int32 moveSlot, bool playerSide );
		int32 pickFoeMoveSlot() const;

		BattlePhase _phase		= BattlePhase::Inactive;
		float32		_phaseTimer = 0.0f;
		PartyMember _player{};
		PartyMember _foe{};
		BattleCommand _pendingCmd = BattleCommand::None;
		char		_statusText[160]{};
		uint8		_bPlayerWon : 1;
		[[maybe_unused]] uint8 _reserved : 7;
	};
} // namespace sw
