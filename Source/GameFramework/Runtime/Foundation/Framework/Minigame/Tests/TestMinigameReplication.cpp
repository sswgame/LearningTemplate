// MinigameReplicationTest — 서버가 들어간 하위 레벨 미니게임의 목록 복제와 준비 확인: 들어온 연결은 비었어도 목록을 받고, 서버는 월드에 있는 연결이 모두 답할 때까지(떠난 연결 · 한도 제외) 짓지 않으며,
// 클라이언트는 깨진 목록 · 경로가 될 수 있는 이름을 받지 않는다(nogpu, 연결 없음 — 가짜 상대).
#include "pch.h"

#include "Core/Network/Tests/FakeNetPeers.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameReplication.h"

using namespace sw;

namespace
{
    struct MinigameReplicationTestUtil
    {
        static constexpr int64 kDeadline = 1000;

        static NetMessageContext makeContext( uint8 kind, int32 connectionID )
        {
            NetMessageContext context;
            context._connectionID = connectionID;
            context._kind         = kind;
            return context;
        }

        /** @brief 서버가 보낸 메시지(종류 · 월드 세대 · 본문)를 클라이언트 처리기에 넘깁니다. */
        static NetHandleResult deliver( MinigameReplicationClient& client, const test::FakeNetPeers::Sent& sent )
        {
            constexpr int32 kHeaderBytes = 2;
            BitReader       body{ sent._bytes.data() + kHeaderBytes, static_cast<int32>( sent._bytes.size() ) - kHeaderBytes };
            return client.handleNetMessage( makeContext( sent._bytes[0], 0 ), body );
        }

        static NetHandleResult sendReady( MinigameReplicationServer& server, int32 connectionID, uint32 entryID, bool bLoaded )
        {
            BitWriter writer;
            writer.writeVarUint( entryID );
            writer.writeBool( bLoaded );
            BitReader body{ writer.getBytes().data(), writer.getByteCount() };
            return server.handleNetMessage( makeContext( MinigameNetMessage::Table::getKindOf<MinigameNetMessage::Ready>(), connectionID ), body );
        }

        /** @brief 항목 하나짜리 목록 본문을 손으로 씁니다. */
        static NetHandleResult receiveState( MinigameReplicationClient& client, string_view name, uint32 entryID, float32 x )
        {
            BitWriter writer;
            writer.writeVarUint( 1 );
            writer.writeVarUint( entryID );
            writer.writeBool( true );
            writer.writeBlob( reinterpret_cast<const uint8*>( name.data() ), static_cast<int32>( name.size() ) );
            for ( const float32 value : { x, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } )
            {
                writer.writeFloat( value );
            }
            BitReader body{ writer.getBytes().data(), writer.getByteCount() };
            return client.handleNetMessage( makeContext( MinigameNetMessage::Table::getKindOf<MinigameNetMessage::State>(), 0 ), body );
        }
    };
    using Util = MinigameReplicationTestUtil;
} // namespace

/** @brief [MinigameReplicationTest] 월드에 들어온 연결은 목록이 비었어도 한 번 받고, 늦게 들어오면 이미 들어간 하위 레벨을 자리 · 짓기 여부까지 그대로 받는다 */
SW_TEST_CASE( MinigameReplicationTest, JoiningPeersReceiveTheWholeList )
{
    test::FakeNetPeers        serverPeers{ true };
    test::FakeNetPeers        clientPeers{ false };
    MinigameReplicationServer server;
    MinigameReplicationClient client;
    server.initialize( &serverPeers );
    client.initialize( &clientPeers );
    serverPeers.connectInWorld( 0 );
    server.onPeerEnteredWorld( 0 );
    server.update();
    vector<test::FakeNetPeers::Sent> listSent = serverPeers.takeSent();
    SW_REQUIRE_EQUAL( size_t( 1 ), listSent.size() );
    SW_EXPECT_FALSE( client.isKnown() );
    SW_REQUIRE_TRUE( Util::deliver( client, listSent[0] ) == NetHandleResult::Handled );
    SW_EXPECT_TRUE( client.isKnown() );
    SW_EXPECT_TRUE( client.getEntries().empty() );

    SubLevelPlacement placement;
    placement._position = float3{ 1.0f, 2.0f, 3.0f };
    placement._rotation = float3{ 0.0f, 0.5f, 0.0f };
    SW_REQUIRE_TRUE( server.addEntry( "cardtable", placement, true, Util::kDeadline ) );
    server.update();
    (void)serverPeers.takeSent();
    server.update();
    SW_EXPECT_TRUE( serverPeers.takeSent().empty() ); // 바뀐 것이 없으면 보내지 않는다

    serverPeers.connectInWorld( 1 );
    server.onPeerEnteredWorld( 1 );
    server.update();
    listSent = serverPeers.takeSent();
    SW_REQUIRE_EQUAL( size_t( 1 ), listSent.size() ); // 들어온 연결에만
    SW_EXPECT_EQUAL( 1, listSent[0]._connectionID );
    client.onWorldOpened( 2 );
    SW_EXPECT_FALSE( client.isKnown() );
    SW_REQUIRE_TRUE( Util::deliver( client, listSent[0] ) == NetHandleResult::Handled );
    SW_REQUIRE_EQUAL( size_t( 1 ), client.getEntries().size() );
    SW_EXPECT_EQUAL( string( "cardtable" ), client.getEntries()[0]._name );
    SW_EXPECT_TRUE( client.getEntries()[0]._placement._position == placement._position );
    SW_EXPECT_TRUE( client.getEntries()[0]._placement._rotation == placement._rotation );
    SW_EXPECT_TRUE( client.getEntries()[0]._bReleased == SW_TRUE );

    server.removeEntry( "cardtable" );
    server.update();
    SW_EXPECT_EQUAL( size_t( 2 ), serverPeers.takeSent().size() ); // 둘 다
}

