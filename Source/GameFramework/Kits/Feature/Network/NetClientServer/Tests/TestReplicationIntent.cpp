#include "pch.h"

#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Character/Hit/MeleeSweepHistory.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationServer.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"

// 폰 게임의 입력 한 길 — 클라이언트의 의도와 맞음 주장이 서버의 꺼내기까지 같은 값으로 가고, 보던 틱이 서버 시계로 잘리는가.

using namespace sw;

namespace
{
    /** @brief 서버 하나와 클라이언트 하나를 루프백으로 잇습니다. */
    struct ReplicationIntentTestRig
    {
        test::LoopbackCluster _cluster{ 7u };
        ReplicationServer     _server;
        ReplicationClient     _client;
        int32                 _connectionID{ -1 };

        ReplicationIntentTestRig()
        {
            const NetEndpointConfig settings;
            (void)_cluster.addEndpoint( 4100, settings ); // 빈 클러스터라 포트가 겹치지 않는다
            (void)_cluster.addEndpoint( 4200, settings );
            SW_EXPECT_TRUE( _cluster.connectClients( 30 ) );
            SW_EXPECT_TRUE( _server.initialize( &_cluster.getServer(), ReplicationServerConfig{} ) );
            SW_EXPECT_TRUE( _client.initialize( &_cluster.getClient( 0 ), ReplicationClientConfig{} ) );
            vector<int32> listConnection;
            _cluster.getServer().collectConnected( listConnection );
            SW_EXPECT_EQUAL( size_t{ 1 }, listConnection.size() );
            if ( listConnection.empty() == false )
                _connectionID = listConnection.front();
        }

        /** @brief 망을 한 걸음 돌리고 받은 메시지를 양쪽에 넘깁니다. */
        void pump()
        {
            _cluster.step();
            vector<uint8>  buffer;
            int32          connectionID = -1;
            NetChannelType channel      = NetChannelType::Unreliable;
            while ( _cluster.getServer().receiveMessage( connectionID, channel, buffer ) )
            {
                (void)_server.handleMessage( connectionID, buffer );
            }
            while ( _cluster.getClient( 0 ).receiveMessage( connectionID, channel, buffer ) )
            {
                (void)_client.handleMessage( 0, buffer );
            }
        }

        /** @brief 보낸 것이 상대에게 닿을 만큼 망을 돌립니다. */
        void pollUntilDelivered()
        {
            for ( int32 step = 0; step < 8; ++step )
            {
                pump();
            }
        }

        /** @brief 서버 틱 @p tick 의 스냅샷을 보내 클라이언트가 받게 합니다(소유자 상태 없이 시계만 맞춘다). */
        void sendSnapshot( uint32 tick )
        {
            _server.beginTick( tick );
            _server.endTick();
            _server.sendSnapshots();
            pollUntilDelivered();
        }

        static ControlIntent makeIntent()
        {
            ControlIntent intent;
            intent._move         = float2{ 0.0f, 1.0f };
            intent._controlYaw   = 0.75f;
            intent._controlPitch = -0.25f;
            intent.setButton( 2, true, true );
            intent.setTriggerLook( 0.5f, 0.125f );
            return intent;
        }
    };
} // namespace

/** @brief [ReplicationIntentTest] 의도와 주장이 선을 지나 quantize 한 값 그대로 온다. */
SW_TEST_CASE( ReplicationIntentTest, IntentAndClaimsRoundTrip )
{
    ReplicationIntentTestRig rig;
    const ControlIntent      sent = ReplicationIntentTestRig::makeIntent();
    vector<MeleeHitClaim>    listSentClaim;
    listSentClaim.push_back( MeleeHitClaim{ 1, 7 } );
    listSentClaim.push_back( MeleeHitClaim{ 1, 9 } );
    SW_REQUIRE_TRUE( rig._client.sendIntent( 1, sent, listSentClaim ) );
    rig.pollUntilDelivered();

    rig._server.beginTick( 10 );
    ControlIntent         received;
    bool                  bExact = false;
    vector<MeleeHitClaim> listClaim;
    SW_REQUIRE_TRUE( rig._server.popIntent( rig._connectionID, 1, received, bExact, listClaim ) );
    ControlIntent expected = sent;
    expected.quantize();
    SW_EXPECT_TRUE( bExact );
    SW_EXPECT_TRUE( received == expected );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listClaim.size() );
    SW_EXPECT_EQUAL( 1u, listClaim[0]._frame );
    SW_EXPECT_EQUAL( uint64{ 7 }, listClaim[0]._targetID );
    SW_EXPECT_EQUAL( uint64{ 9 }, listClaim[1]._targetID );
}

/** @brief [ReplicationIntentTest] 첫 입력이 오기 전에 꺼내 봐도 창이 밀리지 않는다 — 서버는 입력이 올 때까지 같은 틱을 다시 묻는다. */
SW_TEST_CASE( ReplicationIntentTest, PopBeforeFirstInputKeepsTheWindow )
{
    ReplicationIntentTestRig rig;
    rig._server.beginTick( 10 );
    ControlIntent         received;
    bool                  bExact = false;
    vector<MeleeHitClaim> listClaim;
    SW_EXPECT_FALSE( rig._server.popIntent( rig._connectionID, 1, received, bExact, listClaim ) );

    SW_REQUIRE_TRUE( rig._client.sendIntent( 1, ReplicationIntentTestRig::makeIntent(), {} ) );
    rig.pollUntilDelivered();
    SW_REQUIRE_TRUE( rig._server.popIntent( rig._connectionID, 1, received, bExact, listClaim ) );
    SW_EXPECT_TRUE( bExact );
    SW_EXPECT_TRUE( received.wasTriggered( 2 ) );
}

