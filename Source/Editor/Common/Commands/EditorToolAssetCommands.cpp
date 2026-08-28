#include "pch.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/EditorInspectorCommands.h"
#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Animation/AnimationGraphAsset.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Serialization/Core/SerializerInternal.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Serialization/Format/SimpleJsonWalk.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/Resource/ResourceManager.h"
#include "Engine/Utility/Resource/ResourceUtil.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "RuntimeAPI/Service/EditorService.h"

namespace sw::editor
{
	SW_LOG_CALLER( "EditorToolAssetCommands" );

	namespace
	{
		constexpr utf8 kDialogueGraphPath[] = "Saved/Dialogue/default_dialogue.json";

		void resizeTileMapData( EditorTileMapData& data, int32 width, int32 height )
		{
			if ( width <= 0 || height <= 0 )
				return;
			data._width		   = width;
			data._height	   = height;
			const size_t count = static_cast<size_t>( width * height );
			data._listWalkable.assign( count, 1 );
			data._listEncounter.assign( count, 0 );
			data._listPassThrough.assign( count, 0 );
			data._listVisual.assign( count, EditorTileVisual{} );
			data._listWarp.clear();
		}

		bool parseJsonIntInRange( const string& json, size_t from, size_t end, const utf8* pKey, int32& outValue )
		{
			if ( pKey == nullptr )
				return false;
			const string key = string( "\"" ) + pKey + "\"";
			const size_t pos = json.find( key, from );
			if ( pos == string::npos || pos > end )
				return false;
			const size_t colon = json.find( ':', pos );
			if ( colon == string::npos || colon > end )
				return false;
			utf8* pEndPtr{ nullptr };
			outValue = static_cast<int32>( StringUtil::strtoll( json.c_str() + colon + 1, &pEndPtr, 10 ) );
			return true;
		}

		bool parseJsonFloatInRange( const string& json, size_t from, size_t end, const utf8* pKey, float32& outValue )
		{
			if ( pKey == nullptr )
				return false;
			const string key = string( "\"" ) + pKey + "\"";
			const size_t pos = json.find( key, from );
			if ( pos == string::npos || pos > end )
				return false;
			const size_t colon = json.find( ':', pos );
			if ( colon == string::npos || colon > end )
				return false;
			outValue = static_cast<float32>( StringUtil::atof( json.c_str() + colon + 1 ) );
			return true;
		}

		bool parseJsonStringInRange( const string& json, size_t from, size_t end, const utf8* pKey, string& outValue )
		{
			if ( pKey == nullptr )
				return false;
			const string key = string( "\"" ) + pKey + "\"";
			const size_t pos = json.find( key, from );
			if ( pos == string::npos || pos > end )
				return false;
			const size_t colon = json.find( ':', pos );
			if ( colon == string::npos || colon > end )
				return false;
			const size_t q0 = json.find( '"', colon + 1 );
			const size_t q1 = json.find( '"', q0 + 1 );
			if ( q0 == string::npos || q1 == string::npos || q0 > end || q1 > end )
				return false;
			outValue.assign( json, q0 + 1, q1 - q0 - 1 );
			return true;
		}

