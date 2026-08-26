#include "pch.h"

#include "Core/Event/EventDispatcher.h"

#include "Engine/Game/GameState.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Input/ActionMap.h"
#include "Engine/Input/InputManager.h"

#include "GameFramework/Base/GameEvents.h"
#include "GameFramework/Data/GameStrings.h"
#include "GameFramework/Input/GameActions.h"
#include "GameFramework/Kits/Overworld/TileMap.h"
#include "GameFramework/Kits/Overworld/ZoneRuntime.h"

#include "Games/Demo/DemoGame.h"
#include "Games/Demo/DemoGameHelpers.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	void DemoGame::updateOverworld( float32 deltaTime )
	{
		InputManager& inputManager = *game::getService<InputManager>();

		const bool bPlaying = ( getGameState() == GameState::Playing );
		_player.setInputEnabled( canAcceptOverworldInput() );
		if ( bPlaying )
			_player.update( deltaTime, inputManager );

		{
			const float32 posX = static_cast<float32>( _player.getTileX() );
			const float32 posZ = static_cast<float32>( _player.getTileY() );
			game::getService<DebugDrawQueue>()->drawLine( { posX, 0.05f, posZ }, { posX, 1.05f, posZ }, { 0.2f, 1.0f, 0.4f, 1.0f } );
		}

		updateHd2dCameraBias();
		updateActionCombat( deltaTime );

		if ( bPlaying == false || _transitions.isBusy() )
			return;

		ActionMap&			 actions = gameActions();
		const GameActionIds& ids	 = gameActionIds();
		if ( actions.wasActionTriggered( ids._quickSave ) )
		{
			syncSaveFromWorld();
			const string	   savePath = resolveSavePath( _data._defaultSavePath );
			SaveRequestedEvent saveEvent{};
			saveEvent._savePath = savePath;
			game::getService<EventDispatcher>()->publish( gameEventChannel(), saveEvent );
			_save.saveToFile( savePath );
			_hud.setDialogue( GameStrings::get( "ui.game_saved", "Game saved." ) );
		}
		else if ( actions.wasActionTriggered( ids._quickLoad ) )
		{
			const string	   savePath = resolveSavePath( _data._defaultSavePath );
			LoadRequestedEvent loadEvent{};
			loadEvent._savePath = savePath;
			game::getService<EventDispatcher>()->publish( gameEventChannel(), loadEvent );
			if ( _save.loadFromFile( savePath ) )
			{
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
			SW_LOG_INFO( "[DemoGame] Interact facing (%#,%#) flags=%# walk=%# pass=%#",
						 facingX, facingY, static_cast<int32>( _tileMap.getFlags( facingX, facingY ) ),
						 _tileMap.isWalkable( facingX, facingY ) ? 1 : 0,
						 _tileMap.isPassThrough( facingX, facingY ) ? 1 : 0 );
			_hud.setDialogue( GameStrings::get( "ui.interact", "Interact..." ) );
			// 체육관만: 상호작용 스텁이 클리어 게이트를 엽니다. 던전/보스는 RoomCleared로 해제합니다.
			if ( _zones.getActiveRole() == ZoneRole::Gym && _zones.isClearGateLocked() )
			{
				_zones.setClearGateLocked( false );
				_save.setFlag( "clear_gate_unlocked", 1 );
				_hud.setDialogue( GameStrings::get( "ui.gate_unlocked", "The gate unlocked!" ) );
				SW_LOG_INFO( "[DemoGame] Interact stub unlocked gym clear gate." );
			}
		}

		if ( _player.consumeMovedFlag() )
		{
			SW_LOG_TRACE( "[DemoGame] Player tile (%#,%#) on %# [HD-2D cam focus z=%#]",
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
		const bool bIsPlaying = ( getGameState() == GameState::Playing );
		const bool bIsNotBusy = ( _transitions.isBusy() == false && _battle.isActive() == false );
		return bIsPlaying && bIsNotBusy;
	}
} // namespace sw
