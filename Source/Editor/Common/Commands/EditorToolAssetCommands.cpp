#include "pch.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

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
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/Json/JsonDocument.h"
#include "Engine/Utility/Resource/ResourceManager.h"
#include "Engine/Utility/Resource/ResourceUtil.h"
#include "Engine/Utility/Xml/TileMapXml.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "RuntimeAPI/Service/EditorService.h"

namespace sw::editor
{
	SW_LOG_CALLER( "EditorToolAssetCommands" );

	namespace
	{
		constexpr utf8 kDialogueGraphPath[] = "Saved/Dialogue/default_dialogue.json";
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
		TileMapXmlData xmlData{};
		if ( xmlData.load( assetRelativePath ) == false )
		{
			outStatus = "Not found: " + string{ assetRelativePath };
			return false;
		}

		outData._name			 = xmlData._name;
		outData._scenePath		 = xmlData._scenePath;
		outData._role			 = xmlData._role;
		outData._width			 = xmlData._width;
		outData._height			 = xmlData._height;
		outData._spawnX			 = xmlData._spawnX;
		outData._spawnY			 = xmlData._spawnY;
		outData._listWalkable	 = std::move( xmlData._walkableList );
		outData._listEncounter	 = std::move( xmlData._encounterList );
		outData._listPassThrough = std::move( xmlData._passThroughList );
		outData._listVisual.clear();
		outData._listVisual.reserve( xmlData._visualList.size() );
		for ( const TileMapXmlData::Visual& src : xmlData._visualList )
		{
			EditorTileVisual dst{};
			dst._height	 = src._height;
			dst._atlasId = src._atlasId;
			dst._tintR	 = src._tintR;
			dst._tintG	 = src._tintG;
			dst._tintB	 = src._tintB;
			outData._listVisual.push_back( dst );
		}
		outData._listWarp.clear();
		outData._listWarp.reserve( xmlData._warpList.size() );
		for ( const TileMapXmlData::Warp& src : xmlData._warpList )
		{
			EditorTileWarp dst{};
			dst._tileX		 = src._tileX;
			dst._tileY		 = src._tileY;
			dst._targetMap	 = src._targetMap;
			dst._targetTileX = src._targetTileX;
			dst._targetTileY = src._targetTileY;
			dst._pairId		 = src._pairId;
			outData._listWarp.push_back( std::move( dst ) );
		}
		outData._listEncounterEntry.clear();
		outData._listEncounterEntry.reserve( xmlData._encounterEntryList.size() );
		for ( const TileMapXmlData::Encounter& src : xmlData._encounterEntryList )
		{
			EditorTileEncounterEntry dst{};
			dst._speciesId = src._speciesId;
			dst._weight	   = src._weight;
			outData._listEncounterEntry.push_back( std::move( dst ) );
		}

		outStatus = string( "Loaded " ) + string( assetRelativePath );
		return true;
	}

	bool EditorToolAssetCommands::saveTileMap( string_view assetRelativePath, const EditorTileMapData& data )
	{
		TileMapXmlData xmlData{};
		xmlData._name			 = data._name;
		xmlData._sourcePath		 = assetRelativePath;
		xmlData._scenePath		 = data._scenePath;
		xmlData._role			 = data._role;
		xmlData._width			 = data._width;
		xmlData._height			 = data._height;
		xmlData._spawnX			 = data._spawnX;
		xmlData._spawnY			 = data._spawnY;
		xmlData._walkableList	 = data._listWalkable;
		xmlData._encounterList	 = data._listEncounter;
		xmlData._passThroughList = data._listPassThrough;
		xmlData._visualList.reserve( data._listVisual.size() );
		for ( const EditorTileVisual& src : data._listVisual )
		{
			TileMapXmlData::Visual dst{};
			dst._height	 = src._height;
			dst._atlasId = src._atlasId;
			dst._tintR	 = src._tintR;
			dst._tintG	 = src._tintG;
			dst._tintB	 = src._tintB;
			xmlData._visualList.push_back( dst );
		}
		xmlData._warpList.reserve( data._listWarp.size() );
		for ( const EditorTileWarp& src : data._listWarp )
		{
			TileMapXmlData::Warp dst{};
			dst._tileX		 = src._tileX;
			dst._tileY		 = src._tileY;
			dst._targetMap	 = src._targetMap;
			dst._targetTileX = src._targetTileX;
			dst._targetTileY = src._targetTileY;
			dst._pairId		 = src._pairId;
			xmlData._warpList.push_back( dst );
		}
		xmlData._encounterEntryList.reserve( data._listEncounterEntry.size() );
		for ( const EditorTileEncounterEntry& src : data._listEncounterEntry )
		{
			TileMapXmlData::Encounter dst{};
			dst._speciesId = src._speciesId;
			dst._weight	   = src._weight;
			xmlData._encounterEntryList.push_back( dst );
		}

		return xmlData.save( assetRelativePath );
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

		JsonDocument doc;
		if ( doc.loadFile( resolved ) == false )
		{
			outStatus = "Failed to read SpriteClip.json";
			return false;
		}
		if ( parseSpriteClip( doc.dump( -1 ), outData ) == false )
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
		if ( FileUtil::writeTextFile( resolved, text ) == false )
			return false;
		SW_LOG_INFO( "Saved %#", resolved.c_str() );
		return true;
	}

