#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Network/Message/StreamMessageEndpoint.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "GameFramework/Base/Online/Security/NetSecurity.h"

#include "TestFramework/TestFramework.h"
#include "TestFramework/TestStreamEndpointPair.h"

#include <algorithm>
#include <cstring>

// 스트림 TLS — 핸드셰이크 뒤에 열리고 선 위에는 평문이 없다, 변조된 레코드 · 믿지 않는 서버 · TLS 1.2 클라이언트 · 평문 클라이언트는 열림 없이 SecurityFailure 로 끊긴다.
// 끝점 짝은 루프백(전송 무관 — 두 플랫폼 같은 시험), 암호 제공자는 GameFramework 의 OpenSSL 이라 GameFramework 를 링크하는 EngineTest.

using namespace sw;

namespace
{
    /** @brief 안의 전송으로 넘기며 보낸 바이트를 모으고, 지정한 번째 send 의 끝쪽 한 바이트를 뒤집는다(변조). 나머지는 그대로 넘긴다. */
    class TapStreamTransport final : public IStreamTransport
    {
    public:
        explicit TapStreamTransport( unique_ptr<IStreamTransport> inner )
            : _sentBytes{}
            , _tamperSendIndex{ -1 }
            , _sendCount{ 0 }
            , _inner{ std::move( inner ) }
        {
        }

        bool                   initialize( IStreamHandler* pHandler, const StreamTransportSettings& settings ) override { return _inner->initialize( pHandler, settings ); }
        void                   shutdown() override { _inner->shutdown(); }
        bool                   listen( const NetAddress& bindAddress ) override { return _inner->listen( bindAddress ); }
        uint16                 getListenPort() const override { return _inner->getListenPort(); }
        StreamConnectionHandle connect( const NetAddress& remote ) override { return _inner->connect( remote ); }
        StreamSendResult       send( StreamConnectionHandle handle, const uint8* pData, int32 size ) override
        {
            vector<uint8> bytes( pData, pData + size );
            if ( ++_sendCount == _tamperSendIndex && size > 10 )
                bytes[static_cast<size_t>( size - 5 )] ^= 0x20; // 레코드 끝쪽(태그 안) 한 비트
            _sentBytes.insert( _sentBytes.end(), bytes.begin(), bytes.end() );
            return _inner->send( handle, bytes.data(), size );
        }
        void                 close( StreamConnectionHandle handle, StreamCloseMode mode ) override { _inner->close( handle, mode ); }
        void                 setReceivePaused( StreamConnectionHandle handle, bool bPaused ) override { _inner->setReceivePaused( handle, bPaused ); }
        int32                pollIO( int32 timeoutMilli ) override { return _inner->pollIO( timeoutMilli ); }
        NetAddress           getRemoteAddress( StreamConnectionHandle handle ) const override { return _inner->getRemoteAddress( handle ); }
        StreamTransportStats getStats() const override { return _inner->getStats(); }

        vector<uint8> _sentBytes;
        int32         _tamperSendIndex; ///< 1 부터 센 send 번째 — 음수면 뒤집지 않는다
        int32         _sendCount;

    private:
        unique_ptr<IStreamTransport> _inner;
    };

    unique_ptr<IStreamTransport> wrapWithTap( unique_ptr<IStreamTransport> inner ) { return sw::make_unique<TapStreamTransport>( std::move( inner ) ); }

