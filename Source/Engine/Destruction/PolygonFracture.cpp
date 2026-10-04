#include "pch.h"

#include "Engine/Destruction/PolygonFracture.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Destruction/DestructionRandom.h"
#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/FractureGraph.h"
#include "Engine/Destruction/MeshFracture.h"
#include "Engine/Destruction/PolygonTriangulation.h"

namespace sw
{
    namespace
    {
        struct PolygonFractureInternal
        {
            static constexpr int32   kOuterTag          = -1;
            static constexpr float32 kLineEpsilon       = 1.0e-6f;
            static constexpr uint32  kSiteAttemptFactor = 200;

            /** @brief 다각형 꼭짓점 하나와, 그 꼭짓점에서 다음 꼭짓점으로 가는 변을 낸 이웃 씨앗(바깥 변은 -1)입니다. */
            struct Corner
            {
                float2 _point;
                int32  _tag;
            };

            static bool isInside( vector_reference<const float2> listBorder, const float2& point )
            {
                bool         bInside = false;
                const size_t count   = listBorder.size();
                for ( size_t index = 0, prev = count - 1; index < count; prev = index++ )
                {
                    const float2& from = listBorder[index];
                    const float2& to   = listBorder[prev];
                    if ( ( from._y > point._y ) != ( to._y > point._y ) && point._x < ( to._x - from._x ) * ( point._y - from._y ) / ( to._y - from._y ) + from._x )
                        bInside = bInside == false;
                }
                return bInside;
            }

            /** @brief 반평면 n·x ≤ c 쪽만 남깁니다. 자른 변은 @p tag 를 받습니다. */
            static void clip( const vector<Corner>& listInput, const float2& normal, float32 offset, int32 tag, vector<Corner>& outListOutput )
            {
                outListOutput.clear();
                const size_t count = listInput.size();
                for ( size_t index = 0; index < count; ++index )
                {
                    const Corner& cur      = listInput[index];
                    const Corner& next     = listInput[( index + 1 ) % count];
                    float32       curDist  = normal._x * cur._point._x + normal._y * cur._point._y - offset;
                    float32       nextDist = normal._x * next._point._x + normal._y * next._point._y - offset;
                    curDist                = MathUtil::abs( curDist ) <= kLineEpsilon ? 0.0f : curDist;
                    nextDist               = MathUtil::abs( nextDist ) <= kLineEpsilon ? 0.0f : nextDist;
                    const bool bCurKept    = curDist <= 0.0f;
                    const bool bNextKept   = nextDist <= 0.0f;
                    if ( bCurKept )
                        outListOutput.push_back( cur );
                    if ( bCurKept != bNextKept )
                    {
                        const float32 t     = curDist / ( curDist - nextDist );
                        const float2  point = cur._point + ( next._point - cur._point ) * t;
                        // 나가는 점에서 들어오는 점까지는 자른 변(tag), 들어오는 점에서 다음 꼭짓점까지는 원래 변(cur 의 tag).
                        outListOutput.push_back( Corner{ point, bCurKept ? tag : cur._tag } );
                    }
                }
                // 겹친 점을 뺀다.
                vector<Corner> listClean;
                for ( const Corner& corner : outListOutput )
                {
                    if ( listClean.empty() == false && float2::getDistanceSquared( listClean.back()._point, corner._point ) <= kLineEpsilon * kLineEpsilon )
                    {
                        listClean.back()._tag = corner._tag;
                        continue;
                    }
                    listClean.push_back( corner );
                }
                while ( listClean.size() > 1 && float2::getDistanceSquared( listClean.back()._point, listClean.front()._point ) <= kLineEpsilon * kLineEpsilon )
                    listClean.pop_back();
                outListOutput = std::move( listClean );
            }

