#include "pch.h"

#include "GameFramework/World/WorldQuery.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/CharacterHit.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/ContinuousCollision.h"
#include "Engine/Physics/PhysicsWorld.h"

#include "GameFramework/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct WorldQueryInternal
        {
            static constexpr float32 kRayHalfThickness = 0.005f; ///< 폴백 광선의 굵기 절반(m) — 깊이 없는 2D 바디(월드 Z 한 점)도 맞는다
            static constexpr uint8   kQueryLayer       = 0;
        };
    } // namespace
} // namespace sw

namespace sw
{
    PhysicsWorldQuery::PhysicsWorldQuery( const PhysicsWorld& physicsWorld )
        : _physicsWorld{ physicsWorld }
    {
    }

    bool PhysicsWorldQuery::raycast( const float3& from, const float3& to, uint64 ignoreObjectId, WorldRayHit& outHit ) const
    {
        outHit                     = WorldRayHit{};
        const float3 halfThickness = float3{ WorldQueryInternal::kRayHalfThickness };
        const AABB   rayBox{ from - halfThickness, from + halfThickness };
        const float3 displacement = to - from;
        // 2D 콜라이더의 바디는 깊이가 없다(Z 가 0 한 점 — Z 는 그리기 순서). 그런 바디는 광선의 Z 와 상관없이 맞히므로 후보 상자가 Z = 0 도 덮게 한다.
        AABB sweptBounds{ float3::min( from, to ) - halfThickness, float3::max( from, to ) + halfThickness };
        sweptBounds._min._z = MathUtil::min( sweptBounds._min._z, -WorldQueryInternal::kRayHalfThickness );
        sweptBounds._max._z = MathUtil::max( sweptBounds._max._z, WorldQueryInternal::kRayHalfThickness );
        vector<PhysicsWorld::BodyHandle> listHandle;
        _physicsWorld.queryAabb( sweptBounds, WorldQueryInternal::kQueryLayer, listHandle );
        bool bHit = false;
        for ( const PhysicsWorld::BodyHandle handle : listHandle )
        {
            PhysicsBody body;
            if ( _physicsWorld.tryGetBody( handle, body ) == false || body._bTrigger == SW_TRUE || body._objectId == ignoreObjectId )
                continue;
            AABB target = body._aabb;
            if ( target._min._z == target._max._z )
            {
                target._min._z = MathUtil::min( from._z, to._z ) - WorldQueryInternal::kRayHalfThickness;
                target._max._z = MathUtil::max( from._z, to._z ) + WorldQueryInternal::kRayHalfThickness;
            }
            SweepHit sweep;
            if ( ContinuousCollision::sweepAabb( rayBox, displacement, target, sweep ) == false )
                continue;
            if ( bHit && sweep._time >= outHit._fraction )
                continue;
            bHit             = true;
            outHit._fraction = sweep._time;
            outHit._point    = from + displacement * sweep._time;
            outHit._objectId = body._objectId;
        }
        return bHit;
    }

    ScenePhysicsWorldQuery::ScenePhysicsWorldQuery( const GameObjectManager& manager )
        : _manager{ manager }
    {
    }

    bool ScenePhysicsWorldQuery::raycast( const float3& from, const float3& to, uint64 ignoreObjectId, WorldRayHit& outHit ) const
    {
        outHit               = WorldRayHit{};
        const float3  delta  = to - from;
        const float32 length = delta.getLength();
        if ( length <= MathUtil::Epsilon )
            return false;
        bool            bHit = false;
        CharacterRayHit hit;
        if ( CharacterHitUtil::raycast3D( _manager, from, delta, length, MathUtil::MaxUInt32, ignoreObjectId, hit ) )
        {
            bHit             = true;
            outHit._fraction = hit._distance / length;
            outHit._point    = hit._point;
            outHit._objectId = hit._pObject != nullptr ? hit._pObject->getObjectId() : 0;
        }
        // 2D 씬은 XY 평면 — 같은 선분의 평면 길이로 잰다.
        const float32 planarLength = float2{ delta._x, delta._y }.getLength();
        if ( planarLength > MathUtil::Epsilon && CharacterHitUtil::raycast2D( _manager, from, delta, planarLength, MathUtil::MaxUInt32, ignoreObjectId, hit ) )
        {
            const float32 fraction = hit._distance / planarLength;
            if ( bHit == false || fraction < outHit._fraction )
            {
                bHit             = true;
                outHit._fraction = fraction;
                outHit._point    = from + delta * fraction;
                outHit._objectId = hit._pObject != nullptr ? hit._pObject->getObjectId() : 0;
            }
        }
        return bHit;
    }

    bool WorldQuery::raycast( const GameObjectManager& manager, const float3& from, const float3& to, uint64 ignoreObjectId, WorldRayHit& outHit )
    {
        const IWorldQuery* pService = game::getService<IWorldQuery>();
        if ( pService != nullptr )
            return pService->raycast( from, to, ignoreObjectId, outHit );
        // 강체 물리와 겹침 월드(BoxCollider2D) 중 가까운 것 — 두 세계가 한 씬에 섞여 있을 수 있다.
        const ScenePhysicsWorldQuery rigid{ manager };
        const PhysicsWorldQuery      fallback{ manager.getPhysicsWorld() };
        WorldRayHit                  rigidHit;
        WorldRayHit                  fallbackHit;
        const bool                   bRigid    = rigid.raycast( from, to, ignoreObjectId, rigidHit );
        const bool                   bFallback = fallback.raycast( from, to, ignoreObjectId, fallbackHit );
        if ( bRigid && ( bFallback == false || rigidHit._fraction <= fallbackHit._fraction ) )
        {
            outHit = rigidHit;
            return true;
        }
        if ( bFallback )
        {
            outHit = fallbackHit;
            return true;
        }
        outHit = WorldRayHit{};
        return false;
    }

    bool WorldQuery::hasLineOfSight( const GameObjectManager& manager, const float3& from, const float3& to, uint64 viewerObjectId, uint64 targetObjectId )
    {
        WorldRayHit hit;
        if ( raycast( manager, from, to, viewerObjectId, hit ) == false )
            return true;
        return targetObjectId != 0 && hit._objectId == targetObjectId;
    }
} // namespace sw
