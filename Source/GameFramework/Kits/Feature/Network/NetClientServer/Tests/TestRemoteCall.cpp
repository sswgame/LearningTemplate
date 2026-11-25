#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Serialization/BitStream.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/RemoteCall.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/Tests/RemoteCallTestTypes.h"

#include <limits>

// 반사 함수 호출 — `FUNCTION( Server · Client · Reliable · Validate )` 메타데이터가 주인 · 역할 · 채널 · 검증 정책으로 실제 선을 지나 지켜지는가.

using namespace sw;

namespace
{
    constexpr uint32 kAvatarID = 11;

    /** @brief 테스트가 넘기지 않는 한 상한에 닿지 않는 호출률이다(한 테스트의 호출이 수십 개 안이다). */
    constexpr RemoteCallServerConfig kRoomyCallLimit{ 1000, 1000 };

    /** @brief 서버 하나와 클라이언트 하나를 루프백으로 잇고, 받은 메시지를 받은 채널 그대로 넘깁니다. */
    struct RemoteCallTestRig
    {
        test::LoopbackCluster _cluster{ 11u };
        NetMessageRouter      _serverRouter; ///< 월드 세대만 쓴다(메시지는 직접 전달한다)
        NetMessageRouter      _clientRouter;
        RemoteCallServer      _server;
        RemoteCallClient      _client;
        RemoteCallTestAvatar  _serverAvatar;
        RemoteCallTestAvatar  _clientAvatar;
        int32                 _connectionID{ -1 };

