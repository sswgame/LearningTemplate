#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Destruction/DestructionDamage.h"
#include "Engine/Destruction/DestructionProfile.h"
#include "Engine/Destruction/Fracture/FractureGraph.h"

using namespace sw;

namespace
{
    /** @brief x 축에 놓인 잎들입니다(묶음 없음). */
    struct DestructionDataTestUtil
    {
        static FractureGraph makeLeaves( uint32 leafCount )
        {
            FractureGraph graph;
            graph._leafCount = leafCount;
            for ( uint32 leaf = 0; leaf < leafCount; ++leaf )
            {
                FractureNode node;
                node._centroid  = float3{ static_cast<float32>( leaf ), 0.0f, 0.0f };
                node._volume    = 1.0f;
                node._firstLeaf = leaf;
                node._leafCount = 1;
                graph._listNode.push_back( node );
            }
            return graph;
        }

        static vector<DestructionLeafStrain> computeStrain( const FractureGraph& graph, const DestructionDamageEvent& event, vector_reference<const uint32> listLeafGroup = {} )
        {
            vector<DestructionLeafStrain> listStrain;
            DestructionDamageEvent::computeLeafStrain( graph, event, listLeafGroup, listStrain );
            return listStrain;
        }
    };
} // namespace

// 반경이 0 이면 가장 가까운 잎 하나, 힌트가 있으면 그 잎이 맞는다.
SW_TEST_CASE( DestructionDamageTest, ZeroRadiusHitsOneLeaf )
{
    using Util                   = DestructionDataTestUtil;
    const FractureGraph    graph = Util::makeLeaves( 4 );
    DestructionDamageEvent event;
    event._strain   = 5.0f;
    event._position = float3{ 2.2f, 0.0f, 0.0f };

    vector<DestructionLeafStrain> listStrain = Util::computeStrain( graph, event );
    SW_REQUIRE_EQUAL( listStrain.size(), size_t{ 1 } );
    SW_EXPECT_EQUAL( listStrain[0]._leaf, 2u );
    SW_EXPECT_NEAR( listStrain[0]._strain, 5.0f, 1.0e-6f );

    event._leafHint = 0;
    listStrain      = Util::computeStrain( graph, event );
    SW_REQUIRE_EQUAL( listStrain.size(), size_t{ 1 } );
    SW_EXPECT_EQUAL( listStrain[0]._leaf, 0u );

    event._leafHint = 99; // 범위 밖 힌트는 없는 것과 같다
    listStrain      = Util::computeStrain( graph, event );
    SW_REQUIRE_EQUAL( listStrain.size(), size_t{ 1 } );
    SW_EXPECT_EQUAL( listStrain[0]._leaf, 2u );

    event._strain = 0.0f;
    SW_EXPECT_TRUE( Util::computeStrain( graph, event ).empty() );
}

// 반경 안은 거리에 따라 선형으로 줄고, 힌트 잎은 줄지 않는다. 결과는 잎 번호 순이다.
SW_TEST_CASE( DestructionDamageTest, RadiusFallsOffLinearly )
{
    using Util                   = DestructionDataTestUtil;
    const FractureGraph    graph = Util::makeLeaves( 4 );
    DestructionDamageEvent event;
    event._strain   = 8.0f;
    event._radius   = 2.0f;
    event._position = float3{ 0.0f, 0.0f, 0.0f };

    vector<DestructionLeafStrain> listStrain = Util::computeStrain( graph, event );
    SW_REQUIRE_EQUAL( listStrain.size(), size_t{ 2 } ); // 거리 2 인 잎은 0 이라 빠진다
    SW_EXPECT_EQUAL( listStrain[0]._leaf, 0u );
    SW_EXPECT_NEAR( listStrain[0]._strain, 8.0f, 1.0e-5f );
    SW_EXPECT_EQUAL( listStrain[1]._leaf, 1u );
    SW_EXPECT_NEAR( listStrain[1]._strain, 4.0f, 1.0e-5f );

    event._leafHint = 3;
    listStrain      = Util::computeStrain( graph, event );
    SW_REQUIRE_EQUAL( listStrain.size(), size_t{ 3 } );
    SW_EXPECT_EQUAL( listStrain[2]._leaf, 3u );
    SW_EXPECT_NEAR( listStrain[2]._strain, 8.0f, 1.0e-5f );
}

