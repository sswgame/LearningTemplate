#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/Model/MeshFracture.h"

#include "Engine/Destruction/Fracture/FractureAsset.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 한 변 2 의 상자(가운데 @p center)와 그 부피 계산을 돕습니다. */
    struct MeshFractureTestUtil
    {
        static RHIVertex makeVertex( const float3& position )
        {
            RHIVertex vertex{};
            vertex._arrPosition[0] = position._x;
            vertex._arrPosition[1] = position._y;
            vertex._arrPosition[2] = position._z;
            return vertex;
        }

        /** @brief 바깥이 반시계인 상자 삼각형 목록과 인덱스(0 부터 차례)입니다. @p skipFaces 만큼 뒤 면을 뺍니다(열린 상자). */
        static void makeBox( const float3& center, vector<RHIVertex>& outListVertex, vector<uint32>& outListIndex, uint32 skipFaces = 0 )
        {
            static constexpr int32 kArrFace[6][4][3] = {
                { { -1, -1, 1 },   { 1, -1, 1 },   { 1, 1, 1 },  { -1, 1, 1 }},
                { { 1, -1, -1 }, { -1, -1, -1 }, { -1, 1, -1 },  { 1, 1, -1 }},
                {  { 1, -1, 1 },  { 1, -1, -1 },  { 1, 1, -1 },   { 1, 1, 1 }},
                {{ -1, -1, -1 },  { -1, -1, 1 },  { -1, 1, 1 }, { -1, 1, -1 }},
                {  { -1, 1, 1 },    { 1, 1, 1 },  { 1, 1, -1 }, { -1, 1, -1 }},
                {{ -1, -1, -1 },  { 1, -1, -1 },  { 1, -1, 1 }, { -1, -1, 1 }},
            };
            outListVertex.clear();
            outListIndex.clear();
            for ( uint32 face = 0; face < 6 - skipFaces; ++face )
            {
                static constexpr uint32 kArrCorner[6] = { 0, 1, 2, 0, 2, 3 };
                for ( const uint32 corner : kArrCorner )
                {
                    const int32* pCorner = kArrFace[face][corner];
                    outListIndex.push_back( static_cast<uint32>( outListVertex.size() ) );
                    outListVertex.push_back( makeVertex( center + float3{ static_cast<float32>( pCorner[0] ), static_cast<float32>( pCorner[1] ), static_cast<float32>( pCorner[2] ) } ) );
                }
            }
        }

        static vector<RHIVertex> makeBoxVertices( const float3& center = float3{} )
        {
            vector<RHIVertex> listVertex;
            vector<uint32>    listIndex;
            makeBox( center, listVertex, listIndex );
            return listVertex;
        }

        /** @brief 상자 하나를 쪼갭니다. 실패하면 테스트가 집니다. */
        static FractureAsset fractureBox( const FractureSettings& settings )
        {
            vector<RHIVertex> listVertex;
            vector<uint32>    listIndex;
            makeBox( float3{}, listVertex, listIndex );
            expected<FractureAsset> asset = MeshFractureUtil::fracture( listVertex, listIndex, settings );
            SW_EXPECT_TRUE_MSG( asset.has_value(), asset.has_value() ? "" : asset.error().message().data() );
            return asset.has_value() ? std::move( *asset ) : FractureAsset{};
        }

        static float32 sumPieceVolume( const FractureAsset& asset )
        {
            float32 total = 0.0f;
            for ( uint32 piece = 0; piece < asset.getPieceCount(); ++piece )
            {
                float3 centroid{};
                total += MeshFractureUtil::computeVolume( asset.getPieceVertices( piece ), centroid );
            }
            return total;
        }

        /** @brief 연결을 따라 첫 잎에서 닿는 잎 수입니다. */
        static uint32 countConnectedLeaves( const FractureGraph& graph )
        {
            vector<uint8>  listSeen( graph._leafCount, 0 );
            vector<uint32> listStack{ 0 };
            uint32         count = 0;
            listSeen[0]          = 1;
            while ( listStack.empty() == false )
            {
                const uint32 leaf = listStack.back();
                listStack.pop_back();
                ++count;
                for ( const FractureLink& link : graph._listLink )
                {
                    const uint32 other = link._leafA == leaf ? link._leafB : ( link._leafB == leaf ? link._leafA : leaf );
                    if ( listSeen[other] == 0 )
                    {
                        listSeen[other] = 1;
                        listStack.push_back( other );
                    }
                }
            }
            return count;
        }
    };
    using Util = MeshFractureTestUtil;
} // namespace