            static void placeSites( vector_reference<const float2> listBorder, const FractureSettings& settings, vector<float2>& outListSite )
            {
                float2 boundsMin{ MathUtil::MaxFloat, MathUtil::MaxFloat };
                float2 boundsMax{ MathUtil::MinFloat, MathUtil::MinFloat };
                for ( const float2& point : listBorder )
                {
                    boundsMin = float2::min( boundsMin, point );
                    boundsMax = float2::max( boundsMax, point );
                }
                const float2      extent = boundsMax - boundsMin;
                DestructionRandom random{ settings._seed };
                const auto        addIfInside = [&listBorder, &outListSite]( const float2& point )
                {
                    for ( const float2& existing : outListSite )
                    {
                        if ( float2::getDistanceSquared( existing, point ) < 1.0e-10f )
                            return;
                    }
                    if ( isInside( listBorder, point ) )
                        outListSite.push_back( point );
                };
                if ( settings._pattern == FracturePattern::Slices )
                {
                    const uint32  countX = MathUtil::max( settings._arrSliceCount[0], 1u );
                    const uint32  countY = MathUtil::max( settings._arrSliceCount[1], 1u );
                    const float32 jitter = MathUtil::clamp( settings._sliceJitter, 0.0f, 0.5f );
                    for ( uint32 cellY = 0; cellY < countY; ++cellY )
                    {
                        for ( uint32 cellX = 0; cellX < countX; ++cellX )
                        {
                            const float32 offsetX = 0.5f + ( countX > 1 ? random.nextRange( -jitter, jitter ) : 0.0f );
                            const float32 offsetY = 0.5f + ( countY > 1 ? random.nextRange( -jitter, jitter ) : 0.0f );
                            addIfInside( float2{ boundsMin._x + extent._x * ( static_cast<float32>( cellX ) + offsetX ) / static_cast<float32>( countX ),
                                                 boundsMin._y + extent._y * ( static_cast<float32>( cellY ) + offsetY ) / static_cast<float32>( countY ) } );
                        }
                    }
                    return;
                }
                const uint32 target     = MathUtil::max( settings._pieceCount, 1u );
                uint32       nearTarget = 0;
                if ( settings._pattern == FracturePattern::Clustered )
                    nearTarget = static_cast<uint32>( MathUtil::round( static_cast<float32>( target ) * MathUtil::saturate( settings._clusterFraction ) ) );
                const float2 impact{ settings._impactPoint._x, settings._impactPoint._y };
                for ( uint32 attempt = 0; outListSite.size() < nearTarget && attempt < nearTarget * kSiteAttemptFactor; ++attempt )
                {
                    const float32 angle  = random.nextRange( 0.0f, MathUtil::Pi * 2.0f );
                    const float32 radius = MathUtil::sqrt( random.nextFloat01() ) * settings._clusterRadius;
                    addIfInside( impact + float2{ MathUtil::cos( angle ) * radius, MathUtil::sin( angle ) * radius } );
                }
                for ( uint32 attempt = 0; outListSite.size() < target && attempt < target * kSiteAttemptFactor; ++attempt )
                    addIfInside( float2{ boundsMin._x + extent._x * random.nextFloat01(), boundsMin._y + extent._y * random.nextFloat01() } );
            }

