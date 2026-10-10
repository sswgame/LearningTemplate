#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Transport/NetEmulation.h"
#include "Core/Network/Transport/NetTransport.h"

#include "GameFramework/Kits/Feature/Network/NetMMO/MMOReplicator.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimHarness.h"

#include "TestFramework/TestFramework.h"

// MMO 네트워크 키트 — 관심 영역의 들어옴 · 나감(히스테리시스), 대역폭 예산 안에서 가까운 것을 더 자주 갱신,
// 멀어도 굶지 않음, 늘 보이기 정책. 가상 서버 하니스 위에서 — 대량 나감 · 순서가 뒤바뀐 갱신 · 상한을 넘는 상태.

using namespace sw;

namespace
{
    class PartyPolicy final : public IInterestPolicy
    {
    public:
        bool hasAlwaysRelevant() const override { return true; }
        bool isAlwaysRelevant( int32 connectionID, const MMOEntity& entity ) const override
        {
            (void)connectionID;
            return entity._typeID == 9; // 파티원
        }
    };

    constexpr uint32 kMMOObserverID = 1;

    /** @brief 시험이 틱 사이에 바꾸는 서버 쪽 엔티티 표입니다. 표의 엔티티를 틱마다 `setEntity` 하고, 빠진 것은 지운다. */
    struct MMOTable
    {
        vector<MMOEntity>     _listEntity{};
        vector<uint32>        _listRemoved{};
        MMOReplicatorSettings _settings{};
        uint8                 _bStampTick{ SW_FALSE }; ///< 상태 앞 4 바이트에 서버 틱(틱마다 바뀐다 — 받는 쪽이 되돌아감을 잰다)
    };

    /** @brief 서버 — 연결마다 관찰자 엔티티(`kMMOObserverID`)를 중심으로 관심 영역을 돌린다. 연결이 닫히면 라우터가 관찰자를 지운다. */
    class MMOServerSession final : public INetSimSession
    {
    public:
        MMOServerSession( NetSimWorld& world, MMOTable* pTable )
            : _server{}
            , _pTable{ pTable }
        {
            _server.initialize( &world.getHost(), pTable->_settings );
            world.getRouter().addHandler( &_server );
        }

        void onHostEvent( NetSimWorld& world, const NetHostEvent& event ) override
        {
            (void)world;
            if ( event._kind == NetHostEvent::Kind::Connected )
                _server.setObserver( event._connectionID, kMMOObserverID );
        }

        void onTickEnd( NetSimWorld& world, float32 deltaTime ) override
        {
            const uint32 tick = world.getLocalTick();
            for ( MMOEntity& entity : _pTable->_listEntity )
            {
                if ( _pTable->_bStampTick == SW_TRUE && entity._listState.size() >= 4 )
                {
                    for ( size_t index = 0; index < 4; ++index )
                    {
                        entity._listState[index] = static_cast<uint8>( tick >> ( 8 * index ) );
                    }
                }
                _server.setEntity( entity );
            }
            for ( const uint32 entityID : _pTable->_listRemoved )
            {
                _server.removeEntity( entityID );
            }
            _pTable->_listRemoved.clear();
            _server.update( deltaTime );
        }

        const MMOReplicator& getServer() const { return _server; }

    private:
        MMOReplicator _server;
        MMOTable*     _pTable;
    };

    /** @brief 클라이언트 — 보이는 엔티티와, 틱 도장이 있는 상태가 뒤로 간 횟수(옛 갱신이 새 것을 덮음)를 센다. 받은 갱신 틱은 틱마다 확인한다. */
    class MMOClientSession final : public INetSimSession
    {
    public:
        explicit MMOClientSession( NetSimWorld& world )
            : _view{}
            , _listEvent{}
            , _mapLastStamp{}
            , _regressionCount{ 0 }
            , _serverConnectionID{ -1 }
        {
            world.getRouter().addHandler( &_view );
        }

        void onHostEvent( NetSimWorld& world, const NetHostEvent& event ) override
        {
            (void)world;
            if ( event._kind == NetHostEvent::Kind::Connected )
                _serverConnectionID = event._connectionID;
        }

        void onTickEnd( NetSimWorld& world, float32 deltaTime ) override
        {
            (void)deltaTime;
            if ( _serverConnectionID >= 0 )
                _view.sendAck( world.getHost(), _serverConnectionID );
        }

