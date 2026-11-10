#include "pch.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Network/Transport/WebSocketAcceptTransport.h"
#include "Core/Network/Transport/WebSocketProtocol.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

// WebSocket 수락 장식자 — 업그레이드 응답(RFC 6455 의 예시 키) · 가린 클라이언트 프레임의 몸만 위로 · 서버 보내기는 이진 프레임 · 원시 스트림은 그대로 · 잘못된 요청은 400.
// 루프백 망(I/O 스레드 없이 pollIO — 결정적)에서 "클라이언트" 는 감싸지 않은 전송이 바이트를 손으로 짠다(브라우저가 보낼 모양).

using namespace sw;

namespace
{
    class ByteRecorder final : public IStreamHandler
    {
    public:
        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
        {
            ScopedLock<mutex> lock{ _mutex };
            (void)remote;
            (void)bAccepted;
            _listOpened.push_back( handle );
        }
        void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override
        {
            ScopedLock<mutex> lock{ _mutex };
            (void)handle;
            _received.insert( _received.end(), pData, pData + size );
        }
        void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
        {
            ScopedLock<mutex> lock{ _mutex };
            (void)handle;
            (void)reason;
            ++_closedCount;
        }

        int32 getOpenedCount() const
        {
            ScopedLock<mutex> lock{ _mutex };
            return static_cast<int32>( _listOpened.size() );
        }
        StreamConnectionHandle getOpened( int32 index ) const
        {
            ScopedLock<mutex> lock{ _mutex };
            return _listOpened[static_cast<size_t>( index )];
        }
        vector<uint8> copyReceived() const
        {
            ScopedLock<mutex> lock{ _mutex };
            return _received;
        }
        int32 getClosedCount() const
        {
            ScopedLock<mutex> lock{ _mutex };
            return _closedCount;
        }

    private:
        mutable mutex                  _mutex;
        vector<StreamConnectionHandle> _listOpened;
        vector<uint8>                  _received;
        int32                          _closedCount{ 0 };
    };

    struct WebSocketRig
    {
        ByteRecorder                      _serverRecorder{};
        ByteRecorder                      _clientRecorder{};
        unique_ptr<LoopbackStreamNetwork> _network{ make_unique<LoopbackStreamNetwork>( 11u ) };
        unique_ptr<IStreamTransport>      _server{};
        unique_ptr<IStreamTransport>      _client{};
        StreamConnectionHandle            _clientHandle{};

        bool start()
        {
            _server                        = make_unique<WebSocketAcceptTransport>( _network->createTransport() );
            _client                        = _network->createTransport();
            StreamTransportConfig settings = {};
            settings._ioThreadCount        = 0;
            if ( _server->initialize( &_serverRecorder, settings ) == false || _client->initialize( &_clientRecorder, settings ) == false )
                return false;
            if ( _server->listen( NetAddress::makeLoopback( 0 ) ) == false )
                return false;
            _clientHandle = _client->connect( NetAddress::makeLoopback( _server->getListenPort() ) );
            return runUntil( [this]()
            { return _serverRecorder.getOpenedCount() == 1 && _clientRecorder.getOpenedCount() == 1; } );
        }

        template <typename TPredicate>
        bool runUntil( TPredicate&& predicate )
        {
            const Deadline deadline = Deadline::afterMilliseconds( 3000 );
            while ( predicate() == false )
            {
                if ( deadline.isExpired() )
                    return false;
                (void)_server->pollIO( 0 );
                (void)_client->pollIO( 0 );
            }
            return true;
        }

        void sendText( string_view text ) { (void)_client->send( _clientHandle, reinterpret_cast<const uint8*>( text.data() ), static_cast<int32>( text.size() ) ); }

        /** @brief 브라우저가 보내는 가린 클라이언트 프레임(FIN)입니다. */
        void sendMaskedFrame( WebSocketFrameKind kind, const vector<uint8>& payload, bool bFinal = true )
        {
            const uint8   arrMask[4] = { 0x37, 0xFA, 0x21, 0x3D };
            vector<uint8> frame;
            frame.push_back( static_cast<uint8>( ( bFinal ? 0x80u : 0u ) | static_cast<uint8>( opcode ) ) );
            SW_ASSERT( payload.size() < 126 );
            frame.push_back( static_cast<uint8>( 0x80u | payload.size() ) );
            frame.insert( frame.end(), arrMask, arrMask + 4 );
            for ( size_t index = 0; index < payload.size(); ++index )
            {
                frame.push_back( static_cast<uint8>( payload[index] ^ arrMask[index % 4] ) );
            }
            (void)_client->send( _clientHandle, frame.data(), static_cast<int32>( frame.size() ) );
        }
    };