// 그룹을 지정한 사건은 그 그룹의 잎만 맞고, 그룹 밖 힌트는 무시한다.
SW_TEST_CASE( DestructionDamageTest, GroupFilterLimitsTheLeaves )
{
    using Util                   = DestructionDataTestUtil;
    const FractureGraph    graph = Util::makeLeaves( 4 );
    const vector<uint32>   listGroup{ 1, 1, 2, 2 };
    DestructionDamageEvent event;
    event._strain   = 8.0f;
    event._position = float3{ 0.0f, 0.0f, 0.0f };
    event._groupID  = 2;

    vector<DestructionLeafStrain> listStrain = Util::computeStrain( graph, event, listGroup );
    SW_REQUIRE_EQUAL( listStrain.size(), size_t{ 1 } );
    SW_EXPECT_EQUAL( listStrain[0]._leaf, 2u );

    event._leafHint = 0;
    listStrain      = Util::computeStrain( graph, event, listGroup );
    SW_REQUIRE_EQUAL( listStrain.size(), size_t{ 1 } );
    SW_EXPECT_EQUAL( listStrain[0]._leaf, 2u );

    event._leafHint = sw::DestructionDamageEvent::kNoLeafHint;
    event._radius   = 10.0f;
    listStrain      = Util::computeStrain( graph, event, listGroup );
    SW_REQUIRE_EQUAL( listStrain.size(), size_t{ 2 } );
    SW_EXPECT_EQUAL( listStrain[1]._leaf, 3u );

    event._groupID = 9;
    event._radius  = 0.0f;
    SW_EXPECT_TRUE( Util::computeStrain( graph, event, listGroup ).empty() );
}

// 충격량은 문턱을 넘은 만큼만 변형이 된다.
SW_TEST_CASE( DestructionDamageTest, ImpactEventUsesTheProfile )
{
    DestructionProfile profile;
    profile._minImpulse      = 10.0f;
    profile._impulseToStrain = 0.5f;
    profile._impactRadius    = 0.25f;

    DestructionDamageEvent event = DestructionDamageEvent::makeImpactEvent( profile, float3{ 1.0f, 2.0f, 3.0f }, float3{ 0.0f, 1.0f, 0.0f }, 30.0f );
    SW_EXPECT_TRUE( event._kind == DestructionDamageKind::Impact );
    SW_EXPECT_NEAR( event._strain, 10.0f, 1.0e-5f );
    SW_EXPECT_NEAR( event._radius, 0.25f, 1.0e-6f );
    SW_EXPECT_NEAR( event._position._z, 3.0f, 1.0e-6f );
    SW_EXPECT_NEAR( event._direction._y, 1.0f, 1.0e-6f );

    event = DestructionDamageEvent::makeImpactEvent( profile, float3{}, float3{}, 10.0f );
    SW_EXPECT_NEAR( event._strain, 0.0f, 1.0e-6f );
}

SW_TEST_CASE( DestructionProfileTest, ReadsEveryElementAndKeepsDefaultsForTheRest )
{
    DestructionProfile profile;
    SW_REQUIRE_TRUE( profile.loadFromXMLText( R"(<DestructionProfile density="500" physicsMaterial="Wood">
        <Strain thresholds="1 2 3"/>
        <Links strength="4" supportStrength="5"/>
        <Impact impulseToStrain="6" minImpulse="7" radius="8"/>
        <Debris lifetime="9" maxBodies="10" smallVolume="11" fadeTime="12" sleepRemoveTime="13" keepCollisionVolume="14" hullShrink="15"/>
        <Network poseRate="16"/>
    </DestructionProfile>)",
                                              "test" ) );
    SW_EXPECT_NEAR( profile._density, 500.0f, 1.0e-3f );
    SW_EXPECT_TRUE( profile._physicsMaterial == hashed_string( "Wood" ) );
    SW_EXPECT_EQUAL( profile._listStrainThreshold.size(), size_t{ 3 } );
    SW_EXPECT_NEAR( profile._linkStrength, 4.0f, 1.0e-5f );
    SW_EXPECT_NEAR( profile._supportStrength, 5.0f, 1.0e-5f );
    SW_EXPECT_NEAR( profile._impulseToStrain, 6.0f, 1.0e-5f );
    SW_EXPECT_NEAR( profile._minImpulse, 7.0f, 1.0e-5f );
    SW_EXPECT_NEAR( profile._impactRadius, 8.0f, 1.0e-5f );
    SW_EXPECT_NEAR( profile._debrisLifetime, 9.0f, 1.0e-5f );
    SW_EXPECT_EQUAL( profile._maxDebrisBody, 10u );
    SW_EXPECT_NEAR( profile._smallDebrisVolume, 11.0f, 1.0e-5f );
    SW_EXPECT_NEAR( profile._fadeTime, 12.0f, 1.0e-5f );
    SW_EXPECT_NEAR( profile._sleepRemoveTime, 13.0f, 1.0e-5f );
    SW_EXPECT_NEAR( profile._keepCollisionVolume, 14.0f, 1.0e-5f );
    SW_EXPECT_NEAR( profile._hullShrink, 15.0f, 1.0e-5f );
    SW_EXPECT_NEAR( profile._networkPoseRate, 16.0f, 1.0e-5f );

    const DestructionProfile defaults;
    SW_REQUIRE_TRUE( profile.loadFromXMLText( R"(<DestructionProfile density="3"/>)", "test" ) );
    SW_EXPECT_NEAR( profile._density, 3.0f, 1.0e-5f );
    SW_EXPECT_NEAR( profile._linkStrength, defaults._linkStrength, 1.0e-3f ); // 앞 내용이 남지 않는다
}

