#include "pch.h"

#include "Editor/Panels/SpriteClipPanel.h"

#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/Config/EditorData.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"

#include <imgui.h>

namespace sw::editor
{
	SW_LOG_CALLER( "SpriteClip" );

	SpriteClipPanel::SpriteClipPanel()
		: EditorDocumentPanel{ EditorAssetKind::SpriteClip, false }
		, _atlasPath{}
		, _status{}
		, _listFrame{}
		, _listKey{}
		, _selectedFrame{ -1 }
		, _selectedKey{ -1 }
	{
		const string& atlas = editor::getEditorData()._spriteAtlas;
		if ( atlas.empty() == false )
			_atlasPath = atlas.c_str();
		_listFrame.push_back( Frame{} );
	}

	void SpriteClipPanel::drawContent()
	{
		updateFocusedDocument();
		if ( isDocumentLoaded() == false )
		{
			if ( EditorUtil::isTextureAssetPath( getLoadedAssetPath().c_str() ) )
			{
				_atlasPath = getLoadedAssetPath().c_str();
			}
			else
				loadJson();
			markDocumentLoaded();
		}

		ImGui::InputText( "Atlas", _atlasPath.data(), _atlasPath.capacity() );
		if ( ImGui::IsItemDeactivatedAfterEdit() )
			notifyDocumentEdited( "Edit Sprite Clip", "sprite-clip" );
		if ( ImGui::Button( "Load" ) )
			loadJson();
		ImGui::SameLine();
		if ( ImGui::Button( "Save" ) )
		{
			saveJson();
			if ( getLoadedAssetPath().empty() )
				_status = string{ "Saved " } + EditorConfig::getActive()._spriteClipFile;
			else
				_status = string{ "Saved " } + getLoadedAssetPath();
		}
		ImGui::TextDisabled( "%s/%s/%s (separate from AnimGraph)", EditorConfig::getActive()._configFolder.c_str(),
							 EditorConfig::getActive()._editorConfigFolder.c_str(), EditorConfig::getActive()._spriteClipFile.c_str() );

		ImGui::Separator();
		ImGui::TextUnformatted( "Frames (u,v,w,h,durationMs)" );
		if ( ImGui::Button( "Add Frame" ) )
		{
			_listFrame.push_back( Frame{} );
			_selectedFrame = static_cast<int32>( _listFrame.size() ) - 1;
			notifyDocumentEdited( "Add Sprite Frame" );
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Remove Frame" ) && 0 <= _selectedFrame &&
			 _selectedFrame < static_cast<int32>( _listFrame.size() ) )
		{
			_listFrame.erase( _listFrame.begin() + _selectedFrame );
			if ( _selectedFrame >= static_cast<int32>( _listFrame.size() ) )
				_selectedFrame = static_cast<int32>( _listFrame.size() ) - 1;
			notifyDocumentEdited( "Remove Sprite Frame" );
		}

		for ( int32 frameIndex = 0; frameIndex < static_cast<int32>( _listFrame.size() ); ++frameIndex )
		{
			ImGui::PushID( frameIndex );
			fixed_string<constant::kMaxBuffer32> label;
			formatstring( label.data(), label.capacity(), "Frame %#", frameIndex );
			if ( ImGui::Selectable( label.c_str(), _selectedFrame == frameIndex ) )
				_selectedFrame = frameIndex;
			ImGui::PopID();
		}

		if ( 0 <= _selectedFrame && _selectedFrame < static_cast<int32>( _listFrame.size() ) )
		{
			Frame& f = _listFrame[static_cast<size_t>( _selectedFrame )];
			ImGui::DragFloat( "u", &f._u, 0.01f );
			if ( ImGui::IsItemDeactivatedAfterEdit() )
				notifyDocumentEdited( "Edit Sprite Frame", "sprite-clip-frame" );
			ImGui::DragFloat( "v", &f._v, 0.01f );
			if ( ImGui::IsItemDeactivatedAfterEdit() )
				notifyDocumentEdited( "Edit Sprite Frame", "sprite-clip-frame" );
			ImGui::DragFloat( "w", &f._w, 0.01f );
			if ( ImGui::IsItemDeactivatedAfterEdit() )
				notifyDocumentEdited( "Edit Sprite Frame", "sprite-clip-frame" );
			ImGui::DragFloat( "h", &f._h, 0.01f );
			if ( ImGui::IsItemDeactivatedAfterEdit() )
				notifyDocumentEdited( "Edit Sprite Frame", "sprite-clip-frame" );
			ImGui::InputInt( "durationMs", &f._durationMs );
			if ( ImGui::IsItemDeactivatedAfterEdit() )
				notifyDocumentEdited( "Edit Sprite Frame", "sprite-clip-frame" );
		}

		ImGui::Separator();
		ImGui::TextUnformatted( "TransformAnimation Keys (optional)" );
		if ( ImGui::Button( "Add Key" ) )
		{
			_listKey.push_back( TransformKey{} );
			_selectedKey = static_cast<int32>( _listKey.size() ) - 1;
			notifyDocumentEdited( "Add Sprite Key" );
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Remove Key" ) && 0 <= _selectedKey &&
			 _selectedKey < static_cast<int32>( _listKey.size() ) )
		{
			_listKey.erase( _listKey.begin() + _selectedKey );
			if ( _selectedKey >= static_cast<int32>( _listKey.size() ) )
				_selectedKey = static_cast<int32>( _listKey.size() ) - 1;
			notifyDocumentEdited( "Remove Sprite Key" );
		}

		for ( int32 keyIndex = 0; keyIndex < static_cast<int32>( _listKey.size() ); ++keyIndex )
		{
			ImGui::PushID( 1000 + keyIndex );
			fixed_string<constant::kMaxBuffer32> label;
			formatstring( label.data(), label.capacity(), "Key %#", keyIndex );
			if ( ImGui::Selectable( label.c_str(), _selectedKey == keyIndex ) )
				_selectedKey = keyIndex;
			ImGui::PopID();
		}

		if ( 0 <= _selectedKey && _selectedKey < static_cast<int32>( _listKey.size() ) )
		{
			TransformKey& k = _listKey[static_cast<size_t>( _selectedKey )];
			ImGui::DragFloat( "time", &k._time, 0.01f );
			if ( ImGui::IsItemDeactivatedAfterEdit() )
				notifyDocumentEdited( "Edit Sprite Key", "sprite-clip-key" );
			ImGui::DragFloat( "x", &k._x, 0.1f );
			if ( ImGui::IsItemDeactivatedAfterEdit() )
				notifyDocumentEdited( "Edit Sprite Key", "sprite-clip-key" );
			ImGui::DragFloat( "y", &k._y, 0.1f );
			if ( ImGui::IsItemDeactivatedAfterEdit() )
				notifyDocumentEdited( "Edit Sprite Key", "sprite-clip-key" );
			ImGui::DragFloat( "angleDeg", &k._angleDeg, 0.5f );
			if ( ImGui::IsItemDeactivatedAfterEdit() )
				notifyDocumentEdited( "Edit Sprite Key", "sprite-clip-key" );
		}

		EditorWidgets::drawPanelStatus( _status.c_str() );
	}

