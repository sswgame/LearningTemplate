#include "pch.h"

#include "Engine/Destruction/DestructionProfile.h"
#include "Engine/Destruction/DestructionState.h"
#include "Engine/Destruction/FractureGraph.h"

#include "TestFramework/TestFramework.h"

// DestructionStateTest — 묶음이 갈라져도 연결이 그대로면 한 덩어리, 앵커와 끊긴 덩어리는 떨어지고, 붙은 덩어리는 무게로 무너진다(레드 팩션 지지).

namespace
{
    struct TestDestructionStateInternal
    {
        /**
         * @brief 가로 @p columnCount × 세로 @p rowCount 잎 격자(부피 1, 이웃끼리 넓이 1 로 연결)입니다. 잎 번호는 행 우선(아래 행부터).
         * @param listLevelCount 묶음 레벨(비면 뿌리 하나).
         */
        static sw::FractureGraph makeGrid( uint32 columnCount, uint32 rowCount, sw::vector<uint32> listLevelCount = {} )
        {
            sw::FractureGraph graph;
            graph._leafCount = columnCount * rowCount;
            for ( uint32 row = 0; row < rowCount; ++row )
            {
                for ( uint32 column = 0; column < columnCount; ++column )
                {
                    sw::FractureNode node;
                    node._centroid = sw::float3{ static_cast<float32>( column ) + 0.5f, static_cast<float32>( row ) + 0.5f, 0.0f };
                    node._volume   = 1.0f;
                    graph._listNode.push_back( node );
                    const uint32 leaf = row * columnCount + column;
                    if ( column > 0 )
                        graph._listLink.push_back( sw::FractureLink{ leaf - 1, leaf, 1.0f } );
                    if ( row > 0 )
                        graph._listLink.push_back( sw::FractureLink{ leaf - columnCount, leaf, 1.0f } );
                }
            }
            sw::FractureGraphUtil::normalizeLinks( graph._listLink );
            sw::vector<uint32> listOrder;
            sw::FractureGraphUtil::populateHierarchy( graph, listLevelCount, listOrder );
            return graph;
        }

        /** @brief 무게 중심의 Y 가 1 보다 낮은 잎(맨 아래 행)을 앵커로 둡니다. */
        static sw::vector<uint8> makeBottomAnchors( const sw::FractureGraph& graph )
        {
            sw::vector<uint8> listAnchor( graph._leafCount, 0 );
            for ( uint32 leaf = 0; leaf < graph._leafCount; ++leaf )
            {
                listAnchor[leaf] = graph._listNode[leaf]._centroid._y < 1.0f ? 1 : 0;
            }
            return listAnchor;
        }

        static uint32 findLeafAt( const sw::FractureGraph& graph, float32 x, float32 y )
        {
            for ( uint32 leaf = 0; leaf < graph._leafCount; ++leaf )
            {
                const sw::float3& center = graph._listNode[leaf]._centroid;
                if ( center._x == x && center._y == y )
                    return leaf;
            }
            return 0xFFFFFFFFu;
        }

        static uint32 findLink( const sw::FractureGraph& graph, uint32 leafA, uint32 leafB )
        {
            for ( uint32 link = 0; link < static_cast<uint32>( graph._listLink.size() ); ++link )
            {
                const sw::FractureLink& data = graph._listLink[link];
                if ( ( data._leafA == leafA && data._leafB == leafB ) || ( data._leafA == leafB && data._leafB == leafA ) )
                    return link;
            }
            return 0xFFFFFFFFu;
        }

        static uint32 countAnchoredGroups( const sw::DestructionState& state )
        {
            uint32 count = 0;
            for ( const sw::DestructionGroup& group : state.getGroups() )
            {
                count += group._bAnchored == SW_TRUE ? 1u : 0u;
            }
            return count;
        }
    };
} // namespace

/**
 * @brief [DestructionStateTest] 기둥 가운데 연결을 끊으면 위 덩어리가 앵커와 떨어져 동적 그룹이 되고, 아래는 붙은 채 남는다
 */
