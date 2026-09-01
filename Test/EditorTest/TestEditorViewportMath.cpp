#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorCamera.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorViewportMathTest] Orbit 회전 시 Pitch 각도 [-89도, +89도] 클램핑 및 짐벌 락/뷰 반전 방어 검증
 */
SW_TEST_CASE( EditorViewportMathTest, OrbitPitchClampingPreventsGimbalInversion )
{
	constexpr float32 kMinPitchRad = -89.0f * MathUtil::DegreeToRadian;
	constexpr float32 kMaxPitchRad = 89.0f * MathUtil::DegreeToRadian;

	// 극단적인 피치 입력 (+120도, -150도) 시뮬레이션
	float32 rawPitchPositive	 = 120.0f * MathUtil::DegreeToRadian;
	float32 clampedPitchPositive = MathUtil::clamp( rawPitchPositive, kMinPitchRad, kMaxPitchRad );
	SW_EXPECT_NEAR_EQUAL( kMaxPitchRad, clampedPitchPositive, 1e-4f );

	float32 rawPitchNegative	 = -150.0f * MathUtil::DegreeToRadian;
	float32 clampedPitchNegative = MathUtil::clamp( rawPitchNegative, kMinPitchRad, kMaxPitchRad );
	SW_EXPECT_NEAR_EQUAL( kMinPitchRad, clampedPitchNegative, 1e-4f );

	// 정상 범위 내 입력은 유지
	float32 normalPitch	  = 45.0f * MathUtil::DegreeToRadian;
	float32 clampedNormal = MathUtil::clamp( normalPitch, kMinPitchRad, kMaxPitchRad );
	SW_EXPECT_NEAR_EQUAL( normalPitch, clampedNormal, 1e-4f );
}

/**
 * @brief [EditorViewportMathTest] 뷰포트 종횡비 및 투영 행렬 계산 일관성 검증
 */
SW_TEST_CASE( EditorViewportMathTest, ViewportProjectionMatrixAspectScaling )
{
	float32 fovY  = 60.0f * MathUtil::DegreeToRadian;
	float32 nearZ = 0.1f;
	float32 farZ  = 1000.0f;

	float32	 aspectWide = 16.0f / 9.0f;
	float4x4 projWide	= float4x4::createPerspectiveFieldOfView( fovY, aspectWide, nearZ, farZ );

	float32	 aspectSquare = 1.0f;
	float4x4 projSquare	  = float4x4::createPerspectiveFieldOfView( fovY, aspectSquare, nearZ, farZ );

	// 와이드 종횡비의 X 스케일(_11)은 정사각형 대비 작아야 함 (스케일 = 1 / (aspect * tan(fov/2)))
	SW_EXPECT_TRUE( projWide._11 < projSquare._11 );
	SW_EXPECT_NEAR_EQUAL( projWide._22, projSquare._22, 1e-4f ); // Y 스케일은 종횡비와 무관하게 동일
}
