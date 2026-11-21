#include "pch.h"

#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/NetSnapshot.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationServer.h"

#include <type_traits>

// 엔티티 상태 판 번호 — 서버는 바뀐 엔티티에만 새 판을 주고, 연결별 보낸 기록은 (엔티티, 판)만 들며, 델타는 판이 바뀐 엔티티만 싣는다(와이어는 그대로).

using namespace sw;

namespace
{
    struct NetSnapshotRevisionTestInternal
    {
        static NetEntityState makeEntity( uint32 entityID, uint32 typeID, uint8 value, uint64 revision )
        {
            NetEntityState entity;
            entity._entityID = entityID;
            entity._typeID   = typeID;
            entity._buffer.assign( 4, value );
            entity._revision = revision;
            return entity;
        }

        static vector<uint8> makeBytes( uint8 value, size_t count ) { return vector<uint8>( count, value ); }

        static uint64 findRevision( const ReplicationServer& server, uint32 entityID )
        {
            const NetEntityState* pEntity = server.getWorldSnapshot().findEntity( entityID );
            return pEntity != nullptr ? pEntity->_revision : 0u;
        }
    };

    /** @brief 서버 하나와 클라이언트 하나를 루프백으로 잇고, 클라이언트가 받은 스냅샷 메시지의 바이트 수를 셉니다. */
    struct NetSnapshotRevisionTestRig
    {
        test::LoopbackCluster _cluster{ 11u };
        ReplicationServer     _server;
        ReplicationClient     _client;
        size_t                _receivedBytes{ 0 }; ///< 마지막 `pollUntilDelivered` 동안 클라이언트가 받은 바이트

        NetSnapshotRevisionTestRig()
        {
            const NetEndpointConfig settings;
            (void)_cluster.addEndpoint( 4300, settings ); // 빈 클러스터라 포트가 겹치지 않는다
            (void)_cluster.addEndpoint( 4400, settings );
            SW_EXPECT_TRUE( _cluster.connectClients( 30 ) );
            SW_EXPECT_TRUE( _server.initialize( &_cluster.getServer(), ReplicationServerConfig{} ) );
            SW_EXPECT_TRUE( _client.initialize( &_cluster.getClient( 0 ), ReplicationClientConfig{} ) );
        }

        void pollUntilDelivered()
        {
            _receivedBytes = 0;
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
                    _receivedBytes += buffer.size();
                    (void)_client.handleMessage( 0, buffer ); // 받은 스냅샷 — 받으면 곧 확인을 보낸다
                }
            }
        }

        /** @brief 엔티티 열 개(id 1~10, 32 바이트)를 싣고 보냅니다. @p changedEntityID 만 값이 @p changedValue 입니다(0 이면 바뀐 것 없음). */
        void sendTick( uint32 tick, uint32 changedEntityID, uint8 changedValue )
        {
            _server.beginTick( tick );
            for ( uint32 entityID = 1; entityID <= 10; ++entityID )
            {
                const uint8 value = entityID == changedEntityID ? changedValue : static_cast<uint8>( entityID );
                _server.setEntity( entityID, 7, NetSnapshotRevisionTestInternal::makeBytes( value, 32 ) );
            }
            _server.endTick();
            _server.sendSnapshots();
            pollUntilDelivered();
        }
    };
} // namespace

