#include "pch.h"

#include "Core/Container/deque.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Task/TaskManager.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationServer.h"

// 권위 서버의 연결별 스냅샷을 작업 스레드에 나눠도 결과가 같은가 — 결정적인 루프백 망에서 한 스레드 · 여러 스레드로 같은 판을 돌려
// 클라이언트가 받은 바이트를 비교한다(연결마다 독립이라 보낸 내용이 바이트까지 같아야 한다).

using namespace sw;

namespace
{
    struct ReplicationParallelTestInternal
    {
        static constexpr int32 kClientCount = 24;

        static uint64 mixBytes( uint64 hash, const vector<uint8>& buffer )
        {
            for ( const uint8 byte : buffer )
            {
                hash = HashUtil::fnvStep64( hash, byte );
            }
            return HashUtil::fnvStep64( hash, 0xFFu );
        }

        /** @brief 연결마다 다른 관련 · 우선도 — 나눠 돌 때 섞이면 드러나게. 상태를 바꾸지 않는다(여러 스레드가 동시에 부른다). */
        class StripedPolicy final : public IReplicationPolicy
        {
        public:
            bool isRelevant( int32 connectionID, const NetEntityState& entity ) const override
            {
                return ( entity._entityID + static_cast<uint32>( connectionID ) ) % 3 != 0;
            }
            float32 computePriority( int32 connectionID, const NetEntityState& entity ) const override
            {
                return static_cast<float32>( ( entity._entityID * 7u + static_cast<uint32>( connectionID ) * 13u ) % 31u );
            }
        };

        /** @brief 서버(포트 4000) + 클라이언트 kClientCount(5000 부터)를 세우고 30 프레임 돌려 연결합니다. 소금을 고정해 핸드셰이크 값까지 같게 한다(한 스레드라 망은 결정적). */
        static void connectEndpointCluster( test::LoopbackCluster& cluster, const NetEndpointConfig& settings )
        {
            NetEndpointConfig fixed = settings;
            fixed._saltSeed         = 99;
            fixed._maxConnections   = kClientCount + 8;
            (void)cluster.addEndpoint( 4000, fixed ); // 빈 클러스터라 포트가 겹치지 않는다
            for ( int32 index = 0; index < kClientCount; ++index )
            {
                fixed._saltSeed = 1000u + static_cast<uint64>( index );
                (void)cluster.addEndpoint( static_cast<uint16>( 5000 + index ), fixed );
            }
            (void)cluster.connectClients( 30 ); // 같은 판을 두 번 돌려 바이트를 비교하는 테스트 — 연결 실패는 해시가 달라 드러난다
        }

        /** @brief 권위 서버 판 — 클라이언트마다 받은 메시지 바이트의 해시입니다. */
        static vector<uint64> runReplication( TaskManager* pTaskManager )
        {
            NetEndpointConfig settings;
            settings._sendInterval = 1.0 / 60.0;
            test::LoopbackCluster cluster( 21u );
            connectEndpointCluster( cluster, settings );

            StripedPolicy           policy;
            ReplicationServer       server;
            ReplicationServerConfig serverSettings;
            serverSettings._snapshotBudgetBytes = 400; // 예산이 모자라 우선도 순서가 결과를 바꾼다
            SW_EXPECT_TRUE( server.initialize( &cluster.getServer(), serverSettings, &policy ) );
            server.setTaskManager( pTaskManager, 1 );
            deque<ReplicationClient> listReplication;
            for ( int32 index = 0; index < cluster.getClientCount(); ++index )
            {
                listReplication.emplace_back();
                SW_EXPECT_TRUE( listReplication.back().initialize( &cluster.getClient( index ), ReplicationClientConfig{} ) );
            }

            vector<uint64> listHash( kClientCount, HashUtil::kFnvOffset64 );
            vector<uint8>  buffer;
            for ( uint32 tick = 0; tick < 60; ++tick )
            {
                server.beginTick( tick );
                for ( uint32 entityID = 0; entityID < 120; ++entityID )
                {
                    BitWriter writer;
                    writer.writeVarUint( ( entityID * 31u + tick * ( entityID % 5u ) ) % 1000u );
                    server.setEntity( entityID, entityID % 4u, writer.getBytes() );
                }
                server.endTick();
                server.sendSnapshots();
                cluster.step( 1.0 / 60.0 );
                int32          connectionID = -1;
                NetChannelType channel      = NetChannelType::Unreliable;
                while ( cluster.getServer().receiveMessage( connectionID, channel, buffer ) )
                {
                    (void)server.handleMessage( connectionID, buffer ); // 클라이언트의 확인 — 판의 결과는 해시가 본다
                }
                for ( int32 index = 0; index < kClientCount; ++index )
                {
                    while ( cluster.getClient( index ).receiveMessage( connectionID, channel, buffer ) )
                    {
                        listHash[static_cast<size_t>( index )] = mixBytes( listHash[static_cast<size_t>( index )], buffer );
                        (void)listReplication[static_cast<size_t>( index )].handleMessage( 0, buffer ); // 받은 스냅샷 — 판의 결과는 해시가 본다
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
    };
} // namespace

SW_TEST_CASE( ReplicationParallelTest, ReplicationServerSendsTheSameSnapshotsOnWorkerThreads )
{
    const vector<uint64> listSerial   = ReplicationParallelTestInternal::runReplication( nullptr );
    const vector<uint64> listParallel = ReplicationParallelTestInternal::runReplication( &engine::getSubsystem<TaskManager>() ); // 엔진의 작업 스레드
    SW_REQUIRE_EQUAL( listSerial.size(), listParallel.size() );
    for ( size_t index = 0; index < listSerial.size(); ++index )
    {
        SW_EXPECT_TRUE( listSerial[index] != 0 );
        SW_EXPECT_TRUE( listSerial[index] == listParallel[index] );
    }
}
