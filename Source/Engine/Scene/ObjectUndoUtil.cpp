#include "pch.h"

#include "Engine/Scene/ObjectUndoUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    SW_LOG_CALLER( "ObjectUndoUtil" );

    namespace
    {
        struct ObjectUndoUtilInternal
        {
            /** @brief 대상과 그 활성 씬입니다. 활성 씬이 없으면 둘 다 nullptr 입니다. */
            struct ActiveTarget
            {
                Scene*             _pScene{ nullptr };
                GameObjectManager* _pManager{ nullptr };
            };

            static ActiveTarget findActiveTarget( SceneManager* pSceneManager )
            {
                ActiveTarget target;
                target._pScene   = ( pSceneManager != nullptr ) ? pSceneManager->getActiveScene() : nullptr;
                target._pManager = ( target._pScene != nullptr ) ? target._pScene->getObjectManager() : nullptr;
                return target;
            }

            /** @brief 대상을 id 로, 없으면 기록할 때의 이름으로 찾습니다(헤더의 주의 참고). 삭제 대기 오브젝트는 찾지 않습니다. */
            static GameObject* findObject( const ActiveTarget& target, uint64 objectId, const string& name )
            {
                if ( target._pManager == nullptr )
                    return nullptr;
                GameObject* pTarget = target._pManager->findGameObjectById( objectId );
                if ( pTarget == nullptr && name.empty() == false )
                    pTarget = target._pManager->findGameObjectByName( hashed_string( name.c_str() ) );
                return ( pTarget != nullptr && pTarget->isPendingDestroy() == false ) ? pTarget : nullptr;
            }

            /** @brief 스냅샷을 오브젝트에 다시 읽습니다. 부모는 스냅샷의 런타임 id 로 찾습니다(같은 실행의 스냅샷이다). */
            static void loadSnapshot( GameObject* pTarget, const ObjectSnapshot& snapshot )
            {
                ObjectLoadContext context{};
                context._pIdentity = &snapshot._identity;
                // 실패하면 로드가 오브젝트를 읽기 전 상태로 되돌린다 — 되돌리기 하나가 빠졌다고 알린다.
                if ( ObjectStateSerializer::loadFromXMLString( pTarget, snapshot._xml, context ) == false )
                    SW_LOG_WARNING( "Undo/redo could not restore '%#' - it is left as it was", pTarget->getName().c_str() );
            }

            /** @brief 활성 씬에서 @p objectId 를 찾아 @p snapshot 을 다시 읽습니다. 없으면 아무것도 하지 않습니다. */
            static void restore( CommandStack* pStack, SceneManager* pSceneManager, uint64 objectId, const string& name, const ObjectSnapshot& snapshot )
            {
                GameObject* pTarget = findObject( findActiveTarget( pSceneManager ), objectId, name );
                if ( pTarget == nullptr )
                    return;
                loadSnapshot( pTarget, snapshot );
                pStack->notifyObjectEdit( CommandStack::ObjectEditNotice{ pTarget->getObjectId(), ObjectEditKind::Modified } );
            }

            /** @brief 활성 씬에서 @p objectId 를 없앱니다(자식까지). 알림은 없애기 전 — 받는 쪽이 아직 오브젝트를 찾을 수 있다. */
            static void destroy( CommandStack* pStack, SceneManager* pSceneManager, uint64 objectId, const string& name )
            {
                const ActiveTarget target  = findActiveTarget( pSceneManager );
                GameObject*        pTarget = findObject( target, objectId, name );
                if ( pTarget == nullptr )
                    return;
                pStack->notifyObjectEdit( CommandStack::ObjectEditNotice{ pTarget->getObjectId(), ObjectEditKind::Destroyed } );
                target._pManager->destroyObject( pTarget );
            }

            /** @brief 스냅샷으로 오브젝트를 **원래 id 로** 되살립니다. 그래야 그 오브젝트 · 컴포넌트를 가리키던 핸들이 이어진다. */
            static void recreate( CommandStack* pStack, SceneManager* pSceneManager, const string& name, const ObjectSnapshot& snapshot,
                                  const string& prefabPath )
            {
                const ActiveTarget target = findActiveTarget( pSceneManager );
                if ( target._pManager == nullptr )
                    return;
                const uint64 objectId = snapshot._identity._objectId;
                GameObject*  pCreated = target._pManager->createGameObjectWithId( hashed_string( name.c_str() ), objectId );
                if ( pCreated == nullptr )
                {
                    SW_LOG_WARNING( "Undo/redo could not recreate '%#' with its id %#", name.c_str(), objectId );
                    return;
                }
                loadSnapshot( pCreated, snapshot );
                target._pScene->setEntityPrefabPath( pCreated->getObjectId(), prefabPath );
                pStack->notifyObjectEdit( CommandStack::ObjectEditNotice{ pCreated->getObjectId(), ObjectEditKind::Recreated } );
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
        snapshot._xml      = ObjectStateSerializer::saveToXMLString( pObj );
        snapshot._identity = ObjectStateSerializer::captureIdentity( pObj );
        return snapshot;
    }

    CommandStack::Command ObjectUndoUtil::makeModify( CommandStack& stack, SceneManager& sceneManager, const GameObject& obj,
                                                      const ObjectSnapshot& before, const ObjectSnapshot& after, string_view label )
    {
        CommandStack* const   pStack        = &stack;
        SceneManager* const   pSceneManager = &sceneManager;
        const uint64          objectId      = obj.getObjectId();
        const string          name{ obj.getName().c_str() };
        CommandStack::Command cmd{};
        cmd._label = string{ label };
        cmd._undo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [pStack, pSceneManager, objectId, name, before]()
         {
            ObjectUndoUtilInternal::restore( pStack, pSceneManager, objectId, name, before );
        } );
        cmd._redo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [pStack, pSceneManager, objectId, name, after]()
         {
            ObjectUndoUtilInternal::restore( pStack, pSceneManager, objectId, name, after );
        } );
        return cmd;
    }

    CommandStack::Command ObjectUndoUtil::makeLifetime( CommandStack& stack, SceneManager& sceneManager, const GameObject* pObj,
                                                        ObjectLifetimeEdit edit, string_view label )
    {
        CommandStack::Command cmd{};
        if ( pObj == nullptr )
            return cmd;

        CommandStack* const  pStack        = &stack;
        SceneManager* const  pSceneManager = &sceneManager;
        const uint64         objectId      = pObj->getObjectId();
        const string         name{ pObj->getName().c_str() };
        const ObjectSnapshot snapshot   = captureSnapshot( pObj );
        const Scene*         pScene     = sceneManager.getActiveScene();
        const string         prefabPath = ( pScene != nullptr ) ? pScene->getEntityPrefabPath( objectId ) : string{};

        const Delegate<void()> destroyStep  = SW_DELEGATE_LAMBDA( Delegate<void()>, [pStack, pSceneManager, objectId, name]()
         {
            ObjectUndoUtilInternal::destroy( pStack, pSceneManager, objectId, name );
        } );
        const Delegate<void()> recreateStep = SW_DELEGATE_LAMBDA( Delegate<void()>, [pStack, pSceneManager, name, snapshot, prefabPath]()
        {
            ObjectUndoUtilInternal::recreate( pStack, pSceneManager, name, snapshot, prefabPath );
        } );

        // 생성과 삭제는 같은 두 절차를 반대 순서로 잇는 것이다.
        const bool bUndoRecreatesTheObject = ( edit == ObjectLifetimeEdit::Destroyed );
        cmd._label                         = string{ label };
        cmd._undo                          = bUndoRecreatesTheObject ? recreateStep : destroyStep;
        cmd._redo                          = bUndoRecreatesTheObject ? destroyStep : recreateStep;
        return cmd;
    }
} // namespace sw
