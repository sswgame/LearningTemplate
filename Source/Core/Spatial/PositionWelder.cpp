#include "pch.h"

#include "Core/Spatial/PositionWelder.h"

#include "Core/Common/HashUtil.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct PositionWelderInternal
        {
            /** @brief 셀 한 변 = 허용 오차 × 이 값. 2 면 오차 안의 이웃은 축마다 한 쪽에만 있다. */
            static constexpr float32 kCellSizeInTolerance = 2.0f;

            static int64 toCell( float32 value, float32 cellSize ) { return static_cast<int64>( MathUtil::floor( value / cellSize ) ); }

            /** @brief 축 하나에서 볼 두 셀 중 작은 쪽입니다. 셀 안 위치가 앞쪽 절반이면 앞 셀과 자기 셀, 아니면 자기 셀과 다음 셀입니다. */
            static int64 findLowerProbeCell( float32 value, float32 cellSize, int64 cell )
            {
                const float32 offsetInCell = value - static_cast<float32>( cell ) * cellSize;
                return offsetInCell < cellSize * 0.5f ? cell - 1 : cell;
            }

            /** @brief 셀 좌표 셋을 차례로 섞은 키입니다. 충돌해도 결과는 같다(거리 판정이 최종이고, 키는 후보를 모을 뿐이다). */
            static uint64 makeCellKey( int64 cellX, int64 cellY, int64 cellZ )
            {
                uint64 hash = HashUtil::mix64( static_cast<uint64>( cellX ) + HashUtil::kGoldenRatio64 );
                hash        = HashUtil::combine( hash, static_cast<uint64>( cellY ) );
                return HashUtil::combine( hash, static_cast<uint64>( cellZ ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    PositionWelder::PositionWelder( float32 tolerance )
        : _tolerance{ tolerance }
        , _listPoint{}
        , _listNextInCell{}
        , _mapCellToChain{}
    {
        SW_ASSERT( tolerance > 0.0f );
    }

    uint32 PositionWelder::weld( const float3& position )
    {
        using Internal                 = PositionWelderInternal;
        const float32 cellSize         = _tolerance * Internal::kCellSizeInTolerance;
        const int64   cellX            = Internal::toCell( position._x, cellSize );
        const int64   cellY            = Internal::toCell( position._y, cellSize );
        const int64   cellZ            = Internal::toCell( position._z, cellSize );
        const int64   lowerX           = Internal::findLowerProbeCell( position._x, cellSize, cellX );
        const int64   lowerY           = Internal::findLowerProbeCell( position._y, cellSize, cellY );
        const int64   lowerZ           = Internal::findLowerProbeCell( position._z, cellSize, cellZ );
        const float32 toleranceSquared = _tolerance * _tolerance;
        for ( int64 probeZ = lowerZ; probeZ <= lowerZ + 1; ++probeZ )
        {
            for ( int64 probeY = lowerY; probeY <= lowerY + 1; ++probeY )
            {
                for ( int64 probeX = lowerX; probeX <= lowerX + 1; ++probeX )
                {
                    const auto found = _mapCellToChain.find( Internal::makeCellKey( probeX, probeY, probeZ ) );
                    if ( found == _mapCellToChain.end() )
                        continue;
                    for ( uint32 pointIndex = found->second._first; pointIndex != invalid_index::kUint32; pointIndex = _listNextInCell[pointIndex] )
                    {
                        if ( float3::getDistanceSquared( _listPoint[pointIndex], position ) <= toleranceSquared )
                            return pointIndex;
                    }
                }
            }
        }
        const uint32 newIndex = static_cast<uint32>( _listPoint.size() );
        _listPoint.push_back( position );
        _listNextInCell.push_back( invalid_index::kUint32 );
        const uint64 cellKey = Internal::makeCellKey( cellX, cellY, cellZ );
        const auto   found   = _mapCellToChain.find( cellKey );
        if ( found == _mapCellToChain.end() )
        {
            _mapCellToChain.emplace( cellKey, CellChain{ newIndex, newIndex } );
        }
        else
        {
            _listNextInCell[found->second._last] = newIndex;
            found->second._last                  = newIndex;
        }
        return newIndex;
    }

    void PositionWelder::weldAll( span<const float3> listPosition, vector<uint32>& outListRemap )
    {
        outListRemap.resize( listPosition.size() );
        for ( size_t index = 0; index < listPosition.size(); ++index )
        {
            outListRemap[index] = weld( listPosition[index] );
        }
    }

    void PositionWelder::clear()
    {
        _listPoint.clear();
        _listNextInCell.clear();
        _mapCellToChain.clear();
    }
} // namespace sw
