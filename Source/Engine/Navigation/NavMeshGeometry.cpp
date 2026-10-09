#include "pch.h"

#include "Engine/Navigation/NavMeshGeometry.h"

#include "Core/Common/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Physics/PhysicsShape.h"

namespace sw
{
    namespace
    {
        struct NavMeshGeometryInternal
        {
            /** @brief 색인 칸 수의 상한입니다. 넘으면 칸을 키운다. */
            static constexpr int64 kMaxIndexCellCount = 1 << 20;

            /** @brief 삼각형 하나가 덮는 색인 칸 범위(양 끝 포함)입니다. */
            struct CellRange
            {
                int32 _minX{ 0 };
                int32 _minZ{ 0 };
                int32 _maxX{ 0 };
                int32 _maxZ{ 0 };
            };

            static uint64 mixBytes( uint64 hash, const void* pData, size_t size )
            {
                if ( pData == nullptr || size == 0 )
                    return hash;
                return StringUtil::computeHash64( static_cast<const utf8*>( pData ), size, false, hash );
            }

            /** @brief 셰이프 점들의 로컬 경계 상자 반 크기 · 가운데입니다. 점이 없으면 false 입니다. */
            static bool computePointBox( const vector<float3>& listPoint, float3& outCenter, float3& outHalfExtents )
            {
                if ( listPoint.empty() )
                    return false;
                float3 minPoint = listPoint.front();
                float3 maxPoint = listPoint.front();
                for ( const float3& point : listPoint )
                {
                    minPoint = float3{ MathUtil::min( minPoint._x, point._x ), MathUtil::min( minPoint._y, point._y ), MathUtil::min( minPoint._z, point._z ) };
                    maxPoint = float3{ MathUtil::max( maxPoint._x, point._x ), MathUtil::max( maxPoint._y, point._y ), MathUtil::max( maxPoint._z, point._z ) };
                }
                outCenter      = ( minPoint + maxPoint ) * 0.5f;
                outHalfExtents = ( maxPoint - minPoint ) * 0.5f;
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AABB NavConvexVolume::computeBounds() const
    {
        AABB bounds = AABB::empty();
        for ( const float3& point : _listPoint )
        {
            bounds._min = float3{ MathUtil::min( bounds._min._x, point._x ), _minY, MathUtil::min( bounds._min._z, point._z ) };
            bounds._max = float3{ MathUtil::max( bounds._max._x, point._x ), _maxY, MathUtil::max( bounds._max._z, point._z ) };
        }
        return bounds;
    }
} // namespace sw

namespace sw
{
    NavMeshGeometry::NavMeshGeometry()
        : _listVertex{}
        , _listIndex{}
        , _listTriangleArea{}
        , _listVolume{}
        , _listCellStart{}
        , _listCellTriangle{}
        , _bounds{ AABB::empty() }
        , _indexOrigin{ 0.0f, 0.0f }
        , _indexCellSize{ 0.0f }
        , _indexWidth{ 0 }
        , _indexHeight{ 0 }
    {
    }

    void NavMeshGeometry::clear()
    {
        _listVertex.clear();
        _listIndex.clear();
        _listTriangleArea.clear();
        _listVolume.clear();
        _listCellStart.clear();
        _listCellTriangle.clear();
        _bounds        = AABB::empty();
        _indexCellSize = 0.0f;
        _indexWidth    = 0;
        _indexHeight   = 0;
    }

    void NavMeshGeometry::expandBounds( const float3& point )
    {
        _bounds._min = float3{ MathUtil::min( _bounds._min._x, point._x ), MathUtil::min( _bounds._min._y, point._y ), MathUtil::min( _bounds._min._z, point._z ) };
        _bounds._max = float3{ MathUtil::max( _bounds._max._x, point._x ), MathUtil::max( _bounds._max._y, point._y ), MathUtil::max( _bounds._max._z, point._z ) };
    }

