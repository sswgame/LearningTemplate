#include "pch.h"

#include "GameFramework/Kits/Overworld/TileMap.h"

#include "Engine/Utility/Xml/TileMapXml.h"

namespace sw
{
	SW_LOG_CALLER( "TileMap" );

	TileMap::TileMap()
		: _name{}
		, _sourcePath{}
		, _scenePath{}
		, _role{}
		, _width{ 0 }
		, _height{ 0 }
		, _spawnX{ 1 }
		, _spawnY{ 1 }
		, _walkableList{}
		, _encounterList{}
		, _passThroughList{}
		, _visualList{}
		, _warpList{}
		, _encounterEntryList{}
	{
	}

	bool TileMap::loadFromXml( string_view assetRelativePath )
	{
		clear();
		_sourcePath = assetRelativePath;

		TileMapXmlData xmlData{};
		if ( xmlData.load( assetRelativePath ) == false )
			return false;

		_name			 = xmlData._name;
		_scenePath		 = xmlData._scenePath;
		_role			 = xmlData._role;
		_width			 = xmlData._width;
		_height			 = xmlData._height;
		_spawnX			 = xmlData._spawnX;
		_spawnY			 = xmlData._spawnY;
		_walkableList	 = std::move( xmlData._walkableList );
		_encounterList	 = std::move( xmlData._encounterList );
		_passThroughList = std::move( xmlData._passThroughList );
		_visualList.clear();
		_visualList.reserve( xmlData._visualList.size() );
		for ( const TileMapXmlData::Visual& src : xmlData._visualList )
		{
			TileVisual dst{};
			dst._height	 = src._height;
			dst._tintR	 = src._tintR;
			dst._tintG	 = src._tintG;
			dst._tintB	 = src._tintB;
			dst._atlasId = src._atlasId;
			_visualList.push_back( dst );
		}
		_warpList.clear();
		_warpList.reserve( xmlData._warpList.size() );
		for ( const TileMapXmlData::Warp& src : xmlData._warpList )
		{
			TileWarp dst{};
			dst._tileX		 = src._tileX;
			dst._tileY		 = src._tileY;
			dst._targetMap	 = src._targetMap;
			dst._targetTileX = src._targetTileX;
			dst._targetTileY = src._targetTileY;
			dst._pairId		 = src._pairId;
			_warpList.push_back( std::move( dst ) );
		}
		_encounterEntryList.clear();
		_encounterEntryList.reserve( xmlData._encounterEntryList.size() );
		for ( const TileMapXmlData::Encounter& src : xmlData._encounterEntryList )
		{
			TileEncounterEntry dst{};
			dst._speciesId = src._speciesId;
			dst._weight	   = src._weight;
			_encounterEntryList.push_back( std::move( dst ) );
		}
		return true;
	}

	bool TileMap::saveToXml( string_view assetRelativePath ) const
	{
		TileMapXmlData xmlData{};
		xmlData._name			 = _name;
		xmlData._sourcePath		 = assetRelativePath;
		xmlData._scenePath		 = _scenePath;
		xmlData._role			 = _role;
		xmlData._width			 = _width;
		xmlData._height			 = _height;
		xmlData._spawnX			 = _spawnX;
		xmlData._spawnY			 = _spawnY;
		xmlData._walkableList	 = _walkableList;
		xmlData._encounterList	 = _encounterList;
		xmlData._passThroughList = _passThroughList;
		xmlData._visualList.reserve( _visualList.size() );
		for ( const TileVisual& src : _visualList )
		{
			TileMapXmlData::Visual dst{};
			dst._height	 = src._height;
			dst._tintR	 = src._tintR;
			dst._tintG	 = src._tintG;
			dst._tintB	 = src._tintB;
			dst._atlasId = src._atlasId;
			xmlData._visualList.push_back( dst );
		}
		xmlData._warpList.reserve( _warpList.size() );
		for ( const TileWarp& src : _warpList )
		{
			TileMapXmlData::Warp dst{};
			dst._tileX		 = src._tileX;
			dst._tileY		 = src._tileY;
			dst._targetMap	 = src._targetMap;
			dst._targetTileX = src._targetTileX;
			dst._targetTileY = src._targetTileY;
			dst._pairId		 = src._pairId;
			xmlData._warpList.push_back( dst );
		}
		xmlData._encounterEntryList.reserve( _encounterEntryList.size() );
		for ( const TileEncounterEntry& src : _encounterEntryList )
		{
			TileMapXmlData::Encounter dst{};
			dst._speciesId = src._speciesId;
			dst._weight	   = src._weight;
			xmlData._encounterEntryList.push_back( dst );
		}
		return xmlData.save( assetRelativePath );
	}

	void TileMap::clear()
	{
		_name.clear();
		_sourcePath.clear();
		_scenePath.clear();
		_role.clear();
		_width	= 0;
		_height = 0;
		_spawnX = 1;
		_spawnY = 1;
		_walkableList.clear();
		_encounterList.clear();
		_passThroughList.clear();
		_visualList.clear();
		_warpList.clear();
		_encounterEntryList.clear();
	}

	void TileMap::resize( int32 width, int32 height )
	{
		if ( width <= 0 || height <= 0 )
			return;
		_width			   = width;
		_height			   = height;
		const size_t count = static_cast<size_t>( _width * _height );
		_walkableList.assign( count, 1 );
		_encounterList.assign( count, 0 );
		_passThroughList.assign( count, 0 );
		_visualList.assign( count, TileVisual{} );
		_warpList.clear();
		_encounterEntryList.clear();
	}

