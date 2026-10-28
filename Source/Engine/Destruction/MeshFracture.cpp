#include "pch.h"

#include "Engine/Destruction/MeshFracture.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Math/DeterministicMathUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Spatial/PositionWelder.h"

#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/GeometryCut.h"
#include "Engine/Destruction/DestructionRandom.h"
#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/FractureGraph.h"
#include "Engine/Destruction/FractureSiteUtil.h"
#include "Engine/Destruction/PolygonTriangulation.h"

namespace sw
{
    SW_LOG_CALLER( "MeshFracture" );

    namespace
    {
        struct MeshFractureInternal
        {
            static constexpr int32   kOuterTag     = -1;
            static constexpr int32   kSurfaceTag   = -2;      ///< 대리 부피를 쪼갤 때의 원래 겉면 태그다(막지 않고 자른다).
            static constexpr float32 kPlaneEpsilon = 1.0e-5f; ///< 이 거리(미터) 안의 정점은 자르는 평면 위에 있는 것으로 본다.

            /** @brief 삼각형 하나와, 안쪽 면이면 그 면을 만든 이웃 시드입니다. */
            struct Triangle
            {
                RHIVertex _arrVertex[3];
                int32     _tag;
            };

            /** @brief 자른 곳의 선분 하나입니다. 남는 쪽 면의 경계 방향이며, 나가는 점에서 들어오는 점으로 향합니다. */
            struct Segment
            {
                float3 _from;
                float3 _to;
            };

            static float3 getPosition( const RHIVertex& vertex ) { return float3{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] }; }

            static bool isLess( const float3& lhs, const float3& rhs )
            {
                if ( lhs._x != rhs._x )
                    return lhs._x < rhs._x;
                if ( lhs._y != rhs._y )
                    return lhs._y < rhs._y;
                return lhs._z < rhs._z;
            }

            static bool isSamePoint( const float3& lhs, const float3& rhs ) { return lhs._x == rhs._x && lhs._y == rhs._y && lhs._z == rhs._z; }

            static uint64 makePointKey( const float3& point )
            {
                uint32 arrBit[3] = { 0, 0, 0 };
                Memory::copy( &arrBit[0], &point._x, 4 );
                Memory::copy( &arrBit[1], &point._y, 4 );
                Memory::copy( &arrBit[2], &point._z, 4 );
                uint64 hash = HashUtil::kFNVOffset64;
                for ( const uint32 bits : arrBit )
                {
                    hash = HashUtil::fnvStep64( hash, bits );
                    hash ^= hash >> 29;
                }
                return hash;
            }

            static uint64 makeEdgeKey( uint64 from, uint64 to ) { return from * HashUtil::kGoldenRatio64 ^ ( to + 0x632BE59BD9B4E019ull + ( from << 6 ) + ( from >> 2 ) ); }

            /**
             * @brief 허용 오차(`PositionWelder::kDefaultTolerance`) 안의 위치를 처음 본 위치로 맞춥니다.
             * @details 셀 꼭짓점(세 평면이 만나는 점)은 서로 다른 모서리에서 따로 구해져 1e-7 m 쯤 어긋난 두 점이 된다.
             *          그대로 두면 아주 짧은 변이 생기고, 다음 자르기에서 그 둘을 다른 점으로 다뤄 한 모서리에 면이 넷 붙은 조각이 남는다.
             */
            static float3 weldPosition( PositionWelder& inoutWelder, const float3& position ) { return inoutWelder.getPoint( inoutWelder.weld( position ) ); }

            static void setPosition( RHIVertex& inoutVertex, const float3& position )
            {
                inoutVertex._arrPosition[0] = position._x;
                inoutVertex._arrPosition[1] = position._y;
                inoutVertex._arrPosition[2] = position._z;
            }

            /** @brief 허용 오차 안의 위치를 처음 본 위치로 맞춥니다(입력 메시). */
            static void weldPositions( vector<RHIVertex>& inoutListVertex )
            {
                PositionWelder welder;
                for ( RHIVertex& vertex : inoutListVertex )
                {
                    setPosition( vertex, weldPosition( welder, getPosition( vertex ) ) );
                }
            }

            static RHIVertex lerpVertex( const RHIVertex& from, const RHIVertex& to, float32 t )
            {
                RHIVertex result{};
                for ( uint32 axis = 0; axis < 3; ++axis )
                {
                    result._arrPosition[axis] = from._arrPosition[axis] + ( to._arrPosition[axis] - from._arrPosition[axis] ) * t;
                    result._arrNormal[axis]   = from._arrNormal[axis] + ( to._arrNormal[axis] - from._arrNormal[axis] ) * t;
                }
                for ( uint32 axis = 0; axis < 2; ++axis )
                {
                    result._arrUv[axis] = from._arrUv[axis] + ( to._arrUv[axis] - from._arrUv[axis] ) * t;
                }
                for ( uint32 axis = 0; axis < 4; ++axis )
                {
                    result._arrColor[axis] = from._arrColor[axis] + ( to._arrColor[axis] - from._arrColor[axis] ) * t;
                }
                const float3 normal  = float3{ result._arrNormal[0], result._arrNormal[1], result._arrNormal[2] };
                const float3 unit    = CharacterGeometryUtil::makeUnitOr( normal, float3{ 0.0f, 1.0f, 0.0f } );
                result._arrNormal[0] = unit._x;
                result._arrNormal[1] = unit._y;
                result._arrNormal[2] = unit._z;
                return result;
            }

            /**
             * @brief 모서리 위의 교점을 구합니다. 엔드포인트를 위치 순으로 정렬해 구하므로, 그 모서리를 나누는 두 삼각형이 비트까지 같은 위치를 얻습니다.
             *        한 엔드포인트가 평면 위(거리 0)면 그 엔드포인트를 그대로 씁니다.
             */
            static RHIVertex intersect( const RHIVertex& vertexA, float32 distanceA, const RHIVertex& vertexB, float32 distanceB )
            {
                if ( distanceA == 0.0f )
                    return vertexA;
                if ( distanceB == 0.0f )
                    return vertexB;
                if ( isLess( getPosition( vertexB ), getPosition( vertexA ) ) )
                {
                    const float32 t = distanceB / ( distanceB - distanceA );
                    return lerpVertex( vertexB, vertexA, t );
                }
                const float32 t = distanceA / ( distanceA - distanceB );
                return lerpVertex( vertexA, vertexB, t );
            }

            static float32 computeTriangleArea( const float3& a, const float3& b, const float3& c ) { return ( b - a ).cross( c - a ).getLength() * 0.5f; }

            /** @brief 세 꼭짓점이 모두 다른 위치인지 반환합니다(길이 0 인 변이 없는지). */
            static bool hasDistinctCorners( const Triangle& triangle )
            {
                const float3 a = getPosition( triangle._arrVertex[0] );
                const float3 b = getPosition( triangle._arrVertex[1] );
                const float3 c = getPosition( triangle._arrVertex[2] );
                return isSamePoint( a, b ) == false && isSamePoint( b, c ) == false && isSamePoint( c, a ) == false;
            }

            /** @brief 평면 기준 두 축입니다(u × v = n). */
            static void makePlaneBasis( const float3& normal, float3& outU, float3& outV )
            {
                const float3 helper = MathUtil::abs( normal._y ) < 0.9f ? float3{ 0.0f, 1.0f, 0.0f } : float3{ 1.0f, 0.0f, 0.0f };
                outU                = helper.cross( normal ).normalize();
                outV                = normal.cross( outU );
            }

            /** @brief 선분을 이어 닫힌 고리들을 만듭니다. 닫히지 않는 체인은 버리고 그 수를 반환합니다. */
            static uint32 linkSegments( const vector<Segment>& listSegment, vector<vector<float3>>& outListLoop )
            {
                unordered_map<uint64, vector<uint32>> mapStart;
                mapStart.reserve( listSegment.size() );
                for ( uint32 index = 0; index < static_cast<uint32>( listSegment.size() ); ++index )
                {
                    mapStart[makePointKey( listSegment[index]._from )].push_back( index );
                }
                vector<uint8> listUsed( listSegment.size(), 0 );
                uint32        openCount = 0;
                for ( uint32 start = 0; start < static_cast<uint32>( listSegment.size() ); ++start )
                {
                    if ( listUsed[start] != 0 )
                        continue;
                    vector<float3> listLoopPoint;
                    uint32         current = start;
                    bool           bClosed = false;
                    for ( ;; )
                    {
                        listUsed[current] = 1;
                        listLoopPoint.push_back( listSegment[current]._from );
                        const float3& end = listSegment[current]._to;
                        if ( isSamePoint( end, listSegment[start]._from ) )
                        {
                            bClosed = true;
                            break;
                        }
                        const auto iter = mapStart.find( makePointKey( end ) );
                        uint32     next = 0xFFFFFFFFu;
                        if ( iter != mapStart.end() )
                        {
                            for ( const uint32 candidate : iter->second )
                            {
                                if ( listUsed[candidate] == 0 && isSamePoint( listSegment[candidate]._from, end ) )
                                {
                                    next = candidate;
                                    break;
                                }
                            }
                        }
                        if ( next == 0xFFFFFFFFu )
                            break;
                        current = next;
                    }
                    if ( bClosed && listLoopPoint.size() >= 3 )
                        outListLoop.push_back( std::move( listLoopPoint ) );
                    else if ( bClosed == false )
                        ++openCount;
                }
                return openCount;
            }

