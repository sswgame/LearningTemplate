#include "pch.h"

#include "Engine/Spatial/SpatialHashGrid2D.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Math/Math.h"

namespace sw
{
    namespace
    {
        struct SpatialHashGrid2DInternal
        {
            /**
             * @brief 좌표 하나를 셀 번호로 바꿉니다.
             * @details float 을 int 로 캐스팅하는 것은 값이 int32 범위 밖이면 **정의되지 않은 동작**
             *          이고, NaN 도 마찬가지다. 좌표 하나가 이상하다고 프로그램이 이상해지면 안 되므로
             *          범위 안으로 접어 넣는다 — 그렇게 접힌 범위는 어차피 셀 수 상한에 걸려
             *          "너무 넓다" 로 처리된다. 나눗셈을 float64 로 하는 이유는 float64 가 모든
             *          int32 를 정확히 담아서 경계 비교가 어긋나지 않기 때문이다.
             */
            static int32 toCellCoord( float32 value, float32 cellSize )
            {
                const float64 scaled = MathUtil::floor( static_cast<float64>( value ) / static_cast<float64>( cellSize ) );
                if ( ( scaled >= static_cast<float64>( MathUtil::MinInt32 ) ) == false ) // NaN 도 이쪽으로 온다
                    return MathUtil::MinInt32;
                if ( scaled > static_cast<float64>( MathUtil::MaxInt32 ) )
                    return MathUtil::MaxInt32;
                return static_cast<int32>( scaled );
            }
        };
    } // namespace

    SpatialHashGrid2D::CellRange SpatialHashGrid2D::CellRange::fromBounds( float32 minX, float32 minY, float32 maxX, float32 maxY,
                                                                           float32 cellSize )
    {
        // 뒤집힌 상자(min > max)도 받는다 — 호출부마다 정규화를 적으면 그중 하나가 빠진다.
        CellRange range{};
        range._minX = SpatialHashGrid2DInternal::toCellCoord( MathUtil::min( minX, maxX ), cellSize );
        range._maxX = SpatialHashGrid2DInternal::toCellCoord( MathUtil::max( minX, maxX ), cellSize );
        range._minY = SpatialHashGrid2DInternal::toCellCoord( MathUtil::min( minY, maxY ), cellSize );
        range._maxY = SpatialHashGrid2DInternal::toCellCoord( MathUtil::max( minY, maxY ), cellSize );
        return range;
    }

    int64 SpatialHashGrid2D::CellRange::getCellCount() const noexcept
    {
        const int64 spanX = static_cast<int64>( _maxX ) - static_cast<int64>( _minX ) + 1;
        const int64 spanY = static_cast<int64>( _maxY ) - static_cast<int64>( _minY ) + 1;
        if ( spanX <= 0 || spanY <= 0 )
            return 0;

        // **곱하기 전에 넘칠지 본다.** 셀 번호는 int32 라 한 축의 폭이 2^32 까지 가고, 두 축을 곱하면
        // 2^64 — int64 를 넘는다. 넘친 곱은 작은 수(심지어 0)가 되어 "좁은 범위" 로 읽히고, 그러면
        // 상한 검사를 통과해서 **막으려던 순회를 그대로 돌게 된다**. 호출부는 이 값을 상한과 견주기만
        // 하므로, 넘칠 때는 표현 가능한 최댓값으로 붙여 두면 답이 맞는다.
        if ( spanX > MathUtil::MaxInt64 / spanY )
            return MathUtil::MaxInt64;
        return spanX * spanY;
    }

    SpatialHashGrid2D::SpatialHashGrid2D( float32 cellSize )
        : _cellSize{ cellSize > 1.0f ? cellSize : constant::kDefaultSpatialCellSize }
        , _mapBucket{}
        , _mapHandleBound{}
        , _listOversizedHandle{}
    {
    }

    bool SpatialHashGrid2D::shouldScanAllHandles( const CellRange& range ) const
    {
        const int64 cellCount = range.getCellCount();
        return cellCount <= 0 || cellCount > kMaxQueryCellCount || cellCount > static_cast<int64>( _mapHandleBound.size() );
    }