// 닫힌 검사: 상자는 닫혔다. 면이 빠지면 열리고, 같은 위치의 정점을 쓴 퇴화 삼각형도 받지 않는다.
SW_TEST_CASE( MeshFractureTest, ClosedChecks )
{
    vector<RHIVertex> listVertex = Util::makeBoxVertices();
    SW_EXPECT_TRUE( MeshFractureUtil::isClosedMesh( listVertex ) );
    SW_EXPECT_TRUE( MeshFractureUtil::isClosedWithinTolerance( listVertex ) );

    vector<RHIVertex> listOpen( listVertex.begin(), listVertex.end() - 3 );
    SW_EXPECT_FALSE( MeshFractureUtil::isClosedMesh( listOpen ) );
    SW_EXPECT_FALSE( MeshFractureUtil::isClosedWithinTolerance( listOpen ) );

    vector<RHIVertex> listShifted = listVertex;
    listShifted[0]._arrPosition[0] += 1.0e-6f; // 비트 검사는 열렸다고 보고, 허용 오차 검사는 닫혔다고 본다
    SW_EXPECT_FALSE( MeshFractureUtil::isClosedMesh( listShifted ) );
    SW_EXPECT_TRUE( MeshFractureUtil::isClosedWithinTolerance( listShifted ) );

    const RHIVertex         point = Util::makeVertex( float3{} );
    const vector<RHIVertex> listDegenerate{ point, point, point };
    SW_EXPECT_FALSE( MeshFractureUtil::isClosedMesh( listDegenerate ) );
    SW_EXPECT_FALSE( MeshFractureUtil::isClosedMesh( vector<RHIVertex>{} ) );
    SW_EXPECT_FALSE( MeshFractureUtil::isClosedWithinTolerance( vector<RHIVertex>{} ) );
}

// 부피와 무게 중심: 위치와 감은 방향에 흔들리지 않는다. 부피가 없으면 0 과 정점 평균이다.
SW_TEST_CASE( MeshFractureTest, VolumeAndCentroid )
{
    float3 centroid{};
    SW_EXPECT_NEAR( MeshFractureUtil::computeVolume( Util::makeBoxVertices(), centroid ), 8.0f, 1.0e-4f );
    SW_EXPECT_NEAR( centroid.getLength(), 0.0f, 1.0e-4f );

    const float3      center{ 5.0f, -3.0f, 2.0f };
    vector<RHIVertex> listVertex = Util::makeBoxVertices( center );
    SW_EXPECT_NEAR( MeshFractureUtil::computeVolume( listVertex, centroid ), 8.0f, 1.0e-3f );
    SW_EXPECT_NEAR( ( centroid - center ).getLength(), 0.0f, 1.0e-3f );

    for ( size_t index = 0; index + 2 < listVertex.size(); index += 3 )
    {
        std::swap( listVertex[index + 1], listVertex[index + 2] );
    }
    SW_EXPECT_NEAR( MeshFractureUtil::computeVolume( listVertex, centroid ), 8.0f, 1.0e-3f );
    SW_EXPECT_NEAR( ( centroid - center ).getLength(), 0.0f, 1.0e-3f );

    const vector<RHIVertex> listFlat{ Util::makeVertex( float3{ 0.0f, 0.0f, 0.0f } ), Util::makeVertex( float3{ 3.0f, 0.0f, 0.0f } ), Util::makeVertex( float3{ 0.0f, 3.0f, 0.0f } ) };
    SW_EXPECT_NEAR( MeshFractureUtil::computeVolume( listFlat, centroid ), 0.0f, 1.0e-6f );
    SW_EXPECT_NEAR( centroid._x, 1.0f, 1.0e-5f );
    SW_EXPECT_NEAR( MeshFractureUtil::computeVolume( vector<RHIVertex>{}, centroid ), 0.0f, 1.0e-6f );
}

