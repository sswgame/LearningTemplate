#include "pch.h"

#include "Editor/Panels/TileMapPanel.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Config/EditorData.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include <imgui.h>

namespace sw::editor
{
	SW_LOG_CALLER( "TileMapPanel" );

	TileMapPanel::TileMapPanel()
		: EditorDocumentPanel{ EditorAssetKind::TileMap, false }
		, _pathBuffer{}
		, _nameBuffer{ "Untitled" }
		, _edgeTargetN{}
		, _edgeTargetE{}
		, _edgeTargetS{}
		, _edgeTargetW{}
		, _warpTarget{}
		, _scenePath{}
		, _role{}
		, _status{}
		, _listWalkable{}
		, _listEncounter{}
		, _listPassThrough{}
		, _listVisual{}
		, _listWarp{}
		, _listEncounterEntry{}
		, _arrEdgeTx{ 1, 1, 1, 1 }
		, _arrEdgeTy{ 1, 1, 1, 1 }
		, _arrTint{ 180.0f / 255.0f, 200.0f / 255.0f, 160.0f / 255.0f }
		, _width{ 8 }
		, _height{ 8 }
		, _inputWidth{ 8 }
		, _inputHeight{ 8 }
		, _paintHeight{ 1 }
		, _atlasId{ 0 }
		, _warpTx{ 1 }
		, _warpTy{ 1 }
		, _spawnX{ 1 }
		, _spawnY{ 1 }
		, _layer{ PaintLayer::Walkable }
		, _bErase{ false }
	{
		const EditorData& editorData = editor::getEditorData();
		if ( editorData._defaultMap.empty() == false )
			_pathBuffer = editorData._defaultMap.c_str();
		if ( editorData._warpMap.empty() == false )
			_warpTarget = editorData._warpMap.c_str();
		resize( 8, 8 );
	}

