/**
 * @file Win32RawMouse.h
 * @brief Win32 원시 마우스 보고(`RAWMOUSE`)를 픽셀 이동량으로 읽습니다(WM_INPUT).
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct Win32RawMouseUtil
     * @brief 외부 형식 `RAWMOUSE` 를 읽는 함수 묶음입니다. 등록 · 해제는 `InputManagerWin32.cpp` 가 잠금과 함께 한다.
     * @details 상대 보고(보통 마우스)는 그대로, 절대 보고(원격 데스크톱 · 가상 머신 · 펜 — `MOUSE_MOVE_ABSOLUTE`)는 0..65535 정규 좌표를
     *          화면 픽셀로 바꿔 직전 보고와의 차이를 낸다(언리얼 `FWindowsApplication` 의 WM_INPUT 처리와 같은 규칙).
     */
    struct Win32RawMouseUtil
    {
        /** @brief 절대 보고의 정규 좌표 최댓값입니다(`RAWMOUSE` 문서 — 0..65535). */
        static constexpr LONG kAbsoluteRange = 65535;

        /**
         * @brief 절대 보고가 가리키는 화면 사각형입니다. `MOUSE_VIRTUAL_DESKTOP` 이면 가상 데스크톱 전체, 아니면 주 모니터입니다.
         * @return `left` · `top` 은 원점, `right` · `bottom` 은 너비 · 높이입니다(`RAWMOUSE` 문서 예제와 같은 모양).
         */
        [[nodiscard]] static RECT getAbsoluteArea( const RAWMOUSE& mouse )
        {
            if ( ( mouse.usFlags & MOUSE_VIRTUAL_DESKTOP ) != 0 )
                return RECT{ GetSystemMetrics( SM_XVIRTUALSCREEN ), GetSystemMetrics( SM_YVIRTUALSCREEN ), GetSystemMetrics( SM_CXVIRTUALSCREEN ), GetSystemMetrics( SM_CYVIRTUALSCREEN ) };
            return RECT{ 0, 0, GetSystemMetrics( SM_CXSCREEN ), GetSystemMetrics( SM_CYSCREEN ) };
        }

        /**
         * @brief 보고 하나의 이동량(픽셀)입니다.
         * @param absoluteArea 절대 보고일 때만 읽는 화면 사각형입니다(`getAbsoluteArea`). 테스트가 고정 값을 넣는다.
         * @param inoutLastAbsolute 직전 절대 보고의 픽셀 좌표입니다. 첫 절대 보고는 기준점이 없어 이동 0 이다(시작할 때 시점이 튀지 않게).
         *        상대 보고가 오면 비운다 — 장치가 바뀐 뒤의 첫 절대 보고도 기준점 없이 시작한다.
         */
        [[nodiscard]] static float2 readMotion( const RAWMOUSE& mouse, const RECT& absoluteArea, optional<int2>& inoutLastAbsolute )
        {
            if ( ( mouse.usFlags & MOUSE_MOVE_ABSOLUTE ) == 0 )
            {
                inoutLastAbsolute = nullopt;
                return float2{ static_cast<float32>( mouse.lLastX ), static_cast<float32>( mouse.lLastY ) };
            }

            const int2 absolute{ static_cast<int32>( MulDiv( mouse.lLastX, absoluteArea.right, kAbsoluteRange ) + absoluteArea.left ),
                                 static_cast<int32>( MulDiv( mouse.lLastY, absoluteArea.bottom, kAbsoluteRange ) + absoluteArea.top ) };
            float2     motion{};
            if ( inoutLastAbsolute.has_value() )
                motion = float2{ static_cast<float32>( absolute._x - inoutLastAbsolute->_x ), static_cast<float32>( absolute._y - inoutLastAbsolute->_y ) };
            inoutLastAbsolute = absolute;
            return motion;
        }
    };
} // namespace sw