    string toText( const vector<uint8>& bytes )
    {
        return string{ reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() };
    }

    constexpr const utf8* kUpgradeRequest = "GET /chat HTTP/1.1\r\nHost: server.example.com\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\n"
                                            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nOrigin: http://example.com\r\nSec-WebSocket-Version: 13\r\n\r\n";
} // namespace

/** @brief [WebSocketProtocolTest] 수락 키는 RFC 6455 1.3 의 예시 값이다(SHA-1 · base64 를 함께 확인). 판 · Upgrade 가 빠진 요청은 거절한다. */
SW_TEST_CASE( WebSocketProtocolTest, AcceptKeyAndUpgradeRequest )
{
    SW_EXPECT_TRUE( WebSocketProtocol::makeAcceptKey( "dGhlIHNhbXBsZSBub25jZQ==" ) == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" );
    string clientKey;
    SW_EXPECT_TRUE( WebSocketProtocol::parseUpgradeRequest( kUpgradeRequest, clientKey ) );
    SW_EXPECT_TRUE( clientKey == "dGhlIHNhbXBsZSBub25jZQ==" );
    SW_EXPECT_FALSE( WebSocketProtocol::parseUpgradeRequest( "GET / HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n", clientKey ) );
    SW_EXPECT_FALSE( WebSocketProtocol::parseUpgradeRequest( "GET / HTTP/1.1\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n", clientKey ) );
}

/** @brief [WebSocketProtocolTest] 해독기는 가리지 않은 클라이언트 프레임 · 조각난 제어 프레임 · 상한을 넘는 길이를 거절하고, 둘로 나뉘어 온 프레임은 기다렸다 읽는다. */
SW_TEST_CASE( WebSocketProtocolTest, DecoderRejectsMalformedFrames )
{
    WebSocketFrameView frame;
    {
        WebSocketFrameDecoder decoder;
        const uint8           arrUnmasked[] = { 0x82, 0x01, 0x41 };
        decoder.append( arrUnmasked, 3 );
        SW_EXPECT_TRUE( decoder.next( frame ) == WebSocketDecodeResult::Malformed );
    }
    {
        WebSocketFrameDecoder decoder;
        const uint8           arrFragmentedPing[] = { 0x09, 0x80, 0, 0, 0, 0 };
        decoder.append( arrFragmentedPing, 6 );
        SW_EXPECT_TRUE( decoder.next( frame ) == WebSocketDecodeResult::Malformed );
    }
    {
        WebSocketFrameDecoder decoder{ 100 };
        const uint8           arrLong[] = { 0x82, 0xFE, 0x01, 0x00 }; // 256 바이트 — 상한 100
        decoder.append( arrLong, 4 );
        SW_EXPECT_TRUE( decoder.next( frame ) == WebSocketDecodeResult::Malformed );
    }
    {
        WebSocketFrameDecoder decoder;
        const uint8           arrMasked[] = { 0x82, 0x82, 1, 2, 3, 4, static_cast<uint8>( 'h' ^ 1 ), static_cast<uint8>( 'i' ^ 2 ) };
        decoder.append( arrMasked, 5 );
        SW_EXPECT_TRUE( decoder.next( frame ) == WebSocketDecodeResult::NeedMore );
        decoder.append( arrMasked + 5, 3 );
        SW_REQUIRE_TRUE( decoder.next( frame ) == WebSocketDecodeResult::Frame );
        SW_EXPECT_EQUAL( 2, frame._payloadSize );
        SW_EXPECT_TRUE( frame._pPayload[0] == 'h' && frame._pPayload[1] == 'i' );
    }
}

/**
 * @brief [WebSocketAcceptTransportTest] 업그레이드한 연결: 위 층은 수락 때 열림을 받고, 업그레이드 전에 보낸 것은 101 뒤 이진 프레임으로 나가며,
 *        가린 이진 · 이어 붙임 프레임의 몸만 순서대로 올라오고, Ping 에는 Pong 으로 답한다.
 */
SW_TEST_CASE( WebSocketAcceptTransportTest, UpgradesAndUnwrapsFrames )
{
    WebSocketRig rig;
    SW_REQUIRE_TRUE( rig.start() );
    const StreamConnectionHandle serverHandle = rig._serverRecorder.getOpened( 0 );
    const uint8                  arrEarly[]   = { 'o', 'k' };
    SW_EXPECT_TRUE( rig._server->send( serverHandle, arrEarly, 2 ) == StreamSendResult::Queued ); // 갈래가 정해지기 전 — 쌓아 둔다

    rig.sendText( kUpgradeRequest );
    rig.sendMaskedFrame( WebSocketFrameKind::Binary, { 1, 2, 3 }, false );
    rig.sendMaskedFrame( WebSocketFrameKind::Continuation, { 4, 5 } );
    rig.sendMaskedFrame( WebSocketFrameKind::Ping, { 9 } );
    SW_REQUIRE_TRUE( rig.runUntil( [&]()
    { return rig._serverRecorder.copyReceived().size() == 5; } ) );
    SW_EXPECT_TRUE( rig._serverRecorder.copyReceived() == ( vector<uint8>{ 1, 2, 3, 4, 5 } ) );

    const string  expectedHead = WebSocketProtocol::makeUpgradeResponse( "dGhlIHNhbXBsZSBub25jZQ==" );
    vector<uint8> expected( expectedHead.begin(), expectedHead.end() );
    WebSocketProtocol::appendFrame( expected, WebSocketFrameKind::Binary, arrEarly, 2 );
    const uint8 arrPing[] = { 9 };
    WebSocketProtocol::appendFrame( expected, WebSocketFrameKind::Pong, arrPing, 1 );
    SW_REQUIRE_TRUE( rig.runUntil( [&]()
    { return rig._clientRecorder.copyReceived().size() >= expected.size(); } ) );
    SW_EXPECT_TRUE( rig._clientRecorder.copyReceived() == expected );
    SW_EXPECT_TRUE( toText( rig._clientRecorder.copyReceived() ).find( "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" ) != string::npos );
    rig._client->shutdown();
    rig._server->shutdown();
}

/** @brief [WebSocketAcceptTransportTest] `GET ` 으로 시작하지 않는 연결은 원시 스트림이다 — 받은 바이트 · 쌓아 둔 보내기가 그대로 지난다. */
SW_TEST_CASE( WebSocketAcceptTransportTest, RawStreamPassesThrough )
{
    WebSocketRig rig;
    SW_REQUIRE_TRUE( rig.start() );
    const StreamConnectionHandle serverHandle = rig._serverRecorder.getOpened( 0 );
    const uint8                  arrEarly[]   = { 7, 8 };
    SW_EXPECT_TRUE( rig._server->send( serverHandle, arrEarly, 2 ) == StreamSendResult::Queued );
    rig.sendText( "GX" ); // 첫 글자는 같아도 둘째에서 갈린다
    SW_REQUIRE_TRUE( rig.runUntil( [&]()
    { return rig._serverRecorder.copyReceived().size() == 2 && rig._clientRecorder.copyReceived().size() == 2; } ) );
    SW_EXPECT_TRUE( toText( rig._serverRecorder.copyReceived() ) == "GX" );
    SW_EXPECT_TRUE( rig._clientRecorder.copyReceived() == ( vector<uint8>{ 7, 8 } ) );
    rig._client->shutdown();
    rig._server->shutdown();
}

/** @brief [WebSocketAcceptTransportTest] 키 · 판이 없는 업그레이드 요청은 400 으로 답하고 닫는다 — 위 층은 아무 바이트도 받지 않는다. */
SW_TEST_CASE( WebSocketAcceptTransportTest, RejectsBadUpgrade )
{
    WebSocketRig rig;
    SW_REQUIRE_TRUE( rig.start() );
    rig.sendText( "GET / HTTP/1.1\r\nUpgrade: websocket\r\n\r\n" );
    SW_REQUIRE_TRUE( rig.runUntil( [&]()
    { return rig._serverRecorder.getClosedCount() == 1 && rig._clientRecorder.getClosedCount() == 1; } ) );
    SW_EXPECT_TRUE( rig._serverRecorder.copyReceived().empty() );
    SW_EXPECT_TRUE( toText( rig._clientRecorder.copyReceived() ).find( "400 Bad Request" ) != string::npos );
}
