/**
 * @file UIAccessibilityActions.h
 * @brief 스크린 리더가 위젯에 시키는 일(누르기 · 포커스 · 값 바꾸기)을 UI 입력 경로로 보냅니다.
 * @details 플랫폼 브리지(UI Automation의 Invoke · SetFocus · RangeValue.SetValue)가 부르는 한 곳이다. 마우스나 키보드와 같은 길(액션 이벤트 · 포커스 관리자 · 사용자 입력 세터)을 타므로
 *          바인딩 되쓰기 · 명령 · 소리가 사람이 누른 것과 같다. 위젯은 (화면 핸들, 위젯 번호)로 가리킨다 — 스크린 리더가 쥐고 있는 사이 지워질 수 있어 포인터를 쥐지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/UI/Base/WidgetTypes.h"

namespace sw
{
    class UISystem;

    /**
     * @struct UIAccessibilityActions
     * @brief 스크린 리더가 시키는 일입니다. 모두 게임 스레드에서 부릅니다.
     */
    struct SW_API UIAccessibilityActions
    {
        /**
         * @brief 위젯을 "누른다" — `UI.Accept` 액션을 그 위젯까지의 경로로 보냅니다(단추 클릭, 체크 상자 켜고 끄기).
         * @return 위젯이 없거나 꺼졌거나 아무도 처리하지 않으면 false 입니다.
         */
        [[nodiscard]] static bool invoke( UISystem& ui, uint32 screen, WidgetID widget );
        /** @brief 위젯으로 포커스를 옮깁니다. 받을 수 없는 위젯이거나 없으면 false 입니다. */
        [[nodiscard]] static bool focus( UISystem& ui, uint32 screen, WidgetID widget );
        /** @brief 슬라이더 값을 사용자 입력처럼 바꿉니다(범위로 제한된다). 슬라이더가 아니거나 꺼졌거나 없으면 false 입니다. */
        [[nodiscard]] static bool setRangeValue( UISystem& ui, uint32 screen, WidgetID widget, float32 value );
    };
} // namespace sw
