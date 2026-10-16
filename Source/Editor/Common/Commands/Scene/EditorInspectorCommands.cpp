#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorInspectorCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Object/Prefab/PrefabOverrides.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorInspectorCommands" );

    bool EditorInspectorCommands::applyToPrefab( GameObject* pObj, string_view prefabPath )
    {
        if ( EditorPlaySession::areSceneEditsAllowed() == false )
            return false;
        if ( pObj == nullptr || prefabPath.empty() )
            return false;

        // 에셋 관리자와 에디터 컨텍스트가 없으면(도구 · 테스트) 파일만 쓴다 — 캐시도 다른 인스턴스도 없다.
        AssetManager*  pResources   = editor::findSubsystem<AssetManager>();
        EditorContext* pContext     = EditorContext::get();
        PrefabCache*   pPrefabCache = pResources != nullptr ? &pResources->getAssetCache<PrefabCache>() : nullptr;

        // 고치기 전 원형과 파일 글 — 다른 인스턴스의 덮어쓴 값을 옛 원형과 견줘 뽑고, 되돌리기가 파일을 되돌린다.
        const string       filePath = ResourcePaths::getWritePath( prefabPath );
        string             oldFileText;
        string             oldBaseState;
        const bool         bHadFile   = FileUtil::exists( filePath ) && FileUtil::readTextFile( filePath, oldFileText );
        const PrefabAsset* pOldPrefab = ( bHadFile && pPrefabCache != nullptr ) ? pPrefabCache->loadPrefab( prefabPath ) : nullptr;
        if ( pOldPrefab == nullptr || PrefabOverrides::makeBaseState( *pOldPrefab, oldBaseState ) == false )
            oldBaseState.clear();

        // 형식은 경로가 정한다(`.prefab.json` 에는 JSON) — 프리팹 경로가 아니면 쓰지 않는다.
        PrefabAsset asset;
        asset.setFromGameObject( pObj );
        if ( asset.saveToFile( prefabPath ) == false )
            return false;
        string newFileText;
        (void)FileUtil::readTextFile( filePath, newFileText ); // 방금 쓴 파일이다 — 못 읽으면 다시 실행이 파일을 쓰지 않을 뿐이다

        SW_LOG_INFO( "Saved prefab changes to %#", string{ prefabPath }.c_str() );
        if ( pPrefabCache == nullptr || pContext == nullptr )
            return true;
        // 캐시된 옛 내용으로 스폰하지 않게 다음 로드가 파일을 다시 읽도록 한다.
        PrefabCache& prefabCache = *pPrefabCache;
        prefabCache.reload( prefabPath, nullptr );

        // 같은 프리팹의 다른 인스턴스에 퍼뜨린다(덮어쓴 값은 남긴다). 파일 쓰기와 인스턴스 갱신을 되돌리기 하나로 묶는다.
        // 인스턴스는 두 갈래다 — 씬에 놓은 인스턴스(에디터 작업 공간이 경로를 기록)와, 다른 프리팹 인스턴스가 스폰한 중첩 부품
        // (`PrefabInstanceRegistry`). 부품은 씬에 저장되지 않고 바깥 프리팹이 다시 스폰하므로 지금 보이는 부품만 고치면 된다(언리얼 차일드 액터).
        EditorWorkspace&    workspace = pContext->getSubsystem<EditorWorkspace>();
        GameObjectManager*  pManager  = pObj->getManager();
        vector<GameObject*> listInstance;
        if ( pManager != nullptr && oldBaseState.empty() == false )
        {
            pManager->forEachGameObject( [&]( GameObject* pOther )
            {
                const bool bNotOtherLiveObject = pOther == nullptr || pOther == pObj || pOther->isPendingDestroy();
                if ( bNotOtherLiveObject )
                    return;
                const string_view otherPrefab = workspace.findPrefabPath( pOther );
                if ( otherPrefab.empty() == false && FileUtil::pathsEqualNormalized( otherPrefab, prefabPath ) )
                    listInstance.push_back( pOther );
            } );
        }
        EditorTransaction::beginTransaction( "Apply Prefab Overrides" );
        if ( bHadFile )
        {
            const string savedPath{ prefabPath };
            const auto   writePrefabFile = [savedPath, filePath]( const string& text )
            {
                if ( FileUtil::writeTextFile( filePath, text ) == false )
                    SW_LOG_ERROR( "Could not write '%#' back", filePath.c_str() );
                if ( AssetManager* pAssets = editor::findSubsystem<AssetManager>() )
                    pAssets->getAssetCache<PrefabCache>().reload( savedPath, nullptr );
            };
            EditorTransaction::push( SW_DELEGATE_LAMBDA( Delegate<void()>, [writePrefabFile, oldFileText]()
            { writePrefabFile( oldFileText ); } ),
                                     SW_DELEGATE_LAMBDA( Delegate<void()>, [writePrefabFile, newFileText]()
            { writePrefabFile( newFileText ); } ),
                                     "Write Prefab" );
        }
        uint32 updatedCount = 0;
        for ( GameObject* pInstance : listInstance )
        {
            const ObjectSnapshot before = EditorTransaction::captureSnapshot( pInstance );
            if ( prefabCache.updateInstance( pInstance, oldBaseState, prefabPath ) == false )
            {
                SW_LOG_WARNING( "Prefab instance '%#' could not take the new prefab state", pInstance->getName().c_str() );
                continue;
            }
            EditorTransaction::recordModify( pInstance, before, EditorTransaction::captureSnapshot( pInstance ), "Update Prefab Instance" );
            ++updatedCount;
        }
        EditorTransaction::endTransaction();
        if ( updatedCount > 0 )
            SW_LOG_INFO( "Updated %# other instance(s) of %#", updatedCount, string{ prefabPath }.c_str() );
        return true;
    }

    bool EditorInspectorCommands::revertToPrefab( GameObject* pObj, string_view prefabPath )
    {
        if ( EditorPlaySession::areSceneEditsAllowed() == false )
            return false;
        if ( pObj == nullptr || prefabPath.empty() )
            return false;

        AssetManager* pResources = editor::findSubsystem<AssetManager>();
        if ( pResources == nullptr )
            return false;

        // 되돌리기는 엔진이 한다 — 형식(XML · JSON)과 인스턴스의 자리(부모 · 이름 · 루트 위치 · 회전)를 프리팹 쪽이 안다.
        const ObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pObj );
        if ( pResources->getAssetCache<PrefabCache>().revertInstance( pObj, prefabPath ) == false )
            return false;
        const ObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pObj );
        EditorTransaction::recordModify( pObj, beforeSnapshot, afterSnapshot, "Revert to Prefab" );
        return true;
    }

    void EditorInspectorCommands::unlinkPrefab( GameObject* pObj )
    {
        if ( EditorPlaySession::areSceneEditsAllowed() == false )
            return;
        if ( pObj == nullptr )
            return;

        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        EditorWorkspace& workspace = pContext->getSubsystem<EditorWorkspace>();
        if ( workspace.isNestedPrefabPart( pObj ) )
        {
            // 부품은 바깥 프리팹 인스턴스가 다시 스폰한다 — 끊으면 다음 로드에 같은 부품이 하나 더 생긴다(유니티도 중첩 자식만 따로 풀지 못한다).
            SW_LOG_WARNING( "'%#' is a nested prefab part - unlink its outer prefab instance instead", pObj->getName().c_str() );
            return;
        }
        workspace.setPlacedPrefabPath( pObj->getObjectID(), "" );
        workspace.markSceneDirty();
    }

    uint32 EditorInspectorCommands::adoptChildPrefabInstances( GameObject* pRoot )
    {
        EditorContext*     pContext = EditorContext::get();
        GameObjectManager* pManager = pRoot != nullptr ? pRoot->getManager() : nullptr;
        if ( pContext == nullptr || pManager == nullptr )
            return 0;
        EditorWorkspace&        workspace = pContext->getSubsystem<EditorWorkspace>();
        PrefabInstanceRegistry& registry  = pManager->getSubsystem<PrefabInstanceRegistry>();

        // 부모를 먼저 기록한다 — 프리팹 쓰기가 기록 순서(스폰 순서)로 돌며 부모 노드가 자식 노드보다 앞에 있기를 바란다.
        uint32              adoptedCount = 0;
        vector<GameObject*> listPending{ pRoot };
        vector<GameObject*> listChild;
        while ( listPending.empty() == false )
        {
            GameObject* pParent = listPending.back();
            listPending.pop_back();
            pParent->getChildren( listChild );
            for ( GameObject* pChild : listChild )
            {
                if ( pChild == nullptr || pChild->isPendingDestroy() )
                    continue;
                const string childPrefab{ workspace.getPlacedPrefabPath( pChild->getObjectID() ) };
                if ( childPrefab.empty() )
                    continue; // 프리팹 인스턴스가 아니다 — 프리팹 본문에 들지 않는다
                registry.registerNestedPart( pChild->getObjectID(), pRoot->getObjectID(), childPrefab );
                workspace.setPlacedPrefabPath( pChild->getObjectID(), "" );
                listPending.push_back( pChild );
                ++adoptedCount;
            }
        }
        if ( adoptedCount > 0 )
            SW_LOG_INFO( "'%#' took %# child prefab instance(s) as nested prefabs", pRoot->getName().c_str(), adoptedCount );
        return adoptedCount;
    }
} // namespace sw::editor
