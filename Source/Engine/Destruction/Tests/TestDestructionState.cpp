#include "pch.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Destruction/DestructionDamage.h"
#include "Engine/Destruction/DestructionProfile.h"
#include "Engine/Destruction/DestructionState.h"
#include "Engine/Destruction/Fracture/FractureGraph.h"
#include "Engine/Physics/PhysicsSystem.h"

#include <limits>

using namespace sw;

namespace
{
    /** @brief 손으로 만든 그래프 · 테이블 · 사건입니다. 위치는 잎 번호를 x 로 씁니다. */
    struct DestructionStateTestUtil
    {
        static constexpr float32 kStrongArea = 1000.0f;

        /** @brief 잎과 그 위 루트 하나뿐인 그래프입니다(루트 깊이 0, 잎 깊이 1, 부피 1). */
        static FractureGraph makeFlatGraph( uint32 leafCount, const vector<FractureLink>& listLink )
        {
            FractureGraph graph;
            graph._leafCount = leafCount;
            graph._listLink  = listLink;
            FractureGraph::normalizeLinks( graph._listLink );
            for ( uint32 leaf = 0; leaf < leafCount; ++leaf )
            {
                FractureNode node;
                node._centroid  = float3{ static_cast<float32>( leaf ), 0.0f, 0.0f };
                node._volume    = 1.0f;
                node._parent    = static_cast<int32>( leafCount );
                node._firstLeaf = leaf;
                node._leafCount = 1;
                node._depth     = 1;
                graph._listNode.push_back( node );
                graph._listChildNode.push_back( leaf );
            }
            FractureNode root;
            root._volume     = static_cast<float32>( leafCount );
            root._childCount = leafCount;
            root._leafCount  = leafCount;
            graph._listNode.push_back( root );
            return graph;
        }

        /** @brief 잎 넷이 줄로 이어지고 묶음 둘 {0,1} · {2,3}(노드 4 · 5)과 루트(노드 6)를 둔 그래프입니다. */
        static FractureGraph makeTwoLevelGraph()
        {
            FractureGraph graph = makeFlatGraph( 4, {
                                                        {0, 1, 1.0f},
                                                        {1, 2, 1.0f},
                                                        {2, 3, 1.0f}
            } );
            graph._listNode.pop_back();
            graph._listChildNode.clear();
            for ( uint32 leaf = 0; leaf < 4; ++leaf )
            {
                graph._listNode[leaf]._parent = leaf < 2 ? 4 : 5;
                graph._listNode[leaf]._depth  = 2;
            }
            graph._listChildNode = { 0, 1, 2, 3, 4, 5 };
            for ( uint32 cluster = 0; cluster < 2; ++cluster )
            {
                FractureNode node;
                node._volume     = 2.0f;
                node._parent     = 6;
                node._firstChild = cluster * 2;
                node._childCount = 2;
                node._firstLeaf  = cluster * 2;
                node._leafCount  = 2;
                node._depth      = 1;
                graph._listNode.push_back( node );
            }
            FractureNode root;
            root._volume     = 4.0f;
            root._firstChild = 4;
            root._childCount = 2;
            root._leafCount  = 4;
            graph._listNode.push_back( root );
            return graph;
        }

        /** @brief 임계값은 깊이 모두 @p threshold, 밀도 1, 끊기지 않을 만큼 센 연결과 지지입니다. */
        static DestructionProfile makeProfile( float32 threshold = 1000.0f )
        {
            DestructionProfile profile;
            profile._listStrainThreshold = { threshold };
            profile._density             = 1.0f;
            profile._linkStrength        = 1.0e6f;
            profile._supportStrength     = 1.0e6f;
            return profile;
        }

        static DestructionDamageEvent makeHit( uint32 leaf, float32 strain )
        {
            DestructionDamageEvent event;
            event._leafHint = static_cast<int32>( leaf );
            event._strain   = strain;
            return event;
        }

        /** @brief 꾸민 스냅샷의 그룹 하나입니다. 활성 노드는 앞 노드와의 간격으로 씁니다. */
        struct CraftedGroup
        {
            uint32         _id;
            vector<uint64> _listNodeStep;
        };

