#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/2D/SpriteMeshBuilder.h"
#include "Engine/Graphics/Mesh/Mesh.h"

#include "TestFramework/TestFramework.h"

// SpriteMeshBuilderTest — 9-슬라이스 · 타일 스프라이트의 정점 배치(위치 · 프레임 UV · 두 면). 디바이스 없음(nogpu).

namespace
{
    /** @brief 앞면 사각형 하나의 경계 — 위치 (왼, 아래, 오른, 위) · UV (u 왼, v 위, u 오른, v 아래)입니다. */
    struct QuadBounds
    {
        float32 _left;
        float32 _bottom;
        float32 _right;
        float32 _top;
        float32 _uLeft;
        float32 _vTop;
        float32 _uRight;
        float32 _vBottom;
    };

    /** @brief 정점 여섯(삼각형 둘)에서 사각형 경계를 읽습니다. 순서는 (왼아래, 오른위, 오른아래), (왼아래, 왼위, 오른위) 입니다. */
    QuadBounds readQuad( const sw::vector<sw::RHIVertex>& listVertex, size_t quadIndex )
    {
        const sw::RHIVertex& bottomLeft = listVertex[quadIndex * 6 + 0];
        const sw::RHIVertex& topRight   = listVertex[quadIndex * 6 + 1];
        return QuadBounds{ bottomLeft._arrPosition[0], bottomLeft._arrPosition[1], topRight._arrPosition[0], topRight._arrPosition[1],
                           bottomLeft._arrUv[0], topRight._arrUv[1], topRight._arrUv[0], bottomLeft._arrUv[1] };
    }

    bool isNear( float32 expected, float32 actual )
    {
        return sw::MathUtil::abs( expected - actual ) < 1e-5f;
    }
} // namespace

/**
 * @brief [SpriteMeshBuilderTest] 9-슬라이스: 모서리는 자연 크기(테두리 비율 그대로)이고 가운데만 늘어나며, UV 는 테두리에서 끊긴다
 * @details 4 × 2 크기 · 테두리 0.25 — 사각형 아홉(앞면) + 거울 뒷면 아홉. 왼위 모서리는 위치 [-2, -1.75] × [0.75, 1], UV [0, 0.25] × [0, 0.25](v 는 위가 0).
 *          가운데는 위치 [-1.75, 1.75] 를 UV [0.25, 0.75] 로 늘린다. 뒷면은 앞면을 X 로 비춘 것이라 같은 UV 를 쓰고 노멀이 +Z 다.
 */
SW_TEST_CASE( SpriteMeshBuilderTest, SlicedCornersKeepTheirSizeAndTheCenterStretches )
{
    sw::SlicedSpriteDesc desc{};
    desc._size   = sw::float2{ 4.0f, 2.0f };
    desc._border = sw::float4{ 0.25f, 0.25f, 0.25f, 0.25f };
    sw::vector<sw::RHIVertex> listVertex;
    sw::SpriteMeshBuilder::makeSlicedVertices( desc, listVertex );
    SW_ASSERT_EQUAL( 9u * 6u * 2u, static_cast<uint32>( listVertex.size() ) );

    // 행은 아래 → 위, 열은 왼 → 오른. 사각형 6 = 위 행의 왼쪽 열.
    const QuadBounds topLeft = readQuad( listVertex, 6 );
    SW_EXPECT_TRUE( isNear( -2.0f, topLeft._left ) && isNear( -1.75f, topLeft._right ) );
    SW_EXPECT_TRUE( isNear( 0.75f, topLeft._bottom ) && isNear( 1.0f, topLeft._top ) );
    SW_EXPECT_TRUE( isNear( 0.0f, topLeft._uLeft ) && isNear( 0.25f, topLeft._uRight ) );
    SW_EXPECT_TRUE( isNear( 0.0f, topLeft._vTop ) && isNear( 0.25f, topLeft._vBottom ) );

    const QuadBounds center = readQuad( listVertex, 4 );
    SW_EXPECT_TRUE( isNear( -1.75f, center._left ) && isNear( 1.75f, center._right ) );
    SW_EXPECT_TRUE( isNear( -0.75f, center._bottom ) && isNear( 0.75f, center._top ) );
    SW_EXPECT_TRUE( isNear( 0.25f, center._uLeft ) && isNear( 0.75f, center._uRight ) );
    SW_EXPECT_TRUE( isNear( 0.25f, center._vTop ) && isNear( 0.75f, center._vBottom ) );

    const QuadBounds bottomRight = readQuad( listVertex, 2 );
    SW_EXPECT_TRUE( isNear( 1.75f, bottomRight._left ) && isNear( 2.0f, bottomRight._right ) );
    SW_EXPECT_TRUE( isNear( 0.75f, bottomRight._uLeft ) && isNear( 1.0f, bottomRight._uRight ) );
    SW_EXPECT_TRUE( isNear( 0.75f, bottomRight._vTop ) && isNear( 1.0f, bottomRight._vBottom ) );

    // 앞면은 -Z, 뒷면은 X 거울 · +Z 이고 같은 UV 다(뒤에서 봐도 뒤집히지 않는다).
    SW_EXPECT_TRUE( isNear( -1.0f, listVertex[0]._arrNormal[2] ) );
    const sw::RHIVertex& backFirst = listVertex[9 * 6];
    SW_EXPECT_TRUE( isNear( 1.0f, backFirst._arrNormal[2] ) );
    SW_EXPECT_TRUE( isNear( -listVertex[0]._arrPosition[0], backFirst._arrPosition[0] ) );
    SW_EXPECT_TRUE( isNear( listVertex[0]._arrUv[0], backFirst._arrUv[0] ) );
}