        explicit RemoteCallTestRig( const RemoteCallServerConfig& callLimit = kRoomyCallLimit )
        {
            const NetEndpointConfig settings;
            (void)_cluster.addEndpoint( 4300, settings );
            (void)_cluster.addEndpoint( 4400, settings );
            SW_EXPECT_TRUE( _cluster.connectClients( 30 ) );
            SW_EXPECT_TRUE( _server.initialize( &_cluster.getServer(), &_serverRouter, callLimit ) );
            _server.setClock( []() -> int64
            { return 0; } ); // 버킷이 다시 차지 않는다 — 상한 판정이 실행 속도(ASan)에 달리지 않게
            _client.initialize( &_cluster.getClient( 0 ), &_clientRouter );
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

        /** @brief 클라이언트를 거치지 않고 손으로 짠 호출을 서버에 넘깁니다(조작한 클라이언트의 흉내). */
        NetHandleResult craftCall( uint32 targetID, string_view functionName, const vector<int64>& listInteger, NetChannelKind channel )
        {
            return craftCallWith( targetID, functionName, static_cast<uint32>( listInteger.size() ), channel, [&listInteger]( BitWriter& writer )
            {
                for ( const int64 integer : listInteger )
                {
                    writer.writeVarInt( integer );
                }
            } );
        }

        /** @brief 실수 하나를 받는 함수(`moveStep`)의 손 호출입니다. */
        NetHandleResult craftFloatCall( uint32 targetID, string_view functionName, float32 number, NetChannelKind channel )
        {
            return craftCallWith( targetID, functionName, 1u, channel, [number]( BitWriter& writer )
            { writer.writeFloat( number ); } );
        }

        template <typename TWriteArguments>
        NetHandleResult craftCallWith( uint32 targetID, string_view functionName, uint32 argumentCount, NetChannelKind channel, TWriteArguments&& writeArguments )
        {
            NetMessageWriter messageWriter;
            BitWriter&       writer = messageWriter.begin( NetRemoteCallMessage::kCall );
            writer.writeVarUint( 1 );
            writer.writeVarUint( targetID );
            if ( targetID != RemoteCallTargets::kSessionTargetID )
                writer.writeBits( _serverRouter.getWorldGeneration(), 8 );
            writer.writeUint32( hashed_string::computeHash( functionName ) ); // 함수 번호 = 이름 해시(`RemoteCallTargets::getFunctionID`)
            writer.writeVarUint( argumentCount );
            writeArguments( writer );
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

/** @brief [RemoteCallTest] 인자는 타입별 이진 꼴로 오간다 — 정수 끝값 · uint64 · float64 · 글 · bool · 열거형이 바이트 손실 없이 같은 값으로 닿는다. */
SW_TEST_CASE( RemoteCallTest, ArgumentsTravelAsTypedBinary )
{
    RemoteCallTestRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID ) );
    const uint64  bigID  = 0xFEDCBA9876543210ull;
    const float64 number = 0.1 + 0.2; // 글로는 짧게 줄던 값이다
    SW_EXPECT_NOT_EQUAL( 0u, rig._client.callServer( kAvatarID, *RemoteCallTestAvatar::StaticType(), "recordArguments",
                                                     { ReflectedValue::make( bigID ), ReflectedValue::make( number ), ReflectedValue::makeText( "가 나" ), ReflectedValue::make( true ),
                                                       ReflectedValue::make( RemoteCallTestMode::Loud ) } ) );
    rig.pollUntilDelivered();
    SW_EXPECT_EQUAL( bigID, rig._serverAvatar._lastBigID );
    SW_EXPECT_TRUE( number == rig._serverAvatar._lastNumber );
    SW_EXPECT_TRUE( rig._serverAvatar._lastText == "가 나" );
    SW_EXPECT_TRUE( rig._serverAvatar._lastMode == RemoteCallTestMode::Loud );
    SW_EXPECT_TRUE( rig._serverAvatar._bLastFlag );

    // 등록되지 않은 열거자 · 유한하지 않은 실수는 몸에 닿지 않는다(거절 · 깨진 호출).
    const auto writeAll = []( int64 mode, float64 value )
    {
        return [mode, value]( BitWriter& writer )
        {
            uint64 bits = 0;
            Memory::copy( &bits, &value, sizeof( bits ) );
            writer.writeVarUint( bigID );
            writer.writeUint32( static_cast<uint32>( bits ) );
            writer.writeUint32( static_cast<uint32>( bits >> 32 ) );
            writer.writeVarUint( 0 ); // 빈 글의 길이
            writer.writeBool( false );
            writer.writeVarInt( mode );
        };
    };
    SW_EXPECT_TRUE( rig.craftCallWith( kAvatarID, "recordArguments", 5u, NetChannelKind::ReliableOrdered, writeAll( 99, 1.0 ) ) == NetHandleResult::Handled );
    SW_EXPECT_TRUE( rig.craftCallWith( kAvatarID, "recordArguments", 5u, NetChannelKind::ReliableOrdered, writeAll( 0, std::numeric_limits<float64>::infinity() ) ) == NetHandleResult::Malformed );
    SW_EXPECT_EQUAL( uint64{ 1 }, rig._server.getStats()._invokedCount );
    SW_EXPECT_EQUAL( uint64{ 1 }, rig._server.getStats()._rejectedCount );
}

/** @brief [RemoteCallTest] 검증 짝이 거짓이면 몸을 부르지 않고 그 연결을 끊는다(언리얼 WithValidation). */
SW_TEST_CASE( RemoteCallTest, FailedValidationDisconnects )
{
    RemoteCallTestRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID ) );
    SW_EXPECT_LOG( "RemoteCall.ValidationFailed" );
    // 정직한 클라이언트도 이 호출을 보낼 수 있다(검증은 서버 몫) — 망을 더 돌리지 않고 서버 쪽만 본다.
    SW_EXPECT_TRUE( rig.craftCall( kAvatarID, "attackTarget", { 7, 99 }, NetChannelKind::ReliableOrdered ) == NetHandleResult::Handled );
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
    SW_EXPECT_TRUE( rig.craftCall( kAvatarID, "attackTarget", { 7, 20 }, NetChannelKind::ReliableOrdered ) == NetHandleResult::Handled );
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
    SW_EXPECT_TRUE( rig.craftCall( kAvatarID, "applyLocal", { 5 }, NetChannelKind::ReliableOrdered ) == NetHandleResult::Handled );
    SW_EXPECT_EQUAL( 0, rig._serverAvatar._localApplyCount );
    SW_EXPECT_EQUAL( uint64{ 1 }, rig._server.getStats()._violationCount );
}

/** @brief [RemoteCallTest] `Reliable` 함수가 비신뢰 채널로 오면 보내는 쪽 규칙을 어긴 것이라 끊는다. */
SW_TEST_CASE( RemoteCallTest, ReliableFunctionOnUnreliableChannelIsRefused )
{
    RemoteCallTestRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID ) );
    SW_EXPECT_LOG( "RemoteCall.Violation" );
    SW_EXPECT_TRUE( rig.craftCall( kAvatarID, "attackTarget", { 7, 20 }, NetChannelKind::Unreliable ) == NetHandleResult::Handled );
    SW_EXPECT_EQUAL( 0, rig._serverAvatar._totalPower );
}

