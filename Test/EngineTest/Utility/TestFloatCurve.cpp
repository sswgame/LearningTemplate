#include "pch.h"

#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Utility/FloatCurve.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct TestFloatCurveInternal
    {
        static constexpr float32 kTolerance = 1e-4f;

        /** @brief 키 둘((0, 0) · (2, 4))에 보간 하나를 단 커브입니다. */
        static FloatCurve makeTwoKeys( CurveInterpolation interpolation )
        {
            FloatCurve curve;
            curve.addKey( 0.0f, 0.0f, interpolation );
            curve.addKey( 2.0f, 4.0f, interpolation );
            return curve;
        }
    };
} // namespace

/**
 * @brief [FloatCurveTest] 키가 없으면 대체값, 첫 키 앞 · 끝 키 뒤는 끝 값이다
 */
SW_TEST_CASE( FloatCurveTest, EmptyUsesFallbackAndEndsHold )
{
    FloatCurve empty;
    SW_EXPECT_NEAR_EQUAL( 7.0f, empty.evaluate( 1.0f, 7.0f ), TestFloatCurveInternal::kTolerance );
    const FloatCurve curve = TestFloatCurveInternal::makeTwoKeys( CurveInterpolation::Linear );
    SW_EXPECT_NEAR_EQUAL( 0.0f, curve.evaluate( -5.0f ), TestFloatCurveInternal::kTolerance );
    SW_EXPECT_NEAR_EQUAL( 4.0f, curve.evaluate( 9.0f ), TestFloatCurveInternal::kTolerance );
}

/**
 * @brief [FloatCurveTest] Constant 는 앞 키 값, Linear 는 직선이다
 */
SW_TEST_CASE( FloatCurveTest, ConstantAndLinear )
{
    const FloatCurve constant = TestFloatCurveInternal::makeTwoKeys( CurveInterpolation::Constant );
    SW_EXPECT_NEAR_EQUAL( 0.0f, constant.evaluate( 1.5f ), TestFloatCurveInternal::kTolerance );
    const FloatCurve linear = TestFloatCurveInternal::makeTwoKeys( CurveInterpolation::Linear );
    SW_EXPECT_NEAR_EQUAL( 1.0f, linear.evaluate( 0.5f ), TestFloatCurveInternal::kTolerance );
    SW_EXPECT_NEAR_EQUAL( 3.0f, linear.evaluate( 1.5f ), TestFloatCurveInternal::kTolerance );
}

/**
 * @brief [FloatCurveTest] Cubic 은 에르미트다 — 접선이 0 이면 가운데가 평균이고, 접선을 주면 손으로 센 값과 같다
 */
SW_TEST_CASE( FloatCurveTest, CubicIsHermite )
{
    FloatCurve curve = TestFloatCurveInternal::makeTwoKeys( CurveInterpolation::Cubic );
    // 끝 키 둘의 자동 접선은 평평(0)이다 — 가운데는 (0 + 4) / 2, 1/4 지점은 h01(0.25) × 4 = 0.15625 × 4.
    SW_EXPECT_NEAR_EQUAL( 2.0f, curve.evaluate( 1.0f ), TestFloatCurveInternal::kTolerance );
    SW_EXPECT_NEAR_EQUAL( 0.625f, curve.evaluate( 0.5f ), TestFloatCurveInternal::kTolerance );
    // 접선 1 · 1, t = 0.25: h10 = 0.140625, h01 = 0.15625, h11 = -0.046875 → 0.140625 × 2 + 0.15625 × 4 - 0.046875 × 2 = 0.8125.
    curve._listKey[0]._bAutoTangent  = false;
    curve._listKey[0]._leaveTangent  = 1.0f;
    curve._listKey[1]._bAutoTangent  = false;
    curve._listKey[1]._arriveTangent = 1.0f;
    SW_EXPECT_NEAR_EQUAL( 2.0f, curve.evaluate( 1.0f ), TestFloatCurveInternal::kTolerance );
    SW_EXPECT_NEAR_EQUAL( 0.8125f, curve.evaluate( 0.5f ), TestFloatCurveInternal::kTolerance );
}