// 볼록 껍질: 안쪽 점은 빠지고 닫혀 있으며 부피가 같다. 한 평면 · 점 부족은 비어 있다.
SW_TEST_CASE( MeshFractureTest, ConvexHullDropsInnerPoints )
{
    vector<RHIVertex> listVertex = Util::makeBoxVertices();
    listVertex.push_back( Util::makeVertex( float3{ 0.25f, 0.25f, 0.25f } ) );
    vector<RHIVertex> listHull;
    MeshFractureUtil::makeConvexHull( listVertex, listHull );
    SW_REQUIRE_FALSE( listHull.empty() );
    SW_EXPECT_TRUE( MeshFractureUtil::isClosedMesh( listHull ) );
    float3 centroid{};
    SW_EXPECT_NEAR( MeshFractureUtil::computeVolume( listHull, centroid ), 8.0f, 1.0e-3f );
    for ( const RHIVertex& vertex : listHull )
    {
        SW_EXPECT_NEAR( MathUtil::abs( vertex._arrPosition[0] ), 1.0f, 1.0e-5f );
    }

    const vector<RHIVertex> listPlanar{ Util::makeVertex( float3{ 0.0f, 0.0f, 0.0f } ), Util::makeVertex( float3{ 1.0f, 0.0f, 0.0f } ), Util::makeVertex( float3{ 0.0f, 1.0f, 0.0f } ),
                                        Util::makeVertex( float3{ 1.0f, 1.0f, 0.0f } ), Util::makeVertex( float3{ 2.0f, 2.0f, 0.0f } ) };
    MeshFractureUtil::makeConvexHull( listPlanar, listHull );
    SW_EXPECT_TRUE( listHull.empty() );

    const vector<RHIVertex> listFew{ Util::makeVertex( float3{ 0.0f, 0.0f, 0.0f } ), Util::makeVertex( float3{ 1.0f, 0.0f, 0.0f } ), Util::makeVertex( float3{ 0.0f, 1.0f, 0.0f } ) };
    MeshFractureUtil::makeConvexHull( listFew, listHull );
    SW_EXPECT_TRUE( listHull.empty() );
}

SW_TEST_CASE( MeshFractureTest, PointInsideUsesWinding )
{
    vector<RHIVertex> listVertex = Util::makeBoxVertices();
    SW_EXPECT_TRUE( MeshFractureUtil::isPointInside( listVertex, float3{ 0.5f, 0.5f, 0.5f } ) );
    SW_EXPECT_FALSE( MeshFractureUtil::isPointInside( listVertex, float3{ 1.5f, 0.0f, 0.0f } ) );
    for ( size_t index = 0; index + 2 < listVertex.size(); index += 3 )
    {
        std::swap( listVertex[index + 1], listVertex[index + 2] );
    }
    SW_EXPECT_TRUE( MeshFractureUtil::isPointInside( listVertex, float3{ 0.5f, 0.5f, 0.5f } ) ); // 뒤집혀도 같다
}

