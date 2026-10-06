#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/NetTransport.h"

#include "Engine/Network/EngineNetSecurity.h"

#include "TestFramework/TestFramework.h"

#include <algorithm>
#include <cstring>

// UDP 보안 — 암호화된 연결로 메시지가 흐르고 선 위에 평문이 없다, 변조 · 재전송 패킷은 버리고 센다(연결은 산다), 모르는 토큰 · 틀린 비밀은
// AuthenticationFailed(서버는 자리를 잡지 않는다), 한쪽만 암호화면 SecurityMismatch, 인증기 없는 일회 키 모드는 개발 빌드에서만 연결된다.
// 암호 제공자가 Engine 의 OpenSSL 이라 EngineTest.

using namespace sw;

namespace
{
    /** @brief 로그인 키트 흉내 — 토큰 "token1" → 세션 비밀(0x5A 32 바이트) · 주체 42. */
    class TestAuthenticator final : public INetConnectAuthenticator
    {
    public:
        bool findSessionSecret( const uint8* pToken, int32 tokenSize, NetSessionSecret& outSecret, uint64& outPrincipalId ) override
        {
            if ( tokenSize != 6 || std::memcmp( pToken, "token1", 6 ) != 0 )
                return false;
            std::memset( outSecret._arrByte, 0x5A, sizeof( outSecret._arrByte ) );
            outPrincipalId = 42;
            return true;
        }
    };

    NetConnectCredentials makeCredentials( const utf8* pToken, uint8 secretByte )
    {
        NetConnectCredentials credentials;
        credentials._tokenSize = static_cast<int32>( std::strlen( pToken ) );
        std::memcpy( credentials._arrToken, pToken, static_cast<size_t>( credentials._tokenSize ) );
        std::memset( credentials._secret._arrByte, secretByte, sizeof( credentials._secret._arrByte ) );
        return credentials;
    }

    /**
     * @brief 안의 전송으로 넘기며 보낸 데이터그램을 모은다. `_bTamperNext` 면 다음 데이터 패킷의 암호문 한 비트를 뒤집고 체크섬을 다시 맞춘다
     *        (체크섬이 아니라 AEAD 가 거르는지 본다).
     */
    class TapTransport final : public INetTransport
    {
    public:
        explicit TapTransport( INetTransport* pInner )
            : _listSent{}
            , _lastTo{}
            , _bTamperNext{ SW_FALSE }
            , _pInner{ pInner }
        {
        }

        bool send( const NetAddress& to, const uint8* pData, int32 size ) override
        {
            vector<uint8> bytes( pData, pData + size );
            if ( _bTamperNext == SW_TRUE && NetHost::peekPacketType( pData, size ) == NetHost::PacketType::Payload && size > 40 )
            {
                _bTamperNext = SW_FALSE;
                bytes[static_cast<size_t>( size - 20 )] ^= 0x01;
                const uint32 headerId = static_cast<uint32>( pData[0] ) | ( static_cast<uint32>( pData[1] ) << 8 ) | ( static_cast<uint32>( pData[2] ) << 16 ) |
                                        ( static_cast<uint32>( pData[3] ) << 24 );
                const uint32 checksum = NetHost::computePacketChecksum( headerId, bytes.data() + 8, size - 8 );
                for ( int32 index = 0; index < 4; ++index )
                    bytes[static_cast<size_t>( 4 + index )] = static_cast<uint8>( checksum >> ( index * 8 ) );
            }
            _listSent.push_back( bytes );
            _lastTo = to;
            return _pInner->send( to, bytes.data(), size );
        }
        bool       receive( NetAddress& outFrom, vector<uint8>& outBuffer ) override { return _pInner->receive( outFrom, outBuffer ); }
        NetAddress getLocalAddress() const override { return _pInner->getLocalAddress(); }
        void       update( float64 time ) override { _pInner->update( time ); }
        bool       waitForReceive( float64 timeoutSeconds ) override { return _pInner->waitForReceive( timeoutSeconds ); }

        /** @brief 모은 것 하나를 그대로 다시 보낸다(재전송 공격). */
        void replay( size_t index ) { (void)_pInner->send( _lastTo, _listSent[index].data(), static_cast<int32>( _listSent[index].size() ) ); }

        vector<vector<uint8>> _listSent;
        NetAddress            _lastTo;
        uint8                 _bTamperNext;

    private:
        INetTransport* _pInner;
    };

    /** @brief 루프백 망 위의 서버 · 클라이언트 한 쌍 — 클라이언트 전송은 엿보기로 감쌌다. */
    struct SecurePair
    {
        LoopbackNetwork   _network{};
        TestAuthenticator _authenticator{};
        TapTransport      _clientTap;
        NetHost           _server{};
        NetHost           _client{};
        float64           _time{ 0.0 };
        bool              _bListening{ false };

