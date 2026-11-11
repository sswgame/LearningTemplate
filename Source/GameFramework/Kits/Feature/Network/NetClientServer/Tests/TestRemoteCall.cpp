#include "pch.h"

#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/RemoteCall.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/Tests/RemoteCallTestTypes.h"

// 반사 함수 호출 — `FUNCTION( Server · Client · Reliable · Validate )` 메타데이터가 주인 · 역할 · 채널 · 검증 정책으로 실제 선을 지나 지켜지는가.

using namespace sw;

namespace
{
    constexpr uint32 kAvatarID = 11;

    /** @brief 서버 하나와 클라이언트 하나를 루프백으로 잇고, 받은 메시지를 받은 채널 그대로 넘깁니다. */
    struct RemoteCallTestRig
    {
        test::LoopbackCluster _cluster{ 11u };
        RemoteCallServer      _server;
        RemoteCallClient      _client;
        RemoteCallTestAvatar  _serverAvatar;
        RemoteCallTestAvatar  _clientAvatar;
        int32                 _connectionID{ -1 };

        RemoteCallTestRig()
        {
            const NetEndpointConfig settings;
            (void)_cluster.addEndpoint( 4300, settings );
            (void)_cluster.addEndpoint( 4400, settings );
            SW_EXPECT_TRUE( _cluster.connectClients( 30 ) );
            _server.initialize( &_cluster.getServer() );
            _client.initialize( &_cluster.getClient( 0 ) );
            vector<int32> listConnection;
            _cluster.getServer().collectConnected( listConnection );
            SW_EXPECT_EQUAL( size_t{ 1 }, listConnection.size() );
            if ( listConnection.empty() == false )
                _connectionID = listConnection.front();
        }

        void pollUntilDelivered()
        {
            for ( int32 step = 0; step < 8; ++step )
            {
                _cluster.step();
                vector<uint8>  buffer;
                int32          connectionID = -1;
                NetChannelType channel      = NetChannelType::Unreliable;
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

        /** @brief 클라이언트를 거치지 않고 손으로 짠 호출을 서버에 넘깁니다(조작한 클라이언트의 흉내). */
        NetHandleResult forgeCall( uint32 targetID, string_view functionName, const vector<string>& listArgText, NetChannelType channel )
        {
            NetMessageWriter messageWriter;
            BitWriter&       writer = messageWriter.begin( NetRemoteCallMessage::kCall );
            writer.writeVarUint( 1 );
            writer.writeVarUint( targetID );
            writer.writeBlob( reinterpret_cast<const uint8*>( functionName.data() ), static_cast<int32>( functionName.size() ) );
            writer.writeVarUint( listArgText.size() );
            for ( const string& text : listArgText )
            {
                writer.writeBlob( reinterpret_cast<const uint8*>( text.data() ), static_cast<int32>( text.size() ) );
            }
            const vector<uint8>& bytes = messageWriter.getBytes();
            return _server.handleMessage( _connectionID, bytes.data(), messageWriter.getByteCount(), channel );
        }
    };
} // namespace

/** @brief [RemoteCallTest] 주인 클라이언트가 부른 신뢰 · 검증 함수는 검증 짝을 지나 서버의 몸까지 간다. */
SW_TEST_CASE( RemoteCallTest, OwnerCallRunsTheServerFunction )
{
    RemoteCallTestRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID ) );

    const uint32 sequence = rig._client.callServer( kAvatarID, *RemoteCallTestAvatar::StaticType(), "attackTarget", { ReflectedValue::make( int32{ 7 } ), ReflectedValue::make( int32{ 20 } ) } );
    SW_EXPECT_NOT_EQUAL( 0u, sequence );
    rig.pollUntilDelivered();

    SW_EXPECT_EQUAL( 7, rig._serverAvatar._lastTargetID );
    SW_EXPECT_EQUAL( 20, rig._serverAvatar._totalPower );
    SW_EXPECT_EQUAL( uint64{ 1 }, rig._server.getStats()._invokedCount );
    SW_EXPECT_EQUAL( uint64{ 0 }, rig._server.getStats()._violationCount );
}

