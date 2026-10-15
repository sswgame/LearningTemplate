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
        EditorWorkspace&    workspace = pContext->getSubsystem<EditorWorkspace>();
        GameObjectManager*  pManager  = pObj->getManager();
        vector<GameObject*> listInstance;
        if ( pManager != nullptr && oldBaseState.empty() == false )
        {
            pManager->forEachGameObject( [&]( GameObject* pOther )
            {
                const bool bOtherInstance = pOther != nullptr && pOther != pObj && pOther->isPendingDestroy() == false &&
                                            FileUtil::pathsEqualNormalized( workspace.getGameObjectPrefabPath( pOther->getObjectID() ), prefabPath );
                if ( bOtherInstance )
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

        pContext->getSubsystem<EditorWorkspace>().setGameObjectPrefabPath( pObj->getObjectID(), "" );
        pContext->getSubsystem<EditorWorkspace>().markSceneDirty();
    }
} // namespace sw::editor
