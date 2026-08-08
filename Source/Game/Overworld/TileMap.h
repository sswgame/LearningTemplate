#pragma once
/**
 * @file TileMap.h
 * @brief HD-2D overworld tile map (grid + TileFlags + warp / encounter)
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
	enum class TileFlags : uint8
	{
		None		 = 0,
		Walkable	 = 1 << 0,
		Encounter	 = 1 << 1,
		Warp		 = 1 << 2,
		Solid		 = 1 << 3, // blocked (inverse of Walkable for authoring clarity)
		PassThrough	 = 1 << 4  // ledge / one-way hint (still walkable)
	};

	inline TileFlags operator|( TileFlags a, TileFlags b )
	{
		return static_cast<TileFlags>( static_cast<uint8>( a ) | static_cast<uint8>( b ) );
	}
	inline TileFlags operator&( TileFlags a, TileFlags b )
	{
		return static_cast<TileFlags>( static_cast<uint8>( a ) & static_cast<uint8>( b ) );
	}
	inline bool hasTileFlag( TileFlags flags, TileFlags bit )
	{
		return ( static_cast<uint8>( flags ) & static_cast<uint8>( bit ) ) != 0;
	}

	struct TileWarp
	{
		int32		_tileX		 = 0;
		int32		_tileY		 = 0;
		std::string _targetMap;
		int32		_targetTileX = 1;
		int32		_targetTileY = 1;
		std::string _pairId; // optional WarpDoor pair id
	};

	/** @brief HD-2D pass 1: per-tile fake height + tint (software/debug until mesh pass). */
	struct TileVisual
	{
		uint8 _height = 0;
		uint8 _tintR  = 180;
		uint8 _tintG  = 200;
		uint8 _tintB  = 160;
		uint8 _atlasId = 0;
	};

	enum class TilePaintLayer : uint8
	{
		Visual = 0,
		Walkable,
		Encounter,
		Warp,
		PassThrough
	};

	class TileMap
	{
	public:
		bool loadFromXml( const std::string& assetRelativePath );
		bool saveToXml( const std::string& assetRelativePath ) const;
		void clear();
		void resize( int32 width, int32 height );

		int32			   getWidth() const { return _width; }
		int32			   getHeight() const { return _height; }
		const std::string& getName() const { return _name; }
		void			   setName( const std::string& name ) { _name = name; }
		const std::string& getSourcePath() const { return _sourcePath; }

		bool			isWalkable( int32 x, int32 y ) const;
		bool			isEncounterTile( int32 x, int32 y ) const;
		bool			isPassThrough( int32 x, int32 y ) const;
		bool			isSolid( int32 x, int32 y ) const;
		TileFlags		getFlags( int32 x, int32 y ) const;
		const TileWarp* findWarp( int32 x, int32 y ) const;
		TileVisual		getTileVisual( int32 x, int32 y ) const;

		void setWalkable( int32 x, int32 y, bool v );
		void setEncounter( int32 x, int32 y, bool v );
		void setPassThrough( int32 x, int32 y, bool v );
		void setTileVisual( int32 x, int32 y, const TileVisual& v );
		void setOrUpdateWarp( const TileWarp& warp );
		void removeWarp( int32 x, int32 y );
		void paintEdgeWarpPreset( int32 edge /*0=N,1=E,2=S,3=W*/, const std::string& targetMap, int32 tx, int32 ty );

		void debugLogTileHd2d( int32 x, int32 y ) const;

	private:
		bool inBounds( int32 x, int32 y ) const;
		size_t indexOf( int32 x, int32 y ) const { return static_cast<size_t>( y * _width + x ); }

		std::string				_name;
		std::string				_sourcePath;
		int32					_width	= 0;
		int32					_height = 0;
		std::vector<uint8>		_walkable;
		std::vector<uint8>		_encounter;
		std::vector<uint8>		_passThrough;
		std::vector<TileVisual> _visual;
		std::vector<TileWarp>	_warps;
	};
} // namespace sw
