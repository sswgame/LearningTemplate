#include "pch.h"

#include "Engine/Audio/IAudioSystem.h"

#include "GameFramework/Data/GameStrings.h"
#include "GameFramework/Kits/Overworld/ZoneRuntime.h"

#include "Games/Demo/DemoGame.h"

#include "RuntimeAPI/GameService.h"

namespace sw
{
	bool DemoGame::loadMap( string_view mapPath, int32 spawnX, int32 spawnY )
	{
		if ( _tileMap.loadFromXml( mapPath ) == false )
		{
			SW_LOG_WARNING( "[DemoGame] Map load failed: %#", mapPath );
			return false;
		}
		_currentMapPath = mapPath;
		_player.setTileMap( &_tileMap );
		_player.setEncounterRate( _data._encounterRate );
		_zones.setFromMap( mapPath, _tileMap.getName(), _tileMap.getWidth(), _tileMap.getHeight(), _tileMap.getRole() );

		if ( spawnX < 0 || spawnY < 0 )
		{
			spawnX = _tileMap.getSpawnX();
			spawnY = _tileMap.getSpawnY();
		}
		if ( _tileMap.isWalkable( spawnX, spawnY ) == false )
		{
			spawnX = _tileMap.getSpawnX();
			spawnY = _tileMap.getSpawnY();
			if ( _tileMap.isWalkable( spawnX, spawnY ) == false )
			{
				spawnX = 1;
				spawnY = 1;
			}
		}
		_player.setPosition( spawnX, spawnY );
		syncActionRoomForZone();
		playZoneBgm();
		SW_LOG_INFO( "[DemoGame] Overworld map ready: '%#' spawn=(%#,%#) zoneRole=%# gate=%# action=%#",
					 _tileMap.getName(), spawnX, spawnY,
					 static_cast<int32>( _zones.getActiveRole() ),
					 _zones.isClearGateLocked() ? 1 : 0,
					 _actionRoom.isActive() ? 1 : 0 );

		requestSceneForMap( mapPath );
		_tileMap.debugLogTileHd2d( spawnX, spawnY );
		return true;
	}

	void DemoGame::requestSceneForMap( string_view /*mapPath*/ )
	{
		const string& scenePath = _tileMap.getScenePath();
		if ( scenePath.empty() == false )
			game::getService<SceneManager>()->requestLoadAsync( scenePath );
		else if ( _data._battleScene.empty() == false && _currentMapPath == _data._battleMap )
			game::getService<SceneManager>()->requestLoadAsync( _data._battleScene );
	}

	void DemoGame::syncActionRoomForZone()
	{
		const ZoneRole role			= _zones.getActiveRole();
		const string   clearedFlag	= "room_cleared:" + _currentMapPath;
		const bool	   alreadyClear = _save.getFlag( clearedFlag, 0 ) != 0;

		if ( role == ZoneRole::Boss )
		{
			if ( alreadyClear )
			{
				_actionRoom.clear();
				_zones.setClearGateLocked( false );
				_hud.setDialogue( GameStrings::get( "ui.boss_defeated", "Belial defeated!" ) );
			}
			else
			{
				_actionRoom.beginBoss();
				_hud.setDialogue( GameStrings::get( "ui.dungeon_hint",
													"J=Attack  L/Shift=Dash ??clear the room to open the door." ) );
			}
		}
		else if ( role == ZoneRole::Dungeon )
		{
			if ( alreadyClear )
			{
				_actionRoom.clear();
				_zones.setClearGateLocked( false );
				_hud.setDialogue( GameStrings::get( "ui.room_cleared", "Room cleared! The door opened." ) );
			}
			else if ( _currentMapPath.find( "entrance" ) != string::npos )
			{
				_actionRoom.beginEntrance();
				_hud.setDialogue( GameStrings::get( "ui.dungeon_hint",
													"J=Attack  L/Shift=Dash ??clear the room to open the door." ) );
			}
			else
			{
				_actionRoom.beginHall();
				_hud.setDialogue( GameStrings::get( "ui.dungeon_hint",
													"J=Attack  L/Shift=Dash ??clear the room to open the door." ) );
			}
		}
		else
			_actionRoom.clear();
	}

	void DemoGame::playZoneBgm()
	{
		const ZoneRole role = _zones.getActiveRole();
		if ( role == ZoneRole::Boss && _data._bossBgm.empty() == false )
			game::getService<IAudioSystem>()->playMusic( _data._bossBgm );
		else if ( role == ZoneRole::Dungeon && _data._dungeonBgm.empty() == false )
			game::getService<IAudioSystem>()->playMusic( _data._dungeonBgm );
		else
			game::getService<IAudioSystem>()->stopMusic();
	}

	bool DemoGame::isActionZone() const
	{
		const ZoneRole role = _zones.getActiveRole();
		return role == ZoneRole::Dungeon || role == ZoneRole::Boss;
	}
} // namespace sw
