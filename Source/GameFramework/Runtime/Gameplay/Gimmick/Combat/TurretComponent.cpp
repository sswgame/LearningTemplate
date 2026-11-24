#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Combat/TurretComponent.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/Ray.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/TagIndex.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/GameObject/Tick/TickInterval.h"
#include "Engine/Physics/PhysicsSystem.h"

#include "GameFramework/Runtime/Character/Combat/Projectile/ResolvedShot.h"
#include "GameFramework/Runtime/Character/Combat/TargetFilter.h"
#include "GameFramework/Runtime/Character/Combat/Weapon/Ballistics.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Combat/GimmickDamageUtil.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionView.h"
#include "GameFramework/Runtime/World/Query/WorldQuery.h"

namespace sw
{
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
        , _viewAngle{}
        , _turnSpeed{ MathUtil::kPi }
        , _aimTolerance{ Radians::fromDegrees( 5.0f ) }
        , _fireInterval{ 0.5f }
        , _damage{ 10.0f }
        , _projectileSpeed{ 0.0f }
        , _projectileGravityScale{ 0.0f }
        , _hitRadius{ 0.6f }
        , _target{}
        , _cooldownSteps{ 0 }
        , _shotCount{ 0 }
        , _hitCount{ 0 }
        , _listCandidate{}
        , _listTagged{}
        , _listShotTarget{}
        , _projectiles{}
        , _previousTarget{}
        , _targetTeleport{}
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
        const GameObjectManager* pManager = owner.getManager(); // 틱에서 부른다 — 읽기 · 피해는 명령 버퍼(`GimmickDamageUtil`)
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
            const GameObject* pVictim = pManager->resolveGameObject( GameObjectHandle::make( _listShotTarget[static_cast<size_t>( event._hit._other )]._objectID ) );
            if ( pVictim == nullptr )
                continue;
            ++_hitCount;
            GimmickDamageUtil::applyDamage( *pVictim, &owner, hashed_string( "Turret" ), event._projectile._damage );
        }
    }

    void TurretComponent::fireAt( GameObject& owner, const GameObject& victim, const float3& origin, const float3& direction )
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

    void TurretComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const int32 stepCount = context.getIntervalCount();
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepOnce();
        }
    }

    void TurretComponent::onWorldOriginShifted( const float3& delta )
    {
        _previousTargetPosition += delta;
        _projectiles.applyWorldOffset( delta );
    }

    void TurretComponent::stepOnce()
    {
        GameObject*              pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        SceneComponent*          pScene   = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
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
            if ( pOther == nullptr || TargetFilter::isSelectable( *pObject ) == false )
                continue;
            InteractionCandidate candidate;
            candidate._position    = pOther->getWorldPosition();
            candidate._objectID    = pObject->getObjectID();
            candidate._maxDistance = _range;
            candidate._maxAngle    = _viewAngle.radians();
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
        const float3                targetPosition = listCandidate[static_cast<size_t>( *pick )]._position;
        const GameObject* const     pTarget        = pManager->resolveGameObject( _target );
        const SceneComponent* const pTargetScene   = pTarget != nullptr ? pTarget->getPrimarySceneComponent() : nullptr;
        const bool                  bTeleported    = pTargetScene != nullptr && _targetTeleport.consume( *pTargetScene ).has_value();
        const float3                targetVelocity = _previousTarget == _target && bTeleported == false ? ( targetPosition - _previousTargetPosition ) * ( 1.0f / constant::time::kDefaultFixedStepSeconds ) : float3{};
        _previousTarget                            = _target;
        _previousTargetPosition                    = targetPosition;
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

        const bool bAimed = MathUtil::abs( delta ) <= _aimTolerance.radians() + maxTurn;
        if ( bAimed == false || bCanHit == false || _cooldownSteps > 0 )
            return;
        _cooldownSteps            = TickIntervalUtil::secondsToCount( kTickInterval, _fireInterval, 1 );
        const GameObject* pVictim = pManager->resolveGameObject( _target );
        if ( pVictim == nullptr )
            return;
        fireAt( *pOwner, *pVictim, origin, direction );
    }
} // namespace sw
