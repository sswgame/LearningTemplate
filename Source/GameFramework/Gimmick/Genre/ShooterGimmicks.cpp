#include "pch.h"

#include "GameFramework/Gimmick/Genre/ShooterGimmicks.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Framework/GameEventUtil.h"
#include "GameFramework/Gimmick/Genre/GenreGimmickUtil.h"
#include "GameFramework/Gimmick/GimmickDamageUtil.h"
#include "GameFramework/Gimmick/GimmickSensorComponent.h"
#include "GameFramework/Interaction/InteractionSelector.h"

namespace sw
{
    namespace
    {
        struct ShooterGimmicksInternal
        {
            static void sendCue( const GameObject& source, const hashed_string& cue, const float3& position, float32 magnitude )
            {
                GimmickCueEvent event;
                event._source    = source.getHandle();
                event._cue       = cue;
                event._position  = position;
                event._magnitude = magnitude;
                GameEventUtil::send( event );
            }

            /** @brief 각을 (−π, π] 로 감습니다. */
            static float32 wrapAngle( float32 radians )
            {
                float32 wrapped = MathUtil::fmod( radians + MathUtil::Pi, MathUtil::Pi * 2.0f );
                if ( wrapped < 0.0f )
                    wrapped += MathUtil::Pi * 2.0f;
                return wrapped - MathUtil::Pi;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ExplosiveBarrelComponent::ExplosiveBarrelComponent()
        : _health{ 30.0f }
        , _radius{ 4.0f }
        , _damage{ 80.0f }
        , _damageTaken{ 0.0f }
        , _bExploded{ false }
        , _clock{ GenreGimmickUtil::makeClock() }
    {
    }

    void ExplosiveBarrelComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        const int32 stepCount = _clock.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            stepOnce();
    }

    void ExplosiveBarrelComponent::stepOnce()
    {
        GameObject*             pOwner  = getOwner();
        GimmickSensorComponent* pSensor = pOwner != nullptr ? pOwner->getComponent<GimmickSensorComponent>() : nullptr;
        if ( pSensor == nullptr || _bExploded )
            return;
        _damageTaken += pSensor->consumeDamage();
        if ( _damageTaken >= _health )
            explode( *pOwner );
    }

    void ExplosiveBarrelComponent::explode( GameObject& owner )
    {
        _bExploded                     = true;
        GameObjectManager*    pManager = owner.getManager();
        const SceneComponent* pScene   = owner.getPrimarySceneComponent();
        const float3          center   = pScene != nullptr ? pScene->getWorldPosition() : float3{};
        ShooterGimmicksInternal::sendCue( owner, hashed_string( "Explosion" ), center, _damage );
        GenreGimmickUtil::setBodyActive( owner, false, this );
        if ( pManager == nullptr || _radius <= 0.0f )
            return;
        // 반경 안의 것에 거리 감쇠 피해 — 다른 드럼통은 자기 걸음에 터진다(사슬).
        const uint64      selfId  = owner.getObjectId();
        const float32     radius  = _radius;
        const float32     damage  = _damage;
        const GameObject* pSource = &owner;
        pManager->forEachGameObject( [selfId, radius, damage, center, pSource]( GameObject* pObject )
        {
            const SceneComponent* pOther = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pOther == nullptr || pObject->getObjectId() == selfId )
                return;
            const float32 distance = float3::getDistance( pOther->getWorldPosition(), center );
            if ( distance <= radius )
                GimmickDamageUtil::applyDamage( *pObject, pSource, hashed_string( "Explosion" ), damage * ( 1.0f - distance / radius ) );
        } );
    }

    TurretComponent::TurretComponent()
        : _targetTags{}
        , _range{ 15.0f }
        , _viewAngle{ 0.0f }
        , _turnSpeed{ 180.0f }
        , _aimTolerance{ 5.0f }
        , _fireInterval{ 0.5f }
        , _damage{ 10.0f }
        , _target{}
        , _cooldownSteps{ 0 }
        , _shotCount{ 0 }
        , _clock{ GenreGimmickUtil::makeClock() }
    {
    }

    void TurretComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        const int32 stepCount = _clock.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            stepOnce();
    }

