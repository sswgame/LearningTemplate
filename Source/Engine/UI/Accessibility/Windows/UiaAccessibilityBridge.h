/**
 * @file UiaAccessibilityBridge.h
 * @brief Windows UI Automation 으로 런타임 UI 를 내레이터 같은 스크린 리더에 잇는 브리지입니다.
 * @details 창의 `WM_GETOBJECT`(접근성 클라이언트의 요청)에 UIA 제공자 트리로 답한다. 제공자는 위젯을 쥐지 않고 (화면 핸들, 위젯 번호)로 찾아 값을 그때그때 읽는다 —
 *          위젯이 사라진 제공자는 "요소 없음"(`UIA_E_ELEMENTNOTAVAILABLE`)으로 답한다. 접근성 클라이언트가 안 붙어 있으면 아무 일도 하지 않는다(`UiaClientsAreListening`).
 *          UIA 가 메인 스레드로 옮겨 부르도록(`ProviderOptions_UseComThreading`) 해 두어 UI 상태를 읽고 쓰는 호출은 모두 메인(게임) 스레드에서 일어난다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Accessibility/IUIAccessibilityBridge.h"
#include "Engine/UI/Accessibility/UIAccessibilityTypes.h"

namespace sw
{
    class IWindow;
    class UISystem;

    /**
     * @struct UiaAccessibility
     * @brief Windows UI Automation 연결의 순수 부분과 브리지 만들기입니다.
     */
    struct SW_API UiaAccessibility
    {
        /** @brief 역할에 해당하는 UIA 컨트롤 타입 번호입니다(`UIA_ButtonControlTypeId` 등. 역할 `None` 은 0). */
        [[nodiscard]] static int32 getControlType( AccessibleRole role );
        /**
         * @brief @p window 의 `WM_GETOBJECT` 에 답하는 브리지를 만들어 창에 훅을 겁니다. 브리지를 지우면(또는 UI 시스템에서 빼면) 훅이 풀린다.
         * @details 창과 UI 시스템은 브리지보다 오래 살아야 한다(호스트가 UI 시스템에서 브리지를 먼저 뺀다).
         */
        [[nodiscard]] static unique_ptr<IUIAccessibilityBridge> createBridge( UISystem& ui, IWindow& window );
    };
} // namespace sw