            static RHIVertex makeVertex( const float2& point, float32 normalZ, const float2& boundsMin, const float2& extent )
            {
                RHIVertex vertex{};
                vertex._arrPosition[0] = point._x;
                vertex._arrPosition[1] = point._y;
                vertex._arrNormal[2]   = normalZ;
                vertex._arrUv[0]       = extent._x > 0.0f ? ( point._x - boundsMin._x ) / extent._x : 0.0f;
                vertex._arrUv[1]       = extent._y > 0.0f ? 1.0f - ( point._y - boundsMin._y ) / extent._y : 0.0f;
                for ( float32& channel : vertex._arrColor )
                    channel = 1.0f;
                return vertex;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float32 PolygonFractureUtil::computeArea( vector_reference<const float2> listPolygon, float2& outCentroid )
    {
        float32      twiceArea = 0.0f;
        float2       moment{};
        const size_t count = listPolygon.size();
        for ( size_t index = 0; index < count; ++index )
        {
            const float2& from  = listPolygon[index];
            const float2& to    = listPolygon[( index + 1 ) % count];
            const float32 cross = from._x * to._y - to._x * from._y;
            twiceArea += cross;
            moment += ( from + to ) * cross;
        }
        outCentroid = MathUtil::abs( twiceArea ) > 1.0e-12f ? moment / ( 3.0f * twiceArea ) : ( count > 0 ? listPolygon[0] : float2{} );
        return MathUtil::abs( twiceArea ) * 0.5f;
    }

    bool PolygonFractureUtil::fracture( vector_reference<const float2> listBorder, const FractureSettings& settings, FractureAsset& outAsset, string& outError )
    {
        using Internal = PolygonFractureInternal;
        using Corner   = Internal::Corner;
        outAsset.clear();
        outError.clear();
        vector<float2> listShape( listBorder.begin(), listBorder.end() );
        float2         centroid{};
        if ( listShape.size() < 3 || computeArea( listShape, centroid ) <= 0.0f )
        {
            outError = "the outline needs at least three points and an area";
            return false;
        }
        vector<uint32> listOrder( listShape.size() );
        for ( uint32 index = 0; index < static_cast<uint32>( listOrder.size() ); ++index )
            listOrder[index] = index;
        if ( PolygonTriangulationUtil::computeSignedArea( listShape, listOrder ) < 0.0f )
            std::reverse( listShape.begin(), listShape.end() );

        vector<float2> listSite;
        Internal::placeSites( listShape, settings, listSite );
        if ( listSite.empty() )
            listSite.push_back( centroid );
        const uint32 siteCount = static_cast<uint32>( listSite.size() );

        vector<Corner> listSource;
        for ( const float2& point : listShape )
            listSource.push_back( Corner{ point, Internal::kOuterTag } );
        vector<vector<Corner>>             listCell( siteCount );
        vector<Corner>                     listScratch;
        vector<std::pair<float32, uint32>> listNear;
        for ( uint32 site = 0; site < siteCount; ++site )
        {
            vector<Corner> listCorner = listSource;
            listNear.clear();
            for ( uint32 other = 0; other < siteCount; ++other )
            {
                if ( other != site )
                    listNear.emplace_back( float2::getDistanceSquared( listSite[site], listSite[other] ), other );
            }
            std::sort( listNear.begin(), listNear.end() );
            for ( const std::pair<float32, uint32>& entry : listNear )
            {
                float32 radiusSquared = 0.0f;
                for ( const Corner& corner : listCorner )
                    radiusSquared = MathUtil::max( radiusSquared, float2::getDistanceSquared( corner._point, listSite[site] ) );
                if ( entry.first * 0.25f > radiusSquared )
                    break;
                const float2  delta  = listSite[entry.second] - listSite[site];
                const float2  normal = delta / MathUtil::sqrt( entry.first );
                const float2  middle = ( listSite[entry.second] + listSite[site] ) * 0.5f;
                const float32 offset = normal._x * middle._x + normal._y * middle._y;
                Internal::clip( listCorner, normal, offset, static_cast<int32>( entry.second ), listScratch );
                listCorner.swap( listScratch );
                if ( listCorner.size() < 3 )
                {
                    listCorner.clear();
                    break;
                }
            }
            listCell[site] = std::move( listCorner );
        }

        // 조각 — 넓이 0 은 뺀다.
        vector<uint32> listPieceOfSite( siteCount, 0xFFFFFFFFu );
        vector<uint32> listSiteOfPiece;
        FractureGraph  graph;
        vector<float2> listPoint;
        for ( uint32 site = 0; site < siteCount; ++site )
        {
            listPoint.clear();
            for ( const Corner& corner : listCell[site] )
                listPoint.push_back( corner._point );
            float2        pieceCentroid{};
            const float32 area = listPoint.size() >= 3 ? computeArea( listPoint, pieceCentroid ) : 0.0f;
            if ( area <= 1.0e-10f )
                continue;
            listPieceOfSite[site] = static_cast<uint32>( listSiteOfPiece.size() );
            listSiteOfPiece.push_back( site );
            FractureNode node;
            node._centroid = float3{ pieceCentroid, 0.0f };
            node._volume   = area;
            graph._listNode.push_back( node );
        }
        if ( listSiteOfPiece.empty() )
        {
            outError = "no piece has area";
            return false;
        }
        const uint32 pieceCount = static_cast<uint32>( listSiteOfPiece.size() );
        graph._leafCount        = pieceCount;

        // 연결 — 조각 i 에서 j 쪽 직선이 낸 변 길이와 그 반대 중 짧은 것.
        vector<float32> listLength( static_cast<size_t>( pieceCount ) * pieceCount, 0.0f );
        for ( uint32 piece = 0; piece < pieceCount; ++piece )
        {
            const vector<Corner>& listCorner = listCell[listSiteOfPiece[piece]];
            for ( size_t index = 0; index < listCorner.size(); ++index )
            {
                const Corner& corner = listCorner[index];
                if ( corner._tag < 0 || listPieceOfSite[static_cast<uint32>( corner._tag )] == 0xFFFFFFFFu )
                    continue;
                const uint32 other = listPieceOfSite[static_cast<uint32>( corner._tag )];
                listLength[static_cast<size_t>( piece ) * pieceCount + other] +=
                    float2::getDistance( corner._point, listCorner[( index + 1 ) % listCorner.size()]._point );
            }
        }
        for ( uint32 pieceA = 0; pieceA < pieceCount; ++pieceA )
        {
            for ( uint32 pieceB = pieceA + 1; pieceB < pieceCount; ++pieceB )
            {
                const float32 length = MathUtil::min( listLength[static_cast<size_t>( pieceA ) * pieceCount + pieceB], listLength[static_cast<size_t>( pieceB ) * pieceCount + pieceA] );
                if ( length > 0.0f )
                    graph._listLink.push_back( FractureLink{ pieceA, pieceB, length } );
            }
        }
        FractureGraphUtil::normalizeLinks( graph._listLink );
        vector<uint32> listLeafOrder;
        FractureGraphUtil::buildHierarchy( graph, settings._listLevelCount, listLeafOrder );

        float2 boundsMin{ MathUtil::MaxFloat, MathUtil::MaxFloat };
        float2 boundsMax{ MathUtil::MinFloat, MathUtil::MinFloat };
        for ( const float2& point : listShape )
        {
            boundsMin = float2::min( boundsMin, point );
            boundsMax = float2::max( boundsMax, point );
        }
        const float2 extent = boundsMax - boundsMin;
        outAsset._graph     = std::move( graph );
        outAsset._seed      = settings._seed;
        outAsset._boundsMin = float3{ boundsMin, 0.0f };
        outAsset._boundsMax = float3{ boundsMax, 0.0f };
        vector<uint32> listIndex;
        for ( uint32 leaf = 0; leaf < pieceCount; ++leaf )
        {
            const vector<Corner>& listCorner = listCell[listSiteOfPiece[listLeafOrder[leaf]]];
            listPoint.clear();
            for ( const Corner& corner : listCorner )
                listPoint.push_back( corner._point );
            listIndex.clear();
            vector<uint32> listLoop( listPoint.size() );
            for ( uint32 index = 0; index < static_cast<uint32>( listLoop.size() ); ++index )
                listLoop[index] = index;
            (void)PolygonTriangulationUtil::triangulate( listPoint, vector<vector<uint32>>{ listLoop }, listIndex );

            FracturePiece piece;
            piece._firstVertex = static_cast<uint32>( outAsset._listVertex.size() );
            for ( size_t index = 0; index + 2 < listIndex.size(); index += 3 )
            {
                // 앞(+Z) 반시계 그대로 · 뒤(−Z) 뒤집어 — 어느 쪽에서 보아도 보인다.
                const float2& a = listPoint[listIndex[index]];
                const float2& b = listPoint[listIndex[index + 1]];
                const float2& c = listPoint[listIndex[index + 2]];
                for ( const float2* pPoint : { &a, &b, &c } )
                    outAsset._listVertex.push_back( Internal::makeVertex( *pPoint, 1.0f, boundsMin, extent ) );
                for ( const float2* pPoint : { &a, &c, &b } )
                    outAsset._listVertex.push_back( Internal::makeVertex( *pPoint, -1.0f, boundsMin, extent ) );
                outAsset._listTriangleSlot.push_back( static_cast<uint8>( FractureSurfaceSlot::Outer ) );
                outAsset._listTriangleSlot.push_back( static_cast<uint8>( FractureSurfaceSlot::Outer ) );
            }
            piece._vertexCount        = static_cast<uint32>( outAsset._listVertex.size() ) - piece._firstVertex;
            piece._firstHullPoint     = static_cast<uint32>( outAsset._listHullPoint.size() );
            const float3& pieceCenter = outAsset._graph._listNode[leaf]._centroid;
            float2        pieceMin{ MathUtil::MaxFloat, MathUtil::MaxFloat };
            float2        pieceMax{ MathUtil::MinFloat, MathUtil::MinFloat };
            for ( const float2& point : listPoint )
            {
                outAsset._listHullPoint.push_back( float3{ point, 0.0f } - pieceCenter );
                pieceMin = float2::min( pieceMin, point );
                pieceMax = float2::max( pieceMax, point );
            }
            piece._hullPointCount = static_cast<uint32>( listPoint.size() );
            piece._boundsMin      = float3{ pieceMin, 0.0f };
            piece._boundsMax      = float3{ pieceMax, 0.0f };
            outAsset._listPiece.push_back( piece );
        }
        string validation;
        if ( outAsset.isValid( &validation ) == false )
        {
            outError = string( "polygon fracture produced an invalid asset: " ) + validation;
            outAsset.clear();
            return false;
        }
        return true;
    }
} // namespace sw
