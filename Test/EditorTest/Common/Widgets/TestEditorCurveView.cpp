#include "pch.h"

#include "Editor/Common/Widgets/EditorCurveView.h"

#include "Engine/Utility/FloatCurve.h"

#include "TestFramework/TestFramework.h"

using sw::editor::EditorCurveView;

namespace
{
    struct TestEditorCurveViewInternal
    {
        static constexpr float32 kTolerance = 1e-3f;

        /** @brief 화면 (100, 50) 에서 크기 200 × 100, 시간 [0, 2] · 값 [-1, 1] 인 보기입니다. */
        static EditorCurveView makeView()
        {
            EditorCurveView view{};
            view._frameMin  = sw::float2{ 100.0f, 50.0f };
            view._frameSize = sw::float2{ 200.0f, 100.0f };
            view._timeMin   = 0.0f;
            view._timeMax   = 2.0f;
            view._valueMin  = -1.0f;
            view._valueMax  = 1.0f;
            return view;
        }
    };
} // namespace

/**
 * @brief [EditorCurveViewTest] 커브 → 화면 → 커브가 제자리로 돌아오고, 값이 크면 화면 위쪽이다
 */
SW_TEST_CASE( EditorCurveViewTest, ScreenRoundTrip )
{
    const EditorCurveView view   = TestEditorCurveViewInternal::makeView();
    const sw::float2      screen = view.toScreen( 1.5f, 0.5f );
    SW_EXPECT_NEAR_EQUAL( 250.0f, screen._x, TestEditorCurveViewInternal::kTolerance );
    SW_EXPECT_NEAR_EQUAL( 75.0f, screen._y, TestEditorCurveViewInternal::kTolerance );
    float32 time{ 0.0f };
    float32 value{ 0.0f };
    view.toCurve( screen, time, value );
    SW_EXPECT_NEAR_EQUAL( 1.5f, time, TestEditorCurveViewInternal::kTolerance );
    SW_EXPECT_NEAR_EQUAL( 0.5f, value, TestEditorCurveViewInternal::kTolerance );
}

/**
 * @brief [EditorCurveViewTest] 키 맞힘은 반경 안의 가장 가까운 키이고, 반경 밖이면 없다
 */
SW_TEST_CASE( EditorCurveViewTest, FindKeyWithinRadius )
{
    const EditorCurveView view = TestEditorCurveViewInternal::makeView();
    sw::FloatCurve        curve;
    curve.addKey( 0.0f, 0.0f );
    curve.addKey( 1.0f, 0.0f );
    const sw::float2 second = view.toScreen( 1.0f, 0.0f );
    SW_EXPECT_EQUAL( view.findKeyAt( curve, sw::float2{ second._x + 3.0f, second._y }, 6.0f ), 1u );
    SW_EXPECT_EQUAL( view.findKeyAt( curve, sw::float2{ second._x + 10.0f, second._y }, 6.0f ), sw::invalid_index::kUint32 );
}

/**
 * @brief [EditorCurveViewTest] 화면 맞추기가 모든 키를 화면 안에 담고, 키가 하나여도 폭이 무너지지 않는다
 */
SW_TEST_CASE( EditorCurveViewTest, FitContainsEveryKey )
{
    EditorCurveView view = TestEditorCurveViewInternal::makeView();
    sw::FloatCurve  curve;
    curve.addKey( -3.0f, 10.0f );
    curve.addKey( 5.0f, -20.0f );
    curve.addKey( 8.0f, 4.0f );
    view.fitToCurve( curve );
    for ( const sw::FloatCurveKey& key : curve._listKey )
    {
        const sw::float2 screen = view.toScreen( key._time, key._value );
        SW_EXPECT_TRUE( screen._x >= view._frameMin._x && screen._x <= view._frameMin._x + view._frameSize._x );
        SW_EXPECT_TRUE( screen._y >= view._frameMin._y && screen._y <= view._frameMin._y + view._frameSize._y );
    }
    sw::FloatCurve single;
    single.addKey( 1.0f, 1.0f );
    view.fitToCurve( single );
    SW_EXPECT_TRUE( view._timeMax - view._timeMin > 0.5f );
    SW_EXPECT_TRUE( view._valueMax - view._valueMin > 0.5f );
}

/**
 * @brief [EditorCurveViewTest] 접선 손잡이 자리에서 센 기울기가 그 손잡이의 기울기와 같다
 */
SW_TEST_CASE( EditorCurveViewTest, TangentHandleRoundTrip )
{
    const EditorCurveView view = TestEditorCurveViewInternal::makeView();
    sw::FloatCurveKey     key{};
    key._time               = 1.0f;
    key._value              = 0.0f;
    key._leaveTangent       = 0.75f;
    const sw::float2 handle = view.computeTangentHandle( key, true, 40.0f );
    SW_EXPECT_TRUE( handle._x > view.toScreen( 1.0f, 0.0f )._x );
    SW_EXPECT_NEAR_EQUAL( 0.75f, view.computeTangentFromHandle( key, handle ), 1e-2f );
}

/**
 * @brief [EditorCurveViewTest] 격자 간격은 1 · 2 · 5 × 10^n 이고 줄 사이가 최소 픽셀 이상이다
 */
SW_TEST_CASE( EditorCurveViewTest, GridStepIsNiceNumber )
{
    SW_EXPECT_NEAR_EQUAL( 1.0f, EditorCurveView::computeGridStep( 10.0f, 500.0f, 48.0f ), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, EditorCurveView::computeGridStep( 10.0f, 200.0f, 48.0f ), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.2f, EditorCurveView::computeGridStep( 1.0f, 400.0f, 48.0f ), 1e-5f );
}