            /**
             * @brief 삼각형들을 평면 n·x ≤ c 쪽만 남기고 자릅니다. 평면에 걸친 삼각형은 서덜랜드-호지먼 알고리즘으로 자르고, 자른 곳의 선분을 모읍니다.
             */
            static void clipTriangles( const vector<Triangle>& listInput, const float3& normal, float32 offset, vector<Triangle>& outListOutput, vector<Segment>& outListSegment )
            {
                outListOutput.clear();
                outListOutput.reserve( listInput.size() + 16 );
                for ( const Triangle& triangle : listInput )
                {
                    float32 arrDistance[3];
                    uint32  keptCount = 0;
                    for ( uint32 corner = 0; corner < 3; ++corner )
                    {
                        arrDistance[corner] = normal.dot( getPosition( triangle._arrVertex[corner] ) ) - offset;
                        // 평면에 아주 가까운 정점은 평면 위에 있는 것으로 본다. 정점마다 정하므로 이웃 삼각형이 같은 판정을 받고, 셀 꼭짓점 둘레에 아주 짧은 변이 생기지 않는다.
                        if ( MathUtil::abs( arrDistance[corner] ) <= kPlaneEpsilon )
                            arrDistance[corner] = 0.0f;
                        if ( arrDistance[corner] <= 0.0f )
                            ++keptCount;
                    }
                    if ( keptCount == 3 )
                    {
                        outListOutput.push_back( triangle );
                        continue;
                    }
                    if ( keptCount == 0 )
                        continue;

                    // 서덜랜드-호지먼 알고리즘이다. 남는 다각형(볼록, 4 개 이하)과 나가는 점, 들어오는 점을 구한다.
                    RHIVertex arrPolygon[4];
                    uint32    polygonCount = 0;
                    RHIVertex exitVertex{};
                    RHIVertex entryVertex{};
                    for ( uint32 corner = 0; corner < 3; ++corner )
                    {
                        const uint32 next      = ( corner + 1 ) % 3;
                        const bool   bCurKept  = arrDistance[corner] <= 0.0f;
                        const bool   bNextKept = arrDistance[next] <= 0.0f;
                        if ( bCurKept )
                            arrPolygon[polygonCount++] = triangle._arrVertex[corner];
                        if ( bCurKept && bNextKept == false )
                        {
                            exitVertex                 = intersect( triangle._arrVertex[corner], arrDistance[corner], triangle._arrVertex[next], arrDistance[next] );
                            arrPolygon[polygonCount++] = exitVertex;
                        }
                        else if ( bCurKept == false && bNextKept )
                        {
                            entryVertex                = intersect( triangle._arrVertex[corner], arrDistance[corner], triangle._arrVertex[next], arrDistance[next] );
                            arrPolygon[polygonCount++] = entryVertex;
                        }
                    }
                    for ( uint32 corner = 1; corner + 1 < polygonCount; ++corner )
                    {
                        Triangle piece;
                        piece._arrVertex[0] = arrPolygon[0];
                        piece._arrVertex[1] = arrPolygon[corner];
                        piece._arrVertex[2] = arrPolygon[corner + 1];
                        piece._tag          = triangle._tag;
                        if ( hasDistinctCorners( piece ) )
                            outListOutput.push_back( piece );
                    }
                    const float3 exitPoint  = getPosition( exitVertex );
                    const float3 entryPoint = getPosition( entryVertex );
                    if ( isSamePoint( exitPoint, entryPoint ) == false )
                        outListSegment.push_back( Segment{ exitPoint, entryPoint } );
                }
            }

            /**
             * @brief 자른 곳의 선분으로 단면을 막는 삼각형을 만들어 더합니다. 남는 면의 경계는 나가는 점에서 들어오는 점 방향이므로, 캡은 그 반대로 감습니다(메시 앞면 규약과 같아진다).
             * @return 닫히지 않은 고리 수입니다.
             */
            static uint32 appendCap( const vector<Segment>& listSegment, const float3& normal, int32 tag, const FractureSettings& settings, vector<Triangle>& inoutListOutput )
            {
                vector<vector<float3>> listLoop;
                const uint32           openCount = linkSegments( listSegment, listLoop );

                float3 axisU{};
                float3 axisV{};
                makePlaneBasis( normal, axisU, axisV );
                vector<float3>         listPoint3;
                vector<float2>         listPoint2;
                vector<vector<uint32>> listLoopIndex;
                for ( vector<float3>& listLoopPoint : listLoop )
                {
                    vector<uint32> listCornerIndex;
                    for ( size_t index = listLoopPoint.size(); index > 0; --index )
                    {
                        const float3& point = listLoopPoint[index - 1];
                        listCornerIndex.push_back( static_cast<uint32>( listPoint3.size() ) );
                        listPoint3.push_back( point );
                        listPoint2.push_back( float2{ point.dot( axisU ), point.dot( axisV ) } );
                    }
                    listLoopIndex.push_back( std::move( listCornerIndex ) );
                }
                vector<uint32> listIndex;
                (void)PolygonTriangulationUtil::triangulate( listPoint2, listLoopIndex, listIndex ); // 실패하면 빈 목록이라 단면이 비고, 열린 고리 수로 알린다
                for ( size_t index = 0; index + 2 < listIndex.size(); index += 3 )
                {
                    Triangle cap;
                    cap._tag = tag;
                    for ( uint32 corner = 0; corner < 3; ++corner )
                    {
                        const uint32  pointIndex = listIndex[index + corner];
                        const float3& point      = listPoint3[pointIndex];
                        RHIVertex&    vertex     = cap._arrVertex[corner];
                        vertex._arrPosition[0]   = point._x;
                        vertex._arrPosition[1]   = point._y;
                        vertex._arrPosition[2]   = point._z;
                        vertex._arrNormal[0]     = normal._x;
                        vertex._arrNormal[1]     = normal._y;
                        vertex._arrNormal[2]     = normal._z;
                        vertex._arrUv[0]         = listPoint2[pointIndex]._x * settings._interiorUvScale;
                        vertex._arrUv[1]         = listPoint2[pointIndex]._y * settings._interiorUvScale;
                        vertex._arrColor[0]      = settings._interiorColor._x;
                        vertex._arrColor[1]      = settings._interiorColor._y;
                        vertex._arrColor[2]      = settings._interiorColor._z;
                        vertex._arrColor[3]      = settings._interiorColor._w;
                    }
                    // 넓이가 0 에 가까워도 남긴다. 일직선 세 점의 삼각형이 빠지면 옆면과의 이음이 끊긴다. 같은 점이 겹친 것만 뺀다.
                    if ( hasDistinctCorners( cap ) )
                        inoutListOutput.push_back( cap );
                }
                return openCount;
            }

            /**
             * @brief 자른 곳에 아주 가까운 두 점이 생겼는지 확인합니다. 셀 꼭짓점(세 평면이 만나는 점)은 서로 다른 모서리에서 따로 구해져 1e-7 m 쯤 어긋난
             *        두 점이 됩니다. 그대로 두면 다음 자르기에서 그 둘을 다른 점으로 다뤄 한 모서리에 면이 넷 붙습니다. 생겼으면 이 자르기 뒤에 조각 전체를 다듬습니다.
             */
            static bool hasNearDuplicate( const vector<Segment>& listSegment )
            {
                PositionWelder welder;
                bool           bNearDuplicate = false;
                for ( const Segment& segment : listSegment )
                {
                    const float3 weldedFrom = weldPosition( welder, segment._from );
                    const float3 weldedTo   = weldPosition( welder, segment._to );
                    if ( isSamePoint( weldedFrom, segment._from ) == false || isSamePoint( weldedTo, segment._to ) == false )
                        bNearDuplicate = true;
                }
                return bNearDuplicate;
            }

            /**
             * @brief 조각을 평면 n·x ≤ c 쪽만 남기고 자릅니다. 자른 단면은 안쪽 면(태그 @p tag)으로 막습니다.
             * @return 닫히지 않은 고리 수입니다(입력이 닫혔으면 0).
             */
            static uint32 clipPiece( const vector<Triangle>& listInput, const float3& normal, float32 offset, int32 tag, const FractureSettings& settings,
                                     vector<Triangle>& outListOutput, bool& outbNearDuplicate, bool bCap = true )
            {
                outbNearDuplicate = false;
                vector<Segment> listSegment;
                clipTriangles( listInput, normal, offset, outListOutput, listSegment );
                const bool bNoCapNeeded = listSegment.empty() || outListOutput.empty() || bCap == false;
                if ( bNoCapNeeded )
                    return 0;
                outbNearDuplicate = hasNearDuplicate( listSegment );
                return appendCap( listSegment, normal, tag, settings, outListOutput );
            }

            static void collectBounds( vector_reference<const RHIVertex> listVertex, float3& outMin, float3& outMax )
            {
                outMin = float3{ MathUtil::kMaxFloat };
                outMax = float3{ MathUtil::kMinFloat };
                for ( const RHIVertex& vertex : listVertex )
                {
                    outMin = float3::min( outMin, getPosition( vertex ) );
                    outMax = float3::max( outMax, getPosition( vertex ) );
                }
            }

