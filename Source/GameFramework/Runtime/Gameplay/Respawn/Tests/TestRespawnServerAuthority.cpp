#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"
#include "GameFramework/Runtime/Gameplay/Respawn/CheckpointState.h"
#include "GameFramework/Runtime/Gameplay/Respawn/RespawnFlow.h"
#include "GameFramework/Runtime/Gameplay/Respawn/RespawnRemoteTarget.h"
#include "GameFramework/Runtime/Gameplay/Respawn/Tests/RespawnTestWorld.h"

// RespawnServerAuthorityTest — 서버 판정: 클라이언트는 정책 종류 · 지점 해시만 보내고 자리 · 비용 · 횟수는 서버 상태 · 서버 세계로 정한다.
// 잘못된 요청(열거 밖 · 고르기 단계 밖 · 허용 안 된 정책 · 모르는 지점 · 남의 팀 지점 · 돈 부족 · 횟수 끝 · 안전 지점 없음)은 거절하고 흐름은 그대로다.

namespace
{
    constexpr float32 kAuthorityTolerance = 1.0e-4f;

    const sw::hashed_string kGold{ "gold" };

    /** @brief MMO 꼴 서버 하나 — 죽으면 고르기(시한 30 초): 제자리(비용 50 · 한 번, 요청 전용) · 지점 고르기, 시한이 지나면 마을. */
    struct RespawnAuthorityServer
    {
        sw::RespawnRules                  _rules{};
        sw::RespawnFlow                   _flow{};
        sw::RespawnTestWorld              _world{};
        sw::CheckpointState               _checkpoints{};
        sw::vector<sw::RespawnSpawnPoint> _listPoint{};
        sw::Wallet                        _wallet{};
        sw::RespawnRemoteTarget           _target{};
        int32                             _notifiedCount{ 0 };

        RespawnAuthorityServer()
        {
            sw::RespawnPolicyEntry choice;
            choice._kind          = sw::RespawnPolicyKind::PlayerChoice;
            choice._choiceTimeout = 30.0f;
            sw::RespawnPolicyEntry inPlace;
            inPlace._kind         = sw::RespawnPolicyKind::InPlace;
            inPlace._costCurrency = kGold;
            inPlace._costAmount   = 50;
            inPlace._maxUses      = 1;
            inPlace._safeRadius   = 4.0f;
            inPlace._bRequestOnly = true;
            sw::RespawnPolicyEntry town;
            town._kind = sw::RespawnPolicyKind::NearestSpawnPoint;
            _rules._listPolicy.push_back( choice );
            _rules._listPolicy.push_back( inPlace );
            _rules._listPolicy.push_back( town );

            _world._listGround.push_back( sw::RespawnTestGround{ 0.0f, 10.0f } );
            _world._groundY = 1.0f;
            _listPoint.push_back( sw::RespawnSpawnPoint{
                sw::hashed_string( "town" ), sw::float3{ 0.0f, 1.0f, 0.0f },
                0.0f, -1
            } );
            _listPoint.push_back( sw::RespawnSpawnPoint{
                sw::hashed_string( "redBase" ), sw::float3{ 50.0f, 1.0f, 0.0f },
                0.0f, 1
            } );
            _listPoint.push_back( sw::RespawnSpawnPoint{
                sw::hashed_string( "blueBase" ), sw::float3{ -50.0f, 1.0f, 0.0f },
                0.0f, 2
            } );
            _wallet.add( kGold, 80 );
        }

        [[nodiscard]] bool start()
        {
            if ( _flow.initialize( _rules ) == false )
                return false;
            _target._pFlow                 = &_flow;
            _target._context._pCheckpoints = &_checkpoints;
            _target._context._pSpawnPoints = &_listPoint;
            _target._context._pWorld       = &_world;
            _target._context._pWallet      = &_wallet;
            _target._context._team         = 1;
            _target._onResult              = [this]( const sw::RespawnRemoteTarget&, sw::RespawnRequestResult )
            { ++_notifiedCount; };
            return true;
        }

        /** @brief 서버가 아는 죽은 자리에서 죽고 고르기 단계까지 갑니다. */
        void dieAt( const sw::float3& position )
        {
            (void)_flow.notifyDied( position, 0.0f );
            (void)_flow.update( sw::GameSeconds{ 0.016f }, _target._context );
        }
    };
} // namespace

/**
 * @brief [RespawnServerAuthorityTest] 제자리 요청은 서버가 아는 죽은 자리와 서버 세계로 안전 지점을 정하고, 비용을 서버 지갑에서 뺀다. 같은 요청을 다시 하면 횟수 끝이다
 */