    void NavMeshGeometry::addTriangles( const float3* pPoint, uint32 pointCount, const uint32* pIndex, uint32 indexCount, const float4x4& world, uint8 area )
    {
        if ( pPoint == nullptr || pIndex == nullptr || pointCount == 0 || indexCount < 3 )
            return;
        const uint32 base = static_cast<uint32>( _listVertex.size() );
        _listVertex.reserve( _listVertex.size() + pointCount );
        for ( uint32 pointIndex = 0; pointIndex < pointCount; ++pointIndex )
        {
            const float3 worldPoint = float3::transform( pPoint[pointIndex], world );
            _listVertex.push_back( worldPoint );
            expandBounds( worldPoint );
        }
        const uint32 triangleCount = indexCount / 3;
        for ( uint32 triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex )
        {
            const uint32 a = pIndex[triangleIndex * 3 + 0];
            const uint32 b = pIndex[triangleIndex * 3 + 1];
            const uint32 c = pIndex[triangleIndex * 3 + 2];
            if ( a >= pointCount || b >= pointCount || c >= pointCount )
                continue;
            _listIndex.push_back( base + a );
            _listIndex.push_back( base + b );
            _listIndex.push_back( base + c );
            _listTriangleArea.push_back( area );
        }
    }

    void NavMeshGeometry::addTriangleList( const void* pFirstPosition, uint32 vertexCount, uint32 stride, const float4x4& world, uint8 area )
    {
        if ( pFirstPosition == nullptr || vertexCount < 3 || stride < sizeof( float3 ) )
            return;
        const uint32 base   = static_cast<uint32>( _listVertex.size() );
        const uint8* pBytes = static_cast<const uint8*>( pFirstPosition );
        const uint32 usable = vertexCount - vertexCount % 3;
        _listVertex.reserve( _listVertex.size() + usable );
        for ( uint32 vertexIndex = 0; vertexIndex < usable; ++vertexIndex )
        {
            float3 localPoint{};
            Memory::copy( &localPoint, pBytes + static_cast<size_t>( vertexIndex ) * stride, sizeof( float3 ) );
            const float3 worldPoint = float3::transform( localPoint, world );
            _listVertex.push_back( worldPoint );
            expandBounds( worldPoint );
        }
        for ( uint32 vertexIndex = 0; vertexIndex < usable; vertexIndex += 3 )
        {
            _listIndex.push_back( base + vertexIndex );
            _listIndex.push_back( base + vertexIndex + 1 );
            _listIndex.push_back( base + vertexIndex + 2 );
            _listTriangleArea.push_back( area );
        }
    }

    void NavMeshGeometry::addBox( const float3& halfExtents, const float4x4& world, uint8 area )
    {
        const float3 arrCorner[8] = {
            float3{-halfExtents._x, -halfExtents._y, -halfExtents._z},
            float3{ halfExtents._x, -halfExtents._y, -halfExtents._z},
            float3{ halfExtents._x, -halfExtents._y,  halfExtents._z},
            float3{-halfExtents._x, -halfExtents._y,  halfExtents._z},
            float3{-halfExtents._x,  halfExtents._y, -halfExtents._z},
            float3{ halfExtents._x,  halfExtents._y, -halfExtents._z},
            float3{ halfExtents._x,  halfExtents._y,  halfExtents._z},
            float3{-halfExtents._x,  halfExtents._y,  halfExtents._z},
        };
        // 바깥을 보는 면(위에서 보아 시계 반대 — 윗면의 법선이 +Y). 베이크하는 쪽은 법선으로 경사를 잰다.
        const uint32 arrIndex[36] = {
            4, 7, 6, 4, 6, 5, // 위
            0, 1, 2, 0, 2, 3, // 아래
            0, 4, 5, 0, 5, 1, // -Z
            2, 6, 7, 2, 7, 3, // +Z
            3, 7, 4, 3, 4, 0, // -X
            1, 5, 6, 1, 6, 2, // +X
        };
        addTriangles( arrCorner, 8, arrIndex, 36, world, area );
    }

    void NavMeshGeometry::addPhysicsShape( const PhysicsShapeDesc3D& shape, const float4x4& bodyWorld, uint8 area )
    {
        using Internal       = NavMeshGeometryInternal;
        const float4x4 local = float4x4::createTrs( shape._localPosition, shape._localRotation, float3{ 1.0f, 1.0f, 1.0f } );
        const float4x4 world = local * bodyWorld;
        switch ( shape._type )
        {
            case PhysicsShapeType3D::Box:
            {
                addBox( shape._halfExtents, world, area );
                break;
            }
            case PhysicsShapeType3D::Sphere:
            {
                addBox( float3{ shape._radius, shape._radius, shape._radius }, world, area );
                break;
            }
            case PhysicsShapeType3D::Capsule:
            {
                addBox( float3{ shape._radius, shape._halfHeight + shape._radius, shape._radius }, world, area );
                break;
            }
            case PhysicsShapeType3D::ConvexHull:
            {
                float3 center{};
                float3 halfExtents{};
                if ( Internal::computePointBox( shape._listPoint, center, halfExtents ) )
                    addBox( halfExtents, float4x4::createTrs( center, float3{}, float3{ 1.0f, 1.0f, 1.0f } ) * world, area );
                break;
            }
            case PhysicsShapeType3D::TriangleMesh:
            {
                addTriangles( shape._listPoint.data(), static_cast<uint32>( shape._listPoint.size() ), shape._listIndex.data(),
                              static_cast<uint32>( shape._listIndex.size() ), world, area );
                break;
            }
        }
    }

