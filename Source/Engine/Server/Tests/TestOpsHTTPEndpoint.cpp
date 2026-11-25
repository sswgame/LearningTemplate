#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Network/Tests/LoopbackStreamTransport.h"
#include "Core/Network/Transport/UDPNetTransport.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/TestWait.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Network/GameNetSession.h"
#include "Engine/Observability/MetricRegistry.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"
#include "Engine/Server/OpsHTTPEndpoint.h"
#include "Engine/Server/ServiceHealthRegistry.h"

#include <thread>

// 운영 HTTP — 경로 셋 · 405 · 404 · 400 · Content-Length(전송 없이), 루프백 위 /metrics · 머리 상한 431 · 조각난 머리, 실제 소켓(이 기계 주소)의 /healthz,
// 게임 UDP 포트(GameNetSession)의 샤드 지표가 /metrics 에 보인다(감사 N-7).

using namespace sw;

namespace
{
    /** @brief 받은 바이트와 닫힘을 모으는 클라이언트입니다. 실제 전송에서는 I/O 스레드가 쓴다. */
    class OpsHTTPProbe final : public IStreamHandler
    {
    public:
        atomic<bool>           _bOpened{ false };
        atomic<bool>           _bClosed{ false };
        mutex                  _mutex{};
        string                 _received{};
        StreamConnectionHandle _handle{};
        void                   onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
        {
            (void)remote;
            (void)bAccepted;
            std::scoped_lock<mutex> lock{ _mutex };
            _handle = handle;
            _bOpened.store( true, std::memory_order_release );
        }

        void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override
        {
            (void)handle;
            std::scoped_lock<mutex> lock{ _mutex };
            _received.append( reinterpret_cast<const utf8*>( pData ), static_cast<size_t>( size ) );
        }

        void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
        {
            (void)handle;
            (void)reason;
            _bClosed.store( true, std::memory_order_release );
        }

        string getReceived()
        {
            std::scoped_lock<mutex> lock{ _mutex };
            return _received;
        }

        StreamConnectionHandle getHandle()
        {
            std::scoped_lock<mutex> lock{ _mutex };
            return _handle;
        }
    };

    /** @brief 루프백 망 위의 엔드포인트 + 클라이언트 하나(한 스레드, 결정적). */
    struct OpsLoopbackFixture
    {
        LoopbackStreamNetwork        _network{ 3u };
        unique_ptr<IStreamTransport> _serverTransport{ _network.createTransport() };
        unique_ptr<IStreamTransport> _clientTransport{ _network.createTransport() };
        MetricRegistry               _metrics{};
        ServiceHealthRegistry        _health{};
        OpsHTTPEndpoint              _endpoint{};
        OpsHTTPProbe                 _probe{};

        ~OpsLoopbackFixture()
        {
            _endpoint.shutdown();
            _clientTransport->shutdown();
        }

        bool start()
        {
            StreamTransportConfig settings;
            settings._ioThreadCount = 0;
            OpsHTTPEndpointConfig opsSettings;
            opsSettings._bindAddress = NetAddress::makeLoopback( 0 );
            const bool bEndpoint     = _endpoint.initialize( _serverTransport.get(), settings, opsSettings, &_metrics, &_health );
            const bool bClient       = _clientTransport->initialize( &_probe, settings );
            if ( bEndpoint == false || bClient == false )
                return false;
            (void)_clientTransport->connect( NetAddress::makeLoopback( _endpoint.getListenPort() ) );
            poll( 20 );
            return _probe._bOpened.load( std::memory_order_acquire );
        }

        void poll( int32 roundCount )
        {
            for ( int32 round = 0; round < roundCount; ++round )
            {
                (void)_serverTransport->pollIO( 0 );
                (void)_clientTransport->pollIO( 0 );
            }
        }

        void send( string_view text )
        {
            (void)_clientTransport->send( _probe.getHandle(), reinterpret_cast<const uint8*>( text.data() ), static_cast<int32>( text.size() ) );
        }
    };
} // namespace

