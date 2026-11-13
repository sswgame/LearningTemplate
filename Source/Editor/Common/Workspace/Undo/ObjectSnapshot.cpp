#include "pch.h"

#include "Editor/Common/Workspace/Undo/ObjectSnapshot.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
    /** @brief 스냅샷을 되돌리기 단계 인자로 씁니다(상태 XML + 런타임 id 들). */
    static void writeUndoArg( Archive& archive, const ObjectSnapshot& snapshot )
    {
        archive.writeString( snapshot._xml );
        archive << snapshot._identity._objectID;
        archive << static_cast<uint32>( snapshot._identity._listComponent.size() );
        for ( const ObjectIdentity::ComponentEntry& entry : snapshot._identity._listComponent )
        {
            archive.writeString( entry._typeFqn.c_str() );
            archive << entry._componentID;
            archive.writeString( entry._stableKey );
        }
    }

    [[nodiscard]] static bool readUndoArg( Archive& archive, ObjectSnapshot& outSnapshot )
    {
        uint32 componentCount{ 0 };
        if ( archive.readString( outSnapshot._xml ) == false )
            return false;
        archive >> outSnapshot._identity._objectID;
        // 항목 하나는 적어도 이름 길이 · id · 키 길이다.
        if ( archive.readCount( sizeof( uint32 ) + sizeof( uint64 ) + sizeof( uint32 ), componentCount ) == false )
            return false;
        outSnapshot._identity._listComponent.resize( componentCount );
        for ( ObjectIdentity::ComponentEntry& entry : outSnapshot._identity._listComponent )
        {
            string typeFqn;
            if ( archive.readString( typeFqn ) == false )
                return false;
            entry._typeFqn = hashed_string( typeFqn.c_str() );
            archive >> entry._componentID;
            if ( archive.readString( entry._stableKey ) == false )
                return false;
        }
        return archive.isOk();
    }

    /** @brief 중첩 프리팹 부품 표시를 되돌리기 단계 인자로 씁니다. */
    static void writeUndoArg( Archive& archive, const PrefabNestedPart& part )
    {
        archive << part._ownerObjectID << part._registrationOrder;
        archive.writeString( part._prefabPath );
        archive.writeString( part._partKey );
    }

    [[nodiscard]] static bool readUndoArg( Archive& archive, PrefabNestedPart& outPart )
    {
        archive >> outPart._ownerObjectID >> outPart._registrationOrder;
        return archive.readString( outPart._prefabPath ) && archive.readString( outPart._partKey );
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct ObjectSnapshotInternal
        {
            /** @brief 대상 씬과 그 오브젝트 매니저입니다. 씬이 없으면 둘 다 nullptr 입니다. */
            struct ActiveTarget
            {
                Scene*             _pScene{ nullptr };
                GameObjectManager* _pManager{ nullptr };
            };

            /** @brief 명령이 기록한 씬을 찾습니다. 키가 0 이면 활성 씬, 아니면 그 미리보기 씬(내려졌으면 없음)입니다. */
            static ActiveTarget findActiveTarget( SceneManager* pSceneManager, uint32 sceneKey )
            {
                ActiveTarget target;
                if ( pSceneManager != nullptr )
                    target._pScene = ( sceneKey != kInvalidSceneKey ) ? pSceneManager->findPreviewScene( sceneKey ) : pSceneManager->getActiveScene();
                target._pManager = ( target._pScene != nullptr ) ? target._pScene->getObjectManager() : nullptr;
                return target;
            }

            /** @brief @p obj 가 사는 씬의 키입니다(활성 · 로드된 씬이면 0). */
            static uint32 computeSceneKey( const SceneManager& sceneManager, const GameObject& obj )
            {
                return sceneManager.getPreviewSceneKeyOf( obj.getManager() );
            }

            /**
             * @brief 대상을 id 로만 찾습니다. 삭제 대기 오브젝트는 찾지 않습니다.
             * @details 이름으로 대신 찾지 않는다 — 같은 이름의 다른 오브젝트를 고치거나 지웠다(감사 E-12). 지운 것을 되살리는 단계는 원래 id 로 만들므로 id 가 이어진다.
             *          못 찾으면 건너뛴다(언리얼 트랜잭션과 같다).
             */
            static GameObject* findObject( const ActiveTarget& target, uint64 objectID )
            {
                if ( target._pManager == nullptr )
                    return nullptr;
                GameObject* pTarget = target._pManager->findGameObjectByID( objectID );
                return ( pTarget != nullptr && pTarget->isPendingDestroy() == false ) ? pTarget : nullptr;
            }

            /** @brief 스냅샷을 오브젝트에 다시 읽습니다. 부모는 스냅샷의 런타임 id 로 찾습니다(같은 실행의 스냅샷이다). */
            static void loadSnapshot( GameObject* pTarget, const ObjectSnapshot& snapshot )
            {
                ObjectLoadContext context{ SerializePurpose::Authoring };
                context._pIdentity = &snapshot._identity;
                // 실패하면 로드가 오브젝트를 읽기 전 상태로 되돌린다 — 되돌리기 하나가 빠졌다고 알린다.
                if ( ObjectStateSerializer::loadFromXMLString( pTarget, snapshot._xml, context ) == false )
                    SW_LOG_WARNING_ID( "ObjectSnapshot.UndoRedoCouldNot", "Undo/redo could not restore '%#' - it is left as it was", pTarget->getName().c_str() );
            }

            /** @brief 이 단계를 실행하는 스택에 알립니다. */
            static void notify( editor::UndoStepContext& context, uint64 objectID, ObjectEditKind kind )
            {
                context._stack.notifyObjectEdit( CommandStack::ObjectEditEvent{ objectID, kind } );
            }

            /** @brief 기록한 씬에서 @p objectID 를 찾아 @p snapshot 을 다시 읽습니다. 없으면 아무것도 하지 않습니다(데이터 단계). */
            static void restore( editor::UndoStepContext& context, uint32 sceneKey, uint64 objectID, ObjectSnapshot snapshot )
            {
                GameObject* pTarget = findObject( findActiveTarget( editor::findSubsystem<SceneManager>(), sceneKey ), objectID );
                if ( pTarget == nullptr )
                    return;
                loadSnapshot( pTarget, snapshot );
                notify( context, pTarget->getObjectID(), ObjectEditKind::Modified );
            }

            /** @brief 기록한 씬에서 @p objectID 를 없앱니다(자식까지). 알림은 없애기 전 — 받는 쪽이 아직 오브젝트를 찾을 수 있다(데이터 단계). */
            static void destroy( editor::UndoStepContext& context, uint32 sceneKey, uint64 objectID )
            {
                const ActiveTarget target  = findActiveTarget( editor::findSubsystem<SceneManager>(), sceneKey );
                GameObject*        pTarget = findObject( target, objectID );
                if ( pTarget == nullptr )
                    return;
                notify( context, pTarget->getObjectID(), ObjectEditKind::Destroyed );
                target._pManager->destroyGameObject( pTarget );
            }

            /** @brief 스냅샷으로 오브젝트를 **원래 id 로** 복원합니다. 그래야 그 오브젝트 · 컴포넌트를 가리키던 핸들이 이어진다(데이터 단계). */
            static void recreate( editor::UndoStepContext& context, uint32 sceneKey, string name, ObjectSnapshot snapshot, string prefabPath, bool bNestedPart, PrefabNestedPart nestedPart )
            {
                const ActiveTarget target = findActiveTarget( editor::findSubsystem<SceneManager>(), sceneKey );
                if ( target._pManager == nullptr )
                    return;
                const uint64 objectID = snapshot._identity._objectID;
                // 같은 프레임에 지운 오브젝트는 지연 파괴가 끝날 때까지 그 id 로 등록돼 있다 — 그대로 만들면 스토어가 새 id 를 주어 자식 · 핸들이 원래 부모를 못 찾았다
                // (부모 · 자식을 지운 뒤 되돌리면 자식이 루트로 돌아왔다). 되돌리기는 프레임 사이에 돌므로 지연 파괴를 먼저 끝낸다.
                target._pManager->processDeferredDestruction();
                GameObject* pCreated = target._pManager->createGameObjectWithID( hashed_string( name.c_str() ), objectID );
                if ( pCreated == nullptr )
                {
                    SW_LOG_WARNING_ID( "ObjectSnapshot.UndoRedoCouldNot2", "Undo/redo could not recreate '%#' with its id %#", name.c_str(), objectID );
                    return;
                }
                loadSnapshot( pCreated, snapshot );
                target._pScene->setEntityPrefabPath( pCreated->getObjectID(), prefabPath );
                // 중첩 프리팹 부품이었으면 표시도 같은 id 로 복원한다 — 레지스트리는 놓인 오브젝트의 표시를 거두므로 명령이 값으로 들고 있다.
                if ( bNestedPart )
                    target._pManager->getSubsystem<PrefabInstanceRegistry>().restoreNestedPart( pCreated->getObjectID(), nestedPart );
                notify( context, pCreated->getObjectID(), ObjectEditKind::Recreated );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ObjectSnapshot ObjectSnapshot::captureSnapshot( const GameObject* pObj )
    {
        ObjectSnapshot snapshot;
        if ( pObj == nullptr )
            return snapshot;
        snapshot._xml      = ObjectStateSerializer::saveToXMLString( pObj, ObjectSaveOptions{ SerializePurpose::Authoring } );
        snapshot._identity = ObjectStateSerializer::captureIdentity( pObj );
        return snapshot;
    }

    CommandStack::Command ObjectSnapshot::makeModify( SceneManager& sceneManager, const GameObject& obj, const ObjectSnapshot& before, const ObjectSnapshot& after,
                                                      string_view label )
    {
        const uint32          sceneKey = ObjectSnapshotInternal::computeSceneKey( sceneManager, obj );
        const uint64          objectID = obj.getObjectID();
        CommandStack::Command cmd{};
        cmd._label = string{ label };
        cmd._undo  = SW_UNDO_STEP( ObjectSnapshotInternal::restore, sceneKey, objectID, before );
        cmd._redo  = SW_UNDO_STEP( ObjectSnapshotInternal::restore, sceneKey, objectID, after );
        return cmd;
    }

    CommandStack::Command ObjectSnapshot::makeLifetime( SceneManager& sceneManager, const GameObject* pObj, ObjectLifetimeEdit edit, string_view label )
    {
        CommandStack::Command cmd{};
        if ( pObj == nullptr )
            return cmd;

        const uint32         sceneKey = ObjectSnapshotInternal::computeSceneKey( sceneManager, *pObj );
        const uint64         objectID = pObj->getObjectID();
        const string         name{ pObj->getName().c_str() };
        const ObjectSnapshot snapshot = captureSnapshot( pObj );
        const Scene*         pScene   = ObjectSnapshotInternal::findActiveTarget( &sceneManager, sceneKey )._pScene;
        const string         prefabPath{ ( pScene != nullptr ) ? pScene->getEntityPrefabPath( objectID ) : string_view{} };
        // 중첩 프리팹 부품 표시는 값으로 든다 — 레지스트리는 놓인 오브젝트의 표시를 거둔다.
        const PrefabNestedPart* pFoundPart  = ( pObj->getManager() != nullptr ) ? pObj->getManager()->getSubsystem<PrefabInstanceRegistry>().findNestedPart( objectID ) : nullptr;
        const bool              bNestedPart = pFoundPart != nullptr;
        const PrefabNestedPart  nestedPart  = bNestedPart ? *pFoundPart : PrefabNestedPart{};

        const editor::UndoStep destroyStep  = SW_UNDO_STEP( ObjectSnapshotInternal::destroy, sceneKey, objectID );
        const editor::UndoStep recreateStep = SW_UNDO_STEP( ObjectSnapshotInternal::recreate, sceneKey, name, snapshot, prefabPath, bNestedPart, nestedPart );

        // 생성과 삭제는 같은 두 절차를 반대 순서로 잇는 것이다.
        const bool bUndoRecreatesTheObject = ( edit == ObjectLifetimeEdit::Destroyed );
        cmd._label                         = string{ label };
        cmd._undo                          = bUndoRecreatesTheObject ? recreateStep : destroyStep;
        cmd._redo                          = bUndoRecreatesTheObject ? destroyStep : recreateStep;
        return cmd;
    }
} // namespace sw