            /** @brief 시드 점을 놓습니다(메시 안의 점만). */
            static void placeSites( vector_reference<const RHIVertex> listVertex, const FractureSettings& settings, vector<float3>& outListSite )
            {
                float3 boundsMin{};
                float3 boundsMax{};
                collectBounds( listVertex, boundsMin, boundsMax );
                const float3      extent = boundsMax - boundsMin;
                DestructionRandom random{ settings._seed };
                const auto        isInsideMesh = [&listVertex]( const float3& point )
                { return MeshFractureUtil::isPointInside( listVertex, point ); };

                if ( settings._pattern == FracturePattern::Slices )
                {
                    uint32 arrCount[3];
                    for ( uint32 axis = 0; axis < 3; ++axis )
                    {
                        arrCount[axis] = MathUtil::max( settings._arrSliceCount[axis], 1u );
                    }
                    const float32 jitter = MathUtil::clamp( settings._sliceJitter, 0.0f, 0.5f );
                    for ( uint32 cellZ = 0; cellZ < arrCount[2]; ++cellZ )
                    {
                        for ( uint32 cellY = 0; cellY < arrCount[1]; ++cellY )
                        {
                            for ( uint32 cellX = 0; cellX < arrCount[0]; ++cellX )
                            {
                                const uint32 arrCell[3] = { cellX, cellY, cellZ };
                                float32      arrCoord[3];
                                for ( uint32 axis = 0; axis < 3; ++axis )
                                {
                                    const float32 cellSize = ( &extent._x )[axis] / static_cast<float32>( arrCount[axis] );
                                    float32       offset   = 0.5f;
                                    if ( arrCount[axis] > 1 )
                                        offset += random.nextRange( -jitter, jitter );
                                    arrCoord[axis] = ( &boundsMin._x )[axis] + cellSize * ( static_cast<float32>( arrCell[axis] ) + offset );
                                }
                                FractureSiteUtil::addIfInside( outListSite, float3{ arrCoord[0], arrCoord[1], arrCoord[2] }, isInsideMesh );
                            }
                        }
                    }
                    return;
                }

                const uint32 target     = MathUtil::max( settings._pieceCount, 1u );
                uint32       nearTarget = 0;
                if ( settings._pattern == FracturePattern::Clustered )
                    nearTarget = static_cast<uint32>( MathUtil::round( static_cast<float32>( target ) * MathUtil::saturate( settings._clusterFraction ) ) );
                uint32 attempt = 0;
                while ( static_cast<uint32>( outListSite.size() ) < nearTarget && attempt < nearTarget * FractureSiteUtil::kAttemptFactor )
                {
                    ++attempt;
                    const float3 point = settings._impactPoint + random.nextPointInUnitSphere() * settings._clusterRadius;
                    FractureSiteUtil::addIfInside( outListSite, point, isInsideMesh );
                }
                attempt = 0;
                while ( static_cast<uint32>( outListSite.size() ) < target && attempt < target * FractureSiteUtil::kAttemptFactor )
                {
                    ++attempt;
                    const float3 point{ boundsMin._x + extent._x * random.nextFloat(), boundsMin._y + extent._y * random.nextFloat(),
                                        boundsMin._z + extent._z * random.nextFloat() };
                    FractureSiteUtil::addIfInside( outListSite, point, isInsideMesh );
                }
            }

            /** @brief 껍질 점을 구합니다. 고유한 위치만 쓰고, 너무 많으면 고른 방향(피보나치 구)마다 가장 먼 점만 남깁니다. */
            static void makeHull( const vector<Triangle>& listTriangle, const float3& centroid, uint32 maxPoint, vector<float3>& outListPoint )
            {
                vector<float3>                listUnique;
                unordered_map<uint64, uint32> mapSeen;
                for ( const Triangle& triangle : listTriangle )
                {
                    for ( const RHIVertex& vertex : triangle._arrVertex )
                    {
                        const float3 point = getPosition( vertex );
                        const uint64 key   = makePointKey( point );
                        if ( mapSeen.find( key ) != mapSeen.end() )
                            continue;
                        mapSeen.emplace( key, static_cast<uint32>( listUnique.size() ) );
                        listUnique.push_back( point - centroid );
                    }
                }
                if ( listUnique.size() <= maxPoint )
                {
                    outListPoint = std::move( listUnique );
                    return;
                }
                vector<uint8> listPicked( listUnique.size(), 0 );
                const float32 goldenAngle = MathUtil::kPi * ( 3.0f - MathUtil::sqrt( 5.0f ) );
                for ( uint32 direction = 0; direction < maxPoint; ++direction )
                {
                    const float32 y      = 1.0f - 2.0f * ( static_cast<float32>( direction ) + 0.5f ) / static_cast<float32>( maxPoint );
                    const float32 radius = MathUtil::sqrt( MathUtil::max( 0.0f, 1.0f - y * y ) );
                    const float32 angle  = goldenAngle * static_cast<float32>( direction );
                    const float3  axis{ DeterministicMathUtil::cos( angle ) * radius, y, DeterministicMathUtil::sin( angle ) * radius };
                    size_t        best      = 0;
                    float32       bestValue = -MathUtil::kMaxFloat;
                    for ( size_t index = 0; index < listUnique.size(); ++index )
                    {
                        const float32 value = listUnique[index].dot( axis );
                        if ( value > bestValue )
                        {
                            bestValue = value;
                            best      = index;
                        }
                    }
                    listPicked[best] = 1;
                }
                for ( size_t index = 0; index < listUnique.size(); ++index )
                {
                    if ( listPicked[index] != 0 )
                        outListPoint.push_back( listUnique[index] );
                }
            }

            /**
             * @brief 조각을 다듬습니다. 아주 가까운 위치를 하나로 용접하고, 꼭짓점이 겹친 삼각형과 서로 뒤집힌 같은 삼각형 쌍(두께 0 인 지느러미)을 뺍니다.
             *        셋 모두 닫힘을 유지합니다(빠지는 것은 스스로 상쇄되는 변뿐입니다).
             */
            static void cleanPiece( vector<Triangle>& inoutListTriangle )
            {
                PositionWelder welder;
                for ( Triangle& triangle : inoutListTriangle )
                {
                    for ( RHIVertex& vertex : triangle._arrVertex )
                    {
                        setPosition( vertex, weldPosition( welder, getPosition( vertex ) ) );
                    }
                }
                vector<Triangle> listKept;
                listKept.reserve( inoutListTriangle.size() );
                for ( const Triangle& triangle : inoutListTriangle )
                {
                    if ( hasDistinctCorners( triangle ) )
                        listKept.push_back( triangle );
                }
                // 뒤집힌 쌍: 꼭짓점 키를 가장 작은 것부터 돌려 세운 순서(감은 방향 유지)로 묶는다.
                unordered_map<uint64, vector<uint32>> mapTriangle;
                vector<uint8>                         listRemoved( listKept.size(), 0 );
                for ( uint32 index = 0; index < static_cast<uint32>( listKept.size() ); ++index )
                {
                    uint64 arrKey[3];
                    for ( uint32 corner = 0; corner < 3; ++corner )
                    {
                        arrKey[corner] = makePointKey( getPosition( listKept[index]._arrVertex[corner] ) );
                    }
                    uint32 first = 0;
                    for ( uint32 corner = 1; corner < 3; ++corner )
                    {
                        if ( arrKey[corner] < arrKey[first] )
                            first = corner;
                    }
                    const uint64 key0     = arrKey[first];
                    const uint64 key1     = arrKey[( first + 1 ) % 3];
                    const uint64 key2     = arrKey[( first + 2 ) % 3];
                    const uint64 forward  = makeEdgeKey( makeEdgeKey( key0, key1 ), key2 );
                    const uint64 backward = makeEdgeKey( makeEdgeKey( key0, key2 ), key1 );
                    const auto   iter     = mapTriangle.find( backward );
                    if ( iter != mapTriangle.end() && iter->second.empty() == false )
                    {
                        listRemoved[iter->second.back()] = 1;
                        listRemoved[index]               = 1;
                        iter->second.pop_back();
                        continue;
                    }
                    mapTriangle[forward].push_back( index );
                }
                inoutListTriangle.clear();
                for ( uint32 index = 0; index < static_cast<uint32>( listKept.size() ); ++index )
                {
                    if ( listRemoved[index] == 0 )
                        inoutListTriangle.push_back( listKept[index] );
                }
            }

            static void addFace( vector<RHIVertex>& inoutList, const float3& a, const float3& b, const float3& c )
            {
                const float3 normal = CharacterGeometryUtil::makeUnitOr( ( b - a ).cross( c - a ), float3{ 0.0f, 1.0f, 0.0f } );
                for ( const float3* pPoint : { &a, &b, &c } )
                {
                    RHIVertex vertex{};
                    vertex._arrPosition[0] = pPoint->_x;
                    vertex._arrPosition[1] = pPoint->_y;
                    vertex._arrPosition[2] = pPoint->_z;
                    vertex._arrNormal[0]   = normal._x;
                    vertex._arrNormal[1]   = normal._y;
                    vertex._arrNormal[2]   = normal._z;
                    for ( float32& channel : vertex._arrColor )
                    {
                        channel = 1.0f;
                    }
                    inoutList.push_back( vertex );
                }
            }

            /** @brief 경계 상자(바깥 반시계)입니다. */
            static void makeBoundsVolume( vector_reference<const RHIVertex> listVertex, vector<RHIVertex>& outListVertex )
            {
                float3 lo{};
                float3 hi{};
                collectBounds( listVertex, lo, hi );
                outListVertex.clear();
                const float3 arrCorner[8] = {
                    float3{lo._x, lo._y, lo._z},
                    float3{hi._x, lo._y, lo._z},
                    float3{hi._x, hi._y, lo._z},
                    float3{lo._x, hi._y, lo._z},
                    float3{lo._x, lo._y, hi._z},
                    float3{hi._x, lo._y, hi._z},
                    float3{hi._x, hi._y, hi._z},
                    float3{lo._x, hi._y, hi._z}
                };
                const uint32 arrQuad[6][4] = {
                    {1, 2, 6, 5},
                    {4, 7, 3, 0},
                    {3, 7, 6, 2},
                    {4, 0, 1, 5},
                    {4, 5, 6, 7},
                    {1, 0, 3, 2}
                };
                for ( const auto& quad : arrQuad )
                {
                    addFace( outListVertex, arrCorner[quad[0]], arrCorner[quad[1]], arrCorner[quad[2]] );
                    addFace( outListVertex, arrCorner[quad[0]], arrCorner[quad[2]], arrCorner[quad[3]] );
                }
            }