// 깊이가 표보다 깊으면 마지막 값이고, 표가 비면 갈라지지 않는다.
SW_TEST_CASE( DestructionProfileTest, StrainThresholdFollowsDepth )
{
    DestructionProfile profile;
    profile._listStrainThreshold = { 1.0f, 2.0f };
    SW_EXPECT_NEAR( profile.getStrainThreshold( 0 ), 1.0f, 1.0e-6f );
    SW_EXPECT_NEAR( profile.getStrainThreshold( 1 ), 2.0f, 1.0e-6f );
    SW_EXPECT_NEAR( profile.getStrainThreshold( 200 ), 2.0f, 1.0e-6f );
    profile._listStrainThreshold.clear();
    SW_EXPECT_TRUE( profile.getStrainThreshold( 0 ) >= MathUtil::kMaxFloat );
}

SW_TEST_CASE( DestructionProfileTest, RepeatedElementIsAnError )
{
    SW_EXPECT_LOG( "Destruction.ElementRepeated" );
    DestructionProfile profile;
    SW_EXPECT_FALSE( profile.loadFromXMLText( R"(<DestructionProfile density="3"><Links strength="1"/><Links strength="2"/></DestructionProfile>)", "test" ) );
    SW_EXPECT_NEAR( profile._density, DestructionProfile{}._density, 1.0e-3f ); // 오류면 값은 기본값이다
}

SW_TEST_CASE( DestructionProfileTest, BadThresholdsAreAnError )
{
    SW_EXPECT_LOG( "Destruction.ThresholdInvalid" );
    DestructionProfile profile;
    SW_EXPECT_FALSE( profile.loadFromXMLText( R"(<DestructionProfile><Strain thresholds="1 -2 nope"/></DestructionProfile>)", "test" ) );
}

SW_TEST_CASE( DestructionProfileTest, MissingThresholdsIsAnError )
{
    SW_EXPECT_LOG( "Destruction.ThresholdsMissing" );
    DestructionProfile profile;
    SW_EXPECT_FALSE( profile.loadFromXMLText( R"(<DestructionProfile><Strain/></DestructionProfile>)", "test" ) );
}

// 뒤집힌 연결은 A < B 로 돌리고, 같은 쌍은 넓이를 더하고, 자기 연결은 버린다.
SW_TEST_CASE( FractureGraphTest, NormalizeLinksSortsAndMerges )
{
    vector<FractureLink> listLink{
        {3, 1, 1.0f},
        {0, 2, 2.0f},
        {1, 3, 4.0f},
        {2, 2, 8.0f}
    };
    FractureGraph::normalizeLinks( listLink );
    SW_REQUIRE_EQUAL( listLink.size(), size_t{ 2 } );
    SW_EXPECT_EQUAL( listLink[0]._leafA, 0u );
    SW_EXPECT_EQUAL( listLink[0]._leafB, 2u );
    SW_EXPECT_EQUAL( listLink[1]._leafA, 1u );
    SW_EXPECT_EQUAL( listLink[1]._leafB, 3u );
    SW_EXPECT_NEAR( listLink[1]._area, 5.0f, 1.0e-6f );
}

