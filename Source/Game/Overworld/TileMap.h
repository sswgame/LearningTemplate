#pragma once
/**
 * @file TileMap.h
 * @brief Minimal HD-2D overworld tile map (grid + warp / encounter flags)
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
	struct TileWarp
	{
		int32		_tileX		  = 0;
		int32		_tileY		  = 0;
		std::string _targetMap;
		int32		_targetTileX  = 1;
		int32		_targetTileY  = 1;
	};

	/** @brief HD-2D pass 1: per-tile fake height + tint (software/debug until mesh pass). */
	struct TileVisual
	{
		uint8 _height = 0;
		uint8 _tintR  = 180;
		uint8 _tintG  = 200;
		uint8 _tintB  = 160;
	};

	class TileMap
	{
	public:
		bool loadFromXml( const std::string& assetRelativePath );
		void clear();

		int32 getWidth() const { return _width; }
		int32 getHeight() const { return _height; }
		const std::string& getName() const { return _name; }

		bool isWalkable( int32 x, int32 y ) const;
		bool isEncounterTile( int32 x, int32 y ) const;
		const TileWarp* findWarp( int32 x, int32 y ) const;
		TileVisual getTileVisual( int32 x, int32 y ) const;

		/** @brief HD-2D pass 1 debug: log a single tile's fake height/tint. */
		void debugLogTileHd2d( int32 x, int32 y ) const;

	private:
		std::string			  _name;
		int32				  _width  = 0;
		int32				  _height = 0;
		std::vector<uint8>	  _walkable;
		std::vector<uint8>	  _encounter;
		std::vector<TileVisual> _visual;
		std::vector<TileWarp> _warps;
	};
} // namespace sw