SW_TEST_CASE( DestructionStateTest, PartCutFromTheAnchorsFalls )
{
    using Internal                  = TestDestructionStateInternal;
    const sw::FractureGraph graph   = Internal::makeGrid( 1, 4 );
    const sw::vector<uint8> anchors = Internal::makeBottomAnchors( graph );
    sw::DestructionProfile  profile;
    profile._supportStrength = 1.0e9f;
    sw::DestructionState state;
    state.initialize( graph, profile, anchors );
    SW_ASSERT_EQUAL( size_t( 1 ), state.getGroups().size() );
    SW_EXPECT_TRUE( state.getGroups()[0]._bAnchored == SW_TRUE );

    const uint32 lower = Internal::findLeafAt( graph, 0.5f, 1.5f );
    const uint32 upper = Internal::findLeafAt( graph, 0.5f, 2.5f );
    // 뿌리가 갈라지지 않았으면 그 안의 연결은 끊을 수 없다(한 몸).
    sw::DestructionChange change;
    SW_EXPECT_FALSE( state.breakLink( Internal::findLink( graph, lower, upper ), change ) );
    SW_ASSERT_TRUE( state.breakNode( graph.getRootNode(), change ) );
    SW_EXPECT_FALSE( change.hasGroupChange() ); // 갈라졌을 뿐 떨어진 것은 없다 — 번호도 그대로
    SW_EXPECT_EQUAL( 1u, state.getGroups()[0]._id );

    change.clear();
    SW_ASSERT_TRUE( state.breakLink( Internal::findLink( graph, lower, upper ), change ) );
    SW_EXPECT_TRUE( change.hasGroupChange() );
    SW_EXPECT_EQUAL( size_t( 1 ), change._listRemovedGroup.size() );
    SW_EXPECT_EQUAL( size_t( 2 ), change._listCreatedGroup.size() );
    SW_ASSERT_EQUAL( size_t( 2 ), state.getGroups().size() );
    const sw::DestructionGroup* pLower = state.findGroup( state.getGroupOfLeaf( lower ) );
    const sw::DestructionGroup* pUpper = state.findGroup( state.getGroupOfLeaf( upper ) );
    SW_ASSERT_NOT_NULL( pLower );
    SW_ASSERT_NOT_NULL( pUpper );
    SW_EXPECT_TRUE( pLower->_bAnchored == SW_TRUE );
    SW_EXPECT_TRUE( pUpper->_bAnchored == SW_FALSE );
    SW_EXPECT_EQUAL( 2u, pUpper->_leafCount );
    SW_EXPECT_EQUAL( 1u, pUpper->_parentID );
}

/**
 * @brief [DestructionStateTest] 묶음 레벨은 차례로 갈라진다 — 뿌리를 가르면 묶음이, 묶음을 가르면 잎이 활성이 되고, 잎을 떼면 그 잎만 떨어진다
 */