        /** @brief 끊김 없음 · 변형 없음에 그룹만 정해 쓴 스냅샷입니다. 첫 값은 `writeSnapshot` 과 같은 순서입니다. */
        static vector<uint8> forgeSnapshot( const FractureGraph& graph, uint32 nextGroupID, const vector<CraftedGroup>& listGroup, float32 nodeStrain = 0.0f )
        {
            BitWriter writer;
            writer.writeUint32( FourCCUtil::make( "SWDS" ) );
            writer.writeVarUint( 1 );
            writer.writeVarUint( graph._listNode.size() );
            writer.writeVarUint( graph._listLink.size() );
            writer.writeVarUint( graph._leafCount );
            writer.writeVarUint( 0 );
            writer.writeVarUint( nextGroupID );
            for ( size_t bit = 0; bit < graph._listNode.size() + graph._listLink.size() + graph._leafCount; ++bit )
            {
                writer.writeBits( 0u, 1 );
            }
            writer.writeVarUint( nodeStrain != 0.0f ? 1u : 0u );
            if ( nodeStrain != 0.0f )
            {
                writer.writeVarUint( 0 );
                writer.writeFloat( nodeStrain );
            }
            writer.writeVarUint( 0 );
            writer.writeVarUint( listGroup.size() );
            for ( const CraftedGroup& group : listGroup )
            {
                writer.writeVarUint( group._id );
                writer.writeVarUint( 0 );
                writer.writeBits( 0u, 1 );
                writer.writeVarUint( group._listNodeStep.size() );
                for ( const uint64 step : group._listNodeStep )
                {
                    writer.writeVarUint( step );
                }
            }
            return writer.releaseBytes();
        }

        /** @brief 무거운 꼭대기 잎 하나가 길이 1 ~ @p pathCount 인 경로들로 앵커(잎 0)에 매달린 그래프입니다. 꼭대기와 잇는 연결만 약합니다. 꼭대기 번호는 마지막 잎입니다. */
        static FractureGraph makeHangingGraph( uint32 pathCount )
        {
            vector<FractureLink> listLink;
            uint32               nextLeaf = 1;
            const uint32         top      = 1 + pathCount * ( pathCount - 1 ) / 2;
            for ( uint32 length = 1; length <= pathCount; ++length )
            {
                uint32 previous = 0;
                for ( uint32 step = 0; step + 1 < length; ++step )
                {
                    listLink.push_back( FractureLink{ previous, nextLeaf, kStrongArea } );
                    previous = nextLeaf++;
                }
                listLink.push_back( FractureLink{ previous, top, 0.1f } );
            }
            FractureGraph graph = makeFlatGraph( top + 1, listLink );
            for ( uint32 leaf = 0; leaf < top; ++leaf )
            {
                graph._listNode[leaf]._volume = 1.0e-4f; // 꼭대기 무게만 보이게 한다
            }
            return graph;
        }
    };
} // namespace

// 처음에는 루트 하나가 활성인 한 그룹이고, 앵커 잎이 있으면 붙어 있다.
SW_TEST_CASE( DestructionStateTest, InitializeMakesOneGroupWithTheRootActive )
{
    using Util                = DestructionStateTestUtil;
    const FractureGraph graph = Util::makeTwoLevelGraph();
    SW_REQUIRE_TRUE( graph.validate() );

    DestructionState state;
    SW_EXPECT_FALSE( state.isInitialized() );
    state.initialize( graph, Util::makeProfile(), {} );
    SW_REQUIRE_TRUE( state.isInitialized() );
    SW_REQUIRE_EQUAL( state.getGroups().size(), size_t{ 1 } );
    const DestructionGroup& group = state.getGroups()[0];
    SW_EXPECT_EQUAL( group._id, 1u );
    SW_EXPECT_EQUAL( group._leafCount, 4u );
    SW_EXPECT_TRUE( group._bAnchored == SW_FALSE );
    SW_EXPECT_EQUAL( state.getActiveNodeOfLeaf( 3 ), 6u );
    SW_EXPECT_EQUAL( state.getGroupOfLeaf( 3 ), 1u );

    const vector<uint8> listAnchor{ 0, 0, 1 };
    state.initialize( graph, Util::makeProfile(), listAnchor );
    SW_EXPECT_TRUE( state.getGroups()[0]._bAnchored == SW_TRUE );
    SW_EXPECT_TRUE( state.isLeafAnchored( 2 ) );
    SW_EXPECT_FALSE( state.isLeafAnchored( 3 ) ); // 목록이 짧으면 나머지는 앵커가 아니다
}

// 시작 전에는 어떤 변경도 받지 않는다.
SW_TEST_CASE( DestructionStateTest, UninitializedStateRefusesEveryChange )
{
    DestructionState  state;
    DestructionChange change;
    SW_EXPECT_FALSE( state.breakNode( 0, change ) );
    SW_EXPECT_FALSE( state.breakLink( 0, change ) );
    SW_EXPECT_FALSE( state.detachLeaf( 0, change ) );
    SW_EXPECT_FALSE( state.applyDamage( DestructionDamageEvent{}, change ) );
    const uint8 byte = 0;
    SW_EXPECT_FALSE( state.readSnapshot( &byte, 1 ) );
    state.reset(); // 아무 일도 없다
    SW_EXPECT_FALSE( state.isInitialized() );
}