            /** @brief `simplifyCaps` 가 단계마다 넘겨 쓰는 값입니다. 안쪽 면 삼각형을 태그(평면)마다 그룹으로 묶어 둡니다. */
            struct CapState
            {
                vector<int32>                  _listTag;           ///< 그룹마다 태그
                vector<vector<uint32>>         _listGroupTriangle; ///< 그룹마다 삼각형 번호
                unordered_map<uint64, uint8>   _mapSurfaceUse;     ///< 점마다 겉면이 쓰는지
                unordered_map<uint64, float3>  _mapPoint;
                vector<vector<vector<uint64>>> _listGroupLoop; ///< 그룹마다 경계 고리(점 키)
                unordered_map<uint64, uint8>   _mapKeep;       ///< 빼면 안 되는 점
                vector<uint8>                  _listRebuild;   ///< 다시 만들 그룹

                /** @brief 점을 뺄 수 있는지 반환합니다. 겉면이 쓰지 않고 남길 점으로 표시되지 않은 점만 뺍니다. */
                bool isRemovable( uint64 key ) { return _mapKeep.find( key ) == _mapKeep.end() && _mapSurfaceUse[key] == SW_FALSE; }

                /** @brief 그룹 삼각형이 쓰는 점을 모두 남길 점으로 표시합니다. */
                void keepGroupPoints( const vector<Triangle>& listTriangle, size_t group )
                {
                    for ( const uint32 index : _listGroupTriangle[group] )
                    {
                        for ( const RHIVertex& vertex : listTriangle[index]._arrVertex )
                        {
                            _mapKeep[makePointKey( getPosition( vertex ) )] = SW_TRUE;
                        }
                    }
                }
            };

            /** @brief 안쪽 면(태그가 0 이상) 삼각형을 태그마다 그룹으로 묶습니다. */
            static void groupCapTriangles( const vector<Triangle>& listTriangle, CapState& outState )
            {
                for ( uint32 index = 0; index < static_cast<uint32>( listTriangle.size() ); ++index )
                {
                    const int32 tag = listTriangle[index]._tag;
                    if ( tag < 0 )
                        continue;
                    size_t group = 0;
                    while ( group < outState._listTag.size() && outState._listTag[group] != tag )
                    {
                        ++group;
                    }
                    if ( group == outState._listTag.size() )
                    {
                        outState._listTag.push_back( tag );
                        outState._listGroupTriangle.push_back( vector<uint32>{} );
                    }
                    outState._listGroupTriangle[group].push_back( index );
                }
            }

            /** @brief 점의 위치를 키로 모으고, 겉면 삼각형이 쓰는 점을 기록합니다. */
            static void recordPoints( const vector<Triangle>& listTriangle, CapState& outState )
            {
                for ( const Triangle& triangle : listTriangle )
                {
                    for ( const RHIVertex& vertex : triangle._arrVertex )
                    {
                        const float3 point = getPosition( vertex );
                        const uint64 key   = makePointKey( point );
                        outState._mapPoint.emplace( key, point );
                        uint8& bOuter = outState._mapSurfaceUse[key];
                        if ( triangle._tag < 0 )
                            bOuter = SW_TRUE;
                    }
                }
            }

            /**
             * @brief 그룹의 경계 고리(점 키)를 만듭니다. 그룹 안에서 반대 방향이 없는 모서리를 잇습니다.
             * @details 경계가 갈라지거나(한 점에서 나가는 경계가 둘 이상) 닫히지 않으면 이 그룹의 고리는 비워 둔다(건드리지 않는다).
             */
            static void buildGroupLoops( const vector<Triangle>& listTriangle, size_t group, CapState& inoutState )
            {
                unordered_map<uint64, uint32> mapEdge;
                for ( const uint32 index : inoutState._listGroupTriangle[group] )
                {
                    for ( uint32 corner = 0; corner < 3; ++corner )
                    {
                        const uint64 from = makePointKey( getPosition( listTriangle[index]._arrVertex[corner] ) );
                        const uint64 to   = makePointKey( getPosition( listTriangle[index]._arrVertex[( corner + 1 ) % 3] ) );
                        ++mapEdge[makeEdgeKey( from, to )];
                    }
                }
                unordered_map<uint64, vector<uint64>> mapNext;
                for ( const uint32 index : inoutState._listGroupTriangle[group] )
                {
                    for ( uint32 corner = 0; corner < 3; ++corner )
                    {
                        const uint64 from = makePointKey( getPosition( listTriangle[index]._arrVertex[corner] ) );
                        const uint64 to   = makePointKey( getPosition( listTriangle[index]._arrVertex[( corner + 1 ) % 3] ) );
                        if ( mapEdge.find( makeEdgeKey( to, from ) ) == mapEdge.end() )
                            mapNext[from].push_back( to );
                    }
                }
                // 분기(한 점에서 나가는 경계가 둘 이상)가 있으면 이 그룹은 건드리지 않는다.
                bool bSimple = true;
                for ( const auto& [key, listTo] : mapNext )
                {
                    bSimple = bSimple && listTo.size() == 1;
                }
                if ( bSimple == false )
                    return;
                unordered_map<uint64, uint8> mapUsed;
                for ( const auto& [start, listTo] : mapNext )
                {
                    if ( mapUsed.find( start ) != mapUsed.end() )
                        continue;
                    vector<uint64> listLoopKey;
                    uint64         current = start;
                    bool           bClosed = false;
                    for ( size_t guard = 0; guard <= mapNext.size(); ++guard )
                    {
                        mapUsed[current] = SW_TRUE;
                        listLoopKey.push_back( current );
                        const auto iter = mapNext.find( current );
                        if ( iter == mapNext.end() )
                            break;
                        current = iter->second[0];
                        if ( current == start )
                        {
                            bClosed = true;
                            break;
                        }
                    }
                    if ( bClosed == false )
                    {
                        inoutState._listGroupLoop[group].clear();
                        return;
                    }
                    inoutState._listGroupLoop[group].push_back( std::move( listLoopKey ) );
                }
            }

            /** @brief 고리를 못 만든 그룹의 점과, 고리에서 일직선이 아닌 점을 남길 점으로 표시합니다. */
            static void markKeptPoints( const vector<Triangle>& listTriangle, CapState& inoutState )
            {
                for ( size_t group = 0; group < inoutState._listTag.size(); ++group )
                {
                    if ( inoutState._listGroupLoop[group].empty() )
                    {
                        inoutState.keepGroupPoints( listTriangle, group );
                        continue;
                    }
                    for ( const vector<uint64>& loop : inoutState._listGroupLoop[group] )
                    {
                        const size_t count = loop.size();
                        for ( size_t index = 0; index < count; ++index )
                        {
                            const float3& prev   = inoutState._mapPoint[loop[( index + count - 1 ) % count]];
                            const float3& cur    = inoutState._mapPoint[loop[index]];
                            const float3& next   = inoutState._mapPoint[loop[( index + 1 ) % count]];
                            const float32 span   = ( next - prev ).getLength();
                            const float32 offset = span > 0.0f ? ( cur - prev ).cross( next - prev ).getLength() / span : 1.0f;
                            const bool    bLine  = offset <= 1.0e-5f && ( cur - prev ).dot( next - cur ) > 0.0f;
                            if ( bLine == false )
                                inoutState._mapKeep[loop[index]] = SW_TRUE;
                        }
                    }
                }
            }

            /** @brief 다시 만들 그룹을 정합니다. 고리에 없고 삼각형에만 있는 점(그룹 안쪽 점)이 있는 그룹은 다시 만들지 않고 점을 모두 남깁니다. */
            static void markRebuildGroups( const vector<Triangle>& listTriangle, CapState& inoutState )
            {
                inoutState._listRebuild.assign( inoutState._listTag.size(), SW_FALSE );
                for ( size_t group = 0; group < inoutState._listTag.size(); ++group )
                {
                    if ( inoutState._listGroupLoop[group].empty() )
                        continue;
                    unordered_map<uint64, uint8> mapOnLoop;
                    for ( const vector<uint64>& loop : inoutState._listGroupLoop[group] )
                    {
                        for ( const uint64 key : loop )
                        {
                            mapOnLoop[key] = SW_TRUE;
                        }
                    }
                    bool bInnerPoint = false;
                    for ( const uint32 index : inoutState._listGroupTriangle[group] )
                    {
                        for ( const RHIVertex& vertex : listTriangle[index]._arrVertex )
                        {
                            bInnerPoint = bInnerPoint || mapOnLoop.find( makePointKey( getPosition( vertex ) ) ) == mapOnLoop.end();
                        }
                    }
                    if ( bInnerPoint )
                    {
                        inoutState.keepGroupPoints( listTriangle, group );
                        inoutState._listGroupLoop[group].clear();
                        continue;
                    }
                    inoutState._listRebuild[group] = SW_TRUE;
                }
            }

            /** @brief 다시 만들 고리 중에 뺄 점이 하나라도 있는지 반환합니다. */
            static bool hasRemovablePoint( CapState& inoutState )
            {
                bool bAnyRemoved = false;
                for ( size_t group = 0; group < inoutState._listTag.size(); ++group )
                {
                    for ( const vector<uint64>& loop : inoutState._listGroupLoop[group] )
                    {
                        for ( const uint64 key : loop )
                        {
                            bAnyRemoved = bAnyRemoved || inoutState.isRemovable( key );
                        }
                    }
                }
                return bAnyRemoved;
            }

