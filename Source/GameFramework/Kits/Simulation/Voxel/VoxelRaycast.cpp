#include "pch.h"

#include "GameFramework/Kits/Simulation/Voxel/VoxelRaycast.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Kits/Simulation/Voxel/VoxelWorld.h"

namespace sw
{
    namespace
    {
        struct VoxelRaycastInternal
        {
            /** @brief 한 축의 걸음 준비 — 다음 칸 경계까지의 t 와 칸 하나를 건너는 t 입니다. */
            static void prepareAxis( float32 origin, float32 direction, int32 cell, int32& outStep, float32& outNextT, float32& outDeltaT )
            {
                if ( MathUtil::abs( direction ) < 1.0e-8f )
                {
                    outStep   = 0;
                    outNextT  = MathUtil::MaxFloat;
                    outDeltaT = MathUtil::MaxFloat;
                    return;
                }
                outStep                = direction > 0.0f ? 1 : -1;
                const float32 boundary = direction > 0.0f ? static_cast<float32>( cell + 1 ) : static_cast<float32>( cell );
                outNextT               = ( boundary - origin ) / direction;
                outDeltaT              = MathUtil::abs( 1.0f / direction );
            }

            static bool isStopBlock( const VoxelWorld& world, VoxelBlockIndex block, bool bHitNonSolid )
            {
                if ( block == kVoxelAirBlock )
                    return false;
                return bHitNonSolid || world.getCatalog() == nullptr || world.getCatalog()->isSolid( block );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool VoxelRaycast::raycast( const VoxelWorld& world, const float3& origin, const float3& direction, float32 maxDistance, VoxelRayHit& outHit, bool bHitNonSolid )
    {
        const float32 length = direction.getLength();
        if ( length < 1.0e-8f || maxDistance <= 0.0f )
            return false;
        const float3 dir = direction * ( 1.0f / length );

        VoxelCoord cell{ static_cast<int32>( MathUtil::floor( origin._x ) ), static_cast<int32>( MathUtil::floor( origin._y ) ),
                         static_cast<int32>( MathUtil::floor( origin._z ) ) };
        int32      stepX  = 0;
        int32      stepY  = 0;
        int32      stepZ  = 0;
        float32    nextX  = 0.0f;
        float32    nextY  = 0.0f;
        float32    nextZ  = 0.0f;
        float32    deltaX = 0.0f;
        float32    deltaY = 0.0f;
        float32    deltaZ = 0.0f;
        VoxelRaycastInternal::prepareAxis( origin._x, dir._x, cell._x, stepX, nextX, deltaX );
        VoxelRaycastInternal::prepareAxis( origin._y, dir._y, cell._y, stepY, nextY, deltaY );
        VoxelRaycastInternal::prepareAxis( origin._z, dir._z, cell._z, stepZ, nextZ, deltaZ );

        VoxelCoord previous = cell;
        VoxelCoord normal{};
        float32    distance = 0.0f;
        while ( distance <= maxDistance )
        {
            const VoxelBlockIndex block = world.getBlock( cell );
            if ( VoxelRaycastInternal::isStopBlock( world, block, bHitNonSolid ) )
            {
                outHit._block      = cell;
                outHit._previous   = previous;
                outHit._normal     = normal;
                outHit._distance   = distance;
                outHit._blockIndex = block;
                return true;
            }
            previous = cell;
            if ( nextX <= nextY && nextX <= nextZ )
            {
                cell._x += stepX;
                distance = nextX;
                nextX += deltaX;
                normal = VoxelCoord{ -stepX, 0, 0 };
            }
            else if ( nextY <= nextZ )
            {
                cell._y += stepY;
                distance = nextY;
                nextY += deltaY;
                normal = VoxelCoord{ 0, -stepY, 0 };
            }
            else
            {
                cell._z += stepZ;
                distance = nextZ;
                nextZ += deltaZ;
                normal = VoxelCoord{ 0, 0, -stepZ };
            }
            // 월드 위 · 아래로 완전히 빠져나가면 더 볼 것이 없다.
            if ( ( cell._y < 0 && stepY < 0 ) || ( cell._y >= world.getSizeY() && stepY > 0 ) )
                return false;
        }
        return false;
    }
} // namespace sw