    void NavMeshGeometry::addConvexVolume( const NavConvexVolume& volume )
    {
        if ( volume._listPoint.size() < 3 || volume._maxY < volume._minY )
            return;
        _listVolume.push_back( volume );
        const AABB bounds = volume.computeBounds();
        expandBounds( bounds._min );
        expandBounds( bounds._max );
    }

    void NavMeshGeometry::buildSpatialIndex( float32 cellSize )
    {
        using Internal = NavMeshGeometryInternal;
        _listCellStart.clear();
        _listCellTriangle.clear();
        _indexWidth  = 0;
        _indexHeight = 0;
        if ( isEmpty() || cellSize <= 0.0f )
            return;
        const float32 spanX = _bounds._max._x - _bounds._min._x;
        const float32 spanZ = _bounds._max._z - _bounds._min._z;
        float32       size  = cellSize;
        while ( static_cast<int64>( spanX / size + 1.0f ) * static_cast<int64>( spanZ / size + 1.0f ) > Internal::kMaxIndexCellCount )
        {
            size *= 2.0f;
        }
        _indexCellSize         = size;
        _indexOrigin           = float2{ _bounds._min._x, _bounds._min._z };
        _indexWidth            = static_cast<int32>( spanX / size ) + 1;
        _indexHeight           = static_cast<int32>( spanZ / size ) + 1;
        const size_t cellCount = static_cast<size_t>( _indexWidth ) * static_cast<size_t>( _indexHeight );

        // 두 번 훑는다 — 칸마다 수를 세고(앞자리 합으로 시작), 다시 훑으며 채운다. 칸마다 vector 를 두지 않는다.
        _listCellStart.assign( cellCount + 1, 0 );
        const uint32                triangleCount = getTriangleCount();
        vector<Internal::CellRange> listCellRange( triangleCount );
        for ( uint32 triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex )
        {
            const float3& a              = _listVertex[_listIndex[triangleIndex * 3 + 0]];
            const float3& b              = _listVertex[_listIndex[triangleIndex * 3 + 1]];
            const float3& c              = _listVertex[_listIndex[triangleIndex * 3 + 2]];
            const float32 minX           = MathUtil::min( a._x, MathUtil::min( b._x, c._x ) );
            const float32 maxX           = MathUtil::max( a._x, MathUtil::max( b._x, c._x ) );
            const float32 minZ           = MathUtil::min( a._z, MathUtil::min( b._z, c._z ) );
            const float32 maxZ           = MathUtil::max( a._z, MathUtil::max( b._z, c._z ) );
            const int32   cellX0         = MathUtil::clamp( static_cast<int32>( ( minX - _indexOrigin._x ) / size ), 0, _indexWidth - 1 );
            const int32   cellX1         = MathUtil::clamp( static_cast<int32>( ( maxX - _indexOrigin._x ) / size ), 0, _indexWidth - 1 );
            const int32   cellZ0         = MathUtil::clamp( static_cast<int32>( ( minZ - _indexOrigin._y ) / size ), 0, _indexHeight - 1 );
            const int32   cellZ1         = MathUtil::clamp( static_cast<int32>( ( maxZ - _indexOrigin._y ) / size ), 0, _indexHeight - 1 );
            listCellRange[triangleIndex] = Internal::CellRange{ cellX0, cellZ0, cellX1, cellZ1 };
            for ( int32 cellZ = cellZ0; cellZ <= cellZ1; ++cellZ )
            {
                for ( int32 cellX = cellX0; cellX <= cellX1; ++cellX )
                {
                    ++_listCellStart[static_cast<size_t>( cellZ ) * static_cast<size_t>( _indexWidth ) + static_cast<size_t>( cellX ) + 1];
                }
            }
        }
        for ( size_t cellIndex = 1; cellIndex <= cellCount; ++cellIndex )
        {
            _listCellStart[cellIndex] += _listCellStart[cellIndex - 1];
        }
        _listCellTriangle.assign( _listCellStart[cellCount], 0 );
        vector<uint32> listCursor( _listCellStart.begin(), _listCellStart.end() - 1 );
        for ( uint32 triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex )
        {
            const Internal::CellRange& range = listCellRange[triangleIndex];
            for ( int32 cellZ = range._minZ; cellZ <= range._maxZ; ++cellZ )
            {
                for ( int32 cellX = range._minX; cellX <= range._maxX; ++cellX )
                {
                    const size_t cellIndex                     = static_cast<size_t>( cellZ ) * static_cast<size_t>( _indexWidth ) + static_cast<size_t>( cellX );
                    _listCellTriangle[listCursor[cellIndex]++] = triangleIndex;
                }
            }
        }
    }

