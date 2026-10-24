#include "pch.h"

#include "Engine/Renderer/Frame/View/OcclusionTileSnapshot.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    bool OcclusionTileQuery::isSphereOccluded( const OcclusionTileSnapshot& snapshot, const float3& center, float32 radius )
    {
        if ( snapshot.isValid() == false )
            return false;
        const float4x4& m = snapshot._viewProj;

        // 구를 감싼 상자의 여덟 모서리를 지난 깊이를 그린 뷰-투영으로 옮겨 화면 사각형과 가장 가까운 깊이를 얻는다(gpucull.hlsl isOccluded 와 같다).
        float32 minU     = 1.0f;
        float32 minV     = 1.0f;
        float32 maxU     = 0.0f;
        float32 maxV     = 0.0f;
        float32 minDepth = 1.0f;
        for ( uint32 corner = 0; corner < 8; ++corner )
        {
            const float32 x     = center._x + ( ( corner & 1u ) != 0u ? radius : -radius );
            const float32 y     = center._y + ( ( corner & 2u ) != 0u ? radius : -radius );
            const float32 z     = center._z + ( ( corner & 4u ) != 0u ? radius : -radius );
            const float32 clipW = x * m._14 + y * m._24 + z * m._34 + m._44;
            if ( clipW <= OcclusionTileQuery::kMinClipW )
                return false;
            const float32 ndcX = ( x * m._11 + y * m._21 + z * m._31 + m._41 ) / clipW;
            const float32 ndcY = ( x * m._12 + y * m._22 + z * m._32 + m._42 ) / clipW;
            const float32 ndcZ = ( x * m._13 + y * m._23 + z * m._33 + m._43 ) / clipW;
            const float32 u    = ndcX * 0.5f + 0.5f;
            const float32 v    = ndcY * -0.5f + 0.5f;
            minU               = MathUtil::min( minU, u );
            minV               = MathUtil::min( minV, v );
            maxU               = MathUtil::max( maxU, u );
            maxV               = MathUtil::max( maxV, v );
            minDepth           = MathUtil::min( minDepth, ndcZ );
        }
        // 가까운 평면을 걸치거나 지난 화면 밖이면 지난 깊이가 그 자리를 모른다.
        const bool bOutsideHistory = minDepth <= 0.0f || maxU <= 0.0f || maxV <= 0.0f || minU >= 1.0f || minV >= 1.0f;
        if ( bOutsideHistory )
            return false;

        const uint32 lastColumn  = snapshot._columns - 1;
        const uint32 lastRow     = snapshot._rows - 1;
        const uint32 firstColumn = MathUtil::min( static_cast<uint32>( MathUtil::clamp( minU, 0.0f, 1.0f ) * snapshot._depthWidth ) / snapshot._tileSize, lastColumn );
        const uint32 firstRow    = MathUtil::min( static_cast<uint32>( MathUtil::clamp( minV, 0.0f, 1.0f ) * snapshot._depthHeight ) / snapshot._tileSize, lastRow );
        const uint32 endColumn   = MathUtil::min( static_cast<uint32>( MathUtil::clamp( maxU, 0.0f, 1.0f ) * snapshot._depthWidth ) / snapshot._tileSize, lastColumn );
        const uint32 endRow      = MathUtil::min( static_cast<uint32>( MathUtil::clamp( maxV, 0.0f, 1.0f ) * snapshot._depthHeight ) / snapshot._tileSize, lastRow );
        const bool   bTooWide    = endColumn - firstColumn >= OcclusionTileQuery::kMaxTestTiles || endRow - firstRow >= OcclusionTileQuery::kMaxTestTiles;
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
        std::scoped_lock<mutex> lock{ _mutex };
        _pLatest = std::move( pSnapshot );
    }

    void OcclusionTileMailbox::clear()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _pLatest.reset();
    }

    shared_ptr<const OcclusionTileSnapshot> OcclusionTileMailbox::getLatest() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _pLatest;
    }
} // namespace sw
