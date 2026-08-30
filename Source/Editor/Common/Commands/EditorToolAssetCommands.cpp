#include "pch.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Commands/EditorInspectorCommands.h"
#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
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
#include "Engine/Serialization/Core/SerializerUtil.h"
#include "Engine/Utility/Json/JsonDocument.h"
#include "Engine/Utility/Resource/ResourceManager.h"
#include "Engine/Utility/Resource/ResourceUtil.h"
#include "Engine/Utility/Xml/TileMapXml.h"

#include "RuntimeAPI/Service/EditorService.h"

namespace sw::editor
{
	namespace
	{
		struct EditorToolAssetInternal
		{
			static string resolveExistingOrRelativePath( string_view path )
			{
				if ( path.empty() )
					return {};
				string absPath = ResourceUtil::getResourcePath( path );
				if ( absPath.empty() )
					absPath = string{ path };
				return absPath;
			}

			static string resolveAnimGraphPath( string_view path )
			{
				if ( path.empty() == false )
					return resolveExistingOrRelativePath( path );
				return EditorUtil::resolveEditorConfigFile( EditorConfig::getActive()._animationGraphDataFile.c_str() );
			}

			static string resolveDialogueGraphPath( string_view path )
			{
				if ( path.empty() == false )
					return resolveExistingOrRelativePath( path );
				return EditorUtil::resolveEditorConfigFile( EditorConfig::getActive()._dialogueGraphDataFile.c_str() );
			}

			static string resolveSpriteClipPath( string_view path )
			{
				if ( path.empty() == false )
					return resolveExistingOrRelativePath( path );
				return EditorUtil::resolveEditorConfigFile( EditorConfig::getActive()._spriteClipFile.c_str() );
			}