/** @brief [RemoteCallTest] `Reliable` 이 없는 함수는 비신뢰로 가서 그대로 불린다. */
SW_TEST_CASE( RemoteCallTest, UnreliableFunctionTravelsUnreliable )
{
    RemoteCallTestRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID ) );
    SW_EXPECT_NOT_EQUAL( 0u, rig._client.callServer( kAvatarID, *RemoteCallTestAvatar::StaticType(), "moveStep", { ReflectedValue::make( 2.5f ) } ) );
    rig.pollUntilDelivered();
    SW_EXPECT_NEAR( 2.5, rig._serverAvatar._movedDistance, 1e-4 );
}

/** @brief [RemoteCallTest] 검증 짝이 거짓이면 몸을 부르지 않고 그 연결을 끊는다(언리얼 WithValidation). */
SW_TEST_CASE( RemoteCallTest, FailedValidationDisconnects )
{
    RemoteCallTestRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID ) );
    SW_EXPECT_LOG( "RemoteCall.ValidationFailed" );
    // 정직한 클라이언트도 이 호출을 보낼 수 있다(검증은 서버 몫) — 망을 더 돌리지 않고 서버 쪽만 본다.
    SW_EXPECT_TRUE( rig.forgeCall( kAvatarID, "attackTarget", { "7", "99" }, NetChannelType::ReliableOrdered ) == NetHandleResult::Handled );
    SW_EXPECT_EQUAL( 0, rig._serverAvatar._totalPower );
    SW_EXPECT_EQUAL( uint64{ 1 }, rig._server.getStats()._validationFailedCount );
    SW_EXPECT_EQUAL( uint64{ 0 }, rig._server.getStats()._invokedCount );
    SW_EXPECT_TRUE( rig._cluster.getServer().getConnectionState( rig._connectionID ) != NetConnectionState::Connected );
}

/** @brief [RemoteCallTest] 주인이 아닌 연결의 호출은 남의 대상을 움직이려는 것이라 끊는다. */
SW_TEST_CASE( RemoteCallTest, NonOwnerIsDisconnected )
{
    RemoteCallTestRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID + 1000 ) );
    SW_EXPECT_LOG( "RemoteCall.Violation" );
    SW_EXPECT_TRUE( rig.forgeCall( kAvatarID, "attackTarget", { "7", "20" }, NetChannelType::ReliableOrdered ) == NetHandleResult::Handled );
    SW_EXPECT_EQUAL( 0, rig._serverAvatar._totalPower );
    SW_EXPECT_EQUAL( uint64{ 1 }, rig._server.getStats()._violationCount );
}

/** @brief [RemoteCallTest] 원격 역할이 없는 함수 · `Client` 함수는 클라이언트가 보내지 않고, 조작한 호출은 서버가 끊는다. */
SW_TEST_CASE( RemoteCallTest, OnlyServerFunctionsAreCallable )
{
    RemoteCallTestRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID ) );
    SW_EXPECT_LOG( "RemoteCall.ServerCallRefused" );
    SW_EXPECT_EQUAL( 0u, rig._client.callServer( kAvatarID, *RemoteCallTestAvatar::StaticType(), "applyLocal", { ReflectedValue::make( int32{ 5 } ) } ) );
    SW_EXPECT_LOG( "RemoteCall.Violation" );
    SW_EXPECT_TRUE( rig.forgeCall( kAvatarID, "applyLocal", { "5" }, NetChannelType::ReliableOrdered ) == NetHandleResult::Handled );
    SW_EXPECT_EQUAL( 0, rig._serverAvatar._localApplyCount );
    SW_EXPECT_EQUAL( uint64{ 1 }, rig._server.getStats()._violationCount );
}

