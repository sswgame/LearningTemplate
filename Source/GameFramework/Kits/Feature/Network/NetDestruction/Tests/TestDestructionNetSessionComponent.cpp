#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionNetSessionComponent.h"
#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionReplication.h"
#include "GameFramework/Kits/Feature/Network/NetDestruction/Tests/DestructionNetTestComponents.h"

#include <limits>

// DestructionNetSessionComponentTest — 씬의 넷 링크(서버)에 붙은 파괴 세션이 틱 뒤 큐에서 프레임마다 한 번 복제를 돌리는지 본다(틱은 델타만 적는다).
// 관측점: 같은 틱 그룹에서 세션의 주 틱 뒤에 도는 서브틱이 세션 번호를 읽는다 — 틱 안에서 복제하면 오른 번호, 틱 뒤 큐면 오르기 전 번호가 보인다.

using namespace sw;

namespace
{
    /** @brief 아무 데도 보내지 않는 서버 쪽 상대입니다(연결 없음). */
    class SilentServerPeers final : public INetPeers
    {
    public:
        bool               sendMessage( int32, NetChannelKind, const uint8*, int32 ) override { return false; }
        bool               trySendMessage( int32, NetChannelKind, const uint8*, int32 ) override { return false; }
        bool               sendNotifiedMessage( int32, const uint8*, int32, uint32 ) override { return false; }
        int32              broadcast( NetChannelKind, const uint8*, int32, int32 ) override { return 0; }
        void               disconnect( int32 ) override {}
        void               pollInbound( NetInbound& ) override {}
        bool               isServer() const override { return true; }
        NetConnectionState getConnectionState( int32 ) const override { return NetConnectionState::Disconnected; }
        bool               getConnectionStats( int32, NetConnectionStats& ) const override { return false; }
        NetAddress         getConnectionAddress( int32 ) const override { return NetAddress{}; }
        uint64             getConnectionPrincipal( int32 ) const override { return 0; }
        optional<int32>    getClientIndex() const override { return nullopt; }
        int32              getConnectedCount() const override { return 0; }
        void               collectConnected( vector<int32>& outListConnection ) const override { outListConnection.clear(); }
        bool               isConnectionSaturated( int32 ) const override { return false; }
        void               addSentKindStats( NetMessageKindStatsTable& ) const override {}
    };
} // namespace

/**
 * @brief [DestructionNetSessionComponentTest] 서버 세션은 프레임마다 정확히 한 번, 틱이 아니라 틱 뒤 큐에서 복제를 돌린다(남의 파괴 컴포넌트를 쓰는 일이다)
 * @details 같은 그룹에서 세션의 주 틱 뒤에 도는 읽는 쪽 서브틱은 이번 프레임의 복제 전 번호를 봐야 한다. 틱 안에서 복제하던 옛 판은 오른 번호를 보여 진다.
 */
SW_TEST_CASE( DestructionNetSessionComponentTest, ServerSessionUpdatesOncePerFrameAfterTheTick )
{
    SilentServerPeers peers;
    NetMessageRouter  router;
    GameObjectManager manager;
    NetLinkValue      link;
    link._pPeers  = &peers;
    link._pRouter = &router;
    link._role    = NetLinkRole::Server;
    SW_REQUIRE_TRUE( manager.getSubsystem<SceneNetLink>().bind( link ) );
    DestructionNetSessionComponent*     pComponent = manager.createGameObject( hashed_string( "DestructionNet" ) )->addComponent<DestructionNetSessionComponent>();
    DestructionTickReaderTestComponent* pReader    = manager.createGameObject( hashed_string( "Reader" ) )->addComponent<DestructionTickReaderTestComponent>();
    pReader->_pSession                             = pComponent;
    manager.beginPlay();
    SW_REQUIRE_TRUE( pComponent->getSession().isActive() );

    const float32 deltaTime = constant::time::kDefaultFixedStepSeconds; // 컴포넌트의 기본 틱 간격과 같다 — 첫 틱 빈도 검사를 지난다
    manager.update( deltaTime );                                        // 첫 프레임은 틱 그룹 바꿈 · 읽는 쪽의 선행 조건 걸기가 든다
    manager.update( deltaTime );
    const uint32 startTick = pComponent->getSession().getServerTick();
    for ( uint32 frame = 1; frame <= 3; ++frame )
    {
        manager.update( deltaTime );
        SW_EXPECT_EQUAL( startTick + frame, pComponent->getSession().getServerTick() );
        // 틱 동안(세션 주 틱 뒤의 서브틱)은 이번 프레임의 복제 전 번호 — 복제는 그룹이 끝난 뒤 틱 뒤 큐에서 돈다.
        SW_EXPECT_EQUAL( startTick + frame - 1, pReader->_seenServerTick );
    }
    SW_EXPECT_TRUE( pComponent->getSession().isActive() );
    manager.endPlay();
}