/** @brief [RemoteCallTest] 없는 대상 · 함수 · 그 타입에 들지 않는 인자(int32 밖)는 끊지 않고 같은 순번으로 거절을 알린다. */
SW_TEST_CASE( RemoteCallTest, UnknownTargetAndBadArgumentsAreRejected )
{
    RemoteCallTestRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID ) );
    const uint32 sequence = rig._client.callServer( 999, *RemoteCallTestAvatar::StaticType(), "moveStep", { ReflectedValue::make( 1.0f ) } );
    SW_REQUIRE_TRUE( sequence != 0u );
    rig.pollUntilDelivered();
    SW_EXPECT_TRUE( rig.craftCall( kAvatarID, "attackTarget", { int64{ 1 } << 40, 20 }, NetChannelKind::ReliableOrdered ) == NetHandleResult::Handled );
    SW_EXPECT_TRUE( rig.craftCall( kAvatarID, "noSuchFunction", {}, NetChannelKind::ReliableOrdered ) == NetHandleResult::Handled );
    rig.pollUntilDelivered();

    vector<RemoteCallRejectNotification> listNotification;
    rig._client.pollRejections( listNotification );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listNotification.size() );
    SW_EXPECT_EQUAL( sequence, listNotification[0]._sequence );
    SW_EXPECT_TRUE( listNotification[0]._error == RemoteCallError::UnknownTarget );
    SW_EXPECT_TRUE( listNotification[1]._error == RemoteCallError::ArgumentMismatch );
    SW_EXPECT_TRUE( listNotification[2]._error == RemoteCallError::UnknownFunction );
    SW_EXPECT_EQUAL( uint64{ 0 }, rig._server.getStats()._violationCount );
    SW_EXPECT_EQUAL( uint64{ 3 }, rig._server.getStats()._rejectedCount );
}

/** @brief [RemoteCallTest] 상한을 넘는 인자 수는 손상된 메시지다. */
SW_TEST_CASE( RemoteCallTest, TooManyArgumentsAreMalformed )
{
    RemoteCallTestRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID ) );
    const vector<int64> listInteger( static_cast<size_t>( NetRemoteCallMessage::kMaxArgumentCount + 1 ), int64{ 1 } );
    SW_EXPECT_TRUE( rig.craftCall( kAvatarID, "attackTarget", listInteger, NetChannelKind::ReliableOrdered ) == NetHandleResult::Malformed );
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

/** @brief [RemoteCallTest] 계약을 어긴 타입(검증 짝 없음 · 실을 수 없는 인자)은 등록되지 않는다 — 실행 중에 부를 수 없는 함수를 발견하지 않는다. */
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

