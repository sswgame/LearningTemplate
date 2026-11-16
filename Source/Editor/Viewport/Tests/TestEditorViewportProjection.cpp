/**
 * @file TestEditorViewportProjection.cpp
 * @brief 상자의 화면 사각형(`EditorViewportProjectionUtil::projectBoxRect`)이 카메라 뒤로 넘어간 모서리를 근평면에서 잘라 모으는지 봅니다(감사 E-10 — 상자 선택).
 */
#include "pch.h"

#include "Core/Math/AABB.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Viewport/EditorViewportProjection.h"

namespace
{
    struct EditorViewportProjectionTestInternal
    {
        /** @brief 원점에서 +Z 를 보는 시야각 90° · 정사각 화면의 뷰-투영입니다. */
        static sw::float4x4 makeViewProj()
        {
            const sw::float4x4 view = sw::float4x4::makeLookAt( sw::float3{ 0.0f, 0.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f }, sw::float3{ 0.0f, 1.0f, 0.0f } );
            const sw::float4x4 proj = sw::float4x4::makePerspectiveFieldOfView( sw::MathUtil::kPi * 0.5f, 1.0f, 0.1f, 1000.0f );
            return view * proj;
        }
    };
} // namespace

/**
 * @brief [EditorViewportProjectionTest] 앞의 작은 상자는 캔버스 안 사각형이고, 카메라를 품은 큰 상자는 캔버스를 다 덮으며, 카메라 뒤 상자는 없다
 */
SW_TEST_CASE( EditorViewportProjectionTest, BoxRectClipsAtNearPlane )
{
    using Internal = EditorViewportProjectionTestInternal;

    const sw::float4x4 viewProj = Internal::makeViewProj();
    const sw::float2   canvasPos{ 0.0f, 0.0f };
    const sw::float2   canvasSize{ 100.0f, 100.0f };
    sw::float2         screenMin{};
    sw::float2         screenMax{};

    const sw::AABB ahead{
        sw::float3{-1.0f, -1.0f,  9.0f},
        sw::float3{ 1.0f,  1.0f, 11.0f}
    };
    SW_REQUIRE_TRUE( sw::editor::EditorViewportProjectionUtil::projectBoxRect( viewProj, ahead, canvasPos, canvasSize, screenMin, screenMax ) );
    SW_EXPECT_TRUE( screenMin._x > 0.0f && screenMax._x < 100.0f );
    SW_EXPECT_TRUE( screenMin._y > 0.0f && screenMax._y < 100.0f );
    SW_EXPECT_NEAR( 50.0f, ( screenMin._x + screenMax._x ) * 0.5f, 1e-3f ); // 가운데

    // 카메라를 품은 상자 — 꼭짓점 넷이 뒤에 있다. 앞 꼭짓점만 모으면 캔버스 가운데의 작은 사각형이다.
    const sw::AABB around{
        sw::float3{-5.0f, -5.0f, -5.0f},
        sw::float3{ 5.0f,  5.0f,  5.0f}
    };
    SW_REQUIRE_TRUE( sw::editor::EditorViewportProjectionUtil::projectBoxRect( viewProj, around, canvasPos, canvasSize, screenMin, screenMax ) );
    SW_EXPECT_TRUE( screenMin._x <= 0.0f && screenMax._x >= 100.0f );
    SW_EXPECT_TRUE( screenMin._y <= 0.0f && screenMax._y >= 100.0f );

    const sw::AABB behind{
        sw::float3{-1.0f, -1.0f, -11.0f},
        sw::float3{ 1.0f,  1.0f,  -9.0f}
    };
    SW_EXPECT_FALSE( sw::editor::EditorViewportProjectionUtil::projectBoxRect( viewProj, behind, canvasPos, canvasSize, screenMin, screenMax ) );
}