/** @brief [RemoteCallTest] `Reliable` 함수가 비신뢰 채널로 오면 보내는 쪽 규칙을 어긴 것이라 끊는다. */
SW_TEST_CASE( RemoteCallTest, ReliableFunctionOnUnreliableChannelIsRefused )
{
    RemoteCallTestRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID ) );
    SW_EXPECT_LOG( "RemoteCall.Violation" );
    SW_EXPECT_TRUE( rig.forgeCall( kAvatarID, "attackTarget", { "7", "20" }, NetChannelType::Unreliable ) == NetHandleResult::Handled );
    SW_EXPECT_EQUAL( 0, rig._serverAvatar._totalPower );
}

/** @brief [RemoteCallTest] 없는 대상 · 함수 · 읽지 못하는 인자는 끊지 않고 같은 순번으로 거절을 알린다. */
SW_TEST_CASE( RemoteCallTest, UnknownTargetAndBadArgumentsAreRejected )
{
    RemoteCallTestRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID ) );
    const uint32 sequence = rig._client.callServer( 999, *RemoteCallTestAvatar::StaticType(), "moveStep", { ReflectedValue::make( 1.0f ) } );
    SW_REQUIRE_TRUE( sequence != 0u );
    rig.pollUntilDelivered();
    SW_EXPECT_TRUE( rig.forgeCall( kAvatarID, "attackTarget", { "seven", "20" }, NetChannelType::ReliableOrdered ) == NetHandleResult::Handled );
    SW_EXPECT_TRUE( rig.forgeCall( kAvatarID, "noSuchFunction", {}, NetChannelType::ReliableOrdered ) == NetHandleResult::Handled );
    rig.pollUntilDelivered();

    vector<RemoteCallRejectNotice> listNotice;
    rig._client.pollRejections( listNotice );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listNotice.size() );
    SW_EXPECT_EQUAL( sequence, listNotice[0]._sequence );
    SW_EXPECT_TRUE( listNotice[0]._error == RemoteCallError::UnknownTarget );
    SW_EXPECT_TRUE( listNotice[1]._error == RemoteCallError::ArgumentMismatch );
    SW_EXPECT_TRUE( listNotice[2]._error == RemoteCallError::UnknownFunction );
    SW_EXPECT_EQUAL( uint64{ 0 }, rig._server.getStats()._violationCount );
    SW_EXPECT_EQUAL( uint64{ 3 }, rig._server.getStats()._rejectedCount );
}

/** @brief [RemoteCallTest] 상한을 넘는 인자 수는 손상된 메시지다. */
SW_TEST_CASE( RemoteCallTest, TooManyArgumentsAreMalformed )
{
    RemoteCallTestRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID ) );
    vector<string> listText( static_cast<size_t>( NetRemoteCallMessage::kMaxArgumentCount + 1 ), string( "1" ) );
    SW_EXPECT_TRUE( rig.forgeCall( kAvatarID, "attackTarget", listText, NetChannelType::ReliableOrdered ) == NetHandleResult::Malformed );
}

/** @brief [RemoteCallTest] 서버가 주인 클라이언트의 `Client` 함수를 부른다. 서버는 `Server` 함수를 클라이언트에 보내지 않는다. */
SW_TEST_CASE( RemoteCallTest, ServerCallsTheOwnerClientFunction )
{
    RemoteCallTestRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID ) );
    SW_REQUIRE_TRUE( rig._client.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._clientAvatar ) );
    SW_EXPECT_TRUE( rig._server.callClient( kAvatarID, "receiveHealth", { ReflectedValue::make( int32{ 42 } ) } ) );
    SW_EXPECT_LOG( "RemoteCall.ClientCallRefused" );
    SW_EXPECT_FALSE( rig._server.callClient( kAvatarID, "attackTarget", { ReflectedValue::make( int32{ 1 } ), ReflectedValue::make( int32{ 1 } ) } ) );
    rig.pollUntilDelivered();
    SW_EXPECT_EQUAL( 42, rig._clientAvatar._shownHealth );
    SW_EXPECT_EQUAL( uint64{ 1 }, rig._client.getInvokedCount() );
}

