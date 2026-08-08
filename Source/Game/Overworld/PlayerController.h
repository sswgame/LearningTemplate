#pragma once
/**
 * @file PlayerController.h
 * @brief Tile-step player movement + locomotion FSM (Playing-only input)
 */

#include "Core/Common/Types.h"
#include "PlayerLocomotion.h"

namespace sw
{
	class TileMap;
	class InputManager;

	class PlayerController
	{
	public:
		PlayerController();

		void setTileMap( TileMap* map ) { _tileMap = map; }
		void setPosition( int32 x, int32 y );
		void setInputEnabled( bool enabled ) { _bInputEnabled = enabled ? 1 : 0; }
		void update( float32 deltaTime, InputManager& input );

		int32				getTileX() const { return _tileX; }
		int32				getTileY() const { return _tileY; }
		const PlayerLocomotion& getLocomotion() const { return _loco; }
		bool				consumeMovedFlag();
		bool				consumeWarpRequest( std::string& outMapPath, int32& outSpawnX, int32& outSpawnY );
		bool				consumeEncounterRequest();
		bool				consumeInteractRequest();

		void getFacingTile( int32& outX, int32& outY ) const;

	private:
		bool tryStep( int32 dx, int32 dy );

		TileMap*		 _tileMap = nullptr;
		PlayerLocomotion _loco;
		int32			 _tileX			= 1;
		int32			 _tileY			= 1;
		float32			 _stepCooldown	= 0.0f;
		float32			 _encounterRate = 0.33f;
		uint8			 _bMoved : 1;
		uint8			 _bWarpPending : 1;
		uint8			 _bEncounterPending : 1;
		uint8			 _bInteractPending : 1;
		uint8			 _bInputEnabled : 1;
		[[maybe_unused]] uint8 _reserved : 3;
		std::string		 _pendingWarpMap;
		int32			 _pendingWarpSpawnX = 1;
		int32			 _pendingWarpSpawnY = 1;
	};
} // namespace sw
