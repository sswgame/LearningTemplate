#include "pch.h"

#include "Engine/Utility/Xml/TileMapXml.h"

#include "Engine/Utility/Resource/ResourceUtil.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	SW_LOG_CALLER( "TileMapXml" );

	bool TileMapXmlData::load( string_view path )
	{
		XmlDocument doc;
		string		absPath;
		if ( doc.loadPath( path, &absPath ) == false )
		{
			SW_LOG_ERROR( "Not found: %#", path );
			return false;
		}
		if ( loadFromXml( doc.saveToString() ) == false )
			return false;
		_sourcePath = path;
		return true;
	}

	bool TileMapXmlData::loadFromXml( string_view xml )
	{
		*this = {};

		XmlDocument doc;
		if ( doc.parse( xml ) == false )
			return false;

		XmlNode root = doc.root( "TileMap" );
		if ( root.isValid() == false )
		{
			SW_LOG_ERROR( "Missing <TileMap>" );
			return false;
		}

		root.takeChildText( "name", _name );
		_width	= root.childInt( "width", 0 );
		_height = root.childInt( "height", 0 );
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
		_listWalkable.assign( count, 1 );
		_listEncounter.assign( count, 0 );
		_listPassThrough.assign( count, 0 );
		_listVisual.assign( count, Visual{} );

		XmlNode tiles = root.child( "tiles" );
		if ( tiles.isValid() )
		{
			int32 index{ 0 };
			for ( XmlNode tileNode = tiles.child( "t" ); tileNode && index < static_cast<int32>( count );
				  tileNode		   = tileNode.next( "t" ), ++index )
			{
				const utf8*	 pText			   = tileNode.text();
				const size_t elementIndex	   = static_cast<size_t>( index );
				_listWalkable[elementIndex]	   = ( pText == nullptr || pText[0] != '0' ) ? 1 : 0;
				_listEncounter[elementIndex]   = tileNode.attrInt( "enc", 0 ) != 0 ? 1 : 0;
				_listPassThrough[elementIndex] = tileNode.attrInt( "pt", 0 ) != 0 ? 1 : 0;

				Visual tileVisual{};
				if ( tileNode.attr( "h" ) != nullptr )
					tileVisual._height = static_cast<uint8>( tileNode.attrInt( "h", 0 ) );
				else
					tileVisual._height = _listEncounter[elementIndex] != 0 ? 2 : ( _listWalkable[elementIndex] != 0 ? 1 : 0 );

				if ( tileNode.attr( "atlas" ) != nullptr )
					tileVisual._atlasId = static_cast<uint8>( tileNode.attrInt( "atlas", 0 ) );

				const bool bHasTint = tileNode.attr( "tr" ) != nullptr || tileNode.attr( "tg" ) != nullptr || tileNode.attr( "tb" ) != nullptr;
				if ( bHasTint )
				{
					tileVisual._tintR = static_cast<uint8>( tileNode.attrInt( "tr", 255 ) );
					tileVisual._tintG = static_cast<uint8>( tileNode.attrInt( "tg", 255 ) );
					tileVisual._tintB = static_cast<uint8>( tileNode.attrInt( "tb", 255 ) );
				}
				else if ( _listEncounter[elementIndex] != 0 )
				{
					tileVisual._tintR = 120;
					tileVisual._tintG = 190;
					tileVisual._tintB = 90;
				}
				else if ( _listWalkable[elementIndex] == 0 )
				{
					tileVisual._tintR = 80;
					tileVisual._tintG = 80;
					tileVisual._tintB = 90;
				}
				else if ( _listPassThrough[elementIndex] != 0 )
				{
					tileVisual._tintR = 160;
					tileVisual._tintG = 170;
					tileVisual._tintB = 200;
				}
				_listVisual[elementIndex] = tileVisual;
			}
		}

		XmlNode warps = root.child( "warps" );
		if ( warps.isValid() )
		{
			for ( XmlNode warpNode = warps.child( "warp" ); warpNode; warpNode = warpNode.next( "warp" ) )
			{
				Warp warp{};
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
				_listWarp.push_back( std::move( warp ) );
			}
		}

		XmlNode encounters = root.child( "encounters" );
		if ( encounters.isValid() )
		{
			for ( XmlNode encNode = encounters.child( "e" ); encNode; encNode = encNode.next( "e" ) )
			{
				Encounter	entry{};
				const utf8* pId = encNode.attr( "id" );
				if ( pId != nullptr )
					entry._speciesId = pId;
				entry._weight = encNode.attrFloat( "weight", 0.f );
				if ( entry._speciesId.empty() == false )
					_listEncounterEntry.push_back( std::move( entry ) );
			}
		}

		SW_LOG_INFO( "Loaded '%#' (%#x%#) scene=%# role=%# encounters=%#",
					 _name, _width, _height, _scenePath, _role,
					 static_cast<uint32>( _listEncounterEntry.size() ) );
		return true;
	}

	bool TileMapXmlData::save( string_view path ) const
	{
		string absPath = ResourceUtil::getResourcePath( path );
		if ( absPath.empty() )
			absPath = path;

		XmlDocument doc;
		if ( doc.parse( toXml() ) == false )
			return false;
		const bool bOk = doc.saveFile( absPath );
		if ( bOk )
			SW_LOG_INFO( "Saved '%#' -> %#", _name, absPath );
		return bOk;
	}

	string TileMapXmlData::toXml() const
	{
		XmlDocument doc;
		XmlNode		root = doc.appendRoot( "TileMap" );
		root.appendChild( "name", _name.empty() ? string_view{ "Untitled" } : string_view{ _name } );
		root.appendChild( "width", _width );
		root.appendChild( "height", _height );
		if ( _scenePath.empty() == false )
			root.appendChild( "scene", _scenePath );
		if ( _role.empty() == false )
			root.appendChild( "role", _role );

		XmlNode spawn = root.appendChild( "spawn" );
		spawn.appendAttr( "x", _spawnX );
		spawn.appendAttr( "y", _spawnY );

		XmlNode		 tiles = root.appendChild( "tiles" );
		const size_t count = static_cast<size_t>( _width * _height );
		for ( size_t tileIndex = 0; tileIndex < count; ++tileIndex )
		{
			XmlNode		  tileNode	 = tiles.appendChild( "t" );
			const Visual& tileVisual = _listVisual[tileIndex];
			tileNode.appendAttr( "h", static_cast<int32>( tileVisual._height ) );
			if ( _listEncounter[tileIndex] != 0 )
				tileNode.appendAttr( "enc", 1 );
			if ( _listPassThrough[tileIndex] != 0 )
				tileNode.appendAttr( "pt", 1 );
			if ( tileVisual._atlasId != 0 )
				tileNode.appendAttr( "atlas", static_cast<int32>( tileVisual._atlasId ) );
			tileNode.appendAttr( "tr", static_cast<int32>( tileVisual._tintR ) );
			tileNode.appendAttr( "tg", static_cast<int32>( tileVisual._tintG ) );
			tileNode.appendAttr( "tb", static_cast<int32>( tileVisual._tintB ) );
			tileNode.setValue( _listWalkable[tileIndex] != 0 ? "1" : "0" );
		}

		XmlNode warps = root.appendChild( "warps" );
		for ( const Warp& warp : _listWarp )
		{
			XmlNode warpNode = warps.appendChild( "warp" );
			warpNode.appendAttr( "x", warp._tileX );
			warpNode.appendAttr( "y", warp._tileY );
			warpNode.appendAttr( "map", warp._targetMap );
			warpNode.appendAttr( "tx", warp._targetTileX );
			warpNode.appendAttr( "ty", warp._targetTileY );
			if ( warp._pairId.empty() == false )
				warpNode.appendAttr( "pair", warp._pairId );
		}

		if ( _listEncounterEntry.empty() == false )
		{
			XmlNode encounters = root.appendChild( "encounters" );
			for ( const Encounter& entry : _listEncounterEntry )
			{
				XmlNode encNode = encounters.appendChild( "e" );
				encNode.appendAttr( "id", entry._speciesId );
				encNode.appendAttr( "weight", entry._weight );
			}
		}

		return doc.saveToString();
	}
} // namespace sw
