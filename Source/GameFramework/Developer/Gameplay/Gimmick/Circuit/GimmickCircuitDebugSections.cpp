/**
 * @file GimmickCircuitDebugSections.cpp
 * @brief 기믹 회로의 디버그 HUD 섹션 `gimmick` 입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Registry/TagIndex.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Visual/Component/LightComponent.h"

#include "GameFramework/Runtime/Foundation/Debug/GameplayDebugTarget.h"
#include "GameFramework/Runtime/Foundation/Presentation/GameSound.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Circuit/GimmickCircuitComponent.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/GimmickSensorComponent.h"
#include "GameFramework/Runtime/World/Query/WorldQuery.h"
#include "GameFramework/Runtime/World/Spline/SplineComponent.h"

namespace sw
{
    namespace
    {
        /** @brief 런타임 기믹 디버거(HUD 섹션 `gimmick`)입니다. 대상은 `debug.target`, 없으면 활성 씬의 첫 회로입니다. */
        struct GimmickDebugSectionInternal
        {
            static constexpr int32 kSectionOrder = 220; ///< 디버그 HUD 의 섹션 차례

            static void writeGimmick( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* const       pManager = GameplayDebugTarget::findActiveObjectManager();
                const GimmickCircuitComponent* const pCircuit = pManager != nullptr ? GameplayDebugTarget::findTargetOrLowestIDComponent<GimmickCircuitComponent>( writer.getSceneIterationKey(), *pManager ) : nullptr;
                if ( pCircuit == nullptr )
                {
                    writer.addLine( "target", "none (no gimmick circuit in the active scene)" );
                    return;
                }
                writer.addLine( "target", pCircuit->getOwner() != nullptr ? string( pCircuit->getOwner()->getName().c_str() ) : string( "?" ) );
                for ( const string& error : pCircuit->getErrors() )
                {
                    writer.addLine( "error", error );
                }
                if ( pCircuit->getCircuit().isBuilt() == false )
                {
                    writer.addLine( "circuit", "not built (see the errors)" );
                    return;
                }
                pCircuit->getCircuit().writeDebugLines( writer, GameplayDebugTarget::kMaxLines );
            }
        };
    } // namespace

    SW_DEBUG_HUD_SECTION( gimmick, "gimmick", "Gimmick circuit", GimmickDebugSectionInternal::kSectionOrder, DebugHUDSectionFlag::kNone, &GimmickDebugSectionInternal::writeGimmick );
} // namespace sw
