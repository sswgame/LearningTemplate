#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Network/Transport/StreamNetTransport.h"
#include "Core/Network/Transport/WebSocketAcceptTransport.h"
#include "Core/Testing/TestFramework.h"

// 스트림 위의 데이터그램 전송 — 브라우저 게임 세션의 길. 루프백 스트림 망(I/O 스레드 없이 pollIO — 결정적)에서 서버는 운영과 같은 WebSocketAcceptTransport 로 감싼다.

using namespace sw;

namespace
{
    struct StreamNetRig
    {
        unique_ptr<LoopbackStreamNetwork> _network{ make_unique<LoopbackStreamNetwork>( 7u ) };
        unique_ptr<StreamNetTransport>    _server{};
        unique_ptr<StreamNetTransport>    _client{};
        NetAddress                        _serverAddress{};

        static StreamTransportConfig makeConfig()
        {
            StreamTransportConfig settings;
            settings._ioThreadCount = 0;
            return settings;
        }

        bool start( int32 maxConnectionsPerRemote )
        {
            LoopbackStreamConditions conditions;
            conditions._maxChunkBytes = 5; // 프레임 경계와 어긋나게 쪼갠다
            _network->setConditions( conditions );
            _server = make_unique<StreamNetTransport>( make_unique<WebSocketAcceptTransport>( _network->createTransport() ) );
            if ( _server->listen( NetAddress::makeLoopback( 0 ), makeConfig(), maxConnectionsPerRemote ) == false )
                return false;
            _serverAddress = _server->getLocalAddress();
            _client        = makeClient();
            return _client != nullptr;
        }

        unique_ptr<StreamNetTransport> makeClient()
        {
            unique_ptr<StreamNetTransport> client = make_unique<StreamNetTransport>( _network->createTransport() );
            if ( client->connect( _serverAddress, makeConfig() ) == false )
                return nullptr;
            return client;
        }

        void pollAll()
        {
            for ( int32 round = 0; round < 64; ++round )
            {
                (void)_server->pollIO( 0 );
                (void)_client->pollIO( 0 );
            }
        }
    };
} // namespace

/** @brief [StreamNetTransportTest] 데이터그램의 경계 · 보낸 주소가 양쪽으로 지켜지고, 크기가 틀린 것은 보내지 않는다 */
SW_TEST_CASE( StreamNetTransportTest, DatagramKeepsItsBoundariesBothWays )
{
    StreamNetRig rig;
    SW_REQUIRE_TRUE( rig.start( 4 ) );
    rig.pollAll();

    vector<uint8> largest( static_cast<size_t>( kNetMaxPacketSize ), 0xABu );
    const uint8   arrSmall[] = { 1, 2, 3 };
    SW_REQUIRE_TRUE( rig._client->send( rig._serverAddress, arrSmall, 3 ) );
    SW_REQUIRE_TRUE( rig._client->send( rig._serverAddress, largest.data(), kNetMaxPacketSize ) );
    SW_EXPECT_FALSE( rig._client->send( rig._serverAddress, arrSmall, 0 ) );
    largest.push_back( 0u );
    SW_EXPECT_FALSE( rig._client->send( rig._serverAddress, largest.data(), kNetMaxPacketSize + 1 ) );
    SW_EXPECT_FALSE( rig._client->send( NetAddress::makeLoopback( 1 ), arrSmall, 3 ) ); // 걸지 않은 상대
    rig.pollAll();

    NetAddress    from;
    vector<uint8> buffer;
    SW_REQUIRE_TRUE( rig._server->receive( from, buffer ) );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( buffer.size() ) );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( buffer[2] ) );
    SW_REQUIRE_TRUE( rig._server->receive( from, buffer ) );
    SW_EXPECT_EQUAL( kNetMaxPacketSize, static_cast<int32>( buffer.size() ) );
    SW_EXPECT_FALSE( rig._server->receive( from, buffer ) );

    SW_REQUIRE_TRUE( rig._server->send( from, arrSmall, 2 ) ); // 받은 주소로 답한다
    rig.pollAll();
    NetAddress answeredFrom;
    SW_REQUIRE_TRUE( rig._client->receive( answeredFrom, buffer ) );
    SW_EXPECT_TRUE( answeredFrom == rig._serverAddress );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( buffer.size() ) );
}

