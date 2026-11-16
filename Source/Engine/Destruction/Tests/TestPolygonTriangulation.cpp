#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Spatial/PolygonTriangulation.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    struct PolygonTriangulationTestUtil
    {
        /** @brief 삼각형들의 넓이 합입니다. 다각형을 빠짐없이 · 겹침 없이 덮으면 다각형 넓이와 같습니다. */
        static float32 sumTriangleArea( const vector<float2>& listPoint, const vector<uint32>& listIndex )
        {
            float32 total{ 0.0f };
            for ( size_t index = 0; index + 2 < listIndex.size(); index += 3 )
            {
                const float2& a = listPoint[listIndex[index]];
                const float2& b = listPoint[listIndex[index + 1]];
                const float2& c = listPoint[listIndex[index + 2]];
                total += MathUtil::abs( ( b._x - a._x ) * ( c._y - a._y ) - ( c._x - a._x ) * ( b._y - a._y ) ) * 0.5f;
            }
            return total;
        }
    };
} // namespace

/** @brief [PolygonTriangulationTest] 넓이의 부호는 방향이다(반시계 +) */
SW_TEST_CASE( PolygonTriangulationTest, SignedAreaFollowsWinding )
{
    const vector<float2> listPoint{
        float2{0.0f, 0.0f},
        float2{2.0f, 0.0f},
        float2{2.0f, 2.0f},
        float2{0.0f, 2.0f}
    };
    const vector<uint32> listCCW{ 0, 1, 2, 3 };
    const vector<uint32> listCW{ 3, 2, 1, 0 };
    SW_EXPECT_NEAR( 4.0f, PolygonTriangulationUtil::computeSignedArea( listPoint, listCCW ), 0.0001f );
    SW_EXPECT_NEAR( -4.0f, PolygonTriangulationUtil::computeSignedArea( listPoint, listCW ), 0.0001f );
}

/** @brief [PolygonTriangulationTest] 짝수-홀수 규칙으로 안팎을 가른다 */
SW_TEST_CASE( PolygonTriangulationTest, PointInLoop )
{
    const vector<float2> listPoint{
        float2{0.0f, 0.0f},
        float2{2.0f, 0.0f},
        float2{2.0f, 2.0f},
        float2{0.0f, 2.0f}
    };
    const vector<uint32> listLoop{ 0, 1, 2, 3 };
    SW_EXPECT_TRUE( PolygonTriangulationUtil::isPointInLoop( listPoint, listLoop, float2{ 1.0f, 1.0f } ) );
    SW_EXPECT_FALSE( PolygonTriangulationUtil::isPointInLoop( listPoint, listLoop, float2{ 3.0f, 1.0f } ) );
}

/** @brief [PolygonTriangulationTest] 오목 다각형을 넓이 그대로 덮는다 — 방향이 어느 쪽이어도 같다 */
SW_TEST_CASE( PolygonTriangulationTest, ConcavePolygonIsCoveredInEitherWinding )
{
    // L 자: 3x3 에서 오른쪽 위 2x2 를 뺀 넓이 5
    const vector<float2> listPoint{
        float2{0.0f, 0.0f},
        float2{3.0f, 0.0f},
        float2{3.0f, 1.0f},
        float2{1.0f, 1.0f},
        float2{1.0f, 3.0f},
        float2{0.0f, 3.0f}
    };
    for ( const vector<uint32>& listLoop : {
              vector<uint32>{0, 1, 2, 3, 4, 5},
              vector<uint32>{5, 4, 3, 2, 1, 0}
    } )
    {
        vector<uint32> listIndex;
        SW_EXPECT_TRUE( PolygonTriangulationUtil::triangulate( listPoint, { listLoop }, listIndex ) );
        SW_EXPECT_EQUAL( 12u, static_cast<uint32>( listIndex.size() ) ); // 점 6 개 = 삼각형 4 개
        SW_EXPECT_NEAR( 5.0f, PolygonTriangulationTestUtil::sumTriangleArea( listPoint, listIndex ), 0.001f );
    }
}

/** @brief [PolygonTriangulationTest] 반대 방향 고리는 구멍이다 — 구멍 넓이는 빠진다 */
SW_TEST_CASE( PolygonTriangulationTest, OppositeWindingLoopIsAHole )
{
    const vector<float2> listPoint{
        float2{0.0f, 0.0f},
        float2{4.0f, 0.0f},
        float2{4.0f, 4.0f},
        float2{0.0f, 4.0f},
        float2{1.0f, 1.0f},
        float2{1.0f, 3.0f},
        float2{3.0f, 3.0f},
        float2{3.0f, 1.0f}
    };
    vector<uint32> listIndex;
    SW_EXPECT_TRUE( PolygonTriangulationUtil::triangulate( listPoint, {
                                                                          {0, 1, 2, 3},
                                                                          {4, 5, 6, 7}
    },
                                                           listIndex ) );
    SW_EXPECT_NEAR( 12.0f, PolygonTriangulationTestUtil::sumTriangleArea( listPoint, listIndex ), 0.001f );
}

