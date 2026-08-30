#include "pch.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/EditorInspectorCommands.h"
#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/EditorPlaySession.h"
#include "Editor/Common/EditorSessionPolicy.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/AssetEditorManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/Resource/AssetDatabase.h"
#include "Engine/Utility/Resource/ResourceManager.h"
#include "Engine/Utility/Resource/ResourceUtil.h"
#include "Engine/Window/IWindow.h"

#include "RuntimeAPI/Service/EditorService.h"

namespace sw::editor
{
	namespace
	{
		struct EditorAssetCommandsInternal
		{
			static void onSaveSceneDialogResult( const vector<string>& listPaths )
			{
				if ( listPaths.empty() )
					return;
				if ( EditorAssetCommands::saveActiveScene( listPaths[0] ) )
				{
					EditorContext::get()->getNotificationManager().push( "Scene", "Saved", NotificationType::Success );
					EditorContext* pContext = EditorContext::get();
					if ( pContext != nullptr && pContext->getWorkspace().getPendingSceneAction() != EditorPendingSceneAction::None )
						EditorAssetCommands::applyUnsavedSceneChoice( EditorUnsavedChoice::Discard );
				}
				else
					EditorContext::get()->getNotificationManager().push( "Scene", "Save failed", NotificationType::Error );
			}

			static bool isPlayStoppedForSceneSwap()
			{
				if ( EditorPlaySession::isStopped() )
					return true;
				EditorContext* pContext = EditorContext::get();
				if ( pContext != nullptr )
					pContext->getNotificationManager().push( "Scene", "Stop play before opening or creating a scene",
															 NotificationType::Warning );
				return false;
			}

			static void runPendingSceneAction()
			{
				EditorContext* pContext = EditorContext::get();
				if ( pContext == nullptr )
					return;
				EditorWorkspace&			   ws	  = pContext->getWorkspace();
				const EditorPendingSceneAction action = ws.getPendingSceneAction();
				const string				   path	  = ws.getPendingSceneActionPath();
				ws.clearPendingSceneAction();
				if ( action == EditorPendingSceneAction::Load )
					EditorAssetCommands::loadScene( path );
				else if ( action == EditorPendingSceneAction::New )
					EditorAssetCommands::tryCreateNewScene();
				else if ( action == EditorPendingSceneAction::Quit )
				{
					IWindow* pWindow = IWindow::getActiveWindow();
					if ( pWindow != nullptr )
						pWindow->requestClose();
				}
			}

			static bool tryClassifyResourceFile( string_view absPath, EditorResourceIndexEntry& outEntry )
			{
				const string  file{ absPath };
				const string  filename = FileUtil::getFileNamePart( file );
				string		  relPath;
				const string& projectRoot = ResourceUtil::getProjectFolderPath();
				FileUtil::makePathRelative( projectRoot.empty() ? FileUtil::getCurrentPath() : projectRoot, file, relPath );
				relPath = FileUtil::normalizeSeparators( relPath );

				outEntry._path	 = relPath;
				outEntry._title	 = filename;
				outEntry._detail = relPath;

				if ( EditorUtil::isSceneAssetPath( file.c_str() ) )
				{
					outEntry._category = "Scene";
					return true;
				}
				if ( EditorUtil::isPrefabAssetPath( file.c_str() ) )
				{
					outEntry._category = "Prefab";
					return true;
				}
				if ( EditorUtil::isTextureAssetPath( file.c_str() ) )
				{
					outEntry._category = "Texture";
					return true;
				}
				if ( EditorUtil::isShaderAssetPath( file.c_str() ) )
				{
					outEntry._category = "Shader";
					return true;
				}
				if ( FileUtil::hasAnyExtension( file, { ".xml", ".json" } ) )
				{
					outEntry._category = "Data";
					return true;
				}
				return false;
			}

			static void appendFolderListingEntry( vector<EditorFolderListingEntry>& outList, const string& path, bool bIsDirectory,
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
					if ( FileUtil::startsWithPathComponent( absNorm, rootNorm ) )
						item._relativePath = FileUtil::suffixAfterPathComponent( absNorm, rootNorm );
				}
				if ( item._relativePath.empty() )
					item._relativePath = FileUtil::normalizePath( item._name );

