#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/FractureGraph.h"
#include "Engine/Destruction/MeshFracture.h"
#include "Engine/Destruction/PolygonTriangulation.h"

#include "EngineTest/DestructionTestUtil.h"

#include "TestFramework/TestFramework.h"

// FractureTest — 보로노이 파쇄(조각이 닫히고 부피가 보존되고 결정적이다) · 패턴 · 묶음 계층 · 연결 넓이 · 껍질 · `.fracture` 바이트 왕복 · 다각형 삼각분할.

namespace
{
    struct TestFractureInternal
    {
        static float32 computeTotalArea( const sw::vector<sw::float2>& listPoint, const sw::vector<uint32>& listIndex )
        {
            float32 area = 0.0f;
            for ( size_t index = 0; index + 2 < listIndex.size(); index += 3 )
            {
                const sw::float2& a = listPoint[listIndex[index]];
                const sw::float2& b = listPoint[listIndex[index + 1]];
                const sw::float2& c = listPoint[listIndex[index + 2]];
                area += ( ( b._x - a._x ) * ( c._y - a._y ) - ( b._y - a._y ) * ( c._x - a._x ) ) * 0.5f;
            }
            return area;
        }

        static bool fractureBox( const sw::FractureSettings& settings, sw::FractureAsset& outAsset )
        {
            const sw::vector<sw::RHIVertex> listBox = test::DestructionTestUtil::makeBox( sw::float3{ 1.0f, 0.5f, 0.25f } );
            sw::string                      error;
            return sw::MeshFractureUtil::fracture( listBox, settings, outAsset, error );
        }

        static float32 sumPieceVolumes( const sw::FractureAsset& asset )
        {
            float32 total = 0.0f;
            for ( uint32 piece = 0; piece < asset.getPieceCount(); ++piece )
                total += asset._graph._listNode[piece]._volume;
            return total;
        }
    };
} // namespace

/**
 * @brief [FractureTest] 오목 다각형 · 구멍 난 다각형도 넓이를 지키며 바깥 고리와 같은 방향으로 삼각형을 낸다(부채꼴이면 오목한 곳에서 넓이가 넘친다)
 */
SW_TEST_CASE( FractureTest, TriangulationKeepsAreaAndWindingForConcaveAndHoledPolygons )
{
    // L 자(넓이 3) — 반시계.
    const sw::vector<sw::float2> listL = {
        sw::float2{0.0f, 0.0f},
        sw::float2{2.0f, 0.0f},
        sw::float2{2.0f, 1.0f},
        sw::float2{1.0f, 1.0f},
        sw::float2{1.0f, 2.0f},
        sw::float2{0.0f, 2.0f}
    };
    sw::vector<uint32> listIndex;
    SW_EXPECT_TRUE( sw::PolygonTriangulationUtil::triangulate( listL, sw::vector<sw::vector<uint32>>{
                                                                          sw::vector<uint32>{ 1, 2, 3, 4, 5, 0 }
    },
                                                               listIndex ) );
    SW_EXPECT_EQUAL( size_t( 12 ), listIndex.size() );
    SW_EXPECT_NEAR_EQUAL( 3.0f, TestFractureInternal::computeTotalArea( listL, listIndex ), 1e-5f );

    // 시계 방향으로 넘기면 시계 방향 삼각형(넓이 -3).
    listIndex.clear();
    SW_EXPECT_TRUE( sw::PolygonTriangulationUtil::triangulate( listL, sw::vector<sw::vector<uint32>>{
                                                                          sw::vector<uint32>{ 5, 4, 3, 2, 1, 0 }
    },
                                                               listIndex ) );
    SW_EXPECT_NEAR_EQUAL( -3.0f, TestFractureInternal::computeTotalArea( listL, listIndex ), 1e-5f );

    // 4 × 4 사각형(반시계)에 1 × 1 구멍(시계) — 넓이 15.
    const sw::vector<sw::float2> listHoled = {
        sw::float2{0.0f, 0.0f},
        sw::float2{4.0f, 0.0f},
        sw::float2{4.0f, 4.0f},
        sw::float2{0.0f, 4.0f},
        sw::float2{1.5f, 1.5f},
        sw::float2{1.5f, 2.5f},
        sw::float2{2.5f, 2.5f},
        sw::float2{2.5f, 1.5f}
    };
    listIndex.clear();
    SW_EXPECT_TRUE( sw::PolygonTriangulationUtil::triangulate( listHoled, sw::vector<sw::vector<uint32>>{
                                                                              sw::vector<uint32>{0, 1, 2, 3},
                                                                              sw::vector<uint32>{4, 5, 6, 7}
    },
                                                               listIndex ) );
    SW_EXPECT_NEAR_EQUAL( 15.0f, TestFractureInternal::computeTotalArea( listHoled, listIndex ), 1e-4f );
    for ( size_t index = 0; index + 2 < listIndex.size(); index += 3 )
    {
        const sw::vector<uint32> triangle{ listIndex[index], listIndex[index + 1], listIndex[index + 2] };
        SW_EXPECT_TRUE( sw::PolygonTriangulationUtil::computeSignedArea( listHoled, triangle ) >= 0.0f );
    }
}