/** @brief [PolygonTriangulationTest] 같은 방향의 떨어진 고리는 각자 바깥이다 */
SW_TEST_CASE( PolygonTriangulationTest, SameWindingLoopsAreSeparateShells )
{
    const vector<float2> listPoint{
        float2{0.0f, 0.0f},
        float2{1.0f, 0.0f},
        float2{1.0f, 1.0f},
        float2{5.0f, 0.0f},
        float2{7.0f, 0.0f},
        float2{7.0f, 2.0f}
    };
    vector<uint32> listIndex;
    SW_EXPECT_TRUE( PolygonTriangulationUtil::triangulate( listPoint, {
                                                                          {0, 1, 2},
                                                                          {3, 4, 5}
    },
                                                           listIndex ) );
    SW_EXPECT_NEAR( 2.5f, PolygonTriangulationTestUtil::sumTriangleArea( listPoint, listIndex ), 0.001f );
}

/** @brief [PolygonTriangulationTest] 점이 모자라거나 넓이가 0 인 고리는 건너뛴다 — 실패가 아니다 */
SW_TEST_CASE( PolygonTriangulationTest, DegenerateLoopsAreSkipped )
{
    const vector<float2> listPoint{
        float2{0.0f, 0.0f},
        float2{1.0f, 0.0f},
        float2{2.0f, 0.0f}
    };
    vector<uint32> listIndex;
    SW_EXPECT_TRUE( PolygonTriangulationUtil::triangulate( listPoint, {
                                                                          { 0, 1 },
                                                                          { 0, 1, 2 }
    },
                                                           listIndex ) );
    SW_EXPECT_TRUE( listIndex.empty() );
}

/** @brief [PolygonTriangulationTest] 결과는 끝에 붙는다 — 앞서 넣은 삼각형을 지우지 않는다 */
SW_TEST_CASE( PolygonTriangulationTest, ResultIsAppended )
{
    const vector<float2> listPoint{
        float2{0.0f, 0.0f},
        float2{1.0f, 0.0f},
        float2{0.0f, 1.0f}
    };
    vector<uint32> listIndex{ 7u, 8u, 9u };
    SW_EXPECT_TRUE( PolygonTriangulationUtil::triangulate( listPoint, {
                                                                          { 0, 1, 2 }
    },
                                                           listIndex ) );
    SW_EXPECT_EQUAL( 6u, static_cast<uint32>( listIndex.size() ) );
    SW_EXPECT_EQUAL( 7u, listIndex[0] );
}

/**
 * @brief [PolygonTriangulationTest] 구멍 둘이 같은 바깥 꼭짓점으로 다리를 놓아도 막히지 않고 넓이 그대로 덮는다(감사 C-2)
 * @details 첫 다리를 놓으면 바깥 꼭짓점 (10, 0) 이 목록에 두 번 든다. 예전에는 둘째 다리가 원뿔 밖의 사본을 골라 고리가 꼬였고, 귀 자르기가 막혀 남은 부분을 부채꼴로 덮었다(구멍 위에 면).
 */
SW_TEST_CASE( PolygonTriangulationTest, TwoHolesBridgingToTheSameVertex )
{
    const vector<float2> listPoint{
        float2{ 0.0f,  0.0f},
        float2{10.0f,  0.0f},
        float2{10.0f, 10.0f},
        float2{ 0.0f, 10.0f}, // 바깥(반시계)
        float2{ 7.0f,  1.0f},
        float2{ 7.0f,  2.0f},
        float2{ 8.0f,  2.0f},
        float2{ 8.0f,  1.0f}, // 구멍 A(시계) — 오른쪽 점 (8, 1) 은 (10, 0) 이 가장 가깝다
        float2{ 7.0f,  3.0f},
        float2{ 7.0f,  4.0f},
        float2{ 8.0f,  4.0f},
        float2{ 8.0f,  3.0f}, // 구멍 B(시계) — 오른쪽 점 (8, 3) 도 (10, 0) 이 가장 가깝다
    };
    const vector<vector<uint32>> listLoop{
        {0, 1,  2,  3},
        {4, 5,  6,  7},
        {8, 9, 10, 11}
    };
    vector<uint32> listIndex;
    SW_EXPECT_TRUE( PolygonTriangulationUtil::triangulate( listPoint, listLoop, listIndex ) );
    SW_EXPECT_NEAR( 98.0f, PolygonTriangulationTestUtil::sumTriangleArea( listPoint, listIndex ), 0.001f );
}
