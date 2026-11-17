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

/** @brief [FloatCurveKeysTest] 자동 접선은 가운데 키가 앞뒤 키를 잇는 기울기, 끝 키가 0 이다. 손으로 정한 접선은 그대로다 */
SW_TEST_CASE( FloatCurveKeysTest, AutoTangentsFollowNeighbours )
{
    FloatCurve curve;
    (void)curve.addKey( 0.0f, 0.0f );
    (void)curve.addKey( 1.0f, 5.0f );
    (void)curve.addKey( 2.0f, 4.0f );
    SW_EXPECT_NEAR( 0.0f, curve._listKey[0]._leaveTangent, 1e-6f );
    SW_EXPECT_NEAR( 2.0f, curve._listKey[1]._arriveTangent, 1e-6f ); // (4 - 0) / (2 - 0)
    SW_EXPECT_NEAR( 2.0f, curve._listKey[1]._leaveTangent, 1e-6f );
    SW_EXPECT_NEAR( 0.0f, curve._listKey[2]._arriveTangent, 1e-6f );

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
