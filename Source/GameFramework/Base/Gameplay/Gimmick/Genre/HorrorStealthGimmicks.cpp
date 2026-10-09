#include "pch.h"

#include "GameFramework/Base/Gameplay/Gimmick/Genre/HorrorStealthGimmicks.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

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

            static TagID getHiddenTag() { return TagID::request( "State.Hidden" ); }
            static TagID getHideActivityTag() { return TagID::request( "Activity.Hide" ); }

            /** @brief 빛까지 막는 것이 있는가입니다(가림을 볼 때만). */
            static bool isOccluded( const GameObjectManager& manager, const float3& position, const float3& lightPosition, uint64 ignoreObjectId, uint64 lightObjectId )
            {
                return WorldQuery::hasLineOfSight( manager, position, lightPosition, ignoreObjectId, lightObjectId ) == false;
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
        const bool  bTagged = overlap._pOther != nullptr && ( _requiredTags.getTagCount() == 0 || overlap._pOther->getTags().hasAllTags( _requiredTags ) );
        const bool  bReady  = ( _bOnce == false || _fireCount == 0 ) && _cooldownLeft <= 0.0f;
        GameObject* pOwner  = getOwner();
        if ( bTagged == false || bReady == false || pOwner == nullptr )
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
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        LightComponent*    pLight   = pOwner != nullptr ? pOwner->getComponent<LightComponent>() : nullptr;
        const float32      scale    = computeScaleAtStep( _step );
        if ( pLight == nullptr || pManager == nullptr || scale == _appliedScale )
            return;
        _appliedScale                = scale;
        const ComponentHandle light  = pLight->getHandle();
        const float32         target = _baseIntensity * scale;
        pManager->executeOrDeferPostTick( [pManager, light, target]()
        {
            LightComponent* pResolved = static_cast<LightComponent*>( pManager->resolveComponent( light ) );
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

    bool HidingSpotComponent::isHidden( const GameObject& object ) { return object.hasTag( HorrorStealthGimmicksInternal::getHiddenTag(), true ); }

    void HidingSpotComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*             pOwner   = getOwner();
        GameObjectManager*      pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        GimmickSensorComponent* pSensor  = pOwner != nullptr ? pOwner->getComponent<GimmickSensorComponent>() : nullptr;
        if ( pManager == nullptr || pSensor == nullptr || pSensor->consumeUses() <= 0.0f )
            return;
        const InteractableComponent* pInteractable = pOwner->getComponent<InteractableComponent>();
        GameObject*                  pWho          = pInteractable != nullptr ? pManager->resolveGameObject( pInteractable->getLastInteractor() ) : nullptr;
        if ( pWho == nullptr )
            return;
        // 숨은 사람이 다시 쓰면 나오고, 아니면 들어간다.
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

    float32 LightExposure::computeExposure( const GameObjectManager& manager, const float3& position, bool bOcclusion, uint64 ignoreObjectId )
    {
        using Internal   = HorrorStealthGimmicksInternal;
        float32 exposure = 0.0f;
        manager.forEachGameObject( [&manager, &exposure, &position, bOcclusion, ignoreObjectId]( GameObject* pObject )
        {
            if ( pObject == nullptr || pObject->isActiveInHierarchy() == false )
                return;
            const DirectionalLightComponent* pSun = pObject->getComponent<DirectionalLightComponent>();
            if ( pSun != nullptr && pSun->isActive() )
            {
                const float3 toward = position - pSun->getLightDirection().normalize() * Internal::kSunDistance;
                if ( bOcclusion == false || Internal::isOccluded( manager, position, toward, ignoreObjectId, pObject->getObjectId() ) == false )
                    exposure += pSun->getIntensity();
            }
            const PointLightComponent* pPoint = pObject->getComponent<PointLightComponent>();
            if ( pPoint != nullptr && pPoint->isActive() && pPoint->getRadius() > 0.0f )
            {
                const float32 distance = float3::getDistance( position, pPoint->getLightPosition() );
                const float32 falloff  = MathUtil::max( 0.0f, 1.0f - distance / pPoint->getRadius() );
                const bool    bBlocked = bOcclusion && falloff > 0.0f && Internal::isOccluded( manager, position, pPoint->getLightPosition(), ignoreObjectId, pObject->getObjectId() );
                if ( bBlocked == false )
                    exposure += pPoint->getIntensity() * falloff * falloff;
            }
            const SpotLightComponent* pSpot = pObject->getComponent<SpotLightComponent>();
            if ( pSpot != nullptr && pSpot->isActive() && pSpot->getRadius() > 0.0f )
            {
                const float3  offset   = position - pSpot->getLightPosition();
                const float32 distance = offset.getLength();
                const float32 falloff  = MathUtil::max( 0.0f, 1.0f - distance / pSpot->getRadius() );
                const bool    bInCone  = distance <= 1.0e-4f || pSpot->getLightDirection().normalize().dot( offset / distance ) >= MathUtil::cos( pSpot->getOuterConeAngle() );
                const bool    bBlocked = bOcclusion && falloff > 0.0f && Internal::isOccluded( manager, position, pSpot->getLightPosition(), ignoreObjectId, pObject->getObjectId() );
                if ( bInCone && bBlocked == false )
                    exposure += pSpot->getIntensity() * falloff * falloff;
            }
        } );
        return exposure;
    }
} // namespace sw
