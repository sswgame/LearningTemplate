#include "pch.h"

#include "Engine/Renderer/Frame/View/OcclusionTileSnapshot.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Renderer/Frame/View/HZBPyramid.h"

namespace sw
{
    bool OcclusionTileSnapshot::isSphereOccluded( const OcclusionTileSnapshot& snapshot, const float3& center, float32 radius )
    {
        if ( snapshot.isValid() == false )
            return false;
        // 구를 감싼 상자를 지난 깊이를 그린 뷰-투영으로 옮긴다(gpu_cull.hlsl 과 같은 식 한 곳 — `HZBScreenRect::projectBox`). 모르면 보인다.
        const optional<HZBScreenRect> rect = HZBScreenRect::projectBox( snapshot._viewProj, center, float3{ radius, radius, radius } );
        if ( rect.has_value() == false )
            return false;
        const float32 minU     = rect->_minU;
        const float32 minV     = rect->_minV;
        const float32 maxU     = rect->_maxU;
        const float32 maxV     = rect->_maxV;
        const float32 minDepth = rect->_minDepth;

        const uint32 lastColumn  = snapshot._columns - 1;
        const uint32 lastRow     = snapshot._rows - 1;
        const uint32 firstColumn = MathUtil::min( static_cast<uint32>( MathUtil::clamp( minU, 0.0f, 1.0f ) * snapshot._depthWidth ) / snapshot._tileSize, lastColumn );
        const uint32 firstRow    = MathUtil::min( static_cast<uint32>( MathUtil::clamp( minV, 0.0f, 1.0f ) * snapshot._depthHeight ) / snapshot._tileSize, lastRow );
        const uint32 endColumn   = MathUtil::min( static_cast<uint32>( MathUtil::clamp( maxU, 0.0f, 1.0f ) * snapshot._depthWidth ) / snapshot._tileSize, lastColumn );
        const uint32 endRow      = MathUtil::min( static_cast<uint32>( MathUtil::clamp( maxV, 0.0f, 1.0f ) * snapshot._depthHeight ) / snapshot._tileSize, lastRow );
        const bool   bTooWide    = endColumn - firstColumn >= OcclusionTileSnapshot::kMaxTestTiles || endRow - firstRow >= OcclusionTileSnapshot::kMaxTestTiles;
        if ( bTooWide )
            return false;

        float32 maxTileDepth = 0.0f;
        for ( uint32 row = firstRow; row <= endRow; ++row )
        {
            for ( uint32 column = firstColumn; column <= endColumn; ++column )
            {
                maxTileDepth = MathUtil::max( maxTileDepth, snapshot._listTileDepth[static_cast<size_t>( row ) * snapshot._columns + column] );
            }
        }
        return minDepth > maxTileDepth;
    }

    void OcclusionTileMailbox::publish( shared_ptr<const OcclusionTileSnapshot> pSnapshot )
    {
        ScopedLock<mutex> lock{ _mutex };
        _pLatest = std::move( pSnapshot );
    }

    void OcclusionTileMailbox::clear()
    {
        ScopedLock<mutex> lock{ _mutex };
        _pLatest.reset();
    }

    shared_ptr<const OcclusionTileSnapshot> OcclusionTileMailbox::getLatest() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _pLatest;
    }
} // namespace sw