    void TurretComponent::stepOnce()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        SceneComponent*    pScene   = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pManager == nullptr || pScene == nullptr )
            return;
        if ( _cooldownSteps > 0 )
            --_cooldownSteps;

        // 고르기 — 태그를 가진 것들을 후보로, 상호작용과 같은 고르기(거리 · 시야각 · 시야).
        const float3      origin = pScene->getWorldPosition();
        InteractionViewer viewer;
        viewer._position = origin;
        viewer._forward  = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, pScene->getWorldMatrix() );
        viewer._objectId = pOwner->getObjectId();
        vector<InteractionCandidate> listCandidate;
        const TagContainer&          targetTags = _targetTags;
        const float32                range      = _range;
        const float32                viewAngle  = _viewAngle * MathUtil::DegreeToRadian;
        pManager->forEachGameObject( [&listCandidate, &targetTags, range, viewAngle]( GameObject* pObject )
        {
            const SceneComponent* pOther = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pOther == nullptr || pObject->isActiveInHierarchy() == false || targetTags.getTagCount() == 0 || pObject->getTags().hasAllTags( targetTags ) == false )
                return;
            InteractionCandidate candidate;
            candidate._position    = pOther->getWorldPosition();
            candidate._objectId    = pObject->getObjectId();
            candidate._maxDistance = range;
            candidate._maxAngle    = viewAngle;
            listCandidate.push_back( candidate );
        } );
        const WorldLineOfSightQuery lineOfSight{ *pManager };
        const int32                 pick = InteractionSelector::selectBest( viewer, listCandidate, &lineOfSight );
        _target                          = pick >= 0 ? GameObjectHandle::make( listCandidate[static_cast<size_t>( pick )]._objectId ) : GameObjectHandle{};
        if ( pick < 0 )
            return;

        // 조준 — 요를 목표 쪽으로 돌린다.
        const float3  toTarget   = listCandidate[static_cast<size_t>( pick )]._position - origin;
        const float32 desiredYaw = MathUtil::atan2( toTarget._x, toTarget._z );
        float3        rotation   = pScene->getLocalRotation();
        const float32 delta      = ShooterGimmicksInternal::wrapAngle( desiredYaw - rotation._y );
        const float32 maxTurn    = _turnSpeed * MathUtil::DegreeToRadian * GenreGimmickUtil::kStepTime;
        rotation._y += MathUtil::clamp( delta, -maxTurn, maxTurn );
        pScene->setLocalRotation( rotation );

        const bool bAimed = MathUtil::abs( delta ) <= _aimTolerance * MathUtil::DegreeToRadian + maxTurn;
        if ( bAimed == false || _cooldownSteps > 0 )
            return;
        _cooldownSteps      = GenreGimmickUtil::toSteps( _fireInterval, 1 );
        GameObject* pVictim = pManager->resolveGameObject( _target );
        if ( pVictim == nullptr )
            return;
        ++_shotCount;
        GimmickDamageUtil::applyDamage( *pVictim, pOwner, hashed_string( "Turret" ), _damage );
    }

    DestructibleComponent::DestructibleComponent()
        : _listStageThreshold{ 20.0f, 50.0f }
        , _damageTaken{ 0.0f }
        , _stage{ 0 }
        , _clock{ GenreGimmickUtil::makeClock() }
    {
    }

    void DestructibleComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        const int32 stepCount = _clock.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            stepOnce();
    }

    void DestructibleComponent::stepOnce()
    {
        GameObject*             pOwner  = getOwner();
        GimmickSensorComponent* pSensor = pOwner != nullptr ? pOwner->getComponent<GimmickSensorComponent>() : nullptr;
        if ( pSensor == nullptr || isDestroyed() )
            return;
        _damageTaken += pSensor->consumeDamage();
        const int32 stageCount = static_cast<int32>( _listStageThreshold.size() );
        while ( _stage < stageCount && _damageTaken >= _listStageThreshold[static_cast<size_t>( _stage )] )
        {
            ++_stage;
            const SceneComponent* pScene   = pOwner->getPrimarySceneComponent();
            const float3          position = pScene != nullptr ? pScene->getWorldPosition() : float3{};
            ShooterGimmicksInternal::sendCue( *pOwner, hashed_string( _stage >= stageCount ? "Destroyed" : "Stage" ), position, static_cast<float32>( _stage ) );
        }
        pSensor->setSignal( static_cast<float32>( _stage ) );
        if ( isDestroyed() )
            GenreGimmickUtil::setBodyActive( *pOwner, false, this );
    }
} // namespace sw
