#include "pch.h"

#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationServer.h"

// 복제 휴면 — 판이 오르지 않은 엔티티는 연결마다의 평가에서 빠지고(평가 수가 깨어 있는 수에 비례), 받는 쪽에는 그대로 남으며, 바뀌면 깨어 다음 스냅샷에 실린다.

using namespace sw;

namespace
{
    /** @brief 평가 횟수를 세는 정책입니다 — 모두 관련, 우선도 1. 작업 스레드를 쓰지 않는 서버에서만 쓴다(세는 칸이 공유된다). */
    class DormancyCountingPolicy final : public IReplicationPolicy
    {
    public:
        mutable size_t _evaluateCount{ 0 }; // 테스트 계측 — 한 스레드에서만 부른다

        bool evaluateEntity( int32 connectionID, const NetEntityState& entity, float32& outPriority ) const override
        {
            (void)connectionID;
            (void)entity;
            ++_evaluateCount;
            outPriority = 1.0f;
            return true;
        }
    };

    /** @brief 서버 하나(휴면 문턱 4 틱)와 클라이언트 하나를 루프백으로 잇습니다. 엔티티 1~100 중 1~5 만 틱마다 바뀐다. */
    struct ReplicationDormancyTestRig
    {
        static constexpr uint32 kEntityCount  = 100;
        static constexpr uint32 kChangedCount = 5;

        test::LoopbackCluster  _cluster{ 13u };
        DormancyCountingPolicy _policy;
        ReplicationServer      _server;
        NetClock               _worldClock;
        ReplicationClient      _client;

        ReplicationDormancyTestRig()
        {
            const NetEndpointConfig settings;
            (void)_cluster.addEndpoint( 4500, settings ); // 빈 클러스터라 포트가 겹치지 않는다
            (void)_cluster.addEndpoint( 4600, settings );
            SW_EXPECT_TRUE( _cluster.connectClients( 30 ) );
            ReplicationServerConfig serverSettings;
            serverSettings._dormancyTicks = 4;
            SW_EXPECT_TRUE( _server.initialize( &_cluster.getServer(), serverSettings, &_policy ) );
            SW_EXPECT_TRUE( _client.initialize( &_cluster.getClient( 0 ), _worldClock, ReplicationClientConfig{} ) );
        }

        void pollUntilDelivered()
        {
            vector<uint8>  buffer;
            int32          connectionID = -1;
            NetChannelKind channel      = NetChannelKind::Unreliable;
            for ( int32 step = 0; step < 8; ++step )
            {
                _cluster.step();
                while ( _cluster.getServer().receiveMessage( connectionID, channel, buffer ) )
                {
                    (void)_server.handleMessage( connectionID, buffer ); // 클라이언트의 확인
                }
                while ( _cluster.getClient( 0 ).receiveMessage( connectionID, channel, buffer ) )
                {
                    (void)_client.handleMessage( 0, buffer ); // 받은 스냅샷
                }
            }
        }

        /** @brief 틱 하나를 싣고 보내 닿게 합니다. @p wokenEntityID 가 0 이 아니면 그 엔티티의 값도 이번 틱에 바꾼다. @return 이번 틱의 평가 횟수 */
        size_t sendTick( uint32 tick, uint32 wokenEntityID )
        {
            _server.beginTick( tick );
            for ( uint32 entityID = 1; entityID <= kEntityCount; ++entityID )
            {
                const bool  bChanged = entityID <= kChangedCount || entityID == wokenEntityID;
                const uint8 value    = static_cast<uint8>( bChanged ? tick : entityID );
                _server.setEntity( entityID, 1, vector<uint8>{ value, static_cast<uint8>( entityID ) } );
            }
            _server.endTick();
            const size_t before = _policy._evaluateCount;
            _server.sendSnapshots();
            const size_t evaluated = _policy._evaluateCount - before;
            pollUntilDelivered();
            return evaluated;
        }
    };
} // namespace

/** @brief [ReplicationDormancyTest] 안 바뀐 엔티티는 문턱 뒤 휴면에 들어 평가가 깨어 있는 수만큼이 되고, 받는 쪽에서는 지워지지 않는다. */
SW_TEST_CASE( ReplicationDormancyTest, QuietEntitiesLeaveEvaluationButStayOnClient )
{
    ReplicationDormancyTestRig rig;
    using Rig = ReplicationDormancyTestRig;
    SW_EXPECT_EQUAL( size_t{ Rig::kEntityCount }, rig.sendTick( 1, 0 ) ); // 새 연결의 첫 스냅샷은 전부 평가한다
    size_t lastEvaluated = 0;
    for ( uint32 tick = 2; tick <= 10; ++tick )
    {
        lastEvaluated = rig.sendTick( tick, 0 );
    }
    SW_EXPECT_EQUAL( size_t{ Rig::kChangedCount }, lastEvaluated ); // 연결당 평가 수 = 깨어 있는(바뀐) 수(전체 100 이 아니다)
    const NetSnapshot* pLatest = rig._client.getLatest();
    SW_REQUIRE_NOT_NULL( pLatest );
    SW_EXPECT_EQUAL( size_t{ Rig::kEntityCount }, pLatest->_listEntity.size() ); // 휴면 엔티티는 받는 쪽 기준에 그대로 있다
}

/** @brief [ReplicationDormancyTest] 휴면 엔티티는 상태를 쓰는 창구(`setEntity` → `endTick` 의 판 오름)만으로 깨어 그 틱 스냅샷에 실린다. */
SW_TEST_CASE( ReplicationDormancyTest, ChangedEntityWakesAndIsSentNextSnapshot )
{
    ReplicationDormancyTestRig rig;
    using Rig = ReplicationDormancyTestRig;
    for ( uint32 tick = 1; tick <= 10; ++tick )
    {
        (void)rig.sendTick( tick, 0 );
    }
    const size_t evaluated = rig.sendTick( 11, 50 ); // 휴면이던 50 이 바뀐다 — 깨우는 호출은 없다
    SW_EXPECT_EQUAL( size_t{ Rig::kChangedCount + 1 }, evaluated );
    const NetSnapshot* pLatest = rig._client.getLatest();
    SW_REQUIRE_NOT_NULL( pLatest );
    SW_EXPECT_EQUAL( uint32{ 11 }, pLatest->_tick );
    const NetEntityState* pWoken = pLatest->findEntity( 50 );
    SW_REQUIRE_NOT_NULL( pWoken );
    SW_REQUIRE_EQUAL( size_t{ 2 }, pWoken->_buffer.size() );
    SW_EXPECT_EQUAL( uint8{ 11 }, pWoken->_buffer[0] );
    SW_EXPECT_EQUAL( size_t{ Rig::kEntityCount }, pLatest->_listEntity.size() );
}
