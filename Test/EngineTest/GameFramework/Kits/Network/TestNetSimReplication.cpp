#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationServer.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimHarness.h"

#include "TestFramework/TestFramework.h"

// 복제 키트를 가상 서버 하니스 위에서 — 메시지 상한 · 예산이 늘 찬 회선에서 스냅숏이 끊기지 않고 따라오는지, 숫자(틱)로 잰다.
// 서버 세션은 시험이 채운 엔티티 표를 틱마다 그대로 복제하고, 클라이언트 세션은 받은 가장 새 스냅숏을 본다.

using namespace sw;

namespace
{
    constexpr float32 kReplicationTickSeconds = 1.0f / 60.0f;

    /** @brief 시험이 틱 사이에 바꾸는 서버 쪽 엔티티 표와 설정입니다. */
    struct ReplicationTable
    {
        vector<NetEntityState>    _listEntity{};
        ReplicationServerSettings _settings{};
        const IReplicationPolicy* _pPolicy{ nullptr };
        uint8                     _bStampTick{ SW_FALSE }; ///< 상태 앞 4 바이트에 서버 틱을 적는다(틱마다 바뀌고, 받은 쪽이 낡은 정도를 잰다)
    };

    class TableServerSession final : public INetSimSession
    {
    public:
        TableServerSession( NetSimWorld& world, ReplicationTable* pTable )
            : _server{}
            , _pTable{ pTable }
        {
            _server.initialize( &world.getHost(), pTable->_settings, pTable->_pPolicy );
            world.getRouter().addHandler( &_server );
        }

        void onTickEnd( NetSimWorld& world, float32 deltaTime ) override
        {
            (void)deltaTime;
            const uint32 tick = world.getLocalTick();
            _server.beginTick( tick );
            for ( NetEntityState& entity : _pTable->_listEntity )
            {
                if ( _pTable->_bStampTick == SW_TRUE && entity._buffer.size() >= 4 )
                {
                    for ( size_t index = 0; index < 4; ++index )
                    {
                        entity._buffer[index] = static_cast<uint8>( tick >> ( 8 * index ) );
                    }
                }
                _server.setEntity( entity._entityId, entity._typeId, entity._buffer );
            }
            _server.endTick();
            _server.sendSnapshots();
        }

    private:
        ReplicationServer _server;
        ReplicationTable* _pTable;
    };

    /** @brief 클라이언트 — 가장 새 스냅숏과, 엔티티마다 그 상태가 서버 틱 몇에 만들어졌는지(틱 도장)의 가장 큰 뒤처짐을 든다. */
    class TableClientSession final : public INetSimSession
    {
    public:
        explicit TableClientSession( NetSimWorld& world )
            : _client{}
            , _listMaxStaleness{}
            , _measureFromTick{ 0xFFFFFFFFu }
            , _lastSeenTick{ 0 }
        {
            ReplicationClientSettings settings;
            settings._tickInterval = kReplicationTickSeconds;
            _client.initialize( &world.getHost(), settings );
            world.getRouter().addHandler( &_client );
        }

        void onTickBegin( NetSimWorld& world, float32 deltaTime ) override
        {
            (void)world;
            _client.update( deltaTime );
            const NetSnapshot* pLatest = _client.getLatest();
            if ( pLatest == nullptr || pLatest->_tick == _lastSeenTick )
                return;
            _lastSeenTick = pLatest->_tick;
            if ( pLatest->_tick < _measureFromTick )
                return;
            for ( const NetEntityState& entity : pLatest->_listEntity )
            {
                if ( entity._buffer.size() < 4 || entity._entityId >= _listMaxStaleness.size() )
                    continue;
                uint32 stampTick = 0;
                for ( size_t index = 0; index < 4; ++index )
                {
                    stampTick |= static_cast<uint32>( entity._buffer[index] ) << ( 8 * index );
                }
                uint32& maxStaleness = _listMaxStaleness[entity._entityId];
                maxStaleness         = MathUtil::max( maxStaleness, pLatest->_tick - stampTick );
            }
        }

        /** @brief 이 틱부터 받은 스냅숏으로 낡은 정도를 잽니다. 엔티티 id 는 @p entityCount 보다 작다. */
        void beginMeasure( uint32 fromTick, uint32 entityCount )
        {
            _measureFromTick = fromTick;
            _listMaxStaleness.assign( entityCount, 0u );
        }

        const ReplicationClient& getClient() const { return _client; }
        /** @brief 잰 동안 그 엔티티의 상태가 서버보다 가장 많이 뒤처진 틱 수입니다. */
        uint32 getMaxStaleness( uint32 entityId ) const { return _listMaxStaleness[entityId]; }

    private:
        ReplicationClient _client;
        vector<uint32>    _listMaxStaleness; ///< 엔티티 id 자리
        uint32            _measureFromTick;
        uint32            _lastSeenTick;
    };