    struct EndpointRecord final : public IStreamEndpointListener
    {
        vector<StreamConnectionHandle> _listOpened{};
        vector<vector<uint8>>          _listMessage{};
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
            (void)kind;
            _listMessage.emplace_back( pBody, pBody + bodySize );
        }
        void onEndpointClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
        {
            (void)handle;
            _listClosedReason.push_back( reason );
        }
    };

    /** @brief 서버 컨텍스트(자체 서명 인증서)와 그 인증서를 믿는 클라이언트 컨텍스트입니다. @p bTrustStranger 면 클라이언트는 다른 인증서를 믿는다. */
    struct TLSContexts
    {
        unique_ptr<ITLSContext> _server{};
        unique_ptr<ITLSContext> _client{};
    };

    bool makeTLSContexts( TLSVersion clientMaxVersion, bool bTrustStranger, TLSContexts& outContexts )
    {
        INetSecurityProvider& provider = NetSecurity::getProvider();
        string                certificatePem;
        string                privateKeyPem;
        if ( provider.createSelfSignedCertificate( "localhost", 30, certificatePem, privateKeyPem ) == false )
            return false;
        string trustPem = certificatePem;
        if ( bTrustStranger )
        {
            string strangerKeyPem;
            if ( provider.createSelfSignedCertificate( "localhost", 30, trustPem, strangerKeyPem ) == false )
                return false;
        }
        string             error;
        TLSContextSettings server;
        server._role           = TLSRole::Server;
        server._certificatePem = certificatePem;
        server._privateKeyPem  = privateKeyPem;
        outContexts._server    = provider.createTLSContext( server, error );
        TLSContextSettings client;
        client._role        = TLSRole::Client;
        client._trustPem    = trustPem;
        client._serverName  = "localhost";
        client._minVersion  = TLSVersion::TLS12;
        client._maxVersion  = clientMaxVersion;
        outContexts._client = provider.createTLSContext( client, error );
        return outContexts._server != nullptr && outContexts._client != nullptr;
    }

    StreamEndpointSettings makeSettings( ITLSContext* pContext )
    {
        StreamEndpointSettings settings;
        settings._security._pTLSContext = pContext;
        return settings;
    }

    bool containsMarker( const vector<uint8>& bytes, const utf8* pMarker )
    {
        const size_t markerSize = std::strlen( pMarker );
        return std::search( bytes.begin(), bytes.end(), pMarker, pMarker + markerSize ) != bytes.end();
    }
} // namespace

SW_TEST_CASE( StreamTLSTest, FramesFlowAfterHandshakeAndWireIsCiphertext )
{
    TLSContexts contexts;
    SW_ASSERT_TRUE( makeTLSContexts( TLSVersion::TLS13, false, contexts ) );
    EndpointRecord           serverRecord;
    EndpointRecord           clientRecord;
    test::StreamEndpointPair pair( serverRecord, clientRecord, makeSettings( contexts._server.get() ), makeSettings( contexts._client.get() ), LoopbackStreamConditions{},
                                   &wrapWithTap );
    const utf8*              pMarker = "SW-PLAINTEXT-MARKER";
    const int32              size    = static_cast<int32>( std::strlen( pMarker ) );

    // 전송이 연결을 알리자마자(핸드셰이크 중) 하나 — 세션이 모았다가 핸드셰이크 뒤에 보낸다.
    bool bSentDuringHandshake = false;
    for ( int32 step = 0; step < 10 && bSentDuringHandshake == false; ++step )
    {
        bSentDuringHandshake = pair._client.sendMessage( pair._clientHandle, reinterpret_cast<const uint8*>( pMarker ), size ) != StreamSendResult::Closed;
        if ( bSentDuringHandshake == false )
            pair.step( 1 );
    }
    SW_ASSERT_TRUE( bSentDuringHandshake );
    SW_EXPECT_TRUE( clientRecord._listOpened.empty() ); // 아직 핸드셰이크 중
    pair.step( 20 );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( serverRecord._listOpened.size() ) );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( clientRecord._listOpened.size() ) );

    const uint8 arrSecond[4] = { 1, 2, 3, 4 };
    SW_EXPECT_TRUE( pair._client.sendMessage( pair._clientHandle, arrSecond, 4 ) != StreamSendResult::Closed );
    pair.step( 20 );
    SW_ASSERT_EQUAL( 2, static_cast<int32>( serverRecord._listMessage.size() ) );
    SW_EXPECT_TRUE( serverRecord._listMessage[0] == vector<uint8>( reinterpret_cast<const uint8*>( pMarker ), reinterpret_cast<const uint8*>( pMarker + size ) ) );
    SW_EXPECT_EQUAL( 4, static_cast<int32>( serverRecord._listMessage[1].size() ) );

    const TapStreamTransport& tap = static_cast<const TapStreamTransport&>( *pair._clientTransport );
    SW_EXPECT_TRUE( tap._sentBytes.empty() == false );
    SW_EXPECT_FALSE( containsMarker( tap._sentBytes, pMarker ) );

    // 우아한 종료 — close_notify 를 먼저 보내고 닫는다.
    pair._client.close( pair._clientHandle, StreamCloseMode::Graceful );
    pair.step( 20 );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( clientRecord._listClosedReason.size() ) );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( serverRecord._listClosedReason.size() ) );
    SW_EXPECT_TRUE( clientRecord._listClosedReason[0] == StreamCloseReason::LocalClose );
    SW_EXPECT_TRUE( serverRecord._listClosedReason[0] == StreamCloseReason::RemoteClose || serverRecord._listClosedReason[0] == StreamCloseReason::LocalClose );
}

