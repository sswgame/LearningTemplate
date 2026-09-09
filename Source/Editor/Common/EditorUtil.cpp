#include "pch.h"

#include "Editor/Common/EditorUtil.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/Config/EditorData.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"
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

    GameObject* EditorUtil::spawnPrefabFromAssetPath( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent )
    {
        if ( pManager == nullptr || StringUtil::isNullOrEmpty( pPath ) )
            return nullptr;

        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Prefab, pPath ) == false )
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

    void EditorUtil::appendCategoryBadge( string_view category, string& inoutBadge )
    {
        if ( category.empty() )
            return;

        // 같은 Category 의 컴포넌트가 여럿 붙어 있으면 뱃지도 여럿이 된다 — 한 번만 넣는다.
        string token{ "[" };
        token.append( string{ category } );
        token.append( "]" );
        if ( inoutBadge.find( token ) != string::npos )
            return;

        if ( inoutBadge.empty() == false )
            inoutBadge.append( " " );
        inoutBadge.append( token );
    }

} // namespace sw::editor
