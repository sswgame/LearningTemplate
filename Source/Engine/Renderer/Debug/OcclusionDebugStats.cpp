#include "pch.h"

#include "Engine/Renderer/Debug/OcclusionDebugStats.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    OcclusionTileStats OcclusionTileStats::make( const OcclusionTileSnapshot& snapshot )
    {
        OcclusionTileStats stats{};
        if ( snapshot.isValid() == false )
            return stats;
        stats._tileCount = snapshot._columns * snapshot._rows;
        stats._serial    = snapshot._serial;
        for ( const float32 depth : snapshot._listTileDepth )
        {
            if ( depth < 1.0f )
                ++stats._coveredTileCount;
            stats._nearestDepth = MathUtil::min( stats._nearestDepth, depth );
        }
        return stats;
    }

    uint32 OcclusionTileStats::countOccludedSpheres( const OcclusionTileSnapshot& snapshot, vector_reference<const float4> listSphere )
    {
        uint32 occludedCount = 0;
        for ( const float4& sphere : listSphere )
        {
            if ( OcclusionTileSnapshot::isSphereOccluded( snapshot, float3{ sphere._x, sphere._y, sphere._z }, sphere._w ) )
                ++occludedCount;
        }
        return occludedCount;
    }
} // namespace sw
