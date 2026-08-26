#include "pch.h"

#include "Editor/Panels/SpriteClipPanel.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/Config/EditorData.h"
#include "Editor/Common/EditorUtil.h"

#include "Engine/Serialization/Format/JsonSerializer.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>

namespace sw::editor
{
	namespace
	{
		bool parseFloatAfter( string_view src, size_t from, const utf8* pKey, float32& out )
		{
			const size_t klen = StringUtil::strlen( pKey );
			if ( klen == 0 || from >= src.size() )
				return false;

			for ( size_t sliceIndex = from; sliceIndex + klen + 2 <= src.size(); ++sliceIndex )
			{
				if ( src[sliceIndex] == '"' && src[sliceIndex + klen + 1] == '"' &&
					 src.substr( sliceIndex + 1, klen ) == string_view{ pKey, klen } )
				{
					const size_t colon = src.find( ':', sliceIndex + klen + 2 );
					if ( colon == string_view::npos )
						return false;
					out = static_cast<float32>( StringUtil::atof( src.data() + colon + 1 ) );
					return true;
				}
			}
			return false;
		}

		bool parseIntAfter( string_view src, size_t from, const utf8* pKey, int32& out )
		{
			const size_t klen = StringUtil::strlen( pKey );
			if ( klen == 0 || from >= src.size() )
				return false;

			for ( size_t sliceIndex = from; sliceIndex + klen + 2 <= src.size(); ++sliceIndex )
			{
				if ( src[sliceIndex] == '"' && src[sliceIndex + klen + 1] == '"' &&
					 src.substr( sliceIndex + 1, klen ) == string_view{ pKey, klen } )
				{
					const size_t colon = src.find( ':', sliceIndex + klen + 2 );
					if ( colon == string_view::npos )
						return false;
					out = StringUtil::atoi( src.data() + colon + 1 );
					return true;
				}
			}
			return false;
		}

	} // namespace

	SpriteClipPanel::SpriteClipPanel()
		: IEditorPanel( false )
		, _arrAtlasPath{}
		, _listFrames{}
		, _listKeys{}
		, _selectedFrame{ -1 }
		, _selectedKey{ -1 }
		, _status{}
	{
		const string& atlas = editor::getEditorData()._spriteAtlas;
		if ( atlas.empty() == false )
			StringUtil::strncpy( _arrAtlasPath, atlas.c_str(), sizeof( _arrAtlasPath ) - 1 );
		_listFrames.push_back( Frame{} );
	}

	void SpriteClipPanel::drawContent()
	{
		ImGui::InputText( "Atlas", _arrAtlasPath, sizeof( _arrAtlasPath ) );
		if ( ImGui::Button( "Load" ) )
			loadJson();
		ImGui::SameLine();
		if ( ImGui::Button( "Save" ) )
		{
			saveJson();
			_status = "Saved SpriteClip.json";
		}
		ImGui::TextDisabled( "%s/%s/%s (separate from AnimGraph)", EditorConfig::getActive()._configFolder.c_str(),
							 EditorConfig::getActive()._editorConfigFolder.c_str(), EditorConfig::getActive()._spriteClipFile.c_str() );

		ImGui::Separator();
		ImGui::TextUnformatted( "Frames (u,v,w,h,durationMs)" );
		if ( ImGui::Button( "Add Frame" ) )
		{
			_listFrames.push_back( Frame{} );
			_selectedFrame = static_cast<int32>( _listFrames.size() ) - 1;
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Remove Frame" ) && _selectedFrame >= 0 &&
			 _selectedFrame < static_cast<int32>( _listFrames.size() ) )
		{
			_listFrames.erase( _listFrames.begin() + _selectedFrame );
			if ( _selectedFrame >= static_cast<int32>( _listFrames.size() ) )
				_selectedFrame = static_cast<int32>( _listFrames.size() ) - 1;
		}

		for ( int32 frameIndex = 0; frameIndex < static_cast<int32>( _listFrames.size() ); ++frameIndex )
		{
			ImGui::PushID( frameIndex );
			utf8 arrLabel[32];
			formatstring( arrLabel, sizeof( arrLabel ), "Frame %#", frameIndex );
			if ( ImGui::Selectable( arrLabel, _selectedFrame == frameIndex ) )
				_selectedFrame = frameIndex;
			ImGui::PopID();
		}

		if ( _selectedFrame >= 0 && _selectedFrame < static_cast<int32>( _listFrames.size() ) )
		{
			Frame& f = _listFrames[static_cast<size_t>( _selectedFrame )];
			ImGui::DragFloat( "u", &f._u, 0.01f );
			ImGui::DragFloat( "v", &f._v, 0.01f );
			ImGui::DragFloat( "w", &f._w, 0.01f );
			ImGui::DragFloat( "h", &f._h, 0.01f );
			ImGui::InputInt( "durationMs", &f._durationMs );
		}

		ImGui::Separator();
		ImGui::TextUnformatted( "TransformAnimation Keys (optional)" );
		if ( ImGui::Button( "Add Key" ) )
		{
			_listKeys.push_back( TransformKey{} );
			_selectedKey = static_cast<int32>( _listKeys.size() ) - 1;
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Remove Key" ) && _selectedKey >= 0 &&
			 _selectedKey < static_cast<int32>( _listKeys.size() ) )
		{
			_listKeys.erase( _listKeys.begin() + _selectedKey );
			if ( _selectedKey >= static_cast<int32>( _listKeys.size() ) )
				_selectedKey = static_cast<int32>( _listKeys.size() ) - 1;
		}

		for ( int32 keyIndex = 0; keyIndex < static_cast<int32>( _listKeys.size() ); ++keyIndex )
		{
			ImGui::PushID( 1000 + keyIndex );
			utf8 arrLabel[32];
			formatstring( arrLabel, sizeof( arrLabel ), "Key %#", keyIndex );
			if ( ImGui::Selectable( arrLabel, _selectedKey == keyIndex ) )
				_selectedKey = keyIndex;
			ImGui::PopID();
		}

		if ( _selectedKey >= 0 && _selectedKey < static_cast<int32>( _listKeys.size() ) )
		{
			TransformKey& k = _listKeys[static_cast<size_t>( _selectedKey )];
			ImGui::DragFloat( "time", &k._time, 0.01f );
			ImGui::DragFloat( "x", &k._x, 0.1f );
			ImGui::DragFloat( "y", &k._y, 0.1f );
			ImGui::DragFloat( "angleDeg", &k._angleDeg, 0.5f );
		}

		if ( _status.empty() == false )
		{
			ImGui::Separator();
			ImGui::TextDisabled( "%s", _status.c_str() );
		}
	}

