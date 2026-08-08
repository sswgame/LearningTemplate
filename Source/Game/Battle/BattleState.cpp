/**
 * @file BattleState.cpp
 */
#include "BattleState.h"
#include "Core/Utility/Log/Logger.h"
#include <cstring>

namespace sw
{
	BattleState::BattleState()
		: _bPlayerWon{ 0 }
		, _reserved{ 0 }
	{
	}

	void BattleState::startWildEncounter( const char* speciesId )
	{
		startWithPartyLead( SpeciesCatalog::makeStarter(), speciesId );
	}

	void BattleState::startWithPartyLead( const PartyMember& playerLead, const char* foeSpeciesId )
	{
		_player		 = playerLead;
		_foe		 = SpeciesCatalog::makeWild( foeSpeciesId, playerLead._level );
		_phase		 = BattlePhase::Intro;
		_phaseTimer	 = 0.55f;
		_pendingCmd	 = BattleCommand::None;
		_bPlayerWon	 = 0;
		std::snprintf( _statusText, sizeof( _statusText ), "A wild %s appeared!", _foe._nickname.c_str() );
		SW_LOG_INFO( "[Battle] %s", _statusText );
	}

	int32 BattleState::pickFoeMoveSlot() const
	{
		// Thin policy: prefer damaging move when HP high, else mix.
		if ( _foe._hp * 2 < _foe._hpMax )
			return 1;
		return 0;
	}

	void BattleState::applyMove( PartyMember& attacker, PartyMember& defender, int32 moveSlot, bool playerSide )
	{
		const SpeciesDef* sp   = SpeciesCatalog::findSpecies( attacker._speciesId.c_str() );
		const int32		  mid  = ( moveSlot == 0 ) ? sp->_move0 : sp->_move1;
		const MoveDef*	  move = SpeciesCatalog::findMove( mid );
		int32&			  pp   = ( moveSlot == 0 ) ? attacker._pp0 : attacker._pp1;
		if ( pp <= 0 )
		{
			std::snprintf( _statusText, sizeof( _statusText ), "%s has no PP!", attacker._nickname.c_str() );
			return;
		}
		--pp;

		const int32 dmg = move->_power > 0 ? ( move->_power / 4 + attacker._level / 2 ) : 0;
		if ( dmg > 0 )
		{
			defender._hp -= dmg;
			if ( defender._hp < 0 )
				defender._hp = 0;
			std::snprintf( _statusText, sizeof( _statusText ), "%s used %s! (%d dmg)",
						   attacker._nickname.c_str(), move->_name, dmg );
		}
		else
		{
			std::snprintf( _statusText, sizeof( _statusText ), "%s used %s!",
						   attacker._nickname.c_str(), move->_name );
		}
		(void)playerSide;
		SW_LOG_INFO( "[Battle] %s", _statusText );
	}

	void BattleState::update( float32 deltaTime )
	{
		if ( _phase == BattlePhase::Inactive || _phase == BattlePhase::Ended )
			return;

		_phaseTimer -= deltaTime;
		if ( _phaseTimer > 0.0f )
			return;

		if ( _phase == BattlePhase::Intro )
		{
			_phase = BattlePhase::PlayerChoice;
			std::snprintf( _statusText, sizeof( _statusText ), "Fight: 1/2 moves, Enter=Move0, Esc=Run" );
			SW_LOG_INFO( "[Battle] %s", _statusText );
		}
		else if ( _phase == BattlePhase::ResolvePlayer )
		{
			if ( _foe._hp <= 0 )
			{
				_bPlayerWon = 1;
				_player._exp += 10;
				std::snprintf( _statusText, sizeof( _statusText ), "%s fainted! You won!", _foe._nickname.c_str() );
				_phase		= BattlePhase::Ended;
				_phaseTimer = 0.4f;
				SW_LOG_INFO( "[Battle] %s", _statusText );
				return;
			}
			applyMove( _foe, _player, pickFoeMoveSlot(), false );
			_phase		= BattlePhase::ResolveFoe;
			_phaseTimer = 0.45f;
		}
		else if ( _phase == BattlePhase::ResolveFoe )
		{
			if ( _player._hp <= 0 )
			{
				_bPlayerWon = 0;
				std::snprintf( _statusText, sizeof( _statusText ), "%s fainted...", _player._nickname.c_str() );
				_phase		= BattlePhase::Ended;
				_phaseTimer = 0.4f;
				SW_LOG_INFO( "[Battle] %s", _statusText );
				return;
			}
			_phase = BattlePhase::PlayerChoice;
			std::snprintf( _statusText, sizeof( _statusText ), "What will %s do?", _player._nickname.c_str() );
		}
		else if ( _phase == BattlePhase::Ended )
		{
			_phase = BattlePhase::Inactive;
			SW_LOG_INFO( "[Battle] Encounter ended." );
		}
	}

	void BattleState::chooseFight( int32 moveSlot )
	{
		if ( _phase != BattlePhase::PlayerChoice )
			return;
		applyMove( _player, _foe, moveSlot, true );
		_phase		= BattlePhase::ResolvePlayer;
		_phaseTimer = 0.45f;
	}

	void BattleState::chooseRun()
	{
		if ( _phase != BattlePhase::PlayerChoice )
			return;
		std::snprintf( _statusText, sizeof( _statusText ), "Got away safely!" );
		SW_LOG_INFO( "[Battle] %s", _statusText );
		_bPlayerWon = 0;
		_phase		= BattlePhase::Ended;
		_phaseTimer = 0.3f;
	}

	void BattleState::endBattle()
	{
		_phase		   = BattlePhase::Inactive;
		_statusText[0] = '\0';
	}
} // namespace sw