/** @brief [RemoteCallTest] 와이어 번호는 타입마다 한 번 지은 색인으로 찾는다 — 검증 짝이 함께 들고, 타입 테이블 세대가 바뀌면 다시 짓는다. */
SW_TEST_CASE( RemoteCallTest, FunctionTableIsBuiltOncePerTypeGeneration )
{
    RemoteCallTargets targets;
    const TypeInfo&   type = *RemoteCallTestAvatar::StaticType();
    SW_REQUIRE_TRUE( targets.validateRemoteCallContract( type, FunctionNetRole::Server ) );
    SW_REQUIRE_EQUAL( targets._mapFunctionTable.size(), size_t{ 1 } );

    const uint32                            functionID = hashed_string::computeHash( "attackTarget" );
    const RemoteCallTargets::RemoteFunction attack     = targets.findFunctionByID( type, functionID );
    SW_REQUIRE_NOT_NULL( attack._pFunction );
    SW_EXPECT_EQUAL( attack._pFunction, type.findMethodInHierarchy( hashed_string( "attackTarget" ) ) );
    SW_EXPECT_EQUAL( attack._pValidator, type.findMethodInHierarchy( hashed_string( "validateAttackTarget" ) ) );
    SW_EXPECT_TRUE( targets.findFunctionByID( type, hashed_string::computeHash( "noSuchRemoteFunction" ) )._pFunction == nullptr );

    // 같은 세대면 지은 색인을 그대로 쓰고(비운 칸이 다시 차지 않는다), 세대가 다르면 다시 짓는다.
    RemoteCallTargets::FunctionTable& table = targets.getOrBuildFunctionTable( type );
    SW_EXPECT_EQUAL( table._contractGeneration, table._typeTableGeneration );
    table._mapFunction.clear();
    SW_EXPECT_TRUE( targets.findFunctionByID( type, functionID )._pFunction == nullptr );
    --table._typeTableGeneration;
    SW_EXPECT_EQUAL( targets.findFunctionByID( type, functionID )._pFunction, attack._pFunction );
    SW_EXPECT_NOT_EQUAL( table._contractGeneration, table._typeTableGeneration );
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

/** @brief [RemoteCallTest] 연결이 버킷보다 많이 부르면 넘친 호출은 몸까지 가지 않고 `RateLimited` 로 거절된다 — 끊지는 않는다(감사 N-13, 언리얼 RPC DoS Detection). */
SW_TEST_CASE( RemoteCallTest, CallsOverTheRateLimitAreRejected )
{
    // 다시 차는 간격을 길게 잡아 테스트가 도는 동안 버킷이 차지 않는다.
    RemoteCallTestRig rig( RemoteCallServerConfig{ 3, 600000 } );
    SW_REQUIRE_TRUE( rig._server.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID ) );
    for ( int32 index = 0; index < 5; ++index )
    {
        SW_EXPECT_TRUE( rig.craftFloatCall( kAvatarID, "moveStep", 1.0f, NetChannelKind::Unreliable ) == NetHandleResult::Handled );
    }
    SW_EXPECT_EQUAL( uint64{ 3 }, rig._server.getStats()._invokedCount );
    SW_EXPECT_EQUAL( uint64{ 2 }, rig._server.getStats()._rateLimitedCount );
    SW_EXPECT_NEAR( 3.0, rig._serverAvatar._movedDistance, 1e-4 );
    SW_EXPECT_TRUE( rig._cluster.getServer().getConnectionState( rig._connectionID ) == NetConnectionState::Connected );

    rig.pollUntilDelivered();
    vector<RemoteCallRejectNotification> listNotification;
    rig._client.pollRejections( listNotification );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listNotification.size() );
    SW_EXPECT_TRUE( listNotification[0]._error == RemoteCallError::RateLimited );
    SW_EXPECT_TRUE( listNotification[1]._error == RemoteCallError::RateLimited );
}

/** @brief [RemoteCallTest] 호출률 상한이 없으면 서버가 서지 않는다 — 기본값으로 대신하지 않는다. */
SW_TEST_CASE( RemoteCallTest, ServerRefusesAMissingRateLimit )
{
    RemoteCallServer server;
    SW_EXPECT_LOG( "RemoteCall.RateLimitMissing" );
    SW_EXPECT_FALSE( server.initialize( nullptr, nullptr, RemoteCallServerConfig{} ) );
    SW_EXPECT_LOG( "RemoteCall.RateLimitMissing" );
    SW_EXPECT_FALSE( server.initialize( nullptr, nullptr, RemoteCallServerConfig{ 4, 0 } ) );
}

/**
 * @brief [RemoteCallTest] `MaxCallsPerSecond` 를 적은 함수는 연결 상한 안이어도 그 함수의 상한에서 거절된다. 적지 않은 함수는 연결 상한만 받는다(계약)
 */
SW_TEST_CASE( RemoteCallTest, PerFunctionRateLimitRejectsOnlyThatFunction )
{
    RemoteCallTestRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID ) );
    for ( int32 index = 0; index < 4; ++index )
    {
        SW_EXPECT_TRUE( rig.craftCall( kAvatarID, "placeMarker", { 9 }, NetChannelKind::Unreliable ) == NetHandleResult::Handled );
    }
    for ( int32 index = 0; index < 4; ++index )
    {
        SW_EXPECT_TRUE( rig.craftFloatCall( kAvatarID, "moveStep", 1.0f, NetChannelKind::Unreliable ) == NetHandleResult::Handled );
    }
    SW_EXPECT_EQUAL( 9, rig._serverAvatar._lastMarkerID );
    SW_EXPECT_EQUAL( uint64{ 2 }, rig._server.getStats()._rateLimitedCount ); // placeMarker 넷 가운데 둘(한꺼번에 2)
    SW_EXPECT_EQUAL( uint64{ 6 }, rig._server.getStats()._invokedCount );     // placeMarker 둘 + moveStep 넷
    SW_EXPECT_NEAR( 4.0, rig._serverAvatar._movedDistance, 1e-4 );
}

/**
 * @brief [RemoteCallTest] `Server` 가 아닌 함수에 적은 함수별 상한은 지킬 쪽이 없어 등록이 거절된다
 */
