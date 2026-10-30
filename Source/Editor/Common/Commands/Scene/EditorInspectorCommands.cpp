#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorInspectorCommands.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Object/Prefab/PrefabOverrides.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneObjectReference.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorInspectorCommands" );

    namespace
    {
        struct EditorInspectorCommandsInternal
        {
            /** @brief 프리팹 파일 글을 쓰고 캐시를 다시 읽습니다(되돌리기 데이터 단계 — "Write Prefab"). */
            static void writePrefabFile( string prefabPath, string filePath, string text )
            {
                if ( FileUtil::writeTextFile( filePath, text ) == false )
                    SW_LOG_ERROR( "Could not write '%#' back", filePath.c_str() );
                // 이 단계는 에셋 관리자가 있을 때만 기록된다(`applyToPrefab`) - 여기서는 필수다.
                editor::getSubsystem<AssetManager>().getAssetCache<PrefabCache>().reload( AssetID{ prefabPath }, nullptr );
            }

            /**
             * @brief 인스턴스를 프리팹 상태로 되돌립니다. 인스턴스의 위치 정보(부모, 이름, 루트의 위치와 회전)는 유지합니다(유니티 `PrefabUtility.RevertPrefabInstance` 규칙 —
             *        루트의 위치와 회전은 인스턴스의 것이고 스케일은 되돌린다).
             */
            [[nodiscard]] static bool revertInstance( PrefabCache& prefabCache, GameObject* pInstance, string_view assetRelativePath )
            {
                if ( pInstance == nullptr )
                    return false;
                const shared_ptr<const PrefabAsset> pAsset = prefabCache.loadPrefab( assetRelativePath );
                if ( pAsset == nullptr || pAsset->isValid() == false )
                    return false;

                // 인스턴스의 자리를 적어 둔다. 상태를 읽으면 컴포넌트가 모두 새로 만들어지고 이름 · 부착 · 트랜스폼이 프리팹의 것이 된다.
                // 부모는 오브젝트가 아니라 **붙어 있던 컴포넌트**(소켓일 수 있다)를 핸들로 적는다 — 부모 오브젝트의 primary 에 다시 붙이면
                // 소켓에 달린 무기가 튄다. 컴포넌트 id 도 복원한다 — 이 인스턴스의 컴포넌트를 가리키던 핸들(활성 카메라 · 게임 코드)이 이어지게.
                const hashed_string   name         = pInstance->getName();
                const SceneComponent* pOldRoot     = pInstance->getPrimarySceneComponent();
                const bool            bHadRoot     = pOldRoot != nullptr;
                const ComponentHandle parentHandle = ( bHadRoot && pOldRoot->getParent() != nullptr ) ? pOldRoot->getParent()->getHandle() : ComponentHandle{};
                const float3          position     = bHadRoot ? pOldRoot->getLocalPosition() : float3{};
                const float3          rotation     = bHadRoot ? pOldRoot->getLocalRotation() : float3{};
                const ObjectIdentity  identity     = ObjectStateSerializer::captureIdentity( pInstance );

                if ( pAsset->applyStateTo( pInstance, &identity ) == false )
                    return false;

                pInstance->setName( name );
                SceneComponent* pNewRoot = pInstance->getPrimarySceneComponent();
                if ( pNewRoot != nullptr )
                {
                    GameObjectManager* pManager        = pInstance->getManager();
                    Component*         pParent         = ( pManager != nullptr && parentHandle.isValid() ) ? pManager->resolveComponent( parentHandle ) : nullptr;
                    const bool         bReattachNeeded = pParent != nullptr && pParent->isSceneComponent() && pNewRoot->getParent() != pParent &&
                                                 pNewRoot->attachToComponent( static_cast<SceneComponent*>( pParent ) ) == false;
                    if ( bReattachNeeded )
                        SW_LOG_WARNING( "Revert of '%#' could not re-attach it to its parent - it stays at the root", name.c_str() );
                    if ( bHadRoot )
                    {
                        pNewRoot->setLocalPosition( position );
                        pNewRoot->setLocalRotation( rotation );
                    }
                }
                return true;
            }

            /**
             * @brief 프리팹을 고친 뒤 놓인 인스턴스 하나를 새 원형에 맞춥니다. 인스턴스가 옛 원형 위에 덮어쓴 것(`PrefabOverrides`)은 남깁니다
             *        (유니티 Apply 뒤 인스턴스 갱신 · 언리얼 블루프린트 컴파일 뒤 인스턴스 갱신). 캐시는 이미 새 내용이어야 한다(`reload` 뒤).
             */
            [[nodiscard]] static bool updateInstance( PrefabCache& prefabCache, GameObject* pInstance, string_view oldBaseStateXML, string_view assetRelativePath )
            {
                if ( pInstance == nullptr || oldBaseStateXML.empty() )
                    return false;
                const shared_ptr<const PrefabAsset> pAsset = prefabCache.loadPrefab( assetRelativePath );
                string                              newBaseState;
                const bool                          bNoNewBase = pAsset == nullptr || pAsset->isValid() == false || PrefabOverrides::makeBaseState( *pAsset, newBaseState ) == false;
                if ( bNoNewBase )
                    return false;
                // 덮어쓴 것은 옛 원형과 비교해 뽑는다. 새 원형과 비교하면 프리팹에서 바뀐 칸이 모두 "인스턴스가 덮어쓴 값" 으로 보여 퍼지지 않는다.
                const string instanceState = ObjectStateSerializer::saveToXMLString( pInstance, ObjectSaveOptions{ SerializePurpose::Authoring } );
                string       overrideXML;
                string       newInstanceState;
                string       overridesError;
                if ( PrefabOverrides::computeOverrides( instanceState, oldBaseStateXML, overrideXML, overridesError ) == false )
                {
                    SW_LOG_WARNING( "Prefab instance '%#' could not be refreshed: %#", pInstance->getName().c_str(), overridesError.c_str() );
                    return false;
                }
                if ( PrefabOverrides::makeInstanceState( newBaseState, overrideXML, pInstance->getName().c_str(), newInstanceState ) == false )
                    return false;
                const ObjectIdentity identity = ObjectStateSerializer::captureIdentity( pInstance );
                ObjectLoadContext    context{ SerializePurpose::Authoring };
                context._pIdentity = &identity;
                return ObjectStateSerializer::loadFromXMLString( pInstance, newInstanceState, context );
            }
        };
    } // namespace

    bool EditorInspectorCommands::applyToPrefab( GameObject* pObj, const AssetID& assetID )
    {
        const string_view prefabPath = assetID.view();
        if ( EditorPlaySession::refuseSceneEdit( "Apply to prefab" ) )
            return false;
        if ( pObj == nullptr || prefabPath.empty() )
            return false;

        // 도구나 테스트처럼 에셋 관리자와 에디터 컨텍스트가 없으면 파일만 쓴다. 캐시도 다른 인스턴스도 없기 때문이다.
        AssetManager*  pResources   = editor::findSubsystem<AssetManager>();
        EditorContext* pContext     = editor::findSubsystem<EditorContext>();
        PrefabCache*   pPrefabCache = pResources != nullptr ? &pResources->getAssetCache<PrefabCache>() : nullptr;

        // 고치기 전의 원형과 파일 내용이다. 다른 인스턴스의 덮어쓴 값을 옛 원형과 비교해 뽑고, 되돌리기가 이것으로 파일을 되돌린다.
        const std::optional<AbsolutePath> filePath = editor::getSubsystem<AssetPaths>().toDiskPath( prefabPath );
        if ( filePath.has_value() == false )
            return false; // 이유는 toDiskPath 가 남겼다
        string                              oldFileText;
        string                              oldBaseState;
        const bool                          bHadFile   = FileUtil::readOptionalTextFile( filePath->str(), oldFileText ) == OptionalFileRead::Read;
        const shared_ptr<const PrefabAsset> pOldPrefab = ( bHadFile && pPrefabCache != nullptr ) ? pPrefabCache->loadPrefab( prefabPath ) : nullptr;
        if ( pOldPrefab == nullptr || PrefabOverrides::makeBaseState( *pOldPrefab, oldBaseState ) == false )
            oldBaseState.clear();

        // 형식은 경로가 정한다(`.prefab.json` 에는 JSON). 프리팹 경로가 아니면 쓰지 않는다.
        PrefabAsset asset;
        asset.setFromGameObject( pObj );
        if ( asset.saveToFile( *filePath ) == false )
        {
            SW_LOG_ERROR( "Apply to prefab refused: could not write '%#'", string{ prefabPath }.c_str() );
            return false;
        }
        string newFileText;
        (void)FileUtil::readTextFile( filePath->str(), newFileText ); // 방금 쓴 파일이다. 못 읽어도 다시 실행이 파일을 쓰지 않을 뿐이다

        SW_LOG_INFO( "Saved prefab changes to %#", string{ prefabPath }.c_str() );
        if ( pPrefabCache == nullptr || pContext == nullptr )
            return true;
        // 캐시된 옛 내용으로 스폰하지 않게 다음 로드가 파일을 다시 읽도록 한다.
        PrefabCache& prefabCache = *pPrefabCache;
        prefabCache.reload( assetID, nullptr );

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
            EditorTransaction::push( SW_UNDO_STEP( EditorInspectorCommandsInternal::writePrefabFile, savedPath, filePath->str(), string{ oldFileText } ),
                                     SW_UNDO_STEP( EditorInspectorCommandsInternal::writePrefabFile, savedPath, filePath->str(), string{ newFileText } ),
                                     "Write Prefab" );
        }
        uint32 updatedCount = 0;
        for ( GameObject* pInstance : listInstance )
        {
            const ObjectSnapshot before = EditorTransaction::captureSnapshot( pInstance );
            if ( EditorInspectorCommandsInternal::updateInstance( prefabCache, pInstance, oldBaseState, prefabPath ) == false )
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
        if ( EditorPlaySession::refuseSceneEdit( "Revert to prefab" ) )
            return false;
        if ( pObj == nullptr || prefabPath.empty() )
            return false;

        AssetManager* pResources = editor::findSubsystem<AssetManager>();
        if ( pResources == nullptr )
        {
            SW_LOG_ERROR( "Revert to prefab refused: no AssetManager (prefab cache) for '%#'", string{ prefabPath }.c_str() );
            return false;
        }

        const ObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pObj );
        if ( EditorInspectorCommandsInternal::revertInstance( pResources->getAssetCache<PrefabCache>(), pObj, prefabPath ) == false )
            return false;
        const ObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pObj );
        EditorTransaction::recordModify( pObj, beforeSnapshot, afterSnapshot, "Revert to Prefab" );
        return true;
    }

    bool EditorInspectorCommands::unlinkPrefab( GameObject* pObj )
    {
        if ( EditorPlaySession::refuseSceneEdit( "Unlink prefab" ) )
            return false;
        if ( pObj == nullptr )
            return false;

        EditorWorkspace& workspace = editor::getSubsystem<EditorWorkspace>(); // 필수 - 에디터 명령이다
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
            return false;
        }
        if ( pTarget != pObj )
            SW_LOG_INFO( "'%#' is a nested prefab part - its outermost instance '%#' is unpacked one level", pObj->getName().c_str(), pTarget->getName().c_str() );
        workspace.setPlacedPrefabPath( *pTarget, "" );
        workspace.markSceneDirty();
        return true;
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
        GameObject* pOuter = findOuterInstance( pPart );
        if ( pOuter == nullptr )
        {
            SW_LOG_WARNING( "Apply to outer prefab refused: '%#' has no attached outer instance", pPart != nullptr ? pPart->getName().c_str() : "(null)" );
            return false;
        }
        const string outerPath{ editor::getSubsystem<EditorWorkspace>().findPrefabPath( pOuter ) };
        if ( outerPath.empty() )
        {
            SW_LOG_WARNING( "The outer instance '%#' of '%#' has no prefab - nothing to apply to", pOuter->getName().c_str(), pPart->getName().c_str() );
            return false;
        }
        return applyToPrefab( pOuter, AssetID{ outerPath } );
    }

    EditorInspectorCommands::AdoptResult EditorInspectorCommands::adoptChildPrefabInstances( GameObject* pRoot )
    {
        // 참조 검사에 필요한 것을 얻지 못하면 검사를 건너뛰고 옮기지 않는다 — 거절 사유와 오류 로그로 멈춘다.
        GameObjectManager* pManager = pRoot != nullptr ? pRoot->getManager() : nullptr;
        const Scene*       pScene   = pManager != nullptr ? editor::findSceneOf( pManager ) : nullptr;
        if ( pManager == nullptr || pScene == nullptr )
        {
            AdoptResult rejected;
            rejected._rejectReason = "cannot check scene object references: the object or its scene is missing";
            SW_LOG_WARNING( "Prefab creation refused: %#", rejected._rejectReason.c_str() );
            return rejected;
        }
        EditorWorkspace&        workspace    = editor::getSubsystem<EditorWorkspace>(); // 필수 서비스 - 없으면 버그라 멈춘다(대상 확인 뒤에 조회: 대상 없는 호출은 위에서 거절)
        SceneManager&           sceneManager = editor::getSubsystem<SceneManager>();
        PrefabInstanceRegistry& registry     = pManager->getSubsystem<PrefabInstanceRegistry>();

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

        AdoptResult result;
        for ( const GameObject* pChild : listAdopt )
        {
            string holder;
            if ( SceneObjectReferenceUtil::findReferenceTo( sceneManager, editor::getSubsystem<TypeRegistry>(), *pScene, pChild->getObjectID(), holder ) == false )
                continue;
            result._rejectReason = string{ "'" } + pChild->getName().c_str() + "' is referenced by " + holder + " - a scene object reference cannot follow an object the prefab respawns; clear the reference first";
            SW_LOG_WARNING( "Prefab creation from '%#' refused: %#", pRoot->getName().c_str(), result._rejectReason.c_str() );
            return result;
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
