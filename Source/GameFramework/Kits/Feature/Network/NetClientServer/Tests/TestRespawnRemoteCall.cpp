#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Reflection/TypeRegistry.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/RemoteCall.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"
#include "GameFramework/Runtime/Gameplay/Respawn/RespawnFlow.h"
#include "GameFramework/Runtime/Gameplay/Respawn/RespawnRemoteTarget.h"

// 부활 고르기의 멀티플레이 한 판 — 클라이언트가 선을 넘겨 부른 `RespawnRemoteTarget::requestRespawn` 을 서버의 `RespawnFlow` 가 서버 상태 · 서버 세계로 판정하고,
// 판정을 `receiveRespawnResult` 로 돌려보낸다. 꼴이 틀린 호출 · 함수 상한(초당 4)을 넘은 호출은 몸에 닿지 않는다.

using namespace sw;

namespace
{
    constexpr uint32 kPlayerID = 21;

    const hashed_string kGold{ "gold" };

    /** @brief 테스트가 넘기지 않는 한 연결 상한에 닿지 않는 호출률입니다(함수 상한만 본다). */
    constexpr RemoteCallServerConfig kRoomyCallLimit{ 1000, 1000 };

    /** @brief 땅이 x 0..10 · 높이 1 인 서버 세계입니다. */
    class RespawnRemoteCallWorld final : public IRespawnWorld
    {
    public:
        optional<float3> findSafeLocation( const float3& position, float32 radius ) const override
        {
            const float3 candidate{ MathUtil::clamp( position._x, 0.0f, 10.0f ), 1.0f, position._z };
            if ( float3::getDistance( candidate, position ) > radius )
                return nullopt;
            return candidate;
        }
        bool isLocationReady( const float3& position ) const override
        {
            (void)position;
            return true;
        }
    };

    /** @brief 서버 하나 · 클라이언트 하나를 루프백으로 잇고, 서버에 부활 흐름 하나(MMO 꼴 — 고르기, 요청 전용 제자리 비용 50, 마을)를 둡니다. */
    struct RespawnRemoteCallRig
    {
        test::LoopbackCluster     _cluster{ 21u };
        RemoteCallServer          _server;
        RemoteCallClient          _client;
        RespawnFlow               _flow;
        RespawnRemoteCallWorld    _world;
        Wallet                    _wallet;
        vector<RespawnSpawnPoint> _listPoint;
        RespawnRemoteTarget       _serverTarget;
        RespawnRemoteTarget       _clientTarget;
        int32                     _connectionID{ -1 };

        RespawnRemoteCallRig()
        {
            const NetEndpointConfig settings;
            (void)_cluster.addEndpoint( 4500, settings );
            (void)_cluster.addEndpoint( 4600, settings );
            SW_EXPECT_TRUE( _cluster.connectClients( 30 ) );
            SW_EXPECT_TRUE( _server.initialize( &_cluster.getServer(), nullptr, kRoomyCallLimit ) );
            _server.setClock( []() -> int64
            { return 0; } ); // 버킷이 다시 차지 않는다 — 몰아 보낸 호출의 판정이 실행 속도에 달리지 않게
            _client.initialize( &_cluster.getClient( 0 ), nullptr );
            vector<int32> listConnection;
            _cluster.getServer().collectConnected( listConnection );
            SW_EXPECT_EQUAL( size_t{ 1 }, listConnection.size() );
            if ( listConnection.empty() == false )
                _connectionID = listConnection.front();

            RespawnRules       rules;
            RespawnPolicyEntry choice;
            choice._kind = RespawnPolicyKind::PlayerChoice;
            RespawnPolicyEntry inPlace;
            inPlace._kind         = RespawnPolicyKind::InPlace;
            inPlace._costCurrency = kGold;
            inPlace._costAmount   = 50;
            inPlace._safeRadius   = 4.0f;
            inPlace._bRequestOnly = true;
            RespawnPolicyEntry town;
            town._kind = RespawnPolicyKind::NearestSpawnPoint;
            rules._listPolicy.push_back( choice );
            rules._listPolicy.push_back( inPlace );
            rules._listPolicy.push_back( town );
            SW_EXPECT_TRUE( _flow.initialize( rules ) );
            RespawnSpawnPoint townPoint;
            townPoint._id       = hashed_string( "town" );
            townPoint._position = float3{ 0.0f, 1.0f, 0.0f };
            _listPoint.push_back( townPoint );
            _wallet.add( kGold, 80 );

            _serverTarget._pFlow                 = &_flow;
            _serverTarget._context._pSpawnPoints = &_listPoint;
            _serverTarget._context._pWorld       = &_world;
            _serverTarget._context._pWallet      = &_wallet;
            // 판정은 그 연결의 클라이언트 대상으로 돌려보낸다(게임의 서버 링크 자리).
            _serverTarget._onResult = [this]( const RespawnRemoteTarget&, RespawnRequestResult result )
            { (void)_server.callClient( kPlayerID, "receiveRespawnResult", { ReflectedValue::make( result ) } ); };
        }

