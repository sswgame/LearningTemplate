#include "pch.h"

#include "Core/Network/Connection/NetSlotTable.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Testing/TestFramework.h"

// NetSlotTable — 주소 맵은 지금 그 주소를 가진 슬롯만 가리킨다. 풀기는 다른 슬롯이 다시 묶은 같은 주소를 지우지 않는다.
// 빈 슬롯 · 연결 수 · 연결된 슬롯 찾기는 상태(Disconnected · Connected)로 가른다. 열기 · 닫기 · 놓기가 살아 있는 목록과 빈 슬롯 목록을 맞춘다(감사 N-8).

using namespace sw;

SW_TEST_CASE( NetSlotTableTest, ResetMakesEmptySlotsAndClearsAddresses )
{
    NetSlotTable table;
    table.reset( 3 );
    SW_EXPECT_EQUAL( 3, table.getCount() );
    SW_EXPECT_TRUE( table.isValid( 2 ) );
    SW_EXPECT_FALSE( table.isValid( 3 ) );
    SW_EXPECT_FALSE( table.isValid( -1 ) );
    SW_EXPECT_TRUE( table.findFreeSlot() == 0 );

    const NetAddress address = NetAddress::makeLoopback( 5000 );
    table.bindAddress( 1, address );
    SW_EXPECT_TRUE( table.findByAddress( address ) == 1 );
    SW_EXPECT_TRUE( table.get( 1 )._address == address );

    table.reset( 2 );
    SW_EXPECT_FALSE( table.findByAddress( address ).has_value() );
    SW_EXPECT_EQUAL( 2, table.getCount() );
}

SW_TEST_CASE( NetSlotTableTest, UnbindLeavesAnAddressReboundToAnotherSlot )
{
    NetSlotTable table;
    table.reset( 2 );
    const NetAddress address = NetAddress::makeLoopback( 5000 );
    table.bindAddress( 0, address );
    table.bindAddress( 1, address ); // 같은 주소의 새 연결이 다른 슬롯을 잡았다
    table.unbindAddress( 0 );        // 옛 슬롯을 닫아도 새 슬롯의 묶음은 남는다
    SW_EXPECT_TRUE( table.findByAddress( address ) == 1 );
    table.unbindAddress( 1 );
    SW_EXPECT_FALSE( table.findByAddress( address ).has_value() );
}

SW_TEST_CASE( NetSlotTableTest, ConnectedQueriesFollowSlotState )
{
    NetSlotTable table;
    table.reset( 3 );
    table.open( 0, NetConnectionState::Connected );
    table.open( 2, NetConnectionState::Connected );
    table.get( 2 )._clientSalt = 0xF0u;
    table.get( 2 )._serverSalt = 0x0Fu;
    SW_EXPECT_TRUE( table.findFreeSlot() == 1 );
    SW_EXPECT_EQUAL( 2, table.countConnected() );
    SW_EXPECT_TRUE( table.findConnected( 1 ) == nullptr );
    SW_EXPECT_TRUE( table.findConnected( 7 ) == nullptr );
    SW_REQUIRE_TRUE( table.findConnected( 2 ) != nullptr );
    SW_EXPECT_EQUAL( static_cast<uint64>( 0xFFu ), table.findConnected( 2 )->getConnectionToken() );

    vector<int32> listConnection{ 9 };
    table.collectConnected( listConnection );
    SW_REQUIRE_EQUAL( 2, static_cast<int32>( listConnection.size() ) );
    SW_EXPECT_EQUAL( 0, listConnection[0] );
    SW_EXPECT_EQUAL( 2, listConnection[1] );

    int32 visited = 0;
    for ( const NetEndpointSlot& slot : table )
    {
        visited += slot._state == NetConnectionState::Connected ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 2, visited );

    table.close( 0, false );
    SW_EXPECT_TRUE( table.findFreeSlot() == 0 );
    SW_EXPECT_EQUAL( 1, table.countConnected() );
}

// 빈 슬롯은 가장 낮은 번호부터 나가고, 붙잡아 둔(끊김 사건을 게임 스레드가 아직 꺼내지 않은) 슬롯은 놓을 때까지 다시 나가지 않는다(감사 P0-8 · N-8).
SW_TEST_CASE( NetSlotTableTest, FreeListHandsOutLowestAndHoldsClosedSlotsUntilReleased )
{
    NetSlotTable table;
    table.reset( 4 );
    for ( int32 index = 0; index < 4; ++index )
    {
        SW_REQUIRE_TRUE( table.findFreeSlot() == index );
        table.open( index, NetConnectionState::Connected );
    }
    SW_EXPECT_FALSE( table.findFreeSlot().has_value() ); // 가득 찼다

    table.close( 2, true );
    SW_EXPECT_TRUE( table.get( 2 )._state == NetConnectionState::Disconnected );
    SW_EXPECT_TRUE( table.get( 2 )._bReleasePending == SW_TRUE );
    SW_EXPECT_FALSE( table.findFreeSlot().has_value() ); // 붙잡혀 있다
    table.close( 3, false );
    SW_EXPECT_TRUE( table.findFreeSlot() == 3 );
    table.release( 2 );
    SW_EXPECT_TRUE( table.findFreeSlot() == 2 ); // 낮은 번호가 먼저
    table.release( 2 );                          // 두 번 놓아도 한 번만 들어간다
    table.open( 2, NetConnectionState::Connected );
    SW_EXPECT_TRUE( table.findFreeSlot() == 3 );
    table.open( 3, NetConnectionState::Connecting );
    SW_EXPECT_FALSE( table.findFreeSlot().has_value() );
    SW_EXPECT_EQUAL( 4, static_cast<int32>( table.getLiveSlots().size() ) );
    SW_EXPECT_EQUAL( 3, table.countConnected() ); // Connecting 은 살아 있지만 연결된 것은 아니다

    table.reset( 2 ); // 다시 채우면 모두 빈 슬롯이다
    SW_EXPECT_TRUE( table.findFreeSlot() == 0 );
    SW_EXPECT_TRUE( table.getLiveSlots().empty() );
}

