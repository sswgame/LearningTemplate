#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Observability/MetricRegistry.h"
#include "Engine/Observability/OpsHTTPEndpoint.h"
#include "Engine/Observability/ServiceHealthRegistry.h"

#include "TestFramework/TestFramework.h"

#include <charconv>
#include <thread>

// 운영 HTTP — 경로 셋 · 405 · 404 · 400 · Content-Length(전송 없이), 루프백 위 /metrics · 머리 상한 431 · 조각난 머리, 실제 소켓(이 기계 주소)의 /healthz.

using namespace sw;

namespace
{
    /** @brief 받은 바이트와 닫힘을 모으는 클라이언트입니다. 실제 전송에서는 I/O 스레드가 쓴다. */
    class OpsHTTPProbe final : public IStreamHandler
    {
    public:
        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
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

        atomic<bool>           _bOpened{ false };
        atomic<bool>           _bClosed{ false };
        mutex                  _mutex{};
        string                 _received{};
        StreamConnectionHandle _handle{};
    };

    /** @brief 루프백 망 위의 끝점 + 클라이언트 하나(한 스레드, 결정적). */
    struct OpsLoopbackRig
    {
        LoopbackStreamNetwork        _network{ 3u };
        unique_ptr<IStreamTransport> _serverTransport{ _network.createTransport() };
        unique_ptr<IStreamTransport> _clientTransport{ _network.createTransport() };
        MetricRegistry               _metrics{};
        ServiceHealthRegistry        _health{};
        OpsHTTPEndpoint              _endpoint{};
        OpsHTTPProbe                 _probe{};

        ~OpsLoopbackRig()
        {
            _endpoint.shutdown();
            _clientTransport->shutdown();
        }

        bool start()
        {
            StreamTransportSettings settings;
            settings._ioThreadCount = 0;
            OpsHTTPEndpointSettings opsSettings;
            opsSettings._bindAddress = NetAddress::makeLoopback( 0 );
            const bool bEndpoint     = _endpoint.initialize( _serverTransport.get(), settings, opsSettings, &_metrics, &_health );
            const bool bClient       = _clientTransport->initialize( &_probe, settings );
            if ( bEndpoint == false || bClient == false )
                return false;
            (void)_clientTransport->connect( NetAddress::makeLoopback( _endpoint.getListenPort() ) );
            pump( 20 );
            return _probe._bOpened.load( std::memory_order_acquire );
        }

        void pump( int32 roundCount )
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
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /metrics HTTP/1.1\r\nHost: x", &metrics, &health, 0, response ), 200 );
    SW_EXPECT_TRUE( StringUtil::startsWith( response, "HTTP/1.1 200 OK\r\n" ) );
    SW_EXPECT_TRUE( response.find( "Content-Type: text/plain; version=0.0.4" ) != string::npos );
    SW_EXPECT_TRUE( response.find( "probe_total 2\n" ) != string::npos );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /healthz HTTP/1.1", &metrics, &health, 0, response ), 503 ); // 틱 없음
    health.markTick( 0 );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /healthz?verbose=1 HTTP/1.1", &metrics, &health, 0, response ), 200 );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "POST /metrics HTTP/1.1", &metrics, &health, 0, response ), 405 );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /nothing HTTP/1.1", &metrics, &health, 0, response ), 404 );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "garbage", &metrics, &health, 0, response ), 400 );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /metrics HTTP/1.1", nullptr, &health, 0, response ), 404 ); // 등록부 없는 경로
    const int32 store = health.registerCheck( "service_store", true );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /readyz HTTP/1.1", &metrics, &health, 0, response ), 503 ); // 필수 검사 미확인
    health.setCheck( store, HealthState::Ok, "" );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /readyz HTTP/1.1", &metrics, &health, 0, response ), 200 );
    SW_EXPECT_TRUE( response.find( "check service_store ok\n" ) != string::npos );
    health.setDraining( true );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /readyz HTTP/1.1", &metrics, &health, 0, response ), 503 );
    SW_EXPECT_EQUAL( OpsHTTPEndpoint::makeResponse( "GET /healthz HTTP/1.1", &metrics, &health, 0, response ), 200 ); // 비우는 중에도 살아 있다
}

SW_TEST_CASE( OpsHTTPEndpointTest, ContentLengthMatchesTheBody )
{
    MetricRegistry metrics;
    metrics.registerGauge( "g", "G" )->set( 1.5 );
    string response;
    (void)OpsHTTPEndpoint::makeResponse( "GET /metrics HTTP/1.1", &metrics, nullptr, 0, response );
    const size_t headEnd = response.find( "\r\n\r\n" );
    SW_ASSERT_TRUE( headEnd != string::npos );
    const string               body = response.substr( headEnd + 4 );
    utf8                       arrLength[constant::kMaxBuffer32];
    const std::to_chars_result result   = std::to_chars( arrLength, arrLength + sizeof( arrLength ), body.size() );
    const string               expected = string( "Content-Length: " ) + string( arrLength, result.ptr ) + "\r\n";
    SW_EXPECT_TRUE( response.find( expected ) != string::npos );
    SW_EXPECT_TRUE( response.find( "Connection: close\r\n" ) != string::npos );
}

