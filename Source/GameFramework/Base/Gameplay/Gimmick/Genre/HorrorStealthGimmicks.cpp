#include "pch.h"

#include "GameFramework/Base/Gameplay/Gimmick/Genre/HorrorStealthGimmicks.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/PointLightComponent.h"
#include "Engine/Object/Component/3D/SpotLightComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/GameSound.h"
#include "GameFramework/Base/Gameplay/Gimmick/Genre/GenreGimmickUtil.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickSensorComponent.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Base/Gameplay/Interaction/SmartObjectComponent.h"
#include "GameFramework/Base/World/Query/WorldQuery.h"

namespace sw
{
    namespace
    {
        struct HorrorStealthGimmicksInternal
        {
            static constexpr float32 kSunDistance = 100.0f;

            // 리터럴 태그다 — 스텝마다 부르는 곳에서 문자열을 intern 하지 않는다.
            static constexpr TagID getHiddenTag() { return "State.Hidden"_tag; }
            static constexpr TagID getHideActivityTag() { return "Activity.Hide"_tag; }

            /** @brief 라이트까지 막는 것이 있으면 true 입니다. 가림을 검사할 때만 씁니다. */
            static bool isOccluded( const GameObjectManager& manager, const float3& position, const float3& lightPosition, uint64 ignoreObjectID, uint64 lightObjectID )
            {
                return WorldQuery::hasLineOfSight( manager, position, lightPosition, ignoreObjectID, lightObjectID ) == false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ScareTriggerComponent::ScareTriggerComponent()
        : _cue{ "Scare" }
        , _sound{}
        , _requiredTags{}
        , _bOnce{ true }
        , _cooldown{ 10.0f }
        , _cooldownLeft{ 0.0f }
        , _fireCount{ 0 }
    {
        _requiredTags.addTag( TagID::request( "Player" ) );
    }

    void ScareTriggerComponent::onOverlapBegin( const OverlapInfo& overlap )
    {
        Component::onOverlapBegin( overlap );
        const bool  bTagged     = overlap._pOther != nullptr && ( _requiredTags.getTagCount() == 0 || overlap._pOther->getTags().hasAllTags( _requiredTags ) );
        const bool  bReady      = ( _bOnce == false || _fireCount == 0 ) && _cooldownLeft <= 0.0f;
        GameObject* pOwner      = getOwner();
        const bool  bCannotFire = bTagged == false || bReady == false || pOwner == nullptr;
        if ( bCannotFire )
            return;
        ++_fireCount;
        _cooldownLeft = _cooldown;
        GimmickCueEvent event;
        event._source = pOwner->getHandle();
        event._target = overlap._pOther->getHandle();
        event._cue    = _cue;
        if ( pOwner->getPrimarySceneComponent() != nullptr )
            event._position = pOwner->getPrimarySceneComponent()->getWorldPosition();
        GameEventUtil::send( event );
        if ( _sound.empty() == false )
            (void)GameSound::play( _sound );
    }

    void ScareTriggerComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _cooldownLeft > 0.0f )
            _cooldownLeft = MathUtil::max( 0.0f, _cooldownLeft - deltaTime );
    }

    FlickerLightComponent::FlickerLightComponent()
        : _pattern{ "mmamammmmammamamaaamammma" }
        , _rate{ 10.0f }
        , _step{ 0 }
        , _clock{ GenreGimmickUtil::makeClock() }
        , _baseIntensity{ 1.0f }
        , _appliedScale{ -1.0f }
    {
    }

    void FlickerLightComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        const GameObject*     pOwner = getOwner();
        const LightComponent* pLight = pOwner != nullptr ? pOwner->getComponent<LightComponent>() : nullptr;
        if ( pLight != nullptr )
            _baseIntensity = pLight->getIntensity();
    }

    float32 FlickerLightComponent::computeScaleAtStep( int32 step ) const
    {
        if ( _pattern.empty() )
            return 1.0f;
        const int32 letterIndex = static_cast<int32>( static_cast<float32>( step ) * GenreGimmickUtil::kStepTime * _rate ) % static_cast<int32>( _pattern.size() );
        const utf8  letter      = StringUtil::toLowerChar( _pattern[static_cast<size_t>( letterIndex )] );
        if ( letter < 'a' || 'z' < letter )
            return 1.0f;
        return static_cast<float32>( letter - 'a' ) / static_cast<float32>( 'm' - 'a' );
    }

