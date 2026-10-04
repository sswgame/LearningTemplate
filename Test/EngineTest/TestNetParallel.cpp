#include "pch.h"

#include "Core/Container/deque.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/NetHost.h"
#include "Core/Network/NetTransport.h"
#include "Core/Task/TaskManager.h"

#include "GameFramework/Kits/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Kits/Network/NetClientServer/ReplicationServer.h"
#include "GameFramework/Kits/Network/NetMmo/MmoReplicator.h"

#include "TestFramework/TestFramework.h"

// 서버 키트의 연결별 일을 작업 스레드에 나눠도 결과가 같은가 — 권위 서버 스냅샷 · MMO 관심 영역. 같은 씨앗의 루프백 망에서 한 스레드 · 여러 스레드로
// 같은 판을 돌려 클라이언트가 받은 바이트를 비교한다(연결마다 독립이라 보낸 내용이 바이트까지 같아야 한다).

using namespace sw;

namespace
{
    constexpr int32 kClientCount = 24;

    uint64 mixBytes( uint64 hash, const vector<uint8>& buffer )
    {
        for ( const uint8 byte : buffer )
            hash = ( hash ^ byte ) * 1099511628211ull;
        return ( hash ^ 0xFFu ) * 1099511628211ull;
    }

    /** @brief 연결마다 다른 관련 · 우선도 — 나눠 돌 때 섞이면 드러나게. 상태를 바꾸지 않는다(여러 스레드가 동시에 부른다). */
    class StripedPolicy final : public IReplicationPolicy
    {
    public:
        bool isRelevant( int32 connectionId, const NetEntityState& entity ) const override
        {
            return ( entity._entityId + static_cast<uint32>( connectionId ) ) % 3 != 0;
        }
        float32 computePriority( int32 connectionId, const NetEntityState& entity ) const override
        {
            return static_cast<float32>( ( entity._entityId * 7u + static_cast<uint32>( connectionId ) * 13u ) % 31u );
        }
    };

    /** @brief 서버 하나 + 클라이언트 여럿을 한 스레드에서 같은 순서로 돌린다(망은 결정적). */
    struct HostCluster
    {
        LoopbackNetwork _network{ 21u };
        NetHost         _server;
        deque<NetHost>  _listClient;
        float64         _time{ 0.0 };

        explicit HostCluster( const NetHostSettings& settings )
        {
            NetHostSettings fixed = settings;
            fixed._saltSeed       = 99; // 핸드셰이크 값까지 같게
            fixed._maxConnections = kClientCount + 8;
            _server.initialize( _network.createEndpoint( 4000 ), fixed );
            (void)_server.listen();
            for ( int32 index = 0; index < kClientCount; ++index )
            {
                fixed._saltSeed = 1000u + static_cast<uint64>( index );
                _listClient.emplace_back();
                _listClient.back().initialize( _network.createEndpoint( static_cast<uint16>( 5000 + index ) ), fixed );
                (void)_listClient.back().connect( NetAddress::makeLoopback( 4000 ) );
            }
            for ( int32 frame = 0; frame < 30; ++frame )
                step( 1.0 / 60.0 );
        }

        void step( float64 deltaTime )
        {
            _time += deltaTime;
            _server.update( _time );
            for ( NetHost& client : _listClient )
                client.update( _time );
        }
    };

    /** @brief 권위 서버 판 — 클라이언트마다 받은 메시지 바이트의 해시입니다. */
    vector<uint64> runReplication( TaskManager* pTaskManager )
    {
        NetHostSettings settings;
        settings._sendInterval = 1.0 / 60.0;
        HostCluster cluster( settings );

        StripedPolicy             policy;
        ReplicationServer         server;
        ReplicationServerSettings serverSettings;
        serverSettings._snapshotBudgetBytes = 400; // 예산이 모자라 우선도 순서가 결과를 바꾼다
        server.initialize( &cluster._server, serverSettings, &policy );
        server.setTaskManager( pTaskManager, 1 );
        deque<ReplicationClient> listReplication;
        for ( NetHost& client : cluster._listClient )
        {
            listReplication.emplace_back();
            listReplication.back().initialize( &client, ReplicationClientSettings{} );
        }

        vector<uint64> listHash( kClientCount, 1469598103934665603ull );
        vector<uint8>  buffer;
        for ( uint32 tick = 0; tick < 60; ++tick )
        {
            server.beginTick( tick );
            for ( uint32 entityId = 0; entityId < 120; ++entityId )
            {
                BitWriter writer;
                writer.writeVarUint( ( entityId * 31u + tick * ( entityId % 5u ) ) % 1000u );
                server.setEntity( entityId, entityId % 4u, writer.getBytes() );
            }
            server.endTick();
            server.sendSnapshots();
            cluster.step( 1.0 / 60.0 );
            int32          connectionId = -1;
            NetChannelType channel      = NetChannelType::Unreliable;
            while ( cluster._server.receiveMessage( connectionId, channel, buffer ) )
                (void)server.handleMessage( connectionId, buffer );
            for ( int32 index = 0; index < kClientCount; ++index )
            {
                while ( cluster._listClient[static_cast<size_t>( index )].receiveMessage( connectionId, channel, buffer ) )
                {
                    listHash[static_cast<size_t>( index )] = mixBytes( listHash[static_cast<size_t>( index )], buffer );
                    (void)listReplication[static_cast<size_t>( index )].handleMessage( buffer );
                }
                listReplication[static_cast<size_t>( index )].update( 1.0f / 60.0f );
            }
        }
        for ( int32 index = 0; index < kClientCount; ++index )
        {
            if ( listReplication[static_cast<size_t>( index )].hasSnapshot() == false )
                listHash[static_cast<size_t>( index )] = 0; // 받은 것이 없으면 판이 틀렸다
        }
        return listHash;
    }