SW_TEST_CASE( RemoteCallTest, RateOnANonServerFunctionIsRefused )
{
    RemoteCallClient         client;
    RemoteCallTestClientRate target;
    SW_EXPECT_LOG( "RemoteCall.RateOnNonServerFunction" );
    SW_EXPECT_FALSE( client.registerTarget( 1, *RemoteCallTestClientRate::StaticType(), &target ) );
}

/**
 * @brief [RemoteCallTest] 대상보다 먼저 온 신뢰 호출은 버리지 않고 미룬다(라우터가 보류한다) — 비신뢰 호출은 미루지 않고, 뒤 월드의 호출은 같은 번호의 대상이 있어도 부르지 않는다
 */
SW_TEST_CASE( RemoteCallTest, ReliableCallWaitsForItsTarget )
{
    RemoteCallTestRig rig;
    NetMessageWriter  messageWriter;
    const auto        writeCall = [&messageWriter]( uint8 worldGeneration )
    {
        BitWriter& writer = messageWriter.begin( NetRemoteCallMessage::kCall );
        writer.writeVarUint( 0 );
        writer.writeVarUint( kAvatarID );
        writer.writeBits( worldGeneration, 8 );
        writer.writeUint32( hashed_string::computeHash( "receiveHealth" ) );
        writer.writeVarUint( 1 );
        writer.writeVarInt( 42 );
    };
    writeCall( 0 );
    const uint8* pData = messageWriter.getBytes().data();
    const int32  size  = messageWriter.getByteCount();
    SW_EXPECT_TRUE( NetHandleResult::NotReady == rig._client.handleMessage( 0, pData, size, NetChannelKind::ReliableOrdered ) );
    SW_EXPECT_LOG( "RemoteCall.ClientCallDropped" );
    SW_EXPECT_TRUE( NetHandleResult::Handled == rig._client.handleMessage( 0, pData, size, NetChannelKind::Unreliable ) );
    SW_REQUIRE_TRUE( rig._client.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._clientAvatar ) );
    SW_EXPECT_TRUE( NetHandleResult::Handled == rig._client.handleMessage( 0, pData, size, NetChannelKind::ReliableOrdered ) );
    SW_EXPECT_EQUAL( 42, rig._clientAvatar._shownHealth );
    writeCall( 7 ); // 뒤 월드의 호출이다 — 그 씬이 설 때까지 기다린다
    SW_EXPECT_TRUE( NetHandleResult::NotReady == rig._client.handleMessage( 0, messageWriter.getBytes().data(), messageWriter.getByteCount(), NetChannelKind::ReliableOrdered ) );
}

/**
 * @brief [RemoteCallTest] 거절 본문은 고정 배치형이다 — 옛 형식(까닭 1 바이트) · 잘린 본문은 깨진 메시지이고 알림이 쌓이지 않는다. 온전한 거절은 쌓인다
 */
SW_TEST_CASE( RemoteCallTest, RejectedBodyRefusesOldLayoutAndTruncation )
{
    RemoteCallClient client;
    NetMessageWriter writer;
    BitWriter&       oldLayout = writer.begin( NetRemoteCallMessage::kRejected );
    oldLayout.writeVarUint( 3 );
    oldLayout.writeBits( 0xFFu, 8 ); // 까닭은 열거자 범위 비트(4 개 — 2 비트)다. 남는 6 비트가 0 이 아니다
    SW_EXPECT_TRUE( NetHandleResult::Malformed == client.handleMessage( 0, writer.getBytes() ) );
    const uint8 arrTruncated[] = { NetRemoteCallMessage::kRejected };
    SW_EXPECT_TRUE( NetHandleResult::Malformed == client.handleMessage( 0, arrTruncated, 1 ) );
    vector<RemoteCallRejectNotification> listNotification;
    client.pollRejections( listNotification );
    SW_EXPECT_TRUE( listNotification.empty() );

    RemoteCallRejectedBody rejected;
    rejected._sequence = 3;
    rejected._error    = RemoteCallError::RateLimited;
    SW_REQUIRE_TRUE( ( writer.writeMessage<NetRemoteCallMessage::Table, NetRemoteCallMessage::Rejected>( rejected ) ) );
    SW_EXPECT_TRUE( NetHandleResult::Handled == client.handleMessage( 0, writer.getBytes() ) );
    client.pollRejections( listNotification );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listNotification.size() );
    SW_EXPECT_TRUE( listNotification[0]._error == RemoteCallError::RateLimited );
}