/**
 * @brief [FractureTest] 상자를 고르게 쪼개면 모든 조각이 닫힌 메시이고, 부피 합이 상자 부피이고, 안쪽 면이 칸 1 로 붙는다
 */
SW_TEST_CASE( FractureTest, UniformFractureGivesClosedPiecesThatConserveVolume )
{
    sw::FractureSettings settings;
    settings._pieceCount = 24;
    settings._seed       = 7;
    sw::FractureAsset asset;
    SW_ASSERT_TRUE( TestFractureInternal::fractureBox( settings, asset ) );
    SW_EXPECT_EQUAL( 24u, asset.getPieceCount() );
    SW_EXPECT_TRUE( asset.isValid() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, TestFractureInternal::sumPieceVolumes( asset ), 1e-3f );
    SW_EXPECT_TRUE( asset.countTriangles( sw::FractureSurfaceSlot::Interior ) > 0 );
    SW_EXPECT_TRUE( asset.countTriangles( sw::FractureSurfaceSlot::Outer ) >= 12 );
    for ( uint32 piece = 0; piece < asset.getPieceCount(); ++piece )
    {
        SW_EXPECT_TRUE_MSG( sw::MeshFractureUtil::isClosedMesh( asset.getPieceVertices( piece ) ), "every piece must be a closed mesh (caps seal the cut)" );
        sw::float3    centroid{};
        const float32 volume = sw::MeshFractureUtil::computeVolume( asset.getPieceVertices( piece ), centroid );
        SW_EXPECT_NEAR_EQUAL( asset._graph._listNode[piece]._volume, volume, 1e-5f );
        // 껍질 점은 조각 정점이다(무게 중심 기준).
        const sw::vector_reference<const sw::float3> listHull = asset.getPieceHull( piece );
        SW_EXPECT_TRUE( listHull.size() >= 4 && listHull.size() <= 48 );
        for ( const sw::float3& hullPoint : listHull )
        {
            bool bFound = false;
            for ( const sw::RHIVertex& vertex : asset.getPieceVertices( piece ) )
            {
                const sw::float3 position{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] };
                bFound = bFound || ( position - ( hullPoint + centroid ) ).getLength() < 1e-4f;
            }
            SW_EXPECT_TRUE( bFound );
        }
    }
}

/**
 * @brief [FractureTest] 같은 씨앗이면 바이트까지 같고, 씨앗이 다르면 다르다(네트워크는 씨앗만 보낸다)
 */
SW_TEST_CASE( FractureTest, SameSeedGivesTheSameBytes )
{
    sw::FractureSettings settings;
    settings._pieceCount = 12;
    settings._seed       = 99;
    sw::FractureAsset first;
    sw::FractureAsset second;
    sw::FractureAsset other;
    SW_ASSERT_TRUE( TestFractureInternal::fractureBox( settings, first ) );
    SW_ASSERT_TRUE( TestFractureInternal::fractureBox( settings, second ) );
    settings._seed = 100;
    SW_ASSERT_TRUE( TestFractureInternal::fractureBox( settings, other ) );
    sw::vector<uint8> firstBytes;
    sw::vector<uint8> secondBytes;
    sw::vector<uint8> otherBytes;
    first.makeBytes( firstBytes );
    second.makeBytes( secondBytes );
    other.makeBytes( otherBytes );
    SW_EXPECT_TRUE( firstBytes == secondBytes );
    SW_EXPECT_FALSE( firstBytes == otherBytes );
}

/**
 * @brief [FractureTest] 평면 조각(흔들림 0)은 축을 고르게 나누고, 이웃끼리만 단면 넓이로 이어진다
 */
