#include "pch.h"

#include "Core/Event/EventDispatcher.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/InputManager.h"

#include "GameFramework/Base/GameEvents.h"
#include "GameFramework/Base/GameService.h"
#include "GameFramework/Input/GameActions.h"
#include "GameFramework/Kits/TurnBattle/BattleState.h"
#include "GameFramework/Kits/TurnBattle/SpeciesData.h"

#include "Games/Demo/DemoGame.h"

namespace sw
{
	SW_LOG_CALLER( "DemoGame" );

	void DemoGame::syncPartyLeadFromBattle()
	{
		if ( _gameState._listParty.empty() )
			return;
		_gameState._listParty[0] = _battle.player();
	}

	void DemoGame::updateBattle( float32 deltaTime )
	{
		if ( _transitions.isBusy() )
			return;

		InputManager* pInputManager = game::getService<InputManager>();
		const bool	  bWasActive	= _battle.isActive();
		_battle.update( deltaTime );

		if ( _battle.getPhase() == BattlePhase::PlayerChoice )
		{
			ActionMap&			 actions = gameActions();
			const GameActionIds& ids	 = gameActionIds();
			if ( pInputManager != nullptr && actions.getInputManager() != pInputManager )
				actions.setInputManager( pInputManager );

			if ( actions.wasActionTriggered( ids._fightMove0 ) || actions.wasActionTriggered( ids._confirm ) )
				_battle.selectFight( 0 );
			else if ( actions.wasActionTriggered( ids._fightMove1 ) )
				_battle.selectFight( 1 );
			else if ( actions.wasActionTriggered( ids._cancel ) )
				_battle.selectRun();
		}

		if ( bWasActive && _battle.isActive() == false && _gameState._bBattleReturnPending != 0 )
			beginReturnTransition();
	}

	void DemoGame::beginBattleTransition()
	{
		_gameState._returnMapPath		 = _gameState._currentMapPath;
		_gameState._returnScenePath		 = _tileMap.getScenePath();
		_gameState._returnPlayerX		 = _player.getTileX();
		_gameState._returnPlayerY		 = _player.getTileY();
		_gameState._bBattleReturnPending = 1;
		BattleRequestedEvent requestedEvent{};
		requestedEvent._fromMapPath	 = _gameState._returnMapPath;
		EventDispatcher* pDispatcher = game::getService<EventDispatcher>();
		if ( pDispatcher != nullptr )
			pDispatcher->publish( gameEventChannel(), requestedEvent );
		_transitions.beginBattle();
	}

	void DemoGame::beginReturnTransition()
	{
		_transitions.beginReturn();
	}

	void DemoGame::startBattleLoad()
	{
		if ( _gameState._listParty.empty() )
			initNewGameParty();
		string foeId = _tileMap.pickEncounterSpeciesId();
		if ( foeId.empty() )
			foeId = _data._defaultEncounterId;
		_battle.startWithPartyLead( _gameState._listParty[0], foeId.c_str() );
		SceneManager* pSceneManager = game::getService<SceneManager>();
		if ( pSceneManager != nullptr )
			pSceneManager->requestLoadAsync( _data._battleScene );
		_hud.setDialogue( _battle.getStatusText() );
		SW_LOG_INFO( "Battle scene requested: %# (foe=%#)", _data._battleScene, foeId.c_str() );
	}

	void DemoGame::finishBattleReturnLoad()
	{
		const bool bWon = _battle.playerWon();
		{
			BattleEndedEvent endedEvent{};
			endedEvent._bPlayerWon		 = bWon ? 1 : 0;
			EventDispatcher* pDispatcher = game::getService<EventDispatcher>();
			if ( pDispatcher != nullptr )
				pDispatcher->publish( gameEventChannel(), endedEvent );
		}
		syncPartyLeadFromBattle();
		_battle.endBattle();
		if ( bWon )
			_gameState.setFlag( "clear_gate_unlocked", 1 );
		loadMap( _gameState._returnMapPath, _gameState._returnPlayerX, _gameState._returnPlayerY );
		if ( _gameState.getFlag( "clear_gate_unlocked", 0 ) != 0 )
		{
			_zones.setClearGateLocked( false );
			ClearGateChangedEvent gateEvent{};
			gateEvent._bLocked = 0;
			game::getService<EventDispatcher>()->publish( gameEventChannel(), gateEvent );
			if ( bWon )
				SW_LOG_TRACE( "Clear gate unlocked after win." );
		}
		if ( _gameState._returnScenePath.empty() == false )
			game::getService<SceneManager>()->requestLoadAsync( _gameState._returnScenePath );
		_gameState._bBattleReturnPending = 0;
		_hud.clearDialogue();
		SW_LOG_TRACE( "Returned to overworld '%#' @ (%#,%#)",
					  _gameState._returnMapPath, _gameState._returnPlayerX, _gameState._returnPlayerY );
	}
} // namespace sw