// 계층을 만들면 유효한 그래프이고, 잎 번호를 바꿔도 연결 · 부피 · 위치는 같은 조각을 가리킨다.
SW_TEST_CASE( FractureGraphTest, PopulateHierarchyKeepsLeavesAndLinks )
{
    FractureGraph graph = DestructionDataTestUtil::makeLeaves( 8 );
    for ( uint32 leaf = 0; leaf < 8; ++leaf )
    {
        graph._listNode[leaf]._volume = 1.0f + static_cast<float32>( leaf );
    }
    for ( uint32 leaf = 0; leaf + 1 < 8; ++leaf )
    {
        graph._listLink.push_back( FractureLink{ leaf, leaf + 1, 1.0f + static_cast<float32>( leaf ) } );
    }
    const FractureGraph source = graph;

    vector<uint32>       listOrder;
    const vector<uint32> listLevel{ 2, 4 };
    FractureGraph::populateHierarchy( graph, listLevel, listOrder );

    SW_REQUIRE_TRUE( graph.validate() );
    SW_EXPECT_EQUAL( graph._leafCount, 8u );
    SW_EXPECT_EQUAL( graph.getDepthCount(), 4u ); // 루트 · 2 · 4 · 잎 - 줄로 이어져 묶음이 갈리지 않는다
    SW_REQUIRE_EQUAL( listOrder.size(), size_t{ 8 } );
    SW_EXPECT_NEAR( graph._listNode[graph.getRootNode()]._volume, 36.0f, 1.0e-4f );

    vector<uint8> listSeen( 8, 0 );
    for ( uint32 leaf = 0; leaf < 8; ++leaf )
    {
        ++listSeen[listOrder[leaf]];
        SW_EXPECT_NEAR( graph._listNode[leaf]._volume, source._listNode[listOrder[leaf]]._volume, 1.0e-6f );
        SW_EXPECT_NEAR( graph._listNode[leaf]._centroid._x, source._listNode[listOrder[leaf]]._centroid._x, 1.0e-6f );
    }
    for ( const uint8 seen : listSeen )
    {
        SW_EXPECT_EQUAL( seen, static_cast<uint8>( 1 ) );
    }
    SW_REQUIRE_EQUAL( graph._listLink.size(), source._listLink.size() );
    for ( const FractureLink& link : graph._listLink )
    {
        const uint32 oldA = listOrder[link._leafA];
        const uint32 oldB = listOrder[link._leafB];
        const uint32 low  = oldA < oldB ? oldA : oldB;
        SW_EXPECT_EQUAL( ( oldA < oldB ? oldB : oldA ), low + 1 );
        SW_EXPECT_NEAR( link._area, 1.0f + static_cast<float32>( low ), 1.0e-5f );
    }
}

// 연결이 없는 조각은 같은 묶음에 넣지 않는다(떨어진 것을 한 몸으로 묶지 않는다).
SW_TEST_CASE( FractureGraphTest, DisconnectedLeavesAreNotClusteredTogether )
{
    FractureGraph graph = DestructionDataTestUtil::makeLeaves( 4 );
    graph._listLink.push_back( FractureLink{ 0, 1, 1.0f } );
    graph._listLink.push_back( FractureLink{ 2, 3, 1.0f } );
    graph._listNode[2]._centroid = float3{ 0.5f, 0.0f, 0.0f }; // 가까워도 이어지지 않으면 갈린다

    vector<uint32>       listOrder;
    const vector<uint32> listLevel{ 3 };
    FractureGraph::populateHierarchy( graph, listLevel, listOrder );

    SW_REQUIRE_TRUE( graph.validate() );
    for ( uint32 node = graph._leafCount; node < graph.getRootNode(); ++node )
    {
        const FractureNode& cluster = graph._listNode[node];
        for ( uint32 leaf = cluster._firstLeaf; leaf + 1 < cluster._firstLeaf + cluster._leafCount; ++leaf )
        {
            const uint32 oldA = listOrder[leaf];
            const uint32 oldB = listOrder[leaf + 1];
            SW_EXPECT_EQUAL( oldA / 2, oldB / 2 ); // {0,1} · {2,3}
        }
    }
}

