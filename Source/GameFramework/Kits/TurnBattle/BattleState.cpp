#include "pch.h"

#include "GameFramework/Data/GameData.h"
#include "GameFramework/Data/GameStrings.h"
#include "GameFramework/Kits/TurnBattle/BattleState.h"

#include "Engine/Audio/IAudioSystem.h"

#include "RuntimeAPI/GameService.h"

namespace sw
{
	BattleState::BattleState()
		: _player{}
		, _foe{}
		, _phaseTimer{ 0.0f }
		, _arrStatusText{}
		, _phase{ BattlePhase::Inactive }
		, _pendingCmd{ BattleCommand::None }
		, _bPlayerWon{ 0 }
		, _reserved{ 0 }
	{
	}

	void BattleState::startWildEncounter( const utf8* pSpeciesId )
	{
		startWithPartyLead( SpeciesCatalog::makeStarter(), pSpeciesId );
	}

	void BattleState::startWithPartyLead( const PartyMember& playerLead, const utf8* pFoeSpeciesId )
	{
		_player		= playerLead;
		_foe		= SpeciesCatalog::makeWild( pFoeSpeciesId, playerLead._level );
		_phase		= BattlePhase::Intro;
		_phaseTimer = 0.55f;
		_pendingCmd = BattleCommand::None;
		_bPlayerWon = 0;
		formatstring( _arrStatusText, sizeof( _arrStatusText ), GameStrings::get( "battle.wild_appeared", "A wild %# appeared!" ),
					  _foe._nickname.c_str() );
		SW_LOG_INFO( "[Battle] %#", _arrStatusText );
		if ( GameData::get()._dungeonBgm.empty() == false )
			game::getService<IAudioSystem>()->playMusic( GameData::get()._dungeonBgm );
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
			formatstring( _arrStatusText, sizeof( _arrStatusText ), "%#",
						  GameStrings::get( "battle.prompt_fight", "Fight: 1/2 moves, Enter=Move0, Esc=Run" ) );
			SW_LOG_INFO( "[Battle] %#", _arrStatusText );
		}
		else if ( _phase == BattlePhase::ResolvePlayer )
		{
			if ( _foe._hp <= 0 )
			{
				_bPlayerWon = 1;
				_player._exp += 10;
				formatstring( _arrStatusText, sizeof( _arrStatusText ), GameStrings::get( "battle.foe_fainted", "%# fainted! You won!" ),
							  _foe._nickname.c_str() );
				_phase		= BattlePhase::Ended;
				_phaseTimer = 0.4f;
				SW_LOG_INFO( "[Battle] %#", _arrStatusText );
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
				formatstring( _arrStatusText, sizeof( _arrStatusText ), GameStrings::get( "battle.player_fainted", "%# fainted..." ),
							  _player._nickname.c_str() );
				_phase		= BattlePhase::Ended;
				_phaseTimer = 0.4f;
				SW_LOG_INFO( "[Battle] %#", _arrStatusText );
				return;
			}
			_phase = BattlePhase::PlayerChoice;
			formatstring( _arrStatusText, sizeof( _arrStatusText ), GameStrings::get( "battle.what_will", "What will %# do?" ),
						  _player._nickname.c_str() );
		}
		else if ( _phase == BattlePhase::Ended )
		{
			_phase = BattlePhase::Inactive;
			SW_LOG_INFO( "[Battle] Encounter ended." );
		}
	}

	void BattleState::selectFight( int32 moveSlot )
	{
		if ( _phase != BattlePhase::PlayerChoice )
			return;
		applyMove( _player, _foe, moveSlot, true );
		_phase		= BattlePhase::ResolvePlayer;
		_phaseTimer = 0.45f;
	}

	void BattleState::selectRun()
	{
		if ( _phase != BattlePhase::PlayerChoice )
			return;
		formatstring( _arrStatusText, sizeof( _arrStatusText ), "%#",
					  GameStrings::get( "battle.got_away", "Got away safely!" ) );
		SW_LOG_INFO( "[Battle] %#", _arrStatusText );
		_bPlayerWon = 0;
		_phase		= BattlePhase::Ended;
		_phaseTimer = 0.3f;
	}

	void BattleState::endBattle()
	{
		_phase			  = BattlePhase::Inactive;
		_arrStatusText[0] = '\0';
	}

	void BattleState::applyMove( PartyMember& attacker, PartyMember& defender, int32 moveSlot, bool playerSide )
	{
		const SpeciesDef* pSpecies = SpeciesCatalog::findSpecies( attacker._speciesId.c_str() );
		const int32		  mid	   = ( moveSlot == 0 ) ? pSpecies->_move0 : pSpecies->_move1;
		const MoveDef*	  pMove	   = SpeciesCatalog::findMove( mid );
		int32&			  pp	   = ( moveSlot == 0 ) ? attacker._pp0 : attacker._pp1;
		if ( pp <= 0 )
		{
			formatstring( _arrStatusText, sizeof( _arrStatusText ), GameStrings::get( "battle.no_pp", "%# has no PP!" ),
						  attacker._nickname.c_str() );
			return;
		}
		--pp;

		const int32 dmg = pMove->_power > 0 ? ( pMove->_power / 4 + attacker._level / 2 ) : 0;
		if ( dmg > 0 )
		{
			defender._hp -= dmg;
			if ( defender._hp < 0 )
				defender._hp = 0;
			formatstring( _arrStatusText, sizeof( _arrStatusText ), GameStrings::get( "battle.used_move_dmg", "%# used %#! (%# dmg)" ),
						  attacker._nickname.c_str(), pMove->_name.c_str(), dmg );
		}
		else
		{
			formatstring( _arrStatusText, sizeof( _arrStatusText ), GameStrings::get( "battle.used_move", "%# used %#!" ),
						  attacker._nickname.c_str(), pMove->_name.c_str() );
		}
		(void)playerSide;
		SW_LOG_INFO( "[Battle] %#", _arrStatusText );
	}

	int32 BattleState::pickFoeMoveSlot() const
	{
		if ( _foe._hp * 2 < _foe._hpMax )
			return 1;
		return 0;
	}
} // namespace sw
