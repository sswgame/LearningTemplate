#include "pch.h"

#include "EngineTest/NetSimDestructionScenario.h"

#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionNetSession.h"

#include "TestFramework/TestFramework.h"

// 깨끗한 회선 — 구조 수렴 · 덩어리 · 파편 레이어 · 늦은 참가 스냅숏 · 빠진 사건 복구. 시나리오는 NetSimDestructionScenario.h.
// 실제 세션의 복제기(`DestructionNetSession` — 전용 서버의 `DestructionNetSessionComponent` 가 쓰는 것)도 같은 쇼케이스로 본다.

using namespace test;

namespace
{
    /** @brief 월드마다 `DestructionNetSession` 하나 — 서버는 월드 틱 뒤, 클라이언트는 받은 메시지를 나눈 뒤 부른다(컴포넌트의 실제 실행과 같은 순서). */
    class NetSessionWorld final : public sw::INetSimSession
    {
    public:
        NetSessionWorld( sw::NetSimWorld& world, const sw::SceneDocument& document )
            : _session{}
        {
            (void)world.getScene().instantiate( document ); // 쇼케이스 문서는 테스트 머리에서 읽었다
            const sw::DestructionNetSide side = world.isServer() ? sw::DestructionNetSide::Server : sw::DestructionNetSide::Client;
            _session.initialize( world.getPeers(), world.getRouter(), world.getObjectManager(), side, sw::DestructionReplicationConfig{} );
        }

        void onTickBegin( sw::NetSimWorld& world, float32 deltaTime ) override
        {
            if ( world.isServer() == false )
                _session.update( deltaTime );
        }
        void onTickEnd( sw::NetSimWorld& world, float32 deltaTime ) override
        {
            if ( world.isServer() )
                _session.update( deltaTime );
        }
        void onWorldStopping( sw::NetSimWorld& world ) override
        {
            (void)world;
            _session.shutdown();
        }

        sw::DestructionNetSession _session;
    };

    class NetSessionGame final : public sw::INetSimGame
    {
    public:
        explicit NetSessionGame( const sw::SceneDocument& document )
            : _pDocument{ &document }
        {
        }

        sw::unique_ptr<sw::INetSimSession> createSession( sw::NetSimWorld& world ) override { return sw::make_unique<NetSessionWorld>( world, *_pDocument ); }

    private:
        const sw::SceneDocument* _pDocument;
    };

    sw::DestructionNetSession& getNetSession( const sw::NetSimWorld& world )
    {
        return static_cast<NetSessionWorld*>( world.getSession() )->_session;
    }

    /** @brief 클라이언트마다 오브젝트별 마지막 해시 기록이 서버와 같으면 true 입니다(하니스 `stepUntil` 조건). */
    bool doAllClientsMatchServer( const sw::NetSimHarness& harness, void* pContext )
    {
        (void)pContext;
        const sw::DestructionNetSession& server = getNetSession( harness.getServer() );
        sw::vector<sw::NetSimWorld*>     listClient;
        harness.collectClients( listClient );
        for ( sw::NetSimWorld* pClient : listClient )
        {
            const sw::vector<sw::DestructionHashRecord>& listServer = server.getHashRecords();
            const sw::vector<sw::DestructionHashRecord>& listLocal  = getNetSession( *pClient ).getHashRecords();
            for ( uint32 netID = 1; netID <= server.getObjectCount(); ++netID )
            {
                uint64 serverHash = 0;
                uint64 localHash  = 0;
                for ( const sw::DestructionHashRecord& record : listServer )
                {
                    if ( record._netID == netID )
                        serverHash = record._hash;
                }
                for ( const sw::DestructionHashRecord& record : listLocal )
                {
                    if ( record._netID == netID )
                        localHash = record._hash;
                }
                if ( serverHash != localHash )
                    return false;
            }
        }
        return server.getHashRecords().empty() == false;
    }
} // namespace

/**
 * @brief [NetSimDestructionTest] 실제 세션의 복제기(`DestructionNetSession`)로 서버 하나 · 클라이언트 둘(하나는 늦게)이 같은 구조가 되고, 해시 기록이 (번호, 사건 수)마다 서버와 같다
 * @details 전용 서버 ↔ 클라이언트 프로세스의 해시 로그 비교(`Scripts/dev/CompareDestructionHashLogs.py`)가 맞대는 것과 같은 기록이다. 늦은 클라이언트는 스냅숏으로
 *          사건 수를 건너뛰므로 일부 번호만 기록한다 — 겹치는 기록이 모두 같으면 된다.
 */
