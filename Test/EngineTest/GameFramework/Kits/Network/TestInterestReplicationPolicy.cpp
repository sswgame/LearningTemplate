#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/InterestReplicationPolicy.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    NetEntityState makeInterestTestEntity( uint32 entityID )
    {
        NetEntityState entity;
        entity._entityID = entityID;
        return entity;
    }
} // namespace

/**
 * @brief [InterestReplicationPolicyTest] 관찰자 근처(들어오는 반경)의 엔티티만 보이고, 띠(들어오는 ~ 나가는 반경)에서는 보이던 것만 남는다. 자리 없는 엔티티는 모두에게 간다
 */
SW_TEST_CASE( InterestReplicationPolicyTest, ObserversSeeNearbyEntitiesWithHysteresis )
{
    InterestReplicationPolicy policy;
    SW_ASSERT_TRUE( policy.initialize( 8.0f, 10.0f, 15.0f ) );
    policy.setEntityPosition( 1, float3{ 5.0f, 0.0f, 0.0f } );
    policy.setEntityPosition( 2, float3{ 30.0f, 0.0f, 0.0f } );
    policy.setObserver( 0, float3{ 0.0f, 0.0f, 0.0f } );
    policy.updateRelevancy();
    SW_EXPECT_TRUE( policy.isRelevant( 0, makeInterestTestEntity( 1 ) ) );
    SW_EXPECT_FALSE( policy.isRelevant( 0, makeInterestTestEntity( 2 ) ) );
    SW_EXPECT_TRUE( policy.isRelevant( 0, makeInterestTestEntity( 99 ) ) ); // 자리를 알리지 않은 엔티티(판 상태)
    SW_EXPECT_FALSE( policy.isRelevant( 1, makeInterestTestEntity( 1 ) ) ); // 관찰자가 없는 연결

    policy.setEntityPosition( 1, float3{ 12.0f, 0.0f, 0.0f } ); // 띠 안 — 보이던 것은 남는다
    policy.setEntityPosition( 2, float3{ 12.0f, 0.0f, 1.0f } ); // 띠 안 — 안 보이던 것은 들어오지 않는다
    policy.updateRelevancy();
    SW_EXPECT_TRUE( policy.isRelevant( 0, makeInterestTestEntity( 1 ) ) );
    SW_EXPECT_FALSE( policy.isRelevant( 0, makeInterestTestEntity( 2 ) ) );

    policy.setEntityPosition( 1, float3{ 20.0f, 0.0f, 0.0f } ); // 나가는 반경 밖
    policy.updateRelevancy();
    SW_EXPECT_FALSE( policy.isRelevant( 0, makeInterestTestEntity( 1 ) ) );
    SW_EXPECT_EQUAL( 0, policy.getVisibleCount( 0 ) );

    {
        test::ScopedLogSuppressor suppressor; // 거절 오류
        SW_EXPECT_FALSE( policy.initialize( 8.0f, 10.0f, 5.0f ) );
    }
}
