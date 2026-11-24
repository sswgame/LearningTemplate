// AreaGraphTest — 방 사각형 걷기(`AreaGraph::walk`): 방 안은 그대로 걷고, 지날 수 있는 이웃으로 넘어가며, 잠긴 연결과 벽에서는 방 안에 멈춘다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/World/Land/AreaGraph.h"

using namespace sw;

namespace
{
    constexpr const utf8* kAreaWalkXML = R"(<AreaGraph><Area id="hall" x="0" y="0" w="4" h="4"/><Area id="vault" x="4" y="0" w="4" h="4"/><Area id="yard" x="0" y="4" w="4" h="4"/>
  <Link from="hall" to="vault" kind="vaultDoor" requires="vaultOpen"/><Link from="hall" to="yard" kind="Passage"/></AreaGraph>)";
} // namespace

/** @brief [AreaGraphTest] 방 안의 걸음은 그대로 걷고, 열린 이웃의 사각형에 닿으면 그 방으로 넘어간다 */
SW_TEST_CASE( AreaGraphTest, WalkEntersAnOpenNeighbour )
{
    AreaGraph graph;
    SW_REQUIRE_TRUE( graph.loadFromXMLText( kAreaWalkXML, "AreaGraphTest" ) );
    const GameFlags flags;
    const AreaWalk  inside = graph.walk( hashed_string( "hall" ), float2{ 1.0f, 1.0f }, float2{ 1.0f, 0.0f }, flags );
    SW_EXPECT_NEAR( 2.0f, inside._position._x, 0.001f );
    SW_EXPECT_NULL( inside._pEntered );
    const AreaWalk crossing = graph.walk( hashed_string( "hall" ), float2{ 1.0f, 3.5f }, float2{ 0.0f, 1.0f }, flags );
    SW_REQUIRE_NOT_NULL( crossing._pEntered );
    SW_EXPECT_TRUE( crossing._pEntered->_id == hashed_string( "yard" ) );
    SW_EXPECT_NEAR( 4.5f, crossing._position._y, 0.001f );
}

/** @brief [AreaGraphTest] 잠긴 연결은 막은 연결을 답하고 방 안에 멈추며, 깃발이 서면 지난다. 이웃이 없는 벽은 연결 없이 멈춘다 */
SW_TEST_CASE( AreaGraphTest, WalkStopsAtALockedLinkAndAtAWall )
{
    AreaGraph graph;
    SW_REQUIRE_TRUE( graph.loadFromXMLText( kAreaWalkXML, "AreaGraphTest" ) );
    GameFlags      flags;
    const AreaWalk locked = graph.walk( hashed_string( "hall" ), float2{ 3.5f, 1.0f }, float2{ 1.0f, 0.0f }, flags );
    SW_REQUIRE_NOT_NULL( locked._pBlocked );
    SW_EXPECT_TRUE( locked._pBlocked->_kind == hashed_string( "vaultDoor" ) );
    SW_EXPECT_NULL( locked._pEntered );
    SW_EXPECT_TRUE( locked._position._x < 4.0f );
    flags.setFlag( hashed_string( "vaultOpen" ) );
    const AreaWalk opened = graph.walk( hashed_string( "hall" ), float2{ 3.5f, 1.0f }, float2{ 1.0f, 0.0f }, flags );
    SW_EXPECT_NOT_NULL( opened._pEntered );
    const AreaWalk wall = graph.walk( hashed_string( "hall" ), float2{ 0.5f, 1.0f }, float2{ -1.0f, 0.0f }, flags );
    SW_EXPECT_NULL( wall._pBlocked );
    SW_EXPECT_NULL( wall._pEntered );
    SW_EXPECT_TRUE( wall._position._x > 0.0f );
}