    void FlickerLightComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        _step += _clock.consume( deltaTime );
        GameObject*        pOwner               = getOwner();
        GameObjectManager* pManager             = pOwner != nullptr ? pOwner->getManager() : nullptr;
        LightComponent*    pLight               = pOwner != nullptr ? pOwner->getComponent<LightComponent>() : nullptr;
        const float32      scale                = computeScaleAtStep( _step );
        const bool         bNoLightUpdateNeeded = pLight == nullptr || pManager == nullptr || scale == _appliedScale;
        if ( bNoLightUpdateNeeded )
            return;
        _appliedScale = scale;
        const TComponentHandle<LightComponent> light{ pLight };
        const float32                          target = _baseIntensity * scale;
        pManager->executeOrDeferPostTick( [pManager, light, target]()
        {
            LightComponent* pResolved = pManager->resolveComponent( light );
            if ( pResolved != nullptr )
                pResolved->setIntensity( target );
        } );
    }

    HidingSpotComponent::HidingSpotComponent() = default;

    bool HidingSpotComponent::enter( GameObject& who )
    {
        GameObject*           pOwner = getOwner();
        SmartObjectComponent* pSmart = pOwner != nullptr ? pOwner->getComponent<SmartObjectComponent>() : nullptr;
        if ( pSmart == nullptr )
            return false;
        TagContainer hide;
        hide.addTag( HorrorStealthGimmicksInternal::getHideActivityTag() );
        if ( pSmart->claimFreeSlot( who, hide ) < 0 )
            return false;
        who.addTag( HorrorStealthGimmicksInternal::getHiddenTag() );
        return true;
    }

    bool HidingSpotComponent::exit( GameObject& who )
    {
        GameObject*           pOwner = getOwner();
        SmartObjectComponent* pSmart = pOwner != nullptr ? pOwner->getComponent<SmartObjectComponent>() : nullptr;
        if ( pSmart == nullptr || pSmart->releaseSlot( who ) == false )
            return false;
        who.removeTag( HorrorStealthGimmicksInternal::getHiddenTag() );
        return true;
    }

    bool HidingSpotComponent::isHidden( const GameObject& object )
    {
        return object.hasTag( HorrorStealthGimmicksInternal::getHiddenTag(), true );
    }

    void HidingSpotComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*             pOwner           = getOwner();
        GameObjectManager*      pManager         = pOwner != nullptr ? pOwner->getManager() : nullptr;
        GimmickSensorComponent* pSensor          = pOwner != nullptr ? pOwner->getComponent<GimmickSensorComponent>() : nullptr;
        const bool              bNoUsesAvailable = pManager == nullptr || pSensor == nullptr || pSensor->consumeUses() <= 0.0f;
        if ( bNoUsesAvailable )
            return;
        const InteractableComponent* pInteractable = pOwner->getComponent<InteractableComponent>();
        GameObject*                  pWho          = pInteractable != nullptr ? pManager->resolveGameObject( pInteractable->getLastInteractor() ) : nullptr;
        if ( pWho == nullptr )
            return;
        // 숨은 쪽이 다시 사용하면 나오고, 아니면 들어간다.
        if ( exit( *pWho ) == false )
            (void)enter( *pWho );
    }

    NoiseEmitterComponent::NoiseEmitterComponent()
        : _radius{ 8.0f }
        , _interval{ 0.0f }
        , _bOnStep{ true }
        , _noiseRadius{ 0.0f }
        , _steps{ 0 }
        , _lastOccupantCount{ 0 }
        , _emitCount{ 0 }
        , _clock{ GenreGimmickUtil::makeClock() }
    {
    }

    void NoiseEmitterComponent::emit( float32 radius )
    {
        _noiseRadius       = MathUtil::max( _noiseRadius, radius );
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        ++_emitCount;
        GimmickNoiseEvent event;
        event._source = pOwner->getHandle();
        event._radius = radius;
        if ( pOwner->getPrimarySceneComponent() != nullptr )
            event._position = pOwner->getPrimarySceneComponent()->getWorldPosition();
        GameEventUtil::send( event );
    }

    void NoiseEmitterComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        const int32 stepCount = _clock.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepOnce();
        }
    }

    void NoiseEmitterComponent::stepOnce()
    {
        _noiseRadius = 0.0f;
        ++_steps;
        if ( _interval > 0.0f && _steps % GenreGimmickUtil::toSteps( _interval, 1 ) == 0 )
            emit( _radius );
        const GameObject*             pOwner  = getOwner();
        const GimmickSensorComponent* pSensor = pOwner != nullptr ? pOwner->getComponent<GimmickSensorComponent>() : nullptr;
        const int32                   count   = pSensor != nullptr ? pSensor->getOccupantCount() : 0;
        if ( _bOnStep && count > _lastOccupantCount )
            emit( _radius );
        _lastOccupantCount = count;
    }

    float32 LightExposure::computeExposure( const GameObjectManager& manager, const float3& position, bool bOcclusion, uint64 ignoreObjectID )
    {
        using Internal   = HorrorStealthGimmicksInternal;
        float32 exposure = 0.0f;
        // 빛은 씬의 컴포넌트 레지스트리가 종류별 칸으로 든다(`LightComponent::onRegister`). 씬을 훑지 않고 오브젝트마다 컴포넌트를 세 번 찾지 않는다.
        // 칸 번호가 곧 빛 종류라 그 칸의 항목은 그 타입이다. 활성(`isActive`)은 소유 오브젝트의 계층 활성을 함께 본다.
        const ComponentRegistry& registry = manager.getComponentRegistry();
        for ( const LightComponent* pLight : registry.getAll<LightComponent>( shaderslot::kLightTypeDirectional ) )
        {
            const DirectionalLightComponent* pSun = static_cast<const DirectionalLightComponent*>( pLight );
            if ( pSun->isPendingDestroy() || pSun->isActive() == false )
                continue;
            const float3 toward = position - pSun->getLightDirection().normalize() * Internal::kSunDistance;
            if ( bOcclusion == false || Internal::isOccluded( manager, position, toward, ignoreObjectID, pSun->getOwner()->getObjectID() ) == false )
                exposure += pSun->getIntensity();
        }
        for ( const LightComponent* pLight : registry.getAll<LightComponent>( shaderslot::kLightTypePoint ) )
        {
            const PointLightComponent* pPoint            = static_cast<const PointLightComponent*>( pLight );
            const bool                 bPointLightActive = pPoint->isPendingDestroy() == false && pPoint->isActive() && pPoint->getRadius() > 0.0f;
            if ( bPointLightActive == false )
                continue;
            const float32 distance = float3::getDistance( position, pPoint->getLightPosition() );
            const float32 falloff  = MathUtil::max( 0.0f, 1.0f - distance / pPoint->getRadius() );
            const bool    bBlocked = bOcclusion && falloff > 0.0f && Internal::isOccluded( manager, position, pPoint->getLightPosition(), ignoreObjectID, pPoint->getOwner()->getObjectID() );
            if ( bBlocked == false )
                exposure += pPoint->getIntensity() * falloff * falloff;
        }
        for ( const LightComponent* pLight : registry.getAll<LightComponent>( shaderslot::kLightTypeSpot ) )
        {
            const SpotLightComponent* pSpot            = static_cast<const SpotLightComponent*>( pLight );
            const bool                bSpotLightActive = pSpot->isPendingDestroy() == false && pSpot->isActive() && pSpot->getRadius() > 0.0f;
            if ( bSpotLightActive == false )
                continue;
            const float3  offset   = position - pSpot->getLightPosition();
            const float32 distance = offset.getLength();
            const float32 falloff  = MathUtil::max( 0.0f, 1.0f - distance / pSpot->getRadius() );
            const bool    bInCone  = distance <= 1.0e-4f || pSpot->getLightDirection().normalize().dot( offset / distance ) >= MathUtil::cos( pSpot->getOuterConeAngle() );
            const bool    bBlocked = bOcclusion && falloff > 0.0f && Internal::isOccluded( manager, position, pSpot->getLightPosition(), ignoreObjectID, pSpot->getOwner()->getObjectID() );
            if ( bInCone && bBlocked == false )
                exposure += pSpot->getIntensity() * falloff * falloff;
        }
        return exposure;
    }
} // namespace sw
