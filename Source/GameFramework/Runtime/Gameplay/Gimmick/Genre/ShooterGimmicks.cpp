#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Genre/ShooterGimmicks.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/Ray.h"

#include "Engine/Destruction/Fracture/FractureAsset.h"
#include "Engine/Destruction/Fracture/FractureComponentBase.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/PhysicsSystem.h"

#include "GameFramework/Runtime/Character/Combat/Projectile/ResolvedShot.h"
#include "GameFramework/Runtime/Character/Combat/Weapon/Ballistics.h"
#include "GameFramework/Runtime/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Genre/GenreGimmickUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickDamageUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickSensorComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionView.h"
#include "GameFramework/Runtime/World/Query/WorldQuery.h"

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

            /** @brief 오브젝트의 파괴 컴포넌트입니다. 파쇄 데이터가 있든 없든 반환하며, 컴포넌트가 없으면 nullptr 입니다. */
            static FractureComponentBase* findFracture( GameObject& object ) { return object.getComponent<FractureComponentBase>(); }

            /** @brief 오브젝트를 그 경계 구 크기의 폭발로 중심에서 깹니다. 조각을 깎아 내거나 통째로 부술 때 씁니다. */
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
        , _blastCenter{}
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
        // 파괴 컴포넌트가 있으면 몸을 끄지 않는다. 조각으로 부서져 날아가며, 조각의 그래픽과 바디가 그 몸이다.
        FractureComponentBase* pSelfFracture = ShooterGimmicksInternal::findFracture( owner );
        if ( pSelfFracture != nullptr && pSelfFracture->hasFractureData() )
            ShooterGimmicksInternal::breakAtCenter( *pSelfFracture, center, _fractureStrain * 2.0f, _blastImpulse );
        else
            GenreGimmickUtil::setBodyActive( owner, false, this );
        if ( pManager == nullptr || _radius <= 0.0f )
            return;
        _blastCenter = center;
        pManager->executeOrDeferPostTickOnComponent( *this, []( ExplosiveBarrelComponent& self, GameObjectManager& manager )
        { self.applyRadialDamage( manager ); } );
    }

    void ExplosiveBarrelComponent::applyRadialDamage( GameObjectManager& manager ) const
    {
        const float3& center = _blastCenter;
        // 반경 안의 것에 거리 감쇠 피해를 준다. 다른 드럼통은 자기 스텝에 터지며 연쇄가 이어진다. 파괴 오브젝트는 경계가 반경에 닿으면 위치 기반 폭발로 깬다.
        // 대상이 "반경 안 무엇이든" 이라 등록 목록이 없다 — 폭발 순간 한 번 씬을 돈다(틱마다가 아니다, Config/Rules/Lint/TickSceneWalkExceptions.json).
        // 순회는 공유 잠금 안이라 고르기만 하고, 쓰기(조각 깨기 · 피해 이벤트)는 순회 뒤에 한다.
        struct BlastTarget
        {
            GameObject*            _pObject;
            FractureComponentBase* _pFracture; ///< 경계가 반경에 닿은 파괴 컴포넌트(없으면 nullptr)
            float32                _distance;
        };
        GameObject* const   pSource = getOwner();
        const uint64        selfID  = pSource != nullptr ? pSource->getObjectID() : 0;
        const float32       radius  = _radius;
        vector<BlastTarget> listTarget;
        manager.forEachGameObject( [selfID, radius, center, &listTarget]( GameObject* pObject )
        {
            const SceneComponent* pOther = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pOther == nullptr || pObject->getObjectID() == selfID )
                return;
            FractureComponentBase* pFracture = ShooterGimmicksInternal::findFracture( *pObject );
            const bool             bReached  = pFracture != nullptr && pFracture->isReachedBy( center, radius );
            const float32          distance  = float3::getDistance( pOther->getWorldPosition(), center );
            if ( bReached || distance <= radius )
                listTarget.push_back( BlastTarget{ pObject, bReached ? pFracture : nullptr, distance } );
        } );
        for ( const BlastTarget& target : listTarget )
        {
            if ( target._pFracture != nullptr )
                target._pFracture->applyRadialDamageAtWorld( center, radius, _fractureStrain, _blastImpulse );
            if ( target._distance <= radius )
                GimmickDamageUtil::applyDamage( *target._pObject, pSource, hashed_string( "Explosion" ), _damage * ( 1.0f - target._distance / radius ) );
        }
    }

    TurretShotWorld::TurretShotWorld( const vector<TurretShotTarget>& listTarget, float32 hitRadius, const IWorldQuery& blockers, uint64 ownerID )
        : _listTarget{ listTarget }
        , _blockers{ blockers }
        , _ownerID{ ownerID }
        , _hitRadius{ hitRadius }
    {
    }

    bool TurretShotWorld::findTargetPoint( const ProjectileState& /*projectile*/, float3& /*outPoint*/ ) const
    {
        return false; // 포탑 탄은 유도하지 않는다
    }

    bool TurretShotWorld::sweep( const ProjectileState& projectile, const float3& from, const float3& to, ProjectileHit& outHit ) const
    {
        const float3  path   = to - from;
        const float32 length = path.getLength();
        if ( length <= 1.0e-5f )
            return false;
        Ray ray;
        ray._origin    = from;
        ray._direction = path * ( 1.0f / length );
        float32 best   = length;
        bool    bFound = false;
        for ( size_t targetIndex = 0; targetIndex < _listTarget.size(); ++targetIndex )
        {
            float32 hit = 0.0f;
            if ( Ray::intersectSphere( ray, _listTarget[targetIndex]._position, _hitRadius + projectile._radius, best, hit ) == false )
                continue;
            best             = hit;
            bFound           = true;
            outHit._other    = static_cast<int32>( targetIndex );
            outHit._position = from + ray._direction * hit;
            outHit._kind     = ProjectileHitKind::Target;
        }
        // 비행 중에도 막는 것을 본다 — 쏜 뒤 생긴 벽 · 닫힌 문에 탄이 맞는다. 대상(태그를 가진 것)의 몸은 막지 않는다(그 대상은 구로 맞힌다).
        WorldRayHit  blocker;
        const float3 end = from + ray._direction * best;
        if ( _blockers.raycast( from, end, _ownerID, blocker ) == false || isTarget( blocker._objectID ) )
            return bFound;
        outHit._other    = constant::invalid_index::kInt32;
        outHit._position = blocker._point;
        outHit._normal   = ray._direction * -1.0f; // 질의가 법선을 주지 않는다 — 튕기지 않는 탄이라 쓰지 않는다
        outHit._kind     = ProjectileHitKind::Surface;
        return true;
    }

    bool TurretShotWorld::isTarget( uint64 objectID ) const
    {
        for ( const TurretShotTarget& target : _listTarget )
        {
            if ( objectID != 0 && target._objectID == objectID )
                return true;
        }
        return false;
    }

    TurretComponent::TurretComponent()
        : _targetTags{}
        , _range{ 15.0f }
        , _viewAngle{ 0.0f }
        , _turnSpeed{ MathUtil::kPi }
        , _aimTolerance{ 5.0f * MathUtil::kDegreeToRadian }
        , _fireInterval{ 0.5f }
        , _damage{ 10.0f }
        , _projectileSpeed{ 0.0f }
        , _projectileGravityScale{ 0.0f }
        , _hitRadius{ 0.6f }
        , _target{}
        , _cooldownSteps{ 0 }
        , _shotCount{ 0 }
        , _hitCount{ 0 }
        , _clock{ GenreGimmickUtil::makeClock() }
        , _listCandidate{}
        , _listTagged{}
        , _listShotTarget{}
        , _projectiles{}
        , _previousTarget{}
        , _previousTargetPosition{}
    {
    }

    bool TurretComponent::computeAim( const float3& origin, const float3& targetPosition, const float3& targetVelocity, float32 projectileSpeed, float32 gravityScale,
                                      float3& outAimPoint, float3& outDirection )
    {
        outAimPoint = targetPosition;
        if ( projectileSpeed > 0.0f && Ballistics::computeInterceptPoint( origin, targetPosition, targetVelocity, projectileSpeed, outAimPoint ) == false )
            return false;
        if ( projectileSpeed > 0.0f && gravityScale > 0.0f )
            return Ballistics::computeLaunchDirection( origin, outAimPoint, projectileSpeed, gravityScale, false, outDirection );
        const float3  toAim  = outAimPoint - origin;
        const float32 length = toAim.getLength();
        if ( length <= 1.0e-5f )
            return false;
        outDirection = toAim * ( 1.0f / length );
        return true;
    }

    void TurretComponent::stepProjectiles( GameObject& owner )
    {
        GameObjectManager* pManager = owner.getManager();
        if ( _projectiles.getProjectiles().empty() || pManager == nullptr )
            return;
        _listShotTarget.clear();
        for ( const GameObject* pObject : _listTagged )
        {
            const SceneComponent* pScene = pObject->getPrimarySceneComponent();
            if ( pScene != nullptr )
                _listShotTarget.push_back( TurretShotTarget{ pScene->getWorldPosition(), pObject->getObjectID() } );
        }
        const ScenePhysicsWorldQuery sceneQuery{ *pManager };
        const TurretShotWorld        world{ _listShotTarget, _hitRadius, WorldQuery::select( sceneQuery ), owner.getObjectID() };
        for ( const ProjectileEvent& event : _projectiles.step( constant::time::kDefaultFixedStepSeconds, world ) )
        {
            if ( event._kind != ProjectileEvent::Kind::Hit || event._hit._kind != ProjectileHitKind::Target )
                continue;
            GameObject* pVictim = pManager->resolveGameObject( GameObjectHandle::make( _listShotTarget[static_cast<size_t>( event._hit._other )]._objectID ) );
            if ( pVictim == nullptr )
                continue;
            ++_hitCount;
            GimmickDamageUtil::applyDamage( *pVictim, &owner, hashed_string( "Turret" ), event._projectile._damage );
        }
    }

    void TurretComponent::fireAt( GameObject& owner, GameObject& victim, const float3& origin, const float3& direction )
    {
        ++_shotCount;
        if ( _projectileSpeed <= 0.0f )
        {
            ++_hitCount;
            GimmickDamageUtil::applyDamage( victim, &owner, hashed_string( "Turret" ), _damage );
            return;
        }
        ResolvedShot shot;
        shot._damage       = _damage;
        shot._speed        = _projectileSpeed;
        shot._gravityScale = _projectileGravityScale;
        shot._range        = _range * 2.0f; // 앞을 겨눈 자리는 사거리 밖일 수 있다 — 탄은 대상이 달아난 곳까지 간다
        (void)_projectiles.spawn( ProjectileState::launch( shot, origin, direction, PhysicsSystem::getConfiguredGravityMagnitude() ) );
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

        // 대상 고르기: 태그를 가진 것들을 후보로 삼고, 상호작용과 같은 방식(거리, 시야각, 시야)으로 고른다.
        const float3    origin = pScene->getWorldPosition();
        InteractionView viewer;
        viewer._position = origin;
        viewer._forward  = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, pScene->getWorldMatrix() );
        viewer._objectID = pOwner->getObjectID();
        // 후보는 태그 색인에서만 모은다(씬을 훑지 않는다). 대상 태그가 비면 색인이 아무것도 돌려주지 않는다.
        vector<InteractionCandidate>& listCandidate = _listCandidate;
        listCandidate.clear();
        pManager->getTagIndex().findObjectsWithTags( _targetTags, _listTagged );
        for ( const GameObject* pObject : _listTagged )
        {
            const SceneComponent* pOther = pObject->getPrimarySceneComponent();
            if ( pOther == nullptr || pObject->isActiveInHierarchy() == false )
                continue;
            InteractionCandidate candidate;
            candidate._position    = pOther->getWorldPosition();
            candidate._objectID    = pObject->getObjectID();
            candidate._maxDistance = _range;
            candidate._maxAngle    = _viewAngle;
            listCandidate.push_back( candidate );
        }
        const WorldLineOfSightQuery lineOfSight{ *pManager };
        const optional<int32>       pick = InteractionView::selectBest( viewer, listCandidate, &lineOfSight );
        _target                          = pick.has_value() ? GameObjectHandle::make( listCandidate[static_cast<size_t>( *pick )]._objectID ) : GameObjectHandle{};
        stepProjectiles( *pOwner ); // 대상을 놓쳐도 이미 쏜 탄은 날아간다
        if ( pick.has_value() == false )
        {
            _previousTarget = GameObjectHandle{};
            return;
        }

        // 조준: 요를 겨눌 곳 쪽으로 돌린다. 날아가는 탄은 대상이 움직인 만큼 앞을 겨눈다 — 처음 본 스텝은 속도를 모르므로 멈춘 것으로 본다.
        const float3 targetPosition = listCandidate[static_cast<size_t>( *pick )]._position;
        const float3 targetVelocity = _previousTarget == _target ? ( targetPosition - _previousTargetPosition ) * ( 1.0f / constant::time::kDefaultFixedStepSeconds ) : float3{};
        _previousTarget             = _target;
        _previousTargetPosition     = targetPosition;
        float3        aimPoint{};
        float3        direction{};
        const bool    bCanHit    = computeAim( origin, targetPosition, targetVelocity, _projectileSpeed, _projectileGravityScale, aimPoint, direction );
        const float3  toTarget   = ( bCanHit ? aimPoint : targetPosition ) - origin;
        const float32 desiredYaw = MathUtil::atan2( toTarget._x, toTarget._z );
        float3        rotation   = pScene->getLocalRotation();
        const float32 delta      = MathUtil::wrapAngle( desiredYaw - rotation._y );
        const float32 maxTurn    = _turnSpeed * constant::time::kDefaultFixedStepSeconds;
        rotation._y += MathUtil::clamp( delta, -maxTurn, maxTurn );
        pScene->setLocalRotation( rotation );

        const bool bAimed = MathUtil::abs( delta ) <= _aimTolerance + maxTurn;
        if ( bAimed == false || bCanHit == false || _cooldownSteps > 0 )
            return;
        _cooldownSteps      = GenreGimmickUtil::toSteps( _fireInterval, 1 );
        GameObject* pVictim = pManager->resolveGameObject( _target );
        if ( pVictim == nullptr )
            return;
        fireAt( *pOwner, *pVictim, origin, direction );
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