	string TileMap::pickEncounterSpeciesId() const
	{
		if ( _encounterEntryList.empty() )
			return {};

		float32 total{ 0.0f };
		for ( const TileEncounterEntry& entry : _encounterEntryList )
		{
			total += entry._weight > 0.0f ? entry._weight : 0.0f;
		}
		if ( total <= 0.0f )
			return _encounterEntryList[0]._speciesId;

		static size_t s_pick{ 0 };
		const size_t  idx = s_pick++ % _encounterEntryList.size();
		return _encounterEntryList[idx]._speciesId;
	}

	bool TileMap::isWalkable( int32 x, int32 y ) const
	{
		if ( inBounds( x, y ) == false )
			return false;
		return _walkableList[indexOf( x, y )] != 0;
	}

	bool TileMap::isEncounterTile( int32 x, int32 y ) const
	{
		if ( inBounds( x, y ) == false )
			return false;
		return _encounterList[indexOf( x, y )] != 0;
	}

	bool TileMap::isPassThrough( int32 x, int32 y ) const
	{
		if ( inBounds( x, y ) == false )
			return false;
		return _passThroughList[indexOf( x, y )] != 0;
	}

	bool TileMap::isSolid( int32 x, int32 y ) const
	{
		return isWalkable( x, y ) == false;
	}

	TileFlags TileMap::getFlags( int32 x, int32 y ) const
	{
		TileFlags f = TileFlags::None;
		if ( inBounds( x, y ) == false )
			return TileFlags::Solid;
		if ( _walkableList[indexOf( x, y )] != 0 )
			f = f | TileFlags::Walkable;
		else
			f = f | TileFlags::Solid;
		if ( _encounterList[indexOf( x, y )] != 0 )
			f = f | TileFlags::Encounter;
		if ( _passThroughList[indexOf( x, y )] != 0 )
			f = f | TileFlags::PassThrough;
		if ( findWarp( x, y ) != nullptr )
			f = f | TileFlags::Warp;
		return f;
	}

	const TileWarp* TileMap::findWarp( int32 x, int32 y ) const
	{
		for ( const TileWarp& warp : _warpList )
		{
			if ( warp._tileX == x && warp._tileY == y )
				return &warp;
		}
		return nullptr;
	}

	TileVisual TileMap::getTileVisual( int32 x, int32 y ) const
	{
		if ( inBounds( x, y ) == false )
			return {};
		return _visualList[indexOf( x, y )];
	}

	void TileMap::setWalkable( int32 x, int32 y, bool bWalkable )
	{
		if ( inBounds( x, y ) )
			_walkableList[indexOf( x, y )] = bWalkable ? 1 : 0;
	}

	void TileMap::setEncounter( int32 x, int32 y, bool bEncounter )
	{
		if ( inBounds( x, y ) )
			_encounterList[indexOf( x, y )] = bEncounter ? 1 : 0;
	}

	void TileMap::setPassThrough( int32 x, int32 y, bool bPassThrough )
	{
		if ( inBounds( x, y ) )
			_passThroughList[indexOf( x, y )] = bPassThrough ? 1 : 0;
	}

	void TileMap::setTileVisual( int32 x, int32 y, const TileVisual& visual )
	{
		if ( inBounds( x, y ) )
			_visualList[indexOf( x, y )] = visual;
	}

	void TileMap::setOrUpdateWarp( const TileWarp& warp )
	{
		for ( TileWarp& existingWarp : _warpList )
		{
			if ( existingWarp._tileX == warp._tileX && existingWarp._tileY == warp._tileY )
			{
				existingWarp = warp;
				return;
			}
		}
		_warpList.push_back( warp );
	}

	void TileMap::removeWarp( int32 x, int32 y )
	{
		_warpList.erase( std::remove_if( _warpList.begin(), _warpList.end(),
										 [x, y]( const TileWarp& warp )
		{ return warp._tileX == x && warp._tileY == y; } ),
						 _warpList.end() );
	}

	void TileMap::paintEdgeWarpPreset( int32 edge, string_view targetMap, int32 tx, int32 ty )
	{
		if ( _width <= 0 || _height <= 0 || targetMap.empty() )
			return;

		auto stamp = [&]( int32 tileX, int32 tileY )
		{
			setWalkable( tileX, tileY, true );
			TileWarp warp{};
			warp._tileX		  = tileX;
			warp._tileY		  = tileY;
			warp._targetMap	  = targetMap;
			warp._targetTileX = tx;
			warp._targetTileY = ty;
			setOrUpdateWarp( warp );
		};

		switch ( edge )
		{
			case 0: // N
				for ( int32 tileX = 0; tileX < _width; ++tileX )
				{
					stamp( tileX, 0 );
				}
				break;
			case 1: // E
				for ( int32 tileY = 0; tileY < _height; ++tileY )
				{
					stamp( _width - 1, tileY );
				}
				break;
			case 2: // S
				for ( int32 tileX = 0; tileX < _width; ++tileX )
				{
					stamp( tileX, _height - 1 );
				}
				break;
			case 3: // W
				for ( int32 tileY = 0; tileY < _height; ++tileY )
				{
					stamp( 0, tileY );
				}
				break;
			default:
				break;
		}
	}

	void TileMap::debugLogTileHd2d( int32 x, int32 y ) const
	{
		const TileVisual tileVisual = getTileVisual( x, y );
		SW_LOG_TRACE( "tile (%#,%#) h=%# tint=(%#,%#,%#) flags walk=%# enc=%# pt=%#",
					  x, y, tileVisual._height, tileVisual._tintR, tileVisual._tintG, tileVisual._tintB,
					  isWalkable( x, y ) ? 1 : 0, isEncounterTile( x, y ) ? 1 : 0, isPassThrough( x, y ) ? 1 : 0 );
	}

	bool TileMap::inBounds( int32 x, int32 y ) const
	{
		return x >= 0 && y >= 0 && x < _width && y < _height;
	}
} // namespace sw
