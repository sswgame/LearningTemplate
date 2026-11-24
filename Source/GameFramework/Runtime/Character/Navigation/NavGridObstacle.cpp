#include "pch.h"

#include "GameFramework/Runtime/Character/Navigation/NavGridObstacle.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "GameFramework/Runtime/Character/Navigation/NavGrid.h"

namespace sw
{
    uint32 NavGridObstacle::blockBody( NavGrid& inoutGrid, const WorldOrigin& worldOrigin, const RigidBodyComponent& body, float32 clearance )
    {
        float3     position{};
        quaternion rotation{};
        float3     scale{};
        if ( PhysicsComponent::readWorldPose( body, position, rotation, scale ) == false )
            return 0; // 배율 0 — 물리도 짓지 않는 바디다
        NavMeshGeometry geometry;
        geometry.setWorldOrigin( worldOrigin );
        const float4x4 world = float4x4::makeTrs( position, rotation, float3{ 1.0f, 1.0f, 1.0f } );
        for ( const PhysicsShapeDesc3D& shape : body.getShapes() )
        {
            geometry.addPhysicsShape( PhysicsComponent::makeScaledShape( shape, scale ), world, 0 );
        }
        if ( geometry.isEmpty() )
            return 0;
        const float3 margin{ clearance, 0.0f, clearance };
        const float3 minimum = geometry.getBounds()._min - margin;
        const float3 maximum = geometry.getBounds()._max + margin;
        const int2   first   = inoutGrid.computeCell( minimum );
        const int2   last    = inoutGrid.computeCell( maximum );
        uint32       count   = 0;
        for ( int32 cellY = first._y; cellY <= last._y; ++cellY )
        {
            for ( int32 cellX = first._x; cellX <= last._x; ++cellX )
            {
                const float3 center  = inoutGrid.computeCellCenter( int2{ cellX, cellY } );
                const bool   bInside = minimum._x <= center._x && center._x <= maximum._x && minimum._z <= center._z && center._z <= maximum._z;
                if ( bInside == false || inoutGrid.isInside( cellX, cellY ) == false )
                    continue;
                inoutGrid.setBlocked( cellX, cellY, true );
                ++count;
            }
        }
        return count;
    }
} // namespace sw