	void SpriteClipPanel::loadJson()
	{
		EditorSpriteClipData data;
		if ( EditorToolAssetCommands::loadSpriteClip( data, _status, getLoadedAssetPath() ) == false )
			return;

		if ( data._atlasPath.empty() == false )
			_atlasPath = data._atlasPath.c_str();
		_listFrame	   = std::move( data._listFrame );
		_listKey	   = std::move( data._listKey );
		_selectedFrame = _listFrame.empty() ? -1 : 0;
		_selectedKey   = _listKey.empty() ? -1 : 0;
		syncDocumentUndoBaseline();
	}

	void SpriteClipPanel::saveJson()
	{
		if ( getLoadedAssetPath().empty() )
			return;
		if ( EditorToolAssetCommands::saveSpriteClip( captureClipData(), getLoadedAssetPath() ) )
		{
			clearDocumentDirty();
			syncDocumentUndoBaseline();
		}
	}

	bool SpriteClipPanel::saveDocument()
	{
		if ( getLoadedAssetPath().empty() )
			return false;
		if ( EditorToolAssetCommands::saveSpriteClip( captureClipData(), getLoadedAssetPath() ) )
		{
			clearDocumentDirty();
			syncDocumentUndoBaseline();
			return true;
		}
		return false;
	}

	EditorSpriteClipData SpriteClipPanel::captureClipData() const
	{
		EditorSpriteClipData data;
		data._atlasPath = _atlasPath.c_str();
		data._listFrame = _listFrame;
		data._listKey	= _listKey;
		return data;
	}

	string SpriteClipPanel::captureDocumentText() const
	{
		return EditorToolAssetCommands::serializeSpriteClip( captureClipData() );
	}

	void SpriteClipPanel::applyDocumentText( string_view text )
	{
		EditorSpriteClipData restored;
		if ( text.empty() == false )
			EditorToolAssetCommands::parseSpriteClip( text, restored );
		if ( restored._atlasPath.empty() == false )
			_atlasPath = restored._atlasPath.c_str();
		_listFrame	   = std::move( restored._listFrame );
		_listKey	   = std::move( restored._listKey );
		_selectedFrame = _listFrame.empty() ? -1 : 0;
		_selectedKey   = _listKey.empty() ? -1 : 0;
	}
} // namespace sw::editor
