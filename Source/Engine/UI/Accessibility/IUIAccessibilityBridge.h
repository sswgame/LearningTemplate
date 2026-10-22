/**
 * @file IUIAccessibilityBridge.h
 * @brief 런타임 UI 를 운영체제의 접근성 API(Windows UI Automation)에 잇는 플랫폼 쪽 계약입니다.
 * @details 브리지는 `UISystem` 이 가진다(`UISystem::setAccessibilityBridge`). 스크린 리더(내레이터 등)가 창에 붙으면 브리지가 접근성 트리(`UISystem::populateAccessibilitySnapshot`)를
 *          읽어 보여 주고, UI 는 포커스가 옮겨 갈 때 · 알림이 올라올 때 브리지에 알린다. 붙은 스크린 리더가 없으면(`isClientActive` 가 false) 트리를 뽑지도 않는다 — 평소 비용이 없다.
 *          모든 호출은 메인(게임) 스레드이며, 플랫폼이 다른 스레드에서 묻는 것은 브리지가 메인 스레드로 옮겨 처리한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/UI/Accessibility/UIAccessibilityTypes.h"
#include "Engine/UI/Base/WidgetTypes.h"

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
