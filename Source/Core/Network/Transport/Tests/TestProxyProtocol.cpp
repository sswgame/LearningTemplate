#include "pch.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Tests/LoopbackStreamTransport.h"
#include "Core/Network/Transport/ProxyProtocolAcceptTransport.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

// PROXY protocol v2(감사 O-17) — 해석기: 잘린 머리는 기다리고, 거짓 길이 · 틀린 서명 · 판 · 주소족은 거절한다, 머리 뒤 바이트는 스트림이다.
// 장식자: 신뢰 프록시의 연결은 머리의 실제 주소로 열고, 신뢰 밖 출처는 위로 알리지 않고 닫는다. 루프백 망(I/O 스레드 없이 pollIO — 결정적)에서 돈다.

using namespace sw;

namespace
{
    constexpr NetAddress kClient = NetAddress::make( 203, 0, 113, 7, 40000 );
    constexpr NetAddress kServer = NetAddress::make( 10, 0, 0, 5, 7100 );

    vector<uint8> makeHeader()
    {
        vector<uint8> bytes;
        ProxyHeader::appendIpv4( bytes, kClient, kServer );
        return bytes;
    }

    class ProxyRecorder final : public IStreamHandler
    {
    public:
        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
        {
            ScopedLock<mutex> lock{ _mutex };
            (void)handle;
            (void)bAccepted;
            _listRemote.push_back( remote );
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

        vector<NetAddress> copyRemoteList() const
        {
            ScopedLock<mutex> lock{ _mutex };
            return _listRemote;
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
        mutable mutex      _mutex;
        vector<NetAddress> _listRemote;
        vector<uint8>      _received;
        int32              _closedCount{ 0 };
    };

    /** @brief 장식자 서버 하나와 감싸지 않은 클라이언트(프록시 흉내) 하나입니다. */
    struct ProxyRig
    {
        ProxyRecorder                     _serverRecorder{};
        ProxyRecorder                     _clientRecorder{};
        unique_ptr<LoopbackStreamNetwork> _network{ make_unique<LoopbackStreamNetwork>( 13u ) };
        unique_ptr<IStreamTransport>      _server{};
        unique_ptr<IStreamTransport>      _client{};
        StreamConnectionHandle            _clientHandle{};

        bool start( vector<uint32> listTrustedProxy )
        {
            _server                        = make_unique<ProxyProtocolAcceptTransport>( _network->createTransport(), std::move( listTrustedProxy ) );
            _client                        = _network->createTransport();
            StreamTransportConfig settings = {};
            settings._ioThreadCount        = 0;
            if ( _server->initialize( &_serverRecorder, settings ) == false || _client->initialize( &_clientRecorder, settings ) == false )
                return false;
            if ( _server->listen( NetAddress::makeLoopback( 0 ) ) == false )
                return false;
            _clientHandle = _client->connect( NetAddress::makeLoopback( _server->getListenPort() ) );
            return true;
        }

        template <typename TPredicate>
        bool runUntil( TPredicate&& predicate )
        {
            const Deadline deadline = Deadline::afterMilliseconds( 10000 ); // 멈춘 실행을 잡는 그물 — 판정은 조건이다(느린 구성 · 동시 실행에도 넉넉하게)
            while ( predicate() == false )
            {
                if ( deadline.isExpired() )
                    return false;
                (void)_server->pollIO( 0 );
                (void)_client->pollIO( 0 );
            }
            return true;
        }

        void send( const vector<uint8>& bytes ) { (void)_client->send( _clientHandle, bytes.data(), static_cast<int32>( bytes.size() ) ); }
    };
} // namespace

/** @brief [ProxyProtocolTest] 잘린 머리는 서명이 맞는 동안 NeedMore 이고, 다 오면 출처 주소와 머리 길이를 준다(뒤 바이트는 스트림이다) */
SW_TEST_CASE( ProxyProtocolTest, TruncatedHeaderWaitsThenParses )
{
    vector<uint8> bytes = makeHeader();
    bytes.push_back( 0x16 ); // 머리 뒤 TLS 레코드의 첫 바이트
    ProxyHeader header;
    for ( int32 size = 0; size < ProxyHeader::kFixedSize + ProxyHeader::kIpv4AddressSize; ++size )
    {
        SW_EXPECT_TRUE( ProxyHeader::parse( bytes.data(), size, header ) == ProxyHeaderResult::NeedMore );
    }
    SW_REQUIRE_TRUE( ProxyHeader::parse( bytes.data(), static_cast<int32>( bytes.size() ), header ) == ProxyHeaderResult::Parsed );
    SW_EXPECT_TRUE( header._source == kClient );
    SW_EXPECT_EQUAL( ProxyHeader::kFixedSize + ProxyHeader::kIpv4AddressSize, header._headerSize );
    SW_EXPECT_FALSE( header._bLocal );
}

/** @brief [ProxyProtocolTest] 거짓 길이(상한 위 · 주소보다 짧음) · 틀린 서명 · 판 · 명령 · 주소족(IPv6 · UDP)은 Invalid 다 */
SW_TEST_CASE( ProxyProtocolTest, MalformedHeadersAreInvalid )
{
    ProxyHeader header;
    {
        vector<uint8> bytes = makeHeader();
        bytes[14]           = 0xFF; // 길이 65 KB — 상한 위
        bytes[15]           = 0xFF;
        SW_EXPECT_TRUE( ProxyHeader::parse( bytes.data(), static_cast<int32>( bytes.size() ), header ) == ProxyHeaderResult::Invalid );
    }
    {
        vector<uint8> bytes = makeHeader();
        bytes[15]           = 4; // 주소 블록 12 B 보다 짧다
        SW_EXPECT_TRUE( ProxyHeader::parse( bytes.data(), static_cast<int32>( bytes.size() ), header ) == ProxyHeaderResult::Invalid );
    }
    {
        vector<uint8> bytes = makeHeader();
        bytes[0]            = 'G'; // `GET ` — 머리가 아닌 스트림은 첫 바이트에서 거절한다
        SW_EXPECT_TRUE( ProxyHeader::parse( bytes.data(), 1, header ) == ProxyHeaderResult::Invalid );
    }
    {
        vector<uint8> bytes = makeHeader();
        bytes[12]           = 0x11; // 판 1
        SW_EXPECT_TRUE( ProxyHeader::parse( bytes.data(), static_cast<int32>( bytes.size() ), header ) == ProxyHeaderResult::Invalid );
    }
    {
        vector<uint8> bytes = makeHeader();
        bytes[12]           = 0x2F; // 모르는 명령
        SW_EXPECT_TRUE( ProxyHeader::parse( bytes.data(), static_cast<int32>( bytes.size() ), header ) == ProxyHeaderResult::Invalid );
    }
    {
        vector<uint8> bytes = makeHeader();
        bytes[13]           = 0x21; // IPv6
        SW_EXPECT_TRUE( ProxyHeader::parse( bytes.data(), static_cast<int32>( bytes.size() ), header ) == ProxyHeaderResult::Invalid );
        bytes[13] = 0x12; // UDP
        SW_EXPECT_TRUE( ProxyHeader::parse( bytes.data(), static_cast<int32>( bytes.size() ), header ) == ProxyHeaderResult::Invalid );
    }
    {
        vector<uint8> bytes = makeHeader();
        bytes[12]           = 0x20; // LOCAL — 주소를 보지 않는다(프록시의 건강 검사)
        SW_REQUIRE_TRUE( ProxyHeader::parse( bytes.data(), static_cast<int32>( bytes.size() ), header ) == ProxyHeaderResult::Parsed );
        SW_EXPECT_TRUE( header._bLocal );
    }
    // 아무 바이트로 흔들어도 머리 길이는 상한 안이거나 거절이다(주소 밖을 읽지 않는다)
    vector<uint8> fuzz = makeHeader();
    for ( int32 index = 0; index < 4096; ++index )
    {
        fuzz[static_cast<size_t>( 12 + index % 8 )] = static_cast<uint8>( index * 37 );
        const ProxyHeaderResult parsed              = ProxyHeader::parse( fuzz.data(), static_cast<int32>( fuzz.size() ), header );
        SW_EXPECT_TRUE( parsed != ProxyHeaderResult::Parsed || header._headerSize <= static_cast<int32>( fuzz.size() ) );
    }
}

/** @brief [ProxyProtocolAcceptTransportTest] 신뢰 프록시의 연결은 머리를 다 받은 뒤 실제 클라이언트 주소로 열리고, 머리 뒤 바이트만 위로 간다 */
SW_TEST_CASE( ProxyProtocolAcceptTransportTest, TrustedProxyOpensWithClientAddress )
{
    ProxyRig rig;
    SW_REQUIRE_TRUE( rig.start( vector<uint32>{ NetAddress::makeLoopback( 0 ).getIpv4() } ) );
    vector<uint8> bytes = makeHeader();
    rig.send( vector<uint8>( bytes.begin(), bytes.begin() + 10 ) ); // 잘려 온 머리
    (void)rig.runUntil( [&rig]()
    { return rig._clientRecorder.copyRemoteList().size() == 1; } );
    SW_EXPECT_TRUE( rig._serverRecorder.copyRemoteList().empty() ); // 머리 전에는 열지 않는다
    vector<uint8> rest( bytes.begin() + 10, bytes.end() );
    rest.push_back( 'h' );
    rest.push_back( 'i' );
    rig.send( rest );
    SW_REQUIRE_TRUE( rig.runUntil( [&rig]()
    { return rig._serverRecorder.copyReceived().size() == 2; } ) );
    const vector<NetAddress> listRemote = rig._serverRecorder.copyRemoteList();
    SW_REQUIRE_EQUAL( size_t( 1 ), listRemote.size() );
    SW_EXPECT_TRUE( listRemote[0] == kClient );
    SW_EXPECT_TRUE( rig._serverRecorder.copyReceived() == ( vector<uint8>{ 'h', 'i' } ) );
}

/** @brief [ProxyProtocolAcceptTransportTest] 신뢰 밖 출처는 위로 알리지 않고 닫는다 — 머리로 주소를 속이지 못한다. 신뢰 프록시의 틀린 머리도 닫는다 */
SW_TEST_CASE( ProxyProtocolAcceptTransportTest, UntrustedSourceAndBadHeaderAreClosed )
{
    SW_EXPECT_LOG( "ProxyProtocolAcceptTransport.UntrustedSource" ); // 믿는 목록 밖의 출처(첫 경우)
    SW_EXPECT_LOG( "ProxyProtocolAcceptTransport.InvalidHeader" );   // 믿는 프록시가 보낸 깨진 머리(둘째 경우)
    {
        ProxyRig rig;
        SW_REQUIRE_TRUE( rig.start( vector<uint32>{ NetAddress::make( 10, 0, 0, 1, 0 ).getIpv4() } ) );
        rig.send( makeHeader() );
        SW_EXPECT_TRUE( rig.runUntil( [&rig]()
        { return rig._clientRecorder.getClosedCount() == 1; } ) );
        SW_EXPECT_TRUE( rig._serverRecorder.copyRemoteList().empty() );
        SW_EXPECT_EQUAL( 0, rig._serverRecorder.getClosedCount() );
    }
    {
        ProxyRig rig;
        SW_REQUIRE_TRUE( rig.start( vector<uint32>{ NetAddress::makeLoopback( 0 ).getIpv4() } ) );
        rig.send( vector<uint8>{ 'G', 'E', 'T', ' ' } );
        SW_EXPECT_TRUE( rig.runUntil( [&rig]()
        { return rig._clientRecorder.getClosedCount() == 1; } ) );
        SW_EXPECT_TRUE( rig._serverRecorder.copyRemoteList().empty() );
    }
}

/** @brief [ProxyProtocolTest] 신뢰 프록시 목록은 IPv4 글만 받는다 — 이름 · IPv6 · 빈 글은 그 항목을 대상으로 오류다 */
SW_TEST_CASE( ProxyProtocolTest, TrustedProxyListAcceptsOnlyIpv4 )
{
    const expected<vector<uint32>> parsed = ProxyProtocolAcceptTransport::parseTrustedProxies( vector<string>{ "127.0.0.1", "10.0.0.5" } );
    SW_REQUIRE_TRUE( parsed.has_value() );
    SW_REQUIRE_TRUE( parsed->size() == 2 );
    SW_EXPECT_EQUAL( 0x7F000001u, ( *parsed )[0] );
    for ( const utf8* pBad : { "proxy.example", "::1", "" } )
    {
        SW_EXPECT_FALSE( ProxyProtocolAcceptTransport::parseTrustedProxies( vector<string>{ "10.0.0.5", pBad } ).has_value() );
    }
}
