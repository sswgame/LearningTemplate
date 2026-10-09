#include "pch.h"

#include "GameFramework/Base/Actor/AI/Schedule/SchedulePathing.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Actor/Navigation/GridPathfinder.h"
#include "GameFramework/Base/Actor/Navigation/NavGrid.h"
#include "GameFramework/Base/World/Land/AreaGraph.h"
#include "GameFramework/Base/World/Query/GameFlags.h"

namespace sw
{
    namespace
    {
        struct SchedulePathingInternal
        {
            static constexpr float32 kMinSpeed      = 1.0e-3f;
            static constexpr int32   kNearestRadius = 4; ///< 목적지가 막힌 칸이면 이만큼 둘레에서 걸을 칸을 찾는다

            static float32 computeMinutes( float32 length, float32 unitsPerMinute ) { return length / MathUtil::max( kMinSpeed, unitsPerMinute ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    hashed_string ISchedulePathing::computeAreaAt( const ScheduleLocation& from, const ScheduleLocation& to, float32 fraction ) const
    {
        return fraction < 0.5f ? from._area : to._area;
    }

    float3 ISchedulePathing::offsetInPlane( const float3& center, float32 offsetU, float32 offsetV ) const
    {
        return SchedulePathingUtil::offsetInPlane( SchedulePlane::XZ, center, offsetU, offsetV );
    }

    float32 SchedulePathingUtil::computeLength( const vector<float3>& listPoint )
    {
        float32 length = 0.0f;
        for ( size_t pointIndex = 1; pointIndex < listPoint.size(); ++pointIndex )
        {
            length += float3::getDistance( listPoint[pointIndex - 1], listPoint[pointIndex] );
        }
        return length;
    }

    float3 SchedulePathingUtil::computePointAlong( const vector<float3>& listPoint, float32 fraction )
    {
        if ( listPoint.empty() )
            return float3{};
        const float32 total = computeLength( listPoint );
        if ( total <= 0.0f || fraction <= 0.0f )
            return listPoint.front();
        if ( fraction >= 1.0f )
            return listPoint.back();
        float32 left = total * fraction;
        for ( size_t pointIndex = 1; pointIndex < listPoint.size(); ++pointIndex )
        {
            const float32 pieceLength = float3::getDistance( listPoint[pointIndex - 1], listPoint[pointIndex] );
            if ( left <= pieceLength && pieceLength > 0.0f )
                return float3::lerp( listPoint[pointIndex - 1], listPoint[pointIndex], left / pieceLength );
            left -= pieceLength;
        }
        return listPoint.back();
    }

    float3 SchedulePathingUtil::offsetInPlane( SchedulePlane plane, const float3& center, float32 offsetU, float32 offsetV )
    {
        switch ( plane )
        {
            case SchedulePlane::XZ:
                return float3{ center._x + offsetU, center._y, center._z + offsetV };
            case SchedulePlane::XY:
                return float3{ center._x + offsetU, center._y + offsetV, center._z };
        }
        return center;
    }

    StraightSchedulePathing::StraightSchedulePathing( SchedulePlane plane )
        : _plane{ plane }
    {
    }

    float32 StraightSchedulePathing::estimateTravelMinutes( const ScheduleLocation& from, const ScheduleLocation& to, float32 unitsPerMinute ) const
    {
        return SchedulePathingInternal::computeMinutes( float3::getDistance( from._position, to._position ), unitsPerMinute );
    }

    void StraightSchedulePathing::makeRoute( const ScheduleLocation& from, const ScheduleLocation& to, vector<float3>& outListPoint ) const
    {
        outListPoint.clear();
        outListPoint.push_back( from._position );
        outListPoint.push_back( to._position );
    }

    float3 StraightSchedulePathing::offsetInPlane( const float3& center, float32 offsetU, float32 offsetV ) const
    {
        return SchedulePathingUtil::offsetInPlane( _plane, center, offsetU, offsetV );
    }

    NavGridSchedulePathing::NavGridSchedulePathing( const NavGrid* pGrid, GridPathfinder* pPathfinder, SchedulePlane plane )
        : _pGrid{ pGrid }
        , _pPathfinder{ pPathfinder }
        , _plane{ plane }
    {
    }

    float32 NavGridSchedulePathing::estimateTravelMinutes( const ScheduleLocation& from, const ScheduleLocation& to, float32 unitsPerMinute ) const
    {
        return SchedulePathingInternal::computeMinutes( findRoute( from, to, nullptr ), unitsPerMinute );
    }

    void NavGridSchedulePathing::makeRoute( const ScheduleLocation& from, const ScheduleLocation& to, vector<float3>& outListPoint ) const
    {
        (void)findRoute( from, to, &outListPoint );
    }

    float3 NavGridSchedulePathing::offsetInPlane( const float3& center, float32 offsetU, float32 offsetV ) const
    {
        return SchedulePathingUtil::offsetInPlane( _plane, center, offsetU, offsetV );
    }

    float3 NavGridSchedulePathing::toGrid( const float3& position ) const
    {
        if ( _plane == SchedulePlane::XZ || _pGrid == nullptr )
            return position;
        return float3{ position._x, _pGrid->getOrigin()._y, position._y };
    }

    float3 NavGridSchedulePathing::fromGrid( const float3& position ) const
    {
        if ( _plane == SchedulePlane::XZ )
            return position;
        return float3{ position._x, position._z, 0.0f };
    }

    float32 NavGridSchedulePathing::findRoute( const ScheduleLocation& from, const ScheduleLocation& to, vector<float3>* pOutListPoint ) const
    {
        if ( pOutListPoint != nullptr )
            pOutListPoint->clear();
        const float32 straight = float3::getDistance( from._position, to._position );
        if ( _pGrid == nullptr || _pPathfinder == nullptr || straight <= 0.0f )
        {
            if ( pOutListPoint != nullptr )
            {
                pOutListPoint->push_back( from._position );
                pOutListPoint->push_back( to._position );
            }
            return straight;
        }

        GridPathQuery query;
        query._start = _pGrid->computeCell( toGrid( from._position ) );
        query._goal  = _pGrid->computeCell( toGrid( to._position ) );
        int2 walkable{};
        if ( _pGrid->isWalkable( query._start ) == false && _pGrid->findNearestWalkable( query._start, SchedulePathingInternal::kNearestRadius, walkable ) )
            query._start = walkable;
        vector<int2>         listCell;
        const GridPathResult result = _pPathfinder->findPath( *_pGrid, query, listCell );
        vector<float3>       listPoint;
        listPoint.push_back( from._position );
        if ( result == GridPathResult::Found || result == GridPathResult::Partial )
        {
            // 첫 · 끝 칸 가운데 대신 실제 출발 · 도착 자리를 쓴다 — 칸 안의 자리가 그대로 이어진다.
            for ( size_t cellIndex = 1; cellIndex + 1 < listCell.size(); ++cellIndex )
            {
                listPoint.push_back( fromGrid( _pGrid->computeCellCenter( listCell[cellIndex] ) ) );
            }
        }
        listPoint.push_back( to._position );
        const float32 length = SchedulePathingUtil::computeLength( listPoint );
        if ( pOutListPoint != nullptr )
            *pOutListPoint = listPoint;
        return length;
    }

    AreaGraphSchedulePathing::AreaGraphSchedulePathing( const AreaGraph* pGraph, const GameFlags* pFlags, SchedulePlane plane )
        : _pGraph{ pGraph }
        , _pFlags{ pFlags }
        , _plane{ plane }
    {
    }

    float32 AreaGraphSchedulePathing::estimateTravelMinutes( const ScheduleLocation& from, const ScheduleLocation& to, float32 unitsPerMinute ) const
    {
        vector<hashed_string> listArea;
        vector<float3>        listPoint;
        makeAreaRoute( from, to, listArea, listPoint );
        return SchedulePathingInternal::computeMinutes( SchedulePathingUtil::computeLength( listPoint ), unitsPerMinute );
    }

    void AreaGraphSchedulePathing::makeRoute( const ScheduleLocation& from, const ScheduleLocation& to, vector<float3>& outListPoint ) const
    {
        vector<hashed_string> listArea;
        makeAreaRoute( from, to, listArea, outListPoint );
    }

    hashed_string AreaGraphSchedulePathing::computeAreaAt( const ScheduleLocation& from, const ScheduleLocation& to, float32 fraction ) const
    {
        vector<hashed_string> listArea;
        vector<float3>        listPoint;
        makeAreaRoute( from, to, listArea, listPoint );
        if ( fraction <= 0.0f )
            return from._area;
        if ( fraction >= 1.0f || listPoint.size() < 2 )
            return to._area;
        // 점 i 는 방 i 의 가운데(처음 · 끝은 실제 자리)다 — 간 거리가 넘은 점 중 마지막 점의 방이다.
        const float32 total     = SchedulePathingUtil::computeLength( listPoint );
        float32       travelled = 0.0f;
        size_t        areaIndex = 0;
        for ( size_t pointIndex = 1; pointIndex < listPoint.size(); ++pointIndex )
        {
            const float32 pieceLength = float3::getDistance( listPoint[pointIndex - 1], listPoint[pointIndex] );
            // 조각의 반을 넘으면 다음 방에 들어선 것으로 본다(문은 두 방 가운데 사이에 있다).
            if ( travelled + pieceLength * 0.5f > total * fraction )
                break;
            travelled += pieceLength;
            areaIndex = pointIndex;
        }
        return listArea[MathUtil::min( areaIndex, listArea.size() - 1 )];
    }

    float3 AreaGraphSchedulePathing::offsetInPlane( const float3& center, float32 offsetU, float32 offsetV ) const
    {
        return SchedulePathingUtil::offsetInPlane( _plane, center, offsetU, offsetV );
    }

    void AreaGraphSchedulePathing::makeAreaRoute( const ScheduleLocation& from, const ScheduleLocation& to, vector<hashed_string>& outListArea,
                                                  vector<float3>& outListPoint ) const
    {
        outListArea.clear();
        outListPoint.clear();
        vector<hashed_string> listPathArea;
        const bool            bOtherArea = from._area.empty() == false && to._area.empty() == false && from._area != to._area;
        GameFlags             noFlags;
        const GameFlags&      flags = _pFlags != nullptr ? *_pFlags : noFlags;
        if ( bOtherArea && _pGraph != nullptr && _pGraph->findPath( from._area, to._area, flags, listPathArea ) && listPathArea.size() >= 2 )
        {
            for ( size_t areaIndex = 0; areaIndex < listPathArea.size(); ++areaIndex )
            {
                outListArea.push_back( listPathArea[areaIndex] );
                const bool bEnd = areaIndex == 0 || areaIndex + 1 == listPathArea.size();
                if ( bEnd )
                    outListPoint.push_back( areaIndex == 0 ? from._position : to._position );
                else
                    outListPoint.push_back( computeAreaCenter( listPathArea[areaIndex], from._position ) );
            }
            return;
        }
        outListArea.push_back( from._area );
        outListArea.push_back( to._area );
        outListPoint.push_back( from._position );
        outListPoint.push_back( to._position );
    }

    float3 AreaGraphSchedulePathing::computeAreaCenter( const hashed_string& areaId, const float3& fallback ) const
    {
        const AreaDef* pArea = _pGraph != nullptr ? _pGraph->findArea( areaId ) : nullptr;
        if ( pArea == nullptr )
            return fallback;
        const float32 centerU = pArea->_x + pArea->_width * 0.5f;
        const float32 centerV = pArea->_y + pArea->_height * 0.5f;
        return SchedulePathingUtil::offsetInPlane( _plane, float3{}, centerU, centerV );
    }
} // namespace sw