// 고른 쪼개기: 조각들의 부피 합이 원래와 같고, 조각마다 닫혀 있고, 연결로 모두 이어지며, 안쪽 면이 생긴다.
SW_TEST_CASE( MeshFractureTest, UniformPiecesPartitionTheVolume )
{
    FractureSettings settings;
    settings._pieceCount      = 12;
    settings._seed            = 7;
    const FractureAsset asset = Util::fractureBox( settings );

    SW_REQUIRE_TRUE( asset.getPieceCount() > 1 );
    SW_EXPECT_TRUE( asset.getPieceCount() <= 12 );
    SW_EXPECT_TRUE( asset.validate() );
    SW_EXPECT_EQUAL( asset._seed, uint64{ 7 } );
    SW_EXPECT_NEAR( Util::sumPieceVolume( asset ), 8.0f, 1.0e-2f );
    SW_EXPECT_NEAR( asset._boundsMax._x, 1.0f, 1.0e-5f );
    SW_EXPECT_TRUE( asset.countTriangles( FractureSurfaceSlot::Interior ) > 0 );
    SW_EXPECT_TRUE( asset.countTriangles( FractureSurfaceSlot::Outer ) > 0 );
    SW_EXPECT_EQUAL( Util::countConnectedLeaves( asset._graph ), asset.getPieceCount() );

    for ( uint32 piece = 0; piece < asset.getPieceCount(); ++piece )
    {
        const vector_reference<const RHIVertex> listVertex = asset.getPieceVertices( piece );
        SW_EXPECT_TRUE( MeshFractureUtil::isClosedMesh( listVertex ) );
        float3        centroid{};
        const float32 volume = MeshFractureUtil::computeVolume( listVertex, centroid );
        SW_EXPECT_TRUE( volume > 0.0f );
        SW_EXPECT_NEAR( asset._graph._listNode[piece]._volume, volume, 1.0e-3f );
        SW_EXPECT_TRUE( asset.getPieceHull( piece ).size() >= 4 );
        SW_EXPECT_TRUE( MeshFractureUtil::isPointInside( listVertex, centroid ) ); // 볼록한 조각의 무게 중심은 안이다
    }
}

// 같은 메시 · 설정 · 시드면 바이트까지 같고, 시드가 다르면 다르다.
SW_TEST_CASE( MeshFractureTest, SeedDecidesTheResult )
{
    FractureSettings settings;
    settings._pieceCount = 8;
    settings._seed       = 11;
    vector<uint8> first;
    vector<uint8> second;
    Util::fractureBox( settings ).makeBytes( first );
    Util::fractureBox( settings ).makeBytes( second );
    SW_EXPECT_TRUE( first == second );

    settings._seed = 12;
    Util::fractureBox( settings ).makeBytes( second );
    SW_EXPECT_FALSE( first == second );
}

// 흔들림 없는 평면 조각 둘: 부피 반씩, 연결 하나, 맞닿은 넓이는 잘린 면이다.
SW_TEST_CASE( MeshFractureTest, SlicesCutAlongTheGrid )
{
    FractureSettings settings;
    settings._pattern          = FracturePattern::Slices;
    settings._arrSliceCount[0] = 2;
    settings._arrSliceCount[1] = 1;
    settings._arrSliceCount[2] = 1;
    settings._sliceJitter      = 0.0f;
    const FractureAsset asset  = Util::fractureBox( settings );

    SW_REQUIRE_EQUAL( asset.getPieceCount(), 2u );
    SW_EXPECT_NEAR( asset._graph._listNode[0]._volume, 4.0f, 1.0e-3f );
    SW_EXPECT_NEAR( asset._graph._listNode[1]._volume, 4.0f, 1.0e-3f );
    SW_REQUIRE_EQUAL( asset._graph._listLink.size(), size_t{ 1 } );
    SW_EXPECT_NEAR( asset._graph._listLink[0]._area, 4.0f, 1.0e-3f );
    SW_EXPECT_EQUAL( asset.countTriangles( FractureSurfaceSlot::Interior ) % 2, 0u ); // 두 조각이 같은 면을 하나씩 막는다
}

// 몰린 쪼개기: 맞은 지점 쪽 조각이 더 잘다.
SW_TEST_CASE( MeshFractureTest, ClusteredPiecesAreSmallerNearTheImpact )
{
    FractureSettings settings;
    settings._pattern         = FracturePattern::Clustered;
    settings._pieceCount      = 24;
    settings._clusterRadius   = 0.4f;
    settings._clusterFraction = 0.8f;
    settings._impactPoint     = float3{ 1.0f, 1.0f, 1.0f };
    const FractureAsset asset = Util::fractureBox( settings );
    SW_EXPECT_NEAR( Util::sumPieceVolume( asset ), 8.0f, 2.0e-2f );

    float32 nearVolume = 0.0f;
    uint32  nearCount  = 0;
    float32 farVolume  = 0.0f;
    uint32  farCount   = 0;
    for ( uint32 piece = 0; piece < asset.getPieceCount(); ++piece )
    {
        const FractureNode& node  = asset._graph._listNode[piece];
        const bool          bNear = ( node._centroid - settings._impactPoint ).getLength() < 1.0f;
        ( bNear ? nearVolume : farVolume ) += node._volume;
        ( bNear ? nearCount : farCount ) += 1;
    }
    SW_REQUIRE_TRUE( nearCount > 0 );
    SW_REQUIRE_TRUE( farCount > 0 );
    SW_EXPECT_TRUE( nearVolume / static_cast<float32>( nearCount ) < farVolume / static_cast<float32>( farCount ) );
}