SW_TEST_CASE( StreamTLSTest, TamperedRecordClosesWithSecurityFailure )
{
    TLSContexts contexts;
    SW_ASSERT_TRUE( makeTLSContexts( TLSVersion::TLS13, false, contexts ) );
    EndpointRecord           serverRecord;
    EndpointRecord           clientRecord;
    test::StreamEndpointPair pair( serverRecord, clientRecord, makeSettings( contexts._server.get() ), makeSettings( contexts._client.get() ), LoopbackStreamConditions{},
                                   &wrapWithTap );
    pair.step( 20 );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( serverRecord._listOpened.size() ) );

    TapStreamTransport& tap = static_cast<TapStreamTransport&>( *pair._clientTransport );
    tap._tamperSendIndex    = tap._sendCount + 1;
    const uint8 arrBody[32] = { 7 };
    SW_EXPECT_TRUE( pair._client.sendMessage( pair._clientHandle, arrBody, 32 ) != StreamSendResult::Closed );
    pair.step( 20 );
    SW_EXPECT_TRUE( serverRecord._listMessage.empty() );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( serverRecord._listClosedReason.size() ) );
    SW_EXPECT_TRUE( serverRecord._listClosedReason[0] == StreamCloseReason::SecurityFailure );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( clientRecord._listClosedReason.size() ) );
}

SW_TEST_CASE( StreamTLSTest, UntrustedServerNeverOpens )
{
    TLSContexts contexts;
    SW_ASSERT_TRUE( makeTLSContexts( TLSVersion::TLS13, true, contexts ) );
    EndpointRecord           serverRecord;
    EndpointRecord           clientRecord;
    test::StreamEndpointPair pair( serverRecord, clientRecord, makeSettings( contexts._server.get() ), makeSettings( contexts._client.get() ), LoopbackStreamConditions{},
                                   nullptr );
    pair.step( 20 );
    SW_EXPECT_TRUE( clientRecord._listOpened.empty() );
    SW_EXPECT_TRUE( serverRecord._listOpened.empty() );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( clientRecord._listClosedReason.size() ) );
    SW_EXPECT_TRUE( clientRecord._listClosedReason[0] == StreamCloseReason::SecurityFailure );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( serverRecord._listClosedReason.size() ) );
}

SW_TEST_CASE( StreamTLSTest, TLS12ClientNeverOpens )
{
    TLSContexts contexts;
    SW_ASSERT_TRUE( makeTLSContexts( TLSVersion::TLS12, false, contexts ) );
    EndpointRecord           serverRecord;
    EndpointRecord           clientRecord;
    test::StreamEndpointPair pair( serverRecord, clientRecord, makeSettings( contexts._server.get() ), makeSettings( contexts._client.get() ), LoopbackStreamConditions{},
                                   nullptr );
    pair.step( 20 );
    SW_EXPECT_TRUE( serverRecord._listOpened.empty() );
    SW_EXPECT_TRUE( clientRecord._listOpened.empty() );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( serverRecord._listClosedReason.size() ) );
    SW_EXPECT_TRUE( serverRecord._listClosedReason[0] == StreamCloseReason::SecurityFailure );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( clientRecord._listClosedReason.size() ) );
}

SW_TEST_CASE( StreamTLSTest, PlaintextClientAgainstTLSServerIsRejected )
{
    TLSContexts contexts;
    SW_ASSERT_TRUE( makeTLSContexts( TLSVersion::TLS13, false, contexts ) );
    EndpointRecord           serverRecord;
    EndpointRecord           clientRecord;
    test::StreamEndpointPair pair( serverRecord, clientRecord, makeSettings( contexts._server.get() ), makeSettings( nullptr ), LoopbackStreamConditions{}, nullptr );
    pair.step( 2 );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( clientRecord._listOpened.size() ) ); // 평문 클라이언트는 바로 열린다
    const uint8 arrBody[64] = { 9 };
    (void)pair._client.sendMessage( pair._clientHandle, arrBody, 64 );
    pair.step( 20 );
    SW_EXPECT_TRUE( serverRecord._listOpened.empty() );
    SW_EXPECT_TRUE( serverRecord._listMessage.empty() ); // 평문 프레임이 처리기에 닿지 않는다
    SW_ASSERT_EQUAL( 1, static_cast<int32>( serverRecord._listClosedReason.size() ) );
    SW_EXPECT_TRUE( serverRecord._listClosedReason[0] == StreamCloseReason::SecurityFailure );
}