    /** @brief MMO 판 — 관찰자마다 받은 메시지 바이트의 해시 + 보낸 갱신 수(마지막 칸)입니다. */
    vector<uint64> runMmo( TaskManager* pTaskManager )
    {
        NetHostSettings settings;
        settings._sendInterval = 1.0 / 20.0;
        HostCluster cluster( settings );

        MmoReplicator         server;
        MmoReplicatorSettings mmoSettings;
        mmoSettings._enterRadius       = 50.0f;
        mmoSettings._leaveRadius       = 60.0f;
        mmoSettings._updateBudgetBytes = 120;
        mmoSettings._maxEnterPerTick   = 6;
        server.initialize( &cluster._server, mmoSettings );
        server.setTaskManager( pTaskManager, 1 );
        const auto placeEntities = [&server]( uint32 tick )
        {
            for ( uint32 index = 0; index < 600; ++index )
                server.setEntity( MmoEntity{
                    vector<uint8>{ static_cast<uint8>( ( index + tick ) % 7u ) },
                    float3{ static_cast<float32>( index % 30 ) * 15.0f, 0.0f, static_cast<float32>( index / 30 ) * 15.0f },
                    index, 1, 1.0f + static_cast<float32>( index % 3 )
                } );
        };
        placeEntities( 0 );
        // 관찰자마다 자기 엔티티(1000 + n)가 맵 위 다른 자리에서 천천히 움직인다.
        for ( int32 index = 0; index < kClientCount; ++index )
            server.setObserver( index, 1000u + static_cast<uint32>( index ) );

        vector<uint64> listHash( static_cast<size_t>( kClientCount + 1 ), 1469598103934665603ull );
        vector<uint8>  buffer;
        for ( uint32 tick = 0; tick < 60; ++tick )
        {
            if ( tick % 4 == 0 )
                placeEntities( tick );
            for ( int32 index = 0; index < kClientCount; ++index )
                server.setEntity( MmoEntity{
                    vector<uint8>{},
                    float3{ static_cast<float32>( index % 6 ) * 70.0f + static_cast<float32>( tick ), 0.0f, static_cast<float32>( index / 6 ) * 70.0f },
                    1000u + static_cast<uint32>( index ), 0, 2.0f
                } );
            server.update( 1.0f / 20.0f );
            cluster.step( 1.0 / 20.0 );
            int32          connectionId = -1;
            NetChannelType channel      = NetChannelType::Unreliable;
            for ( int32 index = 0; index < kClientCount; ++index )
            {
                while ( cluster._listClient[static_cast<size_t>( index )].receiveMessage( connectionId, channel, buffer ) )
                    listHash[static_cast<size_t>( index )] = mixBytes( listHash[static_cast<size_t>( index )], buffer );
            }
        }
        listHash[kClientCount] = server.getSentUpdateCount();
        return listHash;
    }
} // namespace

SW_TEST_CASE( NetParallelTest, ReplicationServerSendsTheSameSnapshotsOnWorkerThreads )
{
    TaskManager taskManager;
    SW_ASSERT_TRUE( taskManager.initialize( 3 ) );
    const vector<uint64> listSerial   = runReplication( nullptr );
    const vector<uint64> listParallel = runReplication( &taskManager );
    taskManager.shutdown();
    SW_ASSERT_EQUAL( listSerial.size(), listParallel.size() );
    for ( size_t index = 0; index < listSerial.size(); ++index )
    {
        SW_EXPECT_TRUE( listSerial[index] != 0 );
        SW_EXPECT_TRUE( listSerial[index] == listParallel[index] );
    }
}

SW_TEST_CASE( NetParallelTest, MmoReplicatorSendsTheSameViewsOnWorkerThreads )
{
    TaskManager taskManager;
    SW_ASSERT_TRUE( taskManager.initialize( 3 ) );
    const vector<uint64> listSerial   = runMmo( nullptr );
    const vector<uint64> listParallel = runMmo( &taskManager );
    taskManager.shutdown();
    SW_ASSERT_EQUAL( listSerial.size(), listParallel.size() );
    for ( size_t index = 0; index < listSerial.size(); ++index )
        SW_EXPECT_TRUE( listSerial[index] == listParallel[index] );
    SW_EXPECT_TRUE( listSerial.back() > 100u ); // 갱신이 실제로 오갔다
}
