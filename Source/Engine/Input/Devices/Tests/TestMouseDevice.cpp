#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/RawInputEvent.h"

// MouseDeviceTest - 이동량의 원천 규칙(nogpu): 원시 원천은 값이 아니라 출처(플랫폼 등록 뒤 온 원시 보고 · 그 프레임의 원시 이벤트)로 정한다(감사 P-1).

using namespace sw;

/** @brief [MouseDeviceTest] 원천 선언이 없고 원시 이벤트도 없는 프레임은 위치 차이가 이동이다 */
SW_TEST_CASE( MouseDeviceTest, PositionDeltaWithoutRawSource )
{
    MouseDevice mouse;
    mouse.setPosition( 100, 100 ); // 첫 위치는 기준점만 잡는다
    mouse.onFrameBegin( 0.016f );
    mouse.setPosition( 110, 95 );
    SW_EXPECT_NEAR( 10.0f, mouse.getMovementDelta()._x, 0.0001f );
    SW_EXPECT_NEAR( -5.0f, mouse.getMovementDelta()._y, 0.0001f );
}

/**
 * @brief [MouseDeviceTest] 등록만 하고 원시 보고가 오지 않으면 위치 차이가 이동이고, 첫 원시 보고 뒤로는 원시 델타만 쌓여 보고 없는 프레임에도 위치 차이로 넘어가지 않는다
 * @details 원격 데스크톱 · 원격 제어 도구 · 터치가 흉내 낸 마우스는 등록이 성공해도 WM_INPUT 없이 커서만 옮긴다 — 등록 성공만 믿으면 잠금 중 시선이 멈췄다(감사 P-1).
 */
SW_TEST_CASE( MouseDeviceTest, RawSourceNeedsAReportBeforeIgnoringPositionDelta )
{
    MouseDevice mouse;
    mouse.setPosition( 100, 100 );
    mouse.setRawMotionSource( true );
    SW_EXPECT_TRUE( mouse.isRawMotionSource() );
    SW_EXPECT_FALSE( mouse.isRawMotionConfirmed() );

    mouse.onFrameBegin( 0.016f );
    mouse.setPosition( 140, 100 ); // 등록했지만 원시 보고가 없는 환경 — 커서 이동이 시선이다
    SW_EXPECT_NEAR( 40.0f, mouse.getMovementDelta()._x, 0.0001f );

    mouse.onFrameBegin( 0.016f );
    mouse.addRawDelta( 3.0f, -2.0f );
    mouse.addRawDelta( 4.0f, 1.0f );
    SW_EXPECT_TRUE( mouse.isRawMotionConfirmed() );
    SW_EXPECT_NEAR( 7.0f, mouse.getMovementDelta()._x, 0.0001f );
    SW_EXPECT_NEAR( -1.0f, mouse.getMovementDelta()._y, 0.0001f );

    mouse.onFrameBegin( 0.016f );
    mouse.setPosition( 150, 100 ); // 확정된 뒤 보고 없는 프레임의 커서 이동(가속 · 가운데 되돌림)은 섞지 않는다
    SW_EXPECT_NEAR( 0.0f, mouse.getMovementDelta()._x, 0.0001f );

    mouse.resetState(); // 포커스 리셋은 등록 상태를 지우지 않는다 — 등록은 잠금 해제(`releaseMouseLockMode`)가 푼다
    SW_EXPECT_TRUE( mouse.isRawMotionConfirmed() );
    mouse.setRawMotionSource( false );
    SW_EXPECT_FALSE( mouse.isRawMotionConfirmed() );
    mouse.onFrameBegin( 0.016f );
    mouse.setPosition( 160, 100 );
    SW_EXPECT_NEAR( 10.0f, mouse.getMovementDelta()._x, 0.0001f );

    mouse.setRawMotionSource( true ); // 다시 등록하면 지난 등록의 확정을 물려받지 않는다
    mouse.onFrameBegin( 0.016f );
    mouse.setPosition( 165, 100 );
    SW_EXPECT_NEAR( 5.0f, mouse.getMovementDelta()._x, 0.0001f );
}

/** @brief [MouseDeviceTest] 원시 이벤트가 온 프레임은 합이 0 이어도 원시 원천이다 — 옛 "0 이면 위치 차이" 폴백이 커서 이동을 섞던 자리 */
SW_TEST_CASE( MouseDeviceTest, RawEventFrameWithZeroSumStaysRaw )
{
    MouseDevice mouse;
    mouse.setPosition( 0, 0 );
    mouse.onFrameBegin( 0.016f );
    mouse.addRawDelta( 2.0f, 0.0f );
    mouse.addRawDelta( -2.0f, 0.0f );
    mouse.setPosition( 25, 0 );
    SW_EXPECT_NEAR( 0.0f, mouse.getMovementDelta()._x, 0.0001f );

    mouse.onFrameBegin( 0.016f ); // 다음 프레임에는 원시 이벤트 표시가 지워진다
    mouse.setPosition( 30, 0 );
    SW_EXPECT_NEAR( 5.0f, mouse.getMovementDelta()._x, 0.0001f );
}

/** @brief [MouseDeviceTest] 큐에 넣은 원시 이동 이벤트는 한 프레임에 모두 쌓여 장치 이동량과 시선 누적이 된다 */
SW_TEST_CASE( MouseDeviceTest, QueuedRawEventsAccumulate )
{
    InputManager input;
    SW_REQUIRE_TRUE( input.initialize() );
    SW_REQUIRE_NOT_NULL( input.getMouse() );

    SW_REQUIRE_TRUE( input.postRawEvent( RawInputEvent::makeMouseRawDelta( 1.0f, 2.0f ) ) );
    SW_REQUIRE_TRUE( input.postRawEvent( RawInputEvent::makeMouseRawDelta( 3.0f, -1.0f ) ) );
    SW_REQUIRE_TRUE( input.postRawEvent( RawInputEvent::makeMouseRawDelta( -0.5f, 4.0f ) ) );
    input.beginFrame();
    SW_EXPECT_NEAR( 3.5f, input.getMouse()->getMovementDelta()._x, 0.0001f );
    SW_EXPECT_NEAR( 5.0f, input.getMouse()->getMovementDelta()._y, 0.0001f );
    SW_EXPECT_NEAR( 3.5f, input.getFrameLookDelta()._x, 0.0001f );
    SW_EXPECT_NEAR( 5.0f, input.getFrameLookDelta()._y, 0.0001f );
    input.endFrame();
    input.shutdown();
}
