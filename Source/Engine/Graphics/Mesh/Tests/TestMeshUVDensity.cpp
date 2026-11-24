#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Mesh/Mesh.h"

// UV 밀도(텍스처 밉 스트리밍의 우선도) — UV 한 단위가 덮는 메시 공간 길이. 같은 크기라도 UV 를 촘촘히 펼치면 작아지고, UV 가 없으면 0 이다.

using namespace sw;

namespace
{
    struct MeshUVDensityTestUtil
    {
        /** @brief 한 변 @p size m 의 사각형(삼각형 둘), UV 는 0 .. @p uvExtent 입니다. */
        static void makeQuad( float32 size, float32 uvExtent, vector<RHIVertex>& outListVertex, vector<uint32>& outListIndex )
        {
            const float32 arrCorner[4][2] = {
                {0.0f, 0.0f},
                {1.0f, 0.0f},
                {1.0f, 1.0f},
                {0.0f, 1.0f},
            };
            outListVertex.clear();
            for ( const auto& corner : arrCorner )
            {
                RHIVertex vertex{};
                vertex._arrPosition[0] = corner[0] * size;
                vertex._arrPosition[2] = corner[1] * size;
                vertex._arrUV[0]       = corner[0] * uvExtent;
                vertex._arrUV[1]       = corner[1] * uvExtent;
                outListVertex.push_back( vertex );
            }
            outListIndex = { 0u, 1u, 2u, 0u, 2u, 3u };
        }
    };
} // namespace

/** @brief [MeshUVDensityTest] 2 m 사각형에 UV 0..1 이면 UV 한 단위가 2 m, UV 를 네 번 되풀이하면 0.5 m 다 */
SW_TEST_CASE( MeshUVDensityTest, DensityIsMeshLengthPerUVUnit )
{
    vector<RHIVertex> listVertex;
    vector<uint32>    listIndex;
    MeshUVDensityTestUtil::makeQuad( 2.0f, 1.0f, listVertex, listIndex );
    SW_EXPECT_NEAR( 2.0, Mesh::computeUVDensity( listVertex, listIndex ), 1e-5 );
    MeshUVDensityTestUtil::makeQuad( 2.0f, 4.0f, listVertex, listIndex );
    SW_EXPECT_NEAR( 0.5, Mesh::computeUVDensity( listVertex, listIndex ), 1e-5 );
}

/** @brief [MeshUVDensityTest] UV 가 한 점이면(펼치지 않았다) 0 이고, 메시가 정점을 받을 때 같은 값을 든다 */
SW_TEST_CASE( MeshUVDensityTest, MeshWithoutUVsHasZeroDensity )
{
    vector<RHIVertex> listVertex;
    vector<uint32>    listIndex;
    MeshUVDensityTestUtil::makeQuad( 2.0f, 0.0f, listVertex, listIndex );
    SW_EXPECT_NEAR( 0.0, Mesh::computeUVDensity( listVertex, listIndex ), 1e-9 );

    MeshUVDensityTestUtil::makeQuad( 3.0f, 1.0f, listVertex, listIndex );
    const shared_ptr<Mesh> mesh = Mesh::create( MeshCPUAccess::Keep );
    SW_REQUIRE_TRUE( mesh->setGeometryWithTangents( vector<RHIVertex>( listVertex ), vector<uint32>( listIndex ) ) );
    SW_EXPECT_NEAR( 3.0, mesh->getUVDensity(), 1e-5 );
}

/** @brief [MeshUVDensityTest] 평균 변 길이는 평균 넓이 정삼각형의 변이다 — 2 m 사각형(넓이 2 인 삼각형 둘)이면 √(4 × 2 / √3), 삼각형이 없으면 0 */
SW_TEST_CASE( MeshUVDensityTest, MeanEdgeLengthFollowsTriangleArea )
{
    vector<RHIVertex> listVertex;
    vector<uint32>    listIndex;
    MeshUVDensityTestUtil::makeQuad( 2.0f, 1.0f, listVertex, listIndex );
    SW_EXPECT_NEAR( 2.149140, Mesh::computeMeanEdgeLength( listVertex, listIndex ), 1e-4 );
    SW_EXPECT_NEAR( 0.0, Mesh::computeMeanEdgeLength( listVertex, vector<uint32>{} ), 1e-9 );
}
