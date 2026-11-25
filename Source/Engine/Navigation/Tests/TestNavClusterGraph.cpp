// NavClusterGraphTest — 스트리밍 월드 먼 경로의 대략 그래프(`NavClusterGraph`): 짓기 · A* · 쿠킹본 바이트, 실제 백엔드가 지은 덩어리 · 링크 간선, 긴 링크 거절.
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Serialization/ByteStream.h"
#include "Core/TestFramework/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavClusterGraph.h"
#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"

using namespace sw;

namespace
{
    struct NavClusterGraphTestUtil
    {
        static NavCluster makeCluster( int32 tileX, int32 tileZ, const float3& center )
        {
            NavCluster cluster{};
            cluster._tileX  = tileX;
            cluster._tileZ  = tileZ;
            cluster._center = center;
            return cluster;
        }

        /** @brief 타일 (0,0) 에 둘, (1,0) · (2,0) 에 하나씩 — (0,0) 의 둘째 덩어리는 아무와도 잇지 않는다. */
        static NavClusterGraph makeLine()
        {
            vector<NavCluster> listCluster = {
                makeCluster( 0, 0, float3{ 2.0f, 0.0f, 2.0f } ),
                makeCluster( 0, 0, float3{ 2.0f, 5.0f, 2.0f } ),
                makeCluster( 1, 0, float3{ 12.0f, 0.0f, 2.0f } ),
                makeCluster( 2, 0, float3{ 22.0f, 0.0f, 2.0f } ),
            };
            const vector<NavClusterLink> listLink = {
                NavClusterLink{0, 2},
                NavClusterLink{2, 0},
                NavClusterLink{0, 2}, // 같은 이음이 여럿이어도 간선 하나다
                NavClusterLink{2, 3},
                NavClusterLink{3, 2},
            };
            NavClusterGraph graph;
            SW_EXPECT_TRUE( graph.build( std::move( listCluster ), listLink ) );
            return graph;
        }

        static NavMeshSettings makeSettings()
        {
            NavAgentTypeDef agentType;
            agentType._name = hashed_string{ "Walker" };
            NavAreaDef area;
            area._name = hashed_string{ "Walkable" };
            NavMeshSettings settings;
            settings._listAgentType.push_back( agentType );
            settings._listArea.push_back( area );
            settings._defaultAgentType = agentType._name;
            return settings;
        }

        /** @brief 윗면이 y 0.5 인 납작한 상자를 더합니다(XZ 범위 [minX, maxX] × [0, 3]). */
        static void addPad( NavMeshGeometry& geometry, float32 minX, float32 maxX )
        {
            const float32 halfX = ( maxX - minX ) * 0.5f;
            geometry.addBox( float3{ halfX, 0.5f, 1.5f }, float4x4::makeTranslation( float3{ minX + halfX, 0.0f, 1.5f } ), 0 );
        }

        static NavOffMeshLink makeLink( float32 startX, float32 endX, uint32 userID )
        {
            NavOffMeshLink link{};
            link._start  = float3{ startX, 0.5f, 1.5f };
            link._end    = float3{ endX, 0.5f, 1.5f };
            link._userID = userID;
            return link;
        }

        /** @brief 기하를 실제 백엔드로 모두 굽고 대략 그래프를 짓습니다. */
        static bool bakeGraph( NavMeshGeometry& geometry, NavClusterGraph& outGraph )
        {
            const NavMeshSettings settings = makeSettings();
            unique_ptr<INavMesh>  pBaked   = NavMeshBackend::createNavMesh();
            if ( pBaked == nullptr || pBaked->initialize( settings._listAgentType[0], settings, NavMeshAsset::computeBakeBounds( geometry ) ) == false )
                return false;
            const bool bBaked = NavMeshAsset::bakeAllTiles( *pBaked, geometry );
            pBaked->collectClusterGraph( outGraph );
            return bBaked;
        }
    };
    using Util = NavClusterGraphTestUtil;
} // namespace