    class TableGame final : public INetSimGame
    {
    public:
        unique_ptr<INetSimSession> createSession( NetSimWorld& world ) override
        {
            if ( world.isServer() )
                return make_unique<TableServerSession>( world, &_table );
            return make_unique<TableClientSession>( world );
        }

        ReplicationTable _table;
    };

    /** @brief 종류 번호를 우선도로 씁니다(종류 10 = 우선도 10). */
    class TypePriorityPolicy final : public IReplicationPolicy
    {
    public:
        float32 computePriority( int32 connectionId, const NetEntityState& entity ) const override
        {
            (void)connectionId;
            return static_cast<float32>( entity._typeId );
        }
    };

    TableClientSession& getTableClient( const NetSimHarness& harness, int32 worldIndex )
    {
        return *static_cast<TableClientSession*>( harness.findClient( worldIndex )->getSession() );
    }

    struct EntityCountGoal
    {
        int32  _worldIndex{ 0 };
        size_t _entityCount{ 0 };
        uint32 _fromTick{ 0 };
    };

    /** @brief 클라이언트의 가장 새 스냅숏이 @p _fromTick 이후이고 엔티티 수가 목표와 같은가입니다. */
    bool hasEntityCount( const NetSimHarness& harness, void* pContext )
    {
        const EntityCountGoal& goal    = *static_cast<const EntityCountGoal*>( pContext );
        const NetSnapshot*     pLatest = getTableClient( harness, goal._worldIndex ).getClient().getLatest();
        return pLatest != nullptr && pLatest->_tick >= goal._fromTick && pLatest->_listEntity.size() == goal._entityCount;
    }
} // namespace

/**
 * @brief [NetSimReplicationTest] 한 틱에 엔티티 수백 개가 사라져도 스냅숏이 메시지 상한(1024 B) 안에 머물러 클라이언트가 계속 따라온다
 * @details 예산이 사라진 목록을 세지 않고 id 를 3 바이트로 어림하던 때는, 3 바이트 id 400 개가 사라지면 스냅숏이 1200 B 를 넘어 보내기에서 통째로 버려졌다.
 *          확인이 오지 않으니 기준이 그대로이고 다음 틱도 같은 크기라, 클라이언트의 가장 새 틱이 그 자리에 영영 멈췄다(라이브락).
 */
SW_TEST_CASE( NetSimReplicationTest, MassRemovalStaysUnderMessageLimit )
{
    constexpr uint32 kEntityCount = 400;
    constexpr uint32 kFirstId     = 1000000; // 가변 정수 3 바이트
    TableGame        game;
    for ( uint32 index = 0; index < kEntityCount; ++index )
    {
        game._table._listEntity.push_back( NetEntityState{ vector<uint8>( 4, static_cast<uint8>( index ) ), kFirstId + index, 1 } );
    }
    NetSimHarness harness;
    SW_ASSERT_TRUE( harness.initialize( NetSimSettings{}, &game ) );
    const int32 client = harness.addClient( NetSimLinkConditions{} );

    EntityCountGoal full{ client, kEntityCount, 0 };
    const int32     fillTicks = harness.stepUntil( &hasEntityCount, &full, 600 );
    SW_ASSERT_TRUE_MSG( fillTicks > 0, "the client receives every entity" );
    harness.stepTicks( 30 ); // 서버의 확인된 기준도 400 개가 되게(그래야 사라짐 400 개가 한 델타에 몰린다)

    game._table._listEntity.clear();
    EntityCountGoal empty{ client, 0, harness.getServer().getLocalTick() };
    const int32     clearTicks = harness.stepUntil( &hasEntityCount, &empty, 600 );
    SW_LOG_INFO( "[NetSimReplication] %# entities with 3-byte ids: filled in %# ticks, all removed on the client %# ticks after the removal", kEntityCount, fillTicks,
                 clearTicks );
    SW_EXPECT_TRUE_MSG( clearTicks > 0, "removals that do not fit one message go out over several snapshots" );
    SW_EXPECT_TRUE( clearTicks < 30 );
    // 그 뒤로도 스냅숏이 흐른다(가장 새 틱이 멈추지 않는다).
    const uint32 latestTick = getTableClient( harness, client ).getClient().getLatest()->_tick;
    harness.stepTicks( 10 );
    SW_EXPECT_TRUE( getTableClient( harness, client ).getClient().getLatest()->_tick > latestTick );
}

/**
 * @brief [NetSimReplicationTest] 예산이 늘 차는 회선에서도 낮은 우선도 엔티티가 굶지 않는다 — 우선도를 스냅샷마다 쌓고 실은 것만 0 으로 돌린다
 * @details 우선도 10 엔티티 넷(틱마다 바뀜, 각 244 B)이 1000 B 예산을 채운다. 우선도를 틱마다 새로 매겨 정렬하던 때는 우선도 1 엔티티가 바뀐 채 영영
 *          실리지 않아 클라이언트에 나타나지도 않았다.
 */
