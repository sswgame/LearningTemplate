#include "pch.h"

#include "Core/Container/vector.h"

#include "GameFramework/Kits/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Kits/Network/NetClientServer/ReplicationServer.h"
#include "GameFramework/Kits/Network/NetSimulation/NetSimHarness.h"

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
                        entity._buffer[index] = static_cast<uint8>( tick >> ( 8 * index ) );
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

    /** @brief 클라이언트 — 받은 스냅숏을 쌓는다(시험은 가장 새 것을 본다). */
    class TableClientSession final : public INetSimSession
    {
    public:
        explicit TableClientSession( NetSimWorld& world )
            : _client{}
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
        }

        const ReplicationClient& getClient() const { return _client; }

    private:
        ReplicationClient _client;
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
        game._table._listEntity.push_back( NetEntityState{ vector<uint8>( 4, static_cast<uint8>( index ) ), kFirstId + index, 1 } );
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