SW_TEST_CASE( OpsHTTPEndpointTest, ServesMetricsOverLoopbackAndCloses )
{
    OpsLoopbackRig rig;
    SW_ASSERT_TRUE( rig.start() );
    rig._metrics.registerCounter( "loop_total", "Loop" )->add( 5 );
    rig.send( "GET /metr" ); // 머리가 조각나 와도 빈 줄까지 모은다
    rig.pump( 5 );
    SW_EXPECT_TRUE( rig._probe.getReceived().empty() );
    rig.send( "ics HTTP/1.1\r\nHost: localhost\r\n\r\n" );
    rig.pump( 50 );
    const string received = rig._probe.getReceived();
    SW_EXPECT_TRUE_MSG( StringUtil::startsWith( received, "HTTP/1.1 200 OK\r\n" ), received.c_str() );
    SW_EXPECT_TRUE( received.find( "loop_total 5\n" ) != string::npos );
    SW_EXPECT_TRUE( rig._probe._bClosed.load( std::memory_order_acquire ) );
}

SW_TEST_CASE( OpsHTTPEndpointTest, OversizedHeadIsRefusedWith431 )
{
    OpsLoopbackRig rig;
    SW_ASSERT_TRUE( rig.start() );
    const string junk( 9000, 'a' ); // 빈 줄 없이 8 KB 를 넘는다
    rig.send( junk );
    rig.pump( 50 );
    const string received = rig._probe.getReceived();
    SW_EXPECT_TRUE_MSG( StringUtil::startsWith( received, "HTTP/1.1 431 " ), received.c_str() );
    SW_EXPECT_TRUE( rig._probe._bClosed.load( std::memory_order_acquire ) );
}

SW_TEST_CASE( OpsHTTPEndpointTest, ServesHealthOverARealSocketOnThisMachine )
{
    unique_ptr<IStreamTransport> serverTransport = StreamTransportFactory::createPlatformTransport();
    unique_ptr<IStreamTransport> clientTransport = StreamTransportFactory::createPlatformTransport();
    SW_ASSERT_TRUE( serverTransport != nullptr && clientTransport != nullptr );
    MetricRegistry          metrics;
    ServiceHealthRegistry   health;
    OpsHTTPEndpoint         endpoint;
    OpsHTTPProbe            probe;
    OpsHTTPEndpointSettings opsSettings;
    SW_EXPECT_TRUE( opsSettings._bindAddress == NetAddress::makeLoopback( 9100 ) ); // 기본은 이 기계만
    opsSettings._bindAddress = NetAddress::makeLoopback( 0 );
    StreamTransportSettings transportSettings;
    transportSettings._maxConnections = 8;
    health.markTick( MonotonicClock::nowNanoseconds() / 1000000 );
    SW_ASSERT_TRUE( endpoint.initialize( serverTransport.get(), transportSettings, opsSettings, &metrics, &health ) );
    SW_ASSERT_TRUE( clientTransport->initialize( &probe, transportSettings ) );
    SW_ASSERT_TRUE( clientTransport->connect( NetAddress::makeLoopback( endpoint.getListenPort() ) ).isValid() );
    const Deadline openDeadline = Deadline::afterMilliseconds( 5000 );
    while ( probe._bOpened.load( std::memory_order_acquire ) == false && openDeadline.isExpired() == false )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
    SW_ASSERT_TRUE( probe._bOpened.load( std::memory_order_acquire ) );
    const string request = "GET /healthz HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    (void)clientTransport->send( probe.getHandle(), reinterpret_cast<const uint8*>( request.data() ), static_cast<int32>( request.size() ) );
    const Deadline closeDeadline = Deadline::afterMilliseconds( 5000 );
    while ( probe._bClosed.load( std::memory_order_acquire ) == false && closeDeadline.isExpired() == false )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
    const string received = probe.getReceived();
    SW_EXPECT_TRUE_MSG( StringUtil::startsWith( received, "HTTP/1.1 200 OK\r\n" ), received.c_str() );
    SW_EXPECT_TRUE( received.find( "\r\n\r\nok\n" ) != string::npos );
    SW_EXPECT_TRUE( probe._bClosed.load( std::memory_order_acquire ) ); // 답한 뒤 서버가 닫는다
    endpoint.shutdown();
    clientTransport->shutdown();
}
