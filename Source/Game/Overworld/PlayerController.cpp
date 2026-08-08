/**
 * @file PlayerController.cpp
 */
#include "PlayerController.h"
#include "TileMap.h"
#include "Core/Input/InputManager.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	PlayerController::PlayerController()
		: _bMoved{ 0 }
		, _bWarpPending{ 0 }
		, _bEncounterPending{ 0 }
		, _reserved{ 0 }
	{
	}

	void PlayerController::setPosition( int32 x, int32 y )
	{
		_tileX = x;
		_tileY = y;
	}

	bool PlayerController::tryStep( int32 dx, int32 dy )
	{
		const int32 nx = _tileX + dx;
		const int32 ny = _tileY + dy;
		if ( _tileMap == nullptr || _tileMap->isWalkable( nx, ny ) == false )
			return false;

		_tileX	= nx;
		_tileY	= ny;
		_bMoved = 1;

		if ( const TileWarp* warp = _tileMap->findWarp( _tileX, _tileY ) )
		{
			_pendingWarpMap		 = warp->_targetMap;
			_pendingWarpSpawnX	 = warp->_targetTileX;
			_pendingWarpSpawnY	 = warp->_targetTileY;
			_bWarpPending		 = 1;
			SW_LOG_INFO( "[PlayerController] Warp trigger → %# @ (%#,%#)", _pendingWarpMap, _pendingWarpSpawnX, _pendingWarpSpawnY );
		}
		else if ( _tileMap->isEncounterTile( _tileX, _tileY ) )
		{
			// Lightweight random: every other step on encounter tiles for stub predictability.
			static uint32 s_encCounter = 0;
			if ( ( ++s_encCounter % 3 ) == 0 )
			{
				_bEncounterPending = 1;
				SW_LOG_INFO( "[PlayerController] Wild encounter triggered at (%#,%#)", _tileX, _tileY );
			}
		}
		return true;
	}

	void PlayerController::update( float32 deltaTime, InputManager& input )
	{
		_stepCooldown -= deltaTime;
		if ( _stepCooldown > 0.0f )
			return;

		int32 dx = 0;
		int32 dy = 0;
		if ( input.wasKeyPressed( Key::W ) || input.wasKeyPressed( Key::Up ) )
			dy = -1;
		else if ( input.wasKeyPressed( Key::S ) || input.wasKeyPressed( Key::Down ) )
			dy = 1;
		else if ( input.wasKeyPressed( Key::A ) || input.wasKeyPressed( Key::Left ) )
			dx = -1;
		else if ( input.wasKeyPressed( Key::D ) || input.wasKeyPressed( Key::Right ) )
			dx = 1;

		if ( dx == 0 && dy == 0 )
			return;

		if ( tryStep( dx, dy ) )
			_stepCooldown = 0.18f;
	}

	bool PlayerController::consumeMovedFlag()
	{
		const bool v = _bMoved != 0;
		_bMoved		 = 0;
		return v;
	}

	bool PlayerController::consumeWarpRequest( std::string& outMapPath, int32& outSpawnX, int32& outSpawnY )
	{
		if ( _bWarpPending == 0 )
			return false;
		outMapPath		= _pendingWarpMap;
		outSpawnX		= _pendingWarpSpawnX;
		outSpawnY		= _pendingWarpSpawnY;
		_pendingWarpMap.clear();
		_bWarpPending = 0;
		return true;
	}

	bool PlayerController::consumeEncounterRequest()
	{
		const bool v	   = _bEncounterPending != 0;
		_bEncounterPending = 0;
		return v;
	}
} // namespace sw
