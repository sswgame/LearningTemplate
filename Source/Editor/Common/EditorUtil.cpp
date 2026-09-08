#include "pch.h"

#include "Editor/Common/EditorUtil.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/EditorPlaySession.h"
#include "Editor/Common/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/ResourceManager.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorUtil" );

    string EditorUtil::getProjectRootPath()
    {
        const string& projectRoot = ResourceUtil::getProjectFolderPath();
        if ( projectRoot.empty() == false )
            return projectRoot;

        const string& resourceRoot = ResourceUtil::getRootFolderPath();
        if ( resourceRoot.empty() )
            return {};

        return FileUtil::getDirectoryPart( FileUtil::trimTrailingSlashes( resourceRoot ) );
    }

    string EditorUtil::getEditorConfigDirectory()
    {
        const EditorConfig& editorCfg   = EditorConfig::getActive();
        const string        projectRoot = getProjectRootPath();
        if ( projectRoot.empty() )
            return {};

        const string configDir =
            FileUtil::joinPath( FileUtil::joinPath( projectRoot, editorCfg._configFolder ), editorCfg._editorConfigFolder );
        const string markerFile = FileUtil::joinPath( configDir, editorCfg._imguiIniFile );
        FileUtil::createParentDirectory( markerFile );
        return configDir;
    }

    string EditorUtil::resolveEditorConfigFile( const utf8* pFileName )
    {
        if ( StringUtil::isNullOrEmpty( pFileName ) )
            return {};

        const string configDir = getEditorConfigDirectory();
        if ( configDir.empty() )
            return {};

        return FileUtil::joinPath( configDir, pFileName );
    }

    bool EditorUtil::isPrefabAssetPath( const utf8* pPath )
    {
        return EditorAssetTypeRegistry::matches( EditorAssetKind::Prefab, pPath );
    }

    bool EditorUtil::isTextureAssetPath( const utf8* pPath )
    {
        return EditorAssetTypeRegistry::matches( EditorAssetKind::Texture, pPath );
    }

    bool EditorUtil::isMaterialAssetPath( const utf8* pPath )
    {
        return EditorAssetTypeRegistry::matches( EditorAssetKind::Material, pPath );
    }

    bool EditorUtil::isSceneAssetPath( const utf8* pPath )
    {
        return EditorAssetTypeRegistry::matches( EditorAssetKind::Scene, pPath );
    }

    bool EditorUtil::isShaderAssetPath( const utf8* pPath )
    {
        return EditorAssetTypeRegistry::matches( EditorAssetKind::Shader, pPath );
    }

    bool EditorUtil::isAudioAssetPath( const utf8* pPath )
    {
        return EditorAssetTypeRegistry::matches( EditorAssetKind::Audio, pPath );
    }

    bool EditorUtil::isDataAssetPath( const utf8* pPath )
    {
        return EditorAssetTypeRegistry::matches( EditorAssetKind::Data, pPath );
    }

    GameObject* EditorUtil::spawnPrefabFromAssetPath( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent )
    {
        if ( pManager == nullptr || StringUtil::isNullOrEmpty( pPath ) )
            return nullptr;

        if ( isPrefabAssetPath( pPath ) == false )
        {
            SW_LOG_TRACE( "Not a prefab path: %#", pPath );
            return nullptr;
        }

        GameObject* pSpawned = editor::getService<ResourceManager>()->getPrefabManager().spawn( pManager, pPath );
        if ( pSpawned == nullptr )
        {
            SW_LOG_WARNING( "Failed to spawn prefab: %#", pPath );
            return nullptr;
        }

        if ( pParent != nullptr )
            pSpawned->attachToParent( pParent );

        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr )
            pContext->getWorkspace().setGameObjectPrefabPath( pSpawned->getObjectId(), pPath );

        SW_LOG_TRACE( "Spawned prefab from %#", pPath );
        return pSpawned;
    }

    bool EditorUtil::areSceneEditsAllowed()
    {
        return EditorSessionPolicy::areSceneEditsAllowed( EditorPlaySession::isStopped() );
    }
} // namespace sw::editor
