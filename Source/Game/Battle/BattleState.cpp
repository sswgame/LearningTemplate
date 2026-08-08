/**
 * @file BattleState.cpp
 */
#include "BattleState.h"
#include "Core/Utility/Log/Logger.h"
#include <cstring>

namespace sw
{
	void BattleState::startWildEncounter( const char* foeName )
	{
		_phase		= BattlePhase::Intro;
		_phaseTimer = 0.6f;
		std::snprintf( _foeName, sizeof( _foeName ), "%s", foeName != nullptr ? foeName : "Wild Critter" );
		std::snprintf( _statusText, sizeof( _statusText ), "A wild %s appeared! [Fight]/Run]", _foeName );
		SW_LOG_INFO( "[Battle] %s", _statusText );
	}

	void BattleState::update( float32 deltaTime )
	{
		if ( _phase == BattlePhase::Inactive || _phase == BattlePhase::Ended )
			return;

		_phaseTimer -= deltaTime;
		if ( _phase == BattlePhase::Intro && _phaseTimer <= 0.0f )
		{
			_phase = BattlePhase::PlayerChoice;
			std::snprintf( _statusText, sizeof( _statusText ), "Choose: Fight (Enter) / Run (Escape)" );
			SW_LOG_INFO( "[Battle] %s", _statusText );
		}
		else if ( _phase == BattlePhase::Resolve && _phaseTimer <= 0.0f )
		{
			_phase = BattlePhase::Ended;
			SW_LOG_INFO( "[Battle] Encounter ended." );
			_phase = BattlePhase::Inactive;
		}
	}

	void BattleState::chooseFight()
	{
		if ( _phase != BattlePhase::PlayerChoice )
			return;
		std::snprintf( _statusText, sizeof( _statusText ), "You fought %s and won!", _foeName );
		SW_LOG_INFO( "[Battle] %s", _statusText );
		_phase		= BattlePhase::Resolve;
		_phaseTimer = 0.5f;
	}

	void BattleState::chooseRun()
	{
		if ( _phase != BattlePhase::PlayerChoice )
			return;
		std::snprintf( _statusText, sizeof( _statusText ), "Got away safely!" );
		SW_LOG_INFO( "[Battle] %s", _statusText );
		_phase		= BattlePhase::Resolve;
		_phaseTimer = 0.35f;
	}

	void BattleState::endBattle()
	{
		_phase = BattlePhase::Inactive;
		_statusText[0] = '\0';
	}
} // namespace sw
