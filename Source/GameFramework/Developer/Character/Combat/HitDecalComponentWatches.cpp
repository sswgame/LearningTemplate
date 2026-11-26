/**
 * @file HitDecalComponentWatches.cpp
 * @brief `HitDecalComponent` 의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

#include "GameFramework/Runtime/Character/Combat/HitDecalComponent.h"

namespace sw
{
    namespace
    {
        struct HitDecalComponentDevInternal
        {
            /** @brief 감시 값 `Hit.DecalCount` 입니다. 대상이 지금까지 세운 맞음 데칼 수입니다. */
            [[nodiscard]] static bool readDecalCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const HitDecalComponent* pDecals = query.findTargetComponent<HitDecalComponent>( "HitDecalComponent", outValue );
                if ( pDecals == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pDecals->getSpawnedCount() ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( hitDecalCount, "Hit.DecalCount", "Hit decals the target spawned so far", &HitDecalComponentDevInternal::readDecalCount, true, false );
} // namespace sw
