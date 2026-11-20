#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Curve/FloatCurve.h"

// FloatCurveKeysTest — 커브 키 다루기: 키 넣기의 자리(같은 시각은 뒤로), 자동 접선(가운데는 이웃 기울기 · 끝은 평평), 구간 찾기(키 여럿), 보간 종류마다의 값(nogpu).

using namespace sw;

/** @brief [FloatCurveKeysTest] 키는 시각 순 자리에 들어가고 같은 시각은 기존 키 뒤로 간다 — 돌려준 번호가 그 자리다 */
SW_TEST_CASE( FloatCurveKeysTest, AddKeyKeepsTimeOrder )
{
    FloatCurve curve;
    SW_EXPECT_EQUAL( 0u, curve.addKey( 2.0f, 20.0f ) );
    SW_EXPECT_EQUAL( 0u, curve.addKey( 0.0f, 0.0f ) );
    SW_EXPECT_EQUAL( 2u, curve.addKey( 2.0f, 21.0f ) ); // 같은 시각은 뒤로
    SW_EXPECT_EQUAL( 1u, curve.addKey( 1.0f, 10.0f ) );
    SW_REQUIRE_EQUAL( size_t( 4 ), curve._listKey.size() );
    SW_EXPECT_EQUAL( 20.0f, curve._listKey[2]._value );
    SW_EXPECT_EQUAL( 21.0f, curve._listKey[3]._value );
}

/** @brief [FloatCurveKeysTest] 자동 접선은 가운데 키가 앞뒤 키를 잇는 기울기, 극값 키 · 끝 키가 0 이다. 손으로 정한 접선은 그대로다 */
SW_TEST_CASE( FloatCurveKeysTest, AutoTangentsFollowNeighbours )
{
    FloatCurve curve;
    (void)curve.addKey( 0.0f, 0.0f );
    (void)curve.addKey( 1.0f, 1.0f );
    (void)curve.addKey( 2.0f, 4.0f );
    SW_EXPECT_NEAR( 0.0f, curve._listKey[0]._leaveTangent, 1e-6f );
    SW_EXPECT_NEAR( 2.0f, curve._listKey[1]._arriveTangent, 1e-6f ); // (4 - 0) / (2 - 0) — 한도 3 × 1 안
    SW_EXPECT_NEAR( 2.0f, curve._listKey[1]._leaveTangent, 1e-6f );
    SW_EXPECT_NEAR( 0.0f, curve._listKey[2]._arriveTangent, 1e-6f );
    curve._listKey[1]._value = 5.0f; // 극값(이웃 둘이 아래) — 평평해야 키 너머로 넘치지 않는다
    curve.sortAndComputeAutoTangents();
    SW_EXPECT_NEAR( 0.0f, curve._listKey[1]._leaveTangent, 1e-6f );

    curve._listKey[1]._bAutoTangent = false;
    curve._listKey[1]._leaveTangent = -3.0f;
    curve.sortAndComputeAutoTangents();
    SW_EXPECT_EQUAL( -3.0f, curve._listKey[1]._leaveTangent );
}

/** @brief [FloatCurveKeysTest] 키 여럿에서 시각마다 그 구간을 찾고, 상수 · 선형 · 3 차가 각자의 값을 낸다(키 자리에서는 키 값이다) */
SW_TEST_CASE( FloatCurveKeysTest, EvaluatesEachSegmentByItsInterpolation )
{
    FloatCurve curve;
    (void)curve.addKey( 0.0f, 0.0f, CurveInterpolation::Linear );
    (void)curve.addKey( 1.0f, 10.0f, CurveInterpolation::Constant );
    (void)curve.addKey( 2.0f, 20.0f, CurveInterpolation::Cubic );
    (void)curve.addKey( 3.0f, 0.0f, CurveInterpolation::Linear );

    SW_EXPECT_NEAR( 5.0f, *curve.evaluate( 0.5f ), 1e-5f );  // 선형
    SW_EXPECT_NEAR( 10.0f, *curve.evaluate( 1.5f ), 1e-5f ); // 상수 — 다음 키까지 앞 값
    SW_EXPECT_NEAR( 20.0f, *curve.evaluate( 2.0f ), 1e-5f ); // 키 자리
    const float32 cubic = *curve.evaluate( 2.5f );
    SW_EXPECT_TRUE( cubic > 0.0f && cubic < 20.0f );              // 3 차 — 양 끝 사이
    SW_EXPECT_NEAR( 0.0f, *curve.evaluate( 10.0f ), 1e-5f );      // 끝 뒤는 끝 값
    SW_EXPECT_NEAR( 0.0f, *curve.evaluate( -1.0f ), 1e-5f );      // 앞 앞은 첫 값
    SW_EXPECT_FALSE( FloatCurve{}.evaluate( 0.0f ).has_value() ); // 키 없음
}