        void onTickBegin( NetSimWorld& world, float32 deltaTime ) override
        {
            (void)world;
            (void)deltaTime;
            _listEvent.clear();
            _view.drainEvents( _listEvent );
            for ( const MMOClientEvent& event : _listEvent )
            {
                if ( event._kind == MMOClientEvent::Kind::Left )
                {
                    _mapLastStamp.erase( event._entityID );
                    continue;
                }
                const MMOEntity* pEntity = _view.findEntity( event._entityID );
                if ( pEntity == nullptr || pEntity->_listState.size() < 4 )
                    continue;
                uint32 stamp = 0;
                for ( size_t index = 0; index < 4; ++index )
                {
                    stamp |= static_cast<uint32>( pEntity->_listState[index] ) << ( 8 * index );
                }
                const auto stampIter = _mapLastStamp.find( event._entityID );
                if ( stampIter != _mapLastStamp.end() && stamp < stampIter->second )
                    ++_regressionCount;
                _mapLastStamp[event._entityID] = stamp;
            }
        }

        const MMOClientView& getView() const { return _view; }
        int32                getRegressionCount() const { return _regressionCount; }

    private:
        MMOClientView                 _view;
        vector<MMOClientEvent>        _listEvent;
        unordered_map<uint32, uint32> _mapLastStamp;
        int32                         _regressionCount;
        int32                         _serverConnectionID;
    };

    class MMONetSimGame final : public INetSimGame
    {
    public:
        unique_ptr<INetSimSession> createSession( NetSimWorld& world ) override
        {
            if ( world.isServer() )
                return make_unique<MMOServerSession>( world, &_table );
            return make_unique<MMOClientSession>( world );
        }

        MMOTable _table;
    };

    const MMOServerSession& getMMOServer( const NetSimHarness& harness ) { return *static_cast<const MMOServerSession*>( harness.getServer().getSession() ); }

    const MMOClientSession& getMMOClient( const NetSimHarness& harness, int32 worldIndex )
    {
        return *static_cast<const MMOClientSession*>( harness.findClient( worldIndex )->getSession() );
    }

    struct MMOCountGoal
    {
        int32 _worldIndex{ 0 };
        int32 _entityCount{ 0 };
    };

    bool hasMMOEntityCount( const NetSimHarness& harness, void* pContext )
    {
        const MMOCountGoal& goal = *static_cast<const MMOCountGoal*>( pContext );
        return getMMOClient( harness, goal._worldIndex ).getView().getEntityCount() == goal._entityCount;
    }
} // namespace