SW_TEST_CASE( NetSimDestructionTest, NetSessionHashRecordsMatchTheServer )
{
    SceneDocument document;
    SW_ASSERT_TRUE( loadShowcase( document ) );
    NetSessionGame game{ document };
    NetSimHarness  harness;
    NetSimConfig   settings;
    settings._seed = 5u;
    SW_ASSERT_TRUE( harness.initialize( sw::SceneEngineServices::makeFromBoundEngine(), settings, &game ) );
    SW_ASSERT_TRUE( harness.addClient( NetSimLinkConditions{} ) > 0 );
    harness.stepTicks( 240 );
    SW_ASSERT_TRUE( harness.addClient( NetSimLinkConditions{} ) > 0 ); // 늦은 참가 — 스냅숏으로 맞춘다
    harness.stepTicks( 240 );
    SW_EXPECT_TRUE_MSG( harness.stepUntil( &doAllClientsMatchServer, nullptr, 600 ) >= 0, "every client ends with the server's structure hashes" );

    DestructionNetSession&               server     = getNetSession( harness.getServer() );
    const vector<DestructionHashRecord>& listServer = server.getHashRecords();
    SW_ASSERT_TRUE( listServer.empty() == false );
    vector<NetSimWorld*> listClient;
    harness.collectClients( listClient );
    SW_ASSERT_EQUAL( size_t( 2 ), listClient.size() );
    uint32 sharedCount = 0;
    for ( NetSimWorld* pClient : listClient )
    {
        for ( const DestructionHashRecord& local : getNetSession( *pClient ).getHashRecords() )
        {
            for ( const DestructionHashRecord& remote : listServer )
            {
                if ( remote._netID != local._netID || remote._eventCount != local._eventCount )
                    continue;
                ++sharedCount;
                SW_EXPECT_TRUE_MSG( remote._hash == local._hash, DestructionNetSession::formatHashLine( DestructionNetSide::Client, local, server.findObjectName( local._netID ) ).c_str() );
            }
        }
    }
    SW_EXPECT_TRUE_MSG( sharedCount > 0, "the client logs share (object, event count) keys with the server" );

    // 로그 줄 형식은 비교 스크립트의 계약이다.
    const DestructionHashRecord record{ 0xABCDEFull, 3u, 12u };
    SW_EXPECT_TRUE( DestructionNetSession::formatHashLine( DestructionNetSide::Server, record, "BrickWall" ) ==
                    "[DestructionHash] side=server id=3 events=12 hash=0000000000abcdef name=BrickWall" );
}

/**
 * @brief [NetSimDestructionTest] 깨끗한 회선 — 클라이언트 셋이 서버와 같은 구조가 되고(덩어리는 서버를 따르고 파편은 캐릭터와 부딪히지 않는다), 다 부서진
 *        뒤 들어온 넷째는 스냅숏 하나로 같아지고, 사건 하나를 빼먹은 첫째는 해시 비교로 알아채 스냅숏으로 바로잡힌다
 */
SW_TEST_CASE( NetSimDestructionTest, CleanLinkConvergesLateJoinsAndRepairs )
{
    SceneDocument document;
    SW_ASSERT_TRUE( loadShowcase( document ) );
    ScenarioOptions options;
    options._lateJoinTick       = 360;
    options._skipEventClient    = 0;
    options._tickCount          = 480;
    const ScenarioResult result = runShowcase( document, options );
    logResult( "clean + late join + dropped event", result );
    expectConverged( result, 0.1f );
    SW_EXPECT_TRUE_MSG( 0 < result._lateJoinMatchTicks && result._lateJoinMatchTicks < 120, "the late joiner matches within two seconds" );
    SW_EXPECT_TRUE( result._hashMismatchCount >= 1 );
    SW_EXPECT_TRUE_MSG( 0 < result._desyncRecoverTicks && result._desyncRecoverTicks < 180, "the dropped event is repaired within three seconds" );
}

/**
 * @brief [NetSimDestructionTest] 위치 범위(`_positionRange`) 밖으로 나간 덩어리는 움직이는 자세를 보내지 않고 세며, 멈추면 비트 그대로 보내 서버와 같은 자리가 된다
 * @details 예전에는 범위 밖 위치를 경계로 잘라 보내 받는 쪽에서 덩어리가 경계에 붙어 보였다. 범위를 1 m 로 줄여 쇼케이스의 덩어리가 모두 범위 밖이 되게 한다.
 */
SW_TEST_CASE( NetSimDestructionTest, ChunksOutsideThePositionRangeAreNotSentMovingButRestExactly )
{
    SceneDocument document;
    SW_ASSERT_TRUE( loadShowcase( document ) );
    ScenarioOptions options;
    options._tickCount                        = 480;
    options._clientCount                      = 1;
    options._replicationConfig._positionRange = 1.0f;
    ScenarioResult result;
    {
        SW_TEST_DEFENSIVE_SCOPE( "a chunk moved outside the position range" );
        result = runShowcase( document, options );
    }
    logResult( "position range 1 m", result );
    SW_ASSERT_TRUE( result._bValid );
    SW_EXPECT_TRUE_MSG( result._outOfRangePoseCount > 0, "moving poses outside the range are counted, not clamped and sent" );
    SW_EXPECT_TRUE_MSG( result._convergedTick >= 0, "the structure still converges" );
    SW_EXPECT_TRUE_MSG( result._maxRestError < 1.0e-4f, "rested chunks are sent bit for bit and end where the server's are" );
}