SW_TEST_CASE( RespawnServerAuthorityTest, InPlaceIsDecidedFromServerState )
{
    RespawnAuthorityServer server;
    SW_REQUIRE_TRUE( server.start() );
    server.dieAt( sw::float3{ 11.0f, -2.0f, 0.0f } ); // 땅 끝(x 10) 너머로 떨어졌다
    SW_REQUIRE_TRUE( server._flow.getPhase() == sw::RespawnPhase::Selection );

    // 원격 호출 층이 몸 앞에 부르는 검증 짝 — 정책 값은 열거 안이다.
    SW_REQUIRE_TRUE( server._target.validateRequestRespawn( sw::RespawnPolicyKind::InPlace, 0 ) );
    server._target.requestRespawn( sw::RespawnPolicyKind::InPlace, 0 );
    SW_EXPECT_TRUE( server._target._lastResult == sw::RespawnRequestResult::Accepted );
    SW_EXPECT_EQUAL( 1, server._notifiedCount );
    const sw::optional<sw::RespawnSpawn> spawned = server._flow.update( sw::GameSeconds{ 0.016f }, server._target._context );
    SW_REQUIRE_TRUE( spawned.has_value() );
    SW_EXPECT_TRUE( spawned->_policy == sw::RespawnPolicyKind::InPlace );
    SW_EXPECT_NEAR( 10.0f, spawned->_position._x, kAuthorityTolerance ); // 서버 세계의 땅 끝 — 클라이언트가 자리를 보낼 칸은 없다
    SW_EXPECT_NEAR( 1.0f, spawned->_position._y, kAuthorityTolerance );
    SW_EXPECT_EQUAL( int64( 50 ), spawned->_costCharged );
    SW_EXPECT_EQUAL( int64( 30 ), server._wallet.getBalance( kGold ) );

    server._wallet.add( kGold, 100 );
    server.dieAt( sw::float3{ 5.0f, 1.0f, 0.0f } );
    server._target.requestRespawn( sw::RespawnPolicyKind::InPlace, 0 );
    SW_EXPECT_TRUE( server._target._lastResult == sw::RespawnRequestResult::UsesExhausted );
    SW_EXPECT_TRUE( server._flow.getPhase() == sw::RespawnPhase::Selection );
    SW_EXPECT_EQUAL( int64( 130 ), server._wallet.getBalance( kGold ) );
}

/**
 * @brief [RespawnServerAuthorityTest] 잘못된 요청은 거절하고 흐름 · 지갑은 그대로다 — 열거 밖 값은 검증 짝이 거짓(조작), 규칙 거절은 까닭을 돌려준다
 */
SW_TEST_CASE( RespawnServerAuthorityTest, InvalidClientRequestsAreRejected )
{
    RespawnAuthorityServer server;
    SW_REQUIRE_TRUE( server.start() );

    // 열거 밖 — 정상 클라이언트가 보낼 수 없다(호출 층이 연결을 끊는다).
    SW_EXPECT_FALSE( server._target.validateRequestRespawn( sw::RespawnPolicyKind::Count, 0 ) );
    SW_EXPECT_FALSE( server._target.validateRequestRespawn( static_cast<sw::RespawnPolicyKind>( 200 ), 0 ) );

    // 살아 있을 때 · 대기 중에는 고르기 단계가 아니다.
    server._target.requestRespawn( sw::RespawnPolicyKind::PlayerChoice, sw::hashed_string( "town" ).getHash() );
    SW_EXPECT_TRUE( server._target._lastResult == sw::RespawnRequestResult::NotInSelection );
    SW_EXPECT_FALSE( server._flow.isDead() );

    server._wallet.setBalance( kGold, 10 );
    server.dieAt( sw::float3{ 5.0f, 1.0f, 0.0f } );
    const auto expectRejected = [&server]( sw::RespawnPolicyKind kind, uint32 pointHash, sw::RespawnRequestResult expected )
    {
        server._target.requestRespawn( kind, pointHash );
        SW_EXPECT_TRUE( server._target._lastResult == expected );
        SW_EXPECT_TRUE( server._flow.getPhase() == sw::RespawnPhase::Selection );
        SW_EXPECT_EQUAL( int64( 10 ), server._wallet.getBalance( kGold ) );
    };
    expectRejected( sw::RespawnPolicyKind::TeamBase, 0, sw::RespawnRequestResult::PolicyNotAllowed );
    expectRejected( sw::RespawnPolicyKind::PlayerChoice, sw::hashed_string( "nowhere" ).getHash(), sw::RespawnRequestResult::UnknownPoint );
    expectRejected( sw::RespawnPolicyKind::PlayerChoice, sw::hashed_string( "blueBase" ).getHash(), sw::RespawnRequestResult::UnknownPoint ); // 남의 팀 거점
    expectRejected( sw::RespawnPolicyKind::InPlace, 0, sw::RespawnRequestResult::CannotAfford );
    // 규칙에 없는 정책은 허용하지 않는다.
    expectRejected( sw::RespawnPolicyKind::LastCheckpoint, 0, sw::RespawnRequestResult::PolicyNotAllowed );

    // 안전 지점이 반경(4 m) 밖이면 돈이 있어도 제자리는 없다.
    RespawnAuthorityServer deep;
    SW_REQUIRE_TRUE( deep.start() );
    deep.dieAt( sw::float3{ 30.0f, -40.0f, 0.0f } );
    deep._target.requestRespawn( sw::RespawnPolicyKind::InPlace, 0 );
    SW_EXPECT_TRUE( deep._target._lastResult == sw::RespawnRequestResult::NoLocation );
    SW_EXPECT_EQUAL( int64( 80 ), deep._wallet.getBalance( kGold ) );

    // 흐름이 없는 대상(클라이언트 쪽)은 판정하지 않는다.
    sw::RespawnRemoteTarget clientSide;
    clientSide.requestRespawn( sw::RespawnPolicyKind::InPlace, 0 );
    SW_EXPECT_TRUE( clientSide._lastResult == sw::RespawnRequestResult::NotInSelection );
}

