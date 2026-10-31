#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Message/StreamMessageEndpoint.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

// 스트림 엔드포인트 — 조각나 들어와도 메시지 경계 · 순서, 깨진 프레임은 ProtocolError 로 끊기, 받은 줄 상한에서 읽기 멈춤 → pump 뒤 다시, 핑 RTT, 연결 실패도 닫힘 한 번

using namespace sw;

namespace
{
    struct EndpointRecord final : public IStreamEndpointListener
    {
        vector<StreamConnectionHandle> _listOpened{};
        vector<vector<uint8>>          _listMessage{};
        vector<StreamFrameKind>        _listKind{};
        vector<StreamCloseReason>      _listClosedReason{};

        void onEndpointOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
        {
            (void)remote;
            (void)bAccepted;
            _listOpened.push_back( handle );
        }
        void onEndpointFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize ) override
        {
            (void)handle;
            _listKind.push_back( kind );
            _listMessage.emplace_back( pBody, pBody + bodySize );
        }
        void onEndpointClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
        {
            (void)handle;
            _listClosedReason.push_back( reason );
        }
    };

    /**
     * @brief 루프백 스트림 망 위의 서버 · 클라이언트 엔드포인트 한 쌍입니다 — 서버는 `listen` 하고 클라이언트는 `_clientHandle` 로 연결을 겁니다.
     * @details 전송은 I/O 스레드 없이(`_ioThreadCount = 0`) 돌고 `step` 이 두 전송의 `pollIO` 와 두 엔드포인트의 `pump` 를 차례로 부른다 — 한 스레드라 결정적이다.
     */
    class StreamEndpointPair
    {
    public:
        StreamEndpointPair( IStreamEndpointListener& serverListener, IStreamEndpointListener& clientListener, const StreamEndpointConfig& endpointSettings,
                            const LoopbackStreamConditions& conditions )
            : _network{ 11u }
            , _serverTransport{ _network.createTransport() }
            , _clientTransport{ _network.createTransport() }
            , _server{}
            , _client{}
            , _clientHandle{}
            , _pServerListener{ &serverListener }
            , _pClientListener{ &clientListener }
        {
            _network.setConditions( conditions );
            StreamTransportConfig transportSettings;
            transportSettings._ioThreadCount = 0;
            _bReady                          = _server.initialize( _serverTransport.get(), endpointSettings ) && _client.initialize( _clientTransport.get(), endpointSettings ) &&
                      _serverTransport->initialize( &_server, transportSettings ) && _clientTransport->initialize( &_client, transportSettings ) &&
                      _serverTransport->listen( NetAddress::makeLoopback( 0 ) );
            _clientHandle = _client.connect( NetAddress::makeLoopback( _serverTransport->getListenPort() ) );
        }

        ~StreamEndpointPair()
        {
            _clientTransport->shutdown();
            _serverTransport->shutdown();
            step( 1 );
        }

        StreamEndpointPair( const StreamEndpointPair& )            = delete;
        StreamEndpointPair& operator=( const StreamEndpointPair& ) = delete;

        /** @brief 두 전송을 돌고 두 엔드포인트를 pump 하기를 @p count 번 합니다. */
        void step( int32 count )
        {
            for ( int32 index = 0; index < count; ++index )
            {
                (void)_serverTransport->pollIO( 0 );
                (void)_clientTransport->pollIO( 0 );
                (void)_server.pump( *_pServerListener );
                (void)_client.pump( *_pClientListener );
            }
        }

        LoopbackStreamNetwork        _network;
        unique_ptr<IStreamTransport> _serverTransport;
        unique_ptr<IStreamTransport> _clientTransport;
        StreamMessageEndpoint        _server;
        StreamMessageEndpoint        _client;
        StreamConnectionHandle       _clientHandle;
        bool                         _bReady{ false }; ///< 준비(초기화 · listen)가 모두 됐다 — 케이스가 처음에 `SW_REQUIRE_TRUE` 로 본다

    private:
        IStreamEndpointListener* _pServerListener;
        IStreamEndpointListener* _pClientListener;
    };
} // namespace

SW_TEST_CASE( StreamEndpointTest, MessagesKeepBoundariesAndOrderAcrossChunks )
{
    EndpointRecord     serverRecord;
    EndpointRecord     clientRecord;
    StreamEndpointPair pair( serverRecord, clientRecord, StreamEndpointConfig{}, LoopbackStreamConditions{ 5, 0 } ); // 1..5 바이트 조각
    SW_REQUIRE_TRUE( pair._bReady );
    pair.step( 2 );
    SW_REQUIRE_EQUAL( 1, static_cast<int32>( serverRecord._listOpened.size() ) );
    for ( int32 index = 0; index < 50; ++index )
    {
        const vector<uint8> body( static_cast<size_t>( index * 7 ), static_cast<uint8>( index ) );
        SW_EXPECT_TRUE( pair._client.sendMessage( pair._clientHandle, body.data(), static_cast<int32>( body.size() ) ) != StreamSendResult::Closed );
    }
    pair.step( 4000 );
    SW_REQUIRE_EQUAL( 50, static_cast<int32>( serverRecord._listMessage.size() ) );
    for ( int32 index = 0; index < 50; ++index )
    {
        const vector<uint8>& message = serverRecord._listMessage[static_cast<size_t>( index )];
        SW_EXPECT_EQUAL( index * 7, static_cast<int32>( message.size() ) );
        SW_EXPECT_TRUE( message.empty() || message.back() == static_cast<uint8>( index ) );
        SW_EXPECT_TRUE( serverRecord._listKind[static_cast<size_t>( index )] == StreamFrameKind::Message );
    }
}

