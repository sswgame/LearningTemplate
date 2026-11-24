// NetMessageChannelTest — 복제 타입 등록(`registerReplicatedType`)이 와이어 버전을 가르고, 확인한 채널 안에서만 복제 상태를 주고받는다.
#include "pch.h"

#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Transport/NetTransport.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Network/GameNetSession.h"
#include "Engine/Network/NetMessageChannel.h"
#include "Engine/Reflection/Tests/ReflectionTestTypes.h"
#include "Engine/Serialization/Fixed/Tests/FixedLayoutTestTypes.h"

using namespace sw;

namespace
{
    constexpr uint8 kPawnTypeForTest = 1;
} // namespace

/**
 * @brief [NetMessageChannelTest] 같은 엔티티 타입 번호에 복제 스키마가 다른 빌드는 핸드셰이크에서 VersionMismatch 로 거절되고, 같은 스키마는 붙는다
 * @details 칸 순서 · 범위가 다른 빌드가 조용히 어긋난 바이트를 읽던 자리다(Godot `SceneReplicationConfig` 처럼 해시가 없으면 그렇다). 전역 세션 길(`computeSchemaHash` →
 *          `GameNetSession::combineWireContent`)도 같은 값을 가른다.
 */
SW_TEST_CASE( NetMessageChannelTest, ReplicatedSchemaMismatchIsRefusedAtHandshake )
{
    NetMessageChannel current;
    NetMessageChannel same;
    NetMessageChannel other;
    SW_REQUIRE_TRUE( current.registerReplicatedType<ReplicatedLayoutSample>( kPawnTypeForTest ) );
    SW_REQUIRE_TRUE( same.registerReplicatedType<ReplicatedLayoutSample>( kPawnTypeForTest ) );
    SW_REQUIRE_TRUE( other.registerReplicatedType<PropertyRoleActor>( kPawnTypeForTest ) ); // 같은 번호, 다른 칸
    SW_EXPECT_EQUAL( current.computeSchemaHash(), same.computeSchemaHash() );
    SW_EXPECT_TRUE( current.computeSchemaHash() != other.computeSchemaHash() );
    SW_EXPECT_EQUAL( NetMessageChannel{}.computeSchemaHash(), uint64{ 0 } ); // 등록이 없으면 와이어 버전이 바뀌지 않는다

    GameNetWireContent currentContent;
    currentContent._replicatedSchemaHash = current.computeSchemaHash();
    GameNetWireContent otherContent;
    otherContent._replicatedSchemaHash = other.computeSchemaHash();
    SW_EXPECT_TRUE( GameNetSession::combineWireContent( 7u, currentContent ) != GameNetSession::combineWireContent( 7u, otherContent ) );
    SW_EXPECT_EQUAL( GameNetSession::combineWireContent( 7u, GameNetWireContent{} ), 7u );

    NetEndpointConfig baseSettings;
    NetEndpointConfig serverSettings;
    NetEndpointConfig sameSettings;
    NetEndpointConfig otherSettings;
    SW_REQUIRE_TRUE( current.seal( baseSettings, serverSettings ) );
    SW_REQUIRE_TRUE( same.seal( baseSettings, sameSettings ) );
    SW_REQUIRE_TRUE( other.seal( baseSettings, otherSettings ) );
    SW_EXPECT_EQUAL( serverSettings._wireVersion, sameSettings._wireVersion );
    SW_EXPECT_TRUE( serverSettings._wireVersion != otherSettings._wireVersion );

    LoopbackNetwork network;
    NetEndpoint     server;
    NetEndpoint     sameClient;
    NetEndpoint     otherClient;
    SW_REQUIRE_TRUE( server.initialize( network.createEndpoint( 4100 ), serverSettings ) );
    SW_REQUIRE_TRUE( sameClient.initialize( network.createEndpoint( 5101 ), sameSettings ) );
    SW_REQUIRE_TRUE( otherClient.initialize( network.createEndpoint( 5102 ), otherSettings ) );
    SW_REQUIRE_TRUE( server.listen() );
    SW_REQUIRE_TRUE( sameClient.connect( NetAddress::makeLoopback( 4100 ) ) );
    SW_REQUIRE_TRUE( otherClient.connect( NetAddress::makeLoopback( 4100 ) ) );
    float64 time = 0.0;
    SW_EXPECT_LOG( "Net.ConnectionRefused" );
    SW_EXPECT_LOG( "Net.ServerRefused" );
    for ( int32 frame = 0; frame < 20; ++frame )
    {
        time += 1.0 / 60.0;
        server.update( time );
        sameClient.update( time );
        otherClient.update( time );
    }
    SW_EXPECT_TRUE( sameClient.getConnectionState( 0 ) == NetConnectionState::Connected );
    vector<NetEndpointEvent> listEvent;
    otherClient.pollEvents( listEvent );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._reason == NetDisconnectReason::VersionMismatch );

    // 확인한 채널 안에서는 복제 칸만 오가고, 등록하지 않은 형식으로 읽으면 SchemaMismatch 다.
    ReplicatedLayoutSample sent;
    sent._health = 9;
    BitWriter writer;
    SW_REQUIRE_TRUE( current.writeReplicated( kPawnTypeForTest, sent, writer ) );
    const vector<uint8>    bytes = writer.releaseBytes();
    ReplicatedLayoutSample received;
    BitReader              reader( bytes.data(), static_cast<int32>( bytes.size() ) );
    SW_REQUIRE_TRUE( same.readReplicated( kPawnTypeForTest, reader, received, nullptr ) == FixedReadResult::Ok );
    SW_EXPECT_EQUAL( received._health, 9 );
    PropertyRoleActor wrongType;
    BitReader         wrongReader( bytes.data(), static_cast<int32>( bytes.size() ) );
    SW_EXPECT_LOG( "NetMessageChannel.MessageKindXNot" );
    SW_EXPECT_TRUE( current.readReplicated( kPawnTypeForTest, wrongReader, wrongType, nullptr ) == FixedReadResult::SchemaMismatch );
}
