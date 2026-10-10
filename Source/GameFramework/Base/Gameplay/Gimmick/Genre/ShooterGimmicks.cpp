#include "pch.h"

#include "GameFramework/Base/Gameplay/Gimmick/Genre/ShooterGimmicks.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/FractureComponentBase.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Base/Gameplay/Gimmick/Genre/GenreGimmickUtil.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickDamageUtil.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickSensorComponent.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractionSelector.h"

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

            /** @brief 오브젝트의 파괴 컴포넌트입니다(파쇄 데이터가 있든 없든). 없으면 nullptr 입니다. */
            static FractureComponentBase* findFracture( GameObject& object ) { return object.getComponent<FractureComponentBase>(); }

            /** @brief 오브젝트를 그 경계 구 크기의 폭발로 중심에서 깹니다(조각 깎기 · 통째로 부수기). */
            static void breakAtCenter( FractureComponentBase& fracture, const float3& center, float32 strain, float32 impulse )
            {
                const FractureAsset* pAsset = fracture.findAsset();
                const float32        radius = pAsset != nullptr ? float3::getDistance( pAsset->_boundsMin, pAsset->_boundsMax ) : 2.0f;
                fracture.applyRadialDamageAtWorld( center, radius, strain, impulse );
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
        , _fuseTime{ 0.0f }
        , _fractureStrain{ 300.0f }
        , _blastImpulse{ 400.0f }
        , _damageTaken{ 0.0f }
        , _bExploded{ false }
        , _fuseStepsLeft{ -1 }
        , _clock{ GenreGimmickUtil::makeClock() }
    {
    }

    void ExplosiveBarrelComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        const int32 stepCount = _clock.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepOnce();
        }
    }

    void ExplosiveBarrelComponent::stepOnce()
    {
        GameObject*             pOwner  = getOwner();
        GimmickSensorComponent* pSensor = pOwner != nullptr ? pOwner->getComponent<GimmickSensorComponent>() : nullptr;
        if ( pOwner == nullptr || _bExploded )
            return;
        if ( _fuseTime > 0.0f )
        {
            if ( _fuseStepsLeft < 0 )
                _fuseStepsLeft = GenreGimmickUtil::toSteps( _fuseTime, 1 );
            if ( --_fuseStepsLeft <= 0 )
            {
                explode( *pOwner );
                return;
            }
        }
        if ( pSensor == nullptr )
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
        // 파괴 컴포넌트가 있으면 몸을 끄지 않는다 — 조각으로 부서져 날아간다(조각 그림 · 바디가 그 몸이다).
        FractureComponentBase* pSelfFracture = ShooterGimmicksInternal::findFracture( owner );
        if ( pSelfFracture != nullptr && pSelfFracture->hasFractureData() )
            ShooterGimmicksInternal::breakAtCenter( *pSelfFracture, center, _fractureStrain * 2.0f, _blastImpulse );
        else
            GenreGimmickUtil::setBodyActive( owner, false, this );
        if ( pManager == nullptr || _radius <= 0.0f )
            return;
        // 반경 안의 것에 거리 감쇠 피해 — 다른 드럼통은 자기 걸음에 터진다(사슬). 파괴 오브젝트는 경계가 반경에 닿으면 자리 있는 폭발로 깬다.
        const uint64      selfID         = owner.getObjectID();
        const float32     radius         = _radius;
        const float32     damage         = _damage;
        const float32     fractureStrain = _fractureStrain;
        const float32     blastImpulse   = _blastImpulse;
        const GameObject* pSource        = &owner;
        pManager->forEachGameObject( [selfID, radius, damage, fractureStrain, blastImpulse, center, pSource]( GameObject* pObject )
        {
            const SceneComponent* pOther = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pOther == nullptr || pObject->getObjectID() == selfID )
                return;
            FractureComponentBase* pFracture = ShooterGimmicksInternal::findFracture( *pObject );
            if ( pFracture != nullptr && pFracture->isReachedBy( center, radius ) )
                pFracture->applyRadialDamageAtWorld( center, radius, fractureStrain, blastImpulse );
            const float32 distance = float3::getDistance( pOther->getWorldPosition(), center );
            if ( distance <= radius )
                GimmickDamageUtil::applyDamage( *pObject, pSource, hashed_string( "Explosion" ), damage * ( 1.0f - distance / radius ) );
        } );
    }

    TurretComponent::TurretComponent()
        : _targetTags{}
        , _range{ 15.0f }
        , _viewAngle{ 0.0f }
        , _turnSpeed{ MathUtil::kPi }
        , _aimTolerance{ 5.0f * MathUtil::kDegreeToRadian }
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
        {
            stepOnce();
        }
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
        viewer._objectID = pOwner->getObjectID();
        vector<InteractionCandidate> listCandidate;
        const TagContainer&          targetTags = _targetTags;
        const float32                range      = _range;
        const float32                viewAngle  = _viewAngle;
        pManager->forEachGameObject( [&listCandidate, &targetTags, range, viewAngle]( GameObject* pObject )
        {
            const SceneComponent* pOther = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pOther == nullptr || pObject->isActiveInHierarchy() == false || targetTags.getTagCount() == 0 || pObject->getTags().hasAllTags( targetTags ) == false )
                return;
            InteractionCandidate candidate;
            candidate._position    = pOther->getWorldPosition();
            candidate._objectID    = pObject->getObjectID();
            candidate._maxDistance = range;
            candidate._maxAngle    = viewAngle;
            listCandidate.push_back( candidate );
        } );
        const WorldLineOfSightQuery lineOfSight{ *pManager };
        const int32                 pick = InteractionSelector::selectBest( viewer, listCandidate, &lineOfSight );
        _target                          = pick >= 0 ? GameObjectHandle::make( listCandidate[static_cast<size_t>( pick )]._objectID ) : GameObjectHandle{};
        if ( pick < 0 )
            return;

        // 조준 — 요를 목표 쪽으로 돌린다.
        const float3  toTarget   = listCandidate[static_cast<size_t>( pick )]._position - origin;
        const float32 desiredYaw = MathUtil::atan2( toTarget._x, toTarget._z );
        float3        rotation   = pScene->getLocalRotation();
        const float32 delta      = MathUtil::wrapAngle( desiredYaw - rotation._y );
        const float32 maxTurn    = _turnSpeed * GenreGimmickUtil::kStepTime;
        rotation._y += MathUtil::clamp( delta, -maxTurn, maxTurn );
        pScene->setLocalRotation( rotation );

        const bool bAimed = MathUtil::abs( delta ) <= _aimTolerance + maxTurn;
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
        , _stageStrain{ 80.0f }
        , _shatterStrain{ 600.0f }
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
        {
            stepOnce();
        }
    }

    void DestructibleComponent::stepOnce()
    {
        GameObject*             pOwner  = getOwner();
        GimmickSensorComponent* pSensor = pOwner != nullptr ? pOwner->getComponent<GimmickSensorComponent>() : nullptr;
        if ( pSensor == nullptr || isDestroyed() )
            return;
        _damageTaken += pSensor->consumeDamage();
        const int32            stageCount = static_cast<int32>( _listStageThreshold.size() );
        FractureComponentBase* pFracture  = ShooterGimmicksInternal::findFracture( *pOwner );
        const bool             bFracture  = pFracture != nullptr && pFracture->hasFractureData();
        while ( _stage < stageCount && _damageTaken >= _listStageThreshold[static_cast<size_t>( _stage )] )
        {
            ++_stage;
            const SceneComponent* pScene   = pOwner->getPrimarySceneComponent();
            const float3          position = pScene != nullptr ? pScene->getWorldPosition() : float3{};
            ShooterGimmicksInternal::sendCue( *pOwner, hashed_string( _stage >= stageCount ? "Destroyed" : "Stage" ), position, static_cast<float32>( _stage ) );
            if ( bFracture == false )
                continue;
            // 단계마다 조각을 깎고, 마지막 단계는 통째로 부순다.
            const float32 strain = _stage >= stageCount ? _shatterStrain : _stageStrain * static_cast<float32>( _stage ) / static_cast<float32>( stageCount );
            ShooterGimmicksInternal::breakAtCenter( *pFracture, position, strain, strain * 0.5f );
        }
        pSensor->setSignal( static_cast<float32>( _stage ) );
        if ( isDestroyed() && bFracture == false )
            GenreGimmickUtil::setBodyActive( *pOwner, false, this );
    }
} // namespace sw
