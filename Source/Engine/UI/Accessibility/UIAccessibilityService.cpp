#include "pch.h"

#include "Engine/UI/Accessibility/UIAccessibilityService.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/UI/Accessibility/UIAccessibilityTree.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"

namespace sw
{
    UIAccessibilityService::UIAccessibilityService()
        : _pUI{ nullptr }
        , _bridge{}
        , _beforeLayoutSubscription{}
        , _focusScreen{ kInvalidUIScreenHandle }
        , _focusWidget{ kInvalidWidgetID }
    {
    }

    UIAccessibilityService::~UIAccessibilityService() = default;

    SubsystemInitResult UIAccessibilityService::initializeSubsystem()
    {
        _pUI                      = &engine::getSubsystem<UISystem>();
        _beforeLayoutSubscription = _pUI->onUpdateBeforeLayout().add( SW_DELEGATE_METHOD( Delegate<void( float32 )>, &UIAccessibilityService::updateFocus, this ) );
        return SubsystemInitResult::Succeeded;
    }

    void UIAccessibilityService::shutdownSubsystem()
    {
        _beforeLayoutSubscription.reset();
        _bridge.reset();
        _focusScreen = kInvalidUIScreenHandle;
        _focusWidget = kInvalidWidgetID;
        _pUI         = nullptr;
    }

    span<const utf8* const> UIAccessibilityService::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { UISystem::kSubsystemName };
        return kArrDependency;
    }

    void UIAccessibilityService::setBridge( unique_ptr<IUIAccessibilityBridge> bridge )
    {
        _bridge      = std::move( bridge );
        _focusScreen = kInvalidUIScreenHandle;
        _focusWidget = kInvalidWidgetID;
    }

    void UIAccessibilityService::announce( string_view text, UIAnnouncePriority priority )
    {
        const bool bListening = _bridge != nullptr && _bridge->isClientActive();
        if ( bListening && text.empty() == false )
            _bridge->announce( text, priority );
    }

    void UIAccessibilityService::appendScreen( const UISystem& ui, const UIScreen& screen, UIAccessibilitySnapshot& inoutSnapshot )
    {
        // 포커스는 한 화면에만 있다. 노드 번호는 스냅샷 전체의 번호라 `appendTree` 가 정한 `_focusedNode` 가 그대로 쓰인다.
        const UIFocusManager& focus         = ui.getInputRouter().getFocusManager();
        const WidgetID        focusedWidget = focus.getFocusedTree() == &screen.getTree() ? focus.getFocusedWidget() : kInvalidWidgetID;
        UIAccessibilityTree::appendTree( screen.getTree(), screen.getHandle(), focusedWidget, inoutSnapshot );
    }

    void UIAccessibilityService::populateSnapshot( const UISystem& ui, UIAccessibilitySnapshot& outSnapshot )
    {
        outSnapshot.clear();
        const vector<unique_ptr<UIScreen>>& listScreen = ui.getScreens();
        // 입력을 받는 화면: 위에서 아래로 읽는다. 입력을 막는 화면(모달) 아래는 읽지 않는다.
        for ( size_t index = listScreen.size(); index > 0; --index )
        {
            const UIScreen& screen = *listScreen[index - 1];
            if ( screen.isClosing() || screen.receivesPointer() == false )
                continue;
            appendScreen( ui, screen, outSnapshot );
            if ( screen.blocksLowerInput() )
                break;
        }
        // 오버레이(알림, 자막): 입력을 받지 않지만 읽을 글이다. 맨 뒤에 둔다.
        for ( size_t index = listScreen.size(); index > 0; --index )
        {
            const UIScreen& screen = *listScreen[index - 1];
            if ( screen.isClosing() == false && screen.receivesPointer() == false )
                appendScreen( ui, screen, outSnapshot );
        }
    }

    void UIAccessibilityService::updateFocus( [[maybe_unused]] float32 deltaSeconds )
    {
        if ( _bridge != nullptr )
            _bridge->onUIUpdated();
        const bool bListening = _bridge != nullptr && _bridge->isClientActive();
        if ( bListening == false )
        {
            // 붙지 않은 동안 바뀐 포커스는 다시 붙을 때 새로 알린다.
            _focusScreen = kInvalidUIScreenHandle;
            _focusWidget = kInvalidWidgetID;
            return;
        }
        const UIFocusManager& focus   = _pUI->getInputRouter().getFocusManager();
        const WidgetTree*     pTree   = focus.getFocusedTree();
        const UIScreen*       pScreen = pTree != nullptr ? pTree->getScreen() : nullptr;
        const UIScreenHandle  screen  = pScreen != nullptr ? pScreen->getHandle() : kInvalidUIScreenHandle;
        const WidgetID        widget  = pScreen != nullptr ? focus.getFocusedWidget() : kInvalidWidgetID;
        if ( screen == _focusScreen && widget == _focusWidget )
            return;
        _focusScreen = screen;
        _focusWidget = widget;
        _bridge->notifyFocusChanged( screen, widget );
    }
} // namespace sw
