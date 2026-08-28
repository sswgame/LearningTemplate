#include "pch.h"

#include "GameFramework/Kits/Overworld/TileMap.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Resource/ResourceUtil.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include <algorithm>

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

		XmlDocument doc;
		string		absPath;
		if ( doc.loadResource( assetRelativePath, &absPath ) == false )
		{
			SW_LOG_ERROR( "Not found: %#", assetRelativePath );
			return false;
		}

		XmlNode root = doc.root( "TileMap" );
		if ( root.isValid() == false )
		{
			SW_LOG_ERROR( "Missing <TileMap>: %#", absPath );
			return false;
		}

		root.takeChildText( "name", _name );
		const utf8* pWidthText = root.childText( "width" );
		if ( pWidthText != nullptr )
			_width = StringUtil::atoi( pWidthText );
		const utf8* pHeightText = root.childText( "height" );
		if ( pHeightText != nullptr )
			_height = StringUtil::atoi( pHeightText );
		root.takeChildText( "scene", _scenePath );
		root.takeChildText( "role", _role );
		XmlNode spawn = root.child( "spawn" );
		if ( spawn.isValid() )
		{
			_spawnX = spawn.attrInt( "x", _spawnX );
			_spawnY = spawn.attrInt( "y", _spawnY );
		}

		if ( _width <= 0 || _height <= 0 )
		{
			_width	= 8;
			_height = 8;
		}

		const size_t count = static_cast<size_t>( _width * _height );
		_walkableList.assign( count, 1 );
		_encounterList.assign( count, 0 );
		_passThroughList.assign( count, 0 );
		_visualList.assign( count, TileVisual{} );

		XmlNode tiles = root.child( "tiles" );
		if ( tiles.isValid() )
		{
			int32 index{ 0 };
			for ( XmlNode tileNode = tiles.child( "t" ); tileNode && index < static_cast<int32>( count ); tileNode = tileNode.next( "t" ), ++index )
			{
				const utf8*	 pText			   = tileNode.text();
				const size_t elementIndex	   = static_cast<size_t>( index );
				_walkableList[elementIndex]	   = ( pText == nullptr || pText[0] != '0' ) ? 1 : 0;
				_encounterList[elementIndex]   = tileNode.attrInt( "enc", 0 ) != 0 ? 1 : 0;
				_passThroughList[elementIndex] = tileNode.attrInt( "pt", 0 ) != 0 ? 1 : 0;

				TileVisual tileVisual{};
				if ( tileNode.attr( "h" ) != nullptr )
					tileVisual._height = static_cast<uint8>( tileNode.attrInt( "h", 0 ) );
				else
					tileVisual._height = _encounterList[elementIndex] != 0 ? 2 : ( _walkableList[elementIndex] != 0 ? 1 : 0 );

				if ( tileNode.attr( "atlas" ) != nullptr )
					tileVisual._atlasId = static_cast<uint8>( tileNode.attrInt( "atlas", 0 ) );

				const bool hasTint = tileNode.attr( "tr" ) != nullptr || tileNode.attr( "tg" ) != nullptr || tileNode.attr( "tb" ) != nullptr;
				if ( hasTint )
				{
					tileVisual._tintR = static_cast<uint8>( tileNode.attrInt( "tr", 255 ) );
					tileVisual._tintG = static_cast<uint8>( tileNode.attrInt( "tg", 255 ) );
					tileVisual._tintB = static_cast<uint8>( tileNode.attrInt( "tb", 255 ) );
				}
				else
				{
					if ( _encounterList[elementIndex] != 0 )
					{
						tileVisual._tintR = 120;
						tileVisual._tintG = 190;
						tileVisual._tintB = 90;
					}
					else if ( _walkableList[elementIndex] == 0 )
					{
						tileVisual._tintR = 80;
						tileVisual._tintG = 80;
						tileVisual._tintB = 90;
					}
					else if ( _passThroughList[elementIndex] != 0 )
					{
						tileVisual._tintR = 160;
						tileVisual._tintG = 170;
						tileVisual._tintB = 200;
					}
				}
				_visualList[elementIndex] = tileVisual;
			}
		}

		XmlNode warps = root.child( "warps" );
		if ( warps.isValid() )
		{
			for ( XmlNode warpNode = warps.child( "warp" ); warpNode; warpNode = warpNode.next( "warp" ) )
			{
				TileWarp warp{};
				warp._tileX		 = warpNode.attrInt( "x", 0 );
				warp._tileY		 = warpNode.attrInt( "y", 0 );
				const utf8* pMap = warpNode.attr( "map" );
				if ( pMap != nullptr )
					warp._targetMap = pMap;
				warp._targetTileX = warpNode.attrInt( "tx", 0 );
				warp._targetTileY = warpNode.attrInt( "ty", 0 );
				const utf8* pPair = warpNode.attr( "pair" );
				if ( pPair != nullptr )
					warp._pairId = pPair;
				_warpList.push_back( std::move( warp ) );
			}
		}

		XmlNode encounters = root.child( "encounters" );
		if ( encounters.isValid() )
		{
			for ( XmlNode encNode = encounters.child( "e" ); encNode; encNode = encNode.next( "e" ) )
			{
				TileEncounterEntry entry{};
				const utf8*		   pId = encNode.attr( "id" );
				if ( pId != nullptr )
					entry._speciesId = pId;
				entry._weight = encNode.attrFloat( "weight", 0.f );
				if ( entry._speciesId.empty() == false )
					_encounterEntryList.push_back( std::move( entry ) );
			}
		}

		SW_LOG_INFO( "Loaded '%#' (%#x%#) scene=%# role=%# encounters=%#",
					 _name, _width, _height, _scenePath, _role, static_cast<uint32>( _encounterEntryList.size() ) );
		return true;
	}

	bool TileMap::saveToXml( string_view assetRelativePath ) const
	{
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		StringBuilder<constant::kMaxBuffer4096> sb;
		sb.appendFormat( "<TileMap>\n" );
		sb.appendFormat( "  <name>%#</name>\n", _name.empty() ? "Untitled" : _name.c_str() );
		sb.appendFormat( "  <width>%#</width>\n", _width );
		sb.appendFormat( "  <height>%#</height>\n", _height );
		if ( _scenePath.empty() == false )
			sb.appendFormat( "  <scene>%#</scene>\n", _scenePath.c_str() );
		if ( _role.empty() == false )
			sb.appendFormat( "  <role>%#</role>\n", _role.c_str() );
		sb.appendFormat( "  <spawn x=\"%#\" y=\"%#\"/>\n", _spawnX, _spawnY );
		sb.appendFormat( "  <tiles>\n" );
		for ( int32 tileY = 0; tileY < _height; ++tileY )
		{
			for ( int32 tileX = 0; tileX < _width; ++tileX )
			{
				const size_t	  elementIndex = indexOf( tileX, tileY );
				const TileVisual& tileVisual   = _visualList[elementIndex];
				sb.appendFormat( "    <t h=\"%#\"", static_cast<int32>( tileVisual._height ) );
				if ( _encounterList[elementIndex] != 0 )
					sb.appendFormat( " enc=\"1\"" );
				if ( _passThroughList[elementIndex] != 0 )
					sb.appendFormat( " pt=\"1\"" );
				if ( tileVisual._atlasId != 0 )
					sb.appendFormat( " atlas=\"%#\"", static_cast<int32>( tileVisual._atlasId ) );
				sb.appendFormat( " tr=\"%#\" tg=\"%#\" tb=\"%#\">%#</t>\n",
								 static_cast<int32>( tileVisual._tintR ),
								 static_cast<int32>( tileVisual._tintG ),
								 static_cast<int32>( tileVisual._tintB ),
								 _walkableList[elementIndex] != 0 ? "1" : "0" );
			}
		}
		sb.appendFormat( "  </tiles>\n" );
		sb.appendFormat( "  <warps>\n" );
		for ( const TileWarp& warp : _warpList )
		{
			sb.appendFormat( "    <warp x=\"%#\" y=\"%#\" map=\"%#\" tx=\"%#\" ty=\"%#\"",
							 warp._tileX, warp._tileY, warp._targetMap.c_str(), warp._targetTileX, warp._targetTileY );
			if ( warp._pairId.empty() == false )
				sb.appendFormat( " pair=\"%#\"", warp._pairId.c_str() );
			sb.appendFormat( "/>\n" );
		}
		sb.appendFormat( "  </warps>\n" );
		if ( _encounterEntryList.empty() == false )
		{
			sb.appendFormat( "  <encounters>\n" );
			for ( const TileEncounterEntry& entry : _encounterEntryList )
			{
				sb.appendFormat( "    <e id=\"%#\" weight=\"%#\"/>\n", entry._speciesId, entry._weight );
			}
			sb.appendFormat( "  </encounters>\n" );
		}
		sb.appendFormat( "</TileMap>\n" );

		const string text( sb.c_str() );
		const bool	 ok = FileUtil::writeFile( absPath, reinterpret_cast<const uint8*>( text.data() ),
											   text.size() );
		if ( ok )
			SW_LOG_INFO( "Saved '%#' → %#", _name, absPath );
		return ok;
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
