#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Widgets/EditorCurveView.h"

#include "Engine/Curve/FloatCurve.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct TestEditorCurveViewInternal
    {
        static constexpr float32 kTolerance = 1e-4f;
    };
} // namespace

/**
 * @brief [EditorCurveViewTest] 값 범위는 키 값에 곡선의 넘침까지 담고, 키가 없으면 false 다
 */
SW_TEST_CASE( EditorCurveViewTest, ValueRangeIncludesOvershoot )
{
    FloatCurve curve;
    curve.addKey( 0.0f, 0.0f, CurveInterpolation::Cubic );
    curve.addKey( 2.0f, 4.0f, CurveInterpolation::Cubic );
    curve._listKey[0]._bAutoTangent = false;
    curve._listKey[0]._leaveTangent = 20.0f; // 처음에 가파르게 올라 끝 값을 넘는다
    float32 minValue{ 0.0f };
    float32 maxValue{ 0.0f };
    SW_REQUIRE_TRUE( EditorCurveView::computeValueRange( curve, minValue, maxValue ) );
    SW_EXPECT_NEAR( 0.0f, minValue, TestEditorCurveViewInternal::kTolerance );
    SW_EXPECT_TRUE( maxValue > 4.0f );
    FloatCurve empty;
    SW_EXPECT_FALSE( EditorCurveView::computeValueRange( empty, minValue, maxValue ) );
}