	string EditorToolAssetCommands::serializeSpriteClip( const EditorSpriteClipData& data )
	{
		JsonDocument	doc;
		const JsonValue root = doc.makeObject();
		root.set( "atlas" ).setString( data._atlasPath );

		const JsonValue framesVal = root.set( "frames" );
		framesVal.setArray();
		for ( const EditorSpriteClipFrame& frame : data._listFrame )
		{
			const JsonValue frameJson = framesVal.pushBack();
			frameJson.setObject();
			frameJson.set( "u" ).setFloat( static_cast<float64>( frame._u ) );
			frameJson.set( "v" ).setFloat( static_cast<float64>( frame._v ) );
			frameJson.set( "w" ).setFloat( static_cast<float64>( frame._w ) );
			frameJson.set( "h" ).setFloat( static_cast<float64>( frame._h ) );
			frameJson.set( "durationMs" ).setInt( frame._durationMs );
		}

		const JsonValue keysVal = root.set( "transformKeys" );
		keysVal.setArray();
		for ( const EditorSpriteClipKey& key : data._listKey )
		{
			const JsonValue keyJson = keysVal.pushBack();
			keyJson.setObject();
			keyJson.set( "time" ).setFloat( static_cast<float64>( key._time ) );
			keyJson.set( "x" ).setFloat( static_cast<float64>( key._x ) );
			keyJson.set( "y" ).setFloat( static_cast<float64>( key._y ) );
			keyJson.set( "angleDeg" ).setFloat( static_cast<float64>( key._angleDeg ) );
		}

		return doc.dump( 2 );
	}

	bool EditorToolAssetCommands::parseSpriteClip( string_view jsonView, EditorSpriteClipData& outData )
	{
		outData._listFrame.clear();
		outData._listKey.clear();

		JsonDocument doc;
		if ( doc.parse( jsonView ) == false )
			return false;

		const JsonValue root = doc.root();
		outData._atlasPath	 = root.get( "atlas" ).asString();

		const JsonValue framesVal = root.get( "frames" );
		if ( framesVal.isArray() )
		{
			const size_t frameCount = framesVal.size();
			for ( size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
			{
				const JsonValue frameJson = framesVal.at( frameIndex );
				if ( frameJson.isObject() == false )
					continue;
				EditorSpriteClipFrame frame{};
				frame._u		  = static_cast<float32>( frameJson.get( "u" ).asFloat( 0.0 ) );
				frame._v		  = static_cast<float32>( frameJson.get( "v" ).asFloat( 0.0 ) );
				frame._w		  = static_cast<float32>( frameJson.get( "w" ).asFloat( 0.0 ) );
				frame._h		  = static_cast<float32>( frameJson.get( "h" ).asFloat( 0.0 ) );
				frame._durationMs = static_cast<int32>( frameJson.get( "durationMs" ).asInt( 0 ) );
				outData._listFrame.push_back( frame );
			}
		}

		const JsonValue keysVal = root.get( "transformKeys" );
		if ( keysVal.isArray() )
		{
			const size_t keyCount = keysVal.size();
			for ( size_t keyIndex = 0; keyIndex < keyCount; ++keyIndex )
			{
				const JsonValue keyJson = keysVal.at( keyIndex );
				if ( keyJson.isObject() == false )
					continue;
				EditorSpriteClipKey key{};
				key._time	  = static_cast<float32>( keyJson.get( "time" ).asFloat( 0.0 ) );
				key._x		  = static_cast<float32>( keyJson.get( "x" ).asFloat( 0.0 ) );
				key._y		  = static_cast<float32>( keyJson.get( "y" ).asFloat( 0.0 ) );
				key._angleDeg = static_cast<float32>( keyJson.get( "angleDeg" ).asFloat( 0.0 ) );
				outData._listKey.push_back( key );
			}
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

		pLoaded->collectReferencedPrefabPaths( outNestedPrefab );

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
