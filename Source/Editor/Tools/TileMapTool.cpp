/**
 * @file TileMapTool.cpp
 * @brief Lightweight TileMap XML R/W matching Game/Overworld/TileMap format (Editor ??Core only)
 */
#include "Tools/TileMapTool.h"
#include "Workspace/EditorWorkspace.h"
#include "Runtime/EditorUIContext.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/Resource/ResourceUtil.h"

#include <rapidxml/rapidxml.hpp>
#include <imgui.h>
#include <algorithm>
#include <sstream>
#include <cstring>

namespace sw
{
	TileMapTool::TileMapTool()
		: IEditorWindow( false )
	{
		std::strncpy( _pathBuf, "Game/Maps/town01.xml", sizeof( _pathBuf ) - 1 );
		std::strncpy( _nameBuf, "Untitled", sizeof( _nameBuf ) - 1 );
		std::strncpy( _warpTarget, "Game/Maps/route01.xml", sizeof( _warpTarget ) - 1 );
		resize( 8, 8 );
	}

	bool TileMapTool::inBounds( int32 x, int32 y ) const
	{
		return x >= 0 && y >= 0 && x < _width && y < _height;
	}

	size_t TileMapTool::indexOf( int32 x, int32 y ) const
	{
		return static_cast<size_t>( y * _width + x );
	}

	void TileMapTool::resize( int32 width, int32 height )
	{
		if ( width <= 0 || height <= 0 )
			return;
		_width	= width;
		_height = height;
		const size_t count = static_cast<size_t>( _width * _height );
		_walkable.assign( count, 1 );
		_encounter.assign( count, 0 );
		_passThrough.assign( count, 0 );
		_visual.assign( count, EditorTileVisual{} );
		_warps.clear();
	}

	bool TileMapTool::loadXml( const std::string& assetRelativePath )
	{
		std::string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		if ( FileUtil::isFileExist( absPath ) == false )
		{
			_status = "Not found: " + absPath;
			return false;
		}

		std::vector<uint8> fileData;
		if ( FileUtil::readFile( absPath, fileData ) == false || fileData.empty() )
		{
			_status = "Failed to read file";
			return false;
		}

		std::vector<utf8> xmlBuf( fileData.begin(), fileData.end() );
		xmlBuf.push_back( '\0' );

		rapidxml::xml_document<> doc;
		try
		{
			doc.parse<0>( xmlBuf.data() );
		}
		catch ( ... )
		{
			_status = "XML parse error";
			return false;
		}

		rapidxml::xml_node<>* root = doc.first_node( "TileMap" );
		if ( root == nullptr )
		{
			_status = "Missing <TileMap>";
			return false;
		}

		if ( rapidxml::xml_node<>* n = root->first_node( "name" ) )
			std::strncpy( _nameBuf, n->value(), sizeof( _nameBuf ) - 1 );
		int32 w = 8;
		int32 h = 8;
		if ( rapidxml::xml_node<>* n = root->first_node( "width" ) )
			w = std::atoi( n->value() );
		if ( rapidxml::xml_node<>* n = root->first_node( "height" ) )
			h = std::atoi( n->value() );
		if ( w <= 0 )
			w = 8;
		if ( h <= 0 )
			h = 8;
		resize( w, h );

		const size_t count = static_cast<size_t>( _width * _height );
		if ( rapidxml::xml_node<>* tiles = root->first_node( "tiles" ) )
		{
			int32 index = 0;
			for ( rapidxml::xml_node<>* t = tiles->first_node( "t" ); t && index < static_cast<int32>( count );
				  t						 = t->next_sibling( "t" ), ++index )
			{
				const char*	 v = t->value();
				const size_t i = static_cast<size_t>( index );
				_walkable[i]   = ( v == nullptr || v[0] != '0' ) ? 1 : 0;
				if ( rapidxml::xml_attribute<>* enc = t->first_attribute( "enc" ) )
					_encounter[i] = ( std::atoi( enc->value() ) != 0 ) ? 1 : 0;
				if ( rapidxml::xml_attribute<>* pt = t->first_attribute( "pt" ) )
					_passThrough[i] = ( std::atoi( pt->value() ) != 0 ) ? 1 : 0;

				EditorTileVisual vis{};
				if ( rapidxml::xml_attribute<>* ha = t->first_attribute( "h" ) )
					vis.height = static_cast<uint8>( std::atoi( ha->value() ) );
				if ( rapidxml::xml_attribute<>* atlas = t->first_attribute( "atlas" ) )
					vis.atlasId = static_cast<uint8>( std::atoi( atlas->value() ) );
				if ( rapidxml::xml_attribute<>* tr = t->first_attribute( "tr" ) )
					vis.tintR = static_cast<uint8>( std::atoi( tr->value() ) );
				if ( rapidxml::xml_attribute<>* tg = t->first_attribute( "tg" ) )
					vis.tintG = static_cast<uint8>( std::atoi( tg->value() ) );
				if ( rapidxml::xml_attribute<>* tb = t->first_attribute( "tb" ) )
					vis.tintB = static_cast<uint8>( std::atoi( tb->value() ) );
				_visual[i] = vis;
			}
		}

		_warps.clear();
		if ( rapidxml::xml_node<>* warps = root->first_node( "warps" ) )
		{
			for ( rapidxml::xml_node<>* wNode = warps->first_node( "warp" ); wNode; wNode = wNode->next_sibling( "warp" ) )
			{
				EditorTileWarp warp{};
				if ( rapidxml::xml_attribute<>* a = wNode->first_attribute( "x" ) )
					warp.tileX = std::atoi( a->value() );
				if ( rapidxml::xml_attribute<>* a = wNode->first_attribute( "y" ) )
					warp.tileY = std::atoi( a->value() );
				if ( rapidxml::xml_attribute<>* a = wNode->first_attribute( "map" ) )
					warp.targetMap = a->value();
				if ( rapidxml::xml_attribute<>* a = wNode->first_attribute( "tx" ) )
					warp.targetTileX = std::atoi( a->value() );
				if ( rapidxml::xml_attribute<>* a = wNode->first_attribute( "ty" ) )
					warp.targetTileY = std::atoi( a->value() );
				if ( rapidxml::xml_attribute<>* a = wNode->first_attribute( "pair" ) )
					warp.pairId = a->value();
				_warps.push_back( std::move( warp ) );
			}
		}

		std::strncpy( _pathBuf, assetRelativePath.c_str(), sizeof( _pathBuf ) - 1 );
		_status = "Loaded " + assetRelativePath;
		return true;
	}

