#include "pch.h"

#include "Editor/Common/Commands/EditorSceneCommands.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/TagID.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Physics/Collision/AABB.h"
#include "Engine/Physics/Collision/ContinuousCollision.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw::editor
{
    namespace
    {
        struct EditorSceneCommandsInternal
        {
            static bool canMutateScene()
            {
                return EditorUtil::areSceneEditsAllowed();
            }

            /** @brief 서브트리를 **자식부터**(후위 순서) 모읍니다. 마지막이 pObj 입니다. */
            static void collectSubtreeChildFirst( GameObject* pObj, vector<GameObject*>& outListObject )
            {
                vector<GameObject*> listChild;
                pObj->getChildren( listChild );
                for ( GameObject* pChild : listChild )
                {
                    if ( pChild != nullptr )
                        collectSubtreeChildFirst( pChild, outListObject );
                }
                outListObject.push_back( pObj );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    GameObject* EditorSceneCommands::create( GameObjectManager* pManager, GameObject* pParent )
    {
        if ( EditorSceneCommandsInternal::canMutateScene() == false )
            return nullptr;
        if ( pManager == nullptr )
            return nullptr;

        GameObject* pCreated = pManager->createGameObject( hashed_string( GameObject::kDefaultName ) );
        if ( pCreated == nullptr )
            return nullptr;

        pCreated->addComponent<SceneComponent>();
        if ( pParent != nullptr && pCreated->attachToParent( pParent ) == false )
            SW_LOG_WARNING( "New object could not be put under '%#' - it is created at the root", pParent->getName().c_str() );

        EditorTransaction::recordCreation( pCreated, "Create GameObject" );
        select( pCreated, SelectionMode::Replace );
        return pCreated;
    }

    GameObject* EditorSceneCommands::duplicate( GameObjectManager* pManager, GameObject* pSrc )
    {
        if ( EditorSceneCommandsInternal::canMutateScene() == false )
            return nullptr;
        if ( pManager == nullptr || pSrc == nullptr )
            return nullptr;

        // **서브트리 전체를 복제한다**(유니티 Ctrl+D · 언리얼 Duplicate). 씬은 자식을 자기 엔티티로 저장하므로, 선택한 것만 복제하면
        // 자식은 원본 밑에 남는다. 부모부터 모은다 — 되돌리기 묶음의 다시 하기가 부모를 먼저 되살린다.
        vector<GameObject*> listSource;
        EditorSceneCommandsInternal::collectSubtreeChildFirst( pSrc, listSource );
        std::reverse( listSource.begin(), listSource.end() );

        vector<vector<uint8>> listState( listSource.size() );
        for ( size_t sourceIndex = 0; sourceIndex < listSource.size(); ++sourceIndex )
        {
            if ( ObjectStateSerializer::saveToBinaryBuffer( listSource[sourceIndex], listState[sourceIndex] ) == false )
                return nullptr;
        }

        // 복제본끼리의 부착(자식 → 복제된 부모 · 오브젝트 안)은 묶음이 **원본 id** 로 잇는다. 복제한 루트의 부모(묶음 밖)는 같은 실행의 id 로
        // 원본과 같은 부모 · 같은 소켓에 붙는다. 주의: 이름으로 찾으면 이름이 유일하게 바뀐 복제본(`Rig` → `Rig_2`)의 메시가 **원본**의
        // 루트에 붙고, 루트는 원본 부모의 primary 에 다시 붙어 소켓을 잃는다.
        ObjectStateBatch    batch( ObjectIdSpace::Live );
        vector<GameObject*> listCopy;
        listCopy.reserve( listSource.size() );
        bool bAllLoaded = true;
        for ( size_t sourceIndex = 0; sourceIndex < listSource.size(); ++sourceIndex )
        {
            GameObject* pCopy = pManager->createGameObject( listSource[sourceIndex]->getName() );
            if ( pCopy == nullptr )
            {
                bAllLoaded = false;
                break;
            }
            listCopy.push_back( pCopy );
            ObjectLoadContext context{};
            context._pBatch            = &batch;
            context._savedId           = listSource[sourceIndex]->getObjectId();
            const vector<uint8>& state = listState[sourceIndex];
            if ( ObjectStateSerializer::loadFromBinaryBuffer( pCopy, state.data(), state.size(), context ) == 0 )
            {
                bAllLoaded = false;
                break;
            }
        }
        batch.finish();
        if ( bAllLoaded == false )
        {
            SW_LOG_WARNING( "Duplicate of '%#' failed to read a copied state - nothing was duplicated", pSrc->getName().c_str() );
            for ( GameObject* pCopy : listCopy )
            {
                pManager->destroyObject( pCopy );
            }
            return nullptr;
        }

        GameObject*                           pNewObj = listCopy.front();
        fixed_string<constant::kMaxBuffer256> newName;
        formatstring( newName.data(), newName.capacity(), "%#_Copy", pSrc->getName().c_str() );
        pNewObj->setName( hashed_string( newName.c_str() ) );

        EditorContext* pContext = EditorContext::get();
        EditorTransaction::beginTransaction( "Duplicate GameObject" );
        for ( size_t copyIndex = 0; copyIndex < listCopy.size(); ++copyIndex )
        {
            const string prefabPath =
                ( pContext != nullptr ) ? pContext->getWorkspace().getGameObjectPrefabPath( listSource[copyIndex]->getObjectId() ) : string{};
            if ( prefabPath.empty() == false && pContext != nullptr )
                pContext->getWorkspace().setGameObjectPrefabPath( listCopy[copyIndex]->getObjectId(), prefabPath );
            EditorTransaction::recordCreation( listCopy[copyIndex], "Duplicate GameObject" );
        }
        EditorTransaction::endTransaction();
        select( pNewObj, SelectionMode::Replace );
        return pNewObj;
    }

    bool EditorSceneCommands::reparent( GameObject* pChild, GameObject* pNewParent, string_view undoLabel )
    {
        if ( EditorSceneCommandsInternal::canMutateScene() == false )
            return false;
        if ( pChild == nullptr || pNewParent == nullptr || pChild == pNewParent )
            return false;
        if ( wouldCreateParentCycle( pChild, pNewParent ) )
            return false;

        const ObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pChild );
        // 끌어 놓은 자리에 그대로 있게 월드를 지킨다(유니티 계층 창 · 언리얼 아웃라이너와 같다). 로컬을 지키면 새 부모만큼 튄다.
        if ( pChild->attachToParent( pNewParent, AttachRule::KeepWorld ) == false )
            return false;

        const ObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pChild );
        EditorTransaction::recordModify( pChild, beforeSnapshot, afterSnapshot, undoLabel );
        select( pChild, SelectionMode::Replace );
        return true;
    }

    bool EditorSceneCommands::unparent( GameObject* pObj, string_view undoLabel )
    {
        if ( EditorSceneCommandsInternal::canMutateScene() == false )
            return false;
        if ( pObj == nullptr || pObj->getParent() == nullptr )
            return false;

        const ObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pObj );
        pObj->detachFromParent( AttachRule::KeepWorld );
        const ObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pObj );
        EditorTransaction::recordModify( pObj, beforeSnapshot, afterSnapshot, undoLabel );
        select( pObj, SelectionMode::Replace );
        return true;
    }

    uint32 EditorSceneCommands::destroyObjects( GameObjectManager* pManager, const vector<GameObject*>& listObject )
    {
        if ( EditorSceneCommandsInternal::canMutateScene() == false || pManager == nullptr )
            return 0;
        // 고른 조상이 있으면 건너뛴다 — 조상을 지우면 자식도 지워지고, 먼저 지운 자식을 다시 지우지 않는다.
        vector<GameObject*> listRoot;
        for ( GameObject* pObj : listObject )
        {
            if ( pObj == nullptr )
                continue;
            bool bAncestorPicked = false;
            for ( const GameObject* pParent = pObj->getParent(); pParent != nullptr && bAncestorPicked == false; pParent = pParent->getParent() )
            {
                for ( const GameObject* pOther : listObject )
                {
                    bAncestorPicked = bAncestorPicked || pOther == pParent;
                }
            }
            if ( bAncestorPicked == false )
                listRoot.push_back( pObj );
        }
        if ( listRoot.empty() )
            return 0;

        uint32 destroyedCount = 0;
        EditorTransaction::beginTransaction( listRoot.size() == 1 ? string( "Destroy GameObject" ) : "Destroy " + to_string( listRoot.size() ) + " GameObjects" );
        for ( GameObject* pObj : listRoot )
        {
            if ( destroy( pManager, pObj ) )
                ++destroyedCount;
        }
        EditorTransaction::endTransaction();
        return destroyedCount;
    }

    void EditorSceneCommands::collectRootsInOrder( GameObjectManager& manager, vector<GameObject*>& outListRoot )
    {
        outListRoot.clear();
        manager.forEachGameObject( [&outListRoot]( GameObject* pObj )
        {
            if ( pObj != nullptr && pObj->getParent() == nullptr && pObj->isPendingDestroy() == false )
                outListRoot.push_back( pObj );
        } );
        std::sort( outListRoot.begin(), outListRoot.end(),
                   []( const GameObject* pLeft, const GameObject* pRight )
        { return pLeft->getObjectId() < pRight->getObjectId(); } );
    }

    void EditorSceneCommands::duplicateObjects( GameObjectManager* pManager, const vector<GameObject*>& listObject, vector<GameObject*>& outListCreated )
    {
        outListCreated.clear();
        if ( EditorSceneCommandsInternal::canMutateScene() == false || pManager == nullptr || listObject.empty() )
            return;
        EditorTransaction::beginTransaction( listObject.size() == 1 ? string( "Duplicate GameObject" ) : "Duplicate " + to_string( listObject.size() ) + " GameObjects" );
        for ( GameObject* pSource : listObject )
        {
            GameObject* pCopy = duplicate( pManager, pSource );
            if ( pCopy != nullptr )
                outListCreated.push_back( pCopy );
        }
        EditorTransaction::endTransaction();
    }

    bool EditorSceneCommands::destroy( GameObjectManager* pManager, GameObject* pObj )
    {
        if ( EditorSceneCommandsInternal::canMutateScene() == false )
            return false;
        if ( pManager == nullptr || pObj == nullptr )
            return false;

        // 삭제는 자식까지 지운다(`destroyObject` 기본). 그러니 기록도 서브트리 전체다 — 이 오브젝트 하나의 스냅샷만 남기면
        // 되돌릴 때 부모만 돌아오고 자식은 영영 사라진다(그대로 저장하면 파일에서도). **자식부터** 기록해 한 묶음으로 넣는다: 묶음의
        // 되돌리기는 역순이라 부모가 먼저 (원래 id 로) 살아나고, 자식은 그 id 로 부모를 찾아 다시 붙는다.
        vector<GameObject*> listSubtree;
        EditorSceneCommandsInternal::collectSubtreeChildFirst( pObj, listSubtree );

        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr )
        {
            EditorSelection& sel = pContext->getEditorSelection();
            for ( GameObject* pDoomed : listSubtree )
            {
                if ( sel.hasObject( pDoomed ) )
                    sel.selectObject( pDoomed, SelectionMode::Remove );
            }
        }

        EditorTransaction::beginTransaction( "Destroy GameObject" );
        for ( GameObject* pDoomed : listSubtree )
        {
            EditorTransaction::recordDestruction( pDoomed, "Destroy GameObject" );
        }
        EditorTransaction::endTransaction();
        pManager->destroyObject( pObj );
        return true;
    }

    bool EditorSceneCommands::rename( GameObject* pObj, const utf8* pNewName )
    {
        if ( EditorSceneCommandsInternal::canMutateScene() == false )
            return false;
        if ( pObj == nullptr || StringUtil::isNullOrEmpty( pNewName ) )
            return false;

        const ObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pObj );
        pObj->setName( hashed_string( pNewName ) );
        const ObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pObj );
        EditorTransaction::recordModify( pObj, beforeSnapshot, afterSnapshot, "Rename GameObject" );
        return true;
    }

    void EditorSceneCommands::setActive( GameObject* pObj, bool bActive )
    {
        if ( pObj == nullptr || pObj->isActive() == bActive )
            return;
        if ( EditorSceneCommandsInternal::canMutateScene() == false )
        {
            pObj->setActive( bActive );
            return;
        }
        const ObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pObj );
        pObj->setActive( bActive );
        commitModify( pObj, beforeSnapshot, bActive ? "Activate GameObject" : "Deactivate GameObject" );
    }

    Component* EditorSceneCommands::addComponent( GameObject* pObj, const hashed_string& typeName )
    {
        if ( EditorSceneCommandsInternal::canMutateScene() == false )
            return nullptr;
        if ( pObj == nullptr || pObj->getManager() == nullptr || typeName.empty() )
            return nullptr;

        const ObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pObj );
        Component*           pComp          = pObj->getManager()->addComponentByName( pObj, typeName );
        if ( pComp == nullptr )
            return nullptr;
        pComp->onPostLoad();
        commitModify( pObj, beforeSnapshot, "Add Component" );
        return pComp;
    }

    bool EditorSceneCommands::destroyComponent( GameObjectManager* pManager, GameObject* pObj, Component* pComp )
    {
        if ( EditorSceneCommandsInternal::canMutateScene() == false )
            return false;
        if ( pManager == nullptr || pObj == nullptr || pComp == nullptr )
            return false;

        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr )
        {
            EditorWorkspace& ws = pContext->getWorkspace();
            if ( ws.getSelectedObjectId() == pObj->getObjectId() &&
                 ws.getSelectedComponentId() == pComp->getComponentId() )
            {
                ws.setSelectedComponentId( 0 );
                ws.setSelectedComponentKey( "" );
            }
        }

        // 기록을 남긴다 — 기록도 dirty 도 없으면 되돌릴 수 없고, 그대로 다른 씬을 열면 묻지도 않고 사라진다. 삭제 대기 컴포넌트는
        // 스냅샷에 실리지 않으므로(지우기 전 · 후 스냅샷이 다르다) 되돌리면 그 컴포넌트가 원래 id 로 돌아온다.
        const ObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pObj );
        pManager->destroyComponent( pComp );
        commitModify( pObj, beforeSnapshot, "Remove Component" );
        return true;
    }

    void EditorSceneCommands::select( GameObject* pObj, SelectionMode mode )
    {
        if ( pObj == nullptr )
            return;

        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        pContext->getWorkspace().selectGameObject( pObj, mode );
    }

    void EditorSceneCommands::collectObjectsWithComponent( GameObjectManager& manager, const TypeInfo* pComponentType, vector<GameObject*>& outListObject )
    {
        outListObject.clear();
        if ( pComponentType == nullptr )
            return;
        manager.forEachGameObject( [pComponentType, &outListObject]( GameObject* pObj )
        {
            if ( pObj == nullptr || pObj->isPendingDestroy() )
                return;
            for ( const Component* pComponent : pObj->getComponents() )
            {
                const TypeInfo* pType = pComponent != nullptr && pComponent->isPendingDestroy() == false ? pComponent->getTypeInfo() : nullptr;
                if ( pType != nullptr && pType->isDerivedFrom( pComponentType ) )
                {
                    outListObject.push_back( pObj );
                    return;
                }
            }
        } );
    }

    void EditorSceneCommands::collectObjectsWithTag( GameObjectManager& manager, TagID tag, vector<GameObject*>& outListObject )
    {
        outListObject.clear();
        if ( tag.isValid() == false )
            return;
        manager.forEachGameObject( [tag, &outListObject]( GameObject* pObj )
        {
            if ( pObj != nullptr && pObj->isPendingDestroy() == false && pObj->hasTag( tag ) )
                outListObject.push_back( pObj );
        } );
    }

    uint32 EditorSceneCommands::selectObjects( const vector<GameObject*>& listObject )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr || listObject.empty() )
            return 0;
        EditorWorkspace& workspace = pContext->getWorkspace();
        workspace.selectGameObject( listObject[0], SelectionMode::Replace );
        for ( size_t index = 1; index < listObject.size(); ++index )
        {
            workspace.selectGameObject( listObject[index], SelectionMode::Add );
        }
        return static_cast<uint32>( listObject.size() );
    }

    bool EditorSceneCommands::wouldCreateParentCycle( GameObject* pChild, GameObject* pNewParent )
    {
        if ( pChild == nullptr || pNewParent == nullptr )
            return true;
        return pNewParent->isDescendantOf( pChild );
    }

    void EditorSceneCommands::applyLocalTransform( GameObject* pObj, const float3& translation, const float3& rotationRad,
                                                   const float3& scale )
    {
        if ( EditorSceneCommandsInternal::canMutateScene() == false )
            return;
        if ( pObj == nullptr )
            return;

        SceneComponent* pSceneComp = pObj->getPrimarySceneComponent();
        if ( pSceneComp == nullptr )
            return;

        pSceneComp->setLocalPosition( translation );
        pSceneComp->setLocalRotation( rotationRad );
        pSceneComp->setLocalScale( scale );
    }

    void EditorSceneCommands::applyWorldTransform( GameObject* pObj, const float4x4& worldMatrix )
    {
        if ( EditorSceneCommandsInternal::canMutateScene() == false )
            return;
        if ( pObj == nullptr )
            return;

        SceneComponent* pSceneComp = pObj->getPrimarySceneComponent();
        if ( pSceneComp != nullptr )
            pSceneComp->setWorldTransform( worldMatrix );
    }

    void EditorSceneCommands::snapTranslationToSurface( GameObject* pObj, float3& translation )
    {
        if ( pObj == nullptr )
            return;

        // 크기는 월드 상자 하나로 잰다(`GameObject::getWorldBox`). 기준점(primary 의 월드 자리)에서 바닥까지 · 가로 반 크기를 지금 상자에서 얻는다.
        float32               bottomOffset = 0.0f;
        float32               halfExtentX  = 0.5f;
        float32               halfExtentZ  = 0.5f;
        const SceneComponent* pMyScene     = pObj->getPrimarySceneComponent();
        AABB                  myBox{};
        if ( pMyScene != nullptr && pObj->getWorldBox( myBox ) )
        {
            bottomOffset = pMyScene->getWorldPosition()._y - myBox._min._y;
            halfExtentX  = MathUtil::max( ( myBox._max._x - myBox._min._x ) * 0.5f, 0.01f );
            halfExtentZ  = MathUtil::max( ( myBox._max._z - myBox._min._z ) * 0.5f, 0.01f );
        }

        Scene* pScene = editor::getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
        {
            translation._y = bottomOffset;
            return;
        }

        GameObjectManager* pManager = pScene->getObjectManager();
        float32            hitY     = 0.0f;
        bool               bHit     = false;

        const float32 startY = translation._y + 10.0f;
        const AABB    movingBox{
            float3{translation._x - halfExtentX, startY - bottomOffset, translation._z - halfExtentZ},
            float3{translation._x + halfExtentX, startY + bottomOffset, translation._z + halfExtentZ}
        };
        const float3 displacement{ 0.0f, -2000.0f, 0.0f };

        SweepHit sweepHit{};
        if ( pManager->getOverlapWorld2D().getPhysicsWorld().sweepTest( movingBox, displacement, 0, sweepHit ) )
        {
            if ( sweepHit._hitObjectId != pObj->getObjectId() )
            {
                hitY = movingBox._min._y + displacement._y * sweepHit._time;
                bHit = true;
            }
        }

        // 반환값을 const& 로 받으면 복사가 없어 보이지만, 값으로 반환하는 함수라 그때마다 벡터를 하나 할당한다. 그래서
        // 채워 넣는 쪽 오버로드를 쓴다.
        vector<GameObject*> listAll;
        pManager->getAllGameObjects( listAll );
        for ( const GameObject* pOther : listAll )
        {
            if ( pOther == nullptr || pOther == pObj )
                continue;

            // 다른 오브젝트의 윗면 · 발자국도 월드 상자로 본다. 메시의 **로컬** 스케일 × 단위 상자로 셈하면 부모가 키운 바닥 · 단위 상자가
            // 아닌 메시(평면 · 구)의 윗면을 틀리게 잡는다.
            AABB otherBox{};
            if ( pOther->isActiveInHierarchy() && pOther->getWorldBox( otherBox ) )
            {
                const float32 topY = otherBox._max._y;
                if ( topY <= translation._y + 10.0f && ( topY > hitY || bHit == false ) && otherBox._min._x <= translation._x &&
                     translation._x <= otherBox._max._x && otherBox._min._z <= translation._z && translation._z <= otherBox._max._z )
                {
                    hitY = topY;
                    bHit = true;
                }
            }
        }

        translation._y = ( bHit ? hitY : 0.0f ) + bottomOffset;
    }

    void EditorSceneCommands::commitModify( GameObject* pObj, const ObjectSnapshot& before, string_view undoLabel )
    {
        if ( EditorSceneCommandsInternal::canMutateScene() == false )
            return;
        if ( pObj == nullptr || before._xml.empty() )
            return;

        const ObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pObj );
        EditorTransaction::recordModify( pObj, before, afterSnapshot, undoLabel );
    }

    EditorSceneCommands::SceneStatistics EditorSceneCommands::collectSceneStatistics( GameObjectManager* pManager )
    {
        SceneStatistics stats{};
        if ( pManager == nullptr )
            return stats;

        // 타입 이름 → 인스턴스 수. 이름으로 묶으므로 등록된 어떤 컴포넌트든(게임·키트 것 포함) 잡힌다.
        map<string, uint32> mapTypeToCount;

        pManager->forEachGameObject( [&]( GameObject* pObj )
        {
            if ( pObj == nullptr )
                return;

            ++stats._objectCount;
            if ( pObj->getParent() == nullptr )
                ++stats._rootCount;

            pObj->forEachComponent( [&]( Component* pComp )
            {
                if ( pComp == nullptr )
                    return;

                ++stats._componentCount;

                const TypeInfo* pTypeInfo = pComp->getTypeInfo();
                if ( pTypeInfo == nullptr )
                    return;

                // 인스턴스마다 센다. 한 오브젝트에 같은 타입이 여럿이면 그 수만큼 센다.
                ++mapTypeToCount[string{ pTypeInfo->_name.c_str() }];
            } );
        } );

        stats._listDistribution.reserve( mapTypeToCount.size() );
        for ( const auto& [typeName, count] : mapTypeToCount )
        {
            stats._listDistribution.push_back( ComponentDistributionRow{ typeName, count } );
        }

        // 많은 것부터 보여 주는 편이 읽기 쉽다. 수가 같으면 이름순으로 안정화한다.
        std::sort( stats._listDistribution.begin(), stats._listDistribution.end(),
                   []( const ComponentDistributionRow& lhs, const ComponentDistributionRow& rhs )
        {
            if ( lhs._instanceCount != rhs._instanceCount )
                return lhs._instanceCount > rhs._instanceCount;
            return lhs._typeName < rhs._typeName;
        } );

        return stats;
    }

} // namespace sw::editor