// 묶음 계층을 달아도 유효하고, 잎 순서가 바뀐 뒤에도 조각 메시와 노드가 짝이 맞다.
SW_TEST_CASE( MeshFractureTest, HierarchyKeepsPiecesPaired )
{
    FractureSettings settings;
    settings._pieceCount      = 16;
    settings._listLevelCount  = { 2, 4 };
    const FractureAsset asset = Util::fractureBox( settings );
    SW_EXPECT_TRUE( asset.validate() );
    SW_EXPECT_TRUE( asset._graph.getDepthCount() >= 2 );
    for ( uint32 piece = 0; piece < asset.getPieceCount(); ++piece )
    {
        float3        centroid{};
        const float32 volume = MeshFractureUtil::computeVolume( asset.getPieceVertices( piece ), centroid );
        SW_EXPECT_NEAR( asset._graph._listNode[piece]._volume, volume, 1.0e-3f );
        SW_EXPECT_NEAR( ( asset._graph._listNode[piece]._centroid - centroid ).getLength(), 0.0f, 1.0e-3f );
    }
    SW_EXPECT_NEAR( asset._graph._listNode[asset._graph.getRootNode()]._volume, 8.0f, 1.0e-2f );
}

// 껍질 점은 상한을 넘지 않고, 무게 중심 기준이다.
SW_TEST_CASE( MeshFractureTest, HullPointsStayWithinTheLimit )
{
    FractureSettings settings;
    settings._pieceCount      = 2;
    settings._maxHullPoint    = 8;
    const FractureAsset asset = Util::fractureBox( settings );
    for ( uint32 piece = 0; piece < asset.getPieceCount(); ++piece )
    {
        const vector_reference<const float3> listHull = asset.getPieceHull( piece );
        SW_EXPECT_TRUE( listHull.size() <= 8 );
        float3 sum{};
        for ( const float3& point : listHull )
        {
            sum += point;
            SW_EXPECT_TRUE( point.getLength() < 4.0f );
        }
        SW_EXPECT_TRUE( sum.getLength() / static_cast<float32>( listHull.size() ) < 2.0f );
    }
}

// 입력 오류는 이유를 돌려준다.
SW_TEST_CASE( MeshFractureTest, BadInputExplainsItself )
{
    vector<RHIVertex> listVertex;
    vector<uint32>    listIndex;
    Util::makeBox( float3{}, listVertex, listIndex );
    const FractureSettings settings;

    SW_EXPECT_FALSE( MeshFractureUtil::fracture( listVertex, vector<uint32>{}, settings ).has_value() );
    SW_EXPECT_FALSE( MeshFractureUtil::fracture( listVertex, vector<uint32>{ 0, 1 }, settings ).has_value() );
    const expected<FractureAsset> pastEnd = MeshFractureUtil::fracture( listVertex, vector<uint32>{ 0, 1, 9999 }, settings );
    SW_REQUIRE_FALSE( pastEnd.has_value() );
    SW_EXPECT_TRUE( pastEnd.error().message().find( "index" ) != string_view::npos );

    vector<RHIVertex> listOpen;
    vector<uint32>    listOpenIndex;
    Util::makeBox( float3{}, listOpen, listOpenIndex, 1 );
    const expected<FractureAsset> open = MeshFractureUtil::fracture( listOpen, listOpenIndex, settings );
    SW_REQUIRE_FALSE( open.has_value() );
    SW_EXPECT_TRUE( open.error().message().find( "not closed" ) != string_view::npos );
}

