#include "pch.h"

#include "Core/Container/deque.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/InterestReplicationPolicy.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationServer.h"

// 관심 영역 정책이 관찰자의 보이는 목록만 돌아도(collectRelevantEntities 재정의) 모든 엔티티에 묻는 기본 구현과 같은 스냅샷을 내는가 —
// 같은 판을 두 정책으로 돌려 클라이언트가 받은 바이트를 비교한다(네트 바이트가 같아야 한다).

using namespace sw;

namespace
{
    struct InterestReplicationPolicyTestInternal
    {
        static constexpr int32  kClientCount = 6;
        static constexpr uint32 kEntityCount = 90;
        static constexpr uint32 kPositioned  = 70; ///< 이 번호 아래만 자리를 알린다 — 위는 판 상태처럼 모두에게 간다
        static constexpr uint32 kTickCount   = 50;

        /**
         * @brief 기본 구현(모든 엔티티에 `evaluateEntity`)으로 답하는 정책입니다. 판단은 감싼 관심 영역 정책에 그대로 묻는다 — 비교의 기준이다.
         * @details `collectRelevantEntities` 를 재정의하지 않으므로 `IReplicationPolicy` 의 길(엔티티 × 연결)로 돈다.
         */
        class AllEntityReference final : public IReplicationPolicy
        {
        public:
            explicit AllEntityReference( const InterestReplicationPolicy& policy )
                : _policy{ policy }
            {
            }
            bool    isRelevant( int32 connectionID, const NetEntityState& entity ) const override { return _policy.isRelevant( connectionID, entity ); }
            float32 computePriority( int32 connectionID, const NetEntityState& entity ) const override { return _policy.computePriority( connectionID, entity ); }
            bool    evaluateEntity( int32 connectionID, const NetEntityState& entity, float32& outPriority ) const override
            {
                return _policy.evaluateEntity( connectionID, entity, outPriority );
            }

        private:
            const InterestReplicationPolicy& _policy;
        };

        static uint64 mixBytes( uint64 hash, const vector<uint8>& buffer )
        {
            for ( const uint8 byte : buffer )
            {
                hash = HashUtil::fnvStep64( hash, byte );
            }
            return HashUtil::fnvStep64( hash, 0xFFu );
        }

        /** @brief 엔티티 @p entityID 의 틱 @p tick 자리입니다 — 줄 위를 오가며 관찰자 반경을 드나든다. */
        static float3 makePosition( uint32 entityID, uint32 tick )
        {
            const float32 phase = static_cast<float32>( ( entityID * 13u + tick * ( 1u + entityID % 3u ) ) % 200u );
            return float3{ phase - 100.0f, 0.0f, static_cast<float32>( entityID % 7u ) * 4.0f };
        }