// 묶음을 갈라도 자식끼리 이어져 있으면 같은 덩어리다. 활성 묶음만 갈라진다.
SW_TEST_CASE( DestructionStateTest, BreakNodeOpensAnActiveClusterAndKeepsTheGroup )
{
    using Util                = DestructionStateTestUtil;
    const FractureGraph graph = Util::makeTwoLevelGraph();
    DestructionState    state;
    state.initialize( graph, Util::makeProfile(), {} );

    DestructionChange change;
    SW_EXPECT_FALSE( state.breakNode( 4, change ) ); // 아직 루트가 활성이다
    SW_EXPECT_FALSE( state.breakNode( 0, change ) ); // 잎
    SW_EXPECT_FALSE( state.breakNode( 99, change ) );
    SW_EXPECT_EQUAL( change._brokenNodeCount, 0u );

    SW_REQUIRE_TRUE( state.breakNode( 6, change ) );
    SW_EXPECT_EQUAL( change._brokenNodeCount, 1u );
    SW_EXPECT_FALSE( change.hasGroupChange() );
    SW_EXPECT_TRUE( state.isNodeBroken( 6 ) );
    SW_EXPECT_EQUAL( state.getActiveNodeOfLeaf( 1 ), 4u );
    SW_EXPECT_EQUAL( state.getActiveNodeOfLeaf( 2 ), 5u );
    SW_REQUIRE_EQUAL( state.getGroups().size(), size_t{ 1 } );
    SW_EXPECT_EQUAL( state.getGroups()[0]._id, 1u );
    SW_EXPECT_TRUE( ( state.getGroups()[0]._listNode == vector<uint32>{ 4, 5 } ) );

    SW_EXPECT_FALSE( state.breakNode( 6, change ) ); // 이미 갈라졌다
}

// 활성 노드가 같은 두 잎의 연결은 끊을 수 없다. 끊기면 두 그룹으로 나뉘고 새 번호를 받는다.
SW_TEST_CASE( DestructionStateTest, BreakLinkSplitsTheGroupWithNewIDs )
{
    using Util                = DestructionStateTestUtil;
    const FractureGraph graph = Util::makeTwoLevelGraph();
    DestructionState    state;
    const vector<uint8> listAnchor{ 1, 0, 0, 0 };
    state.initialize( graph, Util::makeProfile(), listAnchor );

    DestructionChange change;
    SW_EXPECT_FALSE( state.breakLink( 1, change ) ); // 한 몸이다
    SW_EXPECT_FALSE( state.breakLink( 99, change ) );
    SW_REQUIRE_TRUE( state.breakNode( 6, change ) );
    change.clear();

    SW_EXPECT_FALSE( state.breakLink( 0, change ) ); // 묶음 4 안이다
    SW_REQUIRE_TRUE( state.breakLink( 1, change ) );
    SW_EXPECT_EQUAL( change._brokenLinkCount, 1u );
    SW_EXPECT_EQUAL( change._overloadedLinkCount, 0u );
    SW_EXPECT_TRUE( ( change._listRemovedGroup == vector<uint32>{ 1 } ) );
    SW_EXPECT_TRUE( ( change._listCreatedGroup == vector<uint32>{ 2, 3 } ) );
    SW_EXPECT_TRUE( state.isLinkBroken( 1 ) );
    SW_EXPECT_TRUE( state.findGroup( 1 ) == nullptr );

    const DestructionGroup* pAnchored = state.findGroup( 2 );
    const DestructionGroup* pFalling  = state.findGroup( 3 );
    SW_REQUIRE_NOT_NULL( pAnchored );
    SW_REQUIRE_NOT_NULL( pFalling );
    SW_EXPECT_TRUE( pAnchored->_bAnchored == SW_TRUE );
    SW_EXPECT_TRUE( pFalling->_bAnchored == SW_FALSE );
    SW_EXPECT_EQUAL( pAnchored->_parentID, 1u );
    SW_EXPECT_EQUAL( pFalling->_leafCount, 2u );
    SW_EXPECT_EQUAL( state.getGroupOfLeaf( 0 ), 2u );
    SW_EXPECT_EQUAL( state.getGroupOfLeaf( 3 ), 3u );

    change.clear();
    SW_EXPECT_FALSE( state.breakLink( 1, change ) ); // 이미 끊겼다
}