/** @brief [NavClusterGraphTest] 같은 이음은 간선 하나로 줄고, A* 는 이어진 클러스터 열을 두 끝 포함으로 낸다. 이어지지 않은 덩어리로는 길이 없다 */
SW_TEST_CASE( NavClusterGraphTest, RouteFollowsLinkedClusters )
{
    const NavClusterGraph graph = Util::makeLine();
    SW_REQUIRE_EQUAL( size_t{ 4 }, graph.getClusterList().size() );
    SW_EXPECT_EQUAL( size_t{ 4 }, graph.getEdges().size() );
    SW_EXPECT_EQUAL( 1u, graph.getClusterList()[0]._edgeCount );

    vector<uint32> listRoute;
    SW_REQUIRE_TRUE( graph.findRoute( 0, 3, listRoute ) );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listRoute.size() );
    SW_EXPECT_EQUAL( 0u, listRoute[0] );
    SW_EXPECT_EQUAL( 2u, listRoute[1] );
    SW_EXPECT_EQUAL( 3u, listRoute[2] );
    SW_EXPECT_FALSE( graph.findRoute( 1, 3, listRoute ) );
    SW_EXPECT_TRUE( listRoute.empty() );

    // 한 타일의 덩어리 둘은 높이까지 재어 가까운 것을 고른다(다리 위 · 아래).
    SW_EXPECT_EQUAL( 0u, graph.findCluster( 0, 0, float3{ 3.0f, 0.5f, 2.0f } ) );
    SW_EXPECT_EQUAL( 1u, graph.findCluster( 0, 0, float3{ 3.0f, 4.5f, 2.0f } ) );
    SW_EXPECT_EQUAL( NavClusterGraph::kNoCluster, graph.findCluster( 5, 5, float3{} ) );
}

/** @brief [NavClusterGraphTest] 타일 순서가 어긋나거나 자리 밖 이음이면 짓지 않고 비운다 */
SW_TEST_CASE( NavClusterGraphTest, BadInputLeavesTheGraphEmpty )
{
    SW_EXPECT_LOG( "NavClusterGraph.ClustersOutOfOrder" );
    SW_EXPECT_LOG( "NavClusterGraph.LinkOutOfRange" );
    NavClusterGraph graph;
    SW_EXPECT_FALSE( graph.build( { Util::makeCluster( 1, 0, float3{} ), Util::makeCluster( 0, 0, float3{} ) }, {} ) );
    SW_EXPECT_TRUE( graph.isEmpty() );
    SW_EXPECT_FALSE( graph.build( {
                                      Util::makeCluster( 0, 0, float3{}
                                         )
    },
                                  { NavClusterLink{ 0, 7 } } ) );
    SW_EXPECT_TRUE( graph.isEmpty() );
}

/** @brief [NavClusterGraphTest] 쓴 바이트를 그대로 읽고, 자리 밖 간선 · 잘린 바이트는 읽지 않는다 */
SW_TEST_CASE( NavClusterGraphTest, BytesRoundTripAndRejectCorruption )
{
    const NavClusterGraph graph = Util::makeLine();
    vector<uint8>         listByte;
    ByteWriter            writer{ listByte };
    graph.write( writer );

    NavClusterGraph loaded;
    ByteReader      reader{ listByte.data(), listByte.size() };
    SW_REQUIRE_TRUE( loaded.read( reader ) );
    SW_EXPECT_TRUE( reader.isFullyRead() );
    SW_REQUIRE_EQUAL( graph.getClusterList().size(), loaded.getClusterList().size() );
    SW_REQUIRE_EQUAL( graph.getEdges().size(), loaded.getEdges().size() );
    vector<uint32> listRoute;
    SW_EXPECT_TRUE( loaded.findRoute( 0, 3, listRoute ) );

    // 마지막 간선의 도착 자리를 클러스터 밖으로 — 읽기가 거절하고 비운다.
    vector<uint8> listBad = listByte;
    const uint32  outside = 99;
    Memory::copy( listBad.data() + listBad.size() - sizeof( NavClusterEdge ), &outside, sizeof( outside ) );
    ByteReader badReader{ listBad.data(), listBad.size() };
    SW_EXPECT_FALSE( loaded.read( badReader ) );
    SW_EXPECT_TRUE( loaded.isEmpty() );

    ByteReader shortReader{ listByte.data(), listByte.size() - 1 };
    SW_EXPECT_FALSE( loaded.read( shortReader ) );
    SW_EXPECT_TRUE( loaded.isEmpty() );
}

