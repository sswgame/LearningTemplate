#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Actor/Camera/CameraPose.h"
#include "GameFramework/Base/Actor/Camera/PhotoModeCamera.h"

#include "TestFramework/TestFramework.h"

// 포토 모드 자유 카메라 — 켤 때 포즈에서 시작 · 원근 이동과 둘러보기 · 직교는 이동과 줌만 · 렌즈 값 한계 · 끄면 바탕 포즈 그대로.

using namespace sw;

/**
 * @brief [PhotoModeCameraTest] 켜면 바탕 포즈 자리에서 시작하고, 앞으로 1 초 움직이면 이동 속도만큼 +Z 로 가며, 끄면 바탕 포즈를 그대로 낸다
 * @details 끈 뒤에 덮은 값이 남으면 포토 모드를 나온 게임 카메라가 엉뚱한 자리에 선다.
 */
SW_TEST_CASE( PhotoModeCameraTest, MovesFromTheBasePoseAndRestoresOnEnd )
{
    CameraPose base;
    base._position     = float3{ 1.0f, 2.0f, 3.0f };
    base._fieldOfViewY = 0.8f;

    PhotoModeCamera photoMode;
    photoMode.begin( base );
    SW_EXPECT_TRUE( photoMode.isActive() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, photoMode.getYaw(), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, photoMode.getPitch(), 1.0e-5f );

    PhotoModeInput input;
    input._move = float2{ 0.0f, 1.0f };
    photoMode.applyInput( input, 1.0f );
    const float32    speed = photoMode.getSettings()._moveSpeed;
    const CameraPose moved = photoMode.makePose( base );
    SW_EXPECT_NEAR_EQUAL( 1.0f, moved._position._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, moved._position._y, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 3.0f + speed, moved._position._z, 1.0e-4f );

    photoMode.end();
    SW_EXPECT_FALSE( photoMode.isActive() );
    const CameraPose restored = photoMode.makePose( base );
    SW_EXPECT_NEAR_EQUAL( 3.0f, restored._position._z, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.8f, restored._fieldOfViewY, 1.0e-6f );
}

/**
 * @brief [PhotoModeCameraTest] 줌 · 노출 · 피치는 한계 밖으로 나가지 않고, 노출은 바탕 값에 더해진다
 */
SW_TEST_CASE( PhotoModeCameraTest, LensValuesStayInRange )
{
    CameraPose base;
    base._fieldOfViewY                     = 0.8f;
    base._postProcess._exposureCompensation = 0.5f;

    PhotoModeCamera photoMode;
    photoMode.begin( base );
    const PhotoModeSettings& settings = photoMode.getSettings();

    PhotoModeInput input;
    input._zoomNotches   = 100.0f;
    input._exposureSteps = 100.0f;
    input._look          = float2{ 0.0f, 100000.0f };
    photoMode.applyInput( input, 0.0f );
    SW_EXPECT_NEAR_EQUAL( settings._minFieldOfViewY, photoMode.getFieldOfViewY(), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( settings._maxExposureOffset, photoMode.getExposureOffset(), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( settings._pitchLimit, photoMode.getPitch(), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f + settings._maxExposureOffset, photoMode.makePose( base )._postProcess._exposureCompensation, 1.0e-5f );

    input._zoomNotches = -100.0f;
    photoMode.applyInput( input, 0.0f );
    SW_EXPECT_NEAR_EQUAL( settings._maxFieldOfViewY, photoMode.getFieldOfViewY(), 1.0e-5f );
}

/**
 * @brief [PhotoModeCameraTest] 직교(2D) 시점은 화면 오른쪽 · 위로만 움직이고 둘러보기와 오르내림은 무시하며, 줌은 화면 높이를 바꾼다
 */
SW_TEST_CASE( PhotoModeCameraTest, OrthographicMovesOnTheScreenPlaneAndZoomsHeight )
{
    CameraPose base;
    base._bOrthographic = SW_TRUE;
    base._orthoHeight   = 10.0f;

    PhotoModeCamera photoMode;
    photoMode.begin( base );
    PhotoModeInput input;
    input._move = float2{ 1.0f, 1.0f };
    input._rise = 1.0f;
    input._look = float2{ 500.0f, 500.0f };
    photoMode.applyInput( input, 1.0f );
    const float32 distance = photoMode.getSettings()._orthoPanScreensPerSecond * 10.0f;
    SW_EXPECT_NEAR_EQUAL( distance, photoMode.getPosition()._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( distance, photoMode.getPosition()._y, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, photoMode.getPosition()._z, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, photoMode.getYaw(), 1.0e-5f );

    PhotoModeInput zoom;
    zoom._zoomNotches = 1.0f;
    photoMode.applyInput( zoom, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 10.0f * photoMode.getSettings()._zoomStep, photoMode.getOrthoHeight(), 1.0e-4f );
}