SW_TEST_CASE( DestructionStateTest, ClusterLevelsOpenProgressivelyAndLeafDetaches )
{
    using Internal                = TestDestructionStateInternal;
    const sw::FractureGraph graph = Internal::makeGrid( 4, 2, sw::vector<uint32>{ 2 } );
    sw::DestructionProfile  profile;
    profile._supportStrength = 1.0e9f;
    sw::DestructionState state;
    state.initialize( graph, profile, Internal::makeBottomAnchors( graph ) );
    SW_ASSERT_EQUAL( 3u, graph.getDepthCount() );

    sw::DestructionChange change;
    SW_ASSERT_TRUE( state.breakNode( graph.getRootNode(), change ) );
    SW_EXPECT_EQUAL( size_t( 2 ), state.getGroups()[0]._listNode.size() ); // 묶음 둘
    SW_EXPECT_FALSE( state.breakNode( graph.getRootNode(), change ) );     // 이미 갈라졌다
    const uint32 cluster = state.getActiveNodeOfLeaf( 0 );
    SW_EXPECT_TRUE( cluster >= graph._leafCount );

    // 위 행 오른쪽 끝 잎을 뗀다 — 그 묶음이 갈라지고 그 잎만 떨어진다(나머지는 연결로 앵커에 붙어 있다).
    const uint32 corner = Internal::findLeafAt( graph, 3.5f, 1.5f );
    change.clear();
    SW_ASSERT_TRUE( state.detachLeaf( corner, change ) );
    SW_EXPECT_EQUAL( corner, state.getActiveNodeOfLeaf( corner ) );
    SW_EXPECT_EQUAL( size_t( 2 ), state.getGroups().size() );
    const sw::DestructionGroup* pCorner = state.findGroup( state.getGroupOfLeaf( corner ) );
    SW_ASSERT_NOT_NULL( pCorner );
    SW_EXPECT_EQUAL( 1u, pCorner->_leafCount );
    SW_EXPECT_TRUE( pCorner->_bAnchored == SW_FALSE );
    SW_EXPECT_EQUAL( 1u, Internal::countAnchoredGroups( state ) );
}

/**
 * @brief [DestructionStateTest] 두 기둥 벽에서 한쪽 받침을 끊으면 위 무게가 남은 받침 하나로 몰려 지지 세기를 넘고 무너진다. 세기가 넉넉하면 버틴다
 */
SW_TEST_CASE( DestructionStateTest, OverloadedSupportCollapses )
{
    using Internal                  = TestDestructionStateInternal;
    const sw::FractureGraph graph   = Internal::makeGrid( 2, 4 );
    const sw::vector<uint8> anchors = Internal::makeBottomAnchors( graph );
    const uint32            leftLow = Internal::findLeafAt( graph, 0.5f, 0.5f );
    const uint32            leftUp  = Internal::findLeafAt( graph, 0.5f, 1.5f );
    // 잎 하나 100 kg → 981 N. 위 세 행(잎 여섯) = 5886 N 을 받침 둘이 나누면 2943 N 씩.
    sw::DestructionProfile profile;
    profile._density = 100.0f;

    profile._supportStrength = 4000.0f;
    sw::DestructionState weak;
    weak.initialize( graph, profile, anchors );
    sw::DestructionChange change;
    SW_ASSERT_TRUE( weak.breakNode( graph.getRootNode(), change ) );
    SW_EXPECT_EQUAL( 0u, change._overloadedLinkCount ); // 받침 둘이면 버틴다
    change.clear();
    SW_ASSERT_TRUE( weak.breakLink( Internal::findLink( graph, leftLow, leftUp ), change ) );
    SW_EXPECT_TRUE( change._overloadedLinkCount > 0 );
    SW_EXPECT_EQUAL( 1u, Internal::countAnchoredGroups( weak ) );
    const sw::DestructionGroup* pTop = weak.findGroup( weak.getGroupOfLeaf( Internal::findLeafAt( graph, 1.5f, 3.5f ) ) );
    SW_ASSERT_NOT_NULL( pTop );
    SW_EXPECT_TRUE( pTop->_bAnchored == SW_FALSE );
    SW_EXPECT_EQUAL( 6u, pTop->_leafCount );

    profile._supportStrength = 10000.0f;
    sw::DestructionState strong;
    strong.initialize( graph, profile, anchors );
    change.clear();
    SW_ASSERT_TRUE( strong.breakNode( graph.getRootNode(), change ) );
    SW_ASSERT_TRUE( strong.breakLink( Internal::findLink( graph, leftLow, leftUp ), change ) );
    SW_EXPECT_EQUAL( 0u, change._overloadedLinkCount );
    SW_EXPECT_EQUAL( size_t( 1 ), strong.getGroups().size() );
    SW_EXPECT_TRUE( strong.getLinkLoad( Internal::findLink( graph, Internal::findLeafAt( graph, 1.5f, 0.5f ), Internal::findLeafAt( graph, 1.5f, 1.5f ) ) ) > 5000.0f );
}

