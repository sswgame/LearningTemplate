#pragma once
/**
 * @file PlayerController.h
 * @brief Tile-step player movement driven by InputManager
 */

#include "Core/Common/Types.h"

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
		void update( float32 deltaTime, InputManager& input );

		int32 getTileX() const { return _tileX; }
		int32 getTileY() const { return _tileY; }
		bool  consumeMovedFlag();
		bool  consumeWarpRequest( std::string& outMapPath, int32& outSpawnX, int32& outSpawnY );
		bool  consumeEncounterRequest();

	private:
		bool tryStep( int32 dx, int32 dy );

		TileMap*	_tileMap = nullptr;
		int32		_tileX	 = 1;
		int32		_tileY	 = 1;
		float32		_stepCooldown = 0.0f;
		uint8		_bMoved : 1;
		uint8		_bWarpPending : 1;
		uint8		_bEncounterPending : 1;
		[[maybe_unused]] uint8 _reserved : 5;
		std::string _pendingWarpMap;
		int32		_pendingWarpSpawnX = 1;
		int32		_pendingWarpSpawnY = 1;
	};
} // namespace sw
