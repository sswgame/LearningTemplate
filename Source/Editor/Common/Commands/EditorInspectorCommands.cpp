#include "pch.h"

#include "Editor/Common/Commands/EditorInspectorCommands.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorInspectorCommands" );

    bool EditorInspectorCommands::applyToPrefab( GameObject* pObj, string_view prefabPath )
    {
        if ( EditorUtil::areSceneEditsAllowed() == false )
            return false;
        if ( pObj == nullptr || prefabPath.empty() )
            return false;

        // 형식은 경로가 정한다(`.prefab.json` 에는 JSON) — 프리팹 경로가 아니면 쓰지 않는다.
        PrefabAsset asset;
        asset.setFromGameObject( pObj );
        if ( asset.saveToFile( prefabPath ) == false )
            return false;

        // 캐시된 옛 내용으로 스폰하지 않게 다음 로드가 파일을 다시 읽도록 한다.
        AssetManager* pResources = editor::getService<AssetManager>();
        if ( pResources != nullptr )
            pResources->getPrefabCache().reload( prefabPath, nullptr );
        SW_LOG_INFO( "Saved prefab changes to %#", string{ prefabPath }.c_str() );
        return true;
    }

    bool EditorInspectorCommands::revertToPrefab( GameObject* pObj, string_view prefabPath )
    {
        if ( EditorUtil::areSceneEditsAllowed() == false )
            return false;
        if ( pObj == nullptr || prefabPath.empty() )
            return false;

        AssetManager* pResources = editor::getService<AssetManager>();
        if ( pResources == nullptr )
            return false;

        // 되돌리기는 엔진이 한다 — 형식(XML · JSON)과 인스턴스의 자리(부모 · 이름 · 루트 위치 · 회전)를 프리팹 쪽이 안다.
        const ObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pObj );
        if ( pResources->getPrefabCache().revertInstance( pObj, prefabPath ) == false )
            return false;
        const ObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pObj );
        EditorTransaction::recordModify( pObj, beforeSnapshot, afterSnapshot, "Revert to Prefab" );
        return true;
    }

    void EditorInspectorCommands::unlinkPrefab( GameObject* pObj )
    {
        if ( EditorUtil::areSceneEditsAllowed() == false )
            return;
        if ( pObj == nullptr )
            return;

        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        pContext->getWorkspace().setGameObjectPrefabPath( pObj->getObjectId(), "" );
        pContext->getWorkspace().markSceneDirty();
    }
} // namespace sw::editor