	bool TileMapTool::saveXml( const std::string& assetRelativePath ) const
	{
		std::string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		std::ostringstream oss;
		oss << "<TileMap>\n";
		oss << "  <name>" << ( _nameBuf[0] ? _nameBuf : "Untitled" ) << "</name>\n";
		oss << "  <width>" << _width << "</width>\n";
		oss << "  <height>" << _height << "</height>\n";
		oss << "  <tiles>\n";
		for ( int32 y = 0; y < _height; ++y )
		{
			for ( int32 x = 0; x < _width; ++x )
			{
				const size_t			 i	 = indexOf( x, y );
				const EditorTileVisual& vis = _visual[i];
				oss << "    <t h=\"" << static_cast<int>( vis.height ) << "\"";
				if ( _encounter[i] )
					oss << " enc=\"1\"";
				if ( _passThrough[i] )
					oss << " pt=\"1\"";
				if ( vis.atlasId != 0 )
					oss << " atlas=\"" << static_cast<int>( vis.atlasId ) << "\"";
				oss << " tr=\"" << static_cast<int>( vis.tintR ) << "\""
					<< " tg=\"" << static_cast<int>( vis.tintG ) << "\""
					<< " tb=\"" << static_cast<int>( vis.tintB ) << "\">"
					<< ( _walkable[i] ? "1" : "0" ) << "</t>\n";
			}
		}
		oss << "  </tiles>\n";
		oss << "  <warps>\n";
		for ( const EditorTileWarp& w : _warps )
		{
			oss << "    <warp x=\"" << w.tileX << "\" y=\"" << w.tileY
				<< "\" map=\"" << w.targetMap << "\" tx=\"" << w.targetTileX
				<< "\" ty=\"" << w.targetTileY << "\"";
			if ( w.pairId.empty() == false )
				oss << " pair=\"" << w.pairId << "\"";
			oss << "/>\n";
		}
		oss << "  </warps>\n";
		oss << "</TileMap>\n";

		const std::string text = oss.str();
		const bool		  ok   = FileUtil::writeFile( absPath, reinterpret_cast<const uint8*>( text.data() ),
											   static_cast<uint64>( text.size() ) );
		return ok;
	}