        void pollUntilDelivered()
        {
            for ( int32 step = 0; step < 8; ++step )
            {
                _cluster.step();
                vector<uint8>  buffer;
                int32          connectionID = -1;
                NetChannelKind channel      = NetChannelKind::Unreliable;
                while ( _cluster.getServer().receiveMessage( connectionID, channel, buffer ) )
                {
                    (void)_server.handleMessage( connectionID, buffer.data(), static_cast<int32>( buffer.size() ), channel );
                }
                while ( _cluster.getClient( 0 ).receiveMessage( connectionID, channel, buffer ) )
                {
                    (void)_client.handleMessage( 0, buffer.data(), static_cast<int32>( buffer.size() ), channel );
                }
            }
        }

        uint32 requestRespawn( RespawnPolicyKind kind, uint32 pointHash )
        {
            return _client.callServer( kPlayerID, *RespawnRemoteTarget::StaticType(), "requestRespawn", { ReflectedValue::make( kind ), ReflectedValue::make( pointHash ) } );
        }
    };
} // namespace

/**
 * @brief [RespawnRemoteCallTest] 클라이언트의 제자리 요청이 선을 지나 서버 흐름에 닿고, 서버가 자기 세계 · 지갑으로 판정해 스폰하며, 판정이 클라이언트로 돌아온다
 */
SW_TEST_CASE( RespawnRemoteCallTest, ServerDecidesAndRepliesOverTheWire )
{
    RespawnRemoteCallRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kPlayerID, *RespawnRemoteTarget::StaticType(), &rig._serverTarget, rig._connectionID ) );
    SW_REQUIRE_TRUE( rig._client.registerTarget( kPlayerID, *RespawnRemoteTarget::StaticType(), &rig._clientTarget ) );

    // 서버가 아는 죽은 자리 — 땅 끝(x 10) 너머 구덩이
    SW_REQUIRE_TRUE( rig._flow.notifyDied( float3{ 11.0f, -2.0f, 0.0f }, 0.0f ) );
    SW_EXPECT_FALSE( rig._flow.update( 0.016f, rig._serverTarget._context ).has_value() );
    SW_REQUIRE_TRUE( rig._flow.getPhase() == RespawnPhase::Selection );

    SW_EXPECT_NOT_EQUAL( 0u, rig.requestRespawn( RespawnPolicyKind::InPlace, 0u ) );
    rig.pollUntilDelivered();
    SW_EXPECT_TRUE( rig._serverTarget._lastResult == RespawnRequestResult::Accepted );
    SW_EXPECT_TRUE( rig._clientTarget._lastResult == RespawnRequestResult::Accepted );
    SW_EXPECT_EQUAL( 1, rig._clientTarget._resultCount );
    SW_EXPECT_EQUAL( uint64{ 1 }, rig._server.getStats()._invokedCount );

    const optional<RespawnSpawn> spawned = rig._flow.update( 0.016f, rig._serverTarget._context );
    SW_REQUIRE_TRUE( spawned.has_value() );
    SW_EXPECT_NEAR( 10.0, spawned->_position._x, 1e-4 );
    SW_EXPECT_NEAR( 1.0, spawned->_position._y, 1e-4 );
    SW_EXPECT_EQUAL( int64{ 30 }, rig._wallet.getBalance( kGold ) );

    // 살아 있는데 또 고르면 서버가 거절하고 그 까닭이 돌아온다(돈은 그대로).
    SW_EXPECT_NOT_EQUAL( 0u, rig.requestRespawn( RespawnPolicyKind::PlayerChoice, hashed_string( "town" ).getHash() ) );
    rig.pollUntilDelivered();
    SW_EXPECT_TRUE( rig._clientTarget._lastResult == RespawnRequestResult::NotInSelection );
    SW_EXPECT_EQUAL( 2, rig._clientTarget._resultCount );
    SW_EXPECT_EQUAL( int64{ 30 }, rig._wallet.getBalance( kGold ) );
    SW_EXPECT_FALSE( rig._flow.isDead() );
}