// 잎을 떼면 위 묶음을 모두 가르고 그 잎의 연결과 앵커를 푼다. 앵커가 그 잎뿐이면 남은 덩어리도 떨어진다.
SW_TEST_CASE( DestructionStateTest, DetachLeafOpensAncestorsAndDropsItsAnchor )
{
    using Util                = DestructionStateTestUtil;
    const FractureGraph graph = Util::makeTwoLevelGraph();
    DestructionState    state;
    const vector<uint8> listAnchor{ 0, 1, 0, 0 };
    state.initialize( graph, Util::makeProfile(), listAnchor );

    DestructionChange change;
    SW_EXPECT_FALSE( state.detachLeaf( 99, change ) );
    SW_REQUIRE_TRUE( state.detachLeaf( 1, change ) );

    SW_EXPECT_EQUAL( change._brokenNodeCount, 2u ); // 루트 · 묶음 4
    SW_EXPECT_EQUAL( change._brokenLinkCount, 2u ); // 0-1 · 1-2
    SW_EXPECT_EQUAL( state.getActiveNodeOfLeaf( 1 ), 1u );
    SW_EXPECT_FALSE( state.isLeafAnchored( 1 ) );
    SW_EXPECT_EQUAL( state.getGroups().size(), size_t{ 3 } ); // {0} · {1} · {묶음 5}
    for ( const DestructionGroup& group : state.getGroups() )
    {
        SW_EXPECT_TRUE( group._bAnchored == SW_FALSE );
    }
}

// 약한 피해는 쌓이기만 하고, 임계값에 닿은 묶음이 갈라진다. 넘친 비율만 자식으로 간다.
SW_TEST_CASE( DestructionStateTest, DamageAccumulatesAndPassesTheExcessToChildren )
{
    using Util                = DestructionStateTestUtil;
    const FractureGraph graph = Util::makeTwoLevelGraph();
    DestructionState    state;
    state.initialize( graph, Util::makeProfile( 10.0f ), {} );

    DestructionChange change;
    SW_EXPECT_FALSE( state.applyDamage( Util::makeHit( 0, 5.0f ), change ) );
    SW_EXPECT_NEAR( state.getNodeStrain( 6 ), 5.0f, 1.0e-5f );
    SW_EXPECT_FALSE( state.isNodeBroken( 6 ) );
    SW_EXPECT_EQUAL( state.getEventCount(), 1u );

    SW_EXPECT_TRUE( state.applyDamage( Util::makeHit( 0, 5.0f ), change ) ); // 딱 임계값 - 갈라지되 넘친 것이 없다
    SW_EXPECT_TRUE( state.isNodeBroken( 6 ) );
    SW_EXPECT_NEAR( state.getNodeStrain( 4 ), 0.0f, 1.0e-5f );
    SW_EXPECT_FALSE( change.hasGroupChange() );

    state.reset();
    change.clear();
    SW_EXPECT_TRUE( state.applyDamage( Util::makeHit( 0, 15.0f ), change ) ); // 넘친 비율 1/3 만 내려가 묶음을 가르지 못한다
    SW_EXPECT_NEAR( state.getNodeStrain( 4 ), 5.0f, 1.0e-4f );
    SW_EXPECT_NEAR( state.getNodeStrain( 5 ), 0.0f, 1.0e-5f ); // 맞은 잎이 없는 묶음은 받지 않는다
    SW_EXPECT_FALSE( state.isNodeBroken( 4 ) );
    SW_EXPECT_EQUAL( change._brokenNodeCount, 1u );
}

// 센 한 방은 레벨을 지나 잎까지 내려가 잎을 떼어 낸다.
SW_TEST_CASE( DestructionStateTest, StrongHitCascadesDownToTheLeaf )
{
    using Util                = DestructionStateTestUtil;
    const FractureGraph graph = Util::makeTwoLevelGraph();
    DestructionState    state;
    state.initialize( graph, Util::makeProfile( 10.0f ), {} );

    DestructionChange change;
    SW_REQUIRE_TRUE( state.applyDamage( Util::makeHit( 0, 30.0f ), change ) );
    SW_EXPECT_EQUAL( change._brokenNodeCount, 3u ); // 루트 · 묶음 4 · 잎 0
    SW_EXPECT_NEAR( state.getNodeStrain( 4 ), 20.0f, 1.0e-3f );
    SW_EXPECT_NEAR( state.getNodeStrain( 0 ), 15.0f, 1.0e-3f );
    SW_EXPECT_TRUE( state.isNodeBroken( 0 ) );
    SW_EXPECT_TRUE( state.isLinkBroken( 0 ) );
    SW_EXPECT_EQUAL( change._brokenLinkCount, 1u );
    SW_EXPECT_TRUE( ( change._listRemovedGroup == vector<uint32>{ 1 } ) );
    SW_EXPECT_TRUE( ( change._listCreatedGroup == vector<uint32>{ 2, 3 } ) );
    SW_EXPECT_EQUAL( state.getGroupOfLeaf( 0 ), 2u );
    SW_EXPECT_EQUAL( state.getGroupOfLeaf( 1 ), 3u );
    SW_EXPECT_EQUAL( state.getGroupOfLeaf( 3 ), 3u );
}