/** @brief [RemoteCallTest] 계약을 어긴 타입(검증 짝 없음 · 글로 못 싣는 인자)은 등록되지 않는다 — 실행 중에 부를 수 없는 함수를 발견하지 않는다. */
SW_TEST_CASE( RemoteCallTest, RegistrationChecksTheContract )
{
    RemoteCallServer               server;
    RemoteCallTestMissingValidator missing;
    RemoteCallTestVectorArgument   vectorArgument;
    SW_EXPECT_LOG( "RemoteCall.ValidatorMissing" );
    SW_EXPECT_FALSE( server.registerTarget( 1, *RemoteCallTestMissingValidator::StaticType(), &missing, 0 ) );
    SW_EXPECT_LOG( "RemoteCall.ParameterNotWire" );
    SW_EXPECT_FALSE( server.registerTarget( 2, *RemoteCallTestVectorArgument::StaticType(), &vectorArgument, 0 ) );
    SW_EXPECT_FALSE( server.hasTarget( 1 ) );
    SW_EXPECT_FALSE( server.hasTarget( 2 ) );

    RemoteCallTestAvatar avatar;
    SW_EXPECT_TRUE( server.registerTarget( 3, *RemoteCallTestAvatar::StaticType(), &avatar, 0 ) );
    SW_EXPECT_LOG( "RemoteCall.ServerTargetRefused" );
    SW_EXPECT_FALSE( server.registerTarget( 3, *RemoteCallTestAvatar::StaticType(), &avatar, 0 ) );
}

/** @brief [RemoteCallTest] 세션 대상(id 0)은 연결마다 하나다 — 클라이언트가 서버 번호를 몰라도 양쪽이 서로 부르고, 연결이 닫히면 지워진다. */
SW_TEST_CASE( RemoteCallTest, SessionTargetIsPerConnection )
{
    RemoteCallTestRig rig;
    SW_REQUIRE_TRUE( rig._server.registerSessionTarget( rig._connectionID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar ) );
    SW_REQUIRE_TRUE( rig._client.registerTarget( RemoteCallTargets::kSessionTargetID, *RemoteCallTestAvatar::StaticType(), &rig._clientAvatar ) );
    SW_EXPECT_NOT_EQUAL( 0u, rig._client.callServer( RemoteCallTargets::kSessionTargetID, *RemoteCallTestAvatar::StaticType(), "attackTarget",
                                                     { ReflectedValue::make( int32{ 3 } ), ReflectedValue::make( int32{ 5 } ) } ) );
    SW_EXPECT_TRUE( rig._server.callSession( rig._connectionID, "receiveHealth", { ReflectedValue::make( int32{ 77 } ) } ) );
    rig.pollUntilDelivered();
    SW_EXPECT_EQUAL( 3, rig._serverAvatar._lastTargetID );
    SW_EXPECT_EQUAL( 77, rig._clientAvatar._shownHealth );

    SW_EXPECT_LOG( "RemoteCall.ServerTargetRefused" );
    SW_EXPECT_FALSE( rig._server.registerTarget( RemoteCallTargets::kSessionTargetID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID ) );
    rig._server.onConnectionClosed( rig._connectionID, NetDisconnectReason::Timeout );
    SW_EXPECT_FALSE( rig._server.hasSessionTarget( rig._connectionID ) );
}

/** @brief [RemoteCallTest] 연결이 닫히면 그 연결이 주인인 대상이 지워진다(같은 슬롯의 다음 연결이 물려받지 않는다). */
SW_TEST_CASE( RemoteCallTest, ClosedConnectionDropsItsTargets )
{
    RemoteCallServer     server;
    RemoteCallTestAvatar avatar;
    SW_REQUIRE_TRUE( server.registerTarget( 5, *RemoteCallTestAvatar::StaticType(), &avatar, 3 ) );
    SW_REQUIRE_TRUE( server.registerTarget( 6, *RemoteCallTestAvatar::StaticType(), &avatar, 4 ) );
    server.onConnectionClosed( 3, NetDisconnectReason::Timeout );
    SW_EXPECT_FALSE( server.hasTarget( 5 ) );
    SW_EXPECT_TRUE( server.hasTarget( 6 ) );
}
