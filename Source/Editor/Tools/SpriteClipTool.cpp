/**
 * @file SpriteClipTool.cpp
 */
#include "Tools/SpriteClipTool.h"
#include "EditorUtil.h"
#include "Runtime/EditorUIContext.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"

#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace sw
{
	namespace
	{
		std::string escapeJson( const std::string& s )
		{
			std::string out;
			out.reserve( s.size() + 8 );
			for ( char c : s )
			{
				if ( c == '\\' || c == '"' )
					out.push_back( '\\' );
				out.push_back( c );
			}
			return out;
		}

		bool extractStringField( const std::string& json, const char* key, std::string& out )
		{
			const std::string needle = std::string( "\"" ) + key + "\"";
			const size_t	  pos	 = json.find( needle );
			if ( pos == std::string::npos )
				return false;
			const size_t colon = json.find( ':', pos + needle.size() );
			if ( colon == std::string::npos )
				return false;
			const size_t q0 = json.find( '"', colon + 1 );
			if ( q0 == std::string::npos )
				return false;
			const size_t q1 = json.find( '"', q0 + 1 );
			if ( q1 == std::string::npos )
				return false;
			out.assign( json, q0 + 1, q1 - q0 - 1 );
			return true;
		}

		bool parseFloatAfter( const std::string& src, size_t from, const char* key, float32& out )
		{
			const std::string needle = std::string( "\"" ) + key + "\"";
			const size_t	  pos	 = src.find( needle, from );
			if ( pos == std::string::npos )
				return false;
			const size_t colon = src.find( ':', pos + needle.size() );
			if ( colon == std::string::npos )
				return false;
			out = static_cast<float32>( std::atof( src.c_str() + colon + 1 ) );
			return true;
		}

		bool parseIntAfter( const std::string& src, size_t from, const char* key, int32& out )
		{
			const std::string needle = std::string( "\"" ) + key + "\"";
			const size_t	  pos	 = src.find( needle, from );
			if ( pos == std::string::npos )
				return false;
			const size_t colon = src.find( ':', pos + needle.size() );
			if ( colon == std::string::npos )
				return false;
			out = std::atoi( src.c_str() + colon + 1 );
			return true;
		}
	} // namespace

	SpriteClipTool::SpriteClipTool()
		: IEditorWindow( false )
	{
		std::strncpy( _atlasPath, "Game/Dungreed/Texture/00_Character/00_Player/00_Player.png", sizeof( _atlasPath ) - 1 );
		_frames.push_back( Frame{} );
	}

	void SpriteClipTool::loadJson()
	{
		const std::filesystem::path path = EditorUtil::resolveEditorConfigFile( "SpriteClip.json" );
		if ( path.empty() || FileUtil::isFileExist( path.string() ) == false )
		{
			_status = "No SpriteClip.json yet";
			return;
		}

		std::vector<uint8> data;
		if ( FileUtil::readFile( path.string(), data ) == false || data.empty() )
		{
			_status = "Failed to read SpriteClip.json";
			return;
		}

		const std::string json( data.begin(), data.end() );
		std::string		  atlas;
		if ( extractStringField( json, "atlas", atlas ) )
			std::strncpy( _atlasPath, atlas.c_str(), sizeof( _atlasPath ) - 1 );

		_frames.clear();
		_keys.clear();

		size_t framesPos = json.find( "\"frames\"" );
		if ( framesPos != std::string::npos )
		{
			size_t arr = json.find( '[', framesPos );
			size_t end = json.find( ']', arr );
			if ( arr != std::string::npos && end != std::string::npos )
			{
				size_t cursor = arr;
				while ( true )
				{
					const size_t obj = json.find( '{', cursor );
					if ( obj == std::string::npos || obj > end )
						break;
					Frame f{};
					parseFloatAfter( json, obj, "u", f.u );
					parseFloatAfter( json, obj, "v", f.v );
					parseFloatAfter( json, obj, "w", f.w );
					parseFloatAfter( json, obj, "h", f.h );
					parseIntAfter( json, obj, "durationMs", f.durationMs );
					_frames.push_back( f );
					cursor = json.find( '}', obj );
					if ( cursor == std::string::npos )
						break;
					++cursor;
				}
			}
		}

		size_t keysPos = json.find( "\"transformKeys\"" );
		if ( keysPos != std::string::npos )
		{
			size_t arr = json.find( '[', keysPos );
			size_t end = json.find( ']', arr );
			if ( arr != std::string::npos && end != std::string::npos )
			{
				size_t cursor = arr;
				while ( true )
				{
					const size_t obj = json.find( '{', cursor );
					if ( obj == std::string::npos || obj > end )
						break;
					TransformKey k{};
					parseFloatAfter( json, obj, "time", k.time );
					parseFloatAfter( json, obj, "x", k.x );
					parseFloatAfter( json, obj, "y", k.y );
					parseFloatAfter( json, obj, "angleDeg", k.angleDeg );
					_keys.push_back( k );
					cursor = json.find( '}', obj );
					if ( cursor == std::string::npos )
						break;
					++cursor;
				}
			}
		}

		_selectedFrame = _frames.empty() ? -1 : 0;
		_selectedKey   = _keys.empty() ? -1 : 0;
		_status		   = "Loaded SpriteClip.json";
	}

	void SpriteClipTool::saveJson() const
	{
		const std::filesystem::path path = EditorUtil::resolveEditorConfigFile( "SpriteClip.json" );
		if ( path.empty() )
			return;

		std::ostringstream oss;
		oss << "{\n";
		oss << "  \"atlas\": \"" << escapeJson( _atlasPath ) << "\",\n";
		oss << "  \"frames\": [\n";
		for ( size_t i = 0; i < _frames.size(); ++i )
		{
			const Frame& f = _frames[i];
			oss << "    { \"u\": " << f.u << ", \"v\": " << f.v << ", \"w\": " << f.w << ", \"h\": " << f.h
				<< ", \"durationMs\": " << f.durationMs << " }";
			if ( i + 1 < _frames.size() )
				oss << ",";
			oss << "\n";
		}
		oss << "  ],\n";
		oss << "  \"transformKeys\": [\n";
		for ( size_t i = 0; i < _keys.size(); ++i )
		{
			const TransformKey& k = _keys[i];
			oss << "    { \"time\": " << k.time << ", \"x\": " << k.x << ", \"y\": " << k.y
				<< ", \"angleDeg\": " << k.angleDeg << " }";
			if ( i + 1 < _keys.size() )
				oss << ",";
			oss << "\n";
		}
		oss << "  ]\n";
		oss << "}\n";

		const std::string text = oss.str();
		if ( FileUtil::writeFile( path.string(), reinterpret_cast<const uint8*>( text.data() ),
								  static_cast<uint64>( text.size() ) ) )
			SW_LOG_INFO( "[SpriteClip] Saved %#", path.string().c_str() );
	}

	void SpriteClipTool::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		ImGui::InputText( "Atlas", _atlasPath, sizeof( _atlasPath ) );
		if ( ImGui::Button( "Load" ) )
			loadJson();
		ImGui::SameLine();
		if ( ImGui::Button( "Save" ) )
		{
			saveJson();
			_status = "Saved SpriteClip.json";
		}
		ImGui::TextDisabled( "Config/Editor/SpriteClip.json (separate from AnimGraph)" );

		ImGui::Separator();
		ImGui::TextUnformatted( "Frames (u,v,w,h,durationMs)" );
		if ( ImGui::Button( "Add Frame" ) )
		{
			_frames.push_back( Frame{} );
			_selectedFrame = static_cast<int32>( _frames.size() ) - 1;
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Remove Frame" ) && _selectedFrame >= 0 &&
			 _selectedFrame < static_cast<int32>( _frames.size() ) )
		{
			_frames.erase( _frames.begin() + _selectedFrame );
			if ( _selectedFrame >= static_cast<int32>( _frames.size() ) )
				_selectedFrame = static_cast<int32>( _frames.size() ) - 1;
		}

		for ( int32 i = 0; i < static_cast<int32>( _frames.size() ); ++i )
		{
			ImGui::PushID( i );
			char label[32];
			std::snprintf( label, sizeof( label ), "Frame %d", i );
			if ( ImGui::Selectable( label, _selectedFrame == i ) )
				_selectedFrame = i;
			ImGui::PopID();
		}

		if ( _selectedFrame >= 0 && _selectedFrame < static_cast<int32>( _frames.size() ) )
		{
			Frame& f = _frames[static_cast<size_t>( _selectedFrame )];
			ImGui::DragFloat( "u", &f.u, 0.01f );
			ImGui::DragFloat( "v", &f.v, 0.01f );
			ImGui::DragFloat( "w", &f.w, 0.01f );
			ImGui::DragFloat( "h", &f.h, 0.01f );
			ImGui::InputInt( "durationMs", &f.durationMs );
		}

		ImGui::Separator();
		ImGui::TextUnformatted( "TransformAnimation Keys (optional)" );
		if ( ImGui::Button( "Add Key" ) )
		{
			_keys.push_back( TransformKey{} );
			_selectedKey = static_cast<int32>( _keys.size() ) - 1;
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Remove Key" ) && _selectedKey >= 0 &&
			 _selectedKey < static_cast<int32>( _keys.size() ) )
		{
			_keys.erase( _keys.begin() + _selectedKey );
			if ( _selectedKey >= static_cast<int32>( _keys.size() ) )
				_selectedKey = static_cast<int32>( _keys.size() ) - 1;
		}

		for ( int32 i = 0; i < static_cast<int32>( _keys.size() ); ++i )
		{
			ImGui::PushID( 1000 + i );
			char label[32];
			std::snprintf( label, sizeof( label ), "Key %d", i );
			if ( ImGui::Selectable( label, _selectedKey == i ) )
				_selectedKey = i;
			ImGui::PopID();
		}

		if ( _selectedKey >= 0 && _selectedKey < static_cast<int32>( _keys.size() ) )
		{
			TransformKey& k = _keys[static_cast<size_t>( _selectedKey )];
			ImGui::DragFloat( "time", &k.time, 0.01f );
			ImGui::DragFloat( "x", &k.x, 0.1f );
			ImGui::DragFloat( "y", &k.y, 0.1f );
			ImGui::DragFloat( "angleDeg", &k.angleDeg, 0.5f );
		}

		if ( _status.empty() == false )
		{
			ImGui::Separator();
			ImGui::TextDisabled( "%s", _status.c_str() );
		}

		ImGui::End();
	}
} // namespace sw
