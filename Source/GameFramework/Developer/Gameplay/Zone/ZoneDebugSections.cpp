/**
 * @file ZoneDebugSections.cpp
 * @brief 존의 디버그 HUD 섹션 `zone` 입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Console/DebugHUDSectionFormat.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Physics/Component/CharacterController2DComponent.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Foundation/Debug/GameplayDebugTarget.h"
#include "GameFramework/Runtime/Gameplay/Zone/Reaction/RemoveStacksOnJumpComponent.h"
#include "GameFramework/Runtime/Gameplay/Zone/Reaction/SinkOffsetComponent.h"
#include "GameFramework/Runtime/Gameplay/Zone/Reaction/TagPostProcessComponent.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneContactComponent.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneVolumeComponent.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct ZoneDebugSectionsInternal
        {
            /** @brief 대상 @p target 이 든 지역마다 @p visit( 출처, 위치 값 ) 을 부릅니다 — 볼륨과 대상의 표면 지역. */
            template <typename TVisit>
            static void forEachZone( const GameObjectManager& manager, const GameObject& target, TVisit&& visit )
            {
                const GameObjectHandle handle = target.getHandle();
                for ( const ZoneVolumeComponent* pVolume : manager.getComponentRegistry().getAll<ZoneVolumeComponent>() )
                {
                    const ZoneTracker::OccupantRecord* pOccupancy = pVolume->getTracker().findOccupancy( handle );
                    if ( pOccupancy != nullptr && pOccupancy->_bEntered == SW_TRUE )
                        visit( pVolume->getSourceName(), pOccupancy->_sample );
                }
                const ZoneContactComponent* pContact = target.getComponent<ZoneContactComponent>();
                if ( pContact == nullptr )
                    return;
                for ( const unique_ptr<ZoneContactComponent::SurfaceZone>& pZone : pContact->getSurfaceZones() )
                {
                    const ZoneTracker::OccupantRecord* pOccupancy = pZone->_tracker.findOccupancy( handle );
                    if ( pOccupancy != nullptr && pOccupancy->_bEntered == SW_TRUE )
                        visit( pZone->_sourceName, pOccupancy->_sample );
                }
            }

            /** @brief HUD 섹션 `zone` — 디버그 대상이 든 지역의 출처 · 위치 값과 최근 진입 · 이탈입니다. */
            static void writeZone( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* const pManager = GameplayDebugTarget::findActiveObjectManager();
                // 대상은 지역을 받는 개체다 — 캐릭터(컨트롤러)가 먼저, 없으면 표면 입구를 가진 물체.
                const CharacterControllerComponent* pController = pManager != nullptr ? GameplayDebugTarget::findTargetOrLowestIDComponent<CharacterControllerComponent>( writer.getSceneIterationKey(), *pManager ) : nullptr;
                const ZoneContactComponent*         pContact    = pManager != nullptr && pController == nullptr ? GameplayDebugTarget::findTargetOrLowestIDComponent<ZoneContactComponent>( writer.getSceneIterationKey(), *pManager ) : nullptr;
                const GameObject* const             pTarget     = pController != nullptr ? pController->getOwner() : ( pContact != nullptr ? pContact->getOwner() : nullptr );
                if ( pTarget == nullptr )
                {
                    writer.addLine( "target", "none (debug.target <object>)" );
                    return;
                }
                uint32 zoneCount = 0;
                forEachZone( *pManager, *pTarget, [&]( const hashed_string& source, const ZoneSample& sample )
                {
                    writer.addLineFormat( source.c_str(), "depth %# dist %# immersion %# time %# impact %#", sample._zoneDepth01, sample._distanceToCenter, sample._immersion,
                                          sample._timeInside, sample._impactSpeed );
                    ++zoneCount;
                } );
                if ( zoneCount == 0 )
                    writer.addLine( pTarget->getName().c_str(), "in no zone" );
                if ( pController != nullptr )
                {
                    const PhysicsFieldSample& field = pController->getPhysicsField();
                    writer.addLineFormat( "movement", "%# gravity %# drag %# fluid %# friction %#", getEnumName( pController->getMovementMode() ), field._gravityScale, field._drag,
                                          field._fluidDensity, field._friction );
                }
            }
        };
    } // namespace

    SW_DEBUG_HUD_SECTION( zone, "zone", "Zones the debug target is in, zone values and movement field", 285, DebugHUDSectionFlag::kNone, &ZoneDebugSectionsInternal::writeZone );
} // namespace sw