SW_TEST_CASE( FractureTest, SlicesCutEvenSlabsLinkedByTheirCrossSection )
{
    sw::FractureSettings settings;
    settings._pattern          = sw::FracturePattern::Slices;
    settings._arrSliceCount[0] = 4;
    settings._arrSliceCount[1] = 1;
    settings._arrSliceCount[2] = 1;
    settings._sliceJitter      = 0.0f;
    sw::FractureAsset asset;
    SW_ASSERT_TRUE( TestFractureInternal::fractureBox( settings, asset ) );
    SW_ASSERT_EQUAL( 4u, asset.getPieceCount() );
    for ( uint32 piece = 0; piece < 4; ++piece )
    {
        SW_EXPECT_NEAR_EQUAL( 0.25f, asset._graph._listNode[piece]._volume, 1e-4f );
        SW_EXPECT_NEAR_EQUAL( 0.5f, asset._listPiece[piece]._boundsMax._x - asset._listPiece[piece]._boundsMin._x, 1e-4f );
    }
    // 단면 1 × 0.5 = 0.5, 이웃 셋.
    SW_ASSERT_EQUAL( size_t( 3 ), asset._graph._listLink.size() );
    for ( const sw::FractureLink& link : asset._graph._listLink )
        SW_EXPECT_NEAR_EQUAL( 0.5f, link._area, 1e-4f );
}

/**
 * @brief [FractureTest] 맞은 자리 둘레로 몰면 그 둘레 조각이 나머지보다 작다
 */
SW_TEST_CASE( FractureTest, ClusteredPatternMakesSmallerPiecesNearTheImpact )
{
    sw::FractureSettings settings;
    settings._pattern         = sw::FracturePattern::Clustered;
    settings._pieceCount      = 30;
    settings._impactPoint     = sw::float3{ 0.8f, 0.0f, 0.0f };
    settings._clusterRadius   = 0.25f;
    settings._clusterFraction = 0.6f;
    sw::FractureAsset asset;
    SW_ASSERT_TRUE( TestFractureInternal::fractureBox( settings, asset ) );
    float32 nearSum   = 0.0f;
    float32 farSum    = 0.0f;
    uint32  nearCount = 0;
    uint32  farCount  = 0;
    for ( uint32 piece = 0; piece < asset.getPieceCount(); ++piece )
    {
        const sw::FractureNode& node = asset._graph._listNode[piece];
        if ( ( node._centroid - settings._impactPoint ).getLength() < settings._clusterRadius )
        {
            nearSum += node._volume;
            ++nearCount;
        }
        else
        {
            farSum += node._volume;
            ++farCount;
        }
    }
    SW_ASSERT_TRUE( nearCount > 0 && farCount > 0 );
    SW_EXPECT_TRUE( nearSum / static_cast<float32>( nearCount ) * 3.0f < farSum / static_cast<float32>( farCount ) );
}

/**
 * @brief [FractureTest] 묶음 레벨은 뿌리 → 묶음 → 잎으로 이어지고, 노드마다 잎 구간이 이어지며, 묶음 하나는 연결로 이어진 덩어리다
 */
SW_TEST_CASE( FractureTest, ClusterLevelsNestAndStayConnected )
{
    sw::FractureSettings settings;
    settings._pieceCount     = 40;
    settings._seed           = 3;
    settings._listLevelCount = { 3, 10 };
    sw::FractureAsset asset;
    SW_ASSERT_TRUE( TestFractureInternal::fractureBox( settings, asset ) );
    const sw::FractureGraph& graph = asset._graph;
    sw::string               error;
    SW_EXPECT_TRUE_MSG( graph.isValid( &error ), error.c_str() );
    SW_EXPECT_EQUAL( 4u, graph.getDepthCount() );
    SW_EXPECT_TRUE( graph.getChildren( graph.getRootNode() ).size() >= 3 );
    // 묶음 안의 잎은 묶음 안 연결만으로 모두 이어진다.
    for ( uint32 nodeIndex = graph._leafCount; nodeIndex < static_cast<uint32>( graph._listNode.size() ); ++nodeIndex )
    {
        const sw::FractureNode& node = graph._listNode[nodeIndex];
        sw::vector<uint32>      listReached{ node._firstLeaf };
        sw::vector<uint8>       listSeen( graph._leafCount, 0 );
        listSeen[node._firstLeaf] = 1;
        for ( size_t cursor = 0; cursor < listReached.size(); ++cursor )
        {
            for ( const sw::FractureLink& link : graph._listLink )
            {
                const bool bInside = node._firstLeaf <= link._leafA && link._leafA < node._firstLeaf + node._leafCount && node._firstLeaf <= link._leafB &&
                                     link._leafB < node._firstLeaf + node._leafCount;
                if ( bInside == false )
                    continue;
                const uint32 next = link._leafA == listReached[cursor] ? link._leafB : ( link._leafB == listReached[cursor] ? link._leafA : 0xFFFFFFFFu );
                if ( next != 0xFFFFFFFFu && listSeen[next] == 0 )
                {
                    listSeen[next] = 1;
                    listReached.push_back( next );
                }
            }
        }
        SW_EXPECT_EQUAL( node._leafCount, static_cast<uint32>( listReached.size() ) );
    }
}

