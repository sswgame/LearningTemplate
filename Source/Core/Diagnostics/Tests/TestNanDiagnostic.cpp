// NanDiagnostic — 배열에서 처음 유한하지 않은 값 찾기(실수 · 수학 형식 원소), 개발 구성의 NaN 알림 매크로.
#include "pch.h"

#include "Core/Diagnostics/NanDiagnostic.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

/**
 * @brief [NanDiagnosticTest] findFirstNonFinite 는 처음 NaN · 무한대의 번호를, 모두 유한하면 개수(끝)를 낸다 — 빈 배열도 끝이다
 */
SW_TEST_CASE( NanDiagnosticTest, FindFirstNonFiniteReturnsIndexOrCount )
{
    const float32 nan         = std::numeric_limits<float32>::quiet_NaN();
    const float32 infinity    = std::numeric_limits<float32>::infinity();
    const float32 arrFinite[] = { 0.0f, -1.0f, 3.5f, std::numeric_limits<float32>::max(), -0.0f };
    const float32 arrNan[]    = { 0.0f, 1.0f, nan, 2.0f, infinity };
    const float32 arrNegInf[] = { -infinity };
    SW_EXPECT_EQUAL( static_cast<size_t>( 5 ), NanDiagnostic::findFirstNonFinite( arrFinite, 5 ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), NanDiagnostic::findFirstNonFinite( arrNan, 5 ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), NanDiagnostic::findFirstNonFinite( arrNegInf, 1 ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), NanDiagnostic::findFirstNonFinite( arrFinite, 0 ) );
}

/**
 * @brief [NanDiagnosticTest] 수학 형식 배열은 원소 번호를 낸다 — 행렬 셋째의 마지막 성분이 NaN 이면 2, 쿼터니언 둘째의 w 가 무한대면 1
 */
SW_TEST_CASE( NanDiagnosticTest, FindFirstNonFiniteCountsElements )
{
    float4x4 arrMatrix[] = { float4x4::Identity, float4x4::Identity, float4x4::Identity };
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), NanDiagnostic::findFirstNonFinite( arrMatrix, 3 ) );
    arrMatrix[2]._44 = std::numeric_limits<float32>::quiet_NaN();
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), NanDiagnostic::findFirstNonFinite( arrMatrix, 3 ) );

    quaternion arrRotation[] = { quaternion::Identity, quaternion::Identity };
    arrRotation[1]._w        = std::numeric_limits<float32>::infinity();
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), NanDiagnostic::findFirstNonFinite( arrRotation, 2 ) );

    const float3 arrPosition[] = { float3::Zero, float3::UnitX };
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), NanDiagnostic::findFirstNonFinite( arrPosition, 2 ) );
}

/**
 * @brief [NanDiagnosticTest] SW_DIAGNOSTIC_CHECK_NAN_ID 는 유한하지 않은 값에서만 Error 를 남긴다(개발 구성) — 유한한 값은 조용하다
 */
SW_TEST_CASE( NanDiagnosticTest, CheckLogsOnlyForNonFiniteValues )
{
#if defined( SW_DO_CHECKS )
    SW_EXPECT_LOG( "NanDiagnosticTest.NonFinite" ); // 이 테스트가 보려는 알림이다
#endif
    const string_view name = "Crate";
    SW_DIAGNOSTIC_CHECK_NAN_ID( "NanDiagnosticTest.NonFinite", float3( 0.0f, std::numeric_limits<float32>::quiet_NaN(), 0.0f ), name, "'%#' is not finite", name.data() );
    SW_DIAGNOSTIC_CHECK_NAN_ID( "NanDiagnosticTest.Finite", float3::UnitY, name, "'%#' must stay quiet", name.data() );
}