/** @brief [MinigameReplicationTest] 서버는 월드에 있는 연결이 모두 답해야 짓는다 — 읽지 못했다는 답 · 떠남도 답이고, 모르는 번호 · 다른 연결의 답은 세지 않는다. 답하지 않는 연결은 한도까지만 기다린다 */
SW_TEST_CASE( MinigameReplicationTest, ServerWaitsForEveryPeerWithinTheLimit )
{
    SW_EXPECT_LOG( "MinigameReplication.ClientNotLoaded" );
    SW_EXPECT_LOG( "MinigameReplication.NotEveryoneReady" );
    test::FakeNetPeers        peers{ true };
    MinigameReplicationServer server;
    server.initialize( &peers );
    for ( const int32 connectionID : { 0, 1, 2 } )
    {
        peers.connectInWorld( connectionID );
        server.onPeerEnteredWorld( connectionID );
    }
    SW_REQUIRE_TRUE( server.addEntry( "cardtable", SubLevelPlacement{}, false, Util::kDeadline ) );
    const uint32 entryID = server.getEntries()[0]._entryID;
    SW_EXPECT_FALSE( server.isReady( "cardtable", 0 ) );
    SW_EXPECT_TRUE( Util::sendReady( server, 0, entryID, true ) == NetHandleResult::Handled );
    SW_EXPECT_TRUE( Util::sendReady( server, 0, entryID, true ) == NetHandleResult::Handled ); // 두 번째 답
    SW_EXPECT_TRUE( Util::sendReady( server, 1, entryID + 7u, true ) == NetHandleResult::Handled );
    SW_EXPECT_TRUE( Util::sendReady( server, 9, entryID, true ) == NetHandleResult::Handled );
    SW_EXPECT_FALSE( server.isReady( "cardtable", 0 ) );
    SW_EXPECT_TRUE( Util::sendReady( server, 1, entryID, false ) == NetHandleResult::Handled ); // 읽지 못했다 — 기다리지 않는다
    SW_EXPECT_FALSE( server.isReady( "cardtable", 0 ) );
    server.onPeerLeftWorld( 2 );
    SW_EXPECT_TRUE( server.isReady( "cardtable", 0 ) );
    server.releaseEntry( "cardtable" );
    SW_EXPECT_TRUE( server.getEntries()[0]._bReleased == SW_TRUE );

    // 답하지 않는 연결 하나가 서버를 붙잡지 못한다.
    SW_REQUIRE_TRUE( server.addEntry( "themepark", SubLevelPlacement{}, false, Util::kDeadline ) );
    SW_EXPECT_FALSE( server.isReady( "themepark", Util::kDeadline - 1 ) );
    SW_EXPECT_TRUE( server.isReady( "themepark", Util::kDeadline ) );
    server.releaseEntry( "themepark" );
    SW_EXPECT_TRUE( server.getEntries()[1]._bReleased == SW_TRUE );
}

/** @brief [MinigameReplicationTest] 기다리는 동안 들어온 연결도 함께 기다린다 */
SW_TEST_CASE( MinigameReplicationTest, PeerJoiningDuringTheWaitIsAwaited )
{
    test::FakeNetPeers        peers{ true };
    MinigameReplicationServer server;
    server.initialize( &peers );
    SW_REQUIRE_TRUE( server.addEntry( "cardtable", SubLevelPlacement{}, false, Util::kDeadline ) );
    SW_EXPECT_TRUE( server.isReady( "cardtable", 0 ) ); // 아무도 없다
    peers.connectInWorld( 0 );
    server.onPeerEnteredWorld( 0 );
    SW_EXPECT_FALSE( server.isReady( "cardtable", 0 ) );
    SW_EXPECT_TRUE( Util::sendReady( server, 0, server.getEntries()[0]._entryID, true ) == NetHandleResult::Handled );
    SW_EXPECT_TRUE( server.isReady( "cardtable", 0 ) );
}

/** @brief [MinigameReplicationTest] 클라이언트는 경로가 될 수 있는 이름 · 유한하지 않은 자리 · 번호 없는 항목을 받지 않고, 받아 둔 목록은 그대로다. 서버도 그런 이름을 싣지 않는다 */
SW_TEST_CASE( MinigameReplicationTest, HostileListIsRefused )
{
    SW_EXPECT_LOG( "MinigameReplication.EntryRefused" );
    MinigameReplicationClient client;
    SW_REQUIRE_TRUE( Util::receiveState( client, "cardtable", 1, 4.0f ) == NetHandleResult::Handled );
    for ( const string_view name : { string_view( "../secret" ), string_view( "a/b" ), string_view( "" ), string_view( "Card" ), string_view( "c:d" ) } )
    {
        SW_EXPECT_TRUE( Util::receiveState( client, name, 2, 0.0f ) == NetHandleResult::Malformed );
    }
    SW_EXPECT_TRUE( Util::receiveState( client, "cardtable", 2, std::numeric_limits<float32>::quiet_NaN() ) == NetHandleResult::Malformed );
    SW_EXPECT_TRUE( Util::receiveState( client, "cardtable", 0, 0.0f ) == NetHandleResult::Malformed );
    SW_REQUIRE_EQUAL( size_t( 1 ), client.getEntries().size() );
    SW_EXPECT_EQUAL( 1u, client.getEntries()[0]._entryID );

    test::FakeNetPeers        peers{ true };
    MinigameReplicationServer server;
    server.initialize( &peers );
    SW_EXPECT_FALSE( server.addEntry( "../secret", SubLevelPlacement{}, true, Util::kDeadline ) );
}
