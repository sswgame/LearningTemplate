#include "pch.h"

#include "Core/Event/EventDispatcher.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/InputManager.h"

#include "GameFramework/Base/GameEvents.h"
#include "GameFramework/Input/GameActions.h"
#include "GameFramework/Kits/TurnBattle/BattleState.h"
#include "GameFramework/Kits/TurnBattle/SpeciesData.h"

#include "Games/Demo/DemoGame.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	SW_LOG_CALLER( "DemoGame" );

	void DemoGame::syncPartyLeadFromBattle()
	{
		if ( _listParty.empty() )
			return;
		const PartyMember& leadMember = _battle.player();
		_listParty[0]._hp			  = leadMember._hp;
		_listParty[0]._hpMax		  = leadMember._hpMax;
		_listParty[0]._pp0			  = leadMember._pp0;
		_listParty[0]._pp1			  = leadMember._pp1;
		_listParty[0]._exp			  = leadMember._exp;
		_listParty[0]._level		  = leadMember._level;
		_listParty[0]._expNext		  = leadMember._expNext;
	}

	void DemoGame::updateBattle( float32 deltaTime )
	{
		if ( _transitions.isBusy() )
			return;

		InputManager& inputManager = *game::getService<InputManager>();
		const bool	  bWasActive   = _battle.isActive();
		_battle.update( deltaTime );

		if ( _battle.getPhase() == BattlePhase::PlayerChoice )
		{
			ActionMap&			 actions = gameActions();
			const GameActionIds& ids	 = gameActionIds();
			if ( actions.getInputManager() != &inputManager )
				actions.setInputManager( &inputManager );

			if ( actions.wasActionTriggered( ids._fightMove0 ) || actions.wasActionTriggered( ids._confirm ) )
				_battle.selectFight( 0 );
			else if ( actions.wasActionTriggered( ids._fightMove1 ) )
				_battle.selectFight( 1 );
			else if ( actions.wasActionTriggered( ids._cancel ) )
				_battle.selectRun();
		}

		if ( bWasActive && _battle.isActive() == false && _bBattleReturnPending != 0 )
			beginReturnTransition();
	}

	void DemoGame::beginBattleTransition()
	{
		_returnMapPath		  = _currentMapPath;
		_returnScenePath	  = _tileMap.getScenePath();
		_returnPlayerX		  = _player.getTileX();
		_returnPlayerY		  = _player.getTileY();
		_bBattleReturnPending = 1;
		BattleRequestedEvent requestedEvent{};
		requestedEvent._fromMapPath = _returnMapPath;
		game::getService<EventDispatcher>()->publish( gameEventChannel(), requestedEvent );
		_transitions.beginBattle();
	}

	void DemoGame::beginReturnTransition()
	{
		_transitions.beginReturn();
	}

	void DemoGame::startBattleLoad()
	{
		if ( _listParty.empty() )
			initNewGameParty();
		string foeId = _tileMap.pickEncounterSpeciesId();
		if ( foeId.empty() )
			foeId = _data._defaultEncounterId;
		_battle.startWithPartyLead( _listParty[0], foeId.c_str() );
		game::getService<SceneManager>()->requestLoadAsync( _data._battleScene );
		_hud.setDialogue( _battle.getStatusText() );
		SW_LOG_INFO( "Battle scene requested: %# (foe=%#)", _data._battleScene, foeId.c_str() );
	}

	void DemoGame::finishBattleReturnLoad()
	{
		const bool bWon = _battle.playerWon();
		{
			BattleEndedEvent endedEvent{};
			endedEvent._bPlayerWon = bWon ? 1 : 0;
			game::getService<EventDispatcher>()->publish( gameEventChannel(), endedEvent );
		}
		syncPartyLeadFromBattle();
		_battle.endBattle();
		if ( bWon )
			_save.setFlag( "clear_gate_unlocked", 1 );
		loadMap( _returnMapPath, _returnPlayerX, _returnPlayerY );
		if ( _save.getFlag( "clear_gate_unlocked", 0 ) != 0 )
		{
			_zones.setClearGateLocked( false );
			ClearGateChangedEvent gateEvent{};
			gateEvent._bLocked = 0;
			game::getService<EventDispatcher>()->publish( gameEventChannel(), gateEvent );
			if ( bWon )
				SW_LOG_TRACE( "Clear gate unlocked after win." );
		}
		if ( _returnScenePath.empty() == false )
			game::getService<SceneManager>()->requestLoadAsync( _returnScenePath );
		_bBattleReturnPending = 0;
		_hud.clearDialogue();
		SW_LOG_TRACE( "Returned to overworld '%#' @ (%#,%#)",
					  _returnMapPath, _returnPlayerX, _returnPlayerY );
	}
} // namespace sw