			static string formatPropertyValue( const PropertyInfo& prop, const void* pInstance )
			{
				const void* pPtr = prop.getRawPtr( pInstance );
				if ( pPtr == nullptr )
					return "<null>";

				const string						  typeName = prop._typeName.c_str();
				fixed_string<constant::kMaxBuffer128> arrBuf;
				if ( typeName == "float32" || typeName == "float" )
				{
					float32 value{ 0.0f };
					Memory::copy( &value, pPtr, sizeof( float32 ) );
					formatstring( arrBuf.data(), arrBuf.capacity(), "%#", Fmt( value, Format().precision( 4 ) ) );
					return arrBuf.c_str();
				}
				if ( typeName == "int32" || typeName == "int" )
				{
					int32 value{ 0 };
					Memory::copy( &value, pPtr, sizeof( int32 ) );
					formatstring( arrBuf.data(), arrBuf.capacity(), "%d", value );
					return arrBuf.c_str();
				}
				if ( typeName == "uint32" )
				{
					uint32 value{ 0 };
					Memory::copy( &value, pPtr, sizeof( uint32 ) );
					formatstring( arrBuf.data(), arrBuf.capacity(), "%u", value );
					return arrBuf.c_str();
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
					formatstring( arrBuf.data(), arrBuf.capacity(), "(%#, %#, %#)", Fmt( value._x, Format().precision( 3 ) ), Fmt( value._y, Format().precision( 3 ) ), Fmt( value._z, Format().precision( 3 ) ) );
					return arrBuf.c_str();
				}
				if ( typeName == "float2" )
				{
					float2 value{};
					Memory::copy( &value, pPtr, sizeof( float2 ) );
					formatstring( arrBuf.data(), arrBuf.capacity(), "(%#, %#)", Fmt( value._x, Format().precision( 3 ) ), Fmt( value._y, Format().precision( 3 ) ) );
					return arrBuf.c_str();
				}
				if ( typeName == "float4" )
				{
					float4 value{};
					Memory::copy( &value, pPtr, sizeof( float4 ) );
					formatstring( arrBuf.data(), arrBuf.capacity(), "(%#, %#, %#, %#)", Fmt( value._x, Format().precision( 3 ) ), Fmt( value._y, Format().precision( 3 ) ), Fmt( value._z, Format().precision( 3 ) ), Fmt( value._w, Format().precision( 3 ) ) );
					return arrBuf.c_str();
				}
				return "<value>";
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	SW_LOG_CALLER( "EditorToolAssetCommands" );

	bool EditorToolAssetCommands::loadAnimationGraph( AnimationGraphAsset& outData, string_view path )
	{
		const string resolved = EditorToolAssetInternal::resolveAnimGraphPath( path );
		return outData.loadFromFile( resolved );
	}

	bool EditorToolAssetCommands::saveAnimationGraph( const AnimationGraphAsset& data, string_view path )
	{
		const string resolved = EditorToolAssetInternal::resolveAnimGraphPath( path );
		if ( data.saveToFile( resolved ) == false )
			return false;
		SW_LOG_INFO( "Saved %#", resolved.c_str() );
		return true;
	}

	bool EditorToolAssetCommands::loadDialogueGraph( DialogueGraphAsset& outData, string_view path )
	{
		const string resolved = EditorToolAssetInternal::resolveDialogueGraphPath( path );
		return outData.loadFromFile( resolved );
	}

	bool EditorToolAssetCommands::saveDialogueGraph( const DialogueGraphAsset& data, string_view path )
	{
		const string resolved = EditorToolAssetInternal::resolveDialogueGraphPath( path );
		if ( data.saveToFile( resolved ) == false )
			return false;
		SW_LOG_INFO( "Saved %zu nodes, %zu links -> %#", data._listNode.size(), data._listLink.size(), resolved.c_str() );
		return true;
	}

	bool EditorToolAssetCommands::loadTileMap( string_view assetRelativePath, TileMapXmlData& outData, string& outStatus )
	{
		if ( outData.load( assetRelativePath ) == false )
		{
			outStatus = "Not found: " + string{ assetRelativePath };
			return false;
		}
		outStatus = string( "Loaded " ) + string( assetRelativePath );
		return true;
	}

	bool EditorToolAssetCommands::saveTileMap( string_view assetRelativePath, const TileMapXmlData& data )
	{
		return data.save( assetRelativePath );
	}

	bool EditorToolAssetCommands::loadSpriteClip( EditorSpriteClipData& outData, string& outStatus, string_view path )
	{
		outData._listFrame.clear();
		outData._listKey.clear();
		outData._atlasPath.clear();

		const string resolved = EditorToolAssetInternal::resolveSpriteClipPath( path );
		if ( resolved.empty() || FileUtil::fileExists( resolved ) == false )
		{
			const bool bAtlasImage = EditorAssetTypeRegistry::matches( EditorAssetKind::SpriteClip, path ) &&
									 EditorAssetTypeRegistry::matches( EditorAssetKind::Texture, path );
			if ( bAtlasImage )
			{
				outData._atlasPath = string{ path };
				outStatus		   = "Atlas from focused texture";
				return true;
			}
			outStatus = resolved.empty() ? string{ "No sprite clip file yet" } : ( "No file yet: " + resolved );
			return false;
		}

		JsonDocument doc;
		if ( doc.loadFile( resolved ) == false )
		{
			outStatus = "Failed to read " + resolved;
			return false;
		}
		if ( parseSpriteClip( doc.dump( -1 ), outData ) == false )
		{
			outStatus = "Failed to parse " + resolved;
			return false;
		}
		outStatus = "Loaded " + resolved;
		return true;
	}

	bool EditorToolAssetCommands::saveSpriteClip( const EditorSpriteClipData& data, string_view path )
	{
		const string resolved = EditorToolAssetInternal::resolveSpriteClipPath( path );
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
		const string resolved = EditorToolAssetInternal::resolveExistingOrRelativePath( path );
		return outAsset.loadFromFile( resolved );
	}

	bool EditorToolAssetCommands::saveSequence( const SequenceAsset& asset, string_view path )
	{
		const string resolved = EditorToolAssetInternal::resolveExistingOrRelativePath( path );
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
			Component*		pCdoComp  = pCdo->findComponentByTypeName( pTypeInfo->_name );
			if ( pCdoComp == nullptr )
				continue;

			vector<uint8> cdoBytes;
			vector<uint8> instBytes;
			pTypeInfo->forEachProperty(
				[&]( const PropertyInfo& prop )
			{
				if ( prop._metadata._bTransient == SW_TRUE )
					return;
				const void* pCdoPtr	 = prop.getRawPtr( pCdoComp );
				const void* pInstPtr = prop.getRawPtr( pInstComp );
				if ( pCdoPtr == nullptr || pInstPtr == nullptr )
					return;

				cdoBytes.clear();
				instBytes.clear();
				if ( prop._bIsContainer == SW_TRUE && prop.hasContainerWrapper() )
				{
					SerializerUtil::serializeNestedContainerBinary( pCdoPtr, prop.getContainerShape(), cdoBytes, ctx );
					SerializerUtil::serializeNestedContainerBinary( pInstPtr, prop.getContainerShape(), instBytes, ctx );
				}
				else
				{
					SerializerUtil::serializeValueBinary( pCdoPtr, prop._typeName, cdoBytes, ctx );
					SerializerUtil::serializeValueBinary( pInstPtr, prop._typeName, instBytes, ctx );
				}

				PrefabOverrideItem item{};
				item._componentName	  = pTypeInfo->_name.c_str();
				item._propertyName	  = prop._name.c_str();
				item._defaultValue	  = EditorToolAssetInternal::formatPropertyValue( prop, pCdoComp );
				item._overriddenValue = EditorToolAssetInternal::formatPropertyValue( prop, pInstComp );
				item._bModified		  = ( cdoBytes != instBytes );
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

		Component* pInstComp = pInstance->findComponentByTypeName( hashed_string{ item._componentName } );
		Component* pCdoComp	 = pCdo->findComponentByTypeName( hashed_string{ item._componentName } );
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
					vector<uint8> bytes;
					SerializerUtil::serializeValueBinary( pSrc, pProp->_typeName, bytes, ctx );
					size_t local{ 0 };
					SerializerUtil::deserializeValueBinary( pDest, pProp->_typeName, bytes.data(), bytes.size(), local, ctx );
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
} // namespace sw::editor