				if ( item._bIsDirectory == false && FileUtil::endsWithIgnoreCase( item._name, ".meta" ) )
					return;

				outList.push_back( std::move( item ) );
			}

			static bool matchesPrefabPath( const EditorWorkspace& ws, const GameObject* pObject, string_view prefabPath )
			{
				if ( pObject == nullptr || prefabPath.empty() )
					return false;
				const string& mapped = ws.getGameObjectPrefabPath( pObject->getObjectId() );
				if ( mapped.empty() == false && FileUtil::pathsEqualNormalized( mapped, prefabPath ) )
					return true;
				return false;
			}

			static GameObject* findPrefabInstance( GameObjectManager* pManager, EditorWorkspace& ws, string_view prefabPath,
												   GameObject* pUnderRoot )
			{
				if ( pManager == nullptr )
					return nullptr;

				GameObject* pPrimary = ws.getSelectedObject().get();
				if ( pPrimary != nullptr && matchesPrefabPath( ws, pPrimary, prefabPath ) )
				{
					if ( pUnderRoot == nullptr || pPrimary->isDescendantOf( pUnderRoot ) )
						return pPrimary;
				}

				GameObject*				  pFallback	  = nullptr;
				const vector<GameObject*> listObjects = pManager->getAllGameObjects();
				for ( GameObject* pObject : listObjects )
				{
					if ( pObject == nullptr || pObject->isPendingKill() )
						continue;
					if ( matchesPrefabPath( ws, pObject, prefabPath ) == false )
						continue;
					if ( pUnderRoot != nullptr && pObject->isDescendantOf( pUnderRoot ) == false )
						continue;
					if ( pUnderRoot != nullptr )
						return pObject;
					if ( pFallback == nullptr )
						pFallback = pObject;
				}
				return pFallback;
			}

			static void hideObjectsOutsideIsolation( GameObjectManager* pManager, GameObject* pRoot,
													 vector<PrefabIsolationHiddenObject>& outHidden )
			{
				outHidden.clear();
				if ( pManager == nullptr || pRoot == nullptr )
					return;

				const vector<GameObject*> listObjects = pManager->getAllGameObjects();
				for ( GameObject* pObject : listObjects )
				{
					if ( pObject == nullptr || pObject->isPendingKill() )
						continue;
					if ( pObject->isDescendantOf( pRoot ) )
						continue;
					if ( pRoot->isDescendantOf( pObject ) )
						continue;

					PrefabIsolationHiddenObject entry{};
					entry._objectId	  = pObject->getObjectId();
					entry._bWasActive = pObject->isActive() ? SW_TRUE : SW_FALSE;
					outHidden.push_back( entry );
					pObject->setActive( false );
				}
			}

			static void restoreIsolationHidden( GameObjectManager* pManager, const vector<PrefabIsolationHiddenObject>& listHidden )
			{
				if ( pManager == nullptr )
					return;
				for ( const PrefabIsolationHiddenObject& entry : listHidden )
				{
					GameObject* pObject = pManager->findGameObjectById( entry._objectId );
					if ( pObject == nullptr )
						continue;
					pObject->setActive( entry._bWasActive == SW_TRUE );
				}
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	SW_LOG_CALLER( "EditorAssetCommands" );

	bool EditorAssetCommands::openPath( string_view relativePath )
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext == nullptr )
			return false;

		if ( pContext->getAssetEditorManager().openAssetInEditor( relativePath ) )
			return true;

		const string pathStr{ relativePath };
		if ( EditorUtil::isSceneAssetPath( pathStr.c_str() ) )
			return tryOpenScene( pathStr );

		if ( EditorUtil::isMaterialAssetPath( pathStr.c_str() ) )
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

	bool EditorAssetCommands::tryOpenScene( string_view path )
	{
		if ( EditorAssetCommandsInternal::isPlayStoppedForSceneSwap() == false )
			return false;

		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr && EditorSessionPolicy::needsUnsavedPrompt( pContext->getWorkspace().isSceneDirty() ) )
		{
			pContext->getWorkspace().setPendingSceneAction( EditorPendingSceneAction::Load, path );
			return true;
		}
		return loadScene( path );
	}