/** @brief [NavClusterGraphTest] 실제 백엔드 — 떨어진 두 받침은 덩어리 둘이고 길이 없다. 이웃 타일로 건너는 링크를 놓으면 두 덩어리 사이 간선이 생긴다 */
SW_TEST_CASE( NavClusterGraphTest, BakedClustersFollowIslandsAndLinks )
{
    // 받침 A 는 타일 (0,0)(x [0, 3]), 받침 B 는 타일 (1,0)(x [10, 13]) — 타일 한 변 9.6 m.
    NavMeshGeometry apart;
    Util::addPad( apart, 0.0f, 3.0f );
    Util::addPad( apart, 10.0f, 13.0f );
    NavClusterGraph apartGraph;
    SW_REQUIRE_TRUE( Util::bakeGraph( apart, apartGraph ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, apartGraph.getClusterList().size() );
    vector<uint32> listRoute;
    SW_EXPECT_FALSE( apartGraph.findRoute( 0, 1, listRoute ) );

    NavMeshGeometry linked;
    Util::addPad( linked, 0.0f, 3.0f );
    Util::addPad( linked, 10.0f, 13.0f );
    linked.addOffMeshLink( Util::makeLink( 2.5f, 10.5f, 7 ) );
    NavClusterGraph linkedGraph;
    SW_REQUIRE_TRUE( Util::bakeGraph( linked, linkedGraph ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, linkedGraph.getClusterList().size() );
    SW_EXPECT_TRUE( linkedGraph.findRoute( 0, 1, listRoute ) );
    SW_EXPECT_TRUE( linkedGraph.findRoute( 1, 0, listRoute ) ); // 양방향 링크
}

/** @brief [NavClusterGraphTest] 끝이 이웃 타일 밖인 링크는 Detour 가 잇지 못한다 — 굽기가 오류를 남기고 넣지 않는다(조용히 끊긴 다리가 되지 않게) */
SW_TEST_CASE( NavClusterGraphTest, LinkPastTheNeighbourTileIsRejected )
{
    SW_EXPECT_LOG( "RecastNavMesh.OffMeshLinkSpansTiles" );
    NavMeshGeometry geometry;
    Util::addPad( geometry, 0.0f, 3.0f );
    Util::addPad( geometry, 24.0f, 27.0f ); // 타일 (2,0)
    geometry.addOffMeshLink( Util::makeLink( 2.5f, 24.5f, 9 ) );
    NavClusterGraph graph;
    SW_REQUIRE_TRUE( Util::bakeGraph( geometry, graph ) );
    vector<uint32> listRoute;
    SW_EXPECT_FALSE( graph.findRoute( 0, static_cast<uint32>( graph.getClusterList().size() - 1 ), listRoute ) );
}

/**
 * @brief [NavClusterGraphTest] 64 × 64 타일 격자(타일마다 덩어리 하나, 네 이웃과 이음)에서 맞모서리까지 A* 한 번의 시간입니다(먼 경로 계획 비용 — 판정은 통합이 한다).
 * @details 재는 법: Release `App --test --test-run-disabled --test-filter=NavClusterGraphTest.RouteCostBench` 5 번, 로그(`NavClusterGraphTest.RouteCostBench`) 값의 중앙값.
 */
SW_TEST_CASE( NavClusterGraphTest, RouteCostBench, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다" )
{
    constexpr int32        kSide     = 64;
    constexpr float32      kTileSize = 9.6f;
    constexpr uint32       kRunCount = 100;
    vector<NavCluster>     listCluster;
    vector<NavClusterLink> listLink;
    for ( int32 tileZ = 0; tileZ < kSide; ++tileZ )
    {
        for ( int32 tileX = 0; tileX < kSide; ++tileX )
        {
            const uint32 index = static_cast<uint32>( tileZ * kSide + tileX );
            listCluster.push_back( Util::makeCluster( tileX, tileZ, float3{ ( static_cast<float32>( tileX ) + 0.5f ) * kTileSize, 0.0f, ( static_cast<float32>( tileZ ) + 0.5f ) * kTileSize } ) );
            if ( tileX + 1 < kSide )
            {
                listLink.push_back( NavClusterLink{ index, index + 1 } );
                listLink.push_back( NavClusterLink{ index + 1, index } );
            }
            if ( tileZ + 1 < kSide )
            {
                listLink.push_back( NavClusterLink{ index, index + kSide } );
                listLink.push_back( NavClusterLink{ index + kSide, index } );
            }
        }
    }
    NavClusterGraph graph;
    SW_REQUIRE_TRUE( graph.build( std::move( listCluster ), listLink ) );
    vector<uint32>  listRoute;
    const Stopwatch watch;
    for ( uint32 run = 0; run < kRunCount; ++run )
    {
        SW_REQUIRE_TRUE( graph.findRoute( 0, static_cast<uint32>( kSide * kSide - 1 ), listRoute ) );
    }
    const int64 nanos = watch.getElapsedNanoseconds();
    SW_EXPECT_EQUAL( static_cast<size_t>( kSide * 2 - 1 ), listRoute.size() );
    SW_LOG_INFO_ID( "NavClusterGraphTest.RouteCostBench", "cluster route on a %# by %# tile grid, corner to corner: %# ns per route", kSide, kSide, nanos / static_cast<int64>( kRunCount ) );
}