/**
 * @brief [DestructionStateTest] 앵커가 없으면 통째로 동적이고, 갈라진 덩어리도 모두 동적이다. 같은 조작이면 상태 해시가 같고 다시 시작하면 처음 해시다
 */
SW_TEST_CASE( DestructionStateTest, FreeObjectSplitsIntoFreeGroupsDeterministically )
{
    using Internal                = TestDestructionStateInternal;
    const sw::FractureGraph graph = Internal::makeGrid( 3, 1 );
    sw::DestructionState    first;
    sw::DestructionState    second;
    first.initialize( graph, sw::DestructionProfile{}, {} );
    second.initialize( graph, sw::DestructionProfile{}, {} );
    const uint64 initialHash = first.computeStateHash();
    SW_EXPECT_TRUE( first.getGroups()[0]._bAnchored == SW_FALSE );
    sw::DestructionChange change;
    SW_ASSERT_TRUE( first.detachLeaf( 1, change ) );
    SW_ASSERT_TRUE( second.detachLeaf( 1, change ) );
    SW_EXPECT_EQUAL( size_t( 3 ), first.getGroups().size() );
    SW_EXPECT_EQUAL( 0u, Internal::countAnchoredGroups( first ) );
    SW_EXPECT_EQUAL( first.computeStateHash(), second.computeStateHash() );
    SW_EXPECT_NOT_EQUAL( initialHash, first.computeStateHash() );
    first.reset();
    SW_EXPECT_EQUAL( initialHash, first.computeStateHash() );
    sw::float3    center{};
    const float32 mass = first.computeGroupMass( first.getGroups()[0], center );
    SW_EXPECT_NEAR_EQUAL( 3.0f * sw::DestructionProfile{}._density, mass, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.5f, center._x, 1e-5f );
}

/**
 * @brief [DestructionStateTest] 파괴 재질 표는 모르는 원소 · 속성 · 틀린 수를 오류로 거부하고, 엔진 기본 표는 읽힌다
 */
SW_TEST_CASE( DestructionStateTest, ProfileRejectsUnknownNamesAndReadsTheDefault )
{
    sw::DestructionProfile profile;
    SW_EXPECT_TRUE( profile.loadFromXMLText( R"(<DestructionProfile density="500"><Strain thresholds="10 20"/><Links strength="5" supportStrength="7"/></DestructionProfile>)",
                                             "inline" ) );
    SW_EXPECT_NEAR_EQUAL( 500.0f, profile._density, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, profile.getStrainThreshold( 1 ), 0.0f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, profile.getStrainThreshold( 5 ), 0.0f );
    SW_EXPECT_NEAR_EQUAL( 7.0f, profile._supportStrength, 0.0f );
    {
        test::ScopedDefensiveTestLog expected( "unknown names and bad numbers are load errors" );
        SW_EXPECT_FALSE( profile.loadFromXMLText( R"(<DestructionProfile><Strain thresholds="10"/><Shatter/></DestructionProfile>)", "unknown-element" ) );
        SW_EXPECT_FALSE( profile.loadFromXMLText( R"(<DestructionProfile><Links strenght="5"/></DestructionProfile>)", "unknown-attribute" ) );
        SW_EXPECT_FALSE( profile.loadFromXMLText( R"(<DestructionProfile><Strain thresholds="10 -3"/></DestructionProfile>)", "negative" ) );
        SW_EXPECT_FALSE( profile.loadFromXMLText( R"(<DestructionProfile><Links strength="1"/><Links strength="2"/></DestructionProfile>)", "twice" ) );
    }
    SW_EXPECT_TRUE( profile.loadFromResource( sw::DestructionProfile::kDefaultPath ) );
    SW_EXPECT_TRUE( profile._listStrainThreshold.size() >= 2 );
}