/** @brief [NetSnapshotRevisionTest] 종류 · 바이트가 같으면 판을 물려받고, 바뀌거나 지웠다 다시 만든 엔티티는 겹치지 않는 새 판을 받는다. */
SW_TEST_CASE( NetSnapshotRevisionTest, EndTickRevisesOnlyChangedEntities )
{
    ReplicationServer server;
    SW_REQUIRE_TRUE( server.initialize( nullptr, ReplicationServerConfig{} ) );
    const auto setTick = [&server]( uint32 tick, uint8 secondValue, uint32 secondType, bool bWithThird )
    {
        server.beginTick( tick );
        server.setEntity( 3, 1, NetSnapshotRevisionTestInternal::makeBytes( 30, 8 ) );
        server.setEntity( 2, secondType, NetSnapshotRevisionTestInternal::makeBytes( secondValue, 8 ) );
        if ( bWithThird )
            server.setEntity( 1, 1, NetSnapshotRevisionTestInternal::makeBytes( 10, 8 ) );
        server.endTick();
    };
    setTick( 1, 20, 1, true );
    const uint64 first  = NetSnapshotRevisionTestInternal::findRevision( server, 1 );
    const uint64 second = NetSnapshotRevisionTestInternal::findRevision( server, 2 );
    const uint64 third  = NetSnapshotRevisionTestInternal::findRevision( server, 3 );
    SW_EXPECT_TRUE( first != 0 && second != 0 && third != 0 );
    SW_EXPECT_TRUE( first != second && second != third && first != third );

    setTick( 2, 21, 1, true ); // 2 의 바이트만 바뀐다
    SW_EXPECT_EQUAL( first, NetSnapshotRevisionTestInternal::findRevision( server, 1 ) );
    SW_EXPECT_EQUAL( third, NetSnapshotRevisionTestInternal::findRevision( server, 3 ) );
    const uint64 secondChanged = NetSnapshotRevisionTestInternal::findRevision( server, 2 );
    SW_EXPECT_TRUE( secondChanged > second );

    setTick( 3, 21, 2, true ); // 2 의 종류만 바뀐다
    SW_EXPECT_TRUE( NetSnapshotRevisionTestInternal::findRevision( server, 2 ) > secondChanged );

    setTick( 4, 21, 2, false ); // 1 이 사라진다
    SW_EXPECT_TRUE( server.getWorldSnapshot().findEntity( 1 ) == nullptr );
    setTick( 5, 21, 2, true ); // 같은 바이트로 다시 만들어도 옛 판과 겹치지 않는다
    SW_EXPECT_TRUE( NetSnapshotRevisionTestInternal::findRevision( server, 1 ) > first );
}

/** @brief [NetSnapshotRevisionTest] 델타는 기준과 판이 다른 엔티티만 싣고, 기록은 (엔티티, 판)만 들며, 받는 쪽은 바뀌지 않은 와이어로 같은 상태를 복원한다. */
SW_TEST_CASE( NetSnapshotRevisionTest, WriteDeltaSendsOnlyRevisedEntities )
{
    static_assert( std::is_trivially_copyable<NetSentEntity>::value && sizeof( NetSentEntity ) <= 16, "보낸 기록은 상태 바이트 없이 (엔티티, 판)만 든다" );
    using Internal = NetSnapshotRevisionTestInternal;
    vector<NetEntityState> listWorld;
    listWorld.push_back( Internal::makeEntity( 1, 1, 11, 10 ) ); // 기준과 같은 판
    listWorld.push_back( Internal::makeEntity( 2, 1, 22, 21 ) ); // 판이 바뀌었다
    listWorld.push_back( Internal::makeEntity( 3, 1, 33, 30 ) ); // 새 엔티티
    const vector<uint32> listRelevant{ 0, 1, 2 };
    const vector<int32>  listOrder{ 0, 1, 2 };
    const NetOwnerState  ownerState;
    NetSentSnapshot      baseline;
    baseline._tick = 5;
    baseline._listEntity.push_back( NetSentEntity{ 10, 1 } );
    baseline._listEntity.push_back( NetSentEntity{ 20, 2 } );
    baseline._listEntity.push_back( NetSentEntity{ 90, 9 } ); // 사라진 엔티티

    const NetDeltaSource source{ listWorld, listRelevant, listOrder, ownerState, 6, 0 };
    BitWriter            writer;
    NetSentSnapshot      written;
    vector<uint8>        listCurrent;
    NetSnapshot::writeDelta( writer, source, &baseline, 1000, written, listCurrent );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listCurrent.size() );
    SW_EXPECT_EQUAL( NetSnapshot::kEntityAlreadyCurrent, listCurrent[0] );
    SW_EXPECT_EQUAL( NetSnapshot::kEntityWritten, listCurrent[1] );
    SW_EXPECT_EQUAL( NetSnapshot::kEntityWritten, listCurrent[2] );
    SW_EXPECT_EQUAL( uint32{ 6 }, written._tick );
    SW_REQUIRE_EQUAL( size_t{ 3 }, written._listEntity.size() );
    SW_EXPECT_TRUE( written._listEntity[0]._entityID == 1 && written._listEntity[0]._revision == 10 );
    SW_EXPECT_TRUE( written._listEntity[1]._entityID == 2 && written._listEntity[1]._revision == 21 );
    SW_EXPECT_TRUE( written._listEntity[2]._entityID == 3 && written._listEntity[2]._revision == 30 );

    // 받는 쪽: 기준(틱 5)의 바이트 위에 복원하면 서버 월드와 같다.
    NetSnapshot clientBaseline;
    clientBaseline._tick = 5;
    clientBaseline._listEntity.push_back( Internal::makeEntity( 1, 1, 11, 0 ) );
    clientBaseline._listEntity.push_back( Internal::makeEntity( 2, 1, 2, 0 ) );
    clientBaseline._listEntity.push_back( Internal::makeEntity( 9, 1, 99, 0 ) );
    BitReader   reader( writer.getBytes().data(), static_cast<int32>( writer.getBytes().size() ) );
    NetSnapshot restored;
    SW_REQUIRE_TRUE( NetSnapshot::readDelta( reader, &clientBaseline, restored ) );
    SW_EXPECT_EQUAL( uint32{ 6 }, restored._tick );
    SW_REQUIRE_EQUAL( size_t{ 3 }, restored._listEntity.size() );
    for ( size_t index = 0; index < listWorld.size(); ++index )
    {
        SW_EXPECT_EQUAL( listWorld[index]._entityID, restored._listEntity[index]._entityID );
        SW_EXPECT_TRUE( listWorld[index]._buffer == restored._listEntity[index]._buffer );
    }
}

