/**
 * @file TileMap.cpp
 */
#include "TileMap.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/Resource/ResourceUtil.h"

#include <rapidxml/rapidxml.hpp>
#include <sstream>

namespace sw
{
	void TileMap::clear()
	{
		_name.clear();
		_sourcePath.clear();
		_width	= 0;
		_height = 0;
		_walkable.clear();
		_encounter.clear();
		_passThrough.clear();
		_visual.clear();
		_warps.clear();
	}

	bool TileMap::inBounds( int32 x, int32 y ) const
	{
		return x >= 0 && y >= 0 && x < _width && y < _height;
	}

	void TileMap::resize( int32 width, int32 height )
	{
		if ( width <= 0 || height <= 0 )
			return;
		_width	= width;
		_height = height;
		const size_t count = static_cast<size_t>( _width * _height );
		_walkable.assign( count, 1 );
		_encounter.assign( count, 0 );
		_passThrough.assign( count, 0 );
		_visual.assign( count, TileVisual{} );
		_warps.clear();
	}

	bool TileMap::loadFromXml( const std::string& assetRelativePath )
	{
		clear();
		_sourcePath = assetRelativePath;

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
		_passThrough.assign( count, 0 );
		_visual.assign( count, TileVisual{} );

		if ( rapidxml::xml_node<>* tiles = root->first_node( "tiles" ) )
		{
			int32 index = 0;
			for ( rapidxml::xml_node<>* t = tiles->first_node( "t" ); t && index < static_cast<int32>( count ); t = t->next_sibling( "t" ), ++index )
			{
				const char*	 v = t->value();
				const size_t i = static_cast<size_t>( index );
				_walkable[i]   = ( v == nullptr || v[0] != '0' ) ? 1 : 0;
				if ( rapidxml::xml_attribute<>* enc = t->first_attribute( "enc" ) )
					_encounter[i] = ( std::atoi( enc->value() ) != 0 ) ? 1 : 0;
				if ( rapidxml::xml_attribute<>* pt = t->first_attribute( "pt" ) )
					_passThrough[i] = ( std::atoi( pt->value() ) != 0 ) ? 1 : 0;

				TileVisual vis{};
				if ( rapidxml::xml_attribute<>* h = t->first_attribute( "h" ) )
					vis._height = static_cast<uint8>( std::atoi( h->value() ) );
				else
					vis._height = _encounter[i] ? 2 : ( _walkable[i] ? 1 : 0 );

				if ( rapidxml::xml_attribute<>* atlas = t->first_attribute( "atlas" ) )
					vis._atlasId = static_cast<uint8>( std::atoi( atlas->value() ) );

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
					else if ( _passThrough[i] )
					{
						vis._tintR = 160;
						vis._tintG = 170;
						vis._tintB = 200;
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
				if ( rapidxml::xml_attribute<>* a = w->first_attribute( "pair" ) )
					warp._pairId = a->value();
				_warps.push_back( std::move( warp ) );
			}
		}

		SW_LOG_INFO( "[TileMap] Loaded '%#' (%#x%#)", _name, _width, _height );
		return true;
	}

	bool TileMap::saveToXml( const std::string& assetRelativePath ) const
	{
		std::string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		std::ostringstream oss;
		oss << "<TileMap>\n";
		oss << "  <name>" << ( _name.empty() ? "Untitled" : _name ) << "</name>\n";
		oss << "  <width>" << _width << "</width>\n";
		oss << "  <height>" << _height << "</height>\n";
		oss << "  <tiles>\n";
		for ( int32 y = 0; y < _height; ++y )
		{
			for ( int32 x = 0; x < _width; ++x )
			{
				const size_t	 i	 = indexOf( x, y );
				const TileVisual& vis = _visual[i];
				oss << "    <t h=\"" << static_cast<int>( vis._height ) << "\"";
				if ( _encounter[i] )
					oss << " enc=\"1\"";
				if ( _passThrough[i] )
					oss << " pt=\"1\"";
				if ( vis._atlasId != 0 )
					oss << " atlas=\"" << static_cast<int>( vis._atlasId ) << "\"";
				oss << " tr=\"" << static_cast<int>( vis._tintR ) << "\""
					<< " tg=\"" << static_cast<int>( vis._tintG ) << "\""
					<< " tb=\"" << static_cast<int>( vis._tintB ) << "\">"
					<< ( _walkable[i] ? "1" : "0" ) << "</t>\n";
			}
		}
		oss << "  </tiles>\n";
		oss << "  <warps>\n";
		for ( const TileWarp& w : _warps )
		{
			oss << "    <warp x=\"" << w._tileX << "\" y=\"" << w._tileY
				<< "\" map=\"" << w._targetMap << "\" tx=\"" << w._targetTileX
				<< "\" ty=\"" << w._targetTileY << "\"";
			if ( w._pairId.empty() == false )
				oss << " pair=\"" << w._pairId << "\"";
			oss << "/>\n";
		}
		oss << "  </warps>\n";
		oss << "</TileMap>\n";

		const std::string text = oss.str();
		const bool		  ok   = FileUtil::writeFile( absPath, reinterpret_cast<const uint8*>( text.data() ),
											   static_cast<uint64>( text.size() ) );
		if ( ok )
			SW_LOG_INFO( "[TileMap] Saved '%#' → %#", _name, absPath );
		return ok;
	}

	bool TileMap::isWalkable( int32 x, int32 y ) const
	{
		if ( inBounds( x, y ) == false )
			return false;
		return _walkable[indexOf( x, y )] != 0;
	}

	bool TileMap::isEncounterTile( int32 x, int32 y ) const
	{
		if ( inBounds( x, y ) == false )
			return false;
		return _encounter[indexOf( x, y )] != 0;
	}

	bool TileMap::isPassThrough( int32 x, int32 y ) const
	{
		if ( inBounds( x, y ) == false )
			return false;
		return _passThrough[indexOf( x, y )] != 0;
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
		if ( _walkable[indexOf( x, y )] )
			f = f | TileFlags::Walkable;
		else
			f = f | TileFlags::Solid;
		if ( _encounter[indexOf( x, y )] )
			f = f | TileFlags::Encounter;
		if ( _passThrough[indexOf( x, y )] )
			f = f | TileFlags::PassThrough;
		if ( findWarp( x, y ) != nullptr )
			f = f | TileFlags::Warp;
		return f;
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
		if ( inBounds( x, y ) == false )
			return {};
		return _visual[indexOf( x, y )];
	}

	void TileMap::setWalkable( int32 x, int32 y, bool v )
	{
		if ( inBounds( x, y ) )
			_walkable[indexOf( x, y )] = v ? 1 : 0;
	}

	void TileMap::setEncounter( int32 x, int32 y, bool v )
	{
		if ( inBounds( x, y ) )
			_encounter[indexOf( x, y )] = v ? 1 : 0;
	}

	void TileMap::setPassThrough( int32 x, int32 y, bool v )
	{
		if ( inBounds( x, y ) )
			_passThrough[indexOf( x, y )] = v ? 1 : 0;
	}

	void TileMap::setTileVisual( int32 x, int32 y, const TileVisual& v )
	{
		if ( inBounds( x, y ) )
			_visual[indexOf( x, y )] = v;
	}

	void TileMap::setOrUpdateWarp( const TileWarp& warp )
	{
		for ( TileWarp& w : _warps )
		{
			if ( w._tileX == warp._tileX && w._tileY == warp._tileY )
			{
				w = warp;
				return;
			}
		}
		_warps.push_back( warp );
	}

	void TileMap::removeWarp( int32 x, int32 y )
	{
		_warps.erase( std::remove_if( _warps.begin(), _warps.end(),
									  [x, y]( const TileWarp& w )
									  { return w._tileX == x && w._tileY == y; } ),
					  _warps.end() );
	}

	void TileMap::paintEdgeWarpPreset( int32 edge, const std::string& targetMap, int32 tx, int32 ty )
	{
		if ( _width <= 0 || _height <= 0 || targetMap.empty() )
			return;

		auto stamp = [&]( int32 x, int32 y )
		{
			setWalkable( x, y, true );
			TileWarp w{};
			w._tileX	   = x;
			w._tileY	   = y;
			w._targetMap   = targetMap;
			w._targetTileX = tx;
			w._targetTileY = ty;
			setOrUpdateWarp( w );
		};

		switch ( edge )
		{
		case 0: // N
			for ( int32 x = 0; x < _width; ++x )
				stamp( x, 0 );
			break;
		case 1: // E
			for ( int32 y = 0; y < _height; ++y )
				stamp( _width - 1, y );
			break;
		case 2: // S
			for ( int32 x = 0; x < _width; ++x )
				stamp( x, _height - 1 );
			break;
		case 3: // W
			for ( int32 y = 0; y < _height; ++y )
				stamp( 0, y );
			break;
		default:
			break;
		}
	}

	void TileMap::debugLogTileHd2d( int32 x, int32 y ) const
	{
		const TileVisual v = getTileVisual( x, y );
		SW_LOG_INFO( "[TileMap][HD-2D] tile (%#,%#) h=%# tint=(%#,%#,%#) flags walk=%# enc=%# pt=%#",
					 x, y, v._height, v._tintR, v._tintG, v._tintB,
					 isWalkable( x, y ) ? 1 : 0, isEncounterTile( x, y ) ? 1 : 0, isPassThrough( x, y ) ? 1 : 0 );
	}
} // namespace sw
