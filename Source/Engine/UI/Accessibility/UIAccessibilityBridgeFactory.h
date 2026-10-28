/**
 * @file UIAccessibilityBridgeFactory.h
 * @brief 이 플랫폼의 접근성 브리지를 만듭니다.
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
     * @struct UIAccessibilityBridgeFactory
     * @brief 창 @p window 의 접근성 요청에 답하는 브리지를 만듭니다.
     */
    struct SW_API UIAccessibilityBridgeFactory
    {
        /**
         * @brief Windows 클라이언트는 UI Automation 브리지를 만들고 창에 훅을 겁니다. 리눅스 클라이언트는 `SW_ENABLE_ATSPI` 로 지었을 때 AT-SPI 브리지를 만들고(접근성 버스가 없으면 nullptr),
         *        그 밖(AT-SPI 를 짓지 않은 구성 · 전용 서버)은 nullptr 입니다.
         * @details nullptr 은 "이 플랫폼에는 스크린 리더 연결이 없다" 는 뜻이다 — 호스트는 그대로 두고 진행한다(UI 시스템은 브리지 없이도 동작한다).
         */
        [[nodiscard]] static unique_ptr<IUIAccessibilityBridge> createForWindow( UISystem& ui, IWindow& window );
    };
} // namespace sw