		bool parseFloatAfter( string_view src, size_t from, const utf8* pKey, float32& outValue )
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
					outValue = static_cast<float32>( StringUtil::atof( src.data() + colon + 1 ) );
					return true;
				}
			}
			return false;
		}

		bool parseIntAfter( string_view src, size_t from, const utf8* pKey, int32& outValue )
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
					outValue = StringUtil::atoi( src.data() + colon + 1 );
					return true;
				}
			}
			return false;
		}

		void parseJsonObjectArray( const string& json, const utf8* pArrayKey, vector<size_t>& outObjStartList )
		{
			outObjStartList.clear();
			const string key	= string( "\"" ) + pArrayKey + "\"";
			const size_t keyPos = json.find( key );
			if ( keyPos == string::npos )
				return;
			const size_t arr = json.find( '[', keyPos );
			const size_t end = json.find( ']', arr );
			if ( arr == string::npos || end == string::npos )
				return;

			size_t cursor = arr;
			while ( true )
			{
				const size_t obj = json.find( '{', cursor );
				if ( obj == string::npos || obj > end )
					break;
				outObjStartList.push_back( obj );
				cursor = json.find( '}', obj );
				if ( cursor == string::npos )
					break;
				++cursor;
			}
		}
	} // namespace

	namespace
	{
		bool pathEndsWithIgnoreCase( string_view path, string_view suffix )
		{
			return FileUtil::endsWithIgnoreCase( string{ path }, suffix );
		}

		string resolveExistingOrRelativePath( string_view path )
		{
			if ( path.empty() )
				return {};
			string absPath = ResourceUtil::getResourcePath( path );
			if ( absPath.empty() )
				absPath = string{ path };
			return absPath;
		}

		string resolveAnimGraphPath( string_view path )
		{
			if ( path.empty() == false )
				return resolveExistingOrRelativePath( path );
			return EditorUtil::resolveEditorConfigFile( EditorConfig::getActive()._animationGraphDataFile.c_str() );
		}

		string resolveDialogueGraphPath( string_view path )
		{
			if ( path.empty() == false )
				return resolveExistingOrRelativePath( path );
			string absPath = ResourceUtil::getResourcePath( kDialogueGraphPath );
			if ( absPath.empty() )
				absPath = kDialogueGraphPath;
			return absPath;
		}

		string resolveSpriteClipPath( string_view path )
		{
			if ( path.empty() == false )
				return resolveExistingOrRelativePath( path );
			return EditorUtil::resolveEditorConfigFile( EditorConfig::getActive()._spriteClipFile.c_str() );
		}

		void editorFromEngine( const AnimationGraphAsset& src, EditorAnimGraphData& dst )
		{
			dst._listNode.clear();
			dst._listLink.clear();
			dst._listNode.reserve( src._listNode.size() );
			dst._listLink.reserve( src._listLink.size() );
			for ( const AnimationGraphNode& node : src._listNode )
			{
				EditorAnimGraphNode editorNode{};
				editorNode._id	 = node._id;
				editorNode._name = node._name;
				editorNode._x	 = node._x;
				editorNode._y	 = node._y;
				dst._listNode.push_back( std::move( editorNode ) );
			}
			for ( const AnimationGraphLink& link : src._listLink )
			{
				EditorAnimGraphLink editorLink{};
				editorLink._id		 = link._id;
				editorLink._fromNode = link._fromNode;
				editorLink._toNode	 = link._toNode;
				dst._listLink.push_back( editorLink );
			}
		}

		void engineFromEditor( const EditorAnimGraphData& src, AnimationGraphAsset& dst )
		{
			dst._listNode.clear();
			dst._listLink.clear();
			dst._listNode.reserve( src._listNode.size() );
			dst._listLink.reserve( src._listLink.size() );
			for ( const EditorAnimGraphNode& node : src._listNode )
			{
				AnimationGraphNode engineNode{};
				engineNode._id	 = node._id;
				engineNode._name = node._name;
				engineNode._x	 = node._x;
				engineNode._y	 = node._y;
				dst._listNode.push_back( std::move( engineNode ) );
			}
			for ( const EditorAnimGraphLink& link : src._listLink )
			{
				AnimationGraphLink engineLink{};
				engineLink._id		 = link._id;
				engineLink._fromNode = link._fromNode;
				engineLink._toNode	 = link._toNode;
				dst._listLink.push_back( engineLink );
			}
		}

		void editorFromEngineDialogue( const DialogueGraphAsset& src, EditorDialogueGraphData& dst )
		{
			dst._listNode.clear();
			dst._listLink.clear();
			dst._listNode.reserve( src._listNode.size() );
			dst._listLink.reserve( src._listLink.size() );
			for ( const DialogueAssetNode& node : src._listNode )
			{
				EditorDialogueNode editorNode{};
				editorNode._id			  = node._id;
				editorNode._type		  = static_cast<DialogueNodeType>( node._type );
				editorNode._speaker		  = node._speaker;
				editorNode._text		  = node._text;
				editorNode._condition	  = node._condition;
				editorNode._actionCommand = node._actionCommand;
				editorNode._listChoice	  = node._listChoice;
				editorNode._x			  = node._x;
				editorNode._y			  = node._y;
				dst._listNode.push_back( std::move( editorNode ) );
			}
			for ( const DialogueAssetLink& link : src._listLink )
			{
				EditorDialogueLink editorLink{};
				editorLink._id		= link._id;
				editorLink._fromPin = link._fromPin;
				editorLink._toPin	= link._toPin;
				dst._listLink.push_back( editorLink );
			}
		}

		void engineFromEditorDialogue( const EditorDialogueGraphData& src, DialogueGraphAsset& dst )
		{
			dst._listNode.clear();
			dst._listLink.clear();
			dst._listNode.reserve( src._listNode.size() );
			dst._listLink.reserve( src._listLink.size() );
			for ( const EditorDialogueNode& node : src._listNode )
			{
				DialogueAssetNode engineNode{};
				engineNode._id			  = node._id;
				engineNode._type		  = static_cast<DialogueAssetNodeType>( node._type );
				engineNode._speaker		  = node._speaker;
				engineNode._text		  = node._text;
				engineNode._condition	  = node._condition;
				engineNode._actionCommand = node._actionCommand;
				engineNode._listChoice	  = node._listChoice;
				engineNode._x			  = node._x;
				engineNode._y			  = node._y;
				dst._listNode.push_back( std::move( engineNode ) );
			}
			for ( const EditorDialogueLink& link : src._listLink )
			{
				DialogueAssetLink engineLink{};
				engineLink._id		= link._id;
				engineLink._fromPin = link._fromPin;
				engineLink._toPin	= link._toPin;
				dst._listLink.push_back( engineLink );
			}
		}

		string formatPropertyValue( const PropertyInfo& prop, const void* pInstance )
		{
			const void* pPtr = prop.getRawPtr( pInstance );
			if ( pPtr == nullptr )
				return "<null>";

			const string typeName = prop._typeName.c_str();
			utf8		 arrBuf[128];
			if ( typeName == "float32" || typeName == "float" )
			{
				float32 value{ 0.0f };
				Memory::copy( &value, pPtr, sizeof( float32 ) );
				formatstring( arrBuf, sizeof( arrBuf ), "%.4f", value );
				return arrBuf;
			}
			if ( typeName == "int32" || typeName == "int" )
			{
				int32 value{ 0 };
				Memory::copy( &value, pPtr, sizeof( int32 ) );
				formatstring( arrBuf, sizeof( arrBuf ), "%d", value );
				return arrBuf;
			}
			if ( typeName == "uint32" )
			{
				uint32 value{ 0 };
				Memory::copy( &value, pPtr, sizeof( uint32 ) );
				formatstring( arrBuf, sizeof( arrBuf ), "%u", value );
				return arrBuf;
			}
			if ( typeName == "bool" )
			{
				bool value{ false };
				Memory::copy( &value, pPtr, sizeof( bool ) );
				return value ? "true" : "false";
			}
			if ( typeName == "string" )
				return *static_cast<const string*>( pPtr );
			if ( typeName == "float3" )
			{
				float3 value{};
				Memory::copy( &value, pPtr, sizeof( float3 ) );
				formatstring( arrBuf, sizeof( arrBuf ), "(%.3f, %.3f, %.3f)", value._x, value._y, value._z );
				return arrBuf;
			}
			if ( typeName == "float2" )
			{
				float2 value{};
				Memory::copy( &value, pPtr, sizeof( float2 ) );
				formatstring( arrBuf, sizeof( arrBuf ), "(%.3f, %.3f)", value._x, value._y );
				return arrBuf;
			}
			if ( typeName == "float4" )
			{
				float4 value{};
				Memory::copy( &value, pPtr, sizeof( float4 ) );
				formatstring( arrBuf, sizeof( arrBuf ), "(%.3f, %.3f, %.3f, %.3f)", value._x, value._y, value._z, value._w );
				return arrBuf;
			}
			return "<value>";
		}

		void collectNestedPrefabPaths( string_view stateData, vector<string>& outNested )
		{
			const string xml{ stateData };
			size_t		 cursor = 0;
			while ( true )
			{
				const size_t prefabPos = xml.find( ".prefab", cursor );
				if ( prefabPos == string::npos )
					break;
				size_t start = prefabPos;
				while ( start > 0 && xml[start - 1] != '"' && xml[start - 1] != '>' && xml[start - 1] != '=' &&
						xml[start - 1] != ' ' )
					--start;
				size_t end = prefabPos;
				while ( end < xml.size() && xml[end] != '"' && xml[end] != '<' && xml[end] != ' ' )
					++end;
				string nested = xml.substr( start, end - start );
				if ( nested.empty() == false )
					outNested.push_back( std::move( nested ) );
				cursor = prefabPos + 7;
			}
		}

		Component* findComponentByTypeName( GameObject* pObj, string_view typeName )
		{
			if ( pObj == nullptr )
				return nullptr;
			for ( Component* pComp : pObj->getAllComponents() )
			{
				if ( pComp == nullptr || pComp->getTypeInfo() == nullptr )
					continue;
				if ( string{ pComp->getTypeInfo()->_name.c_str() } == string{ typeName } )
					return pComp;
				if ( string{ pComp->getComponentName().c_str() } == string{ typeName } )
					return pComp;
			}
			return nullptr;
		}
	} // namespace

	bool EditorToolAssetCommands::isAnimationGraphPath( string_view path )
	{
		return pathEndsWithIgnoreCase( path, ".anim.json" ) || pathEndsWithIgnoreCase( path, ".anim" );
	}

	bool EditorToolAssetCommands::isDialogueGraphPath( string_view path )
	{
		return pathEndsWithIgnoreCase( path, ".dialogue.json" ) || pathEndsWithIgnoreCase( path, ".dialogue" );
	}

	bool EditorToolAssetCommands::isSpriteClipPath( string_view path )
	{
		if ( pathEndsWithIgnoreCase( path, ".sprite.json" ) || pathEndsWithIgnoreCase( path, ".sprite" ) )
			return true;
		if ( pathEndsWithIgnoreCase( path, ".png" ) || pathEndsWithIgnoreCase( path, ".jpg" ) ||
			 pathEndsWithIgnoreCase( path, ".jpeg" ) || pathEndsWithIgnoreCase( path, ".dds" ) ||
			 pathEndsWithIgnoreCase( path, ".tga" ) )
			return true;
		return false;
	}

	bool EditorToolAssetCommands::isTileMapPath( string_view path )
	{
		if ( pathEndsWithIgnoreCase( path, ".tilemap.xml" ) || pathEndsWithIgnoreCase( path, ".tilemap" ) )
			return true;
		if ( pathEndsWithIgnoreCase( path, ".xml" ) == false )
			return false;
		if ( pathEndsWithIgnoreCase( path, ".scene.xml" ) || pathEndsWithIgnoreCase( path, ".prefab.xml" ) ||
			 pathEndsWithIgnoreCase( path, ".preset.xml" ) )
			return false;
		return true;
	}

	bool EditorToolAssetCommands::isSequencerPath( string_view path )
	{
		return pathEndsWithIgnoreCase( path, ".seq.json" ) || pathEndsWithIgnoreCase( path, ".seq" );
	}

	bool EditorToolAssetCommands::isPrefabPath( string_view path )
	{
		return pathEndsWithIgnoreCase( path, ".prefab.xml" ) || pathEndsWithIgnoreCase( path, ".prefab.json" ) ||
			   pathEndsWithIgnoreCase( path, ".prefab.bin" ) || pathEndsWithIgnoreCase( path, ".prefab" ) ||
			   pathEndsWithIgnoreCase( path, ".pfb" );
	}

	bool EditorToolAssetCommands::loadAnimationGraph( EditorAnimGraphData& outData, string_view path )
	{
		outData._listNode.clear();
		outData._listLink.clear();

		const string		resolved = resolveAnimGraphPath( path );
		AnimationGraphAsset asset;
		if ( asset.loadFromFile( resolved ) == false )
			return false;
		editorFromEngine( asset, outData );
		return true;
	}

	bool EditorToolAssetCommands::saveAnimationGraph( const EditorAnimGraphData& data, string_view path )
	{
		const string		resolved = resolveAnimGraphPath( path );
		AnimationGraphAsset asset;
		engineFromEditor( data, asset );
		if ( asset.saveToFile( resolved ) == false )
			return false;
		SW_LOG_INFO( "Saved %#", resolved.c_str() );
		return true;
	}

	string EditorToolAssetCommands::serializeAnimationGraph( const EditorAnimGraphData& data )
	{
		AnimationGraphAsset asset;
		engineFromEditor( data, asset );
		return asset.toJson();
	}

	bool EditorToolAssetCommands::parseAnimationGraph( string_view json, EditorAnimGraphData& outData )
	{
		AnimationGraphAsset asset;
		if ( asset.parseJson( json ) == false )
			return false;
		editorFromEngine( asset, outData );
		return true;
	}

	const utf8* EditorToolAssetCommands::dialogueNodeTypeName( DialogueNodeType type )
	{
		switch ( type )
		{
			case DialogueNodeType::Start:
				return "Start";
			case DialogueNodeType::Dialogue:
				return "Dialogue";
			case DialogueNodeType::Choice:
				return "Choice";
			case DialogueNodeType::Branch:
				return "Branch";
			case DialogueNodeType::Action:
				return "Action";
			case DialogueNodeType::End:
				return "End";
			default:
				return "Unknown";
		}
	}

	DialogueNodeType EditorToolAssetCommands::parseDialogueNodeType( string_view typeStr )
	{
		if ( typeStr == "Start" )
			return DialogueNodeType::Start;
		if ( typeStr == "Choice" )
			return DialogueNodeType::Choice;
		if ( typeStr == "Branch" )
			return DialogueNodeType::Branch;
		if ( typeStr == "Action" )
			return DialogueNodeType::Action;
		if ( typeStr == "End" )
			return DialogueNodeType::End;
		return DialogueNodeType::Dialogue;
	}

	bool EditorToolAssetCommands::loadDialogueGraph( EditorDialogueGraphData& outData, string_view path )
	{
		outData._listNode.clear();
		outData._listLink.clear();

		const string	   resolved = resolveDialogueGraphPath( path );
		DialogueGraphAsset asset;
		if ( asset.loadFromFile( resolved ) == false )
			return false;
		editorFromEngineDialogue( asset, outData );
		return true;
	}

	bool EditorToolAssetCommands::saveDialogueGraph( const EditorDialogueGraphData& data, string_view path )
	{
		const string	   resolved = resolveDialogueGraphPath( path );
		DialogueGraphAsset asset;
		engineFromEditorDialogue( data, asset );
		if ( asset.saveToFile( resolved ) == false )
			return false;
		SW_LOG_INFO( "Saved %zu nodes, %zu links -> %#", data._listNode.size(), data._listLink.size(), resolved.c_str() );
		return true;
	}

	string EditorToolAssetCommands::serializeDialogueGraph( const EditorDialogueGraphData& data )
	{
		DialogueGraphAsset asset;
		engineFromEditorDialogue( data, asset );
		return asset.toJson();
	}

	bool EditorToolAssetCommands::parseDialogueGraph( string_view json, EditorDialogueGraphData& outData )
	{
		DialogueGraphAsset asset;
		if ( asset.parseJson( json ) == false )
			return false;
		editorFromEngineDialogue( asset, outData );
		return true;
	}

	bool EditorToolAssetCommands::loadTileMap( string_view assetRelativePath, EditorTileMapData& outData, string& outStatus )
	{
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		if ( FileUtil::fileExists( absPath ) == false )
		{
			outStatus = "Not found: " + absPath;
			return false;
		}

		vector<uint8> listFileData;
		if ( FileUtil::readFile( absPath, listFileData ) == false || listFileData.empty() )
		{
			outStatus = "Failed to read file";
			return false;
		}

		string		xmlStr( reinterpret_cast<const utf8*>( listFileData.data() ), listFileData.size() );
		XmlDocument doc;
		if ( doc.parse( xmlStr ) == false )
		{
			outStatus = "XML parse error";
			return false;
		}

		XmlNode root = doc.root( "TileMap" );
		if ( root.isValid() == false )
		{
			outStatus = "Missing <TileMap>";
			return false;
		}

		const utf8* pName = root.childText( "name" );
		if ( pName != nullptr )
			outData._name = pName;

		int32		width	   = 8;
		int32		height	   = 8;
		const utf8* pWidthText = root.childText( "width" );
		if ( pWidthText != nullptr )
			width = StringUtil::atoi( pWidthText );
		const utf8* pHeightText = root.childText( "height" );
		if ( pHeightText != nullptr )
			height = StringUtil::atoi( pHeightText );
		if ( width <= 0 )
			width = 8;
		if ( height <= 0 )
			height = 8;
		resizeTileMapData( outData, width, height );

		const size_t count = static_cast<size_t>( outData._width * outData._height );
		XmlNode		 tiles = root.child( "tiles" );
		if ( tiles.isValid() )
		{
			int32 index{ 0 };
			for ( XmlNode tileNode = tiles.child( "t" ); tileNode && index < static_cast<int32>( count );
				  tileNode		   = tileNode.next( "t" ), ++index )
			{
				const utf8*	 pV					 = tileNode.text();
				const size_t tileIndex			 = static_cast<size_t>( index );
				outData._listWalkable[tileIndex] = ( pV == nullptr || pV[0] != '0' ) ? 1 : 0;
				const utf8* pEnc				 = tileNode.attr( "enc" );
				if ( pEnc != nullptr )
					outData._listEncounter[tileIndex] = ( StringUtil::atoi( pEnc ) != 0 ) ? 1 : 0;
				const utf8* pPt = tileNode.attr( "pt" );
				if ( pPt != nullptr )
					outData._listPassThrough[tileIndex] = ( StringUtil::atoi( pPt ) != 0 ) ? 1 : 0;

				EditorTileVisual vis{};
				const utf8*		 pHa = tileNode.attr( "h" );
				if ( pHa != nullptr )
					vis._height = static_cast<uint8>( StringUtil::atoi( pHa ) );
				const utf8* pAtlas = tileNode.attr( "atlas" );
				if ( pAtlas != nullptr )
					vis._atlasId = static_cast<uint8>( StringUtil::atoi( pAtlas ) );
				const utf8* pTr = tileNode.attr( "tr" );
				if ( pTr != nullptr )
					vis._tintR = static_cast<uint8>( StringUtil::atoi( pTr ) );
				const utf8* pTg = tileNode.attr( "tg" );
				if ( pTg != nullptr )
					vis._tintG = static_cast<uint8>( StringUtil::atoi( pTg ) );
				const utf8* pTb = tileNode.attr( "tb" );
				if ( pTb != nullptr )
					vis._tintB = static_cast<uint8>( StringUtil::atoi( pTb ) );
				outData._listVisual[tileIndex] = vis;
			}
		}

		outData._listWarp.clear();
		XmlNode warps = root.child( "warps" );
		if ( warps.isValid() )
		{
			for ( XmlNode warpNode = warps.child( "warp" ); warpNode; warpNode = warpNode.next( "warp" ) )
			{
				EditorTileWarp warp{};
				const utf8*	   pX = warpNode.attr( "x" );
				if ( pX != nullptr )
					warp._tileX = StringUtil::atoi( pX );
				const utf8* pY = warpNode.attr( "y" );
				if ( pY != nullptr )
					warp._tileY = StringUtil::atoi( pY );
				const utf8* pMap = warpNode.attr( "map" );
				if ( pMap != nullptr )
					warp._targetMap = pMap;
				const utf8* pTx = warpNode.attr( "tx" );
				if ( pTx != nullptr )
					warp._targetTileX = StringUtil::atoi( pTx );
				const utf8* pTy = warpNode.attr( "ty" );
				if ( pTy != nullptr )
					warp._targetTileY = StringUtil::atoi( pTy );
				const utf8* pPair = warpNode.attr( "pair" );
				if ( pPair != nullptr )
					warp._pairId = pPair;
				outData._listWarp.push_back( std::move( warp ) );
			}
		}

		outStatus = string( "Loaded " ) + string( assetRelativePath );
		return true;
	}

	bool EditorToolAssetCommands::saveTileMap( string_view assetRelativePath, const EditorTileMapData& data )
	{
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		StringBuilder<4096> sb;
		sb.appendFormat( "<TileMap>\n" );
		sb.appendFormat( "  <name>%s</name>\n", data._name.c_str() );
		sb.appendFormat( "  <width>%d</width>\n", data._width );
		sb.appendFormat( "  <height>%d</height>\n", data._height );
		sb.appendFormat( "  <tiles>\n" );

		const size_t count = static_cast<size_t>( data._width * data._height );
		for ( size_t tileIndex = 0; tileIndex < count; ++tileIndex )
		{
			const uint8			   walk = data._listWalkable[tileIndex];
			const uint8			   enc	= data._listEncounter[tileIndex];
			const uint8			   pt	= data._listPassThrough[tileIndex];
			const EditorTileVisual vis	= data._listVisual[tileIndex];
			sb.appendFormat( "    <t enc=\"%u\" pt=\"%u\" h=\"%u\" atlas=\"%u\" tr=\"%u\" tg=\"%u\" tb=\"%u\">%u</t>\n",
							 static_cast<uint32>( enc ),
							 static_cast<uint32>( pt ),
							 static_cast<uint32>( vis._height ),
							 static_cast<uint32>( vis._atlasId ),
							 static_cast<uint32>( vis._tintR ),
							 static_cast<uint32>( vis._tintG ),
							 static_cast<uint32>( vis._tintB ),
							 static_cast<uint32>( walk ) );
		}

		sb.appendFormat( "  </tiles>\n" );
		sb.appendFormat( "  <warps>\n" );
		for ( const EditorTileWarp& warp : data._listWarp )
		{
			sb.appendFormat( "    <warp x=\"%d\" y=\"%d\" map=\"%s\" tx=\"%d\" ty=\"%d\"",
							 warp._tileX,
							 warp._tileY,
							 warp._targetMap.c_str(),
							 warp._targetTileX,
							 warp._targetTileY );
			if ( warp._pairId.empty() == false )
				sb.appendFormat( " pair=\"%s\"", warp._pairId.c_str() );
			sb.appendFormat( "/>\n" );
		}
		sb.appendFormat( "  </warps>\n" );
		sb.appendFormat( "</TileMap>\n" );
		return FileUtil::writeTextFile( absPath, sb.c_str() );
	}

	bool EditorToolAssetCommands::loadSpriteClip( EditorSpriteClipData& outData, string& outStatus, string_view path )
	{
		outData._listFrame.clear();
		outData._listKey.clear();
		outData._atlasPath.clear();

		const string resolved = resolveSpriteClipPath( path );
		if ( resolved.empty() || FileUtil::fileExists( resolved ) == false )
		{
			if ( pathEndsWithIgnoreCase( path, ".png" ) || pathEndsWithIgnoreCase( path, ".jpg" ) ||
				 pathEndsWithIgnoreCase( path, ".jpeg" ) || pathEndsWithIgnoreCase( path, ".dds" ) ||
				 pathEndsWithIgnoreCase( path, ".tga" ) )
			{
				outData._atlasPath = string{ path };
				outStatus		   = "Atlas from focused texture";
				return true;
			}
			outStatus = "No SpriteClip.json yet";
			return false;
		}

		string json;
		if ( FileUtil::readTextFile( resolved, json ) == false || json.empty() )
		{
			outStatus = "Failed to read SpriteClip.json";
			return false;
		}
		if ( parseSpriteClip( json, outData ) == false )
		{
			outStatus = "Failed to parse SpriteClip.json";
			return false;
		}
		outStatus = "Loaded " + resolved;
		return true;
	}

	bool EditorToolAssetCommands::saveSpriteClip( const EditorSpriteClipData& data, string_view path )
	{
		const string resolved = resolveSpriteClipPath( path );
		if ( resolved.empty() )
			return false;
		const string text = serializeSpriteClip( data );
		if ( FileUtil::writeFile( resolved, reinterpret_cast<const uint8*>( text.data() ), text.size() ) == false )
			return false;
		SW_LOG_INFO( "Saved %#", resolved.c_str() );
		return true;
	}

	string EditorToolAssetCommands::serializeSpriteClip( const EditorSpriteClipData& data )
	{
		StringBuilder<2048> sb;
		sb.append( "{\n" );
		sb.append( "  \"atlas\": \"" ).append( JsonSerializer::escapeString( data._atlasPath ).c_str() ).append( "\",\n" );
		sb.append( "  \"frames\": [\n" );
		for ( size_t frameIndex = 0; frameIndex < data._listFrame.size(); ++frameIndex )
		{
			const EditorSpriteClipFrame& frame = data._listFrame[frameIndex];
			sb.append( "    { \"u\": " )
				.append( frame._u )
				.append( ", \"v\": " )
				.append( frame._v )
				.append( ", \"w\": " )
				.append( frame._w )
				.append( ", \"h\": " )
				.append( frame._h )
				.append( ", \"durationMs\": " )
				.append( frame._durationMs )
				.append( " }" );
			if ( frameIndex + 1 < data._listFrame.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ],\n" );
		sb.append( "  \"transformKeys\": [\n" );
		for ( size_t keyIndex = 0; keyIndex < data._listKey.size(); ++keyIndex )
		{
			const EditorSpriteClipKey& key = data._listKey[keyIndex];
			sb.append( "    { \"time\": " )
				.append( key._time )
				.append( ", \"x\": " )
				.append( key._x )
				.append( ", \"y\": " )
				.append( key._y )
				.append( ", \"angleDeg\": " )
				.append( key._angleDeg )
				.append( " }" );
			if ( keyIndex + 1 < data._listKey.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ]\n" );
		sb.append( "}\n" );
		return string{ sb.c_str() };
	}

	bool EditorToolAssetCommands::parseSpriteClip( string_view jsonView, EditorSpriteClipData& outData )
	{
		outData._listFrame.clear();
		outData._listKey.clear();
		const string json{ jsonView };
		const string atlas = JsonSerializer::extractStringField( json, "atlas" );
		if ( atlas.empty() == false )
			outData._atlasPath = atlas;

		vector<size_t> listFrameObj;
		parseJsonObjectArray( json, "frames", listFrameObj );
		for ( const size_t obj : listFrameObj )
		{
			EditorSpriteClipFrame frame{};
			parseFloatAfter( json, obj, "u", frame._u );
			parseFloatAfter( json, obj, "v", frame._v );
			parseFloatAfter( json, obj, "w", frame._w );
			parseFloatAfter( json, obj, "h", frame._h );
			parseIntAfter( json, obj, "durationMs", frame._durationMs );
			outData._listFrame.push_back( frame );
		}

		vector<size_t> listKeyObj;
		parseJsonObjectArray( json, "transformKeys", listKeyObj );
		for ( const size_t obj : listKeyObj )
		{
			EditorSpriteClipKey key{};
			parseFloatAfter( json, obj, "time", key._time );
			parseFloatAfter( json, obj, "x", key._x );
			parseFloatAfter( json, obj, "y", key._y );
			parseFloatAfter( json, obj, "angleDeg", key._angleDeg );
			outData._listKey.push_back( key );
		}
		return true;
	}

	bool EditorToolAssetCommands::loadSequence( SequenceAsset& outAsset, string_view path )
	{
		const string resolved = resolveExistingOrRelativePath( path );
		return outAsset.loadFromFile( resolved );
	}

	bool EditorToolAssetCommands::saveSequence( const SequenceAsset& asset, string_view path )
	{
		const string resolved = resolveExistingOrRelativePath( path );
		return asset.saveToFile( resolved );
	}

	void EditorToolAssetCommands::collectPrefabOverrides( GameObject* pInstance, string_view prefabPath, string& outPrefabPath,
														  string& outInstanceName, vector<PrefabOverrideItem>& outOverride,
														  vector<string>& outNestedPrefab )
	{
		outOverride.clear();
		outNestedPrefab.clear();
		outPrefabPath	= string{ prefabPath };
		outInstanceName = pInstance != nullptr ? string{ pInstance->getName().c_str() } : string{};

		EditorContext* pContext = EditorContext::get();
		if ( outPrefabPath.empty() && pInstance != nullptr && pContext != nullptr )
			outPrefabPath = pContext->getWorkspace().getGameObjectPrefabPath( pInstance->getObjectId() );
		if ( outPrefabPath.empty() && pContext != nullptr )
			outPrefabPath = pContext->getWorkspace().getFocusedAssetPath();

		ResourceManager* pResources = editor::getService<ResourceManager>();
		if ( pResources == nullptr || outPrefabPath.empty() )
			return;

		PrefabAsset* pLoaded = pResources->getPrefabManager().loadPrefab( outPrefabPath );
		if ( pLoaded == nullptr || pLoaded->isValid() == false )
			return;

		collectNestedPrefabPaths( pLoaded->getStateData(), outNestedPrefab );

		if ( pInstance == nullptr )
			return;

		SceneManager* pSceneManager = editor::getService<SceneManager>();
		if ( pSceneManager == nullptr )
			return;
		Scene* pScene = pSceneManager->getActiveScene();
		if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
			return;

		GameObjectManager* pManager = pScene->getObjectManager();
		GameObject*		   pCdo		= pManager->createGameObject( hashed_string( "__PrefabDiffCdo" ) );
		if ( pCdo == nullptr )
			return;

		const string body = StringUtil::trim( pLoaded->getStateData().c_str() );
		bool		 bLoadedCdo{ false };
		if ( body.empty() == false && body.front() == '{' )
			bLoadedCdo = ObjectStateSerializer::loadFromJsonString( pCdo, pLoaded->getStateData() );
		else if ( body.empty() == false )
			bLoadedCdo = ObjectStateSerializer::loadFromXmlString( pCdo, pLoaded->getStateData() );
		if ( bLoadedCdo == false )
		{
			pManager->destroyObject( pCdo );
			return;
		}

		const SerializeContext& ctx = SerializeContext::getDefault();
		for ( Component* pInstComp : pInstance->getAllComponents() )
		{
			if ( pInstComp == nullptr || pInstComp->getTypeInfo() == nullptr )
				continue;
			const TypeInfo* pTypeInfo = pInstComp->getTypeInfo();
			Component*		pCdoComp  = findComponentByTypeName( pCdo, pTypeInfo->_name.c_str() );
			if ( pCdoComp == nullptr )
				continue;

			vector<uint8> listCdoBytes;
			vector<uint8> listInstBytes;
			pTypeInfo->forEachProperty(
				[&]( const PropertyInfo& prop )
			{
				if ( prop._metadata._bTransient == SW_TRUE )
					return;
				const void* pCdoPtr	 = prop.getRawPtr( pCdoComp );
				const void* pInstPtr = prop.getRawPtr( pInstComp );
				if ( pCdoPtr == nullptr || pInstPtr == nullptr )
					return;

				listCdoBytes.clear();
				listInstBytes.clear();
				if ( prop._bIsContainer == SW_TRUE && prop.hasContainerWrapper() )
				{
					serializeNestedContainerBinary( pCdoPtr, prop.getContainerShape(), listCdoBytes, ctx );
					serializeNestedContainerBinary( pInstPtr, prop.getContainerShape(), listInstBytes, ctx );
				}
				else
				{
					serializeValueBinary( pCdoPtr, prop._typeName, listCdoBytes, ctx );
					serializeValueBinary( pInstPtr, prop._typeName, listInstBytes, ctx );
				}

				PrefabOverrideItem item{};
				item._componentName	  = pTypeInfo->_name.c_str();
				item._propertyName	  = prop._name.c_str();
				item._defaultValue	  = formatPropertyValue( prop, pCdoComp );
				item._overriddenValue = formatPropertyValue( prop, pInstComp );
				item._bModified		  = ( listCdoBytes != listInstBytes );
				outOverride.push_back( std::move( item ) );
			},
				true );
		}

		pManager->destroyObject( pCdo );
	}

	void EditorToolAssetCommands::revertPrefabOverride( GameObject* pInstance, PrefabOverrideItem& item, string_view prefabPath )
	{
		if ( pInstance == nullptr || prefabPath.empty() )
		{
			item._overriddenValue = item._defaultValue;
			item._bModified		  = false;
			return;
		}

		ResourceManager* pResources = editor::getService<ResourceManager>();
		if ( pResources == nullptr )
			return;
		PrefabAsset* pLoaded = pResources->getPrefabManager().loadPrefab( prefabPath );
		if ( pLoaded == nullptr )
			return;

		SceneManager* pSceneManager = editor::getService<SceneManager>();
		if ( pSceneManager == nullptr || pSceneManager->getActiveScene() == nullptr )
			return;
		GameObjectManager* pManager = pSceneManager->getActiveScene()->getObjectManager();
		if ( pManager == nullptr )
			return;

		GameObject* pCdo = pManager->createGameObject( hashed_string( "__PrefabRevertCdo" ) );
		if ( pCdo == nullptr )
			return;
		ObjectStateSerializer::loadFromXmlString( pCdo, pLoaded->getStateData() );

		Component* pInstComp = findComponentByTypeName( pInstance, item._componentName );
		Component* pCdoComp	 = findComponentByTypeName( pCdo, item._componentName );
		if ( pInstComp != nullptr && pCdoComp != nullptr && pInstComp->getTypeInfo() != nullptr )
		{
			const PropertyInfo* pProp = pInstComp->getTypeInfo()->findPropertyInHierarchy( hashed_string( item._propertyName.c_str() ) );
			if ( pProp != nullptr )
			{
				const string			beforeXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pInstance } );
				void*					pDest	  = pProp->getRawPtr( pInstComp );
				const void*				pSrc	  = pProp->getRawPtr( pCdoComp );
				const SerializeContext& ctx		  = SerializeContext::getDefault();
				if ( pDest != nullptr && pSrc != nullptr )
				{
					vector<uint8> listBytes;
					serializeValueBinary( pSrc, pProp->_typeName, listBytes, ctx );
					size_t local{ 0 };
					deserializeValueBinary( pDest, pProp->_typeName, listBytes.data(), listBytes.size(), local, ctx );
				}
				const string afterXml = EditorTransaction::captureSnapshot( GameObjectPtr{ pInstance } );
				EditorTransaction::recordModify( GameObjectPtr{ pInstance }, beforeXml, afterXml, "Revert Prefab Override" );
				item._overriddenValue = item._defaultValue;
				item._bModified		  = false;
			}
		}
		pManager->destroyObject( pCdo );
	}

	bool EditorToolAssetCommands::applyPrefabOverridesToTemplate( GameObject* pInstance, string_view prefabPath )
	{
		return EditorInspectorCommands::applyToPrefab( pInstance, prefabPath );
	}

	bool EditorToolAssetCommands::revertAllPrefabOverrides( GameObject* pInstance, string_view prefabPath )
	{
		return EditorInspectorCommands::revertToPrefab( pInstance, prefabPath );
	}

	void EditorToolAssetCommands::pushDocumentUndo( Delegate<void()> undo, Delegate<void()> redo, string_view label,
													string_view coalesceKey )
	{
		CommandStack* pStack = editor::getService<CommandStack>();
		if ( pStack == nullptr )
			return;

		CommandStack::Command cmd;
		cmd._label = string{ label };
		cmd._undo  = std::move( undo );
		cmd._redo  = std::move( redo );
		if ( coalesceKey.empty() == false )
			pStack->pushCoalesce( coalesceKey, std::move( cmd ) );
		else
			pStack->push( std::move( cmd ) );
	}
} // namespace sw::editor