// 열린 모델은 대리 부피로 쪼갠다: 부피는 대리의 것이고, 겉면은 원래 메시의 조각이다(열린 곳은 열린 채다).
SW_TEST_CASE( MeshFractureTest, ProxyVolumesCutOpenMeshes )
{
    vector<RHIVertex> listOpen;
    vector<uint32>    listOpenIndex;
    Util::makeBox( float3{}, listOpen, listOpenIndex, 1 );

    for ( const FractureVolume volume : { FractureVolume::Bounds, FractureVolume::Hull } )
    {
        FractureSettings settings;
        settings._volume                        = volume;
        settings._pieceCount                    = 6;
        const expected<FractureAsset> fractured = MeshFractureUtil::fracture( listOpen, listOpenIndex, settings );
        SW_REQUIRE_TRUE( fractured.has_value() );
        const FractureAsset& asset = *fractured;
        SW_EXPECT_TRUE( asset.getPieceCount() > 1 );
        float32 total = 0.0f;
        for ( uint32 piece = 0; piece < asset.getPieceCount(); ++piece )
        {
            total += asset._graph._listNode[piece]._volume;
        }
        SW_EXPECT_NEAR( total, 8.0f, 1.0e-2f );
        SW_EXPECT_TRUE( asset.countTriangles( FractureSurfaceSlot::Outer ) > 0 );
        SW_EXPECT_TRUE( asset.countTriangles( FractureSurfaceSlot::Interior ) > 0 );
    }

    // 평면 메시는 껍질이 없고 경계 상자도 두께가 0 이다 - 자르기 전에 이유를 돌려준다.
    const vector<RHIVertex> listFlat{ Util::makeVertex( float3{ 0.0f, 0.0f, 0.0f } ), Util::makeVertex( float3{ 1.0f, 0.0f, 0.0f } ), Util::makeVertex( float3{ 0.0f, 1.0f, 0.0f } ) };
    const vector<uint32>    listFlatIndex{ 0, 1, 2 };
    FractureSettings        settings;
    settings._volume                   = FractureVolume::Hull;
    const expected<FractureAsset> flat = MeshFractureUtil::fracture( listFlat, listFlatIndex, settings );
    SW_REQUIRE_FALSE( flat.has_value() );
    SW_EXPECT_TRUE( flat.error().message().find( "no volume" ) != string_view::npos );
}

// 구운 파일 왕복: 바이트로 썼다 읽으면 같고, 다시 쓰면 바이트까지 같다.
SW_TEST_CASE( FractureAssetTest, BytesRoundTrip )
{
    FractureSettings settings;
    settings._pieceCount      = 6;
    settings._listLevelCount  = { 2 };
    const FractureAsset asset = Util::fractureBox( settings );

    vector<uint8> bytes;
    asset.makeBytes( bytes );
    FractureAsset read;
    SW_REQUIRE_TRUE( read.readFromBytes( bytes.data(), bytes.size() ) );
    SW_EXPECT_EQUAL( read.getPieceCount(), asset.getPieceCount() );
    SW_EXPECT_EQUAL( read._seed, asset._seed );
    SW_EXPECT_EQUAL( read._graph._listLink.size(), asset._graph._listLink.size() );
    SW_EXPECT_EQUAL( read.countTriangles( FractureSurfaceSlot::Interior ), asset.countTriangles( FractureSurfaceSlot::Interior ) );

    vector<uint8> again;
    read.makeBytes( again );
    SW_EXPECT_TRUE( again == bytes );
}

SW_TEST_CASE( FractureAssetTest, RefusesBadMagic )
{
    SW_EXPECT_LOG( "FractureAsset.NotFractureAssetBad" );
    FractureAsset asset;
    const uint8   arrBytes[16] = {};
    SW_EXPECT_FALSE( asset.readFromBytes( arrBytes, sizeof( arrBytes ) ) );
    SW_EXPECT_FALSE( asset.readFromBytes( nullptr, 0 ) );
}