/** @brief [StreamNetTransportTest] 깨진 프레임을 보낸 연결은 닫히고 그 앞까지만 닿는다. 클라이언트의 다음 보내기가 다시 건다 */
SW_TEST_CASE( StreamNetTransportTest, MalformedStreamClosesAndClientReconnects )
{
    class Ignore final : public IStreamHandler
    {
    public:
        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
        {
            (void)remote;
            (void)bAccepted;
            _handle = handle;
        }
        void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override
        {
            (void)handle;
            (void)pData;
            (void)size;
        }
        void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
        {
            (void)handle;
            (void)reason;
            ++_closedCount;
        }

        StreamConnectionHandle _handle{};
        int32                  _closedCount{ 0 };
    };

    StreamNetRig rig;
    SW_REQUIRE_TRUE( rig.start( 4 ) );
    Ignore                       stranger;
    unique_ptr<IStreamTransport> raw = rig._network->createTransport();
    SW_REQUIRE_TRUE( raw->initialize( &stranger, StreamNetRig::makeConfig() ) );
    SW_REQUIRE_TRUE( raw->connect( rig._serverAddress ).isValid() );
    (void)raw->pollIO( 0 );
    const uint8 arrTooLong[] = { 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 }; // 몸 65533 바이트 — 패킷 상한을 넘는다
    SW_EXPECT_TRUE( raw->send( stranger._handle, arrTooLong, static_cast<int32>( sizeof( arrTooLong ) ) ) == StreamSendResult::Queued );
    for ( int32 round = 0; round < 16; ++round )
    {
        rig.pollAll();
        (void)raw->pollIO( 0 );
    }
    SW_EXPECT_EQUAL( 1, stranger._closedCount );
    NetAddress    from;
    vector<uint8> buffer;
    SW_EXPECT_FALSE( rig._server->receive( from, buffer ) );

    // 서버가 내려갔다 다시 뜬다 — 끊긴 클라이언트의 첫 보내기는 버려지고(다시 건다) 열린 뒤의 보내기가 닿는다.
    const uint16 port = rig._serverAddress._port;
    rig._server->close();
    (void)rig._client->pollIO( 0 );
    rig._server = make_unique<StreamNetTransport>( make_unique<WebSocketAcceptTransport>( rig._network->createTransport() ) );
    SW_REQUIRE_TRUE( rig._server->listen( NetAddress::makeLoopback( port ), StreamNetRig::makeConfig(), 4 ) );
    const uint8 arrByte[] = { 9 };
    SW_EXPECT_FALSE( rig._client->send( rig._serverAddress, arrByte, 1 ) );
    rig.pollAll();
    SW_REQUIRE_TRUE( rig._client->send( rig._serverAddress, arrByte, 1 ) );
    rig.pollAll();
    SW_EXPECT_TRUE( rig._server->receive( from, buffer ) );
}

/** @brief [StreamNetTransportTest] 한 호스트가 상한을 넘겨 연 연결은 닫히고, 먼저 열린 연결은 그대로다 */
SW_TEST_CASE( StreamNetTransportTest, ConnectionsPerHostAreLimited )
{
    StreamNetRig rig;
    SW_EXPECT_FALSE( rig.start( 0 ) ); // 상한 없이는 열지 않는다
    SW_REQUIRE_TRUE( rig.start( 1 ) );
    rig.pollAll();
    unique_ptr<StreamNetTransport> second = rig.makeClient();
    SW_REQUIRE_TRUE( second != nullptr );
    for ( int32 round = 0; round < 16; ++round )
    {
        (void)second->pollIO( 0 );
        rig.pollAll();
    }
    const uint8 arrByte[] = { 5 };
    SW_EXPECT_FALSE( second->send( rig._serverAddress, arrByte, 1 ) );
    SW_REQUIRE_TRUE( rig._client->send( rig._serverAddress, arrByte, 1 ) );
    rig.pollAll();
    NetAddress    from;
    vector<uint8> buffer;
    SW_EXPECT_TRUE( rig._server->receive( from, buffer ) );
    SW_EXPECT_FALSE( rig._server->receive( from, buffer ) );
}

/** @brief [StreamNetTransportTest] 엔드포인트가 UDP 와 같은 길로 붙고 신뢰 메시지가 순서대로 닿는다 */
SW_TEST_CASE( StreamNetTransportTest, EndpointConnectsOverStream )
{
    StreamNetRig rig;
    SW_REQUIRE_TRUE( rig.start( 4 ) );
    NetEndpoint server;
    NetEndpoint client;
    SW_REQUIRE_TRUE( server.initialize( rig._server.get(), NetEndpointConfig{} ) );
    SW_REQUIRE_TRUE( client.initialize( rig._client.get(), NetEndpointConfig{} ) );
    SW_REQUIRE_TRUE( server.listen() );
    const TaskFuture<NetConnectResult> future = client.connectAsync( rig._serverAddress );

    float64    now      = 0.0;
    const auto stepOnce = [&]()
    {
        now += 1.0 / 60.0;
        client.update( now );
        rig.pollAll();
        server.update( now );
        rig.pollAll();
    };
    for ( int32 step = 0; step < 300 && future.isReady() == false; ++step )
    {
        stepOnce();
    }
    SW_REQUIRE_TRUE( future.isReady() );
    SW_REQUIRE_TRUE( future.get().isConnected() );

    for ( int32 index = 0; index < 8; ++index )
    {
        SW_REQUIRE_TRUE( client.sendMessage( 0, NetChannelKind::ReliableOrdered, vector<uint8>( 1, static_cast<uint8>( index ) ) ) );
    }
    int32          received     = 0;
    int32          connectionID = -1;
    NetChannelKind channel      = NetChannelKind::Unreliable;
    vector<uint8>  buffer;
    for ( int32 step = 0; step < 300 && received < 8; ++step )
    {
        stepOnce();
        while ( server.receiveMessage( connectionID, channel, buffer ) )
        {
            SW_EXPECT_EQUAL( received, static_cast<int32>( buffer[0] ) );
            ++received;
        }
    }
    SW_EXPECT_EQUAL( 8, received );
}
