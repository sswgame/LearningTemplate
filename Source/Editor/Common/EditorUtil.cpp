#include "pch.h"

#include "Editor/Common/EditorUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorUtil" );

    GameObject* EditorUtil::spawnPrefabFromAssetPath( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent )
    {
        if ( pManager == nullptr || StringUtil::isNullOrEmpty( pPath ) )
            return nullptr;

        if ( EditorAssetTypeRegistry::matches( EditorAssetType::Prefab, pPath ) == false )
        {
            SW_LOG_TRACE( "Not a prefab path: %#", pPath );
            return nullptr;
        }

        // `getService<T>()` 는 nullptr 을 반환할 수 있다(문서에 그렇게 적혀 있다). 역참조하기 전에 확인한다.
        AssetManager* pResources = editor::getService<AssetManager>();
        if ( pResources == nullptr )
        {
            SW_LOG_WARNING( "AssetManager 서비스가 없어 프리팹을 스폰할 수 없습니다: %#", pPath );
            return nullptr;
        }

        GameObject* pSpawned = pResources->getPrefabCache().spawn( pManager, pPath );
        if ( pSpawned == nullptr )
        {
            SW_LOG_WARNING( "Failed to spawn prefab: %#", pPath );
            return nullptr;
        }

        if ( pParent != nullptr && pSpawned->attachToParent( pParent ) == false )
            SW_LOG_WARNING( "Spawned prefab could not be put under '%#' - it stays at the root", pParent->getName().c_str() );

        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr )
            pContext->getWorkspace().setGameObjectPrefabPath( pSpawned->getObjectID(), pPath );

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

        // 같은 Category 의 컴포넌트가 여럿 붙어 있으면 뱃지도 여럿이 된다. 그래서 한 번만 넣는다.
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
