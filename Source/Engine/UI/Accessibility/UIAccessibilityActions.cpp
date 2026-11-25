#include "pch.h"

#include "Engine/UI/Accessibility/UIAccessibilityActions.h"

#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/UI/Accessibility/UIAccessibilitySnapshot.h"
#include "Engine/UI/Accessibility/UIAccessibilitySubsystem.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/UIEvents.h"
#include "Engine/UI/WidgetTree/UIWidgetPath.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/SliderWidget.h"

namespace sw
{
    namespace
    {
        struct UIAccessibilityActionsInternal
        {
            /** @brief 사람이 입력할 수 있는 화면만 찾습니다. 스크린 리더가 쥔 요소는 낡을 수 있어(뒤에 모달이 떴다) 시킬 때 다시 본다. */
            static UIScreen* findInteractiveScreen( const UISystem& ui, uint32 screen )
            {
                vector<UIScreen*> listScreen;
                UIAccessibilitySubsystem::collectInteractiveScreens( ui.getScreens(), listScreen );
                for ( UIScreen* pScreen : listScreen )
                {
                    if ( pScreen->getHandle() == screen )
                        return pScreen;
                }
                return nullptr;
            }

            /** @brief (화면, 위젯)을 찾습니다. 없거나 보이지 않거나(읽을 노드가 없다) 꺼졌으면 nullptr 입니다. 그런 위젯은 사람도 누르지 못한다. */
            static Widget* findEnabledWidget( const UISystem& ui, uint32 screen, WidgetID widget )
            {
                UIScreen* pScreen = findInteractiveScreen( ui, screen );
                if ( pScreen == nullptr )
                    return nullptr;
                UIAccessibilitySnapshot snapshot;
                UIAccessibilitySnapshot::appendTree( pScreen->getTree(), screen, kInvalidWidgetID, snapshot );
                if ( snapshot.findNode( screen, widget ).has_value() == false )
                    return nullptr;
                Widget* pWidget = pScreen->getTree().findWidgetByID( widget );
                return pWidget != nullptr && pWidget->isEnabledInHierarchy() ? pWidget : nullptr;
            }
        };
    } // namespace

    bool UIAccessibilityActions::invoke( UISystem& ui, uint32 screen, WidgetID widget )
    {
        Widget* pWidget = UIAccessibilityActionsInternal::findEnabledWidget( ui, screen, widget );
        if ( pWidget == nullptr )
            return false;
        WidgetTree&  tree = ui.findScreen( screen )->getTree();
        UIWidgetPath path{};
        if ( UIWidgetPath::makePathTo( tree, widget, path ) == false )
            return false;
        UIActionEvent event{};
        event._action = hashed_string( UIActionName::kAccept );
        WidgetID handler{ kInvalidWidgetID };
        return UIWidgetPath::routeActionEvent( tree, path, event, handler ).isHandled();
    }

    bool UIAccessibilityActions::focus( UISystem& ui, uint32 screen, WidgetID widget )
    {
        if ( UIAccessibilityActionsInternal::findEnabledWidget( ui, screen, widget ) == nullptr )
            return false;
        return ui.getInputRouter().getFocusManager().setFocus( ui.findScreen( screen )->getTree(), widget );
    }

    bool UIAccessibilityActions::setRangeValue( UISystem& ui, uint32 screen, WidgetID widget, float32 value )
    {
        SliderWidget* pSlider = castTo<SliderWidget>( UIAccessibilityActionsInternal::findEnabledWidget( ui, screen, widget ) );
        if ( pSlider == nullptr )
            return false;
        pSlider->setValueFromUser( value );
        return true;
    }
} // namespace sw