// 살아 있는 목록은 번호 순이고, 기한이 된 슬롯만 모은다. 연 슬롯과 `touch` 한 슬롯은 다음 갱신에서 바로 본다.
SW_TEST_CASE( NetSlotTableTest, LiveListCollectsOnlyDueSlots )
{
    NetSlotTable table;
    table.reset( 8 );
    table.open( 5, NetConnectionState::Connected );
    table.open( 1, NetConnectionState::Connected );
    table.open( 3, NetConnectionState::Connecting );
    const vector<NetLiveSlot>& listLive = table.getLiveSlots();
    SW_REQUIRE_EQUAL( 3, static_cast<int32>( listLive.size() ) );
    SW_EXPECT_EQUAL( 1, listLive[0]._slotIndex );
    SW_EXPECT_EQUAL( 3, listLive[1]._slotIndex );
    SW_EXPECT_EQUAL( 5, listLive[2]._slotIndex );

    vector<int32> listDue;
    table.collectDue( 0.0, listDue );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( listDue.size() ) ); // 막 연 슬롯은 모두 기한이다

    table.setDueTime( 1, 2.0 );
    table.setDueTime( 3, 1.0 );
    table.setDueTime( 5, 3.0 );
    table.collectDue( 0.5, listDue );
    SW_EXPECT_TRUE( listDue.empty() );
    table.collectDue( 2.0, listDue );
    SW_REQUIRE_EQUAL( 2, static_cast<int32>( listDue.size() ) );
    SW_EXPECT_EQUAL( 1, listDue[0] );
    SW_EXPECT_EQUAL( 3, listDue[1] );

    table.touch( 5 ); // 받은 패킷 · 넣은 메시지
    table.touch( 6 ); // 살아 있지 않은 슬롯은 그대로 둔다
    table.collectDue( 0.5, listDue );
    SW_REQUIRE_EQUAL( 1, static_cast<int32>( listDue.size() ) );
    SW_EXPECT_EQUAL( 5, listDue[0] );

    table.close( 3, false );
    table.collectDue( 10.0, listDue );
    SW_REQUIRE_EQUAL( 2, static_cast<int32>( listDue.size() ) );
    SW_EXPECT_EQUAL( 1, listDue[0] );
    SW_EXPECT_EQUAL( 5, listDue[1] );

    table.touchAll();
    table.setDueTime( 1, 4.0 );
    table.collectDue( 0.0, listDue );
    SW_REQUIRE_EQUAL( 1, static_cast<int32>( listDue.size() ) );
    SW_EXPECT_EQUAL( 5, listDue[0] );
}

SW_TEST_CASE( NetSlotTableTest, ConnectionObjectLivesOnlyWhileTheSlotIsOpen )
{
    // 연결 객체(약 75 KB)는 정원만큼 미리 잡지 않는다 — 열 때 만들고 닫을 때 놓는다(언리얼 · GameNetworkingSockets 처럼 연결마다).
    NetSlotTable table;
    table.reset( 256 );
    for ( const NetEndpointSlot& slot : table )
    {
        SW_EXPECT_TRUE( slot._connection == nullptr );
    }
    table.open( 3, NetConnectionState::Connecting );
    SW_REQUIRE_TRUE( table.get( 3 )._connection != nullptr );
    SW_EXPECT_TRUE( table.get( 4 )._connection == nullptr );
    SW_REQUIRE_TRUE( table.get( 3 )._connection->sendMessage( NetChannelType::ReliableOrdered, reinterpret_cast<const uint8*>( "ab" ), 2 ) );
    table.close( 3, true );
    SW_EXPECT_TRUE( table.get( 3 )._connection == nullptr ); // 받은 메시지 · 신뢰 창도 함께 버린다
    table.release( 3 );
    table.open( 3, NetConnectionState::Connected );
    SW_REQUIRE_TRUE( table.get( 3 )._connection != nullptr );
    SW_EXPECT_EQUAL( 0, table.get( 3 )._connection->getPendingReliableCount() ); // 새 연결은 옛 연결의 신뢰 창을 잇지 않는다
}
