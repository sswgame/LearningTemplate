#include "pch.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/HTTP/HTTP2Client.h"

// HTTP2ClientTest — 루프백 망에서 HTTP/2 클라이언트의 연결 수명. 핸드셰이크에 답하지 않는 상대의 연결을 버리고 다음 요청이 새 연결을 맺는다(nogpu).

using namespace sw;

namespace
{
    /** @brief 핸드셰이크를 끝내지 못하는 TLS 세션입니다. 첫 레코드 한 바이트만 보냅니다. */
    class StalledTLSSession final : public ITLSSession
    {
    public:
        TLSSessionState getState() const override { return TLSSessionState::Handshaking; }
        bool            feedCiphertext( const uint8* /*pData*/, int32 /*size*/ ) override { return true; }
        bool            readPlaintext( vector<uint8>& /*outBytes*/ ) override { return true; }
        bool            writePlaintext( const uint8* /*pData*/, int32 /*size*/ ) override { return true; }
        void            takeCiphertext( vector<uint8>& outBytes ) override
        {
            if ( _bHelloSent == SW_FALSE )
                outBytes.push_back( 0x16 );
            _bHelloSent = SW_TRUE;
        }
        void        close() override {}
        const utf8* getFailureText() const override { return ""; }
        string      getNegotiatedProtocol() const override { return string{}; }

    private:
        uint8 _bHelloSent{ SW_FALSE };
    };

    class StalledTLSContext final : public ITLSContext
    {
    public:
        unique_ptr<ITLSSession> createSession() override { return make_unique<StalledTLSSession>(); }
        TLSRole                 getRole() const override { return TLSRole::Client; }
    };

    /** @brief 연결은 받지만 아무 답도 하지 않는 서버입니다. */
    class SilentServer final : public IStreamHandler
    {
    public:
        static constexpr uint16 kPort = 8443;

        unique_ptr<IStreamTransport> _transport{};
        int32                        _openedCount{ 0 };
        int32                        _closedCount{ 0 };

        void start( LoopbackStreamNetwork& network )
        {
            StreamTransportConfig settings;
            settings._ioThreadCount = 0;
            _transport              = network.createTransport();
            SW_REQUIRE_TRUE( _transport->initialize( this, settings ) );
            SW_REQUIRE_TRUE( _transport->listen( NetAddress::makeLoopback( kPort ) ) );
        }

        void onStreamOpened( StreamConnectionHandle /*handle*/, const NetAddress& /*remote*/, bool /*bAccepted*/ ) override { ++_openedCount; }
        void onStreamReceived( StreamConnectionHandle /*handle*/, const uint8* /*pData*/, int32 /*size*/ ) override {}
        void onStreamClosed( StreamConnectionHandle /*handle*/, StreamCloseReason /*reason*/ ) override { ++_closedCount; }
    };
} // namespace

/**
 * @brief [HTTP2ClientTest] 핸드셰이크에 답하지 않는 연결은 기다리던 요청이 시한을 넘기면 닫히고, 다음 요청은 새 연결을 맺는다(결함: 멎은 연결이 남아 그 호스트의 뒤 요청이 모두 같은 연결에서 시한을 넘겼다)
 */
SW_TEST_CASE( HTTP2ClientTest, StalledHandshakeIsDroppedAfterItsRequestsTimeOut )
{
    LoopbackStreamNetwork network;
    ManualClock           clock{ MonotonicMilliseconds{ 1000 } };
    SilentServer          server;
    server.start( network );
    StalledTLSContext     tlsContext;
    HTTP2Client           client{ clock };
    StreamTransportConfig settings;
    settings._ioThreadCount = 0;
    SW_REQUIRE_TRUE( client.initialize( network.createTransport(), settings, HTTPClientConfig{} ) );
    client.registerTLSContext( "127.0.0.1", &tlsContext );
    const auto poll = [&client, &server]()
    {
        for ( int32 round = 0; round < 4; ++round )
        {
            client.update();
            (void)server._transport->pollIO( 0 );
        }
    };

    HTTPClientRequest request;
    request._url         = "https://127.0.0.1:8443/push";
    request._timeoutMs   = 100;
    const uint64 firstID = client.submitRequest( request );
    poll();
    SW_EXPECT_EQUAL( 1, server._openedCount );
    SW_EXPECT_EQUAL( 1, client.getPendingCount() );

    clock.advanceMilliseconds( 101 );
    poll();
    vector<HTTPClientResponse> listResponse;
    SW_REQUIRE_EQUAL( 1, client.pollResponses( listResponse ) );
    SW_EXPECT_EQUAL( firstID, listResponse[0]._requestID );
    SW_EXPECT_TRUE( listResponse[0].getKind() == HTTPResponseKind::TransportFailed );
    SW_EXPECT_EQUAL( 0, client.getSessionCount() );
    SW_EXPECT_EQUAL( 1, server._closedCount );

    (void)client.submitRequest( request );
    poll();
    SW_EXPECT_EQUAL( 2, server._openedCount );
    client.shutdown();
    server._transport->shutdown();
}
