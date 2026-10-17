#include "pch.h"

#include "Engine/Scene/WorldCellGrid.h"

#include "TestFramework/TestFramework.h"

/**
 * @brief [WorldCellGridTest] 셀 크기가 없는 설정으로는 격자를 만들지 않는다 — 기본값으로 메우지 않는다
 */
SW_TEST_CASE( WorldCellGridTest, MissingCellSizeIsRejected )
{
    SW_TEST_SUPPRESS_LOGS();
    sw::WorldCellGrid grid;
    SW_EXPECT_FALSE( sw::WorldCellGrid::tryCreate( sw::WorldCellGridDef{ 0.0f, 100.0f }, grid ) );
    SW_EXPECT_FALSE( sw::WorldCellGrid::tryCreate( sw::WorldCellGridDef{ 64.0f, -1.0f }, grid ) );
    SW_EXPECT_TRUE( sw::WorldCellGrid::tryCreate( sw::WorldCellGridDef{ 64.0f, 0.0f }, grid ) );
}

/**
 * @brief [WorldCellGridTest] 위치는 아래쪽 경계에 속하는 셀로 간다 — 음수 좌표도 내림이다
 * @details 0 쪽으로 자르면(정수 변환) -0.5 와 +0.5 가 같은 셀 0 이 되어 원점 둘레 셀 하나가 두 배 크기가 된다.
 */
SW_TEST_CASE( WorldCellGridTest, ComputeCellFloorsNegativeCoordinates )
{
    sw::WorldCellGrid grid;
    SW_ASSERT_TRUE( sw::WorldCellGrid::tryCreate( sw::WorldCellGridDef{ 10.0f, 0.0f }, grid ) );
    SW_EXPECT_TRUE( grid.computeCell( sw::float3( 0.5f, 99.0f, 0.5f ) ) == ( sw::WorldCellCoord{ 0, 0 } ) );
    SW_EXPECT_TRUE( grid.computeCell( sw::float3( -0.5f, 0.0f, -0.5f ) ) == ( sw::WorldCellCoord{ -1, -1 } ) );
    SW_EXPECT_TRUE( grid.computeCell( sw::float3( 10.0f, 0.0f, 25.0f ) ) == ( sw::WorldCellCoord{ 1, 2 } ) );
}

/**
 * @brief [WorldCellGridTest] 로딩 반경 안에 닿는 셀만 고른다 — 사각형 모서리 셀은 원 밖이면 빠진다
 */
SW_TEST_CASE( WorldCellGridTest, CellsInRangeFollowLoadingRadius )
{
    sw::WorldCellGrid grid;
    SW_ASSERT_TRUE( sw::WorldCellGrid::tryCreate( sw::WorldCellGridDef{ 10.0f, 12.0f }, grid ) );
    sw::vector<sw::WorldCellCoord> listCell;
    // 원점이 셀 (0,0) 한가운데(5,5). 반경 12 는 앞뒤좌우 한 칸(거리 5)에 닿고, 대각선 칸(거리 √50 ≈ 7.07)에도 닿으며, 두 칸 너머(거리 15)에는 닿지 않는다.
    grid.computeCellsInRange( sw::float3( 5.0f, 0.0f, 5.0f ), listCell );
    SW_EXPECT_EQUAL( size_t( 9 ), listCell.size() );
    SW_EXPECT_TRUE( listCell.front() == ( sw::WorldCellCoord{ -1, -1 } ) );
    SW_EXPECT_TRUE( listCell.back() == ( sw::WorldCellCoord{ 1, 1 } ) );

    // 반경 0 이면 원점이 든 셀 하나다.
    SW_ASSERT_TRUE( sw::WorldCellGrid::tryCreate( sw::WorldCellGridDef{ 10.0f, 0.0f }, grid ) );
    grid.computeCellsInRange( sw::float3( 5.0f, 0.0f, 5.0f ), listCell );
    SW_EXPECT_EQUAL( size_t( 1 ), listCell.size() );
}

/**
 * @brief [WorldCellGridTest] 셀을 넘는 참조는 위반이다 — 같은 셀이거나 항상 올라 있는 대상만 허용한다
 * @details 대상이 다른 셀에 있으면 그 셀이 내려가 있을 때 참조가 조용히 null 이 된다. 쿠킹이 이 목록으로 멈춘다.
 */
SW_TEST_CASE( WorldCellGridTest, CrossCellReferencesAreReported )
{
    const sw::vector<sw::WorldCellObjectRecord> listObject{
        {1, { 0, 0 }, false},
        {2, { 0, 0 }, false},
        {3, { 1, 0 }, false},
        {4, { 5, 5 },  true},
    };
    const sw::vector<sw::WorldCellReference> listReference{
        {1, 2}, // 같은 셀
        {1, 3}, // 다른 셀 — 위반
        {3, 4}, // 항상 올라 있는 대상
        {4, 1}, // 항상 올라 있는 쪽이 셀 오브젝트를 가리킨다 — 위반(그 셀이 내려가면 끊긴다)
        {2, 9}, // 없는 대상 — 위반
    };
    sw::vector<sw::WorldCellReference> listViolation;
    sw::WorldCellGrid::findCrossCellReferences( listObject, listReference, listViolation );
    SW_ASSERT_EQUAL( size_t( 3 ), listViolation.size() );
    SW_EXPECT_EQUAL( uint64( 3 ), listViolation[0]._toObjectID );
    SW_EXPECT_EQUAL( uint64( 1 ), listViolation[1]._toObjectID );
    SW_EXPECT_EQUAL( uint64( 9 ), listViolation[2]._toObjectID );
}