SW_TEST_CASE( NetMMOTest, ObserversSeeNearbyEntitiesWithinBudgetAndHysteresis )
{
    LoopbackNetwork        network;
    NetEmulationTransport  serverTransport( network.createEndpoint( 4000 ), 9u );
    NetEmulationTransport  clientTransport( network.createEndpoint( 5000 ), 10u );
    NetEmulationConditions conditions;
    conditions._latency  = 0.03;
    conditions._lossRate = 0.05f;
    NetHostSettings hostSettings;
    hostSettings._sendInterval = 1.0 / 20.0;
    NetHost serverHost;
    NetHost clientHost;
    serverHost.initialize( &serverTransport, hostSettings );
    clientHost.initialize( &clientTransport, hostSettings );
    (void)serverHost.listen();
    (void)clientHost.connect( NetAddress::makeLoopback( 4000 ) );
    float64 time = 0.0;
    for ( int32 frame = 0; frame < 60 && clientHost.getConnectionState( 0 ) != NetConnectionState::Connected; ++frame )
    {
        time += 1.0 / 60.0;
        serverHost.update( time );
        clientHost.update( time );
    }
    SW_ASSERT_TRUE( clientHost.getConnectionState( 0 ) == NetConnectionState::Connected );
    serverTransport.setDefaultConditions( conditions );
    clientTransport.setDefaultConditions( conditions );

    PartyPolicy           policy;
    MMOReplicator         server;
    MMOClientView         view;
    MMOReplicatorSettings settings;
    settings._enterRadius       = 50.0f;
    settings._leaveRadius       = 60.0f;
    settings._updateBudgetBytes = 150;
    server.initialize( &serverHost, settings, &policy );

    // 400 개를 400 × 400 에 고르게. 플레이어(1000)는 (100, 100). 파티원(1001)은 멀리.
    for ( uint32 index = 0; index < 400; ++index )
    {
        server.setEntity( MMOEntity{
            vector<uint8>{ 0 },
            float3{ static_cast<float32>( index % 20 ) * 20.0f, 0.0f, static_cast<float32>( index / 20 ) * 20.0f },
            index, 1, 1.0f
        } );
    }
    server.setEntity( MMOEntity{
        vector<uint8>{},
        float3{ 100.0f, 0.0f, 100.0f },
        1000, 0, 1.0f
    } );
    server.setEntity( MMOEntity{
        vector<uint8>{},
        float3{ 380.0f, 0.0f, 380.0f },
        1001, 9, 1.0f
    } );
    server.setObserver( 0, 1000 );

    vector<uint8> buffer;
    vector<int32> listUpdateCount( 402, 0 );
    const auto    runFor = [&]( float64 seconds, bool bAnimate )
    {
        for ( float64 elapsed = 0.0; elapsed < seconds; elapsed += 1.0 / 20.0 )
        {
            time += 1.0 / 20.0;
            if ( bAnimate )
            {
                // 모두 상태가 매 틱 바뀐다(애니메이션 프레임).
                for ( uint32 index = 0; index < 400; ++index )
                {
                    server.setEntity( MMOEntity{
                        vector<uint8>{ static_cast<uint8>( time * 20.0 ) },
                        float3{ static_cast<float32>( index % 20 ) * 20.0f, 0.0f, static_cast<float32>( index / 20 ) * 20.0f },
                        index, 1, 1.0f
                    } );
                }
            }
            server.update( 1.0f / 20.0f );
            // 흉내 줄을 먼저 모두 비운다 — 지연이 방향과 상관없이 같다.
            serverTransport.update( time );
            clientTransport.update( time );
            serverHost.update( time );
            clientHost.update( time );
            int32          connectionID = -1;
            NetChannelType channel      = NetChannelType::Unreliable;
            while ( clientHost.receiveMessage( connectionID, channel, buffer ) )
            {
                SW_EXPECT_TRUE( NetHandleResult::Handled == view.handleMessage( 0, buffer ) );
            }
            // 클라이언트는 받은 갱신 틱을 틱마다 확인한다 — 서버는 그것으로 잃은 갱신을 가린다.
            view.sendAck( clientHost, 0 );
            while ( serverHost.receiveMessage( connectionID, channel, buffer ) )
            {
                SW_EXPECT_TRUE( NetHandleResult::Handled == server.handleMessage( connectionID, buffer ) );
            }
            vector<MMOClientEvent> listEvent;
            view.drainEvents( listEvent );
            for ( const MMOClientEvent& event : listEvent )
            {
                if ( event._kind == MMOClientEvent::Kind::Updated && event._entityID < listUpdateCount.size() )
                    ++listUpdateCount[event._entityID];
            }
        }
    };
    runFor( 2.0, false );
    // 반경 50 안(칸 20 간격 — 대략 π·50²/400 ≈ 20 개) + 자기 자신 + 파티원.
    const int32 visibleCount = view.getEntityCount();
    SW_EXPECT_TRUE( visibleCount > 15 && visibleCount < 40 );
    SW_EXPECT_EQUAL( visibleCount, server.getVisibleCount( 0 ) );
    SW_EXPECT_TRUE( view.findEntity( 1001 ) != nullptr ); // 멀어도 파티원
    SW_EXPECT_TRUE( view.findEntity( 399 ) == nullptr );  // (380, 380)

    // 모두 움직이면 예산 때문에 다 갱신하지 못한다 — 가까운 것이 더 자주.
    std::fill( listUpdateCount.begin(), listUpdateCount.end(), 0 );
    runFor( 4.0, true );
    const int32 nearCount = listUpdateCount[5 * 20 + 5]; // (100, 100) — 바로 옆
    const int32 farCount  = listUpdateCount[4 * 20 + 7]; // (140, 80) — 약 45 m, 반경 안 끝자락
    SW_EXPECT_TRUE( view.findEntity( 4 * 20 + 7 ) != nullptr );
    SW_EXPECT_TRUE( nearCount > 60 );
    SW_EXPECT_TRUE( farCount > 5 && farCount < nearCount / 2 );                                // 굶지는 않지만 덜 자주
    SW_EXPECT_TRUE( server.getSentUpdateCount() < static_cast<uint64>( visibleCount ) * 80u ); // 예산이 막았다

    // 히스테리시스 — 55 m 로 물러나도 나가지 않고(들어오는 반경 50 < 55 < 나가는 반경 60), 70 m 면 나간다.
    server.setEntity( MMOEntity{
        vector<uint8>{},
        float3{ 100.0f, 0.0f, 100.0f },
        1002, 1, 1.0f
    } );
    runFor( 0.5, false );
    SW_ASSERT_TRUE( view.findEntity( 1002 ) != nullptr );
    server.setEntity( MMOEntity{
        vector<uint8>{},
        float3{ 155.0f, 0.0f, 100.0f },
        1002, 1, 1.0f
    } );
    runFor( 0.5, false );
    SW_EXPECT_TRUE( view.findEntity( 1002 ) != nullptr );
    server.setEntity( MMOEntity{
        vector<uint8>{},
        float3{ 170.0f, 0.0f, 100.0f },
        1002, 1, 1.0f
    } );
    runFor( 0.5, false );
    SW_EXPECT_TRUE( view.findEntity( 1002 ) == nullptr );
    server.removeEntity( 1001 );
    runFor( 0.5, false );
    SW_EXPECT_TRUE( view.findEntity( 1001 ) == nullptr );
}