    void SpatialHashGrid2D::insert( ObjectHandle handle, float32 minX, float32 minY, float32 maxX, float32 maxY )
    {
        if ( handle.isValid() == false )
            return;

        if ( _mapHandleBound.find( handle ) != _mapHandleBound.end() )
            remove( handle );

        const AABB2D bounds{
            float2{MathUtil::min( minX, maxX ), MathUtil::min( minY, maxY )},
            float2{MathUtil::max( minX, maxX ), MathUtil::max( minY, maxY )}
        };
        _mapHandleBound[handle] = bounds;

        const CellRange range = CellRange::fromBounds( minX, minY, maxX, maxY, _cellSize );
        if ( range.getCellCount() > kMaxHandleCellCount )
        {
            // 흩뿌리지 않는다 — 이유는 `kMaxHandleCellCount` 참고. 질의가 이 목록을 함께 본다.
            _listOversizedHandle.push_back( handle );
            return;
        }

        range.forEachCell( [this, handle]( int32 cellX, int32 cellY )
        {
            _mapBucket[getCellKey( cellX, cellY )].push_back( handle );
        } );
    }

    void SpatialHashGrid2D::update( ObjectHandle handle, float32 minX, float32 minY, float32 maxX, float32 maxY )
    {
        insert( handle, minX, minY, maxX, maxY );
    }

    void SpatialHashGrid2D::remove( ObjectHandle handle )
    {
        auto boundIt = _mapHandleBound.find( handle );
        if ( boundIt == _mapHandleBound.end() )
            return;

        const AABB2D bounds = boundIt->second;
        _mapHandleBound.erase( boundIt );

        // 넣을 때와 **같은 계산**으로 같은 셀들을 본다 — 어긋나면 죽은 핸들이 셀에 남는다.
        const CellRange range = CellRange::fromBounds( bounds._min._x, bounds._min._y, bounds._max._x, bounds._max._y, _cellSize );
        if ( range.getCellCount() > kMaxHandleCellCount )
        {
            for ( size_t handleIndex = 0; handleIndex < _listOversizedHandle.size(); ++handleIndex )
            {
                if ( _listOversizedHandle[handleIndex] == handle )
                {
                    _listOversizedHandle[handleIndex] = _listOversizedHandle.back();
                    _listOversizedHandle.pop_back();
                    break;
                }
            }
            return;
        }

        range.forEachCell( [this, handle]( int32 cellX, int32 cellY )
        {
            auto bucketIt = _mapBucket.find( getCellKey( cellX, cellY ) );
            if ( bucketIt == _mapBucket.end() )
                return;

            auto& listHandle = bucketIt->second;
            for ( size_t handleIndex = 0; handleIndex < listHandle.size(); ++handleIndex )
            {
                if ( listHandle[handleIndex] == handle )
                {
                    listHandle[handleIndex] = listHandle.back();
                    listHandle.pop_back();
                    break;
                }
            }
            if ( listHandle.empty() )
                _mapBucket.erase( bucketIt );
        } );
    }

    void SpatialHashGrid2D::clear()
    {
        _mapBucket.clear();
        _mapHandleBound.clear();
        _listOversizedHandle.clear();
    }

    void SpatialHashGrid2D::queryAabb( float32 minX, float32 minY, float32 maxX, float32 maxY, vector<ObjectHandle>& outListHandle ) const
    {
        outListHandle.clear();

        const AABB2D queryBounds{
            float2{MathUtil::min( minX, maxX ), MathUtil::min( minY, maxY )},
            float2{MathUtil::max( minX, maxX ), MathUtil::max( minY, maxY )}
        };

        forEachCandidateHandle( CellRange::fromBounds( minX, minY, maxX, maxY, _cellSize ), [&]( ObjectHandle handle )
        {
            const auto boundIt = _mapHandleBound.find( handle );
            if ( boundIt != _mapHandleBound.end() && queryBounds.intersects( boundIt->second ) )
                outListHandle.push_back( handle );
        } );

        std::sort( outListHandle.begin(), outListHandle.end() );
        outListHandle.erase( std::unique( outListHandle.begin(), outListHandle.end() ), outListHandle.end() );
    }

    void SpatialHashGrid2D::queryCircle( float32 centerX, float32 centerY, float32 radius, vector<ObjectHandle>& outListHandle ) const
    {
        outListHandle.clear();

        const float32 radiusSq = radius * radius;
        const float2  center{ centerX, centerY };

        const CellRange range = CellRange::fromBounds( centerX - radius, centerY - radius, centerX + radius, centerY + radius, _cellSize );
        forEachCandidateHandle( range, [&]( ObjectHandle handle )
        {
            const auto boundIt = _mapHandleBound.find( handle );
            if ( boundIt == _mapHandleBound.end() )
                return;

            const AABB2D& bounds     = boundIt->second;
            const float2  closePoint = center.clamped( float2{ bounds._min._x, bounds._min._y }, float2{ bounds._max._x, bounds._max._y } );
            if ( float2::getDistanceSquared( center, closePoint ) <= radiusSq )
                outListHandle.push_back( handle );
        } );

        std::sort( outListHandle.begin(), outListHandle.end() );
        outListHandle.erase( std::unique( outListHandle.begin(), outListHandle.end() ), outListHandle.end() );
    }

