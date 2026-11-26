#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/AssetRef.h"
#include "Engine/Behavior/ActionRegistry.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Condition/GameConditions.h"

namespace sw
{
    /** @brief `object.replace` · `object.spawn` 의 칸입니다. */
    REFLECT()
    struct ObjectPrefabParams
    {
        REFLECT_BODY();

        /** @brief 스폰할 프리팹입니다 — 저장 꼴은 GUID 참조(`AssetRefBase`). 글 칸이었을 때는 같은 이름의 참조 칸(`prefab`)과 GUID 이전 도구가 가르지 못했다. */
        PROPERTY( Required, Tooltip = "Prefab spawned at the target's position (*.prefab.xml)" )
        AssetRef<PrefabAsset> _prefab{};
    };

    /** @brief `object.teleport` 의 칸입니다. */
    REFLECT()
    struct ObjectTeleportParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Object name whose position the target moves to (checkpoint, portal exit, key home)" )
        hashed_string _to{};
    };

    namespace
    {
        struct ObjectActionsInternal
        {
            /** @brief 대상과 그 매니저입니다. 대상 칸이 없거나 지우는 중이면 false 입니다. */
            static bool findTarget( const ActionContext& context, GameObject*& outTarget, GameObjectManager*& outManager )
            {
                outTarget  = context._world.find<ActionTargetSlot>();
                outManager = outTarget != nullptr ? outTarget->getMutableManager() : nullptr;
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
                GameObject* const   pSpawn  = pAssets != nullptr ? pAssets->getAssetCache<PrefabCache>().spawn( &manager, path, SpawnLifetime::Persistent ) : nullptr;
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
                if ( spawnAt( *pManager, params._prefab.path(), findPosition( *pTarget ) ) == nullptr )
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
                return spawnAt( *pManager, params._prefab.path(), findPosition( *pTarget ) ) != nullptr ? ActionResult::Succeeded : ActionResult::Failed;
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
                // 경로를 쓸지 않고 옮기며 남은 운동(강체 · 캐릭터)도 버린다 — 빠진 열쇠가 제자리에서 다시 떨어지지 않게. 볼륨(`TeleportTriggerComponent`)과 같은 길이다.
                pScene->teleportTo( findPosition( *pDestination ), TeleportType::ResetPhysics );
                return ActionResult::Succeeded;
            }
        };
    } // namespace

    SW_GAME_ACTION( ObjectDestroy, "object.destroy", VocabularyNoParams, &ObjectActionsInternal::runDestroy );
    SW_GAME_ACTION( ObjectReplace, "object.replace", ObjectPrefabParams, &ObjectActionsInternal::runReplace );
    SW_GAME_ACTION( ObjectSpawn, "object.spawn", ObjectPrefabParams, &ObjectActionsInternal::runSpawn );
    SW_GAME_ACTION( ObjectTeleport, "object.teleport", ObjectTeleportParams, &ObjectActionsInternal::runTeleport );
} // namespace sw

#include "ObjectActions.cpp.gen.inl"
