#include "pch.h"

#include "GameFramework/Base/World/Query/WorldQuery.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    ScenePhysicsWorldQuery::ScenePhysicsWorldQuery( const GameObjectManager& manager )
        : _manager{ manager }
    {
    }

    bool ScenePhysicsWorldQuery::raycast( const float3& from, const float3& to, uint64 ignoreObjectID, WorldRayHit& outHit ) const
    {
        outHit               = WorldRayHit{};
        const float3  delta  = to - from;
        const float32 length = delta.getLength();
        if ( length <= MathUtil::kEpsilon )
            return false;
        bool            bHit = false;
        CharacterRayHit hit;
        if ( CharacterHitUtil::raycast3D( _manager, from, delta, length, MathUtil::kMaxUInt32, ignoreObjectID, hit ) )
        {
            bHit             = true;
            outHit._fraction = hit._distance / length;
            outHit._point    = hit._point;
            outHit._objectID = hit._pObject != nullptr ? hit._pObject->getObjectID() : 0;
        }
        // 2D 씬은 XY 평면 — 같은 선분의 평면 길이로 측정한다. 2D 콜라이더(`BoxCollider2DComponent`)도 이 씬의 바디다.
        const float32 planarLength = float2{ delta._x, delta._y }.getLength();
        if ( planarLength > MathUtil::kEpsilon && CharacterHitUtil::raycast2D( _manager, from, delta, planarLength, MathUtil::kMaxUInt32, ignoreObjectID, hit ) )
        {
            const float32 fraction = hit._distance / planarLength;
            if ( bHit == false || fraction < outHit._fraction )
            {
                bHit             = true;
                outHit._fraction = fraction;
                outHit._point    = from + delta * fraction;
                outHit._objectID = hit._pObject != nullptr ? hit._pObject->getObjectID() : 0;
            }
        }
        return bHit;
    }

    bool WorldQuery::raycast( const GameObjectManager& manager, const float3& from, const float3& to, uint64 ignoreObjectID, WorldRayHit& outHit )
    {
        const IWorldQuery* pService = game::findSubsystem<IWorldQuery>();
        if ( pService != nullptr )
            return pService->raycast( from, to, ignoreObjectID, outHit );
        const ScenePhysicsWorldQuery scene{ manager };
        return scene.raycast( from, to, ignoreObjectID, outHit );
    }

    bool WorldQuery::hasLineOfSight( const GameObjectManager& manager, const float3& from, const float3& to, uint64 viewerObjectID, uint64 targetObjectID )
    {
        WorldRayHit hit;
        if ( raycast( manager, from, to, viewerObjectID, hit ) == false )
            return true;
        return targetObjectID != 0 && hit._objectID == targetObjectID;
    }
} // namespace sw