// 변형이 없는 사건도 사건 수에는 든다(시드와 해시가 그 수를 쓴다). 상태는 그대로다.
SW_TEST_CASE( DestructionStateTest, EventThatTouchesNothingOnlyCounts )
{
    using Util                = DestructionStateTestUtil;
    const FractureGraph graph = Util::makeTwoLevelGraph();
    DestructionState    state;
    state.initialize( graph, Util::makeProfile( 10.0f ), {} );
    const uint64 hashBefore = state.computeStateHash();

    DestructionChange      change;
    DestructionDamageEvent event = Util::makeHit( 0, 100.0f );
    event._groupID               = 99; // 없는 그룹
    SW_EXPECT_FALSE( state.applyDamage( event, change ) );
    SW_EXPECT_FALSE( state.applyDamage( Util::makeHit( 0, 0.0f ), change ) );
    SW_EXPECT_EQUAL( state.getEventCount(), 2u );
    SW_EXPECT_EQUAL( change._brokenNodeCount, 0u );
    SW_EXPECT_FALSE( state.isNodeBroken( 6 ) );
    SW_EXPECT_NOT_EQUAL( state.computeStateHash(), hashBefore );
}

// 연결은 활성 노드가 갈린 뒤에만 변형을 쌓고, 넓이 × 세기에 닿으면 끊긴다.
SW_TEST_CASE( DestructionStateTest, LinkStrainAccumulatesOnlyBetweenActiveNodes )
{
    using Util                  = DestructionStateTestUtil;
    const FractureGraph graph   = Util::makeFlatGraph( 2, {
                                                            { 0, 1, 2.0f }
    } );
    DestructionProfile  profile = Util::makeProfile();
    profile._linkStrength       = 1.0f;
    DestructionState state;
    state.initialize( graph, profile, {} );

    DestructionChange change;
    SW_EXPECT_FALSE( state.applyDamage( Util::makeHit( 0, 3.0f ), change ) );
    SW_EXPECT_NEAR( state.getLinkStrain( 0 ), 0.0f, 1.0e-6f ); // 아직 한 몸이다

    SW_REQUIRE_TRUE( state.breakNode( 2, change ) );
    SW_EXPECT_FALSE( state.applyDamage( Util::makeHit( 0, 3.0f ), change ) );
    SW_EXPECT_NEAR( state.getLinkStrain( 0 ), 1.5f, 1.0e-5f ); // 두 잎 변형의 평균
    SW_EXPECT_FALSE( state.isLinkBroken( 0 ) );

    SW_EXPECT_TRUE( state.applyDamage( Util::makeHit( 1, 1.0f ), change ) ); // 2.0 = 넓이 2 × 세기 1
    SW_EXPECT_TRUE( state.isLinkBroken( 0 ) );
    SW_EXPECT_EQUAL( state.getGroups().size(), size_t{ 2 } );
}

// 앵커에 이어진 덩어리는 무게를 앵커 쪽으로 흘린다. 깊은 잎의 무게가 지나는 연결마다 쌓이고, 여럿이면 세기 비율로 나뉜다.
SW_TEST_CASE( DestructionStateTest, SupportCarriesWeightTowardTheAnchor )
{
    using Util            = DestructionStateTestUtil;
    const float32 gravity = PhysicsSystem::getConfiguredGravityMagnitude();
    SW_REQUIRE_TRUE( gravity > 0.0f );
    DestructionChange change;

    const FractureGraph chain = Util::makeFlatGraph( 3, {
                                                            {0, 1, 1.0f},
                                                            {1, 2, 1.0f}
    } );
    DestructionState    chainState;
    const vector<uint8> listChainAnchor{ 1, 0, 0 };
    chainState.initialize( chain, Util::makeProfile(), listChainAnchor );
    SW_REQUIRE_TRUE( chainState.breakNode( 3, change ) );
    SW_EXPECT_NEAR( chainState.getLinkLoad( 1 ), gravity, 1.0e-3f );
    SW_EXPECT_NEAR( chainState.getLinkLoad( 0 ), 2.0f * gravity, 1.0e-3f );
    SW_EXPECT_EQUAL( change._overloadedLinkCount, 0u );

    const FractureGraph shared = Util::makeFlatGraph( 3, {
                                                             {0, 2, 1.0f},
                                                             {1, 2, 3.0f}
    } );
    DestructionState    sharedState;
    const vector<uint8> listSharedAnchor{ 1, 1, 0 };
    sharedState.initialize( shared, Util::makeProfile(), listSharedAnchor );
    SW_REQUIRE_TRUE( sharedState.breakNode( 3, change ) );
    SW_EXPECT_NEAR( sharedState.getLinkLoad( 0 ), 0.25f * gravity, 1.0e-3f );
    SW_EXPECT_NEAR( sharedState.getLinkLoad( 1 ), 0.75f * gravity, 1.0e-3f );
}