/**
 * @brief [RemoteCallTest] 앞 씬에서 보낸 월드 대상 호출은 새 씬의 같은 번호 대상에 닿지 않는다 — 세션 대상은 씬이 바뀌어도 그대로 불린다
 */
SW_TEST_CASE( RemoteCallTest, CallFromAnEarlierWorldIsRejected )
{
    RemoteCallTestRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID ) );
    // 클라이언트가 아직 새 세대를 모른 채 보낸다(0) — 서버는 그 사이 새 씬을 열었다.
    SW_EXPECT_NOT_EQUAL( 0u, rig._client.callServer( kAvatarID, *RemoteCallTestAvatar::StaticType(), "attackTarget", { ReflectedValue::make( int32{ 7 } ), ReflectedValue::make( int32{ 20 } ) } ) );
    rig._serverRouter.openWorld( rig._cluster.getServer(), 0 );
    rig.pollUntilDelivered();
    SW_EXPECT_EQUAL( uint64{ 0 }, rig._server.getStats()._invokedCount );
    vector<RemoteCallRejectNotification> listNotification;
    rig._client.pollRejections( listNotification );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listNotification.size() );
    SW_EXPECT_TRUE( listNotification[0]._error == RemoteCallError::UnknownTarget );

    // 지금 세대를 실은 호출은 닿는다.
    SW_EXPECT_TRUE( NetHandleResult::Handled == rig.craftCall( kAvatarID, "attackTarget", { 7, 20 }, NetChannelKind::ReliableOrdered ) );
    SW_EXPECT_EQUAL( uint64{ 1 }, rig._server.getStats()._invokedCount );

    // 세션 대상은 세대를 싣지 않는다 — 씬이 바뀌어도 그 연결의 것이다.
    SW_REQUIRE_TRUE( rig._server.registerSessionTarget( rig._connectionID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar ) );
    SW_EXPECT_TRUE( NetHandleResult::Handled == rig.craftCall( RemoteCallTargets::kSessionTargetID, "attackTarget", { 7, 20 }, NetChannelKind::ReliableOrdered ) );
    SW_EXPECT_EQUAL( uint64{ 2 }, rig._server.getStats()._invokedCount );
}

/**
 * @brief [RemoteCallTest] 손 본문 호출 — 인자 뒤 남는 바이트 · 상한 넘는 인자 수 · 잘린 호출은 깨진 메시지이고 몸을 부르지 않는다
 */
SW_TEST_CASE( RemoteCallTest, CallBodyRejectsTrailingBytesAndHugeArgumentCounts )
{
    RemoteCallTestRig rig;
    SW_REQUIRE_TRUE( rig._server.registerTarget( kAvatarID, *RemoteCallTestAvatar::StaticType(), &rig._serverAvatar, rig._connectionID ) );
    NetMessageWriter messageWriter;
    BitWriter&       writer = messageWriter.begin( NetRemoteCallMessage::kCall );
    writer.writeVarUint( 1 );
    writer.writeVarUint( kAvatarID );
    writer.writeBits( 0, 8 ); // 월드 세대
    writer.writeUint32( 0 );  // 함수 번호
    writer.writeVarUint( 0 );
    writer.writeBits( 0xFFu, 8 ); // 남는 바이트
    SW_EXPECT_TRUE( NetHandleResult::Malformed == rig._server.handleMessage( rig._connectionID, messageWriter.getBytes() ) );

    BitWriter& hugeCount = messageWriter.begin( NetRemoteCallMessage::kCall );
    hugeCount.writeVarUint( 2 );
    hugeCount.writeVarUint( kAvatarID );
    hugeCount.writeBits( 0, 8 ); // 월드 세대
    hugeCount.writeUint32( 0 );  // 함수 번호
    hugeCount.writeVarUint( static_cast<uint64>( NetRemoteCallMessage::kMaxArgumentCount ) + 1 );
    SW_EXPECT_TRUE( NetHandleResult::Malformed == rig._server.handleMessage( rig._connectionID, messageWriter.getBytes() ) );

    const uint8 arrTruncated[] = { NetRemoteCallMessage::kCall, 3 };
    SW_EXPECT_TRUE( NetHandleResult::Malformed == rig._server.handleMessage( rig._connectionID, arrTruncated, 2 ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, rig._server.getStats()._invokedCount );
}