        SecurePair( NetSecurityMode serverMode, NetSecurityMode clientMode, bool bAuthenticator )
            : _clientTap{ _network.createEndpoint( 5000 ) }
        {
            NetHostSettings serverSettings;
            serverSettings._saltSeed                 = 11u;
            serverSettings._security._mode           = serverMode;
            serverSettings._security._pProvider      = &EngineNetSecurity::getProvider();
            serverSettings._security._pAuthenticator = bAuthenticator ? &_authenticator : nullptr;
            NetHostSettings clientSettings;
            clientSettings._saltSeed            = 12u;
            clientSettings._security._mode      = clientMode;
            clientSettings._security._pProvider = &EngineNetSecurity::getProvider();
            _server.initialize( _network.createEndpoint( 4000 ), serverSettings );
            _client.initialize( &_clientTap, clientSettings );
            _bListening = _server.listen();
        }

        void run( float64 seconds )
        {
            for ( float64 elapsed = 0.0; elapsed < seconds; elapsed += 1.0 / 60.0 )
            {
                _time += 1.0 / 60.0;
                _server.update( _time );
                _client.update( _time );
            }
        }
    };

    bool containsBytes( const vector<vector<uint8>>& listDatagram, const utf8* pMarker )
    {
        const size_t markerSize = std::strlen( pMarker );
        for ( const vector<uint8>& datagram : listDatagram )
        {
            if ( std::search( datagram.begin(), datagram.end(), pMarker, pMarker + markerSize ) != datagram.end() )
                return true;
        }
        return false;
    }

    int32 countConnectedEvents( NetHost& host )
    {
        vector<NetHostEvent> listEvent;
        host.drainEvents( listEvent );
        return static_cast<int32>(
            std::count_if( listEvent.begin(), listEvent.end(), []( const NetHostEvent& event )
        { return event._kind == NetHostEvent::Kind::Connected; } ) );
    }
} // namespace

SW_TEST_CASE( NetSecureHostTest, TokenBoundConnectionCarriesMessagesWithoutPlaintextOnTheWire )
{
    SecurePair pair( NetSecurityMode::Encrypted, NetSecurityMode::Encrypted, true );
    SW_ASSERT_TRUE( pair._bListening );
    SW_ASSERT_TRUE( pair._client.connect( NetAddress::makeLoopback( 4000 ), makeCredentials( "token1", 0x5A ) ) );
    pair.run( 0.5 );
    SW_ASSERT_EQUAL( 1, pair._server.getConnectedCount() );
    SW_EXPECT_EQUAL( 42ull, pair._server.getConnectionPrincipal( 0 ) );
    const utf8* pMarker = "SW-SECRET-MARKER";
    SW_ASSERT_TRUE( pair._client.sendMessage( 0, NetChannelType::ReliableOrdered, reinterpret_cast<const uint8*>( pMarker ), 16 ) );
    pair.run( 0.3 );
    int32          connectionId = -1;
    NetChannelType channel      = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    SW_ASSERT_TRUE( pair._server.receiveMessage( connectionId, channel, buffer ) );
    SW_ASSERT_EQUAL( 16, static_cast<int32>( buffer.size() ) );
    SW_EXPECT_TRUE( std::memcmp( buffer.data(), pMarker, 16 ) == 0 );
    SW_EXPECT_FALSE( containsBytes( pair._clientTap._listSent, pMarker ) );
    SW_EXPECT_EQUAL( 0ull, pair._server.getAuthenticationFailureCount() );

    // 끊기도 봉인이다 — 서버가 Remote 로 안다.
    pair._client.disconnect( 0 );
    pair.run( 0.1 );
    SW_EXPECT_EQUAL( 0, pair._server.getConnectedCount() );
}