	void TileMapTool::paintCell( int32 x, int32 y )
	{
		if ( inBounds( x, y ) == false )
			return;
		const size_t i = indexOf( x, y );

		switch ( _layer )
		{
		case PaintLayer::Visual:
			if ( _bErase )
			{
				_visual[i] = EditorTileVisual{};
			}
			else
			{
				_visual[i].height  = static_cast<uint8>( std::clamp( _paintHeight, 0, 255 ) );
				_visual[i].atlasId = static_cast<uint8>( std::clamp( _atlasId, 0, 255 ) );
				_visual[i].tintR   = static_cast<uint8>( std::clamp( static_cast<int>( _tint[0] * 255.0f ), 0, 255 ) );
				_visual[i].tintG   = static_cast<uint8>( std::clamp( static_cast<int>( _tint[1] * 255.0f ), 0, 255 ) );
				_visual[i].tintB   = static_cast<uint8>( std::clamp( static_cast<int>( _tint[2] * 255.0f ), 0, 255 ) );
			}
			break;
		case PaintLayer::Walkable:
			_walkable[i] = _bErase ? 0 : 1;
			break;
		case PaintLayer::Encounter:
			_encounter[i] = _bErase ? 0 : 1;
			break;
		case PaintLayer::PassThrough:
			_passThrough[i] = _bErase ? 0 : 1;
			break;
		case PaintLayer::Warp:
		{
			_warps.erase( std::remove_if( _warps.begin(), _warps.end(),
										  [x, y]( const EditorTileWarp& w )
										  { return w.tileX == x && w.tileY == y; } ),
						  _warps.end() );
			if ( _bErase == false && _warpTarget[0] != '\0' )
			{
				EditorTileWarp w{};
				w.tileX		  = x;
				w.tileY		  = y;
				w.targetMap	  = _warpTarget;
				w.targetTileX = _warpTx;
				w.targetTileY = _warpTy;
				_warps.push_back( std::move( w ) );
				_walkable[i] = 1;
			}
			break;
		}
		}
	}

	void TileMapTool::paintEdgeWarp( int32 edge )
	{
		const char* targets[] = { _edgeTargetN, _edgeTargetE, _edgeTargetS, _edgeTargetW };
		if ( edge < 0 || edge > 3 || targets[edge][0] == '\0' )
			return;

		auto stamp = [&]( int32 x, int32 y )
		{
			_walkable[indexOf( x, y )] = 1;
			_warps.erase( std::remove_if( _warps.begin(), _warps.end(),
										  [x, y]( const EditorTileWarp& w )
										  { return w.tileX == x && w.tileY == y; } ),
						  _warps.end() );
			EditorTileWarp w{};
			w.tileX		  = x;
			w.tileY		  = y;
			w.targetMap	  = targets[edge];
			w.targetTileX = _edgeTx[edge];
			w.targetTileY = _edgeTy[edge];
			_warps.push_back( std::move( w ) );
		};

		switch ( edge )
		{
		case 0:
			for ( int32 x = 0; x < _width; ++x )
				stamp( x, 0 );
			break;
		case 1:
			for ( int32 y = 0; y < _height; ++y )
				stamp( _width - 1, y );
			break;
		case 2:
			for ( int32 x = 0; x < _width; ++x )
				stamp( x, _height - 1 );
			break;
		case 3:
			for ( int32 y = 0; y < _height; ++y )
				stamp( 0, y );
			break;
		default:
			break;
		}
	}

	void TileMapTool::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		const std::string& focused = editor::focusedAssetPath();
		if ( focused.empty() == false && focused.find( ".xml" ) != std::string::npos && focused != _pathBuf )
		{
			std::strncpy( _pathBuf, focused.c_str(), sizeof( _pathBuf ) - 1 );
			_pathBuf[sizeof( _pathBuf ) - 1] = '\0';
			loadXml( _pathBuf );
		}

		if ( focused.empty() == false )
			ImGui::TextDisabled( "Focused: %s", focused.c_str() );

		ImGui::InputText( "Path", _pathBuf, sizeof( _pathBuf ) );
		ImGui::InputText( "Name", _nameBuf, sizeof( _nameBuf ) );
		ImGui::InputInt( "Width", &_width );
		ImGui::SameLine();
		ImGui::InputInt( "Height", &_height );
		if ( ImGui::Button( "Apply Size" ) )
			resize( std::max( 1, _width ), std::max( 1, _height ) );

