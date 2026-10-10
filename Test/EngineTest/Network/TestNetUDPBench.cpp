// 실제 UDP 처리량 — 서버 NetHost 하나(UDPNetTransport) + 가짜 클라이언트 N 개(각자 소켓 · 드라이버 스레드 4 개가 나눠 돈다). 서버는 20 Hz 로 연결마다 200 B 스냅숏,
// 클라이언트는 30 Hz 로 40 B 입력. 측정 5 초: 서버 받은/보낸 패킷/초, 서버 스레드의 바쁜 몫(update + 보내기)과 패킷당 ns, 서버가 본 연결 RTT p50 · p99,
// 클라이언트가 보낸 수 대 서버가 받은 수(커널 버림 · 드라이버 지연). 두 플랫폼 같은 본문 — 값은 Release 로 읽는다.
#include "pch.h"

#include "Core/Common/BuildInfo.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Connection/NetHostThread.h"
#include "Core/Network/Transport/UDPNetTransport.h"
#include "Core/Time/MonotonicClock.h"

#include "TestFramework/TestBench.h"
#include "TestFramework/TestFramework.h"

#include <algorithm>
#include <chrono>
#include <thread>

SW_TEST_REQUIRES_HOST( NetUDPBenchTest, "opens thousands of real UDP sockets for about 30 s - run with the host suites in Release on Windows and Linux" );

SW_LOG_CALLER( "NetUDPBench" );

using namespace sw;

namespace
{
    struct NetUDPBenchInternal
    {
        static constexpr int32   kDriverThreads       = 4;
        static constexpr int32   kSnapshotBytes       = 200;
        static constexpr int32   kInputBytes          = 40;
        static constexpr float64 kSnapshotInterval    = 1.0 / 20.0;
        static constexpr float64 kInputInterval       = 1.0 / 30.0;
        static constexpr int64   kWarmupMilli         = 2000;
        static constexpr int64   kMeasureMilli        = 5000;
        static constexpr int64   kConnectTimeoutMilli = 30000;
    };

    /** @brief 전송을 감싸 데이터그램 수를 센다(서버 스레드 하나가 받고 보낸다 — 측정 스레드가 읽으므로 원자). */
    class CountingTransport final : public INetTransport
    {
    public:
        explicit CountingTransport( INetTransport* pInner )
            : _sentCount{ 0 }
            , _receivedCount{ 0 }
            , _pInner{ pInner }
        {
        }

        bool send( const NetAddress& to, const uint8* pData, int32 size ) override
        {
            _sentCount.fetch_add( 1, std::memory_order_relaxed );
            return _pInner->send( to, pData, size );
        }
        bool receive( NetAddress& outFrom, vector<uint8>& outBuffer ) override
        {
            const bool bReceived = _pInner->receive( outFrom, outBuffer );
            if ( bReceived )
                _receivedCount.fetch_add( 1, std::memory_order_relaxed );
            return bReceived;
        }
        NetAddress getLocalAddress() const override { return _pInner->getLocalAddress(); }
        bool       waitForReceive( float64 timeoutSeconds ) override { return _pInner->waitForReceive( timeoutSeconds ); }

        atomic<uint64> _sentCount;
        atomic<uint64> _receivedCount;

    private:
        INetTransport* _pInner;
    };

    struct BenchClient
    {
        UDPNetTransport _transport{};
        NetHost         _host{};
        float64         _nextInputTime{ 0.0 };
    };

    struct BenchResult
    {
        float64 _receivedPerSecond{ 0.0 };
        float64 _sentPerSecond{ 0.0 };
        float64 _busyFraction{ 0.0 };
        int64   _nanosecondsPerPacket{ 0 };
        float32 _rttP50{ 0.0f };
        float32 _rttP99{ 0.0f };
        uint64  _clientSentInputs{ 0 };
        uint64  _serverReceivedPackets{ 0 };
    };