SW_TEST_CASE( FractureAssetTest, RefusesOtherVersionAndTruncation )
{
    FractureSettings settings;
    settings._pieceCount = 2;
    vector<uint8> bytes;
    Util::fractureBox( settings ).makeBytes( bytes );

    SW_EXPECT_LOG( "FractureAsset.FractureAssetVersionNot" );
    vector<uint8> wrongVersion = bytes;
    wrongVersion[4] ^= 0xFF;
    FractureAsset asset;
    SW_EXPECT_FALSE( asset.readFromBytes( wrongVersion.data(), wrongVersion.size() ) );

    SW_EXPECT_LOG( "FractureAsset.FractureAssetTruncated" );
    SW_EXPECT_FALSE( asset.readFromBytes( bytes.data(), 40 ) );
    SW_EXPECT_EQUAL( asset.getPieceCount(), 0u );

    SW_EXPECT_LOG( "FractureAsset.FractureAssetMalformed" );
    SW_EXPECT_FALSE( asset.readFromBytes( bytes.data(), bytes.size() - 1 ) );
    vector<uint8> longer = bytes;
    longer.push_back( 0 );
    SW_EXPECT_FALSE( asset.readFromBytes( longer.data(), longer.size() ) );
    SW_EXPECT_EQUAL( asset.getPieceCount(), 0u );
}

/** @brief [FractureAssetTest] 어느 길이로 잘려도 거절하고 조각을 남기지 않는다 */
SW_TEST_CASE( FractureAssetTest, EveryTruncatedLengthIsRefused )
{
    SW_EXPECT_LOG( "FractureAsset.NotFractureAssetBad" );
    SW_EXPECT_LOG( "FractureAsset.FractureAssetTruncated" );
    SW_EXPECT_LOG( "FractureAsset.FractureAssetMalformed" );
    FractureSettings settings;
    settings._pieceCount = 2;
    vector<uint8> bytes;
    Util::fractureBox( settings ).makeBytes( bytes );

    for ( size_t length = 0; length < bytes.size(); ++length )
    {
        FractureAsset asset;
        SW_EXPECT_FALSE( asset.readFromBytes( bytes.data(), length ) );
        SW_EXPECT_EQUAL( asset.getPieceCount(), 0u );
    }
}

/** @brief [FractureAssetTest] 꾸민 개수는 할당 전에, NaN 은 칸에서 거절한다 */
SW_TEST_CASE( FractureAssetTest, ForgedCountsAndNonFiniteValuesAreRefused )
{
    SW_EXPECT_LOG( "FractureAsset.FractureAssetTruncated" );
    SW_EXPECT_LOG( "FractureAsset.FractureAssetMalformed" );
    FractureSettings settings;
    settings._pieceCount = 2;
    vector<uint8> bytes;
    Util::fractureBox( settings ).makeBytes( bytes );

    // 바이트 자리: 매직(0) · 버전(4) · 잎 · 노드 · 자식 · 연결 · 정점 · 껍질(8 ~ 28) · 씨앗(32) · 경계(40 ~ 64) · 노드.
    for ( size_t offset = 8; offset <= 28; offset += sizeof( uint32 ) )
    {
        vector<uint8>    crafted = bytes;
        constexpr uint32 kHuge   = 0xFFFFFFFFu;
        Memory::copy( crafted.data() + offset, &kHuge, sizeof( kHuge ) );
        FractureAsset asset;
        SW_EXPECT_FALSE( asset.readFromBytes( crafted.data(), crafted.size() ) );
        SW_EXPECT_EQUAL( asset.getPieceCount(), 0u );
    }
    const uint32 nanBits = 0x7FC00000u;
    for ( const size_t offset : { size_t{ 40 }, size_t{ 64 } } )
    {
        vector<uint8> crafted = bytes;
        Memory::copy( crafted.data() + offset, &nanBits, sizeof( nanBits ) );
        FractureAsset asset;
        SW_EXPECT_FALSE( asset.readFromBytes( crafted.data(), crafted.size() ) );
    }
}

SW_TEST_CASE( FractureAssetTest, PathForMeshSwapsTheExtension )
{
    SW_EXPECT_EQUAL( FractureAsset::makePathForMesh( "a/b.mesh" ), string( "a/b.fracture" ) );
}
