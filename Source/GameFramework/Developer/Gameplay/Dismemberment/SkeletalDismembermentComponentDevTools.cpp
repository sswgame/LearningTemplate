/**
 * @file SkeletalDismembermentComponentDevTools.cpp
 * @brief `SkeletalDismembermentComponent` 의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/CharacterMeshUtil.h"
#include "Engine/Character/Hit/RagdollComponent.h"
#include "Engine/Character/PoseModifier/CharacterPoseUtil.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Physics/Asset/PhysicsAsset.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/Scene/PhysicsNameValidation.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Dismemberment/DismemberResult.h"
#include "GameFramework/Runtime/Gameplay/Dismemberment/SkeletalDismembermentComponent.h"

namespace sw
{
    namespace
    {
        struct SkeletalDismembermentComponentDevInternal
        {
            /** @brief 질의 대상의 절단 컴포넌트입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static const SkeletalDismembermentComponent* findTarget( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return query.findTargetComponent<SkeletalDismembermentComponent>( "SkeletalDismembermentComponent", outValue );
            }

            /** @brief 감시 값 `SkeletalDismemberment.SeveredCount` 입니다. 대상에서 잘린 영역 수입니다. */
            [[nodiscard]] static bool readSeveredCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SkeletalDismembermentComponent* pFound = findTarget( query, outValue );
                if ( pFound == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pFound->getSeveredRegions().size() ) );
                return true;
            }

            /** @brief 감시 값 `SkeletalDismemberment.MeshLODCount` 입니다. 대상 몸 메시(같은 오브젝트의 SkeletalMeshComponent)의 LOD 단계 수(LOD 0 포함)입니다 — 자른 뒤에도 원본과 같아야 한다. */
            [[nodiscard]] static bool readMeshLODCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SkeletalDismembermentComponent* pFound = findTarget( query, outValue );
                const SkeletalMeshComponent*          pUnit  = pFound != nullptr ? pFound->getOwner()->getComponent<SkeletalMeshComponent>() : nullptr;
                if ( pUnit == nullptr || pUnit->getMesh() == nullptr )
                {
                    if ( pFound != nullptr )
                        outValue.setText( "no skinned body mesh yet" );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( pUnit->getMesh()->getLODCount() ) );
                return true;
            }

            /** @brief 감시 값 `SkeletalDismemberment.RegionSurfaceValue` 입니다. 영역 `key` 의 표면 상태에 오른 잘린 면 채널(_severedSurfaceChannel) 값입니다. 표에 없는 영역이면 값이 없습니다. */
            [[nodiscard]] static bool readRegionSurfaceValue( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SkeletalDismembermentComponent* pFound = findTarget( query, outValue );
                if ( pFound == nullptr )
                    return false;
                const optional<uint32> regionIndex = pFound->findRegionIndex( hashed_string{ query._key } );
                if ( regionIndex.has_value() == false )
                {
                    outValue.setText( "no such body region" );
                    return false;
                }
                const CharacterSurfaceState* pSurface = pFound->findSurfaceState();
                if ( pSurface == nullptr )
                {
                    outValue.setText( "no appearance surface state" );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( pSurface->getRegionValue( *regionIndex, pFound->getSeveredSurfaceChannel() ) ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( dismembermentSeveredCount, "SkeletalDismemberment.SeveredCount", "Body regions severed from the target", &SkeletalDismembermentComponentDevInternal::readSeveredCount, true, false );
    SW_AUTOMATION_QUERY_WATCH( dismembermentMeshLODCount, "SkeletalDismemberment.MeshLODCount", "LOD levels of the target's body mesh (LOD 0 included); a cut keeps the count", &SkeletalDismembermentComponentDevInternal::readMeshLODCount, true,
                               false );
    SW_AUTOMATION_QUERY_WATCH( dismembermentRegionSurfaceValue, "SkeletalDismemberment.RegionSurfaceValue", "Severed surface channel value of the target's body region named key", &SkeletalDismembermentComponentDevInternal::readRegionSurfaceValue, true, true );
} // namespace sw