/**
 * @brief [SpriteMeshBuilderTest] 타일: 가운데가 자연 크기 칸으로 되풀이되고 마지막 칸은 잘린 만큼만 UV 를 쓴다. 테두리 합보다 작으면 테두리가 줄어든다
 * @details 테두리 0 · 크기 2.5 × 1 이면 칸 1 × 1 이 셋(마지막 0.5 칸은 u [0, 0.5]). 크기가 테두리 합(0.5)보다 작은 0.2 면 테두리를 0.1 씩으로 줄이고
 *          가운데는 없다 — 모서리 텍스처는 다 보인다.
 */
SW_TEST_CASE( SpriteMeshBuilderTest, TiledRepeatsTheMiddleAndCropsTheLastTile )
{
    sw::SlicedSpriteDesc tiled{};
    tiled._size   = sw::float2{ 2.5f, 1.0f };
    tiled._bTiled = SW_TRUE;
    sw::vector<sw::RHIVertex> listVertex;
    sw::SpriteMeshBuilder::makeSlicedVertices( tiled, listVertex );
    SW_ASSERT_EQUAL( 3u * 6u * 2u, static_cast<uint32>( listVertex.size() ) );
    const QuadBounds first = readQuad( listVertex, 0 );
    const QuadBounds last  = readQuad( listVertex, 2 );
    SW_EXPECT_TRUE( isNear( -1.25f, first._left ) && isNear( -0.25f, first._right ) );
    SW_EXPECT_TRUE( isNear( 0.0f, first._uLeft ) && isNear( 1.0f, first._uRight ) );
    SW_EXPECT_TRUE( isNear( 0.75f, last._left ) && isNear( 1.25f, last._right ) );
    SW_EXPECT_TRUE( isNear( 0.0f, last._uLeft ) && isNear( 0.5f, last._uRight ) );

    // Sliced 로 같은 크기면 칸 하나가 늘어난다.
    sw::SlicedSpriteDesc stretched = tiled;
    stretched._bTiled              = SW_FALSE;
    sw::SpriteMeshBuilder::makeSlicedVertices( stretched, listVertex );
    SW_ASSERT_EQUAL( 1u * 6u * 2u, static_cast<uint32>( listVertex.size() ) );

    // 테두리 합보다 작은 크기.
    sw::SlicedSpriteDesc tiny{};
    tiny._size   = sw::float2{ 0.2f, 1.0f };
    tiny._border = sw::float4{ 0.25f, 0.0f, 0.25f, 0.0f };
    sw::SpriteMeshBuilder::makeSlicedVertices( tiny, listVertex );
    SW_ASSERT_EQUAL( 2u * 6u * 2u, static_cast<uint32>( listVertex.size() ) );
    const QuadBounds leftBorder = readQuad( listVertex, 0 );
    SW_EXPECT_TRUE( isNear( -0.1f, leftBorder._left ) && isNear( 0.0f, leftBorder._right ) );
    SW_EXPECT_TRUE( isNear( 0.0f, leftBorder._uLeft ) && isNear( 0.25f, leftBorder._uRight ) );
}

/**
 * @brief [SpriteMeshBuilderTest] 같은 크기 · 테두리 · 방식의 메시는 하나를 나눠 쓴다 — 같은 패널들이 한 배치로 묶인다
 */
SW_TEST_CASE( SpriteMeshBuilderTest, SameDescSharesOneMesh )
{
    sw::SlicedSpriteDesc desc{};
    desc._size                            = sw::float2{ 3.0f, 1.5f };
    desc._border                          = sw::float4{ 0.2f, 0.2f, 0.2f, 0.2f };
    const sw::shared_ptr<sw::Mesh> first  = sw::SpriteMeshBuilder::acquireSlicedMesh( desc );
    const sw::shared_ptr<sw::Mesh> second = sw::SpriteMeshBuilder::acquireSlicedMesh( desc );
    SW_ASSERT_NOT_NULL( first.get() );
    SW_EXPECT_TRUE( first == second );
    desc._bTiled                         = SW_TRUE;
    const sw::shared_ptr<sw::Mesh> tiled = sw::SpriteMeshBuilder::acquireSlicedMesh( desc );
    SW_EXPECT_TRUE( tiled != first );
    // 경계 반지름이 크기를 따른다(컬링이 메시의 반지름을 쓴다).
    SW_EXPECT_TRUE( first->getBoundingRadius() >= 1.6f );
}
