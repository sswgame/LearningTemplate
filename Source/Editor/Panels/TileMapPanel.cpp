#include "pch.h"

#include "Editor/Panels/TileMapPanel.h"

#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Config/EditorData.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>

namespace sw::editor
{
	SW_LOG_CALLER( "TileMapPanel" );

	TileMapPanel::TileMapPanel()
		: IEditorPanel( false )
		, _arrPathBuffer{}
		, _arrNameBuffer{}
		, _arrEdgeTargetN{}
		, _arrEdgeTargetE{}
		, _arrEdgeTargetS{}
		, _arrEdgeTargetW{}
		, _arrEdgeTx{ 1, 1, 1, 1 }
		, _arrEdgeTy{ 1, 1, 1, 1 }
		, _width{ 8 }
		, _height{ 8 }
		, _layer{ PaintLayer::Walkable }
		, _paintHeight{ 1 }
		, _atlasId{ 0 }
		, _arrTint{ 180.0f / 255.0f, 200.0f / 255.0f, 160.0f / 255.0f }
		, _arrWarpTarget{}
		, _warpTx{ 1 }
		, _warpTy{ 1 }
		, _bErase{ false }
		, _listWalkable{}
		, _listEncounter{}
		, _listPassThrough{}
		, _listVisual{}
		, _listWarp{}
		, _status{}
	{
		const EditorData& editorData = editor::getEditorData();
		if ( editorData._defaultMap.empty() == false )
			StringUtil::strncpy( _arrPathBuffer, editorData._defaultMap.c_str(), sizeof( _arrPathBuffer ) - 1 );
		StringUtil::strncpy( _arrNameBuffer, "Untitled", sizeof( _arrNameBuffer ) - 1 );
		if ( editorData._warpMap.empty() == false )
			StringUtil::strncpy( _arrWarpTarget, editorData._warpMap.c_str(), sizeof( _arrWarpTarget ) - 1 );
		resize( 8, 8 );
	}