SW_TEST_CASE( NetSimReplicationTest, LowPriorityEntitiesAreNotStarved )
{
    constexpr uint32   kHighCount = 4;
    constexpr uint32   kLowId     = kHighCount + 1;
    TypePriorityPolicy policy;
    TableGame          game;
    game._table._pPolicy    = &policy;
    game._table._bStampTick = SW_TRUE;
    for ( uint32 entityId = 1; entityId <= kHighCount; ++entityId )
    {
        game._table._listEntity.push_back( NetEntityState{ vector<uint8>( 240, 0 ), entityId, 10 } );
    }
    game._table._listEntity.push_back( NetEntityState{ vector<uint8>( 240, 0 ), kLowId, 1 } );
    NetSimSettings settings;
    settings._hostSettings._sendInterval = 1.0 / 60.0; // 스냅숏마다 한 패킷 — 순서만 채널이 앞 스냅숏을 지우지 않게
    NetSimHarness harness;
    SW_ASSERT_TRUE( harness.initialize( settings, &game ) );
    const int32 client = harness.addClient( NetSimLinkConditions{} );
    harness.stepTicks( 60 );

    TableClientSession& session = getTableClient( harness, client );
    session.beginMeasure( harness.getServer().getLocalTick(), kLowId + 1 );
    harness.stepTicks( 600 );
    const NetSnapshot* pLatest = session.getClient().getLatest();
    SW_ASSERT_NOT_NULL( pLatest );
    uint32 maxHighStaleness = 0;
    for ( uint32 entityId = 1; entityId <= kHighCount; ++entityId )
    {
        maxHighStaleness = MathUtil::max( maxHighStaleness, session.getMaxStaleness( entityId ) );
    }
    const bool bLowSeen = pLatest->findEntity( kLowId ) != nullptr;
    SW_LOG_INFO( "[NetSimReplication] saturated budget over 600 ticks: priority-10 entities at most %# ticks stale, priority-1 entity %# (seen %#)", maxHighStaleness,
                 session.getMaxStaleness( kLowId ), bLowSeen );
    SW_ASSERT_TRUE_MSG( bLowSeen, "the low-priority entity reaches the client" );
    // 쌓인 1 이 10 을 넘는 열한 번째쯤 — 실린 스냅숏이 패킷으로 나가기 전에 다음 스냅숏에 밀리면(순서만 채널) 한 차례 더.
    SW_EXPECT_TRUE( session.getMaxStaleness( kLowId ) <= 24 );
    SW_EXPECT_TRUE( maxHighStaleness <= 4 ); // 높은 것은 여전히 거의 매번
}

/**
 * @brief [NetSimReplicationTest] 잃은 스냅숏에 실렸던 낮은 우선도 엔티티는 확인을 보고 바로 다시 앞선다 — 한 차례(우선도 비)를 통째로 다시 기다리지 않는다
 * @details 내리막 손실 20 %. 서버가 실은 순간 우선도를 0 으로 돌리기만 하면, 잃은 상태는 쌓인 1 이 10 을 넘을 때까지(열한 번째쯤) 다시 기다린다.
 */
SW_TEST_CASE( NetSimReplicationTest, LowPriorityStateLostInASnapshotIsResentSoon )
{
    constexpr uint32   kHighCount = 4;
    constexpr uint32   kLowId     = kHighCount + 1;
    TypePriorityPolicy policy;
    TableGame          game;
    game._table._pPolicy    = &policy;
    game._table._bStampTick = SW_TRUE;
    for ( uint32 entityId = 1; entityId <= kHighCount; ++entityId )
    {
        game._table._listEntity.push_back( NetEntityState{ vector<uint8>( 240, 0 ), entityId, 10 } );
    }
    game._table._listEntity.push_back( NetEntityState{ vector<uint8>( 240, 0 ), kLowId, 1 } );
    NetSimSettings settings;
    settings._hostSettings._sendInterval = 1.0 / 60.0;
    NetSimHarness harness;
    SW_ASSERT_TRUE( harness.initialize( settings, &game ) );
    NetEmulationConditions lossy;
    lossy._latency  = 0.02;
    lossy._lossRate = 0.2f;
    NetSimLinkConditions link;
    link._downstream   = lossy;
    const int32 client = harness.addClient( link );
    harness.stepTicks( 60 );

    TableClientSession& session = getTableClient( harness, client );
    session.beginMeasure( harness.getServer().getLocalTick(), kLowId + 1 );
    harness.stepTicks( 1200 );
    SW_LOG_INFO( "[NetSimReplication] 20 percent downstream loss over 1200 ticks: priority-1 entity at most %# ticks stale", session.getMaxStaleness( kLowId ) );
    // 씨앗이 고정이라 값은 결정적이다 — 판정 없이(실은 순간 0 으로만) 96, 판정하면 89. 고친 값 + 3.
    SW_EXPECT_TRUE( session.getMaxStaleness( kLowId ) <= 92u );
}
