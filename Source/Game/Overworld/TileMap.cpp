/**
 * @file TileMap.cpp
 */
#include "TileMap.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/Resource/ResourceUtil.h"

#include <rapidxml/rapidxml.hpp>

namespace sw
{
	void TileMap::clear()
	{
		_name.clear();
		_width	= 0;
		_height = 0;
		_walkable.clear();
		_encounter.clear();
		_visual.clear();
		_warps.clear();
	}

	bool TileMap::loadFromXml( const std::string& assetRelativePath )
	{
		clear();

		std::string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		if ( FileUtil::isFileExist( absPath ) == false )
		{
			SW_LOG_ERROR( "[TileMap] Not found: %#", absPath );
			return false;
		}

		std::vector<uint8> fileData;
		if ( FileUtil::readFile( absPath, fileData ) == false || fileData.empty() )
			return false;

		std::vector<utf8> xmlBuf( fileData.begin(), fileData.end() );
		xmlBuf.push_back( '\0' );

		rapidxml::xml_document<> doc;
		doc.parse<0>( xmlBuf.data() );
		rapidxml::xml_node<>* root = doc.first_node( "TileMap" );
		if ( root == nullptr )
		{
			SW_LOG_ERROR( "[TileMap] Missing <TileMap>: %#", absPath );
			return false;
		}

		if ( rapidxml::xml_node<>* n = root->first_node( "name" ) )
			_name = n->value();
		if ( rapidxml::xml_node<>* n = root->first_node( "width" ) )
			_width = std::atoi( n->value() );
		if ( rapidxml::xml_node<>* n = root->first_node( "height" ) )
			_height = std::atoi( n->value() );

		if ( _width <= 0 || _height <= 0 )
		{
			_width	= 8;
			_height = 8;
		}

		const size_t count = static_cast<size_t>( _width * _height );
		_walkable.assign( count, 1 );
		_encounter.assign( count, 0 );
		_visual.assign( count, TileVisual{} );

		if ( rapidxml::xml_node<>* tiles = root->first_node( "tiles" ) )
		{
			int32 index = 0;
			for ( rapidxml::xml_node<>* t = tiles->first_node( "t" ); t && index < static_cast<int32>( count ); t = t->next_sibling( "t" ), ++index )
			{
				const char* v = t->value();
				const size_t i = static_cast<size_t>( index );
				_walkable[i] = ( v == nullptr || v[0] != '0' ) ? 1 : 0;
				if ( rapidxml::xml_attribute<>* enc = t->first_attribute( "enc" ) )
					_encounter[i] = ( std::atoi( enc->value() ) != 0 ) ? 1 : 0;

				// HD-2D pass 1: optional height/tint from XML; defaults derive from tile type.
				TileVisual vis{};
				if ( rapidxml::xml_attribute<>* h = t->first_attribute( "h" ) )
					vis._height = static_cast<uint8>( std::atoi( h->value() ) );
				else
					vis._height = _encounter[i] ? 2 : ( _walkable[i] ? 1 : 0 );

				if ( rapidxml::xml_attribute<>* tr = t->first_attribute( "tr" ) )
					vis._tintR = static_cast<uint8>( std::atoi( tr->value() ) );
				if ( rapidxml::xml_attribute<>* tg = t->first_attribute( "tg" ) )
					vis._tintG = static_cast<uint8>( std::atoi( tg->value() ) );
				if ( rapidxml::xml_attribute<>* tb = t->first_attribute( "tb" ) )
					vis._tintB = static_cast<uint8>( std::atoi( tb->value() ) );
				if ( t->first_attribute( "tr" ) == nullptr && t->first_attribute( "tg" ) == nullptr && t->first_attribute( "tb" ) == nullptr )
				{
					if ( _encounter[i] )
					{
						vis._tintR = 120;
						vis._tintG = 190;
						vis._tintB = 90;
					}
					else if ( _walkable[i] == 0 )
					{
						vis._tintR = 80;
						vis._tintG = 80;
						vis._tintB = 90;
					}
				}
				_visual[i] = vis;
			}
		}

		if ( rapidxml::xml_node<>* warps = root->first_node( "warps" ) )
		{
			for ( rapidxml::xml_node<>* w = warps->first_node( "warp" ); w; w = w->next_sibling( "warp" ) )
			{
				TileWarp warp{};
				if ( rapidxml::xml_attribute<>* a = w->first_attribute( "x" ) )
					warp._tileX = std::atoi( a->value() );
				if ( rapidxml::xml_attribute<>* a = w->first_attribute( "y" ) )
					warp._tileY = std::atoi( a->value() );
				if ( rapidxml::xml_attribute<>* a = w->first_attribute( "map" ) )
					warp._targetMap = a->value();
				if ( rapidxml::xml_attribute<>* a = w->first_attribute( "tx" ) )
					warp._targetTileX = std::atoi( a->value() );
				if ( rapidxml::xml_attribute<>* a = w->first_attribute( "ty" ) )
					warp._targetTileY = std::atoi( a->value() );
				_warps.push_back( std::move( warp ) );
			}
		}

		SW_LOG_INFO( "[TileMap] Loaded '%#' (%#x%#)", _name, _width, _height );
		return true;
	}

	bool TileMap::isWalkable( int32 x, int32 y ) const
	{
		if ( x < 0 || y < 0 || x >= _width || y >= _height )
			return false;
		return _walkable[static_cast<size_t>( y * _width + x )] != 0;
	}

	bool TileMap::isEncounterTile( int32 x, int32 y ) const
	{
		if ( x < 0 || y < 0 || x >= _width || y >= _height )
			return false;
		return _encounter[static_cast<size_t>( y * _width + x )] != 0;
	}

	const TileWarp* TileMap::findWarp( int32 x, int32 y ) const
	{
		for ( const TileWarp& w : _warps )
		{
			if ( w._tileX == x && w._tileY == y )
				return &w;
		}
		return nullptr;
	}

	TileVisual TileMap::getTileVisual( int32 x, int32 y ) const
	{
		if ( x < 0 || y < 0 || x >= _width || y >= _height )
			return {};
		return _visual[static_cast<size_t>( y * _width + x )];
	}

	void TileMap::debugLogTileHd2d( int32 x, int32 y ) const
	{
		const TileVisual v = getTileVisual( x, y );
		SW_LOG_INFO( "[TileMap][HD-2D pass1] tile (%#,%#) h=%# tint=(%#,%#,%#)",
					 x, y, v._height, v._tintR, v._tintG, v._tintB );
	}
} // namespace sw