/** @brief [NetSnapshotRevisionTest] 확인을 받은 뒤에는 확인한 틱의 기록이 기준이 되어 바뀐 엔티티 하나만 싣는다. 아무것도 안 바뀌면 머리만 간다. */
SW_TEST_CASE( NetSnapshotRevisionTest, AckedBaselineCarriesOnlyChangedEntity )
{
    NetSnapshotRevisionTestRig rig;
    rig.sendTick( 1, 0, 0 );
    const size_t fullBytes = rig._receivedBytes;
    SW_REQUIRE_TRUE( rig._client.hasSnapshot() );
    vector<int32> listConnection;
    rig._cluster.getServer().collectConnected( listConnection );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listConnection.size() );
    SW_EXPECT_EQUAL( uint32{ 1 }, rig._server.getAckedTick( listConnection.front() ) );

    rig.sendTick( 2, 4, 200 ); // 엔티티 4 만 바뀐다
    const size_t changedBytes = rig._receivedBytes;
    rig.sendTick( 3, 4, 200 ); // 바뀐 것 없음
    const size_t idleBytes = rig._receivedBytes;
    SW_EXPECT_TRUE( changedBytes > idleBytes + 32 );      // 바뀐 엔티티 하나의 상태가 실렸다
    SW_EXPECT_TRUE( changedBytes < idleBytes + 32 + 16 ); // 그 하나뿐이다
    SW_EXPECT_TRUE( fullBytes > idleBytes + 10 * 32 );    // 첫 스냅샷은 기준 없이 열 개 모두
    const NetSnapshot* pLatest = rig._client.getLatest();
    SW_REQUIRE_NOT_NULL( pLatest );
    const NetEntityState* pChanged = pLatest->findEntity( 4 );
    SW_REQUIRE_NOT_NULL( pChanged );
    SW_EXPECT_TRUE( pChanged->_buffer == NetSnapshotRevisionTestInternal::makeBytes( 200, 32 ) );
    SW_EXPECT_EQUAL( size_t{ 10 }, pLatest->_listEntity.size() );
}