// 버티는 세기를 넘으면 끊겨 떨어진다. 앵커가 없으면 무게가 흐를 곳이 없어 끊기지 않는다.
SW_TEST_CASE( DestructionStateTest, OverloadedLinkCollapsesOnlyWhenAnchored )
{
    using Util                  = DestructionStateTestUtil;
    const float32       gravity = PhysicsSystem::getConfiguredGravityMagnitude();
    const FractureGraph graph   = Util::makeFlatGraph( 2, {
                                                            { 0, 1, 1.0f }
    } );
    DestructionProfile  profile = Util::makeProfile();
    profile._supportStrength    = gravity * 0.5f;

    DestructionState  falling;
    DestructionChange change;
    falling.initialize( graph, profile, {} );
    SW_REQUIRE_TRUE( falling.breakNode( 2, change ) );
    SW_EXPECT_EQUAL( change._overloadedLinkCount, 0u );
    SW_EXPECT_EQUAL( falling.getGroups().size(), size_t{ 1 } );

    DestructionState    anchored;
    const vector<uint8> listAnchor{ 1, 0 };
    anchored.initialize( graph, profile, listAnchor );
    change.clear();
    SW_REQUIRE_TRUE( anchored.breakNode( 2, change ) );
    SW_EXPECT_EQUAL( change._overloadedLinkCount, 1u );
    SW_EXPECT_EQUAL( change._brokenLinkCount, 1u );
    SW_EXPECT_TRUE( ( change._listCreatedGroup == vector<uint32>{ 2, 3 } ) );
    SW_EXPECT_TRUE( anchored.findGroup( anchored.getGroupOfLeaf( 0 ) )->_bAnchored == SW_TRUE );
    SW_EXPECT_TRUE( anchored.findGroup( anchored.getGroupOfLeaf( 1 ) )->_bAnchored == SW_FALSE );
}

// 결함: 상한에 닿은 회차가 끊은 연결은 덩어리 나누기에 들어가지 않아, 앵커와 끊긴 잎이 붙은 그룹에 남았다.
SW_TEST_CASE( DestructionStateTest, LinkBrokenOnTheLastSupportPassStillSplitsTheGroup )
{
    using Util                  = DestructionStateTestUtil;
    const float32       gravity = PhysicsSystem::getConfiguredGravityMagnitude();
    const FractureGraph graph   = Util::makeHangingGraph( DestructionState::kMaxSupportPass );
    const uint32        top     = graph._leafCount - 1;
    SW_REQUIRE_TRUE( graph.validate() );
    DestructionProfile profile = Util::makeProfile();
    profile._supportStrength   = gravity; // 약한 연결이 버티는 하중은 꼭대기 무게의 1/10 이다
    vector<uint8> listAnchor( graph._leafCount, 0 );
    listAnchor[0] = 1;

    DestructionState state;
    state.initialize( graph, profile, listAnchor );
    DestructionChange change;
    SW_REQUIRE_TRUE( state.breakNode( graph.getRootNode(), change ) );

    SW_EXPECT_EQUAL( change._overloadedLinkCount, DestructionState::kMaxSupportPass ); // 한 회차에 경로 하나씩
    SW_REQUIRE_EQUAL( state.getGroups().size(), size_t{ 2 } );
    const DestructionGroup* pTop = state.findGroup( state.getGroupOfLeaf( top ) );
    SW_REQUIRE_NOT_NULL( pTop );
    SW_EXPECT_EQUAL( pTop->_leafCount, 1u );
    SW_EXPECT_TRUE( pTop->_bAnchored == SW_FALSE );
    SW_EXPECT_TRUE( state.findGroup( state.getGroupOfLeaf( 0 ) )->_bAnchored == SW_TRUE );
}

// 질량은 활성 노드 부피 × 밀도, 중심은 부피 가중 평균이다.
SW_TEST_CASE( DestructionStateTest, GroupMassUsesActiveNodeVolumes )
{
    using Util                   = DestructionStateTestUtil;
    FractureGraph graph          = Util::makeFlatGraph( 2, {
                                                      { 0, 1, 1.0f }
    } );
    graph._listNode[0]._volume   = 1.0f;
    graph._listNode[1]._volume   = 3.0f;
    graph._listNode[2]._volume   = 4.0f;
    graph._listNode[2]._centroid = float3{ 0.75f, 0.0f, 0.0f };
    DestructionProfile profile   = Util::makeProfile();
    profile._density             = 2.0f;
    DestructionState state;
    state.initialize( graph, profile, {} );

    float3 center{};
    SW_EXPECT_NEAR( state.computeGroupMass( state.getGroups()[0], center ), 8.0f, 1.0e-5f );
    SW_EXPECT_NEAR( center._x, 0.75f, 1.0e-5f );

    DestructionChange change;
    SW_REQUIRE_TRUE( state.detachLeaf( 1, change ) );
    const DestructionGroup* pLeafOne = state.findGroup( state.getGroupOfLeaf( 1 ) );
    SW_REQUIRE_NOT_NULL( pLeafOne );
    SW_EXPECT_NEAR( state.computeGroupMass( *pLeafOne, center ), 6.0f, 1.0e-5f );
    SW_EXPECT_NEAR( center._x, 1.0f, 1.0e-5f );
}