		ImGui::SameLine();
		if ( ImGui::Button( "Load" ) )
		{
			if ( loadXml( _pathBuf ) == false )
				SW_LOG_WARNING( "[TileMapTool] %#", _status.c_str() );
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Save" ) )
		{
			if ( saveXml( _pathBuf ) )
				_status = std::string( "Saved " ) + _pathBuf;
			else
				_status = "Save failed";
		}

		ImGui::Checkbox( "Erase", &_bErase );
		ImGui::Separator();

		const char* layerNames[] = { "Visual", "Walkable", "Encounter", "Warp", "PassThrough" };
		int			layerIdx	 = static_cast<int>( _layer );
		if ( ImGui::Combo( "Layer", &layerIdx, layerNames, 5 ) )
			_layer = static_cast<PaintLayer>( layerIdx );

		if ( _layer == PaintLayer::Visual )
		{
			ImGui::InputInt( "Height", &_paintHeight );
			ImGui::InputInt( "Atlas Id", &_atlasId );
			ImGui::ColorEdit3( "Tint", _tint );
		}
		else if ( _layer == PaintLayer::Warp )
		{
			ImGui::InputText( "Warp Target", _warpTarget, sizeof( _warpTarget ) );
			ImGui::InputInt( "Target TX", &_warpTx );
			ImGui::InputInt( "Target TY", &_warpTy );
		}

		ImGui::Separator();
		ImGui::TextUnformatted( "Edge Warp Presets" );
		const char* edges[] = { "N", "E", "S", "W" };
		char*		bufs[]	= { _edgeTargetN, _edgeTargetE, _edgeTargetS, _edgeTargetW };
		for ( int e = 0; e < 4; ++e )
		{
			ImGui::PushID( e );
			ImGui::SetNextItemWidth( 180.0f );
			ImGui::InputText( edges[e], bufs[e], 128 );
			ImGui::SameLine();
			ImGui::SetNextItemWidth( 50.0f );
			ImGui::InputInt( "##tx", &_edgeTx[e] );
			ImGui::SameLine();
			ImGui::SetNextItemWidth( 50.0f );
			ImGui::InputInt( "##ty", &_edgeTy[e] );
			ImGui::SameLine();
			if ( ImGui::Button( "Apply" ) )
				paintEdgeWarp( e );
			ImGui::PopID();
		}

		ImGui::Separator();
		ImGui::Text( "Grid %dx%d ??click to paint", _width, _height );

		const float cell = 18.0f;
		ImDrawList* dl	 = ImGui::GetWindowDrawList();
		const ImVec2 origin = ImGui::GetCursorScreenPos();

		auto hasWarp = [&]( int32 x, int32 y ) -> bool
		{
			for ( const EditorTileWarp& w : _warps )
			{
				if ( w.tileX == x && w.tileY == y )
					return true;
			}
			return false;
		};

		for ( int32 y = 0; y < _height; ++y )
		{
			for ( int32 x = 0; x < _width; ++x )
			{
				const size_t i	   = indexOf( x, y );
				ImU32		 color = IM_COL32( 60, 60, 70, 255 );
				switch ( _layer )
				{
				case PaintLayer::Visual:
					color = IM_COL32( _visual[i].tintR, _visual[i].tintG, _visual[i].tintB, 255 );
					break;
				case PaintLayer::Walkable:
					color = _walkable[i] ? IM_COL32( 80, 160, 90, 255 ) : IM_COL32( 70, 70, 80, 255 );
					break;
				case PaintLayer::Encounter:
					color = _encounter[i] ? IM_COL32( 120, 190, 90, 255 ) : IM_COL32( 50, 50, 55, 255 );
					break;
				case PaintLayer::PassThrough:
					color = _passThrough[i] ? IM_COL32( 160, 170, 200, 255 ) : IM_COL32( 50, 50, 55, 255 );
					break;
				case PaintLayer::Warp:
					color = hasWarp( x, y ) ? IM_COL32( 200, 120, 80, 255 ) : IM_COL32( 50, 50, 55, 255 );
					break;
				}

				const ImVec2 p0( origin.x + x * cell, origin.y + y * cell );
				const ImVec2 p1( p0.x + cell - 1.0f, p0.y + cell - 1.0f );
				dl->AddRectFilled( p0, p1, color );
				dl->AddRect( p0, p1, IM_COL32( 20, 20, 24, 255 ) );
			}
		}

		ImGui::InvisibleButton( "##tilegrid", ImVec2( _width * cell, _height * cell ) );
		if ( ImGui::IsItemHovered() && ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
		{
			const ImVec2 mouse = ImGui::GetMousePos();
			const int32	 gx	   = static_cast<int32>( ( mouse.x - origin.x ) / cell );
			const int32	 gy	   = static_cast<int32>( ( mouse.y - origin.y ) / cell );
			paintCell( gx, gy );
		}

		if ( _status.empty() == false )
		{
			ImGui::Separator();
			ImGui::TextDisabled( "%s", _status.c_str() );
		}

		ImGui::End();
	}
} // namespace sw
