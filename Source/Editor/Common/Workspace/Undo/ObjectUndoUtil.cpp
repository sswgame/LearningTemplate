#include "pch.h"

#include "Editor/Common/Workspace/Undo/ObjectUndoUtil.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
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
        archive >> outSnapshot._identity._objectID >> componentCount;
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
    SW_LOG_CALLER( "ObjectUndoUtil" );

    namespace
    {
        struct ObjectUndoUtilInternal
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

            /** @brief 대상을 id 로, 없으면 기록할 때의 이름으로 찾습니다(헤더의 주의 참고). 삭제 대기 오브젝트는 찾지 않습니다. */
            static GameObject* findObject( const ActiveTarget& target, uint64 objectID, const string& name )
            {
                if ( target._pManager == nullptr )
                    return nullptr;
                GameObject* pTarget = target._pManager->findGameObjectByID( objectID );
                if ( pTarget == nullptr && name.empty() == false )
                    pTarget = target._pManager->findGameObjectByName( hashed_string( name.c_str() ) );
                return ( pTarget != nullptr && pTarget->isPendingDestroy() == false ) ? pTarget : nullptr;
            }

            /** @brief 스냅샷을 오브젝트에 다시 읽습니다. 부모는 스냅샷의 런타임 id 로 찾습니다(같은 실행의 스냅샷이다). */
            static void loadSnapshot( GameObject* pTarget, const ObjectSnapshot& snapshot )
            {
                ObjectLoadContext context{ SerializePurpose::Authoring };
                context._pIdentity = &snapshot._identity;
                // 실패하면 로드가 오브젝트를 읽기 전 상태로 되돌린다 — 되돌리기 하나가 빠졌다고 알린다.
                if ( ObjectStateSerializer::loadFromXMLString( pTarget, snapshot._xml, context ) == false )
                    SW_LOG_WARNING( "Undo/redo could not restore '%#' - it is left as it was", pTarget->getName().c_str() );
            }

            /** @brief 실행 중인 스택에 알립니다(단계 밖에서 부르면 아무것도 하지 않는다). */
            static void notify( uint64 objectID, ObjectEditKind kind )
            {
                if ( CommandStack* pStack = CommandStack::findExecuting() )
                    pStack->notifyObjectEdit( CommandStack::ObjectEditNotice{ objectID, kind } );
            }

            /** @brief 기록한 씬에서 @p objectID 를 찾아 @p snapshot 을 다시 읽습니다. 없으면 아무것도 하지 않습니다(데이터 단계). */
            static void restore( uint32 sceneKey, uint64 objectID, string name, ObjectSnapshot snapshot )
            {
                GameObject* pTarget = findObject( findActiveTarget( editor::findSubsystem<SceneManager>(), sceneKey ), objectID, name );
                if ( pTarget == nullptr )
                    return;
                loadSnapshot( pTarget, snapshot );
                notify( pTarget->getObjectID(), ObjectEditKind::Modified );
            }

            /** @brief 기록한 씬에서 @p objectID 를 없앱니다(자식까지). 알림은 없애기 전 — 받는 쪽이 아직 오브젝트를 찾을 수 있다(데이터 단계). */
            static void destroy( uint32 sceneKey, uint64 objectID, string name )
            {
                const ActiveTarget target  = findActiveTarget( editor::findSubsystem<SceneManager>(), sceneKey );
                GameObject*        pTarget = findObject( target, objectID, name );
                if ( pTarget == nullptr )
                    return;
                notify( pTarget->getObjectID(), ObjectEditKind::Destroyed );
                target._pManager->destroyObject( pTarget );
            }

            /** @brief 스냅샷으로 오브젝트를 **원래 id 로** 복원합니다. 그래야 그 오브젝트 · 컴포넌트를 가리키던 핸들이 이어진다(데이터 단계). */
            static void recreate( uint32 sceneKey, string name, ObjectSnapshot snapshot, string prefabPath, bool bNestedPart, PrefabNestedPart nestedPart )
            {
                const ActiveTarget target = findActiveTarget( editor::findSubsystem<SceneManager>(), sceneKey );
                if ( target._pManager == nullptr )
                    return;
                const uint64 objectID = snapshot._identity._objectID;
                GameObject*  pCreated = target._pManager->createGameObjectWithID( hashed_string( name.c_str() ), objectID );
                if ( pCreated == nullptr )
                {
                    SW_LOG_WARNING( "Undo/redo could not recreate '%#' with its id %#", name.c_str(), objectID );
                    return;
                }
                loadSnapshot( pCreated, snapshot );
                target._pScene->setEntityPrefabPath( pCreated->getObjectID(), prefabPath );
                // 중첩 프리팹 부품이었으면 표시도 같은 id 로 복원한다 — 레지스트리는 놓인 오브젝트의 표시를 거두므로 명령이 값으로 들고 있다.
                if ( bNestedPart )
                    target._pManager->getSubsystem<PrefabInstanceRegistry>().restoreNestedPart( pCreated->getObjectID(), nestedPart );
                notify( pCreated->getObjectID(), ObjectEditKind::Recreated );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ObjectSnapshot ObjectUndoUtil::captureSnapshot( const GameObject* pObj )
    {
        ObjectSnapshot snapshot;
        if ( pObj == nullptr )
            return snapshot;
        snapshot._xml      = ObjectStateSerializer::saveToXMLString( pObj, ObjectSaveOptions{ SerializePurpose::Authoring } );
        snapshot._identity = ObjectStateSerializer::captureIdentity( pObj );
        return snapshot;
    }

    CommandStack::Command ObjectUndoUtil::makeModify( SceneManager& sceneManager, const GameObject& obj, const ObjectSnapshot& before, const ObjectSnapshot& after,
                                                      string_view label )
    {
        const uint32          sceneKey = ObjectUndoUtilInternal::computeSceneKey( sceneManager, obj );
        const uint64          objectID = obj.getObjectID();
        const string          name{ obj.getName().c_str() };
        CommandStack::Command cmd{};
        cmd._label = string{ label };
        cmd._undo  = SW_UNDO_STEP( ObjectUndoUtilInternal::restore, sceneKey, objectID, name, before );
        cmd._redo  = SW_UNDO_STEP( ObjectUndoUtilInternal::restore, sceneKey, objectID, name, after );
        return cmd;
    }

    CommandStack::Command ObjectUndoUtil::makeLifetime( SceneManager& sceneManager, const GameObject* pObj, ObjectLifetimeEdit edit, string_view label )
    {
        CommandStack::Command cmd{};
        if ( pObj == nullptr )
            return cmd;

        const uint32         sceneKey = ObjectUndoUtilInternal::computeSceneKey( sceneManager, *pObj );
        const uint64         objectID = pObj->getObjectID();
        const string         name{ pObj->getName().c_str() };
        const ObjectSnapshot snapshot = captureSnapshot( pObj );
        const Scene*         pScene   = ObjectUndoUtilInternal::findActiveTarget( &sceneManager, sceneKey )._pScene;
        const string         prefabPath{ ( pScene != nullptr ) ? pScene->getEntityPrefabPath( objectID ) : string_view{} };
        // 중첩 프리팹 부품 표시는 값으로 든다 — 레지스트리는 놓인 오브젝트의 표시를 거둔다.
        const PrefabNestedPart* pFoundPart  = ( pObj->getManager() != nullptr ) ? pObj->getManager()->getSubsystem<PrefabInstanceRegistry>().findNestedPart( objectID ) : nullptr;
        const bool              bNestedPart = pFoundPart != nullptr;
        const PrefabNestedPart  nestedPart  = bNestedPart ? *pFoundPart : PrefabNestedPart{};

        const editor::UndoStep destroyStep  = SW_UNDO_STEP( ObjectUndoUtilInternal::destroy, sceneKey, objectID, name );
        const editor::UndoStep recreateStep = SW_UNDO_STEP( ObjectUndoUtilInternal::recreate, sceneKey, name, snapshot, prefabPath, bNestedPart, nestedPart );

        // 생성과 삭제는 같은 두 절차를 반대 순서로 잇는 것이다.
        const bool bUndoRecreatesTheObject = ( edit == ObjectLifetimeEdit::Destroyed );
        cmd._label                         = string{ label };
        cmd._undo                          = bUndoRecreatesTheObject ? recreateStep : destroyStep;
        cmd._redo                          = bUndoRecreatesTheObject ? destroyStep : recreateStep;
        return cmd;
    }
} // namespace sw
