#include "pch.h"

#include "Core/Event/EventDispatcher.h"

#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Input/ActionMap.h"

#include "GameFramework/Base/GameEvents.h"
#include "GameFramework/Data/GameStrings.h"
#include "GameFramework/Input/GameActions.h"
#include "GameFramework/Kits/ActionCombat/ActionRoom.h"
#include "GameFramework/Kits/Overworld/PlayerLocomotion.h"

#include "Games/Demo/DemoGame.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	SW_LOG_CALLER( "DemoGame" );

	void DemoGame::updateActionCombat( float32 deltaTime )
	{
		if ( _actionRoom.isActive() == false || canAcceptOverworldInput() == false )
			return;

		ActionMap&			 actions = gameActions();
		const GameActionIds& ids	 = gameActionIds();
		ActionRoomFrameInput frame{};
		frame._playerX		  = static_cast<float32>( _player.getTileX() );
		frame._playerY		  = static_cast<float32>( _player.getTileY() );
		frame._facing		  = _player.getLocomotion().getFacing();
		frame._bAttackPressed = actions.wasActionTriggered( ids._attack ) ? 1 : 0;
		frame._bDashPressed	  = actions.wasActionTriggered( ids._dash ) ? 1 : 0;

		if ( frame._bAttackPressed != 0 && _data._attackSfx.empty() == false )
			game::getService<IAudioSystem>()->play( _data._attackSfx );

		const ActionRoomFrameResult result = _actionRoom.update( deltaTime, frame );
		_actionRoom.drawDebug();

		if ( result._bDashStarted != 0 )
			applyActionDash();

		if ( result._damageToPlayer > 0 && _listParty.empty() == false )
		{
			PartyMember& leadMember = _listParty[0];
			leadMember._hp -= result._damageToPlayer;
			if ( leadMember._hp < 0 )
				leadMember._hp = 0;
			if ( leadMember._hp <= 0 )
			{
				leadMember._hp = leadMember._hpMax > 0 ? leadMember._hpMax : 1;
				PlayerDefeatedInRoomEvent defeatedEvent{};
				defeatedEvent._returnMapPath = _data._startMap;
				game::getService<EventDispatcher>()->publish( gameEventChannel(), defeatedEvent );
				_hud.setDialogue( GameStrings::get( "ui.player_down", "You were defeated... returning to town." ) );
				_actionRoom.clear();
				_zones.setClearGateLocked( false );
				beginWarpTransition( _data._startMap, -1, -1 );
				return;
			}
		}

		if ( result._bClearedThisFrame != 0 )
		{
			_zones.setClearGateLocked( false );
			_save.setFlag( "room_cleared:" + _currentMapPath, 1 );
			RoomClearedEvent clearedEvent{};
			clearedEvent._mapPath		= _currentMapPath;
			clearedEvent._bBossDefeated = result._bBossDefeated != 0 ? 1 : 0;
			game::getService<EventDispatcher>()->publish( gameEventChannel(), clearedEvent );
			ClearGateChangedEvent gateEvent{};
			gateEvent._bLocked = 0;
			game::getService<EventDispatcher>()->publish( gameEventChannel(), gateEvent );
			if ( result._bBossDefeated != 0 )
			{
				if ( _data._bossDefeatSfx.empty() == false )
					game::getService<IAudioSystem>()->play( _data._bossDefeatSfx );
				_hud.setDialogue( GameStrings::get( "ui.boss_defeated", "Belial defeated!" ) );
				_save.setFlag( "story_belial_cleared", 1 );
				SW_LOG_TRACE( "Boss cleared ??victory exit to town unlocked (east door)." );
			}
			else
			{
				_hud.setDialogue( GameStrings::get( "ui.room_cleared", "Room cleared! The door opened." ) );
			}
			SW_LOG_TRACE( "Action room cleared (boss=%#).", result._bBossDefeated != 0 ? 1 : 0 );
		}
	}

	void DemoGame::applyActionDash()
	{
		int32 deltaX{ 0 };
		int32 deltaY{ 0 };
		switch ( _player.getLocomotion().getFacing() )
		{
			case FacingDir::Up:
				deltaY = -1;
				break;
			case FacingDir::Down:
				deltaY = 1;
				break;
			case FacingDir::Left:
				deltaX = -1;
				break;
			case FacingDir::Right:
				deltaX = 1;
				break;
			default:
				break;
		}

		const int32 startX = _player.getTileX();
		const int32 startY = _player.getTileY();
		for ( int32 stepIndex = 2; stepIndex >= 1; --stepIndex )
		{
			const int32 nextX = startX + deltaX * stepIndex;
			const int32 nextY = startY + deltaY * stepIndex;
			if ( _tileMap.isWalkable( nextX, nextY ) )
			{
				_player.setPosition( nextX, nextY );
				return;
			}
		}
	}
} // namespace sw
