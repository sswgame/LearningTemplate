/**
 * @file PlayerController.cpp
 */
#include "PlayerController.h"
#include "TileMap.h"
#include "Core/Input/ActionMap.h"
#include "Core/Input/InputManager.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	PlayerController::PlayerController()
		: _bMoved{ 0 }
		, _bWarpPending{ 0 }
		, _bEncounterPending{ 0 }
		, _bInteractPending{ 0 }
		, _bInputEnabled{ 1 }
		, _reserved{ 0 }
	{
	}

	void PlayerController::setPosition( int32 x, int32 y )
	{
		_tileX = x;
		_tileY = y;
	}

	void PlayerController::getFacingTile( int32& outX, int32& outY ) const
	{
		outX = _tileX;
		outY = _tileY;
		switch ( _loco.getFacing() )
		{
		case FacingDir::Up:	   --outY; break;
		case FacingDir::Down:  ++outY; break;
		case FacingDir::Left:  --outX; break;
		case FacingDir::Right: ++outX; break;
		}
	}

	bool PlayerController::tryStep( int32 dx, int32 dy )
	{
		const int32 nx = _tileX + dx;
		const int32 ny = _tileY + dy;
		if ( _tileMap == nullptr || _tileMap->isWalkable( nx, ny ) == false )
			return false;

		_loco.setFacingFromDelta( dx, dy );
		_loco.notifyStepStarted();
		_tileX	= nx;
		_tileY	= ny;
		_bMoved = 1;

		if ( const TileWarp* warp = _tileMap->findWarp( _tileX, _tileY ) )
		{
			_pendingWarpMap	   = warp->_targetMap;
			_pendingWarpSpawnX = warp->_targetTileX;
			_pendingWarpSpawnY = warp->_targetTileY;
			_bWarpPending	   = 1;
			SW_LOG_INFO( "[PlayerController] Warp trigger → %# @ (%#,%#)", _pendingWarpMap, _pendingWarpSpawnX, _pendingWarpSpawnY );
		}
		else if ( _tileMap->isEncounterTile( _tileX, _tileY ) )
		{
			static uint32 s_encCounter = 0;
			const uint32  period	   = _encounterRate > 0.01f ? static_cast<uint32>( 1.0f / _encounterRate ) : 3u;
			if ( ( ++s_encCounter % ( period < 1 ? 3u : period ) ) == 0 )
			{
				_bEncounterPending = 1;
				SW_LOG_INFO( "[PlayerController] Wild encounter at (%#,%#)", _tileX, _tileY );
			}
		}
		return true;
	}

	void PlayerController::update( float32 deltaTime, InputManager& input )
	{
		_loco.update( deltaTime );
		_stepCooldown -= deltaTime;

		if ( _bInputEnabled == 0 )
			return;
		if ( _loco.canAcceptMoveInput() == false )
			return;
		if ( _stepCooldown > 0.0f )
			return;

		static ActionMap s_actions;
		static bool		 s_bound = false;
		if ( s_bound == false )
		{
			s_actions.bindDefaults();
			s_bound = true;
		}
		s_actions.setInputManager( &input );
		s_actions.setGamepad( input.getGamepad() );

		if ( s_actions.wasActionPressed( Action::Interact ) )
		{
			_loco.beginInteract();
			_bInteractPending = 1;
			return;
		}

		int32 dx = 0;
		int32 dy = 0;
		if ( s_actions.wasActionPressed( Action::MoveUp ) || s_actions.isActionDown( Action::MoveUp ) )
			dy = -1;
		else if ( s_actions.wasActionPressed( Action::MoveDown ) || s_actions.isActionDown( Action::MoveDown ) )
			dy = 1;
		else if ( s_actions.wasActionPressed( Action::MoveLeft ) || s_actions.isActionDown( Action::MoveLeft ) )
			dx = -1;
		else if ( s_actions.wasActionPressed( Action::MoveRight ) || s_actions.isActionDown( Action::MoveRight ) )
			dx = 1;

		if ( dx == 0 && dy == 0 )
			return;

		if ( tryStep( dx, dy ) )
		{
			_stepCooldown = 0.18f;
			_loco.notifyStepFinished();
		}
		else
		{
			_loco.setFacingFromDelta( dx, dy );
		}
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
		outMapPath = _pendingWarpMap;
		outSpawnX  = _pendingWarpSpawnX;
		outSpawnY  = _pendingWarpSpawnY;
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

	bool PlayerController::consumeInteractRequest()
	{
		const bool v	  = _bInteractPending != 0;
		_bInteractPending = 0;
		return v;
	}
} // namespace sw
