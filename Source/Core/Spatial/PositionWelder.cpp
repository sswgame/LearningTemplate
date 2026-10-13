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
            static int64 toCell( float32 value, float32 tolerance ) { return static_cast<int64>( MathUtil::floor( value / tolerance ) ); }

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
        , _mapCellToPoint{}
    {
        SW_ASSERT( tolerance > 0.0f );
    }

    uint32 PositionWelder::weld( const float3& position )
    {
        const int64   cellX            = PositionWelderInternal::toCell( position._x, _tolerance );
        const int64   cellY            = PositionWelderInternal::toCell( position._y, _tolerance );
        const int64   cellZ            = PositionWelderInternal::toCell( position._z, _tolerance );
        const float32 toleranceSquared = _tolerance * _tolerance;
        for ( int64 offsetZ = -1; offsetZ <= 1; ++offsetZ )
        {
            for ( int64 offsetY = -1; offsetY <= 1; ++offsetY )
            {
                for ( int64 offsetX = -1; offsetX <= 1; ++offsetX )
                {
                    const auto found = _mapCellToPoint.find( PositionWelderInternal::makeCellKey( cellX + offsetX, cellY + offsetY, cellZ + offsetZ ) );
                    if ( found == _mapCellToPoint.end() )
                        continue;
                    for ( const uint32 pointIndex : found->second )
                    {
                        if ( float3::getDistanceSquared( _listPoint[pointIndex], position ) <= toleranceSquared )
                            return pointIndex;
                    }
                }
            }
        }
        const uint32 newIndex = static_cast<uint32>( _listPoint.size() );
        _listPoint.push_back( position );
        _mapCellToPoint[PositionWelderInternal::makeCellKey( cellX, cellY, cellZ )].push_back( newIndex );
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
        _mapCellToPoint.clear();
    }
} // namespace sw
