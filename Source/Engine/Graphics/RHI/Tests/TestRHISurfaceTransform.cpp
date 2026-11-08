#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/RHISurfaceTransform.h"

namespace
{
    struct SurfaceCoord
    {
        float32 _u;
        float32 _v;
    };

    SurfaceCoord mapToLogical( const sw::RHISurfaceCoordMapping& mapping, float32 physicalU, float32 physicalV )
    {
        return SurfaceCoord{ mapping._m00 * physicalU + mapping._m01 * physicalV + mapping._offsetU, mapping._m10 * physicalU + mapping._m11 * physicalV + mapping._offsetV };
    }
} // namespace

/**
 * @brief [RHISurfaceTransformTest] 90 · 270 회전만 실제 이미지의 너비 · 높이를 맞바꾼다(회전 출력 패스의 뷰포트 · 시저 크기)
 */
SW_TEST_CASE( RHISurfaceTransformTest, PhysicalExtentSwapsOnlyForQuarterTurns )
{
    using sw::RHISurfaceTransform;
    const sw::RHISurfaceRotation identity{ RHISurfaceTransform::Identity, 2400, 1080 };
    const sw::RHISurfaceRotation quarter{ RHISurfaceTransform::Rotate90, 2400, 1080 };
    const sw::RHISurfaceRotation half{ RHISurfaceTransform::Rotate180, 2400, 1080 };
    const sw::RHISurfaceRotation threeQuarter{ RHISurfaceTransform::Rotate270, 2400, 1080 };
    SW_EXPECT_TRUE( identity.isIdentity() );
    SW_EXPECT_FALSE( half.isIdentity() );
    SW_EXPECT_EQUAL( 2400u, identity.getPhysicalWidth() );
    SW_EXPECT_EQUAL( 1080u, identity.getPhysicalHeight() );
    SW_EXPECT_EQUAL( 1080u, quarter.getPhysicalWidth() );
    SW_EXPECT_EQUAL( 2400u, quarter.getPhysicalHeight() );
    SW_EXPECT_EQUAL( 2400u, half.getPhysicalWidth() );
    SW_EXPECT_EQUAL( 1080u, half.getPhysicalHeight() );
    SW_EXPECT_EQUAL( 1080u, threeQuarter.getPhysicalWidth() );
    SW_EXPECT_EQUAL( 2400u, threeQuarter.getPhysicalHeight() );
}

/**
 * @brief [RHISurfaceTransformTest] UV 대응이 논리 그림의 모서리를 시계 방향 회전한 자리로 보낸다
 * @details 시계 방향 90 도로 미리 돌린 이미지는 논리 왼쪽 위가 실제 오른쪽 위에, 논리 오른쪽 위가 실제 오른쪽 아래에 놓인다 —
 *          실제 UV 에서 읽을 논리 UV 를 모서리마다 대조한다. 거꾸로 돌면 화면이 위아래로 뒤집혀 보인다.
 */
SW_TEST_CASE( RHISurfaceTransformTest, UVMappingRotatesCornersClockwise )
{
    using sw::RHISurfaceTransform;
    const sw::RHISurfaceCoordMapping identity = sw::RHISurfaceRotation{ RHISurfaceTransform::Identity, 4, 2 }.computeLogicalUVMapping();
    const SurfaceCoord               same     = mapToLogical( identity, 0.25f, 0.75f );
    SW_EXPECT_EQUAL( 0.25f, same._u );
    SW_EXPECT_EQUAL( 0.75f, same._v );

    // 90: 실제 오른쪽 위(1, 0) 는 논리 왼쪽 위(0, 0), 실제 오른쪽 아래(1, 1) 는 논리 오른쪽 위(1, 0)
    const sw::RHISurfaceCoordMapping quarter         = sw::RHISurfaceRotation{ RHISurfaceTransform::Rotate90, 4, 2 }.computeLogicalUVMapping();
    const SurfaceCoord               quarterTopRight = mapToLogical( quarter, 1.0f, 0.0f );
    const SurfaceCoord               quarterBotRight = mapToLogical( quarter, 1.0f, 1.0f );
    SW_EXPECT_EQUAL( 0.0f, quarterTopRight._u );
    SW_EXPECT_EQUAL( 0.0f, quarterTopRight._v );
    SW_EXPECT_EQUAL( 1.0f, quarterBotRight._u );
    SW_EXPECT_EQUAL( 0.0f, quarterBotRight._v );

    // 180: 실제 왼쪽 위는 논리 오른쪽 아래
    const SurfaceCoord half = mapToLogical( sw::RHISurfaceRotation{ RHISurfaceTransform::Rotate180, 4, 2 }.computeLogicalUVMapping(), 0.0f, 0.0f );
    SW_EXPECT_EQUAL( 1.0f, half._u );
    SW_EXPECT_EQUAL( 1.0f, half._v );

    // 270: 실제 왼쪽 아래(0, 1) 는 논리 왼쪽 위, 실제 왼쪽 위(0, 0) 는 논리 오른쪽 위
    const sw::RHISurfaceCoordMapping threeQuarter        = sw::RHISurfaceRotation{ RHISurfaceTransform::Rotate270, 4, 2 }.computeLogicalUVMapping();
    const SurfaceCoord               threeQuarterBotLeft = mapToLogical( threeQuarter, 0.0f, 1.0f );
    const SurfaceCoord               threeQuarterTopLeft = mapToLogical( threeQuarter, 0.0f, 0.0f );
    SW_EXPECT_EQUAL( 0.0f, threeQuarterBotLeft._u );
    SW_EXPECT_EQUAL( 0.0f, threeQuarterBotLeft._v );
    SW_EXPECT_EQUAL( 1.0f, threeQuarterTopLeft._u );
    SW_EXPECT_EQUAL( 0.0f, threeQuarterTopLeft._v );
}