/**
 * @brief [FractureTest] 오목한 메시(L 자 기둥)도 조각이 닫히고 부피가 보존된다 — 오목한 단면을 귀 자르기로 막는다
 */
SW_TEST_CASE( FractureTest, ConcaveMeshPiecesStayClosed )
{
    const sw::vector<sw::RHIVertex> listPrism = test::DestructionTestUtil::makeLPrism( 0.5f );
    SW_ASSERT_TRUE( sw::MeshFractureUtil::isClosedMesh( listPrism ) );
    sw::FractureSettings settings;
    settings._pieceCount = 10;
    settings._seed       = 11;
    sw::FractureAsset asset;
    sw::string        error;
    SW_ASSERT_TRUE_MSG( sw::MeshFractureUtil::fracture( listPrism, settings, asset, error ), error.c_str() );
    SW_EXPECT_NEAR_EQUAL( 1.5f, TestFractureInternal::sumPieceVolumes( asset ), 2e-3f );
    for ( uint32 piece = 0; piece < asset.getPieceCount(); ++piece )
        SW_EXPECT_TRUE( sw::MeshFractureUtil::isClosedMesh( asset.getPieceVertices( piece ) ) );
}

/**
 * @brief [FractureTest] 열린 메시는 쪼개지 않고 까닭을 알린다
 */
SW_TEST_CASE( FractureTest, OpenMeshIsRejected )
{
    sw::vector<sw::RHIVertex> listBox = test::DestructionTestUtil::makeBox( sw::float3{ 0.5f, 0.5f, 0.5f } );
    listBox.resize( listBox.size() - 6 ); // 한 면을 뺀다
    sw::FractureAsset asset;
    sw::string        error;
    SW_EXPECT_FALSE( sw::MeshFractureUtil::fracture( listBox, sw::FractureSettings{}, asset, error ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_EQUAL( 0u, asset.getPieceCount() );
}

/**
 * @brief [FractureTest] `.fracture` 바이트는 왕복해도 같고, 잘린 파일 · 다른 매직은 거절한다
 */
SW_TEST_CASE( FractureTest, AssetBytesRoundTripAndRejectTruncation )
{
    sw::FractureSettings settings;
    settings._pieceCount     = 8;
    settings._listLevelCount = { 2 };
    sw::FractureAsset asset;
    SW_ASSERT_TRUE( TestFractureInternal::fractureBox( settings, asset ) );
    sw::vector<uint8> bytes;
    asset.makeBytes( bytes );
    sw::FractureAsset loaded;
    SW_ASSERT_TRUE( loaded.readFromBytes( bytes.data(), bytes.size() ) );
    sw::vector<uint8> again;
    loaded.makeBytes( again );
    SW_EXPECT_TRUE( bytes == again );

    test::ScopedDefensiveTestLog expected( "a truncated or foreign fracture file is rejected with an error" );
    SW_EXPECT_FALSE( loaded.readFromBytes( bytes.data(), bytes.size() - 5 ) );
    SW_EXPECT_EQUAL( 0u, loaded.getPieceCount() );
    bytes[0] = 'X';
    SW_EXPECT_FALSE( loaded.readFromBytes( bytes.data(), bytes.size() ) );
    SW_EXPECT_EQUAL( sw::string( "a/b.fracture" ), sw::FractureAsset::makePathForMesh( "a/b.mesh" ) );
}

/**
 * @brief [FractureTest] 씨앗 여덟 개 × 서른 조각이 모두 정확히 닫힌다 — 칸 꼭짓점 둘레의 아주 가까운 두 점을 용접하지 않으면 한 모서리에 면 넷이 붙는다
 */
SW_TEST_CASE( FractureTest, ManySeedsGiveOnlyClosedPieces )
{
    uint32 openCount  = 0;
    uint32 pieceCount = 0;
    for ( uint64 seed = 1; seed <= 8; ++seed )
    {
        sw::FractureSettings settings;
        settings._pieceCount = 30;
        settings._seed       = seed;
        sw::FractureAsset asset;
        SW_ASSERT_TRUE( TestFractureInternal::fractureBox( settings, asset ) );
        for ( uint32 piece = 0; piece < asset.getPieceCount(); ++piece )
        {
            ++pieceCount;
            openCount += sw::MeshFractureUtil::isClosedMesh( asset.getPieceVertices( piece ) ) ? 0u : 1u;
        }
    }
    SW_EXPECT_EQUAL( 240u, pieceCount );
    SW_EXPECT_EQUAL( 0u, openCount );
}