/** @brief [FloatCurveKeysTest] 자동 접선의 Cubic 은 키 값 밖으로 넘치지 않는다 — 단조 구간은 단조, 극값 키는 키 값이 끝이다(언리얼 Smart Auto · 유니티 Clamped Auto) */
SW_TEST_CASE( FloatCurveKeysTest, AutoCubicDoesNotOvershoot )
{
    FloatCurve curve;
    (void)curve.addKey( 0.0f, 0.0f );
    (void)curve.addKey( 2.0f, 1.0f );
    (void)curve.addKey( 4.0f, 8.0f );
    (void)curve.addKey( 5.0f, 8.0f );
    (void)curve.addKey( 6.0f, 2.0f );
    float32 previous = 0.0f;
    for ( int32 sample = 0; sample <= 80; ++sample )
    {
        const float32 time  = static_cast<float32>( sample ) * 0.05f;
        const float32 value = *curve.evaluate( time );
        SW_EXPECT_TRUE( value >= previous - 1e-5f ); // 0..4 는 키가 오르기만 한다
        previous = value;
    }
    for ( int32 sample = 0; sample <= 40; ++sample )
    {
        const float32 value = *curve.evaluate( 4.0f + static_cast<float32>( sample ) * 0.05f );
        SW_EXPECT_TRUE( value <= 8.0f + 1e-5f && value >= 2.0f - 1e-5f );
    }
}

/** @brief [FloatCurveKeysTest] 사용자 접선은 그 기울기를 지킨다 — 0..1 을 접선 0 · 0 이면 가운데가 0.5, 나가는 접선 2 면 앞쪽이 직선보다 높다 */
SW_TEST_CASE( FloatCurveKeysTest, UserTangentsShapeTheCubic )
{
    FloatCurve curve;
    (void)curve.addKey( 0.0f, 0.0f );
    (void)curve.addKey( 1.0f, 1.0f );
    SW_EXPECT_NEAR( 0.5f, *curve.evaluate( 0.5f ), 1e-5f ); // 끝 키는 평평 — 스무스스텝
    curve._listKey[0]._bAutoTangent = false;
    curve._listKey[0]._leaveTangent = 2.0f;
    curve.sortAndComputeAutoTangents();
    // 에르미트: h10 · 2 = (t³ - 2t² + t) · 2 를 더한다 — t 0.25 에서 0.15625 + 0.28125
    SW_EXPECT_NEAR( 0.4375f, *curve.evaluate( 0.25f ), 1e-5f );
}

/** @brief [FloatCurveKeysTest] Eased 키는 다음 키까지 블렌드 곡선(`BlendCurve`)을 따른다 — 표본 값이 그 곡선의 가중치이고 두 키 사이를 넘지 않는다 */
SW_TEST_CASE( FloatCurveKeysTest, EasedSegmentFollowsTheBlendCurve )
{
    FloatCurve curve;
    (void)curve.addKey( 0.0f, 10.0f, CurveInterpolation::Eased );
    (void)curve.addKey( 2.0f, 20.0f, CurveInterpolation::Linear );
    curve._listKey[0]._easing = BlendCurve::EaseIn; // t² (기본 지수 2)
    SW_EXPECT_NEAR( 10.0f + 10.0f * 0.0625f, *curve.evaluate( 0.5f ), 1e-4f );
    SW_EXPECT_NEAR( 12.5f, *curve.evaluate( 1.0f ), 1e-4f );
    curve._listKey[0]._easing = BlendCurve::SmoothStep;
    SW_EXPECT_NEAR( 15.0f, *curve.evaluate( 1.0f ), 1e-4f );
    SW_EXPECT_NEAR( 10.0f + 10.0f * 0.15625f, *curve.evaluate( 0.5f ), 1e-4f ); // 3t² - 2t³, t 0.25
    curve._listKey[0]._easing = BlendCurve::Cut;
    SW_EXPECT_NEAR( 20.0f, *curve.evaluate( 0.1f ), 1e-4f );
    SW_EXPECT_NEAR( 10.0f, *curve.evaluate( 0.0f ), 1e-4f );
}
