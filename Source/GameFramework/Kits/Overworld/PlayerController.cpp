#include "pch.h"

#include "GameFramework/Kits/Overworld/PlayerController.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/InputManager.h"

#include "GameFramework/Input/GameActions.h"
#include "GameFramework/Kits/Overworld/TileMap.h"

namespace sw
{
	SW_LOG_CALLER( "PlayerController" );

	PlayerController::PlayerController()
		: _pTileMap{ nullptr }
		, _pActionMap{ nullptr }
		, _pendingWarpMap{}
		, _loco{}
		, _tileX{ 1 }
		, _tileY{ 1 }
		, _pendingWarpSpawnX{ 1 }
		, _pendingWarpSpawnY{ 1 }
		, _encounterStepCounter{ 0 }
		, _stepCooldown{ 0.0f }
		, _encounterRate{ 0.33f }
		, _bMoved{ SW_FALSE }
		, _bWarpPending{ SW_FALSE }
		, _bEncounterPending{ SW_FALSE }
		, _bInteractPending{ SW_FALSE }
		, _bInputEnabled{ SW_TRUE }
		, _reserved{ 0 }
	{
	}

	void PlayerController::setPosition( int32 x, int32 y )
	{
		_tileX = x;
		_tileY = y;
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

		ActionMap* pActionMap = _pActionMap != nullptr ? _pActionMap : &gameActions();
		if ( pActionMap->getInputManager() != &input )
			pActionMap->setInputManager( &input );

		const GameActionIds& ids = gameActionIds();

		if ( pActionMap->wasActionTriggered( ids._interact ) )
		{
			_loco.beginInteract();
			_bInteractPending = 1;
			return;
		}

		int32 deltaX{ 0 };
		int32 deltaY{ 0 };
		// Move* 바인딩은 InputMap에서 trigger=Down — wasActionTriggered는 홀드와 같습니다.
		if ( pActionMap->wasActionTriggered( ids._moveUp ) )
			deltaY = -1;
		else if ( pActionMap->wasActionTriggered( ids._moveDown ) )
			deltaY = 1;
		else if ( pActionMap->wasActionTriggered( ids._moveLeft ) )
			deltaX = -1;
		else if ( pActionMap->wasActionTriggered( ids._moveRight ) )
			deltaX = 1;

		if ( deltaX == 0 && deltaY == 0 )
			return;

		if ( tryStep( deltaX, deltaY ) )
		{
			_stepCooldown = 0.18f;
			_loco.notifyStepFinished();
		}
		else
		{
			_loco.setFacingFromDelta( deltaX, deltaY );
		}
	}

	bool PlayerController::consumeMovedFlag()
	{
		const bool v = _bMoved != 0;
		_bMoved		 = 0;
		return v;
	}

	bool PlayerController::consumeWarpRequest( string& outMapPath, int32& outSpawnX, int32& outSpawnY )
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

	void PlayerController::getFacingTile( int32& outX, int32& outY ) const
	{
		outX = _tileX;
		outY = _tileY;
		switch ( _loco.getFacing() )
		{
			case FacingDir::Up:
				--outY;
				break;
			case FacingDir::Down:
				++outY;
				break;
			case FacingDir::Left:
				--outX;
				break;
			case FacingDir::Right:
				++outX;
				break;
			default:
				break;
		}
	}

	bool PlayerController::tryStep( int32 deltaX, int32 deltaY )
	{
		const int32 nextX = _tileX + deltaX;
		const int32 nextY = _tileY + deltaY;
		if ( _pTileMap == nullptr || _pTileMap->isWalkable( nextX, nextY ) == false )
			return false;

		_loco.setFacingFromDelta( deltaX, deltaY );
		_loco.notifyStepStarted();
		_tileX	= nextX;
		_tileY	= nextY;
		_bMoved = 1;

		const TileWarp* pWarp = _pTileMap->findWarp( _tileX, _tileY );
		if ( pWarp != nullptr )
		{
			_pendingWarpMap	   = pWarp->_targetMap;
			_pendingWarpSpawnX = pWarp->_targetTileX;
			_pendingWarpSpawnY = pWarp->_targetTileY;
			_bWarpPending	   = 1;
			SW_LOG_TRACE( "Warp trigger → %# @ (%#,%#)", _pendingWarpMap, _pendingWarpSpawnX, _pendingWarpSpawnY );
		}
		else if ( _pTileMap->isEncounterTile( _tileX, _tileY ) )
		{
			const uint32 period = _encounterRate > 0.01f ? static_cast<uint32>( 1.0f / _encounterRate ) : 3u;
			if ( ( ++_encounterStepCounter % ( period < 1 ? 3u : period ) ) == 0 )
			{
				_bEncounterPending = 1;
				SW_LOG_TRACE( "Wild encounter at (%#,%#)", _tileX, _tileY );
			}
		}
		return true;
	}
} // namespace sw