            /**
             * @brief 그룹 하나를 다시 만듭니다. 고리에서 뺄 점을 빼고 평면 기준 축으로 귀 자르기를 합니다.
             * @return 귀 자르기가 막히면 그 그룹의 점을 모두 남길 점으로 돌리고 false 입니다(호출한 쪽이 처음부터 다시 합니다).
             */
            static bool rebuildGroup( const vector<Triangle>& listTriangle, size_t group, const FractureSettings& settings, CapState& inoutState, vector<uint8>& inoutListDrop,
                                      vector<Triangle>& inoutListNew )
            {
                const Triangle& sample = listTriangle[inoutState._listGroupTriangle[group][0]];
                const float3    normal{ sample._arrVertex[0]._arrNormal[0], sample._arrVertex[0]._arrNormal[1], sample._arrVertex[0]._arrNormal[2] };
                float3          axisU{};
                float3          axisV{};
                makePlaneBasis( normal, axisU, axisV );
                vector<float3>         listPoint3;
                vector<float2>         listPoint2;
                vector<vector<uint32>> listLoopIndex;
                for ( const vector<uint64>& loop : inoutState._listGroupLoop[group] )
                {
                    vector<uint32> listLoopCorner;
                    for ( const uint64 key : loop )
                    {
                        if ( inoutState.isRemovable( key ) )
                            continue;
                        const float3& point = inoutState._mapPoint[key];
                        listLoopCorner.push_back( static_cast<uint32>( listPoint3.size() ) );
                        listPoint3.push_back( point );
                        listPoint2.push_back( float2{ point.dot( axisU ), point.dot( axisV ) } );
                    }
                    listLoopIndex.push_back( std::move( listLoopCorner ) );
                }
                vector<uint32> listIndex;
                if ( PolygonTriangulationUtil::triangulate( listPoint2, listLoopIndex, listIndex ) == false )
                {
                    for ( const vector<uint64>& loop : inoutState._listGroupLoop[group] )
                    {
                        for ( const uint64 key : loop )
                        {
                            inoutState._mapKeep[key] = SW_TRUE;
                        }
                    }
                    inoutState._listRebuild[group] = SW_FALSE;
                    return false;
                }
                for ( const uint32 index : inoutState._listGroupTriangle[group] )
                {
                    inoutListDrop[index] = 1;
                }
                for ( size_t index = 0; index + 2 < listIndex.size(); index += 3 )
                {
                    Triangle cap;
                    cap._tag = inoutState._listTag[group];
                    for ( uint32 corner = 0; corner < 3; ++corner )
                    {
                        const uint32 pointIndex = listIndex[index + corner];
                        RHIVertex&   vertex     = cap._arrVertex[corner];
                        vertex                  = sample._arrVertex[0];
                        setPosition( vertex, listPoint3[pointIndex] );
                        vertex._arrUv[0] = listPoint2[pointIndex]._x * settings._interiorUvScale;
                        vertex._arrUv[1] = listPoint2[pointIndex]._y * settings._interiorUvScale;
                    }
                    if ( hasDistinctCorners( cap ) )
                        inoutListNew.push_back( cap );
                }
                return true;
            }

            /**
             * @brief 안쪽 면(평면마다 한 그룹)의 삼각형을 다시 만듭니다. 그룹 경계 고리에서 일직선 위의 점을 빼고 귀 자르기로 다시 나눕니다.
             * @details 자를 때마다 앞서 막은 면의 대각선을 가로질러 교점이 생기고, 그 점들이 다음 막음의 고리에 일직선으로 쌓인다.
             *          그러면 조각 하나의 안쪽 삼각형이 수백 개가 된다(벽 200 조각에서 4.7 만 개).
             *          점은 그 점을 쓰는 **모든** 면이 안쪽 면이고 그 모든 그룹의 고리에서 일직선일 때만 함께 뺀다.
             *          한쪽만 빼면 이웃과 T 자 이음이 생긴다. 겉면이 쓰는 점은 그대로 둔다.
             */
            static void simplifyCaps( vector<Triangle>& inoutListTriangle, const FractureSettings& settings )
            {
                CapState state;
                groupCapTriangles( inoutListTriangle, state );
                if ( state._listTag.empty() )
                    return;
                recordPoints( inoutListTriangle, state );
                state._listGroupLoop.resize( state._listTag.size() );
                for ( size_t group = 0; group < state._listTag.size(); ++group )
                {
                    buildGroupLoops( inoutListTriangle, group, state );
                }
                // 뺄 점은 겉면이 쓰지 않고, 그 점이 있는 모든 고리에서 일직선이고, 모든 그룹이 고리를 만든 점이다.
                markKeptPoints( inoutListTriangle, state );
                markRebuildGroups( inoutListTriangle, state );
                if ( hasRemovablePoint( state ) == false )
                    return;

                // 다시 만들기: 한 그룹이라도 막히면 그 그룹의 점을 모두 남기고 처음부터 다시 한다
                // (그 그룹만 원래 삼각형을 두면 이웃이 뺀 점과 T 자 이음이 생긴다).
                vector<uint8>    listDrop( inoutListTriangle.size(), 0 );
                vector<Triangle> listNew;
                for ( uint32 attempt = 0; attempt < 4; ++attempt )
                {
                    listDrop.assign( inoutListTriangle.size(), 0 );
                    listNew.clear();
                    bool bFailed = false;
                    for ( size_t group = 0; group < state._listTag.size() && bFailed == false; ++group )
                    {
                        if ( state._listRebuild[group] == SW_FALSE )
                            continue;
                        bFailed = rebuildGroup( inoutListTriangle, group, settings, state, listDrop, listNew ) == false;
                    }
                    if ( bFailed == false )
                        break;
                    if ( attempt == 3 )
                        return; // 계속 막히면 손대지 않는다.
                }
                vector<Triangle> listResult;
                listResult.reserve( inoutListTriangle.size() );
                for ( uint32 index = 0; index < static_cast<uint32>( inoutListTriangle.size() ); ++index )
                {
                    if ( listDrop[index] == 0 )
                        listResult.push_back( inoutListTriangle[index] );
                }
                listResult.insert( listResult.end(), listNew.begin(), listNew.end() );
                inoutListTriangle = std::move( listResult );
            }

            static vector_reference<const RHIVertex> asVertices( const vector<Triangle>& listTriangle, vector<RHIVertex>& outListScratch )
            {
                outListScratch.clear();
                outListScratch.reserve( listTriangle.size() * 3 );
                for ( const Triangle& triangle : listTriangle )
                {
                    for ( const RHIVertex& vertex : triangle._arrVertex )
                    {
                        outListScratch.push_back( vertex );
                    }
                }
                return outListScratch;
            }

            struct Face
            {
                uint32 _arrIndex[3];
                uint8  _bAlive;
            };

            /**
             * @brief 처음 사면체의 네 점을 가장 먼 점들로 고릅니다.
             * @return 네 점이 한 평면 위에 있어 부피가 없으면 false 입니다.
             */
            [[nodiscard]] static bool pickSeedTetrahedron( const vector<float3>& listPoint, float32 epsilon, uint32 ( &outStart )[4] )
            {
                outStart[0] = outStart[1] = outStart[2] = outStart[3] = 0;
                for ( uint32 index = 1; index < static_cast<uint32>( listPoint.size() ); ++index )
                {
                    if ( listPoint[index]._x < listPoint[outStart[0]]._x )
                        outStart[0] = index;
                }
                float32 best = -1.0f;
                for ( uint32 index = 0; index < static_cast<uint32>( listPoint.size() ); ++index )
                {
                    const float32 distance = float3::getDistanceSquared( listPoint[index], listPoint[outStart[0]] );
                    if ( distance > best )
                    {
                        best        = distance;
                        outStart[1] = index;
                    }
                }
                best = -1.0f;
                for ( uint32 index = 0; index < static_cast<uint32>( listPoint.size() ); ++index )
                {
                    const float32 area = ( listPoint[outStart[1]] - listPoint[outStart[0]] ).cross( listPoint[index] - listPoint[outStart[0]] ).getLengthSquared();
                    if ( area > best )
                    {
                        best        = area;
                        outStart[2] = index;
                    }
                }
                const float3 baseNormal = ( listPoint[outStart[1]] - listPoint[outStart[0]] ).cross( listPoint[outStart[2]] - listPoint[outStart[0]] );
                best                    = -1.0f;
                for ( uint32 index = 0; index < static_cast<uint32>( listPoint.size() ); ++index )
                {
                    const float32 height = MathUtil::abs( baseNormal.dot( listPoint[index] - listPoint[outStart[0]] ) );
                    if ( height > best )
                    {
                        best        = height;
                        outStart[3] = index;
                    }
                }
                return best > epsilon * baseNormal.getLength();
            }

            /** @brief 면을 바깥(@p inside 의 반대쪽)을 향하게 감아 더합니다. */
            static void addFaceOutward( vector<Face>& inoutListFace, const vector<float3>& listPoint, const float3& inside, uint32 a, uint32 b, uint32 c )
            {
                const float3 normal = ( listPoint[b] - listPoint[a] ).cross( listPoint[c] - listPoint[a] );
                if ( normal.dot( listPoint[a] - inside ) < 0.0f )
                    inoutListFace.push_back( Face{
                        { a, c, b },
                        SW_TRUE
                    } );
                else
                    inoutListFace.push_back( Face{
                        { a, b, c },
                        SW_TRUE
                    } );
            }