    void SpatialHashGrid2D::queryRay( float32 startX, float32 startY, float32 dirX, float32 dirY, float32 maxDist, vector<ObjectHandle>& outListHandle ) const
    {
        outListHandle.clear();

        float2 dir{ dirX, dirY };
        if ( dir.getLengthSquared() <= MathUtil::Epsilon || maxDist <= 0.0f )
            return;

        dir.normalize();
        const float32 ndx = dir._x;
        const float32 ndy = dir._y;

        // 그리드에 흩뿌리기엔 너무 큰 핸들은 어느 셀에도 없다 — 다른 질의들과 같이 **항상 함께** 본다.
        for ( const ObjectHandle handle : _listOversizedHandle )
            outListHandle.push_back( handle );

        const CellRange startCell = CellRange::fromBounds( startX, startY, startX, startY, _cellSize );
        int32           cellX     = startCell._minX;
        int32           cellY     = startCell._minY;

        const int32 stepX = ( ndx > 0.0f ) ? 1 : ( ( ndx < 0.0f ) ? -1 : 0 );
        const int32 stepY = ( ndy > 0.0f ) ? 1 : ( ( ndy < 0.0f ) ? -1 : 0 );

        // 셀 번호는 좌표에서 나오고 좌표는 호출부에서 온다 — int32 끝에 접혀 있을 수 있으므로
        // `+ 1` 은 넓은 타입에서 한다.
        const int64   boundaryCellX = static_cast<int64>( cellX ) + ( ( stepX > 0 ) ? 1 : 0 );
        const int64   boundaryCellY = static_cast<int64>( cellY ) + ( ( stepY > 0 ) ? 1 : 0 );
        const float32 nextBoundaryX = static_cast<float32>( boundaryCellX ) * _cellSize;
        const float32 nextBoundaryY = static_cast<float32>( boundaryCellY ) * _cellSize;

        float32 tMaxX = ( stepX != 0 ) ? ( nextBoundaryX - startX ) / ndx : MathUtil::MaxFloat;
        float32 tMaxY = ( stepY != 0 ) ? ( nextBoundaryY - startY ) / ndy : MathUtil::MaxFloat;

        const float32 tDeltaX = ( stepX != 0 ) ? ( _cellSize * static_cast<float32>( stepX ) ) / ndx : MathUtil::MaxFloat;
        const float32 tDeltaY = ( stepY != 0 ) ? ( _cellSize * static_cast<float32>( stepY ) ) / ndy : MathUtil::MaxFloat;

        // 걸음 수를 막는 이유는 나머지 셋이 셀 수를 막는 이유와 같다 — 셀 크기에 견줘 사거리가
        // 길면 훑을 셀이 끝없이 늘어난다. 상한은 `kMaxQueryCellCount` 하나를 같이 쓴다.
        float32 currentT  = 0.0f;
        int64   stepCount = 0;

        while ( currentT <= maxDist && stepCount++ < kMaxQueryCellCount )
        {
            const uint64 key      = getCellKey( cellX, cellY );
            auto         bucketIt = _mapBucket.find( key );

            if ( bucketIt != _mapBucket.end() )
            {
                for ( const ObjectHandle handle : bucketIt->second )
                {
                    auto boundIt = _mapHandleBound.find( handle );
                    if ( boundIt != _mapHandleBound.end() )
                        outListHandle.push_back( handle );
                }
            }

            // 셀 번호가 int32 끝에 닿았으면 더 나아갈 수 없다 — 증감 자체가 넘침이다.
            if ( ( stepX > 0 && cellX == MathUtil::MaxInt32 ) || ( stepX < 0 && cellX == MathUtil::MinInt32 ) ||
                 ( stepY > 0 && cellY == MathUtil::MaxInt32 ) || ( stepY < 0 && cellY == MathUtil::MinInt32 ) )
                break;

            if ( tMaxX < tMaxY )
            {
                currentT = tMaxX;
                cellX += stepX;
                tMaxX += tDeltaX;
            }
            else
            {
                currentT = tMaxY;
                cellY += stepY;
                tMaxY += tDeltaY;
            }
        }

        std::sort( outListHandle.begin(), outListHandle.end() );
        outListHandle.erase( std::unique( outListHandle.begin(), outListHandle.end() ), outListHandle.end() );
    }

} // namespace sw
