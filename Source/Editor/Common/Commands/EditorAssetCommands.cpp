#include "pch.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/AssetEditorManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/Resource/AssetDatabase.h"
#include "Engine/Utility/Resource/ResourceManager.h"
#include "Engine/Utility/Resource/ResourceUtil.h"

#include "RuntimeAPI/Service/EditorService.h"

namespace sw::editor
{
	SW_LOG_CALLER( "EditorAssetCommands" );

	namespace
	{
		void onSaveSceneDialogResult( const vector<string>& listPaths )
		{
			if ( listPaths.empty() )
				return;
			if ( EditorAssetCommands::saveActiveScene( listPaths[0] ) )
				EditorContext::get()->getNotificationManager().push( "Scene", "Saved", NotificationType::Success );
			else
				EditorContext::get()->getNotificationManager().push( "Scene", "Save failed", NotificationType::Error );
		}

		bool isSceneAssetPath( string_view path )
		{
			if ( path.empty() )
				return false;

			const string pathStr{ path };
			const string ext = StringUtil::toLower( FileUtil::getExtension( pathStr ).c_str() );
			if ( ext == ".scene" )
				return true;
			if ( ext == ".xml" && StringUtil::stristr( pathStr.c_str(), ".scene" ) != nullptr )
				return true;
			if ( FileUtil::endsWithIgnoreCase( pathStr, "_scene.xml" ) )
				return true;
			return false;
		}

		bool tryClassifyResourceFile( string_view absPath, EditorResourceIndexEntry& outEntry )
		{
			const string file{ absPath };
			const string ext	  = FileUtil::getExtension( file );
			const string filename = FileUtil::getFileNamePart( file );
			string		 relPath;
			FileUtil::makePathRelative( FileUtil::getCurrentPath(), file, relPath );
			relPath = FileUtil::normalizeSeparators( relPath );

			outEntry._path	 = relPath;
			outEntry._title	 = filename;
			outEntry._detail = relPath;

			if ( ext == ".scene" || ( ext == ".xml" && filename.find( ".scene" ) != string::npos ) )
			{
				outEntry._category = "Scene";
				return true;
			}
			if ( ext == ".prefab" || ext == ".pfb" )
			{
				outEntry._category = "Prefab";
				return true;
			}
			if ( ext == ".png" || ext == ".jpg" || ext == ".dds" || ext == ".tga" || ext == ".bmp" )
			{
				outEntry._category = "Texture";
				return true;
			}
			if ( ext == ".hlsl" || ext == ".glsl" || ext == ".spv" )
			{
				outEntry._category = "Shader";
				return true;
			}
			if ( ext == ".xml" || ext == ".json" )
			{
				outEntry._category = "Data";
				return true;
			}
			return false;
		}

		void appendFolderListingEntry( vector<EditorFolderListingEntry>& outList, const string& path, bool bIsDirectory,
									   const string& rootNorm )
		{
			EditorFolderListingEntry item;
			item._absolutePath = FileUtil::normalizeSeparators( path );
			item._name		   = FileUtil::getFileNamePart( item._absolutePath );
			item._bIsDirectory = bIsDirectory;
			if ( item._bIsDirectory == false )
				item._extension = FileUtil::getExtension( item._name );

			if ( rootNorm.empty() == false )
			{
				const string absNorm = FileUtil::normalizePath( item._absolutePath );
				if ( absNorm.size() > rootNorm.size() && absNorm.compare( 0, rootNorm.size(), rootNorm ) == 0 &&
					 absNorm[rootNorm.size()] == '/' )
					item._relativePath = absNorm.substr( rootNorm.size() + 1 );
			}
			if ( item._relativePath.empty() )
				item._relativePath = FileUtil::normalizePath( item._name );

			if ( item._bIsDirectory == false && FileUtil::endsWithIgnoreCase( item._name, ".meta" ) )
				return;

			outList.push_back( std::move( item ) );
		}
	} // namespace