            /** @brief 점마다 보이는 면을 지우고, 지평선 모서리마다 그 점과 새 면을 잇습니다. */
            static void growHull( const vector<float3>& listPoint, float32 epsilon, vector<Face>& inoutListFace )
            {
                unordered_map<uint64, uint32> mapEdgeFace;
                for ( uint32 point = 0; point < static_cast<uint32>( listPoint.size() ); ++point )
                {
                    vector<uint32> listVisible;
                    for ( uint32 face = 0; face < static_cast<uint32>( inoutListFace.size() ); ++face )
                    {
                        if ( inoutListFace[face]._bAlive == SW_FALSE )
                            continue;
                        const float3& a      = listPoint[inoutListFace[face]._arrIndex[0]];
                        const float3  normal = ( listPoint[inoutListFace[face]._arrIndex[1]] - a ).cross( listPoint[inoutListFace[face]._arrIndex[2]] - a );
                        if ( normal.dot( listPoint[point] - a ) > epsilon * normal.getLength() )
                            listVisible.push_back( face );
                    }
                    if ( listVisible.empty() )
                        continue;
                    mapEdgeFace.clear();
                    for ( const uint32 face : listVisible )
                    {
                        for ( uint32 corner = 0; corner < 3; ++corner )
                        {
                            const uint64 from                = inoutListFace[face]._arrIndex[corner];
                            const uint64 to                  = inoutListFace[face]._arrIndex[( corner + 1 ) % 3];
                            mapEdgeFace[( from << 32 ) | to] = face;
                        }
                    }
                    vector<std::pair<uint32, uint32>> listHorizon;
                    for ( const uint32 face : listVisible )
                    {
                        for ( uint32 corner = 0; corner < 3; ++corner )
                        {
                            const uint32 from = inoutListFace[face]._arrIndex[corner];
                            const uint32 to   = inoutListFace[face]._arrIndex[( corner + 1 ) % 3];
                            if ( mapEdgeFace.find( ( static_cast<uint64>( to ) << 32 ) | from ) == mapEdgeFace.end() )
                                listHorizon.emplace_back( from, to );
                        }
                    }
                    for ( const uint32 face : listVisible )
                    {
                        inoutListFace[face]._bAlive = SW_FALSE;
                    }
                    for ( const std::pair<uint32, uint32>& edge : listHorizon )
                    {
                        inoutListFace.push_back( Face{
                            { edge.first, edge.second, point },
                            SW_TRUE
                        } );
                    }
                }
            }

            /** @brief `MeshFractureUtil::fracture` 의 단계 함수들입니다. 단계마다 앞 단계의 결과를 값으로 받습니다. */

            /** @brief 시드마다 자른 조각과, 대리 부피일 때 원래 겉면의 조각입니다. */
            struct SiteCut
            {
                vector<vector<Triangle>> _listSitePiece;
                vector<vector<Triangle>> _listSiteSurface;
                uint32                   _openLoopCount{ 0 };
            };

            /** @brief 부피가 있는 조각의 번호표와 그래프입니다. */
            struct PieceSet
            {
                vector<uint32> _listPieceOfSite;
                vector<uint32> _listSiteOfPiece;
                FractureGraph  _graph;
            };

            static void makeTriangles( const vector<RHIVertex>& listInput, int32 tag, vector<Triangle>& outListTriangle )
            {
                outListTriangle.reserve( listInput.size() / 3 );
                for ( size_t index = 0; index + 2 < listInput.size(); index += 3 )
                {
                    Triangle triangle;
                    triangle._arrVertex[0] = listInput[index];
                    triangle._arrVertex[1] = listInput[index + 1];
                    triangle._arrVertex[2] = listInput[index + 2];
                    triangle._tag          = tag;
                    outListTriangle.push_back( triangle );
                }
            }

            /** @brief 조각의 정점이 시드에서 가장 멀리 떨어진 거리의 제곱입니다. */
            static float32 computeRadiusSquared( const vector<Triangle>& listTriangle, const float3& site )
            {
                float32 radiusSquared = 0.0f;
                for ( const Triangle& triangle : listTriangle )
                {
                    for ( const RHIVertex& vertex : triangle._arrVertex )
                    {
                        radiusSquared = MathUtil::max( radiusSquared, float3::getDistanceSquared( MeshFractureInternal::getPosition( vertex ), site ) );
                    }
                }
                return radiusSquared;
            }

            /**
             * @brief 쪼갤 부피를 고릅니다. 메시 자체이거나 닫힌 대리(경계 상자, 볼록 껍질)입니다.
             * @details 대리면 원래 겉면은 따로 막지 않고 자른다.
             */
            static void pickVolume( const vector<RHIVertex>& listWelded, const FractureSettings& settings, bool bProxy, vector<RHIVertex>& outListVolume )
            {
                if ( settings._volume == FractureVolume::Hull )
                    MeshFractureUtil::makeConvexHull( listWelded, outListVolume );
                if ( settings._volume == FractureVolume::Bounds || ( settings._volume == FractureVolume::Hull && outListVolume.empty() ) )
                    MeshFractureInternal::makeBoundsVolume( listWelded, outListVolume );
                if ( bProxy == false )
                    outListVolume = listWelded;
            }

            /** @brief 시드 하나의 셀을 가까운 이웃부터 이등분 평면으로 자릅니다. 자른 조각과 겉면 조각을 @p outCut 에 넣습니다. */
            static void cutSite( uint32 site, const vector<float3>& listSite, const vector<Triangle>& listSource, const vector<Triangle>& listSurfaceSource, const FractureSettings& settings,
                                 vector<Triangle>& inoutListScratch, vector<std::pair<float32, uint32>>& inoutListOrder, SiteCut& inoutCut )
            {
                const uint32     siteCount           = static_cast<uint32>( listSite.size() );
                vector<Triangle> listPieceTriangle   = listSource;
                vector<Triangle> listSurfaceTriangle = listSurfaceSource;
                if ( siteCount > 1 )
                {
                    inoutListOrder.clear();
                    for ( uint32 other = 0; other < siteCount; ++other )
                    {
                        if ( other != site )
                            inoutListOrder.emplace_back( float3::getDistanceSquared( listSite[site], listSite[other] ), other );
                    }
                    std::sort( inoutListOrder.begin(), inoutListOrder.end() );
                    float32 radiusSquared = computeRadiusSquared( listPieceTriangle, listSite[site] );
                    for ( const std::pair<float32, uint32>& entry : inoutListOrder )
                    {
                        // 이등분 평면까지의 거리는 시드 거리 / 2 다. 조각 반경보다 멀면 이 평면도, 뒤의 평면도 닿지 않는다.
                        if ( entry.first * 0.25f > radiusSquared )
                            break;
                        const float3  delta       = listSite[entry.second] - listSite[site];
                        const float3  normal      = delta / MathUtil::sqrt( entry.first );
                        const float32 offset      = normal.dot( ( listSite[entry.second] + listSite[site] ) * 0.5f );
                        float32       maxDistance = -MathUtil::kMaxFloat;
                        for ( const Triangle& triangle : listPieceTriangle )
                        {
                            for ( const RHIVertex& vertex : triangle._arrVertex )
                            {
                                maxDistance = MathUtil::max( maxDistance, normal.dot( MeshFractureInternal::getPosition( vertex ) ) - offset );
                            }
                        }
                        if ( maxDistance <= 0.0f )
                            continue;
                        bool bNearDuplicate = false;
                        inoutCut._openLoopCount += MeshFractureInternal::clipPiece( listPieceTriangle, normal, offset, static_cast<int32>( entry.second ), settings, inoutListScratch, bNearDuplicate );
                        if ( bNearDuplicate )
                            MeshFractureInternal::cleanPiece( inoutListScratch );
                        listPieceTriangle.swap( inoutListScratch );
                        if ( listSurfaceTriangle.empty() == false )
                        {
                            (void)MeshFractureInternal::clipPiece( listSurfaceTriangle, normal, offset, MeshFractureInternal::kSurfaceTag, settings, inoutListScratch, bNearDuplicate,
                                                                   false ); // 겉면은 막지 않고 자르므로 열린 고리 수는 셀 필요가 없다
                            listSurfaceTriangle.swap( inoutListScratch );
                        }
                        if ( listPieceTriangle.empty() )
                            break;
                        radiusSquared = computeRadiusSquared( listPieceTriangle, listSite[site] );
                    }
                }
                MeshFractureInternal::simplifyCaps( listPieceTriangle, settings );
                inoutCut._listSitePiece[site]   = std::move( listPieceTriangle );
                inoutCut._listSiteSurface[site] = std::move( listSurfaceTriangle );
            }

            /** @brief 부피가 있는 조각만 번호를 매기고 그래프 노드(무게중심 · 부피)를 만듭니다. 빈 셀과 부피 0 은 뺍니다. */
            static void numberPieces( const SiteCut& cut, PieceSet& outPieces )
            {
                const uint32      siteCount = static_cast<uint32>( cut._listSitePiece.size() );
                vector<RHIVertex> listScratchVertex;
                outPieces._listPieceOfSite.assign( siteCount, 0xFFFFFFFFu );
                for ( uint32 site = 0; site < siteCount; ++site )
                {
                    if ( cut._listSitePiece[site].empty() )
                        continue;
                    float3        centroid{};
                    const float32 volume = MeshFractureUtil::computeVolume( MeshFractureInternal::asVertices( cut._listSitePiece[site], listScratchVertex ), centroid );
                    if ( volume <= 0.0f )
                        continue;
                    outPieces._listPieceOfSite[site] = static_cast<uint32>( outPieces._listSiteOfPiece.size() );
                    outPieces._listSiteOfPiece.push_back( site );
                    FractureNode node;
                    node._centroid = centroid;
                    node._volume   = volume;
                    outPieces._graph._listNode.push_back( node );
                }
                outPieces._graph._leafCount = static_cast<uint32>( outPieces._listSiteOfPiece.size() );
            }

