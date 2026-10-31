#include "pch.h"

#include "Core/Network/Connection/NetSlotTable.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Testing/TestFramework.h"

// NetSlotTable — 주소 맵은 지금 그 주소를 가진 슬롯만 가리킨다. 풀기는 다른 슬롯이 다시 묶은 같은 주소를 지우지 않는다.
// 빈 슬롯 · 연결 수 · 연결된 슬롯 찾기는 상태(Disconnected · Connected)로 가른다.

using namespace sw;

SW_TEST_CASE( NetSlotTableTest, ResetMakesEmptySlotsAndClearsAddresses )
{
    NetSlotTable table;
    table.reset( 3 );
    SW_EXPECT_EQUAL( 3, table.getCount() );
    SW_EXPECT_TRUE( table.isValid( 2 ) );
    SW_EXPECT_FALSE( table.isValid( 3 ) );
    SW_EXPECT_FALSE( table.isValid( -1 ) );
    SW_EXPECT_EQUAL( 0, table.findFreeSlot() );

    const NetAddress address = NetAddress::makeLoopback( 5000 );
    table.bindAddress( 1, address );
    SW_EXPECT_EQUAL( 1, table.findByAddress( address ) );
    SW_EXPECT_TRUE( table.get( 1 )._address == address );

    table.reset( 2 );
    SW_EXPECT_EQUAL( -1, table.findByAddress( address ) );
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
    SW_EXPECT_EQUAL( 1, table.findByAddress( address ) );
    table.unbindAddress( 1 );
    SW_EXPECT_EQUAL( -1, table.findByAddress( address ) );
}

SW_TEST_CASE( NetSlotTableTest, ConnectedQueriesFollowSlotState )
{
    NetSlotTable table;
    table.reset( 3 );
    table.get( 0 )._state      = NetConnectionState::Connected;
    table.get( 2 )._state      = NetConnectionState::Connected;
    table.get( 2 )._clientSalt = 0xF0u;
    table.get( 2 )._serverSalt = 0x0Fu;
    SW_EXPECT_EQUAL( 1, table.findFreeSlot() );
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

    table.get( 0 )._state = NetConnectionState::Disconnected;
    SW_EXPECT_EQUAL( 0, table.findFreeSlot() );
}
