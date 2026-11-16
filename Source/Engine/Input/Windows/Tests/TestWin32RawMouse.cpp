#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Input/Windows/Win32RawMouse.h"

// Win32RawMouseTest - WM_INPUT 의 원시 마우스 보고를 이동량으로 읽는 규칙(nogpu, 감사 P-1). `GetRawInputData` 는 진짜 핸들만 받아서
// 가짜 보고(`RAWMOUSE`)를 직접 만들어 같은 읽기 함수에 넣는다. 등록(`RegisterRawInputDevices`)과 실제 장치 보고는 이 테스트가 보지 못한다.

using namespace sw;

namespace
{
    struct Win32RawMouseTestUtil
    {
        static RAWMOUSE makeReport( USHORT flags, LONG lastX, LONG lastY )
        {
            RAWMOUSE mouse{};
            mouse.usFlags = flags;
            mouse.lLastX  = lastX;
            mouse.lLastY  = lastY;
            return mouse;
        }

        /** @brief 테스트용 고정 화면(1920 × 1080, 원점 0) — 절대 보고는 이 사각형에 맞춘다. */
        static RECT makeArea() { return RECT{ 0, 0, 1920, 1080 }; }
    };
} // namespace

/** @brief [Win32RawMouseTest] 상대 보고(보통 마우스)는 그대로 픽셀 이동이고, 절대 기준점을 비운다 */
SW_TEST_CASE( Win32RawMouseTest, RelativeReportIsTheMotion )
{
    using Util = Win32RawMouseTestUtil;
    optional<int2> lastAbsolute{
        int2{ 5, 5 }
    };
    const float2 motion = Win32RawMouseUtil::readMotion( Util::makeReport( MOUSE_MOVE_RELATIVE, -7, 3 ), Util::makeArea(), lastAbsolute );
    SW_EXPECT_NEAR( -7.0f, motion._x, 0.0001f );
    SW_EXPECT_NEAR( 3.0f, motion._y, 0.0001f );
    SW_EXPECT_FALSE( lastAbsolute.has_value() );
}

/** @brief [Win32RawMouseTest] 절대 보고(원격 데스크톱 · 가상 머신)는 정규 좌표 0..65535 를 픽셀로 바꿔 직전 보고와의 차이다. 첫 보고는 기준점만 잡는다 */
SW_TEST_CASE( Win32RawMouseTest, AbsoluteReportIsTheDifferenceFromTheLastReport )
{
    using Util = Win32RawMouseTestUtil;
    optional<int2> lastAbsolute;
    const float2   first = Win32RawMouseUtil::readMotion( Util::makeReport( MOUSE_MOVE_ABSOLUTE, 32768, 32768 ), Util::makeArea(), lastAbsolute );
    SW_EXPECT_NEAR( 0.0f, first._x, 0.0001f ); // 정규 좌표를 상대 이동으로 읽으면 수만 픽셀이 튄다
    SW_EXPECT_NEAR( 0.0f, first._y, 0.0001f );
    SW_REQUIRE_TRUE( lastAbsolute.has_value() );

    const float2 second = Win32RawMouseUtil::readMotion( Util::makeReport( MOUSE_MOVE_ABSOLUTE, 65535, 0 ), Util::makeArea(), lastAbsolute );
    SW_EXPECT_NEAR( 1920.0f - static_cast<float32>( MulDiv( 32768, 1920, 65535 ) ), second._x, 0.0001f );
    SW_EXPECT_NEAR( -static_cast<float32>( MulDiv( 32768, 1080, 65535 ) ), second._y, 0.0001f );
}

/** @brief [Win32RawMouseTest] 가짜 보고를 읽어 큐에 넣으면(WM_INPUT 처리기와 같은 길) 한 프레임의 이동량으로 쌓인다 */
SW_TEST_CASE( Win32RawMouseTest, FakeReportsAccumulateIntoTheFrame )
{
    using Util = Win32RawMouseTestUtil;
    InputManager input;
    SW_REQUIRE_TRUE( input.initialize() );
    SW_REQUIRE_NOT_NULL( input.getMouse() );

    optional<int2> lastAbsolute;
    const RAWMOUSE arrReport[]{ Util::makeReport( MOUSE_MOVE_RELATIVE, 4, -1 ), Util::makeReport( MOUSE_MOVE_RELATIVE, 6, 2 ), Util::makeReport( MOUSE_MOVE_RELATIVE, -3, 0 ) };
    for ( const RAWMOUSE& report : arrReport )
    {
        const float2 motion = Win32RawMouseUtil::readMotion( report, Util::makeArea(), lastAbsolute );
        SW_REQUIRE_TRUE( input.postRawEvent( RawInputEvent::makeMouseRawDelta( motion._x, motion._y ) ) );
    }
    input.beginFrame();
    SW_EXPECT_NEAR( 7.0f, input.getMouse()->getMovementDelta()._x, 0.0001f );
    SW_EXPECT_NEAR( 1.0f, input.getMouse()->getMovementDelta()._y, 0.0001f );
    input.endFrame();
    input.shutdown();
}