	void TileMapPanel::drawContent()
	{
		const string& focused = EditorContext::get()->getWorkspace().getFocusedAssetPath();
		if ( focused.empty() == false && focused.find( ".xml" ) != string::npos && focused != _arrPathBuffer )
		{
			StringUtil::strncpy( _arrPathBuffer, focused.c_str(), sizeof( _arrPathBuffer ) - 1 );
			_arrPathBuffer[sizeof( _arrPathBuffer ) - 1] = '\0';
			loadXml( _arrPathBuffer );
		}

		if ( focused.empty() == false )
			ImGui::TextDisabled( "Focused: %s", focused.c_str() );

		ImGui::InputText( "Path", _arrPathBuffer, sizeof( _arrPathBuffer ) );
		ImGui::InputText( "Name", _arrNameBuffer, sizeof( _arrNameBuffer ) );
		ImGui::InputInt( "Width", &_width );
		ImGui::SameLine();
		ImGui::InputInt( "Height", &_height );
		if ( ImGui::Button( "Apply Size" ) )
			resize( MathUtil::max( 1, _width ), MathUtil::max( 1, _height ) );

		ImGui::SameLine();
		if ( ImGui::Button( "Load" ) )
		{
			if ( loadXml( _arrPathBuffer ) == false )
				SW_LOG_WARNING( "%#", _status.c_str() );
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Save" ) )
		{
			if ( saveXml( _arrPathBuffer ) )
				_status = string( "Saved " ) + _arrPathBuffer;
			else
				_status = "Save failed";
		}

		ImGui::Checkbox( "Erase", &_bErase );
		ImGui::Separator();

		const utf8* layerNames[] = { "Visual", "Walkable", "Encounter", "Warp", "PassThrough" };
		int32		layerIdx	 = static_cast<int32>( _layer );
		if ( ImGui::Combo( "Layer", &layerIdx, layerNames, 5 ) )
			_layer = static_cast<PaintLayer>( layerIdx );

		if ( _layer == PaintLayer::Visual )
		{
			ImGui::InputInt( "Height", &_paintHeight );
			ImGui::InputInt( "Atlas Id", &_atlasId );
			ImGui::ColorEdit3( "Tint", _arrTint );
		}
		else if ( _layer == PaintLayer::Warp )
		{
			ImGui::InputText( "Warp Target", _arrWarpTarget, sizeof( _arrWarpTarget ) );
			ImGui::InputInt( "Target TX", &_warpTx );
			ImGui::InputInt( "Target TY", &_warpTy );
		}

		ImGui::Separator();
		ImGui::TextUnformatted( "Edge Warp Presets" );
		const utf8* edges[] = { "N", "E", "S", "W" };
		utf8*		bufs[]	= { _arrEdgeTargetN, _arrEdgeTargetE, _arrEdgeTargetS, _arrEdgeTargetW };
		for ( int32 edgeIndex = 0; edgeIndex < 4; ++edgeIndex )
		{
			ImGui::PushID( edgeIndex );
			ImGui::SetNextItemWidth( 180.0f );
			ImGui::InputText( edges[edgeIndex], bufs[edgeIndex], 128 );
			ImGui::SameLine();
			ImGui::SetNextItemWidth( 50.0f );
			ImGui::InputInt( "##tx", &_arrEdgeTx[edgeIndex] );
			ImGui::SameLine();
			ImGui::SetNextItemWidth( 50.0f );
			ImGui::InputInt( "##ty", &_arrEdgeTy[edgeIndex] );
			ImGui::SameLine();
			if ( ImGui::Button( "Apply" ) )
				paintEdgeWarp( edgeIndex );
			ImGui::PopID();
		}

		ImGui::Separator();
		ImGui::Text( "Grid %dx%d ??click to paint", _width, _height );

		constexpr float32 cell	 = 18.0f;
		ImDrawList*		  pDl	 = ImGui::GetWindowDrawList();
		const ImVec2	  origin = ImGui::GetCursorScreenPos();

		unordered_set<uint64> uniqueWarpCells;
		if ( _layer == PaintLayer::Warp )
		{
			uniqueWarpCells.reserve( _listWarp.size() );
			for ( const EditorTileWarp& warp : _listWarp )
			{
				uniqueWarpCells.insert( ( static_cast<uint64>( static_cast<uint32>( warp._tileY ) ) << 32 ) |
										static_cast<uint32>( warp._tileX ) );
			}
		}

		for ( int32 tileY = 0; tileY < _height; ++tileY )
		{
			for ( int32 tileX = 0; tileX < _width; ++tileX )
			{
				const size_t tileIndex = indexOf( tileX, tileY );
				ImU32		 color	   = IM_COL32( 60, 60, 70, 255 );
				switch ( _layer )
				{
					case PaintLayer::Visual:
						color = IM_COL32( _listVisual[tileIndex]._tintR, _listVisual[tileIndex]._tintG, _listVisual[tileIndex]._tintB, 255 );
						break;
					case PaintLayer::Walkable:
						color = _listWalkable[tileIndex] ? IM_COL32( 80, 160, 90, 255 ) : IM_COL32( 70, 70, 80, 255 );
						break;
					case PaintLayer::Encounter:
						color = _listEncounter[tileIndex] ? IM_COL32( 120, 190, 90, 255 ) : IM_COL32( 50, 50, 55, 255 );
						break;
					case PaintLayer::PassThrough:
						color = _listPassThrough[tileIndex] ? IM_COL32( 80, 120, 200, 255 ) : IM_COL32( 70, 70, 80, 255 );
						break;
					case PaintLayer::Warp:
					{
						const uint64 key = ( static_cast<uint64>( static_cast<uint32>( tileY ) ) << 32 ) | static_cast<uint32>( tileX );
						color			 = uniqueWarpCells.count( key ) ? IM_COL32( 200, 120, 80, 255 ) : IM_COL32( 50, 50, 55, 255 );
						break;
					}
				}

				const float32 fx = static_cast<float32>( tileX );
				const float32 fy = static_cast<float32>( tileY );
				const ImVec2  p0( origin.x + fx * cell, origin.y + fy * cell );
				const ImVec2  p1( p0.x + cell - 1.0f, p0.y + cell - 1.0f );
				pDl->AddRectFilled( p0, p1, color );
				pDl->AddRect( p0, p1, IM_COL32( 20, 20, 24, 255 ) );
			}
		}

		ImGui::InvisibleButton( "##tilegrid",
								ImVec2( static_cast<float32>( _width ) * cell, static_cast<float32>( _height ) * cell ) );
		if ( ImGui::IsItemHovered() && ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
		{
			const ImVec2 mouse = ImGui::GetMousePos();
			const int32	 gx	   = static_cast<int32>( ( mouse.x - origin.x ) / cell );
			const int32	 gy	   = static_cast<int32>( ( mouse.y - origin.y ) / cell );
			paintCell( gx, gy );
		}

		editor::drawPanelStatus( _status.c_str() );
	}

	void TileMapPanel::resize( int32 width, int32 height )
	{
		if ( width <= 0 || height <= 0 )
			return;
		_width			   = width;
		_height			   = height;
		const size_t count = static_cast<size_t>( _width * _height );
		_listWalkable.assign( count, 1 );
		_listEncounter.assign( count, 0 );
		_listPassThrough.assign( count, 0 );
		_listVisual.assign( count, EditorTileVisual{} );
		_listWarp.clear();
	}

	bool TileMapPanel::loadXml( string_view assetRelativePath )
	{
		EditorTileMapData data;
		if ( EditorToolAssetCommands::loadTileMap( assetRelativePath, data, _status ) == false )
			return false;

		StringUtil::strncpy( _arrNameBuffer, data._name.c_str(), sizeof( _arrNameBuffer ) - 1 );
		_width			 = data._width;
		_height			 = data._height;
		_listWalkable	 = std::move( data._listWalkable );
		_listEncounter	 = std::move( data._listEncounter );
		_listPassThrough = std::move( data._listPassThrough );
		_listVisual		 = std::move( data._listVisual );
		_listWarp		 = std::move( data._listWarp );
		StringUtil::strncpy( _arrPathBuffer, string( assetRelativePath ).c_str(), sizeof( _arrPathBuffer ) - 1 );
		return true;
	}

	bool TileMapPanel::saveXml( string_view assetRelativePath ) const
	{
		EditorTileMapData data;
		data._name			  = _arrNameBuffer;
		data._width			  = _width;
		data._height		  = _height;
		data._listWalkable	  = _listWalkable;
		data._listEncounter	  = _listEncounter;
		data._listPassThrough = _listPassThrough;
		data._listVisual	  = _listVisual;
		data._listWarp		  = _listWarp;
		return EditorToolAssetCommands::saveTileMap( assetRelativePath, data );
	}

	void TileMapPanel::paintCell( int32 x, int32 y )
	{
		if ( inBounds( x, y ) == false )
			return;

		const size_t tileIndex = indexOf( x, y );
		switch ( _layer )
		{
			case PaintLayer::Visual:
				if ( _bErase == false )
				{
					_listVisual[tileIndex]._height	= static_cast<uint8>( _paintHeight );
					_listVisual[tileIndex]._atlasId = static_cast<uint8>( _atlasId );
					_listVisual[tileIndex]._tintR	= static_cast<uint8>( MathUtil::clamp( _arrTint[0] * 255.0f, 0.0f, 255.0f ) );
					_listVisual[tileIndex]._tintG	= static_cast<uint8>( MathUtil::clamp( _arrTint[1] * 255.0f, 0.0f, 255.0f ) );
					_listVisual[tileIndex]._tintB	= static_cast<uint8>( MathUtil::clamp( _arrTint[2] * 255.0f, 0.0f, 255.0f ) );
				}
				break;
			case PaintLayer::Walkable:
				_listWalkable[tileIndex] = _bErase ? 0 : 1;
				break;
			case PaintLayer::Encounter:
				_listEncounter[tileIndex] = _bErase ? 0 : 1;
				break;
			case PaintLayer::PassThrough:
				_listPassThrough[tileIndex] = _bErase ? 0 : 1;
				break;
			case PaintLayer::Warp:
			{
				_listWarp.erase( std::remove_if( _listWarp.begin(), _listWarp.end(),
												 [x, y]( const EditorTileWarp& warp )
				{ return warp._tileX == x && warp._tileY == y; } ),
								 _listWarp.end() );
				if ( _bErase == false && _arrWarpTarget[0] != '\0' )
				{
					EditorTileWarp warpItem{};
					warpItem._tileX		  = x;
					warpItem._tileY		  = y;
					warpItem._targetMap	  = _arrWarpTarget;
					warpItem._targetTileX = _warpTx;
					warpItem._targetTileY = _warpTy;
					_listWarp.push_back( std::move( warpItem ) );
					_listWalkable[tileIndex] = 1;
				}
				break;
			}
		}
	}

	void TileMapPanel::paintEdgeWarp( int32 edge )
	{
		const utf8* targets[] = { _arrEdgeTargetN, _arrEdgeTargetE, _arrEdgeTargetS, _arrEdgeTargetW };
		if ( edge < 0 || edge > 3 || targets[edge][0] == '\0' )
			return;

		auto stamp = [&]( int32 tileX, int32 tileY )
		{
			_listWalkable[indexOf( tileX, tileY )] = 1;
			_listWarp.erase( std::remove_if( _listWarp.begin(), _listWarp.end(),
											 [tileX, tileY]( const EditorTileWarp& warp )
			{ return warp._tileX == tileX && warp._tileY == tileY; } ),
							 _listWarp.end() );
			EditorTileWarp warpItem{};
			warpItem._tileX		  = tileX;
			warpItem._tileY		  = tileY;
			warpItem._targetMap	  = targets[edge];
			warpItem._targetTileX = _arrEdgeTx[edge];
			warpItem._targetTileY = _arrEdgeTy[edge];
			_listWarp.push_back( std::move( warpItem ) );
		};

		switch ( edge )
		{
			case 0:
				for ( int32 tileX = 0; tileX < _width; ++tileX )
				{
					stamp( tileX, 0 );
				}
				break;
			case 1:
				for ( int32 tileY = 0; tileY < _height; ++tileY )
				{
					stamp( _width - 1, tileY );
				}
				break;
			case 2:
				for ( int32 tileX = 0; tileX < _width; ++tileX )
				{
					stamp( tileX, _height - 1 );
				}
				break;
			case 3:
				for ( int32 tileY = 0; tileY < _height; ++tileY )
				{
					stamp( 0, tileY );
				}
				break;
			default:
				break;
		}
	}

	bool TileMapPanel::inBounds( int32 x, int32 y ) const
	{
		return x >= 0 && y >= 0 && x < _width && y < _height;
	}

	size_t TileMapPanel::indexOf( int32 x, int32 y ) const
	{
		return static_cast<size_t>( y * _width + x );
	}
} // namespace sw::editor