/**
 * @brief [NetMMOTest] 관찰자가 멀리 순간 이동해 수백 개가 한꺼번에 나가도 클라이언트에 유령이 남지 않는다 — 나감을 메시지 상한 안에서 쪼갠다
 * @details 나감 하나에 모두 담던 때는 5 바이트 id 300 개 = 1500 B 가 상한을 넘어 보내기에서 버려졌는데, 서버는 보이는 목록에서 먼저 지워 다시 보내지 않았다.
 */
SW_TEST_CASE( NetMMOTest, TeleportLeavesWithoutGhosts )
{
    constexpr uint32 kNeighborCount = 300;
    constexpr uint32 kFirstID       = 0x10000000; // 가변 정수 5 바이트
    MMONetSimGame    game;
    game._table._listEntity.push_back( MMOEntity{
        vector<uint8>{},
        float3{ 0.0f, 0.0f, 0.0f },
        kMMOObserverID, 0, 1.0f
    } );
    for ( uint32 index = 0; index < kNeighborCount; ++index )
    {
        game._table._listEntity.push_back(
            MMOEntity{
                vector<uint8>{ 1 },
                float3{ static_cast<float32>( index % 20 ) * 2.0f, 0.0f, static_cast<float32>( index / 20 ) * 2.0f },
                kFirstID + index, 1, 1.0f
        } );
    }
    NetSimHarness harness;
    SW_ASSERT_TRUE( harness.initialize( NetSimSettings{}, &game ) );
    const int32 client = harness.addClient( NetSimLinkConditions{} );

    MMOCountGoal all{ client, static_cast<int32>( kNeighborCount + 1 ) };
    SW_ASSERT_TRUE_MSG( harness.stepUntil( &hasMMOEntityCount, &all, 300 ) > 0, "every neighbor enters" );

    game._table._listEntity[0]._position = float3{ 10000.0f, 0.0f, 10000.0f };
    MMOCountGoal alone{ client, 1 };
    const int32  leaveTicks = harness.stepUntil( &hasMMOEntityCount, &alone, 120 );
    SW_LOG_INFO( "[NetMMO] teleport away from %# neighbors: client left with %# entities after %# ticks (-1 = never)", kNeighborCount,
                 getMMOClient( harness, client ).getView().getEntityCount(), leaveTicks );
    SW_EXPECT_TRUE_MSG( 0 < leaveTicks && leaveTicks < 10, "every neighbor leaves the client within a few ticks" );
    SW_EXPECT_EQUAL( 1, getMMOServer( harness ).getServer().getVisibleCount( 0 ) );
}

/**
 * @brief [NetMMOTest] 순서가 뒤바뀐 비신뢰 갱신이 더 새 상태를 덮지 않는다 — 갱신에 서버 틱을 싣고, 클라이언트는 엔티티마다 마지막으로 적용한 틱보다 옛것을 버린다
 */
