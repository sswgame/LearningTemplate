#include "pch.h"

#include "GameFramework/World/WorldQuery.h"

#include "Core/Math/MathUtil.h"

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
        outHit                                         = WorldRayHit{};
        const float3                     halfThickness = float3{ WorldQueryInternal::kRayHalfThickness };
        const AABB                       rayBox{ from - halfThickness, from + halfThickness };
        const float3                     displacement = to - from;
        const AABB                       sweptBounds{ float3::min( from, to ) - halfThickness, float3::max( from, to ) + halfThickness };
        vector<PhysicsWorld::BodyHandle> listHandle;
        _physicsWorld.queryAabb( sweptBounds, WorldQueryInternal::kQueryLayer, listHandle );
        bool bHit = false;
        for ( const PhysicsWorld::BodyHandle handle : listHandle )
        {
            PhysicsBody body;
            if ( _physicsWorld.tryGetBody( handle, body ) == false || body._bTrigger == SW_TRUE || body._objectId == ignoreObjectId )
                continue;
            SweepHit sweep;
            if ( ContinuousCollision::sweepAabb( rayBox, displacement, body._aabb, sweep ) == false )
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

    bool WorldQuery::raycast( const GameObjectManager& manager, const float3& from, const float3& to, uint64 ignoreObjectId, WorldRayHit& outHit )
    {
        const IWorldQuery* pService = game::getService<IWorldQuery>();
        if ( pService != nullptr )
            return pService->raycast( from, to, ignoreObjectId, outHit );
        const PhysicsWorldQuery fallback{ manager.getPhysicsWorld() };
        return fallback.raycast( from, to, ignoreObjectId, outHit );
    }

    bool WorldQuery::hasLineOfSight( const GameObjectManager& manager, const float3& from, const float3& to, uint64 viewerObjectId, uint64 targetObjectId )
    {
        WorldRayHit hit;
        if ( raycast( manager, from, to, viewerObjectId, hit ) == false )
            return true;
        return targetObjectId != 0 && hit._objectId == targetObjectId;
    }
} // namespace sw
