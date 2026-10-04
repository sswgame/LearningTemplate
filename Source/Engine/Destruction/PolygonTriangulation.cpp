#include "pch.h"

#include "Engine/Destruction/PolygonTriangulation.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct PolygonTriangulationInternal
        {
            static constexpr float32 kAreaEpsilon = 1.0e-12f;

            static float32 cross2( const float2& origin, const float2& a, const float2& b )
            {
                return ( a._x - origin._x ) * ( b._y - origin._y ) - ( a._y - origin._y ) * ( b._x - origin._x );
            }

            static bool isSamePoint( const float2& lhs, const float2& rhs ) { return lhs._x == rhs._x && lhs._y == rhs._y; }

            /** @brief 점이 삼각형(감은 방향 @p sign) 안 또는 변 위에 있는지입니다. */
            static bool isPointInTriangle( const float2& point, const float2& a, const float2& b, const float2& c, float32 sign )
            {
                const float32 edgeAb = cross2( a, b, point ) * sign;
                const float32 edgeBc = cross2( b, c, point ) * sign;
                const float32 edgeCa = cross2( c, a, point ) * sign;
                return edgeAb >= 0.0f && edgeBc >= 0.0f && edgeCa >= 0.0f;
            }

            /** @brief 두 선분이 끝점이 아닌 곳에서 엇갈리는지입니다. */
            static bool doSegmentsCross( const float2& a0, const float2& a1, const float2& b0, const float2& b1 )
            {
                const float32 side0 = cross2( a0, a1, b0 );
                const float32 side1 = cross2( a0, a1, b1 );
                const float32 side2 = cross2( b0, b1, a0 );
                const float32 side3 = cross2( b0, b1, a1 );
                return ( ( side0 > 0.0f && side1 < 0.0f ) || ( side0 < 0.0f && side1 > 0.0f ) ) && ( ( side2 > 0.0f && side3 < 0.0f ) || ( side2 < 0.0f && side3 > 0.0f ) );
            }

            /** @brief 고리 하나의 변 중 @p from → @p to 선분과 엇갈리는 것이 있는지입니다(그 두 점에 닿은 변은 뺀다). */
            static bool crossesLoop( vector_reference<const float2> listPoint, const vector<uint32>& listLoopIndex, const float2& from, const float2& to )
            {
                const size_t count = listLoopIndex.size();
                for ( size_t index = 0; index < count; ++index )
                {
                    const float2& edgeFrom = listPoint[listLoopIndex[index]];
                    const float2& edgeTo   = listPoint[listLoopIndex[( index + 1 ) % count]];
                    const bool    bTouches = isSamePoint( edgeFrom, from ) || isSamePoint( edgeFrom, to ) || isSamePoint( edgeTo, from ) || isSamePoint( edgeTo, to );
                    if ( bTouches == false && doSegmentsCross( from, to, edgeFrom, edgeTo ) )
                        return true;
                }
                return false;
            }

            /**
             * @brief 구멍을 바깥 다각형에 다리로 이어 붙입니다. 구멍의 가장 오른쪽 점에서 보이는 가장 가까운 바깥 점을 고릅니다.
             * @return 다리를 놓았으면 true 입니다.
             */
            static bool bridgeHole( vector_reference<const float2> listPoint, vector<uint32>& inoutListPolygon, const vector<uint32>& hole,
                                    const vector<vector<uint32>>& listOtherHole )
            {
                size_t holeStart = 0;
                for ( size_t index = 1; index < hole.size(); ++index )
                {
                    const float2& candidate = listPoint[hole[index]];
                    const float2& best      = listPoint[hole[holeStart]];
                    if ( candidate._x > best._x || ( candidate._x == best._x && candidate._y < best._y ) )
                        holeStart = index;
                }
                const float2& holePoint = listPoint[hole[holeStart]];

                size_t  bestPolygonIndex = inoutListPolygon.size();
                float32 bestDistance     = MathUtil::MaxFloat;
                for ( size_t index = 0; index < inoutListPolygon.size(); ++index )
                {
                    const float2& candidate = listPoint[inoutListPolygon[index]];
                    const float32 distance  = float2::getDistanceSquared( candidate, holePoint );
                    if ( distance >= bestDistance )
                        continue;
                    if ( crossesLoop( listPoint, inoutListPolygon, holePoint, candidate ) || crossesLoop( listPoint, hole, holePoint, candidate ) )
                        continue;
                    bool bBlocked = false;
                    for ( const vector<uint32>& other : listOtherHole )
                    {
                        if ( &other != &hole && crossesLoop( listPoint, other, holePoint, candidate ) )
                        {
                            bBlocked = true;
                            break;
                        }
                    }
                    if ( bBlocked )
                        continue;
                    bestDistance     = distance;
                    bestPolygonIndex = index;
                }
                if ( bestPolygonIndex == inoutListPolygon.size() )
                    return false;

                // [.. V] + [H .. 구멍 한 바퀴 .. H] + [V ..]
                vector<uint32> merged;
                merged.reserve( inoutListPolygon.size() + hole.size() + 2 );
                for ( size_t index = 0; index <= bestPolygonIndex; ++index )
                    merged.push_back( inoutListPolygon[index] );
                for ( size_t step = 0; step <= hole.size(); ++step )
                    merged.push_back( hole[( holeStart + step ) % hole.size()] );
                for ( size_t index = bestPolygonIndex; index < inoutListPolygon.size(); ++index )
                    merged.push_back( inoutListPolygon[index] );
                inoutListPolygon = std::move( merged );
                return true;
            }

            /** @brief 귀 자르기입니다. 막히면 남은 부분을 부채꼴로 막고 false 입니다. */
            static bool clipEars( vector_reference<const float2> listPoint, vector<uint32> listPolygon, float32 sign, vector<uint32>& outListIndex )
            {
                bool bComplete = true;
                while ( listPolygon.size() > 3 )
                {
                    const size_t count = listPolygon.size();
                    bool         bClip = false;
                    size_t       earAt = 0;
                    bool         bEmit = true;
                    // 정점은 삼각형 없이 빼지 않는다 — 빼면 그 정점을 쓰는 이웃 면과 이음이 끊긴다(T 자 이음). 같은 번호가 이어진 것만 그냥 뺀다.
                    // 평면에 투영해 같은 자리가 된 서로 다른 점(셋이 만나는 꼭짓점 둘레의 아주 짧은 변)은 넓이 0 인 삼각형으로 빼 이음을 지킨다.
                    for ( size_t index = 0; index < count; ++index )
                    {
                        const uint32 cur  = listPolygon[index];
                        const uint32 next = listPolygon[( index + 1 ) % count];
                        if ( cur == next || isSamePoint( listPoint[cur], listPoint[next] ) )
                        {
                            earAt = index;
                            bEmit = cur != next;
                            bClip = true;
                            break;
                        }
                    }
                    for ( size_t index = 0; bClip == false && index < count; ++index )
                    {
                        const float2& prev = listPoint[listPolygon[( index + count - 1 ) % count]];
                        const float2& cur  = listPoint[listPolygon[index]];
                        const float2& next = listPoint[listPolygon[( index + 1 ) % count]];
                        const float32 turn = cross2( prev, cur, next ) * sign;
                        if ( turn <= 0.0f )
                            continue;
                        bool bContainsOther = false;
                        for ( size_t other = 0; other < count; ++other )
                        {
                            const float2& point = listPoint[listPolygon[other]];
                            if ( isSamePoint( point, prev ) || isSamePoint( point, cur ) || isSamePoint( point, next ) )
                                continue;
                            if ( isPointInTriangle( point, prev, cur, next, sign ) )
                            {
                                bContainsOther = true;
                                break;
                            }
                        }
                        if ( bContainsOther )
                            continue;
                        earAt = index;
                        bClip = true;
                    }
                    if ( bClip == false )
                    {
                        bComplete = false;
                        break;
                    }
                    if ( bEmit )
                    {
                        outListIndex.push_back( listPolygon[( earAt + count - 1 ) % count] );
                        outListIndex.push_back( listPolygon[earAt] );
                        outListIndex.push_back( listPolygon[( earAt + 1 ) % count] );
                    }
                    listPolygon.erase( listPolygon.begin() + static_cast<std::ptrdiff_t>( earAt ) );
                }
                if ( listPolygon.size() == 3 )
                {
                    const bool bDistinct = listPolygon[0] != listPolygon[1] && listPolygon[1] != listPolygon[2] && listPolygon[2] != listPolygon[0];
                    if ( bDistinct )
                    {
                        outListIndex.push_back( listPolygon[0] );
                        outListIndex.push_back( listPolygon[1] );
                        outListIndex.push_back( listPolygon[2] );
                    }
                    return bComplete;
                }
                // 막힌 나머지(자기 교차) — 부채꼴로 막는다.
                for ( size_t index = 1; index + 1 < listPolygon.size(); ++index )
                {
                    outListIndex.push_back( listPolygon[0] );
                    outListIndex.push_back( listPolygon[index] );
                    outListIndex.push_back( listPolygon[index + 1] );
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float32 PolygonTriangulationUtil::computeSignedArea( vector_reference<const float2> listPoint, vector_reference<const uint32> listLoopIndex )
    {
        float32      twiceArea = 0.0f;
        const size_t count     = listLoopIndex.size();
        for ( size_t index = 0; index < count; ++index )
        {
            const float2& from = listPoint[listLoopIndex[index]];
            const float2& to   = listPoint[listLoopIndex[( index + 1 ) % count]];
            twiceArea += from._x * to._y - to._x * from._y;
        }
        return twiceArea * 0.5f;
    }

    bool PolygonTriangulationUtil::isPointInLoop( vector_reference<const float2> listPoint, vector_reference<const uint32> listLoopIndex, const float2& point )
    {
        bool         bInside = false;
        const size_t count   = listLoopIndex.size();
        for ( size_t index = 0, prevIndex = count - 1; index < count; prevIndex = index++ )
        {
            const float2& from      = listPoint[listLoopIndex[index]];
            const float2& to        = listPoint[listLoopIndex[prevIndex]];
            const bool    bStraddle = ( from._y > point._y ) != ( to._y > point._y );
            if ( bStraddle && point._x < ( to._x - from._x ) * ( point._y - from._y ) / ( to._y - from._y ) + from._x )
                bInside = bInside == false;
        }
        return bInside;
    }

    bool PolygonTriangulationUtil::triangulate( vector_reference<const float2> listPoint, const vector<vector<uint32>>& listLoop, vector<uint32>& outListIndex )
    {
        vector<float32> listArea;
        listArea.reserve( listLoop.size() );
        float32 largestArea = 0.0f;
        float32 sign        = 1.0f;
        for ( const vector<uint32>& listLoopIndex : listLoop )
        {
            const float32 area = listLoopIndex.size() >= 3 ? computeSignedArea( listPoint, listLoopIndex ) : 0.0f;
            listArea.push_back( area );
            if ( MathUtil::abs( area ) > largestArea )
            {
                largestArea = MathUtil::abs( area );
                sign        = area >= 0.0f ? 1.0f : -1.0f;
            }
        }
        if ( largestArea <= PolygonTriangulationInternal::kAreaEpsilon )
            return true;

        vector<uint32> listShellLoop;
        vector<uint32> listHole;
        for ( uint32 loopIndex = 0; loopIndex < static_cast<uint32>( listLoop.size() ); ++loopIndex )
        {
            const float32 area = listArea[loopIndex] * sign;
            if ( area > PolygonTriangulationInternal::kAreaEpsilon )
                listShellLoop.push_back( loopIndex );
            else if ( area < -PolygonTriangulationInternal::kAreaEpsilon )
                listHole.push_back( loopIndex );
        }

        // 구멍마다 그것을 품은 가장 작은 바깥 고리를 고른다.
        vector<vector<vector<uint32>>> listHoleOfOuter( listShellLoop.size() );
        for ( const uint32 holeIndex : listHole )
        {
            const vector<uint32>& hole      = listLoop[holeIndex];
            const float2&         probe     = listPoint[hole[0]];
            size_t                bestOuter = listShellLoop.size();
            float32               bestArea  = MathUtil::MaxFloat;
            for ( size_t outer = 0; outer < listShellLoop.size(); ++outer )
            {
                const float32 area = MathUtil::abs( listArea[listShellLoop[outer]] );
                if ( area < bestArea && isPointInLoop( listPoint, listLoop[listShellLoop[outer]], probe ) )
                {
                    bestArea  = area;
                    bestOuter = outer;
                }
            }
            if ( bestOuter < listShellLoop.size() )
                listHoleOfOuter[bestOuter].push_back( hole );
        }

        bool bComplete = true;
        for ( size_t outer = 0; outer < listShellLoop.size(); ++outer )
        {
            vector<uint32>          listPolygon = listLoop[listShellLoop[outer]];
            vector<vector<uint32>>& listOwnHole = listHoleOfOuter[outer];
            // 오른쪽 구멍부터 잇는다 — 먼저 이은 다리가 뒤 구멍의 시야를 덜 막는다.
            vector<size_t> listOrder( listOwnHole.size() );
            for ( size_t index = 0; index < listOrder.size(); ++index )
                listOrder[index] = index;
            std::sort( listOrder.begin(), listOrder.end(), [&listOwnHole, &listPoint]( size_t lhs, size_t rhs )
            {
                float32 lhsMax = -MathUtil::MaxFloat;
                float32 rhsMax = -MathUtil::MaxFloat;
                for ( const uint32 pointIndex : listOwnHole[lhs] )
                    lhsMax = MathUtil::max( lhsMax, listPoint[pointIndex]._x );
                for ( const uint32 pointIndex : listOwnHole[rhs] )
                    rhsMax = MathUtil::max( rhsMax, listPoint[pointIndex]._x );
                return lhsMax > rhsMax || ( lhsMax == rhsMax && lhs < rhs );
            } );
            for ( const size_t holeOrder : listOrder )
            {
                if ( PolygonTriangulationInternal::bridgeHole( listPoint, listPolygon, listOwnHole[holeOrder], listOwnHole ) == false )
                    bComplete = false;
            }
            if ( PolygonTriangulationInternal::clipEars( listPoint, std::move( listPolygon ), sign, outListIndex ) == false )
                bComplete = false;
        }
        return bComplete;
    }
} // namespace sw
