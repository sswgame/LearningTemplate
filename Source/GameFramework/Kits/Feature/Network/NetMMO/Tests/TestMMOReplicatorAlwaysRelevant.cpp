#include "pch.h"

#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Scene/StreamingCellGrid.h"

#include "GameFramework/Kits/Feature/Network/NetMMO/MMOReplicator.h"

// 항상 보임 후보(`MMOEntity::_bAlwaysRelevant`)는 등록 · 해제 때 모은 집합에서만 정책에 묻는가 — 반경 밖 후보가 보이고,
// 관찰자별 정책이 거르고, 표시를 내리거나 지우면 사라지며, 정책 호출 수가 월드 엔티티 수가 아니라 관찰자 × 후보 수에 묶이는지 본다.

using namespace sw;

namespace
{
    struct MMOReplicatorAlwaysRelevantTestInternal
    {
        static constexpr int32  kClientCount = 2;
        static constexpr uint32 kGlobalID    = 9000; ///< 모든 관찰자에게 보이는 후보
        static constexpr uint32 kPartyID     = 9001; ///< 연결 0 에게만 보이는 후보(파티원)
        static constexpr uint32 kFillerCount = 5000; ///< 반경 밖의 일반 엔티티. 옛 구현은 관찰자마다 이것까지 정책에 물었다

        /** @brief 파티원은 연결 0 에게만 보이게 하고 호출 수를 셉니다. 작업 스레드를 주지 않으므로 한 스레드에서만 불린다. */
        class CountingPartyPolicy : public IInterestPolicy
        {
        public:
            mutable uint64 _callCount{ 0 };

            bool isAlwaysRelevant( int32 connectionID, const MMOEntity& entity ) const override
            {
                ++_callCount;
                return entity._entityID != kPartyID || connectionID == 0;
            }
        };

        static MMOEntity makeEntity( uint32 entityID, const float3& position, bool bAlwaysRelevant )
        {
            MMOEntity entity;
            entity._position        = position;
            entity._entityID        = entityID;
            entity._typeID          = 1;
            entity._bAlwaysRelevant = bAlwaysRelevant ? SW_TRUE : SW_FALSE;
            return entity;
        }

        /** @brief 복제기를 @p tickCount 틱 돌리고 클라이언트가 받은 메시지를 비웁니다. */
        static void runTicks( MMOReplicator& server, test::LoopbackCluster& cluster, int32 tickCount )
        {
            vector<uint8>  buffer;
            int32          connectionID = -1;
            NetChannelType channel      = NetChannelType::Unreliable;
            for ( int32 tick = 0; tick < tickCount; ++tick )
            {
                server.update( 1.0f / 20.0f );
                cluster.step( 1.0 / 20.0 );
                for ( int32 index = 0; index < kClientCount; ++index )
                {
                    while ( cluster.getClient( index ).receiveMessage( connectionID, channel, buffer ) )
                    {
                    }
                }
            }
        }
    };
} // namespace

SW_TEST_CASE( MMOReplicatorAlwaysRelevantTest, AlwaysRelevantCandidatesAreAskedWithoutScanningTheWorld )
{
    using Internal = MMOReplicatorAlwaysRelevantTestInternal;

    NetEndpointConfig settings;
    settings._sendInterval   = 1.0 / 20.0;
    settings._saltSeed       = 7;
    settings._maxConnections = Internal::kClientCount + 2;
    test::LoopbackCluster cluster( 5u );
    SW_REQUIRE_TRUE( cluster.addEndpoint( 4100, settings ).has_value() );
    for ( int32 index = 0; index < Internal::kClientCount; ++index )
    {
        SW_REQUIRE_TRUE( cluster.addEndpoint( static_cast<uint16>( 5100 + index ), settings ).has_value() );
    }
    SW_REQUIRE_TRUE( cluster.connectClients( 30 ) );

    MMOReplicatorConfig mmoSettings;
    mmoSettings._enterRadius = 50.0f;
    mmoSettings._leaveRadius = 60.0f;
    StreamingCellGridDef cellGridDef;
    cellGridDef._cellSize      = 32.0f;
    cellGridDef._loadingRadius = 100.0f;
    StreamingCellGrid cellGrid;
    SW_REQUIRE_TRUE( StreamingCellGrid::tryCreate( cellGridDef, cellGrid ) );
    Internal::CountingPartyPolicy policy;
    MMOReplicator                 server;
    SW_REQUIRE_TRUE( server.initialize( &cluster.getServer(), mmoSettings, cellGrid, &policy ) );

    // 관찰자 둘은 서로 멀고, 후보 둘과 일반 엔티티 5000 은 두 관찰자의 반경 밖에 있다.
    server.setEntity( Internal::makeEntity( 1000, float3{ 0.0f, 0.0f, 0.0f }, false ) );
    server.setEntity( Internal::makeEntity( 1001, float3{ 3000.0f, 0.0f, 0.0f }, false ) );
    server.setEntity( Internal::makeEntity( Internal::kGlobalID, float3{ 9000.0f, 0.0f, 9000.0f }, true ) );
    server.setEntity( Internal::makeEntity( Internal::kPartyID, float3{ -9000.0f, 0.0f, 0.0f }, true ) );
    for ( uint32 index = 0; index < Internal::kFillerCount; ++index )
    {
        server.setEntity( Internal::makeEntity( index, float3{ 20000.0f + static_cast<float32>( index % 100 ) * 10.0f, 0.0f, static_cast<float32>( index / 100 ) * 10.0f }, false ) );
    }
    server.setObserver( 0, 1000 );
    server.setObserver( 1, 1001 );

    constexpr int32 kTickCount = 10;
    Internal::runTicks( server, cluster, kTickCount );
    SW_EXPECT_EQUAL( 3, server.getVisibleCount( 0 ) ); // 자기 자신 + 전역 후보 + 파티원
    SW_EXPECT_EQUAL( 2, server.getVisibleCount( 1 ) ); // 자기 자신 + 전역 후보(파티원은 정책이 거른다)
    // 틱마다 관찰자 × 후보(새로 보임) + 관찰자 × 보이는 후보(보이지 않게 됨)만 묻는다. 월드를 훑으면 틱당 5000 을 넘는다.
    SW_EXPECT_TRUE( policy._callCount <= static_cast<uint64>( kTickCount * Internal::kClientCount * 2 * 2 ) );

    // 표시를 내리면 집합에서 빠지고 반경 밖이라 보이지 않게 된다. 지운 후보도 집합에서 빠진다.
    server.setEntity( Internal::makeEntity( Internal::kGlobalID, float3{ 9000.0f, 0.0f, 9000.0f }, false ) );
    server.removeEntity( Internal::kPartyID );
    Internal::runTicks( server, cluster, kTickCount );
    SW_EXPECT_EQUAL( 1, server.getVisibleCount( 0 ) );
    SW_EXPECT_EQUAL( 1, server.getVisibleCount( 1 ) );

    // 후보가 없으면 정책을 부르지 않는다.
    policy._callCount = 0;
    Internal::runTicks( server, cluster, kTickCount );
    SW_EXPECT_EQUAL( static_cast<uint64>( 0 ), policy._callCount );
}
