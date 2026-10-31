#include "pch.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Task/TaskManager.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Scene/StreamingCellGrid.h"

#include "GameFramework/Kits/Feature/Network/NetMMO/MMOReplicator.h"

// MMO 관심 영역의 관찰자별 계산을 작업 스레드에 나눠도 결과가 같은가 — 결정적인 루프백 망에서 한 스레드 · 여러 스레드로 같은 판을 돌려
// 관찰자가 받은 바이트를 비교한다(관찰자마다 독립이라 보낸 내용이 바이트까지 같아야 한다).

using namespace sw;

namespace
{
    struct MMOReplicatorParallelTestInternal
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

        /** @brief MMO 판 — 관찰자마다 받은 메시지 바이트의 해시 + 보낸 갱신 수(마지막 칸)입니다. */
        static vector<uint64> runMMO( TaskManager* pTaskManager )
        {
            NetEndpointConfig settings;
            settings._sendInterval = 1.0 / 20.0;
            test::LoopbackCluster cluster( 21u );
            connectEndpointCluster( cluster, settings );

            MMOReplicator       server;
            MMOReplicatorConfig mmoSettings;
            mmoSettings._enterRadius       = 50.0f;
            mmoSettings._leaveRadius       = 60.0f;
            mmoSettings._updateBudgetBytes = 120;
            mmoSettings._maxEnterPerTick   = 6;
            StreamingCellGridDef cellGridDef;
            cellGridDef._cellSize      = 32.0f;
            cellGridDef._loadingRadius = 100.0f;
            StreamingCellGrid cellGrid;
            SW_EXPECT_TRUE( StreamingCellGrid::tryCreate( cellGridDef, cellGrid ) );
            SW_EXPECT_TRUE( server.initialize( &cluster.getServer(), mmoSettings, cellGrid ) );
            server.setTaskManager( pTaskManager, 1 );
            const auto placeEntities = [&server]( uint32 tick )
            {
                for ( uint32 index = 0; index < 600; ++index )
                {
                    server.setEntity( MMOEntity{
                        vector<uint8>{ static_cast<uint8>( ( index + tick ) % 7u ) },
                        float3{ static_cast<float32>( index % 30 ) * 15.0f, 0.0f, static_cast<float32>( index / 30 ) * 15.0f },
                        index, 1, 1.0f + static_cast<float32>( index % 3 )
                    } );
                }
            };
            placeEntities( 0 );
            // 관찰자마다 자기 엔티티(1000 + n)가 맵 위 다른 자리에서 천천히 움직인다.
            for ( int32 index = 0; index < kClientCount; ++index )
            {
                server.setObserver( index, 1000u + static_cast<uint32>( index ) );
            }

            vector<uint64> listHash( static_cast<size_t>( kClientCount + 1 ), HashUtil::kFnvOffset64 );
            vector<uint8>  buffer;
            for ( uint32 tick = 0; tick < 60; ++tick )
            {
                if ( tick % 4 == 0 )
                    placeEntities( tick );
                for ( int32 index = 0; index < kClientCount; ++index )
                {
                    server.setEntity( MMOEntity{
                        vector<uint8>{},
                        float3{ static_cast<float32>( index % 6 ) * 70.0f + static_cast<float32>( tick ), 0.0f, static_cast<float32>( index / 6 ) * 70.0f },
                        1000u + static_cast<uint32>( index ), 0, 2.0f
                    } );
                }
                server.update( 1.0f / 20.0f );
                cluster.step( 1.0 / 20.0 );
                int32          connectionID = -1;
                NetChannelType channel      = NetChannelType::Unreliable;
                for ( int32 index = 0; index < kClientCount; ++index )
                {
                    while ( cluster.getClient( index ).receiveMessage( connectionID, channel, buffer ) )
                    {
                        listHash[static_cast<size_t>( index )] = mixBytes( listHash[static_cast<size_t>( index )], buffer );
                    }
                }
            }
            listHash[kClientCount] = server.getSentUpdateCount();
            return listHash;
        }
    };
} // namespace

SW_TEST_CASE( MMOReplicatorParallelTest, MMOReplicatorSendsTheSameViewsOnWorkerThreads )
{
    const vector<uint64> listSerial   = MMOReplicatorParallelTestInternal::runMMO( nullptr );
    const vector<uint64> listParallel = MMOReplicatorParallelTestInternal::runMMO( &engine::getSubsystem<TaskManager>() ); // 엔진의 작업 스레드
    SW_REQUIRE_EQUAL( listSerial.size(), listParallel.size() );
    for ( size_t index = 0; index < listSerial.size(); ++index )
    {
        SW_EXPECT_TRUE( listSerial[index] == listParallel[index] );
    }
    SW_EXPECT_TRUE( listSerial.back() > 100u ); // 갱신이 실제로 오갔다
}