/**
 * @brief [RespawnRemoteCallTest] 정책으로 읽히지 않는 인자는 몸에 닿지 않고, 함수 상한(초당 4)을 넘은 호출은 판정하지 않고 거절한다
 */
SW_TEST_CASE( RespawnRemoteCallTest, MalformedAndFloodedCallsNeverReachTheFlow )
{
    RespawnRemoteCallRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kPlayerID, *RespawnRemoteTarget::StaticType(), &rig._serverTarget, rig._connectionID ) );
    SW_REQUIRE_TRUE( rig._client.registerTarget( kPlayerID, *RespawnRemoteTarget::StaticType(), &rig._clientTarget ) );
    SW_REQUIRE_TRUE( rig._flow.notifyDied( float3{ 5.0f, 1.0f, 0.0f }, 0.0f ) );
    (void)rig._flow.update( 0.016f, rig._serverTarget._context );
    SW_REQUIRE_TRUE( rig._flow.getPhase() == RespawnPhase::Selection );

    // 손으로 짠 호출 — 정책 자리에 없는 열거자(조작한 클라이언트의 흉내)
    NetMessageWriter messageWriter;
    BitWriter&       writer = messageWriter.begin( NetRemoteCallMessage::kCall );
    writer.writeVarUint( 1 );
    writer.writeVarUint( kPlayerID );
    writer.writeBits( 0, 8 ); // 월드 세대
    writer.writeUint32( hashed_string::computeHash( "requestRespawn" ) );
    writer.writeVarUint( 2 );
    writer.writeVarInt( 99 ); // 등록되지 않은 `RespawnPolicyKind`
    writer.writeVarInt( 0 );
    const vector<uint8>& bytes = messageWriter.getBytes();
    (void)rig._server.handleMessage( rig._connectionID, bytes.data(), messageWriter.getByteCount(), NetChannelKind::ReliableOrdered );
    SW_EXPECT_EQUAL( uint64{ 0 }, rig._server.getStats()._invokedCount );
    SW_EXPECT_EQUAL( 0, rig._serverTarget._resultCount );
    SW_EXPECT_TRUE( rig._flow.getPhase() == RespawnPhase::Selection );

    // 돈이 모자란 제자리를 여섯 번 몰아 보낸다 — 함수 버킷(한꺼번에 4)에서 위의 조작 호출이 하나를 썼으므로 셋만 몸에 닿고(판정 CannotAfford) 셋은 상한에 걸린다.
    rig._wallet.setBalance( kGold, 10 );
    for ( int32 index = 0; index < 6; ++index )
    {
        (void)rig.requestRespawn( RespawnPolicyKind::InPlace, 0u );
    }
    rig.pollUntilDelivered();
    SW_EXPECT_EQUAL( 3, rig._serverTarget._resultCount );
    SW_EXPECT_EQUAL( uint64{ 3 }, rig._server.getStats()._rateLimitedCount );
    SW_EXPECT_TRUE( rig._serverTarget._lastResult == RespawnRequestResult::CannotAfford );
    SW_EXPECT_TRUE( rig._flow.getPhase() == RespawnPhase::Selection );
    SW_EXPECT_EQUAL( int64{ 10 }, rig._wallet.getBalance( kGold ) );
}