	void SpriteClipPanel::loadJson()
	{
		const string path = EditorUtil::resolveEditorConfigFile( EditorConfig::getActive()._spriteClipFile.c_str() );
		if ( path.empty() || FileUtil::fileExists( path ) == false )
		{
			_status = "No SpriteClip.json yet";
			return;
		}

		string json;
		if ( FileUtil::readTextFile( path, json ) == false || json.empty() )
		{
			_status = "Failed to read SpriteClip.json";
			return;
		}

		const string atlas = JsonSerializer::extractStringField( json, "atlas" );
		if ( atlas.empty() == false )
			StringUtil::strncpy( _arrAtlasPath, atlas.c_str(), sizeof( _arrAtlasPath ) - 1 );

		_listFrames.clear();
		_listKeys.clear();

		size_t framesPos = json.find( "\"frames\"" );
		if ( framesPos != string::npos )
		{
			size_t arr = json.find( '[', framesPos );
			size_t end = json.find( ']', arr );
			if ( arr != string::npos && end != string::npos )
			{
				size_t cursor = arr;
				while ( true )
				{
					const size_t obj = json.find( '{', cursor );
					if ( obj == string::npos || obj > end )
						break;
					Frame f{};
					parseFloatAfter( json, obj, "u", f._u );
					parseFloatAfter( json, obj, "v", f._v );
					parseFloatAfter( json, obj, "w", f._w );
					parseFloatAfter( json, obj, "h", f._h );
					parseIntAfter( json, obj, "durationMs", f._durationMs );
					_listFrames.push_back( f );
					cursor = json.find( '}', obj );
					if ( cursor == string::npos )
						break;
					++cursor;
				}
			}
		}

		size_t keysPos = json.find( "\"transformKeys\"" );
		if ( keysPos != string::npos )
		{
			size_t arr = json.find( '[', keysPos );
			size_t end = json.find( ']', arr );
			if ( arr != string::npos && end != string::npos )
			{
				size_t cursor = arr;
				while ( true )
				{
					const size_t obj = json.find( '{', cursor );
					if ( obj == string::npos || obj > end )
						break;
					TransformKey k{};
					parseFloatAfter( json, obj, "time", k._time );
					parseFloatAfter( json, obj, "x", k._x );
					parseFloatAfter( json, obj, "y", k._y );
					parseFloatAfter( json, obj, "angleDeg", k._angleDeg );
					_listKeys.push_back( k );
					cursor = json.find( '}', obj );
					if ( cursor == string::npos )
						break;
					++cursor;
				}
			}
		}

		_selectedFrame = _listFrames.empty() ? -1 : 0;
		_selectedKey   = _listKeys.empty() ? -1 : 0;
		_status		   = "Loaded SpriteClip.json";
	}

	void SpriteClipPanel::saveJson() const
	{
		const string path = EditorUtil::resolveEditorConfigFile( EditorConfig::getActive()._spriteClipFile.c_str() );
		if ( path.empty() )
			return;

		StringBuilder<2048> sb;
		sb.append( "{\n" );
		sb.append( "  \"atlas\": \"" ).append( JsonSerializer::escapeString( _arrAtlasPath ).c_str() ).append( "\",\n" );
		sb.append( "  \"frames\": [\n" );
		for ( size_t frameIndex = 0; frameIndex < _listFrames.size(); ++frameIndex )
		{
			const Frame& f = _listFrames[frameIndex];
			sb.append( "    { \"u\": " ).append( f._u ).append( ", \"v\": " ).append( f._v ).append( ", \"w\": " ).append( f._w ).append( ", \"h\": " ).append( f._h ).append( ", \"durationMs\": " ).append( f._durationMs ).append( " }" );
			if ( frameIndex + 1 < _listFrames.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ],\n" );
		sb.append( "  \"transformKeys\": [\n" );
		for ( size_t keyIndex = 0; keyIndex < _listKeys.size(); ++keyIndex )
		{
			const TransformKey& k = _listKeys[keyIndex];
			sb.append( "    { \"time\": " ).append( k._time ).append( ", \"x\": " ).append( k._x ).append( ", \"y\": " ).append( k._y ).append( ", \"angleDeg\": " ).append( k._angleDeg ).append( " }" );
			if ( keyIndex + 1 < _listKeys.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ]\n" );
		sb.append( "}\n" );

		const string text( sb.c_str() );
		if ( FileUtil::writeFile( path, reinterpret_cast<const uint8*>( text.data() ),
								  text.size() ) )
			SW_LOG_INFO( "[SpriteClip] Saved %#", path.c_str() );
	}
} // namespace sw::editor