/**
 * @brief [RespawnServerAuthorityTest] 지점 고르기는 켠 체크포인트와 내 팀 · 모든 팀 지점만 받고, 받은 뒤 다음 걸음에 그 지점에 세운다. 클라이언트는 판정을 받아 남긴다
 */
SW_TEST_CASE( RespawnServerAuthorityTest, PointChoiceAcceptsOnlyServerKnownPoints )
{
    RespawnAuthorityServer server;
    sw::CheckpointDef      shrine;
    shrine._id        = sw::hashed_string( "shrine" );
    shrine._bReusable = true;
    (void)server._checkpoints.activate( shrine, sw::float3{ 7.0f, 1.0f, 0.0f }, 0.0f );
    SW_REQUIRE_TRUE( server.start() );
    server.dieAt( sw::float3{ 5.0f, 1.0f, 0.0f } );

    sw::vector<sw::hashed_string> listChoice;
    server._flow.collectChoices( server._target._context, listChoice );
    SW_REQUIRE_EQUAL( size_t( 3 ), listChoice.size() ); // shrine · town · redBase(내 팀) — blueBase 는 남의 팀
    SW_EXPECT_EQUAL( sw::hashed_string( "shrine" ), listChoice[0] );

    server._target.requestRespawn( sw::RespawnPolicyKind::PlayerChoice, sw::hashed_string( "redBase" ).getHash() );
    SW_EXPECT_TRUE( server._target._lastResult == sw::RespawnRequestResult::Accepted );
    // 받은 뒤에는 고르기 단계가 아니다 — 두 번째 요청이 고른 자리를 바꾸지 못한다.
    server._target.requestRespawn( sw::RespawnPolicyKind::PlayerChoice, sw::hashed_string( "shrine" ).getHash() );
    SW_EXPECT_TRUE( server._target._lastResult == sw::RespawnRequestResult::NotInSelection );
    const sw::optional<sw::RespawnSpawn> spawned = server._flow.update( sw::GameSeconds{ 0.016f }, server._target._context );
    SW_REQUIRE_TRUE( spawned.has_value() );
    SW_EXPECT_EQUAL( sw::hashed_string( "redBase" ), spawned->_pointID );
    SW_EXPECT_NEAR( 50.0f, spawned->_position._x, kAuthorityTolerance );
    SW_EXPECT_TRUE( spawned->_policy == sw::RespawnPolicyKind::PlayerChoice );
    SW_EXPECT_EQUAL( 2, server._notifiedCount );

    sw::RespawnRemoteTarget client;
    client.receiveRespawnResult( sw::RespawnRequestResult::UnknownPoint );
    SW_EXPECT_TRUE( client._lastResult == sw::RespawnRequestResult::UnknownPoint );
    SW_EXPECT_EQUAL( 1, client._resultCount );
}