SW_TEST_CASE( StreamEndpointTest, MalformedFrameClosesWithProtocolError )
{
    EndpointRecord     serverRecord;
    EndpointRecord     clientRecord;
    StreamEndpointPair pair( serverRecord, clientRecord, StreamEndpointConfig{}, LoopbackStreamConditions{} );
    SW_REQUIRE_TRUE( pair._bReady );
    pair.step( 2 );
    // 엔드포인트를 거치지 않고 전송에 직접 깨진 머리를 쓴다(모르는 종류 0x7F).
    const uint8 arrWire[6] = { 0x02, 0x00, 0x00, 0x00, 0x7F, 0x00 };
    SW_EXPECT_LOG( "Stream.ClosedForError" ); // 서버 엔드포인트가 끊는 까닭을 남긴다
    (void)pair._clientTransport->send( pair._clientHandle, arrWire, 6 );
    pair.step( 4 );
    SW_REQUIRE_EQUAL( 1, static_cast<int32>( serverRecord._listClosedReason.size() ) );
    SW_EXPECT_TRUE( serverRecord._listClosedReason[0] == StreamCloseReason::ProtocolError );
    SW_REQUIRE_EQUAL( 1, static_cast<int32>( clientRecord._listClosedReason.size() ) );
    SW_EXPECT_TRUE( clientRecord._listClosedReason[0] == StreamCloseReason::Reset );
}

/**
 * @brief [StreamEndpointTest] 받은 줄이 상한을 넘으면 읽기를 멈춘다 — pump 하지 않는 동안 보낸 쪽 줄이 쌓이고, pump 하면 다시 흐른다
 */
SW_TEST_CASE( StreamEndpointTest, PendingReceiveCapPausesTransportUntilPumped )
{
    StreamEndpointConfig settings;
    settings._maxPendingReceiveBytes = 4096;
    EndpointRecord     serverRecord;
    EndpointRecord     clientRecord;
    StreamEndpointPair pair( serverRecord, clientRecord, settings, LoopbackStreamConditions{ 0, 2048 } ); // 한 pollIO 가 2 KB 씩 — 멈춤이 걸릴 틈을 준다
    SW_REQUIRE_TRUE( pair._bReady );
    pair.step( 2 );
    const vector<uint8> body( 1000, 0x42 );
    for ( int32 index = 0; index < 40; ++index )
    {
        (void)pair._client.sendMessage( pair._clientHandle, body.data(), 1000 );
    }
    for ( int32 index = 0; index < 20; ++index ) // 서버는 pump 하지 않고 전송만 돈다
    {
        (void)pair._serverTransport->pollIO( 0 );
        (void)pair._clientTransport->pollIO( 0 );
    }
    SW_EXPECT_TRUE( pair._clientTransport->getStats()._sentBytes > 0 );
    // 멈췄으면 클라이언트 쪽 보낼 줄(아직 넘겨받지 않은 바이트)이 남아 있다 — 서버가 받은 것은 상한 + 한 덩어리 근처.
    SW_EXPECT_TRUE( pair._serverTransport->getStats()._receivedBytes < 40u * 1006u );
    pair.step( 200 );
    SW_EXPECT_EQUAL( 40, static_cast<int32>( serverRecord._listMessage.size() ) );
}

SW_TEST_CASE( StreamEndpointTest, PingMeasuresRoundTripAndFailedConnectStillCloses )
{
    StreamEndpointConfig settings;
    settings._pingIntervalSeconds = 0.001;
    EndpointRecord     serverRecord;
    EndpointRecord     clientRecord;
    StreamEndpointPair pair( serverRecord, clientRecord, settings, LoopbackStreamConditions{} );
    SW_REQUIRE_TRUE( pair._bReady );
    const Deadline deadline = Deadline::afterMilliseconds( 2000 );
    while ( pair._client.getRoundTripSeconds( pair._clientHandle ) < 0.0 && deadline.isExpired() == false )
    {
        pair.step( 1 );
    }
    SW_EXPECT_TRUE( pair._client.getRoundTripSeconds( pair._clientHandle ) >= 0.0 );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( clientRecord._listMessage.size() ) ); // 핑 · 퐁은 리스너에 오지 않는다

    const StreamConnectionHandle failed = pair._client.connect( NetAddress::makeLoopback( 1 ) ); // 아무도 듣지 않는 포트
    SW_EXPECT_TRUE( failed.isValid() );
    pair.step( 2 );
    SW_REQUIRE_EQUAL( 1, static_cast<int32>( clientRecord._listClosedReason.size() ) );
    SW_EXPECT_TRUE( clientRecord._listClosedReason[0] == StreamCloseReason::ConnectFailed );
}
