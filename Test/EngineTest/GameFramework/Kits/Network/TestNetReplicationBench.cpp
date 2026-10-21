// 복제 벤치 — 가상 서버 하니스에서 클라이언트 16 × 엔티티 1000(틱마다 10 % 가 바뀐다), 60 Hz. 틱 시간(하니스 한 스텝 · 서버 복제)과 틱당 할당 수. 평문 판과 UDP 암호화 판. 값은 Release 로 읽는다.
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Time/MonotonicClock.h"

#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationServer.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimHarness.h"

#include "TestFramework/Harness/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_TEST_LOAD_SUITE( NetReplicationBenchTest, "16 clients and 1000 replicated entities at 60 Hz" );
SW_TEST_TIME_BUDGET( NetReplicationBenchTest, 8000, "Release 케이스 최대 1899 ms(2026-10-15, Ryzen 7 6800H, 세 번 중 최대)의 네 배 — CI 러너 여유" );

SW_LOG_CALLER( "NetReplicationBench" );

using namespace sw;

namespace
{
    struct NetReplicationBenchInternal
    {
        static constexpr uint32 kClientCount  = 16;
        static constexpr uint32 kEntityCount  = 1000;
        static constexpr uint32 kStateBytes   = 16;
        static constexpr uint32 kChangeStride = 10; ///< 틱마다 열에 하나(10 %)가 바뀐다
        static constexpr uint32 kWarmupTicks  = 120;
        static constexpr uint32 kMeasureTicks = 300;

        static uint64 readAllocationCount()
        {
            const MemoryProfiler* pProfiler = MemoryProfiler::getActive();
            return pProfiler != nullptr ? pProfiler->getTotalAllocationCount() : 0;
        }

        /** @brief 하니스를 @p hostSettings 로 세워 측정 창을 돌고 `[Bench]` 줄에 @p pLabel 을 붙여 남깁니다. 암호화 판과 평문 판이 같은 몸을 씁니다. */
        static void runBench( const NetHostConfig& hostSettings, const utf8* pLabel );
    };

    /** @brief 측정 창 안의 틱마다 서버 복제 시간 · 할당 수를 모읍니다. */
    struct BenchProbe
    {
        vector<int64> _listServerMicroseconds{};
        uint64        _serverAllocationCount{ 0 };
        uint8         _bMeasuring{ SW_FALSE };
    };

    class BenchServerSession final : public INetSimSession
    {
    public:
        BenchServerSession( NetSimWorld& world, BenchProbe* pProbe )
            : _server{}
            , _listEntity{}
            , _pProbe{ pProbe }
        {
            SW_EXPECT_TRUE( _server.initialize( &world.getPeers(), ReplicationServerConfig{} ) );
            world.getRouter().registerHandler( &_server );
            for ( uint32 index = 0; index < NetReplicationBenchInternal::kEntityCount; ++index )
            {
                _listEntity.push_back( NetEntityState{ vector<uint8>( NetReplicationBenchInternal::kStateBytes, static_cast<uint8>( index ) ), index + 1, 1 } );
            }
        }

        void onTickEnd( NetSimWorld& world, float32 deltaTime ) override
        {
            using Internal = NetReplicationBenchInternal;
            (void)deltaTime;
            const uint32 tick = world.getLocalTick();
            // 움직이는 것들 — 틱마다 다른 10 % 의 첫 4 바이트에 틱을 적는다.
            for ( uint32 index = tick % Internal::kChangeStride; index < static_cast<uint32>( _listEntity.size() ); index += Internal::kChangeStride )
            {
                for ( uint32 byte = 0; byte < 4; ++byte )
                {
                    _listEntity[index]._buffer[byte] = static_cast<uint8>( tick >> ( 8 * byte ) );
                }
            }
            const uint64    allocationBefore = Internal::readAllocationCount();
            const Stopwatch stopwatch;
            _server.beginTick( tick );
            for ( const NetEntityState& entity : _listEntity )
            {
                _server.setEntity( entity._entityID, entity._typeID, entity._buffer );
            }
            _server.endTick();
            _server.sendSnapshots();
            if ( _pProbe->_bMeasuring == SW_TRUE )
            {
                _pProbe->_listServerMicroseconds.push_back( stopwatch.getElapsedMicroseconds() );
                _pProbe->_serverAllocationCount += Internal::readAllocationCount() - allocationBefore;
            }
        }

    private:
        ReplicationServer      _server;
        vector<NetEntityState> _listEntity;
        BenchProbe*            _pProbe;
    };

    class BenchClientSession final : public INetSimSession
    {
    public:
        explicit BenchClientSession( NetSimWorld& world )
            : _client{}
        {
            ReplicationClientConfig settings;
            settings._tickInterval = 1.0f / 60.0f;
            SW_EXPECT_TRUE( _client.initialize( &world.getPeers(), settings ) );
            world.getRouter().registerHandler( &_client );
        }

        void onTickBegin( NetSimWorld& world, float32 deltaTime ) override
        {
            (void)world;
            _client.update( deltaTime );
        }

        const ReplicationClient& getClient() const { return _client; }