    void NavMeshGeometry::collectTriangles( const float2& min, const float2& max, vector<uint32>& outListTriangle ) const
    {
        outListTriangle.clear();
        const uint32 triangleCount = getTriangleCount();
        if ( _indexWidth <= 0 || _indexHeight <= 0 )
        {
            outListTriangle.resize( triangleCount );
            for ( uint32 triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex )
            {
                outListTriangle[triangleIndex] = triangleIndex;
            }
            return;
        }
        const int32 cellX0 = MathUtil::clamp( static_cast<int32>( MathUtil::floor( ( min._x - _indexOrigin._x ) / _indexCellSize ) ), 0, _indexWidth - 1 );
        const int32 cellX1 = MathUtil::clamp( static_cast<int32>( MathUtil::floor( ( max._x - _indexOrigin._x ) / _indexCellSize ) ), 0, _indexWidth - 1 );
        const int32 cellZ0 = MathUtil::clamp( static_cast<int32>( MathUtil::floor( ( min._y - _indexOrigin._y ) / _indexCellSize ) ), 0, _indexHeight - 1 );
        const int32 cellZ1 = MathUtil::clamp( static_cast<int32>( MathUtil::floor( ( max._y - _indexOrigin._y ) / _indexCellSize ) ), 0, _indexHeight - 1 );
        if ( max._x < _indexOrigin._x || max._y < _indexOrigin._y )
            return;
        for ( int32 cellZ = cellZ0; cellZ <= cellZ1; ++cellZ )
        {
            for ( int32 cellX = cellX0; cellX <= cellX1; ++cellX )
            {
                const size_t cellIndex = static_cast<size_t>( cellZ ) * static_cast<size_t>( _indexWidth ) + static_cast<size_t>( cellX );
                for ( uint32 slot = _listCellStart[cellIndex]; slot < _listCellStart[cellIndex + 1]; ++slot )
                {
                    outListTriangle.push_back( _listCellTriangle[slot] );
                }
            }
        }
        // 여러 칸에 걸친 삼각형은 한 번만 — 정렬 뒤 겹친 것을 지운다.
        std::sort( outListTriangle.begin(), outListTriangle.end() );
        outListTriangle.erase( std::unique( outListTriangle.begin(), outListTriangle.end() ), outListTriangle.end() );
    }

    uint64 NavMeshGeometry::computeHash() const
    {
        using Internal = NavMeshGeometryInternal;
        uint64 hash    = HashUtil::kFnvOffset64;
        hash           = Internal::mixBytes( hash, _listVertex.data(), _listVertex.size() * sizeof( float3 ) );
        hash           = Internal::mixBytes( hash, _listIndex.data(), _listIndex.size() * sizeof( uint32 ) );
        hash           = Internal::mixBytes( hash, _listTriangleArea.data(), _listTriangleArea.size() );
        for ( const NavConvexVolume& volume : _listVolume )
        {
            hash = Internal::mixBytes( hash, volume._listPoint.data(), volume._listPoint.size() * sizeof( float3 ) );
            hash = Internal::mixBytes( hash, &volume._minY, sizeof( volume._minY ) );
            hash = Internal::mixBytes( hash, &volume._maxY, sizeof( volume._maxY ) );
            hash = Internal::mixBytes( hash, &volume._area, sizeof( volume._area ) );
        }
        return hash;
    }
} // namespace sw
