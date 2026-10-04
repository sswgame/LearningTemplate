#include "pch.h"

#include "GameFramework/Camera/CameraCollisionProbe.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Physics/ContinuousCollision.h"
#include "Engine/Physics/PhysicsWorld.h"

namespace sw
{
    namespace
    {
        struct CameraCollisionProbeInternal
        {
            /** @brief 구를 상자 하나에 쓸어 닿는 거리(@p from 기준)를 @p inoutNearest 보다 가까우면 줄입니다. */
            static void sweepAgainst( const float3& from, const float3& displacement, float32 length, float32 radius, const AABB& box,
                                      float32& inoutNearest, bool& inoutHit )
            {
                SweepHit hit{};
                if ( ContinuousCollision::sweepSphere( from, radius, displacement, box, hit ) == false )
                    return;
                const float32 distance = hit._time * length;
                if ( inoutHit == false || distance < inoutNearest )
                {
                    inoutNearest = distance;
                    inoutHit     = true;
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CameraBoxCollisionProbe::CameraBoxCollisionProbe()
        : _listBox{}
    {
    }

    bool CameraBoxCollisionProbe::sweepSphere( const float3& from, const float3& to, float32 radius, float32& outDistance ) const
    {
        const float3  displacement = to - from;
        const float32 length       = displacement.getLength();
        float32       nearest      = length;
        bool          bHit         = false;
        if ( length <= MathUtil::Epsilon )
            return false;
        for ( const AABB& box : _listBox )
            CameraCollisionProbeInternal::sweepAgainst( from, displacement, length, radius, box, nearest, bHit );
        outDistance = nearest;
        return bHit;
    }

    PhysicsWorldCameraProbe::PhysicsWorldCameraProbe( const PhysicsWorld& world, uint8 layer, uint64 ignoredObjectId )
        : _pWorld{ &world }
        , _ignoredObjectId{ ignoredObjectId }
        , _layer{ layer }
    {
    }

    bool PhysicsWorldCameraProbe::sweepSphere( const float3& from, const float3& to, float32 radius, float32& outDistance ) const
    {
        const float3  displacement = to - from;
        const float32 length       = displacement.getLength();
        if ( _pWorld == nullptr || length <= MathUtil::Epsilon )
            return false;
        // 쓸리는 구간을 덮는 상자로 후보를 모으고, 바디마다 구를 쓸어 가장 가까운 것을 고른다. 대상 자신(피벗이 그 안에 있다)은 건너뛴다.
        const float3                     extent{ radius, radius, radius };
        const AABB                       sweptBounds{ float3::min( from, to ) - extent, float3::max( from, to ) + extent };
        vector<PhysicsWorld::BodyHandle> listHandle;
        _pWorld->queryAabb( sweptBounds, _layer, listHandle );
        float32 nearest = length;
        bool    bHit    = false;
        for ( const PhysicsWorld::BodyHandle handle : listHandle )
        {
            PhysicsBody body{};
            if ( _pWorld->tryGetBody( handle, body ) == false || body._bTrigger == SW_TRUE )
                continue;
            if ( _ignoredObjectId != 0 && body._objectId == _ignoredObjectId )
                continue;
            CameraCollisionProbeInternal::sweepAgainst( from, displacement, length, radius, body._aabb, nearest, bHit );
        }
        outDistance = nearest;
        return bHit;
    }
} // namespace sw