// 잎 하나뿐이면 묶음 없이 그 잎이 루트다.
SW_TEST_CASE( FractureGraphTest, SingleLeafIsItsOwnRoot )
{
    FractureGraph  graph = DestructionDataTestUtil::makeLeaves( 1 );
    vector<uint32> listOrder;
    FractureGraph::populateHierarchy( graph, {}, listOrder );
    SW_REQUIRE_TRUE( graph.validate() );
    SW_EXPECT_EQUAL( graph.getRootNode(), 0u );
    SW_EXPECT_EQUAL( graph.getDepthCount(), 1u );
    SW_EXPECT_TRUE( graph.getChildren( 0 ).empty() );
}

// 구조 검사는 틀린 곳마다 이유를 말한다.
SW_TEST_CASE( FractureGraphTest, IsValidExplainsWhatIsWrong )
{
    const auto reasonOf = []( const FractureGraph& graph )
    {
        const expected<void> valid = graph.validate();
        return valid.has_value() ? string{} : string( valid.error().message() );
    };
    SW_EXPECT_EQUAL( reasonOf( FractureGraph{} ), string( "no leaves" ) );

    FractureGraph  base = DestructionDataTestUtil::makeLeaves( 2 );
    vector<uint32> listOrder;
    FractureGraph::populateHierarchy( base, {}, listOrder );
    base._listLink.push_back( FractureLink{ 0, 1, 1.0f } );
    SW_EXPECT_TRUE( base.validate() );

    FractureGraph badLink = base;
    badLink._listLink[0]  = FractureLink{ 1, 0, 1.0f };
    SW_EXPECT_EQUAL( reasonOf( badLink ), string( "bad link" ) );

    FractureGraph badRoot               = base;
    badRoot._listNode.back()._leafCount = 1;
    SW_EXPECT_EQUAL( reasonOf( badRoot ), string( "root must hold every leaf" ) );

    FractureGraph badParent        = base;
    badParent._listNode[0]._parent = 0;
    SW_EXPECT_EQUAL( reasonOf( badParent ), string( "a parent must come after its child" ) );

    FractureGraph badChild     = base;
    badChild._listChildNode[0] = 99;
    SW_EXPECT_EQUAL( reasonOf( badChild ), string( "child does not point back at its parent" ) );

    FractureGraph badLeaf = base;
    badLeaf._leafCount    = 3;
    SW_EXPECT_EQUAL( reasonOf( badLeaf ), string( "root must hold every leaf" ) );
}

/**
 * @brief [DestructionProfileTest] 같은 경로는 공유 캐시의 같은 객체이고, 다시 읽기는 쥔 객체를 제자리에서 바꾸는지 검증(`InPlace` — 파괴 오브젝트는 다시 읽은 횟수로 상태를 다시 시작한다)
 */
SW_TEST_CASE( DestructionProfileTest, SharedCacheGivesOneObjectAndReloadsInPlace )
{
    const test::ScopedScratchDomain domain;
    const string                    id = test::ScopedScratchDomain::makeID( "concrete.destruction.xml" ).str();
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "concrete.destruction.xml" ), R"(<DestructionProfile density="3"/>)" ) );

    const shared_ptr<const DestructionProfile> pHeld  = SharedAssetCache<DestructionProfile>::acquire( id );
    const shared_ptr<const DestructionProfile> pAgain = SharedAssetCache<DestructionProfile>::acquire( id );
    SW_REQUIRE_TRUE( pHeld != nullptr );
    SW_EXPECT_TRUE( pHeld.get() == pAgain.get() );
    SW_EXPECT_NEAR( 3.0f, pHeld->_density, 0.0001f );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "concrete.destruction.xml" ), R"(<DestructionProfile density="5"/>)" ) );
    const uint32 reloadsBefore = SharedAssetCache<DestructionProfile>::getReloadCount();
    SW_REQUIRE_TRUE( SharedAssetCache<DestructionProfile>::reloadShared( id ) );
    SW_EXPECT_EQUAL( reloadsBefore + 1, SharedAssetCache<DestructionProfile>::getReloadCount() );
    SW_EXPECT_NEAR( 5.0f, pHeld->_density, 0.0001f ); // 쥔 쪽이 같은 객체에서 새 값을 본다
    SW_EXPECT_TRUE( SharedAssetCache<DestructionProfile>::acquire( id ).get() == pHeld.get() );
}
