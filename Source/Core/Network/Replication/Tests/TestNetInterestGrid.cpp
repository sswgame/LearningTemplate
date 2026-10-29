#include "pch.h"

#include "Core/Network/Replication/NetInterestGrid.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

/**
 * @brief [NetInterestGridTest] 들어오는 반경 안만 새로 보일 후보이고, 나가는 반경 밖으로 가야 빠진다(히스테리시스). 높이(Y)는 거리에 들지 않는다
 */
SW_TEST_CASE( NetInterestGridTest, EnterAndLeaveRadiiFormAHysteresisBand )
{
    NetInterestGrid grid;
    string          error;
    SW_REQUIRE_TRUE( grid.initialize( 8.0f, 10.0f, 15.0f, error ) );
    grid.setPoint( 1, float3{ 5.0f, 100.0f, 0.0f } ); // 안쪽(높이는 무시)
    grid.setPoint( 2, float3{ 12.0f, 0.0f, 0.0f } );  // 띠 안 — 새로 보이지는 않지만 보이던 것은 남는다
    grid.setPoint( 3, float3{ 0.0f, 0.0f, -20.0f } ); // 바깥

    vector<SlotHandle> listKey;
    const float3       center{ 0.0f, 0.0f, 0.0f };
    grid.queryEntering( center, listKey );
    SW_REQUIRE_EQUAL( size_t( 1 ), listKey.size() );
    SW_EXPECT_EQUAL( uint32( 1 ), NetInterestGrid::getEntityID( listKey[0] ) );
    SW_EXPECT_FALSE( grid.isOutsideLeaveRadius( center, float3{ 12.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( grid.isOutsideLeaveRadius( center, float3{ 0.0f, 0.0f, -20.0f } ) );

    grid.removePoint( 1 );
    grid.queryEntering( center, listKey );
    SW_EXPECT_TRUE( listKey.empty() );
}

/**
 * @brief [NetInterestGridTest] 나가는 반경이 들어오는 반경보다 작은 설정은 맞춰 받지 않고 거절하며, 앞의 설정을 그대로 둔다
 */
SW_TEST_CASE( NetInterestGridTest, ALeaveRadiusBelowTheEnterRadiusIsRefused )
{
    NetInterestGrid grid;
    string          error;
    SW_REQUIRE_TRUE( grid.initialize( 8.0f, 10.0f, 15.0f, error ) );
    SW_EXPECT_FALSE( grid.initialize( 8.0f, 10.0f, 5.0f, error ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_FALSE( grid.initialize( 0.0f, 10.0f, 15.0f, error ) );
    SW_EXPECT_EQUAL( 15.0f, grid.getLeaveRadius() );
}
