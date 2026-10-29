/**
 * @file UIAccessibilityService.h
 * @brief 런타임 UI 를 스크린 리더에 잇는 서비스입니다 — 플랫폼 브리지 소유, 포커스 알림, 읽어 줄 알림, 접근성 트리 뽑기(엔진 범위 서브시스템 `UIAccessibility`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/UI/Accessibility/IUIAccessibilityBridge.h"
#include "Engine/UI/Accessibility/UIAccessibilityTypes.h"
#include "Engine/UI/Screen/UIScreenTypes.h"
#include "Engine/UI/WidgetTree/WidgetTypes.h"

namespace sw
{
    class UIScreen;
    class UISystem;

    struct UIAccessibilitySnapshot;

    /**
     * @class UIAccessibilityService
     * @brief 플랫폼 접근성 브리지를 들고 화면 스택의 포커스 · 알림을 스크린 리더에 전합니다(엔진 범위 서브시스템 `UIAccessibility`, `engine::getSubsystem<UIAccessibilityService>()`).
     * @details 화면 스택(`UISystem`)은 화면을 쌓고 포커스를 옮기는 데까지이고, 그것을 운영체제 접근성 API 로 내보내는 일은 그 위의 서비스다
     *          (언리얼 `FSlateAccessibleMessageHandler` 가 `FSlateApplication` 과 따로 서는 것과 같다). 화면 스택에 의존하고 UI 갱신의 레이아웃 앞 칸
     *          (`UISystem::onUpdateBeforeLayout`)에서 포커스가 옮겨 갔는지 본다. 스크린 리더가 붙어 있을 때만 일한다 — 붙지 않았으면 포커스 알림 · 트리 뽑기 · 알림 전달 모두
     *          아무것도 하지 않는다. 게임 스레드에서만 쓴다.
     */
    class SW_API UIAccessibilityService final : public ISubsystem
    {
    public:
        UIAccessibilityService();
        ~UIAccessibilityService() override;

        // ISubsystem — 화면 스택에 의존한다. 종료는 브리지를 놓는다(창이 먼저 사라지는 호스트는 그 전에 `setBridge( nullptr )` 를 부른다).
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "UIAccessibility";
        SubsystemInitResult          initializeSubsystem() override;
        void                         shutdownSubsystem() override;
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const>      getSubsystemDependencies() const override;
        SubsystemHostMask            getSubsystemHost() const override { return EngineSubsystemHost::kClient; }

        /**
         * @brief 플랫폼 접근성 브리지를 끼웁니다(호스트가 창을 만든 뒤. nullptr 로 빼며, 빼기 전에 브리지는 창에서 자기 훅을 풀어야 한다). 브리지는 이 서비스가 소유합니다.
         * @details 바꾸면 지금 포커스를 다음 갱신에 새로 알린다.
         */
        void                    setBridge( unique_ptr<IUIAccessibilityBridge> bridge );
        IUIAccessibilityBridge* findBridge() const { return _bridge.get(); }
        /**
         * @brief 스크린 리더에 글 @p text 를 읽어 달라고 알립니다(저장했습니다, 경고 같은 화면에 잠깐 뜨는 알림). 스크린 리더가 붙어 있지 않으면 아무것도 하지 않습니다.
         * @details 알림 서비스(`UINotificationService`)가 보이는 알림마다 부른다. 게임이 화면에 띄우지 않는 소식을 읽히고 싶으면 직접 부른다.
         */
        void announce( string_view text, UIAnnouncePriority priority );
        /**
         * @brief 화면 스택 @p ui 의 지금 접근성 트리를 @p outSnapshot 에 새로 뽑습니다. 읽는 순서는 입력을 받는 화면의 위에서 아래(입력을 막는 화면 아래는 뺀다), 그다음 오버레이입니다.
         * @details 화면마다 위젯 트리 순서입니다(`UIAccessibilityTree::appendTree`). 닫는 중인 화면은 뺀다. 화면 스택을 읽기만 한다 — 브리지가 클라이언트의 물음에 답할 때 부른다.
         */
        static void populateSnapshot( const UISystem& ui, UIAccessibilitySnapshot& outSnapshot );

    private:
        /** @brief 화면 하나의 접근성 노드를 스냅샷에 덧붙입니다(포커스 위젯을 알아 둔다). */
        static void appendScreen( const UISystem& ui, const UIScreen& screen, UIAccessibilitySnapshot& inoutSnapshot );
        /** @brief UI 갱신의 레이아웃 앞 칸입니다 — 스크린 리더가 붙어 있으면 포커스가 옮겨 갔는지 보고 브리지에 알립니다. */
        void updateFocus( float32 deltaSeconds );

    private:
        UISystem*                          _pUI;                      ///< 화면 스택(기동이 찾는다, 그 전 · 종료 뒤에는 nullptr)
        unique_ptr<IUIAccessibilityBridge> _bridge;                   ///< 플랫폼 접근성 브리지입니다(호스트가 끼우며 없으면 nullptr).
        Subscription                       _beforeLayoutSubscription; ///< UI 갱신의 레이아웃 앞 칸(`UISystem::onUpdateBeforeLayout`) 구독
        UIScreenHandle                     _focusScreen;              ///< 브리지에 마지막으로 알린 포커스 위젯의 화면입니다.
        WidgetID                           _focusWidget;              ///< 브리지에 마지막으로 알린 포커스 위젯입니다(없으면 `kInvalidWidgetID`).
    };
} // namespace sw