/**
 * @brief [DestructionReplicationTest] 고정 배치형 본문 — 잘린 해시 · 남는 바이트 · 빈 스냅숏은 깨진 메시지이고, 등록하지 않은 오브젝트의 온전한 해시는 소비하고 버린다
 */
SW_TEST_CASE( DestructionReplicationTest, BodyMessagesRejectTruncatedAndTrailingBytes )
{
    DestructionReplicationClient client;
    DestructionHashBody          hash;
    hash._netID      = 42;
    hash._eventCount = 3;
    hash._hash       = 0x0123456789ABCDEFull;
    NetMessageWriter writer;
    SW_REQUIRE_TRUE( ( writer.writeMessage<NetDestructionMessage::Table, NetDestructionMessage::Hash>( hash ) ) );
    const vector<uint8> listExact = writer.getBytes();
    SW_EXPECT_TRUE( NetHandleResult::Handled == client.handleMessage( 0, listExact ) );

    const vector<uint8> listTruncated( listExact.begin(), listExact.end() - 1 );
    SW_EXPECT_TRUE( NetHandleResult::Malformed == client.handleMessage( 0, listTruncated ) );
    vector<uint8> listTrailing = listExact;
    listTrailing.push_back( 0xFF );
    SW_EXPECT_TRUE( NetHandleResult::Malformed == client.handleMessage( 0, listTrailing ) );

    DestructionSnapshotBody snapshot; // 바이트가 없다 — 서버는 이벤트가 없으면 보내지 않는다
    snapshot._netID = 42;
    SW_REQUIRE_TRUE( ( writer.writeMessage<NetDestructionMessage::Table, NetDestructionMessage::Snapshot>( snapshot ) ) );
    SW_EXPECT_TRUE( NetHandleResult::Malformed == client.handleMessage( 0, writer.getBytes() ) );

    // 클라이언트는 스냅숏 요청(클라이언트 → 서버)을 맡지 않는다.
    DestructionSnapshotRequestBody request;
    request._netID = 42;
    SW_REQUIRE_TRUE( ( writer.writeMessage<NetDestructionMessage::Table, NetDestructionMessage::SnapshotRequest>( request ) ) );
    SW_EXPECT_TRUE( NetHandleResult::NotMine == client.handleMessage( 0, writer.getBytes() ) );
}

/**
 * @brief [DestructionReplicationTest] 손 본문 자세 — 남는 바이트 · 잘린 자세 · 유한하지 않은 자세는 깨진 메시지다(하나도 적용하지 않는다)
 */
SW_TEST_CASE( DestructionReplicationTest, PoseMessagesRejectTrailingBytesAndNonFinitePoses )
{
    DestructionReplicationClient client;
    BitWriter                    writer;
    DestructionExactPoseNetState pose; // 회전 기본값 — 단위 사원수
    writer.writeBits( NetDestructionMessage::kPose, 8 );
    writer.writeBool( true );  // 멈춤 — 정확한 자세
    writer.writeVarUint( 42 ); // 오브젝트(등록 없음 — 소비하고 버린다)
    writer.writeVarUint( 3 );  // 틱
    writer.writeBool( true );  // 자세가 이어진다
    writer.writeVarUint( 5 );  // 덩어리
    SW_REQUIRE_TRUE( ReplicatedLayout::writeFull( pose, writer ) );
    writer.writeBool( false ); // 끝
    SW_EXPECT_TRUE( NetHandleResult::Handled == client.handleMessage( 0, writer.getBytes() ) );
    vector<uint8> listTrailing = writer.getBytes();
    listTrailing.push_back( 0xFF );
    SW_EXPECT_TRUE( NetHandleResult::Malformed == client.handleMessage( 0, listTrailing ) );
    const vector<uint8> listTruncated( writer.getBytes().begin(), writer.getBytes().end() - 2 );
    SW_EXPECT_TRUE( NetHandleResult::Malformed == client.handleMessage( 0, listTruncated ) );

    // 자리에 NaN 비트를 실은 자세 — 칸 선언의 범위가 거절한다.
    writer.clear();
    writer.writeBits( NetDestructionMessage::kPose, 8 );
    writer.writeBool( true );
    writer.writeVarUint( 42 );
    writer.writeVarUint( 3 );
    writer.writeBool( true );
    writer.writeVarUint( 5 );
    writer.writeFloat( std::numeric_limits<float32>::quiet_NaN() );
    for ( int32 index = 0; index < 6; ++index )
        writer.writeFloat( index == 5 ? 1.0f : 0.0f ); // 나머지 자리 · 회전(w = 1)
    writer.writeBool( false );
    SW_EXPECT_TRUE( NetHandleResult::Malformed == client.handleMessage( 0, writer.getBytes() ) );
}