SW_TEST_CASE( NetMMOTest, ReorderedUpdateDoesNotOverwriteNewer )
{
    MMONetSimGame game;
    game._table._bStampTick                  = SW_TRUE;
    game._table._settings._updateBudgetBytes = 1000;
    game._table._listEntity.push_back( MMOEntity{
        vector<uint8>( 4, 0 ), float3{ 0.0f, 0.0f, 0.0f },
        kMMOObserverID, 0, 1.0f
    } );
    for ( uint32 index = 0; index < 20; ++index )
    {
        game._table._listEntity.push_back( MMOEntity{
            vector<uint8>( 4, 0 ), float3{ static_cast<float32>( index ), 0.0f, 3.0f },
            100 + index, 1, 1.0f
        } );
    }
    NetSimSettings settings;
    settings._hostSettings._sendInterval = 1.0 / 60.0;
    NetSimHarness harness;
    SW_ASSERT_TRUE( harness.initialize( settings, &game ) );
    NetEmulationConditions reorder;
    reorder._latency      = 0.03;
    reorder._jitter       = 0.02;
    reorder._reorderRate  = 0.3f;
    reorder._reorderDelay = 0.05;
    NetSimLinkConditions link;
    link._downstream   = reorder;
    const int32 client = harness.addClient( link );
    harness.stepTicks( 600 );

    const MMOClientSession& session = getMMOClient( harness, client );
    SW_LOG_INFO( "[NetMMO] 30 percent reordered downstream over 600 ticks: %# state regressions on the client, %# stale updates dropped", session.getRegressionCount(),
                 session.getView().getStaleUpdateCount() );
    SW_EXPECT_EQUAL( 21, session.getView().getEntityCount() );
    SW_EXPECT_EQUAL( 0, session.getRegressionCount() );
    SW_EXPECT_TRUE( session.getView().getStaleUpdateCount() > 0 ); // 실제로 뒤바뀐 것이 왔다
}

/**
 * @brief [NetMMOTest] 상태 상한(512 B)을 넘는 엔티티는 서버가 받지 않는다 — 클라이언트가 거절할 크기를 보내 두 쪽 보이는 목록이 어긋나지 않게
 * @details 서버가 그대로 보내던 때는 클라이언트가 들어옴을 깨짐으로 버려, 서버는 보인다고 알고 클라이언트에는 없는 채로 남았다.
 */
SW_TEST_CASE( NetMMOTest, OversizedStateIsRefusedByTheServer )
{
    MMONetSimGame game;
    game._table._listEntity.push_back( MMOEntity{
        vector<uint8>{},
        float3{ 0.0f, 0.0f, 0.0f },
        kMMOObserverID, 0, 1.0f
    } );
    game._table._listEntity.push_back( MMOEntity{
        vector<uint8>( 600, 3 ), float3{ 2.0f, 0.0f, 0.0f },
        50, 1, 1.0f
    } );
    game._table._listEntity.push_back( MMOEntity{
        vector<uint8>( NetMMOMessage::kMaxStateBytes, 4 ), float3{ 3.0f, 0.0f, 0.0f },
        51, 1, 1.0f
    } );
    NetSimHarness harness;
    SW_ASSERT_TRUE( harness.initialize( NetSimSettings{}, &game ) );
    const int32 client = harness.addClient( NetSimLinkConditions{} );
    harness.stepTicks( 60 );

    const MMOReplicator& server = getMMOServer( harness ).getServer();
    const MMOClientView& view   = getMMOClient( harness, client ).getView();
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( harness.findClient( client )->getRouter().getMalformedCount() ) );
    SW_EXPECT_EQUAL( server.getVisibleCount( 0 ), view.getEntityCount() );
    SW_EXPECT_NULL( view.findEntity( 50 ) );
    SW_ASSERT_NOT_NULL( view.findEntity( 51 ) );
    SW_EXPECT_EQUAL( size_t{ NetMMOMessage::kMaxStateBytes }, view.findEntity( 51 )->_listState.size() );
    SW_EXPECT_TRUE( server.getOversizedEntityCount() > 0 );
}