    /** @brief 가짜 클라이언트 N 개로 한 판을 돈다. 소켓을 못 열거나 모두 연결하지 못하면 false(리눅스 기본 ulimit -n 1024 — 2000 개는 `ulimit -n 8192`). */
    bool runBench( int32 clientCount, BenchResult& outResult )
    {
        using Internal = NetUDPBenchInternal;
        UDPNetTransport serverSocket;
        if ( serverSocket.open( 0 ) == false )
            return false;
        CountingTransport serverTransport( &serverSocket );
        NetHostSettings   settings;
        settings._maxConnections = clientCount + 16;
        settings._timeoutSeconds = 20.0; // 측정 중 클라이언트 드라이버가 밀려도 끊기지 않게
        settings._connectTimeout = 20.0;
        NetHost server;
        server.initialize( &serverTransport, settings );
        if ( server.listen() == false )
            return false;

        vector<unique_ptr<BenchClient>> listClient;
        for ( int32 index = 0; index < clientCount; ++index )
        {
            unique_ptr<BenchClient> client = make_unique<BenchClient>();
            if ( client->_transport.open( 0 ) == false )
            {
                SW_LOG_WARNING( "Opened only %# client sockets of %# - raise the descriptor limit (Linux: ulimit -n 8192)", index, clientCount );
                return false;
            }
            client->_host.initialize( &client->_transport, settings );
            listClient.push_back( std::move( client ) );
        }
        const NetAddress serverAddress = NetAddress::makeLoopback( serverSocket.getLocalAddress()._port );
        for ( unique_ptr<BenchClient>& client : listClient )
        {
            (void)client->_host.connect( serverAddress );
        }

        // 드라이버 스레드는 표를 원시 포인터로 본다 — sw::vector 를 여러 스레드가 만지면 Debug 레이스 탐지기가 잡는다.
        unique_ptr<BenchClient>* const pClientArray = listClient.data();
        const size_t                   clientTotal  = listClient.size();
        atomic<bool>                   bStop{ false };
        atomic<bool>                   bMeasuring{ false };
        atomic<int64>                  busyNanoseconds{ 0 };
        atomic<uint64>                 clientSentInputs{ 0 };

        // 서버 스레드 — 소켓을 기다렸다가 update, 20 Hz 로 연결마다 스냅숏. 바쁜 시간은 기다림을 뺀 몫.
        std::thread serverThread( [&]()
        {
            vector<int32> listConnection;
            uint8         arrSnapshot[Internal::kSnapshotBytes] = { 0x80 };
            float64       nextSnapshot                          = 0.0;
            while ( bStop.load( std::memory_order_acquire ) == false )
            {
                (void)server.waitForReceive( 0.002 );
                const Stopwatch busy;
                const float64   time = NetHostThread::getTime();
                if ( time >= nextSnapshot )
                {
                    nextSnapshot = time + Internal::kSnapshotInterval;
                    server.collectConnected( listConnection );
                    for ( const int32 connectionID : listConnection )
                    {
                        arrSnapshot[1] = static_cast<uint8>( connectionID );
                        (void)server.sendMessage( connectionID, NetChannelType::Unreliable, arrSnapshot, Internal::kSnapshotBytes );
                    }
                }
                server.update( time );
                if ( bMeasuring.load( std::memory_order_relaxed ) )
                    busyNanoseconds.fetch_add( busy.getElapsedNanoseconds(), std::memory_order_relaxed );
            }
        } );

        // 클라이언트 드라이버 — 스레드마다 자기 몫의 클라이언트를 update, 30 Hz 입력.
        vector<std::thread> listDriver;
        for ( int32 driver = 0; driver < Internal::kDriverThreads; ++driver )
        {
            listDriver.emplace_back( [&, driver]()
            {
                uint8 arrInput[Internal::kInputBytes] = { 0x81 };
                while ( bStop.load( std::memory_order_acquire ) == false )
                {
                    const float64 time = NetHostThread::getTime();
                    for ( size_t index = static_cast<size_t>( driver ); index < clientTotal; index += Internal::kDriverThreads )
                    {
                        BenchClient& client = *pClientArray[index];
                        client._host.update( time );
                        if ( time >= client._nextInputTime && client._host.getConnectedCount() > 0 )
                        {
                            client._nextInputTime = time + Internal::kInputInterval;
                            if ( client._host.sendMessage( 0, NetChannelType::Unreliable, arrInput, Internal::kInputBytes ) && bMeasuring.load( std::memory_order_relaxed ) )
                                clientSentInputs.fetch_add( 1, std::memory_order_relaxed );
                        }
                    }
                    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
                }
            } );
        }

        const Deadline connectDeadline = Deadline::afterMilliseconds( Internal::kConnectTimeoutMilli );
        while ( server.getConnectedCount() < clientCount && connectDeadline.isExpired() == false )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
        const int32 connectedCount = server.getConnectedCount();
        std::this_thread::sleep_for( std::chrono::milliseconds( Internal::kWarmupMilli ) );

        const uint64 receivedBefore = serverTransport._receivedCount.load();
        const uint64 sentBefore     = serverTransport._sentCount.load();
        bMeasuring.store( true );
        const Stopwatch measure;
        std::this_thread::sleep_for( std::chrono::milliseconds( Internal::kMeasureMilli ) );
        bMeasuring.store( false );
        const float64 seconds  = static_cast<float64>( measure.getElapsedNanoseconds() ) * 1.0e-9;
        const uint64  received = serverTransport._receivedCount.load() - receivedBefore;
        const uint64  sent     = serverTransport._sentCount.load() - sentBefore;

        vector<int32> listConnection;
        server.collectConnected( listConnection );
        vector<float32> listRtt;
        for ( const int32 connectionID : listConnection )
        {
            NetConnectionStats stats;
            if ( server.getConnectionStats( connectionID, stats ) )
                listRtt.push_back( stats._rtt );
        }
        std::sort( listRtt.begin(), listRtt.end() );

        bStop.store( true, std::memory_order_release );
        serverThread.join();
        for ( std::thread& driver : listDriver )
        {
            driver.join();
        }

        const int64 busy                 = busyNanoseconds.load();
        outResult._receivedPerSecond     = static_cast<float64>( received ) / seconds;
        outResult._sentPerSecond         = static_cast<float64>( sent ) / seconds;
        outResult._busyFraction          = static_cast<float64>( busy ) * 1.0e-9 / seconds;
        outResult._nanosecondsPerPacket  = received + sent > 0 ? busy / static_cast<int64>( received + sent ) : 0;
        outResult._rttP50                = listRtt.empty() ? 0.0f : listRtt[listRtt.size() / 2];
        outResult._rttP99                = listRtt.empty() ? 0.0f : listRtt[( listRtt.size() * 99 ) / 100];
        outResult._clientSentInputs      = clientSentInputs.load();
        outResult._serverReceivedPackets = received;
        SW_LOG_INFO( "[Bench] NetUDP %# clients (%# connected) on %# %#: in %# pkt/s, out %# pkt/s, server busy %# pct (%# ns/packet), rtt p50 %# ms p99 %# ms, "
                     "client inputs %# vs server packets in %#",
                     clientCount, connectedCount, build::kPlatformName, build::kConfigName, static_cast<int64>( outResult._receivedPerSecond ),
                     static_cast<int64>( outResult._sentPerSecond ), static_cast<int64>( outResult._busyFraction * 100.0 ), outResult._nanosecondsPerPacket,
                     static_cast<int64>( outResult._rttP50 * 1000.0f ), static_cast<int64>( outResult._rttP99 * 1000.0f ), outResult._clientSentInputs, received );
        return connectedCount == clientCount;
    }
} // namespace

/**
 * @brief [NetUDPBenchTest] 가짜 클라이언트 500 — 실제 UDP 소켓, 서버 스레드 하나
 */
SW_TEST_CASE( NetUDPBenchTest, FiveHundredClients )
{
    BenchResult result;
    if ( runBench( 500, result ) == false )
        SW_TEST_SKIP( "could not open or connect 500 client sockets" );
    SW_EXPECT_TRUE( result._receivedPerSecond > 0.0 ); // 벤치다 — 흐름이 있는지만 본다
}

/**
 * @brief [NetUDPBenchTest] 가짜 클라이언트 2000 — 실제 UDP 소켓, 서버 스레드 하나
 */
SW_TEST_CASE( NetUDPBenchTest, TwoThousandClients )
{
    BenchResult result;
    if ( runBench( 2000, result ) == false )
        SW_TEST_SKIP( "could not open or connect 2000 client sockets (Linux: ulimit -n 8192)" );
    SW_EXPECT_TRUE( result._receivedPerSecond > 0.0 );
}
