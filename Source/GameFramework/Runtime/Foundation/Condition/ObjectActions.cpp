#include "pch.h"

#include "GameFramework/Runtime/Foundation/Condition/ObjectActions.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Behavior/ActionRegistry.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "GameFramework/Runtime/Foundation/Condition/GameConditions.h"

namespace sw
{
    namespace
    {
        struct ObjectActionsInternal
        {
            /** @brief 대상과 그 매니저입니다. 대상 칸이 없거나 지우는 중이면 false 입니다. */
            static bool findTarget( const ActionContext& context, GameObject*& outTarget, GameObjectManager*& outManager )
            {
                outTarget  = context._world.find<ActionTargetSlot>();
                outManager = outTarget != nullptr ? outTarget->getManager() : nullptr;
                return outManager != nullptr && outTarget->isPendingDestroy() == false;
            }

            /** @brief @p pTarget 의 월드 자리입니다(장면 컴포넌트가 없으면 원점). */
            static float3 findPosition( const GameObject& target )
            {
                const SceneComponent* const pScene = target.getPrimarySceneComponent();
                return pScene != nullptr ? pScene->getWorldPosition() : float3{};
            }

            /** @brief 프리팹 @p path 를 @p position 에 세웁니다. 못 세우면 오류를 남기고 nullptr 입니다. */
            static GameObject* spawnAt( GameObjectManager& manager, const string& path, const float3& position )
            {
                AssetManager* const pAssets = manager.findAssetManager();
                GameObject* const   pSpawn  = pAssets != nullptr ? pAssets->getAssetCache<PrefabCache>().spawn( &manager, path ) : nullptr;
                SceneComponent*     pScene  = pSpawn != nullptr ? pSpawn->getPrimarySceneComponent() : nullptr;
                if ( pScene == nullptr )
                {
                    SW_LOG_ERROR_ID( "ObjectActions.SpawnFailed", "object.spawn/replace: prefab '%#' could not be spawned or has no scene component", path.c_str() );
                    return nullptr;
                }
                pScene->setWorldPosition( position );
                return pSpawn;
            }

            static ActionResult runDestroy( ActionContext& context, const VocabularyNoParams& /*params*/ )
            {
                GameObject*        pTarget  = nullptr;
                GameObjectManager* pManager = nullptr;
                if ( findTarget( context, pTarget, pManager ) == false )
                    return ActionResult::Failed;
                pManager->destroyGameObject( pTarget );
                return ActionResult::Succeeded;
            }

            static ActionResult runReplace( ActionContext& context, const ObjectPrefabParams& params )
            {
                GameObject*        pTarget  = nullptr;
                GameObjectManager* pManager = nullptr;
                if ( findTarget( context, pTarget, pManager ) == false )
                    return ActionResult::Failed;
                // 바꿀 것이 서지 못하면 원래 것을 지우지 않는다(물체가 조용히 사라지지 않게).
                if ( spawnAt( *pManager, params._prefab, findPosition( *pTarget ) ) == nullptr )
                    return ActionResult::Failed;
                pManager->destroyGameObject( pTarget );
                return ActionResult::Succeeded;
            }

            static ActionResult runSpawn( ActionContext& context, const ObjectPrefabParams& params )
            {
                GameObject*        pTarget  = nullptr;
                GameObjectManager* pManager = nullptr;
                if ( findTarget( context, pTarget, pManager ) == false )
                    return ActionResult::Failed;
                return spawnAt( *pManager, params._prefab, findPosition( *pTarget ) ) != nullptr ? ActionResult::Succeeded : ActionResult::Failed;
            }

            static ActionResult runTeleport( ActionContext& context, const ObjectTeleportParams& params )
            {
                GameObject*        pTarget  = nullptr;
                GameObjectManager* pManager = nullptr;
                if ( findTarget( context, pTarget, pManager ) == false )
                    return ActionResult::Failed;
                const GameObject* const pDestination = pManager->findGameObjectByName( params._to );
                SceneComponent* const   pScene       = pTarget->getPrimarySceneComponent();
                if ( pDestination == nullptr || pScene == nullptr )
                    return ActionResult::Failed;
                // 트랜스폼을 옮기면 물리 컴포넌트가 순간이동으로 받는다(벽을 쓸고 가지 않는다). 강체는 남은 속도도 버린다 — 빠진 열쇠가 제자리에서 다시 떨어지지 않게.
                pScene->setWorldPosition( findPosition( *pDestination ) );
                if ( RigidBodyComponent* const pBody = pTarget->getComponent<RigidBodyComponent>() )
                    pBody->setLinearVelocity( float3{} );
                return ActionResult::Succeeded;
            }
        };
    } // namespace

    SW_GAME_ACTION( ObjectDestroy, "object.destroy", VocabularyNoParams, &ObjectActionsInternal::runDestroy );
    SW_GAME_ACTION( ObjectReplace, "object.replace", ObjectPrefabParams, &ObjectActionsInternal::runReplace );
    SW_GAME_ACTION( ObjectSpawn, "object.spawn", ObjectPrefabParams, &ObjectActionsInternal::runSpawn );
    SW_GAME_ACTION( ObjectTeleport, "object.teleport", ObjectTeleportParams, &ObjectActionsInternal::runTeleport );
} // namespace sw