	bool EditorAssetCommands::tryCreateNewScene()
	{
		if ( EditorAssetCommandsInternal::isPlayStoppedForSceneSwap() == false )
			return false;

		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr && EditorSessionPolicy::needsUnsavedPrompt( pContext->getWorkspace().isSceneDirty() ) )
		{
			pContext->getWorkspace().setPendingSceneAction( EditorPendingSceneAction::New );
			return true;
		}

		SceneManager* pSceneManager = editor::getService<SceneManager>();
		if ( pSceneManager == nullptr )
			return false;
		Scene* pScene = pSceneManager->createEmptyActiveScene( "Untitled" );
		if ( pScene == nullptr )
			return false;
		syncAfterSceneGenerationChange();
		if ( pContext != nullptr )
			pContext->getNotificationManager().push( "Scene", "New scene", NotificationType::Info );
		return true;
	}

	void EditorAssetCommands::saveFocusedOrScene()
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr && pContext->getPanelManager().saveFocusedDirtyDocument() )
			return;
		saveActiveSceneOrPrompt();
	}

	void EditorAssetCommands::applyUnsavedSceneChoice( EditorUnsavedChoice choice )
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext == nullptr )
			return;
		EditorWorkspace& ws = pContext->getWorkspace();
		if ( ws.getPendingSceneAction() == EditorPendingSceneAction::None )
			return;

		if ( EditorSessionPolicy::shouldSaveBeforeAction( choice ) )
		{
			if ( pContext->getPanelManager().saveAllDirtyDocuments() == false )
			{
				pContext->getNotificationManager().push( "Editor", "Document save failed", NotificationType::Error );
				return;
			}
			if ( ws.isSceneDirty() )
			{
				SceneManager* pSceneManager = editor::getService<SceneManager>();
				Scene*		  pScene		= ( pSceneManager != nullptr ) ? pSceneManager->getActiveScene() : nullptr;
				if ( pScene != nullptr && pScene->getSourcePath().empty() == false )
				{
					if ( saveActiveScene( {} ) == false )
					{
						pContext->getNotificationManager().push( "Scene", "Save failed", NotificationType::Error );
						return;
					}
				}
				else
				{
					saveActiveSceneOrPrompt();
					return;
				}
			}
		}

		if ( EditorSessionPolicy::shouldClearDirtyWithoutSave( choice ) )
		{
			ws.clearSceneDirty();
			pContext->getPanelManager().discardAllDirtyDocuments();
		}

		if ( EditorSessionPolicy::shouldProceedWithAction( choice ) )
			EditorAssetCommandsInternal::runPendingSceneAction();
		else
			ws.clearPendingSceneAction();
	}

	void EditorAssetCommands::syncAfterSceneGenerationChange()
	{
		EditorContext* pContext		 = EditorContext::get();
		SceneManager*  pSceneManager = editor::getService<SceneManager>();
		if ( pContext == nullptr || pSceneManager == nullptr )
			return;

		EditorWorkspace& ws			= pContext->getWorkspace();
		const uint64	 generation = pSceneManager->getSceneGeneration();
		if ( generation == ws.getObservedSceneGeneration() )
			return;

		ws.setObservedSceneGeneration( generation );
		ws.clearSceneDirty();
		ws.clearSelection();
		Scene*			   pScene	= pSceneManager->getActiveScene();
		GameObjectManager* pManager = ( pScene != nullptr ) ? pScene->getObjectManager() : nullptr;
		ws.rebuildGameObjectPrefabMap( pManager );
	}

	bool EditorAssetCommands::tryBeginQuit()
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext == nullptr )
			return true;
		if ( pContext->getWorkspace().getPendingSceneAction() != EditorPendingSceneAction::None )
			return false;

		const bool	 bSceneDirty = pContext->getWorkspace().isSceneDirty();
		const uint32 dirtyDocs	 = pContext->getPanelManager().countDirtyDocuments();
		if ( EditorSessionPolicy::needsQuitPrompt( bSceneDirty, dirtyDocs ) == false )
			return true;

		pContext->getWorkspace().setPendingSceneAction( EditorPendingSceneAction::Quit );
		return false;
	}

	void EditorAssetCommands::requestExit()
	{
		IWindow* pWindow = IWindow::getActiveWindow();
		if ( pWindow != nullptr )
			pWindow->tryBeginClose();
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
		if ( EditorUtil::areSceneEditsAllowed() == false )
			return nullptr;
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
		if ( EditorUtil::areSceneEditsAllowed() == false )
			return nullptr;
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
			string		  relPath;
			const string& projectRoot = ResourceUtil::getProjectFolderPath();
			FileUtil::makePathRelative( projectRoot.empty() ? FileUtil::getCurrentPath() : projectRoot, pPath, relPath );
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

		if ( EditorUtil::isPrefabAssetPath( pPath ) )
		{
			GameObject* pSpawned = spawnPrefab( pManager, pPath, nullptr, "Spawn Prefab in Viewport" );
			if ( pSpawned == nullptr )
				return;
			SceneComponent* pSc = pSpawned->getPrimarySceneComponent();
			if ( pSc != nullptr )
				pSc->setLocalPosition( spawnPos );
			return;
		}

		if ( EditorUtil::isSceneAssetPath( pPath ) )
		{
			tryOpenScene( pPath );
			return;
		}

		if ( EditorUtil::isTextureAssetPath( pPath ) )
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
		params._listFilterExtension = { ".scene.xml", ".xml" };
		const string mapsDir		= ResourceUtil::joinActivePackPath( path::kMapsFolder );
		if ( FileUtil::directoryExists( mapsDir ) )
			params._initialDirectory = mapsDir;
		FileUtil::openFileDialog( params, SW_DELEGATE_FUNCTION( FileDialogDelegate, EditorAssetCommandsInternal::onSaveSceneDialogResult ) );
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

		const string resourceFolder = ResourceUtil::getRootFolderPath();
		if ( resourceFolder.empty() )
			return;

		vector<string> listAllFiles;
		FileUtil::collectFiles( resourceFolder, "", listAllFiles, true, false );

		outList.reserve( listAllFiles.size() );
		for ( const string& file : listAllFiles )
		{
			EditorResourceIndexEntry entry{};
			if ( EditorAssetCommandsInternal::tryClassifyResourceFile( file, entry ) )
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
			EditorAssetCommandsInternal::appendFolderListingEntry( outList, folder, true, rootNorm );
		for ( const string& file : listFiles )
			EditorAssetCommandsInternal::appendFolderListingEntry( outList, file, false, rootNorm );
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
		const string resPath = ResourceUtil::getRootFolderPath();
		if ( resPath.empty() )
			return;

		vector<string> listScenes;
		vector<string> listPrefabs;
		vector<string> listTextures;
		vector<string> listShaders;
		FileUtil::collectFiles( resPath, ".scene.xml", listScenes, true, false );
		FileUtil::collectFiles( resPath, ".prefab.xml", listPrefabs, true, false );
		FileUtil::collectFiles( resPath, ".png", listTextures, true, false );
		FileUtil::collectFiles( resPath, ".hlsl", listShaders, true, false );

		outCounts._sceneCount	= listScenes.size();
		outCounts._prefabCount	= listPrefabs.size();
		outCounts._textureCount = listTextures.size();
		outCounts._shaderCount	= listShaders.size();
	}

	bool EditorAssetCommands::enterPrefabIsolation( string_view prefabPath )
	{
		if ( prefabPath.empty() )
			return false;
		if ( EditorPlaySession::isStopped() == false )
			return false;

		EditorContext* pContext = EditorContext::get();
		if ( pContext == nullptr )
			return false;
		EditorWorkspace& ws = pContext->getWorkspace();
		if ( ws.isPrefabIsolationActive() == false && ws.isSceneDirty() &&
			 EditorSessionPolicy::requiresCleanSceneForPrefabIsolation() )
		{
			pContext->getNotificationManager().push( "Prefab", "Save the scene before opening prefab isolation",
													 NotificationType::Warning );
			return false;
		}

		SceneManager* pSceneManager = editor::getService<SceneManager>();
		if ( pSceneManager == nullptr )
			return false;
		Scene* pScene = pSceneManager->getActiveScene();
		if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
			return false;
		GameObjectManager* pManager = pScene->getObjectManager();

		GameObject* pUnderRoot = nullptr;
		if ( ws.isPrefabIsolationActive() )
			pUnderRoot = pManager->findGameObjectById( ws.getPrefabIsolationRootId() );

		const string pathStr{ prefabPath };
		GameObject*	 pRoot = EditorAssetCommandsInternal::findPrefabInstance( pManager, ws, prefabPath, pUnderRoot );
		uint8		 bSpawnedRoot{ SW_FALSE };
		if ( pRoot == nullptr )
		{
			pRoot = EditorUtil::spawnPrefabFromAssetPath( pManager, pathStr.c_str(), pUnderRoot );
			if ( pRoot == nullptr )
				return false;
			bSpawnedRoot = SW_TRUE;
		}

		PrefabIsolationFrame frame{};
		frame._prefabPath	= pathStr;
		frame._rootObjectId = pRoot->getObjectId();
		frame._bSpawnedRoot = bSpawnedRoot;
		EditorAssetCommandsInternal::hideObjectsOutsideIsolation( pManager, pRoot, frame._listHidden );
		ws.pushPrefabIsolation( std::move( frame ) );
		ws.selectGameObject( GameObjectPtr{ pRoot } );
		ws.setFocusedAssetPath( pathStr.c_str() );
		ws.requestOpenPanel( "Prefab Editor" );
		pContext->getNotificationManager().push( "Prefab", "Isolated prefab in the current scene", NotificationType::Info );
		return true;
	}

	bool EditorAssetCommands::exitPrefabIsolation( bool bSaveToPrefab )
	{
		EditorContext* pContext = EditorContext::get();
		if ( pContext == nullptr )
			return false;
		EditorWorkspace& ws = pContext->getWorkspace();
		if ( ws.isPrefabIsolationActive() == false )
			return false;
		const PrefabIsolationFrame* pFrame = ws.getPrefabIsolationFrame();
		if ( pFrame == nullptr )
			return false;

		const PrefabIsolationFrame frame		 = *pFrame;
		SceneManager*			   pSceneManager = editor::getService<SceneManager>();
		if ( pSceneManager == nullptr )
			return false;
		Scene*			   pScene	= pSceneManager->getActiveScene();
		GameObjectManager* pManager = ( pScene != nullptr ) ? pScene->getObjectManager() : nullptr;
		GameObject*		   pRoot	= nullptr;
		if ( pManager != nullptr )
			pRoot = pManager->findGameObjectById( frame._rootObjectId );

		if ( bSaveToPrefab && pRoot != nullptr )
			EditorInspectorCommands::applyToPrefab( pRoot, frame._prefabPath );

		EditorAssetCommandsInternal::restoreIsolationHidden( pManager, frame._listHidden );

		const bool bFullyExit = ws.popPrefabIsolation();
		if ( frame._bSpawnedRoot == SW_TRUE && bSaveToPrefab == false && pManager != nullptr && pRoot != nullptr )
		{
			if ( pContext->getSelectionManager().hasObject( GameObjectPtr{ pRoot } ) )
				pContext->getSelectionManager().selectObject( GameObjectPtr{ pRoot }, SelectionMode::Remove );
			pManager->destroyObject( pRoot );
			pRoot = nullptr;
		}
		else if ( frame._bSpawnedRoot == SW_TRUE && bSaveToPrefab )
			ws.markSceneDirty();

		if ( bFullyExit )
		{
			pContext->getNotificationManager().push( "Prefab", "Exited prefab isolation", NotificationType::Info );
			return true;
		}

		GameObject* pParentRoot = nullptr;
		if ( pManager != nullptr )
			pParentRoot = pManager->findGameObjectById( ws.getPrefabIsolationRootId() );
		if ( pParentRoot != nullptr )
			ws.selectGameObject( GameObjectPtr{ pParentRoot } );
		ws.setFocusedAssetPath( ws.getPrefabIsolationPrefabPath().c_str() );
		return true;
	}
} // namespace sw::editor