/** @brief [ReplicationIntentTest] 첫 입력의 틱은 1 이 아닐 수 있다(접속 전에도 틱을 센다) — 받은 가장 오래된 틱을 알려 준다. */
SW_TEST_CASE( ReplicationIntentTest, OldestInputTickFollowsTheFirstReceived )
{
    ReplicationIntentTestRig rig;
    SW_EXPECT_FALSE( rig._server.findOldestInputTick( rig._connectionID ).has_value() );
    SW_REQUIRE_TRUE( rig._client.sendIntent( 40, ReplicationIntentTestRig::makeIntent(), {} ) );
    rig.pollUntilDelivered();
    const sw::optional<uint32> oldestTick = rig._server.findOldestInputTick( rig._connectionID );
    SW_REQUIRE_TRUE( oldestTick.has_value() );
    SW_EXPECT_EQUAL( 40u, *oldestTick );
}

/** @brief [ReplicationIntentTest] 되풀이한 입력은 눌림과 시선만 잇고 발동과 주장을 지운다 — 한 번 맞음이 두 번 되지 않게. */
SW_TEST_CASE( ReplicationIntentTest, RepeatedInputClearsTriggersAndClaims )
{
    ReplicationIntentTestRig rig;
    vector<MeleeHitClaim>    listSentClaim;
    listSentClaim.push_back( MeleeHitClaim{ 1, 7 } );
    SW_REQUIRE_TRUE( rig._client.sendIntent( 1, ReplicationIntentTestRig::makeIntent(), listSentClaim ) );
    rig.pollUntilDelivered();
    rig._server.beginTick( 10 );
    ControlIntent         received;
    bool                  bExact = false;
    vector<MeleeHitClaim> listClaim;
    SW_REQUIRE_TRUE( rig._server.popIntent( rig._connectionID, 1, received, bExact, listClaim ) );

    SW_REQUIRE_TRUE( rig._server.popIntent( rig._connectionID, 2, received, bExact, listClaim ) );
    SW_EXPECT_FALSE( bExact );
    SW_EXPECT_TRUE( received.isDown( 2 ) );
    SW_EXPECT_FALSE( received.wasTriggered( 2 ) );
    SW_EXPECT_FALSE( received._bTriggerLook );
    SW_EXPECT_NEAR( 0.75f, received._controlYaw, 1e-3f );
    SW_EXPECT_TRUE( listClaim.empty() );
}

/** @brief [ReplicationIntentTest] 입력 하나의 주장 상한을 넘으면 보내지 않는다. */
SW_TEST_CASE( ReplicationIntentTest, TooManyClaimsAreRefused )
{
    ReplicationIntentTestRig    rig;
    const vector<MeleeHitClaim> listClaim( static_cast<size_t>( NetClientServerMessage::kMaxClaimsPerIntent + 1 ), MeleeHitClaim{ 1, 7 } );
    SW_EXPECT_LOG( "ReplicationClient.ReplicationClientTooManyClaims" );
    SW_EXPECT_FALSE( rig._client.sendIntent( 1, ReplicationIntentTestRig::makeIntent(), listClaim ) );
}

/**
 * @brief [ReplicationIntentTest] 보던 틱은 서버 시계로 자른다. 입력 틱으로 자르던 때는 입력 틱이 1 인 늦게 붙은 클라이언트의 보던 틱이 1 로 잘려 랙 보정이 맨 끝 틱으로 되감았다.
 */
SW_TEST_CASE( ReplicationIntentTest, ViewTickIsClampedOnTheServerClock )
{
    ReplicationIntentTestRig rig;
    rig.sendSnapshot( 500 );
    rig._client.update( 0.0f );
    SW_REQUIRE_TRUE( rig._client.hasSnapshot() );
    const float32 renderTick = rig._client.getRenderTick();
    SW_REQUIRE_TRUE( renderTick > 400.0f ); // 보간 지연만큼 500 의 과거다

    const vector<MeleeHitClaim> listNoClaim;
    SW_REQUIRE_TRUE( rig._client.sendIntent( 1, ReplicationIntentTestRig::makeIntent(), listNoClaim ) );
    SW_REQUIRE_TRUE( rig._client.sendIntent( 2, ReplicationIntentTestRig::makeIntent(), listNoClaim ) );
    rig.pollUntilDelivered();

    rig._server.beginTick( 501 );
    ControlIntent         received;
    bool                  bExact = false;
    vector<MeleeHitClaim> listClaim;
    SW_REQUIRE_TRUE( rig._server.popIntent( rig._connectionID, 1, received, bExact, listClaim ) );
    SW_EXPECT_NEAR( renderTick, rig._server.getClientViewTick( rig._connectionID ), 0.01f );

    // 한도보다 오래된 시점은 한도까지만 되감는다(기본 32 틱).
    rig._server.beginTick( 1000 );
    SW_REQUIRE_TRUE( rig._server.popIntent( rig._connectionID, 2, received, bExact, listClaim ) );
    SW_EXPECT_NEAR( 968.0f, rig._server.getClientViewTick( rig._connectionID ), 0.01f );
}
