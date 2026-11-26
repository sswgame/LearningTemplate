/**
 * @file CharacterBodyDynamicsComponentWatches.cpp
 * @brief `CharacterBodyDynamicsComponent` 의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Component/FacialAnimationComponent.h"
#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Facial/WrinkleMap.h"
#include "Engine/Animation/Rig/RigJiggleNode.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/SurfaceState.h"
#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/VFX/VFXComponent.h"
#include "Engine/Visual/Component/DirectionalLightComponent.h"
#include "Engine/Visual/Component/LightComponent.h"

#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/BodyProfile.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterBodyDynamicsComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterSurfaceBinding.h"

namespace sw
{
    namespace
    {
        struct CharacterBodyDynamicsDevInternal
        {
            static const CharacterBodyDynamicsComponent* findTarget( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return query.findTargetComponent<CharacterBodyDynamicsComponent>( "CharacterBodyDynamicsComponent", outValue );
            }

            template <float32 ( CharacterBodyDynamicsComponent::*Getter )() const>
            [[nodiscard]] static bool readValue( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterBodyDynamicsComponent* pFound = findTarget( query, outValue );
                if ( pFound == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( ( pFound->*Getter )() ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( bodyFootLift, "Body.FootLift", "Heel foot lift (m) the target's body dynamics hands to foot IK",
                               &CharacterBodyDynamicsDevInternal::readValue<&CharacterBodyDynamicsComponent::getFootLift>, true, false );
    SW_AUTOMATION_QUERY_WATCH( bodyPupilSize, "Body.PupilSize", "Pupil size (0..1) after light adaptation and emotion",
                               &CharacterBodyDynamicsDevInternal::readValue<&CharacterBodyDynamicsComponent::getPupilSize>, true, false );
    SW_AUTOMATION_QUERY_WATCH( bodyEyeLuminance, "Body.EyeLuminance", "Light measured at the eye (sun when unoccluded, ambient, point lights)",
                               &CharacterBodyDynamicsDevInternal::readValue<&CharacterBodyDynamicsComponent::getEyeLuminance>, true, false );
    SW_AUTOMATION_QUERY_WATCH( bodyJiggle, "Body.JiggleDisplacement", "Largest distance (m) a jiggle tip is away from its animated pose",
                               &CharacterBodyDynamicsDevInternal::readValue<&CharacterBodyDynamicsComponent::computeJiggleDisplacement>, true, false );
    SW_AUTOMATION_QUERY_WATCH( bodyFluidWetness, "Body.FluidWetness", "Largest tear / saliva mask value on the target",
                               &CharacterBodyDynamicsDevInternal::readValue<&CharacterBodyDynamicsComponent::computeFluidWetness>, true, false );
    SW_AUTOMATION_QUERY_WATCH( bodySmearDilution, "Body.SmearDilution", "Largest makeup smear dilution (0..1) on the target",
                               &CharacterBodyDynamicsDevInternal::readValue<&CharacterBodyDynamicsComponent::computeMaxSmearDilution>, true, false );
    SW_AUTOMATION_QUERY_WATCH( bodyFluidEffects, "Body.FluidEffects", "Tear drip / saliva strand effects emitting on the target",
                               &CharacterBodyDynamicsDevInternal::readValue<&CharacterBodyDynamicsComponent::computeActiveFluidEffects>, true, false );
} // namespace sw