// 같은 사건열은 같은 해시, 다른 사건열은 다른 해시다. reset 은 처음으로 돌아가고 앵커도 되살린다.
SW_TEST_CASE( DestructionStateTest, HashFollowsTheEventsAndResetRestores )
{
    using Util                = DestructionStateTestUtil;
    const FractureGraph graph = Util::makeTwoLevelGraph();
    const vector<uint8> listAnchor{ 1, 0, 0, 0 };
    DestructionState    first;
    DestructionState    second;
    first.initialize( graph, Util::makeProfile( 10.0f ), listAnchor );
    second.initialize( graph, Util::makeProfile( 10.0f ), listAnchor );
    const uint64 hashStart = first.computeStateHash();
    SW_EXPECT_EQUAL( second.computeStateHash(), hashStart );

    DestructionChange change;
    (void)first.applyDamage( Util::makeHit( 0, 30.0f ), change );
    (void)second.applyDamage( Util::makeHit( 0, 30.0f ), change );
    SW_EXPECT_EQUAL( first.computeStateHash(), second.computeStateHash() );
    SW_EXPECT_FALSE( first.isLeafAnchored( 0 ) );

    (void)second.applyDamage( Util::makeHit( 3, 1.0f ), change );
    SW_EXPECT_NOT_EQUAL( first.computeStateHash(), second.computeStateHash() );

    first.reset();
    SW_EXPECT_EQUAL( first.computeStateHash(), hashStart );
    SW_EXPECT_TRUE( first.isLeafAnchored( 0 ) );
    SW_EXPECT_EQUAL( first.getEventCount(), 0u );
    change.clear();
    SW_REQUIRE_TRUE( first.applyDamage( Util::makeHit( 0, 30.0f ), change ) );
    SW_EXPECT_TRUE( ( change._listCreatedGroup == vector<uint32>{ 2, 3 } ) ); // 번호도 처음부터
}

// 스냅샷을 읽은 쪽은 쓴 쪽과 같은 해시이고, 쌓인 변형까지 넘어가 뒤따르는 사건도 같은 결과를 낸다.
SW_TEST_CASE( DestructionStateTest, SnapshotCarriesTheStateAndFollowingEventsMatch )
{
    using Util                = DestructionStateTestUtil;
    const FractureGraph graph = Util::makeTwoLevelGraph();
    const vector<uint8> listAnchor{ 0, 0, 0, 1 };
    DestructionState    source;
    source.initialize( graph, Util::makeProfile( 10.0f ), listAnchor );
    DestructionChange change;
    (void)source.applyDamage( Util::makeHit( 0, 30.0f ), change );
    (void)source.applyDamage( Util::makeHit( 2, 4.0f ), change );

    vector<uint8> bytes;
    source.writeSnapshot( bytes );

    DestructionState target;
    target.initialize( graph, Util::makeProfile( 10.0f ), listAnchor );
    SW_REQUIRE_TRUE( target.readSnapshot( bytes.data(), bytes.size() ) );
    SW_EXPECT_EQUAL( target.computeStateHash(), source.computeStateHash() );
    SW_EXPECT_EQUAL( target.getEventCount(), 2u );
    SW_EXPECT_EQUAL( target.getGroups().size(), source.getGroups().size() );
    for ( uint32 leaf = 0; leaf < graph._leafCount; ++leaf )
    {
        SW_EXPECT_EQUAL( target.getGroupOfLeaf( leaf ), source.getGroupOfLeaf( leaf ) );
        SW_EXPECT_EQUAL( target.getActiveNodeOfLeaf( leaf ), source.getActiveNodeOfLeaf( leaf ) );
    }
    SW_EXPECT_EQUAL( target.findGroup( source.getGroups()[0]._id )->_leafCount, source.getGroups()[0]._leafCount );

    DestructionChange sourceChange;
    DestructionChange targetChange;
    (void)source.applyDamage( Util::makeHit( 2, 7.0f ), sourceChange );
    (void)target.applyDamage( Util::makeHit( 2, 7.0f ), targetChange );
    SW_EXPECT_EQUAL( target.computeStateHash(), source.computeStateHash() );
    SW_EXPECT_TRUE( targetChange._listCreatedGroup == sourceChange._listCreatedGroup );
}