/**
 * @brief [FloatCurveTest] 가운데 키의 자동 접선은 이웃 기울기, 끝 키는 평평하다 · addKey 는 시각 순 자리에 넣는다
 */
SW_TEST_CASE( FloatCurveTest, AutoTangentsAndKeyOrder )
{
    FloatCurve curve;
    curve.addKey( 0.0f, 0.0f );
    curve.addKey( 4.0f, 8.0f );
    const uint32 middle = curve.addKey( 2.0f, 1.0f );
    SW_ASSERT_EQUAL( middle, 1u );
    SW_ASSERT_EQUAL( curve._listKey.size(), size_t{ 3 } );
    SW_EXPECT_NEAR_EQUAL( 2.0f, curve._listKey[1]._leaveTangent, TestFloatCurveInternal::kTolerance );
    SW_EXPECT_NEAR_EQUAL( 2.0f, curve._listKey[1]._arriveTangent, TestFloatCurveInternal::kTolerance );
    SW_EXPECT_NEAR_EQUAL( 0.0f, curve._listKey[0]._leaveTangent, TestFloatCurveInternal::kTolerance );
    SW_EXPECT_NEAR_EQUAL( 0.0f, curve._listKey[2]._arriveTangent, TestFloatCurveInternal::kTolerance );
}

/**
 * @brief [FloatCurveTest] 값 범위는 키 값에 곡선의 넘침까지 담는다
 */
SW_TEST_CASE( FloatCurveTest, ValueRangeIncludesOvershoot )
{
    FloatCurve curve                = TestFloatCurveInternal::makeTwoKeys( CurveInterpolation::Cubic );
    curve._listKey[0]._bAutoTangent = false;
    curve._listKey[0]._leaveTangent = 20.0f; // 처음에 가파르게 올라 끝 값을 넘는다
    float32 minValue{ 0.0f };
    float32 maxValue{ 0.0f };
    SW_ASSERT_TRUE( curve.computeValueRange( minValue, maxValue ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, minValue, TestFloatCurveInternal::kTolerance );
    SW_EXPECT_TRUE( maxValue > 4.0f );
    FloatCurve empty;
    SW_EXPECT_FALSE( empty.computeValueRange( minValue, maxValue ) );
}

/**
 * @brief [FloatCurveTest] JSON 왕복이 키를 그대로 둔다(`_listKey` 는 JSON 배열)
 */
SW_TEST_CASE( FloatCurveTest, JSONRoundTrip )
{
    FloatCurve source                 = TestFloatCurveInternal::makeTwoKeys( CurveInterpolation::Linear );
    source._listKey[1]._bAutoTangent  = false;
    source._listKey[1]._arriveTangent = -3.5f;
    const string json                 = JSONSerializer::serialize( &source, *FloatCurve::StaticType() );
    SW_EXPECT_TRUE( json.find( "\"_listKey\":[" ) != string::npos );
    FloatCurve parsed;
    SW_ASSERT_TRUE( JSONSerializer::deserialize( &parsed, *FloatCurve::StaticType(), json ) );
    SW_ASSERT_EQUAL( parsed._listKey.size(), size_t{ 2 } );
    SW_EXPECT_NEAR_EQUAL( 2.0f, parsed._listKey[1]._time, TestFloatCurveInternal::kTolerance );
    SW_EXPECT_NEAR_EQUAL( -3.5f, parsed._listKey[1]._arriveTangent, TestFloatCurveInternal::kTolerance );
    SW_EXPECT_TRUE( parsed._listKey[1]._interpolation == CurveInterpolation::Linear );
    SW_EXPECT_FALSE( parsed._listKey[1]._bAutoTangent );
}