/**
 * @brief [NetMMOTest] 한 틱에 새로 보이는 엔티티는 신뢰 메시지 하나에 묶여 간다 — 상태 512 B 서른둘(17 KB)이 창 서른두 칸이 아니라 조각 수(17)만큼만 쓰고, 상태는 바이트 그대로 온다
 */
SW_TEST_CASE( NetMMOTest, EntersShareOneReliableMessagePerTick )
{
    constexpr uint32 kNeighborCount = 32;
    constexpr uint32 kFirstID       = 100;
    MMONetSimGame    game;
    game._table._listEntity.push_back( MMOEntity{
        vector<uint8>{},
        float3{ 0.0f, 0.0f, 0.0f },
        kMMOObserverID, 0, 1.0f
    } );
    for ( uint32 index = 0; index < kNeighborCount; ++index )
    {
        game._table._listEntity.push_back(
            MMOEntity{
                vector<uint8>( static_cast<size_t>( NetMMOMessage::kMaxStateBytes ), static_cast<uint8>( index + 1 ) ),
                float3{ static_cast<float32>( index % 8 ) * 2.0f, 0.0f, static_cast<float32>( index / 8 ) * 2.0f },
                kFirstID + index, 1, 1.0f
        } );
    }
    NetSimHarness harness;
    SW_ASSERT_TRUE( harness.initialize( NetSimSettings{}, &game ) );
    const int32 client = harness.addClient( NetSimLinkConditions{} );

    // 들어옴이 나가는 틱의 서버 쪽 미확인 신뢰 수 — 엔티티마다 메시지 하나면 32 를 넘고, 묶으면 조각 17 남짓이다.
    MMOCountGoal all{ client, static_cast<int32>( kNeighborCount + 1 ) };
    int32        maxPending = 0;
    for ( int32 tick = 0; tick < 120 && hasMMOEntityCount( harness, &all ) == false; ++tick )
    {
        harness.stepTicks( 1 );
        const NetConnection* pConnection = harness.getServer().getHost().findConnection( 0 );
        if ( pConnection != nullptr )
            maxPending = MathUtil::max( maxPending, pConnection->getPendingReliableCount() );
    }
    SW_ASSERT_TRUE_MSG( hasMMOEntityCount( harness, &all ), "every neighbor enters" );
    SW_EXPECT_TRUE_MSG( maxPending <= 20, "the enters of one tick take the fragments of one message, not one window slot per entity" );
    const MMOClientView& view = getMMOClient( harness, client ).getView();
    for ( uint32 index = 0; index < kNeighborCount; ++index )
    {
        const MMOEntity* pEntity = view.findEntity( kFirstID + index );
        SW_ASSERT_TRUE( pEntity != nullptr );
        SW_EXPECT_TRUE( pEntity->_listState == game._table._listEntity[index + 1]._listState );
    }
}

/**
 * @brief [NetMMOTest] 연결 상한을 낮추면 MMO 갱신 예산이 따라 준다 — 모두 매 틱 바뀌는 이웃 스물을 기본 상한과 6 KB/s(60 Hz 몫 50 B)에서 120 틱 보내 보낸 갱신 수를 견준다
 */
SW_TEST_CASE( NetMMOTest, UpdateBudgetFollowsTheConnectionCap )
{
    const auto countUpdates = []( int32 maxBytesPerSecond ) -> uint64
    {
        MMONetSimGame game;
        game._table._bStampTick = SW_TRUE;
        game._table._listEntity.push_back( MMOEntity{
            vector<uint8>{},
            float3{ 0.0f, 0.0f, 0.0f },
            kMMOObserverID, 0, 1.0f
        } );
        for ( uint32 index = 0; index < 20; ++index )
        {
            game._table._listEntity.push_back( MMOEntity{
                vector<uint8>( 4, 0 ),
                float3{ static_cast<float32>( index % 5 ) * 2.0f, 0.0f, static_cast<float32>( index / 5 ) * 2.0f },
                200 + index, 1, 1.0f
            } );
        }
        NetSimSettings settings;
        settings._hostSettings._maxBytesPerSecond = maxBytesPerSecond;
        NetSimHarness harness;
        if ( harness.initialize( settings, &game ) == false )
            return 0;
        (void)harness.addClient( NetSimLinkConditions{} );
        harness.stepTicks( 120 );
        return getMMOServer( harness ).getServer().getSentUpdateCount();
    };
    const uint64 defaultCount = countUpdates( NetHostSettings{}._maxBytesPerSecond );
    const uint64 cappedCount  = countUpdates( 6000 );
    SW_LOG_INFO( "[NetMMO] updates in 120 ticks: default cap %#, 6 KB/s cap %#", defaultCount, cappedCount );
    SW_EXPECT_TRUE( defaultCount > 1000 );            // 600 B 예산 — 스물이 거의 매 틱
    SW_EXPECT_TRUE( cappedCount * 3 < defaultCount ); // 50 B 예산 — 틱마다 두셋
}