SW_TEST_CASE( NetSecureHostTest, TamperedAndReplayedPacketsAreDroppedAndCounted )
{
    SecurePair pair( NetSecurityMode::Encrypted, NetSecurityMode::Encrypted, true );
    SW_ASSERT_TRUE( pair._client.connect( NetAddress::makeLoopback( 4000 ), makeCredentials( "token1", 0x5A ) ) );
    pair.run( 0.5 );
    SW_ASSERT_EQUAL( 1, pair._server.getConnectedCount() );

    // 변조 — 비신뢰 메시지 하나가 실린 다음 데이터 패킷의 암호문 한 비트(체크섬은 다시 맞췄다).
    uint8 arrFirst[24]           = { 0x81, 1 };
    pair._clientTap._bTamperNext = SW_TRUE;
    SW_ASSERT_TRUE( pair._client.sendMessage( 0, NetChannelType::Unreliable, arrFirst, 24 ) );
    pair.run( 0.1 );
    SW_EXPECT_FALSE( pair._clientTap._bTamperNext == SW_TRUE ); // 실제로 하나를 뒤집었다
    int32          connectionId = -1;
    NetChannelType channel      = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    SW_EXPECT_FALSE( pair._server.receiveMessage( connectionId, channel, buffer ) );
    SW_EXPECT_TRUE( pair._server.getAuthenticationFailureCount() >= 1 );

    // 연결은 산다 — 다음 메시지는 온다. 그 데이터그램을 그대로 다시 보내면 재전송 창이 버린다(두 번 받지 않는다).
    uint8        arrSecond[24] = { 0x81, 2 };
    const size_t sentBefore    = pair._clientTap._listSent.size();
    SW_ASSERT_TRUE( pair._client.sendMessage( 0, NetChannelType::Unreliable, arrSecond, 24 ) );
    pair.run( 0.1 );
    SW_ASSERT_TRUE( pair._server.receiveMessage( connectionId, channel, buffer ) );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( buffer[1] ) );
    SW_EXPECT_FALSE( pair._server.receiveMessage( connectionId, channel, buffer ) );
    for ( size_t index = sentBefore; index < pair._clientTap._listSent.size(); ++index )
        pair._clientTap.replay( index );
    pair.run( 0.1 );
    SW_EXPECT_FALSE( pair._server.receiveMessage( connectionId, channel, buffer ) );
    SW_EXPECT_TRUE( pair._server.getReplayRejectedCount() >= 1 );
    SW_EXPECT_EQUAL( 1, pair._server.getConnectedCount() );
}

SW_TEST_CASE( NetSecureHostTest, UnknownTokenAndWrongSecretAreRefused )
{
    for ( const NetConnectCredentials& credentials : { makeCredentials( "token9", 0x5A ), makeCredentials( "token1", 0x11 ) } )
    {
        SecurePair                   pair( NetSecurityMode::Encrypted, NetSecurityMode::Encrypted, true );
        TaskFuture<NetConnectResult> future = pair._client.connectAsync( NetAddress::makeLoopback( 4000 ), credentials );
        {
            SW_TEST_DEFENSIVE_SCOPE( "the client logs the refused connection" );
            pair.run( 1.0 );
        }
        SW_ASSERT_TRUE( future.isReady() );
        SW_EXPECT_TRUE( future.get()._reason == NetDisconnectReason::AuthenticationFailed );
        SW_EXPECT_EQUAL( 0, countConnectedEvents( pair._server ) ); // 증명이 틀리면 자리를 한 번도 잡지 않는다
        SW_EXPECT_TRUE( pair._server.getAuthenticationFailureCount() >= 1 );
    }
}

SW_TEST_CASE( NetSecureHostTest, OneSidedEncryptionIsRefusedAsSecurityMismatch )
{
    SecurePair                   pair( NetSecurityMode::Encrypted, NetSecurityMode::Off, true );
    TaskFuture<NetConnectResult> future = pair._client.connectAsync( NetAddress::makeLoopback( 4000 ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "both hosts log the refused protocol" );
        pair.run( 1.0 );
    }
    SW_ASSERT_TRUE( future.isReady() );
    SW_EXPECT_TRUE( future.get()._reason == NetDisconnectReason::SecurityMismatch );
    SW_EXPECT_EQUAL( 0, pair._server.getConnectedCount() );
}

SW_TEST_CASE( NetSecureHostTest, EphemeralModeWithoutAuthenticatorIsDevelopmentOnly )
{
    SecurePair pair( NetSecurityMode::Encrypted, NetSecurityMode::Encrypted, false );
#if defined( SW_SHIPPING )
    // Shipping 서버는 인증기 없이 암호화로 listen 하지 못한다(일회 키는 중간자를 못 막는다).
    SW_EXPECT_FALSE( pair._bListening );
#else
    SW_ASSERT_TRUE( pair._bListening );
    SW_ASSERT_TRUE( pair._client.connect( NetAddress::makeLoopback( 4000 ) ) );
    pair.run( 0.5 );
    SW_EXPECT_EQUAL( 1, pair._server.getConnectedCount() );
    SW_EXPECT_EQUAL( 0ull, pair._server.getConnectionPrincipal( 0 ) );
    const uint8 arrMessage[8] = { 0x81, 7 };
    SW_ASSERT_TRUE( pair._client.sendMessage( 0, NetChannelType::ReliableOrdered, arrMessage, 8 ) );
    pair.run( 0.3 );
    int32          connectionId = -1;
    NetChannelType channel      = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    SW_ASSERT_TRUE( pair._server.receiveMessage( connectionId, channel, buffer ) );
    SW_EXPECT_EQUAL( 7, static_cast<int32>( buffer[1] ) );
#endif
}
