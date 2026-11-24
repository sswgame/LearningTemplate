// PairedAnimationNetStateTest — 페어드 애니메이션 세션 상태의 바이트는 칸 선언(`PairedAnimationSessionStateBody`)이 정한다.
// 왕복하면 경로 · 역할 번호 · 스냅숏이 같고, 빈 경로 · 상한을 넘는 역할 수 · 잘린 바이트는 쓰지도 읽지도 않는다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Feature/Network/NetPairedAnimation/PairedAnimationReplication.h"

using namespace sw;

/** @brief [PairedAnimationNetStateTest] 세션 상태가 같은 값으로 돌아오고, 빈 경로 · 역할 수 상한 초과는 쓰지 않으며, 잘린 바이트는 읽지 않는다. */
SW_TEST_CASE( PairedAnimationNetStateTest, StateRoundTripsAndRejectsOverLimits )
{
    PairedAnimationNetState sent;
    sent._assetPath                 = "Character/Paired/Takedown.pairedanim.json";
    sent._listRoleNetID             = { 0x1122334455667788ull, 0, 42 };
    sent._snapshot._sessionID       = 9;
    sent._snapshot._anchorTime      = 0.75f;
    sent._snapshot._nextNotifyIndex = 2;
    PairedAnimationRoleSnapshot role;
    role._stage           = PairedAnimationRoleStage::Playing;
    role._entryAnchorTime = 0.25f;
    sent._snapshot._listRole.push_back( role );
    BitWriter writer;
    SW_REQUIRE_TRUE( sent.write( writer ) );

    BitReader               reader( writer.getBytes().data(), static_cast<int32>( writer.getBytes().size() ) );
    PairedAnimationNetState received;
    SW_REQUIRE_TRUE( PairedAnimationNetState::read( reader, received ) );
    SW_EXPECT_TRUE( received._assetPath == sent._assetPath );
    SW_EXPECT_TRUE( received._listRoleNetID == sent._listRoleNetID );
    SW_EXPECT_EQUAL( uint32{ 9 }, received._snapshot._sessionID );
    SW_EXPECT_EQUAL( 0.75f, received._snapshot._anchorTime );
    SW_REQUIRE_EQUAL( size_t{ 1 }, received._snapshot._listRole.size() );
    SW_EXPECT_TRUE( received._snapshot._listRole[0]._stage == PairedAnimationRoleStage::Playing );

    PairedAnimationNetState noPath = sent;
    noPath._assetPath.clear();
    BitWriter noPathWriter;
    SW_EXPECT_FALSE( noPath.write( noPathWriter ) );

    PairedAnimationNetState crowded = sent;
    crowded._listRoleNetID.assign( static_cast<size_t>( PairedAnimationNetLimit::kMaxRoleCount ) + 1, NetObjectID{ 1 } );
    BitWriter crowdedWriter;
    SW_EXPECT_FALSE( crowded.write( crowdedWriter ) );

    // 스냅숏도 칸 선언이다 — 역할 상한을 넘거나 시각이 유한하지 않으면 바이트를 내지 않는다.
    PairedAnimationSessionSnapshot crowdedSnapshot = sent._snapshot;
    crowdedSnapshot._listRole.resize( static_cast<size_t>( PairedAnimationSessionSnapshot::kMaxRoleCount ) + 1 );
    vector<uint8> snapshotBytes;
    SW_EXPECT_FALSE( crowdedSnapshot.writeBytes( snapshotBytes ) );
    SW_EXPECT_TRUE( snapshotBytes.empty() );

    const vector<uint8>     truncated( writer.getBytes().begin(), writer.getBytes().begin() + static_cast<std::ptrdiff_t>( writer.getBytes().size() / 2 ) );
    BitReader               truncatedReader( truncated.data(), static_cast<int32>( truncated.size() ) );
    PairedAnimationNetState cleared = sent;
    SW_EXPECT_FALSE( PairedAnimationNetState::read( truncatedReader, cleared ) );
    SW_EXPECT_TRUE( cleared._assetPath.empty() ); // 깨졌으면 비운다
}

/** @brief [PairedAnimationNetStateTest] 앞 씬의 세대를 실은 세션 상태는 소비하고 버린다 — 이름이 같은 새 씬의 오브젝트에 세션을 잇지 않는다. */
SW_TEST_CASE( PairedAnimationNetStateTest, StateFromAnEarlierWorldIsDropped )
{
    GameObjectManager                manager;
    PairedAnimationReplicationClient client;
    client.initialize( &manager );
    PairedAnimationNetState state;
    state._assetPath           = "Character/Paired/Missing.pairedanim.json";
    state._snapshot._sessionID = 9;
    NetMessageWriter messageWriter;
    BitWriter&       writer = messageWriter.beginMessage<NetPairedAnimationMessage::Table, NetPairedAnimationMessage::SessionState>();
    writer.writeUint32( 7 ); // 이 월드는 링크가 없어 0 이다
    writer.writeVarUint( state._snapshot._sessionID );
    SW_REQUIRE_TRUE( state.write( writer ) );
    SW_EXPECT_TRUE( NetHandleResult::Handled == client.handleMessage( 0, messageWriter.getBytes() ) );
    SW_EXPECT_EQUAL( 0u, client.getStats()._rejectedCount ); // 적용을 시도하지 않았다(없는 에셋이면 거절로 센다)
    SW_EXPECT_EQUAL( 0u, client.getStats()._appliedCount );
}
