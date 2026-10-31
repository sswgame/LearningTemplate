/**
 * @file AtSpiAccessibilityBridge.h
 * @brief Linux AT-SPI(D-Bus `org.a11y.atspi`)로 런타임 UI 를 Orca 같은 스크린 리더에 잇는 브리지입니다.
 * @details 세션 버스의 `org.a11y.Bus` 에서 접근성 버스 주소를 받아 붙고, 접근성 레지스트리에 앱 루트를 등록(`Socket.Embed`)한 뒤, 접근성 트리 스냅샷(`UIAccessibilityTree`)을
 *          `Accessible` · `Component` · `Action` · `Value` 객체로 내보인다. Windows 브리지와 같은 방식이다 — 위젯을 쥐지 않고 (화면 핸들, 위젯 번호)를 객체 경로에 적어 찾고,
 *          붙은 스크린 리더가 없으면(`org.a11y.Status.ScreenReaderEnabled` 가 거짓) 트리를 뽑지 않는다. 모든 처리는 메인(게임) 스레드의 `onUIUpdated` 안에서 D-Bus 를 한 번 돌려 한다 — 별도 스레드가 없다.
 *          구성: `SW_ENABLE_ATSPI` (libdbus-1 필요, cmake/Modules/Platform/Linux.cmake). 꺼져 있으면 이 브리지는 지어지지 않아 등록표가 비고 팩토리가 nullptr 을 준다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Accessibility/IUIAccessibilityBridge.h"

namespace sw
{
    class IWindow;
    class UISystem;

    /**
     * @struct AtSpiAccessibility
     * @brief AT-SPI 연결 만들기입니다.
     */
    struct SW_API AtSpiAccessibility
    {
        /**
         * @brief 접근성 버스에 붙어 앱을 등록한 브리지를 만듭니다. 세션 버스 · 접근성 버스 · 레지스트리가 없으면(헤드리스, 스크린 리더 환경이 아님) 이유를 로그로 남기고 nullptr 입니다.
         * @details nullptr 은 이 기계에 스크린 리더 연결이 없다는 뜻이다 — 호스트는 그대로 진행한다(Windows 브리지와 같다).
         */
        [[nodiscard]] static unique_ptr<IUIAccessibilityBridge> createBridge( UISystem& ui, IWindow& window );
    };
} // namespace sw