	bool EditorAssetCommands::openPath( string_view relativePath )
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext == nullptr )
			return false;

		if ( pContext->getAssetEditorManager().openAssetInEditor( relativePath ) )
			return true;

		const string pathStr{ relativePath };
		if ( isSceneAssetPath( pathStr ) )
			return loadScene( pathStr );

		const string lowerExt = StringUtil::toLower( FileUtil::getExtension( pathStr ).c_str() );
		if ( lowerExt == "._material" )
		{
			pContext->getWorkspace().setFocusedAssetPath( pathStr.c_str() );
			pContext->getWorkspace().setInspectMode( InspectMode::Asset );
			return true;
		}

		return false;
	}

	bool EditorAssetCommands::loadScene( string_view path )
	{
		string loadPath = AssetDatabase::toRelativePath( string{ path } );
		if ( loadPath.empty() )
			loadPath = string{ path };

		SceneManager* pSceneManager = editor::getService<SceneManager>();
		if ( pSceneManager == nullptr )
		{
			SW_LOG_ERROR( "Open Scene: SceneManager unavailable" );
			return false;
		}

		if ( pSceneManager->requestLoadAsync( loadPath ) == false )
			return false;

		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			pContext->getWorkspace().clearSelection();

		SW_LOG_INFO( "Open Scene: %#", loadPath );
		return true;
	}

	void EditorAssetCommands::focusPath( string_view relativePath )
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext == nullptr )
			return;
		pContext->getWorkspace().setFocusedAssetPath( string{ relativePath }.c_str() );
	}

	GameObject* EditorAssetCommands::spawnPrefab( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent,
												  const utf8* pUndoLabel )
	{
		GameObject* pSpawned = EditorUtil::spawnPrefabFromAssetPath( pManager, pPath, pParent );
		if ( pSpawned == nullptr )
			return nullptr;

		const utf8* pLabel = ( pUndoLabel != nullptr ) ? pUndoLabel : "Spawn Prefab";
		EditorTransaction::recordCreation( GameObjectPtr{ pSpawned }, pLabel );
		EditorSceneCommands::select( pSpawned, SelectionMode::Replace );
		return pSpawned;
	}

	GameObject* EditorAssetCommands::spawnSprite( GameObjectManager* pManager, const utf8* pPath, const float3& worldPos )
	{
		if ( pManager == nullptr || pPath == nullptr || pPath[0] == '\0' )
			return nullptr;

		string filename = FileUtil::getFileNamePart( pPath );
		filename		= FileUtil::removeExtension( filename );

		GameObject* pSpawned = pManager->createGameObject( hashed_string( filename.c_str() ) );
		if ( pSpawned == nullptr )
			return nullptr;

		SceneComponent* pSc = pSpawned->addComponent<SceneComponent>();
		if ( pSc != nullptr )
			pSc->setLocalPosition( worldPos );

		SpriteComponent* pSprite = pSpawned->addComponent<SpriteComponent>();
		if ( pSprite != nullptr )
		{
			string relPath;
			FileUtil::makePathRelative( FileUtil::getCurrentPath(), pPath, relPath );
			relPath = FileUtil::normalizeSeparators( relPath );
			pSprite->setTextureName( relPath );
		}

		EditorTransaction::recordCreation( GameObjectPtr{ pSpawned }, "Spawn Sprite in Viewport" );
		EditorSceneCommands::select( pSpawned, SelectionMode::Replace );
		return pSpawned;
	}

	void EditorAssetCommands::dropAt( GameObjectManager* pManager, const utf8* pPath, const float3& spawnPos )
	{
		if ( pManager == nullptr || pPath == nullptr )
			return;

		const string ext = FileUtil::getExtension( pPath );
		if ( ext == ".prefab" || ext == ".pfb" || StringUtil::stristr( pPath, ".prefab.xml" ) != nullptr )
		{
			GameObject* pSpawned = spawnPrefab( pManager, pPath, nullptr, "Spawn Prefab in Viewport" );
			if ( pSpawned == nullptr )
				return;
			SceneComponent* pSc = pSpawned->getPrimarySceneComponent();
			if ( pSc != nullptr )
				pSc->setLocalPosition( spawnPos );
			return;
		}

		if ( isSceneAssetPath( pPath ) )
		{
			loadScene( pPath );
			return;
		}

		if ( ext == ".png" || ext == ".jpg" || ext == ".dds" || ext == ".tga" || ext == ".bmp" )
		{
			spawnSprite( pManager, pPath, spawnPos );
			return;
		}

		openPath( pPath );
	}

	bool EditorAssetCommands::saveActiveScene( string_view path )
	{
		SceneManager* pSceneManager = editor::getService<SceneManager>();
		if ( pSceneManager == nullptr )
			return false;
		if ( pSceneManager->saveActiveScene( path ) == false )
			return false;
		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			pContext->getWorkspace().clearSceneDirty();
		return true;
	}

	void EditorAssetCommands::saveActiveSceneOrPrompt()
	{
		EditorContext* pContext		 = EditorContext::get();
		SceneManager*  pSceneManager = editor::getService<SceneManager>();
		Scene*		   pScene		 = ( pSceneManager != nullptr ) ? pSceneManager->getActiveScene() : nullptr;
		if ( pScene != nullptr && pScene->getSourcePath().empty() == false )
		{
			if ( saveActiveScene( {} ) )
			{
				if ( pContext != nullptr )
					pContext->getNotificationManager().push( "Scene", "Saved", NotificationType::Success );
			}
			else if ( pContext != nullptr )
				pContext->getNotificationManager().push( "Scene", "Save failed", NotificationType::Error );
			return;
		}

		FileDialogParams params{};
		params._type				= FileDialogParams::Type::Save;
		params._title				= "Save Scene";
		params._description			= "Scene";
		params._bEnableMultiselect	= false;
		params._filterExtensionList = { ".scene.xml", ".xml" };
		const string mapsDir		= FileUtil::joinPath( ResourceUtil::getGameFolderPath(), "demo/maps" );
		if ( FileUtil::directoryExists( mapsDir ) )
			params._initialDirectory = mapsDir;
		FileUtil::openFileDialog( params, SW_DELEGATE_FUNCTION( FileDialogDelegate, onSaveSceneDialogResult ) );
	}

	uint32 EditorAssetCommands::importFiles( string_view destFolderAbs, const vector<string>& listSourcePath )
	{
		if ( destFolderAbs.empty() )
		{
			SW_LOG_WARNING( "Import cancelled — no destination folder." );
			return 0;
		}

		uint32 copied{ 0 };
		for ( const string& sourcePath : listSourcePath )
		{
			if ( FileUtil::fileExists( sourcePath ) == false )
			{
				SW_LOG_WARNING( "Import skipped (missing): %#", sourcePath.c_str() );
				continue;
			}

			const string fileName = FileUtil::getFileNamePart( sourcePath );
			const string destPath = ResourceUtil::makeSavePath( destFolderAbs, fileName );

			if ( FileUtil::pathsEqualNormalized( sourcePath, destPath ) )
			{
				SW_LOG_TRACE( "Already in folder: %#", fileName.c_str() );
				continue;
			}

			FileUtil::createDirectory( destPath );
			if ( FileUtil::copyFile( sourcePath, destPath ) == false )
			{
				SW_LOG_ERROR( "Failed to import: %#", sourcePath.c_str() );
				continue;
			}

			++copied;
			const string rel = AssetDatabase::toRelativePath( destPath );
			if ( rel.empty() == false )
			{
				ResourceManager* pResources = editor::getService<ResourceManager>();
				if ( pResources != nullptr )
					pResources->getAssetDatabase().ensureMeta( rel, true );
			}
			SW_LOG_TRACE( "Imported: %# -> %#", sourcePath.c_str(), destPath.c_str() );
		}
		return copied;
	}

	bool EditorAssetCommands::deleteAsset( string_view absolutePath )
	{
		if ( absolutePath.empty() )
			return false;
		const string abs{ absolutePath };
		const bool	 bRemoved = FileUtil::removeFile( abs );
		const string metaPath = abs + ".meta";
		if ( FileUtil::fileExists( metaPath ) )
			FileUtil::removeFile( metaPath );
		return bRemoved;
	}

	void EditorAssetCommands::collectResourceIndex( vector<EditorResourceIndexEntry>& outList )
	{
		outList.clear();

		const string   resourceFolder = FileUtil::joinPath( FileUtil::getCurrentPath(), "Resource" );
		vector<string> listAllFiles;
		FileUtil::collectFiles( resourceFolder, "", listAllFiles, true, false );

		outList.reserve( listAllFiles.size() );
		for ( const string& file : listAllFiles )
		{
			EditorResourceIndexEntry entry{};
			if ( tryClassifyResourceFile( file, entry ) )
				outList.push_back( std::move( entry ) );
		}
	}

	void EditorAssetCommands::collectFolderListing( string_view folderAbs, vector<EditorFolderListingEntry>& outList )
	{
		outList.clear();
		if ( folderAbs.empty() || FileUtil::directoryExists( folderAbs ) == false )
			return;

		vector<string> listFolders;
		vector<string> listFiles;
		FileUtil::collectFolders( folderAbs, listFolders, false, false );
		FileUtil::collectFiles( folderAbs, {}, listFiles, false, false );

		const string& resourceRoot = ResourceUtil::getRootFolderPath();
		const string  rootNorm	   = resourceRoot.empty() ? string{} : FileUtil::normalizePath( resourceRoot );

		for ( const string& folder : listFolders )
			appendFolderListingEntry( outList, folder, true, rootNorm );
		for ( const string& file : listFiles )
			appendFolderListingEntry( outList, file, false, rootNorm );
	}

	void EditorAssetCommands::collectChildFolders( string_view folderAbs, vector<string>& outList )
	{
		outList.clear();
		if ( folderAbs.empty() || FileUtil::directoryExists( folderAbs ) == false )
			return;

		FileUtil::collectFolders( folderAbs, outList, false, false );
		for ( string& child : outList )
			child = FileUtil::normalizeSeparators( child );
	}

	void EditorAssetCommands::collectResourceCatalogCounts( EditorResourceCatalogCounts& outCounts )
	{
		vector<string> listScenes;
		vector<string> listPrefabs;
		vector<string> listTextures;
		vector<string> listShaders;
		const string   resPath = FileUtil::joinPath( FileUtil::getCurrentPath(), "Resource" );
		FileUtil::collectFiles( resPath, ".scene.xml", listScenes, true, false );
		FileUtil::collectFiles( resPath, ".prefab.xml", listPrefabs, true, false );
		FileUtil::collectFiles( resPath, ".png", listTextures, true, false );
		FileUtil::collectFiles( resPath, ".hlsl", listShaders, true, false );

		outCounts._sceneCount	= listScenes.size();
		outCounts._prefabCount	= listPrefabs.size();
		outCounts._textureCount = listTextures.size();
		outCounts._shaderCount	= listShaders.size();
	}
} // namespace sw::editor