SW_TEST_CASE( OpsHTTPEndpointTest, RoutesAndStatusCodes )
{
    MetricRegistry        metrics;
    ServiceHealthRegistry health;
    metrics.registerCounter( "probe_total", "Probe" )->add( 2 );
    string response;
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /metrics HTTP/1.1\r\nHost: x", &metrics, &health, 0, {}, response ), 200 );
    SW_EXPECT_TRUE( StringUtil::startsWith( response, "HTTP/1.1 200 OK\r\n" ) );
    SW_EXPECT_TRUE( response.find( "Content-Type: text/plain; version=0.0.4" ) != string::npos );
    SW_EXPECT_TRUE( response.find( "probe_total 2\n" ) != string::npos );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /healthz HTTP/1.1", &metrics, &health, 0, {}, response ), 503 ); // 틱 없음
    health.markTick( 0 );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /healthz?verbose=1 HTTP/1.1", &metrics, &health, 0, {}, response ), 200 );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "POST /metrics HTTP/1.1", &metrics, &health, 0, {}, response ), 405 );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /nothing HTTP/1.1", &metrics, &health, 0, {}, response ), 404 );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "garbage", &metrics, &health, 0, {}, response ), 400 );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /metrics HTTP/1.1", nullptr, &health, 0, {}, response ), 404 ); // 레지스트리 없는 경로
    const int32 store = health.registerCheck( "service_store", true );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /readyz HTTP/1.1", &metrics, &health, 0, {}, response ), 503 ); // 필수 검사 미확인
    health.setCheck( store, HealthState::Ok, "" );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /readyz HTTP/1.1", &metrics, &health, 0, {}, response ), 200 );
    SW_EXPECT_TRUE( response.find( "check service_store ok\n" ) != string::npos );
    health.setDraining( true );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /readyz HTTP/1.1", &metrics, &health, 0, {}, response ), 503 );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /healthz HTTP/1.1", &metrics, &health, 0, {}, response ), 200 ); // 비우는 중에도 살아 있다
}

/**
 * @brief [OpsHTTPEndpointTest] 토큰이 있으면 지표는 Bearer 가 같아야 하고, 탐침은 상태 코드만 받는다 · 토큰 없이는 루프백 · 사설 바인드만 받는다
 */
SW_TEST_CASE( OpsHTTPEndpointTest, BearerTokenGuardsMetricsAndPublicBinds )
{
    MetricRegistry        metrics;
    ServiceHealthRegistry health;
    health.markTick( 0 );
    const int32 store = health.registerCheck( "service_store", true );
    health.setCheck( store, HealthState::Ok, "" );
    string response;
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /metrics HTTP/1.1\r\nHost: x", &metrics, &health, 0, "s3cret", response ), 401 );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /metrics HTTP/1.1\r\nAuthorization: Bearer wrong", &metrics, &health, 0, "s3cret", response ), 401 );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /metrics HTTP/1.1\r\nauthorization: Bearer s3cret", &metrics, &health, 0, "s3cret", response ), 200 );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /healthz HTTP/1.1", &metrics, &health, 0, "s3cret", response ), 200 );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /readyz HTTP/1.1", &metrics, &health, 0, "s3cret", response ), 200 );
    SW_EXPECT_TRUE( response.find( "service_store" ) == string::npos ); // 서비스별 보고는 토큰이 있어야 보인다

    SW_EXPECT_TRUE( OpsHTTPEndpoint::isBindAllowedWithoutToken( NetAddress::makeLoopback( 9100 ) ) );
    SW_EXPECT_TRUE( OpsHTTPEndpoint::isBindAllowedWithoutToken( NetAddress::make( 10, 1, 2, 3, 9100 ) ) );
    SW_EXPECT_TRUE( OpsHTTPEndpoint::isBindAllowedWithoutToken( NetAddress::make( 172, 31, 0, 1, 9100 ) ) );
    SW_EXPECT_TRUE( OpsHTTPEndpoint::isBindAllowedWithoutToken( NetAddress::make( 192, 168, 0, 1, 9100 ) ) );
    SW_EXPECT_FALSE( OpsHTTPEndpoint::isBindAllowedWithoutToken( NetAddress::make( 172, 32, 0, 1, 9100 ) ) );
    SW_EXPECT_FALSE( OpsHTTPEndpoint::isBindAllowedWithoutToken( NetAddress::makeAnyInterface( 9100 ) ) );
    SW_EXPECT_FALSE( OpsHTTPEndpoint::isBindAllowedWithoutToken( NetAddress::make( 8, 8, 8, 8, 9100 ) ) );
}