            /** @brief 연결: 조각 i 에 j 쪽 평면이 만든 안쪽 면 넓이다. 두 쪽 중 작은 값을 쓴다(둘 다 닿아야 연결이다). */
            static void linkPieces( const SiteCut& cut, PieceSet& inoutPieces )
            {
                const uint32                   pieceCount = static_cast<uint32>( inoutPieces._listSiteOfPiece.size() );
                unordered_map<uint64, float32> mapArea;
                for ( uint32 piece = 0; piece < pieceCount; ++piece )
                {
                    for ( const Triangle& triangle : cut._listSitePiece[inoutPieces._listSiteOfPiece[piece]] )
                    {
                        if ( triangle._tag < 0 || inoutPieces._listPieceOfSite[static_cast<uint32>( triangle._tag )] == 0xFFFFFFFFu )
                            continue;
                        const uint32 other = inoutPieces._listPieceOfSite[static_cast<uint32>( triangle._tag )];
                        const uint64 key   = ( static_cast<uint64>( piece ) << 32 ) | other;
                        mapArea[key] += MeshFractureInternal::computeTriangleArea( MeshFractureInternal::getPosition( triangle._arrVertex[0] ),
                                                                                   MeshFractureInternal::getPosition( triangle._arrVertex[1] ),
                                                                                   MeshFractureInternal::getPosition( triangle._arrVertex[2] ) );
                    }
                }
                for ( uint32 pieceA = 0; pieceA < pieceCount; ++pieceA )
                {
                    for ( uint32 pieceB = pieceA + 1; pieceB < pieceCount; ++pieceB )
                    {
                        const auto iterAb = mapArea.find( ( static_cast<uint64>( pieceA ) << 32 ) | pieceB );
                        const auto iterBa = mapArea.find( ( static_cast<uint64>( pieceB ) << 32 ) | pieceA );
                        if ( iterAb == mapArea.end() || iterBa == mapArea.end() )
                            continue;
                        inoutPieces._graph._listLink.push_back( FractureLink{ pieceA, pieceB, MathUtil::min( iterAb->second, iterBa->second ) } );
                    }
                }
                FractureGraphUtil::normalizeLinks( inoutPieces._graph._listLink );
            }

            /** @brief 조각 하나의 렌더링 삼각형을 에셋에 더합니다. 메시 부피면 조각 그대로, 대리 부피면 원래 겉면 조각과 대리의 안쪽 면을 씁니다(대리의 바깥 면은 그리지 않는다). */
            static void appendPieceMesh( const vector<Triangle>& listTriangle, const vector<Triangle>& listSurface, bool bProxy, FractureAsset& inoutAsset )
            {
                const auto appendTriangle = [&inoutAsset]( const Triangle& triangle, FractureSurfaceSlot slot )
                {
                    for ( const RHIVertex& vertex : triangle._arrVertex )
                    {
                        inoutAsset._listVertex.push_back( vertex );
                    }
                    inoutAsset._listTriangleSlot.push_back( static_cast<uint8>( slot ) );
                };
                for ( const Triangle& triangle : listSurface )
                {
                    appendTriangle( triangle, FractureSurfaceSlot::Outer );
                }
                for ( const Triangle& triangle : listTriangle )
                {
                    if ( triangle._tag >= 0 )
                        appendTriangle( triangle, FractureSurfaceSlot::Interior );
                    else if ( bProxy == false )
                        appendTriangle( triangle, FractureSurfaceSlot::Outer );
                }
            }