    private:
        ReplicationClient _client;
    };

    class BenchGame final : public INetSimGame
    {
    public:
        unique_ptr<INetSimSession> createSession( NetSimWorld& world ) override
        {
            if ( world.isServer() )
                return make_unique<BenchServerSession>( world, &_probe );
            return make_unique<BenchClientSession>( world );
        }

        BenchProbe _probe;
    };

    void NetReplicationBenchInternal::runBench( const NetHostConfig& hostSettings, const utf8* pLabel )
    {
        BenchGame    game;
        NetSimConfig settings;
        settings._hostSettings = hostSettings;
        NetSimHarness harness;
        SW_ASSERT_TRUE( harness.initialize( sw::SceneEngineServices::makeFromBoundEngine(), settings, &game ) );
        for ( uint32 index = 0; index < kClientCount; ++index )
        {
            SW_ASSERT_TRUE( harness.addClient( NetSimLinkConditions{} ) > 0 );
        }
        harness.stepTicks( kWarmupTicks );
        SW_ASSERT_TRUE( harness.areAllClientsConnected() );

        MemoryProfiler* pProfiler    = MemoryProfiler::getActive();
        const bool      bWasTracking = pProfiler != nullptr && pProfiler->isTrackingEnabled();
        if ( pProfiler != nullptr )
            pProfiler->setTrackingEnabled( true );
        game._probe._bMeasuring        = SW_TRUE;
        const uint64  sentBytesBefore  = harness.getServer().getTraffic()._sentBytes;
        const uint64  allocationBefore = readAllocationCount();
        vector<int64> listStepMicrosecond;
        for ( uint32 tick = 0; tick < kMeasureTicks; ++tick )
        {
            const Stopwatch stopwatch;
            harness.step();
            listStepMicrosecond.push_back( stopwatch.getElapsedMicroseconds() );
        }
        [[maybe_unused]] const uint64 allocationCount = readAllocationCount() - allocationBefore;
        [[maybe_unused]] const uint64 sentBytes       = harness.getServer().getTraffic()._sentBytes - sentBytesBefore;
        game._probe._bMeasuring                       = SW_FALSE;
        if ( pProfiler != nullptr )
            pProfiler->setTrackingEnabled( bWasTracking );

        utf8 arrStepLabel[constant::kMaxBuffer128];
        utf8 arrServerLabel[constant::kMaxBuffer128];
        formatstring( arrStepLabel, constant::kMaxBuffer128, "NetSim step 16 clients x 1000 entities%# (us)", pLabel );
        formatstring( arrServerLabel, constant::kMaxBuffer128, "ReplicationServer tick 16 clients x 1000 entities%# (us)", pLabel );
        test::logBenchSamples( arrStepLabel, listStepMicrosecond );
        test::logBenchSamples( arrServerLabel, game._probe._listServerMicroseconds );
        SW_LOG_INFO( "[Bench] NetSim 16 x 1000%# per tick: allocations %# (server replication %#), server up %# B", pLabel, allocationCount / kMeasureTicks,
                     game._probe._serverAllocationCount / kMeasureTicks, sentBytes / kMeasureTicks );
        // 벤치도 복제가 흐르는지는 본다.
        vector<NetSimWorld*> listClient;
        harness.collectClients( listClient );
        for ( NetSimWorld* pClient : listClient )
        {
            SW_EXPECT_NOT_NULL( static_cast<BenchClientSession*>( pClient->getSession() )->getClient().getLatest() );
        }
    }
} // namespace

/**
 * @brief [NetReplicationBenchTest] 클라이언트 16 × 엔티티 1000(틱마다 10 % 바뀜), 60 Hz — 하니스 한 스텝 · 서버 복제 시간(us)과 틱당 할당 수, 서버 올림 바이트
 * @details 할당 수는 메모리 프로파일러가 있는 구성(Dev)에서만 센다(Shipping 은 0). 시간은 Release 로 읽는다.
 */
SW_TEST_CASE( NetReplicationBenchTest, SixteenClientsThousandEntitiesAtSixtyHertz )
{
    NetReplicationBenchInternal::runBench( NetHostConfig{}, "" );
}

/**
 * @brief [NetReplicationBenchTest] 같은 부하를 UDP 암호화(X25519 키 교환 + 패킷마다 AEAD)를 켠 호스트로 — 평문 판과 줄을 나란히 놓아 암호화 비용을 읽는다
 * @details 인증기 없는 암호화는 개발 빌드에만 있다(결정 기록 5-9). Shipping 은 건너뛴다. 서버 틱 시간은 "하니스 한 스텝"(봉인 · 열기가 든 송수신 포함)으로 읽는다.
 */
SW_TEST_CASE( NetReplicationBenchTest, SixteenClientsThousandEntitiesAtSixtyHertzEncrypted )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "UDP encryption without an authenticator is a development-build mode" );
#else
    NetHostConfig hostSettings;
    hostSettings._security._mode      = NetSecurityMode::Encrypted;
    hostSettings._security._pProvider = &NetSecurity::getProvider();
    NetReplicationBenchInternal::runBench( hostSettings, " encrypted" );
#endif
}