SW_TEST_CASE( OpsHTTPEndpointTest, ContentLengthMatchesTheBody )
{
    MetricRegistry metrics;
    metrics.registerGauge( "g", "G" )->set( 1.5 );
    string response;
    (void)OpsHTTPEndpoint::makeResponse( "GET /metrics HTTP/1.1", &metrics, nullptr, 0, {}, response );
    const size_t headEnd = response.find( "\r\n\r\n" );
    SW_REQUIRE_TRUE( headEnd != string::npos );
    const string                           body = response.substr( headEnd + 4 );
    fixed_string<constant::buffer::kMax64> expected;
    formatstring( expected.data(), expected.capacity(), "Content-Length: %#\r\n", body.size() );
    SW_EXPECT_TRUE( response.find( expected.c_str() ) != string::npos );
    SW_EXPECT_TRUE( response.find( "Connection: close\r\n" ) != string::npos );
}

SW_TEST_CASE( OpsHTTPEndpointTest, ServesMetricsOverLoopbackAndCloses )
{
    OpsLoopbackFixture rig;
    SW_REQUIRE_TRUE( rig.start() );
    rig._metrics.registerCounter( "loop_total", "Loop" )->add( 5 );
    rig.send( "GET /metr" ); // 머리가 조각나 와도 빈 줄까지 모은다
    rig.poll( 5 );
    SW_EXPECT_TRUE( rig._probe.getReceived().empty() );
    rig.send( "ics HTTP/1.1\r\nHost: localhost\r\n\r\n" );
    rig.poll( 50 );
    const string received = rig._probe.getReceived();
    SW_EXPECT_TRUE_MSG( StringUtil::startsWith( received, "HTTP/1.1 200 OK\r\n" ), received.c_str() );
    SW_EXPECT_TRUE( received.find( "loop_total 5\n" ) != string::npos );
    SW_EXPECT_TRUE( rig._probe._bClosed.load( std::memory_order_acquire ) );
}

SW_TEST_CASE( OpsHTTPEndpointTest, OversizedHeadIsRefusedWith431 )
{
    OpsLoopbackFixture rig;
    SW_REQUIRE_TRUE( rig.start() );
    const string junk( 9000, 'a' ); // 빈 줄 없이 8 KB 를 넘는다
    rig.send( junk );
    rig.poll( 50 );
    const string received = rig._probe.getReceived();
    SW_EXPECT_TRUE_MSG( StringUtil::startsWith( received, "HTTP/1.1 431 " ), received.c_str() );
    SW_EXPECT_TRUE( rig._probe._bClosed.load( std::memory_order_acquire ) );
}