            /** @brief 잎 조각마다 정점 · 경계 · 껍질 점을 에셋에 채웁니다. 잎 순서는 계층 구성(`populateHierarchy`)이 정합니다. */
            static void fillPieces( const SiteCut& cut, const PieceSet& pieces, const vector<uint32>& listLeafOrder, const FractureSettings& settings, bool bProxy, FractureAsset& inoutAsset )
            {
                const uint32 pieceCount = static_cast<uint32>( pieces._listSiteOfPiece.size() );
                for ( uint32 leaf = 0; leaf < pieceCount; ++leaf )
                {
                    const uint32            site         = pieces._listSiteOfPiece[listLeafOrder[leaf]];
                    const vector<Triangle>& listTriangle = cut._listSitePiece[site];
                    FracturePiece           piece;
                    piece._firstVertex = static_cast<uint32>( inoutAsset._listVertex.size() );
                    appendPieceMesh( listTriangle, cut._listSiteSurface[site], bProxy, inoutAsset );
                    piece._vertexCount = static_cast<uint32>( inoutAsset._listVertex.size() ) - piece._firstVertex;
                    MeshFractureInternal::collectBounds( vector_reference<const RHIVertex>{ inoutAsset._listVertex.data() + piece._firstVertex, piece._vertexCount }, piece._boundsMin,
                                                         piece._boundsMax );
                    vector<float3> listHull;
                    MeshFractureInternal::makeHull( listTriangle, inoutAsset._graph._listNode[leaf]._centroid, MathUtil::max( settings._maxHullPoint, 8u ), listHull );
                    piece._firstHullPoint = static_cast<uint32>( inoutAsset._listHullPoint.size() );
                    piece._hullPointCount = static_cast<uint32>( listHull.size() );
                    inoutAsset._listHullPoint.insert( inoutAsset._listHullPoint.end(), listHull.begin(), listHull.end() );
                    inoutAsset._listPiece.push_back( piece );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    FractureSettings::FractureSettings()
        : _listLevelCount{}
        , _interiorColor{ 0.62f, 0.58f, 0.52f, 1.0f }
        , _impactPoint{}
        , _clusterRadius{ 0.5f }
        , _clusterFraction{ 0.7f }
        , _interiorUvScale{ 1.0f }
        , _sliceJitter{ 0.15f }
        , _seed{ 1 }
        , _pieceCount{ 16 }
        , _arrSliceCount{ 4, 1, 1 }
        , _maxHullPoint{ 48 }
        , _pattern{ FracturePattern::Uniform }
        , _volume{ FractureVolume::Mesh }
    {
    }

    bool FractureSettings::parsePattern( string_view text, FracturePattern& outPattern )
    {
        if ( text == "uniform" )
            outPattern = FracturePattern::Uniform;
        else if ( text == "clustered" )
            outPattern = FracturePattern::Clustered;
        else if ( text == "slices" )
            outPattern = FracturePattern::Slices;
        else
            return false;
        return true;
    }

    string_view FractureSettings::getPatternName( FracturePattern pattern )
    {
        switch ( pattern )
        {
            case FracturePattern::Uniform:
                return "uniform";
            case FracturePattern::Clustered:
                return "clustered";
            case FracturePattern::Slices:
                return "slices";
        }
        return "uniform";
    }

    bool FractureSettings::parseVolume( string_view text, FractureVolume& outVolume )
    {
        if ( text == "mesh" )
            outVolume = FractureVolume::Mesh;
        else if ( text == "bounds" )
            outVolume = FractureVolume::Bounds;
        else if ( text == "hull" )
            outVolume = FractureVolume::Hull;
        else
            return false;
        return true;
    }

    string_view FractureSettings::getVolumeName( FractureVolume volume )
    {
        switch ( volume )
        {
            case FractureVolume::Mesh:
                return "mesh";
            case FractureVolume::Bounds:
                return "bounds";
            case FractureVolume::Hull:
                return "hull";
        }
        return "mesh";
    }

    void MeshFractureUtil::makeConvexHull( vector_reference<const RHIVertex> listVertex, vector<RHIVertex>& outListVertex )
    {
        outListVertex.clear();
        // 고유한 점만 모은다.
        vector<float3>                listPoint;
        unordered_map<uint64, uint32> mapSeen;
        for ( const RHIVertex& vertex : listVertex )
        {
            const float3 point = MeshFractureInternal::getPosition( vertex );
            if ( mapSeen.emplace( MeshFractureInternal::makePointKey( point ), 0u ).second )
                listPoint.push_back( point );
        }
        if ( listPoint.size() < 4 )
            return;
        float3 lo{};
        float3 hi{};
        MeshFractureInternal::collectBounds( listVertex, lo, hi );
        const float32 epsilon = MathUtil::max( ( hi - lo ).getLength(), 1.0e-3f ) * 1.0e-6f;
        uint32        arrStart[4];
        if ( MeshFractureInternal::pickSeedTetrahedron( listPoint, epsilon, arrStart ) == false )
            return;

        vector<MeshFractureInternal::Face> listFace;
        const float3                       inside = ( listPoint[arrStart[0]] + listPoint[arrStart[1]] + listPoint[arrStart[2]] + listPoint[arrStart[3]] ) * 0.25f;
        MeshFractureInternal::addFaceOutward( listFace, listPoint, inside, arrStart[0], arrStart[1], arrStart[2] );
        MeshFractureInternal::addFaceOutward( listFace, listPoint, inside, arrStart[0], arrStart[1], arrStart[3] );
        MeshFractureInternal::addFaceOutward( listFace, listPoint, inside, arrStart[0], arrStart[2], arrStart[3] );
        MeshFractureInternal::addFaceOutward( listFace, listPoint, inside, arrStart[1], arrStart[2], arrStart[3] );
        MeshFractureInternal::growHull( listPoint, epsilon, listFace );
        for ( const MeshFractureInternal::Face& face : listFace )
        {
            if ( face._bAlive == SW_TRUE )
                MeshFractureInternal::addFace( outListVertex, listPoint[face._arrIndex[0]], listPoint[face._arrIndex[1]], listPoint[face._arrIndex[2]] );
        }
    }

    bool MeshFractureUtil::isClosedMesh( vector_reference<const RHIVertex> listVertex )
    {
        // 위치가 비트까지 같은 정점만 같은 정점이다. 세 평면이 만나는 꼭짓점 둘레의 아주 짧은 변(1e-5 미만)도 변으로 센다.
        unordered_map<uint64, uint32> mapEdgeCount;
        vector<uint64>                listKey( listVertex.size() );
        for ( size_t index = 0; index < listVertex.size(); ++index )
        {
            listKey[index] = MeshFractureInternal::makePointKey( MeshFractureInternal::getPosition( listVertex[index] ) );
        }
        if ( listVertex.size() < 3 )
            return false;
        for ( size_t index = 0; index + 2 < listVertex.size(); index += 3 )
        {
            for ( uint32 corner = 0; corner < 3; ++corner )
            {
                const uint64 from = listKey[index + corner];
                const uint64 to   = listKey[index + ( corner + 1 ) % 3];
                if ( from == to )
                    return false;
                ++mapEdgeCount[MeshFractureInternal::makeEdgeKey( from, to )];
            }
        }
        for ( size_t index = 0; index + 2 < listVertex.size(); index += 3 )
        {
            for ( uint32 corner = 0; corner < 3; ++corner )
            {
                const uint64 from          = listKey[index + corner];
                const uint64 to            = listKey[index + ( corner + 1 ) % 3];
                const auto   forward       = mapEdgeCount.find( MeshFractureInternal::makeEdgeKey( from, to ) );
                const auto   backward      = mapEdgeCount.find( MeshFractureInternal::makeEdgeKey( to, from ) );
                const bool   bOpenMeshEdge = forward->second != 1 || backward == mapEdgeCount.end() || backward->second != 1;
                if ( bOpenMeshEdge )
                    return false;
            }
        }
        return true;
    }

    bool MeshFractureUtil::isClosedWithinTolerance( vector_reference<const RHIVertex> listVertex )
    {
        AppearanceGeometry geometry;
        geometry._listPosition.reserve( listVertex.size() );
        geometry._listNormal.reserve( listVertex.size() );
        geometry._listUv.reserve( listVertex.size() );
        geometry._listIndex.reserve( listVertex.size() );
        for ( const RHIVertex& vertex : listVertex )
        {
            geometry._listIndex.push_back( static_cast<uint32>( geometry._listPosition.size() ) );
            geometry._listPosition.push_back( MeshFractureInternal::getPosition( vertex ) );
            geometry._listNormal.push_back( float3{ vertex._arrNormal[0], vertex._arrNormal[1], vertex._arrNormal[2] } );
            geometry._listUv.push_back( float2{ vertex._arrUv[0], vertex._arrUv[1] } );
        }
        return geometry.getTriangleCount() > 0 && GeometryCutUtil::isClosed( geometry );
    }

    float32 MeshFractureUtil::computeVolume( vector_reference<const RHIVertex> listVertex, float3& outCentroid )
    {
        float64 volume6      = 0.0;
        float64 arrMoment[3] = { 0.0, 0.0, 0.0 };
        for ( size_t index = 0; index + 2 < listVertex.size(); index += 3 )
        {
            const float3  a   = MeshFractureInternal::getPosition( listVertex[index] );
            const float3  b   = MeshFractureInternal::getPosition( listVertex[index + 1] );
            const float3  c   = MeshFractureInternal::getPosition( listVertex[index + 2] );
            const float64 det = static_cast<float64>( a.dot( b.cross( c ) ) );
            volume6 += det;
            arrMoment[0] += det * static_cast<float64>( a._x + b._x + c._x );
            arrMoment[1] += det * static_cast<float64>( a._y + b._y + c._y );
            arrMoment[2] += det * static_cast<float64>( a._z + b._z + c._z );
        }
        if ( MathUtil::abs( volume6 ) < 1.0e-18 )
        {
            outCentroid = float3{};
            for ( const RHIVertex& vertex : listVertex )
            {
                outCentroid += MeshFractureInternal::getPosition( vertex );
            }
            if ( listVertex.empty() == false )
                outCentroid /= static_cast<float32>( listVertex.size() );
            return 0.0f;
        }
        outCentroid = float3{ static_cast<float32>( arrMoment[0] / ( 4.0 * volume6 ) ), static_cast<float32>( arrMoment[1] / ( 4.0 * volume6 ) ),
                              static_cast<float32>( arrMoment[2] / ( 4.0 * volume6 ) ) };
        return static_cast<float32>( MathUtil::abs( volume6 ) / 6.0 );
    }

    bool MeshFractureUtil::isPointInside( vector_reference<const RHIVertex> listVertex, const float3& point )
    {
        float64 solidAngle = 0.0;
        for ( size_t index = 0; index + 2 < listVertex.size(); index += 3 )
        {
            const double3 a       = double3{ MeshFractureInternal::getPosition( listVertex[index] ) - point };
            const double3 b       = double3{ MeshFractureInternal::getPosition( listVertex[index + 1] ) - point };
            const double3 c       = double3{ MeshFractureInternal::getPosition( listVertex[index + 2] ) - point };
            const float64 lengthA = MathUtil::sqrt( a._x * a._x + a._y * a._y + a._z * a._z );
            const float64 lengthB = MathUtil::sqrt( b._x * b._x + b._y * b._y + b._z * b._z );
            const float64 lengthC = MathUtil::sqrt( c._x * c._x + c._y * c._y + c._z * c._z );
            const float64 crossX  = b._y * c._z - b._z * c._y;
            const float64 crossY  = b._z * c._x - b._x * c._z;
            const float64 crossZ  = b._x * c._y - b._y * c._x;
            const float64 numer   = a._x * crossX + a._y * crossY + a._z * crossZ;
            const float64 dotAb   = a._x * b._x + a._y * b._y + a._z * b._z;
            const float64 dotBc   = b._x * c._x + b._y * c._y + b._z * c._z;
            const float64 dotCa   = c._x * a._x + c._y * a._y + c._z * a._z;
            const float64 denom   = lengthA * lengthB * lengthC + dotAb * lengthC + dotBc * lengthA + dotCa * lengthB;
            solidAngle += 2.0 * ::atan2( numer, denom );
        }
        const float64 winding = solidAngle / ( 4.0 * MathUtil::kPi64 );
        return MathUtil::abs( winding ) > 0.5;
    }

    bool MeshFractureUtil::fracture( vector_reference<const RHIVertex> listVertex, const FractureSettings& settings, FractureAsset& outAsset, string& outError )
    {
        using Triangle = MeshFractureInternal::Triangle;
        using Build    = MeshFractureInternal;
        outAsset.clear();
        outError.clear();
        if ( listVertex.size() < 3 || listVertex.size() % 3 != 0 )
        {
            outError = "the mesh has no triangles";
            return false;
        }
        const bool bProxy = settings._volume != FractureVolume::Mesh;
        if ( bProxy == false && isClosedWithinTolerance( listVertex ) == false )
        {
            outError = "the mesh is not closed (every edge must be shared by exactly two triangles) - use \"volume\": \"bounds\" or \"hull\"";
            return false;
        }

        // 용접: 허용 오차 안의 위치를 한 점으로 맞춘다. 자르기는 위치가 비트까지 같아야 이웃 삼각형의 교점이 같다.
        vector<RHIVertex> listWelded( listVertex.begin(), listVertex.end() );
        MeshFractureInternal::weldPositions( listWelded );
        vector<RHIVertex> listVolume;
        Build::pickVolume( listWelded, settings, bProxy, listVolume );
        vector<Triangle> listSource;
        vector<Triangle> listSurfaceSource;
        Build::makeTriangles( listVolume, MeshFractureInternal::kOuterTag, listSource );
        if ( bProxy )
            Build::makeTriangles( listWelded, MeshFractureInternal::kSurfaceTag, listSurfaceSource );

        vector<float3> listSite;
        MeshFractureInternal::placeSites( listVolume, settings, listSite );
        if ( listSite.empty() )
            listSite.push_back( float3{} );
        const uint32 siteCount = static_cast<uint32>( listSite.size() );

        Build::SiteCut cut;
        cut._listSitePiece.resize( siteCount );
        cut._listSiteSurface.resize( siteCount );
        vector<Triangle>                   listScratch;
        vector<std::pair<float32, uint32>> listOrder;
        for ( uint32 site = 0; site < siteCount; ++site )
        {
            Build::cutSite( site, listSite, listSource, listSurfaceSource, settings, listScratch, listOrder, cut );
        }
        if ( cut._openLoopCount > 0 )
            SW_LOG_WARNING( "Fracture left %# cut loops open (non-manifold input) - those pieces may have holes", cut._openLoopCount );

        Build::PieceSet pieces;
        Build::numberPieces( cut, pieces );
        if ( pieces._listSiteOfPiece.empty() )
        {
            outError = "no piece has volume";
            return false;
        }
        Build::linkPieces( cut, pieces );

        vector<uint32> listLeafOrder;
        FractureGraphUtil::populateHierarchy( pieces._graph, settings._listLevelCount, listLeafOrder );

        outAsset._graph = std::move( pieces._graph );
        outAsset._seed  = settings._seed;
        MeshFractureInternal::collectBounds( listWelded, outAsset._boundsMin, outAsset._boundsMax );
        Build::fillPieces( cut, pieces, listLeafOrder, settings, bProxy, outAsset );
        string validation;
        if ( outAsset.isValid( &validation ) == false )
        {
            outError = string( "fracture produced an invalid asset: " ) + validation;
            outAsset.clear();
            return false;
        }
        return true;
    }
} // namespace sw