/**
 * @brief [NetMMOTest] 잃은 갱신의 엔티티는 확인을 보고 곧 다시 간다 — 서버가 보낸 순간 "안 바뀜" 으로 보고 한 차례를 통째로 기다리지 않는다
 * @details 먼 엔티티(낮은 우선도) 하나가 바뀐 직후 내리막을 30 틱 모두 잃는다. 그 사이 서버는 그 갱신을 보낸다(바뀜 가속). 회선이 돌아온 뒤
 *          클라이언트가 새 상태를 볼 때까지의 틱을 잰다. 가까운 여덟이 틱마다 바뀌어 예산(200 B)을 채운다.
 */
SW_TEST_CASE( NetMMOTest, LostUpdateIsResentSoonAfterTheAck )
{
    constexpr uint32 kFarID = 200;
    MMONetSimGame    game;
    game._table._settings._updateBudgetBytes = 200;
    game._table._listEntity.push_back( MMOEntity{
        vector<uint8>( 4, 0 ), float3{ 0.0f, 0.0f, 0.0f },
        kMMOObserverID, 0, 1.0f
    } );
    for ( uint32 index = 0; index < 8; ++index )
    {
        game._table._listEntity.push_back( MMOEntity{
            vector<uint8>( 16, 0 ), float3{ static_cast<float32>( index + 1 ), 0.0f, 1.0f },
            100 + index, 1, 1.0f
        } );
    }
    game._table._listEntity.push_back( MMOEntity{
        vector<uint8>( 16, 0 ), float3{ 50.0f, 0.0f, 0.0f },
        kFarID, 1, 1.0f
    } );
    NetSimSettings settings;
    settings._hostSettings._sendInterval = 1.0 / 60.0;
    NetSimHarness harness;
    SW_ASSERT_TRUE( harness.initialize( settings, &game ) );
    const int32 client = harness.addClient( NetSimLinkConditions{} );
    for ( uint32 tick = 0; tick < 120; ++tick )
    {
        for ( size_t index = 1; index <= 8; ++index )
        {
            ++game._table._listEntity[index]._listState[0];
        }
        harness.step();
    }
    const MMOClientSession& session = getMMOClient( harness, client );
    SW_ASSERT_NOT_NULL( session.getView().findEntity( kFarID ) );

    NetEmulationConditions dropAll;
    dropAll._lossRate = 1.0f;
    NetSimLinkConditions blackout;
    blackout._downstream = dropAll;
    harness.setLinkConditions( client, blackout );
    game._table._listEntity.back()._listState[0] = 7; // 먼 엔티티가 한 번 바뀐다 — 이 갱신은 잃는다
    for ( uint32 tick = 0; tick < 30; ++tick )
    {
        for ( size_t index = 1; index <= 8; ++index )
        {
            ++game._table._listEntity[index]._listState[0];
        }
        harness.step();
    }
    harness.setLinkConditions( client, NetSimLinkConditions{} );

    uint32 ticksToSee = 0;
    while ( ticksToSee < 300 && session.getView().findEntity( kFarID )->_listState[0] != 7 )
    {
        for ( size_t index = 1; index <= 8; ++index )
        {
            ++game._table._listEntity[index]._listState[0];
        }
        harness.step();
        ++ticksToSee;
    }
    SW_LOG_INFO( "[NetMMO] far entity changed during a 30-tick downstream blackout: seen %# ticks after the link came back", ticksToSee );
    // 판정 없이(보낸 순간 "안 바뀜") 13 틱, 확인으로 판정하면 4 틱. 고친 값 + 3.
    SW_EXPECT_TRUE( ticksToSee <= 7u );
}