SW_TEST_CASE( OpsHTTPEndpointTest, ServesHealthOverARealSocketOnThisMachine )
{
    unique_ptr<IStreamTransport> serverTransport = StreamTransportFactory::createPlatformTransport();
    unique_ptr<IStreamTransport> clientTransport = StreamTransportFactory::createPlatformTransport();
    SW_REQUIRE_TRUE( serverTransport != nullptr && clientTransport != nullptr );
    MetricRegistry        metrics;
    ServiceHealthRegistry health;
    OpsHTTPEndpoint       endpoint;
    OpsHTTPProbe          probe;
    OpsHTTPEndpointConfig opsSettings;
    SW_EXPECT_TRUE( opsSettings._bindAddress == NetAddress::makeLoopback( 9100 ) ); // 기본은 이 기계만
    opsSettings._bindAddress = NetAddress::makeLoopback( 0 );
    StreamTransportConfig transportSettings;
    transportSettings._maxConnections = 8;
    health.markTick( MonotonicClock::nowNanoseconds() / 1000000 );
    SW_REQUIRE_TRUE( endpoint.initialize( serverTransport.get(), transportSettings, opsSettings, &metrics, &health ) );
    SW_REQUIRE_TRUE( clientTransport->initialize( &probe, transportSettings ) );
    SW_REQUIRE_TRUE( clientTransport->connect( NetAddress::makeLoopback( endpoint.getListenPort() ) ).isValid() );
    SW_REQUIRE_TRUE( test::waitUntil( [&probe]()
    { return probe._bOpened.load( std::memory_order_acquire ); } ) );
    const string request = "GET /healthz HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    (void)clientTransport->send( probe.getHandle(), reinterpret_cast<const uint8*>( request.data() ), static_cast<int32>( request.size() ) );
    (void)test::waitUntil( [&probe]()
    { return probe._bClosed.load( std::memory_order_acquire ); } ); // 닫힘은 아래에서 단언한다 — 받은 글을 먼저 보인다
    const string received = probe.getReceived();
    SW_EXPECT_TRUE_MSG( StringUtil::startsWith( received, "HTTP/1.1 200 OK\r\n" ), received.c_str() );
    SW_EXPECT_TRUE( received.find( "\r\n\r\nok\n" ) != string::npos );
    SW_EXPECT_TRUE( probe._bClosed.load( std::memory_order_acquire ) ); // 답한 뒤 서버가 닫는다
    endpoint.shutdown();
    clientTransport->shutdown();
}

/**
 * @brief [OpsHTTPEndpointTest] 게임 UDP 포트의 샤드 지표(연결 수 · 거절 · 끊김 사유)가 /metrics 에 보인다 — 깨진 데이터그램 하나가 거절 1 이 된다(감사 N-7)
 */
SW_TEST_CASE( OpsHTTPEndpointTest, GameNetSessionMetricsReachTheMetricsPath )
{
    MetricRegistry metrics;
    GameNetSession session{ engine::getSubsystem<ResourceComposition>() };
    session.bindMetricRegistry( &metrics );
    GameNetSessionParams params;
    params._role = GameNetRole::Server; // 포트 0 — 운영체제가 고른다
    SW_REQUIRE_TRUE( session.initialize( params ) );

    UDPNetTransport stranger; // 다른 게임 · 위조 — 헤더의 프로토콜 ID 가 맞지 않는다
    SW_REQUIRE_TRUE( stranger.open( 0, true ) );
    const uint8 arrJunk[16] = {};
    SW_REQUIRE_TRUE( stranger.send( NetAddress::makeLoopback( session.getListenPort() ), arrJunk, static_cast<int32>( sizeof( arrJunk ) ) ) );

    string     response;
    const bool bRejectedSeen = test::waitUntil( [&session, &metrics, &response]()
    {
        session.update(); // 네트워크 스레드가 센 것을 게임 스레드가 지표로 옮긴다
        (void)OpsHTTPEndpoint::makeResponse( "GET /metrics HTTP/1.1", &metrics, nullptr, 0, {}, response );
        return response.find( "net_rejected_packets_total{shard=\"0\"} 1\n" ) != string::npos;
    } );
    SW_EXPECT_TRUE_MSG( bRejectedSeen, response.c_str() );
    SW_EXPECT_TRUE( response.find( "net_connections{shard=\"0\"} 0\n" ) != string::npos );
    SW_EXPECT_TRUE( response.find( "net_authentication_failures_total{shard=\"0\"} 0\n" ) != string::npos );
    SW_EXPECT_TRUE( response.find( "net_replay_rejected_packets_total{shard=\"0\"} 0\n" ) != string::npos );
    SW_EXPECT_TRUE( response.find( "net_disconnects_total{shard=\"0\",reason=\"Timeout\"} 0\n" ) != string::npos ); // 사유마다 시리즈가 열거에서 선다
    SW_EXPECT_TRUE( response.find( "reason=\"None\"" ) == string::npos );                                           // None 은 끊김 사유가 아니다

    session.bindMetricRegistry( nullptr ); // 레지스트리보다 세션이 늦게 닫힌다(전용 서버와 같은 순서)
    session.shutdown();
}