// 잘린 바이트 · 다른 그래프는 받지 않고, 실패해도 상태는 그대로다.
SW_TEST_CASE( DestructionStateTest, SnapshotRefusesTruncatedBytesAndOtherGraphs )
{
    using Util                = DestructionStateTestUtil;
    const FractureGraph graph = Util::makeTwoLevelGraph();
    DestructionState    source;
    source.initialize( graph, Util::makeProfile( 10.0f ), {} );
    DestructionChange change;
    (void)source.applyDamage( Util::makeHit( 0, 30.0f ), change );
    vector<uint8> bytes;
    source.writeSnapshot( bytes );

    DestructionState target;
    target.initialize( graph, Util::makeProfile( 10.0f ), {} );
    const uint64 hashBefore = target.computeStateHash();
    for ( size_t size = 0; size < bytes.size(); ++size )
    {
        SW_EXPECT_FALSE( target.readSnapshot( bytes.data(), size ) );
    }
    SW_EXPECT_FALSE( target.readSnapshot( nullptr, bytes.size() ) );
    SW_EXPECT_EQUAL( target.computeStateHash(), hashBefore );

    const FractureGraph other = Util::makeFlatGraph( 2, {
                                                            { 0, 1, 1.0f }
    } );
    DestructionState    otherState;
    otherState.initialize( other, Util::makeProfile(), {} );
    SW_EXPECT_FALSE( otherState.readSnapshot( bytes.data(), bytes.size() ) );

    vector<uint8> wrongMagic = bytes;
    wrongMagic[0] ^= 0xFF;
    SW_EXPECT_FALSE( target.readSnapshot( wrongMagic.data(), wrongMagic.size() ) );
}

// 결함: 스냅샷은 받은 데이터다. 그룹 · 활성 노드의 오름차순을 안 봐서, 꾸민 바이트가 이분 탐색이 기대는 순서를 깨고 들어왔다.
SW_TEST_CASE( DestructionStateTest, SnapshotRefusesGroupsThatBreakTheSortedOrder )
{
    using Util                = DestructionStateTestUtil;
    const FractureGraph graph = Util::makeTwoLevelGraph();
    DestructionState    state;
    state.initialize( graph, Util::makeProfile(), {} );
    const auto read = [&]( const vector<uint8>& bytes )
    { return state.readSnapshot( bytes.data(), bytes.size() ); };

    // 꾸미개가 쓰는 형식이 진짜 형식임을 먼저 확인한다 - 묶음 4 · 5 가 각자 그룹이다.
    SW_REQUIRE_TRUE( read( Util::forgeSnapshot( graph, 3, {
                                                              {1, { 4 }},
                                                              {2, { 5 }}
    } ) ) );
    SW_EXPECT_EQUAL( state.getGroupOfLeaf( 3 ), 2u );

    const uint64 kWrapToPrevious = std::numeric_limits<uint64>::max(); // 5 + 이 값은 4 다
    SW_EXPECT_FALSE( read( Util::forgeSnapshot( graph, 3, {
                                                              {2, { 4 }},
                                                              {1, { 5 }}
    } ) ) ); // 그룹이 내림차순
    SW_EXPECT_FALSE( read( Util::forgeSnapshot( graph, 3, {
                                                              {1, { 4 }},
                                                              {1, { 5 }}
    } ) ) ); // 같은 번호
    SW_EXPECT_FALSE( read( Util::forgeSnapshot( graph, 3, {
                                                              { 0, { 6 } }
    } ) ) ); // 번호 0
    SW_EXPECT_FALSE( read( Util::forgeSnapshot( graph, 3, {
                                                              { 3, { 6 } }
    } ) ) ); // 다음 번호 이상
    SW_EXPECT_FALSE( read( Util::forgeSnapshot( graph, 3, {
                                                              { 1, { 5, kWrapToPrevious } }
    } ) ) ); // 간격이 감겨 내림차순
    SW_EXPECT_FALSE( read( Util::forgeSnapshot( graph, 3, {
                                                              { 1, { 4 } }
    } ) ) ); // 잎을 다 덮지 않는다
    SW_EXPECT_FALSE( read( Util::forgeSnapshot( graph, 3, {
                                                              { 1, { 4, 2 } }
    } ) ) ); // 묶음 4 와 루트가 겹친다
    SW_EXPECT_FALSE( read( Util::forgeSnapshot( graph, 3, {
                                                              { 1, { 99 } }
    } ) ) ); // 없는 노드
    const float32 kNotFinite = std::numeric_limits<float32>::infinity();
    SW_EXPECT_FALSE( read( Util::forgeSnapshot( graph, 3, {
                                                              { 1, { 6 } }
    },
                                                kNotFinite ) ) ); // 변형이 유한하지 않다

    SW_EXPECT_EQUAL( state.getGroupOfLeaf( 3 ), 2u ); // 실패는 상태를 바꾸지 않는다
}