/**
 * @brief [RHISurfaceTransformTest] 뷰포트 · 시저 사각형이 Android pre-rotation 지침의 식대로 옮겨지고, 논리 전체는 실제 전체가 된다
 * @details 논리 2400x1080(가로) 화면의 (100, 50, 500, 400) 사각형 — 90 이면 실제 1080x2400 의 (1080 - 400 - 50, 100, 400, 500) 이다.
 */
SW_TEST_CASE( RHISurfaceTransformTest, LogicalRectMapsToPhysicalViewport )
{
    using sw::RHISurfaceTransform;
    const sw::RHISurfaceRect rect{ 100, 50, 500, 400 };

    const sw::RHISurfaceRect identity = sw::RHISurfaceRotation{ RHISurfaceTransform::Identity, 2400, 1080 }.mapLogicalRect( rect );
    SW_EXPECT_EQUAL( 100u, identity._x );
    SW_EXPECT_EQUAL( 50u, identity._y );
    SW_EXPECT_EQUAL( 500u, identity._width );
    SW_EXPECT_EQUAL( 400u, identity._height );

    const sw::RHISurfaceRect quarter = sw::RHISurfaceRotation{ RHISurfaceTransform::Rotate90, 2400, 1080 }.mapLogicalRect( rect );
    SW_EXPECT_EQUAL( 630u, quarter._x );
    SW_EXPECT_EQUAL( 100u, quarter._y );
    SW_EXPECT_EQUAL( 400u, quarter._width );
    SW_EXPECT_EQUAL( 500u, quarter._height );

    const sw::RHISurfaceRect half = sw::RHISurfaceRotation{ RHISurfaceTransform::Rotate180, 2400, 1080 }.mapLogicalRect( rect );
    SW_EXPECT_EQUAL( 1800u, half._x );
    SW_EXPECT_EQUAL( 630u, half._y );

    const sw::RHISurfaceRect threeQuarter = sw::RHISurfaceRotation{ RHISurfaceTransform::Rotate270, 2400, 1080 }.mapLogicalRect( rect );
    SW_EXPECT_EQUAL( 50u, threeQuarter._x );
    SW_EXPECT_EQUAL( 1800u, threeQuarter._y );
    SW_EXPECT_EQUAL( 400u, threeQuarter._width );
    SW_EXPECT_EQUAL( 500u, threeQuarter._height );

    // 논리 전체 사각형은 실제 이미지 전체다(회전 출력 패스의 뷰포트)
    const sw::RHISurfaceRotation full{ RHISurfaceTransform::Rotate270, 2400, 1080 };
    const sw::RHISurfaceRect     whole = full.mapLogicalRect( sw::RHISurfaceRect{ 0, 0, 2400, 1080 } );
    SW_EXPECT_EQUAL( 0u, whole._x );
    SW_EXPECT_EQUAL( 0u, whole._y );
    SW_EXPECT_EQUAL( full.getPhysicalWidth(), whole._width );
    SW_EXPECT_EQUAL( full.getPhysicalHeight(), whole._height );
}
