/**
 * @file IUIAccessibilityBridge.h
 * @brief 런타임 UI 를 운영체제의 접근성 API(Windows UI Automation)에 잇는 플랫폼 쪽 계약입니다.
 * @details 브리지는 접근성 서비스가 가진다(`UIAccessibilityService::setBridge`). 스크린 리더(내레이터 등)가 창에 붙으면 브리지가 접근성 트리(`UIAccessibilityService::populateSnapshot`)를
 *          읽어 보여 주고, UI 는 포커스가 옮겨 갈 때 · 알림이 올라올 때 브리지에 알린다. 붙은 스크린 리더가 없으면(`isClientActive` 가 false) 트리를 뽑지도 않는다 — 평소 비용이 없다.
 *          모든 호출은 메인(게임) 스레드이며, 플랫폼이 다른 스레드에서 묻는 것은 브리지가 메인 스레드로 옮겨 처리한다.
 *          이 플랫폼의 브리지를 만드는 팩토리(`UIAccessibilityBridgeFactory`)도 여기 둔다 — 구현은 `UIAccessibilityBridgeFactory.cpp`.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Accessibility/UIAccessibilityTypes.h"
#include "Engine/UI/WidgetTree/WidgetTypes.h"

namespace sw
{
    /**
     * @class IUIAccessibilityBridge
     * @brief 플랫폼 접근성 브리지의 계약입니다.
     */
    class IUIAccessibilityBridge
    {
    public:
        virtual ~IUIAccessibilityBridge() = default;

        /** @brief 스크린 리더 같은 접근성 클라이언트가 지금 붙어 있으면 true 입니다. */
        virtual bool isClientActive() const = 0;
        /** @brief UI 가 한 프레임 갱신됐습니다 — 브리지가 둔 접근성 트리는 낡았습니다. 클라이언트가 붙어 있든 아니든 매 프레임 부릅니다. */
        virtual void onUIUpdated() = 0;
        /** @brief 포커스가 위젯 (@p screen, @p widget) 으로 옮겨 갔습니다(`kInvalidWidgetID` 면 포커스가 없어졌다). 클라이언트가 붙어 있을 때만 부릅니다. */
        virtual void notifyFocusChanged( uint32 screen, WidgetID widget ) = 0;
        /** @brief 글 @p text 를 읽어 달라고 알립니다. 클라이언트가 붙어 있을 때만 부릅니다. */
        virtual void announce( string_view text, UIAnnouncePriority priority ) = 0;
    };
} // namespace sw

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