        /**
         * @brief 서버 + 클라이언트 kClientCount 를 세우고 판을 돌려 클라이언트마다 받은 바이트의 해시를 돌려줍니다.
         * @param bReference true 면 서버가 기본 구현 정책(`AllEntityReference`)으로, false 면 관심 영역 정책을 그대로 쓴다. 정책 상태는 두 판이 같다.
         */
        static vector<uint64> runReplication( bool bReference )
        {
            NetEndpointConfig settings;
            settings._sendInterval = 1.0 / 60.0;
            settings._saltSeed     = 77;
            test::LoopbackCluster cluster( 31u );
            (void)cluster.addEndpoint( 4300, settings ); // 빈 클러스터라 포트가 겹치지 않는다
            for ( int32 index = 0; index < kClientCount; ++index )
            {
                settings._saltSeed = 2000u + static_cast<uint64>( index );
                (void)cluster.addEndpoint( static_cast<uint16>( 4400 + index ), settings );
            }
            (void)cluster.connectClients( 30 ); // 연결 실패는 해시가 0 이 되어 드러난다

            InterestReplicationPolicy interest;
            SW_EXPECT_TRUE( interest.initialize( 16.0f, 40.0f, 48.0f ) );
            AllEntityReference      reference( interest );
            ReplicationServer       server;
            ReplicationServerConfig serverSettings;
            serverSettings._snapshotBudgetBytes = 300; // 예산이 모자라 우선도가 결과를 바꾼다
            IReplicationPolicy* pPolicy         = bReference ? static_cast<IReplicationPolicy*>( &reference ) : static_cast<IReplicationPolicy*>( &interest );
            SW_EXPECT_TRUE( server.initialize( &cluster.getServer(), serverSettings, pPolicy ) );
            deque<ReplicationClient> listReplication;
            for ( int32 index = 0; index < cluster.getClientCount(); ++index )
            {
                listReplication.emplace_back();
                SW_EXPECT_TRUE( listReplication.back().initialize( &cluster.getClient( index ), ReplicationClientConfig{} ) );
            }
            vector<int32> listConnection;
            cluster.getServer().collectConnected( listConnection );

            vector<uint64> listHash( kClientCount, HashUtil::kFNVOffset64 );
            vector<uint8>  buffer;
            for ( uint32 tick = 0; tick < kTickCount; ++tick )
            {
                server.beginTick( tick );
                for ( uint32 entityID = 0; entityID < kEntityCount; ++entityID )
                {
                    BitWriter writer;
                    writer.writeVarUint( ( entityID * 31u + tick * ( entityID % 5u ) ) % 1000u );
                    server.setEntity( entityID, entityID % 4u, writer.getBytes() );
                    // 자리를 알린 엔티티 몇은 중간에 자리를 지운다 — 보이는 목록에 남은 채 자리 없는 엔티티가 되는 길까지 같아야 한다.
                    const bool bDropped = entityID % 11u == 0u && tick >= 20u;
                    if ( entityID < kPositioned && bDropped == false )
                        interest.setEntityPosition( entityID, makePosition( entityID, tick ) );
                    else if ( entityID < kPositioned && tick == 20u )
                        interest.removeEntity( entityID );
                }
                // 관찰자는 연결마다 다른 자리 — 하나(마지막 연결)는 관찰자를 두지 않는다(자리 없는 엔티티만 간다).
                for ( size_t index = 0; index + 1 < listConnection.size(); ++index )
                {
                    interest.setObserver( listConnection[index], float3{ static_cast<float32>( index ) * 30.0f - 75.0f + static_cast<float32>( tick ), 0.0f, 8.0f } );
                }
                server.endTick();
                interest.updateRelevancy();
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

SW_TEST_CASE( InterestReplicationPolicyTest, VisibleListWalkSendsTheSameSnapshotBytes )
{
    const vector<uint64> listReference = InterestReplicationPolicyTestInternal::runReplication( true );
    const vector<uint64> listVisible   = InterestReplicationPolicyTestInternal::runReplication( false );
    SW_REQUIRE_EQUAL( listReference.size(), listVisible.size() );
    for ( size_t index = 0; index < listReference.size(); ++index )
    {
        SW_EXPECT_TRUE( listReference[index] != 0 );
        SW_EXPECT_TRUE( listReference[index] == listVisible[index] );
    }
}

SW_TEST_CASE( InterestReplicationPolicyTest, CollectMatchesEvaluateForEveryEntity )
{
    // 같은 입력에서 재정의한 모으기와 기본 구현(모든 엔티티에 evaluateEntity)이 차례 · 우선도까지 같다.
    InterestReplicationPolicy policy;
    SW_REQUIRE_TRUE( policy.initialize( 16.0f, 40.0f, 48.0f ) );
    vector<NetEntityState> listWorld;
    for ( uint32 entityID = 0; entityID < 60; ++entityID )
    {
        NetEntityState& entity = listWorld.emplace_back();
        entity._entityID       = entityID * 2u + 1u; // 이빠진 id
        if ( entityID < 45 )
            policy.setEntityPosition( entity._entityID, InterestReplicationPolicyTestInternal::makePosition( entityID, 3u ) );
    }
    policy.setObserver( 0, float3{ -20.0f, 0.0f, 0.0f } );
    policy.setObserver( 2, float3{ 60.0f, 0.0f, 10.0f } );
    policy.updateRelevancy();
    policy.removeEntity( listWorld[4]._entityID ); // 보이는 목록에 남은 채 자리를 지운 엔티티
    policy.prepareSnapshots( listWorld );
    vector<NetRelevantEntity> listVisible;
    vector<NetRelevantEntity> listAll;
    for ( int32 connectionID = 0; connectionID < 4; ++connectionID ) // 1 · 3 은 관찰자가 없다
    {
        policy.collectRelevantEntities( connectionID, listWorld, listVisible );
        policy.IReplicationPolicy::collectRelevantEntities( connectionID, listWorld, listAll );
        SW_REQUIRE_EQUAL( listAll.size(), listVisible.size() );
        for ( size_t index = 0; index < listAll.size(); ++index )
        {
            SW_EXPECT_EQUAL( listAll[index]._worldIndex, listVisible[index]._worldIndex );
            SW_EXPECT_TRUE( listAll[index]._priority == listVisible[index]._priority );
        }
    }
}
