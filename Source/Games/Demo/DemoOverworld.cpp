#include "pch.h"

#include "Core/Event/EventDispatcher.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Input/ActionMap.h"
#include "Engine/Input/InputManager.h"

#include "GameFramework/Base/GameEvents.h"
#include "GameFramework/Data/GameStrings.h"
#include "GameFramework/Input/GameActions.h"
#include "GameFramework/Kits/Overworld/TileMap.h"
#include "GameFramework/Kits/Overworld/ZoneRuntime.h"
#include "GameFramework/Kits/TurnBattle/SaveGame.h"

#include "Games/Demo/DemoGame.h"
#include "Games/Demo/DemoGameHelpers.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	SW_LOG_CALLER( "DemoGame" );

	void DemoGame::updateOverworld( float32 deltaTime )
	{
		InputManager* pInputManager = game::getService<InputManager>();
		if ( pInputManager != nullptr )
		{
			_player.setInputEnabled( canAcceptOverworldInput() );
			_player.update( deltaTime, *pInputManager );
		}

		{
			const float32	posX	   = static_cast<float32>( _player.getTileX() );
			const float32	posZ	   = static_cast<float32>( _player.getTileY() );
			DebugDrawQueue* pDebugDraw = game::getService<DebugDrawQueue>();
			if ( pDebugDraw != nullptr )
				pDebugDraw->drawLine( { posX, 0.05f, posZ }, { posX, 1.05f, posZ }, { 0.2f, 1.0f, 0.4f, 1.0f } );
		}

		updateHd2dCameraBias();
		updateActionCombat( deltaTime );

		if ( _transitions.isBusy() )
			return;

		ActionMap&			 actions = gameActions();
		const GameActionIds& ids	 = gameActionIds();
		if ( actions.wasActionTriggered( ids._quickSave ) )
		{
			syncSaveFromWorld();
			const string	   savePath = resolveSavePath( _data._defaultSavePath );
			SaveRequestedEvent saveEvent{};
			saveEvent._savePath			 = savePath;
			EventDispatcher* pDispatcher = game::getService<EventDispatcher>();
			if ( pDispatcher != nullptr )
				pDispatcher->publish( gameEventChannel(), saveEvent );
			SaveGame save;
			save._mapPath	= _gameState._currentMapPath;
			save._playerX	= _gameState._spawnX;
			save._playerY	= _gameState._spawnY;
			save._listParty = _gameState._listParty;
			save._mapFlag	= _gameState._mapFlag;
			save.saveToFile( savePath );
			_hud.setDialogue( GameStrings::get( "ui.game_saved", "Game saved." ) );
		}
		else if ( actions.wasActionTriggered( ids._quickLoad ) )
		{
			const string	   savePath = resolveSavePath( _data._defaultSavePath );
			LoadRequestedEvent loadEvent{};
			loadEvent._savePath			 = savePath;
			EventDispatcher* pDispatcher = game::getService<EventDispatcher>();
			if ( pDispatcher != nullptr )
				pDispatcher->publish( gameEventChannel(), loadEvent );
			SaveGame save;
			if ( save.loadFromFile( savePath ) )
			{
				_gameState._currentMapPath = std::move( save._mapPath );
				_gameState._spawnX		   = save._playerX;
				_gameState._spawnY		   = save._playerY;
				_gameState._listParty	   = std::move( save._listParty );
				_gameState._mapFlag		   = std::move( save._mapFlag );
				applySaveToWorld();
				_hud.setDialogue( "Game loaded." );
			}
		}

		string warpMap;
		int32  warpX{ 1 };
		int32  warpY{ 1 };
		if ( _player.consumeWarpRequest( warpMap, warpX, warpY ) )
			beginWarpTransition( warpMap, warpX, warpY );

		if ( isActionZone() == false && _player.consumeEncounterRequest() )
			beginBattleTransition();
		else
			(void)_player.consumeEncounterRequest();

		if ( _player.consumeInteractRequest() )
		{
			int32 facingX{ 0 };
			int32 facingY{ 0 };
			_player.getFacingTile( facingX, facingY );
			SW_LOG_TRACE( "Interact facing (%#,%#) flags=%# walk=%# pass=%#",
						  facingX, facingY, static_cast<int32>( _tileMap.getFlags( facingX, facingY ) ),
						  _tileMap.isWalkable( facingX, facingY ) ? 1 : 0,
						  _tileMap.isPassThrough( facingX, facingY ) ? 1 : 0 );
			_hud.setDialogue( GameStrings::get( "ui.interact", "Interact..." ) );
			// 체육관만: 상호작용 스텁이 클리어 게이트를 엽니다. 던전/보스는 RoomCleared로 해제합니다.
			if ( _zones.getActiveRole() == ZoneRole::Gym && _zones.isClearGateLocked() )
			{
				_zones.setClearGateLocked( false );
				_gameState.setFlag( "clear_gate_unlocked", 1 );
				_hud.setDialogue( GameStrings::get( "ui.gate_unlocked", "The gate unlocked!" ) );
				SW_LOG_TRACE( "Interact stub unlocked gym clear gate." );
			}
		}

		if ( _player.consumeMovedFlag() )
		{
			SW_LOG_TRACE( "Player tile (%#,%#) on %# [HD-2D cam focus z=%#]",
						  _player.getTileX(), _player.getTileY(), _tileMap.getName(), _cameraBias._focusWorldZ );
			_tileMap.debugLogTileHd2d( _player.getTileX(), _player.getTileY() );
		}
	}

	void DemoGame::updateHd2dCameraBias()
	{
		constexpr float32 kTileSize	  = 1.0f;
		const int32		  playerTileX = _player.getTileX();
		const int32		  playerTileY = _player.getTileY();
		const TileVisual  tileVisual  = _tileMap.getTileVisual( playerTileX, playerTileY );

		_cameraBias._focusWorldX = static_cast<float32>( playerTileX ) * kTileSize;
		_cameraBias._focusWorldY = static_cast<float32>( playerTileY ) * kTileSize;
		_cameraBias._focusWorldZ = static_cast<float32>( tileVisual._height ) * 0.25f;
	}

	bool DemoGame::canAcceptOverworldInput() const
	{
		return _transitions.isBusy() == false && _battle.isActive() == false;
	}
} // namespace sw
