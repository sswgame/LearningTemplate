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
#include "Engine/Scene/SceneObjectReference.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorInspectorCommands" );

    bool EditorInspectorCommands::applyToPrefab( GameObject* pObj, string_view prefabPath )
    {
        if ( EditorPlaySession::areSceneEditsAllowed() == false )
            return false;
        if ( pObj == nullptr || prefabPath.empty() )
            return false;

        // 도구나 테스트처럼 에셋 관리자와 에디터 컨텍스트가 없으면 파일만 쓴다. 캐시도 다른 인스턴스도 없기 때문이다.
        AssetManager*  pResources   = editor::findSubsystem<AssetManager>();
        EditorContext* pContext     = EditorContext::get();
        PrefabCache*   pPrefabCache = pResources != nullptr ? &pResources->getAssetCache<PrefabCache>() : nullptr;

        // 고치기 전의 원형과 파일 내용이다. 다른 인스턴스의 덮어쓴 값을 옛 원형과 비교해 뽑고, 되돌리기가 이것으로 파일을 되돌린다.
        const string                        filePath = ResourcePaths::getWritePath( prefabPath );
        string                              oldFileText;
        string                              oldBaseState;
        const bool                          bHadFile   = FileUtil::exists( filePath ) && FileUtil::readTextFile( filePath, oldFileText );
        const shared_ptr<const PrefabAsset> pOldPrefab = ( bHadFile && pPrefabCache != nullptr ) ? pPrefabCache->loadPrefab( prefabPath ) : nullptr;
        if ( pOldPrefab == nullptr || PrefabOverrides::makeBaseState( *pOldPrefab, oldBaseState ) == false )
            oldBaseState.clear();

        // 형식은 경로가 정한다(`.prefab.json` 에는 JSON). 프리팹 경로가 아니면 쓰지 않는다.
        PrefabAsset asset;
        asset.setFromGameObject( pObj );
        if ( asset.saveToFile( prefabPath ) == false )
            return false;
        string newFileText;
        (void)FileUtil::readTextFile( filePath, newFileText ); // 방금 쓴 파일이다. 못 읽어도 다시 실행이 파일을 쓰지 않을 뿐이다

        SW_LOG_INFO( "Saved prefab changes to %#", string{ prefabPath }.c_str() );
        if ( pPrefabCache == nullptr || pContext == nullptr )
            return true;
        // 캐시된 옛 내용으로 스폰하지 않게 다음 로드가 파일을 다시 읽도록 한다.
        PrefabCache& prefabCache = *pPrefabCache;
        prefabCache.reload( prefabPath, nullptr );

        // 같은 프리팹의 다른 인스턴스에 퍼뜨린다(덮어쓴 값은 남긴다). 파일 쓰기와 인스턴스 갱신을 되돌리기 하나로 묶는다.
        // 인스턴스는 두 종류다. 씬에 놓은 인스턴스(에디터 작업 공간이 경로를 기록)와 다른 프리팹 인스턴스가 스폰한 중첩 부품이다.
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

        // 되돌리기는 엔진이 한다. 형식(XML, JSON)과 인스턴스의 배치(부모, 이름, 루트 위치, 회전)를 프리팹 쪽이 알고 있다.
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
        // 부품은 바깥 프리팹 인스턴스가 다시 스폰한다. 부품만 연결을 끊으면 다음 로드에 같은 부품이 하나 더 생긴다. 유니티도 중첩 자식만 따로 풀지 못한다.
        // 그래서 맨 바깥에 놓인 인스턴스를 한 단계 푼다(유니티 Unpack Prefab). 그 바로 아래 부품은 씬이 자기 프리팹의 인스턴스로 기록한다.
        GameObject* pTarget = pObj;
        while ( workspace.isNestedPrefabPart( pTarget ) )
        {
            pTarget = findOuterInstance( pTarget );
        }
        if ( pTarget == nullptr || workspace.getPlacedPrefabPath( *pTarget ).empty() )
        {
            SW_LOG_WARNING( "'%#' has no placed prefab instance to unlink", pObj->getName().c_str() );
            return;
        }
        if ( pTarget != pObj )
            SW_LOG_INFO( "'%#' is a nested prefab part - its outermost instance '%#' is unpacked one level", pObj->getName().c_str(), pTarget->getName().c_str() );
        workspace.setPlacedPrefabPath( *pTarget, "" );
        workspace.markSceneDirty();
    }

    GameObject* EditorInspectorCommands::findOuterInstance( const GameObject* pPart )
    {
        GameObjectManager* pManager = pPart != nullptr ? pPart->getManager() : nullptr;
        if ( pManager == nullptr )
            return nullptr;
        const PrefabInstanceRegistry& registry = pManager->getSubsystem<PrefabInstanceRegistry>();
        if ( registry.isAttachedNestedPart( pPart ) == false )
            return nullptr;
        return pManager->findGameObjectByID( registry.findNestedPart( pPart->getObjectID() )->_ownerObjectID ); // 붙은 부품이면 기록이 있다
    }

    bool EditorInspectorCommands::applyToOuterPrefab( GameObject* pPart )
    {
        EditorContext* pContext = EditorContext::get();
        GameObject*    pOuter   = findOuterInstance( pPart );
        if ( pContext == nullptr || pOuter == nullptr )
            return false;
        const string outerPath{ pContext->getSubsystem<EditorWorkspace>().findPrefabPath( pOuter ) };
        if ( outerPath.empty() )
        {
            SW_LOG_WARNING( "The outer instance '%#' of '%#' has no prefab - nothing to apply to", pOuter->getName().c_str(), pPart->getName().c_str() );
            return false;
        }
        return applyToPrefab( pOuter, outerPath );
    }

    EditorInspectorCommands::AdoptResult EditorInspectorCommands::adoptChildPrefabInstances( GameObject* pRoot )
    {
        GameObjectManager* pManager   = pRoot != nullptr ? pRoot->getManager() : nullptr;
        EditorWorkspace*   pWorkspace = editor::findSubsystem<EditorWorkspace>();
        if ( pWorkspace == nullptr || pManager == nullptr )
            return {};
        EditorWorkspace&        workspace = *pWorkspace;
        PrefabInstanceRegistry& registry  = pManager->getSubsystem<PrefabInstanceRegistry>();

        // 부모를 먼저 기록한다. 프리팹 쓰기는 기록 순서(스폰 순서)로 돌며 부모 노드가 자식 노드보다 앞에 있다고 가정한다.
        // 먼저 옮길 오브젝트를 모두 모으고(아직 아무것도 바꾸지 않는다), 씬 파일을 넘는 참조가 가리키는 것이 있으면 거절한다.
        vector<GameObject*> listAdopt;
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
                if ( workspace.getPlacedPrefabPath( *pChild ).empty() )
                    continue; // 프리팹 인스턴스가 아니므로 프리팹 본문에 들어가지 않는다
                listAdopt.push_back( pChild );
                listPending.push_back( pChild );
            }
        }

        AdoptResult   result;
        const Scene*  pScene        = editor::findSceneOf( pManager );
        SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
        if ( pScene != nullptr && pSceneManager != nullptr )
        {
            for ( const GameObject* pChild : listAdopt )
            {
                string holder;
                if ( SceneObjectReferenceUtil::findReferenceTo( *pSceneManager, *pScene, pChild->getObjectID(), holder ) == false )
                    continue;
                result._rejectReason = string{ "'" } + pChild->getName().c_str() + "' is referenced by " + holder + " - a scene object reference cannot follow an object the prefab respawns; clear the reference first";
                SW_LOG_WARNING( "Prefab creation from '%#' refused: %#", pRoot->getName().c_str(), result._rejectReason.c_str() );
                return result;
            }
        }

        for ( GameObject* pChild : listAdopt )
        {
            const string childPrefab{ workspace.getPlacedPrefabPath( *pChild ) };
            registry.registerNestedPart( pChild->getObjectID(), pRoot->getObjectID(), childPrefab, {} ); // 노드는 프리팹을 쓸 때 생기므로 키는 다음 로드에서 얻는다
            workspace.setPlacedPrefabPath( *pChild, "" );
        }
        result._adoptedCount = static_cast<uint32>( listAdopt.size() );
        if ( result._adoptedCount > 0 )
            SW_LOG_INFO( "'%#' took %# child prefab instance(s) as nested prefabs", pRoot->getName().c_str(), result._adoptedCount );
        return result;
    }
} // namespace sw::editor