	void TileMapPanel::drawContent()
	{
		updateFocusedDocument();
		if ( isDocumentLoaded() == false )
		{
			_pathBuffer = getLoadedAssetPath().c_str();
			if ( getLoadedAssetPath().empty() == false )
				loadXml( _pathBuffer.c_str() );
			markDocumentLoaded();
		}

		const string& focused = EditorContext::get()->getWorkspace().getFocusedAssetPath();
		if ( focused.empty() == false )
			ImGui::TextDisabled( "Focused: %s", focused.c_str() );

		ImGui::InputText( "Path", _pathBuffer.data(), _pathBuffer.capacity() );
		ImGui::InputText( "Name", _nameBuffer.data(), _nameBuffer.capacity() );
		if ( ImGui::IsItemDeactivatedAfterEdit() )
			notifyDocumentEdited( "Edit Tile Map Name", "tilemap-name" );
		ImGui::InputInt( "Width", &_inputWidth );
		ImGui::SameLine();
		ImGui::InputInt( "Height", &_inputHeight );
		if ( ImGui::Button( "Apply Size" ) )
		{
			resize( MathUtil::max( 1, _inputWidth ), MathUtil::max( 1, _inputHeight ) );
			notifyDocumentEdited( "Resize Tile Map" );
		}

		ImGui::SameLine();
		if ( ImGui::Button( "Load" ) )
		{
			if ( loadXml( _pathBuffer.c_str() ) == false )
				SW_LOG_WARNING( "%#", _status.c_str() );
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Save" ) )
		{
			if ( saveXml( _pathBuffer.c_str() ) )
			{
				_status = string( "Saved " ) + _pathBuffer.c_str();
				clearDocumentDirty();
			}
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
			ImGui::InputText( "Warp Target", _warpTarget.data(), _warpTarget.capacity() );
			ImGui::InputInt( "Target TX", &_warpTx );
			ImGui::InputInt( "Target TY", &_warpTy );
		}

		ImGui::Separator();
		ImGui::TextUnformatted( "Edge Warp Presets" );
		const utf8*							   edges[] = { "N", "E", "S", "W" };
		fixed_string<constant::kMaxBuffer128>* bufs[]  = { &_edgeTargetN, &_edgeTargetE, &_edgeTargetS, &_edgeTargetW };
		for ( int32 edgeIndex = 0; edgeIndex < 4; ++edgeIndex )
		{
			ImGui::PushID( edgeIndex );
			ImGui::SetNextItemWidth( 180.0f );
			ImGui::InputText( edges[edgeIndex], bufs[edgeIndex]->data(), bufs[edgeIndex]->capacity() );
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
			for ( const TileMapXmlData::Warp& warp : _listWarp )
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
					default:
						break;
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

		EditorWidgets::drawPanelStatus( _status.c_str() );
	}

	void TileMapPanel::resize( int32 width, int32 height )
	{
		if ( width <= 0 || height <= 0 )
			return;
		_width			   = width;
		_height			   = height;
		_inputWidth		   = width;
		_inputHeight	   = height;
		const size_t count = static_cast<size_t>( _width * _height );
		_listWalkable.assign( count, 1 );
		_listEncounter.assign( count, 0 );
		_listPassThrough.assign( count, 0 );
		_listVisual.assign( count, TileMapXmlData::Visual{} );
		_listWarp.clear();
	}

	bool TileMapPanel::loadXml( string_view assetRelativePath )
	{
		TileMapXmlData data;
		if ( EditorToolAssetCommands::loadTileMap( assetRelativePath, data, _status ) == false )
			return false;

		_nameBuffer			= data._name.c_str();
		_width				= data._width;
		_height				= data._height;
		_inputWidth			= data._width;
		_inputHeight		= data._height;
		_listWalkable		= std::move( data._listWalkable );
		_listEncounter		= std::move( data._listEncounter );
		_listPassThrough	= std::move( data._listPassThrough );
		_listVisual			= std::move( data._listVisual );
		_listWarp			= std::move( data._listWarp );
		_listEncounterEntry = std::move( data._listEncounterEntry );
		_scenePath			= data._scenePath;
		_role				= data._role;
		_spawnX				= data._spawnX;
		_spawnY				= data._spawnY;
		_pathBuffer			= string( assetRelativePath ).c_str();
		syncDocumentUndoBaseline();
		return true;
	}

	bool TileMapPanel::saveXml( string_view assetRelativePath )
	{
		if ( EditorToolAssetCommands::saveTileMap( assetRelativePath, captureMapData() ) == false )
			return false;
		clearDocumentDirty();
		syncDocumentUndoBaseline();
		return true;
	}

	bool TileMapPanel::saveDocument()
	{
		if ( saveXml( _pathBuffer.c_str() ) == false )
			return false;
		return true;
	}

	TileMapXmlData TileMapPanel::captureMapData() const
	{
		TileMapXmlData data;
		data._name				 = _nameBuffer.c_str();
		data._width				 = _width;
		data._height			 = _height;
		data._listWalkable		 = _listWalkable;
		data._listEncounter		 = _listEncounter;
		data._listPassThrough	 = _listPassThrough;
		data._listVisual		 = _listVisual;
		data._listWarp			 = _listWarp;
		data._scenePath			 = _scenePath;
		data._role				 = _role;
		data._spawnX			 = _spawnX;
		data._spawnY			 = _spawnY;
		data._listEncounterEntry = _listEncounterEntry;
		return data;
	}

	void TileMapPanel::applyMapData( const TileMapXmlData& data )
	{
		_nameBuffer			= data._name.c_str();
		_width				= data._width;
		_height				= data._height;
		_listWalkable		= data._listWalkable;
		_listEncounter		= data._listEncounter;
		_listPassThrough	= data._listPassThrough;
		_listVisual			= data._listVisual;
		_listWarp			= data._listWarp;
		_listEncounterEntry = data._listEncounterEntry;
		_scenePath			= data._scenePath;
		_role				= data._role;
		_spawnX				= data._spawnX;
		_spawnY				= data._spawnY;
	}

	string TileMapPanel::captureDocumentText() const
	{
		return captureMapData().toXml();
	}

	void TileMapPanel::applyDocumentText( string_view text )
	{
		TileMapXmlData restored;
		if ( text.empty() == false )
			restored.loadFromXml( text );
		applyMapData( restored );
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
												 [x, y]( const TileMapXmlData::Warp& warp )
				{ return warp._tileX == x && warp._tileY == y; } ),
								 _listWarp.end() );
				if ( _bErase == false && _warpTarget.empty() == false )
				{
					TileMapXmlData::Warp warpItem{};
					warpItem._tileX		  = x;
					warpItem._tileY		  = y;
					warpItem._targetMap	  = _warpTarget.c_str();
					warpItem._targetTileX = _warpTx;
					warpItem._targetTileY = _warpTy;
					_listWarp.push_back( std::move( warpItem ) );
					_listWalkable[tileIndex] = 1;
				}
				break;
			}
			default:
				break;
		}
		notifyDocumentEdited( "Paint Tile Map", "tilemap-paint" );
	}

	void TileMapPanel::paintEdgeWarp( int32 edge )
	{
		const fixed_string<constant::kMaxBuffer128>* targets[] = { &_edgeTargetN, &_edgeTargetE, &_edgeTargetS, &_edgeTargetW };
		if ( edge < 0 || edge > 3 || targets[edge]->empty() )
			return;

		auto stamp = [&]( int32 tileX, int32 tileY )
		{
			_listWalkable[indexOf( tileX, tileY )] = 1;
			_listWarp.erase( std::remove_if( _listWarp.begin(), _listWarp.end(),
											 [tileX, tileY]( const TileMapXmlData::Warp& warp )
			{ return warp._tileX == tileX && warp._tileY == tileY; } ),
							 _listWarp.end() );
			TileMapXmlData::Warp warpItem{};
			warpItem._tileX		  = tileX;
			warpItem._tileY		  = tileY;
			warpItem._targetMap	  = targets[edge]->c_str();
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
		notifyDocumentEdited( "Paint Tile Map Edge", "tilemap-edge" );
	}

	bool TileMapPanel::inBounds( int32 x, int32 y ) const
	{
		return 0 <= x && x < _width && 0 <= y && y < _height;
	}

	size_t TileMapPanel::indexOf( int32 x, int32 y ) const
	{
		return static_cast<size_t>( y * _width + x );
	}
} // namespace sw::editor
